//
// A body at a point: what it sees, is seen from, shoots; a corner's shelter
// (p_cover.h).
//

#include <string.h>

#include "doomdef.h"
#include "doomstat.h"
#include "p_local.h"
#include "r_main.h"
#include "p_nav.h"
#include "p_tune.h"
#include "p_cover.h"

// A body of that height standing at (x, y, z), for the sight checks.
static mobj_t* Body (fixed_t x, fixed_t y, fixed_t z, fixed_t height)
{
    static mobj_t	eye;

    memset (&eye, 0, sizeof eye);
    eye.x = x;
    eye.y = y;
    eye.z = z;
    eye.height = height;
    eye.subsector = R_PointInSubsector (x, y);
    return &eye;
}

boolean P_CoverSees (fixed_t x, fixed_t y, fixed_t z, mobj_t* target)
{
    return P_CheckSight (Body (x, y, z, 56*FRACUNIT), target);
}

boolean P_CoverShoots (fixed_t x, fixed_t y, fixed_t z, mobj_t* target)
{
    return P_CheckSight (Body (x, y, z, 48*FRACUNIT), target);
}

boolean P_CoverSeen (fixed_t x, fixed_t y, fixed_t z, mobj_t* by)
{
    return P_CheckSight (by, Body (x, y, z, 56*FRACUNIT));
}

// P_CheckSight goes from one eye to all of the other's height, so it is not
// the same both ways: across a step a head shows to him while its own eye
// sees nothing of him (three shelters in 18 on E1M1 passed as hidden with him
// looking straight at them).
boolean P_DerpExposedAt (fixed_t x, fixed_t y, fixed_t z, mobj_t* target)
{
    return P_CoverSees (x, y, z, target) || P_CoverSeen (x, y, z, target);
}

// On one knee: a block a standing man's head shows over hides this much of
// him. More than Kneel's half a body, the picture being what he sees.
#define LOW_BODY	TUNE (LOW_BODY, 32)

boolean P_DerpExposedLowAt (fixed_t x, fixed_t y, fixed_t z, mobj_t* target)
{
    return P_CheckSight (Body (x, y, z, LOW_BODY*FRACUNIT), target)
	|| P_CheckSight (target, Body (x, y, z, LOW_BODY*FRACUNIT));
}

boolean P_DerpShootsAt (fixed_t x, fixed_t y, fixed_t z, mobj_t* target)
{
    return P_CoverShoots (x, y, z, target);
}

// How far back from a corner's end a body shelters.
#define SHELTER		TUNE (SHELTER, 32)

int P_DerpCornerSpot (const corner_t* c, int nav, fixed_t off, fixed_t* px, fixed_t* py, fixed_t* pf,
		      fixed_t* ex, fixed_t* ey)
{
    fixed_t	sx, sy;
    int		cell;

    // Off the wall, at the corner: the line a body slides along.
    *ex = c->x + FixedMul (c->nx, off);
    *ey = c->y + FixedMul (c->ny, off);
    sx = *ex + SHELTER*c->dx;
    sy = *ey + SHELTER*c->dy;
    cell = P_NavCellAt (nav, sx, sy);
    if (cell < 0)
	return SPOT_NOFLOOR;
    if (P_NavDoor (nav, cell))
	return SPOT_DOOR;
    if (P_NavHurts (cell))
	return SPOT_HURTS;
    P_NavPoint (nav, cell, px, py, pf);
    return P_AproxDistance (*px - sx, *py - sy) > 16*FRACUNIT ? SPOT_OFFGRID : SPOT_OK;
}
