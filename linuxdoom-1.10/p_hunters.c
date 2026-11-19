//
// Imps as opportunistic hunters (p_hunters.h).
//

#include <stdlib.h>

#include "doomdef.h"
#include "doomstat.h"
#include "m_random.h"
#include "p_local.h"
#include "p_barks.h"
#include "p_derp.h"
#include "p_derplog.h"
#include "p_fire.h"
#include "p_nav.h"
#include "p_senses.h"
#include "p_tactics.h"
#include "r_main.h"
#include "tables.h"
#include "p_director.h"
#include "p_hang.h"
#include "p_hunters.h"
#include "p_pincer.h"
#include "p_navbudget.h"
#include "p_tune.h"

// What it is doing: mobj_t.lurk, p_mind.h's LURK_*.

// How many are after him at once, near him; how near that is.
#define HUNTERS_MOST	TUNE (HUNTERS_MOST, 3)
#define HUNTERS_NEAR	TUNE (HUNTERS_NEAR, 1024)
// A noise nearer than this it answers like anybody else.
#define LURK_NOISE	TUNE (LURK_NOISE, 384)
// He comes this close, it goes for him.
#define LURK_CLOSE	TUNE (LURK_CLOSE, 224)
// He looks this straight at it, seeing it, for this long: spotted. (A glance past
// it is not noticing it: one tic of that ended every ambush across a yard.)
#define LURK_SEEN	(ANG45/2)
#define LURK_NOTICED	TUNE (LURK_NOTICED, TICRATE)
// He looks this far away from it: an opening.
#define LOOKS_AWAY	(ANG45 + ANG45/2)
#define HURTING		TUNE (HUNTERS_HURTING, 50)
// How long it waits at the least, each its own (they do not all pop out together),
// before moving nearer anyway, and how long a stalk may take.
#define LURK_LEAST	TUNE (LURK_LEAST, 3*TICRATE)
#define LURK_SPREAD	TUNE (LURK_SPREAD, 4*TICRATE)
#define LURK_MOST	TUNE (LURK_MOST, 30*TICRATE)
// How near him it is heard: breathing where it waits, claws on the wall on the move.
#define LURK_BREATH	TUNE (LURK_BREATH, 640)
#define LURK_SCRAPE	TUNE (LURK_SCRAPE, 900)
#define STALK_MOST	TUNE (STALK_MOST, 20*TICRATE)
// One holding something (P_DerpOnDuty) waits until he is this near it, on foot:
// a walk there no longer than HOLD_WALK cells, looked for every few tics.
#define HOLD_NEAR	TUNE (HOLD_NEAR, 384)
#define HOLD_WALK	36
#define HOLD_SEARCH	TUNE (HOLD_SEARCH, 1500)
// A way round to him is a long one.
#define PINCER_MOST	TUNE (PINCER_MOST, 60*TICRATE)
// A stalk getting nowhere (no way to him, or it is there and he is not): it waits
// again, and leaves the turn to another. Checked this often, this much ground.
#define STALK_CHECK	TUNE (STALK_CHECK, 2*TICRATE)
#define STALK_MOVED	TUNE (STALK_MOVED, 32)
#define STALK_REST	TUNE (STALK_REST, 10*TICRATE)
#define SHUT_REST	TUNE (SHUT_REST, 60*TICRATE)
// Where he is, roughly: how near, and how often.
#define SENSE_NEAR	TUNE (SENSE_NEAR, 768)
#define SENSE_EVERY	TUNE (SENSE_EVERY, 2*TICRATE)
// A stalk keeps out of what he faces: a place this far ahead of him, this wide.
#define FRONT_AHEAD	TUNE (FRONT_AHEAD, 256)
#define FRONT_WIDE	TUNE (FRONT_WIDE, 256)


static boolean Hunter (const mobj_t* mo)
{
    return mo->type == MT_TROOP && !mo->player && mo->health > 0 && P_DerpAI () && derpparts.demons
	&& derpparts.senses;
}

