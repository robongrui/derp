/* The listener's own room, on authored maps (level_sound.h), from a grid of
 * room measurements baked at level start rather than measured live every
 * other tic. A cell every 64 units over every room and yard holds what
 * room.c measures there facing no way, and the space beyond (beyond.h)
 * found from it. The listener hears the cells around them blended, turned
 * to their facing; cells that can't see the listener (a wall between) are
 * left out, and where none can the room is measured live as before.
 * Workers bake the grid nearest the listener first, while the level plays;
 * until a cell is done the room is measured live there. A door, lift or
 * floor that settles somewhere new, and a window that breaks, send the
 * cells that reach it back to a worker; the old ones hold meanwhile. */
#ifndef DOOM_AUDIO_ROOM_GRID_H
#define DOOM_AUDIO_ROOM_GRID_H
#include "beyond.h"

/* Once a tic on an authored level, on the game thread: starts a new level's
   bake, sends moved and broken things' cells to be measured again. */
void RoomGrid_Follow(void);
/* The room heard at (x, y) facing (radians, a map angle) and the space
   beyond it; 0 when the grid has nothing for there yet (measure live). */
int RoomGrid_Listener(float x, float y, float facing, room_t *room, beyond_t *beyond);
/* Stops the workers and forgets the grid: the level ends, or the mode changes. */
void RoomGrid_Stop(void);
/* A window pane on line broke or came back. */
void RoomGrid_Glass(int line);
/* Bakes the whole grid now and waits for it (doom-room-probe); its seconds. */
double RoomGrid_Finish(void);
#endif
