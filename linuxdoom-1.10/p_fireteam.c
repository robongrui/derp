// DERP's fire team (p_tactics.h): a soldier keeps to its gun's range at a fire
// position of its own, bounds to a new one while its team covers it, pushes in,
// goes round a held door, lays fire where he went, baits him past a friend.

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "doomdef.h"
#include "doomstat.h"
#include "i_system.h"
#include "m_random.h"
#include "p_local.h"
#include "p_derp.h"
#include "p_nav.h"
#include "p_navbudget.h"
#include "p_senses.h"
#include "p_tactics.h"
#include "p_decide.h"
#include "p_word.h"
#include "p_hang.h"
#include "p_derplog.h"
#include "p_corners.h"
#include "p_bullets.h"
#include "p_fire.h"
#include "p_director.h"
#include "p_portals.h"
#include "p_coverplan.h"
#include "p_gunwalk.h"
#include "p_roles.h"
#include "p_chatter.h"
#include "p_difficulty.h"
#include "p_barks.h"
#include "r_main.h"
#include "r_state.h"
#include "s_sound.h"
#include "tables.h"
#include "p_tune.h"
#include "p_mind.h"
#include "p_tactics_local.h"

#define BOUND_TIME	TUNE (BOUND_TIME, 5*TICRATE)
#define PUSH_TIME	TUNE (PUSH_TIME, 6*TICRATE)
#define ROUND_TIME	TUNE (ROUND_TIME, 12*TICRATE)
#define ROUND_CLEAR	TUNE (ROUND_CLEAR, 128)
#define ROUND_MOST	320
#define ROUND_AVOID	TUNE (ROUND_AVOID, 192)
#define CLAIMED		TUNE (CLAIMED, 96)

static int FirePosition (mobj_t* actor, mobj_t* player, int lo, int hi, fixed_t* fx, fixed_t* fy);
static boolean Claimed (mobj_t* actor, fixed_t x, fixed_t y);
static boolean Holds (mobj_t* actor, mobj_t* player, fixed_t dist, int hi);


// A doorway behind him for actor to run to: beside the one of its room
// nearest a point behind his back, not on top of him and not in front of
// him, nobody's, a walk it can make (FindAmbush sees to that). He in sight:
// it sees which way he faces, and no more than that.
boolean P_TacWayOut (mobj_t* actor, mobj_t* player, fixed_t* x, fixed_t* y)
{
    angle_t	an = (player->angle + ANG180)>>ANGLETOFINESHIFT, off, facing;
    fixed_t	cx, cy, d;

    if (!P_TacFindAmbush (actor, player->x + 384*finecosine[an], player->y + 384*finesine[an], &cx, &cy, &facing))
	return false;
    d = P_AproxDistance (cx - player->x, cy - player->y);
    off = R_PointToAngle2 (player->x, player->y, cx, cy) - player->angle;
    if (d < 192*FRACUNIT || d > 768*FRACUNIT || off < ANG90 || off > ANG270 || Claimed (actor, cx, cy))
	return false;
    *x = cx;
    *y = cy;
    return true;
}

boolean P_TacOutOfRange (mobj_t* actor, mobj_t* player)
{
    int	lo, hi, aside;

    P_TacRange (actor, &lo, &hi, &aside);
    return P_AproxDistance (player->x - actor->x, player->y - actor->y) > hi*FRACUNIT;
}

// No way to him on foot, looked for every so often. He has it beaten: walking at
// him only takes it into his sights.
#define NOWAY_EVERY	TUNE (NOWAY_EVERY, 2*TICRATE)

boolean P_TacNoWay (mobj_t* actor, mobj_t* player)
{
    static int	path[2];
    int		nav = P_NavClassOf (actor);

    if (nav < 0)
	return false;
    // (The tic's routes spent: what it last found holds, and it looks at its next step.)
    if (!actor->waychecktic || leveltime - actor->waychecktic >= NOWAY_EVERY)
    {
	int	n = P_NavPathFor (actor, nav, actor->x, actor->y, player->x, player->y, path, 2, 12000);
	if (n != NAV_DEFERRED)
	{
	    actor->waychecktic = leveltime;
	    actor->noway = n <= 0 && !navexhausted;
	}
    }
    return actor->noway;
}

