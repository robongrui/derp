//
// The library up and down (scp_7179_b/README.md, Chasm and falling): the
// stairwell that repeats every storey, over the railing, the fall through
// the chasm, and waking on a floor caught on the way down (see p_library.h,
// p_library_local.h).
//

#include <math.h>
#include <stdlib.h>

#include "doomdef.h"
#include "doomstat.h"
#include "d_event.h"
#include "r_main.h"
#include "tables.h"
#include "p_library_local.h"

// The stairwells behind the near back wall; the chasm between the railings
// and the landings at its ends.
#define STAIRS_NORTH	(-LIBRARY_WALL*FRACUNIT)
#define NEAR_RAIL	(LIBRARY_NEAR_RAIL*FRACUNIT)
#define FAR_RAIL	(LIBRARY_FAR_RAIL*FRACUNIT)
#define WEST_RAIL	(LIBRARY_LANDING*FRACUNIT)
#define EAST_RAIL	((LIBRARY_LANDING + LIBRARY_SIDE)*FRACUNIT)
#define STOREY		(LIBRARY_STOREY*FRACUNIT)

// The fall: DOOM's own gravity (a unit a tic a tic) up to a falling body's
// top speed, 40 units a tic spread out to 70 diving (12 floors a second
// between). Steering is easy: he drifts where the keys say, up to AIR_TOP,
// a third of the way there each tic, and stops drifting when he lets go,
// diving or not (no DOOM slide: in the air nothing would ever stop it).
#define FALL_SPREAD	(40*FRACUNIT)
#define FALL_DIVE	(30*FRACUNIT)
#define AIR_TOP		(12*FRACUNIT)
#define AIR_TAKE	3
#define EDGE		(8*FRACUNIT)	// this close to an edge as a floor goes by, he hits it

static fixed_t		lastz;		// where he was falling from, the tic before


static boolean InStairwell (mobj_t* mo)
{
    return mo->y < STAIRS_NORTH;
}


static boolean Over (fixed_t x, fixed_t y)
{
    return y > NEAR_RAIL && y < FAR_RAIL && x > WEST_RAIL && x < EAST_RAIL;
}


boolean L_InChasm (mobj_t* mo)
{
    return Over (mo->x, mo->y);
}


void L_ShaftLevel (mobj_t* mo)
{
    lastz = mo->z;
}


// The stairwell repeats every storey: past half a storey up or down the
// flight, he is a storey back the other way on the same flight, and on the
// next floor.
static void Stairs (mobj_t* mo)
{
    if (mo->floorz >= STOREY/2 && L_ChangeFloor (1))
	L_Lift (mo, InStairwell, -LIBRARY_FLIGHT, -LIBRARY_STOREY);
    else if (mo->floorz < -STOREY/2 && L_ChangeFloor (-1))
	L_Lift (mo, InStairwell, LIBRARY_FLIGHT, LIBRARY_STOREY);
    else
	return;
    L_PopulateAll ();
    L_Log ("stairs", mo);
}


// Use, facing the railing close by (the chasm a step ahead, on any side of
// it): he is over it, clear of the edge.
static void OverTheRailing (player_t* player)
{
    mobj_t*	mo = player->mo;
    int		an = mo->angle >> ANGLETOFINESHIFT;
    fixed_t	x, y;

    if (!(player->cmd.buttons & BT_USE) || L_InChasm (mo)
	|| !Over (mo->x + 48*finecosine[an], mo->y + 48*finesine[an]))
	return;
    x = mo->x + 64*finecosine[an];
    y = mo->y + 64*finesine[an];
    x = x < WEST_RAIL + 24*FRACUNIT ? WEST_RAIL + 24*FRACUNIT : x > EAST_RAIL - 24*FRACUNIT ? EAST_RAIL - 24*FRACUNIT : x;
    y = y < NEAR_RAIL + 24*FRACUNIT ? NEAR_RAIL + 24*FRACUNIT : y > FAR_RAIL - 24*FRACUNIT ? FAR_RAIL - 24*FRACUNIT : y;
    P_UnsetThingPosition (mo);
    mo->x = x;
    mo->y = y;
    P_SetThingPosition (mo);
    mo->floorz = -LIBRARY_DEPTH*FRACUNIT;
    lastz = mo->z;
    L_Log ("over the railing", mo);
}


