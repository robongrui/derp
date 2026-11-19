// DERP's guards and posts (p_tactics.h): monsters near a key, a switch or a
// hidden door hold it; a far fight readies them, and a readied soldier takes
// a post from the cover plan or stands ready where it is.

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

static boolean IsKey (mobj_t* mo);
static boolean Guarded (mobj_t* mo);
static int AroundPickup (mobj_t* item);
static void GuardPlace (int id, fixed_t x, fixed_t y, int route, int most, const char* what);
static void GuardKey (mobj_t* key, int route);
static boolean GuardedSwitch (int special);
static void GuardSwitches (void);
static boolean SecretGuard (const mobj_t* mo);
static void GuardSecrets (void);
static boolean AtTheDoor (mobj_t* actor, fixed_t x, fixed_t y);
static boolean EntranceCover (mobj_t* actor, mobj_t* player, fixed_t hx, fixed_t hy, fixed_t* fx, fixed_t* fy);
static boolean StandReady (mobj_t* actor, mobj_t* player, angle_t facing);


//
// Guarding keys. Monsters near a key at the level's start (a short walk
// from it, not through a wall) hold it: a far fight readies them, it does
// not empty the room, and led away they come back. Every third stays free
// to answer a fight (a key room is not a closet), and at most GUARD_MOST
// hold one key. Taking the key ends the duty, with an alarm.
//
#define GUARD_ROUTE	48	// cells: 768 units on foot
#define GUARD_MOST	TUNE (GUARD_MOST, 6)
#define GUARD_HEARS	TUNE (GUARD_HEARS, 768)	// a noise nearer the key than this is its business
#define GUARD_LEASH	TUNE (GUARD_LEASH, 1024)
#define POST_AGAIN	TUNE (POST_AGAIN, 15*TICRATE)	// a post, looked for at most this often

static boolean IsKey (mobj_t* mo)
{
    return mo->type >= MT_MISC4 && mo->type <= MT_MISC9;
}

// What soldiers hold a post by: keys, and armor. Going for it is a risk, and
// they do not all run out to him (a third stay free, GuardKey).
static boolean Guarded (mobj_t* mo)
{
    return IsKey (mo) || mo->type == MT_MISC0 || mo->type == MT_MISC1;
}

// Any other pickup with monsters placed right round it: the mapper put them
// there for it. Two at least, this near; one beside a clip is only standing there.
#define PICKUP_ROUTE	TUNE (PICKUP_ROUTE, 12)	// cells: 192 units on foot
#define PICKUP_FEW	TUNE (PICKUP_FEW, 2)

static int AroundPickup (mobj_t* item)
{
    thinker_t*	th;
    int		n = 0;

    for (th = thinkercap.next; th != &thinkercap; th = th->next)
    {
	mobj_t*	mo = (mobj_t *)th;
	if (th->function.acp1 == (actionf_p1)P_MobjThinker && P_DerpMonster (mo) && !mo->guard
	    && P_AproxDistance (mo->x - item->x, mo->y - item->y) <= PICKUP_ROUTE*NAVCELL*FRACUNIT)
	    n++;
    }
    return n;
}

// A hidden door's guards come from this far on foot (cells: 1536 units), as its far
// side may be a long way round.
#define SECRET_ROUTE	96
// How near the door its foot of the stairs must be, and how far apart they stand (cells).
#define SECRET_NEAR	TUNE (SECRET_NEAR, 448)
#define SECRET_APART	TUNE (SECRET_APART, 4)
// The hidden door being given guards (GuardSecrets): soldiers only, on the side it
// hides, none left free.
static line_t*	guarddoor;

// Those a short walk from (x, y), nearest first (in the order they were spawned, E1M2's
// switch went to a zombie 29 cells off, and the imp beside it, 5 off, was left over), up to
// most. Every third (by serial, the same one for every place) stays free to answer a fight,
// a key room is not a closet; but never the nearest, which the mapper put there for it.
// A hidden door's take soldiers only, in the order they come, each a little behind the last.
#define PLACE_TRIED	64