// A way to where it thinks he is that stays clear of a door, not too long.
boolean P_TacOtherWay (mobj_t* actor, fixed_t dx, fixed_t dy)
{
    static int	path[ROUND_MOST + 1];
    int		nav = P_NavClassOf (actor), n, i;

    if (nav < 0)
	return false;
    navavoidx = dx;
    navavoidy = dy;
    navavoidr = ROUND_AVOID;
    n = P_NavPath (nav, actor->x, actor->y, actor->knownx, actor->knowny, path, ROUND_MOST + 1, 12000);
    navavoidr = 0;
    if (n <= 0 || n > ROUND_MOST)
	return false;
    for (i = 0; i < n; i++)
    {
	fixed_t	x, y;
	P_NavPoint (nav, path[i], &x, &y, NULL);
	if (P_AproxDistance (x - dx, y - dy) < ROUND_CLEAR*FRACUNIT)
	    return false;
    }
    return true;
}

// A man with a gun does not walk up to what he shoots. Each kind keeps to
// its range (units): from beyond it, to a fire position of its own; inside
// it, it backs off; between, it holds and fires, a step aside only while
// he is shooting (chance in 256 a chase step). The shotgunner pushes in,
// its spread wants it close; the chaingunner holds and fires to keep him
// down.
boolean P_Move (mobj_t* actor);

void P_TacRange (mobj_t* actor, int* lo, int* hi, int* aside)
{
    switch (actor->type)
    {
      case MT_SHOTGUY:	*lo = 128; *hi = 384; *aside = 96; break;
      case MT_CHAINGUY:	*lo = 384; *hi = 896; *aside = 24; break;
      default:		*lo = 320; *hi = 768; *aside = 64; break;
    }
}

// A place of its own to fire from: in the middle of its range, seeing him,
// at its own angle off the line it comes in on (by its number, up to 60
// degrees either way), so a squad closing in fans out instead of filing up
// one behind another. True if found; NAV_DEFERRED with the tic's routes spent
// (then *fx, *fy are untouched and nothing is known).
static int FirePosition (mobj_t* actor, mobj_t* player, int lo, int hi, fixed_t* fx, fixed_t* fy)
{
    static int	path[2];
    int		nav = P_NavClassOf (actor), k, cell, walk;
    angle_t	base = R_PointToAngle2 (player->x, player->y, actor->x, actor->y), an;
    int		spread = (int)((unsigned)actor->serial*37 % 121) - 60;
    fixed_t	r = (lo + hi)/2*FRACUNIT, qx, qy, qf;

    // A rifleman: back and off to the side, a clear angle on him past whoever
    // is in front, never straight behind them.
    if (P_RoleOf (actor) == ROLE_RIFLE)
	spread = ((actor->serial & 1) ? 1 : -1)*(35 + (int)((unsigned)actor->serial*37 % 41));

    if (nav < 0)
	return false;
    // Its own angle first, then nearer the straight line, then straight.
    for (k = 0; k < 3; k++)
    {
	an = base + (angle_t)(spread*(2 - k)/2)*(ANG45/45);
	cell = P_NavCellAt (nav, player->x + FixedMul (r, finecosine[an>>ANGLETOFINESHIFT]),
			    player->y + FixedMul (r, finesine[an>>ANGLETOFINESHIFT]));
	if (cell < 0 || P_NavDoor (nav, cell) || P_NavHurts (cell))
	    continue;
	P_NavPoint (nav, cell, &qx, &qy, &qf);
	// Seeing him, nobody's, and a walk it can make (not across a gap
	// it would stand short of, choosing it again each time it thinks).
	if (!P_CoverShoots (qx, qy, qf, player) || Claimed (actor, qx, qy))
	    continue;
	walk = P_NavPathFor (actor, nav, actor->x, actor->y, qx, qy, path, 2, 2000);
	if (walk == NAV_DEFERRED)
	    return NAV_DEFERRED;
	if (walk <= 0)
	    continue;
	*fx = qx;
	*fy = qy;
	return true;
    }
    return false;
}

