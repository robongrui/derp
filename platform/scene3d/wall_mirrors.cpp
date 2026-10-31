// Wall mirrors (DERPINFO mirror): silvered glass hung on a wall. No line of
// the map, only a rectangle along one, half a unit out into the room so it
// covers the wall's own texture. The glass mirror's pass shows it like a
// window pane (windows.cpp: chooseMirror takes it among the panes,
// buildGlass draws it), the player's own reflection included, and
// glass.frag shades it all reflection (silvered).
#include "internal.h"

namespace doom3d {
static std::vector<WallMirror> mirrors;
static unsigned mirrorsFor=~0u;

const std::vector<WallMirror> &wallMirrors() {
    if(mirrorsFor==r_levelserial)return mirrors;
    mirrorsFor=r_levelserial;
    mirrors.clear();
    for(int i=0;i<DerpInfo_Map.mirrors;++i) {
        const float *m=DerpInfo_Map.mirror[i];
        float ex=m[2]-m[0],ey=m[3]-m[1],length=std::hypot(ex,ey);
        if(length<1)continue;
        // It faces its line's right side, as a linedef's front does.
        float nx=ey/length,ny=-ex/length;
        mirrors.push_back({m[0]+nx*0.5f,m[1]+ny*0.5f,m[2]+nx*0.5f,m[3]+ny*0.5f,m[4],m[5],nx,ny});
    }
    if(!mirrors.empty())fprintf(stderr,"3D mirrors: %zu on the walls.\n",mirrors.size());
    return mirrors;
}
} // namespace doom3d
