// DERP's fire lanes and bodies in them (p_tactics.h): a friend kneels for a
// shot or steps aside, a soldier leans out of cover, sprints under fire, and
// is drawn turning after its angle.

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
#include "p_swept.h"
#include "r_main.h"
#include "r_state.h"
#include "s_sound.h"
#include "tables.h"
#include "p_tune.h"
#include "p_mind.h"
#include "p_tactics_local.h"

boolean P_Move (mobj_t* actor);	// p_enemy.c

#define POINT_BLANK	TUNE (POINT_BLANK, 192)
#define KNEEL_TIME	TUNE (KNEEL_TIME, 2*TICRATE)

static boolean LeanShows (mobj_t* mo, mobj_t* player);
static void LeanEase (mobj_t* mo);
static boolean ClearsKnee (mobj_t* actor, mobj_t* mo, fixed_t d);
static void TurnEase (mobj_t* mo);

void P_TacKneel (mobj_t* mo)
{
    mo->kneeltic = leveltime + KNEEL_TIME;
    // Under the gun: at five eighths, a friend's rounds still found it.
    mo->height = mo->info->height/2;
}

// Up off its knee now, if there is room over it.
void P_TacStandUp (mobj_t* mo)
{
    if (mo->kneeltic && mo->ceilingz - mo->z >= mo->info->height)
    {
	mo->height = mo->info->height;
	mo->kneeltic = 0;
    }
}


//
// Deciding.
//
void P_DerpCoverLeft (mobj_t* actor, boolean gone)
{
    // Done with its low wall: not left on one knee to wait the time out.
    if (gone && actor->lowcover)
    {
	actor->lowcover = false;
	P_TacStandUp (actor);
    }
    actor->fightsfrom = false;
    actor->heldready = false;
}


//
// Making room for a friend's shot.
//
fixed_t P_DerpAim (mobj_t* actor, angle_t angle)
{
    mobj_t*	t = actor->target;
    fixed_t	dist;

    if (!P_TacticsOn () || !t || !P_DerpSoldier (actor->type))
	return P_AimLineAttack (actor, angle, MISSILERANGE);
    // At its target's chest, not at whatever stands first in the line: the
    // shot leaves 36 units up and rises (on level ground), so a friend
    // kneeling in between (35 units) is shot over, one standing is shot.
    dist = P_AproxDistance (t->x - actor->x, t->y - actor->y);
    if (dist < FRACUNIT)
	dist = FRACUNIT;
    return FixedDiv (t->z + t->height*3/4 - (actor->z + (actor->height>>1) + 8*FRACUNIT), dist);
}

// A lean is drawn only while it makes sense to him: he is on the side it leans
// to, with the corner still between him and the cover. Run past and it straightens
// up, whatever it is still doing. The state stays, this is only how it looks.
#define LEAN_FULL	TUNE (LEAN_FULL, 256)
#define LEAN_STEP	TUNE (LEAN_STEP, 32)

static boolean LeanShows (mobj_t* mo, mobj_t* player)
{
    double	ox = (mo->peekx - mo->coverx)/65536.0, oy = (mo->peeky - mo->covery)/65536.0;
    double	tx = (player->x - mo->coverx)/65536.0, ty = (player->y - mo->covery)/65536.0;

    return ox*tx + oy*ty > 0
	&& !P_DerpExposedAt (mo->coverx, mo->covery, R_PointInSubsector (mo->coverx, mo->covery)->sector->floorheight, player);
}

static void LeanEase (mobj_t* mo)
{
    mobj_t*	player = P_DerpPlayer ();
    boolean	wants = player && mo->mind == MIND_COVER && mo->tactic == STEP_COVER_LEANING
	&& P_DerpSoldier (mo->type) && LeanShows (mo, player);

    if (wants)
	mo->leanshow = mo->leanshow + LEAN_STEP > LEAN_FULL ? LEAN_FULL : mo->leanshow + LEAN_STEP;
    else
	mo->leanshow = mo->leanshow > LEAN_STEP ? mo->leanshow - LEAN_STEP : 0;
}

