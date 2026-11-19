//
// DERP's squads and tactics (p_tactics.h).
//

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

#define JOIN_AHEAD	TUNE (JOIN_AHEAD, 256)
// Hurt and hidden: this long at a spot before it looks for a better one further off,
// and how much further that has to be.
#define HIDE_REST	TUNE (HIDE_REST, 2*TICRATE)
#define HIDE_ON		TUNE (HIDE_ON, 96)
#define BAIT_TIME	TUNE (BAIT_TIME, 8*TICRATE)
#define COVER_TIME	TUNE (COVER_TIME, 12*TICRATE)
#define FLANK_BEAT	TUNE (FLANK_BEAT, 14)
#define WATCH_TIME	TUNE (WATCH_TIME, 3*TICRATE)
#define RETREAT_TIME	TUNE (RETREAT_TIME, 10*TICRATE)
#define AMBUSH_TIME	TUNE (AMBUSH_TIME, 30*TICRATE)
#define CHOKE_WAIT	TUNE (CHOKE_WAIT, 15*TICRATE)
#define CHOKE_BELIEF	TUNE (CHOKE_BELIEF, 10*TICRATE)
#define CHOKE_AGAIN	TUNE (CHOKE_AGAIN, 25*TICRATE)
#define DECIDE_EVERY	TUNE (TACTICS_DECIDE_EVERY, 24)	// about three times a second would be dithering; every 2 s, it looked slow
#define BOUND_AGAIN	TUNE (BOUND_AGAIN, 6*TICRATE)

static boolean SeesItThrough (mobj_t* actor, decide_t* c);
static boolean BadlyHurt (mobj_t* actor, decide_t* c);
static boolean Barks (mobj_t* actor, decide_t* c);
static boolean ByTheCover (mobj_t* actor, decide_t* c);
static boolean BaitHim (mobj_t* actor, decide_t* c);
static boolean MayMove (mobj_t* actor, decide_t* c);
static boolean PushIn (mobj_t* actor, decide_t* c);
static boolean BoundRound (mobj_t* actor, decide_t* c);
static boolean UnderFire (mobj_t* actor, decide_t* c);
static boolean AmbushOrRegroup (mobj_t* actor, decide_t* c);
static boolean HeldUpWaiting (mobj_t* actor, decide_t* c);
static boolean HeldUpOnTheWay (mobj_t* actor, decide_t* c);
static boolean JoinsTrap (mobj_t* actor, decide_t* c);
static boolean SetsTrap (mobj_t* actor, decide_t* c);
static boolean Choke (mobj_t* actor, decide_t* c);
static void Decide (mobj_t* actor, boolean visible);
static void Sprung (mobj_t* actor);
static void Duck (mobj_t* actor, int wait);
static void Face (mobj_t* actor, angle_t angle);
static boolean CoverStep (mobj_t* actor, mobj_t* player, boolean visible);
static boolean RetreatStep (mobj_t* actor, mobj_t* player, boolean visible);
static boolean AmbushStep (mobj_t* actor, mobj_t* player, boolean visible);
static boolean RegroupStep (mobj_t* actor, mobj_t* player, boolean visible);
static boolean HuntStep (mobj_t* actor, mobj_t* player, boolean visible);


boolean P_TacticsOn (void)
{
    return derpparts.senses && derpparts.tactics && P_DerpAI ();
}

void P_TacticsLevel (void)
{
    P_MindLeaving (P_DerpCoverLeft);
    P_SquadLevel ();
    P_FireteamLevel ();
    P_PortalsLevel ();
    P_BarksLevel ();
    P_ChatterLevel ();
    if (!P_TacticsOn ())
	return;
    P_CornersLevel ();
    P_CoverPlanLevel ();
    P_FireLevel ();
    P_SquadsKnown ();
    P_GuardsLevel ();
}

//
// Deciding (p_decide.h): the soldiers' rules, in the order they are asked
// (DECIDE_EVERY). The first that acts has the decision and the rest are not
// asked until next time. Each rule is a function of its own, and the table at
// the end is the only place the order lives: a new rule gets a line there,
// where it belongs in the order, and nowhere else.
//

// decide_t, the DUTY_ flags and the loop over the table: p_decide.h.

// On a bound or a push it sees its move through: only a bad wound (below)
// stops it.
static boolean SeesItThrough (mobj_t* actor, decide_t* c)
{
    return (P_MindBounding (actor) || P_MindPushing (actor) || P_MindRounding (actor)) && c->health >= 40;
}

// A retreat only for wounds taken since the last one: it hid, came back,
// and does not hide again for the same wounds.
static boolean BadlyHurt (mobj_t* actor, decide_t* c)
{
    if (!(c->health < 40 && c->fear > 10 && actor->mind != MIND_RETREAT
	  && (!actor->retreathealth || actor->health < actor->retreathealth)))
	return false;
    // Away from him if it can, else just out of his sight: from where it
    // thinks he is.
    if (P_TacFindCover (actor, P_TacBelieved (actor, c->player, c->visible), false, true, &actor->coverx, &actor->covery,
		   &actor->peekx, &actor->peeky)
	|| P_TacFindCover (actor, P_TacBelieved (actor, c->player, c->visible), false, false, &actor->coverx, &actor->covery,
		      &actor->peekx, &actor->peeky))
    {
	P_MindBecome (actor, MIND_RETREAT, STEP_NONE, "any.badly_hurt",
		      "Under 40% health, afraid, with new wounds since it last "
		      "ran: to a hidden spot, away from him if it can");
	P_DerpBark (actor, BARK_FALLBACK);
	actor->retreathealth = actor->health;
	return true;
    }
    return false;
}

