// What DERP's monsters do, a line each, for checking them against a game
// played (-derplog <file>): what they spot and hear, how their minds and
// tactics change, their wounds, kneels, sprints, silent deaths, bodies
// found, nudges, pounces. Off without the option: one test a call.

#ifndef __P_DERPLOG__
#define __P_DERPLOG__

#include "doomtype.h"
#include "p_mobj.h"

// Whether -derplog is on (asked once).
boolean P_DerpLogging (void);
// mo did something (printf-style); mo may be NULL for the level's own.
void P_DerpLog (mobj_t* mo, const char* fmt, ...);
// Each tic for each monster: changes of mind, tactic, kneel, pounce, health.
void P_DerpLogThink (mobj_t* mo);
// At level setup: a line for the map, and what each monster starts as.
void P_DerpLogLevel (void);
// A monster about to be hurt by another: who, how much, and how it was firing.
void P_DerpLogHit (mobj_t* target, mobj_t* source, int damage);
// A soldier sprinting (P_DerpSprint): a line a second at most.
void P_DerpLogSprint (mobj_t* mo);

// A named rule moved mo on: written just before the code that does it, and logged
// as "rule <name>". Off without -derplog: one flag, and the words never reach the
// game at all: they are there for the AI graph (devtools/ai_graph), which reads
// these calls out of the source. So what it shows comes from here.
//   name:  "from.what". from is what it leaves: a mind, mind/step, several joined
//          by +, or any. Where it goes is the next thing the code does.
//   words: the rule in plain words. A [tag] at the end says who it is for, where
//          that is not the file's own: all, soldier, imp, pinky, rifle, assault, lane.
// The If form is for a place the code passes every tic: only a real change counts.
extern boolean derplogon;
void P_DerpRuleFired (mobj_t* mo, const char* name);
#define P_DerpRule(mo, name, words) \
    do { if (derplogon) P_DerpRuleFired (mo, name); } while (0)
#define P_DerpRuleIf(cond, mo, name, words) \
    do { if (derplogon && (cond)) P_DerpRuleFired (mo, name); } while (0)

#endif