static void GuardPlace (int id, fixed_t x, fixed_t y, int route, int most, const char* what)
{
    static int	path[SECRET_ROUTE + 1];
    mobj_t*	near[PLACE_TRIED];
    int		cost[PLACE_TRIED];
    thinker_t*	th;
    int		n = 0, found = 0, i, j;

    for (th = thinkercap.next; th != &thinkercap && (!guarddoor || n < most); th = th->next)
    {
	mobj_t*	mo = (mobj_t *)th;
	int	nav, cells;
	if (th->function.acp1 != (actionf_p1)P_MobjThinker || !P_DerpMonster (mo) || mo->guard
	    || (guarddoor && !P_DerpSoldier (mo->type))
	    || mo->type == MT_CYBORG || mo->type == MT_SPIDER
	    || (nav = P_NavClassOf (mo)) < 0
	    || P_AproxDistance (mo->x - x, mo->y - y) > route*NAVCELL*FRACUNIT)
	    continue;
	navpartial = guarddoor != NULL;
	cells = P_NavPath (nav, mo->x, mo->y, x, y, path, route + 1, guarddoor ? 20000 : 3000);
	navpartial = false;
	if (cells <= 0 || cells > route)
	    continue;
	if (!guarddoor)
	{
	    if (found == PLACE_TRIED && cells >= cost[found - 1])
		continue;
	    j = found < PLACE_TRIED ? found++ : found - 1;
	    for (; j > 0 && cost[j - 1] > cells; j--)
	    {
		near[j] = near[j - 1];
		cost[j] = cost[j - 1];
	    }
	    near[j] = mo;
	    cost[j] = cells;
	    continue;
	}
	mo->guardx = x;
	mo->guardy = y;
	// As near as its feet take it (a mapper may keep monsters off the stairs up to
	// a hidden door), each a little behind the last.
	{
	    int	at = cells - 1 - SECRET_APART*n;
	    P_NavPoint (nav, path[at > 0 ? at : 0], &mo->guardx, &mo->guardy, NULL);
	    // (Out in front of the door is the other side of it: nothing to hold there.)
	    if (P_AproxDistance (mo->guardx - x, mo->guardy - y) > SECRET_NEAR*FRACUNIT
		|| !P_PointOnLineSide (mo->guardx, mo->guardy, guarddoor))
		continue;
	}
	mo->guard = id;
	n++;
	P_DerpLog (mo, "guards %s (%d cells off)", what, cells);
    }
    for (i = 0; i < found && n < most; i++)
    {
	if (i && near[i]->serial % 3 == 0)
	    continue;
	near[i]->guardx = x;
	near[i]->guardy = y;
	near[i]->guard = id;
	n++;
	P_DerpLog (near[i], "guards %s (%d cells off)", what, cost[i]);
    }
}

static void GuardKey (mobj_t* key, int route)
{
    GuardPlace (key->serial, key->x, key->y, route, GUARD_MOST, IsKey (key) ? "a key" : "a pickup");
}

// A switch that does something: a few near it stay by it. A lift's is only a lift.
// The room's button is worth holding, and what the mapper put round it was put there.
#define SWITCH_ROUTE	TUNE (SWITCH_ROUTE, 32)	// cells: 512 units on foot
#define SWITCH_MOST	TUNE (SWITCH_MOST, 3)
#define SWITCH_ID	0x40000000
#define SWITCH_OFF	TUNE (SWITCH_OFF, 24)

static boolean GuardedSwitch (int special)
{
    switch (special)
    {
      case 7: case 9: case 11: case 14: case 15: case 18: case 20: case 23: case 29:
      case 41: case 42: case 43: case 45: case 49: case 50: case 51: case 55: case 60:
      case 61: case 63: case 64: case 65: case 66: case 67: case 68: case 69: case 70:
      case 71: case 101: case 102: case 103: case 111: case 112: case 113: case 114:
      case 115: case 116: case 127: case 131: case 132: case 133: case 134: case 135:
      case 136: case 137: case 138: case 139: case 140:
	return true;
    }
    return false;
}

