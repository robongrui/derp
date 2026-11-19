//
// The route budget and the tic phases (p_navbudget.h).
//

#include <stdio.h>
#include <stdlib.h>

#include "doomdef.h"
#include "doomstat.h"
#include "p_local.h"
#include "p_derp.h"
#include "p_nav.h"
#include "p_navbudget.h"

// Near him, or seen him lately: a monster whose hesitation would show.
#define NEAR_HIM	1024
#define SEEN_LATELY	(3*TICRATE)

// The tic the cells are counted for. Between tics (after leveltime moves on,
// before the next P_Ticker) nothing is counted and nothing waits.
static int	budgettic = -1;
static int	spent;
static int	held;

// NAVBUDGETDEBUG=1: each tic whose own searches (not a test's between tics)
// took over 20,000 cells, to stderr. NAVBUDGETOFF=1: nothing ever waits, to
// measure against.
static int	debug = -1, off;

void P_NavBudgetTic (void)
{
    if (debug < 0)
    {
	debug = getenv ("NAVBUDGETDEBUG") != NULL;
	off = getenv ("NAVBUDGETOFF") != NULL;
    }
    if (debug && budgettic >= 0 && spent > 20000)
	fprintf (stderr, "NAVBUDGET tic %d: %d cells\n", budgettic, spent);
    budgettic = leveltime;
    spent = 0;
    held = 0;
}

void P_NavBudgetSpend (int cells)
{
    if (budgettic == leveltime)
	spent += cells;
}

static boolean Near (mobj_t* actor)
{
    mobj_t*	him = P_DerpPlayer ();

    if (!actor || !him)
	return false;
    return P_AproxDistance (him->x - actor->x, him->y - actor->y) < NEAR_HIM*FRACUNIT
	|| (actor->sighttic && leveltime - actor->sighttic < SEEN_LATELY);
}

boolean P_NavBudgetSpent (mobj_t* actor)
{
    // (The level's first tic is the setup's own: what it asks, it gets.)
    if (off || budgettic != leveltime || leveltime == 0)
	return false;
    // Inside a decision let through, a near monster's whole tic of cells, and then
    // what can wait waits there too (the rule goes without: no way round this time,
    // the old fire position kept). Its searches that cannot wait run regardless.
    if (held)
	return spent >= NAV_BUDGET_NEAR;
    return spent >= (Near (actor) ? NAV_BUDGET_NEAR : NAV_BUDGET);
}

int P_NavBudgetLeft (void)
{
    if (off || budgettic != leveltime || leveltime == 0)
	return MAXINT;
    return spent < NAV_BUDGET ? NAV_BUDGET - spent : 0;
}

void P_NavBudgetHold (void)
{
    held++;
}

void P_NavBudgetRelease (void)
{
    if (held > 0)
	held--;
}

int P_NavPathFor (mobj_t* actor, int nav, fixed_t sx, fixed_t sy, fixed_t tx, fixed_t ty,
		  int* path, int max, int limit)
{
    if (limit > NAV_BUDGET_SMALL && P_NavBudgetSpent (actor))
	return NAV_DEFERRED;
    return P_NavPath (nav, sx, sy, tx, ty, path, max, limit);
}

int P_DecideSoon (const mobj_t* actor, int spread)
{
    int		soon = spread > 1 ? leveltime + (int)((unsigned)actor->serial % (unsigned)spread) : leveltime;

    // (Never later than it would have thought anyway.)
    return actor->decidetic < soon ? actor->decidetic : soon;
}

int P_DecideNext (const mobj_t* actor, int every)
{
    int		next = leveltime + every;

    // Back to its own tic of the 8, so those that thought together drift apart
    // once and stay apart; the gap stays every tics from then on.
    if (every >= 8 && !(every & 7))
	next -= (next + actor->serial) & 7;
    return next > leveltime ? next : leveltime + 1;
}
