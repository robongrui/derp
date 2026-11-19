/* The lamps' baked light on the graphics card (PLAN-gpu-bakes.md, stage 4):
   the lights, pools and bake grid flattened for lights.comp, the texels and
   cells listed as lightCell and lightStrip list them, and each one finished
   here as gatherStatic finishes it (stained glass added, rounded, its
   direction texel and lobes kept). */
#include "gpu_trace.h"
#include <atomic>

namespace doom3d {
namespace {
uint32_t f2u(float f) {uint32_t u;std::memcpy(&u,&f,4);return u;}
// The card's lamp light failed its check at a load this run (lampsAgree): the processor bakes it from then on.
std::atomic<bool> lampsUntrusted{false};
// The lights, tiles, pools, their pieces, corners and samples, and the grid, as lights.comp reads them.
// changed: instead of the level's lights and pools, the changed lamps (gatherChanged), each as
// it was and then as it is. boxes: a moved door's (gatherMoved).
struct LightWords { std::vector<uint32_t> words; uint32_t parts[8],boxes=0,boxCount=0,lamps=0; };
void buildLightWords(const BakeMap &map,LightWords &w,const ChangedLamps *changed=nullptr,const std::vector<std::array<float,4>> *boxes=nullptr) {
    std::vector<uint32_t> lights,tiles,pools,pieces,corners,samples,offsets,lists,boxWords;
    auto light=[&](const BakeLight &l,uint32_t group,uint32_t flags) {
        uint32_t firstTile=(uint32_t)(tiles.size()/3);
        if(l.tiles&&l.tileCount>0)for(int k=0;k<3*l.tileCount;++k)tiles.push_back(f2u(l.tiles[k]));
        lights.insert(lights.end(),{f2u(l.x),f2u(l.y),f2u(l.z),f2u(l.radius),f2u(l.strength),f2u(l.directionality),
            f2u(l.color[0]),f2u(l.color[1]),f2u(l.color[2]),f2u(l.extent),f2u(l.nx),f2u(l.ny),f2u(l.beam),f2u(l.tail),f2u(l.post),
            firstTile,(uint32_t)(l.tiles?l.tileCount:0),group,flags,0});
    };
    if(changed) {
        for(size_t n=0;n<changed->was.size()&&n<changed->now.size();++n) {
            uint32_t group=n<changed->group.size()?changed->group[n]:0;
            light(changed->was[n],group,0);light(changed->now[n],group,0);
        }
        w.lamps=(uint32_t)std::min(changed->was.size(),changed->now.size());
    } else for(int n=0;n<(int)bakeSources.size();++n)
        light(bakeSources[n],n<(int)sourceGroups.size()?sourceGroups[n]:0,(n>=firstWindowSource?1u:0u)|(shutWindowLamp(map,n)?2u:0u));
    if(boxes)for(const auto &b:*boxes)for(float c:b)boxWords.push_back(f2u(c));
    if(!changed)for(const BakeArea &a:bakeAreas) {
        uint32_t firstPiece=(uint32_t)(pieces.size()/2),firstSample=(uint32_t)(samples.size()/2);
        for(const auto &piece:a.pieces) {
            pieces.push_back((uint32_t)(corners.size()/2));pieces.push_back((uint32_t)piece.size());
            for(const auto &c:piece) {corners.push_back(f2u(c[0]));corners.push_back(f2u(c[1]));}
        }
        for(const auto &s:a.samples) {samples.push_back(f2u(s[0]));samples.push_back(f2u(s[1]));}
        pools.insert(pools.end(),{f2u(a.z),f2u(a.clearance),a.ceiling?1u:0u,f2u(a.radius),f2u(a.strength),f2u(a.directionality),
            f2u(a.color[0]),f2u(a.color[1]),f2u(a.color[2]),f2u(a.low[0]),f2u(a.low[1]),f2u(a.high[0]),f2u(a.high[1]),
            firstPiece,(uint32_t)a.pieces.size(),firstSample,(uint32_t)a.samples.size(),0,0,0});
    }
    if(!changed)for(const auto &cell:bakeGrid) {
        offsets.push_back((uint32_t)lists.size());offsets.push_back((uint32_t)cell.size());
        for(int n:cell)lists.push_back((uint32_t)n);
    }
    w.words.clear();
    auto append=[&](const std::vector<uint32_t> &part) {
        uint32_t at=(uint32_t)w.words.size();
        w.words.insert(w.words.end(),part.begin(),part.end());
        return at;
    };
    w.parts[0]=append(lights);w.parts[1]=append(tiles);w.parts[2]=append(pools);w.parts[3]=append(pieces);
    w.parts[4]=append(corners);w.parts[5]=append(samples);w.parts[6]=append(offsets);w.parts[7]=append(lists);
    w.boxes=append(boxWords);w.boxCount=(uint32_t)(boxes?boxes->size():0);
    w.words.push_back(0);
}
bool grow(gpucompute::Buffer *&buffer,size_t bytes) {
    if(buffer&&gpucompute::size(buffer)>=bytes)return true;
    gpucompute::destroy(buffer);
    buffer=gpucompute::create(bytes+bytes/4);
    return buffer!=nullptr;
}
} // namespace

void staticItemsOfCell(const BakeMap &map,size_t i,bool only,std::vector<BakeSample> &items,std::vector<StaticTarget> &targets) {
    int sector=seamCells[i].own;
    if(sector==noSector)return;
    float px=mapOrigin[0]+(i%mapWidth+0.5f)*mapCell,py=mapOrigin[1]+(i/mapWidth+0.5f)*mapCell;
    const BakeSector &s=map.sectors[sector];
    size_t layer=(size_t)mapWidth*mapHeight;
    bool lobes=!flatLampLobes.empty();
    if(sectors[sector].floorpic!=skyflatnum) {
        items.push_back({px,py,s.floor+1,sector,{0,0,1},0,false});
        targets.push_back({flatCell(floorLight,i),only?&flatDirectionCells[4*i]:nullptr,lobes?&flatLampLobes[i]:nullptr});
    }
    if(sectors[sector].ceilingpic!=skyflatnum) {
        items.push_back({px,py,s.ceiling-1,sector,{0,0,-1},0,false});
        targets.push_back({flatCell(ceilingLight,i),only?&flatDirectionCells[4*(layer+i)]:nullptr,lobes?&flatLampLobes[layer+i]:nullptr});
    }
}
void staticItemsOfStrip(const BakeStrip &strip,bool only,std::vector<BakeSample> &items,std::vector<StaticTarget> &targets) {
    forStripTexels(strip,[&](size_t i,float x,float y,float z) {
        size_t texel=i/atlasStride*atlasWidth+i%atlasStride;
        items.push_back({x,y,z,strip.sector,{strip.dy,-strip.dx,0},0,true});
        targets.push_back({&wallBakeCells[4*i],only?&wallDirectionCells[4*i]:nullptr,wallLampLobes.empty()?nullptr:&wallLampLobes[texel]});
    });
}

namespace {
struct LightBuffers {
    gpucompute::Buffer *items=nullptr,*lights=nullptr,*out=nullptr;
    ~LightBuffers() {release();}
    void release() {gpucompute::destroy(items);gpucompute::destroy(lights);gpucompute::destroy(out);}
};
thread_local LightBuffers lightBuffers;
// lights.comp over items in one of its modes: stride words back an item. before: the map as it
// was (a moved door's change).
bool runLights(const BakeMap &map,const BakeMap *before,const std::vector<BakeSample> &items,const LightWords &lw,uint32_t mode,uint32_t flags,
               uint32_t stride,std::vector<uint32_t> &got) {
    got.assign(items.size()*stride,0);
    if(items.empty())return true;
    GpuMapUniforms mu;
    if(!gpuUploadMap(map,nullptr,nullptr,mu,nullptr,before))return false;
    std::vector<std::array<uint32_t,8>> up(items.size());
    for(size_t k=0;k<items.size();++k) {
        const BakeSample &s=items[k];
        up[k]={f2u(s.x),f2u(s.y),f2u(s.z),(uint32_t)s.sector,f2u(s.normal[0]),f2u(s.normal[1]),f2u(s.normal[2]),0};
    }
    const size_t most=std::max<size_t>(1024,((size_t)6<<20)/(4*stride));
    // Kept from one call to the next on this thread (a door re-bakes every few tics), let go at shutdown.
    gpucompute::Buffer *&itemBuffer=lightBuffers.items,*&lightBuffer=lightBuffers.lights,*&outBuffer=lightBuffers.out;
    if(!grow(itemBuffer,up.size()*32)||!grow(lightBuffer,lw.words.size()*4)||!grow(outBuffer,std::min(items.size(),most)*4*stride))return false;
    gpucompute::upload(itemBuffer,up.data(),up.size()*32);
    gpucompute::upload(lightBuffer,lw.words.data(),lw.words.size()*4);
    struct { uint32_t mapA[4],mapB[4],job[4]; float grid[4]; uint32_t size[4],parts[4],parts2[4],more[4]; } u={};
    std::memcpy(u.mapA,mu.mapA,16);std::memcpy(u.mapB,mu.mapB,16);
    int gridW,gridH;bakeGridSize(gridW,gridH);
    u.grid[0]=mapOrigin[0];u.grid[1]=mapOrigin[1];u.grid[2]=bakeGridCellSize();u.grid[3]=(float)gridW;
    u.size[0]=(uint32_t)gridH;u.size[1]=(uint32_t)bakeSources.size();u.size[2]=(uint32_t)firstWindowSource;u.size[3]=f2u(bakeTailShare);
    std::memcpy(u.parts,lw.parts,16);std::memcpy(u.parts2,lw.parts+4,16);
    u.more[0]=lw.boxes;u.more[1]=lw.boxCount;u.more[2]=lw.lamps;
    const uint32_t group=gpucompute::groupSize(gpucompute::Kernel::Lights);
    return gpuSliced(items.size(),loadingOnThisThread()?8192:1024,most,[&](size_t first,size_t n) {
        u.job[0]=(uint32_t)first;u.job[1]=(uint32_t)n;u.job[2]=flags|mode<<4;u.job[3]=stride;
        gpucompute::dispatch(gpucompute::Kernel::Lights,{gpuMapBuffer(),itemBuffer,lightBuffer},{outBuffer},&u,sizeof(u),(uint32_t)((n+group-1)/group));
        gpucompute::download(outBuffer,&got[first*stride],n*4*stride);
    },mode==0?"the lamps' light":mode==1?"a door's light":"changed lamps' light");
}
// A loop over the items: on every core at load, on this thread in play (the light worker leaves
// the game its cores).
void forItems(size_t count,const std::function<void(size_t)> &work) {
    if(!loadingOnThisThread()) {for(size_t k=0;k<count;++k)work(k);return;}
    parallelFor((int)((count+1023)/1024),[&](int chunk) {
        for(size_t k=(size_t)chunk*1024;k<std::min(count,(size_t)chunk*1024+1024);++k)work(k);
    });
}
// The load's check of the card's lamp light: a sample spread over the items, each baked here
// as the processor's bake makes it (stained glass and rounding included) and set beside what
// the card left in its target. A sound card differs only where a ray grazes a corner (tens of
// texels in a million); Mesa's compiler on the Steam Deck once got most of them wrong.
bool lampsAgree(const BakeMap &map,const std::vector<BakeSample> &items,const std::vector<StaticTarget> &targets) {
    const size_t count=std::min<size_t>(4096,items.size());
    std::vector<uint8_t> off(count,0);
    parallelFor((int)((count+255)/256),[&](int chunk) {
        for(size_t j=(size_t)chunk*256;j<std::min(count,(size_t)chunk*256+256);++j) {
            size_t k=j*items.size()/count;
            uint8_t here[3];
            gatherStaticAt(map,items[k],here);
            for(int c=0;c<3;++c)off[j]|=std::abs((int)here[c]-(int)targets[k].out[c])>4;
        }
    });
    size_t wrong=0;
    for(uint8_t o:off)wrong+=o;
    if(wrong<=std::max<size_t>(8,count/100))return true;
    fprintf(stderr,"GPU lights: the graphics card's lamp light disagrees with the processor's in %zu of %zu sampled texels "
                   "(a driver fault); lamp light is baked on the processor from here on.\n",wrong,count);
    return false;
}
} // namespace

void gpuLightsShutdown() {lightBuffers.release();}

bool gpuGatherStatic(const BakeMap &map,const std::vector<BakeSample> &items,const std::vector<StaticTarget> &targets,bool directed) {
    if(items.empty())return true;
    if(targets.size()!=items.size()||lampsUntrusted)return false;
    const uint64_t t0=SDL_GetTicksNS();
    LightWords lw;buildLightWords(map,lw);
    bool lobes=false;
    for(const StaticTarget &t:targets)if(t.kept) {lobes=true;break;}
    std::vector<uint32_t> got;
    if(!runLights(map,nullptr,items,lw,0,(directed?1u:0u)|(lobes?2u:0u),6,got))return false;
    const uint64_t t1=SDL_GetTicksNS();
    // gatherStatic's end: the stained glass, the rounding, the direction texel, the lobes.
    const bool whole=settings.lightModel==1;
    forItems(items.size(),[&](size_t k) {
        const BakeSample &s=items[k];const StaticTarget &t=targets[k];const uint32_t *g=&got[6*k];
        float sum[3];std::memcpy(sum,g,12);
        addGlassLight(map,s.sector,s.x,s.y,s.z,s.normal,sum);
        if(whole)for(int c=0;c<3;++c)t.out[c]=(uint8_t)std::min(255L,std::lround(sum[c]*0.5f*255));
        else for(int c=0;c<3;++c)t.out[c]=(uint8_t)std::min(255L,std::lround(std::round(sum[c]*32)/32*0.5f*255));
        if(t.direction&&directed)for(int c=0;c<4;++c)t.direction[c]=(uint8_t)(g[3]>>(8*c));
        if(t.kept) {(*t.kept)[0]=(uint16_t)(g[4]&0xFFFF);(*t.kept)[1]=(uint16_t)(g[4]>>16);(*t.kept)[2]=(uint16_t)g[5];}
    });
    const uint64_t t2=SDL_GetTicksNS();
    if(loadingOnThisThread()&&!lampsAgree(map,items,targets)) {lampsUntrusted=true;return false;}
    static const bool timing=getenv("DERP_GPUTIMES")!=nullptr;
    if(timing)fprintf(stderr,"GPU lights: %zu items, card %.0f ms, here %.0f ms, check %.0f ms.\n",items.size(),(t1-t0)/1e6,(t2-t1)/1e6,
                      (SDL_GetTicksNS()-t2)/1e6);
    return true;
}

bool gpuGatherMoved(const BakeMap &before,const BakeMap &now,const std::vector<std::array<float,4>> &boxes,const std::vector<BakeSample> &items,
                    std::vector<std::array<float,4>> &deltas) {
    if(lampsUntrusted)return false;
    deltas.assign(items.size(),{});
    LightWords lw;buildLightWords(now,lw,nullptr,&boxes);
    std::vector<uint32_t> got;
    if(!runLights(now,&before,items,lw,1,0,4,got))return false;
    for(size_t k=0;k<items.size();++k) {std::memcpy(deltas[k].data(),&got[4*k],12);deltas[k][3]=got[4*k+3]?1.0f:0.0f;}
    return true;
}

bool gpuGatherChanged(const BakeMap &map,const ChangedLamps &lamps,const std::vector<BakeSample> &items,std::vector<std::array<float,20>> &changes) {
    if(lampsUntrusted)return false;
    changes.assign(items.size(),{});
    LightWords lw;buildLightWords(map,lw,&lamps);
    std::vector<uint32_t> got;
    if(!runLights(map,nullptr,items,lw,2,0,20,got))return false;
    for(size_t k=0;k<items.size();++k) {std::memcpy(changes[k].data(),&got[20*k],19*4);changes[k][19]=got[20*k+19]?1.0f:0.0f;}
    return true;
}
} // namespace doom3d