boolean P_DerpLean (mobj_t* mo, fixed_t* dx, fixed_t* dy)
{
    fixed_t	full, out;

    if (!P_TacticsOn () || mo->health <= 0 || mo->mind != MIND_COVER || !mo->leanshow
	|| !P_DerpSoldier (mo->type))
	return false;
    full = P_AproxDistance (mo->peekx - mo->coverx, mo->peeky - mo->covery);
    if (full < FRACUNIT)
	return false;
    out = P_AproxDistance (mo->x - mo->coverx, mo->y - mo->covery);
    if (out > full)
	out = full;
    // Towards the open side, as far as it is out (a unit way at the lean).
    out = FixedDiv (out, full)/LEAN_FULL*mo->leanshow;
    *dx = FixedMul (FixedDiv (mo->peekx - mo->coverx, full), out);
    *dy = FixedMul (FixedDiv (mo->peeky - mo->covery, full), out);
    return true;
}


// How tall it looks. Kneeling, the game keeps half a body (a friend's shot
// clears it), but the picture shows more: the soldiers' kneeling frames
// (kneel_view.cpp) stand 39-40 of their 55, and a squashed frame (any other
// art: body_motion.cpp, the rows below the hip 45% up at 0.4) the same, so
// the head he sees is 27% down from standing, not half. His rounds strike
// the picture; theirs, the half body.
#define KNEEL_SHOWN	(FRACUNIT*73/100)

fixed_t P_DerpShown (mobj_t* mo)
{
    if (P_Swept (mo))
	return P_SweptShown (mo);
    if (!mo->kneeltic || mo->health <= 0 || !mo->info || mo->height >= mo->info->height)
	return mo->height;
    return FixedMul (mo->info->height, KNEEL_SHOWN);
}

// What stands first in actor's line of fire at its target, if it is one
// to spare: a fellow soldier (not one down on a knee: shot over), or a
// demon after the same target and near (an imp crossing the doorway the
// soldiers cover: not their friend, but no target either while both are
// after him; a stray hit still starts the fight vanilla's would).
mobj_t* P_TacInTheWay (mobj_t* actor)
{
    mobj_t*	blocker = P_TacLane (actor, actor->target->x, actor->target->y);

    return blocker || !actor->target->player ? blocker : P_TacSpread (actor);
}

// The nearest friend, short of him or not far past, where its shot may go: most of its scatter
// either side of the line, all of it for one close by (two in three friends hit
// were within 128 of the gun), and a shell's spread on top (shotgun guys did
// most of the harm). Lane sees only the middle of the line. A round in flight
// still finds whoever steps into it. Far off, it is his luck.
#define SPREAD_SHARE	0.6
#define SPREAD_SHELL	5.6
#define SPREAD_NEAR	TUNE (SPREAD_NEAR, 192)
#define SPREAD_FAR	TUNE (SPREAD_FAR, 768)
// And past him: what misses him goes on. (The bot's runs: the soldiers on the step by
// E1M1's computer room shot the ones beside the door, with him between them, 112 hits
// in 12 runs.)
#define SPREAD_BEYOND	TUNE (SPREAD_BEYOND, 384)

// A friend on one knee, d off: whether the lowest of its shot still goes over. It
// was all friendly fire there was: every soldier hit by a soldier was kneeling,
// told to get down, and shot through at 45 to 90 units. A shot leaves 8 units over
// a kneeling head, and the aim's scatter takes that away within a few metres.
static boolean ClearsKnee (mobj_t* actor, mobj_t* mo, fixed_t d)
{
    mobj_t*	him = actor->target;
    double	unit = 1.0/FRACUNIT, along, from, to, drop, deg;
    fixed_t	far;

    if (!him)
	return true;
    far = P_AproxDistance (him->x - actor->x, him->y - actor->y);
    along = far > 0 ? (double)d/far : 1;
    from = (actor->z + (actor->height>>1) + 8*FRACUNIT)*unit;
    to = (him->z + him->height*3/4)*unit;
    deg = P_Difficulty ()->aim*(actor->movedir < 8 ? 2 : 1)*2/9 + (actor->type == MT_SHOTGUY ? 2.8 : 0);
    drop = d*unit*tan (deg*3.14159265358979/180);
    return from + (to - from)*along - drop > (mo->z + mo->height)*unit + 2;
}

