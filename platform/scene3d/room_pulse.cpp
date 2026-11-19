/* A flash lights its room, not just what faces it: a shot, a spark or a
   bursting barrel raises the light of the sectors of the room it goes off
   in for as long as it shines, by a share of its strength that falls with
   the distance. Doom's own gun flash did this for the whole screen
   (extralight); here it is the flash's room (rooms.c: the sectors of one
   room, or of one yard outside), so the room next door stays as it was and
   nothing shines through a wall. It stands for the flash's light bounced
   round the room, which the lights themselves don't trace: they light what
   they see, and the far side of a pillar stayed dark through a firefight.
   The collector (lights.cpp) gives each frame's flashes; drawing reads the
   list of the frame before while the next is gathered. Drawing only: the
   game's sectors keep their light, and so do the bakes, the eye's
   adaptation and everything else that asks lighting(). */
#include "internal.h"
#include "../rooms.h"

namespace doom3d {
namespace {
constexpr float share=0.06f;      // of a flash's strength at its place (a shotgun's 1.9: about two of Doom's light steps)
constexpr float ceilingLevel=1.2f; // a lit room rises no further than a little past full
std::vector<float> shown,gathered; // per sector, the light added: as drawing reads it, and as this frame gathers it
bool any=false,anyGathered=false;
unsigned boundsSerial=~0u;
std::vector<std::array<float,4>> bounds; // per sector: x0, y0, x1, y1 of its lines

void findBounds() {
    if(boundsSerial==r_levelserial&&bounds.size()==(size_t)numsectors)return;
    boundsSerial=r_levelserial;
    bounds.assign(numsectors,{1e9f,1e9f,-1e9f,-1e9f});
    for(int i=0;i<numlines;++i)for(const sector_t *side:{lines[i].frontsector,lines[i].backsector}) {
        if(!side)continue;
        auto &b=bounds[side-sectors];
        for(const vertex_t *v:{lines[i].v1,lines[i].v2}) {
            b[0]=std::min(b[0],units(v->x));b[1]=std::min(b[1],units(v->y));
            b[2]=std::max(b[2],units(v->x));b[3]=std::max(b[3],units(v->y));
        }
    }
}
// Whether two sectors are one room for a flash: the same room indoors, the
// same yard outside (a porch with the yard it opens onto).
bool sameRoom(int a,int b) {
    if(a==b)return true;
    int room=Rooms_Of(a);
    if(room>=0)return Rooms_Of(b)==room;
    int yard=Rooms_Yard(a);
    return yard>=0&&Rooms_Yard(b)==yard;
}
}

void roomPulseBegin() {
    static unsigned serial=~0u;
    if(serial!=r_levelserial) {serial=r_levelserial;shown.clear();gathered.clear();anyGathered=false;}   // a new level: nothing carries over
    shown.swap(gathered);any=anyGathered;anyGathered=false;
    if(gathered.size()!=(size_t)numsectors)gathered.assign(numsectors,0);
    else std::fill(gathered.begin(),gathered.end(),0.0f);
}
void roomPulseAdd(float x,float y,float radius,float strength) {
    if(!settings.flashRooms||strength<=0||radius<=0||gathered.size()!=(size_t)numsectors)return;
    Rooms_Build();findBounds();
    int own=(int)(R_PointInSubsector((fixed_t)(x*FRACUNIT),(fixed_t)(y*FRACUNIT))->sector-sectors);
    for(int s=0;s<numsectors;++s) {
        const auto &b=bounds[s];
        float dx=std::max({b[0]-x,0.0f,x-b[2]}),dy=std::max({b[1]-y,0.0f,y-b[3]}),distance=std::hypot(dx,dy);
        if(distance>=radius||!sameRoom(own,s))continue;
        float near=1-distance/radius;
        gathered[s]+=share*strength*near;anyGathered=true;
    }
}
float roomPulse(const sector_t *sector,float level) {
    size_t index=sector-sectors;
    if(!any||index>=shown.size()||shown[index]<=0)return level;
    // In Doom's light steps, like the flash it rides on.
    return std::max(level,std::min(ceilingLevel,level+std::round(shown[index]*16)/16));
}
void roomPulseShutdown() {shown.clear();gathered.clear();bounds.clear();any=anyGathered=false;boundsSerial=~0u;}
} // namespace doom3d