// What it says about the fight: no decision.
static boolean Barks (mobj_t* actor, decide_t* c)
{
    mobj_t*	player = c->player;

    if (c->visible && P_DerpPinned ())
	P_DerpBark (actor, BARK_PINNED);
    // Making off, at speed, away from it.
    if (c->visible && P_AproxDistance (player->momx, player->momy) > 6*FRACUNIT
	&& (double)(player->x - actor->x)*player->momx + (double)(player->y - actor->y)*player->momy > 0)
	P_DerpBark (actor, BARK_RUNNING);
    // Out of his sight behind his gun, he has all day.
    if (!c->visible && actor->type == MT_CHAINGUY && actor->mind == MIND_HUNT)
	P_DerpBark (actor, BARK_WAITING);
    return false;
}

// One of them has already gone down walking at him: the rest come by the
// cover, not across the open. Too far for its gun, a corner on the way. And
// with no way to him at all, any cover: out of his line, leaning out to shoot.
// They walked into his sights one by one, for him to pick off.
static boolean ByTheCover (mobj_t* actor, decide_t* c)
{
    mobj_t*	player = c->player;
    int		lo, hi, aside;
    fixed_t	here;
    boolean	beaten;

    if (!(c->visible && actor->mind == MIND_HUNT && !P_MindBounding (actor) && !P_MindPushing (actor) && !P_MindRounding (actor)
	  && leveltime - actor->tactictic > 3*TICRATE))
	return false;
    here = P_AproxDistance (player->x - actor->x, player->y - actor->y);
    beaten = P_TacNoWay (actor, player);
    P_TacRange (actor, &lo, &hi, &aside);
    if ((beaten || (actor->fallen && here > hi*FRACUNIT))
	&& P_TacFindCover (actor, player, true, false, &actor->coverx, &actor->covery, &actor->peekx, &actor->peeky)
	&& (beaten || P_AproxDistance (player->x - actor->coverx, player->y - actor->covery) < here + 64*FRACUNIT))
    {
	if (beaten)
	    P_MindBecome (actor, MIND_COVER, STEP_COVER_GOING, "hunt.no_way",
			  "In sight with no way to him on foot: it gets out of his line, leaning out to shoot");
	else
	    P_MindBecome (actor, MIND_COVER, STEP_COVER_GOING, "hunt.friend_fell",
			  "Out of range, and a friend went down in the open: it comes on by the cover");
	P_DerpLog (actor, beaten ? "no way to him: gets out of his line"
		   : "comes on by the cover: a friend went down in the open");
	if (beaten)
	    P_DerpBark (actor, BARK_COVER);
	return true;
    }
    return false;
}

// A lull, a friend in wait behind it: a feigned retreat (Bait, below).
static boolean BaitHim (mobj_t* actor, decide_t* c)
{
    return c->visible && actor->mind == MIND_HUNT && c->health >= 50 && P_TacBait (actor, c->player);
}

// Whether it may set off on a bound or a push now: with word of him, in a
// hunt, fit, not just back from one, and not holding the way out.
static boolean MayMove (mobj_t* actor, decide_t* c)
{
    return (c->visible || (actor->knowntic && leveltime - actor->knowntic < 3*TICRATE))
	&& actor->mind == MIND_HUNT && c->health >= 50
	&& (!actor->boundtic || leveltime - actor->boundtic > BOUND_AGAIN) && !(c->duty & DUTY_HOLDS);
}

// Its chance: in, now, while he has his hands full.
static boolean PushIn (mobj_t* actor, decide_t* c)
{
    mobj_t*	player = c->player;
    int		lo, hi, aside;
    boolean	moving;
    const char*	why;

    if (!MayMove (actor, c))
	return false;
    P_TacRange (actor, &lo, &hi, &aside);
    P_TacTeam (actor, &moving);
    // (Not while a friend is on a bound: then it covers.)
    if (!moving && P_AproxDistance (player->x - actor->x, player->y - actor->y) > PUSH_HOME (hi)*FRACUNIT
	&& (why = P_RoleChance (actor, player, c->visible)) != NULL)
    {
	P_MindBecome (actor, MIND_HUNT, STEP_PUSHING, "hunt.push",
		      "He is pinned, hurting, looking away or has not seen it, and "
		      "nobody on its team is moving: it runs at him [assault]");
	actor->boundtic = leveltime;
	if (!P_DerpBark (actor, !strcmp (why, "he is hurting") ? BARK_PUSHHURT
			 : !strcmp (why, "he looks away") ? BARK_PUSHAWAY : BARK_PUSH)
	    && actor->info->activesound)
	    S_StartSound (actor, actor->info->activesound);
	P_DerpLog (actor, "pushes in: %s", why);
	return true;
    }
    return false;
}

