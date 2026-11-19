// What a DERP monster has in mind, and the one door it changes through.
//
// A monster's mind (mobj_t.mind) is what it is doing about the player: nothing,
// going to look, hunting, searching, going home, or one of the soldiers' or imps'
// tactics. Its step (mobj_t.tactic) is where in that it is. Every change of
// either goes through P_MindBecome or P_MindStep, nowhere else: they clear what
// a change clears, and carry the rule that made it, so the log says why and the
// AI graph (devtools/ai_graph) reads the transition out of the call itself.
//
//   P_MindBecome (actor, MIND_COVER, STEP_COVER_GOING, "any.under_fire",
//                 "Under fire and afraid enough: to a cover");
//   P_MindStep (actor, STEP_COVER_DUCKED, "cover/going.in_cover", "There: it ducks");
//
//   rule:  "from.what". from is what it leaves: a mind, mind/step, several joined
//          by +, or any. NULL where the rule stands at the caller (a helper).
//   words: the rule in plain words, for the graph; they never reach the game. A
//          [tag] at the end says who it is for: all, soldier, imp, pinky, rifle,
//          assault, lane.
// A place the code passes every tic guards the call (if (mind != X) ...): only a
// real change counts.

#ifndef __P_MIND__
#define __P_MIND__

#include "doomtype.h"
#include "p_mobj.h"

// A monster's state of mind (mobj_t.mind).
enum
{
    MIND_NONE,		// unaware, or vanilla
    MIND_INVESTIGATE,	// heard something: going to look
    MIND_HUNT,		// saw the player: after him, or where he last was
    MIND_SEARCH,	// lost him: looking round
    MIND_RETURN,	// gave up: going home
    // The soldiers' tactics (p_tactics.h).
    MIND_COVER,		// under fire: ducking, peeking, firing
    MIND_RETREAT,	// badly hurt: out of sight
    MIND_AMBUSH,	// friends heard dying: waiting beside a doorway
    MIND_REGROUP,	// alone: off to another squad
    // The imps' (p_demons.h).
    MIND_HEAL,		// badly hurt: out of sight, healing
    MIND_FLANK,		// a blow landed: away, to come again from elsewhere
    NUMMINDS
};

// The step of its mind it is at (mobj_t.tactic). One list for every mind and
// kind, each value its own, so a step left over from one mind can never read
// as a step of another. The numbers are fixed: the log prints them and the AI
// graph reads them from here.
typedef enum
{
    STEP_NONE = 0,		// the mind's first or only step
    // MIND_NONE, a soldier
    STEP_READIED = 1,		// a far noise heard: turned that way, a post to take
    // MIND_SEARCH, anyone
    STEP_LOOKING = 2,		// where he was and nothing: looking one way and the other
    // MIND_HUNT, a soldier
    STEP_BOUNDING = 3,		// to a new firing spot while its team covers it
    STEP_PUSHING = 4,		// running at him, fire held until in range
    STEP_ROUNDING = 5,		// another way in, round a held door
    // MIND_HUNT, an imp
    STEP_CIRCLING = 6,		// round him the long way, angles growing
    STEP_CIRCLING_BACK = 7,	// the other way
    STEP_SHOOTING = 8,		// the pack's first, holding him while one goes round
    // MIND_COVER
    STEP_COVER_GOING = 9,
    STEP_COVER_DUCKED = 10,
    STEP_COVER_LEANING = 11,
    STEP_COVER_WATCHING = 12,
    STEP_COVER_BACK = 13,	// from the lean, back in
    STEP_COVER_SEEN = 14,	// he has come round it: a beat to take that in
    // MIND_RETREAT
    STEP_BAIT = 15,		// a feigned retreat past a friend
    // MIND_AMBUSH
    STEP_WAITING = 16,		// in place, facing the way in
    // MIND_FLANK
    STEP_ROUND_CHOKE = 17,	// round a doorway he holds
    NUMSTEPS
} step_t;

// An imp lying in wait (mobj_t.lurk, p_hunters.c): what it is at.
typedef enum
{
    LURK_NONE = 0,
    LURK_WOKEN = 1,		// decided to, not up yet
    LURK_GOING = 2,		// on its way to a place to wait
    LURK_WAITING = 3,
    LURK_STALKING = 4,
    LURK_SLIPPING = 5		// waiting, off to a new place: he was about to see the old one
} lurk_t;

// Questions of its state, for whoever needs them without the file that sets
// it: on a bound; pushing in on him; going round a held door (a soldier); running round him (an imp of
// the pack, this second, and not stalled); lying in wait at all, waiting
// rather than stalking, stalking.
boolean P_MindBounding (const mobj_t* mo);
boolean P_MindPushing (const mobj_t* mo);
boolean P_MindRounding (const mobj_t* mo);
boolean P_MindRunningRound (const mobj_t* mo);
boolean P_MindLurks (const mobj_t* mo);
boolean P_MindLurkWaits (const mobj_t* mo);
boolean P_MindStalks (const mobj_t* mo);
// Its lying in wait is over (why, for the log): at him.
void P_MindLurkOver (mobj_t* mo, const char* why);
// How alert it is (0-2), raised to level, and since when.
void P_MindAlert (mobj_t* mo, int level);
// A far fight heard at (x, y): alert, and unaware it turns that way, ready, a
// post to take at its own next look (P_DerpGuardPost).
void P_MindReady (mobj_t* mo, fixed_t x, fixed_t y);

// What a change of mind leaves behind, for the file that knows (the tactics:
// a soldier's knee, its low wall, its post); set at level setup.
void P_MindLeaving (void (*left) (mobj_t* actor, boolean gone));
// A change of mind: to mind at step, by rule. Clears the step's time, the route
// and, through P_MindLeaving, what a soldier leaves behind in its cover.
#define P_MindBecome(actor, mind, step, rule, words) P_MindChange (actor, mind, step, rule)
// A change of step within its mind, by rule. Nothing else changes.
#define P_MindStep(actor, step, rule, words) P_MindStepTo (actor, step, rule)
void P_MindChange (mobj_t* actor, int mind, int step, const char* rule);
void P_MindStepTo (mobj_t* actor, int step, const char* rule);
// Bookkeeping, not a rule: a load or a raise leaves it with nothing in mind.
void P_MindReset (mobj_t* actor);

// Their names, for the log and the tools.
const char* P_MindName (int mind);
const char* P_StepName (int step);

#endif
