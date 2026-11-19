//
// Kept ways: a walker's long route, searched once and then followed (p_navkept.h).
//

#include <stdlib.h>

#include "doomdef.h"
#include "doomstat.h"
#include "p_local.h"
#include "p_nav.h"
#include "p_navbudget.h"
#include "p_navkept.h"

// Walkers with a long way at once. More than this and the one asked least
// lately gives its slot up: it searches again, as every walker did before.
#define KEPT_SLOTS	32
// How far from the way it may stand and still be on it, in cells (a friend's
// shove, a step round one): further, and it was pushed off, or fell, or
// jumped wrong, and searches afresh.
#define KEPT_OFF	2
// Where along the way it is looked for: a few cells back from where it was
// last (pushed back), and on past the waypoint it was given (8 cells on, and
// it may overshoot).
#define KEPT_BACK	8
#define KEPT_AHEAD	40
// And how far above or below the cell's floor, in units (a step and a half:
// the grid's floor of a lift may lag the lift by a few tics).
#define KEPT_RISE	40
// How far the goal may have moved from where the way ends, in cells, and the
// end still be patched by a short search: further, a search from scratch.
// Under KEPT_STILL it is the same place (a goal drifting inside its cell).
#define KEPT_DRIFT	24
#define KEPT_STILL	1
// The patch starts this many cells back from the end (the goal may have come
// back towards it: the patch cuts the corner, not walk out and back), and
// looks no further than this.
#define KEPT_PATCH_BACK	16
#define KEPT_PATCH	4000
// What it keeps away from (navavoid) moved by more than this, in units: a
// different search, so the way kept is not its way.
#define KEPT_AVOID	64
// No progress for a second (Walk's stall): the way kept may be what it is
// stuck on (a friend in a corridor, a lift up), so a search finds another.
#define KEPT_STUCK	TICRATE
// A goal this near the one that took the long look, in cells: the long look again.
#define LONG_NEAR	8

typedef struct
{
    int		serial;		// whose (0: a free slot)
    int		used;		// the tic it was last asked
    int		nav, gen, cellsof;
    int		n, at;		// cells kept; the one it was at last
    boolean	truncated;	// the way goes on past what was kept
    fixed_t	goalx, goaly;	// what the way ends at
    // The way it was searched: a climber's, the jump it gave up on, what it
    // kept away from.
    boolean	climb;
    int		badfrom, badto;
    int		avoidr;
    boolean	avoidhard;
    fixed_t	avoidx, avoidy;
    // The goal that last took the long look (longnav -1: none).
    int		longnav;
    fixed_t	longx, longy;
    int		cells[NAV_KEPT_CELLS];
} kept_t;

static kept_t	kept[KEPT_SLOTS];

void P_NavKeptClear (void)
{
    int		i;

    for (i = 0; i < KEPT_SLOTS; i++)
    {
	kept[i].serial = 0;
	kept[i].n = 0;
	kept[i].longnav = -1;
    }
}

// How far apart two points are, in units, the longer of the two ways (as
// cells count).
static int Apart (fixed_t ax, fixed_t ay, fixed_t bx, fixed_t by)
{
    int		dx = abs ((ax>>FRACBITS) - (bx>>FRACBITS)), dy = abs ((ay>>FRACBITS) - (by>>FRACBITS));
    return dx > dy ? dx : dy;
}

// By serial, not by pointer: a slot whose walker was freed is never found
// again, only given up when the slot is wanted.
static kept_t* Find (const mobj_t* actor)
{
    int		i;

    if (!actor->serial)
	return NULL;
    for (i = 0; i < KEPT_SLOTS; i++)
	if (kept[i].serial == actor->serial)
	    return &kept[i];
    return NULL;
}

static kept_t* Slot (const mobj_t* actor)
{
    kept_t*	k = Find (actor);
    int		i;

    if (k || !actor->serial)
	return k;
    k = &kept[0];
    for (i = 0; i < KEPT_SLOTS; i++)
    {
	if (!kept[i].serial)
	{
	    k = &kept[i];
	    break;
	}
	if (kept[i].used < k->used)
	    k = &kept[i];
    }
    k->serial = actor->serial;
    k->used = leveltime;
    k->n = 0;
    k->longnav = -1;
    return k;
}