static void GuardSwitches (void)
{
    int	i;

    for (i = 0; i < numlines; i++)
    {
	line_t*	ld = &lines[i];
	fixed_t	l, x, y;
	if (!GuardedSwitch (ld->special))
	    continue;
	// Out in front of it, in the room it is pressed from.
	l = P_AproxDistance (ld->dx, ld->dy);
	if (l < FRACUNIT)
	    continue;
	x = ld->v1->x + ld->dx/2 + FixedMul (FixedDiv (ld->dy, l), SWITCH_OFF*FRACUNIT);
	y = ld->v1->y + ld->dy/2 - FixedMul (FixedDiv (ld->dx, l), SWITCH_OFF*FRACUNIT);
	GuardPlace (SWITCH_ID + i, x, y, SWITCH_ROUTE, SWITCH_MOST, "a switch");
    }
}

// A hidden door: those on the side it hides stay by it. Their fight is whoever comes
// through, not whatever they can hear going on outside. Its face looks out, so their
// place is through the door, on the far side.
#define SECRET_MOST	TUNE (SECRET_MOST, 3)
#define SECRET_ID	0x20000000
#define SECRET_BEHIND	TUNE (SECRET_BEHIND, 48)

static boolean SecretGuard (const mobj_t* mo)
{
    return mo->guard >= SECRET_ID && mo->guard < SWITCH_ID;
}

static void GuardSecrets (void)
{
    int	i, k;

    for (i = 0; i < numlines; i++)
    {
	line_t*	ld = &lines[i];
	fixed_t	l = P_AproxDistance (ld->dx, ld->dy), mx, my;
	if (!(ld->flags & ML_SECRET) || !ld->special || !ld->backsector || l < FRACUNIT)
	    continue;
	mx = ld->v1->x + ld->dx/2;
	my = ld->v1->y + ld->dy/2;
	// Back from the face, out of the door's own sector and a body's room further.
	for (k = 16; k <= 256; k += 16)
	{
	    fixed_t	x = mx - FixedMul (FixedDiv (ld->dy, l), k*FRACUNIT);
	    fixed_t	y = my + FixedMul (FixedDiv (ld->dx, l), k*FRACUNIT);
	    if (R_PointInSubsector (x, y)->sector == ld->backsector)
		continue;
	    x -= FixedMul (FixedDiv (ld->dy, l), SECRET_BEHIND*FRACUNIT);
	    y += FixedMul (FixedDiv (ld->dx, l), SECRET_BEHIND*FRACUNIT);
	    guarddoor = ld;
	    GuardPlace (SECRET_ID + i, x, y, SECRET_ROUTE, SECRET_MOST, "a hidden door");
	    guarddoor = NULL;
	    break;
	}
    }
}

// At level setup: who guards what. Monsters near a key hold it, then the rest
// near a pickup worth holding, then the switches and the hidden doors.
void P_GuardsLevel (void)
{
    thinker_t*	th;

    for (th = thinkercap.next; th != &thinkercap; th = th->next)
	if (th->function.acp1 == (actionf_p1)P_MobjThinker && Guarded ((mobj_t *)th))
	    GuardKey ((mobj_t *)th, GUARD_ROUTE);
    // Then the rest, from who is left.
    for (th = thinkercap.next; th != &thinkercap; th = th->next)
	if (th->function.acp1 == (actionf_p1)P_MobjThinker && (((mobj_t *)th)->flags & MF_SPECIAL)
	    && !Guarded ((mobj_t *)th) && AroundPickup ((mobj_t *)th) >= PICKUP_FEW)
	    GuardKey ((mobj_t *)th, PICKUP_ROUTE);
    GuardSwitches ();
    GuardSecrets ();
}

void P_DerpSwitchUsed (line_t* ld)
{
    int		id = SWITCH_ID + (int)(ld - lines), secret = SECRET_ID + (int)(ld - lines);
    thinker_t*	th;

    if (!P_TacticsOn ())
	return;
    for (th = thinkercap.next; th != &thinkercap; th = th->next)
    {
	mobj_t*	mo = (mobj_t *)th;
	if (th->function.acp1 == (actionf_p1)P_MobjThinker && (mo->guard == id || mo->guard == secret))
	{
	    mo->guard = 0;
	    P_DerpLog (mo, "what it guarded is gone: free");
	}
    }
}

