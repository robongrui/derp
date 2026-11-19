/* Light through doorways (settings.doorwayLight): a room with no lamps of
   its own beside a brighter one is lit through the opening between them, as
   the outdoors lights a room through a window (addWindowLights). Every
   opening from a brighter room into a darker one is a soft lamp facing into
   the darker, built like a window's: glowing panels 2 units inside it,
   about 32 units wide and at most 64 tall, as strong as the rooms' light
   levels differ and the opening is large, in the brighter room's lamp
   color paled halfway to white (white where it has no lamps). So the darker
   room's light has a place it comes from and falls off from there, and its
   even level drops by what the doorway gives (classifyLampSectors) like any
   lamp-lit room's.
   Rooms are rooms.c's: a line inside one room is no doorway, however its
   sectors' levels differ (the mapper's bright patch under a lamp is the
   lamp's own business). Between two rooms usually lies an opening's or a
   door's sector of its own: the panels stand on its line onto the darker
   room and shine with the brightest room beyond it. A door shut at load has
   the lamp it will have open, hidden while it is shut (windowDoors), and
   relight.cpp re-bakes round it as it opens.
   Only downhill, and only one opening deep: light a room got through a door
   is not passed on through the next. Baked with the lamps (bake_lights.cpp);
   these lamps end bakeSources with the windows' and are eased with them
   (capWindowLight), casting no thing shadows. */
#include "internal.h"
#include "../rooms.h"

namespace doom3d {
namespace {
constexpr float doorwayStrength=1.6f;   // as a window's, for a level difference of 1
constexpr float doorwayReach=320;       // a window's is 448: a doorway's light comes from a room, not the sky
constexpr float doorwayLeast=0.06f;     // a level step under this is no light source
float levelOf(const sector_t *sector) {return std::clamp(lampLightLevel(sector)/255.0f,0.12f,1.0f);}
bool indoors(const sector_t *sector) {return sector->ceilingpic!=skyflatnum;}
// A sector that is the way through, not a room: an opening of its own, or a door.
bool passage(int sector) {return Rooms_Kind(sector)==ROOM_OPENING||Rooms_Door(sector,nullptr);}
bool sameRoom(int a,int b) {return a==b||(Rooms_Of(a)>=0&&Rooms_Of(a)==Rooms_Of(b));}
// Each room's lamp color: the lamps standing in its sectors by their strength, paled halfway to white.
std::vector<std::array<float,3>> roomColors(const std::vector<BakeLight> &sources,int lamps) {
    std::vector<std::array<float,3>> sum(Rooms_Count(),{0,0,0});
    for(int n=0;n<lamps&&n<(int)sources.size();++n) {
        const BakeLight &light=sources[n];
        int room=Rooms_Of(int(R_PointInSubsector((fixed_t)(light.x*FRACUNIT),(fixed_t)(light.y*FRACUNIT))->sector-sectors));
        if(room>=0)for(int c=0;c<3;++c)sum[room][c]+=light.color[c]*light.strength;
    }
    for(auto &color:sum) {
        float peak=std::max({color[0],color[1],color[2]});
        for(float &c:color)c=peak>0?0.5f+0.5f*c/peak:1.0f;
    }
    return sum;
}
}

void addDoorwayLights(std::vector<BakeLight> &sources,std::vector<int> &doors,int lamps) {
    Rooms_Build();
    const std::vector<std::array<float,3>> colors=roomColors(sources,lamps);
    int count=0;
    for(int i=0;i<numlines;++i) {
        const line_t &line=lines[i];
        if(!line.frontsector||!line.backsector||line.frontsector==line.backsector)continue;
        if(!indoors(line.frontsector)||!indoors(line.backsector))continue;
        for(int side=0;side<2;++side) {
            const sector_t *to=side?line.backsector:line.frontsector,*from=side?line.frontsector:line.backsector;
            int t=int(to-sectors),f=int(from-sectors);
            // Into a room, from another room or from the passage between them.
            if(passage(t)||sameRoom(t,f))continue;
            // The light's room: the sector across the line, or the brightest room beyond a passage.
            const sector_t *bright=nullptr;
            if(!passage(f))bright=from;
            else for(int n=0;n<from->linecount;++n) {
                const line_t *other=from->lines[n];
                const sector_t *beyond=other->frontsector==from?other->backsector:other->frontsector;
                if(!beyond||beyond==from||beyond==to||!indoors(beyond)||sameRoom(int(beyond-sectors),t))continue;
                if(!bright||levelOf(beyond)>levelOf(bright))bright=beyond;
            }
            if(!bright)continue;
            float difference=levelOf(bright)-levelOf(to);
            if(difference<doorwayLeast)continue;
            float low=std::max(floorZ(to),floorZ(from)),high=std::min(ceilZ(to),ceilZ(from)),top;
            // A door shut at load: the lamp it has open, sized by the height it opens to.
            bool shut=high-low<8&&Rooms_Door(f,&top);
            if(shut)high=std::min(top,ceilZ(to));
            if(high-low<8)continue;
            float ax=units(line.v1->x),ay=units(line.v1->y),dx=units(line.v2->x)-ax,dy=units(line.v2->y)-ay;
            float length=std::hypot(dx,dy);if(length<8)continue;
            dx/=length;dy/=length;
            // The front side lies to the right of v1->v2; the panels face into the darker room.
            float nx=to==line.frontsector?dy:-dy,ny=to==line.frontsector?-dx:dx;
            int columns=std::max(1,(int)std::lround(length/32)),rows=std::max(1,(int)std::ceil((high-low)/64));
            float width=length/columns,height=(high-low)/rows;
            float strength=doorwayStrength*difference*width/128*height/96;
            int room=Rooms_Of(int(bright-sectors));
            const std::array<float,3> white={1,1,1},&color=room>=0?colors[room]:white;
            for(int column=0;column<columns;++column)for(int row=0;row<rows;++row) {
                float along=(column+0.5f)*width;
                sources.push_back({ax+dx*along+nx*2,ay+dy*along+ny*2,low+(row+0.5f)*height,doorwayReach,strength,0.5f,
                                   {color[0],color[1],color[2]},height*0.5f,nx,ny,0.0f});
                doors.push_back(shut?f:-1);
                ++count;
            }
        }
    }
    fprintf(stderr,"3D bake: %d doorway panels light the darker rooms beside them.\n",count);
}
} // namespace doom3d
