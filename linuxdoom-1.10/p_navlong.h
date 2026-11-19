// Long searches that carry over tics. The game runs on the frame's thread, so
// one route search of 40,000 cells (a walker's longer look) is about 8 ms in
// one tic, and a pair of imps setting out on a pincer searched up to 120,000
// cells four times over. Here such a search is owned by its monster (by serial
// and what it is for), walks part of the way each tic, and pauses: until it is
// done its monster hears NAV_DEFERRED and goes on as it was, as with any
// search that waits. The search is P_NavPath's own, step by step and in the
// same order (P_NavExpand), so what it finds is what P_NavPath would have
// found at the tic it started; it only arrives a few tics later.
//
// Searches up to the short look's size stay with P_NavPath: one tic, as before.
#ifndef __P_NAVLONG__
#define __P_NAVLONG__

#include "doomtype.h"
#include "m_fixed.h"
#include "p_mobj.h"

// What a long search is for: a monster may have one of each.
enum
{
    NAVLONG_WALK,		// Walk's longer look (p_senses.c)
    NAVLONG_PAIRMATE,		// a pincer: the partner's way to him (p_pincer.c)
    NAVLONG_PAIR,		// a pincer: its own way round
    NAVLONG_MEASUREMATE,	// a pincer's look each second: the partner's way
    NAVLONG_MEASURE,		// and its own
    NAVLONG_WAYIN,		// his way in, for an imp on the ceiling (p_hang.c)
    NAVLONG_WALLED		// whether a demon is walled off from him (p_demons.c)
};

// At level load and every full rebuild: nothing pending.
void P_NavLongClear (void);
// Once a tic, after the grid's rebuilds (P_Ticker): the pending searches walk
// on, nearest him first, sharing the tic's cells; those not asked about for a
// while are dropped.
void P_NavLongTic (void);
// P_NavPath for owner, as one of its long searches (tag), with navclimb and the
// rest set around the call as for P_NavPath. NAV_DEFERRED while it is not done
// (asked again with about the same start and goal it goes on; with another, it
// starts again); then what P_NavPath answers, and navexhausted, navwalk as it
// sets them. Between tics, on the level's first and with NAVBUDGETOFF: P_NavPath.
int P_NavLongPath (mobj_t* owner, int tag, int nav, fixed_t sx, fixed_t sy, fixed_t tx, fixed_t ty,
		   int* path, int max, int limit);
// Whether owner has that search pending (or done and not yet asked for).
boolean P_NavLongPending (mobj_t* owner, int tag);
// After an answer: the cells it took, and whether it started on an earlier tic
// (then its owner has moved on since, and its way starts where it stood then).
extern int	navlongcells;
extern boolean	navlonglate;
// A late way (path, n cells): cut to start at the cell by actor, if it is near
// one of its first; the count left.
int P_NavLongCatchUp (mobj_t* actor, int nav, int* path, int n);

// From p_nav.c, which holds the grid: for the search here only.
// The grid's width in cells.
int P_NavWidth (void);
// P_NavPath's step from cell: the cells it goes on to and what each adds to the
// way's cost, in P_NavPath's order (next and add room for 16); their count.
int P_NavExpand (int nav, int cell, int* next, int* add);
// P_NavPath's early answer from what earlier searches found reachable. Loose:
// also from a set smaller than limit that holds start but not goal, though
// start is not where it was walked from (no way, and not run out of cells:
// a walker shut in some 24,000 cells asked the 40,000 look every 5 seconds).
boolean P_NavKnownUnreachable (int nav, int start, int goal, int limit, boolean loose);
// Keeping what a search reached, as P_NavPath keeps it: the set to fill (NULL:
// none kept yet), then its size; or, for one that ran out, the flood from start.
byte* P_NavReachOpen (void);
void P_NavReachClose (int nav, int start, int size);
void P_NavReachFlood (int nav, int start);

#endif
