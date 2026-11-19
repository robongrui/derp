/* Doors shut at bake time, raised for the bakes of their own surfaces. */
#pragma once
#include "internal.h"
#include "../rooms.h"

namespace doom3d {
// A door shut at load has no height in bakeMap: its floor's samples start
// above its ceiling and every ray stops at once, so the floor that comes to
// light as it opens was baked shut in (no bounce, no sky, all occluded),
// darker and greyer than the floors round it (E1M1's sector 2, the walls
// round the yard). Its own surfaces are baked as it stands open, in a copy
// of the map with such doors raised to the height they open to: nobody sees
// them while it is shut. Every other surface is baked against the map as it
// is, the door shut, so nothing leaks through it.
// Build it on the game's thread (it asks rooms.c); from is bakeMap, or a copy
// of it a background bake works on.
struct OpenDoors {
    BakeMap map;
    const BakeMap *base;
    std::vector<char> door;   // per sector: a shut door, raised in map
    bool any=false;
    explicit OpenDoors(const BakeMap &from=bakeMap):base(&from) {
        Rooms_Build();
        door.assign(numsectors,0);
        for(int s=0;s<numsectors&&s<(int)from.sectors.size();++s) {
            const BakeSector &b=from.sectors[s];float top;
            if(b.ceiling-b.floor<8&&Rooms_Door(s,&top)&&top-b.floor>=8)door[s]=1,any=true;
        }
        if(!any)return;
        map=from;
        for(int s=0;s<numsectors;++s)if(door[s]) {float top;Rooms_Door(s,&top);map.sectors[s].ceiling=top;}
    }
    // The map a sample in sector is baked against.
    const BakeMap &of(int sector) const {return any&&sector>=0&&sector<(int)door.size()&&door[sector]?map:*base;}
};
} // namespace doom3d