boolean P_DerpOnDuty (const mobj_t* mo)
{
    return P_TacticsOn () && mo->guard;
}

boolean P_DerpGuarding (mobj_t* mo, fixed_t x, fixed_t y)
{
    return P_TacticsOn () && mo->guard
	&& (SecretGuard (mo) || P_AproxDistance (x - mo->guardx, y - mo->guardy) > GUARD_HEARS*FRACUNIT);
}


// Back to where the map put it.
void P_TacBackToPost (mobj_t* actor)
{
    P_MindBecome (actor, MIND_RETURN, STEP_NONE, NULL, NULL);
    P_TacStandUp (actor);
    actor->goalx = actor->spawnpoint.x<<FRACBITS;
    actor->goaly = actor->spawnpoint.y<<FRACBITS;
    actor->goaltic = leveltime;
    P_DerpLog (actor, "back to the key it guards");
}

void P_DerpLeash (mobj_t* actor, boolean visible)
{
    if (!P_TacticsOn () || !actor->guard || visible || !actor->spawnpoint.type
	|| (actor->mind != MIND_HUNT && actor->mind != MIND_SEARCH && actor->mind != MIND_INVESTIGATE)
	|| P_AproxDistance (actor->x - actor->guardx, actor->y - actor->guardy) <= GUARD_LEASH*FRACUNIT)
	return;
    P_DerpRule (actor, "hunt+search+investigate.leash", "A guard too far from what it guards, and him out "
		"of sight: back to its post");
    P_TacBackToPost (actor);
}

void P_DerpKeyTaken (mobj_t* key)
{
    thinker_t*	th;

    if (!P_TacticsOn ())
	return;
    for (th = thinkercap.next; th != &thinkercap; th = th->next)
    {
	mobj_t*	mo = (mobj_t *)th;
	if (th->function.acp1 == (actionf_p1)P_MobjThinker && mo->guard == key->serial && key->serial)
	{
	    mo->guard = 0;
	    P_DerpLog (mo, "what it guarded is gone: free");
	    if (IsKey (key))
		P_DerpBark (mo, BARK_KEY);
	    else if (Guarded (key))
		P_DerpBark (mo, BARK_TOOKITEM);
	}
    }
    // The alarm: as loud as a door, there. (Not for a clip.)
    if (Guarded (key))
	P_DerpNoise (key, NOISE_DOOR, key->x, key->y);
}

// Whether someone standing at (x, y) blocks a way too narrow to pass them: across
// it, in any of four directions, less floor than this for a body's middle. (One
// took its post on E1M1's zigzag walkway, 105 wide, and three stood behind it for
// fifteen seconds.)
#define NARROW_WAY	TUNE (NARROW_WAY, 88)
#define NARROW_LOOK	6

boolean P_TacBlocksWay (mobj_t* actor, fixed_t x, fixed_t y)
{
    static const int	axis[4][2] = { {1, 0}, {0, 1}, {1, 1}, {1, -1} };
    int			nav = P_NavClassOf (actor), a, side, k;

    if (nav < 0)
	return false;
    for (a = 0; a < 4; a++)
    {
	int	span = 0, step = axis[a][0] && axis[a][1] ? NAVCELL*7/5 : NAVCELL;
	for (side = -1; side <= 1; side += 2)
	    for (k = 1; k <= NARROW_LOOK; k++)
	    {
		int	cell = P_NavCellAt (nav, x + side*k*axis[a][0]*NAVCELL*FRACUNIT, y + side*k*axis[a][1]*NAVCELL*FRACUNIT);
		if (cell < 0 || !P_NavStandable (nav, cell) || P_NavHurts (cell))
		    break;
		span += step;
	    }
	if (span < NARROW_WAY)
	    return true;
    }
    return false;
}

// Whether a post at (x, y) answers a noise at (nx, ny): a short walk for it, and a way
// from the noise that is not much longer than the line. Two soldiers walked 600 units
// to stand at a doorway of the outer perimeter, the fight the far side of a wall.
#define POST_WALK	TUNE (POST_WALK, 384)
#define POST_CELLS	96

