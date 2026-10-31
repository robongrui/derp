/* What the renderer keeps of the world, carried with the game's steps
   (Game_WorldShift: the library's stairs and chasm, a storey at a time):
   blood, marks, scorches, debris and dead bodies' dolls move with
   everything else, onto the geometry built alike that the step lands on.
   When all round the player is made anew (Game_WorldRenewed: a new floor, a
   morning, when the dead get up) the blood and marks go. */
#include "internal.h"

#include <map>

namespace doom3d {
static unsigned carriedLevel;
static float seenShift[3];
static int seenRenewed;
static std::map<std::array<fixed_t,4>,int> lineEnds;   // each line by its ends, for lineCarried

// Where the map's line index would be stepped by (dx,dy): the line built
// alike there, or -1 where none is.
int lineCarried(int line,float dx,float dy) {
    if(line<0||line>=numlines)return -1;
    if(lineEnds.empty())for(int i=0;i<numlines;++i)
        lineEnds[{lines[i].v1->x,lines[i].v1->y,lines[i].v2->x,lines[i].v2->y}]=i;
    fixed_t fx=(fixed_t)std::lround(dx*FRACUNIT),fy=(fixed_t)std::lround(dy*FRACUNIT);
    const line_t &l=lines[line];
    auto found=lineEnds.find({l.v1->x+fx,l.v1->y+fy,l.v2->x+fx,l.v2->y+fy});
    return found==lineEnds.end()?-1:found->second;
}
// The sector under (x,y), or -1 off the map (stepped past the built part's ends).
int sectorCarried(float x,float y) {
    if(x<mapOrigin[0]||y<mapOrigin[1]||x>mapOrigin[0]+mapWidth*mapCell||y>mapOrigin[1]+mapHeight*mapCell)return -1;
    return int(R_PointInSubsector((fixed_t)(x*FRACUNIT),(fixed_t)(y*FRACUNIT))->sector-sectors);
}
// Each tic and frame, before anything is drawn: catch a step the moment the
// game made it, so nothing is drawn a cycle off for a frame.
void followCarry() {
    float shift[3];Game_WorldShift(&shift[0],&shift[1],&shift[2]);
    int renewed=Game_WorldRenewed();
    if(carriedLevel!=r_levelserial) {
        carriedLevel=r_levelserial;lineEnds.clear();
        std::copy(shift,shift+3,seenShift);seenRenewed=renewed;
        return;
    }
    if(renewed!=seenRenewed) {seenRenewed=renewed;bloodShutdown();impactsShutdown();}
    float dx=Game_ShiftGrew(shift[0],seenShift[0]),dy=Game_ShiftGrew(shift[1],seenShift[1]),dz=Game_ShiftGrew(shift[2],seenShift[2]);
    if(dx==0&&dy==0&&dz==0)return;
    std::copy(shift,shift+3,seenShift);
    bloodCarried(dx,dy);impactsCarried(dx,dy,dz);ragdollsCarried(dx,dy,dz);
}
} // namespace doom3d