// In a fire team with nobody moving: now and then it is the one to move,
// to a new fire position round him, while the others cover it.
static boolean BoundRound (mobj_t* actor, decide_t* c)
{
    mobj_t*	player = c->player;
    boolean	moving, wayout;
    int		lo, hi, aside;

    if (!MayMove (actor, c) || P_RoleOf (actor) == ROLE_LANE)
	return false;
    // He stays put under their fire: this is the time to go round him.
    if (!(P_TacTeam (actor, &moving) && !moving && P_Random () < (P_DerpPinned () ? 72 : 36)))
	return false;
    // A way out: it runs to the doorway behind him while the others keep
    // him busy, so leaving is a fight too. One bound in four, and every
    // move a rifleman makes (it has no other). A guard keeps to its key.
    wayout = c->visible && !(c->duty & DUTY_GUARD)
	&& (P_RoleOf (actor) == ROLE_RIFLE || !(P_Random () & 3))
	&& P_TacWayOut (actor, player, &actor->firex, &actor->firey);
    P_TacRange (actor, &lo, &hi, &aside);
    // (Round him is the assault man's. The rifleman covers it, or takes the door.)
    if (wayout || (P_RoleOf (actor) == ROLE_ASSAULT
		   && P_TacBoundPosition (actor, P_TacBelieved (actor, player, c->visible), lo, hi, &actor->firex, &actor->firey)))
    {
	if (wayout)
	    P_MindBecome (actor, MIND_HUNT, STEP_BOUNDING, "hunt.way_out",
			  "Its team holds him: it runs to the doorway behind him, so leaving is a fight too "
			  "[rifle assault]");
	else
	    P_MindBecome (actor, MIND_HUNT, STEP_BOUNDING, "hunt.bound",
			  "Its team holds him: it moves to a new firing spot round him while they cover it "
			  "[assault]");
	actor->boundtic = leveltime;
	actor->holdtic = wayout ? -1 : 0;
	// A call as it goes: the team's move is heard, so the player can
	// tell one moving from the others covering it. (Its grunt, with
	// no recording or somebody else speaking.)
	if (!P_DerpBark (actor, wayout ? BARK_WATCHDOOR : BARK_BOUNDING) && actor->info->activesound)
	    S_StartSound (actor, actor->info->activesound);
	P_DerpLog (actor, wayout ? "runs to cover the way out behind him"
		   : "bounds to a new position, its team covering");
	return true;
    }
    return false;
}

// The rifleman takes cover early, the assault man only when it has to.
static boolean UnderFire (mobj_t* actor, decide_t* c)
{
    mobj_t*	player = c->player;
    boolean	moving;

    if (!(P_DerpUnderFire (actor, c->visible)
	  && c->fear > (P_RoleOf (actor) == ROLE_RIFLE ? -60 : P_RoleOf (actor) == ROLE_ASSAULT ? 0 : -30)
	  && actor->mind != MIND_COVER && actor->mind != MIND_RETREAT
	  && (actor->mind != MIND_HUNT || leveltime - actor->tactictic > 3*TICRATE)))
	return false;
    // Covering a friend's move it holds where it is, unless the rounds are
    // going by its own head.
    if (P_TacTeam (actor, &moving) && moving && !P_DerpSuppression (actor))
	return true;
    // The cover it had, if it still serves and is near; else a new one.
    if ((P_TacCoverServes (actor, player)
	 && P_AproxDistance (actor->coverx - actor->x, actor->covery - actor->y) < 256*FRACUNIT)
	|| P_TacFindCover (actor, player, true, false, &actor->coverx, &actor->covery, &actor->peekx, &actor->peeky))
    {
	P_MindBecome (actor, MIND_COVER, STEP_COVER_GOING, "any.under_fire",
		      "Under fire and afraid enough for its role, with a cover "
		      "to go to: the one it had if that still serves");
	P_DerpBark (actor, BARK_COVER);
	return true;
    }
    // Nowhere to get to and rounds going by: down on one knee where it
    // is, a smaller target, still firing. The rules below are still asked.
    if (P_DerpSuppression (actor) && !P_DerpKneeling (actor))
    {
	P_TacKneel (actor);
	P_DerpLog (actor, "kneels under fire: no cover in reach");
	P_DerpBarkSoon (actor, BARK_TAKINGFIRE);
    }
    return false;
}

// An ambush (or a regroup): two or more fellow soldiers heard dying
// since its last, the fight gone quiet (he has not fired for a couple of
// seconds: he won), and he out of its sight. It waits for him beside the
// doorway nearest the danger rather than walk into him; a brave one goes
// to look instead. Held once, it searches out as any other.
static boolean AmbushOrRegroup (mobj_t* actor, decide_t* c)
{
    angle_t	facing;

    if (!(!c->visible && actor->fallen >= 2 && actor->fallen > actor->ambushed
	  && c->fear + actor->fallen*8 > 0 && leveltime - P_DerpLastShot () > 2*TICRATE
	  && (actor->mind == MIND_INVESTIGATE || actor->mind == MIND_SEARCH || actor->mind == MIND_HUNT)))
	return false;
    if (!c->friends && c->fear > 30)
    {
	int	squad;
	if (P_TacFindSquad (actor, &actor->coverx, &actor->covery, &squad))
	{
	    P_MindBecome (actor, MIND_REGROUP, STEP_NONE, "investigate+search+hunt.regroup",
			  "Alone, friends dead, the fight "
			  "gone quiet and him out of sight: off to the nearest other squad");
	    P_DerpBark (actor, BARK_REGROUP);
	    actor->joinsquad = squad;
	    return true;
	}
    }
    actor->ambushed = actor->fallen;
    if (P_TacFindAmbush (actor, actor->knownx, actor->knowny, &actor->coverx, &actor->covery, &facing)
	&& !P_TacBlocksWay (actor, actor->coverx, actor->covery))
    {
	P_MindBecome (actor, MIND_AMBUSH, STEP_NONE, "investigate+search+hunt.ambush",
		      "Two or more friends dead, the fight "
		      "gone quiet and him out of sight: it waits by the doorway nearest the danger");
	actor->facing = facing;
	return true;
    }
    return false;
}

// On its way somewhere and held up behind a friend: here will do.
static boolean HeldUpWaiting (mobj_t* actor, decide_t* c)
{
    if (!(!c->visible && actor->mind == MIND_AMBUSH && actor->tactic != STEP_WAITING && P_TacHeldUp (actor)))
	return false;
    P_MindStep (actor, STEP_WAITING, "ambush.held_up", "Held up behind a friend on its way there: it waits here instead");
    actor->coverx = actor->x;
    actor->covery = actor->y;
    actor->tactictic = leveltime;
    P_DerpLog (actor, "held up: waits here");
    return true;
}

