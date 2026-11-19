/* Lights by room (PLAN-room-light-filter.md): the spaces each baked light and
   pool can get its light into, so the bake skips a light whose rays could
   not arrive anyway, a lamp behind a wall.

   A space is a room as rooms.c joins them (a sheltered space and an opening
   each count as one), or a yard under the sky. Two spaces join where the
   bake's ray (traceRay) can cross a line between them: a two-sided line with
   a gap between the higher floor and the lower ceiling, any line between two
   sky sectors (a ray passes over a sky neighbour's ceiling), and any line
   beside a sector the game can move (doors, lifts, floors: open or shut, as
   if open, so the sets hold for the whole level).

   From the spaces a light starts in (its centre, its tiles, a pool's
   pieces), its set floods across every join whose line lies within its
   reach. Every point a light reaches lies within its reach, and a straight
   ray to it crosses only lines that do too (the distance to a point or a
   box never peaks between the ends of a segment), so no lit texel is ever
   left out; the trace still decides what each one gets. Generous on
   purpose: an extra space costs rays, a missing one costs light. */
#include "internal.h"
#include "light_rooms.h"
#include "../rooms.h"
#include <bitset>
#include <functional>

namespace doom3d {
namespace {
int spaceCount=0;
std::vector<int> sectorSpace;             // per sector: its space, -1 for a place every light may reach
struct Join { int to; float ax,ay,bx,by; };
std::vector<std::vector<Join>> joins;     // per space: the lines that cross to another, with that space
std::vector<std::vector<uint64_t>> reach; // per light, then per pool: a bit per space
size_t words=0;

// A sector's space: its room, or its yard under the sky.
int spaceOf(int sector) {
    int room=Rooms_Of(sector);
    if(room>=0)return room;
    int yard=Rooms_Yard(sector);
    return yard>=0?Rooms_Count()+yard:-1;
}
float segmentPointDistance(float ax,float ay,float bx,float by,float px,float py) {
    float ex=bx-ax,ey=by-ay,length2=ex*ex+ey*ey;
    float t=length2>0?std::clamp(((px-ax)*ex+(py-ay)*ey)/length2,0.0f,1.0f):0;
    return std::hypot(ax+ex*t-px,ay+ey*t-py);
}
// The distance from a line segment to a box (0 where they meet).
float segmentBoxDistance(float ax,float ay,float bx,float by,const float low[2],const float high[2]) {
    auto inside=[&](float x,float y){return x>=low[0]&&x<=high[0]&&y>=low[1]&&y<=high[1];};
    if(inside(ax,ay)||inside(bx,by))return 0;
    const float cx[4]={low[0],high[0],high[0],low[0]},cy[4]={low[1],low[1],high[1],high[1]};
    float best=INFINITY;
    for(int k=0;k<4;++k) {
        float px=cx[k],py=cy[k],qx=cx[(k+1)%4],qy=cy[(k+1)%4];
        // Crossing an edge: they meet.
        float d1=(bx-ax)*(py-ay)-(by-ay)*(px-ax),d2=(bx-ax)*(qy-ay)-(by-ay)*(qx-ax);
        float d3=(qx-px)*(ay-py)-(qy-py)*(ax-px),d4=(qx-px)*(by-py)-(qy-py)*(bx-px);
        if(((d1>0)!=(d2>0))&&((d3>0)!=(d4>0)))return 0;
        best=std::min(best,segmentPointDistance(ax,ay,bx,by,px,py));
    }
    auto toBox=[&](float x,float y) {return std::hypot(std::max({low[0]-x,0.0f,x-high[0]}),std::max({low[1]-y,0.0f,y-high[1]}));};
    return std::min({best,toBox(ax,ay),toBox(bx,by)});
}
int spaceAt(float x,float y) {
    return sectorSpace[int(R_PointInSubsector((fixed_t)(x*FRACUNIT),(fixed_t)(y*FRACUNIT))->sector-sectors)];
}
// Flood from the starting spaces across joins within reach (within: by the line's segment).
void flood(std::vector<uint64_t> &bits,const std::vector<int> &starts,const std::function<bool(const Join&)> &within) {
    bits.assign(words,0);
    std::vector<int> todo;
    for(int s:starts) {
        if(s<0) {std::fill(bits.begin(),bits.end(),~0ull);return;}   // starts where every light may reach: everywhere
        if(!(bits[s>>6]>>(s&63)&1)) {bits[s>>6]|=1ull<<(s&63);todo.push_back(s);}
    }
    while(!todo.empty()) {
        int s=todo.back();todo.pop_back();
        for(const Join &j:joins[s]) {
            if(bits[j.to>>6]>>(j.to&63)&1)continue;
            if(!within(j))continue;
            bits[j.to>>6]|=1ull<<(j.to&63);todo.push_back(j.to);
        }
    }
}
// A lamp: from its centre's space and its tiles', as far as its reach and the spread of its tiles.
void floodLamp(int n) {
    const BakeLight &light=bakeSources[n];
    std::vector<int> starts{spaceAt(light.x,light.y)};
    float spread=0;
    for(int k=0;light.tiles&&k<light.tileCount;++k) {
        float tx=light.tiles[3*k],ty=light.tiles[3*k+1];
        spread=std::max(spread,std::hypot(tx-light.x,ty-light.y));
        starts.push_back(spaceAt(tx,ty));
    }
    float r=bakeReach(light)+spread+1;
    flood(reach[n],starts,[&](const Join &j){return segmentPointDistance(j.ax,j.ay,j.bx,j.by,light.x,light.y)<r;});
}
// A pool: from the spaces of its pieces and sample points, as far as its reach from its box.
void floodPool(size_t a) {
    const BakeArea &area=bakeAreas[a];
    std::vector<int> starts;
    for(const auto &s:area.samples)starts.push_back(spaceAt(s[0],s[1]));
    for(const auto &piece:area.pieces) {
        if(piece.empty())continue;
        float cx=0,cy=0;
        for(const auto &p:piece) {cx+=p[0];cy+=p[1];}
        starts.push_back(spaceAt(cx/piece.size(),cy/piece.size()));
    }
    float r=area.radius+1;
    flood(reach[bakeSources.size()+a],starts,[&](const Join &j){return segmentBoxDistance(j.ax,j.ay,j.bx,j.by,area.low,area.high)<r;});
}
// Off unless asked for (2026-11-18): as built it saved 0-6% of the rays on E1 and changed the light on E1M2,
// E1M3, E1M6 and E1M8, so a ray crosses some line the joins call shut. PLAN-room-light-filter.md, "Result".
#ifdef DOOM_DEVTOOLS
bool filterOff() {static const bool off=[]{const char *v=getenv("DERP_ROOMFILTER");return !(v&&!strcmp(v,"1"));}();return off;}
#else
bool filterOff() {return true;}
#endif
}

void buildLightRooms() {
    Rooms_Build();
    spaceCount=Rooms_Count()+Rooms_YardCount();
    words=(size_t)(spaceCount+63)/64;
    sectorSpace.assign(numsectors,-1);
    for(int s=0;s<numsectors;++s)sectorSpace[s]=spaceOf(s);
    // Sectors the game can move: tagged by a line with a special, behind a line with one (a
    // manual door), or a door by rooms.c's reading.
    std::vector<char> mover(numsectors,0);
    std::vector<char> tagUsed(65536,0);
    for(int i=0;i<numlines;++i) {
        if(!lines[i].special)continue;
        tagUsed[(uint16_t)lines[i].tag]=1;
        if(lines[i].backsector)mover[lines[i].backsector-sectors]=1;
    }
    for(int s=0;s<numsectors;++s)if((sectors[s].tag&&tagUsed[(uint16_t)sectors[s].tag])||Rooms_Door(s,nullptr))mover[s]=1;
#ifdef DOOM_DEVTOOLS
    // DERP_LIGHTROOMSSHUT=1: movers count as they stand at load (a shut door joins nothing), to measure what that saves.
    if(getenv("DERP_LIGHTROOMSSHUT"))std::fill(mover.begin(),mover.end(),0);
#endif
    // The joins: per space, the lines that cross to another, with that space.
    joins.assign(spaceCount,{});
    for(const BakeLine &line:bakeMap.lines) {
        if(line.front<0||line.back<0||line.front==line.back)continue;
        int a=sectorSpace[line.front],b=sectorSpace[line.back];
        if(a<0||b<0||a==b)continue;
        const BakeSector &f=bakeMap.sectors[line.front],&k=bakeMap.sectors[line.back];
        bool open=std::min(f.ceiling,k.ceiling)>std::max(f.floor,k.floor)||(f.sky&&k.sky)||mover[line.front]||mover[line.back];
        if(!open)continue;
        joins[a].push_back({b,line.ax,line.ay,line.bx,line.by});
        joins[b].push_back({a,line.ax,line.ay,line.bx,line.by});
    }
    const int points=(int)bakeSources.size();
    reach.assign(points+bakeAreas.size(),{});
    for(int n=0;n<points;++n)floodLamp(n);
    for(size_t a=0;a<bakeAreas.size();++a)floodPool(a);
#ifdef DOOM_DEVTOOLS
    // DERP_LIGHTROOMS=1: how many spaces each light reaches; =2 also each light and pool on its own line.
    if(const char *show=getenv("DERP_LIGHTROOMS")) {
        size_t joinCount=0;for(const auto &j:joins)joinCount+=j.size();
        double share=0;size_t everywhere=0;
        auto count=[&](const std::vector<uint64_t> &bits){size_t c=0;for(uint64_t w:bits)c+=std::bitset<64>(w).count();return std::min(c,(size_t)spaceCount);};
        for(const auto &bits:reach) {size_t c=count(bits);share+=spaceCount?(double)c/spaceCount:0;everywhere+=c>=(size_t)spaceCount;}
        fprintf(stderr,"light rooms: %d spaces (%d rooms, %d yards), %zu joins; %zu lights and pools reach %.1f%% of the spaces on average, %zu everywhere.\n",
                spaceCount,Rooms_Count(),Rooms_YardCount(),joinCount/2,reach.size(),reach.empty()?0.0:100*share/reach.size(),everywhere);
        // DERP_LIGHTROOMSAT=sector,sector: per sector, its space and the pools and the number of lamps that reach it.
        if(const char *at=getenv("DERP_LIGHTROOMSAT"))for(const char *q=at;q&&*q;) {
            int s=atoi(q);
            if(s>=0&&s<numsectors) {
                size_t lamps=0;std::string pools;
                for(size_t n=0;n<reach.size();++n)if(lightReachesSector((int)n,s)) {
                    if((int)n<points)++lamps;else pools+=" "+std::to_string(n-points);
                }
                fprintf(stderr,"light rooms: sector %d (space %d) is reached by %zu of %d lamps and pools:%s.\n",s,sectorSpace[s],lamps,points,pools.empty()?" none":pools.c_str());
            }
            q=strchr(q,',');if(q)++q;
        }
        if(atoi(show)>=2)for(size_t n=0;n<reach.size();++n) {
            bool pool=(int)n>=points;
            float x=pool?(bakeAreas[n-points].low[0]+bakeAreas[n-points].high[0])*0.5f:bakeSources[n].x;
            float y=pool?(bakeAreas[n-points].low[1]+bakeAreas[n-points].high[1])*0.5f:bakeSources[n].y;
            float r=pool?bakeAreas[n-points].radius:bakeReach(bakeSources[n]);
            fprintf(stderr,"light rooms: %s %zu at %.0f,%.0f reach %.0f: %zu of %d spaces.\n",pool?"pool":"lamp",pool?n-points:n,x,y,r,count(reach[n]),spaceCount);
        }
    }
#endif
}
void rebuildLightRoom(int light) {
    if(light<0||(size_t)light>=reach.size()||joins.empty())return;
    if(light<(int)bakeSources.size())floodLamp(light);else floodPool((size_t)light-bakeSources.size());
}
bool lightReachesSector(int light,int sector) {
    if(filterOff()||light<0||(size_t)light>=reach.size()||sector<0||(size_t)sector>=sectorSpace.size())return true;
    int s=sectorSpace[sector];
    if(s<0)return true;
    return reach[light][s>>6]>>(s&63)&1;
}
} // namespace doom3d
