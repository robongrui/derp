//
// Imps on the ceiling (p_hang.h).
//

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "doomdef.h"
#include "doomstat.h"
#include "m_random.h"
#include "p_local.h"
#include "p_barks.h"
#include "p_derp.h"
#include "p_derplog.h"
#include "p_hang.h"
#include "p_nav.h"
#include "p_navbudget.h"
#include "p_navlong.h"
#include "p_senses.h"
#include "p_tactics.h"
#include "p_mind.h"
#include "r_main.h"
#include "r_state.h"
#include "s_sound.h"
#include "sounds.h"
#include "tables.h"
#include "p_tune.h"

// Where: the ceiling at least this far over the floor, not sky, within this of the hiding
// place it picked. The odds in 256 that one goes up at all. (128 was a man and an imp and a
// hand between: its feet just over his head, and it looked like it stood on a shelf, the
// user, 2026-11-17. Now more than an imp between them. Doom's rooms are 128-152 or 192 and
// up, so this leaves the low half out and keeps the tall rooms.)
#define HANG_CEILING	TUNE (HANG_CEILING, 176)
#define HANG_NEAR	TUNE (HANG_NEAR, 512)
#define HANG_ODDS	TUNE (HANG_ODDS, 160)
// Over his way in, not the darkest corner (that took them to the far end of a room, or out
// of it): on his walk from where it thinks he is to where it hides, the last stretch of it,
// where he does not see it up there from this far back along the way, nor from twice that.
// Just past a doorway, a lintel lower than the ceiling hides it, and better than anywhere.
#define HANG_BACK	8
#define HANG_ROUTE	1024
#define WAYIN_SEARCH	16000
// When: he within this, walked on past it (his back to it), and far enough on that it lands
// beside him, not on him. The hiss lasts this long; it turns over in this many tics falling.
#define HANG_STRIKE	TUNE (HANG_STRIKE, 192)
#define HANG_CLEAR	TUNE (HANG_CLEAR, 48)
#define HANG_HISS	TUNE (HANG_HISS, 10)
#define HANG_ROLL	TUNE (HANG_ROLL, 10)
#define HANG_BREATH	TUNE (HANG_BREATH, 640)
#define HANG_SEEN	(ANG45/2)
#define HANG_BEHIND	ANG90

enum { HANG_NONE, HANG_MEANS, HANG_CLIMB, HANG_ON, HANG_HISSING, HANG_DROP };


boolean P_HangPlace (fixed_t x, fixed_t y)
{
    sector_t*	s = R_PointInSubsector (x, y)->sector;

    return s->ceilingpic != skyflatnum && s->ceilingheight - s->floorheight >= HANG_CEILING*FRACUNIT;
}

// A place to hang all round, not its edge: the bot's imps chose spots at the brink of a
// dark room, and stood on the ledge beside it under a ceiling too low.
#define HANG_EDGE	32

static boolean Inside (fixed_t x, fixed_t y)
{
    return P_HangPlace (x, y) && P_HangPlace (x + HANG_EDGE*FRACUNIT, y) && P_HangPlace (x - HANG_EDGE*FRACUNIT, y)
	&& P_HangPlace (x, y + HANG_EDGE*FRACUNIT) && P_HangPlace (x, y - HANG_EDGE*FRACUNIT);
}

static int RoomAt (fixed_t x, fixed_t y)
{
    return P_NavRoom ((int)(R_PointInSubsector (x, y)->sector - sectors));
}

// One standing at (ox, oy) sees an imp hanging at (x, y): a lintel lower than the room's
// ceiling hides it from outside, as the doorway's own wall does from either side.
static boolean SeenUp (fixed_t x, fixed_t y, fixed_t ox, fixed_t oy)
{
    static mobj_t	eye, body;

    memset (&eye, 0, sizeof eye);
    eye.x = ox;
    eye.y = oy;
    eye.subsector = R_PointInSubsector (ox, oy);
    eye.z = eye.subsector->sector->floorheight;
    eye.height = 56*FRACUNIT;
    memset (&body, 0, sizeof body);
    body.x = x;
    body.y = y;
    body.subsector = R_PointInSubsector (x, y);
    body.height = 56*FRACUNIT;
    body.z = body.subsector->sector->ceilingheight - body.height;
    return P_CheckSight (&eye, &body);
}