// Those after him now: near him, or stalking him from anywhere. Those lying in wait
// do not count.
static int Hunters (mobj_t* actor, mobj_t* him)
{
    thinker_t*	th;
    int		n = 0;

    for (th = thinkercap.next; th != &thinkercap; th = th->next)
    {
	mobj_t*	mo = (mobj_t *)th;
	if (th->function.acp1 != (actionf_p1)P_MobjThinker || mo == actor || !Hunter (mo)
	    || mo->target != him)
	    continue;
	if (mo->lurk == LURK_STALKING
	    || (!mo->lurk && (mo->mind == MIND_HUNT || mo->mind == MIND_FLANK)
		&& P_AproxDistance (him->x - mo->x, him->y - mo->y) < HUNTERS_NEAR*FRACUNIT))
	    n++;
    }
    return n;
}

boolean P_HunterHeard (mobj_t* mo, int distance)
{
    mobj_t*	him = P_DerpPlayer ();

    // (One holding something waits for him at it whatever it heard: E1M2's shotgun imp
    // went to look at a zombie's shots beside it, out of a window and down to him.)
    if (!him || !Hunter (mo) || mo->mind != MIND_NONE || (distance <= LURK_NOISE && !P_DerpOnDuty (mo)))
	return false;
    // All of them wait: an opening, or the turn of the few, is what brings one out.
    P_DerpRuleIf (!mo->lurk, mo, "none.lurks", "A far noise: instead of coming, it means to lie in wait "
		  "for him");
    if (!mo->lurk)
	mo->lurk = LURK_WOKEN;
    return true;
}

// One getting up clicks to those beside it: a pair does not split over whether a
// noise reached 80 units further.
#define PACK_WAKE	TUNE (PACK_WAKE, 384)

static void WakeNeighbours (mobj_t* actor)
{
    thinker_t*	th;

    for (th = thinkercap.next; th != &thinkercap; th = th->next)
    {
	mobj_t*	mo = (mobj_t *)th;
	if (th->function.acp1 != (actionf_p1)P_MobjThinker || mo == actor || !Hunter (mo) || mo->lurk
	    || mo->mind != MIND_NONE || P_AproxDistance (mo->x - actor->x, mo->y - actor->y) > PACK_WAKE*FRACUNIT)
	    continue;
	P_DerpRule (mo, "none.lurks", "A far noise: instead of coming, it means to lie in wait for him");
	mo->lurk = LURK_WOKEN;
    }
}

// The tic an imp last got up to lurk. Finding its place is a walk of the floor
// round it and a look at every ceiling there; a pack woken by one noise did it
// all in one tic. One a tic, then, and not once the tic's routes are spent: the
// rest stay as they are, woken, and get up at a later look. (Its look is spent on
// that, as getting up would have spent it: a look that went on could wake it to
// something else with its lurk still pending, and a lurker never decides.)
static int	waketic = -1;

boolean P_HunterWakes (mobj_t* actor)
{
    mobj_t*	him = P_DerpPlayer ();

    if (actor->lurk != LURK_WOKEN || !him)
	return false;
    if (waketic == leveltime || P_NavBudgetSpent (actor))
	return true;
    waketic = leveltime;
    actor->target = him;
    P_MindBecome (actor, MIND_HUNT, STEP_NONE, "none/lurk_woken.goes", "Up, and off to a hidden place to wait for him");
    actor->knownx = him->x;
    actor->knowny = him->y;
    actor->knowntic = leveltime;
    actor->lurk = LURK_GOING;
    actor->lurktic = leveltime;
    actor->lurkseen = 0;
    if (!P_DerpHide (actor, him, &actor->lurkx, &actor->lurky))
    {
	actor->lurkx = actor->x;
	actor->lurky = actor->y;
    }
    // Somewhere dark and high near there: the ceiling.
    P_HangSpot (actor);
    P_DerpLog (actor, "lurks: goes to wait for him at (%d,%d)", actor->lurkx >> FRACBITS, actor->lurky >> FRACBITS);
    P_DerpCry (actor, BARK_IMPCLICKS);
    P_DerpBarkNear (actor->x, actor->y, 1024, BARK_IMPSQUIET);
    WakeNeighbours (actor);
    P_SetMobjState (actor, actor->info->seestate);
    return true;
}

// He is about here: the further off, the rougher.
static void Fix (mobj_t* actor, mobj_t* him)
{
    int	error = (P_AproxDistance (him->x - actor->x, him->y - actor->y)>>FRACBITS)/4;

    actor->knownx = him->x + (P_Random () - 128)*error/128*FRACUNIT;
    actor->knowny = him->y + (P_Random () - 128)*error/128*FRACUNIT;
    actor->knowntic = leveltime;
}

