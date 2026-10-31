/* Wear: what the weather has left on the map's surfaces, drawn by
   world/wear.glsl over the artwork, read from the map itself.
   - Window streaks (settings.windowStreaks): rain has run down the outside
     wall below every window looking out under the sky; rusty on metal.
     Inside, under a broken pane, they start as rain blows in, with the
     floor and sill beneath going dark and wet (weather.cpp's soak).
   - Leaks (while it rains): a few rooms beside the open sky have a crack in
     the ceiling, damp round it, rain dripping through (weather.cpp) onto a
     small puddle below.
   - Water splashes (wetSplash): where a plunge threw water out of its pool
     (puddles.cpp), the floor and walls it came down on lie wet. They stay.
   - Drafts (drafts.cpp): the trails the wind lays in through the buildings
     and rime round cold openings, passed on as they are.
   The shader finds them in the sector buffer after the sector entries:
   per sector its first record, how many (the drafts' trails stored last
   and counted apart: others + 4096 trails), a seed; then the records, two
   entries each (kind, three numbers; four more). */
#include "internal.h"
#include "../rooms.h"

namespace doom3d {
namespace {
// Record kinds, as world/wear.glsl reads them.
enum { WEAR_STREAK=1,WEAR_WET=3,WEAR_CRACK=4,WEAR_PUDDLE=5,WEAR_FROST=6,WEAR_ICE_CRACK=7,WEAR_SPLASH=8 };
struct Record { int sector; std::array<float,4> a,b; };
unsigned wearLevelSerial=~0u,wearPaneSerial=~0u;
bool built=false;
std::vector<Record> streaks;
// Below a broken pane, inside: the sill and the floor of the room in front
// of it (strip a-b, wet depth units in), and the wall under the sill on the
// room's side (key: line*2+side).
struct Inside { int pane,frame,inner,key;float sill,ax,ay,bx,by,depth;bool metal; };
std::vector<Inside> insides;
std::vector<CeilingLeak> leaks;
bool leaksFound=false;
// Water splashes: (WEAR_SPLASH, center), (radius, seed, -, amount). Every
// texel of a sector reads all its records, so they are few: one close to
// another of its sector grows into it, and the oldest go first.
std::vector<Record> splashes;
constexpr size_t maxSplashes=48;

bool metalWall(int texture) {
    if(texture<=0)return false;
    char name[9];R_TextureName(texture,name);
    return Game_WallSurface(name)==GAME_SURFACE_METAL;
}
// The side of a line facing a sector (-1: none).
int sideFacing(const line_t &line,int sector) {
    for(int side=0;side<2;++side)
        if(line.sidenum[side]>=0&&sides[line.sidenum[side]].sector==&sectors[sector])return side;
    return -1;
}

// The windows: outside, the wall below each pane under the sky, its run
// from the sill down; inside, what rain coming through will wet.
void buildStreaks() {
    streaks.clear();insides.clear();
    for(int i=0;i<windowPaneCount();++i) {
        PaneView p;
        if(!windowPane(i,p)||!p.sky)continue;
        const line_t &line=lines[p.line];
        int side=sideFacing(line,p.outer);
        if(side<0)continue;
        float sill=floorZ(&sectors[p.frame]);
        float ax=units((side?line.v2:line.v1)->x),ay=units((side?line.v2:line.v1)->y),bx=units((side?line.v1:line.v2)->x),by=units((side?line.v1:line.v2)->y);
        bool metal=metalWall(sides[line.sidenum[side]].bottomtexture);
        Record r={p.outer,{(float)WEAR_STREAK,(float)(p.line*2+side),sill,metal?-1.0f:1.0f},{ax,ay,bx,by}};
        streaks.push_back(r);
        // Inside: the frame's lines onto the room in front of it.
        if(p.inner<0)continue;
        const sector_t &frame=sectors[p.frame];
        for(int k=0;k<frame.linecount;++k) {
            const line_t &in=*frame.lines[k];
            if(!in.frontsector||!in.backsector)continue;
            int s=sideFacing(in,p.inner);
            if(s<0)continue;
            float ix=units((s?in.v2:in.v1)->x),iy=units((s?in.v2:in.v1)->y),jx=units((s?in.v1:in.v2)->x),jy=units((s?in.v1:in.v2)->y);
            // Blown in, the rain reaches about a third of the opening's height past the sill.
            float depth=std::clamp((p.top-floorZ(&sectors[p.inner]))*0.45f,16.0f,72.0f);
            insides.push_back({i,p.frame,p.inner,int(&in-lines)*2+s,sill,ix,iy,jx,jy,depth,metalWall(sides[in.sidenum[s]].bottomtexture)});
        }
    }
    if(windowPaneCount())fprintf(stderr,"3D wear: %zu window streaks, %zu window insides.\n",streaks.size(),insides.size());
}

// The leaks: in rooms of the level's first state that stay put (no tag,
// no door), tall enough, dry underfoot, under a ceiling that does not glow,
// in a room bordering the sky. Each such room's crack goes where its floor lies
// furthest from the walls, at least 24 units off; then up to four of them,
// picked by the level's own hash and 384 units apart.
void findLeaks() {
    leaks.clear();
    if(contactCells.size()<(size_t)mapWidth*mapHeight*4||seamCells.size()<(size_t)mapWidth*mapHeight)return;
    leaksFound=true;
    std::vector<char> fit(numsectors,0);
    for(int i=0;i<numsectors;++i) {
        const sector_t &s=sectors[i];
        if(s.tag||s.ceilingpic==skyflatnum||s.floorpic==skyflatnum||units(s.ceilingheight)-units(s.floorheight)<72)continue;
        if((size_t)i<sectorMist.size()&&sectorMist[i].liquid)continue;
        if(lumpImage(firstflat+flattranslation[s.ceilingpic],true).emissionWeight>0)continue;
        fit[i]=1;
    }
    // Beside the sky: the room next to a sky sector, or one sector away
    // (through a window's frame, a door, a step). Sectors under one
    // ceiling height are one room (a walkway and the pools beside it), so
    // stepping between them costs nothing.
    std::vector<int> fromSky(numsectors,99);
    for(int i=0;i<numsectors;++i)if(sectors[i].ceilingpic==skyflatnum)fromSky[i]=0;
    for(int i=0;i<numlines;++i)if(lines[i].special&&lines[i].backsector)fit[lines[i].backsector-sectors]=0; // A manual door.
    for(bool changed=true;changed;) {
        changed=false;
        for(int i=0;i<numlines;++i) {
            const line_t &line=lines[i];
            if(!line.frontsector||!line.backsector)continue;
            int f=int(line.frontsector-sectors),b=int(line.backsector-sectors);
            bool room=line.frontsector->ceilingpic!=skyflatnum&&line.backsector->ceilingpic!=skyflatnum&&
                      line.frontsector->ceilingheight==line.backsector->ceilingheight;
            int cost=room?0:1;
            if(fromSky[f]+cost<fromSky[b]) {fromSky[b]=fromSky[f]+cost;changed=true;}
            if(fromSky[b]+cost<fromSky[f]) {fromSky[f]=fromSky[b]+cost;changed=true;}
        }
    }
    for(int i=0;i<windowPaneCount();++i) {PaneView p;if(windowPane(i,p))fit[p.frame]=0;}
    uint32_t seed=hash32((uint32_t)gameepisode*7919u+(uint32_t)gamemap*104729u+0x6c8e9cf5u);
    std::vector<int> best(numsectors,-1);
    std::vector<float> bestScore(numsectors,-1);
    for(size_t i=0;i<seamCells.size();++i) {
        int own=seamCells[i].own;
        if(own==noSector||!fit[own]||fromSky[own]>2)continue;
        float room=contactCells[4*i]/255.0f*48;
        if(room<24)continue;
        float score=room+8*hashUnit((int)(i%mapWidth),(int)(i/mapWidth),seed);
        if(score>bestScore[own]) {bestScore[own]=score;best[own]=(int)i;}
    }
    std::vector<int> order;
    for(int i=0;i<numsectors;++i)if(best[i]>=0)order.push_back(i);
    std::sort(order.begin(),order.end(),[&](int a,int b){return hash32((uint32_t)a^seed)<hash32((uint32_t)b^seed);});
    for(int sector:order) {
        if(leaks.size()>=4)break;
        int cell=best[sector];
        float x=mapOrigin[0]+(cell%mapWidth+0.5f)*mapCell,y=mapOrigin[1]+(cell/mapWidth+0.5f)*mapCell;
        x=std::floor(x)+0.5f;y=std::floor(y)+0.5f;
        bool apart=true;
        for(const CeilingLeak &l:leaks)apart=apart&&std::hypot(l.x-x,l.y-y)>=384;
        if(apart)leaks.push_back({sector,x,y,hash32((uint32_t)sector*2654435761u^seed)});
    }
    if(!leaks.empty()) {
        fprintf(stderr,"3D wear: %zu ceiling leaks (sectors",leaks.size());
        for(const CeilingLeak &l:leaks)fprintf(stderr," %d",l.sector);
        fprintf(stderr,").\n");
    }
}

bool wearOn() {
    return worldPending&&levelSerial==r_levelserial&&built&&(settings.windowStreaks||settings.weather||Game_Phobos()||!splashes.empty());
}
}

// Each tic: the streaks once the level's first tic has found its panes, and
// again whenever the panes are found anew.
void watchWear() {
    if(gamestate!=GS_LEVEL||levelSerial!=r_levelserial)return;
    if(wearLevelSerial!=r_levelserial) {wearLevelSerial=r_levelserial;built=true;wearPaneSerial=~0u;splashes.clear();}
    if(wearPaneSerial!=windowPaneSerial()) {wearPaneSerial=windowPaneSerial();buildStreaks();leaksFound=false;}
    if(!leaksFound&&falling()!=WEATHER_NONE)findLeaks();   // only rain drips through them
}

void wetSplash(int sector,float x,float y,float z,float radius) {
    if(sector<0||sector>=numsectors||levelSerial!=r_levelserial)return;
    for(Record &r:splashes) {
        if(r.sector!=sector||std::abs(r.a[3]-z)>radius)continue;
        float apart=std::hypot(r.a[1]-x,r.a[2]-y);
        if(apart>(r.b[0]+radius)*0.5f)continue;
        r.b[0]=std::min(48.0f,std::max(r.b[0],apart+radius)*1.05f);
        return;
    }
    if(splashes.size()>=maxSplashes)splashes.erase(splashes.begin());
    uint32_t seed=hash32((uint32_t)std::lround(x*7+y*13+z*3));
    splashes.push_back({sector,{(float)WEAR_SPLASH,x,y,z},{radius,(float)(seed&0xffff),0,1}});
}
// Whether rain blown in through a broken window has soaked the floor at
// (x, y) in a sector: well inside the wet patch in front of the window.
bool rainSoaked(int sector,float x,float y) {
    if(!weatherKind(falling()).wets)return false;
    for(const Inside &in:insides) {
        if(sector!=in.frame&&sector!=in.inner)continue;
        float soak=paneSoak(in.pane);
        if(soak<0.3f)continue;
        float ex=in.bx-in.ax,ey=in.by-in.ay,len=std::hypot(ex,ey);
        if(len<1)continue;
        float dx=x-in.ax,dy=y-in.ay,along=(dx*ex+dy*ey)/len,across=std::abs(dx*ey-dy*ex)/len;
        if(along>=-2&&along<=len+2&&across<=in.depth*(0.25f+0.75f*soak)*0.75f)return true;
    }
    return false;
}

// The leaks while it rains.
const std::vector<CeilingLeak> &ceilingLeaks() {
    static const std::vector<CeilingLeak> none;
    return settings.weather&&weatherNow(WEATHER_RAIN)>0.01f&&levelSerial==r_levelserial?leaks:none;
}

// After the sector entries and the fog bank's (fog.cpp).
float wearBase() {return wearOn()?(float)(numsectors+fogBankCount()):0.0f;}

// The records this frame, after the sector entries: the streaks, and the
// insides of broken windows as wet as they have soaked.
void appendWear(std::vector<std::array<float,4>> &info) {
    if(!wearOn()||info.size()!=(size_t)(numsectors+fogBankCount()))return;
    std::vector<const Record*> all;
    std::vector<Record> dynamic;
    // On Phobos it has never rained: no streaks, no soaked sills.
    bool rained=!Game_Phobos();
    if(settings.windowStreaks&&rained)for(const Record &r:streaks)all.push_back(&r);
    for(const Record &r:splashes)all.push_back(&r);
    const WeatherKind &kind=weatherKind(falling());
    for(const Inside &in:insides) {
        if(!rained||(!kind.settles&&!kind.wets))break;   // dry ice leaves the sill as it was
        float soak=paneSoak(in.pane);
        if(soak<0.01f)continue;
        // Weather that settles runs down no wall: it lies white instead (wear.glsl).
        bool settles=weatherKind(falling()).settles;
        if(settings.windowStreaks&&!settles)dynamic.push_back({in.inner,{(float)WEAR_STREAK,(float)in.key,in.sill,(in.metal?-1.0f:1.0f)*soak},{in.ax,in.ay,in.bx,in.by}});
        // The wet spreads out from the sill as it soaks, dark from the start;
        // snow whitens it as it settles.
        float amount=settles?soak:std::min(1.0f,0.5f+soak);
        for(int s:{in.frame,in.inner})dynamic.push_back({s,{(float)WEAR_WET,in.depth*(0.25f+0.75f*soak),0,amount},{in.ax,in.ay,in.bx,in.by}});
    }
    // Each leak: the crack (center, reach) and the puddle under it (center, radius).
    for(const CeilingLeak &l:ceilingLeaks()) {
        float seed=(float)(l.seed&0xffff);
        dynamic.push_back({l.sector,{(float)WEAR_CRACK,l.x,l.y,14+6*hashUnit(1,0,l.seed)},{seed,0,0,0}});
        dynamic.push_back({l.sector,{(float)WEAR_PUDDLE,l.x,l.y,7+4*hashUnit(2,0,l.seed)},{seed,0,0,1}});
    }
    // Phobos: frost round each breach (decompression.cpp), on both sides of the opening.
    std::vector<BreachFrost> frosted;breachFrost(frosted);
    for(const BreachFrost &f:frosted)for(int s:f.sectors)
        if(s>=0&&sectors[s].ceilingpic!=skyflatnum)dynamic.push_back({s,{(float)WEAR_FROST,40+40*f.frost,0,f.frost},{f.ax,f.ay,f.bx,f.by}});
    for(const IceCrack &k:iceCracksNow())
        dynamic.push_back({k.sector,{(float)WEAR_ICE_CRACK,k.x,k.y,k.reach},{(float)(k.seed&0xffff),0,0,0}});
    // The drafts' trails and the rime round cold openings (drafts.cpp).
    std::vector<WearMark> drafts;draftMarks(drafts);
    for(const WearMark &m:drafts)if(m.sector>=0&&m.sector<numsectors)dynamic.push_back({m.sector,m.a,m.b});
    // What snow and rain lay at doorways opened during play (blown_in.cpp).
    std::vector<WearMark> doors;blownInMarks(doors);
    for(const WearMark &m:doors)if(m.sector>=0&&m.sector<numsectors)dynamic.push_back({m.sector,m.a,m.b});
    // The craters blasts and the like dug in it indoors (disturbances.cpp).
    std::vector<WearMark> craters;disturbanceMarks(craters);
    for(const WearMark &m:craters)if(m.sector>=0&&m.sector<numsectors)dynamic.push_back({m.sector,m.a,m.b});
    for(const Record &r:dynamic)all.push_back(&r);
    // Per sector the others first, the drafts' trails (kind 9) last and
    // counted apart (count + 4096 trails): only the floor reads them.
    std::stable_partition(all.begin(),all.end(),[](const Record *r){return r->a[0]!=9;});
    std::vector<int> counts(numsectors,0),trails(numsectors,0);
    for(const Record *r:all)if(r->sector>=0&&r->sector<numsectors)++(r->a[0]==9?trails:counts)[r->sector];
    // How open to the wind the ground is: out under the sky the sector's own
    // (air_push.cpp's gustShare); under a roof in the open air (a porch, a
    // doorless hall) the open ground's it opens onto, so the yard and the
    // porch are one ground, the same ripples and gusts across the roof's line.
    static std::vector<float> groundOpen;
    static unsigned groundSerial=~0u;
    if(groundSerial!=r_levelserial||groundOpen.size()!=(size_t)numsectors) {
        groundSerial=r_levelserial;
        groundOpen.assign(numsectors,-1);
        for(int s=0;s<numsectors;++s)if(underSky(s))groundOpen[s]=gustShare(s);
        for(int pass=0;pass<4;++pass)
            for(int n=0;n<numlines;++n) {
                const line_t &line=lines[n];
                if(!line.frontsector||!line.backsector)continue;
                int f=int(line.frontsector-sectors),k=int(line.backsector-sectors);
                for(auto [a,b]:{std::pair{f,k},std::pair{k,f}})
                    if(groundOpen[a]<0&&groundOpen[b]>=0&&Rooms_Outside(a))groundOpen[a]=groundOpen[b];
            }
        for(float &o:groundOpen)o=std::max(o,0.0f);
    }
    size_t base=info.size();
    info.resize(base+numsectors);
    size_t first=base+numsectors;
    std::vector<size_t> next(numsectors);
    for(int s=0;s<numsectors;++s) {
        // w: seed (bits 0-15), how open to the wind in fifteenths (16-19), the open air (20, rooms.c).
        info[base+s]={(float)first,(float)(std::min(counts[s],4095)+4096*trails[s]),poolIceCode(s),
                      (float)((hash32((uint32_t)s*2654435761u)&0xffff)+65536*std::lround(groundOpen[s]*15)+(Rooms_Outside(s)?1048576:0))};
        next[s]=first;first+=2*(counts[s]+trails[s]);
    }
    info.resize(first);
    for(const Record *r:all) {
        if(r->sector<0||r->sector>=numsectors)continue;
        info[next[r->sector]++]=r->a;info[next[r->sector]++]=r->b;
    }
}

void wearShutdown() {streaks.clear();insides.clear();leaks.clear();splashes.clear();leaksFound=false;built=false;wearLevelSerial=wearPaneSerial=~0u;}
} // namespace doom3d