static boolean HeldUpOnTheWay (mobj_t* actor, decide_t* c)
{
    angle_t	facing;

    if (!(!c->visible && (actor->mind == MIND_INVESTIGATE || actor->mind == MIND_SEARCH) && P_TacHeldUp (actor)))
	return false;
    facing = R_PointToAngle2 (actor->x, actor->y, actor->goalx, actor->goaly);
    P_MindBecome (actor, MIND_AMBUSH, STEP_NONE, "investigate+search.held_up",
		  "Held up behind a friend: it stays there, facing that way");
    actor->coverx = actor->x;
    actor->covery = actor->y;
    actor->facing = facing;
    actor->heldready = true;
    P_DerpLog (actor, "held up: waits here");
    return true;
}

// A friend lying in wait right here: it does not stand behind it pushing. It
// takes a place of its own covering the same way in, and they wait together.
static boolean JoinsTrap (mobj_t* actor, decide_t* c)
{
    mobj_t*	waiter;
    angle_t	an;
    fixed_t	kx, ky;

    if (!(!c->visible && (actor->mind == MIND_INVESTIGATE || actor->mind == MIND_SEARCH)))
	return false;
    waiter = P_TacWaiter (actor);
    if (!waiter)
	return false;
    an = waiter->facing>>ANGLETOFINESHIFT;
    if (P_TacFindCoverNear (actor, actor->x, actor->y, P_TacPoint (waiter->x + JOIN_AHEAD*finecosine[an],
							 waiter->y + JOIN_AHEAD*finesine[an]),
		       false, false, &actor->coverx, &actor->covery, &kx, &ky)
	&& !P_TacBlocksWay (actor, actor->coverx, actor->covery))
    {
	P_MindBecome (actor, MIND_AMBUSH, STEP_NONE, "investigate+search.joins_trap",
		      "A friend is lying in wait right here: "
		      "it takes its own place covering the same way in");
	actor->facing = waiter->facing;
	P_DerpLog (actor, "joins #%d's trap", waiter->serial);
	return true;
    }
    return false;
}

// A trap, while the fight next door goes on: on its way to look at it
// (or looking round where it heard it),
// a fellow soldier heard dying there, or two friends with it (enough to
// split), every other one (by its number) waits beside its own room's
// doorway nearest the noise instead; the others go and join in. Once
// for friends alone (ambushed -1), again for each death. Before, all
// went to look, and the ambush above wanted the fight over first.
static boolean SetsTrap (mobj_t* actor, decide_t* c)
{
    angle_t	facing;
    fixed_t	kx, ky;

    if (!(!c->visible && (actor->mind == MIND_INVESTIGATE || actor->mind == MIND_SEARCH) && (actor->serial & 1)
	  && ((actor->fallen >= 1 && actor->fallen > actor->ambushed)
	      || (c->friends >= 2 && !actor->fallen && !actor->ambushed))))
	return false;
    actor->ambushed = actor->fallen ? actor->fallen : -1;
    // Beside a doorway; or, the noise's room one with its own (a wide
    // opening, no doorway: E1M1's zigzag bridge into the computer room),
    // hidden near where it is from where the noise came, facing it.
    if ((P_TacFindAmbush (actor, actor->goalx, actor->goaly, &actor->coverx, &actor->covery, &facing)
	 || (P_TacFindCoverNear (actor, actor->x, actor->y, P_TacPoint (actor->goalx, actor->goaly), false, false,
			    &actor->coverx, &actor->covery, &kx, &ky)
	     && (facing = R_PointToAngle2 (actor->coverx, actor->covery, actor->goalx, actor->goaly), true)))
	&& !P_TacBlocksWay (actor, actor->coverx, actor->covery)
	&& P_TacPostServes (actor, actor->coverx, actor->covery, actor->goalx, actor->goaly))
    {
	P_MindBecome (actor, MIND_AMBUSH, STEP_NONE, "investigate+search.trap",
		      "The fight next door goes on: every other one "
		      "waits beside its own doorway instead of going to look");
	actor->facing = facing;
	P_DerpLog (actor, "sets a trap: the fight next door is going on");
	P_DerpBark (actor, BARK_TRAP);
	return true;
    }
    return false;
}

