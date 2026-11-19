/* The bake map on the graphics card (PLAN-gpu-bakes.md): BakeMap flattened
   into one buffer of words for platform/shaders/compute/trace.glsl, rays and
   samples traced there in slices, and the switch that says whether a bake
   runs there at all. The tracer on the processor (baked_lighting.h) stays the
   reference and the fallback. */
#pragma once
#include "internal.h"
#include "../gpu_compute.h"
#include <functional>

namespace doom3d {
struct OpenDoors;
// A ray as trace_rays.comp reads it: origin, the sector it starts in (-1 for
// none) with the gpuRay flags above bit 24, unit direction, length.
struct GpuRay { float x,y,z; uint32_t sectorFlags; float dx,dy,dz,length; };
// What it met: open (bit 0), skyward (bit 1), BakeHit kind (bits 4-7) and
// part (bits 8-9); the sector it was in; the line; how far along it stopped.
struct GpuRayResult { uint32_t flags; int32_t sector,line; float at; };
constexpr uint32_t gpuRayEscape=1,gpuRayDoors=2,gpuRayRoofless=4,gpuRayBefore=8;
inline uint32_t gpuSectorFlags(int sector,uint32_t flags) {return (sector<0?0xFFFFFFu:(uint32_t)sector&0xFFFFFFu)|flags<<24;}
// The BakeHit a result stands for, as traceRay would have filled it.
inline BakeHit gpuHit(const GpuRay &ray,const GpuRayResult &r) {
    BakeHit hit;
    hit.kind=(BakeHit::Kind)((r.flags>>4)&15);hit.sector=r.sector;hit.line=r.line;hit.part=(int)((r.flags>>8)&3);
    if(hit.kind!=BakeHit::none) {hit.x=ray.x+ray.dx*r.at;hit.y=ray.y+ray.dy*r.at;hit.z=ray.z+ray.dz*r.at;}
    else hit.sector=-1,hit.line=-1;
    return hit;
}

// A sample a bake asks for: a place on a surface, its sector, the surface's
// normal and the bake's seed for it; wall for samples on wall strips.
struct BakeSample { float x,y,z; int sector; float normal[3]; uint32_t seed; bool wall; };

// gpu_bakes.cpp. Sky light and occlusion (bakeAmbient): per sample the rays
// that reach the sky (a wall's counted twice) and the openness, each over the
// side*side rays; false when the card could not (the caller bakes here).
bool gpuAmbient(const std::vector<BakeSample> &samples,int side,float reach,const OpenDoors &doors,std::vector<std::array<float,2>> &values);
// Sun visibility (sunVisibility, its rays spread by spread) at points (x, y,
// z, sector): the share of the rays that reach the sky, 0 to 255, per point.
bool gpuSunVisibility(const std::vector<std::array<float,4>> &points,const float direction[3],float spread,std::vector<uint8_t> &open,
                      const BakeMap &map=bakeMap);

// The bounce on the card (bake_bounce.cpp's BounceSolve): its samples (seed:
// the pattern's turn, 0 to 1023) with their rays traced once and kept there,
// as many as fit gpuBounceBudget (the rest are traced again each pass), then
// a pass's gather at a time. table: 1024 turns of rays (a, b, c) in the
// hemisphere's frame. Null when the card could not.
struct GpuBounce;
std::shared_ptr<GpuBounce> gpuBounceStart(const BakeMap &map,const std::vector<BakeSample> &samples,int rays,const std::vector<std::array<float,3>> &table,
                                          float reach,bool skyDome,const OpenDoors &doors);
size_t gpuBounceSamples(const GpuBounce &bounce);
// What a pass reads, as the solve has it.
struct GpuGatherPass {
    int pass=0,passes=1;
    bool lights=false,skyDome=false,adapts=false,split=false,settles=false,wets=false,weather=false;
    bool trueLamps=false;   // the lamps' light as baked, without fillHeadroom (whole-level lighting)
    float sunEnergy[3]={},lies[3]={},skyRadiance[3]={},skyGlow=0,day=1,skyGain=1,most=0,sunGain=1;
    const std::vector<std::array<float,3>> *sideAlbedo=nullptr,*flatAlbedo=nullptr;
    const std::vector<float> *level=nullptr;
    const std::vector<uint8_t> *ground=nullptr;   // a byte a cell, when weather
    const uint8_t *flatCells=nullptr,*wallCells=nullptr,*contact=nullptr;
    // The pass before's bounce in half floats (bounce_carry.h), read in place of the 8-bit layers;
    // null: those. carryWalls: where its walls start, in entries of three halves.
    const std::vector<uint16_t> *carry=nullptr;
    size_t carryWalls=0;
};
// Per sample: its light (rgb) and its split over the basis, as the gather returns them.
bool gpuBounceGather(GpuBounce &bounce,const GpuGatherPass &pass,std::vector<std::array<float,6>> &values);

// gpu_lights.cpp. The lamps' light (gatherStatic) for items: where each one's
// light, direction texel and lobes go (direction and kept may be null).
struct StaticTarget { uint8_t *out,*direction; std::array<uint16_t,3> *kept; };
// The items lightCell and lightStrip bake, in their order.
void staticItemsOfCell(const BakeMap &map,size_t cell,bool only,std::vector<BakeSample> &items,std::vector<StaticTarget> &targets);
void staticItemsOfStrip(const BakeStrip &strip,bool only,std::vector<BakeSample> &items,std::vector<StaticTarget> &targets);
// False when the card could not (the caller bakes them here, over whatever was written). At
// load a sample of the items is baked on the processor too: if the card's light disagrees
// (a driver that compiles lights.comp wrongly), false, and the lamps' light stays on the
// processor for the rest of the run, re-bakes in play included.
bool gpuGatherStatic(const BakeMap &map,const std::vector<BakeSample> &items,const std::vector<StaticTarget> &targets,bool directed);
// bake_lights.cpp: one item's lamp light (rgb, as the bake stores it) baked on the processor.
void gatherStaticAt(const BakeMap &map,const BakeSample &item,uint8_t out[3]);
// In play (relight.cpp): what a moved door changes (gatherMoved: rgb and whether any lamp's way
// crossed it), and what changed lamps change (gatherChanged: rgb, what each flicker group gains,
// whether any did).
bool gpuGatherMoved(const BakeMap &before,const BakeMap &now,const std::vector<std::array<float,4>> &boxes,const std::vector<BakeSample> &items,
                    std::vector<std::array<float,4>> &deltas);
bool gpuGatherChanged(const BakeMap &map,const ChangedLamps &lamps,const std::vector<BakeSample> &items,std::vector<std::array<float,20>> &changes);
// The calling thread's buffers let go (the renderer shuts down).
void gpuTraceShutdown();
void gpuLightsShutdown();

// Whether re-bakes in play (relight.cpp, the bounce's solves round them) run on the
// graphics card: settings.gpuRelight, auto off on a Steam Deck; DERP_GPURELIGHT=0/1 overrides.
bool gpuRelightOn();
// Whether the bakes run on the graphics card: a device that runs compute, and
// F4 "Bakes on" set to it (settings.gpuBakes). DERP_GPUBAKES=0 or 1 overrides.
bool gpuBakesOn();
// The map words for the kernels' mapA and mapB, uploaded: bakeMap as it is
// now, with doors (when given) as OpenDoors raises them and roofless
// sectors. False when the card cannot take them. Cheap enough to call at the
// start of every bake: the map is a few hundred kilobytes.
struct GpuMapUniforms { uint32_t mapA[4],mapB[4]; };
// target: a buffer of the caller's own (made or grown as needed) instead of the
// thread's. before: the same map with other heights (a door before it moved),
// which rays with gpuRayBefore see.
bool gpuUploadMap(const BakeMap &map,const OpenDoors *doors,const std::vector<char> *roofless,GpuMapUniforms &uniforms,
                  gpucompute::Buffer **target=nullptr,const BakeMap *before=nullptr);
gpucompute::Buffer *gpuMapBuffer();
// Runs count items through a kernel in slices that each take the card a few
// tens of milliseconds (Windows resets a driver that stays away two
// seconds; the loading screen goes on drawing between them). run(first,n)
// records the slice's dispatch and downloads; the slice is then submitted
// and waited for. The size adapts to how long the last one took. False when
// the work failed (the caller bakes on the processor instead). most: the
// largest slice the caller's result buffer holds.
bool gpuSliced(size_t count,size_t firstSlice,size_t most,const std::function<void(size_t first,size_t n)> &run,const char *what);
// Rays one by one (trace_rays.comp).
bool gpuTraceRays(const std::vector<GpuRay> &rays,std::vector<GpuRayResult> &results,const OpenDoors *doors=nullptr,
                  const BakeMap &map=bakeMap);
// The trace check (PLAN-gpu-bakes.md, stage 1): DERP_GPUTRACECHECK=<file>
// traces a few million rays from the level's own sample places on both sides
// after a level's bakes, and appends agreement and rays a second to the file.
void gpuTraceCheck();
} // namespace doom3d