// When a soldier last set off on a fake retreat (Bait): one at a time.
static int	lastbait;

void P_FireteamLevel (void)
{
    lastbait = 0;
}


// A fake retreat: in a lull (the director holding back), a soldier he can see
// with a friend lying in wait behind it turns and runs, past that friend. It
// looks like any other retreat, and that is the point: he gives chase, comes
// round the corner into the one waiting, and it turns on him too. One at a
// time on a level.
#define BAIT_NEAR	TUNE (BAIT_NEAR, 640)
#define BAIT_PAST	TUNE (BAIT_PAST, 96)
#define BAIT_AGAIN	TUNE (BAIT_AGAIN, 30*TICRATE)
#define BAIT_ODDS	TUNE (BAIT_ODDS, 36)

//
// Fire teams: the soldiers fighting him near each other. One at a time moves
// to a new fire position (a bound) while the others hold and shoot to cover
// it; the mover sees its move through.
//

boolean P_TacBait (mobj_t* actor, mobj_t* player)
{
    thinker_t*	th;
    mobj_t*	trap = NULL;
    fixed_t	bestd = MAXINT, here = P_AproxDistance (actor->x - player->x, actor->y - player->y);
    int		nav = P_NavClassOf (actor), cell;
    static int	path[2];

    if (P_DirectorPressing () || (lastbait && leveltime - lastbait < BAIT_AGAIN) || nav < 0
	|| P_Random () >= BAIT_ODDS)
	return false;
    for (th = thinkercap.next; th != &thinkercap; th = th->next)
    {
	mobj_t*	mo = (mobj_t *)th;
	fixed_t	d;
	if (th->function.acp1 != (actionf_p1)P_MobjThinker || mo == actor || !P_DerpMonster (mo)
	    || !P_DerpSoldier (mo->type) || mo->mind != MIND_AMBUSH || mo->tactic != STEP_WAITING)
	    continue;
	d = P_AproxDistance (mo->x - actor->x, mo->y - actor->y);
	if (d < bestd && d <= BAIT_NEAR*FRACUNIT
	    && P_AproxDistance (mo->x - player->x, mo->y - player->y) > here && !P_CheckSight (player, mo))
	{
	    bestd = d;
	    trap = mo;
	}
    }
    if (!trap)
	return false;
    // Past it, on the far side from him, or beside it.
    {
	angle_t	an = R_PointToAngle2 (player->x, player->y, trap->x, trap->y);
	fixed_t	x = trap->x + BAIT_PAST*finecosine[an>>ANGLETOFINESHIFT];
	fixed_t	y = trap->y + BAIT_PAST*finesine[an>>ANGLETOFINESHIFT];
	cell = P_NavCellAt (nav, x, y);
	if (cell < 0 || !P_NavStandable (nav, cell) || P_NavHurts (cell))
	{
	    x = trap->x + BAIT_PAST*finesine[an>>ANGLETOFINESHIFT];
	    y = trap->y - BAIT_PAST*finecosine[an>>ANGLETOFINESHIFT];
	    cell = P_NavCellAt (nav, x, y);
	    if (cell < 0 || !P_NavStandable (nav, cell) || P_NavHurts (cell))
		return false;
	}
	if (P_NavPath (nav, actor->x, actor->y, x, y, path, 2, 3000) <= 0)
	    return false;
	actor->coverx = x;
	actor->covery = y;
    }
    lastbait = leveltime;
    P_MindBecome (actor, MIND_RETREAT, STEP_BAIT, "hunt.bait",
    		"A lull, a friend in wait behind it, and he can see it: it fakes a "
    		"retreat to draw him on to that friend");
    P_DerpLog (actor, "falls back to draw him on to a friend in wait");
    P_DerpBark (actor, BARK_BAIT);	// as loud as when it means it: that is the trick
    return true;
}