// It gets up there on foot, and soon: a ceiling 120 units off as the crow flies was a jump
// out of a window and the long way round (E1M2's shotgun imp left the shotgun for it). One
// holding something hangs by it, or not at all.
#define HANG_WALK	TUNE (HANG_WALK, 640)
#define HANG_POST	TUNE (HANG_POST, 384)

// The walks from where it stands, flooded once for every spot P_HangSpot tries:
// a search each walked the same floor again, hundreds of 8,000-cell searches
// as one imp woke. 0: not flooded yet; 1: flooded; -1: no flood, a search each.
static int	flooded;

static boolean Reaches (mobj_t* actor, int nav, fixed_t x, fixed_t y)
{
    static int	path[2];
    int		reached, walk;

    if (P_DerpOnDuty (actor) && P_AproxDistance (x - actor->guardx, y - actor->guardy) > HANG_POST*FRACUNIT)
	return false;
    if (!flooded)
    {
	P_DerpRouteBegin (actor);
	flooded = P_NavFlood (nav, actor->x, actor->y, HANG_WALK) ? 1 : -1;
	P_DerpRouteEnd ();
    }
    walk = flooded > 0 ? P_NavFloodWalk (nav, x, y) : -2;
    if (walk != -2)
	return walk >= 0 && walk <= HANG_WALK;
    P_DerpRouteBegin (actor);
    reached = P_NavPath (nav, actor->x, actor->y, x, y, path, 2, 8000);
    P_DerpRouteEnd ();
    return reached > 0 && navwalk <= HANG_WALK;
}

// The way he would walk to it, and on that, as near where it hides as can be, a place he does
// not see it hang until he is under it; one he has just come into a room at first. True if
// there is one; NAV_DEFERRED while his way is being searched, over tics (p_navlong.h).
static int WayIn (mobj_t* actor, int nav, fixed_t* hx, fixed_t* hy)
{
    static int	way[HANG_ROUTE];
    int		n, i, best = -1, besttier = 2;

    // (Searched no further than half a tic's cells near him: his way in from far
    // off, round the level, took up to 120,000 cells in the tic the imp woke.
    // Past that, the ceiling nearest where it hides.)
    P_DerpRouteBegin (actor);
    n = P_NavLongPath (actor, NAVLONG_WAYIN, nav, actor->knownx, actor->knowny, actor->lurkx, actor->lurky,
		       way, HANG_ROUTE, WAYIN_SEARCH);
    P_DerpRouteEnd ();
    if (n == NAV_DEFERRED)
	return NAV_DEFERRED;
    if (n <= 2*HANG_BACK)
	return false;
    for (i = n - 1; i >= 2*HANG_BACK && besttier; i -= 2)
    {
	fixed_t	x, y, bx, by, fx, fy;
	int	tier;
	P_NavPoint (nav, way[i], &x, &y, NULL);
	if (P_AproxDistance (x - actor->lurkx, y - actor->lurky) > HANG_NEAR*FRACUNIT)
	    break;
	P_NavPoint (nav, way[i - HANG_BACK], &bx, &by, NULL);
	P_NavPoint (nav, way[i - 2*HANG_BACK], &fx, &fy, NULL);
	tier = RoomAt (bx, by) != RoomAt (x, y) ? 0 : 1;
	if (tier >= besttier || P_NavDoor (nav, way[i]) || P_NavHurts (way[i]) || !Inside (x, y)
	    || SeenUp (x, y, bx, by) || SeenUp (x, y, fx, fy))
	    continue;
	if (!Reaches (actor, nav, x, y))
	    continue;
	best = way[i];
	besttier = tier;
    }
    if (best < 0)
	return false;
    P_NavPoint (nav, best, hx, hy, NULL);
    return true;
}

