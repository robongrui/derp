/* One frame: the scene prepared by prepareFrame drawn in passes (liquid and
   glass reflections, depth for the mist, the main view with mist and overlays,
   bloom), then presented with the HUD and the settings panel. */
#include "internal.h"
#include <cstring>
#include <functional>

extern "C" double SlowMotion_Share(void);

namespace gpu3d {
// The panels laid over the picture, blended inside the present pass (present.frag) before the
// display's encoding, so in HDR they dim and cover exactly as in SDR (blended over an HDR
// swapchain, the F4 panel's dimming came out a grey veil). Each a rect over the drawable in uv
// (left, top, width, height; no width: not shown).
// The in-game settings panel (or an open book's page): the 640x400 image at a whole-pixel scale
// in the middle; beyond it its clamped border texels dim the view.
static float settingsRect[4]={};
static float settingsOrigin[2]={},settingsScale=1;   // in drawable pixels, for the mouse
// F4's FPS readout (statsOverlay) in the top left corner, at a whole-pixel scale.
static float statsRect[4]={};
static GpuTextureRef statsTexture;
#ifdef DOOM_DEVTOOLS
// A local dev tool's overlay (devOverlay) in the top right corner, at a whole-pixel scale.
static float overlayRect[4]={};
static GpuTextureRef overlayTexture;
#endif
// The render stretched over a target of targetWidth x targetHeight: the HUD's
// place, then the upscale mode (settings.upscale) and screen pixels per render pixel,
// the display's encoding (hdr_output.cpp), the panels' rects; sdr: drawn as SDR draws it (screenshots).
// A palette flash laid over the picture (i_sdl_video.c, I_Render3DScreenTint): colour and amount;
// the pickup flash by item over it (I_Render3DPickupTint; a negative colour keeps the palette's).
static float screenTint[4]={},pickupTint[4]={};
// The 2D layer (the HUD image, the status bar and its sides: scene3d/status_bar.cpp) is laid over the
// picture here, at the target's resolution, whatever the render's.
static void present(SDL_GPUCommandBuffer *command,SDL_GPUTexture *target,const FrameView &view,const StatusBarDraw &bar,
                    int targetWidth,int targetHeight,bool sdr=false) {
    SDL_GPUColorTargetInfo info={};
    info.texture=target;info.load_op=SDL_GPU_LOADOP_DONT_CARE;info.store_op=SDL_GPU_STOREOP_STORE;
    SDL_GPURenderPass *pass=SDL_BeginGPURenderPass(command,&info,1,nullptr);
    SDL_BindGPUGraphicsPipeline(pass,sdr?capturePresentPipeline:presentPipeline);
    float stretch[48]={view.uiX/width,view.uiY/height,view.uiWidth/width,view.uiHeight/height,
                      (float)settings.upscale,(float)targetWidth/width,(float)targetHeight/height,
#ifdef DOOM_DEVTOOLS
                      devGlitch()};
#else
                      0};
#endif
    hdrUniform(stretch+8,sdr);
    SDL_GPUTexture *stats=statsTexture?gpu(statsTexture):dummyContact,*overlay=dummyContact;
    std::copy(settingsRect,settingsRect+4,stretch+12);
    if(statsTexture)std::copy(statsRect,statsRect+4,stretch+16);
    std::copy(screenTint,screenTint+4,stretch+24);
    if(settings.pickupFlash&&pickupTint[3]>0) {
        if(pickupTint[0]>=0)std::copy(pickupTint,pickupTint+3,stretch+24);
        stretch[27]=pickupTint[3];
    }
    // Slow motion's colour pop (slow_motion.c), as far in as the clock is; screenshots get it too.
    stretch[28]=(float)SlowMotion_Share();
    std::copy(bar.uniform,bar.uniform+16,stretch+32);   // the status bar apart and the hourglass (scene3d.h's StatusBarDraw)
#ifdef DOOM_DEVTOOLS
    if(overlayTexture) {std::copy(overlayRect,overlayRect+4,stretch+20);overlay=gpu(overlayTexture);}
#endif
    SDL_PushGPUFragmentUniformData(command,0,stretch,sizeof(stretch));
    SDL_PushGPUFragmentUniformData(command,1,&view.camera,sizeof(view.camera));
    SDL_GPUSampler *upscaled=settings.upscale==1?nearestClamp:linearClamp;
    SDL_GPUTexture *flat=bar.flatTexture?gpu(bar.flatTexture):dummyContact,*wide=bar.wideTexture?gpu(bar.wideTexture):dummyContact;
    SDL_GPUTexture *glass=bar.glassTexture?gpu(bar.glassTexture):dummyContact;
    SDL_GPUTextureSamplerBinding samplers[]={{colorTexture,upscaled},{bloomTexture,linearClamp},{hudTexture,nearestClamp},{paletteLUT,nearestClamp},
        {settingsTexture,nearestClamp},{stats,nearestClamp},{overlay,nearestClamp},{flat,nearestRepeat},{wide,nearestClamp},{glass,nearestClamp}};
    SDL_BindGPUFragmentSamplers(pass,0,samplers,10);
    // The bloom and the view, for how much of the bloom and the headroom the view keeps (screen.vert).
    SDL_GPUTextureSamplerBinding vertexSamplers[]={{bloomTexture,linearClamp},{colorTexture,nearestClamp}};
    SDL_BindGPUVertexSamplers(pass,0,vertexSamplers,2);
    SDL_DrawGPUPrimitives(pass,3,1,0,0);
    SDL_EndGPURenderPass(pass);
}
// The picture's own light, for the eye's adaptation: the 3D view shrunk to a thumbnail, read
// back when the GPU has drawn it (a frame or two later; nothing waits for it), and the
// geometric mean of its luminance taken. Measured off the picture, as a camera meters, so it
// counts what is seen: dark textures, the sky through a window, the weapon. One reading is
// in flight at a time.
constexpr int meterWidth=32,meterHeight=18;
static SDL_GPUTexture *meterTexture=nullptr;
static SDL_GPUTransferBuffer *meterBuffer=nullptr;
static SDL_GPUFence *meterFence=nullptr;
static float meterLight=-1,meterBright=-1;
static void meterRead() {
    if(!meterFence||!SDL_QueryGPUFence(device,meterFence))return;
    SDL_ReleaseGPUFence(device,meterFence);meterFence=nullptr;
    const uint8_t *pixels=(const uint8_t*)SDL_MapGPUTransferBuffer(device,meterBuffer,false);
    if(!pixels)return;
    double sum=0;
    // And its bright end: the brightest channel of each texel, the value nine tenths of them lie under.
    int counts[256]={};
    for(int i=0;i<meterWidth*meterHeight;++i) {
        const uint8_t *p=pixels+4*i;   // BGRA
        sum+=std::log((0.114*p[0]+0.587*p[1]+0.299*p[2])/255+0.02);
        ++counts[std::max({p[0],p[1],p[2]})];
    }
    SDL_UnmapGPUTransferBuffer(device,meterBuffer);
    meterLight=(float)std::max(std::exp(sum/(meterWidth*meterHeight))-0.02,0.0);
    int below=0,level=0;
    while(level<255&&below+counts[level]<meterWidth*meterHeight*9/10)below+=counts[level++];
    meterBright=level/255.0f;
}
// Queues a reading of the view just drawn; true when the frame's submit has to keep its fence (meterKeep).
static bool meterQueue(SDL_GPUCommandBuffer *command) {
    if(meterFence||!colorTexture||width<=0||height<=0)return false;
    if(!meterTexture)meterTexture=createTexture(SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM,meterWidth,meterHeight,SDL_GPU_TEXTUREUSAGE_COLOR_TARGET|SDL_GPU_TEXTUREUSAGE_SAMPLER);
    if(!meterBuffer) {
        SDL_GPUTransferBufferCreateInfo info={SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD,(Uint32)(meterWidth*meterHeight*4),0};
        meterBuffer=SDL_CreateGPUTransferBuffer(device,&info);
    }
    if(!meterTexture||!meterBuffer)return false;
    SDL_GPUBlitInfo blit={};
    blit.source.texture=colorTexture;blit.source.w=(Uint32)width;blit.source.h=(Uint32)height;
    blit.destination.texture=meterTexture;blit.destination.w=meterWidth;blit.destination.h=meterHeight;
    blit.load_op=SDL_GPU_LOADOP_DONT_CARE;blit.filter=SDL_GPU_FILTER_LINEAR;
    SDL_BlitGPUTexture(command,&blit);
    SDL_GPUCopyPass *copy=SDL_BeginGPUCopyPass(command);
    SDL_GPUTextureRegion source={};source.texture=meterTexture;source.w=meterWidth;source.h=meterHeight;source.d=1;
    SDL_GPUTextureTransferInfo target={meterBuffer,0,meterWidth,meterHeight};
    SDL_DownloadFromGPUTexture(copy,&source,&target);
    SDL_EndGPUCopyPass(copy);
    return true;
}
// Always drawn as SDR draws it, into BGRA, whatever the swapchain is (HDR's isn't 8-bit).
static void capture(SDL_GPUCommandBuffer *command,const FrameView &view,const StatusBarDraw &bar,int w,int h) {
    SDL_GPUTexture *image=createTexture(SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM,w,h,SDL_GPU_TEXTUREUSAGE_COLOR_TARGET);
    SDL_GPUTransferBufferCreateInfo info={SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD,(Uint32)(w*h*4),0};
    SDL_GPUTransferBuffer *download=image?SDL_CreateGPUTransferBuffer(device,&info):nullptr;
    if(!download) {
        release(image);SDL_SubmitGPUCommandBuffer(command);
        saveScreenshot(nullptr,0,0,0);return;
    }
    present(command,image,view,bar,w,h,true);
    SDL_GPUCopyPass *copy=SDL_BeginGPUCopyPass(command);
    SDL_GPUTextureRegion source={};source.texture=image;source.w=(Uint32)w;source.h=(Uint32)h;source.d=1;
    SDL_GPUTextureTransferInfo target={download,0,(Uint32)w,(Uint32)h};
    SDL_DownloadFromGPUTexture(copy,&source,&target);
    SDL_EndGPUCopyPass(copy);
    SDL_GPUFence *fence=SDL_SubmitGPUCommandBufferAndAcquireFence(command);
    if(fence) {SDL_WaitForGPUFences(device,true,&fence,1);SDL_ReleaseGPUFence(device,fence);}
    std::vector<unsigned> pixels((size_t)w*h);
    memcpy(pixels.data(),SDL_MapGPUTransferBuffer(device,download,false),pixels.size()*4);
    SDL_UnmapGPUTransferBuffer(device,download);
    SDL_ReleaseGPUTransferBuffer(device,download);release(image);
    saveScreenshot(fence?pixels.data():nullptr,w,h,w*4);
}
std::vector<Vertex> panelQuad(Uint32 drawableWidth,Uint32 drawableHeight,float origin[2],float &scale) {
    float dw=(float)drawableWidth,dh=(float)drawableHeight;
    scale=std::max(1.0f,std::floor(std::min(dw/DOOM_SETTINGS_WIDTH,dh/DOOM_SETTINGS_HEIGHT)));
    float pw=DOOM_SETTINGS_WIDTH*scale,ph=DOOM_SETTINGS_HEIGHT*scale;
    origin[0]=std::floor((dw-pw)/2);origin[1]=std::floor((dh-ph)/2);
    std::vector<Vertex> quad;
    screenQuad(quad,-1,1,2,2,-origin[0]/pw,-origin[1]/ph,(dw-origin[0])/pw,(dh-origin[1])/ph);
    return quad;
}
// Draws the settings panel (or an open book's page) into image and places it over the drawable.
static void layoutSettingsPanel(Uint32 drawableWidth,Uint32 drawableHeight,std::vector<unsigned> &image,void (*draw)(unsigned *)) {
    image.assign((size_t)DOOM_SETTINGS_WIDTH*DOOM_SETTINGS_HEIGHT,0);
    draw(image.data());
    float *origin=settingsOrigin,&scale=settingsScale,dw=(float)drawableWidth,dh=(float)drawableHeight;
    panelQuad(drawableWidth,drawableHeight,origin,scale);
    const float rect[4]={origin[0]/dw,origin[1]/dh,DOOM_SETTINGS_WIDTH*scale/dw,DOOM_SETTINGS_HEIGHT*scale/dh};
    std::copy(rect,rect+4,settingsRect);
}
// Mirrors and probes draw the whole map from elsewhere; a depth prepass of
// the opaque batches first, as in the main view, lets each pixel shade once.
static void mirrorPrepass(SDL_GPURenderPass *pass) {
    SDL_BindGPUGraphicsPipeline(pass,reflectPrepassPipeline);
    for(const auto &batch:batches) if(opaqueBatch(batch)) drawVertices(pass,batch.second);
}
// A mirror's render target at the reflection resolution, reallocated when the view changes size.
struct MirrorTarget { SDL_GPUTexture *color=nullptr,*depth=nullptr; int width=0,height=0; };
static MirrorTarget liquidMirror,glassMirror;
static bool glassReflected; // This frame drew a glass reflection.
// Glass probes: per probe a row of six glassProbeSize faces (+x, -x, +y,
// -y, +z, -z; glass.frag reads them the same way), for glassProbeSerial.
static SDL_GPUTexture *probeAtlas,*probeDepth;
static unsigned probeAtlasSerial=~0u;
// The world drawn again at reduced resolution from the mirrored camera,
// clipped to the eye's side of plane (unit normal toward the eye, offset).
// Its sprites: the main view's, or extra in their place.
static void drawMirror(SDL_GPUCommandBuffer *render,const FrameView &view,const Uniforms &mirror,const float plane[4],MirrorTarget &target,
                       const std::map<int,std::vector<Vertex>> *extra=nullptr,const float *box=nullptr) {
    // Retro: an eighth of the screen; a wall mirror, looked into, a quarter (about DOOM's own pixels).
    int divisor=settings.retroReflections?(&target==&glassMirror&&glassMirrorSilvered?4:8):2;
    int rw=std::max(1,(int)(view.worldW/divisor)),rh=std::max(1,(int)(view.worldH/divisor));
    if(!target.color||rw!=target.width||rh!=target.height) {
        release(target.color);release(target.depth);
        target.color=createTexture(SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM,rw,rh,SDL_GPU_TEXTUREUSAGE_COLOR_TARGET|SDL_GPU_TEXTUREUSAGE_SAMPLER);
        target.depth=createTexture(depthFormat,rw,rh,SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET);
        if(!target.color||!target.depth)I_Error((char*)"Could not allocate reflection targets");
        target.width=rw;target.height=rh;
    }
    SDL_GPUColorTargetInfo color={};
    color.texture=target.color;color.load_op=SDL_GPU_LOADOP_CLEAR;color.store_op=SDL_GPU_STOREOP_STORE;
    // The void (drawWorld draws no sky there): black and far (alpha, world.frag's
    // mirror pass), so where the wobble reaches past what the mirror holds, at a
    // pool's rims, the liquid mirrors nothing instead of a dark smear.
    color.clear_color={0,0,0,0};
    SDL_GPUDepthStencilTargetInfo depth={};
    depth.texture=target.depth;depth.clear_depth=1;depth.load_op=SDL_GPU_LOADOP_CLEAR;depth.store_op=SDL_GPU_STOREOP_DONT_CARE;
    depth.stencil_load_op=SDL_GPU_LOADOP_DONT_CARE;depth.stencil_store_op=SDL_GPU_STOREOP_DONT_CARE;
    SDL_GPURenderPass *pass=SDL_BeginGPURenderPass(render,&color,1,&depth);
    // Drawn only inside box (0-1 across the view, where it will be read: glassMirrorBox), four
    // reflection pixels to spare for the filter and the target rounded down from the view (two
    // were one short at a MAP14 pane edge); the clear still covers the whole target.
    if(box) {
        int x0=std::max(0,(int)std::floor(box[0]*rw)-4),y0=std::max(0,(int)std::floor(box[1]*rh)-4);
        int x1=std::min(rw,(int)std::ceil(box[2]*rw)+4),y1=std::min(rh,(int)std::ceil(box[3]*rh)+4);
        SDL_Rect clip={x0,y0,std::max(0,x1-x0),std::max(0,y1-y0)};
        SDL_SetGPUScissor(pass,&clip);
    }
    pushWorldUniforms(render,mirror);
    SDL_PushGPUVertexUniformData(render,1,plane,4*sizeof(float));
    mirrorPrepass(pass);
    drawWorld(render,pass,reflectOpaquePipeline,reflectSkySurfacePipeline,reflectSkyPipeline,reflectWorldPipeline,nullptr,nullptr,nullptr,!extra);
    if(extra) {
        SDL_BindGPUGraphicsPipeline(pass,reflectWorldPipeline);bindWorld(pass,nullptr);
        for(const auto &batch:*extra) if(!batch.second.empty()) {bindSurface(render,pass,batch.first,3);drawVertices(pass,batch.second);}
    }
    SDL_EndGPURenderPass(pass);
}
// Liquid reflections: the camera mirrored at the liquid height, clipped to
// above the surface. Its alpha holds how near each point is to the surface
// along the mirrored ray; the mirror pass is flagged by water = (plane, 0, 0, 1).
static void drawReflection(SDL_GPUCommandBuffer *render,const FrameView &view,Uniforms &camera) {
    camera.water[2]=camera.water[3]=0;
    if(!worldPending||!reflectionActive)return;
    Uniforms mirror=camera;
    mirror.eye[2]=2*reflectionPlane-camera.eye[2];mirror.forward[2]=-camera.forward[2];mirror.up[2]=-camera.up[2];
    mirror.water[0]=reflectionPlane;mirror.water[3]=1;
    mirror.skyLayers[3]=0;   // the plain sky: the mirrored eye under the surface would sink the ridges
    float plane[4]={0,0,1,reflectionPlane};
    drawMirror(render,view,mirror,plane,liquidMirror);
    camera.water[0]=view.worldX;camera.water[1]=view.worldY;camera.water[2]=1/view.worldW;camera.water[3]=1/view.worldH;
}
// Glass probes: each probe in glassCaptures drawn six times into its row of
// the atlas, a 90-degree view per cube face from the probe. Only the map,
// as its bakes light it: no sprites, no dynamic lights (they would freeze
// in the glass), no fog and no eye adaptation. True when it drew any.
static bool captureGlassProbes(SDL_GPUCommandBuffer *render,const Uniforms &camera) {
    if(!worldPending)return false;
    if(probeAtlasSerial!=glassProbeSerial) {
        release(probeAtlas);release(probeDepth);probeAtlasSerial=glassProbeSerial;
        if(!glassProbes.empty()) {
            int w=6*glassProbeSize,h=(int)glassProbes.size()*glassProbeSize;
            probeAtlas=createTexture(SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM,w,h,SDL_GPU_TEXTUREUSAGE_COLOR_TARGET|SDL_GPU_TEXTUREUSAGE_SAMPLER);
            probeDepth=createTexture(depthFormat,w,h,SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET);
            if(!probeAtlas||!probeDepth)I_Error((char*)"Could not allocate the glass probe atlas");
        }
    }
    if(glassCaptures.empty()||!probeAtlas)return false;
    static const float faces[6][2][3]={{{1,0,0},{0,0,1}},{{-1,0,0},{0,0,1}},{{0,1,0},{0,0,1}},{{0,-1,0},{0,0,1}},{{0,0,1},{0,1,0}},{{0,0,-1},{0,1,0}}};
    SDL_GPUColorTargetInfo color={};
    color.texture=probeAtlas;color.load_op=SDL_GPU_LOADOP_LOAD;color.store_op=SDL_GPU_STOREOP_STORE;
    SDL_GPUDepthStencilTargetInfo depth={};
    depth.texture=probeDepth;depth.clear_depth=1;depth.load_op=SDL_GPU_LOADOP_CLEAR;depth.store_op=SDL_GPU_STOREOP_DONT_CARE;
    depth.stencil_load_op=SDL_GPU_LOADOP_DONT_CARE;depth.stencil_store_op=SDL_GPU_STOREOP_DONT_CARE;
    SDL_GPURenderPass *pass=SDL_BeginGPURenderPass(render,&color,1,&depth);
    static const FlashSet noFlashes;static const FogLights noFog;
    const float noClip[4]={0,0,0,-1e9f};
    for(int index:glassCaptures) {
        const GlassProbe &probe=glassProbes[index];
        for(int face=0;face<6;++face) {
            Uniforms view=camera;
            const float *f=faces[face][0],*u=faces[face][1];
            for(int i=0;i<3;++i) {view.eye[i]=probe.at[i];view.forward[i]=f[i];view.up[i]=u[i];}
            view.right[0]=f[1]*u[2]-f[2]*u[1];view.right[1]=f[2]*u[0]-f[0]*u[2];view.right[2]=f[0]*u[1]-f[1]*u[0];
            view.projection[0]=view.projection[1]=1;
            for(float &w:view.water)w=0;
            view.materials[2]=0;view.fx[0]=1;view.fx2[0]=glassProbeSize/200.0f;
            // The plain sky: captured once, a probe would freeze the sky's
            // drifting mist and its ridges where the probe stands.
            view.skyLayers[3]=0;
            setView(pass,(float)(face*glassProbeSize),(float)(index*glassProbeSize),(float)glassProbeSize,(float)glassProbeSize);
            pushWorldUniforms(render,view);
            SDL_PushGPUFragmentUniformData(render,1,&noFlashes,sizeof(noFlashes));
            SDL_PushGPUFragmentUniformData(render,2,&noFog,sizeof(noFog));
            SDL_PushGPUVertexUniformData(render,1,noClip,sizeof(noClip));
            mirrorPrepass(pass);
            drawWorld(render,pass,reflectOpaquePipeline,reflectSkySurfacePipeline,reflectSkyPipeline,reflectWorldPipeline,nullptr,nullptr,nullptr,false);
        }
    }
    SDL_EndGPURenderPass(pass);
    return true;
}
// Glass reflections: the camera and its axes mirrored across the chosen
// window's upright plane (glassMirrorPlane), so each point of the mirrored
// view lands where its reflection shows on the pane and glass.frag reads it
// at its own screen position. water stays 0: nothing in it is a liquid
// mirror. Its sprites are its own (mirrorBatches), the player's included.
// Where on the view (0-1 from its top left, as glass.frag reads the mirror) the panes that show the
// glass mirror lie: the box round the live panes' corners (lightMask bit 1, windows.cpp), projected
// as world.vert does. False, the whole view, when one reaches behind the near plane.
static bool glassMirrorBox(const Uniforms &camera,float box[4]) {
    box[0]=box[1]=1e9f;box[2]=box[3]=-1e9f;
    bool any=false;
    for(const Vertex &v:glassVertices) {
        if(!(v.lightMask[0]&1u))continue;
        float d[3]={v.x-camera.eye[0],v.y-camera.eye[1],v.z+v.lift-camera.eye[2]};
        float z=d[0]*camera.forward[0]+d[1]*camera.forward[1]+d[2]*camera.forward[2];
        if(z<=camera.projection[2])return false;
        float x=(d[0]*camera.right[0]+d[1]*camera.right[1]+d[2]*camera.right[2])*camera.projection[0]/z;
        float y=(d[0]*camera.up[0]+d[1]*camera.up[1]+d[2]*camera.up[2])*camera.projection[1]/z;
        float u=0.5f+0.5f*x,w=0.5f-0.5f*y;
        box[0]=std::min(box[0],u);box[1]=std::min(box[1],w);box[2]=std::max(box[2],u);box[3]=std::max(box[3],w);
        any=true;
    }
    return any;
}
static bool drawGlassReflection(SDL_GPUCommandBuffer *render,const FrameView &view,const Uniforms &camera) {
    if(!worldPending||!glassMirrorActive)return false;
    const float *n=glassMirrorPlane;
    Uniforms mirror=camera;
    float side=n[0]*camera.eye[0]+n[1]*camera.eye[1]-n[3];
    mirror.eye[0]-=2*side*n[0];mirror.eye[1]-=2*side*n[1];
    for(float *axis:{mirror.forward,mirror.right,mirror.up}) {
        float along=n[0]*axis[0]+n[1]*axis[1];
        axis[0]-=2*along*n[0];axis[1]-=2*along*n[1];
    }
    for(float &w:mirror.water)w=0;
    mirror.skyLayers[3]=0;   // the plain sky, as the probes it fades from hold it
    float box[4];bool boxed=glassMirrorBox(camera,box);
#ifdef DOOM_DEVTOOLS
    static const bool unboxed=getenv("DERP_MIRRORFULL")!=nullptr;   // the whole reflection, to compare against
    if(unboxed)boxed=false;
#endif
    drawMirror(render,view,mirror,n,glassMirror,&mirrorBatches,boxed?box:nullptr);
    return true;
}
// Soft mist needs single-sample scene depth; under MSAA a depth-only prepass supplies it.
static void drawMistDepth(SDL_GPUCommandBuffer *render,const FrameView &view,const Uniforms &camera) {
    SDL_GPUDepthStencilTargetInfo depth={};
    depth.texture=sceneDepth;depth.clear_depth=1;depth.load_op=SDL_GPU_LOADOP_CLEAR;depth.store_op=SDL_GPU_STOREOP_STORE;
    depth.stencil_load_op=SDL_GPU_LOADOP_DONT_CARE;depth.stencil_store_op=SDL_GPU_STOREOP_DONT_CARE;
    SDL_GPURenderPass *pass=SDL_BeginGPURenderPass(render,nullptr,0,&depth);
    setView(pass,view.worldX,view.worldY,view.worldW,view.worldH);
    SDL_BindGPUGraphicsPipeline(pass,depthPipeline);
    SDL_PushGPUVertexUniformData(render,0,&camera,sizeof(camera));
    for(const auto &batch:batches) if(!batch.second.empty()) {bindImage(pass,images.at(batch.first).texture,nearestClamp);drawVertices(pass,batch.second);}
    SDL_GPUTextureSamplerBinding solid={solidTexel,nearestClamp};
    SDL_BindGPUFragmentSamplers(pass,0,&solid,1);drawVertices(pass,skyVertices);
    SDL_EndGPURenderPass(pass);
}
// Shadow volumes, as Doom 3 drew its shadows: what stands in a dynamic light keeps that light
// off whatever lies behind it, floor, wall, step or ceiling. In a pass of its own between the
// world and what is drawn over it, on a depth target with a stencil: the scene's depth once
// more (sprites cut out, so the redraw stays behind them), the volumes counted into the
// stencil where their faces lie behind what is drawn, and the opaque world drawn again where
// the count is not zero, with that one light left out (c.shadowSkip). So a shadow is the
// light gone and nothing else: where the light did not reach, nothing changes.
#ifdef DOOM_DEVTOOLS
// DERP_VOLUMEAB: the shadow pass on every other frame only, each frame's GPU time taken by a
// fence as the profiler does, and the two means printed every 200 frames: what the pass
// costs, with whatever drifts from run to run (the GPU's clock, its load) the same for both.
static bool volumeAB=false;static unsigned volumeFrame=0;
// DERP_AB names what DERP_VOLUMEAB leaves out every other frame instead of the shadow pass: bloom (all of
// it), mips (the bloom's mipmaps), blur (its blur passes, the shrink kept), blur2d (the blur in two
// passes: every other frame in the old four instead).
static int abWhat() {
    static const int what=[]{const char *s=getenv("DERP_AB");
        return !s?0:!strcmp(s,"bloom")?1:!strcmp(s,"mips")?2:!strcmp(s,"blur")?3:!strcmp(s,"blur2d")?4:0;}();
    return what;
}
static bool abSkips(int what) {return volumeAB&&(volumeFrame&1)&&abWhat()==what;}
#else
static bool abSkips(int) {return false;}
#endif
static void drawShadowVolumes(SDL_GPUCommandBuffer *render,const FrameView &view,const Uniforms &camera,SDL_GPUColorTargetInfo color) {
    color.load_op=SDL_GPU_LOADOP_LOAD;color.store_op=SDL_GPU_STOREOP_STORE;color.resolve_texture=nullptr;
    SDL_GPUDepthStencilTargetInfo depth={};
    depth.texture=shadowDepth;depth.clear_depth=1;depth.clear_stencil=0;
    depth.load_op=SDL_GPU_LOADOP_CLEAR;depth.store_op=SDL_GPU_STOREOP_DONT_CARE;
    depth.stencil_load_op=SDL_GPU_LOADOP_CLEAR;depth.stencil_store_op=SDL_GPU_STOREOP_DONT_CARE;
    SDL_GPURenderPass *pass=SDL_BeginGPURenderPass(render,&color,1,&depth);
    setView(pass,view.worldX,view.worldY,view.worldW,view.worldH);
    SDL_PushGPUVertexUniformData(render,0,&camera,sizeof(camera));
    SDL_BindGPUGraphicsPipeline(pass,volumeDepthPipeline);
    for(const auto &batch:batches) if(!batch.second.empty()) {bindImage(pass,images.at(batch.first).texture,nearestClamp);drawVertices(pass,batch.second);}
    SDL_GPUTextureSamplerBinding solid={solidTexel,nearestClamp};
    SDL_BindGPUFragmentSamplers(pass,0,&solid,1);drawVertices(pass,skyVertices);
    // A round per kind of light: its volumes counted, what lies in them drawn again, the count taken out
    // again for the next. A dynamic light's is the world without that light, as it is drawn
    // anyway; the baked lamps' and the sun's cannot be left out of the sum they are baked or
    // reckoned into, so there the surface writes the share of its light that is left without
    // them and the picture is multiplied by it (shadowFactorPipeline; world.frag's shadowRemoved).
    SDL_GPUTexture *reflection=camera.water[2]>0?liquidMirror.color:nullptr;
#ifdef DOOM_DEVTOOLS
    // DERP_VOLUMEPART, to time the pass's parts: 1 the depth alone, 2 with the volumes counted, else all.
    static const int part=getenv("DERP_VOLUMEPART")?atoi(getenv("DERP_VOLUMEPART")):3;
#else
    const int part=3;
#endif
    bool anySprite=false;
    for(const auto &batch:batches)anySprite=anySprite||(!batch.second.empty()&&(batch.second.front().mode&MODE_SPRITE));
    for(const ShadowVolumeSet &set:shadowVolumes) {
        if(!set.kind||set.vertices.empty()||part<2)continue;
        SDL_BindGPUGraphicsPipeline(pass,volumePipeline);
        drawVertices(pass,set.vertices);
        Uniforms without=camera;
        bool dynamic=set.kind==1||set.kind==5;
        if(dynamic)without.shadowSkip[0]=set.light[0]+1;
        else without.shadowSkip[1]=(float)set.kind;
#ifdef DOOM_DEVTOOLS
        if(getenv("DERP_VOLUMEMARK")) {without.effects[2]=32;without.shadowSkip[1]=0;}
#endif
        if(part>2) {
            SDL_PushGPUFragmentUniformData(render,0,&without,sizeof(without));
            SDL_BindGPUGraphicsPipeline(pass,dynamic||without.effects[2]==32?shadowedPipeline:shadowFactorPipeline);SDL_SetGPUStencilReference(pass,0);
            drawOpaque(render,pass,dynamic||without.effects[2]==32?shadowedPipeline:shadowFactorPipeline,reflection);
        }
        SDL_BindGPUGraphicsPipeline(pass,volumeUndoPipeline);
        drawVertices(pass,set.vertices);
        // And what stands in them: a round of its own for the sprites, the volumes counted again
        // with their near ends stepped back behind the things that cast them (world.vert, by the
        // vertex shader's c.shadowSkip.w), so no thing lies in its own shadow, and the sprites
        // drawn again where the count is not zero. (Not the sun's: world.frag.)
        bool marked=without.effects[2]==32;
        if(part>2&&anySprite&&!marked&&set.kind!=3&&shadowedSpritePipeline&&shadowSpriteFactorPipeline) {
            Uniforms stepped=camera;stepped.shadowSkip[3]=1;
            SDL_PushGPUVertexUniformData(render,0,&stepped,sizeof(stepped));
            SDL_BindGPUGraphicsPipeline(pass,volumePipeline);
            drawVertices(pass,set.vertices);
            SDL_PushGPUVertexUniformData(render,0,&camera,sizeof(camera));
            without.shadowSkip[3]=1;
            SDL_PushGPUFragmentUniformData(render,0,&without,sizeof(without));
            SDL_BindGPUGraphicsPipeline(pass,dynamic?shadowedSpritePipeline:shadowSpriteFactorPipeline);SDL_SetGPUStencilReference(pass,0);
            bindWorld(pass,reflection);
            for(const auto &batch:batches) if(!batch.second.empty()&&(batch.second.front().mode&MODE_SPRITE)) {bindSurface(render,pass,batch.first,3);drawVertices(pass,batch.second);}
            SDL_PushGPUVertexUniformData(render,0,&stepped,sizeof(stepped));
            SDL_BindGPUGraphicsPipeline(pass,volumeUndoPipeline);
            drawVertices(pass,set.vertices);
            SDL_PushGPUVertexUniformData(render,0,&camera,sizeof(camera));
        }
    }
    float noBlend[44]={};SDL_PushGPUFragmentUniformData(render,3,noBlend,sizeof(noBlend));
    SDL_EndGPURenderPass(pass);
    SDL_PushGPUFragmentUniformData(render,0,&camera,sizeof(camera));
}
static bool shadowVolumesPending() {
    if(!shadowDepth||!shadowedPipeline||!shadowFactorPipeline||!volumeUndoPipeline)return false;
#ifdef DOOM_DEVTOOLS
    static const bool skip=getenv("DERP_VOLUMESKIP")!=nullptr;   // no cast shadows at all, to compare against
    if(skip||abSkips(0))return false;
#endif
    for(const ShadowVolumeSet &set:shadowVolumes)if(set.kind&&!set.vertices.empty())return true;
    return false;
}
// The world, its shadows, decals, particles and heat haze. between: ends the pass after the
// world for the shadow volumes and begins it again (the main view's). cut: while the frame is
// timed piece by piece, ends each piece but the last (the transparent run, which the main view
// ends); render and pass then change, which is why they are references.
static void drawScene(SDL_GPUCommandBuffer *&render,SDL_GPURenderPass *&pass,const FrameView &view,const Uniforms &camera,
                      const std::function<void()> &between=nullptr,const PieceCut *cut=nullptr) {
    setView(pass,view.worldX,view.worldY,view.worldW,view.worldH);
    // Only fully covered textures use early depth writes. Cutouts and
    // sprites retain discard-aware shading after the sky background.
    // The whole map is drawn each frame in texture order, so without a
    // depth prepass hidden walls and floors would run the full lighting
    // before nearer ones cover them; with it each pixel shades once.
    if(!skyOnlyView()) {
        SDL_BindGPUGraphicsPipeline(pass,prepassPipeline);
        for(const auto &batch:batches) if(opaqueBatch(batch)) drawVertices(pass,batch.second);
    }
    if(cut)(*cut)(I_RENDER3D_PIECE_PREPASS,render,pass);
    bool cached=skyCacheOn(camera);
    drawWorld(render,pass,opaquePipeline,cached?skyCachedSurfacePipeline:skySurfacePipeline,cached?skyCachedPipeline:skyPipeline,worldPipeline,camera.water[2]>0?liquidMirror.color:nullptr,
              coveragePipeline,cutoutPipeline,true,cut);
    // The shadow volumes end the cutouts' piece and their own themselves (between).
    if(between&&shadowVolumesPending()) {between();setView(pass,view.worldX,view.worldY,view.worldW,view.worldH);}
    else if(cut)(*cut)(I_RENDER3D_PIECE_CUTOUTS,render,pass);
    // The completed world depth hides shadows behind walls and enemies.
    SDL_BindGPUGraphicsPipeline(pass,shadowPipeline);
    for(const auto &batch:shadowBatches) {if(batch.second.empty())continue;bindImage(pass,images.at(-1-batch.first).texture,linearClamp);drawVertices(pass,batch.second);}
    SDL_BindGPUGraphicsPipeline(pass,decalPipeline);
    for(const auto &batch:decalBatches) {if(batch.second.empty())continue;bindImage(pass,images.at(-1-batch.first).texture,linearClamp);drawVertices(pass,batch.second);}
    SDL_BindGPUGraphicsPipeline(pass,particlePipeline);drawVertices(pass,particleVertices);drawVertices(pass,glowVertices);
    SDL_BindGPUGraphicsPipeline(pass,crestPipeline);drawVertices(pass,crestVertices);
    if(!lampSheetVertices.empty()) {SDL_BindGPUGraphicsPipeline(pass,lampSheetPipeline);drawVertices(pass,lampSheetVertices);}   // the rain round the lamps, added
    // Window glass over everything behind it, shards included; it writes no depth.
    if(!glassVertices.empty()) {
        SDL_BindGPUGraphicsPipeline(pass,glassPipeline);
        SDL_GPUTextureSamplerBinding images[]={{gpu(skyImage()),linearRepeat},{paletteTexture,nearestClamp},
                                              {glassReflected?glassMirror.color:paletteTexture,nearestClamp},
                                              {probeAtlas?probeAtlas:paletteTexture,nearestClamp}};
        SDL_BindGPUFragmentSamplers(pass,0,images,4);bindLightBlockers(pass);
        // Where the mirrored view sits on screen (z is 0 without a
        // reflection), the probe face size and the mirror's fade-in, then each
        // probe's point and box.
        static std::vector<std::array<float,4>> mirror(2+3*maxGlassProbes);
        mirror[0]={view.worldX,view.worldY,glassReflected?1/view.worldW:0,1/view.worldH};
        mirror[1]={(float)glassProbeSize,glassMirrorFade,0,0};
        for(size_t i=0;i<glassProbes.size();++i) {
            const GlassProbe &p=glassProbes[i];
            mirror[2+3*i]={p.at[0],p.at[1],p.at[2],0};mirror[3+3*i]={p.low[0],p.low[1],p.low[2],0};mirror[4+3*i]={p.high[0],p.high[1],p.high[2],0};
        }
        SDL_PushGPUFragmentUniformData(render,3,mirror.data(),(Uint32)(mirror.size()*sizeof(mirror[0])));
        drawVertices(pass,glassVertices);
    }
    // Hot air marks the color target's alpha for the shimmer in present.
    if(!heatVertices.empty()) {SDL_BindGPUGraphicsPipeline(pass,heatPipeline);drawVertices(pass,heatVertices);}
}
// Soft mist and sunbeams over the finished scene, against its depth.
static void drawMistPass(SDL_GPUCommandBuffer *render,SDL_GPURenderPass *pass,const Uniforms &camera,bool msaa) {
    SDL_BindGPUGraphicsPipeline(pass,mistPipeline);
    SDL_PushGPUVertexUniformData(render,0,&camera,sizeof(camera));
    SDL_PushGPUFragmentUniformData(render,0,&camera,sizeof(camera));
    SDL_GPUTextureSamplerBinding mist[]={{gpu(cloudTexture),linearRepeat},{msaa?sceneDepth:depthTexture,nearestClamp}};
    SDL_BindGPUFragmentSamplers(pass,0,mist,2);
    drawVertices(pass,mistVertices);
    if(!shaftVertices.empty()) {
        SDL_BindGPUGraphicsPipeline(pass,shaftPipeline);SDL_BindGPUFragmentSamplers(pass,0,mist,2);
        drawVertices(pass,shaftVertices);
    }
}
// The scene with mist and the overlays on top, into colorTexture.
// Timed piece by piece (piecesOn), the pass is ended at the end of each piece and begun again on
// the next piece's command buffer, loading what the last left: every pass but the last stores
// colour and depth (under MSAA the multisampled colour, as the shadow volumes keep it), and only
// the last resolves, as the one pass would. Each new buffer gets the world's uniforms again.
static void drawMainView(SDL_GPUCommandBuffer *&render,const FrameView &view,const Uniforms &camera,bool drawMist,bool msaa,
                  const std::vector<SpriteDraw> &weapons,const std::vector<Vertex> &cross) {
    SDL_GPUColorTargetInfo color={};
    color.texture=msaa?multisampleTexture:colorTexture;color.clear_color={0.025f,0.025f,0.03f,1};
    color.load_op=SDL_GPU_LOADOP_CLEAR;
    color.store_op=msaa&&!drawMist?SDL_GPU_STOREOP_RESOLVE:SDL_GPU_STOREOP_STORE;
    color.resolve_texture=msaa&&!drawMist?colorTexture:nullptr;
    SDL_GPUDepthStencilTargetInfo depth={};
    depth.texture=depthTexture;depth.clear_depth=1;depth.load_op=SDL_GPU_LOADOP_CLEAR;
    depth.store_op=drawMist&&!msaa?SDL_GPU_STOREOP_STORE:SDL_GPU_STOREOP_DONT_CARE;
    depth.stencil_load_op=SDL_GPU_LOADOP_DONT_CARE;depth.stencil_store_op=SDL_GPU_STOREOP_DONT_CARE;
    // With shadow volumes the pass is ended after the world and begun again: both targets are kept for that.
    bool volumes=worldPending&&shadowVolumesPending();
    const bool pieced=piecesOn();
    const SDL_GPUStoreOp colorStore=color.store_op,depthStore=depth.store_op;SDL_GPUTexture *resolve=color.resolve_texture;
    if(volumes||pieced) {color.store_op=SDL_GPU_STOREOP_STORE;color.resolve_texture=nullptr;depth.store_op=SDL_GPU_STOREOP_STORE;}
    SDL_GPURenderPass *pass=SDL_BeginGPURenderPass(render,&color,1,&depth);
    pushWorldUniforms(render,camera);
    auto between=[&] {
        SDL_EndGPURenderPass(pass);
        if(pieced) {pieceDone(render,I_RENDER3D_PIECE_CUTOUTS);pushWorldUniforms(render,camera);}
        drawShadowVolumes(render,view,camera,color);
        if(pieced) {pieceDone(render,I_RENDER3D_PIECE_VOLUMES);pushWorldUniforms(render,camera);}
        color.load_op=SDL_GPU_LOADOP_LOAD;depth.load_op=SDL_GPU_LOADOP_LOAD;
        if(!pieced) {color.store_op=colorStore;color.resolve_texture=resolve;depth.store_op=depthStore;}
        pass=SDL_BeginGPURenderPass(render,&color,1,&depth);
    };
    auto reopen=[&](int piece,SDL_GPUCommandBuffer *&command,SDL_GPURenderPass *&current) {
        SDL_EndGPURenderPass(pass);
        pieceDone(render,piece);
        color.load_op=SDL_GPU_LOADOP_LOAD;depth.load_op=SDL_GPU_LOADOP_LOAD;
        pass=SDL_BeginGPURenderPass(render,&color,1,&depth);
        pushWorldUniforms(render,camera);
        setView(pass,view.worldX,view.worldY,view.worldW,view.worldH);
        command=render;current=pass;
    };
    PieceCut cut;   // (left empty outside piece timing: nothing to build)
    if(pieced)cut=reopen;
    if(worldPending)drawScene(render,pass,view,camera,between,pieced?&cut:nullptr);
    if(drawMist) {
        SDL_EndGPURenderPass(pass);
        if(pieced&&worldPending) {pieceDone(render,I_RENDER3D_PIECE_TRANSPARENT);}
        color.load_op=SDL_GPU_LOADOP_LOAD;
        color.store_op=msaa&&!pieced?SDL_GPU_STOREOP_RESOLVE:SDL_GPU_STOREOP_STORE;
        color.resolve_texture=msaa&&!pieced?colorTexture:nullptr;
        pass=SDL_BeginGPURenderPass(render,&color,1,nullptr);
        setView(pass,view.worldX,view.worldY,view.worldW,view.worldH);
        drawMistPass(render,pass,camera,msaa);
        pushWorldUniforms(render,camera);
        if(pieced) {
            // The overlays' pass, without depth as after the mist, is the last: it resolves.
            SDL_EndGPURenderPass(pass);
            pieceDone(render,I_RENDER3D_PIECE_MIST);
            color.store_op=msaa?SDL_GPU_STOREOP_RESOLVE:SDL_GPU_STOREOP_STORE;color.resolve_texture=msaa?colorTexture:nullptr;
            pass=SDL_BeginGPURenderPass(render,&color,1,nullptr);
            pushWorldUniforms(render,camera);
        }
    }
    else if(pieced) {
        // The overlays' pass, with depth, is the last: it stores and resolves as the one pass would.
        SDL_EndGPURenderPass(pass);
        if(worldPending)pieceDone(render,I_RENDER3D_PIECE_TRANSPARENT);
        color.load_op=SDL_GPU_LOADOP_LOAD;depth.load_op=SDL_GPU_LOADOP_LOAD;
        color.store_op=colorStore;color.resolve_texture=resolve;depth.store_op=depthStore;
        pass=SDL_BeginGPURenderPass(render,&color,1,&depth);
        pushWorldUniforms(render,camera);
    }
    drawOverlays(render,pass,view,weapons,cross,!drawMist);
    SDL_EndGPURenderPass(pass);
    pieceDone(render,I_RENDER3D_PIECE_OVERLAYS);
}
// Bloom: emissive texels drawn at half resolution, then blurred.
static void drawBloom(SDL_GPUCommandBuffer *render,const FrameView &view,const Uniforms &camera,const std::vector<SpriteDraw> &weapons) {
    int bw=std::max(1,width/2),bh=std::max(1,height/2);
    float sx=(float)bw/width,sy=(float)bh/height;
    SDL_GPUColorTargetInfo glow={};
    glow.texture=emissionTexture;glow.load_op=SDL_GPU_LOADOP_CLEAR;glow.store_op=SDL_GPU_STOREOP_STORE;glow.clear_color={0,0,0,1};
    SDL_GPUDepthStencilTargetInfo glowDepth={};
    glowDepth.texture=emissionDepth;glowDepth.clear_depth=1;glowDepth.load_op=SDL_GPU_LOADOP_CLEAR;
    glowDepth.store_op=SDL_GPU_STOREOP_DONT_CARE;glowDepth.stencil_load_op=SDL_GPU_LOADOP_DONT_CARE;glowDepth.stencil_store_op=SDL_GPU_STOREOP_DONT_CARE;
    SDL_GPURenderPass *glowPass=SDL_BeginGPURenderPass(render,&glow,1,&glowDepth);
    SDL_Rect area={0,0,bw,bh};
    setView(glowPass,view.worldX*sx,view.worldY*sy,view.worldW*sx,view.worldH*sy,&area);
    SDL_BindGPUGraphicsPipeline(glowPass,emissionPipeline);
    SDL_PushGPUVertexUniformData(render,0,&camera,sizeof(camera));
    SDL_GPUTextureSamplerBinding palette={paletteTexture,nearestClamp};
    SDL_BindGPUFragmentSamplers(glowPass,1,&palette,1);
    bindEmissionEdges(render,glowPass,camera);
    for(const auto &batch:batches) if(!batch.second.empty()) {bindSurface(render,glowPass,batch.first,0,false);drawVertices(glowPass,batch.second);}
    // The sky hides glow behind it: a covered texel with no emission.
    SDL_GPUTextureSamplerBinding solid={solidTexel,nearestClamp};
    SDL_BindGPUFragmentSamplers(glowPass,0,&solid,1);SDL_BindGPUFragmentSamplers(glowPass,2,&solid,1);
    float noBlend[4]={};SDL_PushGPUFragmentUniformData(render,0,noBlend,sizeof(noBlend));
    drawVertices(glowPass,skyVertices);
    // Glowing particles (embers) glow where the world leaves them seen.
    // (the weapon below is drawn with the emission pipeline again).
    // Their shader wants the camera in slot 0, where this pass keeps the emission shader's blend: left at that,
    // it read the haze from whatever lay past the blend (some runs a negative glow, a dark blue square round
    // every ember). No haze here: a far ember's glow stays an ember's.
    if(!glowVertices.empty()) {
        Uniforms glowCamera=camera;glowCamera.haze[3]=0;glowCamera.materials[2]=0;glowCamera.fx[0]=1;   // (nor fog, nor exposure: flat.frag)
        SDL_BindGPUGraphicsPipeline(glowPass,glowParticlePipeline);
        SDL_PushGPUFragmentUniformData(render,0,&glowCamera,sizeof(glowCamera));
        drawVertices(glowPass,glowVertices);
        SDL_PushGPUFragmentUniformData(render,0,noBlend,sizeof(noBlend));
        SDL_BindGPUGraphicsPipeline(glowPass,emissionPipeline);
    }
    // The weapon hides glow behind it too; it is drawn over the view in
    // the HUD's frame, so the glow from lamps behind it would otherwise
    // show through it.
    if(!weapons.empty()) {
        setView(glowPass,view.uiX*sx,view.uiY*sy,view.uiWidth*sx,view.uiHeight*sy,&area);
        for(const auto &draw:weapons) {
            SDL_GPUTextureSamplerBinding image[]={{gpu(draw.image->texture),nearestClamp},{paletteTexture,nearestClamp},{gpu(draw.image->texture),nearestClamp}};
            SDL_BindGPUFragmentSamplers(glowPass,0,image,3);drawVertices(glowPass,draw.vertices);
        }
    }
    SDL_EndGPURenderPass(glowPass);
    // The emission shrunk to the bloom's size (every texel of it counted: blur.frag's box), then blurred
    // there twice, across and down in one pass each, the taps a bloom texel apart (internal.h's bloomSize).
    // (It was across and down in passes of their own, four in all: on a Steam Deck each pass cost about
    // 0.1 ms whatever it drew, and the blur 0.44 ms of the bloom's 0.8.)
    int gw,gh;bloomSize(width,height,&gw,&gh);
    float across=1.0f/((float)BLOOM_ROWS*bw/bh),down=1.0f/BLOOM_ROWS;
#ifdef DOOM_DEVTOOLS
    const bool separate=abSkips(4);   // DERP_AB=blur2d: every other frame the four passes, to time the two against them
#else
    const bool separate=false;
#endif
    for(int step=0;step<(abSkips(3)?1:separate?5:3);++step) {
        bool toScratch=step%2==1;   // shrink -> bloom, then blurred -> scratch, blurred -> bloom
        SDL_GPUColorTargetInfo blur={};
        blur.texture=toScratch?bloomScratch:bloomTexture;blur.load_op=SDL_GPU_LOADOP_DONT_CARE;blur.store_op=SDL_GPU_STOREOP_STORE;
        SDL_GPURenderPass *blurPass=SDL_BeginGPURenderPass(render,&blur,1,nullptr);
        SDL_BindGPUGraphicsPipeline(blurPass,blurPipeline);
        SDL_GPUTextureSamplerBinding source={step==0?emissionTexture:toScratch?bloomTexture:bloomScratch,linearClamp};
        SDL_BindGPUFragmentSamplers(blurPass,0,&source,1);
        float direction[4]={step&&(toScratch||!separate)?across:0,step&&(!toScratch||!separate)?down:0,step?0:1.0f/gw,step?0:1.0f/gh};
        SDL_PushGPUFragmentUniformData(render,0,direction,sizeof(direction));
        SDL_DrawGPUPrimitives(blurPass,3,1,0,0);
        SDL_EndGPURenderPass(blurPass);
    }
    if(!abSkips(2))SDL_GenerateMipmapsForGPUTexture(render,bloomTexture);
}
// Profiling: submits the scene's own command buffer and times it.
static void submitProfiled(SDL_GPUCommandBuffer *render,uint64_t frameStart) {
    flushUploads();
    uint64_t submit=SDL_GetTicksNS();
    SDL_GPUFence *fence=SDL_SubmitGPUCommandBufferAndAcquireFence(render);
    if(!fence) I_Error((char*)"GPU rendering failed: %s",SDL_GetError());
    SDL_WaitForGPUFences(device,true,&fence,1);SDL_ReleaseGPUFence(device,fence);
    uint64_t done=SDL_GetTicksNS();
    profile.frame.cpuMs=(submit-frameStart)/1e6;profile.frame.gpuMs=(done-submit)/1e6;
}
void releaseReflection() {
    for(MirrorTarget *t:{&liquidMirror,&glassMirror}) {release(t->color);release(t->depth);t->width=t->height=0;}
    release(probeAtlas);release(probeDepth);probeAtlasSerial=~0u;
}
} // namespace gpu3d

