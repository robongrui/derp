//
// The Authored player's legs (p_moves.c): a crouch, a slide out of a run,
// a low jump. Vanilla has none of them.
//

#ifndef __P_MOVES__
#define __P_MOVES__

#include "d_player.h"

// Whether he has them: Authored, his feet on Doom's own ground.
boolean P_MovesOn (void);
// His legs this tic (P_MovePlayer): the keys' thrust, forward and side, as
// they leave him to it (halved crouched, none sliding).
void P_MovesLegs (player_t* player, boolean grounded, int* forward, int* side);
// How far his eye is down for it.
fixed_t P_MovesDuck (player_t* player);
boolean P_Sliding (player_t* player);
// Sliding, or his slide over a moment ago: a kick now is the sweep (KICK_GRACE).
boolean P_SlideKicks (player_t* player);
// His slide's speed as a kick out of it hands it on, units a tic (none: not sliding,
// nor a moment ago).
void P_SlideGiven (player_t* player, fixed_t* x, fixed_t* y);
// That kick landed (p_kick.c): the slide keeps a quarter of its speed, and
// he stays down a while after it.
void P_MovesKicked (player_t* player);
// Whether mo is a player sliding: no steps, no bob.
boolean P_SlidingBody (const mobj_t* mo);
// For the view: how far it is in a slide, 0 to 1, between the last tic and
// this one by fraction (tipped, widened; his eye is already down, P_MovesDuck).
float P_SlideView (player_t* player, float fraction);

#endif