// Nowhere on his way in (no way, or he would see it up there all along it): the ceiling
// nearest where it hides that he does not see it on from where it thinks he is, nor from a
// step to either side.
static boolean NearHide (mobj_t* actor, int nav, fixed_t* hx, fixed_t* hy)
{
    angle_t	an = (R_PointToAngle2 (actor->knownx, actor->knowny, actor->lurkx, actor->lurky) + ANG90)>>ANGLETOFINESHIFT;
    fixed_t	sx = 48*finecosine[an], sy = 48*finesine[an], bestd = MAXINT;
    int		dx, dy;

    for (dy = -HANG_NEAR; dy <= HANG_NEAR; dy += 32)
	for (dx = -HANG_NEAR; dx <= HANG_NEAR; dx += 32)
	{
	    fixed_t	x = actor->lurkx + dx*FRACUNIT, y = actor->lurky + dy*FRACUNIT, d = P_AproxDistance (dx*FRACUNIT, dy*FRACUNIT);
	    int		cell = P_NavCellAt (nav, x, y);
	    if (d >= bestd || d > HANG_NEAR*FRACUNIT || cell < 0 || !P_NavStandable (nav, cell) || P_NavDoor (nav, cell)
		|| P_NavHurts (cell) || !Inside (x, y) || SeenUp (x, y, actor->knownx, actor->knowny)
		|| SeenUp (x, y, actor->knownx + sx, actor->knowny + sy) || SeenUp (x, y, actor->knownx - sx, actor->knowny - sy))
		continue;
	    if (!Reaches (actor, nav, x, y))
		continue;
	    bestd = d;
	    P_NavPoint (nav, cell, hx, hy, NULL);
	}
    return bestd != MAXINT;
}

static void Means (mobj_t* actor, fixed_t x, fixed_t y, const char* where)
{
    actor->lurkx = x;
    actor->lurky = y;
    actor->hang = HANG_MEANS;
    P_DerpLog (actor, "means to hang %s at (%d,%d)", where, x >> FRACBITS, y >> FRACBITS);
}

// Imps whose dice said hang, waiting for his way in to be searched (by serial; tic
// last asked). It walks to where it hides meanwhile; past HANG_WAITED unasked, forgotten.
#define HANG_WAITERS	8
#define HANG_WAITED	(2*TICRATE)

static struct
{
    int		serial, tic;
} waiters[HANG_WAITERS];

static int Waiter (const mobj_t* actor)
{
    int		i;

    for (i = 0; i < HANG_WAITERS; i++)
	if (waiters[i].serial == actor->serial && actor->serial
	    && leveltime >= waiters[i].tic && leveltime - waiters[i].tic <= HANG_WAITED)
	    return i;
    return -1;
}

// After the dice: on his way in, else out of his sight; or waits for the way.
static void Spot (mobj_t* actor, int nav)
{
    fixed_t	x, y;
    int		in, w = Waiter (actor), i;

    flooded = 0;
    in = WayIn (actor, nav, &x, &y);
    if (in == NAV_DEFERRED)
    {
	// (A free place, else the oldest; one from a level before is the oldest.)
	if (w < 0)
	    for (w = 0, i = 0; i < HANG_WAITERS; i++)
	    {
		if (!waiters[i].serial)
		{
		    w = i;
		    break;
		}
		if (waiters[i].tic < waiters[w].tic || waiters[i].tic > leveltime)
		    w = i;
	    }
	waiters[w].serial = actor->serial;
	waiters[w].tic = leveltime;
	return;
    }
    if (w >= 0)
	waiters[w].serial = 0;
    if (in)
	Means (actor, x, y, "over his way in");
    else if (NearHide (actor, nav, &x, &y))
	Means (actor, x, y, "out of his sight");
}