// A choke: he is just the other side of a narrow opening and it is going
// badly there. It does not walk into that one by one. It gets out of the
// line of it, somewhere hidden from the far side that the way out comes
// past, and waits for him to come through; he has to, or the demons get
// round to him. Not for ever: after CHOKE_WAIT it goes on.
// (Seeing him down the length of it changes nothing, out of range: they came on
// in plain sight and he shot them one by one.)
static boolean Choke (mobj_t* actor, decide_t* c)
{
    fixed_t	ox, oy;

    if (!((!c->visible || P_TacOutOfRange (actor, c->player))
	  && (actor->mind == MIND_HUNT || actor->mind == MIND_INVESTIGATE || actor->mind == MIND_SEARCH)
	  && !(c->duty & (DUTY_GUARD | DUTY_GOIN)) && !P_MindBounding (actor) && !P_MindPushing (actor) && !P_MindRounding (actor)
	  && (!actor->chokedtic || leveltime - actor->chokedtic > CHOKE_AGAIN)))
	return false;
    // (By word of him: a noise behind a door is no reason to stay out.)
    if (!(actor->knowntic && leveltime - actor->knowntic < CHOKE_BELIEF
	  && P_PortalChoke (actor, actor->knownx, actor->knowny, &ox, &oy, &actor->coverx, &actor->covery)))
	return false;
    actor->chokedtic = leveltime;
    // The assault man does not wait: another way in, if there is one.
    if (P_RoleOf (actor) == ROLE_ASSAULT && P_TacOtherWay (actor, ox, oy))
    {
	P_MindBecome (actor, MIND_HUNT, STEP_ROUNDING, "hunt+investigate+search.go_round",
		      "He holds a narrow doorway: it "
		      "takes another way in, if there is one [assault]");
	actor->avoidx = ox;
	actor->avoidy = oy;
	P_DerpLog (actor, "goes round: that doorway is held");
	P_DerpBark (actor, BARK_GOROUND);
	return true;
    }
    P_MindBecome (actor, MIND_AMBUSH, STEP_NONE, "hunt+investigate+search.choke",
		  "He holds a narrow doorway: it will not "
		  "walk into that one by one, so it gets out of the line and waits for him");
    actor->tactictic = leveltime - (AMBUSH_TIME - CHOKE_WAIT);
    actor->facing = R_PointToAngle2 (actor->coverx, actor->covery, ox, oy);
    P_DerpLog (actor, "will not walk into that doorway: covers the way out");
    P_DerpBark (actor, BARK_DOOR);
    return true;
}

// The order. A rule's name here is for switching it off (tune.txt): the rule it
// fires, or a short name for a pair of them, a gate, or the barks.
static const decision_t decisions[] =
{
    { "gate.sees_it_through",		SeesItThrough },
    { "any.badly_hurt",			BadlyHurt },
    { "barks",				Barks },
    { "hunt.no_way+hunt.friend_fell",	ByTheCover },
    { "hunt.bait",			BaitHim },
    { "hunt.push",			PushIn },
    { "hunt.bound+hunt.way_out",	BoundRound },
    { "any.under_fire",			UnderFire },
    { "regroup+ambush",			AmbushOrRegroup },
    { "ambush.held_up",			HeldUpWaiting },
    { "investigate+search.held_up",	HeldUpOnTheWay },
    { "investigate+search.joins_trap",	JoinsTrap },
    { "investigate+search.trap",	SetsTrap },
    { "go_round+choke",			Choke },
    { NULL, NULL }
};

static void Decide (mobj_t* actor, boolean visible)
{
    decide_t	c;

    if (!P_DecideBegin (actor, visible, &c))
	return;
    P_TacKnow (actor);
    c.friends = P_TacFriends (actor);
    // What it fears: losses heard, wounds, being alone, against its nerve.
    c.fear = actor->losses*12 + (100 - c.health)/2 - actor->nerve/2 + (c.friends ? 0 : 15)
	+ P_Random ()%10;
    P_Decide (actor, decisions, &c);
}

// Its ambush went off: it takes the cover it was waiting in, up and at the lean.
#define AMBUSH_HEARS	TUNE (AMBUSH_HEARS, 2*TICRATE)

static void Sprung (mobj_t* actor)
{
    actor->sighttic = 0;	// A moment before it opens up, like anyone seeing him.
    P_DerpSighted (actor);
    P_MindBecome (actor, MIND_COVER, STEP_COVER_LEANING, NULL, NULL);
    P_TacStandUp (actor);
    actor->waytic = leveltime;
    actor->movedtic = leveltime;
    actor->waydist = MAXINT;
}

// Into its cover, out of sight until wait; behind a low wall that means down.
static void Duck (mobj_t* actor, int wait)
{
    P_MindStep (actor, STEP_COVER_DUCKED, NULL, NULL);
    actor->waytic = wait;
    if (actor->lowcover && !P_DerpKneeling (actor))
	P_TacKneel (actor);
}

static void Face (mobj_t* actor, angle_t angle)
{
    actor->angle = angle;
    actor->movedir = 8;	// no direction: it stands
}

