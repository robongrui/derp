// The kinds of monster, and a table of decisions for each (p_decide.h).
//
// The soldiers (zombieman, shotgunner, chaingunner, the SS) decide in
// p_tactics.c and the imps in p_demons.c, the two kinds with rules of their
// own so far; the pinkies' charge is p_charge.c. Every other kind of Doom's and
// Doom 2's has its table here, empty: it plays as vanilla until a rule is
// written, and then the rule is a function and a line in that table, with no
// new plumbing. The brain (p_brain.c) asks the kind's table each DECIDE_EVERY.

#ifndef __P_KINDS__
#define __P_KINDS__

#include "doomtype.h"
#include "info.h"
#include "p_mobj.h"

typedef enum
{
    KIND_NONE,		// not a monster of the AI's (Keen, the boss brain)
    KIND_SOLDIER,	// zombieman, shotgunner, chaingunner, SS (p_tactics.c)
    KIND_IMP,		// (p_demons.c)
    KIND_PINKY,		// demon and spectre (p_charge.c)
    KIND_LOSTSOUL,
    KIND_CACO,
    KIND_BARON,		// baron of hell and hell knight
    KIND_ARACHNOTRON,
    KIND_PAIN,		// pain elemental
    KIND_REVENANT,
    KIND_MANCUBUS,
    KIND_ARCHVILE,
    KIND_CYBERDEMON,
    KIND_MASTERMIND,
    NUMKINDS
} kind_t;

kind_t P_KindOf (mobjtype_t type);
const char* P_KindName (kind_t kind);

// From the brain, each tic of a chase, after the soldiers' and the imps' own
// ways: the kind's table, every DECIDE_EVERY. True if a rule acted.
boolean P_KindThink (mobj_t* actor, boolean visible);

#endif