// actor's fire team: fellow soldiers within earshot fighting him (after him,
// or in cover, and in touch with him in the last few seconds). How many,
// and whether one of them is on a bound now.
int P_TacTeam (mobj_t* actor, boolean* bounding)
{
    thinker_t*	th;
    int		n = 0;

    *bounding = false;
    for (th = thinkercap.next; th != &thinkercap; th = th->next)
    {
	mobj_t*	mo = (mobj_t *)th;
	if (th->function.acp1 != (actionf_p1)P_MobjThinker || mo == actor || !P_DerpMonster (mo)
	    || !P_DerpSoldier (mo->type) || mo->target != actor->target
	    || (mo->mind != MIND_HUNT && mo->mind != MIND_COVER)
	    || leveltime - mo->knowntic > 3*TICRATE
	    || P_AproxDistance (mo->x - actor->x, mo->y - actor->y) >= NEARBY*FRACUNIT)
	    continue;
	n++;
	if (P_MindBounding (mo))
	    *bounding = true;
    }
    return n;
}

// Whether a fire position at (x, y) is someone else's: a soldier stands
// there, or is on its way there.
static boolean Claimed (mobj_t* actor, fixed_t x, fixed_t y)
{
    thinker_t*	th;

    for (th = thinkercap.next; th != &thinkercap; th = th->next)
    {
	mobj_t*	mo = (mobj_t *)th;
	if (th->function.acp1 != (actionf_p1)P_MobjThinker || mo == actor || !P_DerpMonster (mo)
	    || !P_DerpSoldier (mo->type))
	    continue;
	if (P_AproxDistance (mo->x - x, mo->y - y) < CLAIMED*FRACUNIT
	    || ((mo->firex || mo->firey) && P_AproxDistance (mo->firex - x, mo->firey - y) < CLAIMED*FRACUNIT))
	    return true;
    }
    return false;
}

// A new fire position some way round him from where it stands, on the side
// away from its nearest teammate (the team spreads out), in its range,
// seeing him, not claimed, and walkable to.
boolean P_TacBoundPosition (mobj_t* actor, mobj_t* player, int lo, int hi, fixed_t* fx, fixed_t* fy)
{
    static const int	offs[6] = { 45, 30, 60, -45, -30, -60 };
    static int		path[2];
    int			nav = P_NavClassOf (actor), k, cell, sign = 1, walk;
    angle_t		base = R_PointToAngle2 (player->x, player->y, actor->x, actor->y), an;
    fixed_t		r = P_AproxDistance (player->x - actor->x, player->y - actor->y), qx, qy, qf;
    fixed_t		nearest = MAXINT;
    thinker_t*		th;

    if (nav < 0)
	return false;
    if (r < lo*FRACUNIT || r > hi*FRACUNIT)
	r = (lo + hi)/2*FRACUNIT;
    // Away from the nearest teammate round him.
    for (th = thinkercap.next; th != &thinkercap; th = th->next)
    {
	mobj_t*	mo = (mobj_t *)th;
	fixed_t	d;
	if (th->function.acp1 != (actionf_p1)P_MobjThinker || mo == actor || !P_DerpMonster (mo)
	    || !P_DerpSoldier (mo->type) || mo->target != actor->target)
	    continue;
	d = P_AproxDistance (mo->x - actor->x, mo->y - actor->y);
	if (d < nearest)
	{
	    nearest = d;
	    sign = (angle_t)(R_PointToAngle2 (player->x, player->y, mo->x, mo->y) - base) < ANG180 ? -1 : 1;
	}
    }
    for (k = 0; k < 6; k++)
    {
	an = base + (angle_t)(offs[k]*sign)*(ANG45/45);
	cell = P_NavCellAt (nav, player->x + FixedMul (r, finecosine[an>>ANGLETOFINESHIFT]),
			    player->y + FixedMul (r, finesine[an>>ANGLETOFINESHIFT]));
	if (cell < 0 || P_NavDoor (nav, cell) || P_NavHurts (cell))
	    continue;
	P_NavPoint (nav, cell, &qx, &qy, &qf);
	if (!P_CoverShoots (qx, qy, qf, player) || Claimed (actor, qx, qy))
	    continue;
	// (Asked inside a decision, which never waits; were it to, no bound this
	// time, the old fire position kept.)
	walk = P_NavPathFor (actor, nav, actor->x, actor->y, qx, qy, path, 2, 2000);
	if (walk == NAV_DEFERRED)
	    return false;
	if (walk <= 0)
	    continue;
	*fx = qx;
	*fy = qy;
	return true;
    }
    return false;
}