void P_HangSpot (mobj_t* actor)
{
    int		nav = P_NavClassOf (actor);

    if (nav < 0 || P_Random () >= HANG_ODDS)
	return;
    Spot (actor, nav);
}

void P_HangWaits (mobj_t* actor)
{
    int		nav = P_NavClassOf (actor);

    if (Waiter (actor) < 0)
	return;
    if (nav < 0)
    {
	waiters[Waiter (actor)].serial = 0;
	return;
    }
    Spot (actor, nav);
}

boolean P_HangUp (mobj_t* actor)
{
    mobj_t*	him = P_DerpPlayer ();
    fixed_t	gap;

    if (actor->hang != HANG_MEANS)
	return false;
    // Seen there, or it is not where it can: it waits on the floor as the others do. The
    // ceiling it would reach is the lowest its body is under, not the one at its middle:
    // a step off the spot, beside a doorway, it went up to the lintel and hung with its
    // feet a hand over the floor (E1M1, a 72 lintel by a 128 room).
    if (!him || P_CheckSight (him, actor) || !P_HangPlace (actor->x, actor->y) || actor->z > actor->floorz
	|| actor->ceilingz - actor->floorz < HANG_CEILING*FRACUNIT)
    {
	P_DerpLog (actor, "does not hang: %s", !him ? "nobody" : P_CheckSight (him, actor) ? "he sees it"
		   : actor->z > actor->floorz ? "in the air"
		   : !P_HangPlace (actor->x, actor->y) ? "no ceiling to hang from here" : "a lower ceiling over it here");
	actor->hang = HANG_NONE;
	return false;
    }
    P_DerpRule (actor, "hunt/lurk_going.hangs", "Dark, high, and he does not see it: up it goes, to hang "
		"from the ceiling");
    gap = actor->ceilingz - actor->height - actor->z;
    // Up fast enough to get there (a tic of gravity on each): held there on arrival.
    actor->momz = (fixed_t)((sqrt (2.0*gap/FRACUNIT) + 1.5)*FRACUNIT);
    actor->momx = actor->momy = 0;
    actor->hang = HANG_CLIMB;
    actor->hangtic = leveltime;
    P_DerpLog (actor, "goes up to hang from the ceiling");
    return true;
}

void P_HangEnd (mobj_t* mo)
{
    if (!mo->hang)
	return;
    mo->flags &= ~MF_NOGRAVITY;
    mo->hang = HANG_NONE;
}

// It lets go. At him, if it can: down and at his back, claws out.
static void Drop (mobj_t* actor, mobj_t* him, boolean strike)
{
    actor->flags &= ~MF_NOGRAVITY;
    actor->hang = HANG_DROP;
    actor->hangtic = leveltime;
    P_MindLurkOver (actor, "goes in");
    if (!strike || !him)
	return;
    {
	double	fall = sqrt (2.0*(actor->z - him->z)/FRACUNIT);
	fixed_t	dist = P_AproxDistance (him->x - actor->x, him->y - actor->y);
	angle_t	an = R_PointToAngle2 (actor->x, actor->y, him->x, him->y)>>ANGLETOFINESHIFT;
	fixed_t	speed = fall > 1 ? (fixed_t)(dist/fall) : 0;
	if (speed > 16*FRACUNIT)
	    speed = 16*FRACUNIT;
	actor->momx = FixedMul (speed, finecosine[an]);
	actor->momy = FixedMul (speed, finesine[an]);
	actor->target = him;
	actor->angle = an<<ANGLETOFINESHIFT;
	// The pounce's strike in the air, and its landing (p_demons.c).
	actor->pounce = 2;
	actor->vault = false;
	actor->pouncetic = leveltime;
	P_SetMobjState (actor, S_TROO_ATK2);
	actor->tics = 2;
    }
}