// The bed nearest x on a floor: four, two on each landing, against the
// near back wall.
static fixed_t NearestBed (fixed_t x)
{
    int		beds[4], i, best = 0;
    int		at = x >> FRACBITS;

    for (i = 0; i < 4; i++)
    {
	int	west = i < 2 ? 0 : LIBRARY_LANDING + LIBRARY_SIDE;

	beds[i] = i & 1 ? west + LIBRARY_LANDING - LIBRARY_CORNICE - LIBRARY_BED_INSET - LIBRARY_BED_WIDTH/2
			: west + LIBRARY_CORNICE + LIBRARY_BED_INSET + LIBRARY_BED_WIDTH/2;
	if (abs (beds[i] - at) < abs (beds[best] - at))
	    best = i;
    }
    return beds[best]*FRACUNIT;
}


// Hitting a floor's edge: the ugly death. Black at once, his ears ringing,
// and he wakes in the nearest bed on the floor he hit, whole, in the
// morning (p_library_day.c).
static void Wake (player_t* player, int storeys)
{
    mobj_t*	mo = player->mo;

    while (storeys < 0 && L_ChangeFloor (-1))
	storeys++;
    while (storeys > 0 && L_ChangeFloor (1))
	storeys--;
    P_UnsetThingPosition (mo);
    mo->x = NearestBed (mo->x);
    mo->y = (LIBRARY_CORNICE + LIBRARY_BED_LENGTH/2)*FRACUNIT;
    mo->angle = ANG90;
    P_SetThingPosition (mo);
    mo->z = mo->floorz = 0;
    P_CheckPosition (mo, mo->x, mo->y);
    mo->floorz = tmfloorz;
    mo->ceilingz = tmceilingz;
    mo->z = mo->floorz;
    mo->momx = mo->momy = mo->momz = 0;
    mo->health = player->health = 100;
    player->viewz = mo->z + player->viewheight;
    player->damagecount = 0;
    L_KnockOut (player);
    L_Log ("woke", mo);
}


// Falling through the chasm: up to a body's top speed, steering a little, a
// storey back up for each one fallen (the floor's count going down), and the
// edge of a floor hit on the way down if he comes close to it.
static void Fall (player_t* player)
{
    mobj_t*	mo = player->mo;
    fixed_t	dive = tourpitch < 0 ? (fixed_t)(-tourpitch*FRACUNIT) : 0;
    fixed_t	top, wantx, wanty, want;
    angle_t	an = mo->angle >> ANGLETOFINESHIFT, right = (mo->angle - ANG90) >> ANGLETOFINESHIFT;
    int		level;

    if (dive > FRACUNIT)
	dive = FRACUNIT;
    top = FALL_SPREAD + FixedMul (FALL_DIVE, dive);
    if (mo->momz < -top)
	mo->momz = -top;
    // Running forward is 50, strafing 40: either at full is the top.
    wantx = FixedMul (player->cmd.forwardmove*(AIR_TOP/50), finecosine[an])
	  + FixedMul (player->cmd.sidemove*(AIR_TOP/40), finecosine[right]);
    wanty = FixedMul (player->cmd.forwardmove*(AIR_TOP/50), finesine[an])
	  + FixedMul (player->cmd.sidemove*(AIR_TOP/40), finesine[right]);
    want = P_AproxDistance (wantx, wanty);
    if (want > AIR_TOP)
    {
	wantx = FixedMul (wantx, FixedDiv (AIR_TOP, want));
	wanty = FixedMul (wanty, FixedDiv (AIR_TOP, want));
    }
    mo->momx += (wantx - mo->momx)/AIR_TAKE;
    mo->momy += (wanty - mo->momy)/AIR_TAKE;

    // A floor's level passed since the last tic, up against any edge.
    level = lastz >= 0 ? lastz/STOREY : -((-lastz + STOREY - 1)/STOREY);
    if (mo->z < level*STOREY && lastz >= level*STOREY
	&& (mo->y - mo->radius <= NEAR_RAIL + EDGE || mo->y + mo->radius >= FAR_RAIL - EDGE
	    || mo->x - mo->radius <= WEST_RAIL + EDGE || mo->x + mo->radius >= EAST_RAIL - EDGE))
    {
	Wake (player, level);
	return;
    }

    if (mo->z < -STOREY && L_ChangeFloor (-1))
    {
	L_Lift (mo, L_InChasm, 0, LIBRARY_STOREY);
	L_Log ("falling", mo);
    }
    lastz = mo->z;
}