// On its bound: there (or long enough), it holds and fires from there. At the
// way out behind him it stays a while: that is what it ran there for, and it
// dropped the job the moment it arrived.
#define WAYOUT_HOLD	TUNE (WAYOUT_HOLD, 8*TICRATE)

boolean P_TacBound (mobj_t* actor)
{
    boolean	there = P_AproxDistance (actor->x - actor->firex, actor->y - actor->firey) < 24*FRACUNIT;

    if (there || leveltime - actor->tactictic > BOUND_TIME)
    {
	if (actor->holdtic < 0)
	    actor->holdtic = there ? leveltime + WAYOUT_HOLD : 0;
	P_MindStep (actor, STEP_NONE, "hunt/bounding.done",
			"There, or long enough: it holds and fires from there");
	actor->movedir = 8;
	return true;
    }
    P_DerpWalkTo (actor, actor->firex, actor->firey);
    return true;
}

// On its way round a held door: until it has him in sight, or it has been long
// enough. The rest is an ordinary fight.
boolean P_TacRound (mobj_t* actor, boolean visible)
{
    if (visible || leveltime - actor->tactictic > ROUND_TIME)
    {
	P_MindStep (actor, STEP_NONE, "hunt/rounding.done",
			"It has him in sight, or has been long enough: an "
			"ordinary fight again");
	return false;
    }
    navavoidx = actor->avoidx;
    navavoidy = actor->avoidy;
    navavoidr = ROUND_AVOID;
    P_DerpWalkTo (actor, actor->knownx, actor->knowny);
    navavoidr = 0;
    return true;
}

// On its push: in where its gun counts, then it fights from there.
boolean P_TacPush (mobj_t* actor, mobj_t* player, boolean visible)
{
    int		lo, hi, aside;
    mobj_t*	at = P_TacBelieved (actor, player, visible);

    P_TacRange (actor, &lo, &hi, &aside);
    if (P_AproxDistance (at->x - actor->x, at->y - actor->y) <= PUSH_HOME (hi)*FRACUNIT
	|| leveltime - actor->tactictic > PUSH_TIME)
    {
	P_MindStep (actor, STEP_NONE, "hunt/pushing.done",
			"In where its gun counts, or long enough: it fights from "
			"there");
	return false;
    }
    P_DerpWalkTo (actor, at->x, at->y);
    return true;
}

#define HOLD_FAR	TUNE (HOLD_FAR, 1024)
#define HIGH_GROUND	TUNE (HIGH_GROUND, 24)

// Beyond its range and it stays anyway: it has what it wants where it is (up
// above him, at its post, at the way out), and he comes to it.
static boolean Holds (mobj_t* actor, mobj_t* player, fixed_t dist, int hi)
{
    return dist > hi*FRACUNIT
	&& (P_TacNoWay (actor, player)	// (No use going to him: there is no way to him.)
	    || (dist <= HOLD_FAR*FRACUNIT
		&& (actor->guard || leveltime < actor->holdtic || P_RoleOf (actor) == ROLE_LANE
		    || actor->z >= player->z + HIGH_GROUND*FRACUNIT)));
}