boolean P_HangThink (mobj_t* actor, boolean visible)
{
    mobj_t*	him = P_DerpPlayer ();
    fixed_t	dist;
    angle_t	off;

    switch (actor->hang)
    {
      case HANG_CLIMB:
	actor->movedir = 8;
	if (actor->z + actor->height >= actor->ceilingz - 2*FRACUNIT)
	{
	    actor->flags |= MF_NOGRAVITY;
	    actor->z = actor->ceilingz - actor->height;
	    actor->momz = 0;
	    actor->hang = HANG_ON;
	    actor->hangtic = leveltime;
	    P_DerpLog (actor, "hangs from the ceiling");
	    return true;
	}
	// Fell short: on the floor after all.
	if (actor->momz <= 0 && leveltime - actor->hangtic > 2)
	{
	    actor->hang = HANG_NONE;
	    return false;
	}
	return true;

      case HANG_ON:
	actor->movedir = 8;
	actor->momx = actor->momy = actor->momz = 0;
	actor->z = actor->ceilingz - actor->height;
	if (!him)
	    return true;
	// Hurt up there, or no longer lying in wait: down it comes.
	if (actor->state == &states[actor->info->painstate] || !actor->lurk)
	{
	    P_DerpRule (actor, "hunt/lurking.falls", "Shot hanging, or done waiting: it lets go");
	    Drop (actor, him, false);
	    return false;
	}
	dist = P_AproxDistance (him->x - actor->x, him->y - actor->y);
	off = R_PointToAngle2 (him->x, him->y, actor->x, actor->y) - him->angle;
	// He looks at it: no more hiding, down and at him now.
	if (visible && P_CheckSight (him, actor) && (off < HANG_SEEN || off > (angle_t)-HANG_SEEN))
	{
	    P_DerpRule (actor, "hunt/lurking.seen_hanging", "He looked up at it: it drops on him at once");
	    P_DerpCry (actor, BARK_IMPAIR);
	    Drop (actor, him, true);
	    return false;
	}
	if (dist < HANG_BREATH*FRACUNIT)
	    P_DerpCry (actor, BARK_IMPLURK);
	// He has walked on under it, back to it, not right beneath: it gathers itself.
	if (dist < HANG_STRIKE*FRACUNIT && dist > HANG_CLEAR*FRACUNIT && off > HANG_BEHIND && off < (angle_t)-HANG_BEHIND)
	{
	    P_DerpRule (actor, "hunt/lurking.hisses", "He passed under it, his back to it: a hiss, and it lets go");
	    if (!P_DerpCry (actor, BARK_IMPCROUCH))
		S_StartSound (actor, sfx_bgact);
	    actor->hang = HANG_HISSING;
	    actor->hangtic = leveltime;
	}
	return true;

      case HANG_HISSING:
	actor->movedir = 8;
	actor->z = actor->ceilingz - actor->height;
	if (leveltime - actor->hangtic < HANG_HISS)
	    return true;
	P_DerpCry (actor, BARK_IMPAIR);
	Drop (actor, him, him && P_AproxDistance (him->x - actor->x, him->y - actor->y) > HANG_CLEAR*FRACUNIT);
	return false;

      default:
	return false;
    }
}

float P_HangRoll (const mobj_t* mo, float fraction)
{
    float	t;

    switch (mo->hang)
    {
      case HANG_CLIMB:
	t = (leveltime - mo->hangtic + fraction)/HANG_ROLL;
	return t < 1 ? t : 1;
      case HANG_ON:
      case HANG_HISSING:
	return 1;
      case HANG_DROP:
	t = (leveltime - mo->hangtic + fraction)/HANG_ROLL;
	return t < 1 ? 1 - t : 0;
      default:
	return 0;
    }
}

boolean P_HangAbove (const mobj_t* a, const mobj_t* b)
{
    return ((a->hang == HANG_ON || a->hang == HANG_HISSING || a->hang == HANG_CLIMB) && b->z + b->height <= a->z + FRACUNIT)
	|| ((b->hang == HANG_ON || b->hang == HANG_HISSING || b->hang == HANG_CLIMB) && a->z + a->height <= b->z + FRACUNIT);
}
