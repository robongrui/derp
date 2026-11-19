/* Internal to the SDL_gpu backend in platform/gpu3d/: the device, pipelines,
   targets and helpers its files share. The scene it draws is scene3d.h. */
#ifndef DOOM_GPU3D_INTERNAL_H
#define DOOM_GPU3D_INTERNAL_H
#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>
extern "C" {
#include "doomdef.h"
#include "doomstat.h"
#include "r_sky.h"
#include "m_argv.h"
#include "i_system.h"
extern boolean paused;
}
#include <SDL3/SDL.h>
#include "../i_render3d.h"
#include "../scene3d.h"

struct GpuTexture {
    SDL_GPUTexture *texture;
    ~GpuTexture();
};
namespace doom3d {
// scene3d/geometry.cpp: one more each frame the batches are built anew (drawOpaque's classes).
extern unsigned batchGeneration;
// scene3d/caustics.cpp: whether the box (map units) comes near any liquid's caustics or shore.
bool nearLiquid(float x0,float y0,float x1,float y1);
// scene3d/images.cpp: every PLAYPAL palette as I_SetPalette hands it on.
std::vector<std::array<unsigned,256>> gamePalettes();
}

namespace gpu3d {
using namespace doom3d;

// device.cpp
extern SDL_GPUDevice *device;
extern SDL_GPUShaderFormat shaderFormat;
extern SDL_GPUTextureFormat depthFormat,swapchainFormat;
extern int maxSamples;extern bool forceSingleSample;
extern SDL_GPUSampler *linearRepeat,*linearClamp,*nearestClamp,*nearestRepeat;
// Fixed textures: the HUD and settings panel images, the palette and its
// 32³ lookup, 1x1 stand-ins for missing scene maps, a covered solid texel.
extern SDL_GPUTexture *settingsTexture,*hudTexture,*paletteTexture,*paletteLUT,*dummySeam,*dummyContact,*dummyBake,*dummyRelief,*solidTexel;
SDL_GPUTexture *createTexture(SDL_GPUTextureFormat format,int w,int h,SDL_GPUTextureUsageFlags usage,
                              SDL_GPUSampleCount samples=SDL_GPU_SAMPLECOUNT_1,SDL_GPUTextureType type=SDL_GPU_TEXTURETYPE_2D,int depth=1);
void flushUploads();
void updatePaletteLUT(const unsigned *palette);
// After a load: every PLAYPAL palette's lookup made ahead, so the first
// flash of a level only uploads its table (a gamma change still builds them
// as they come).
void prebuildPaletteLUTs();

// pipelines.cpp
extern SDL_GPUSampleCount sampleCount;
// The main view's colour format: 8-bit, or float for HDR in the scene (applySampleCount).
extern SDL_GPUTextureFormat sceneFormat;
extern SDL_GPUGraphicsPipeline *opaquePipeline,*skyPipeline,*skySurfacePipeline,*skyBakePipeline,*skyCachedPipeline,*skyCachedSurfacePipeline,*reflectSkySurfacePipeline,*worldPipeline,*shadowPipeline,*decalPipeline,*particlePipeline,*crestPipeline,*heatPipeline,
    *weaponPipeline,*solidPipeline,*mistPipeline,*shaftPipeline,*overlayWeaponPipeline,*overlaySolidPipeline,
    *reflectOpaquePipeline,*reflectSkyPipeline,*reflectWorldPipeline,*emissionPipeline,*blurPipeline,*presentPipeline,*depthPipeline,*prepassPipeline,*coveragePipeline,*corePipeline,*cutoutPipeline,
    *settingsPipeline,*glassPipeline,*reflectPrepassPipeline,*glowParticlePipeline,*lampSheetPipeline;
// Render targets at the render resolution (width x height); the emission at half, the bloom at bloomSize.
extern SDL_GPUTexture *multisampleTexture,*colorTexture,*depthTexture,*sceneDepth,*emissionTexture,*emissionDepth,*bloomScratch,*bloomTexture;
extern int width,height;
// The bloom is blurred small: BLOOM_ROWS rows at the view's shape (fewer where the emission has fewer), so
// the blur's taps sit a texel apart and leave no gaps, and the halo is the same share of the view at any
// resolution. (Blurred at the emission's size with the taps spread out, a far lamp's halo was a few faint
// copies of its texels.)
#define BLOOM_ROWS 270
inline void bloomSize(int w,int h,int *bloomW,int *bloomH) {
    int ew=w/2>1?w/2:1,eh=h/2>1?h/2:1;
    *bloomH=eh<BLOOM_ROWS?eh:BLOOM_ROWS;
    int across=(int)((float)*bloomH*ew/eh+0.5f);
    *bloomW=across>1?across:1;
}
extern SDL_GPUGraphicsPipeline *volumeDepthPipeline,*volumePipeline,*volumeUndoPipeline,*shadowedPipeline,*shadowFactorPipeline,*shadowedSpritePipeline,*shadowSpriteFactorPipeline;
// world.frag's opaque variants beside the full shader's four pipelines (drawOpaque): PLAIN, and
// PLAIN for walls only or flats only, each also QUIET (+1: no dynamic light, no shore), with
// CLEAR_AIR (+2: no weather air or fog bank this frame), or both (+3).
// [main, reflection, shadowed, shadow factor][variant].
enum OpaqueVariant { opaquePlain, opaquePlainWalls, opaquePlainFlats=opaquePlainWalls+4, opaqueVariantCount=opaquePlainFlats+4 };
// The walls and flats ones come from a thread of their own (buildVariants): null until ready.
extern std::atomic<SDL_GPUGraphicsPipeline*> opaqueVariants[4][opaqueVariantCount];
// The live sky's twins for one kind of sky under clear air (sky.frag's SKY_CLEAR and kind
// defines; skyTwin in draw.cpp), from the same builder: null until ready. [SkyVariant][SkyTwin].
enum SkyTwin { skyTwinMain, skyTwinSurface, skyTwinReflect, skyTwinReflectSurface, skyTwinCount };
enum SkyVariant { skyField, skyFieldCloud, skyCloud, skyVariantCount };
extern std::atomic<SDL_GPUGraphicsPipeline*> skyClearPipelines[skyVariantCount][skyTwinCount];
extern SDL_GPUTexture *shadowDepth;
bool createPipelines();
void releasePipelines();
// The present pass (with the panels over it) and the loading screen's panel pass into the
// swapchain, for swapchainFormat now; capturePresentPipeline draws the present in SDR
// (screenshots while HDR is on).
extern SDL_GPUGraphicsPipeline *capturePresentPipeline;
bool createOutputPipelines();
void releaseOutputPipelines();
void releaseTargets();
void allocateTargets(int w,int h);
void applySampleCount();

// draw.cpp
// hudChanged: the HUD's pixels differ from those last uploaded (hudPixels);
// else hudTexture keeps them and they are not sent again.
void uploadFrame(SDL_GPUCommandBuffer *command,const std::vector<const std::vector<Vertex>*> &transient,
                 const std::vector<std::array<float,4>> &sectors,const unsigned *palette,const std::vector<unsigned> &hud,
                 bool hudChanged,const std::vector<unsigned> &panel);
// Whether hudTexture holds the HUD as last uploaded: false once it is made anew.
extern bool hudCurrent;
void releaseFrameUploads();
// The next upload (the frame after a load) sizes the vertex arena with room
// to spare, so no played frame has to grow it.
void presizeFrameUploads();
void drawVertices(SDL_GPURenderPass *pass,const std::vector<Vertex> &vertices);
void setView(SDL_GPURenderPass *pass,float x,float y,float w,float h,const SDL_Rect *scissor=nullptr);
void bindWorld(SDL_GPURenderPass *pass,SDL_GPUTexture *reflection);
void bindEmissionEdges(SDL_GPUCommandBuffer *command,SDL_GPURenderPass *pass,const Uniforms &camera);
// Just the lights' blocker buffer, for passes that only light highlights (glass.frag).
void bindLightBlockers(SDL_GPURenderPass *pass);
void bindSurface(SDL_GPUCommandBuffer *command,SDL_GPURenderPass *pass,int key,Uint32 blendSlot,bool world=true);
void bindSurface(SDL_GPUCommandBuffer *command,SDL_GPURenderPass *pass,const SurfaceBinding &binding,Uint32 blendSlot,bool world=true);
void bindImage(SDL_GPURenderPass *pass,const GpuTextureRef &texture,SDL_GPUSampler *filter);
void pushWorldUniforms(SDL_GPUCommandBuffer *command,const Uniforms &view);
void skyImages(SDL_GPUTextureSamplerBinding images[6]);   // what sky.frag samples
// sky_cache.cpp: F4's cached sky, its faces painted before the main view
// (true when this frame painted one).
bool drawSkyCache(SDL_GPUCommandBuffer *render,const Uniforms &camera);
bool skyCacheOn(const Uniforms &camera);
SDL_GPUTextureSamplerBinding skyCacheImage();
void releaseSkyCache();
bool opaqueBatch(const std::pair<const int,std::vector<Vertex>> &batch);
// The opaque batches with full, a pipeline built on world.frag's full opaque shader: each batch
// that needs none of the screen, liquid and blood code through its PLAIN twin (draw.cpp).
void drawOpaque(SDL_GPUCommandBuffer *command,SDL_GPURenderPass *pass,SDL_GPUGraphicsPipeline *full,SDL_GPUTexture *reflection);
// The end of a timed piece inside a render pass (gpu_pieces.cpp): the pass
// ended, its command buffer submitted and waited for, and the pass begun
// again on a new one, which command and pass then hold.
using PieceCut=std::function<void(int piece,SDL_GPUCommandBuffer *&command,SDL_GPURenderPass *&pass)>;
// cut (the main view's, while timed piece by piece): ends the opaque world's
// piece and the sky's.
// A dev view of the sky alone, no world (DERP_SKYONLY, devtools builds).
bool skyOnlyView();
void drawWorld(SDL_GPUCommandBuffer *command,SDL_GPURenderPass *pass,SDL_GPUGraphicsPipeline *opaque,SDL_GPUGraphicsPipeline *skySurface,
               SDL_GPUGraphicsPipeline *sky,SDL_GPUGraphicsPipeline *world,SDL_GPUTexture *reflection,
               SDL_GPUGraphicsPipeline *coverage=nullptr,SDL_GPUGraphicsPipeline *cutout=nullptr,bool sprites=true,
               const PieceCut *cut=nullptr);
void drawOverlays(SDL_GPUCommandBuffer *command,SDL_GPURenderPass *pass,const FrameView &view,
                  const std::vector<SpriteDraw> &weapons,const std::vector<Vertex> &cross,bool depth);

// presentation.cpp: before each frame takes its swapchain image, the chosen
// present mode and frames in flight (F4 "Frame presentation") and the frame
// limit (F4 "Frame limit"), the adaptive mode's switching, profiling uncapped.
void presentationWait();

// hdr_output.cpp: F4 "HDR". The composition the swapchain should have now (SDR unless HDR is
// on and the display takes it); told when one was applied, it switches swapchainFormat and the
// output pipelines; and the present and panel passes' encoding (hdr_output.glsl's hdr), SDR for
// screenshots.
SDL_GPUSwapchainComposition hdrWanted();
void hdrApplied(SDL_GPUSwapchainComposition composition,bool worked);
void hdrUniform(float hdr[4],bool sdr=false);
// Whether the view should be drawn in float, keeping light past white: the display in HDR now
// and F4 "HDR in the scene" on.
bool hdrSceneWanted();

// frame.cpp
void releaseReflection();
// A 640x400 panel image (settings, loading) at a whole-pixel scale in the
// middle of the drawable, as a quad over all of it; origin and scale in drawable pixels.
std::vector<Vertex> panelQuad(Uint32 drawableWidth,Uint32 drawableHeight,float origin[2],float &scale);

// gpu_pieces.cpp: piece timing (I_Render3DProfilePieces, F4's GPU times).
// piecesBegin, once the scene is built, says whether this frame is timed
// piece by piece. While it is, pieceDone submits command (after the pending
// texture uploads), waits for it, adds the wait to piece and puts a new
// command buffer in its place; outside it, it does nothing. A new buffer has
// no uniforms: whatever draws in it pushes its own again. piecesEnd submits
// the buffer the last piece left and puts the times in profile.frame;
// piecePresented adds the present's, timed on the swapchain's buffer.
bool piecesBegin();
bool piecesOn();
void pieceDone(SDL_GPUCommandBuffer *&command,int piece);
void piecesEnd(SDL_GPUCommandBuffer *command,uint64_t frameStart);
void piecePresented(double presentMs);

// loading.cpp
void showLoading(const char *title,const char *stage,float fraction);
void releaseLoading();

inline SDL_GPUTexture *gpu(const GpuTextureRef &texture) {return texture?texture->texture:nullptr;}
template<class T> inline void release(T *&object,void (*destroy)(SDL_GPUDevice*,T*)) {if(object&&device)destroy(device,object);object=nullptr;}
inline void release(SDL_GPUTexture *&texture) {release(texture,SDL_ReleaseGPUTexture);}
inline void release(SDL_GPUGraphicsPipeline *&pipeline) {release(pipeline,SDL_ReleaseGPUGraphicsPipeline);}
} // namespace gpu3d
#endif