boolean P_TacKeepRange (mobj_t* actor, mobj_t* player)
{
    int		lo, hi, aside, i;
    fixed_t	dist = P_AproxDistance (player->x - actor->x, player->y - actor->y), x, y;
    angle_t	toward = R_PointToAngle2 (actor->x, actor->y, player->x, player->y), an;
    boolean	moved = false;

    P_TacRange (actor, &lo, &hi, &aside);
    // In sight: it knows where he is (P_BrainChase's own in-sight bookkeeping,
    // which this takes the place of).
    if (actor->mind != MIND_HUNT)
	P_MindBecome (actor, MIND_HUNT, STEP_NONE, "any.in_sight", "He is in plain sight: after him");
    actor->knownx = player->x;
    actor->knowny = player->y;
    actor->knowntic = leveltime;
    // In a doorway too narrow for two: through it first, out on his side and
    // clear of the way. It may not fire from there (P_DerpMayFire), and
    // everybody behind it is waiting on it. Where it means to stand can wait.
    if (dist >= lo*FRACUNIT && P_PortalClear (actor, player->x, player->y, &x, &y))
    {
	actor->firex = actor->firey = 0;
	actor->firepostic = leveltime;
	P_DerpWalkTo (actor, x, y);
	return true;
    }
    // It stays and shoots from there. They gave up the armor's ledge every time
    // just to get within a shotgun's best range. (Counting down to its next
    // shot all the same, as a chase step does: past the pace's reach that
    // count is all that lets it fire.)
    if (Holds (actor, player, dist, hi))
    {
	if (actor->movecount > 0)
	    actor->movecount--;
	actor->movedir = 8;
	if (P_RoleOf (actor) == ROLE_LANE)
	    P_DerpBark (actor, BARK_LANE);
	return true;
    }
    if (dist > hi*FRACUNIT)
    {
	// Beyond its range: to its fire position, looked for again every
	// couple of seconds as he moves; none, vanilla's chase.
	// (The tic's routes spent: the old one, or the chase, and it looks again
	// at its next step.)
	if (!actor->firepostic || leveltime - actor->firepostic > 2*TICRATE)
	{
	    fixed_t	qx = 0, qy = 0;
	    int		found = FirePosition (actor, player, lo, hi, &qx, &qy);
	    if (found != NAV_DEFERRED)
	    {
		actor->firepostic = leveltime;
		actor->firex = found ? qx : 0;
		actor->firey = found ? qy : 0;
	    }
	}
	if (!actor->firex && !actor->firey)
	    return false;
	if (P_AproxDistance (actor->x - actor->firex, actor->y - actor->firey) > 24*FRACUNIT)
	    P_DerpWalkTo (actor, actor->firex, actor->firey);
	else
	    actor->movedir = 8;
	return true;
    }
    // Still counting down to its next shot, as a chase step does; while a
    // teammate is on a bound, covering fire: the shot as soon as it may,
    // and it stays put.
    {
	boolean	covering;
	P_TacTeam (actor, &covering);
	if (covering && dist >= lo*FRACUNIT)
	{
	    actor->movecount = 0;
	    actor->movedir = 8;
	    return true;
	}
    }
    if (actor->movecount > 0)
	actor->movecount--;
    if (dist < lo*FRACUNIT)
    {
	// Too close: back, or back and to a side.
	P_DerpBark (actor, BARK_TOOCLOSE);
	static const angle_t offs[3] = { 0, ANG45, (angle_t)-ANG45 };
	for (i = 0; i < 3 && !moved; i++)
	{
	    an = toward + ANG180 + offs[i];
	    actor->movedir = ((an + ANG45/2)>>29) & 7;
	    moved = P_Move (actor);
	}
    }
    else if (P_DerpRoundsNear (actor, TICRATE) && P_Random () < aside)
    {
	// Shot at: a step aside, one way for a second, then the other.
	an = toward + (((actor->serial + leveltime/TICRATE) & 1) ? ANG90 : ANG270);
	for (i = 0; i < 2 && !moved; i++, an += ANG180)
	{
	    actor->movedir = ((an + ANG45/2)>>29) & 7;
	    moved = P_Move (actor);
	}
    }
    if (!moved)
	actor->movedir = 8;	// it holds its ground
    return true;
}