// Near enough, it feels him. Only while it is after him: one healing has other plans.
static void Sense (mobj_t* actor, mobj_t* him)
{
    if ((actor->mind != MIND_HUNT && actor->mind != MIND_FLANK)
	|| (leveltime + actor->serial) % SENSE_EVERY
	|| P_AproxDistance (him->x - actor->x, him->y - actor->y) > SENSE_NEAR*FRACUNIT)
	return;
    Fix (actor, him);
}

// How far it is out of the middle of his view.
static int OffHisView (mobj_t* actor, mobj_t* him)
{
    return abs ((int)(R_PointToAngle2 (him->x, him->y, actor->x, actor->y) - him->angle));
}

// Only near him is there anything to see an opening in, and not the moment it got there.
static boolean Opening (mobj_t* actor, mobj_t* him, fixed_t dist)
{
    if (leveltime - actor->lurktic <= LURK_LEAST + (actor->serial*37)%LURK_SPREAD)
	return false;
    // Hurt, and they are all over him: that it smells from anywhere. (Two sat at home
    // for most of a minute, too far off to care, with him on 18 and a room on him.)
    if (him->health < HURTING && (P_FightOn () || P_DirectorPressing ()))
	return true;
    return dist < SENSE_NEAR*FRACUNIT
	&& (OffHisView (actor, him) > (int)LOOKS_AWAY || P_FightOn () || him->health < HURTING);
}

// He has had it in the middle of his eye long enough to have noticed.
static boolean Spotted (mobj_t* actor, mobj_t* him, boolean visible)
{
    if (!visible || OffHisView (actor, him) >= (int)LURK_SEEN || !P_CheckSight (him, actor))
    {
	actor->lurkseen = 0;
	return false;
    }
    if (!actor->lurkseen)
	actor->lurkseen = leveltime;
    return leveltime - actor->lurkseen >= LURK_NOTICED;
}

// He is coming for what it holds: near it, and a short walk from it. Near it is not
// near enough: E1M2's shotgun imp took him walking past below its ledge, 136 units
// down, for him come, and followed him off across the level.
static boolean HeComes (mobj_t* actor, mobj_t* him)
{
    static int	path[HOLD_WALK + 1];
    int		nav = P_NavClassOf (actor), n;

    if (nav < 0 || ((leveltime + actor->serial) & 3)
	|| P_AproxDistance (him->x - actor->guardx, him->y - actor->guardy) >= HOLD_NEAR*FRACUNIT)
	return false;
    // (Waiting on the tic's routes: not coming yet, as far as it knows; it looks
    // again in four tics anyway.)
    n = P_NavPathFor (actor, nav, him->x, him->y, actor->guardx, actor->guardy, path, HOLD_WALK + 1, HOLD_SEARCH);
    return n > 0 && n <= HOLD_WALK;
}

// Moved on, or stuck: true if it has got nowhere since the last look.
static boolean Stuck (mobj_t* actor)
{
    boolean	stuck;

    if ((leveltime - actor->lurktic) % STALK_CHECK)
	return false;
    stuck = P_AproxDistance (actor->lurkx - actor->x, actor->lurky - actor->y) < STALK_MOVED*FRACUNIT;
    actor->lurkx = actor->x;
    actor->lurky = actor->y;
    return stuck;
}

// Waiting, it keeps its place hidden as he moves (P_DerpLurkSlip): looked at this
// often, not again for this long once its place was about to be seen, and the walk
// to a new one given this long. One holding something keeps near it.
#define SLIP_EVERY	8
#define SLIP_AGAIN	TUNE (SLIP_AGAIN, 2*TICRATE)
#define SLIP_MOST	TUNE (SLIP_MOST, 4*TICRATE)