boolean P_TacPostServes (mobj_t* actor, fixed_t x, fixed_t y, fixed_t nx, fixed_t ny)
{
    static int	path[POST_CELLS + 1];
    int		nav = P_NavClassOf (actor), cell, n;

    if (nav < 0 || P_AproxDistance (x - actor->x, y - actor->y) > POST_WALK*FRACUNIT)
	return false;
    // (A rough guess at the noise may fall in a wall: then only the walk counts.)
    cell = P_NavCellAt (nav, nx, ny);
    if (cell < 0 || !P_NavStandable (nav, cell))
	return true;
    n = P_NavPath (nav, nx, ny, x, y, path, POST_CELLS + 1, 8000);
    return n > 0 && n <= POST_CELLS
	&& n*NAVCELL <= (int)(P_AproxDistance (x - nx, y - ny)>>FRACBITS)*2 + 256;
}

// A fellow soldier already waiting here: this way in is taken care of.
#define JOIN_NEAR	TUNE (JOIN_NEAR, 128)

// Getting nowhere, this long, with a friend this near: it has been stepping round
// that friend and back. (A zombie swung to and fro across a walkway for seven seconds,
// another for eight, each behind one standing at its post.)
#define HELD_CHECK	TUNE (HELD_CHECK, 2*TICRATE)
#define HELD_MOVED	TUNE (HELD_MOVED, 48)
#define HELD_NEAR	TUNE (HELD_NEAR, 96)

boolean P_TacHeldUp (mobj_t* actor)
{
    thinker_t*	th;
    boolean	stuck;

    if (leveltime - actor->stalltic < HELD_CHECK)
	return false;
    stuck = actor->stalltic && P_AproxDistance (actor->x - actor->stallx, actor->y - actor->stally) < HELD_MOVED*FRACUNIT;
    actor->stalltic = leveltime;
    actor->stallx = actor->x;
    actor->stally = actor->y;
    if (!stuck)
	return false;
    for (th = thinkercap.next; th != &thinkercap; th = th->next)
    {
	mobj_t*	mo = (mobj_t *)th;
	// (One in wait, not one on the move: a queue at a door moves on.)
	if (th->function.acp1 == (actionf_p1)P_MobjThinker && mo != actor && P_DerpMonster (mo) && P_DerpSoldier (mo->type)
	    && mo->mind == MIND_AMBUSH && mo->tactic == STEP_WAITING
	    && P_AproxDistance (mo->x - actor->x, mo->y - actor->y) < HELD_NEAR*FRACUNIT)
	    return true;
    }
    return false;
}

mobj_t* P_TacWaiter (mobj_t* actor)
{
    thinker_t*	th;

    for (th = thinkercap.next; th != &thinkercap; th = th->next)
    {
	mobj_t*	mo = (mobj_t *)th;
	if (th->function.acp1 == (actionf_p1)P_MobjThinker && mo != actor && P_DerpMonster (mo) && P_DerpSoldier (mo->type)
	    && mo->mind == MIND_AMBUSH && mo->tactic == STEP_WAITING
	    && P_AproxDistance (mo->x - actor->x, mo->y - actor->y) < JOIN_NEAR*FRACUNIT)
	    return mo;
    }
    return NULL;
}

// A guard or a reserve readied by a far fight, at its own look (not in the
// middle of the noise: P_DerpNoise can come from another thing's move): a
// soldier takes a useful place, beside its room's doorway nearest the
// noise (a guard's near its key), else hidden from that way, and waits
// there facing it (an ambush). A demon stays loose.
#define POST_BEAT	TUNE (POST_BEAT, 20)
#define PLAN_DOORS	4
#define PLAN_REACH	TUNE (PLAN_REACH, 640)
// The sides of the door are worth a longer walk.
#define FLANK_REACH	TUNE (FLANK_REACH, 1024)

// A cover for the way someone at (hx, hy) comes in: the best there is on any of
// them, in its gun's range before out of it, and a walk there he does not see,
// since this is the fight not started yet. A room with two ways in nearly as
// short splits, by number. (*fx, *fy): the mouth it watches.
static boolean	flanking;	// the cover it found is a side of the door

