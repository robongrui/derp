/* The level's bakes on the graphics card (PLAN-gpu-bakes.md, stages 2 and
   3): the kernels in platform/shaders/compute trace what the bakes ask; what
   is around the loops (which samples, smoothing, blending, encoding) stays in
   the bakes' own files. */
#include "gpu_trace.h"
#include "open_doors.h"

namespace doom3d {
namespace {
uint32_t f2u(float f) {uint32_t u;std::memcpy(&u,&f,4);return u;}
// A sample as the kernels read it: x, y, z, sector | flags << 24; normal, seed.
struct GpuSample { uint32_t a[4],b[4]; };
GpuSample gpuSample(const BakeSample &s,uint32_t flags) {
    return {{f2u(s.x),f2u(s.y),f2u(s.z),gpuSectorFlags(s.sector,flags)},{f2u(s.normal[0]),f2u(s.normal[1]),f2u(s.normal[2]),s.seed}};
}
uint32_t doorFlag(const OpenDoors &doors,int sector) {
    return doors.any&&sector>=0&&sector<(int)doors.door.size()&&doors.door[sector]?gpuRayDoors:0;
}
// Per-sample kernels' uniforms: the map, then the job.
struct Job { uint32_t mapA[4],mapB[4],job[4]; };
Job job(const GpuMapUniforms &map) {Job j={};std::memcpy(j.mapA,map.mapA,16);std::memcpy(j.mapB,map.mapB,16);return j;}
} // namespace

bool gpuAmbient(const std::vector<BakeSample> &samples,int side,float reach,const OpenDoors &doors,std::vector<std::array<float,2>> &values) {
    values.assign(samples.size(),{0,1});
    if(samples.empty())return true;
    GpuMapUniforms map;
    if(!gpuUploadMap(bakeMap,&doors,nullptr,map))return false;
    // The rays in the hemisphere's frame, as cosineDirection makes them.
    const int rays=side*side;
    std::vector<std::array<float,4>> pattern(rays);
    for(int n=0;n<rays;++n) {
        float u=((n%side)+0.5f)/side,v=((n/side)+0.5f)/side;
        float radius=std::sqrt(u),angle=6.28318531f*v;
        pattern[n]={radius*std::cos(angle),radius*std::sin(angle),std::sqrt(std::max(0.0f,1-u)),0};
    }
    std::vector<GpuSample> up(samples.size());
    for(size_t k=0;k<samples.size();++k)up[k]=gpuSample(samples[k],gpuRayEscape|doorFlag(doors,samples[k].sector));
    constexpr size_t most=1u<<18;
    gpucompute::Buffer *in=gpucompute::create(up.size()*sizeof(GpuSample)),*rayBuffer=gpucompute::create(pattern.size()*16);
    gpucompute::Buffer *out=gpucompute::create(std::min(samples.size(),most)*8);
    std::vector<std::array<float,2>> got(samples.size());
    bool ok=in&&rayBuffer&&out;
    if(ok) {
        gpucompute::upload(in,up.data(),up.size()*sizeof(GpuSample));
        gpucompute::upload(rayBuffer,pattern.data(),pattern.size()*16);
        const uint32_t group=gpucompute::groupSize(gpucompute::Kernel::Ambient);
        ok=gpuSliced(samples.size(),4096,most,[&](size_t first,size_t n) {
            Job j=job(map);
            j.job[0]=(uint32_t)first;j.job[1]=(uint32_t)n;j.job[2]=(uint32_t)rays;j.job[3]=f2u(reach);
            gpucompute::dispatch(gpucompute::Kernel::Ambient,{gpuMapBuffer(),in,rayBuffer},{out},&j,sizeof(j),(uint32_t)((n+group-1)/group));
            gpucompute::download(out,&got[first],n*8);
        },"sky light");
    }
    gpucompute::destroy(in);gpucompute::destroy(rayBuffer);gpucompute::destroy(out);
    if(!ok)return false;
    for(size_t k=0;k<samples.size();++k) {
        values[k]={got[k][0]/rays,got[k][1]/rays};
        if(samples[k].wall)values[k][0]*=2;
    }
    return true;
}

bool gpuSunVisibility(const std::vector<std::array<float,4>> &points,const float direction[3],float spread,std::vector<uint8_t> &open,const BakeMap &onMap) {
    open.assign(points.size(),0);
    if(points.empty())return true;
    GpuMapUniforms map;
    if(!gpuUploadMap(onMap,nullptr,nullptr,map))return false;
    struct { Job j; float rays[sunRaysMost][4]; } u={};
    u.j=job(map);
    // sunVisibility's rays.
    float rays[sunRaysMost][3];
    int count=sunRays(direction,spread,rays);
    for(int k=0;k<count;++k)for(int c=0;c<3;++c)u.rays[k][c]=rays[k][c];
    u.j.job[2]=(uint32_t)count;
    std::vector<std::array<uint32_t,4>> up(points.size());
    for(size_t k=0;k<points.size();++k)
        up[k]={f2u(points[k][0]),f2u(points[k][1]),f2u(points[k][2]),gpuSectorFlags((int)points[k][3],gpuRayEscape)};
    constexpr size_t most=1u<<20;
    gpucompute::Buffer *in=gpucompute::create(up.size()*16),*out=gpucompute::create(std::min(points.size(),most)*4);
    std::vector<uint32_t> got(points.size());
    bool ok=in&&out;
    if(ok) {
        gpucompute::upload(in,up.data(),up.size()*16);
        const uint32_t group=gpucompute::groupSize(gpucompute::Kernel::Sun);
        ok=gpuSliced(points.size(),loadingOnThisThread()?1u<<16:4096,most,[&](size_t first,size_t n) {
            u.j.job[0]=(uint32_t)first;u.j.job[1]=(uint32_t)n;
            gpucompute::dispatch(gpucompute::Kernel::Sun,{gpuMapBuffer(),in},{out},&u,sizeof(u),(uint32_t)((n+group-1)/group));
            gpucompute::download(out,&got[first],n*4);
        },"sunlight");
    }
    gpucompute::destroy(in);gpucompute::destroy(out);
    if(!ok)return false;
    for(size_t k=0;k<points.size();++k)open[k]=(uint8_t)std::lround(std::min<uint32_t>(got[k],(uint32_t)count)*255.0/count);
    return true;
}
struct GpuBounce {
    GpuMapUniforms map;
    size_t samples=0,kept=0;int rays=0;bool skyDome=false;float reach=0;
    gpucompute::Buffer *mapBuffer=nullptr,*sampleBuffer=nullptr,*tableBuffer=nullptr,*stripBuffer=nullptr,*hits=nullptr,*statics=nullptr,*light=nullptr,*out=nullptr;
    std::vector<uint32_t> staticWords,lightWords;
    ~GpuBounce() {
        for(gpucompute::Buffer **b:{&mapBuffer,&sampleBuffer,&tableBuffer,&stripBuffer,&hits,&statics,&light,&out})gpucompute::destroy(*b);
    }
};
namespace {
// What the card keeps of the rays: 8 bytes a ray. Past it, samples are traced again every pass.
size_t bounceBudget() {
    const char *e=getenv("DERP_GPUBOUNCEMB");
    return (e&&*e?(size_t)atoll(e):1024)<<20;
}
// The uniforms the bounce kernels share: the map, the job, the grid, the sizes.
struct BounceJob { Job j; float grid[4]; uint32_t dims[4]; };
BounceJob bounceJob(const GpuBounce &b) {
    BounceJob u={};
    u.j=job(b.map);
    u.grid[0]=mapOrigin[0];u.grid[1]=mapOrigin[1];u.grid[2]=mapCell;u.grid[3]=b.reach;
    u.dims[0]=(uint32_t)mapWidth;u.dims[1]=(uint32_t)mapHeight;u.dims[2]=(uint32_t)atlasStride;u.dims[3]=(uint32_t)atlasWidth;
    return u;
}
bool grow(gpucompute::Buffer *&buffer,size_t bytes) {
    if(buffer&&gpucompute::size(buffer)>=bytes)return true;
    gpucompute::destroy(buffer);
    buffer=gpucompute::create(bytes+bytes/8);
    return buffer!=nullptr;
}
} // namespace

size_t gpuBounceSamples(const GpuBounce &bounce) {return bounce.samples;}

std::shared_ptr<GpuBounce> gpuBounceStart(const BakeMap &onMap,const std::vector<BakeSample> &samples,int rays,const std::vector<std::array<float,3>> &table,
                                          float reach,bool skyDome,const OpenDoors &doors) {
    if(samples.empty()||table.size()!=(size_t)1024*rays)return nullptr;
    auto b=std::make_shared<GpuBounce>();
    b->samples=samples.size();b->rays=rays;b->skyDome=skyDome;b->reach=reach;
    if(!gpuUploadMap(onMap,&doors,nullptr,b->map,&b->mapBuffer))return nullptr;
    std::vector<GpuSample> up(samples.size());
    for(size_t k=0;k<samples.size();++k)up[k]=gpuSample(samples[k],gpuRayEscape|doorFlag(doors,samples[k].sector));
    std::vector<std::array<float,4>> pattern(table.size());
    for(size_t k=0;k<table.size();++k)pattern[k]={table[k][0],table[k][1],table[k][2],0};
    // Every line side's strip (bakeStrips is indexed line*2+side), as the kernels read it.
    std::vector<std::array<float,4>> strips(bakeStrips.size()*3+1);
    for(size_t k=0;k<bakeStrips.size();++k) {
        const BakeStrip &s=bakeStrips[k];
        strips[3*k]={(float)s.x,(float)s.y,(float)s.width,(float)s.height};
        strips[3*k+1]={s.ax,s.ay,s.dx,s.dy};
        strips[3*k+2]={s.scale,s.low,0,0};
    }
    b->kept=std::min(samples.size(),bounceBudget()/(8*(size_t)rays));
    b->sampleBuffer=gpucompute::create(up.size()*sizeof(GpuSample));
    b->tableBuffer=gpucompute::create(pattern.size()*16);
    b->stripBuffer=gpucompute::create(strips.size()*16);
    b->hits=gpucompute::create(std::max<size_t>(1,b->kept)*rays*8);
    if(!b->sampleBuffer||!b->tableBuffer||!b->stripBuffer||!b->hits)return nullptr;
    gpucompute::upload(b->sampleBuffer,up.data(),up.size()*sizeof(GpuSample));
    gpucompute::upload(b->tableBuffer,pattern.data(),pattern.size()*16);
    gpucompute::upload(b->stripBuffer,strips.data(),strips.size()*16);
    if(!gpucompute::finish())return nullptr;
    // The rays of the samples kept, traced once.
    const uint32_t group=gpucompute::groupSize(gpucompute::Kernel::BounceTrace);
    struct { BounceJob u; uint32_t kept[4]; } u={};
    u.u=bounceJob(*b);
    u.kept[0]=(uint32_t)b->kept;
    bool ok=gpuSliced(b->kept,loadingOnThisThread()?2048:256,(size_t)1<<20,[&](size_t first,size_t n) {
        u.u.j.job[0]=(uint32_t)first;u.u.j.job[1]=(uint32_t)n;u.u.j.job[2]=(uint32_t)rays;u.u.j.job[3]=skyDome?1u:0u;
        gpucompute::dispatch(gpucompute::Kernel::BounceTrace,{b->mapBuffer,b->sampleBuffer,b->tableBuffer,b->stripBuffer},{b->hits},
                             &u,sizeof(u),(uint32_t)((n*rays+group-1)/group));
    },"the bounce's rays");
    if(!ok)return nullptr;
    return b;
}

bool gpuBounceGather(GpuBounce &b,const GpuGatherPass &p,std::vector<std::array<float,6>> &values) {
    values.assign(b.samples,{});
    if(!p.sideAlbedo||!p.flatAlbedo||!p.level||!p.flatCells||!p.wallCells||!p.contact)return false;
    const size_t layer=(size_t)mapWidth*mapHeight;
    // The statics: side and flat albedos, sector levels, the weather's ground.
    std::vector<uint32_t> &st=b.staticWords;st.clear();
    uint32_t offsets[4];
    auto rgbs=[&](const std::vector<std::array<float,3>> &from) {
        uint32_t at=(uint32_t)st.size();
        for(const auto &rgb:from)for(float c:rgb)st.push_back(f2u(c));
        return at;
    };
    offsets[0]=rgbs(*p.sideAlbedo);offsets[1]=rgbs(*p.flatAlbedo);
    offsets[2]=(uint32_t)st.size();
    for(float l:*p.level)st.push_back(f2u(l));
    offsets[3]=(uint32_t)st.size();
    if(p.weather&&p.ground&&p.ground->size()>=layer) {
        st.resize(st.size()+(layer+3)/4,0);
        for(size_t i=0;i<layer;++i)st[offsets[3]+i/4]|=(uint32_t)(*p.ground)[i]<<(8*(i%4));
    }
    st.push_back(0);
    // The light as the pass starts: the flat cells' four layers, the wall atlas, the contact map.
    const size_t wallWords=(size_t)atlasStride*atlasHeight;
    // And after them the pass before's bounce in half floats, two to a word (bounce_carry.h).
    const size_t carryWords=p.carry?p.carry->size()/2:0;
    const size_t lightCount=4*layer+wallWords+layer+carryWords;
    if(!grow(b.statics,st.size()*4)||!grow(b.light,lightCount*4)||!grow(b.out,b.samples*24))return false;
    gpucompute::upload(b.statics,st.data(),st.size()*4);
    gpucompute::upload(b.light,p.flatCells,4*layer*4,0);
    gpucompute::upload(b.light,p.wallCells,wallWords*4,4*layer*4);
    gpucompute::upload(b.light,p.contact,layer*4,(4*layer+wallWords)*4);
    if(carryWords)gpucompute::upload(b.light,p.carry->data(),carryWords*4,(5*layer+wallWords)*4);
    struct {
        Job j; uint32_t pass[4]; float grid[4]; uint32_t dims[4]; uint32_t statics[4]; uint32_t lights[4];
        float sunEnergy[4],lies[4],skyRadiance[4],more[4];
        uint32_t carry[4];
    } u={};
    if(carryWords) {u.carry[0]=(uint32_t)(5*layer+wallWords);u.carry[1]=(uint32_t)p.carryWalls;u.carry[2]=(uint32_t)atlasWidth;u.carry[3]=1;}
    BounceJob base=bounceJob(b);
    u.j=base.j;std::memcpy(u.grid,base.grid,16);std::memcpy(u.dims,base.dims,16);
    u.pass[0]=(uint32_t)p.pass;u.pass[1]=(uint32_t)p.passes;
    u.pass[2]=(p.lights?1u:0u)|(p.skyDome?2u:0u)|(p.adapts?4u:0u)|(p.split?8u:0u)|(p.settles?16u:0u)|(p.wets?32u:0u)|(p.weather?64u:0u)|(p.trueLamps?128u:0u);
    std::memcpy(u.statics,offsets,16);
    u.lights[0]=0;u.lights[1]=(uint32_t)(4*layer);u.lights[2]=(uint32_t)(4*layer+wallWords);u.lights[3]=(uint32_t)layer;
    for(int c=0;c<3;++c) {u.sunEnergy[c]=p.sunEnergy[c];u.lies[c]=p.lies[c];u.skyRadiance[c]=p.skyRadiance[c];}
    u.sunEnergy[3]=p.day;u.lies[3]=p.skyGain;u.skyRadiance[3]=p.skyGlow;u.more[0]=p.most;u.more[1]=p.sunGain;
    const uint32_t group=gpucompute::groupSize(gpucompute::Kernel::BounceGather);
    std::vector<float> got(b.samples*6);
    bool ok=gpuSliced(b.samples,loadingOnThisThread()?8192:1024,b.samples,[&](size_t first,size_t n) {
        u.j.job[0]=(uint32_t)first;u.j.job[1]=(uint32_t)n;u.j.job[2]=(uint32_t)b.rays;u.j.job[3]=(uint32_t)b.kept;
        gpucompute::dispatch(gpucompute::Kernel::BounceGather,{b.mapBuffer,b.sampleBuffer,b.tableBuffer,b.stripBuffer,b.hits,b.statics,b.light},{b.out},
                             &u,sizeof(u),(uint32_t)((n+group-1)/group));
        gpucompute::download(b.out,&got[first*6],n*24,0);
    },"the bounce");
    if(!ok)return false;
    for(size_t k=0;k<b.samples;++k)for(int c=0;c<6;++c)values[k][c]=got[k*6+c];
    return true;
}
} // namespace doom3d
