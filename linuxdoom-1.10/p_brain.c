//
// The brain: who gets the tic, in what order; and what a monster makes of
// what its senses tell it (p_brain.h).
//

#include "doomdef.h"
#include "doomstat.h"
#include "p_local.h"
#include "p_derp.h"
#include "p_derptiming.h"
#include "p_mind.h"
#include "p_decide.h"
#include "p_senses.h"
#include "p_tactics.h"
#include "p_demons.h"
#include "p_kinds.h"
#include "p_charge.h"
#include "p_hunters.h"
#include "p_word.h"
#include "p_director.h"
#include "p_fire.h"
#include "p_barks.h"
#include "p_derplog.h"
#include "p_navbudget.h"
#include "p_tune.h"
#include "p_brain.h"

static boolean Look (mobj_t* actor)
{
    actor->threshold = 0;
    // Told something while it looked (a noise, a shout): up and after it,
    // if what it is after is still there. A target dead or gone, it forgets
    // (A_Chase just sent it here for that: back to A_Chase would never end).
    if (actor->mind != MIND_NONE)
    {
	if (actor->target && actor->target->health > 0 && (actor->target->flags & MF_SHOOTABLE))
	{
	    P_SetMobjState (actor, actor->info->seestate);
	    return true;
	}
	P_MindBecome (actor, MIND_NONE, STEP_NONE, "any.target_gone", "What it was after is dead or gone: it forgets");
    }
    // A body of its own lying in sight: the alarm.
    if (P_DerpBodies (actor))
	return true;
    // An imp that means to wait for him: up, and to a place.
    if (P_HunterWakes (actor))
	return true;
    // Word of a fight from its own.
    if (P_WordReaches (actor))
	return true;
    // A guard readied by a far fight: a useful place near its key.
    if (P_DerpGuardPost (actor))
	return true;
    // Nothing told it anything: it looks, and may make him out.
    return P_SensesLook (actor);
}

boolean P_BrainLook (mobj_t* actor)
{
    derp_timing_group previous;
    boolean	acted;

    if (!P_SensesOn ())
	return false;
    previous = P_DerpTimingBegin (DERP_TIMING_BRAIN);
    acted = Look (actor);
    P_DerpTimingEnd (previous);
    return acted;
}

static boolean Chase (mobj_t* actor, mobj_t* player)
{
    boolean	visible = P_CheckSight (actor, player);

    // In sight it knows where he is. Before anything else: a soldier leaning
    // out to fire went on to "where did he go" with him in its sights.
    if (visible)
    {
	P_SensesSaw (actor, player);
	P_DerpSighted (actor);
    }
    P_SensesBelieve (actor);
    // A guard led too far from what it guards, and him out of sight: back.
    P_DerpLeash (actor, visible);
    // Standing in something that hurts: out, before anything it had in mind.
    if (P_SensesGetOut (actor))
	return true;
    // A pinky smells him through walls, and charges.
    P_ChargeSmell (actor);
    if (P_ChargeStart (actor, visible))
	return true;
    // A soldier's tactics: cover, retreat, ambush, regroup, the fight at its range.
    if (P_DerpTactics (actor, visible))
	return true;
    // An imp's ways: lying in wait, the pounce, the side-step, the pack round
    // him, away to heal or to come again from elsewhere.
    if (P_DerpHunt (actor, visible))
	return true;
    // Every other kind: its own table of decisions (p_kinds.c), empty where
    // vanilla still rules.
    if (P_KindThink (actor, visible))
	return true;
    // In sight and nothing above took it: after him as vanilla's monsters are.
    // Stopped at the edge of a floor that hurts: its route, round it if there
    // is a way round.
    if (visible)
    {
	if (actor->mind != MIND_HUNT)
	    P_MindBecome (actor, MIND_HUNT, STEP_NONE, "any.in_sight", "He is in plain sight: after him");
	if (leveltime - actor->drytic < TICRATE)
	{
	    P_DerpWalkTo (actor, player->x, player->y);
	    return true;
	}
	return false;
    }
    // Going to look or searching, a body of its own in sight (every few tics):
    // the alarm, and off to where it lies.
    if ((actor->mind == MIND_INVESTIGATE || actor->mind == MIND_SEARCH) && P_PHASE_OF (actor) && P_DerpBodies (actor))
    {
	P_DerpWalkTo (actor, actor->goalx, actor->goaly);
	return true;
    }
    // Out of sight: the basic minds. To where he was, to a noise, searching,
    // home.
    return P_SensesChase (actor);
}

