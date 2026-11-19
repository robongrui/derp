// The route budget: how many cells the route searches of one tic (p_nav.h's
// P_NavPath) may take between them. The game runs on the frame's thread, so
// a tic that searches 400,000 cells is a frame that hitches, though routes
// cost only a few percent of a tic on average. A search that can wait (the
// monster keeps its old plan and asks again on its next step) waits once the
// tic's cells are spent. One that cannot always runs: a short one, any inside
// a decision already let through (up to a near monster's budget), level setup,
// and anything between tics (the tests, which ask for their own routes).
//
// And the tic phases: the AI's work done every 8 tics, each system on a tic
// of its own and each monster's on its own, so they do not all land on one.
#ifndef __P_NAVBUDGET__
#define __P_NAVBUDGET__

#include "doomtype.h"
#include "m_fixed.h"
#include "p_mobj.h"

// What P_NavPathFor answers when the search waits: none ran, so this is
// neither a way nor no way. (0 is no way.)
#define NAV_DEFERRED		(-1)

// Cells a tic; for a monster near him (he would see it hesitate), twice that.
#define NAV_BUDGET		15000
#define NAV_BUDGET_NEAR		30000
// A search with a limit this small always runs.
#define NAV_BUDGET_SMALL	300

// The tic of each 8 that a system's every-8-tics work falls on.
#define PHASE_NAV		0	// p_nav.c: moved sectors, blockers
#define PHASE_LOOK		2	// p_senses.c: the searchers' look round
#define PHASE_STEPS		4	// p_senses.c: his running footsteps heard
#define PHASE_DIRECTOR		6	// p_director.c
#define P_PHASE(p)		((leveltime & 7) == (p))
// A monster's own tic of each 8, by its number.
#define P_PHASE_OF(mo)		(((leveltime + (mo)->serial) & 7) == 0)

// Once a tic, before anything searches (the top of P_Ticker): the tic's cells afresh.
void P_NavBudgetTic (void);
// The cells a search took (P_NavPath, P_NavFlood), counted against the tic's.
void P_NavBudgetSpend (int cells);
// Whether a search for actor that can wait would wait now (actor NULL: no
// monster's, the plain budget).
boolean P_NavBudgetSpent (mobj_t* actor);
// The cells left of the plain budget this tic (0 when spent); MAXINT when
// nothing waits (NAVBUDGETOFF, the level's first tic, between tics).
int P_NavBudgetLeft (void);
// Around a decision let through: every search in it runs, until the tic has
// spent NAV_BUDGET_NEAR; past that, those that can wait wait.
void P_NavBudgetHold (void);
void P_NavBudgetRelease (void);
// P_NavPath for actor, or NAV_DEFERRED if the limit is over NAV_BUDGET_SMALL
// and the tic's cells are spent. Then navexhausted and navwalk say nothing.
int P_NavPathFor (mobj_t* actor, int nav, fixed_t sx, fixed_t sy, fixed_t tx, fixed_t ty,
		  int* path, int max, int limit);

// When a monster stirred by something it must answer thinks: within spread
// tics, by its number, so a squad stirred by one shot does not think as one
// (or at its own next decision, if that is sooner).
int P_DecideSoon (const mobj_t* actor, int spread);
// Its next decision every tics on, on its own tic of each 8 (when every is a
// multiple of 8: else just every tics on).
int P_DecideNext (const mobj_t* actor, int every);

#endif