mobj_t* P_TacSpread (mobj_t* actor)
{
    mobj_t*	him = actor->target;
    mobj_t*	nearest = NULL;
    fixed_t	far = P_AproxDistance (him->x - actor->x, him->y - actor->y), best;
    angle_t	to = R_PointToAngle2 (actor->x, actor->y, him->x, him->y);
    double	aim = P_Difficulty ()->aim*(actor->movedir < 8 ? 2 : 1);
    double	shell = actor->type == MT_SHOTGUY ? SPREAD_SHELL : 0;
    thinker_t*	th;

    if (far > SPREAD_FAR*FRACUNIT)
	far = SPREAD_FAR*FRACUNIT;
    best = far + SPREAD_BEYOND*FRACUNIT;
    for (th = thinkercap.next; th != &thinkercap; th = th->next)
    {
	mobj_t*	mo = (mobj_t *)th;
	fixed_t	d;
	double	off, body;
	if (th->function.acp1 != (actionf_p1)P_MobjThinker || mo == actor || mo == him || !P_DerpMonster (mo))
	    continue;
	d = P_AproxDistance (mo->x - actor->x, mo->y - actor->y);
	if (d >= best || !d || (P_DerpKneeling (mo) && ClearsKnee (actor, mo, d)))
	    continue;
	off = fabs ((double)(int)(R_PointToAngle2 (actor->x, actor->y, mo->x, mo->y) - to))*360/4294967296.0;
	body = atan2 ((double)mo->radius, (double)d)*180/3.14159265358979;
	if (off < aim*(d < SPREAD_NEAR*FRACUNIT ? 1 : SPREAD_SHARE) + shell + body && P_CheckSight (actor, mo))
	{
	    nearest = mo;
	    best = d;
	}
    }
    return nearest;
}

// Who of its side stands in the line to (x, y).
mobj_t* P_TacLane (mobj_t* actor, fixed_t x, fixed_t y)
{
    mobj_t*	blocker;

    P_AimLineAttack (actor, R_PointToAngle2 (actor->x, actor->y, x, y), MISSILERANGE);
    blocker = linetarget;
    if (!blocker || blocker == actor->target || !P_DerpMonster (blocker))
	return NULL;
    if (P_DerpSoldier (blocker->type))
	return P_DerpKneeling (blocker)
	    && ClearsKnee (actor, blocker, P_AproxDistance (blocker->x - actor->x, blocker->y - actor->y)) ? NULL : blocker;
    if (blocker->target == actor->target && blocker->health > 0
	&& P_AproxDistance (blocker->x - actor->x, blocker->y - actor->y) < 512*FRACUNIT)
	return blocker;
    return NULL;
}

// He comes into view: even someone expecting him takes a moment to take that in. So
// long, and a little more each by its number, so they do not all fire as one. Out of
// sight this long first, or it is the same sighting.
#define SIGHT_BEAT	TUNE (SIGHT_BEAT, 6)
#define SIGHT_SPREAD	TUNE (SIGHT_SPREAD, 4)
#define SIGHT_LOST	TUNE (SIGHT_LOST, TICRATE)

void P_DerpSighted (mobj_t* actor)
{
    // (Leaning out from its cover it knows what it came out for.)
    if (leveltime - actor->sighttic > SIGHT_LOST && actor->mind != MIND_COVER)
	actor->beattic = leveltime + SIGHT_BEAT + actor->serial%SIGHT_SPREAD;
    actor->sighttic = leveltime;
}

boolean P_DerpMayFire (mobj_t* actor)
{
    // Its last rush: nothing but claws.
    if (actor->rushtic)
	return false;
    if (P_MindRunningRound (actor) || P_MindLurks (actor))
	return false;
    if (!P_TacticsOn () || !P_DerpMonster (actor))
	return true;
    // Right on top of it: whatever it was doing, that comes first.
    if (actor->target
	&& P_AproxDistance (actor->target->x - actor->x, actor->target->y - actor->y) < POINT_BLANK*FRACUNIT)
	return true;
    if (P_DerpSoldier (actor->type) && actor->target && actor->target->player)
    {
	if (P_CheckSight (actor, actor->target))
	    P_DerpSighted (actor);
	if (leveltime < actor->beattic)
	    return false;
    }
    // Running away for show: a shot back gives the game away.
    if (actor->mind == MIND_RETREAT && actor->tactic == STEP_BAIT)
	return false;
    // Down behind its low wall: in sight all the way back, and a shot on the
    // walk carried it past the spot, to and fro for ever.
    if (actor->lowcover && actor->mind == MIND_COVER && actor->tactic == STEP_COVER_BACK)
	return false;
    // Pushing in: firing drops it to a walk and it would never arrive, and a
    // shell is wasted out here. In range, it fires.
    if (P_MindPushing (actor) && actor->target)
    {
	int	lo, hi, aside;
	P_TacRange (actor, &lo, &hi, &aside);
	if (P_AproxDistance (actor->target->x - actor->x, actor->target->y - actor->y) > hi*FRACUNIT)
	    return false;
    }
    // Else it fires, whatever it is doing: it goes on walking (p_gunwalk.h).
    // Stopping in a doorway stops everybody behind it, walking through it does
    // not. Still there a second on, with nowhere to go on to: it may as well
    // shoot. (Timed from here: movedtic is Walk's, and old on anything that
    // came in on a chase step.)
    if (!P_PortalInFrame (actor) || (P_DerpSoldier (actor->type) && actor->movedir < 8))
    {
	actor->framedtic = 0;
	return true;
    }
    if (!actor->framedtic)
	actor->framedtic = leveltime;
    return leveltime - actor->framedtic > TICRATE;
}

