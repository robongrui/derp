//
// The kinds of monster and their tables of decisions (p_kinds.h).
//

#include "doomdef.h"
#include "doomstat.h"
#include "p_local.h"
#include "p_derp.h"
#include "p_decide.h"
#include "p_kinds.h"
#include "p_navbudget.h"
#include "p_tune.h"

#define DECIDE_EVERY	TUNE (KINDS_DECIDE_EVERY, 24)

kind_t P_KindOf (mobjtype_t type)
{
    switch (type)
    {
      case MT_POSSESSED: case MT_SHOTGUY: case MT_CHAINGUY: case MT_WOLFSS: return KIND_SOLDIER;
      case MT_TROOP:				return KIND_IMP;
      case MT_SERGEANT: case MT_SHADOWS:	return KIND_PINKY;
      case MT_SKULL:				return KIND_LOSTSOUL;
      case MT_HEAD:				return KIND_CACO;
      case MT_BRUISER: case MT_KNIGHT:		return KIND_BARON;
      case MT_BABY:				return KIND_ARACHNOTRON;
      case MT_PAIN:				return KIND_PAIN;
      case MT_UNDEAD:				return KIND_REVENANT;
      case MT_FATSO:				return KIND_MANCUBUS;
      case MT_VILE:				return KIND_ARCHVILE;
      case MT_CYBORG:				return KIND_CYBERDEMON;
      case MT_SPIDER:				return KIND_MASTERMIND;
      default:					return KIND_NONE;
    }
}

static const char* names[NUMKINDS] = { "none", "soldier", "imp", "pinky", "lost soul", "caco", "baron",
				       "arachnotron", "pain elemental", "revenant", "mancubus", "arch-vile",
				       "cyberdemon", "mastermind" };

const char* P_KindName (kind_t kind)
{
    return kind >= 0 && kind < NUMKINDS ? names[kind] : "?";
}

//
// The tables. Each kind's rules go here, in the order they are asked; a rule
// is a static function above its table (see p_tactics.c's for the shape).
// Empty so far: the kind plays as vanilla, with the senses, the navigation
// and the director it already has.
//

// The lost soul: vanilla's charge. Flies, so the navigation has no layer for it yet.
static const decision_t lostsoul[] =
{
    { NULL, NULL }
};

// The cacodemon: vanilla's float and bite. Flies, as the lost soul.
static const decision_t caco[] =
{
    { NULL, NULL }
};

// The baron and the hell knight: vanilla's walk and fireball.
static const decision_t baron[] =
{
    { NULL, NULL }
};

// The arachnotron: vanilla's plasma.
static const decision_t arachnotron[] =
{
    { NULL, NULL }
};

// The pain elemental: vanilla's spitting of souls. Flies.
static const decision_t pain[] =
{
    { NULL, NULL }
};

// The revenant: vanilla's punch and homing rocket.
static const decision_t revenant[] =
{
    { NULL, NULL }
};

// The mancubus: vanilla's volleys.
static const decision_t mancubus[] =
{
    { NULL, NULL }
};

// The arch-vile: vanilla's fire and raising of the dead (P_DerpRaised clears
// what the raised one knew).
static const decision_t archvile[] =
{
    { NULL, NULL }
};

// The cyberdemon: vanilla's rockets. Never a silent death.
static const decision_t cyberdemon[] =
{
    { NULL, NULL }
};

// The spider mastermind: vanilla's chaingun. Never a silent death.
static const decision_t mastermind[] =
{
    { NULL, NULL }
};

// By kind. NULL: the kind decides in a file of its own (or not at all).
static const decision_t* const tables[NUMKINDS] =
{
    NULL,		// none
    NULL,		// soldier: p_tactics.c
    NULL,		// imp: p_demons.c
    NULL,		// pinky: p_charge.c
    lostsoul, caco, baron, arachnotron, pain, revenant, mancubus, archvile, cyberdemon, mastermind,
};

boolean P_KindThink (mobj_t* actor, boolean visible)
{
    const decision_t*	rules = tables[P_KindOf (actor->type)];
    decide_t		c;
    boolean		acted;

    if (!rules || !P_DerpAI () || leveltime < actor->decidetic)
	return false;
    // (As a soldier's, p_tactics.c: the tic's routes spent, it thinks next tic;
    // let through, its rules' routes run, up to a near monster's tic of them.)
    if (P_NavBudgetSpent (actor))
    {
	actor->decidetic = leveltime + 1;
	return false;
    }
    actor->decidetic = P_DecideNext (actor, DECIDE_EVERY);
    P_NavBudgetHold ();
    acted = P_DecideBegin (actor, visible, &c) && P_Decide (actor, rules, &c);
    P_NavBudgetRelease ();
    return acted;
}