static boolean SameSearch (const kept_t* k, int nav)
{
    if (k->nav != nav || k->climb != navclimb || k->badfrom != navbadfrom || k->badto != navbadto
	|| k->avoidr != navavoidr)
	return false;
    return !navavoidr || (k->avoidhard == navavoidhard
			  && Apart (k->avoidx, k->avoidy, navavoidx, navavoidy) <= KEPT_AVOID);
}

// A step on the grid as it is now, or a climber's jump that is still there.
// (Asking for the jump works out the cell's jumps afresh after a rebuild,
// which Walk's take-off needs for the sill's height.)
static boolean Link (int nav, int a, int b)
{
    return P_NavStep (nav, a, b) || (navclimb && P_NavClimbs (nav, a, b));
}

int P_NavKeptNext (mobj_t* actor, int nav, fixed_t gx, fixed_t gy, int* path, int max)
{
    static int	patch[NAV_KEPT_CELLS];
    kept_t*	k = Find (actor);
    int		i, best = -1, bestoff = MAXINT, lo, hi, drift, count;

    if (!k || k->n < 2)
	return 0;
    k->used = leveltime;
    drift = Apart (gx, gy, k->goalx, k->goaly);
    if (!SameSearch (k, nav) || k->cellsof != P_NavCells () || drift > KEPT_DRIFT*NAVCELL
	|| leveltime - actor->movedtic > KEPT_STUCK)
    {
	k->n = 0;
	return 0;
    }
    // Where along it it stands.
    lo = k->at - KEPT_BACK > 0 ? k->at - KEPT_BACK : 0;
    hi = k->at + KEPT_AHEAD < k->n ? k->at + KEPT_AHEAD : k->n;
    for (i = lo; i < hi; i++)
    {
	fixed_t	x, y, floor;
	int	off;
	P_NavPoint (nav, k->cells[i], &x, &y, &floor);
	off = Apart (actor->x, actor->y, x, y);
	// (Not a walkway over its head, off which it fell, nor the floor below
	// one it stands on.)
	if (abs ((floor>>FRACBITS) - (actor->floorz>>FRACBITS)) > KEPT_RISE)
	    continue;
	if (off < bestoff)
	{
	    bestoff = off;
	    best = i;
	}
    }
    if (best < 0 || bestoff > KEPT_OFF*NAVCELL + NAVCELL/2)
    {
	k->n = 0;
	return 0;
    }
    k->at = best;
    // The grid rebuilt somewhere since (a lift, a door's track, a barrel
    // moved): the steps still ahead looked at again, a few hundred at most,
    // where a search would walk tens of thousands of cells.
    if (k->gen != P_NavBuild ())
    {
	for (i = best; i + 1 < k->n; i++)
	    if (!Link (nav, k->cells[i], k->cells[i + 1]))
	    {
		k->n = 0;
		return 0;
	    }
	k->gen = P_NavBuild ();
    }
    if (k->truncated)
    {
	// Near the end of what was kept: the rest of the way is a long search
	// again (straight to the long look, P_NavKeptLong).
	if (k->n - best <= KEPT_PATCH_BACK)
	{
	    k->n = 0;
	    return 0;
	}
    }
    else if (drift > KEPT_STILL*NAVCELL)
    {
	// The goal has moved (a hunter's player): the way's end searched again,
	// from a little back along it to where the goal is now.
	int	p = k->n - 1 - KEPT_PATCH_BACK > best ? k->n - 1 - KEPT_PATCH_BACK : best, m;
	fixed_t	px, py;
	P_NavPoint (nav, k->cells[p], &px, &py, NULL);
	m = P_NavPathFor (actor, nav, px, py, gx, gy, patch, NAV_KEPT_CELLS - p, KEPT_PATCH);
	if (m == NAV_DEFERRED)
	{
	    // The tic's routes spent: on along the old way, and the patch asked
	    // for at the next waypoint; at its very end, it waits for one.
	    if (k->n - best <= 2)
		return NAV_DEFERRED;
	}
	else if (m == 0 || (patch[0] != k->cells[p] && p > best && !Link (nav, k->cells[p - 1], patch[0])))
	{
	    k->n = 0;
	    return 0;
	}
	else
	{
	    for (i = 0; i < m; i++)
		k->cells[p + i] = patch[i];
	    k->n = p + m;
	    k->truncated = k->n >= NAV_KEPT_CELLS;
	    k->goalx = gx;
	    k->goaly = gy;
	}
    }
    count = k->n - best < max ? k->n - best : max;
    for (i = 0; i < count; i++)
	path[i] = k->cells[best + i];
    return count;
}