// In cover: there, ducked, leaning out, back, watching where he went.
static boolean CoverStep (mobj_t* actor, mobj_t* player, boolean visible)
{
    // Up close: back to the fight. Long enough there: it stays while the
    // cover serves (the time starts again), else back to the fight.
    if (visible && P_AproxDistance (player->x - actor->x, player->y - actor->y) < 160*FRACUNIT)
    {
        P_MindBecome (actor, MIND_HUNT, STEP_NONE, "cover.too_close",
        		"He is close and it sees him: back to the fight");
        return false;
    }
    // Never got there: the place goes back for others, not held for ever
    // by one stuck on its way.
    if (actor->tactic == STEP_COVER_GOING && leveltime - actor->tactictic > COVER_TIME)
    {
        P_DerpLog (actor, "gives up on its cover: it never got there");
        P_MindBecome (actor, MIND_HUNT, STEP_NONE, "cover/going.never_got_there",
        		"Too long and it never reached its cover: "
        		"back to the fight");
        return false;
    }
    // No word of him: not straight out after him. It watches where he went.
    if (actor->tactic != STEP_COVER_GOING && actor->tactic != STEP_COVER_WATCHING && !visible
        && (!actor->knowntic || leveltime - actor->knowntic > COVER_BELIEF))
    {
        P_MindStep (actor, STEP_COVER_WATCHING, "cover.no_word",
        		"No word of him for a while: it watches where he went");
        actor->waytic = leveltime;
        P_DerpLog (actor, "watches where he went");
    }
    if (actor->tactic != STEP_COVER_WATCHING && leveltime - actor->tactictic > COVER_TIME)
    {
        // The cover no use any more: another only if it can get there unseen,
        // and otherwise back to the fight and its rules.
        if (!P_TacCoverServes (actor, player))
        {
    	fixed_t	ox = actor->coverx, oy = actor->covery, okx = actor->peekx, oky = actor->peeky;
    	boolean	low = actor->lowcover;
    	if (!visible
    	    && P_TacFindCover (actor, player, true, false, &actor->coverx, &actor->covery, &actor->peekx, &actor->peeky)
    	    && P_AproxDistance (actor->coverx - ox, actor->covery - oy) >= 48*FRACUNIT
    	    && P_TacSafeWalk (actor, player, actor->coverx, actor->covery))
    	{
    	    P_DerpLog (actor, "moves up to better cover, unseen");
    	    P_TacStandUp (actor);
    	    P_MindStep (actor, STEP_COVER_GOING, "cover.better_cover",
    	    		"Long enough there, the cover no use, and a "
    	    		"better one it can reach unseen: it moves");
    	    actor->tactictic = leveltime;
    	    return true;
    	}
    	actor->coverx = ox;
    	actor->covery = oy;
    	actor->peekx = okx;
    	actor->peeky = oky;
    	actor->lowcover = low;
    	P_MindBecome (actor, MIND_HUNT, STEP_NONE, "cover.no_use",
    			"Long enough there, the cover no use, and none better "
    			"to reach unseen: back to the fight");
    	return false;
        }
        actor->tactictic = leveltime;
        P_DerpRule (actor, "cover.still_serves", "Long enough there, but the cover still serves: its "
    		"time starts again");
        P_DerpLog (actor, "holds its cover: it still serves");
    }
    switch (actor->tactic)
    {
      case STEP_COVER_GOING:
      case STEP_COVER_BACK:
        if (P_TacInCover (actor))
        {
    	P_DerpRule (actor, "cover/going+cover/back.in_cover", "It has reached its cover: it ducks");
    	Duck (actor, leveltime + 35 + (P_Random () & 31));
    	return true;
        }
        P_DerpWalkTo (actor, actor->coverx, actor->covery);
        return true;
      case STEP_COVER_WATCHING:
        actor->movedir = 8;
        actor->angle = R_PointToAngle2 (actor->x, actor->y, actor->knownx, actor->knowny);
        if (!actor->lowcover && P_PHASE_OF (actor) && P_DerpBodies (actor))
    	return false;
        // Word of him again, or there he is: on with the cover.
        if (visible || (actor->knowntic && leveltime - actor->knowntic <= COVER_BELIEF))
        {
    	P_DerpRule (actor, "cover/watching.word_again", "Word of him again, or there he is: on "
    		    "with the cover");
    	Duck (actor, leveltime);
    	return true;
        }
        if (leveltime - actor->waytic > WATCH_TIME)
        {
    	P_MindBecome (actor, MIND_HUNT, STEP_NONE, "cover/watching.nothing",
    			"Watched long enough and nothing came: back "
    			"to the fight");
    	return false;
        }
        return true;
      case STEP_COVER_SEEN:
        actor->movedir = 8;
        if (!visible)
        {
    	P_DerpRule (actor, "cover/seen.out_of_sight", "He is out of sight again: it ducks back");
    	Duck (actor, leveltime + TICRATE);
    	return true;
        }
        actor->angle = R_PointToAngle2 (actor->x, actor->y, player->x, player->y);
        if (leveltime < actor->waytic)
    	return true;
        // He has come round it. No running for another cover under his gun:
        // the cover is done, and the fight goes by its own rules, push and all.
        P_DerpLog (actor, "flanked: back to the fight");
        P_DerpBark (actor, BARK_FLANKED);
        P_MindBecome (actor, MIND_HUNT, STEP_NONE, "cover/seen.flanked",
        		"He came round its cover: after a beat, back to the "
        		"fight");
        return false;
      case STEP_COVER_DUCKED:
        actor->movedir = 8;
        if (visible)
        {
    	P_TacStandUp (actor);
    	P_MindStep (actor, STEP_COVER_SEEN, "cover/ducked.seen",
    			"He can see it where it hides: a beat to take "
    			"that in, shooting if it can");
    	actor->waytic = leveltime + FLANK_BEAT + P_Random ()%(FLANK_BEAT/2);
    	return true;
        }
        if (leveltime >= actor->waytic)
        {
    	P_TacStandUp (actor);
    	P_MindStep (actor, STEP_COVER_LEANING, "cover/ducked.lean_out",
    			"Its wait is over: it leans out to find him");
    	actor->waytic = leveltime;
    	// A fresh walk: a second ducked is no second stuck. It stood
    	// there (Walk's stall) and never leaned, by the dice of its wait.
    	actor->movedtic = leveltime;
    	actor->waydist = MAXINT;
        }
        return true;
      default:	// leaning out to where it sees him, a shot, back in
        if (visible)
        {
    	actor->movedir = 8;
    	// Out to shoot: the shot at once (it came out for it), not when
    	// vanilla's dice say. A chaingunner's burst runs on in its attack
    	// while he is in sight.
    	if (actor->firetic < actor->waytic)
    	{
    	    // Someone of its side in the line: out, ready, not yet.
    	    if (leveltime < actor->beattic || P_TacInTheWay (actor))
    		return true;
    	    P_DerpFiring (actor);
    	    P_DerpLog (actor, "leans out and fires");
    	    P_SetMobjState (actor, actor->info->missilestate);
    	    return true;
    	}
    	// Fired: back in.
    	P_MindStep (actor, STEP_COVER_BACK, "cover/leaning.fired", "It got its shot off: back in");
    	actor->waytic = -1000;
    	return true;
        }
        // Up over its wall and nothing there: down again, no walking about.
        if (leveltime - actor->waytic > (actor->lowcover && P_TacAt (actor, actor->peekx, actor->peeky) ? TICRATE/2 : 3*TICRATE))
        {
    	P_MindStep (actor, STEP_COVER_BACK, "cover/leaning.nothing",
    			"Leaned out and nothing there: back in");
    	actor->waytic = -1000;
    	return true;
        }
        P_DerpWalkTo (actor, actor->peekx, actor->peeky);
        return true;
    }
}

