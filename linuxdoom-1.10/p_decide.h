// A monster's decisions: rules asked in order, the first that acts has it.
//
// Every kind of monster decides the same way, every DECIDE_EVERY tics: what
// each rule needs is worked out once into a decide_t, then the kind's table of
// rules is asked top to bottom, and the first that acts ends it. The table is
// the only place the order lives, so a new rule is a function and a line in
// the table, where it belongs in the order, and nothing else. A rule is a
// function of (actor, decide_t*) that returns true if it acted; it makes its
// own P_MindBecome or P_MindStep (p_mind.h) with its rule name and words.
//
// The soldiers' table is in p_tactics.c, the imps' in p_demons.c, and every
// other kind's in p_kinds.c, empty where vanilla still rules. In a dev build
// (DOOM_TUNE) a rule is switched off when devtools/ai_graph/tune.txt names
// its table entry with a 1 ("hunt.bait 1"): a bot run without it.

#ifndef __P_DECIDE__
#define __P_DECIDE__

#include "doomtype.h"
#include "p_mobj.h"

// What every rule is told.
typedef struct
{
    mobj_t*	player;		// him, alive
    boolean	visible;	// in its sight now
    int		health;		// of its full, in percent
    int		fear;		// soldiers: losses heard, wounds, alone, against its nerve
    int		friends;	// soldiers: fellow soldiers near
    int		duty;		// what holds it, DUTY_*
} decide_t;

typedef boolean (*rule_t) (mobj_t* actor, decide_t* c);

// A line of a table: the name it is switched off by (the rule it fires, or a
// short name for a pair, a gate or the barks), and the function. A table ends
// in { NULL, NULL }.
typedef struct
{
    const char*	name;
    rule_t	rule;
} decision_t;

// What holds it, worked out once before any rule: a key or switch it guards
// (it keeps to it), the way out behind him it holds (until holdtic: no bound
// or push), and the director saying go in on the fight (no waiting at a
// choke). Rules read these; none asks the guard list or the director itself.
#define DUTY_GUARD	1
#define DUTY_HOLDS	2
#define DUTY_GOIN	4
int P_DecideDuty (mobj_t* actor);

// The common part of a decide_t: him, sight, health, duty; fear and friends
// are 0 until the kind fills them. False if there is no player to decide about.
boolean P_DecideBegin (mobj_t* actor, boolean visible, decide_t* c);
// Asks the rules in order: true if one acted.
boolean P_Decide (mobj_t* actor, const decision_t* rules, decide_t* c);
// Whether a rule is switched off by its table name (a dev build, tune.txt).
// The brain's tables of other shapes ask this too.
boolean P_DecideOff (const char* name);

#endif
