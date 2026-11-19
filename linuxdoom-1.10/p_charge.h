// A pinky's charge. From across a room with him in sight and a clear run at him,
// it lowers its head, a roar and a moment he can see coming, then comes at him
// far faster than it walks, turning only a little. He can sidestep it, and what
// it runs into instead stops it dead for a moment. Spectres too.
//
// It also smells him, near, through walls, and beats on a door it cannot open.
#ifndef __P_CHARGE__
#define __P_CHARGE__

#include "p_mobj.h"

// In its chase: near enough, it knows where he is by smell.
void P_ChargeSmell (mobj_t* mo);

// In its chase, him in sight or not: true if it has just begun a charge.
boolean P_ChargeStart (mobj_t* mo, boolean visible);

// Begin one, whatever the room (the tests).
void P_ChargeBegin (mobj_t* mo);

// Each tic.
void P_ChargeThink (mobj_t* mo);

// Whether it is in one: its chase waits.
boolean P_Charging (const mobj_t* mo);

#endif