// Someone else waiting at that mouth already: this is the other side.
static boolean AtTheDoor (mobj_t* actor, fixed_t x, fixed_t y)
{
    thinker_t*	th;

    for (th = thinkercap.next; th != &thinkercap; th = th->next)
    {
	mobj_t*	mo = (mobj_t *)th;
	if (th->function.acp1 == (actionf_p1)P_MobjThinker && mo != actor && P_DerpMonster (mo) && P_DerpSoldier (mo->type)
	    && mo->mind == MIND_AMBUSH && P_AproxDistance (mo->coverx - x, mo->covery - y) <= 240*FRACUNIT)
	    return true;
    }
    return false;
}

static boolean EntranceCover (mobj_t* actor, mobj_t* player, fixed_t hx, fixed_t hy, fixed_t* fx, fixed_t* fy)
{
    int		doors[PLAN_DOORS], n, i, j, far, rank, lo, hi, aside;

    n = P_CoverPlanDoors (P_NavRoom ((int)(actor->subsector->sector - sectors)), hx, hy, doors, PLAN_DOORS);
    P_TacRange (actor, &lo, &hi, &aside);
    for (rank = PLAN_FLANK; rank <= PLAN_ENOUGH; rank++)
	for (far = 0; far < 2; far++)
	    // (Beside the way he comes in by, not one nearly as short.)
	    for (i = 0; i < (rank == PLAN_FLANK && n ? 1 : n); i++)
	    {
		const planspot_t*	list;
		int			door = doors[rank == PLAN_FLANK ? 0 : (i + actor->serial)%n];
		int			count = P_CoverPlanSpots (door, &list);
		P_CoverPlanMouth (door, fx, fy);
		for (j = 0; j < count; j++)
		{
		    const planspot_t*	s = &list[j];
		    if (s->rank != rank || (P_AproxDistance (s->kx - *fx, s->ky - *fy) > hi*FRACUNIT) != far
			|| P_AproxDistance (s->x - actor->x, s->y - actor->y)
			   > (rank == PLAN_FLANK ? FLANK_REACH : PLAN_REACH)*FRACUNIT
			|| P_TacCoverTaken (actor, s->x, s->y) || !P_TacSafeWalk (actor, player, s->x, s->y))
			continue;
		    actor->coverx = s->x;
		    actor->covery = s->y;
		    actor->peekx = s->kx;
		    actor->peeky = s->ky;
		    actor->lowcover = s->rank == PLAN_LOW;
		    flanking = s->rank == PLAN_FLANK;
		    return true;
		}
	    }
    return false;
}

// No post worth walking to: it does not go back to sleep with a fight on. It stays
// where it stands, facing that way, first shot ready. (Three by the computer room
// were refused their posts and were standing idle when he walked in.)
static boolean StandReady (mobj_t* actor, mobj_t* player, angle_t facing)
{
    if (P_TacBlocksWay (actor, actor->x, actor->y))
	return false;
    actor->target = player;
    P_MindBecome (actor, MIND_AMBUSH, STEP_NONE, "none.stands_ready",
    		"No post worth walking to: it stays where it is, facing that way, ready");
    actor->coverx = actor->x;
    actor->covery = actor->y;
    actor->facing = facing;
    actor->heldready = true;
    P_DerpBark (actor, BARK_STANDSREADY);
    P_DerpLog (actor, "stands ready where it is");
    P_SetMobjState (actor, actor->info->seestate);
    return true;
}

