// What a body at a point can see, be seen from, and shoot (the tests every
// cover search runs), and where a corner shelters one. Below the tactics,
// the cover plan and the openings, which all ask these and used to ask the
// tactics for them.

#ifndef __P_COVER__
#define __P_COVER__

#include "doomtype.h"
#include "m_fixed.h"
#include "p_mobj.h"
#include "p_corners.h"

// Whether something standing at (x, y, z) sees target; whether its gun has a
// line on target from there (the gun is lower than the eye: 36 units up, not
// 42, and a sill it sees over can still stop the shot); whether `by` sees a
// standing body there.
boolean P_CoverSees (fixed_t x, fixed_t y, fixed_t z, mobj_t* target);
boolean P_CoverShoots (fixed_t x, fixed_t y, fixed_t z, mobj_t* target);
boolean P_CoverSeen (fixed_t x, fixed_t y, fixed_t z, mobj_t* by);
// Whether a standing body at (x, y) on floor z is in view of target either
// way; the same on one knee; whether its gun has a line on target from there.
boolean P_DerpExposedAt (fixed_t x, fixed_t y, fixed_t z, mobj_t* target);
boolean P_DerpExposedLowAt (fixed_t x, fixed_t y, fixed_t z, mobj_t* target);
boolean P_DerpShootsAt (fixed_t x, fixed_t y, fixed_t z, mobj_t* target);

// Where a corner shelters a body (*px, *py, floor *pf) and where its edge is
// (*ex, *ey), off the wall by off; or why it does not.
enum { SPOT_OK, SPOT_NOFLOOR, SPOT_DOOR, SPOT_HURTS, SPOT_OFFGRID };
int P_DerpCornerSpot (const corner_t* c, int nav, fixed_t off, fixed_t* px, fixed_t* py, fixed_t* pf,
		      fixed_t* ex, fixed_t* ey);

#endif
