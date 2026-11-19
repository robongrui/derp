/* The room's fill (whole-level lighting, settings.lightModel 1;
   PLAN-light-model.md): the one light with no source. Doom's maps draw too
   few fixtures to light their rooms, so each room (rooms.c's, never a
   sector) keeps one even light: what the light level its mapper gave it
   asks for beyond what the solve already shows there. "Already shows" is
   what a typical surface of the room shows, the median over its floors,
   ceilings and walls by area of the lamps' light through the tone curve
   plus the bounce: lamp light lies in pools, and the average would say a
   room was lit whose corners are black. So a room its lights carry gets
   none, a room with a lamp in one corner most of its level, a room with no
   fixtures all of it, and nothing changes on a line inside a room.
   The way through between two rooms (an opening's or a door's sector) takes
   the mean of the rooms it joins, so the step from one room's fill to the
   next is halved there.
   Worked out once, when the bakes it reads are done. lighting() (map.cpp)
   shows it, times the player's dial (settings.fillLight, no re-bake) and
   times how far the sector's light stands below its steady level now, so
   a light switch, a trap or a flicker still darkens the room.
   Outdoors the sky is the fill, and the unit is the yard (rooms.c's): every
   sector under the sky takes its yard's one level, the median of the levels
   the mapper gave the yard's sky sectors, by area. What the sky and the sun
   make of it at each place is traced (lightRule), so a strip the mapper
   darkened to paint a shadow is no longer a second shadow beside the real
   one, and no line shows where two sectors of one yard meet. */
#include "internal.h"
#include "../rooms.h"

