/* Shaders and graphics pipelines from the embedded shader code, the render
   targets, and the MSAA sample count that both depend on. */
#include "internal.h"
#include "gpu_shaders.h"   // <build>/gpu-shaders (scripts/gpu_shaders.cmake)
#include <thread>

namespace gpu3d {
SDL_GPUSampleCount sampleCount=SDL_GPU_SAMPLECOUNT_1;
SDL_GPUTextureFormat sceneFormat=SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM;
static SDL_GPUShader *worldVertexShader,*reflectVertexShader,*screenVertexShader,*skyVertexShader;
SDL_GPUGraphicsPipeline *opaquePipeline,*skyPipeline,*skySurfacePipeline,*skyBakePipeline,*skyCachedPipeline,*skyCachedSurfacePipeline,*reflectSkySurfacePipeline,*worldPipeline,*shadowPipeline,*decalPipeline,*particlePipeline,*crestPipeline,*heatPipeline,
    *weaponPipeline,*solidPipeline,*mistPipeline,*shaftPipeline,*overlayWeaponPipeline,*overlaySolidPipeline,
    *reflectOpaquePipeline,*reflectSkyPipeline,*reflectWorldPipeline,*emissionPipeline,*blurPipeline,*presentPipeline,*depthPipeline,*prepassPipeline,*coveragePipeline,*corePipeline,*cutoutPipeline,
    *settingsPipeline,*glassPipeline,*reflectPrepassPipeline,*glowParticlePipeline,*lampSheetPipeline;
// The opaque world's variants (world.frag; drawOpaque in draw.cpp) beside each pipeline of the full
// shader: [main view, reflection, shadowed, shadow factor][OpaqueVariant].
std::atomic<SDL_GPUGraphicsPipeline*> opaqueVariants[4][opaqueVariantCount];
// The live sky's twins for one kind of sky under clear air (sky.frag; skyTwin in draw.cpp).
std::atomic<SDL_GPUGraphicsPipeline*> skyClearPipelines[skyVariantCount][skyTwinCount];
// The passes into the swapchain as SDR always draws it, for screenshots while HDR is on.
SDL_GPUGraphicsPipeline *capturePresentPipeline;
// Shadow volumes (frame.cpp's drawShadowVolumes), on a depth target of their own with a stencil:
// the scene's depth again, the volumes counted into the stencil, the opaque world redrawn where it is not zero.
SDL_GPUGraphicsPipeline *volumeDepthPipeline,*volumePipeline,*volumeUndoPipeline,*shadowedPipeline,*shadowFactorPipeline;
// And the sprites that stand in them, drawn again the same two ways.
SDL_GPUGraphicsPipeline *shadowedSpritePipeline,*shadowSpriteFactorPipeline;
SDL_GPUTextureFormat shadowFormat=SDL_GPU_TEXTUREFORMAT_INVALID;
SDL_GPUTexture *shadowDepth;
SDL_GPUTexture *multisampleTexture,*colorTexture,*depthTexture,*sceneDepth,*emissionTexture,*emissionDepth,*bloomScratch,*bloomTexture;
int width,height;
struct ShaderCode { const unsigned char *spirv; size_t spirvSize; const unsigned char *msl; size_t mslSize; };
static SDL_GPUShader *shader(const ShaderCode &code,SDL_GPUShaderStage stage,Uint32 samplers,Uint32 storage,Uint32 uniforms) {
    SDL_GPUShaderCreateInfo info={};
    bool msl=shaderFormat==SDL_GPU_SHADERFORMAT_MSL;
    info.code=msl?code.msl:code.spirv;info.code_size=msl?code.mslSize:code.spirvSize;
    // SPIRV-Cross renames the entry point because main is reserved in MSL.
    info.entrypoint=msl?"main0":"main";info.format=shaderFormat;
    info.stage=stage;info.num_samplers=samplers;info.num_storage_buffers=storage;info.num_uniform_buffers=uniforms;
    SDL_GPUShader *result=SDL_CreateGPUShader(device,&info);
    if(!result)fprintf(stderr,"GPU shader: %s\n",SDL_GetError());
    return result;
}
#define SHADER_CODE(name) ShaderCode{doom_spv_##name,sizeof(doom_spv_##name),doom_msl_##name,sizeof(doom_msl_##name)}
#define VERTEX_SHADER(name,uniforms) shader(SHADER_CODE(name),SDL_GPU_SHADERSTAGE_VERTEX,0,0,uniforms)
#define FRAGMENT_SHADER(name,samplers,storage,uniforms) shader(SHADER_CODE(name),SDL_GPU_SHADERSTAGE_FRAGMENT,samplers,storage,uniforms)
// heatBlend writes only alpha, keeping the lowest (hottest) value.
enum Blend { opaqueBlend, alphaBlend, heatBlend, addBlend, mirrorBlend, depthBlend, brightenBlend, multiplyBlend };
// depthEqual: only where the depth already written is this very surface's (after a pass that wrote it).
enum Depth { noDepth, depthWrite, depthTest, depthAlways, depthOnly, depthEqual };
// On the stencilled depth target: plain; counting the volumes' faces that lie behind what is
// drawn (depth fail: right with the eye inside a volume too); drawing only where the count is not zero.
enum Stencil { noStencil, stencilPlain, stencilCount, stencilUncount, stencilInside };   // (uncount: the count taken out again, for the next light)
// Geometry pipelines read the 68-byte Vertex; screen and sky passes have no vertex input. Which one
// comes from the vertex shader given (the world's or the reflection's), unless input says (Input):
// the output pipelines and the variant builder thread say it, as they must not lean on those globals.
enum Input { inputByShader, inputVertices, inputNone };
static SDL_GPUGraphicsPipeline *pipeline(SDL_GPUShader *vertex,SDL_GPUShader *fragment,Blend blend,Depth depth,
                                  SDL_GPUSampleCount samples,SDL_GPUTextureFormat color,Stencil stencil=noStencil,Input input=inputByShader) {
    if(!vertex||!fragment)return nullptr;
    static const SDL_GPUVertexBufferDescription buffer={0,sizeof(Vertex),SDL_GPU_VERTEXINPUTRATE_VERTEX,0};
    static const SDL_GPUVertexAttribute attributes[]={
        {0,0,SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3,offsetof(Vertex,x)},{1,0,SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2,offsetof(Vertex,u)},
        {2,0,SDL_GPU_VERTEXELEMENTFORMAT_FLOAT,offsetof(Vertex,light)},{3,0,SDL_GPU_VERTEXELEMENTFORMAT_UINT,offsetof(Vertex,mode)},
        {4,0,SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3,offsetof(Vertex,red)},{5,0,SDL_GPU_VERTEXELEMENTFORMAT_UINT2,offsetof(Vertex,lightMask)},
        {6,0,SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2,offsetof(Vertex,sunU)},{7,0,SDL_GPU_VERTEXELEMENTFORMAT_UINT,offsetof(Vertex,statics)},
        {8,0,SDL_GPU_VERTEXELEMENTFORMAT_UINT,offsetof(Vertex,lampTint)},{9,0,SDL_GPU_VERTEXELEMENTFORMAT_FLOAT,offsetof(Vertex,lift)}};
    SDL_GPUGraphicsPipelineCreateInfo info={};
    info.vertex_shader=vertex;info.fragment_shader=fragment;
    if(input==inputByShader?vertex==worldVertexShader||vertex==reflectVertexShader:input==inputVertices) {
        info.vertex_input_state.vertex_buffer_descriptions=&buffer;info.vertex_input_state.num_vertex_buffers=1;
        info.vertex_input_state.vertex_attributes=attributes;info.vertex_input_state.num_vertex_attributes=10;
    }
    info.primitive_type=SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    info.rasterizer_state.fill_mode=SDL_GPU_FILLMODE_FILL;info.rasterizer_state.cull_mode=SDL_GPU_CULLMODE_NONE;
    info.rasterizer_state.enable_depth_clip=true;
    info.multisample_state.sample_count=samples;
    SDL_GPUColorTargetDescription target={};
    target.format=color;
    if(blend==alphaBlend) {
        auto &b=target.blend_state;b.enable_blend=true;
        b.src_color_blendfactor=SDL_GPU_BLENDFACTOR_SRC_ALPHA;b.dst_color_blendfactor=SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        b.src_alpha_blendfactor=SDL_GPU_BLENDFACTOR_ONE;b.dst_alpha_blendfactor=SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        b.color_blend_op=b.alpha_blend_op=SDL_GPU_BLENDOP_ADD;
    } else if(blend==mirrorBlend) {
        // Blended like alphaBlend, but the target's alpha (the reflection's
        // distance from the surface, written by opaque geometry) is kept.
        auto &b=target.blend_state;b.enable_blend=true;
        b.src_color_blendfactor=SDL_GPU_BLENDFACTOR_SRC_ALPHA;b.dst_color_blendfactor=SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        b.src_alpha_blendfactor=SDL_GPU_BLENDFACTOR_ZERO;b.dst_alpha_blendfactor=SDL_GPU_BLENDFACTOR_ONE;
        b.color_blend_op=b.alpha_blend_op=SDL_GPU_BLENDOP_ADD;
    } else if(blend==addBlend) {
        // Light added by alpha; the target's alpha (heat shimmer marks) is kept.
        auto &b=target.blend_state;b.enable_blend=true;
        b.src_color_blendfactor=SDL_GPU_BLENDFACTOR_SRC_ALPHA;b.dst_color_blendfactor=SDL_GPU_BLENDFACTOR_ONE;
        b.src_alpha_blendfactor=SDL_GPU_BLENDFACTOR_ZERO;b.dst_alpha_blendfactor=SDL_GPU_BLENDFACTOR_ONE;
        b.color_blend_op=b.alpha_blend_op=SDL_GPU_BLENDOP_ADD;
    } else if(blend==brightenBlend) {
        // What is drawn already, times one plus the source color: liquid
        // crests brighten the shaded surface under them by a step; the
        // target's alpha is kept.
        auto &b=target.blend_state;b.enable_blend=true;
        b.src_color_blendfactor=SDL_GPU_BLENDFACTOR_DST_COLOR;b.dst_color_blendfactor=SDL_GPU_BLENDFACTOR_ONE;
        b.src_alpha_blendfactor=SDL_GPU_BLENDFACTOR_ZERO;b.dst_alpha_blendfactor=SDL_GPU_BLENDFACTOR_ONE;
        b.color_blend_op=b.alpha_blend_op=SDL_GPU_BLENDOP_ADD;
    } else if(blend==multiplyBlend) {
        // What is drawn already, times the source colour (a shadow volume's share of light left); the target's alpha is kept.
        auto &b=target.blend_state;b.enable_blend=true;
        b.src_color_blendfactor=SDL_GPU_BLENDFACTOR_DST_COLOR;b.dst_color_blendfactor=SDL_GPU_BLENDFACTOR_ZERO;
        b.src_alpha_blendfactor=SDL_GPU_BLENDFACTOR_ZERO;b.dst_alpha_blendfactor=SDL_GPU_BLENDFACTOR_ONE;
        b.color_blend_op=b.alpha_blend_op=SDL_GPU_BLENDOP_ADD;
    } else if(blend==depthBlend) {
        // Depth prepass: the color target stays untouched.
        target.blend_state.enable_color_write_mask=true;target.blend_state.color_write_mask=0;
    } else if(blend==heatBlend) {
        auto &b=target.blend_state;b.enable_blend=true;b.enable_color_write_mask=true;b.color_write_mask=SDL_GPU_COLORCOMPONENT_A;
        b.src_color_blendfactor=b.src_alpha_blendfactor=b.dst_color_blendfactor=b.dst_alpha_blendfactor=SDL_GPU_BLENDFACTOR_ONE;
        b.color_blend_op=SDL_GPU_BLENDOP_ADD;b.alpha_blend_op=SDL_GPU_BLENDOP_MIN;
    }
    if(depth!=depthOnly) {info.target_info.color_target_descriptions=&target;info.target_info.num_color_targets=1;}
    if(depth!=noDepth) {
        info.target_info.has_depth_stencil_target=true;info.target_info.depth_stencil_format=depthFormat;
        info.depth_stencil_state.enable_depth_test=depth!=depthAlways;
        info.depth_stencil_state.enable_depth_write=depth==depthWrite||depth==depthOnly;
        info.depth_stencil_state.compare_op=depth==depthEqual?SDL_GPU_COMPAREOP_EQUAL:SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
        if(stencil!=noStencil)info.target_info.depth_stencil_format=shadowFormat;
        if(stencil==stencilCount||stencil==stencilUncount||stencil==stencilInside) {
            bool counts=stencil!=stencilInside;
            auto &s=info.depth_stencil_state;
            s.enable_stencil_test=true;s.compare_mask=0xFF;s.write_mask=counts?0xFF:0;
            SDL_GPUStencilOpState keep={};
            keep.fail_op=keep.pass_op=keep.depth_fail_op=SDL_GPU_STENCILOP_KEEP;
            keep.compare_op=counts?SDL_GPU_COMPAREOP_ALWAYS:SDL_GPU_COMPAREOP_NOT_EQUAL;
            s.front_stencil_state=s.back_stencil_state=keep;
            if(counts) {
                bool undo=stencil==stencilUncount;
                s.front_stencil_state.depth_fail_op=undo?SDL_GPU_STENCILOP_INCREMENT_AND_WRAP:SDL_GPU_STENCILOP_DECREMENT_AND_WRAP;
                s.back_stencil_state.depth_fail_op=undo?SDL_GPU_STENCILOP_DECREMENT_AND_WRAP:SDL_GPU_STENCILOP_INCREMENT_AND_WRAP;
            }
        }
    }
    SDL_GPUGraphicsPipeline *result=SDL_CreateGPUGraphicsPipeline(device,&info);
    if(!result)fprintf(stderr,"GPU pipeline: %s\n",SDL_GetError());
    return result;
}
// The passes into the swapchain, for its format now (hdr_output.cpp changes it), and in SDR for screenshots.
bool createOutputPipelines() {
    releaseOutputPipelines();
    SDL_GPUShader *wv=VERTEX_SHADER(world_vert,1),*pv=shader(SHADER_CODE(present_vert),SDL_GPU_SHADERSTAGE_VERTEX,2,0,0);
    SDL_GPUShader *present=FRAGMENT_SHADER(present_frag,10,0,2),*panel=FRAGMENT_SHADER(panel_frag,1,0,1);
    // (Said outright, not by swapping worldVertexShader: the variant builder may be reading it.)
    const auto sdr=SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM;
    presentPipeline=pipeline(pv,present,opaqueBlend,noDepth,SDL_GPU_SAMPLECOUNT_1,swapchainFormat,noStencil,inputNone);
    settingsPipeline=pipeline(wv,panel,alphaBlend,noDepth,SDL_GPU_SAMPLECOUNT_1,swapchainFormat,noStencil,inputVertices);   // the loading screen
    capturePresentPipeline=pipeline(pv,present,opaqueBlend,noDepth,SDL_GPU_SAMPLECOUNT_1,sdr,noStencil,inputNone);
    for(SDL_GPUShader *s:{wv,pv,present,panel})if(s)SDL_ReleaseGPUShader(device,s);
    return presentPipeline&&settingsPipeline&&capturePresentPipeline;
}
void releaseOutputPipelines() {
    for(SDL_GPUGraphicsPipeline **p:{&presentPipeline,&settingsPipeline,&capturePresentPipeline})release(*p);
}
// world.frag's walls and flats variants (8 shaders, up to 32 pipelines) are built on a thread of
// their own. Without the driver's cache (the first start after every update) they took 12 s of
// the Mac's start and 17 s of the Deck's. Until a pipeline is ready drawOpaque takes PLAIN in its
// place, which draws the same picture to the bit, only slower. Render checks wait for them, so
// tests see the finished set. SDL_gpu's Vulkan backend locks its shared caches for this, and a
// Metal device makes pipelines from any thread.
static std::thread variantBuilder;
static std::atomic<bool> variantStop{false};
static void buildVariants(SDL_GPUShader *world,SDL_GPUShader *reflect,SDL_GPUShader *skyVertex,SDL_GPUSampleCount msaa,SDL_GPUTextureFormat color,bool shadows) {
    const SDL_GPUTextureFormat mirror=SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM;
    // In OpaqueVariant's order from opaquePlainWalls: walls then flats, each plain, quiet, clear air, both.
    const ShaderCode codes[opaqueVariantCount-1]={SHADER_CODE(plain_walls_frag),SHADER_CODE(plain_walls_quiet_frag),
        SHADER_CODE(plain_walls_air_frag),SHADER_CODE(plain_walls_quiet_air_frag),SHADER_CODE(plain_flats_frag),
        SHADER_CODE(plain_flats_quiet_frag),SHADER_CODE(plain_flats_air_frag),SHADER_CODE(plain_flats_quiet_air_frag)};
    // The commonest first (quiet triangles under clear air, then quiet ones, then the rest), and
    // every main view pipeline before any of the shadow volumes' and the reflections'.
    const int order[opaqueVariantCount-1]={opaquePlainWalls+3,opaquePlainFlats+3,opaquePlainWalls+1,opaquePlainFlats+1,
                                           opaquePlainWalls+2,opaquePlainFlats+2,opaquePlainWalls,opaquePlainFlats};
    SDL_GPUShader *fragments[opaqueVariantCount]={};
    for(int v:order) {
        if(variantStop)break;
        fragments[v]=shader(codes[v-opaquePlainWalls],SDL_GPU_SHADERSTAGE_FRAGMENT,16,2,4);
        if(fragments[v])opaqueVariants[0][v]=pipeline(world,fragments[v],opaqueBlend,depthWrite,msaa,color,noStencil,inputVertices);
    }
    // The live sky for each kind of sky under clear air, and its map surfaces, for the main view.
    const ShaderCode skyCodes[skyVariantCount][2]={{SHADER_CODE(sky_field_frag),SHADER_CODE(sky_surface_field_frag)},
        {SHADER_CODE(sky_field_cloud_frag),SHADER_CODE(sky_surface_field_cloud_frag)},{SHADER_CODE(sky_cloud_frag),SHADER_CODE(sky_surface_cloud_frag)}};
    SDL_GPUShader *sky[skyVariantCount]={},*skySurface[skyVariantCount]={};
    for(int k=0;k<skyVariantCount&&!variantStop;++k) {
        sky[k]=shader(skyCodes[k][0],SDL_GPU_SHADERSTAGE_FRAGMENT,6,0,3);
        skySurface[k]=shader(skyCodes[k][1],SDL_GPU_SHADERSTAGE_FRAGMENT,6,0,3);
        if(sky[k])skyClearPipelines[k][skyTwinMain]=pipeline(skyVertex,sky[k],opaqueBlend,depthTest,msaa,color,noStencil,inputNone);
        if(skySurface[k])skyClearPipelines[k][skyTwinSurface]=pipeline(world,skySurface[k],opaqueBlend,depthWrite,msaa,color,noStencil,inputVertices);
    }
    for(int v:order) {
        if(variantStop||!fragments[v]||!shadows)break;
        opaqueVariants[2][v]=pipeline(world,fragments[v],opaqueBlend,depthTest,msaa,color,stencilInside,inputVertices);
        opaqueVariants[3][v]=pipeline(world,fragments[v],multiplyBlend,depthTest,msaa,color,stencilInside,inputVertices);
    }
    for(int v:order) {
        if(variantStop)break;
        if(fragments[v])opaqueVariants[1][v]=pipeline(reflect,fragments[v],opaqueBlend,depthWrite,SDL_GPU_SAMPLECOUNT_1,mirror,noStencil,inputVertices);
    }
    for(int k=0;k<skyVariantCount&&!variantStop;++k) {
        if(sky[k])skyClearPipelines[k][skyTwinReflect]=pipeline(skyVertex,sky[k],opaqueBlend,depthTest,SDL_GPU_SAMPLECOUNT_1,mirror,noStencil,inputNone);
        if(skySurface[k])skyClearPipelines[k][skyTwinReflectSurface]=pipeline(reflect,skySurface[k],opaqueBlend,depthWrite,SDL_GPU_SAMPLECOUNT_1,mirror,noStencil,inputVertices);
    }
    for(SDL_GPUShader *fragment:fragments)if(fragment)SDL_ReleaseGPUShader(device,fragment);
    for(int k=0;k<skyVariantCount;++k)for(SDL_GPUShader *s:{sky[k],skySurface[k]})if(s)SDL_ReleaseGPUShader(device,s);
}
static void stopVariants() {
    variantStop=true;
    if(variantBuilder.joinable())variantBuilder.join();
}
static void startVariants(SDL_GPUSampleCount msaa,SDL_GPUTextureFormat color,bool shadows) {
    stopVariants();   // (releasePipelines did already)
    variantStop=false;
    // The vertex shaders as they are now: createPipelines' and releasePipelines' alone change them, after stopVariants.
    try {variantBuilder=std::thread(buildVariants,worldVertexShader,reflectVertexShader,skyVertexShader,msaa,color,shadows);}
    catch(...) {buildVariants(worldVertexShader,reflectVertexShader,skyVertexShader,msaa,color,shadows);return;}   // no thread to be had: here and now
    bool wait=M_CheckParm((char*)"-rendercheck")!=0;
#ifdef DOOM_DEVTOOLS
    if(getenv("DERP_VARIANTSASYNC"))wait=false;   // a render check that times the start as players get it
#endif
    if(wait)variantBuilder.join();
}
bool createPipelines() {
    worldVertexShader=VERTEX_SHADER(world_vert,1);reflectVertexShader=VERTEX_SHADER(reflect_vert,2);
    screenVertexShader=VERTEX_SHADER(screen_vert,0);skyVertexShader=VERTEX_SHADER(sky_vert,0);
    SDL_GPUShader *world=FRAGMENT_SHADER(world_frag,16,2,4),*opaque=FRAGMENT_SHADER(opaque_frag,16,2,4);
    SDL_GPUShader *plain=FRAGMENT_SHADER(plain_frag,16,2,4);   // (the walls and flats variants: buildVariants)
    SDL_GPUShader *cutout=FRAGMENT_SHADER(cutout_frag,16,2,4),*coverage=FRAGMENT_SHADER(coverage_frag,16,2,4);
    SDL_GPUShader *core=FRAGMENT_SHADER(core_frag,16,2,4);
    SDL_GPUShader *overlay=FRAGMENT_SHADER(overlay_frag,16,2,4);
    SDL_GPUShader *sky=FRAGMENT_SHADER(sky_frag,6,0,3),*skySurface=FRAGMENT_SHADER(sky_surface_frag,6,0,3),*shadow=FRAGMENT_SHADER(shadow_frag,1,0,1),*decal=FRAGMENT_SHADER(decal_frag,1,0,1);
    SDL_GPUShader *skyBake=FRAGMENT_SHADER(sky_bake_frag,6,0,3),*skyCached=FRAGMENT_SHADER(sky_cached_frag,7,0,3),*skyCachedSurface=FRAGMENT_SHADER(sky_cached_surface_frag,7,0,3);
    SDL_GPUShader *particle=FRAGMENT_SHADER(particle_frag,0,0,1),*solid=FRAGMENT_SHADER(solid_frag,0,0,0);
    SDL_GPUShader *heat=FRAGMENT_SHADER(heat_frag,0,0,0),*glass=FRAGMENT_SHADER(glass_frag,4,1,4);
    SDL_GPUShader *emission=FRAGMENT_SHADER(emission_frag,5,1,2),*blur=FRAGMENT_SHADER(blur_frag,1,0,1);
    SDL_GPUShader *mist=FRAGMENT_SHADER(mist_frag,2,0,1),*depth=FRAGMENT_SHADER(depth_frag,1,0,0);
    // The main view draws in sceneFormat (float for HDR in the scene); mirrors and probes stay 8-bit.
    const auto color=sceneFormat,mirror=SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM,glow=SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;
    const auto one=SDL_GPU_SAMPLECOUNT_1,msaa=sampleCount;
    SDL_GPUShader *wv=worldVertexShader,*rv=reflectVertexShader,*sv=screenVertexShader,*kv=skyVertexShader;
    opaquePipeline=pipeline(wv,opaque,opaqueBlend,depthWrite,msaa,color);
    opaqueVariants[0][opaquePlain]=pipeline(wv,plain,opaqueBlend,depthWrite,msaa,color);
    prepassPipeline=pipeline(wv,solid,depthBlend,depthWrite,msaa,color);
    coveragePipeline=pipeline(wv,coverage,depthBlend,depthWrite,msaa,color);
    corePipeline=pipeline(wv,core,depthBlend,depthWrite,msaa,color);
    // Masked walls shade only where their own depth was written (COVERAGE, just before):
    // tested for less-or-equal, the holes of a grate passed too, nearer than what shows
    // through them, and ran the whole of the lighting before being thrown away. A grate
    // across the view cost more than the world behind it.
    cutoutPipeline=pipeline(wv,cutout,alphaBlend,depthEqual,msaa,color);
    skyPipeline=pipeline(kv,sky,opaqueBlend,depthTest,msaa,color);
    skySurfacePipeline=pipeline(wv,skySurface,opaqueBlend,depthWrite,msaa,color);
    skyBakePipeline=pipeline(sv,skyBake,opaqueBlend,noDepth,one,glow);
    skyCachedPipeline=pipeline(kv,skyCached,opaqueBlend,depthTest,msaa,color);
    skyCachedSurfacePipeline=pipeline(wv,skyCachedSurface,opaqueBlend,depthWrite,msaa,color);
    worldPipeline=pipeline(wv,world,alphaBlend,depthWrite,msaa,color);
    shadowPipeline=pipeline(wv,shadow,alphaBlend,depthTest,msaa,color);
    decalPipeline=pipeline(wv,decal,alphaBlend,depthTest,msaa,color);
    particlePipeline=pipeline(wv,particle,alphaBlend,depthTest,msaa,color);
    crestPipeline=pipeline(wv,particle,brightenBlend,depthTest,msaa,color);
    // The rain round the lamps (flat.frag MODE_LAMP_SHEET): light added over the scene, tested
    // against the world's depth so a wall in front hides it, writing none.
    lampSheetPipeline=pipeline(wv,particle,addBlend,depthTest,msaa,color);
    heatPipeline=pipeline(wv,heat,heatBlend,depthTest,msaa,color);
    glassPipeline=pipeline(wv,glass,alphaBlend,depthTest,msaa,color);
    weaponPipeline=pipeline(wv,world,alphaBlend,depthAlways,msaa,color);
    solidPipeline=pipeline(wv,solid,alphaBlend,depthAlways,msaa,color);
    // After the mist, overlays continue in a pass without a depth target.
    mistPipeline=pipeline(wv,mist,alphaBlend,noDepth,msaa,color);
    shaftPipeline=pipeline(wv,mist,addBlend,noDepth,msaa,color);
    overlayWeaponPipeline=pipeline(wv,overlay,alphaBlend,noDepth,msaa,color);
    overlaySolidPipeline=pipeline(wv,solid,alphaBlend,noDepth,msaa,color);
    reflectOpaquePipeline=pipeline(rv,opaque,opaqueBlend,depthWrite,one,mirror);
    opaqueVariants[1][opaquePlain]=pipeline(rv,plain,opaqueBlend,depthWrite,one,mirror);
    reflectPrepassPipeline=pipeline(rv,solid,depthBlend,depthWrite,one,mirror);
    reflectSkyPipeline=pipeline(kv,sky,opaqueBlend,depthTest,one,mirror);
    reflectSkySurfacePipeline=pipeline(rv,skySurface,opaqueBlend,depthWrite,one,mirror);
    reflectWorldPipeline=pipeline(rv,world,mirrorBlend,depthWrite,one,mirror);
    emissionPipeline=pipeline(wv,emission,opaqueBlend,depthWrite,one,glow);
    glowParticlePipeline=pipeline(wv,particle,alphaBlend,depthTest,one,glow);   // embers into the bloom
    blurPipeline=pipeline(sv,blur,opaqueBlend,noDepth,one,glow);
    depthPipeline=pipeline(wv,depth,opaqueBlend,depthOnly,one,mirror);   // (no colour target)
    // Shadow volumes need a depth format with a stencil; without one there are none.
    shadowFormat=SDL_GPU_TEXTUREFORMAT_INVALID;
    for(SDL_GPUTextureFormat format:{SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT,SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT})
        if(shadowFormat==SDL_GPU_TEXTUREFORMAT_INVALID&&SDL_GPUTextureSupportsFormat(device,format,SDL_GPU_TEXTURETYPE_2D,SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET)&&
           SDL_GPUTextureSupportsSampleCount(device,format,msaa))shadowFormat=format;
    volumeDepthPipeline=volumePipeline=volumeUndoPipeline=shadowedPipeline=shadowFactorPipeline=shadowedSpritePipeline=shadowSpriteFactorPipeline=nullptr;
    opaqueVariants[2][opaquePlain]=opaqueVariants[3][opaquePlain]=nullptr;
    if(shadowFormat!=SDL_GPU_TEXTUREFORMAT_INVALID) {
        volumeDepthPipeline=pipeline(wv,depth,depthBlend,depthWrite,msaa,color,stencilPlain);
        volumePipeline=pipeline(wv,solid,depthBlend,depthTest,msaa,color,stencilCount);
        volumeUndoPipeline=pipeline(wv,solid,depthBlend,depthTest,msaa,color,stencilUncount);
        shadowedPipeline=pipeline(wv,opaque,opaqueBlend,depthTest,msaa,color,stencilInside);
        shadowFactorPipeline=pipeline(wv,opaque,multiplyBlend,depthTest,msaa,color,stencilInside);
        opaqueVariants[2][opaquePlain]=pipeline(wv,plain,opaqueBlend,depthTest,msaa,color,stencilInside);
        opaqueVariants[3][opaquePlain]=pipeline(wv,plain,multiplyBlend,depthTest,msaa,color,stencilInside);
        shadowedSpritePipeline=pipeline(wv,world,alphaBlend,depthTest,msaa,color,stencilInside);
        shadowSpriteFactorPipeline=pipeline(wv,world,multiplyBlend,depthTest,msaa,color,stencilInside);
    }
    if(plain)SDL_ReleaseGPUShader(device,plain);
    for(SDL_GPUShader *s:{world,opaque,cutout,coverage,core,overlay,sky,skySurface,skyBake,skyCached,skyCachedSurface,shadow,decal,particle,heat,glass,solid,emission,blur,mist,depth})
        if(s)SDL_ReleaseGPUShader(device,s);
    // (The world's, the reflection's and the sky's vertex shaders stay for buildVariants: releasePipelines.)
    for(SDL_GPUShader **s:{&screenVertexShader})
        if(*s) {SDL_ReleaseGPUShader(device,*s);*s=nullptr;}
    if(!createOutputPipelines())return false;
    for(auto *p:{opaquePipeline,skyPipeline,skySurfacePipeline,skyBakePipeline,skyCachedPipeline,skyCachedSurfacePipeline,reflectSkySurfacePipeline,worldPipeline,shadowPipeline,decalPipeline,particlePipeline,crestPipeline,heatPipeline,weaponPipeline,
                 solidPipeline,mistPipeline,shaftPipeline,overlayWeaponPipeline,overlaySolidPipeline,reflectOpaquePipeline,
                 reflectSkyPipeline,reflectWorldPipeline,emissionPipeline,blurPipeline,presentPipeline,depthPipeline,prepassPipeline,coveragePipeline,corePipeline,cutoutPipeline,settingsPipeline,glassPipeline,reflectPrepassPipeline,glowParticlePipeline,lampSheetPipeline})
        if(!p)return false;
    if(!opaqueVariants[0][opaquePlain]||!opaqueVariants[1][opaquePlain])return false;
    startVariants(msaa,color,shadowFormat!=SDL_GPU_TEXTUREFORMAT_INVALID);
    return true;
}
void releasePipelines() {
    stopVariants();
    for(SDL_GPUGraphicsPipeline **p:{&opaquePipeline,&skyPipeline,&skySurfacePipeline,&skyBakePipeline,&skyCachedPipeline,&skyCachedSurfacePipeline,&reflectSkySurfacePipeline,&worldPipeline,&shadowPipeline,&decalPipeline,&particlePipeline,&crestPipeline,&heatPipeline,
            &weaponPipeline,&solidPipeline,&mistPipeline,&shaftPipeline,&overlayWeaponPipeline,&overlaySolidPipeline,
            &reflectOpaquePipeline,&reflectSkyPipeline,&reflectWorldPipeline,&emissionPipeline,&blurPipeline,&presentPipeline,&depthPipeline,&prepassPipeline,&coveragePipeline,&corePipeline,&cutoutPipeline,
            &settingsPipeline,&glassPipeline,&reflectPrepassPipeline,&glowParticlePipeline,&lampSheetPipeline,&volumeDepthPipeline,&volumePipeline,&volumeUndoPipeline,&shadowedPipeline,&shadowFactorPipeline,&shadowedSpritePipeline,&shadowSpriteFactorPipeline})
        release(*p);
    for(auto &kind:opaqueVariants)for(auto &slot:kind)if(SDL_GPUGraphicsPipeline *p=slot.exchange(nullptr))SDL_ReleaseGPUGraphicsPipeline(device,p);
    for(auto &kind:skyClearPipelines)for(auto &slot:kind)if(SDL_GPUGraphicsPipeline *p=slot.exchange(nullptr))SDL_ReleaseGPUGraphicsPipeline(device,p);
    for(SDL_GPUShader **s:{&worldVertexShader,&reflectVertexShader,&skyVertexShader})
        if(*s) {SDL_ReleaseGPUShader(device,*s);*s=nullptr;}
    releaseOutputPipelines();
}
void releaseTargets() {
    for(SDL_GPUTexture **t:{&multisampleTexture,&colorTexture,&depthTexture,&sceneDepth,&emissionTexture,&emissionDepth,&bloomScratch,&bloomTexture,&shadowDepth})
        release(*t);
}
void allocateTargets(int w,int h) {
    releaseTargets();
    const auto color=sceneFormat,glow=SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;
    const auto target=SDL_GPU_TEXTUREUSAGE_COLOR_TARGET|SDL_GPU_TEXTUREUSAGE_SAMPLER;
    bool msaa=sampleCount!=SDL_GPU_SAMPLECOUNT_1;
    if(msaa)multisampleTexture=createTexture(color,w,h,SDL_GPU_TEXTUREUSAGE_COLOR_TARGET,sampleCount);
    colorTexture=createTexture(color,w,h,target);
    // Single-sample depth is read by the mist directly; MSAA depth uses a prepass copy.
    depthTexture=createTexture(depthFormat,w,h,SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET|(msaa?0:SDL_GPU_TEXTUREUSAGE_SAMPLER),sampleCount);
    if(msaa)sceneDepth=createTexture(depthFormat,w,h,SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET|SDL_GPU_TEXTUREUSAGE_SAMPLER);
    if(shadowFormat!=SDL_GPU_TEXTUREFORMAT_INVALID&&shadowedPipeline&&shadowFactorPipeline&&volumeUndoPipeline)shadowDepth=createTexture(shadowFormat,w,h,SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET,sampleCount);
    if(!colorTexture||!depthTexture||(msaa&&(!multisampleTexture||!sceneDepth)))
        I_Error((char*)"Could not allocate GPU render targets at %dx%d: %s",w,h,SDL_GetError());
    int bw=std::max(1,w/2),bh=std::max(1,h/2);
    emissionTexture=createTexture(glow,bw,bh,target);
    int gw,gh;bloomSize(w,h,&gw,&gh);
    bloomScratch=createTexture(glow,gw,gh,target);
    // The bloom with its mip levels: how much the whole view glows (screen.vert's bloomKept).
    SDL_GPUTextureCreateInfo bloomInfo={};
    bloomInfo.type=SDL_GPU_TEXTURETYPE_2D;bloomInfo.format=glow;bloomInfo.usage=target;
    bloomInfo.width=(Uint32)gw;bloomInfo.height=(Uint32)gh;bloomInfo.layer_count_or_depth=1;bloomInfo.sample_count=SDL_GPU_SAMPLECOUNT_1;
    bloomInfo.num_levels=1;for(int side=std::max(gw,gh);side>1;side/=2)++bloomInfo.num_levels;
    bloomTexture=SDL_CreateGPUTexture(device,&bloomInfo);
    emissionDepth=createTexture(depthFormat,bw,bh,SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET);
    if(!emissionTexture||!bloomScratch||!bloomTexture||!emissionDepth) I_Error((char*)"Could not allocate bloom targets");
}
// The world targets' sample count from the MSAA setting, capped by the GPU, and their format:
// 8-bit, or float while the display is in HDR with F4 "HDR in the scene" on (hdrSceneWanted),
// where the GPU takes it at that count. Changing either rebuilds the pipelines and the targets.
void applySampleCount() {
    int wanted=forceSingleSample?1:std::min(maxSamples,settings.msaa>0?1<<(int)std::lround(settings.msaa):1);
    SDL_GPUSampleCount count=wanted>=8?SDL_GPU_SAMPLECOUNT_8:wanted>=4?SDL_GPU_SAMPLECOUNT_4:wanted>=2?SDL_GPU_SAMPLECOUNT_2:SDL_GPU_SAMPLECOUNT_1;
    const auto plain=SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM,hdr=SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;
    SDL_GPUTextureFormat format=hdrSceneWanted()&&SDL_GPUTextureSupportsSampleCount(device,hdr,count)?hdr:plain;
    if(count==sampleCount&&format==sceneFormat&&opaquePipeline)return;
    SDL_WaitForGPUIdle(device);
    releasePipelines();sampleCount=count;sceneFormat=format;
    if(!createPipelines())I_Error((char*)"Could not create the GPU pipelines for %dx MSAA",wanted);
    releaseTargets();width=height=0;
    fprintf(stderr,"GPU anti-aliasing: %dx MSAA; the view drawn in %s.\n",wanted,format==hdr?"float (HDR in the scene)":"8 bits");
}
} // namespace gpu3d
