/* How much of the open air's weather the wind carries in under a roof, per
   map cell (0-1): 1 under the sky; under a roof (a porch, a doorless
   corridor, a garage, the floor round a skylight) what blows in from the
   nearest open sky, through open space only (never through a wall, a shut
   door, a door's or a window frame's sector: indoors behind those is the
   drafts' and the floor film's), fading with the way it has to go, by how
   far the wind carries it: downwind the full reach, upwind what the eddies
   round the roof's edge bring (two fifths of it), across halfway between.
   The reach is the wind's alone, the opening's size never: in still air a
   strip under the roof's edge (what falls straight down there), in a gale
   most of the way to the back of a deep porch; snow and ash ride furthest,
   sand less and it climbs no step taller than it blows, rain least.
   Baked with the ground's wetness (weather.cpp traceWetness, which lays
   the falling kind's ground there by it), at level load and when the
   wind or the weather turns; round a lift or a lowering wall that came to
   rest, over the cells it may change. The weather that blows: it never
   fades over time, it was blowing before the player came. */
#include "internal.h"
#include "../rooms.h"

namespace doom3d {
namespace {
std::vector<float> air;         // per map cell, 0-1
unsigned airSerial=~0u;
// Per sector: the open air's weather may blow through it (not a door's or a window frame's sector).
std::vector<char> passes;
std::unordered_set<uint64_t> joined;   // sector pairs across an open line (a gap of 8 units or more, no whole glass)

void findPasses() {
    passes.assign(numsectors,1);
    for(int s=0;s<numsectors;++s)if(Rooms_Door(s,nullptr))passes[s]=0;
    for(int i=0;i<Rooms_FrameCount();++i) {const rooms_frame *f=Rooms_Frame(i);if(f&&f->sector>=0&&f->sector<numsectors)passes[f->sector]=0;}
    std::vector<char> whole(numlines,0);
    for(int n=0;n<windowPaneCount();++n) {PaneView p;if(windowPane(n,p)&&!p.broken&&p.line>=0&&p.line<numlines)whole[p.line]=1;}
    joined.clear();
    for(int n=0;n<numlines;++n) {
        const line_t &line=lines[n];
        if(!line.frontsector||!line.backsector||whole[n])continue;
        const BakeSector &f=bakeMap.sectors[line.frontsector-sectors],&b=bakeMap.sectors[line.backsector-sectors];
        if(std::min(f.ceiling,b.ceiling)-std::max(f.floor,b.floor)<8)continue;
        uint64_t p=(uint64_t)(line.frontsector-sectors),q=(uint64_t)(line.backsector-sectors);
        joined.insert(p<q?p<<32|q:q<<32|p);
    }
}
}

float openAirReach(Weather kind) {
    float scale=kind==WEATHER_RAIN||kind==WEATHER_NUKAGE?0.35f:kind==WEATHER_SAND?0.8f:kind==WEATHER_ASH?1.1f:1.0f;
    return (16+224*std::pow(windAmount(),1.2f))*scale;
}

void bakeOpenAir(Weather kind,float hx,float hy,int x0,int y0,int x1,int y1) {
    size_t cells=(size_t)mapWidth*mapHeight;
    if(seamCells.size()<cells||bakeMap.sectors.size()!=(size_t)numsectors)return;
    Rooms_Build();
    uint64_t start=SDL_GetTicksNS();   // ROOMSDEBUG=1 times it
    bool whole=airSerial!=r_levelserial||air.size()!=cells;
    if(whole) {airSerial=r_levelserial;air.assign(cells,0);x0=0;y0=0;x1=mapWidth-1;y1=mapHeight-1;}
    findPasses();
    const float reach=openAirReach(kind),lee=12+0.4f*reach;
    // What blows low (sand) climbs no step taller than a quarter of how high it blows.
    const float climb=weatherKind(kind).lift*windCarry()/4;
    // The share kept over a step to each of the eight neighbours, by the way it goes against the wind.
    static const int dx[8]={1,1,0,-1,-1,-1,0,1},dy[8]={0,1,1,1,0,-1,-1,-1};
    float keep[8];
    for(int d=0;d<8;++d) {
        float l=std::hypot((float)dx[d],(float)dy[d]),cosine=(dx[d]*hx+dy[d]*hy)/l;
        float along=lee+(reach-lee)*(0.5f+0.5f*cosine);   // across the wind halfway
        keep[d]=std::exp(-l*mapCell/along);
    }
    // The open air (rooms.c: the sky, porches, the doorless recesses off them) is one ground:
    // all of it feeds what blows on in, so it carries on past an opening into a room and
    // thins there, never stopping on the opening's line.
    auto sky=[&](int own){return own!=noSector&&Rooms_Outside(own);};
    for(int y=y0;y<=y1;++y)for(int x=x0;x<=x1;++x) {
        size_t i=(size_t)y*mapWidth+x;int own=seamCells[i].own;
        air[i]=sky(own)?1.0f:0.0f;
    }
    // From a neighbour at (x - dx, y - dy) into (x, y), stepping the way d goes.
    auto from=[&](size_t i,int own,int x,int y,int d) {
        int nx=x-dx[d],ny=y-dy[d];
        if(nx<0||ny<0||nx>=mapWidth||ny>=mapHeight)return;
        size_t n=(size_t)ny*mapWidth+nx;
        float there=air[n]*keep[d];
        if(there<=air[i])return;
        int other=seamCells[n].own;
        if(other==noSector||!passes[other])return;
        if(other!=own) {
            uint64_t p=(uint64_t)other,q=(uint64_t)own;
            if(!joined.count(p<q?p<<32|q:q<<32|p))return;
            if(bakeMap.sectors[own].floor-bakeMap.sectors[other].floor>climb)return;
        }
        air[i]=there;
    };
    for(int pass=0;pass<2;++pass) {
        for(int y=y0;y<=y1;++y)for(int x=x0;x<=x1;++x) {
            size_t i=(size_t)y*mapWidth+x;int own=seamCells[i].own;
            if(own==noSector||!passes[own]||sky(own))continue;
            for(int d:{0,1,2,3})from(i,own,x,y,d);   // from the left and the row before
        }
        for(int y=y1;y>=y0;--y)for(int x=x1;x>=x0;--x) {
            size_t i=(size_t)y*mapWidth+x;int own=seamCells[i].own;
            if(own==noSector||!passes[own]||sky(own))continue;
            for(int d:{4,5,6,7})from(i,own,x,y,d);   // from the right and the row after
        }
    }
    static const bool debug=getenv("ROOMSDEBUG")!=nullptr;
    if(debug)fprintf(stderr,"OPEN AIR tic %d: cells %dx%d, reach %.0f, in %.2f ms\n",leveltime,x1-x0+1,y1-y0+1,reach,(SDL_GetTicksNS()-start)/1e6);
}

float openAirAt(size_t cell) {
    return airSerial==r_levelserial&&cell<air.size()?air[cell]:0.0f;
}
bool openAirBaked() {return airSerial==r_levelserial&&air.size()==(size_t)mapWidth*mapHeight;}
void openAirShutdown() {air.clear();passes.clear();joined.clear();airSerial=~0u;}
} // namespace doom3d
