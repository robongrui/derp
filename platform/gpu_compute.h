/* Compute work on the graphics card (PLAN-gpu-bakes.md): storage buffers, a
   kernel run over them, results read back. The bakes use it through
   scene3d/gpu_trace.h; this layer knows nothing of the map. Implemented by the
   SDL_gpu backend in gpu3d/compute.cpp.

   Work is recorded into one command buffer at a time: uploads, dispatches and
   downloads in the order they are asked for, sent off by submit (or finish,
   which also waits). A download's bytes land in its destination only once the
   work it belongs to is done. Each thread records its own work; buffers may
   be shared only by work that is not in flight at the same time. */
#ifndef DOOM_GPU_COMPUTE_H
#define DOOM_GPU_COMPUTE_H
#include <cstddef>
#include <cstdint>
#include <initializer_list>

namespace gpucompute {
// The kernels in platform/shaders/compute (compiled with the build into
// <build>/gpu-shaders/gpu_compute_shaders.h: scripts/gpu_shaders.cmake).
enum class Kernel { Fill, TraceRays, Ambient, Sun, BounceTrace, BounceGather, Lights, Count };
struct Buffer;
// A device that runs compute shaders: false in the software renderer, or
// when a kernel would not build (then the bakes stay on the processor).
bool available();
// The backend's name, for the bake cache's key ("vulkan", "metal").
const char *backend();
// A storage buffer the kernels read and write; nullptr when it cannot be made.
Buffer *create(size_t bytes);
void destroy(Buffer *&buffer);
size_t size(const Buffer *buffer);
// Recorded into the open work, in order.
void upload(Buffer *to,const void *data,size_t bytes,size_t offset=0);
// groups: workgroups along x (groupSize threads each). readOnly and readWrite in
// the kernel's binding order; uniforms go to its one uniform block.
void dispatch(Kernel kernel,std::initializer_list<Buffer*> readOnly,std::initializer_list<Buffer*> readWrite,
              const void *uniforms,uint32_t uniformBytes,uint32_t groups);
void download(Buffer *from,void *to,size_t bytes,size_t offset=0);
uint32_t groupSize(Kernel kernel);
// The open work sent off; the ticket to wait on (0 when nothing was recorded or it failed).
uint64_t submit();
// An empty submission (with whatever was recorded before it): done once the card has reached it, so
// work sent right after it costs the time from this one's being done to its own (gpuSliced's play
// slices: the time from sending includes the wait behind the frames queued ahead).
uint64_t mark();
// Whether that work is done (and its downloads landed); wait blocks until it is.
bool done(uint64_t ticket);
bool wait(uint64_t ticket);
// submit and wait: false if the work could not be run.
bool finish();
// Everything let go (the renderer shuts down).
void shutdown();
}
#endif
