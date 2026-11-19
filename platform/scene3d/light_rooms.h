/* Lights by room (light_rooms.cpp, PLAN-room-light-filter.md). */
#pragma once

namespace doom3d {
// The spaces each of bakeSources, then bakeAreas, can get light into; after the light grid is built.
void buildLightRooms();
// Light n's spaces again (a shot fixture's merged light, remergeBakeSource).
void rebuildLightRoom(int light);
// Whether light n (a pool: bakeSources.size() + its index) can reach the sector at all. True when unsure.
bool lightReachesSector(int light,int sector);
}