// Retreating: to its hiding place, and further off while he has not come; a bait turns.
static boolean RetreatStep (mobj_t* actor, mobj_t* player, boolean visible)
{
    if (actor->tactic == STEP_BAIT)
    {
        if (P_TacAt (actor, actor->coverx, actor->covery) || leveltime - actor->tactictic > BAIT_TIME)
        {
    	P_DerpLog (actor, "turns on him: it was never running");
    	P_DerpBark (actor, BARK_BAITTURN);
    	P_MindBecome (actor, MIND_HUNT, STEP_NONE, "retreat/bait.turns",
    			"Past its friend, or long enough: it turns on "
    			"him, it was never running");
    	return false;
        }
        P_DerpWalkTo (actor, actor->coverx, actor->covery);
        return true;
    }
    if (leveltime - actor->tactictic > RETREAT_TIME)
    {
        // Back, from where it hid.
        P_MindBecome (actor, MIND_HUNT, STEP_NONE, "retreat.time_up",
        		"Long enough hidden: back to the hunt");
        return false;
    }
    // Seen where it meant to hide: somewhere else, farther.
    if (visible && P_DerpTucked (actor))
        P_TacFindCover (actor, player, false, true, &actor->coverx, &actor->covery, &actor->peekx, &actor->peeky);
    // Hidden, and he has not come yet: time to get further off, not to stand there.
    if (!visible && P_DerpTucked (actor) && leveltime - actor->movedtic > HIDE_REST)
    {
        fixed_t	x, y, kx, ky;
        actor->movedtic = leveltime;
        if (P_TacFindCover (actor, P_TacBelieved (actor, player, visible), false, true, &x, &y, &kx, &ky)
    	&& P_AproxDistance (x - actor->coverx, y - actor->covery) >= HIDE_ON*FRACUNIT)
        {
    	actor->coverx = x;
    	actor->covery = y;
    	P_DerpLog (actor, "gets further off while it can");
        }
    }
    if (!P_DerpTucked (actor))
        P_DerpWalkTo (actor, actor->coverx, actor->covery);
    else
    {
        actor->movedir = 8;
        P_DerpFaceDanger (actor);
    }
    return true;
}

// In ambush: to its post, then the wait, facing the way in, until he comes or it is called.
static boolean AmbushStep (mobj_t* actor, mobj_t* player, boolean visible)
{
    if (visible)
    {
        // He came: open fire.
        P_DerpBark (actor, BARK_TRAPSPRUNG);
        // From the cover it took for this, not out in the open after him.
        if (actor->fightsfrom && P_TacAt (actor, actor->coverx, actor->covery))
        {
    	P_DerpRule (actor, "ambush.sprung", "He came into view and it is at the cover it chose: "
    		    "up, and at the lean");
    	Sprung (actor);
    	return true;
        }
        P_MindBecome (actor, MIND_HUNT, STEP_NONE, "ambush.he_came", "He came into view: open fire");
        return false;
    }
    // Hidden from the way in, so it does not see him come: it hears him, and
    // he has walked into what its lean covers. Out to meet him.
    if (actor->fightsfrom && P_PHASE_OF (actor) && P_TacInCover (actor)
        && actor->knowntic && leveltime - actor->knowntic < AMBUSH_HEARS
        && P_CoverShoots (actor->peekx, actor->peeky, R_PointInSubsector (actor->peekx, actor->peeky)->sector->floorheight, player))
    {
        P_DerpBark (actor, BARK_TRAPSPRUNG);
        P_DerpRule (actor, "ambush.heard_him", "It hears him walk into what its lean covers: out to "
    		"meet him");
        Sprung (actor);
        return true;
    }
    // Waiting by a door, it listens. The fight still going on after a
    // while, rounds coming in on him: he has his hands full, and it goes in.
    if (P_TacBreach (actor))
    {
        P_DerpLog (actor, "hears the fight still on: goes in");
        P_MindBecome (actor, MIND_HUNT, STEP_NONE, "ambush.goes_in",
        		"The fight still on after a while, and the director says "
        		"go: it goes in");
        return false;
    }
    if (leveltime - actor->tactictic > BREACH_AFTER && P_WordCalled (actor))
    {
        P_DerpLog (actor, "the fight it was told of: goes in");
        P_MindBecome (actor, MIND_INVESTIGATE, STEP_NONE, "ambush.word_goes",
        		"Ready on word of a fight, and the director turns to "
        		"press him: it goes to look");
        actor->goalx = actor->wordx;
        actor->goaly = actor->wordy;
        actor->goaltic = leveltime;
        return false;
    }
    if (leveltime - actor->tactictic > AMBUSH_TIME && !P_WordHolds (actor))
    {
        // Nobody came: a guard goes back, the others go and look.
        if (actor->guard && actor->spawnpoint.type)
        {
    	P_DerpRule (actor, "ambush.back_to_post", "Nobody came: a guard goes back to what it "
    		    "guards");
    	P_TacBackToPost (actor);
    	return false;
        }
        if (actor->heldready)
        {
    	P_MindBecome (actor, MIND_NONE, STEP_NONE, "ambush.stands_down",
    		      "It stood ready where it was and nobody came: it stands down");
    	P_DerpBark (actor, BARK_STANDSDOWN);
    	actor->target = NULL;
    	actor->awareness = 500;
    	P_SetMobjState (actor, actor->info->spawnstate);
    	return true;
        }
        P_MindBecome (actor, MIND_SEARCH, STEP_NONE, "ambush.nobody_came",
        		"Nobody came: it goes and looks");
        actor->searchtic = leveltime;
        return false;
    }
    if (!P_TacInCover (actor))
    {
        // A doorway may be a long walk off: the waiting starts there.
        P_DerpWalkTo (actor, actor->coverx, actor->covery);
        if (actor->tactic != STEP_WAITING && leveltime - actor->tactictic > AMBUSH_TIME)
        {
    	P_MindBecome (actor, MIND_SEARCH, STEP_NONE, "ambush.never_got_there",
    			"Too long on its way to the doorway: it goes "
    			"and looks");
    	actor->searchtic = leveltime;
    	return false;
        }
    }
    else
    {
        if (actor->tactic != STEP_WAITING)
        {
    	P_MindStep (actor, STEP_WAITING, "ambush.in_place", "It has reached its place: the waiting starts, "
    		    "facing the way in");
    	actor->tactictic = leveltime;
        }
        Face (actor, actor->facing);
        // Ready: the first shot comes as he shows himself.
        actor->movecount = 0;
        // Down behind its low wall the whole wait.
        if (actor->lowcover)
    	P_TacKneel (actor);
    }
    return true;
}