using namespace gpu3d;
float gpuFrameLight() {return meterLight;}
float gpuFrameBright() {return meterBright;}
// The render size for a drawable: a percentage of it, or (scale above 100) a
// fixed height no taller than the drawable, the width keeping its shape.
static void renderSize(int drawableWidth,int drawableHeight,int &w,int &h) {
    if(settings.scale>100&&settings.scale<drawableHeight) {
        h=settings.scale;w=std::max(1,(int)std::lround((double)drawableWidth*h/drawableHeight));
    }
    else {
        int percent=std::min(settings.scale,100);
        w=std::max(1,drawableWidth*percent/100);h=std::max(1,drawableHeight*percent/100);
    }
}
void I_Render3DScreenTint(const float tint[4]) {std::copy(tint,tint+4,screenTint);}
void I_Render3DPickupTint(const float tint[4]) {std::copy(tint,tint+4,pickupTint);}
void I_Render3DPresent(const unsigned *pixels,const unsigned *palette) {
    static const bool tracing=startFrameTrace();
    uint64_t renderStart=tracing?SDL_GetTicksNS():0;
    applySampleCount();
    // A new level's build and bakes run first, with the loading screen up.
    bool loading=loadPending();
    if(loading) {
        int w=width,h=height;
        if(!colorTexture) {
            int pw=1,ph=1;SDL_GetWindowSizeInPixels(gameWindow,&pw,&ph);
            renderSize(pw,ph,w,h);
        }
        runLoading(w,h,showLoading);
        prebuildPaletteLUTs();
        // This frame (a level's first is built whole: geometry.cpp) sizes the vertex upload.
        presizeFrameUploads();
    }
    SDL_GPUCommandBuffer *command=SDL_AcquireGPUCommandBuffer(device);
    if(!command) {
        if(tracing)skipFrameTrace();
        fprintf(stderr,"GPU command buffer: %s\n",SDL_GetError());return;
    }
    SDL_GPUTexture *swapchain=nullptr;Uint32 drawableWidth=0,drawableHeight=0;
    uint64_t waitStart=SDL_GetTicksNS();   // the frame's moments, for the FPS readout (statsFrame)
    uint64_t paced=waitStart;
    SDL_WindowFlags windowFlags=SDL_GetWindowFlags(gameWindow);
    // A hidden window (render checks) has no swapchain: the frame is drawn for capture() alone.
    if(windowFlags&SDL_WINDOW_HIDDEN) {
        int pw=1,ph=1;SDL_GetWindowSizeInPixels(gameWindow,&pw,&ph);drawableWidth=(Uint32)pw;drawableHeight=(Uint32)ph;
    }
    else {
        double paceWait=paceStart();
        presentationWait();   // the present mode and the frame limit (presentation.cpp)
        if(tracing)paced=SDL_GetTicksNS();
        if(!SDL_WaitAndAcquireGPUSwapchainTexture(command,gameWindow,&swapchain,&drawableWidth,&drawableHeight)||!swapchain) {
            if(tracing)skipFrameTrace();
            flushUploads();SDL_SubmitGPUCommandBuffer(command);return;
        }
        paceAcquired(paceWait);
    }
    uint64_t frameStart=SDL_GetTicksNS();
    int w,h;renderSize((int)drawableWidth,(int)drawableHeight,w,h);
    if(w!=width||h!=height||!colorTexture) {
        width=w;height=h;allocateTargets(w,h);
        fprintf(stderr,"GPU resolution: %dx%d (display %ux%u).\n",w,h,drawableWidth,drawableHeight);
    }
    // Follows the live palette in every mode: brick tone (world/brick_wear.glsl) reads it too, and the
    // first frame comes before Doom sets any palette (all black), which left painted bricks black until
    // a palette mode or retro reflections rebuilt it. Same palette: updatePaletteLUT returns at once.
    updatePaletteLUT(palette);
    bool hudChanged=false;
    const std::vector<unsigned> &overlay=hudPixels(pixels,hudChanged);
    setScreenSize((int)drawableWidth,(int)drawableHeight);   // the 2D layer's own resolution (status_bar.cpp)
    FrameView view=prepareFrame(w,h);Uniforms &camera=view.camera;
    camera.shadowSkip[2]=sceneFormat!=SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM?1.0f:0.0f;   // HDR in the scene: past white kept (toneCurve)
    uint64_t prepared=SDL_GetTicksNS();
    bool drawMist=!mistVertices.empty()||!shaftVertices.empty();
    bool msaa=sampleCount!=SDL_GPU_SAMPLECOUNT_1;
    // Transient quads join the frame upload; textures created while drawing
    // go through the upload command buffer, which is submitted first.
    std::vector<SpriteDraw> weapons;if(worldPending)weapons=weaponDraws(camera);
    StatusBarDraw bar=statusBarDraw(view,palette);
    std::vector<Vertex> cross=crosshairVertices(view.worldW,view.worldH);
    std::vector<const std::vector<Vertex>*> transient={&cross};
    for(const auto &draw:weapons)transient.push_back(&draw.vertices);
    std::vector<unsigned> settingsImage;std::fill(settingsRect,settingsRect+4,0.0f);
    if(I_Render3DSettingsOpen())layoutSettingsPanel(drawableWidth,drawableHeight,settingsImage,I_Render3DSettingsDraw);
    else if(I_Render3DPageOpen())layoutSettingsPanel(drawableWidth,drawableHeight,settingsImage,I_Render3DPageDraw);
    int statsW=0,statsH=0;
    if((statsTexture=statsOverlay(statsW,statsH,w,h,(int)drawableWidth,(int)drawableHeight))) {
        float dw=(float)drawableWidth,dh=(float)drawableHeight,scale=(float)std::max(1,(int)drawableHeight/540),margin=8*scale;
        const float rect[4]={margin/dw,margin/dh,statsW*scale/dw,statsH*scale/dh};
        std::copy(rect,rect+4,statsRect);
    }
#ifdef DOOM_DEVTOOLS
    int overlayW=0,overlayH=0;
    if((overlayTexture=devOverlay(overlayW,overlayH))) {
        float dw=(float)drawableWidth,dh=(float)drawableHeight,scale=(float)std::max(1,(int)drawableHeight/720),margin=12*scale;
        const float rect[4]={(dw-margin-overlayW*scale)/dw,margin/dh,overlayW*scale/dw,overlayH*scale/dh};
        std::copy(rect,rect+4,overlayRect);
    }
#endif
    // Profiling: the scene goes in a command buffer of its own, timed by a
    // fence. The swapchain's may wait for the compositor, which in a window
    // keeps the display's rate even without vsync.
#ifdef DOOM_DEVTOOLS
    static const bool ab=getenv("DERP_VOLUMEAB")!=nullptr;
    volumeAB=ab;++volumeFrame;
#else
    const bool ab=false;
#endif
    // Piece timing (gpu_pieces.cpp): the scene's own command buffers too, one per piece in
    // the same order, each waited for; a piece with nothing to draw is not cut.
    const bool pieced=piecesBegin();
    SDL_GPUCommandBuffer *render=pieced||profile.enabled||ab?SDL_AcquireGPUCommandBuffer(device):command;
    if(!render) I_Error((char*)"GPU command buffer: %s",SDL_GetError());
    uploadFrame(render,transient,sectorInfo(),palette,overlay,hudChanged,settingsImage);
    pieceDone(render,I_RENDER3D_PIECE_UPLOADS);
    drawReflection(render,view,camera);
    if(camera.water[2]>0)pieceDone(render,I_RENDER3D_PIECE_LIQUID_MIRROR);
    if(captureGlassProbes(render,camera))pieceDone(render,I_RENDER3D_PIECE_GLASS_PROBES);
    glassReflected=drawGlassReflection(render,view,camera);
    if(glassReflected)pieceDone(render,I_RENDER3D_PIECE_GLASS_MIRROR);
    if(drawSkyCache(render,camera))pieceDone(render,I_RENDER3D_PIECE_SKY_CACHE);
    if(drawMist&&msaa) {drawMistDepth(render,view,camera);pieceDone(render,I_RENDER3D_PIECE_MIST_DEPTH);}
    drawMainView(render,view,camera,drawMist,msaa,weapons,cross);
    if(camera.materials[1]>0&&!abSkips(1)) {drawBloom(render,view,camera,weapons);pieceDone(render,I_RENDER3D_PIECE_BLOOM);}
    if(pieced)piecesEnd(render,frameStart);
    else if(render!=command)submitProfiled(render,frameStart);
#ifdef DOOM_DEVTOOLS
    if(ab&&volumeFrame>120) {
        static double sum[2]={};static int count[2]={};static int with=0;
        int odd=volumeFrame&1;
        sum[odd]+=profile.frame.gpuMs;++count[odd];
        for(const ShadowVolumeSet &set:shadowVolumes)if(!odd&&set.kind&&!set.vertices.empty()) {++with;break;}
        if((count[0]+count[1])%200==0)
            fprintf(stderr,"volume AB: with the pass %.2f ms, without %.2f ms, over %d and %d frames; %d of the first had volumes\n",
                sum[0]/std::max(count[0],1),sum[1]/std::max(count[1],1),count[0],count[1],with);
    }
#endif
    if(swapchain)present(command,swapchain,view,bar,(int)drawableWidth,(int)drawableHeight);
    flushUploads();
    bool capturing=screenshotPending||renderCheckDue();
    meterRead();
    bool metered=!capturing&&meterQueue(command);
    uint64_t submitStart=tracing?SDL_GetTicksNS():0;
    if(capturing) {screenshotPending=false;capture(command,view,bar,(int)drawableWidth,(int)drawableHeight);}
    else {
        double submitStart=paceStart();
        // Timed piece by piece, the present is the last piece: the swapchain's buffer is waited for
        // as well (on the meter's fence when it has one, which meterRead then finds signalled).
        bool timePresent=pieced&&swapchain;
        uint64_t presentSubmit=timePresent?SDL_GetTicksNS():0;
        SDL_GPUFence *presentFence=nullptr;
        if(metered) {
            meterFence=SDL_SubmitGPUCommandBufferAndAcquireFence(command);
            if(!meterFence) I_Error((char*)"GPU rendering failed: %s",SDL_GetError());
            presentFence=meterFence;
        } else if(timePresent) {
            presentFence=SDL_SubmitGPUCommandBufferAndAcquireFence(command);
            if(!presentFence) I_Error((char*)"GPU rendering failed: %s",SDL_GetError());
        } else if(!swapchain&&(SDL_GetWindowFlags(gameWindow)&SDL_WINDOW_HIDDEN)) {
            // Hidden (render checks): no swapchain to wait for, so nothing held the game back from
            // the GPU; frames piled up in flight until the device was lost a few hundred frames in.
            // Each one waits for its own work, as a windowed frame waits for its swapchain image
            // (waiting for the whole device counted the light worker's re-bakes on the graphics
            // card as frame time).
            SDL_GPUFence *own=SDL_SubmitGPUCommandBufferAndAcquireFence(command);
            if(!own) I_Error((char*)"GPU rendering failed: %s",SDL_GetError());
            SDL_WaitForGPUFences(device,true,&own,1);
            SDL_ReleaseGPUFence(device,own);
        } else if(!SDL_SubmitGPUCommandBuffer(command)) I_Error((char*)"GPU rendering failed: %s",SDL_GetError());
        if(swapchain)paceSubmitted(submitStart);
        if(timePresent) {
            SDL_WaitForGPUFences(device,true,&presentFence,1);
            piecePresented((SDL_GetTicksNS()-presentSubmit)/1e6);
            if(presentFence!=meterFence)SDL_ReleaseGPUFence(device,presentFence);
        }
    }
    uint64_t submitted=SDL_GetTicksNS();
    if(tracing)traceFrame({renderStart,waitStart,paced,frameStart,prepared,submitStart,submitted},loading,capturing,
        !(windowFlags&SDL_WINDOW_HIDDEN),windowFlags&SDL_WINDOW_INPUT_FOCUS);
    statsFrame(waitStart,frameStart,prepared,submitted);
    char renderer[64];snprintf(renderer,sizeof(renderer),"%s 3D",SDL_GetGPUDeviceDriver(device));
    if(renderer[0])renderer[0]=(char)toupper((unsigned char)renderer[0]);
    updateTitle(w,h,renderer);
}

void I_Render3DSettingsMouse(float x,float y,int button) {
    float density=gameWindow?SDL_GetWindowPixelDensity(gameWindow):1;
    if(density<=0)density=1;
    float px=(x*density-settingsOrigin[0])/settingsScale,py=(y*density-settingsOrigin[1])/settingsScale;
    I_Render3DSettingsClick((int)std::floor(px),(int)std::floor(py),button);
}