// How fast he fell at the last tic (units a tic; 0 out of the chasm),
// kept for the sound, which mustn't look at his thing (P_LibraryFallSpeed).
static float		fallspeed;

// How open he stands to the chasm, 0 shut away from it to 1 at its edge or
// in it, and on which side of him it lies as he faces (-1 left .. 1 right):
// for the sound (P_LibraryChasm). Kept each tic.
#define CHASM_HEARD	640	// how far from its edge it is still heard
static float		chasmopen, chasmside;

static void Chasm (mobj_t* mo)
{
    float	x = mo->x/(float)FRACUNIT, y = mo->y/(float)FRACUNIT;
    float	cx, cy, d, rel;
    boolean	among;

    if (L_InChasm (mo))
    {
	chasmopen = 1;
	chasmside = 0;
	return;
    }
    // The chasm's nearest point.
    cx = x < LIBRARY_LANDING ? LIBRARY_LANDING : x > LIBRARY_LANDING + LIBRARY_SIDE ? LIBRARY_LANDING + LIBRARY_SIDE : x;
    cy = y < LIBRARY_NEAR_RAIL ? LIBRARY_NEAR_RAIL : y > LIBRARY_FAR_RAIL ? LIBRARY_FAR_RAIL : y;
    d = sqrtf ((cx - x)*(cx - x) + (cy - y)*(cy - y));
    chasmopen = d >= CHASM_HEARD ? 0 : 1 - d/CHASM_HEARD;
    // Behind the back walls (the stairwells) nothing of it; among the
    // stacks the books take most of it.
    among = x > LIBRARY_LANDING && x < LIBRARY_LANDING + LIBRARY_SIDE
	&& (y < LIBRARY_STACKS || y > LIBRARY_FAR_WALL - LIBRARY_STACKS);
    if (y < 0 || y > LIBRARY_FAR_WALL)
	chasmopen = 0;
    else if (among)
	chasmopen *= 0.35f;
    // Map angles turn left; as he faces, left is negative.
    rel = atan2f (cy - y, cx - x) - mo->angle*(6.2831853f/4294967296.0f);
    chasmside = d < 1 ? 0 : -sinf (rel);
}


void L_Shaft (player_t* player)
{
    mobj_t*	mo = player->mo;

    if (InStairwell (mo))
	Stairs (mo);
    if (L_InChasm (mo))
	Fall (player);
    else
    {
	OverTheRailing (player);
	lastz = mo->z;
    }
    Chasm (mo);
    fallspeed = L_InChasm (mo) ? sqrtf ((float)mo->momx*mo->momx + (float)mo->momy*mo->momy
					+ (float)mo->momz*mo->momz)/FRACUNIT : 0;
}


float P_LibraryFallSpeed (void)
{
    return P_Library () && gamestate == GS_LEVEL && !paused && !menuactive ? fallspeed : 0;
}


float P_LibraryChasm (float* side)
{
    boolean	on = P_Library () && gamestate == GS_LEVEL && !paused && !menuactive;

    *side = on ? chasmside : 0;
    return on ? chasmopen : 0;
}


// Falling through the chasm: how fast he goes, in units a tic (for the
// renderer's rush).
boolean P_LibraryFall (float* x, float* y, float* z)
{
    mobj_t*	mo = players[displayplayer].mo;

    if (!P_Library () || !mo || !L_InChasm (mo))
	return false;
    *x = mo->momx/(float)FRACUNIT;
    *y = mo->momy/(float)FRACUNIT;
    *z = mo->momz/(float)FRACUNIT;
    return true;
}


void L_ArchiveShaft (void)
{
    L_Put (&lastz, sizeof lastz);
}


void L_UnArchiveShaft (void)
{
    L_Get (&lastz, sizeof lastz);
}
