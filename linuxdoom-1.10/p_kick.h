//
// The Authored player's kicks (p_kick.c): a front kick, and out of a slide
// a sidesweep.
//

#ifndef __P_KICK__
#define __P_KICK__

#include "d_player.h"

void P_KickThink (player_t* player);
// For the drawing: whether his leg shows, which of its frames
// (platform/kick_frames_stock.h's order) and how far the gun is out of
// its way, 0 to 1.
boolean P_KickShown (player_t* player, int* frame, float* dip);
// A kicked severed head (the drawing's, platform/scene3d/flying_piece.cpp) at
// (x,y,z), radius across, flying into a monster: one hurt and its pain frame.
// Authored only. Whether it struck one.
int P_HeadStrikes (fixed_t x, fixed_t y, fixed_t z, fixed_t radius, int player);

#endif