static boolean Slips (mobj_t* actor, boolean visible)
{
    fixed_t	x, y;
    int		slip;

    // (A look for a new place is a cover search, a flood of the floor round it
    // and a route or two: with the tic's routes spent it looks at its next turn.)
    if ((leveltime + actor->serial) % SLIP_EVERY || leveltime - actor->lurkslip < SLIP_AGAIN
	|| P_NavBudgetSpent (actor))
	return false;
    slip = P_DerpLurkSlip (actor, visible, &x, &y);
    if (slip == SLIP_HIDDEN)
	return false;
    actor->lurkslip = leveltime;
    if (slip == SLIP_GOES && P_DerpOnDuty (actor)
	&& P_AproxDistance (x - actor->guardx, y - actor->guardy) >= HOLD_NEAR*FRACUNIT)
	slip = SLIP_STAYS;
    if (slip == SLIP_STAYS)
    {
	P_DerpRule (actor, "hunt/lurking.holds_still", "Its place is about to be seen and no way out of "
		    "it is hidden: it keeps still");
	return false;
    }
    P_DerpRule (actor, "hunt/lurking.slips", "Its place is about to be seen: it slips off to another, "
		"by a way he does not see");
    actor->lurk = LURK_SLIPPING;
    actor->lurkx = x;
    actor->lurky = y;
    P_DerpLog (actor, "slips off to wait for him at (%d,%d)", x >> FRACBITS, y >> FRACBITS);
    return true;
}

static void Stalk (mobj_t* actor, mobj_t* him)
{
    angle_t	an = him->angle>>ANGLETOFINESHIFT;

    // Two on him have to know where he is to meet there.
    if (actor->pincer && !((leveltime + actor->serial) % SENSE_EVERY))
	Fix (actor, him);
    navavoidx = him->x + FRONT_AHEAD*finecosine[an];
    navavoidy = him->y + FRONT_AHEAD*finesine[an];
    navavoidr = FRONT_WIDE;
    P_PincerRoute (actor);
    P_DerpWalkTo (actor, actor->knownx, actor->knowny);
    navavoidr = 0;
    navavoidhard = false;
    // No way there. That may only be the guess, which can fall in a wall the further
    // off he is: where he really is, then, before it calls the stalk off.
    if (actor->nowaytic == leveltime)
    {
	actor->knownx = him->x;
	actor->knowny = him->y;
	actor->knowntic = leveltime;
	actor->waytic = -1000;
    }
    P_DerpCry (actor, BARK_IMPCLICKS);
}

static void Over (mobj_t* actor, const char* why)
{
    P_MindLurkOver (actor, why);
}

