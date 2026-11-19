//
// A monster with its feet swept away (p_swept.c): Authored's sidesweep.
//

#ifndef __P_SWEPT__
#define __P_SWEPT__

#include "p_mobj.h"

// Whether mo is one a sweep takes off its feet, as it stands now.
boolean P_Sweepable (mobj_t* mo);
// Its feet go way, and it flies off with push (units a tic); false for one
// a sweep does not fell.
boolean P_Sweep (mobj_t* mo, angle_t way, fixed_t pushx, fixed_t pushy);
// Down, or on its way down or up: it does nothing.
boolean P_Swept (const mobj_t* mo);
void P_SweptThink (mobj_t* mo);
// How tall it is to the player's shots, as it is drawn.
fixed_t P_SweptShown (const mobj_t* mo);
// For the drawing: how far it is tipped over, 0 to 1, between the last tic
// and this one by fraction, and the way its feet went.
float P_SweptShare (const mobj_t* mo, float fraction, float* dx, float* dy);
// The same for one killed on the way: its doll starts as it was drawn.
float P_SweptTilt (const mobj_t* mo, float fraction, float* dx, float* dy);
// The way it is drawn facing, for one facing base when it stands: rolled
// over through its views in the air, back as it rises (drawing only).
angle_t P_SweptFacing (const mobj_t* mo, angle_t base);
// How fast it is rolling over now, radians a tic, the facing's sense (0 settled).
float P_SweptRoll (const mobj_t* mo);
// Just down from its kip-up: how far crouched, 0 to 1, a moment (drawing only).
float P_SweptCrouch (const mobj_t* mo, float fraction);
// Whether by's hit on mo kills it whatever it does: mo kicked off its feet
// and still in the air, by a player's shotgun (no missile: his own shot).
boolean P_SweptAirKill (const mobj_t* mo, const mobj_t* with, const mobj_t* by);
// That shot throws it on, away from by.
void P_SweptBlast (mobj_t* mo, const mobj_t* by);

#endif