void P_NavKeep (mobj_t* actor, int nav, fixed_t gx, fixed_t gy, const int* path, int n)
{
    kept_t*	k;
    int		i;

    if (n < 2 || navpartial || !(k = Slot (actor)))
	return;
    if (n > NAV_KEPT_CELLS)
	n = NAV_KEPT_CELLS;
    k->used = leveltime;
    k->nav = nav;
    k->gen = P_NavBuild ();
    k->cellsof = P_NavCells ();
    k->n = n;
    k->at = 0;
    k->truncated = n >= NAV_KEPT_CELLS;
    k->goalx = gx;
    k->goaly = gy;
    k->climb = navclimb;
    k->badfrom = navbadfrom;
    k->badto = navbadto;
    k->avoidr = navavoidr;
    k->avoidhard = navavoidhard;
    k->avoidx = navavoidx;
    k->avoidy = navavoidy;
    for (i = 0; i < n; i++)
	k->cells[i] = path[i];
}

boolean P_NavKeptLong (mobj_t* actor, int nav, fixed_t gx, fixed_t gy)
{
    kept_t*	k = Find (actor);

    return k && k->longnav == nav && Apart (gx, gy, k->longx, k->longy) <= LONG_NEAR*NAVCELL;
}

void P_NavKeptMarkLong (mobj_t* actor, int nav, fixed_t gx, fixed_t gy, boolean islong)
{
    kept_t*	k = islong ? Slot (actor) : Find (actor);

    if (!k)
	return;
    k->used = leveltime;
    k->longnav = islong ? nav : -1;
    k->longx = gx;
    k->longy = gy;
}

//
// Long looks that found nothing (p_navkept.h): by the walker's serial, the
// goal and the tic. Few and short-lived; the oldest note gives way.
//
#define FAILED_NOTES	32
#define FAILED_FOR	(5*TICRATE)
#define FAILED_NEAR	(128*FRACUNIT)

static struct
{
    int		serial;
    fixed_t	gx, gy;
    int		tic;
} failednotes[FAILED_NOTES];

boolean P_NavKeptFailed (mobj_t* actor, fixed_t gx, fixed_t gy)
{
    int		i;

    for (i = 0; i < FAILED_NOTES; i++)
	if (failednotes[i].tic && failednotes[i].serial == actor->serial
	    && leveltime >= failednotes[i].tic && leveltime - failednotes[i].tic < FAILED_FOR
	    && P_AproxDistance (failednotes[i].gx - gx, failednotes[i].gy - gy) < FAILED_NEAR)
	    return true;
    return false;
}

void P_NavKeptMarkFailed (mobj_t* actor, fixed_t gx, fixed_t gy)
{
    int		i, oldest = 0;

    for (i = 0; i < FAILED_NOTES; i++)
    {
	if (failednotes[i].serial == actor->serial && failednotes[i].tic)
	{
	    oldest = i;
	    break;
	}
	// (A note from a level before counts as the oldest: its tic is ahead of this one's.)
	if (failednotes[i].tic > leveltime || failednotes[i].tic < failednotes[oldest].tic)
	    oldest = i;
    }
    failednotes[oldest].serial = actor->serial;
    failednotes[oldest].gx = gx;
    failednotes[oldest].gy = gy;
    failednotes[oldest].tic = leveltime ? leveltime : 1;
}