boolean P_BrainChase (mobj_t* actor)
{
    mobj_t*	player = P_SensesPlayer ();
    derp_timing_group previous;
    boolean	acted;

    if (!P_SensesOn () || !player || actor->target != player || !P_DerpMonster (actor))
	return false;
    previous = P_DerpTimingBegin (DERP_TIMING_BRAIN);
    acted = Chase (actor, player);
    P_DerpTimingEnd (previous);
    return acted;
}


//
// Hearing: what a listener makes of a noise its senses brought it. A table
// of rules asked in order, as the decisions are (p_decide.h): the first that
// settles it ends it. The senses only say what was heard and, on asking,
// where it seems to come from (P_SensesGuess, which rolls the dice: so only
// the rules that need it ask).
//

// How near a noise a monster answers it whatever the director's count.
#define RESERVE_NEAR	TUNE (RESERVE_NEAR, 512)
// How near a fight still going on brings it, by the sound of it.
#define BREACH_NEAR	TUNE (BREACH_NEAR, 1024)
// After it found no way somewhere, noises from beyond that wall are no news
// for this long, this near where it could not get.
#define NOWAY_DEAF	TUNE (NOWAY_DEAF, 30*TICRATE)
#define NOWAY_NEAR	TUNE (NOWAY_NEAR, 640)
// And no farther than this from home: shut in there.
#define SHUT_HOME	TUNE (SHUT_HOME, 384)

typedef boolean (*hearing_t) (heard_t* h);
typedef struct
{
    const char*	name;
    hearing_t	rule;
} hearrule_t;

static boolean Gunfire (noise_t kind)
{
    return kind == NOISE_MONSTERSHOT || kind == NOISE_IMPACT || (kind >= NOISE_PISTOL && kind <= NOISE_BFG);
}

// Busy with another monster: nothing new.
static boolean Busy (heard_t* h)
{
    mobj_t*	l = h->listener;
    return l->target && l->target != h->player && l->target->health > 0;
}

// The player in plain sight: nothing new.
static boolean SeesHim (heard_t* h)
{
    return h->listener->mind == MIND_HUNT && P_CheckSight (h->listener, h->player);
}

// In the middle of a tactic (cover, retreat, ambush, regroup, lying in
// wait): a noise it is ready for does not undo it. Though his own noise
// tells it he is still about, and about where.
static boolean KeepsTactic (heard_t* h)
{
    mobj_t*	l = h->listener;

    if (!((l->mind >= MIND_COVER || P_MindLurks (l)) && l->target == h->player))
	return false;
    if (h->source == h->player)
    {
	fixed_t	gx = h->x, gy = h->y;
	P_SensesRough (&gx, &gy, h->distance);
	P_SensesTold (l, gx, gy);
    }
    return true;
}

// On his heels (word of him in the last few seconds): it keeps after him.
// His own noise freshens where it thinks he is; another's (a gun, a death, a
// shout) is no news. Before, every noise turned a hunter back to "going to
// look", hunt and look by turns: no plan to see.
static boolean Hunting (heard_t* h)
{
    mobj_t*	l = h->listener;

    if (!P_SensesHunting (l))
	return false;
    if (h->source == h->player)
    {
	fixed_t	gx = h->x, gy = h->y;
	P_SensesRough (&gx, &gy, h->distance);
	P_SensesTold (l, gx, gy);
    }
    return true;
}

// On guard, and that is far from its key: ready, not gone.
static boolean GuardPost (heard_t* h)
{
    if (!P_DerpGuarding (h->listener, h->x, h->y))
	return false;
    P_MindReady (h->listener, h->x, h->y);
    return true;
}