// Regrouping: to the other squad, one of them on arrival.
static boolean RegroupStep (mobj_t* actor, mobj_t* player, boolean visible)
{
    if (visible)
    {
        P_MindBecome (actor, MIND_HUNT, STEP_NONE, "regroup.seen",
        		"He came into view: back to the fight");
        return false;
    }
    if (P_TacAt (actor, actor->coverx, actor->covery)
        || P_AproxDistance (actor->x - actor->coverx, actor->y - actor->covery) < 96*FRACUNIT
        || leveltime - actor->tactictic > 30*TICRATE)
    {
        // Among them: one of them now, watching with them.
        P_MindBecome (actor, MIND_NONE, STEP_NONE, "regroup.arrived", "Among the others: it joins their squad and watches "
    		  "with them");
        actor->squad = actor->joinsquad;
        actor->target = NULL;
        P_MindAlert (actor, 2);
        actor->awareness = 500;
        P_SetMobjState (actor, actor->info->spawnstate);
        return true;
    }
    P_DerpWalkTo (actor, actor->coverx, actor->covery);
    return true;
}

// The hunt (and any mind of the senses'): a bound, a push, going round, holding the way out, keeping range.
static boolean HuntStep (mobj_t* actor, mobj_t* player, boolean visible)
{
    // On a bound, seen through; in sight, fighting: at its range.
    if (P_MindBounding (actor))
        return P_TacBound (actor);
    if (P_MindPushing (actor) && P_TacPush (actor, player, visible))
        return true;
    if (P_MindRounding (actor) && P_TacRound (actor, visible))
        return true;
    // At the way out with him out of sight: it waits for him there.
    if (!visible && leveltime < actor->holdtic)
    {
        actor->movedir = 8;
        if (actor->knowntic)
    	actor->angle = R_PointToAngle2 (actor->x, actor->y, actor->knownx, actor->knowny);
        return true;
    }
    return visible && P_TacKeepRange (actor, player);
}

// What a soldier does each tic in each mind, once Decide has had its say
// (p_mind.h's order of minds; NULL is the hunt's). Every change of step a mind
// makes is in its function here, and nowhere else.
typedef boolean (*mindstep_t) (mobj_t* actor, mobj_t* player, boolean visible);
static const mindstep_t steps[NUMMINDS] =
{
    NULL, NULL, NULL, NULL, NULL,	// none, investigate, hunt, search, return: HuntStep
    CoverStep, RetreatStep, AmbushStep, RegroupStep,
    NULL, NULL,				// heal, flank: the imps' (p_demons.c)
};

boolean P_DerpTactics (mobj_t* actor, boolean visible)
{
    mobj_t*	player = P_DerpPlayer ();

    if (!P_TacticsOn () || !player || !P_DerpMonster (actor) || !P_DerpSoldier (actor->type))
	return false;
    // Down on one knee for a friend's shot: it stays down. Down behind a low
    // wall it goes on thinking: that is where it fights from.
    if (P_DerpKneeling (actor) && !(actor->lowcover && (actor->mind == MIND_AMBUSH
							|| (actor->mind == MIND_COVER && actor->tactic == STEP_COVER_DUCKED))))
    {
	actor->movecount = 0;
	return true;
    }
    // With the tic's routes spent it thinks next tic, before any rule runs; let
    // through, every route its rules ask for runs (one put off is no answer to
    // a rule), up to a near monster's tic of them; past that, a rule whose route
    // can wait goes without it this time. Then again on its own tic, apart from
    // those it thought with.
    if (leveltime >= actor->decidetic && P_NavBudgetSpent (actor))
	actor->decidetic = leveltime + 1;
    else if (leveltime >= actor->decidetic)
    {
	actor->decidetic = P_DecideNext (actor, DECIDE_EVERY);
	P_NavBudgetHold ();
	Decide (actor, visible);
	P_NavBudgetRelease ();
    }
    if (P_TacLayFire (actor, visible))
	return true;
    // The step it is at, by its mind.
    return (steps[actor->mind] ? steps[actor->mind] : HuntStep) (actor, player, visible);
}
