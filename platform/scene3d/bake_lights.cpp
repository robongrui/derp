/* Static lights baked at level load: lamps, glowing pools and window
   light, and the grid of lights that reach each map cell. */
#include "internal.h"
#include "gpu_trace.h"
#include "light_rooms.h"
#include "../rooms.h"

namespace doom3d {
int lightBakeKey=-1;
unsigned lightBakeSerial=0;
bool bakedLightsActive=false; // This frame's lights rely on the bake.

std::vector<float> lampDrop;
bool bakeOnlyActive=false; // ...and static lights come only from it.

std::vector<BakeLight> liveBakeSources;
thread_local LightTarget *lightTarget=nullptr;
thread_local bool backgroundLight=false;
std::vector<SurfaceLightKey> bakeSourceKeys;
int firstWindowSource=0; // Window lights (addWindowLights) and doorway lights (bake_doorways.cpp) end bakeSources; they cast no thing shadows.
static std::vector<int> windowDoors; // Per window or doorway light: the shut door it sits in, -1 for none.
bool shutWindowLamp(const BakeMap &map,int n) {
    size_t k=(size_t)(n-firstWindowSource);
    if(n<firstWindowSource||k>=windowDoors.size()||windowDoors[k]<0)return false;
    const BakeSector &door=map.sectors[windowDoors[k]];
    return door.ceiling<=door.floor;
}
// Window light reaching one point eases toward about the outdoor level, so
// a room with windows on several sides isn't lit past it by their sum. Scales
// rgb (by its luminance) and returns the factor.
float capWindowLight(float rgb[3]) {
    constexpr float cap=1.0f;
    float l=0.299f*rgb[0]+0.587f*rgb[1]+0.114f*rgb[2];
    if(l<=0)return 1;
    float scale=cap*(1-std::exp(-l/cap))/l;
    for(int c=0;c<3;++c)rgb[c]*=scale;
    return scale;
}
constexpr float bakeGridCell=128;
static int bakeGridWidth=0,bakeGridHeight=0;
std::vector<std::vector<int>> bakeGrid;

std::vector<BakeArea> bakeAreas;
std::vector<uint8_t> sourceGroups;
std::vector<uint8_t> liveWallDirectionCells,liveFlatDirectionCells;
bool bakeDirected=false;
void bakeGridSize(int &width,int &height) {width=bakeGridWidth;height=bakeGridHeight;}
float bakeGridCellSize() {return bakeGridCell;}
const std::vector<int> &bakeGridLights(float x,float y) {
    int gx=std::clamp((int)std::floor((x-mapOrigin[0])/bakeGridCell),0,bakeGridWidth-1);
    int gy=std::clamp((int)std::floor((y-mapOrigin[1])/bakeGridCell),0,bakeGridHeight-1);
    return bakeGrid[(size_t)gy*bakeGridWidth+gx];
}

// The light bake's inputs: 1 emissive textures, 2 bake-only lights.
// The library (Game_WorldWraps: its lamps stand in for many alike, every
// storey) is always bake-only: per-frame light adds nothing there.
int lightBakeInputs() {return (settings.emissive?1:0)|(settings.bakeOnlyLights||Game_WorldWraps()?2:0)|(settings.windowLight?4:0)|(settings.dimLitRooms?8:0)|(settings.lightModel==1?1<<21:0)|((int)std::lround(settings.lampTail*20)<<16)|(settings.doorwayLight?1<<15:0)|
    (settings.windowLight?(int)std::lround(settings.windowStrength*4)<<4:0)|
    (settings.dimLitRooms?(int)std::lround(settings.unlitDarkening*20)<<8:0)|(settings.windowLight&&settings.sun?1<<14:0)|
    (settings.lightModel==1?(settings.poolPoints<=16?2:settings.poolPoints<=32?1:0)<<22:0);}
// Bake-only lights: glowing liquid floors and ceilings (lava, nukage, slime,
// magma, or flats that mostly glow) light as pools; lamps and panels stay
// point lights.
static bool poolSurface(int sector,bool ceiling) {
    int pic=ceiling?sectors[sector].ceilingpic:sectors[sector].floorpic;
    if(pic==skyflatnum)return false;
    int lump=firstflat+flattranslation[pic],grey=0,glow=8;
    bool frozen=!ceiling&&frozenLook(sector,grey,glow);
    const Image &image=frozen?frozenFlat(firstflat+pic,grey,glow):sectorFlatImage(sector,ceiling);   // frozen (Phobos): its glow dying
    if(image.emissionWeight<=0)return false;
    char name[9];memcpy(name,lumpinfo[lump].name,8);name[8]=0;
    int kind=Game_GlowKind(name,1);
    return kind==DOOM_EMISSIVE_LAVA||kind==DOOM_EMISSIVE_SLIME||kind==DOOM_EMISSIVE_MAGMA||image.emissionCoverage>=0.4f;   // lit paint glows all over
}
// One area light per pool: neighboring sectors whose floor (or ceiling)
// glows in the same flat at the same height join. Strength and reach grow
// gently with the pool's size, like merged tile lights did. The 0.68 keeps
// the total near what the tile lights gave (E1M1: Doom +12%, Freedoom -10%),
// as the glow now spills further over rims.
constexpr float poolGlow=0.3f;   // a glowing pool's light against a lamp's, in whole-level lighting
// Whole-level lighting: daylight is drawn far below what it is (the bounce bake's skyInGain, 4),
// so a lamp or a pool that stands in it counts that much less, wherever its light goes: by up
// to 4 under a sky at full level, not at all at night or under a roof. By the source, so its
// light has no step where the daylight on the ground changes.
static float dayWeak(float x,float y) {
    if(settings.lightModel!=1)return 1;
    const sector_t *sector=R_PointInSubsector((fixed_t)(x*FRACUNIT),(fixed_t)(y*FRACUNIT))->sector;
    if(sector->ceilingpic!=skyflatnum)return 1;
    return 1/(1+3*std::clamp(lighting(sector),0.0f,1.0f));
}
static void buildPoolAreas() {
    bakeAreas.clear();
    std::vector<float> surface,weight; // Per area: size, and emission weight times size.
    for(int ceiling=0;ceiling<2;++ceiling) {
        std::vector<int> parent(numsectors);
        for(int i=0;i<numsectors;++i)parent[i]=i;
        std::function<int(int)> root=[&](int i) {return parent[i]==i?i:parent[i]=root(parent[i]);};
        std::vector<char> pool(numsectors);
        for(int i=0;i<numsectors;++i)pool[i]=poolSurface(i,ceiling);        for(int i=0;i<numlines;++i) {
            const line_t &line=lines[i];
            if(!line.frontsector||!line.backsector)continue;
            int a=(int)(line.frontsector-sectors),b=(int)(line.backsector-sectors);
            if(!pool[a]||!pool[b])continue;
            const sector_t &sa=sectors[a],&sb=sectors[b];
            bool same=ceiling?sa.ceilingpic==sb.ceilingpic&&sa.ceilingheight==sb.ceilingheight
                             :sa.floorpic==sb.floorpic&&sa.floorheight==sb.floorheight;
            if(same)parent[root(a)]=root(b);
        }
        std::map<int,size_t> areaOf;
        for(int i=0;i<numsectors;++i) {
            if(!pool[i])continue;
            auto found=areaOf.find(root(i));
            if(found==areaOf.end()) {
                found=areaOf.emplace(root(i),bakeAreas.size()).first;
                BakeArea area;area.ceiling=ceiling;
                area.z=ceiling?bakeMap.sectors[i].ceiling:bakeMap.sectors[i].floor;
                area.clearance=1e9f;
                area.color[0]=area.color[1]=area.color[2]=0;
                bakeAreas.push_back(area);surface.push_back(0);weight.push_back(0);
            }
            size_t index=found->second;BakeArea &area=bakeAreas[index];
            // Rays test against a point up to 48 units off the surface, inside the room.
            area.clearance=std::min(area.clearance,std::clamp(bakeMap.sectors[i].ceiling-bakeMap.sectors[i].floor-2,0.0f,48.0f));
            int grey=0,glow=8,pic=ceiling?sectors[i].ceilingpic:sectors[i].floorpic;
            bool frozen=!ceiling&&frozenLook(i,grey,glow);
            const Image &image=frozen?frozenFlat(firstflat+pic,grey,glow):sectorFlatImage(i,ceiling);
            for(int leaf:sectorFloors[i]) {
                const auto &poly=floors[leaf];if(poly.size()<3)continue;
                std::vector<std::array<float,2>> piece;float twice=0;
                for(size_t n=0;n<poly.size();++n) {
                    const Point &p=poly[n],&q=poly[(n+1)%poly.size()];
                    piece.push_back({p.x,p.y});twice+=p.x*q.y-q.x*p.y;
                    area.low[0]=std::min(area.low[0],p.x);area.low[1]=std::min(area.low[1],p.y);
                    area.high[0]=std::max(area.high[0],p.x);area.high[1]=std::max(area.high[1],p.y);
                }
                float size=std::fabs(twice)*0.5f;
                for(int c=0;c<3;++c)area.color[c]+=image.emissionColor[c]*size;
                surface[index]+=size;weight[index]+=image.emissionWeight*size;
                area.pieces.push_back(std::move(piece));
            }
        }
        for(size_t n=0;n<bakeAreas.size();++n) {
            BakeArea &area=bakeAreas[n];
            if(area.ceiling!=(bool)ceiling||surface[n]<=0)continue;
            for(float &c:area.color)c/=surface[n];
            float tiles=surface[n]/4096;
            area.strength=0.68f*1.65f*weight[n]/surface[n]*std::clamp(0.6f+0.2f*std::sqrt(tiles),0.75f,1.5f);
            // Whole-level lighting: a pool glows, it is no fixture. At a lamp's strength (a big
            // pool at one and a half) nukage lit a court and the rooms off it green as far as it
            // reached, and with every lamp's light now followed and bounced there was nothing
            // left to hide that behind.
            if(settings.lightModel==1)area.strength*=poolGlow*dayWeak((area.low[0]+area.high[0])*0.5f,(area.low[1]+area.high[1])*0.5f);
            area.radius=256+std::clamp(std::sqrt(surface[n])*0.125f,0.0f,96.0f);
        }
    }
    bakeAreas.erase(std::remove_if(bakeAreas.begin(),bakeAreas.end(),[](const BakeArea &a){return a.pieces.empty();}),bakeAreas.end());
#ifdef DOOM_DEVTOOLS
    // DERP_POOLS=1: where each glowing area lies and how far it reaches; DERP_NOPOOLS=1: none at all, to time re-bakes without them.
    if(getenv("DERP_POOLS"))for(const BakeArea &a:bakeAreas)
        fprintf(stderr,"pool: %s at %.0f, from %.0f,%.0f to %.0f,%.0f, reach %.0f, %zu pieces, strength %.2f.\n",a.ceiling?"ceiling":"floor",a.z,
                a.low[0],a.low[1],a.high[0],a.high[1],a.radius,a.pieces.size(),a.strength);
    if(getenv("DERP_NOPOOLS"))bakeAreas.clear();
#endif
    // Whole-level lighting: up to 64 points over each area (16 drew spokes on the ceiling over a ring of nukage), evenly picked from its pieces'
    // middles and the points halfway out to their corners (BakeArea::samples).
    if(settings.lightModel==1)for(BakeArea &area:bakeAreas) {
        std::vector<std::array<float,2>> all;
        for(const auto &piece:area.pieces) {
            float cx=0,cy=0;
            for(const auto &p:piece) {cx+=p[0];cy+=p[1];}
            cx/=piece.size();cy/=piece.size();
            all.push_back({cx,cy});
            for(const auto &p:piece)all.push_back({(cx+p[0])*0.5f,(cy+p[1])*0.5f});
        }
        // F4 Pool light points: 64, 32 or 16 (2026-11-18, E1M7: 32 looked the same as 64 and halved a door's
        // re-bake on the processor; 16 shifted walls over nukage by a few shades).
        size_t most=(size_t)std::clamp(settings.poolPoints,16,64);
#ifdef DOOM_DEVTOOLS
        // DERP_POOLSAMPLES=n: at most n points, to see how few still hold the look.
        if(const char *cap=getenv("DERP_POOLSAMPLES"))most=std::clamp<size_t>((size_t)atoi(cap),1,64);
#endif
        size_t count=std::min<size_t>(all.size(),most);
        for(size_t k=0;k<count;++k)area.samples.push_back(all[k*all.size()/count]);
    }
}
// Direction texel (bake-only lights, or lamps that switch): where the static light comes from in the
// surface's tangent frame (rg; walls: along the side and up, floors and
// ceilings: map x and y), how much of it comes from that one direction (b),
// and the strongest flicker group with its share in 1/15 steps
// (a: group*16+share). Without a flicker group the low nibble holds the
// share of light from a pool the surface lies in (its sheen; see world.frag).
// Keep the frame equivalent to world.frag.
static void encodeDirection(const float normal[3],const float towards[3],float total,const float groups[],float sheen,uint8_t *out) {
    out[0]=out[1]=128;out[2]=out[3]=0;
    float length=std::sqrt(towards[0]*towards[0]+towards[1]*towards[1]+towards[2]*towards[2]);
    if(total<=0||length<=1e-6f)return;
    bool flat=std::fabs(normal[2])>0.5f;
    float t[3]={flat?1.0f:-normal[1],flat?0.0f:normal[0],0},b[3]={0,flat?1.0f:0.0f,flat?0.0f:1.0f};
    float x=(towards[0]*t[0]+towards[1]*t[1]+towards[2]*t[2])/length,y=(towards[0]*b[0]+towards[1]*b[1]+towards[2]*b[2])/length;
    out[0]=(uint8_t)std::lround(std::clamp(x*0.5f+0.5f,0.0f,1.0f)*255);
    out[1]=(uint8_t)std::lround(std::clamp(y*0.5f+0.5f,0.0f,1.0f)*255);
    out[2]=(uint8_t)std::lround(std::clamp(length/total,0.0f,1.0f)*255);
    int best=0;
    for(int g=1;g<flickerGroups;++g)if(groups[g]>0&&groups[g]>(best?groups[best]:0))best=g;
    if(best)out[3]=(uint8_t)(best*16+std::lround(std::clamp(groups[best]/total,0.0f,1.0f)*15));
    else out[3]=(uint8_t)std::lround(std::clamp(sheen/total,0.0f,1.0f)*15);
}
#ifdef DOOM_DEVTOOLS
// DERP_RAYCOUNT=1 (with DERP_GPUBAKES=0, DERP_NOBAKECACHE=1): what the processor's lamp bake at load
// spends its rays on. Per light-texel pair that casts rays: whether any arrived, and whether the light
// stands in the texel's own room (rooms.c). Indexed [lamp 0 / pool 1][same room 0 / other room 1].
namespace {
struct RayTally { std::atomic<uint64_t> pairs{0},hidden{0},rays{0},hiddenRays{0},quick{0}; };
RayTally rayTally[2][2];
std::vector<int> lightRooms;   // per light, then per pool: its room, -1 none
void tallyPair(int n,int points,int sector) {
    if(!bakePairRays)return;
    int room=sector>=0&&sector<numsectors?Rooms_Of(sector):-1,own=(size_t)n<lightRooms.size()?lightRooms[n]:-1;
    RayTally &t=rayTally[n>=points][room<0||own!=room];
    ++t.pairs;t.rays+=bakePairRays;t.quick+=bakePairQuick;
    if(!bakePairOpen) {++t.hidden;t.hiddenRays+=bakePairRays;}
}
void startRayTally() {
    bakeRayCounting=getenv("DERP_RAYCOUNT")!=nullptr;
    if(!bakeRayCounting)return;
    for(auto &kind:rayTally)for(RayTally &t:kind)t.pairs=t.hidden=t.rays=t.hiddenRays=t.quick=0;
    lightRooms.clear();
    auto roomAt=[](float x,float y) {return Rooms_Of(int(R_PointInSubsector((fixed_t)(x*FRACUNIT),(fixed_t)(y*FRACUNIT))->sector-sectors));};
    for(const BakeLight &l:bakeSources)lightRooms.push_back(roomAt(l.x,l.y));
    for(const BakeArea &a:bakeAreas)lightRooms.push_back(a.samples.empty()?roomAt((a.low[0]+a.high[0])*0.5f,(a.low[1]+a.high[1])*0.5f):roomAt(a.samples[0][0],a.samples[0][1]));
}
void reportRayTally() {
    if(!bakeRayCounting)return;
    bakeRayCounting=false;
    const char *kinds[2]={"lamps","pools"},*places[2]={"same room","other room"};
    uint64_t rays=0,wasted=0;
    for(auto &kind:rayTally)for(RayTally &t:kind) {rays+=t.rays;wasted+=t.hiddenRays;}
    fprintf(stderr,"ray count: %llu rays, %llu of them (%.1f%%) in pairs where none arrived.\n",(unsigned long long)rays,(unsigned long long)wasted,rays?100.0*wasted/rays:0.0);
    for(int k=0;k<2;++k)for(int p=0;p<2;++p) {
        const RayTally &t=rayTally[k][p];
        fprintf(stderr,"ray count %s, %s: %llu pairs, %llu hidden (%.1f%%); %llu rays, %llu in hidden pairs (%.1f%% of all rays), %llu stopped without a trace.\n",
                kinds[k],places[p],(unsigned long long)t.pairs.load(),(unsigned long long)t.hidden.load(),t.pairs?100.0*t.hidden/t.pairs:0.0,
                (unsigned long long)t.rays.load(),(unsigned long long)t.hiddenRays.load(),rays?100.0*t.hiddenRays/rays:0.0,(unsigned long long)t.quick.load());
    }
}
}
#endif
// The static light at a surface point, unflickered, into out (rgb at half
// scale, in 1/32 steps) and, if given, its bake-only direction texel and its
// light split over the HL2 basis (bake_basis.cpp). blockers, if given: per light and pool, the
// wall that last stopped its rays (bakeRayOpen), carried from texel to texel.
static void gatherStatic(const BakeMap &map,int sector,float x,float y,float z,const float normal[3],uint8_t *out,uint8_t *direction,
                         std::array<uint16_t,3> *kept=nullptr,int *blockers=nullptr) {
    float lobes[3]={};
    // From a place outside its sector the trace runs through the void, which no wall stops: left as it was,
    // and every light still traced from there (the rooms say nothing about the void).
    const bool inSector=bakeInsideSector(map,sector,x,y);
    if(blockers&&!inSector)blockers=nullptr;
    int points=(int)bakeSources.size();
    float sum[3]={},towards[3]={},total=0,groups[flickerGroups]={},sheen=0;
    float window[3]={},windowTowards[3]={},windowTotal=0;
    for(int n:bakeGridLights(x,y)) {
        if(inSector&&!lightReachesSector(n,sector))continue;   // behind walls from here (light_rooms.cpp)
        if(shutWindowLamp(map,n))continue;
        float to[3];bool inside=false;
        const float *color=n<points?bakeSources[n].color:bakeAreas[n-points].color;
        bool isWindow=n>=firstWindowSource&&n<points;
        int *blocker=blockers?&blockers[n]:nullptr;
#ifdef DOOM_DEVTOOLS
        bakePairRays=bakePairOpen=bakePairQuick=0;
        // DERP_SAMEROOMONLY=1 (with DERP_RAYCOUNT): lights outside the texel's room are skipped, wrong on purpose, to time
        // what a filter by rooms could save at most.
        static const bool sameRoomOnly=getenv("DERP_SAMEROOMONLY")!=nullptr;
        if(sameRoomOnly&&bakeRayCounting&&(size_t)n<lightRooms.size()&&sector>=0&&lightRooms[n]!=Rooms_Of(sector))continue;
#endif
        float amount=n<points?addBakedLight(map,bakeSources[n],sector,x,y,z,normal,isWindow?window:sum,to,blocker)
                             :addBakedArea(map,bakeAreas[n-points],sector,x,y,z,normal,sum,to,&inside,blocker);
#ifdef DOOM_DEVTOOLS
        if(bakeRayCounting)tallyPair(n,points,sector);
#endif
        if((!direction&&!kept)||amount<=0)continue;
        float weight=amount*luminance(color);
        if(kept)addLobes(normal,to,weight,lobes);
        if(!direction)continue;
        if(isWindow) {for(int c=0;c<3;++c)windowTowards[c]+=to[c]*weight;windowTotal+=weight;continue;}
        for(int c=0;c<3;++c)towards[c]+=to[c]*weight;
        total+=weight;
        if(n<points)groups[sourceGroups[n]]+=weight;
        // The pool's own surface (facing the way the pool glows).
        else if(inside&&normal[2]*(bakeAreas[n-points].ceiling?-1:1)>0.5f)sheen+=weight;
    }
    addGlassLight(map,sector,x,y,z,normal,sum);
    float scale=capWindowLight(window);
    for(int c=0;c<3;++c) {sum[c]+=window[c];towards[c]+=windowTowards[c]*scale;}
    total+=windowTotal*scale;
    // In Doom's 32 light steps; whole-level lighting keeps every step a byte holds: far from the
    // lamps its light is faint, and 32 steps drew it in rings.
    if(settings.lightModel==1)for(int c=0;c<3;++c)out[c]=(uint8_t)std::min(255L,std::lround(sum[c]*0.5f*255));
    else for(int c=0;c<3;++c)out[c]=(uint8_t)std::min(255L,std::lround(std::round(sum[c]*32)/32*0.5f*255));
    if(direction)encodeDirection(normal,towards,total,groups,sheen,direction);
    if(kept)keepLampLobes(*kept,lobes);
}
void gatherStaticAt(const BakeMap &map,const BakeSample &item,uint8_t out[3]) {
    gatherStatic(map,item.sector,item.x,item.y,item.z,item.normal,out,nullptr);
}
// Static light for a floor/ceiling map cell (only: with directions and flicker groups).
void lightCell(const BakeMap &map,size_t i,bool only,int *blockers) {
    static const float up[3]={0,0,1},down[3]={0,0,-1};
    int sector=seamCells[i].own;
    if(sector==noSector)return;
    float px=mapOrigin[0]+(i%mapWidth+0.5f)*mapCell,py=mapOrigin[1]+(i/mapWidth+0.5f)*mapCell;
    const BakeSector &s=map.sectors[sector];
    size_t layer=(size_t)mapWidth*mapHeight;
    bool lobes=!flatLampLobes.empty();
    if(sectors[sector].floorpic!=skyflatnum)
        gatherStatic(map,sector,px,py,s.floor+1,up,flatCell(floorLight,i),only?&flatDirectionCells[4*i]:nullptr,lobes?&flatLampLobes[i]:nullptr,blockers);
    if(sectors[sector].ceilingpic!=skyflatnum)
        gatherStatic(map,sector,px,py,s.ceiling-1,down,flatCell(ceilingLight,i),only?&flatDirectionCells[4*(layer+i)]:nullptr,
                     lobes?&flatLampLobes[layer+i]:nullptr,blockers);
}
// Static light for a wall strip's atlas texels.
void lightStrip(const BakeMap &map,const BakeStrip &strip,bool only,int *blockers) {
    const float normal[3]={strip.dy,-strip.dx,0};
    forStripTexels(strip,[&](size_t i,float x,float y,float z) {
        size_t texel=i/atlasStride*atlasWidth+i%atlasStride;   // the lamp lobes' index; the direction maps are as wide as the atlas
        gatherStatic(map,strip.sector,x,y,z,normal,&wallBakeCells[4*i],only?&wallDirectionCells[4*i]:nullptr,
                     wallLampLobes.empty()?nullptr:&wallLampLobes[texel],blockers);
    });
}
// A window frame at night (Game_Mood shineOut): a thin indoor sector (at
// most 64 units across) beside the sky, as bright as it and brighter than
// every indoor sector beside it, has the outdoor level from id (E1M1's
// FLAT14 room has its frames at 255 in a 224 room). Its room, the brightest
// of those, is what shines through it; any other sector is its own room.
static const sector_t *nightRoom(const sector_t *sector) {
    if(!levelMood().shineOut||sector->ceilingpic==skyflatnum)return sector;
    int sky=-1;const sector_t *room=nullptr;
    float x0=INFINITY,y0=INFINITY,x1=-INFINITY,y1=-INFINITY;
    for(int n=0;n<sector->linecount;++n) {
        const line_t *side=sector->lines[n];
        for(const vertex_t *v:{side->v1,side->v2}) {
            x0=std::min(x0,units(v->x));x1=std::max(x1,units(v->x));
            y0=std::min(y0,units(v->y));y1=std::max(y1,units(v->y));
        }
        const sector_t *other=side->frontsector==sector?side->backsector:side->frontsector;
        if(!other||other==sector)continue;
        if(other->ceilingpic==skyflatnum)sky=std::max(sky,(int)other->lightlevel);
        else if(!room||other->lightlevel>room->lightlevel)room=other;
    }
    bool frame=sky>=0&&room&&sector->lightlevel>=sky&&sector->lightlevel>room->lightlevel&&std::min(x1-x0,y1-y0)<=64;
    return frame?room:sector;
}
// A sector's light level (given as level) at night: a window frame's is its
// room's, with the outdoors dark.
static int nightFrameLevel(const sector_t *sector,int level) {
    const sector_t *room=nightRoom(sector);
    return room==sector?level:room->lightlevel;
}
// Lamp-lit sectors: Doom maps fake a lamp's glow with a brighter sector under
// and around it (E1M1's monitor boxes stand at 255 in a 128 room). With
// baked lights that glow is drawn by the lamps themselves, falling off
// smoothly, so the sector's own extra brightness would count it twice and
// show as a hard-edged bright patch. Touching sectors at the same level are
// one region and drop together: beside a dimmer sector, without sky, by the
// baked light every cell of its floors gets (its dimmest cell), but not below
// its brightest dimmer neighbor; dropped regions pass the level on inward. A
// bright room its lamps do not fully light keeps its level, glowing pools in
// it included (E1M1's nukage room), or they would leave the rest of the room
// as a bright island.
// Darker rooms around lamps and windows (settings.dimLitRooms): a region also
// drops by the static light its floors get on average (in full: a bright
// room shows little of it until it drops), at most 60% of its level, so the
// light visibly comes from its lamps, screens and windows and far corners
// fall darker, but no further than leaves its least-lit floor point 60% of
// the level (E1M1 and E1M2 had 6% of their indoor floors under half their
// level). Every region drops by at least settings.unlitDarkening of its
// level, or a room nothing visibly lights would keep its full even light and
// look brighter and flatter than the lit rooms around it (E1M3's sector 98).
// This needs no dimmer neighbor; only regions mostly open to the sky keep
// their level. Sectors whose lamps flicker count at their low, and only
// steady light dims a region: with the lamps out, a room is as Doom has it.
// When the episode's mood turns window light round (Game_Mood shineOut),
// rooms opening onto the sky dim like any other: the night outside no
// longer lights them, so only their own lamps do. Their window frames,
// which id gave the outdoor level, take their room's (nightFrameLevel).
static void classifyLampSectors() {
    lampDrop.assign(numsectors,0.0f);
    if(flatLightCells.empty()||seamCells.empty())return;
    if(settings.lightModel==1)return;   // whole-level lighting has no level to drop (lighting(), map.cpp)
    std::vector<float> least(numsectors,INFINITY),shown(numsectors,0.0f);
    std::vector<int> cells(numsectors,0);
    std::vector<float> level(numsectors);
    for(int i=0;i<numsectors;++i)level[i]=std::clamp(nightFrameLevel(&sectors[i],lampLightLevel(&sectors[i]))/255.0f,0.12f,1.0f);
    for(size_t i=0;i<seamCells.size();++i) {
        int s=seamCells[i].own;if(s==noSector)continue;
        const uint8_t *cell=flatCell(floorLight,i);
        // Only steady light counts: flickering lamps' share (their flicker
        // group, assignLampGroups) leaves when they go out.
        float steady=1;
        if(lampGroupCount>0&&!flatDirectionCells.empty()) {
            unsigned packed=flatDirectionCells[4*i+3],group=packed>>4;
            if(group>0&&(int)group<=lampGroupCount)steady=1-(packed&15)/15.0f;
        }
        least[s]=std::min(least[s],std::max({cell[0],cell[1],cell[2]})/255.0f*2*steady);
        float rgb[3]={cell[0]/255.0f*2*steady,cell[1]/255.0f*2*steady,cell[2]/255.0f*2*steady};
        shown[s]+=bakeLuminance(rgb);++cells[s];
    }
    bool dim=settings.dimLitRooms;
    auto neighbors=[&](int i,auto &&visit) {
        for(int n=0;n<sectors[i].linecount;++n) {
            const line_t *line=sectors[i].lines[n];
            const sector_t *other=line->frontsector==&sectors[i]?line->backsector:line->frontsector;
            if(other&&other!=&sectors[i])visit(int(other-sectors));
        }
    };
    // Regions: their members, how far their lamps drop them, and whether sky keeps them.
    std::vector<int> regionOf(numsectors,-1);
    std::vector<std::vector<int>> members;std::vector<float> regionLeast;std::vector<bool> fixed;
    for(int i=0;i<numsectors;++i) {
        if(regionOf[i]>=0)continue;
        int r=(int)members.size();members.push_back({i});regionOf[i]=r;
        float dimmest=INFINITY,sum=0;bool sky=false;int count=0,open=0;
        for(size_t k=0;k<members[r].size();++k) {
            int s=members[r][k];
            dimmest=std::min(dimmest,least[s]);sky|=sectors[s].ceilingpic==skyflatnum;
            sum+=shown[s];count+=cells[s];if(sectors[s].ceilingpic==skyflatnum)open+=cells[s];
            // Dimming keeps indoor sectors out of sky regions: a window
            // frame at the outdoor level would stay at it with the outdoors.
            neighbors(s,[&](int o){if(regionOf[o]<0&&std::abs(level[o]-level[s])<0.01f&&
                (!dim||(sectors[o].ceilingpic==skyflatnum)==(sectors[s].ceilingpic==skyflatnum))){regionOf[o]=r;members[r].push_back(o);}});
        }
        if(dim&&count>0) {
            // Its least-lit floor point keeps at least 60% of the level: the
            // drop goes no further than the static light that point gets.
            float least=dimmest==INFINITY?0.0f:dimmest;
            float lit=std::min({sum/count,0.6f*level[i],least+0.4f*level[i]});
            lit=std::max(lit,settings.unlitDarkening*level[i]);
            regionLeast.push_back(std::max(least,lit));fixed.push_back(open*2>count);
        } else {regionLeast.push_back(dimmest);fixed.push_back(sky||dimmest==INFINITY);}
    }
    std::vector<float> base=level;
    for(int pass=0;pass<16;++pass) {
        bool changed=false;
        for(size_t r=0;r<members.size();++r) {
            if(fixed[r])continue;
            float now=base[members[r][0]],to=-1;
            for(int s:members[r])neighbors(s,[&](int o){if(base[o]<now-0.01f)to=std::max(to,base[o]);});
            float lowered=std::max(to,level[members[r][0]]-regionLeast[r]);
            if((to>=0||dim)&&lowered<now-0.01f) {for(int s:members[r])base[s]=lowered;changed=true;}
        }
        if(!changed)break;
    }
    // A hole in a ceiling (an enclosed sky opening, packOutdoor) brighter than
    // the room round it falls to the room's level by its enclosure: id lit the
    // floor under the hole as a hard-edged sunny patch (E1M6's slot, sector
    // 167, at 255 in a 144 corridor), which the hole's rim lamps
    // (addWindowLights) and the baked sun now draw.
    if(!levelMood().shineOut)for(int i=0;i<numsectors;++i) {
        if(sectors[i].ceilingpic!=skyflatnum)continue;
        float enclosed=0;packOutdoor(&sectors[i],&enclosed);
        int opening=Rooms_SkyOpening(i);
        if(enclosed<=0||opening<0)continue;
        float sum=0,open=0;
        for(int k=0;k<Rooms_SkyIndoorCount(opening);++k) {const rooms_sky_indoor &in=*Rooms_SkyIndoor(opening,k);sum+=in.length*base[in.sector];open+=in.length;}
        if(open>0&&sum/open<base[i])base[i]+=(sum/open-base[i])*enclosed;
    }
    for(int i=0;i<numsectors;++i)lampDrop[i]=std::clamp(lampLightLevel(&sectors[i])/255.0f,0.12f,1.0f)-base[i];
}
// The sky's color: the upper rows of the sky texture, above any horizon art,
// halfway to white.
std::array<float,3> skyColor() {
    // Phobos: no sky light, only Mars-shine, in Mars' rusty butterscotch.
    if(Game_Phobos())return {1.0f,0.6f,0.4f};
    const Image &sky=wallImage(skytexture);
    const byte *palette=(const byte*)W_CacheLumpName((char*)"PLAYPAL",PU_CACHE);
    double sum[3]={};
    for(int y=0;y<std::max(1,sky.height*2/5);++y)for(int x=0;x<sky.width;++x) {
        size_t i=(size_t)y*sky.width+x;if(!sky.pixels[2*i+1])continue;
        for(int c=0;c<3;++c)sum[c]+=palette[sky.pixels[2*i]*3+c];
    }
    double peak=std::max({sum[0],sum[1],sum[2],1e-9});
    std::array<float,3> tint;
    for(int c=0;c<3;++c)tint[c]=(float)(0.5+0.5*sum[c]/peak);
    return tint;
}
// The colour the sky lights the level in: the sky's colour as it is drawn,
// under the hour's tint (climate.cpp's mood.skyTint: dawn red, night blue;
// sky.frag tints the painting by it), and under cloud (overcast) the
// deck's: the weather's own colour (nukage green), else a cool grey. Peak 1.
// The sky's light in the bounce and the sun under cloud (placeSun) take it.
std::array<float,3> skyLightColor() {
    std::array<float,3> color=skyColor();
    float cover=overcast();
    if(cover>0) {
        const WeatherKind &kind=weatherKind(climateNow().weather?falling():WEATHER_NONE);
        std::array<float,3> cloud=kind.tint?kind.tint():std::array<float,3>{0.9f,0.93f,1.0f};
        float peak=std::max({cloud[0],cloud[1],cloud[2],1e-4f});
        for(int c=0;c<3;++c)color[c]+=(cloud[c]/peak-color[c])*cover;
    }
    // The hour tints the cloud as it tints the sky: a grey dusk is still a warm one.
    const game_mood &mood=levelMood();
    for(int c=0;c<3;++c)color[c]*=mood.skyTint[c];
    float peak=std::max({color[0],color[1],color[2],1e-4f});
    for(float &c:color)c/=peak;
    return color;
}
// The sky's color at the horizon, as sky.frag draws its low rows: what
// distant outdoor surfaces fade into (Game_Sun haze).
std::array<float,3> skyHorizon() {
    const Image &sky=wallImage(skytexture);
    const byte *palette=(const byte*)W_CacheLumpName((char*)"PLAYPAL",PU_CACHE);
    double sum[3]={},count=0;
    // The sky's own low rows: where ground fills the painting's foot (2025
    // Heretic) it browned the haze over everything.
    skyLayersImage();
    int rows=skyPaintRows?skyPaintRows:sky.height;
    for(int y=rows*88/100;y<std::max(rows*88/100+1,rows*96/100);++y)for(int x=0;x<sky.width;++x) {
        size_t i=(size_t)y*sky.width+x;if(!sky.pixels[2*i+1])continue;
        for(int c=0;c<3;++c)sum[c]+=palette[sky.pixels[2*i]*3+c]/255.0;
        ++count;
    }
    return {(float)(count?sum[0]/count:0.5),(float)(count?sum[1]/count:0.5),(float)(count?sum[2]/count:0.5)};
}
// Window light: every opening from an indoor sector into a sky sector
// (windows, doorways, the rim of a hole in the ceiling) is a soft lamp facing
// inward, built as glowing panels 2 units inside it, about 32 units wide and
// at most 64 tall. They shine at the sky sector's light level in the sky's
// color paled further, so the outdoors lights the rooms beside it as Doom
// shows it, not as its dark textures would bounce it. A panel's strength
// follows its area (and settings.windowStrength): a long window lights the
// floor beside it well past the outdoor level before the headroom eases it,
// a slot barely; light fades out over 448 units. With sun shadows, each
// 32-unit column is weighted by the sun on the outdoor floor 16-80 units
// outside it against its sunniest neighbors (below), so the sunny side of a
// room leads; capWindowLight keeps several windows from adding past the
// outdoor level.
// An episode whose mood turns window light round (Game_Mood shineOut) has
// the panels 2 units outside instead, facing out, shining by how much
// brighter the room is than the dark outdoors (three times that, as the
// night makes the eye see more of it), in the room's lamp color made
// bolder (nightRoomColors), reaching half as far and thrown straight out
// (beam 4: half as strong 33 degrees aside): the room's light falls in
// lanes out of its windows and doors onto the ground outside.
// A room's color at night, per sector: its lamps' color (the lights already
// in sources sitting in it, by strength; white with none), saturated 2.5
// times as much and scaled to a peak of 1. Not its flats: E1M1's blue
// carpet threw blue light out of the FLAT14 room.
static std::vector<std::array<float,3>> nightRoomColors(const std::vector<BakeLight> &sources) {
    std::vector<std::array<float,3>> lamp(numsectors,{0,0,0});
    for(const BakeLight &light:sources) {
        int s=int(R_PointInSubsector((fixed_t)(light.x*FRACUNIT),(fixed_t)(light.y*FRACUNIT))->sector-sectors);
        for(int c=0;c<3;++c)lamp[s][c]+=light.color[c]*light.strength;
    }
    std::vector<std::array<float,3>> colors(numsectors);
    for(int i=0;i<numsectors;++i) {
        float lit=std::max({lamp[i][0],lamp[i][1],lamp[i][2]});
        std::array<float,3> color;
        for(int c=0;c<3;++c)color[c]=lit>0?lamp[i][c]/lit:1.0f;
        float grey=0.299f*color[0]+0.587f*color[1]+0.114f*color[2];
        for(float &c:color)c=std::max(0.0f,grey+(c-grey)*2.5f);
        float peak=std::max({color[0],color[1],color[2]});
        for(float &c:color)c=peak>0?c/peak:1.0f;
        colors[i]=color;
    }
    return colors;
}
static void addWindowLights(std::vector<BakeLight> &sources) {
    bool sunny=settings.sun&&sunBaked&&sunLevel>0&&!contactCells.empty()&&!seamCells.empty();
    const game_mood &mood=levelMood();
    std::array<float,3> tint=skyColor();
    for(float &c:tint)c=0.5f+0.5f*c;
    float peak=std::max({tint[0],tint[1],tint[2]});
    for(float &c:tint)c/=peak;
    std::vector<std::array<float,3>> roomColors;
    if(mood.shineOut)roomColors=nightRoomColors(sources);
    struct Window { float x,y,nx,ny,low,height,strength,sunlit;int rows;std::array<float,3> color;bool overhead=false,back=false;int door=-1; };
    Rooms_Build();
    std::vector<Window> windows;
    for(int i=0;i<numlines;++i) {
        const line_t &line=lines[i];
        if(!line.frontsector||!line.backsector)continue;
        bool frontSky=line.frontsector->ceilingpic==skyflatnum,backSky=line.backsector->ceilingpic==skyflatnum;
        if(frontSky==backSky)continue;
        const sector_t *in=frontSky?line.backsector:line.frontsector,*out=frontSky?line.frontsector:line.backsector;
        // Whole-level lighting: a porch is outside, lit by the sky it sees (lightRule), so its
        // open edge is no window.
        if(settings.lightModel==1&&Rooms_Outside(int(in-sectors)))continue;
        float low=std::max(floorZ(in),floorZ(out)),high=std::min(ceilZ(in),ceilZ(out)),top;
        // A door shut at load (E1M1's computer-room wall onto the yard) gets
        // the lamp it has open, sized by the height it opens to, sitting just
        // inside it: shut, the door hides it from both sides; bakeMap follows
        // the door and relight.cpp re-bakes round it as it opens.
        bool shut=high-low<8&&Rooms_Door(int(in-sectors),&top);
        if(shut)high=std::min(top,ceilZ(out));
        if(high-low<8)continue;
        float ax=units(line.v1->x),ay=units(line.v1->y),dx=units(line.v2->x)-ax,dy=units(line.v2->y)-ay;
        float length=std::hypot(dx,dy);if(length<8)continue;
        dx/=length;dy/=length;
        // The front side lies to the right of v1->v2.
        float nx=in==line.frontsector?dy:-dy,ny=in==line.frontsector?-dx:dx;
        // The rim of a hole in the ceiling over a floor that runs on under
        // it: the light comes down through the hole, not in at the floor.
        // One lamp per column at the roof's edge, just inside the hole,
        // shining all round as a 64-unit window would: the floor under the
        // roof and the floor under the hole fall off from it alike, where
        // panels down to the floor facing in lit the roofed side brightest
        // right at the line, leaving a dark rim round the hole's own floor.
        float enclosed=0;
        if(!shut&&!mood.shineOut&&std::fabs(floorZ(in)-floorZ(out))<=24&&ceilZ(in)<=ceilZ(out))packOutdoor(out,&enclosed);
        bool overhead=enclosed>0.5f;
        int columns=std::max(1,(int)std::lround(length/32)),rows=overhead?1:std::max(1,(int)std::ceil((high-low)/64));
        float width=length/columns,height=overhead?std::min(high-low,64.0f):(high-low)/rows;
        if(overhead)low=high-4-height;
        float outdoor=std::clamp(out->lightlevel/255.0f*mood.outdoor,0.12f,1.0f),level=outdoor;
        std::array<float,3> color=tint;
        if(mood.shineOut) {
            // A window frame often has the outdoor level; it shines as its room.
            float room=nightFrameLevel(in,in->lightlevel)/255.0f;
            color=roomColors[nightRoom(in)-sectors];
            level=(std::clamp(room,0.12f,1.0f)-outdoor)*3;
            if(level<0.08f)continue; // A room no brighter than the night outside.
            nx=-nx;ny=-ny;
        }
        float strength=1.6f*settings.windowStrength*level*width/128*height/96;
        int outside=int(out-sectors);
        for(int column=0;column<columns;++column) {
            float along=(column+0.5f)*width,px=ax+dx*along,py=ay+dy*along;
            // How sunlit the ground just outside is (0-1; -1 unknown).
            float seen=0,count=0;
            if(sunny&&!mood.shineOut)for(float reach:{16.0f,40.0f,80.0f}) {
                int cx=(int)std::floor((px-nx*reach-mapOrigin[0])/mapCell),cy=(int)std::floor((py-ny*reach-mapOrigin[1])/mapCell);
                if(cx<0||cy<0||cx>=mapWidth||cy>=mapHeight)continue;
                size_t cell=(size_t)cy*mapWidth+cx;
                if(seamCells[cell].own!=outside)continue;
                seen+=contactCells[4*cell+2]/255.0f;++count;
            }
            windows.push_back({px,py,nx,ny,low,height,strength,count>0?seen/count:-1,rows,color,overhead,shut&&mood.shineOut,shut?int(in-sectors):-1});
        }
    }
    // A window onto a sunny courtyard takes over the room from those onto
    // shade: each column against the sunniest within its reach, 0.35 for
    // full shade beside full sun. Where all are alike (all shaded, or no sun)
    // they keep their strength, so a room's light still leans on its windows.
    int shining=0;
    for(const Window &w:windows) {
        float sunlit=1;
        if(w.sunlit>=0) {
            float best=w.sunlit;
            for(const Window &o:windows)
                if(o.sunlit>best&&(o.x-w.x)*(o.x-w.x)+(o.y-w.y)*(o.y-w.y)<448.0f*448)best=o.sunlit;
            if(best>0.05f)sunlit=0.35f+0.65f*w.sunlit/best;
        }
        if(mood.shineOut)++shining;
        if(w.overhead) {
            sources.push_back({w.x-w.nx*2,w.y-w.ny*2,w.low+w.height,448.0f,w.strength*sunlit,0.5f,{w.color[0],w.color[1],w.color[2]}});
            windowDoors.push_back(-1);
            continue;
        }
        for(int row=0;row<w.rows;++row) {
            sources.push_back({w.x+w.nx*(w.back?-2:2),w.y+w.ny*(w.back?-2:2),w.low+(row+0.5f)*w.height,mood.shineOut?224.0f:448.0f,w.strength*sunlit,0.5f,
                               {w.color[0],w.color[1],w.color[2]},w.height*0.5f,w.nx,w.ny,mood.shineOut?4.0f:0.0f});
            windowDoors.push_back(w.door);
        }
    }
    if(mood.shineOut)fprintf(stderr,"3D bake: %d window columns shine out into the night.\n",shining);
}
// How far a lamp's light goes on past its radius in the bake, in radii (BakeLight::tail; settings.lampTail).
constexpr float lampTailReach=3.0f;
// Whole-level lighting: how far a lamp's light is followed, in units (a negative BakeLight::tail).
constexpr float wholeReach=1024;
static float lampTailOf() {return settings.lightModel==1?-wholeReach:settings.lampTail>0?lampTailReach:0.0f;}
// A merged surface light as the bake keeps it.
static BakeLight surfaceSource(const SurfaceLight &light) {
    auto n=panelNormal(light);
    BakeLight source={light.x,light.y,light.z,light.radius,light.strength,0.35f,{light.color[0],light.color[1],light.color[2]},
                      std::min(light.extent,light.radius),n[0],n[1]};
    source.tail=lampTailOf();
    source.strength*=dayWeak(light.x,light.y);
    return source;
}
// Whether the bake takes a glowing tile into a point light; with bake-only
// lights, pools light as areas instead.
static bool bakedPoint(const SurfaceLight &tile,bool only) {
    return !only||tile.key[0]!=0||!poolSurface(tile.key[1],tile.key[2]);
}
// Calls visit with each bake grid cell's list over x0..x1, y0..y1.
template<class Visit> static void forGridCells(float x0,float y0,float x1,float y1,Visit visit) {
    auto gridX=[&](float x){return std::clamp((int)std::floor((x-mapOrigin[0])/bakeGridCell),0,bakeGridWidth-1);};
    auto gridY=[&](float y){return std::clamp((int)std::floor((y-mapOrigin[1])/bakeGridCell),0,bakeGridHeight-1);};
    for(int y=gridY(y0);y<=gridY(y1);++y)for(int x=gridX(x0);x<=gridX(x1);++x)visit(bakeGrid[(size_t)y*bakeGridWidth+x]);
}
BakeLight remergedBakeSource(int n,const std::set<SurfaceLightKey> &also) {
    BakeLight source=bakeSources[n];
    const SurfaceLightKey &cell=bakeSourceKeys[n];
    bool only=lightBakeKey&2;
    std::vector<SurfaceLight> left;
    for(const auto &tile:surfaceLights)
        if(!tileShotOut(tile.key)&&!also.count(tile.key)&&bakedPoint(tile,only)&&surfaceCellOf(tile)==cell)left.push_back(tile);
    std::vector<SurfaceLight> merged=mergedSurfaceLights(left);
    if(merged.size()!=1) {source.strength=0;return source;}
    return surfaceSource(merged[0]);
}
void listBakeSource(int n,const BakeLight &light) {
    // Listed in order, as bakeLights lists them.
    float reach=bakeReach(light);
    forGridCells(light.x-reach,light.y-reach,light.x+reach,light.y+reach,[&](std::vector<int> &list) {
        auto at=std::lower_bound(list.begin(),list.end(),n);
        if(at==list.end()||*at!=n)list.insert(at,n);
    });
}
void remergeBakeSource(int n) {
    BakeLight &source=bakeSources[n];
    BakeLight now=remergedBakeSource(n,{});
    if(now.strength<=0) {source.strength=0;return;}
    source=now;
    listBakeSource(n,source);
    rebuildLightRoom(n);   // its tiles and centre moved: its spaces again
}
// Flicker groups for lamps in flickering sectors (lampOut): each such
// sector's merged lights take one of groups 1..lampGroupCount, which the
// bake records like a flickering decoration's (encodeDirection), so their
// light leaves with its baked shadows and steps when they switch off, in
// every mode, dimming as they fade (lampShine). Sectors share a group only
// 1024 units or more apart, where the one nearest the eye sets it
// (updateLampGroups). Decorations hash into the groups left (flickerGroup).
int lampGroupCount=0;
float lampGroupFlicker[flickerGroups];
struct LampGroupSector { int sector; float x,y; };
static std::vector<std::vector<LampGroupSector>> lampGroupSectors;
static std::vector<int> lampGroupOf; // Per sector; 0 for none.
static void assignLampGroups(const std::vector<SurfaceLight> &merged) {
    lampGroupCount=0;lampGroupSectors.assign(flickerGroups,{});lampGroupOf.assign(numsectors,0);
    std::map<int,std::array<float,3>> centers; // Per sector: x and y sums, count.
    for(const auto &light:merged)if(light.flickerSector>=0&&light.strength>0) {
        auto &c=centers[light.flickerSector];c[0]+=light.x;c[1]+=light.y;c[2]+=1;
    }
    for(const auto &[sector,c]:centers) {
        float x=c[0]/c[2],y=c[1]/c[2],farthest=-1;int pick=1;
        for(int g=1;g<flickerGroups;++g) {
            float nearest=INFINITY;
            for(const auto &o:lampGroupSectors[g])nearest=std::min(nearest,std::hypot(o.x-x,o.y-y));
            if(nearest>=1024) {pick=g;break;}
            if(nearest>farthest) {farthest=nearest;pick=g;}
        }
        lampGroupSectors[pick].push_back({sector,x,y});lampGroupOf[sector]=pick;
        lampGroupCount=std::max(lampGroupCount,pick);
    }
}
void updateLampGroups(const float eye[3]) {
    for(int g=0;g<flickerGroups;++g)lampGroupFlicker[g]=1;
    for(int g=1;g<=lampGroupCount&&g<(int)lampGroupSectors.size();++g) {
        const LampGroupSector *nearest=nullptr;float best=INFINITY;
        for(const auto &o:lampGroupSectors[g]) {
            float d=std::hypot(o.x-eye[0],o.y-eye[1]);
            if(d<best) {best=d;nearest=&o;}
        }
        if(!nearest)continue;
        // A sector whose lamps were all shot out flickers nothing: what its group still marks
        // in the bake (the quick re-bake leaves the marks, bake_moved.cpp) is other lamps' light.
        bool left=false;
        for(size_t n=0;n<bakeSourceKeys.size()&&n<bakeSources.size()&&!left;++n)
            left=bakeSourceKeys[n][0]==4+nearest->sector&&bakeSources[n].strength>0;
        lampGroupFlicker[g]=left?lampShine(nearest->sector):1;
#ifdef DOOM_DEVTOOLS
        // DERP_FLICKER=0..1: every lamp group held there, to see the two states in stills.
        if(const char *held=getenv("DERP_FLICKER"))lampGroupFlicker[g]=(float)atof(held);
#endif
    }
}
bool lampGroupSector(int sector) {return sector>=0&&sector<(int)lampGroupOf.size()&&lampGroupOf[sector]>0;}
// Static lights, baked into the floor/ceiling map and the wall atlas rgb:
// fullbright decorations and (with emissive textures) merged surface lights,
// unflickered, with the sector heights in bakeMap. Values step by 1/32, like the
// 32 light rows of COLORMAP. Bake-only lights also light liquid pools as
// areas and record each texel's light direction and flicker group.
void bakeLights() {
    loadingStage(LoadStage::Lights);
    finishRelight();
    lightBakeKey=lightBakeInputs();++lightBakeSerial;
    bakeTailShare=settings.lampTail;
    bool only=lightBakeKey&2;
    if(flatLightCells.empty())return;
    uint64_t start=SDL_GetTicksNS();
    std::vector<BakeLight> &sources=bakeSources;sources.clear();sourceGroups.clear();bakeAreas.clear();bakeSourceKeys.clear();
    std::vector<SurfaceLight> merged;
    if(settings.emissive) {
        if(only)buildPoolAreas();
        std::vector<SurfaceLight> points;
        for(const auto &light:surfaceLights)
            if(bakedPoint(light,only))points.push_back(light);
        merged=mergedSurfaceLights(points);
    }
    assignLampGroups(merged); // Before decorations take the groups left.
    findGlassWindows();
    for(int i=0;i<numsectors;++i)for(mobj_t *thing=sectors[i].thinglist;thing;thing=thing->snext)
        if(decorationLight(*thing)) {
            sources.push_back(decorationSource(*thing));sources.back().tail=lampTailOf();
            sources.back().strength*=dayWeak(sources.back().x,sources.back().y);
            sourceGroups.push_back((uint8_t)flickerGroup(*thing));
        }
    // Whole-level lighting: each merged floor or ceiling lamp keeps the places of the tiles it
    // stands for (BakeLight::tiles), at most lampTilesMost of them, evenly picked.
    static std::vector<float> lampTiles;
    std::map<SurfaceLightKey,std::vector<std::array<float,3>>> tilesOf;
    if(settings.lightModel==1)for(const auto &tile:surfaceLights)
        if(tile.facing<2&&tile.strength>0&&bakedPoint(tile,only))tilesOf[surfaceCellOf(tile)].push_back({tile.x,tile.y,tile.z});
    constexpr size_t lampTilesMost=12;
    std::vector<std::array<size_t,3>> kept;   // source, first float, count
    lampTiles.clear();
    for(const auto &light:merged) {
        sources.push_back(surfaceSource(light));
        sourceGroups.push_back((uint8_t)(light.flickerSector>=0?lampGroupOf[light.flickerSector]:0));
        bakeSourceKeys.resize(sources.size()-1);bakeSourceKeys.push_back(light.key);
        auto found=tilesOf.find(light.key);
        if(found==tilesOf.end()||found->second.size()<2)continue;
        const auto &all=found->second;
        size_t count=std::min(all.size(),lampTilesMost),first=lampTiles.size();
        for(size_t k=0;k<count;++k) {const auto &p=all[k*all.size()/count];lampTiles.insert(lampTiles.end(),p.begin(),p.end());}
        kept.push_back({sources.size()-1,first,count});
    }
    for(const auto &k:kept) {sources[k[0]].tiles=lampTiles.data()+k[1];sources[k[0]].tileCount=(int)k[2];}
    // Directions (and flicker groups) with bake-only lights, or for lamps that switch.
    bool directed=only||lampGroupCount>0;
    firstWindowSource=(int)sources.size();windowDoors.clear();
    // Whole-level lighting: the sky comes in through the openings in the bounce bake (bake_bounce.cpp's sky dome).
    if(settings.windowLight&&!(settings.lightModel==1&&settings.skyLight&&settings.bounce)) {addWindowLights(sources);sourceGroups.resize(sources.size(),0);}
    if(settings.doorwayLight&&settings.lightModel==0) {addDoorwayLights(sources,windowDoors,firstWindowSource);sourceGroups.resize(sources.size(),0);}
    bakeSourceKeys.resize(sources.size());
    int points=(int)sources.size();
#ifdef DOOM_DEVTOOLS
    // DERP_NOFLICKLAMPS: the lamps of flickering sectors are baked out, to compare with their off state.
    if(getenv("DERP_NOFLICKLAMPS"))for(size_t n=0;n<sourceGroups.size()&&n<sources.size();++n)
        if(sourceGroups[n]>0&&sourceGroups[n]<=lampGroupCount)sources[n].strength=0;
    // DERP_NOLAMPS: every lamp a shot could put out is baked out, to compare with a level shot dark.
    if(getenv("DERP_NOLAMPS"))for(size_t n=0;n<bakeSourceKeys.size()&&n<sources.size();++n)if(bakeSourceKeys[n][0]>=2)sources[n].strength=0;
#endif
    bakeGridWidth=(int)std::ceil(mapWidth*mapCell/bakeGridCell);bakeGridHeight=(int)std::ceil(mapHeight*mapCell/bakeGridCell);
    bakeGrid.assign((size_t)bakeGridWidth*bakeGridHeight,{});
    auto list=[&](int n,float x0,float y0,float x1,float y1) {
        forGridCells(x0,y0,x1,y1,[&](std::vector<int> &cell){cell.push_back(n);});
    };
    for(int n=0;n<points;++n) {
        const BakeLight &light=sources[n];
        float reach=bakeReach(light);
        list(n,light.x-reach,light.y-reach,light.x+reach,light.y+reach);
    }
    for(size_t n=0;n<bakeAreas.size();++n) {
        const BakeArea &area=bakeAreas[n];
        list(points+(int)n,area.low[0]-area.radius,area.low[1]-area.radius,area.high[0]+area.radius,area.high[1]+area.radius);
    }
    buildLightRooms();   // the spaces each light can reach (light_rooms.cpp): a texel skips the lights it can't
    // The direction maps (bake_basis.cpp): lamp directions in their first
    // halves, the HL2 basis in their second.
    bakeDirected=directed;
    bool basis=settings.bakedBumps!=0;
    if(directed||basis) {
        wallDirectionCells.assign((size_t)atlasStride*atlasHeight*4,0);
        flatDirectionCells.assign((size_t)mapWidth*mapHeight*4*4,0);
    } else {wallDirectionCells.clear();flatDirectionCells.clear();}
    if(basis) {
        wallLampLobes.assign((size_t)atlasWidth*atlasHeight,{});
        flatLampLobes.assign((size_t)mapWidth*mapHeight*2,{});
    } else {wallLampLobes.clear();flatLampLobes.clear();}
    // The loops, from an earlier visit if the bake cache has them (bake_cache.cpp); the rest is cheap.
    if(!bakeCacheRestore(LoadStage::Lights,BAKE_BUILT,glassLightInputs())) {
        // Each row and each strip starts with no blockers (bakeRayOpen) and hands them on along
        // itself only, so what it bakes does not depend on which thread baked what before it.
        const size_t lights=sources.size()+bakeAreas.size();
        // On the graphics card when it can (gpu_lights.cpp): every cell's and texel's light in one batch.
        std::vector<BakeSample> items;std::vector<StaticTarget> targets;
        bool card=gpuBakesOn();
        if(card) {
            for(size_t i=0;i<(size_t)mapWidth*mapHeight;++i)staticItemsOfCell(bakeMap,i,directed,items,targets);
            for(int index:bakeOrder)staticItemsOfStrip(bakeStrips[index],directed,items,targets);
            card=gpuGatherStatic(bakeMap,items,targets,directed);
            if(card)fprintf(stderr,"3D lights: %zu texels and cells on the graphics card.\n",items.size());
            // DERP_GPULIGHTCHECK=1: the same texels baked here too, and the ones that differ most listed.
            if(card&&getenv("DERP_GPULIGHTCHECK")) {
                std::vector<std::array<uint8_t,8>> gpu(items.size());
                for(size_t k=0;k<items.size();++k) {
                    std::memcpy(gpu[k].data(),targets[k].out,3);
                    if(targets[k].direction)std::memcpy(gpu[k].data()+4,targets[k].direction,4);
                }
                parallelFor(mapHeight,[&](int y) {
                    std::vector<int> blockers(lights,-1);
                    for(int x=0;x<mapWidth;++x)lightCell(bakeMap,(size_t)y*mapWidth+x,directed,blockers.data());
                });
                parallelFor((int)bakeOrder.size(),[&](int n) {
                    std::vector<int> blockers(lights,-1);
                    lightStrip(bakeMap,bakeStrips[bakeOrder[n]],directed,blockers.data());
                });
                std::vector<std::pair<int,size_t>> worst;
                for(size_t k=0;k<items.size();++k) {
                    int off=0;
                    for(int c=0;c<3;++c)off=std::max(off,std::abs((int)gpu[k][c]-(int)targets[k].out[c]));
                    if(targets[k].direction)for(int c=0;c<4;++c)off=std::max(off,std::abs((int)gpu[k][4+c]-(int)targets[k].direction[c]));
                    if(off>2)worst.push_back({off,k});
                }
                std::sort(worst.rbegin(),worst.rend());
                size_t outside=0;
                for(const auto &w:worst)outside+=!bakeInsideSector(bakeMap,items[w.second].sector,items[w.second].x,items[w.second].y);
                fprintf(stderr,"GPU light check: %zu of %zu items off by more than 2, %zu of them outside their sector.\n",worst.size(),items.size(),outside);
                std::stable_partition(worst.begin(),worst.end(),[&](const auto &w){return bakeInsideSector(bakeMap,items[w.second].sector,items[w.second].x,items[w.second].y);});
                for(size_t w=0;w<std::min<size_t>(worst.size(),10);++w) {
                    size_t k=worst[w].second;const BakeSample &s=items[k];const StaticTarget &t=targets[k];
                    fprintf(stderr,"  off %d at (%.2f, %.2f, %.2f) sector %d normal (%.2f, %.2f, %.2f): gpu %d %d %d / %d %d %d %d, cpu %d %d %d / %d %d %d %d\n",
                        worst[w].first,s.x,s.y,s.z,s.sector,s.normal[0],s.normal[1],s.normal[2],gpu[k][0],gpu[k][1],gpu[k][2],gpu[k][4],gpu[k][5],gpu[k][6],gpu[k][7],
                        t.out[0],t.out[1],t.out[2],t.direction?t.direction[0]:0,t.direction?t.direction[1]:0,t.direction?t.direction[2]:0,t.direction?t.direction[3]:0);
                }
            }
        }
#ifdef DOOM_DEVTOOLS
        if(!card)startRayTally();
#endif
        if(!card)parallelFor(mapHeight,[&](int y) {
            std::vector<int> blockers(lights,-1);
            for(int x=0;x<mapWidth;++x)lightCell(bakeMap,(size_t)y*mapWidth+x,directed,blockers.data());
        });
        if(!card)parallelFor((int)bakeOrder.size(),[&](int n) {
            std::vector<int> blockers(lights,-1);
            lightStrip(bakeMap,bakeStrips[bakeOrder[n]],directed,blockers.data());
        });
#ifdef DOOM_DEVTOOLS
        if(!card)reportRayTally();
        // DERP_BAKEDUMP=<file>: the lamp light as baked (floors and ceilings, walls, their directions and lobes) into
        // <file>.lights, to hold one bake against another byte for byte (PLAN-room-light-filter.md).
        if(const char *dump=getenv("DERP_BAKEDUMP"))if(FILE *f=fopen((std::string(dump)+".lights").c_str(),"wb")) {
            for(const auto *cells:{&flatLightCells,&wallBakeCells,&flatDirectionCells,&wallDirectionCells})fwrite(cells->data(),1,cells->size(),f);
            for(const auto *lobes:{&flatLampLobes,&wallLampLobes})fwrite(lobes->data(),sizeof((*lobes)[0]),lobes->size(),f);
            fclose(f);
        }
#endif
        bakeCacheKeep(LoadStage::Lights);
    }
    uploadFlatLight();
    uploadWallBake();
    writeLampBasis();   // the bounce bake adds the rest where it runs
    uploadDirections();
    classifyLampSectors();
    relightBaked();
    solveRoomFill();   // again once the bounce is baked, where it is on
    fprintf(stderr,"3D lights: %zu static lights (%zu window panels)%s baked in %.0f ms.\n",sources.size(),sources.size()-firstWindowSource,
        only?(", "+std::to_string(bakeAreas.size())+" pools, bake-only").c_str():"",(SDL_GetTicksNS()-start)/1e6);
}
void lightBakeNewLevel() {
    lightBakeKey=-1;
    bakeSources.clear();bakeSourceKeys.clear();windowDoors.clear();bakeGrid.clear();bakeAreas.clear();sourceGroups.clear();lampDrop.clear();roomFillShutdown();
    lampGroupCount=0;lampGroupSectors.clear();lampGroupOf.clear();
    wallDirectionCells.clear();flatDirectionCells.clear();wallDirectionTexture=nullptr;flatDirectionTexture=nullptr;
    wallLampLobes.clear();flatLampLobes.clear();bakeDirected=false;
}
void lightBakeShutdown() {lightBakeNewLevel();bakedLightsActive=false;bakeOnlyActive=false;}
} // namespace doom3d