// Shut in: it found no way out lately, and is still by home, or this noise is
// somewhere it cannot get to either. (A soldier behind E1M1's lift turned to
// every shot across the zigzag room for fifteen seconds; an imp went home and
// back to the same wall at each shot.) It turns that way and waits.
static boolean ShutIn (heard_t* h)
{
    mobj_t*	l = h->listener;

    if (!(l->nowaytic && leveltime - l->nowaytic < NOWAY_DEAF
	  && (l->mind == MIND_NONE || l->mind == MIND_RETURN || l->mind == MIND_INVESTIGATE)
	  && (P_AproxDistance (l->x - (l->spawnpoint.x<<FRACBITS), l->y - (l->spawnpoint.y<<FRACBITS)) < SHUT_HOME*FRACUNIT
	      || !P_SensesReachable (l, h->x, h->y))))
	return false;
    P_DerpRuleIf (l->mind == MIND_NONE && l->tactic != STEP_READIED, l, "none.shut_in",
		  "Shut in, no way out found: whatever it hears, it turns that way and waits");
    P_MindReady (l, h->x, h->y);
    return true;
}

// A lone crack, not near: it looks up, and that is all.
static boolean LoneCrack (heard_t* h)
{
    if (!(h->lone && h->listener->mind == MIND_NONE && h->distance > RESERVE_NEAR))
	return false;
    P_MindReady (h->listener, h->x, h->y);
    return true;
}

// An imp does not come running: it may mean to wait for him instead.
static boolean HunterWaits (heard_t* h)
{
    if (!P_HunterHeard (h->listener, h->distance))
	return false;
    P_MindReady (h->listener, h->x, h->y);
    return true;
}

// Enough of its room have gone: it holds the room, ready.
static boolean Garrison (heard_t* h)
{
    if (!(h->listener->mind == MIND_NONE && h->distance > RESERVE_NEAR && P_DerpGarrisonHolds (h->listener)))
	return false;
    P_MindReady (h->listener, h->x, h->y);
    return true;
}

// Far off, and enough are after him already: a reserve, ready, facing it.
// Near noises, and what it sees, it answers whatever the count. (A shout
// tells of him, not of the shouter: near the one shouting and far from the
// fight is far. E1M1: a shotgunner spotted him 1900 units off, and all 8
// round it set off at once, none of them "far".) It listens, too: a fight
// near enough and still going, rounds coming in on him, and it goes in to
// help whatever the count.
static boolean Reserve (heard_t* h)
{
    mobj_t*	l = h->listener;
    int		away;

    if (l->mind != MIND_NONE)
	return false;
    away = h->kind == NOISE_SHOUT && h->source
	? P_AproxDistance (l->x - h->source->knownx, l->y - h->source->knowny)>>FRACBITS
	: h->distance;
    if (away > RESERVE_NEAR && !(away <= BREACH_NEAR && P_DirectorGoIn () ? P_DirectorBreach () : P_DirectorAdmit ()))
    {
	P_MindReady (l, h->x, h->y);
	return true;
    }
    return false;
}

// It has been to that wall: whatever goes on behind it, there is no way there.
static boolean NoWayWall (heard_t* h)
{
    mobj_t*	l = h->listener;

    P_SensesGuess (h);
    return l->nowaytic && leveltime - l->nowaytic < NOWAY_DEAF && h->distance > RESERVE_NEAR
	&& P_AproxDistance (h->guessx - l->nowayx, h->guessy - l->nowayy) < NOWAY_NEAR*FRACUNIT;
}

// Already on its way to look, and this points about where it is going (each
// shot a fresh rough guess): it keeps its way rather than turn at every shot,
// only its time renewed.
static boolean OnItsWay (heard_t* h)
{
    mobj_t*	l = h->listener;

    P_SensesGuess (h);
    if (l->mind == MIND_INVESTIGATE && l->target == h->player
	&& P_AproxDistance (h->guessx - l->goalx, h->guessy - l->goaly)
	   < (h->distance/4 > 256 ? h->distance/4 : 256)*FRACUNIT)
    {
	l->goaltic = leveltime;
	return true;
    }
    return false;
}