boolean P_DerpGuardPost (mobj_t* actor)
{
    mobj_t*	player = P_DerpPlayer ();
    angle_t	facing;
    fixed_t	kx, ky;
    boolean	low;

    if (!P_TacticsOn () || !player || !P_DerpSoldier (actor->type) || actor->mind != MIND_NONE
	|| actor->tactic != STEP_READIED)
	return false;
    // What was that? A moment, its own, before it does anything about it. This is
    // working something out, so it may take a while (he in plain sight never does).
    if (leveltime - actor->goaltic < POST_BEAT + actor->serial % POST_BEAT)
	return false;
    // Finding a post is a cover search, many routes: with the tic's routes spent
    // it is still taking it in, and its look goes on as before.
    if (P_NavBudgetSpent (actor))
	return false;
    P_MindStep (actor, STEP_NONE, "none/readied.beat_over",
    		"A moment to take it in, then it looks for a post");
    if (leveltime - actor->goaltic > 2*TICRATE
	|| (actor->tactictic && leveltime - actor->tactictic < POST_AGAIN))
	return false;
    actor->tactictic = leveltime;
    // A hidden door's guard goes and stands by it, whatever the noise was.
    if (SecretGuard (actor))
    {
	line_t*	door = &lines[actor->guard - SECRET_ID];
	actor->target = player;
	P_MindBecome (actor, MIND_AMBUSH, STEP_NONE, "none.hidden_door",
			"A fight off somewhere: it goes and stands by the hidden door it guards");
	actor->coverx = actor->guardx;
	actor->covery = actor->guardy;
	actor->facing = R_PointToAngle2 (actor->guardx, actor->guardy, door->v1->x + door->dx/2, door->v1->y + door->dy/2);
	P_DerpLog (actor, "stands by the hidden door");
	P_DerpBark (actor, BARK_HIDDENDOOR);
	P_SetMobjState (actor, actor->info->seestate);
	return true;
    }
    // The way he will come in, if it has a plan for that and a guard stays near
    // its key. Then it waits there and fights from there.
    if (EntranceCover (actor, player, actor->goalx, actor->goaly, &kx, &ky)
	&& !(actor->guard && P_AproxDistance (actor->coverx - actor->guardx, actor->covery - actor->guardy)
			     > GUARD_HEARS*FRACUNIT))
    {
	low = actor->lowcover;
	actor->target = player;
	P_MindBecome (actor, MIND_AMBUSH, STEP_NONE, "none.post_entrance",
			"A cover by the way he will come in: it waits there, and "
			"fights from there");
	actor->lowcover = low;
	actor->fightsfrom = true;
	actor->facing = R_PointToAngle2 (actor->coverx, actor->covery, kx, ky);
	P_DerpLog (actor, "takes cover by the way he will come in");
	if (flanking)
	    P_DerpBark (actor, AtTheDoor (actor, kx, ky) ? BARK_FLANKOTHER : BARK_FLANKDOOR);
	else
	    P_DerpBark (actor, actor->guard ? BARK_GUARDSITEM : BARK_RESERVE);
	P_SetMobjState (actor, actor->info->seestate);
	return true;
    }
    actor->lowcover = false;
    if (!P_TacFindAmbush (actor, actor->goalx, actor->goaly, &actor->coverx, &actor->covery, &facing)
	|| (actor->guard && P_AproxDistance (actor->coverx - actor->guardx, actor->covery - actor->guardy)
			    > GUARD_HEARS*FRACUNIT))
    {
	if (!P_TacFindCoverNear (actor, actor->x, actor->y, P_TacPoint (actor->goalx, actor->goaly), false, false,
			    &actor->coverx, &actor->covery, &kx, &ky))
	    return StandReady (actor, player, R_PointToAngle2 (actor->x, actor->y, actor->goalx, actor->goaly));
	facing = R_PointToAngle2 (actor->coverx, actor->covery, actor->goalx, actor->goaly);
    }
    if (P_TacBlocksWay (actor, actor->coverx, actor->covery)
	|| !P_TacPostServes (actor, actor->coverx, actor->covery, actor->goalx, actor->goaly))
	return StandReady (actor, player, R_PointToAngle2 (actor->x, actor->y, actor->goalx, actor->goaly));
    actor->target = player;
    P_MindBecome (actor, MIND_AMBUSH, STEP_NONE, "none.post_reserve",
    		"A place beside its doorway, or hidden from that way: it "
    		"waits there");
    actor->facing = facing;
    P_DerpBark (actor, actor->guard ? BARK_GUARDSITEM : BARK_RESERVE);
    P_DerpLog (actor, actor->guard ? "takes a post by its key: a fight off that way"
	       : "takes a post in reserve: a fight off that way");
    P_SetMobjState (actor, actor->info->seestate);
    return true;
}
