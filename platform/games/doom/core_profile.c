/* The game profile answers doom_core itself asks for (platform/stride.c), for the programs
   built without DOOM's full profile (profile.c): headless and the historical X11 port. */
#include "doomdef.h"
#include "p_mobj.h"
#include "p_moves.h"

int Game_Sliding(const struct mobj_s *thing) { return P_SlidingBody((const mobj_t *)thing); }