// A noise it has not explained: it goes to look near where it seems to be.
// Up without a word, at its own next look (P_BrainLook), not here: a noise
// is made in the middle of another thing's doings (a door a monster opens
// from P_Move's loop over the lines it touched), and a hearer chasing now
// would walk, and overwrite those lines under it (a crash).
static boolean GoesToLook (heard_t* h)
{
    mobj_t*	l = h->listener;

    P_SensesGuess (h);
    if (l->mind == MIND_NONE)
    {
	// (Plain noises it says as it goes: P_WordOut.)
	if (h->kind == NOISE_FOOTSTEP)
	    P_DerpBark (l, BARK_HEARSRUN);
	else if (h->kind == NOISE_IMPACT)
	    P_DerpBark (l, BARK_BULLETNEAR);
	P_DerpCry (l, BARK_IMPHEARD);
	P_WordOut (l, !h->lone && Gunfire (h->kind), h->x, h->y);
    }
    l->target = h->player;
    P_MindBecome (l, MIND_INVESTIGATE, STEP_NONE, "any.heard",
		  "A noise it has not explained: it goes to look near it");
    l->goalx = h->guessx;
    l->goaly = h->guessy;
    l->goaltic = leveltime;
    return true;
}

// The order.
static const hearrule_t hearing[] =
{
    { "busy",		Busy },
    { "sees_him",	SeesHim },
    { "keeps_tactic",	KeepsTactic },
    { "hunting",	Hunting },
    { "guard_post",	GuardPost },
    { "none.shut_in",	ShutIn },
    { "lone_crack",	LoneCrack },
    { "hunter_waits",	HunterWaits },
    { "garrison",	Garrison },
    { "reserve",	Reserve },
    { "no_way_wall",	NoWayWall },
    { "on_its_way",	OnItsWay },
    { "any.heard",	GoesToLook },
    { NULL, NULL }
};

static void Heard (heard_t* h)
{
    mobj_t*	l = h->listener;
    const hearrule_t*	r;

    static const char*	names[NUMNOISES] = { "fist", "chainsaw", "pistol", "shotgun", "super shotgun",
	"chaingun", "rocket", "plasma", "BFG", "footsteps", "door", "death", "shout", "monster's gun", "bullet strike" };

    if (P_DerpLogging ())
	P_DerpLog (l, "hears %s %d units off (%s)", names[h->kind], h->distance,
		   l->target && l->target != h->player && l->target->health > 0 ? "busy"
		   : l->mind == MIND_HUNT && P_CheckSight (l, h->player) ? "sees him anyway"
		   : (l->mind >= MIND_COVER || P_MindLurks (l)) && l->target == h->player ? "keeps its tactic"
		   : P_SensesHunting (l) ? "keeps hunting"
		   : P_DerpGuarding (l, h->x, h->y) ? "readies at its post"
		   : "goes to look");
    // A death heard: one fewer, and if one of its own, it counts it.
    if (h->kind == NOISE_DEATH)
	P_DerpLossHeard (l, h->source);
    // Gunfire, and it knows one of its own went off: it passes that on.
    if (Gunfire (h->kind) && !h->lone)
	P_WordFight (l, h->x, h->y);
    for (r = hearing; r->rule; r++)
	if (!P_DecideOff (r->name) && r->rule (h))
	    return;
}


//
// Hurt by him.
//

void P_BrainHit (mobj_t* target, mobj_t* source)
{
    derp_timing_group previous;

    if (!P_SensesOn () || !source || !source->player || !P_DerpMonster (target))
	return;
    previous = P_DerpTimingBegin (DERP_TIMING_BRAIN);
    P_DerpShotRunning (target);
    P_HunterHit (target);
    // Shot: in the middle of a tactic it thinks again (p_tactics.h) rather
    // than dropping it; else it goes after him.
    if (target->mind >= MIND_COVER)
	target->decidetic = leveltime;
    else
    {
	if (target->mind != MIND_HUNT)
	    P_MindBecome (target, MIND_HUNT, STEP_NONE, "none+investigate+search+return.shot",
			  "Shot: it knows where from, and goes after him");
	// An imp thinks again on each blow (p_demons.h); a soldier's tactics
	// keep their own time.
	if (!P_DerpSoldier (target->type))
	    target->decidetic = leveltime;
    }
    // A demon heals only some while after its last wound.
    target->seentic = leveltime;
    // Where from: the senses' business.
    P_SensesHit (target, source);
    P_DerpTimingEnd (previous);
}


// What the senses ask the brain, set at level setup.
static const senseshooks_t hooks = { Heard, P_DirectorSteps, P_DirectorHoldBack, P_FightOn };

void P_BrainLevel (void)
{
    P_SensesHooks (&hooks);
}