boolean P_DerpHolding (mobj_t* actor)
{
    mobj_t*	player = P_DerpPlayer ();
    int		lo, hi, aside;
    fixed_t	dist;

    if (!P_TacticsOn () || !player || actor->target != player || !P_DerpMonster (actor)
	|| !P_DerpSoldier (actor->type))
	return false;
    // That beat at its cover when he has come round it: no step anywhere.
    if (actor->mind == MIND_COVER && actor->tactic == STEP_COVER_SEEN)
	return true;
    if (actor->mind >= MIND_COVER)
	return false;
    P_TacRange (actor, &lo, &hi, &aside);
    dist = P_AproxDistance (player->x - actor->x, player->y - actor->y);
    return dist <= hi*FRACUNIT || Holds (actor, player, dist, hi);
}

void P_DerpFacing (mobj_t* actor)
{
    mobj_t*	player = P_DerpPlayer ();

    // Running (from him, to cover, falling back to a door) it looks where
    // it goes; stepping about in a fight it keeps its eyes and gun on him,
    // as he strafes.
    if (!P_TacticsOn () || !player || actor->target != player || !P_DerpMonster (actor)
	|| !P_DerpSoldier (actor->type) || actor->mind == MIND_RETREAT
	|| actor->mind == MIND_AMBUSH || actor->mind == MIND_REGROUP
	|| (actor->mind == MIND_COVER && (actor->tactic == STEP_COVER_GOING || actor->tactic == STEP_COVER_BACK)) || P_MindBounding (actor)
	|| P_MindPushing (actor) || !P_CheckSight (actor, player))
	return;
    actor->angle = R_PointToAngle2 (actor->x, actor->y, player->x, player->y);
}

// Laying fire: a friend on a bound and he out of sight, it fires on where it
// believes he is (the corner he left, the doorway he went through), and he
// keeps his head down while the friend gets across. For LAY_TIME at a time,
// a shot every so often; a chaingunner's burst runs on in its attack. It
// stops with the bound, with a stale belief, when it sees him, or with a
// friend in the lane.
#define LAY_TIME	TUNE (LAY_TIME, 2*TICRATE)
#define LAY_AGAIN	TUNE (LAY_AGAIN, 6*TICRATE)
#define LAY_BELIEF	TUNE (LAY_BELIEF, 3*TICRATE)
#define LAY_PACE	TUNE (LAY_PACE, 30)

boolean P_TacLayFire (mobj_t* actor, boolean visible)
{
    boolean	moving = false;

    if (visible || actor->mind != MIND_HUNT || P_MindBounding (actor) || !P_BulletsTravel ()
	|| !actor->knowntic || leveltime - actor->knowntic > LAY_BELIEF
	|| !P_TacTeam (actor, &moving) || !moving
	|| !P_CheckSight (actor, P_TacPoint (actor->knownx, actor->knowny)))
    {
	if (actor->layfire > leveltime)
	    actor->layfire = leveltime;
	return false;
    }
    if (actor->layfire <= leveltime)
    {
	if (leveltime - actor->layfire < LAY_AGAIN)
	    return false;
	actor->layfire = leveltime + LAY_TIME;
	P_DerpLog (actor, "lays fire on where he went: its friend is moving");
	P_DerpBark (actor, BARK_SUPPRESSING);
    }
    actor->movedir = 8;
    actor->angle = R_PointToAngle2 (actor->x, actor->y, actor->knownx, actor->knowny);
    if (leveltime - actor->firetic >= (actor->type == MT_CHAINGUY ? 0 : LAY_PACE)
	&& !P_TacLane (actor, actor->knownx, actor->knowny))
    {
	P_DerpFiring (actor);
	P_SetMobjState (actor, actor->info->missilestate);
    }
    return true;
}

#define BREACH_FAR	TUNE (BREACH_FAR, 680)
#define BREACH_WORD	TUNE (BREACH_WORD, 3*TICRATE)

boolean P_TacBreach (mobj_t* actor)
{
    int	since = actor->chokedtic > actor->tactictic ? actor->chokedtic : actor->tactictic;

    return !actor->guard && leveltime - since > BREACH_AFTER && P_DirectorGoIn ()
	&& actor->knowntic && leveltime - actor->knowntic < BREACH_WORD
	&& P_AproxDistance (actor->knownx - actor->x, actor->knowny - actor->y) <= BREACH_FAR*FRACUNIT
	&& P_DirectorBreach ();
}
