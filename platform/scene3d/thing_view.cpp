/* Which things the eye might see this frame. A map holds hundreds of things and the eye a dozen, and a
   thing out of sight needs no light, shadow, wounds or picture. Its body goes on stepping (ragdollView),
   only its drawing is left out. The leaves the eye sees (cullHidden) stamp a coarse grid, wide of them by
   the most a thing drawn elsewhere can reach into them: a sprite's half and the longest shadow. So
   a thing behind a corner still shows its shoulder, and next door costs what it did. */
#include "internal.h"

namespace doom3d {
namespace {
constexpr float cell=128;
// How far a thing drawn elsewhere reaches into what is seen: a sprite's half and the longest shadow.
// The laid-on outlines are at most 192 long (addEnemyShadow); a shadow volume runs up to 400 from a
// lamp (shadowVolume), and at 256 a monster round a corner cast nothing into view until it came closer.
float reach=256;
std::vector<char> nearLeaves;
int columns=0,rows=0;
bool planned=false;
int column(float x) {return (int)std::floor((x-mapOrigin[0])/cell);}
int row(float y) {return (int)std::floor((y-mapOrigin[1])/cell);}
}

// shown: cullHidden's leaves; empty draws every thing (the switch is off, or a probe looks all round).
void planThingView(const std::vector<char> &shown) {
    planned=shown.size()==(size_t)numsubsectors&&numsubsectors>0;
    if(!planned)return;
    reach=settings.shadowVolumes&&settings.lightModel==1?448.0f:256.0f;
    columns=(int)std::ceil(mapWidth*mapCell/cell)+1;rows=(int)std::ceil(mapHeight*mapCell/cell)+1;
    nearLeaves.assign((size_t)columns*rows,0);
    for(int leaf=0;leaf<numsubsectors;++leaf) {
        if(!shown[leaf]||floors[leaf].size()<3)continue;
        const auto &b=floorBounds[leaf];
        int x0=std::max(0,column(b[0]-reach)),x1=std::min(columns-1,column(b[2]+reach));
        int y0=std::max(0,row(b[1]-reach)),y1=std::min(rows-1,row(b[3]+reach));
        for(int y=y0;y<=y1;++y)std::fill_n(nearLeaves.begin()+(size_t)y*columns+x0,std::max(0,x1-x0+1),(char)1);
    }
}
// A leaf a shadow volume lands on, as seen: every leaf while the things are not culled (the switch off,
// a probe, the storeys' copies, which are seen from everywhere).
bool shadowLeafSeen(int leaf) {return !planned||leafInView(leaf);}
bool thingInView(float x,float y) {
    if(!planned)return true;
    int cx=column(x),cy=row(y);
    return cx<0||cy<0||cx>=columns||cy>=rows||nearLeaves[(size_t)cy*columns+cx];
}
}