namespace doom3d {
namespace {
std::vector<float> fill;   // per sector, 0-1; empty until solved
std::vector<float> yard;   // per sector outside (under the sky or a porch): its yard's level, 0-1; -1 elsewhere
unsigned fillSerial=~0u;
constexpr int steps=64;
constexpr float most=1.5f; // the tone curve's limit is 1.25
struct Spread {
    double area[steps]={},total=0;
    void add(float light,double size) {
        int step=std::clamp((int)(light/most*steps),0,steps-1);
        area[step]+=size;total+=size;
    }
    float median() const {
        if(total<=0)return 0;
        double run=0;
        for(int n=0;n<steps;++n) {run+=area[n];if(run>=total*0.5)return (n+0.5f)/steps*most;}
        return most;
    }
};
float shown(const uint8_t *light,const uint8_t *bounce) {
    float direct[3]={light[0]/255.0f*2,light[1]/255.0f*2,light[2]/255.0f*2},again[3]={bounce[0]/255.0f*2,bounce[1]/255.0f*2,bounce[2]/255.0f*2};
    return bakeShoulder(bakeLuminance(direct),bakeHeadroom(0))+bakeLuminance(again)*bounceStrength;
}
bool passage(int sector) {return Rooms_Kind(sector)==ROOM_OPENING||Rooms_Door(sector,nullptr);}
}

// Each yard's level: the median of its sky sectors' steady levels, by map cells.
static void solveYards() {
    int yards=Rooms_YardCount();
    std::vector<std::array<uint32_t,256>> levels(yards);
    for(auto &l:levels)l.fill(0);
    for(size_t i=0;i<seamCells.size();++i) {
        int s=seamCells[i].own;
        if(s==noSector||sectors[s].ceilingpic!=skyflatnum)continue;
        if(int y=Rooms_Yard(s);y>=0)++levels[y][std::clamp(lampPeakLevel(s),0,255)];
    }
    std::vector<float> median(yards,-1.0f);
    for(int y=0;y<yards;++y) {
        uint64_t total=0,run=0;
        for(uint32_t n:levels[y])total+=n;
        if(!total)continue;
        for(int l=0;l<256;++l) {run+=levels[y][l];if(run*2>=total) {median[y]=l/255.0f;break;}}
    }
    yard.assign(numsectors,-1.0f);
#ifdef DOOM_DEVTOOLS
    // DERP_PORCHES: lists the roofed outside sectors, to find one to look at.
    if(getenv("DERP_PORCHES"))for(int s=0;s<numsectors;++s)if(Rooms_Outside(s)&&sectors[s].ceilingpic!=skyflatnum&&sectors[s].linecount)
        fprintf(stderr,"porch %d yard %d at %.0f,%.0f floor %.0f ceiling %.0f level %d\n",s,Rooms_Yard(s),units(sectors[s].lines[0]->v1->x),units(sectors[s].lines[0]->v1->y),
                floorZ(&sectors[s]),ceilZ(&sectors[s]),sectors[s].lightlevel);
#endif
    // Porches too (the open air under a roof belongs to the yard it opens onto).
    // Light by logic: every yard at full daylight (lighting() then takes the hour's share of it).
    for(int s=0;s<numsectors;++s)if(int y=Rooms_Yard(s);y>=0&&(sectors[s].ceilingpic==skyflatnum||Rooms_Outside(s)))yard[s]=lightByLogic()&&median[y]>=0?1.0f:median[y];
}
void solveRoomFill() {
    fill.clear();yard.clear();fillSerial=r_levelserial;
    if(settings.lightModel!=1||flatLightCells.empty()||wallBakeCells.empty()||seamCells.empty())return;
    Rooms_Build();
    solveYards();
    int rooms=Rooms_Count();
    std::vector<Spread> spread(rooms);
    std::vector<double> want(rooms,0.0),floor(rooms,0.0);
    const double cellArea=(double)mapCell*mapCell;
    for(size_t i=0;i<seamCells.size();++i) {
        int s=seamCells[i].own;if(s==noSector)continue;
        int r=Rooms_Of(s);if(r<0||Rooms_Outside(s))continue;
        if(sectors[s].floorpic!=skyflatnum) {
            spread[r].add(shown(flatCell(floorLight,i),flatCell(floorBounce,i)),cellArea);
            want[r]+=std::clamp(lampPeakLevel(s)/255.0f,0.12f,1.0f)*cellArea;floor[r]+=cellArea;
        }
        if(sectors[s].ceilingpic!=skyflatnum)spread[r].add(shown(flatCell(ceilingLight,i),flatCell(ceilingBounce,i)),cellArea);
    }
    for(int index:bakeOrder) {
        const BakeStrip &strip=bakeStrips[index];
        if(strip.sector<0||strip.sector>=numsectors||strip.scale<=0)continue;
        int r=Rooms_Of(strip.sector);if(r<0||Rooms_Outside(strip.sector))continue;
        double texel=1.0/((double)strip.scale*strip.scale);
        for(int row=0;row<strip.height;++row)for(int column=0;column<strip.width;++column) {
            size_t at=4*((size_t)(strip.y+row)*atlasStride+strip.x+column);
            spread[r].add(shown(&wallBakeCells[at],&wallBakeCells[at+4*(size_t)atlasWidth]),texel);
        }
    }
    std::vector<float> roomFill(rooms,0.0f);
    int lit=0,counted=0;double sum=0;
    for(int r=0;r<rooms;++r) {
        if(floor[r]<=0)continue;
        roomFill[r]=std::max(0.0f,(float)(want[r]/floor[r])-spread[r].median());
        ++counted;sum+=roomFill[r];if(roomFill[r]<=0.02f)++lit;
    }
    fill.assign(numsectors,0.0f);
    for(int s=0;s<numsectors;++s)if(int r=Rooms_Of(s);r>=0&&!Rooms_Outside(s))fill[s]=roomFill[r];
    // The way through: the mean of the rooms it joins.
    for(int s=0;s<numsectors;++s) {
        if(Rooms_Outside(s)||!passage(s))continue;
        float joined=0;int count=0;
        for(int n=0;n<sectors[s].linecount;++n) {
            const line_t *line=sectors[s].lines[n];
            const sector_t *other=line->frontsector==&sectors[s]?line->backsector:line->frontsector;
            if(!other||other==&sectors[s])continue;
            int o=int(other-sectors),r=Rooms_Of(o);
            if(Rooms_Outside(o)||passage(o)||r<0)continue;
            joined+=roomFill[r];++count;
        }
        if(count)fill[s]=joined/count;
    }
    fprintf(stderr,"3D fill: %d rooms, %d lit by their own lights, the rest keep %.2f of a light level on average.\n",
            counted,lit,counted>lit?sum/(counted-lit):0.0);
}
// A sector's fill as it stands now: the room's, times the player's dial and
// how far the sector's light is below its steady level (a switch, a trap, a
// flicker). 0 before it is solved.
float roomFillNow(const sector_t *sector) {
    size_t index=sector-sectors;
    if(fillSerial!=r_levelserial||index>=fill.size()||lightByLogic())return 0;
    int steady=std::max(1,lampPeakLevel((int)index));
    return fill[index]*settings.fillLight*std::clamp(lampLightLevel(sector)/(float)steady,0.0f,1.0f);
}
// An outside sector's level as its yard gives it, times how far the sector's own light stands below
// its steady level now (a switch, a flicker); -1 where there is none (indoors, or not solved).
float yardLevelNow(const sector_t *sector) {
    size_t index=sector-sectors;
    if(fillSerial!=r_levelserial||index>=yard.size()||yard[index]<0)return -1;
    int steady=std::max(1,lampPeakLevel((int)index));
    return yard[index]*std::clamp(lampLightLevel(sector)/(float)steady,0.0f,1.0f);
}
float daylightLevel(float mapped) {return lightByLogic()?std::clamp(levelMood().outdoor,0.12f,1.0f):mapped;}
void roomFillShutdown() {fill.clear();yard.clear();fillSerial=~0u;}
} // namespace doom3d