boolean P_DerpLaneBlocked (mobj_t* actor)
{
    return P_TacticsOn () && P_DerpMonster (actor) && P_DerpSoldier (actor->type) && actor->target && P_TacInTheWay (actor);
}

#define ASIDE_STEPS	TUNE (ASIDE_STEPS, 4)
#define ASIDE_HOLD	TUNE (ASIDE_HOLD, TICRATE + TICRATE/2)

boolean P_DerpAllyInTheWay (mobj_t* actor)
{
    mobj_t*	blocker;
    angle_t	an;

    if (!P_TacticsOn () || !P_DerpMonster (actor) || !P_DerpSoldier (actor->type) || !actor->target)
	return false;
    an = R_PointToAngle2 (actor->x, actor->y, actor->target->x, actor->target->y);
    blocker = P_TacInTheWay (actor);
    if (!blocker)
	return false;
    // A demon in the lane: it holds its fire a second, then a second to find
    // a line of its own, and so on until the lane clears. It does not stand
    // for ever behind one that has stopped in a doorway.
    if (!P_DerpSoldier (blocker->type))
    {
	if ((leveltime/TICRATE + actor->serial) & 1)
	{
	    an += (actor->serial & 2) ? ANG90 : ANG270;
	    actor->movedir = ((an + ANG45/2)>>29) & 7;
	    if (P_DerpWalkStep (actor))
		return true;
	}
	actor->movedir = 8;
	P_DerpLog (actor, "holds its fire: a demon in its line");
	return true;
    }
    if (P_AproxDistance (blocker->x - actor->x, blocker->y - actor->y) < 512*FRACUNIT
	&& !(blocker->flags & MF_JUSTATTACKED) && !P_DerpKneeling (blocker))
    {
	// Down, friend.
	P_TacKneel (blocker);
	P_DerpBark (actor, BARK_FRIENDINLINE);
	return true;
    }
    // Too far to call down: a few steps to one side, all the same way, for a
    // line of its own. Then it holds and aims, and the next time tries the other
    // side. A step either way at random every few tics was a dance on the spot,
    // never getting clear and never firing. No step to take, it waits: it does
    // not shoot a friend.
    if (leveltime < actor->asidetic)
    {
	actor->movedir = 8;
	return true;
    }
    an += ((actor->serial + actor->asideway) & 1) ? ANG90 : ANG270;
    actor->movedir = ((an + ANG45/2)>>29) & 7;
    if (!actor->asidesteps)
    {
	P_DerpLog (actor, "steps aside for a friend in its line of fire");
	P_DerpBark (actor, BARK_FRIENDINLINE);
    }
    if (!P_DerpWalkStep (actor) || ++actor->asidesteps >= ASIDE_STEPS)
    {
	actor->asidesteps = 0;
	actor->asideway++;
	actor->asidetic = leveltime + ASIDE_HOLD;
    }
    return true;
}

// Under fire a man runs: some 5.5 m/s against a walk's 2.7, two steps a
// chase step. Seen only: unseen, the director's offstage steps hurry it.

// It does not go from a walk to a run at once, nor stop dead. Its gait climbs a
// notch a chase step while it runs and falls one when it stops wanting to, and
// its second step is that share of a step: about half a second to full speed,
// and it carries on a little past the moment it stops. Doom's steps are jumps,
// and it was the jump from one speed to double that made cardboard of them.
#define GAIT_FULL	TUNE (GAIT_FULL, 6)

