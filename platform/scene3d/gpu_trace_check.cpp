/* The trace check (PLAN-gpu-bakes.md, stage 1): rays from the level's own
   sample places traced by the processor's tracer and the graphics card's,
   compared, and both timed. DERP_GPUTRACECHECK=<file> appends one report a
   level; DERP_GPUTRACERAYS sets how many rays (default 8 million). */
#include "gpu_trace.h"
#include "open_doors.h"

extern "C" {
extern int gameepisode,gamemap;
}

namespace doom3d {
namespace {
struct Rng {
    uint64_t s;
    explicit Rng(uint64_t seed):s(seed*0x9E3779B97F4A7C15ull+1) {}
    uint32_t next() {s^=s<<13;s^=s>>7;s^=s<<17;return (uint32_t)(s>>16);}
    float unit() {return (next()&0xFFFFFF)/16777216.0f;}
};
// What a ray is for, as the bakes shoot them.
enum Kind { rayFloorSky, rayFloorBounce, rayCeilingBounce, rayWallBounce, rayWallSky, rayLight, raySun, rayKinds };
const char *const kindNames[rayKinds]={"floor sky","floor bounce","ceiling bounce","wall bounce","wall sky","light","sun"};
struct Outcome { bool open,skyward; BakeHit hit; };
float stopAt(const GpuRay &r,const BakeHit &h) {return std::sqrt((h.x-r.x)*(h.x-r.x)+(h.y-r.y)*(h.y-r.y)+(h.z-r.z)*(h.z-r.z));}
} // namespace

void gpuTraceCheck() {
    static const char *path=getenv("DERP_GPUTRACECHECK");
    if(!path||!*path||bakeStrips.empty()||seamCells.empty())return;
    static int checked=-1;
    int level=gameepisode*100+gamemap;
    if(level==checked)return;
    checked=level;
    FILE *out=fopen(path,"a");
    if(!out)return;
    fprintf(out,"== E%dM%d / MAP%02d: %zu sectors, %zu lines, %zu decorations\n",gameepisode,gamemap,gamemap,bakeMap.sectors.size(),bakeMap.lines.size(),bakeMap.things.size());
    if(!gpucompute::available()) {fprintf(out,"no compute on this device (%s)\n",gpucompute::backend());fclose(out);return;}
    // The compute layer on its own first: fill a buffer, read it back.
    {
        const uint32_t count=100000;
        std::vector<uint32_t> back(count,0);
        gpucompute::Buffer *b=gpucompute::create(count*4);
        struct { uint32_t job[4]; } job={{count,7,0,0}};
        gpucompute::dispatch(gpucompute::Kernel::Fill,{},{b},&job,sizeof(job),(count+63)/64);
        gpucompute::download(b,back.data(),count*4);
        bool ok=gpucompute::finish();
        size_t wrong=0;
        for(uint32_t i=0;i<count;++i)wrong+=back[i]!=i*3+7;
        gpucompute::destroy(b);
        fprintf(out,"fill: %s, %zu of %u words wrong (%s)\n",ok?"ran":"FAILED",wrong,count,gpucompute::backend());
        if(!ok||wrong) {fclose(out);return;}
    }
    const char *wanted=getenv("DERP_GPUTRACERAYS");
    size_t total=wanted&&*wanted?(size_t)atoll(wanted):(size_t)8<<20;
    const OpenDoors doors;
    Rng rng(level*7919u+17);
    std::vector<GpuRay> rays;rays.reserve(total);
    std::vector<uint8_t> kindOf;kindOf.reserve(total);
    // Floor cells under a roof or sky and wall texels, as the bakes place their samples.
    std::vector<size_t> cells;
    for(size_t i=0;i<seamCells.size();++i)if(seamCells[i].own!=noSector)cells.push_back(i);
    if(cells.empty()||bakeOrder.empty()) {fprintf(out,"no samples\n");fclose(out);return;}
    const float *sun=sunDirection;
    auto add=[&](int kind,int sector,float x,float y,float z,const float d[3],float length,uint32_t flags) {
        rays.push_back({x,y,z,gpuSectorFlags(sector,flags),d[0],d[1],d[2],length});kindOf.push_back((uint8_t)kind);
    };
    while(rays.size()<total) {
        int kind=(int)(rng.next()%rayKinds);
        float x,y,z,normal[3]={0,0,1};int sector;
        bool wall=kind==rayWallBounce||kind==rayWallSky||(kind>=rayLight&&(rng.next()&1));
        if(wall) {
            const BakeStrip &strip=bakeStrips[bakeOrder[rng.next()%bakeOrder.size()]];
            if(strip.width<1||strip.height<1)continue;
            int column=(int)(rng.next()%strip.width),row=(int)(rng.next()%strip.height);
            float along=std::clamp((column+0.5f)/strip.scale,0.5f,strip.length-0.5f);
            z=std::min(strip.low+(row+0.5f)/strip.scale,strip.high);
            x=strip.ax+strip.dx*along+strip.dy*0.5f;y=strip.ay+strip.dy*along-strip.dx*0.5f;
            normal[0]=strip.dy;normal[1]=-strip.dx;normal[2]=0;sector=strip.sector;
        } else {
            size_t i=cells[rng.next()%cells.size()];sector=seamCells[i].own;
            bool ceiling=kind==rayCeilingBounce;
            int pic=ceiling?sectors[sector].ceilingpic:sectors[sector].floorpic;
            if(pic==skyflatnum&&kind!=rayLight)continue;
            const BakeSector &s=doors.of(sector).sectors[sector];
            x=mapOrigin[0]+(i%mapWidth+0.5f)*mapCell;y=mapOrigin[1]+(i/mapWidth+0.5f)*mapCell;
            z=ceiling?s.ceiling-1:s.floor+1;
            if(ceiling)normal[2]=-1;
        }
        uint32_t door=doors.any&&sector>=0&&sector<(int)doors.door.size()&&doors.door[sector]?gpuRayDoors:0;
        float d[3];
        cosineDirection(normal,rng.unit(),rng.unit(),d);
        switch(kind) {
        case rayFloorSky:case rayWallSky:add(kind,sector,x,y,z,d,INFINITY,gpuRayEscape|door);break;
        case rayFloorBounce:case rayCeilingBounce:case rayWallBounce:add(kind,sector,x,y,z,d,1024,gpuRayEscape|door);break;
        case rayLight: {
            float length=16+rng.unit()*600;
            add(kind,sector,x,y,z,d,length,0);break;
        }
        case raySun:add(kind,sector,x,y,z,sun,INFINITY,gpuRayEscape);break;
        }
    }
    // The processor, on every core.
    std::vector<Outcome> cpu(rays.size());
    const int chunk=4096,chunks=(int)((rays.size()+chunk-1)/chunk);
    uint64_t start=SDL_GetTicksNS();
    parallelFor(chunks,[&](int c) {
        for(size_t i=(size_t)c*chunk;i<std::min(rays.size(),(size_t)(c+1)*chunk);++i) {
            const GpuRay &r=rays[i];
            int sector=(int)(r.sectorFlags&0xFFFFFF);uint32_t flags=r.sectorFlags>>24;
            const BakeMap &map=(flags&gpuRayDoors)&&doors.any?doors.map:bakeMap;
            const float d[3]={r.dx,r.dy,r.dz};
            Outcome &o=cpu[i];
            o.open=traceRay(map,sector,r.x,r.y,r.z,d,r.length,(flags&gpuRayEscape)?SkyCeiling::escape:SkyCeiling::open,&o.hit,nullptr,&o.skyward);
        }
    });
    double cpuMs=(SDL_GetTicksNS()-start)/1e6;
    // The card: once to warm it (pipeline, buffers), once timed.
    std::vector<GpuRayResult> gpu;
    if(!gpuTraceRays(rays,gpu,&doors)) {fprintf(out,"the GPU trace FAILED\n");fclose(out);return;}
    start=SDL_GetTicksNS();
    gpuTraceRays(rays,gpu,&doors);
    double gpuMs=(SDL_GetTicksNS()-start)/1e6;
    // The kernel alone: the rays already on the card, nothing read back (as the bakes
    // will run, making their rays there and reducing what they meet).
    double kernelMs=-1;
    {
        GpuMapUniforms mu;
        gpucompute::Buffer *in=gpucompute::create(rays.size()*sizeof(GpuRay)),*res=gpucompute::create(rays.size()*sizeof(GpuRayResult));
        if(in&&res&&gpuUploadMap(bakeMap,&doors,nullptr,mu)) {
            gpucompute::upload(in,rays.data(),rays.size()*sizeof(GpuRay));
            gpucompute::finish();
            const uint32_t group=gpucompute::groupSize(gpucompute::Kernel::TraceRays);
            start=SDL_GetTicksNS();
            // In pieces of a million, as the slicing would, all in one submission.
            for(size_t first=0;first<rays.size();first+=1u<<20) {
                size_t n=std::min<size_t>(1u<<20,rays.size()-first);
                struct { uint32_t mapA[4],mapB[4],job[4]; } job={};
                std::memcpy(job.mapA,mu.mapA,16);std::memcpy(job.mapB,mu.mapB,16);
                job.job[0]=(uint32_t)first;job.job[1]=(uint32_t)n;
                gpucompute::dispatch(gpucompute::Kernel::TraceRays,{gpuMapBuffer(),in},{res},&job,sizeof(job),(uint32_t)((n+group-1)/group));
                if(first==0) {gpucompute::finish();start=SDL_GetTicksNS();}   // the first piece warms it
            }
            gpucompute::finish();
            kernelMs=(SDL_GetTicksNS()-start)/1e6*rays.size()/std::max<size_t>(1,rays.size()-std::min<size_t>(rays.size(),1u<<20));
        }
        gpucompute::destroy(in);gpucompute::destroy(res);
    }
    // Agreement.
    size_t agree[rayKinds]={},count[rayKinds]={},outside=0,edge=0,real=0,openDiffers=0;
    int shown=0;
    for(size_t i=0;i<rays.size();++i) {
        const GpuRay &r=rays[i];const Outcome &c=cpu[i];const GpuRayResult &g=gpu[i];
        BakeHit h=gpuHit(r,g);
        bool open=g.flags&1,skyward=g.flags&2;
        ++count[kindOf[i]];
        bool same=open==c.open&&skyward==c.skyward;
        if(same&&!open)same=h.kind==c.hit.kind&&h.line==c.hit.line&&h.sector==c.hit.sector&&h.part==c.hit.part&&
                             std::fabs(h.x-c.hit.x)+std::fabs(h.y-c.hit.y)+std::fabs(h.z-c.hit.z)<0.05f;
        if(same) {++agree[kindOf[i]];continue;}
        int sector=(int)(r.sectorFlags&0xFFFFFF);
        openDiffers+=open!=c.open;
        if(!bakeInsideSector(bakeMap,sector,r.x,r.y)) {++outside;continue;}
        // Both stopped about the same place (a tie at a vertex, a ray grazing a corner or ending at a wall).
        float cpuAt=c.open?r.length:c.hit.kind==BakeHit::none?-1:stopAt(r,c.hit),gpuAt=open?r.length:h.kind==BakeHit::none?-1:g.at;
        if(cpuAt>=0&&gpuAt>=0&&std::fabs(cpuAt-gpuAt)<0.05f+1e-4f*cpuAt) {++edge;continue;}
        ++real;
        if(shown<12) {
            ++shown;
            fprintf(out,"  differs: %s ray from (%.3f, %.3f, %.3f) in %d along (%.5f, %.5f, %.5f) for %.1f%s%s: cpu %s%s kind %d sector %d line %d part %d at %.3f; gpu %s%s kind %d sector %d line %d part %d at %.3f\n",
                kindNames[kindOf[i]],r.x,r.y,r.z,sector,r.dx,r.dy,r.dz,r.length,(r.sectorFlags>>24)&gpuRayEscape?" escape":"",(r.sectorFlags>>24)&gpuRayDoors?" doors":"",
                c.open?"open":"stopped",c.skyward?" skyward":"",(int)c.hit.kind,c.hit.sector,c.hit.line,c.hit.part,cpuAt,
                open?"open":"stopped",skyward?" skyward":"",(int)h.kind,h.sector,h.line,h.part,gpuAt);
        }
    }
    size_t agreed=0;
    for(int k=0;k<rayKinds;++k) {agreed+=agree[k];fprintf(out,"  %-15s %9zu rays, %.5f%% agree\n",kindNames[k],count[k],count[k]?100.0*agree[k]/count[k]:100.0);}
    double n=(double)rays.size();
    fprintf(out,"agree %.5f%%; differ: %zu outside their sector, %zu at an edge or tie, %zu otherwise (%.2f in 10,000; %zu open on one side only)\n",
        100*agreed/n,outside,edge,real,real/n*1e4,openDiffers);
    fprintf(out,"processor %.0f ms (%.1f M rays/s, %u threads); GPU %.0f ms with upload and download (%.1f M rays/s): %.1fx; kernel alone %.0f ms (%.1f M rays/s): %.1fx\n",
        cpuMs,n/cpuMs/1e3,std::clamp(std::thread::hardware_concurrency(),1u,16u),gpuMs,n/gpuMs/1e3,cpuMs/std::max(gpuMs,0.001),
        kernelMs,n/std::max(kernelMs,0.001)/1e3,cpuMs/std::max(kernelMs,0.001));
    fclose(out);
    fprintf(stderr,"GPU trace check: %.5f%% agree, %zu real differences, %.1fx the processor; report in %s.\n",100*agreed/n,real,cpuMs/std::max(gpuMs,0.001),path);
}
} // namespace doom3d
