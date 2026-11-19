/* Compute on the SDL_gpu device (gpu_compute.h): the kernels' pipelines,
   made the first time one is asked for, storage buffers, and the work
   recorded into a command buffer of its own, sent off with a fence. */
#include "internal.h"
#include "../gpu_compute.h"
#include "gpu_compute_shaders.h"   // <build>/gpu-shaders (scripts/gpu_shaders.cmake)
#include <atomic>
#include <mutex>

struct gpucompute::Buffer { SDL_GPUBuffer *buffer=nullptr; size_t bytes=0; Uint32 capacity=0; };

namespace gpucompute {
using gpu3d::device;
using gpu3d::shaderFormat;
namespace {
struct KernelInfo {
    const char *name; const unsigned char *spirv; size_t spirvSize; const unsigned char *msl; size_t mslSize;
    Uint32 readOnly,readWrite,uniforms,threads;
};
#define KERNEL(name,ro,rw,threads) KernelInfo{#name,doom_spv_##name,sizeof(doom_spv_##name),doom_msl_##name,sizeof(doom_msl_##name),ro,rw,1,threads}
const KernelInfo kernels[(int)Kernel::Count]={
    KERNEL(fill_comp,0,1,64),
    KERNEL(trace_rays_comp,2,1,64),
    KERNEL(ambient_comp,3,1,64),
    KERNEL(sun_comp,2,1,64),
    KERNEL(bounce_trace_comp,4,1,64),
    KERNEL(bounce_gather_comp,7,1,64),
    KERNEL(lights_comp,3,1,64),
};
#undef KERNEL
SDL_GPUComputePipeline *pipelines[(int)Kernel::Count];
bool tried[(int)Kernel::Count];
std::mutex lock;   // the pipelines only: everything else is the calling thread's own
std::atomic<int> usable{-1};   // available(): -1 not asked yet
std::atomic<uint64_t> nextTicket{1};
// Each thread's own work (the load's bakes on the game's thread, the re-bakes in play on the
// light worker): the open command buffer, its transfers, the work in flight, and the transfer
// buffers kept for reuse, so a re-bake every few tics allocates none. Nothing here waits on
// another thread, and a thread waiting on its fences holds no lock the frame needs.
struct Transfer { SDL_GPUTransferBuffer *buffer; Uint32 bytes; bool upload; };
struct Pending { Transfer transfer; void *to; size_t bytes; };
struct Flight { uint64_t ticket; SDL_GPUFence *fence; std::vector<Pending> downloads; std::vector<Transfer> uploads; };
struct Work {
    SDL_GPUCommandBuffer *command=nullptr;
    std::vector<Pending> downloads;
    std::vector<Transfer> uploads,spare;
    std::vector<Flight> flights;
    // Storage buffers let go, kept for the next of their size: freeing one makes SDL defragment its
    // memory in the next submission, which is the frame's own when the light worker freed it.
    std::vector<std::pair<SDL_GPUBuffer*,Uint32>> buffers;
    ~Work() {
        if(!device)return;
        for(const Transfer &t:spare)SDL_ReleaseGPUTransferBuffer(device,t.buffer);
        for(const auto &b:buffers)SDL_ReleaseGPUBuffer(device,b.first);
        spare.clear();buffers.clear();
    }
};
thread_local Work work;
constexpr Uint32 spareMost=256u<<20;   // transfer bytes kept for reuse a thread
constexpr Uint32 keptBufferMost=64u<<20,keptBuffersMost=256u<<20;   // storage buffers kept: each, and in all

Transfer takeTransfer(Uint32 bytes,bool upload) {
    size_t best=SIZE_MAX;
    for(size_t k=0;k<work.spare.size();++k) {
        const Transfer &t=work.spare[k];
        if(t.upload==upload&&t.bytes>=bytes&&(best==SIZE_MAX||t.bytes<work.spare[best].bytes))best=k;
    }
    if(best!=SIZE_MAX) {Transfer t=work.spare[best];work.spare.erase(work.spare.begin()+best);return t;}
    Uint32 size=64u<<10;
    while(size<bytes&&size<(1u<<31))size<<=1;
    if(size<bytes)size=bytes;
    SDL_GPUTransferBufferCreateInfo info={upload?SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD:SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD,size,0};
    SDL_GPUTransferBuffer *buffer=SDL_CreateGPUTransferBuffer(device,&info);
    if(!buffer)fprintf(stderr,"GPU compute: no transfer of %u bytes: %s\n",bytes,SDL_GetError());
    return {buffer,buffer?size:0,upload};
}
// A transfer whose work is done, back for the next; the largest let go past spareMost.
void giveTransfer(const Transfer &t) {
    if(!t.buffer)return;
    work.spare.push_back(t);
    Uint64 total=0;
    for(const Transfer &k:work.spare)total+=k.bytes;
    while(total>spareMost&&!work.spare.empty()) {
        auto largest=std::max_element(work.spare.begin(),work.spare.end(),[](const Transfer &a,const Transfer &b){return a.bytes<b.bytes;});
        total-=largest->bytes;
        SDL_ReleaseGPUTransferBuffer(device,largest->buffer);
        work.spare.erase(largest);
    }
}

SDL_GPUComputePipeline *pipeline(Kernel kernel) {
    std::lock_guard<std::mutex> hold(lock);
    int k=(int)kernel;
    if(tried[k])return pipelines[k];
    tried[k]=true;
    const KernelInfo &info=kernels[k];
    bool msl=shaderFormat==SDL_GPU_SHADERFORMAT_MSL;
    SDL_GPUComputePipelineCreateInfo create={};
    create.code=msl?info.msl:info.spirv;create.code_size=msl?info.mslSize:info.spirvSize;
    create.entrypoint=msl?"main0":"main";create.format=shaderFormat;
    create.num_readonly_storage_buffers=info.readOnly;create.num_readwrite_storage_buffers=info.readWrite;
    create.num_uniform_buffers=info.uniforms;
    create.threadcount_x=info.threads;create.threadcount_y=1;create.threadcount_z=1;
    pipelines[k]=SDL_CreateGPUComputePipeline(device,&create);
    if(!pipelines[k])fprintf(stderr,"GPU compute: kernel %s would not build: %s\n",info.name,SDL_GetError());
    return pipelines[k];
}
SDL_GPUCommandBuffer *open() {
    if(!work.command)work.command=SDL_AcquireGPUCommandBuffer(device);
    return work.command;
}
void land(Flight &f) {
    for(Pending &p:f.downloads) {
        if(void *mapped=SDL_MapGPUTransferBuffer(device,p.transfer.buffer,false)) {
            std::memcpy(p.to,mapped,p.bytes);
            SDL_UnmapGPUTransferBuffer(device,p.transfer.buffer);
        }
        giveTransfer(p.transfer);
    }
    for(const Transfer &t:f.uploads)giveTransfer(t);
    f.downloads.clear();f.uploads.clear();
}
} // namespace

bool available() {
    int known=usable.load();
    if(known>=0)return known==1&&device;
    if(!device)return false;
    // The tracer is what every bake needs: without it nothing runs here.
    bool ok=pipeline(Kernel::TraceRays)!=nullptr;
    usable=ok?1:0;
    return ok;
}
const char *backend() {return device?SDL_GetGPUDeviceDriver(device):"none";}
Buffer *create(size_t bytes) {
    if(!device||bytes==0||bytes>0xFFFFFFFFu)return nullptr;
    Uint32 size=(Uint32)((bytes+15)&~(size_t)15);
    // One kept of this size or a little larger (at most twice).
    size_t best=SIZE_MAX;
    for(size_t k=0;k<work.buffers.size();++k) {
        Uint32 have=work.buffers[k].second;
        if(have>=size&&have/2<=size&&(best==SIZE_MAX||have<work.buffers[best].second))best=k;
    }
    if(best!=SIZE_MAX) {
        auto kept=work.buffers[best];work.buffers.erase(work.buffers.begin()+best);
        return new Buffer{kept.first,bytes,kept.second};
    }
    // Small ones rounded up, so they are found again.
    if(size<=keptBufferMost) {Uint32 rounded=4096;while(rounded<size)rounded<<=1;size=rounded;}
    SDL_GPUBufferCreateInfo info={};
    info.usage=SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ|SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE;
    info.size=size;
    SDL_GPUBuffer *buffer=SDL_CreateGPUBuffer(device,&info);
    if(!buffer) {fprintf(stderr,"GPU compute: no buffer of %zu bytes: %s\n",bytes,SDL_GetError());return nullptr;}
    return new Buffer{buffer,bytes,size};
}
void destroy(Buffer *&buffer) {
    if(buffer&&device) {
        Uint64 total=buffer->capacity;
        for(const auto &b:work.buffers)total+=b.second;
        if(buffer->capacity<=keptBufferMost&&total<=keptBuffersMost)work.buffers.push_back({buffer->buffer,buffer->capacity});
        else SDL_ReleaseGPUBuffer(device,buffer->buffer);
    }
    delete buffer;buffer=nullptr;
}
size_t size(const Buffer *buffer) {return buffer?buffer->bytes:0;}
void upload(Buffer *to,const void *data,size_t bytes,size_t offset) {
    if(!to||!bytes||offset+bytes>to->bytes||!open())return;
    Transfer t=takeTransfer((Uint32)bytes,true);
    if(!t.buffer)return;
    if(void *mapped=SDL_MapGPUTransferBuffer(device,t.buffer,false)) {
        std::memcpy(mapped,data,bytes);
        SDL_UnmapGPUTransferBuffer(device,t.buffer);
        SDL_GPUCopyPass *copy=SDL_BeginGPUCopyPass(work.command);
        SDL_GPUTransferBufferLocation source={t.buffer,0};
        SDL_GPUBufferRegion target={to->buffer,(Uint32)offset,(Uint32)bytes};
        SDL_UploadToGPUBuffer(copy,&source,&target,false);
        SDL_EndGPUCopyPass(copy);
    }
    work.uploads.push_back(t);   // reused once this work is done
}
void dispatch(Kernel kernel,std::initializer_list<Buffer*> readOnly,std::initializer_list<Buffer*> readWrite,
              const void *uniforms,uint32_t uniformBytes,uint32_t groups) {
    SDL_GPUComputePipeline *p=pipeline(kernel);
    if(!p||!groups||!open())return;
    SDL_GPUStorageBufferReadWriteBinding written[8]={};
    Uint32 writes=0;
    for(Buffer *b:readWrite)if(writes<8)written[writes++].buffer=b?b->buffer:nullptr;
    SDL_GPUBuffer *read[8]={};
    Uint32 reads=0;
    for(Buffer *b:readOnly)if(reads<8)read[reads++]=b?b->buffer:nullptr;
    SDL_GPUComputePass *pass=SDL_BeginGPUComputePass(work.command,nullptr,0,written,writes);
    SDL_BindGPUComputePipeline(pass,p);
    if(reads)SDL_BindGPUComputeStorageBuffers(pass,0,read,reads);
    if(uniforms&&uniformBytes)SDL_PushGPUComputeUniformData(work.command,0,uniforms,uniformBytes);
    SDL_DispatchGPUCompute(pass,groups,1,1);
    SDL_EndGPUComputePass(pass);
}
void download(Buffer *from,void *to,size_t bytes,size_t offset) {
    if(!from||!bytes||offset+bytes>from->bytes||!open())return;
    Transfer t=takeTransfer((Uint32)bytes,false);
    if(!t.buffer)return;
    SDL_GPUCopyPass *copy=SDL_BeginGPUCopyPass(work.command);
    SDL_GPUBufferRegion source={from->buffer,(Uint32)offset,(Uint32)bytes};
    SDL_GPUTransferBufferLocation target={t.buffer,0};
    SDL_DownloadFromGPUBuffer(copy,&source,&target);
    SDL_EndGPUCopyPass(copy);
    work.downloads.push_back({t,to,bytes});
}
uint32_t groupSize(Kernel kernel) {return kernels[(int)kernel].threads;}
uint64_t submit() {
    if(!work.command)return 0;
    SDL_GPUFence *fence=SDL_SubmitGPUCommandBufferAndAcquireFence(work.command);
    work.command=nullptr;
    if(!fence) {
        fprintf(stderr,"GPU compute: the work was not submitted: %s\n",SDL_GetError());
        for(Pending &p:work.downloads)giveTransfer(p.transfer);
        for(const Transfer &t:work.uploads)giveTransfer(t);
        work.downloads.clear();work.uploads.clear();
        return 0;
    }
    uint64_t ticket=nextTicket++;
    work.flights.push_back({ticket,fence,std::move(work.downloads),std::move(work.uploads)});
    work.downloads.clear();work.uploads.clear();
    return ticket;
}
uint64_t mark() {return open()?submit():0;}
static bool settle(uint64_t ticket,bool block) {
    for(size_t n=0;n<work.flights.size();++n) {
        Flight &f=work.flights[n];
        if(f.ticket!=ticket)continue;
        if(block)SDL_WaitForGPUFences(device,true,&f.fence,1);
        else if(!SDL_QueryGPUFence(device,f.fence))return false;
        land(f);
        SDL_ReleaseGPUFence(device,f.fence);
        work.flights.erase(work.flights.begin()+n);
        return true;
    }
    return true;   // unknown or already settled
}
bool done(uint64_t ticket) {return !device||settle(ticket,false);}
bool wait(uint64_t ticket) {return !device||settle(ticket,true);}
bool finish() {
    if(!work.command)return true;
    uint64_t ticket=submit();
    return ticket&&wait(ticket);
}
void shutdown() {
    if(!device)return;
    if(work.command) {SDL_CancelGPUCommandBuffer(work.command);work.command=nullptr;}
    for(Pending &p:work.downloads)giveTransfer(p.transfer);
    for(const Transfer &t:work.uploads)giveTransfer(t);
    work.downloads.clear();work.uploads.clear();
    while(!work.flights.empty())settle(work.flights.front().ticket,true);
    for(const Transfer &t:work.spare)SDL_ReleaseGPUTransferBuffer(device,t.buffer);
    work.spare.clear();
    for(const auto &b:work.buffers)SDL_ReleaseGPUBuffer(device,b.first);
    work.buffers.clear();
    std::lock_guard<std::mutex> hold(lock);
    for(int k=0;k<(int)Kernel::Count;++k) {
        if(pipelines[k])SDL_ReleaseGPUComputePipeline(device,pipelines[k]);
        pipelines[k]=nullptr;tried[k]=false;
    }
    usable=-1;
}
} // namespace gpucompute