boolean P_DerpSprint (mobj_t* actor)
{
    mobj_t*	player = P_DerpPlayer ();
    boolean	walking, want;
    boolean	moved;

    if (!P_TacticsOn () || !player || !P_DerpMonster (actor) || !P_DerpSoldier (actor->type))
	return false;
    if (P_DerpKneeling (actor) || actor->z > actor->floorz)
    {
	actor->gait = 0;
	return false;
    }
    // To cover (not out to peek: that is careful), away to hide, or shot at.
    walking = (actor->mind == MIND_COVER && (actor->tactic == STEP_COVER_GOING || actor->tactic == STEP_COVER_BACK))
	|| actor->mind == MIND_RETREAT
	|| P_MindBounding (actor) || P_MindPushing (actor) || P_MindRounding (actor);
    want = actor->movedir < 8 && (walking || P_DerpRoundsNear (actor, 50));
    // Its heading is good only up to its waypoint: not past it.
    if (want && walking
	&& P_AproxDistance (actor->wayx - actor->x, actor->wayy - actor->y) < 2*P_DerpStep (actor))
	want = false;
    if (want && !P_MindPushing (actor) && !P_MindRounding (actor) && !P_CheckSight (player, actor))
	want = false;
    if (want)
    {
	if (actor->gait < GAIT_FULL)
	    actor->gait++;
    }
    else if (actor->gait > 0)
	actor->gait = actor->gait > 2 ? actor->gait - 2 : 0;	// slowing is quicker than picking up
    if (!actor->gait || actor->movedir >= 8)
    {
	// Stopped short of a wall or a friend: all of it goes at once.
	if (actor->movedir >= 8)
	    actor->gait = 0;
	return false;
    }
    P_DerpStepShare (256*actor->gait/GAIT_FULL);
    moved = P_Move (actor);
    P_DerpStepShare (256);
    if (!moved)
    {
	actor->gait = 0;
	return false;
    }
    if (want)
	P_DerpLogSprint (actor);
    return true;
}

// An alert that nothing renews fades: a level a minute.
#define ALERT_FADES	TUNE (ALERT_FADES, 60*TICRATE)

// What is drawn turns after what it faces, a share of an eighth of a turn a tic:
// a turn about shows the frames between, not one frame and then its opposite.
// Only the drawing: its aim is its angle, at once.
#define TURN_STEP	(ANG45/3)

static void TurnEase (mobj_t* mo)
{
    int	left;

    if (!mo->faceset)
    {
	mo->faceset = true;
	mo->faceangle = mo->angle;
	return;
    }
    left = (int)(mo->angle - mo->faceangle);
    if (left > (int)TURN_STEP)
	mo->faceangle += TURN_STEP;
    else if (left < -(int)TURN_STEP)
	mo->faceangle -= TURN_STEP;
    else
	mo->faceangle = mo->angle;
}

angle_t P_DerpDrawAngle (const mobj_t* mo)
{
    angle_t	facing = mo->faceset && P_DerpAI () ? mo->faceangle : mo->angle;

    // Kicked off its feet: it rolls over in the air (p_swept.c).
    return P_Swept (mo) ? P_SweptFacing (mo, facing) : facing;
}

void P_DerpBodyThink (mobj_t* mo)
{
    if (P_TacticsOn () && (mo->flags & MF_COUNTKILL) && mo->health > 0)
    {
	TurnEase (mo);
	LeanEase (mo);
    }
    P_GunWalkThink (mo);
    if (mo->alert && leveltime - mo->alerttic > ALERT_FADES)
    {
	mo->alert--;
	mo->alerttic = leveltime;
    }
    if (!mo->kneeltic || leveltime < mo->kneeltic)
	return;
    // Up again, if there is room over it.
    if (mo->health > 0 && mo->ceilingz - mo->z >= mo->info->height)
    {
	mo->height = mo->info->height;
	mo->kneeltic = 0;
    }
    else if (mo->health <= 0)
	mo->kneeltic = 0;
}

void P_DerpFiring (mobj_t* actor)
{
    actor->firetic = leveltime;
    P_GunWalkStart (actor);
    if (actor->type == MT_CHAINGUY)
	P_DerpBark (actor, BARK_FIRES);
    P_DerpCry (actor, BARK_IMPTHROW);
}