boolean P_HunterThink (mobj_t* actor, boolean visible)
{
    mobj_t*	him = P_DerpPlayer ();
    fixed_t	dist;

    if (!him || !Hunter (actor))
	return false;
    Sense (actor, him);
    if (actor->lurk < LURK_GOING)
	return false;
    if (actor->mind != MIND_HUNT)
    {
	P_DerpRule (actor, "hunt/lurk_going+hunt/lurking+hunt/stalking.mind_elsewhere", "Its mind went "
		    "elsewhere: no more lying in wait");
	Over (actor, "stops lurking: its mind went elsewhere");
	return false;
    }
    dist = P_AproxDistance (him->x - actor->x, him->y - actor->y);
    // Near enough, or he has it in the middle of his eye: no more of this.
    if (dist < LURK_CLOSE*FRACUNIT)
    {
	P_DerpRule (actor, "hunt/lurk_going+hunt/lurking+hunt/stalking.too_close", "He came too close: it "
		    "goes for him");
	Over (actor, "breaks cover: he came too close");
	return false;
    }
    if (Spotted (actor, him, visible))
    {
	P_DerpRule (actor, "hunt/lurk_going+hunt/lurking+hunt/stalking.spotted", "He has it in the middle "
		    "of his eye: no more hiding");
	Over (actor, "breaks cover: spotted");
	return false;
    }
    switch (actor->lurk)
    {
      case LURK_GOING:
	// (Its spot on the ceiling may still be being found: his way in, over tics.)
	P_HangWaits (actor);
	if (P_AproxDistance (actor->lurkx - actor->x, actor->lurky - actor->y) < 32*FRACUNIT
	    || leveltime - actor->lurktic > (actor->hang ? 10 : 5)*TICRATE)
	{
	    P_DerpRule (actor, "hunt/lurk_going.there", "There, or long enough: it waits");
	    actor->lurk = LURK_WAITING;
	    actor->lurktic = leveltime;
	    P_HangUp (actor);
	    return true;
	}
	P_DerpWalkTo (actor, actor->lurkx, actor->lurky);
	return true;

      case LURK_WAITING:
	if (dist < LURK_BREATH*FRACUNIT)
	    P_DerpCry (actor, BARK_IMPLURK);
	actor->movedir = 8;
	actor->angle = R_PointToAngle2 (actor->x, actor->y, actor->knownx, actor->knowny);
	// One holding something waits for him to come to it: not done waiting, not
	// after an opening somewhere else. (E1M2's shotgun imp went at the first
	// shots, and the shotgun was his for the walking up.) Taking what it holds
	// frees it (P_DerpKeyTaken).
	// Him come for what it holds, it waits for its moment, not for the time it
	// has waited; and then it is on him there, not after him round the level.
	if (P_DerpOnDuty (actor))
	{
	    boolean	comes = HeComes (actor, him);
	    if (comes && Opening (actor, him, dist))
	    {
		P_DerpRule (actor, "hunt/lurking.strikes", "He came for what it holds and gave it an opening: "
			    "it is on him, there");
		Over (actor, "breaks cover: he came for what it holds");
		return false;
	    }
	    P_DerpRuleIf (!comes && leveltime - actor->lurktic == LURK_MOST + 1, actor, "hunt/lurking.holds",
			  "It holds something: waited long, it keeps to it, and he has to come");
	    Slips (actor, visible);
	    return true;
	}
	// An opening, or it has waited long enough and he went elsewhere; and
	// room for another on him either way.
	if ((Opening (actor, him, dist) || leveltime - actor->lurktic > LURK_MOST)
	    && Hunters (actor, him) < HUNTERS_MOST)
	{
	    Fix (actor, him);
	    P_DerpRule (actor, "hunt/lurking.stalks", "He looks away, a fight is on, he is hurting, or it "
			"waited long enough, and there is room for another on him: it stalks him");
	    actor->lurk = LURK_STALKING;
	    actor->lurktic = leveltime;
	    actor->lurkx = actor->x;
	    actor->lurky = actor->y;
	    P_DerpLog (actor, dist < SENSE_NEAR*FRACUNIT ? "stalks him: sees an opening"
		       : "stalks him: he went elsewhere");
	    P_PincerPair (actor, him);
	    return true;
	}
	Slips (actor, visible);
	return true;

      case LURK_SLIPPING:
	// Waiting still: the clock of its wait runs on, it only moves.
	if (P_AproxDistance (actor->lurkx - actor->x, actor->lurky - actor->y) >= 32*FRACUNIT
	    && leveltime - actor->lurkslip <= SLIP_MOST)
	{
	    P_DerpWalkTo (actor, actor->lurkx, actor->lurky);
	    if (actor->nowaytic != leveltime)
		return true;
	}
	P_DerpRule (actor, "hunt/lurk_slipping.there", "At its new place, no way on, or long enough: it waits again");
	actor->lurk = LURK_WAITING;
	return true;

      default:
	if (!visible && dist < LURK_SCRAPE*FRACUNIT)
	    P_DerpCry (actor, BARK_IMPSCRAPE);
	if (P_PincerHold (actor, him, visible))
	    return true;
	// (Stopped being a stalk just now: it is in, not walking on.)
	if (actor->lurk != LURK_STALKING)
	    return false;
	if (leveltime - actor->lurktic > (actor->pincer ? PINCER_MOST : STALK_MOST))
	{
	    P_DerpRule (actor, "hunt/stalking.done", "Stalked long enough: it goes for him");
	    Over (actor, "done stalking: goes for him");
	    return false;
	}
	if (Stuck (actor))
	{
	    P_DerpRule (actor, "hunt/stalking.nowhere", "It got nowhere: it waits again, and leaves the "
			"turn to another");
	    actor->lurk = LURK_WAITING;
	    // No way even to where he really is: shut in, and it waits for that to change.
	    actor->lurktic = leveltime + (leveltime - actor->nowaytic < 2*STALK_CHECK ? SHUT_REST : STALK_REST);
	    actor->pincer = 0;
	    P_DerpLog (actor, "stalk got nowhere: waits again");
	    return true;
	}
	Stalk (actor, him);
	return true;
    }
}



void P_HunterHit (mobj_t* mo)
{
    P_DerpRuleIf (mo->lurk, mo, "hunt/lurk_going+hunt/lurking+hunt/stalking.shot", "Shot: no more lying "
		  "in wait");
    if (mo->lurk)
	Over (mo, "breaks cover: shot");
}
