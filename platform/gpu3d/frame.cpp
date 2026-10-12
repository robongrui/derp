/* One frame: the scene prepared by prepareFrame drawn in passes (liquid
   reflection, depth for the mist, the main view with mist and overlays,
   bloom), then presented with the HUD and the settings panel. */
#include "internal.h"

namespace gpu3d {
static SDL_GPUTexture *reflectionTexture,*reflectionDepth;
static int reflectionWidth,reflectionHeight;
// The in-game settings panel: a quad over the whole drawable whose texture
// coordinates place the 640x400 image at a whole-pixel scale in the middle;
// beyond it the clamped border texels dim the view.
static std::vector<Vertex> settingsQuad;
static float settingsOrigin[2]={},settingsScale=1;
static void present(SDL_GPUCommandBuffer *command,SDL_GPUTexture *target,const FrameView &view) {
    SDL_GPUColorTargetInfo info={};
    info.texture=target;info.load_op=SDL_GPU_LOADOP_DONT_CARE;info.store_op=SDL_GPU_STOREOP_STORE;
    SDL_GPURenderPass *pass=SDL_BeginGPURenderPass(command,&info,1,nullptr);
    SDL_BindGPUGraphicsPipeline(pass,presentPipeline);
    float hudRect[4]={view.uiX/width,view.uiY/height,view.uiWidth/width,view.uiHeight/height};
    SDL_PushGPUFragmentUniformData(command,0,hudRect,sizeof(hudRect));
    SDL_PushGPUFragmentUniformData(command,1,&view.camera,sizeof(view.camera));
    SDL_GPUTextureSamplerBinding samplers[]={{colorTexture,linearClamp},{bloomTexture,linearClamp},{hudTexture,linearClamp},{paletteLUT,nearestClamp}};
    SDL_BindGPUFragmentSamplers(pass,0,samplers,4);
    SDL_DrawGPUPrimitives(pass,3,1,0,0);
    if(!settingsQuad.empty()) {
        SDL_BindGPUGraphicsPipeline(pass,settingsPipeline);
        SDL_GPUTextureSamplerBinding panel={settingsTexture,nearestClamp};
        SDL_BindGPUFragmentSamplers(pass,0,&panel,1);
        drawVertices(pass,settingsQuad);
    }
    SDL_EndGPURenderPass(pass);
}
static void capture(SDL_GPUCommandBuffer *command,const FrameView &view,int w,int h) {
    SDL_GPUTexture *image=createTexture(swapchainFormat,w,h,SDL_GPU_TEXTUREUSAGE_COLOR_TARGET);
    SDL_GPUTransferBufferCreateInfo info={SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD,(Uint32)(w*h*4),0};
    SDL_GPUTransferBuffer *download=image?SDL_CreateGPUTransferBuffer(device,&info):nullptr;
    if(!download) {
        release(image);SDL_SubmitGPUCommandBuffer(command);
        saveScreenshot(nullptr,0,0,0);return;
    }
    present(command,image,view);
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
    if(swapchainFormat==SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM)
        for(unsigned &p:pixels)p=(p&0xff00ff00u)|((p&0xffu)<<16)|((p>>16)&0xffu);
    saveScreenshot(fence?pixels.data():nullptr,w,h,w*4);
}
// Draws the settings panel into image and places it over the drawable.
static void layoutSettingsPanel(Uint32 drawableWidth,Uint32 drawableHeight,std::vector<unsigned> &image) {
    image.assign((size_t)DOOM_SETTINGS_WIDTH*DOOM_SETTINGS_HEIGHT,0);
    I_Render3DSettingsDraw(image.data());
    float dw=(float)drawableWidth,dh=(float)drawableHeight;
    settingsScale=std::max(1.0f,std::floor(std::min(dw/DOOM_SETTINGS_WIDTH,dh/DOOM_SETTINGS_HEIGHT)));
    float pw=DOOM_SETTINGS_WIDTH*settingsScale,ph=DOOM_SETTINGS_HEIGHT*settingsScale;
    settingsOrigin[0]=std::floor((dw-pw)/2);settingsOrigin[1]=std::floor((dh-ph)/2);
    screenQuad(settingsQuad,-1,1,2,2,-settingsOrigin[0]/pw,-settingsOrigin[1]/ph,
               (dw-settingsOrigin[0])/pw,(dh-settingsOrigin[1])/ph);
}
// Liquid reflections: the world drawn again at reduced resolution from a
// camera mirrored at the liquid height, clipped to above the surface.
// Its alpha holds how near each point is to the surface along the
// mirrored ray; the mirror pass is flagged by water = (plane, 0, 0, 1).
static void drawReflection(SDL_GPUCommandBuffer *render,const FrameView &view,Uniforms &camera) {
    camera.water[2]=camera.water[3]=0;
    if(!worldPending||!reflectionActive)return;
    int divisor=settings.retroReflections?8:2;
    int rw=std::max(1,(int)(view.worldW/divisor)),rh=std::max(1,(int)(view.worldH/divisor));
    if(!reflectionTexture||rw!=reflectionWidth||rh!=reflectionHeight) {
        release(reflectionTexture);release(reflectionDepth);
        reflectionTexture=createTexture(SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM,rw,rh,SDL_GPU_TEXTUREUSAGE_COLOR_TARGET|SDL_GPU_TEXTUREUSAGE_SAMPLER);
        reflectionDepth=createTexture(depthFormat,rw,rh,SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET);
        if(!reflectionTexture||!reflectionDepth)I_Error((char*)"Could not allocate reflection targets");
        reflectionWidth=rw;reflectionHeight=rh;
    }
    Uniforms mirror=camera;
    mirror.eye[2]=2*reflectionPlane-camera.eye[2];mirror.forward[2]=-camera.forward[2];mirror.up[2]=-camera.up[2];
    mirror.water[0]=reflectionPlane;mirror.water[3]=1;
    SDL_GPUColorTargetInfo color={};
    color.texture=reflectionTexture;color.load_op=SDL_GPU_LOADOP_CLEAR;color.store_op=SDL_GPU_STOREOP_STORE;
    color.clear_color={0.025f,0.025f,0.03f,1};
    SDL_GPUDepthStencilTargetInfo depth={};
    depth.texture=reflectionDepth;depth.clear_depth=1;depth.load_op=SDL_GPU_LOADOP_CLEAR;depth.store_op=SDL_GPU_STOREOP_DONT_CARE;
    depth.stencil_load_op=SDL_GPU_LOADOP_DONT_CARE;depth.stencil_store_op=SDL_GPU_STOREOP_DONT_CARE;
    SDL_GPURenderPass *pass=SDL_BeginGPURenderPass(render,&color,1,&depth);
    pushWorldUniforms(render,mirror);
    float plane[4]={reflectionPlane,0,0,0};SDL_PushGPUVertexUniformData(render,1,plane,sizeof(plane));
    drawWorld(render,pass,reflectOpaquePipeline,reflectSkySurfacePipeline,reflectSkyPipeline,reflectWorldPipeline,nullptr);
    SDL_EndGPURenderPass(pass);
    camera.water[0]=view.worldX;camera.water[1]=view.worldY;camera.water[2]=1/view.worldW;camera.water[3]=1/view.worldH;
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
// The world, its shadows, decals, particles and heat haze.
static void drawScene(SDL_GPUCommandBuffer *render,SDL_GPURenderPass *pass,const FrameView &view,const Uniforms &camera) {
    setView(pass,view.worldX,view.worldY,view.worldW,view.worldH);
    // Only fully covered textures use early depth writes. Cutouts and
    // sprites retain discard-aware shading after the sky background.
    // The whole map is drawn each frame in texture order, so without a
    // depth prepass hidden walls and floors would run the full lighting
    // before nearer ones cover them; with it each pixel shades once.
    SDL_BindGPUGraphicsPipeline(pass,prepassPipeline);
    for(const auto &batch:batches) if(opaqueBatch(batch)) drawVertices(pass,batch.second);
    drawWorld(render,pass,opaquePipeline,skySurfacePipeline,skyPipeline,worldPipeline,camera.water[2]>0?reflectionTexture:nullptr,
              coveragePipeline,cutoutPipeline);
    // The completed world depth hides shadows behind walls and enemies.
    SDL_BindGPUGraphicsPipeline(pass,shadowPipeline);
    for(const auto &batch:shadowBatches) {bindImage(pass,images.at(-1-batch.first).texture,linearClamp);drawVertices(pass,batch.second);}
    SDL_BindGPUGraphicsPipeline(pass,decalPipeline);
    for(const auto &batch:decalBatches) {bindImage(pass,images.at(-1-batch.first).texture,linearClamp);drawVertices(pass,batch.second);}
    SDL_BindGPUGraphicsPipeline(pass,particlePipeline);drawVertices(pass,particleVertices);
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
static void drawMainView(SDL_GPUCommandBuffer *render,const FrameView &view,const Uniforms &camera,bool drawMist,bool msaa,
                  const std::vector<SpriteDraw> &weapons,const std::vector<Vertex> &hud,const std::vector<Vertex> &cross) {
    SDL_GPUColorTargetInfo color={};
    color.texture=msaa?multisampleTexture:colorTexture;color.clear_color={0.025f,0.025f,0.03f,1};
    color.load_op=SDL_GPU_LOADOP_CLEAR;
    color.store_op=msaa&&!drawMist?SDL_GPU_STOREOP_RESOLVE:SDL_GPU_STOREOP_STORE;
    color.resolve_texture=msaa&&!drawMist?colorTexture:nullptr;
    SDL_GPUDepthStencilTargetInfo depth={};
    depth.texture=depthTexture;depth.clear_depth=1;depth.load_op=SDL_GPU_LOADOP_CLEAR;
    depth.store_op=drawMist&&!msaa?SDL_GPU_STOREOP_STORE:SDL_GPU_STOREOP_DONT_CARE;
    depth.stencil_load_op=SDL_GPU_LOADOP_DONT_CARE;depth.stencil_store_op=SDL_GPU_STOREOP_DONT_CARE;
    SDL_GPURenderPass *pass=SDL_BeginGPURenderPass(render,&color,1,&depth);
    pushWorldUniforms(render,camera);
    if(worldPending)drawScene(render,pass,view,camera);
    if(drawMist) {
        SDL_EndGPURenderPass(pass);
        color.load_op=SDL_GPU_LOADOP_LOAD;
        color.store_op=msaa?SDL_GPU_STOREOP_RESOLVE:SDL_GPU_STOREOP_STORE;
        color.resolve_texture=msaa?colorTexture:nullptr;
        pass=SDL_BeginGPURenderPass(render,&color,1,nullptr);
        setView(pass,view.worldX,view.worldY,view.worldW,view.worldH);
        drawMistPass(render,pass,camera,msaa);
        pushWorldUniforms(render,camera);
    }
    drawOverlays(render,pass,view,weapons,hud,cross,!drawMist);
    SDL_EndGPURenderPass(pass);
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
    for(const auto &batch:batches) if(!batch.second.empty()) {bindSurface(render,glowPass,batch.first,0,false);drawVertices(glowPass,batch.second);}
    // The sky hides glow behind it: a covered texel with no emission.
    SDL_GPUTextureSamplerBinding solid={solidTexel,nearestClamp};
    SDL_BindGPUFragmentSamplers(glowPass,0,&solid,1);SDL_BindGPUFragmentSamplers(glowPass,2,&solid,1);
    float noBlend[4]={};SDL_PushGPUFragmentUniformData(render,0,noBlend,sizeof(noBlend));
    drawVertices(glowPass,skyVertices);
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
    for(int axis=0;axis<2;++axis) {
        SDL_GPUColorTargetInfo blur={};
        blur.texture=axis?bloomTexture:bloomScratch;blur.load_op=SDL_GPU_LOADOP_DONT_CARE;blur.store_op=SDL_GPU_STOREOP_STORE;
        SDL_GPURenderPass *blurPass=SDL_BeginGPURenderPass(render,&blur,1,nullptr);
        SDL_BindGPUGraphicsPipeline(blurPass,blurPipeline);
        SDL_GPUTextureSamplerBinding source={axis?bloomScratch:emissionTexture,linearClamp};
        SDL_BindGPUFragmentSamplers(blurPass,0,&source,1);
        // Radius scales with resolution, retaining a similar halo at 50–100%.
        float direction[4]={axis?0:0.0035f,axis?0.0056f:0,0,0};
        SDL_PushGPUFragmentUniformData(render,0,direction,sizeof(direction));
        SDL_DrawGPUPrimitives(blurPass,3,1,0,0);
        SDL_EndGPURenderPass(blurPass);
    }
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
void releaseReflection() {release(reflectionTexture);release(reflectionDepth);reflectionWidth=reflectionHeight=0;}
} // namespace gpu3d

using namespace gpu3d;
void I_Render3DPresent(const unsigned *pixels,const unsigned *palette) {
    applySampleCount();
    SDL_GPUCommandBuffer *command=SDL_AcquireGPUCommandBuffer(device);
    if(!command) {fprintf(stderr,"GPU command buffer: %s\n",SDL_GetError());return;}
    SDL_GPUTexture *swapchain=nullptr;Uint32 drawableWidth=0,drawableHeight=0;
    if(!SDL_WaitAndAcquireGPUSwapchainTexture(command,gameWindow,&swapchain,&drawableWidth,&drawableHeight)||!swapchain) {
        flushUploads();SDL_SubmitGPUCommandBuffer(command);return;
    }
    uint64_t frameStart=SDL_GetTicksNS();
    int w=std::max(1,(int)drawableWidth*settings.scale/100),h=std::max(1,(int)drawableHeight*settings.scale/100);
    if(w!=width||h!=height||!colorTexture) {
        width=w;height=h;allocateTargets(w,h);
        fprintf(stderr,"GPU resolution: %dx%d (display %ux%u, %d%%).\n",w,h,drawableWidth,drawableHeight,settings.scale);
    }
    if(!paletteLUT||((settings.palette||(settings.reflections&&settings.retroReflections))&&worldPending))updatePaletteLUT(palette);
    std::vector<unsigned> overlay=hudPixels(pixels);
    FrameView view=prepareFrame(w,h);Uniforms &camera=view.camera;
    bool drawMist=!mistVertices.empty()||!shaftVertices.empty();
    bool msaa=sampleCount!=SDL_GPU_SAMPLECOUNT_1;
    // Transient quads join the frame upload; textures created while drawing
    // go through the upload command buffer, which is submitted first.
    std::vector<SpriteDraw> weapons;if(worldPending)weapons=weaponDraws(camera);
    std::vector<Vertex> hud;screenQuad(hud,-1,1,2,2,0,0,1,1);
    std::vector<Vertex> cross=crosshairVertices(view.worldW,view.worldH);
    std::vector<const std::vector<Vertex>*> transient={&hud,&cross};
    for(const auto &draw:weapons)transient.push_back(&draw.vertices);
    std::vector<unsigned> settingsImage;settingsQuad.clear();
    if(I_Render3DSettingsOpen()) {layoutSettingsPanel(drawableWidth,drawableHeight,settingsImage);transient.push_back(&settingsQuad);}
    // Profiling: the scene goes in a command buffer of its own, timed by a
    // fence. The swapchain's may wait for the compositor, which in a window
    // keeps the display's rate even without vsync.
    SDL_GPUCommandBuffer *render=profile.enabled?SDL_AcquireGPUCommandBuffer(device):command;
    if(!render) I_Error((char*)"GPU command buffer: %s",SDL_GetError());
    uploadFrame(render,transient,sectorInfo(),palette,overlay,settingsImage);
    drawReflection(render,view,camera);
    if(drawMist&&msaa)drawMistDepth(render,view,camera);
    drawMainView(render,view,camera,drawMist,msaa,weapons,hud,cross);
    if(camera.materials[1]>0)drawBloom(render,view,camera,weapons);
    if(render!=command)submitProfiled(render,frameStart);
    present(command,swapchain,view);
    flushUploads();
    if(screenshotPending||renderCheckDue()) {screenshotPending=false;capture(command,view,(int)drawableWidth,(int)drawableHeight);}
    else if(!SDL_SubmitGPUCommandBuffer(command)) I_Error((char*)"GPU rendering failed: %s",SDL_GetError());
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
