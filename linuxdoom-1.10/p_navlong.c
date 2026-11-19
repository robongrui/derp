//
// Long searches that carry over tics (p_navlong.h).
//

#include <stdlib.h>
#include <string.h>

#include "doomdef.h"
#include "doomstat.h"
#include "i_system.h"
#include "p_local.h"
#include "p_derp.h"
#include "p_derptiming.h"
#include "p_nav.h"
#include "p_navbudget.h"
#include "p_navlong.h"

// Searches pending at once. More than this and a new one waits for a slot
// (its monster goes on as it was), or takes one nobody has asked about for a
// second.
#define LONG_SLOTS	8
#define LONG_EVICT	TICRATE
// Cells a tic the pending searches walk between them, at the tic's start; at
// least LONG_LEAST of them even when his own think has spent the tic's budget
// (a search must get somewhere). And what one may walk on when its monster
// asks, out of what the tic has left: most long looks end inside that, in the
// tic they were asked, as they did before.
#define LONG_TIC	8000
#define LONG_LEAST	4000
#define LONG_FIRST	12000
// Not asked about for this long: its monster has moved on to something else.
#define LONG_IDLE	(2*TICRATE)
// Asked again from this near where it started, to this near where it was
// going (cells), it is the same search: its monster walks on its old plan
// while it waits, and a goal that is him moves. Further: a search afresh.
// (KEPT_DRIFT's 24: a kept way's end is patched as far as that.)
#define LONG_MOVED	8
#define LONG_DRIFT	24
// A late way is caught up from its owner's cell among its first this many,
// this far off at most (units) and on the same floor.
#define CATCH_AHEAD	40
#define CATCH_OFF	40
#define CATCH_RISE	40

// The cells a search has touched: an open-addressed map from cell to its cost
// and parent, where P_NavPath keeps arrays the size of the grid (up to some
// 450,000 cells). Sized for twice the search's limit, so it rarely grows.
typedef struct
{
    int		serial, tag;	// whose and what for (serial 0: a free slot)
    int		asked, began;	// tics: last asked about, started
    fixed_t	ownerx, ownery;	// where its monster was when last asked
    // What was asked: P_NavPath's arguments and the globals set round it.
    int		nav, start, goal, limit;
    boolean	climb, partial, avoidhard;
    int		badfrom, badto, avoidr;
    fixed_t	avoidx, avoidy;
    // The grid it is walking (P_NavBuild, P_NavCells); restarted once when it
    // changed, then walked on as it is (a lift that never rests would starve
    // it) and not kept as a reach set.
    int		gen, cellsof, restarts;
    boolean	mixed;
    // Where it is.
    int		searched, nearest, least;
    boolean	done, found, exhausted;
    int*	key;
    int*	cost;
    int*	parent;
    int		cap, count;
    int*	heap;
    int*	heapcost;
    int		heapsize, heapmax;
} slot_t;

static slot_t	slots[LONG_SLOTS];
static int	width;

int		navlongcells;
boolean		navlonglate;

void P_NavLongClear (void)
{
    int		i;

    for (i = 0; i < LONG_SLOTS; i++)
	slots[i].serial = 0;
}

static int Guess (int a, int b)
{
    int		dx = abs (a % width - b % width), dy = abs (a / width - b / width);
    return dx < dy ? 14*dx + 10*(dy - dx) : 14*dy + 10*(dx - dy);
}

static int Apart (int a, int b)
{
    int		dx = abs (a % width - b % width), dy = abs (a / width - b / width);
    return dx > dy ? dx : dy;
}

//
// The map.
//
static int Find (const slot_t* s, int cell)
{
    unsigned	h = (unsigned)cell*2654435761u;
    int		i = (int)((h ^ (h >> 15)) & (unsigned)(s->cap - 1));

    while (s->key[i] != -1 && s->key[i] != cell)
	i = (i + 1) & (s->cap - 1);
    return i;
}

static void Alloc (slot_t* s, int cap)
{
    s->key = malloc (sizeof *s->key*cap);
    s->cost = malloc (sizeof *s->cost*cap);
    s->parent = malloc (sizeof *s->parent*cap);
    if (!s->key || !s->cost || !s->parent)
	I_Error ("P_NavLong: out of memory");
    s->cap = cap;
    memset (s->key, 0xff, sizeof *s->key*cap);
    s->count = 0;
}

static void Grow (slot_t* s)
{
    int*	key = s->key;
    int*	cost = s->cost;
    int*	parent = s->parent;
    int		cap = s->cap, i;

    Alloc (s, 2*cap);
    for (i = 0; i < cap; i++)
	if (key[i] != -1)
	{
	    int	j = Find (s, key[i]);
	    s->key[j] = key[i];
	    s->cost[j] = cost[i];
	    s->parent[j] = parent[i];
	    s->count++;
	}
    free (key);
    free (cost);
    free (parent);
}

// The entry for a cell not in the map yet (j: where Find said it goes).
static int Add (slot_t* s, int j, int cell)
{
    if (10*(s->count + 1) > 7*s->cap)
    {
	Grow (s);
	j = Find (s, cell);
    }
    s->key[j] = cell;
    s->count++;
    return j;
}

//
// The open list: P_NavPath's heap, the same pushes and pops, so the same order.
//
static void Push (slot_t* s, int cell, int f)
{
    int		i;

    if (s->heapsize == s->heapmax)
    {
	s->heapmax = s->heapmax ? 2*s->heapmax : 4096;
	s->heap = realloc (s->heap, sizeof *s->heap*s->heapmax);
	s->heapcost = realloc (s->heapcost, sizeof *s->heapcost*s->heapmax);
	if (!s->heap || !s->heapcost)
	    I_Error ("P_NavLong: out of memory");
    }
    i = s->heapsize++;
    while (i > 0 && s->heapcost[(i - 1)/2] > f)
    {
	s->heap[i] = s->heap[(i - 1)/2];
	s->heapcost[i] = s->heapcost[(i - 1)/2];
	i = (i - 1)/2;
    }
    s->heap[i] = cell;
    s->heapcost[i] = f;
}

static int Pop (slot_t* s)
{
    int		top = s->heap[0], cell, f, i = 0;

    cell = s->heap[--s->heapsize];
    f = s->heapcost[s->heapsize];
    for (;;)
    {
	int	child = 2*i + 1;
	if (child >= s->heapsize)
	    break;
	if (child + 1 < s->heapsize && s->heapcost[child + 1] < s->heapcost[child])
	    child++;
	if (s->heapcost[child] >= f)
	    break;
	s->heap[i] = s->heap[child];
	s->heapcost[i] = s->heapcost[child];
	i = child;
    }
    s->heap[i] = cell;
    s->heapcost[i] = f;
    return top;
}

//
// The search.
//
static void Start (slot_t* s)
{
    int		want = 4096, j;

    while (want < 2*s->limit && want < (1 << 22))
	want <<= 1;
    if (s->cap < want)
    {
	free (s->key);
	free (s->cost);
	free (s->parent);
	Alloc (s, want);
    }
    else
    {
	memset (s->key, 0xff, sizeof *s->key*s->cap);
	s->count = 0;
    }
    s->gen = P_NavBuild ();
    s->cellsof = P_NavCells ();
    s->mixed = false;
    s->done = s->found = s->exhausted = false;
    s->searched = 0;
    s->heapsize = 0;
    j = Add (s, Find (s, s->start), s->start);
    s->cost[j] = 0;
    s->parent[j] = -1;
    Push (s, s->start, Guess (s->start, s->goal));
    s->nearest = s->start;
    s->least = Guess (s->start, s->goal);
}

// The globals P_NavPath reads, set as they were when it was asked; the
// caller's put back after.
typedef struct
{
    boolean	climb, partial, avoidhard;
    int		badfrom, badto, avoidr;
    fixed_t	avoidx, avoidy;
} asked_t;

static void SetGlobals (const slot_t* s, asked_t* was)
{
    was->climb = navclimb;
    was->partial = navpartial;
    was->badfrom = navbadfrom;
    was->badto = navbadto;
    was->avoidr = navavoidr;
    was->avoidhard = navavoidhard;
    was->avoidx = navavoidx;
    was->avoidy = navavoidy;
    navclimb = s->climb;
    navpartial = s->partial;
    navbadfrom = s->badfrom;
    navbadto = s->badto;
    navavoidr = s->avoidr;
    navavoidhard = s->avoidhard;
    navavoidx = s->avoidx;
    navavoidy = s->avoidy;
}

static void PutGlobals (const asked_t* was)
{
    navclimb = was->climb;
    navpartial = was->partial;
    navbadfrom = was->badfrom;
    navbadto = was->badto;
    navavoidr = was->avoidr;
    navavoidhard = was->avoidhard;
    navavoidx = was->avoidx;
    navavoidy = was->avoidy;
}

// Done: kept as P_NavPath keeps what it reached (globals set as asked).
static void Finish (slot_t* s)
{
    s->done = true;
    s->found = s->key[Find (s, s->goal)] == s->goal;
    s->exhausted = !s->found && s->heapsize && s->searched >= s->limit;
    if (s->mixed)
	return;
    if (!s->found && !s->exhausted)
    {
	byte*	in = P_NavReachOpen ();
	int	i, size = 0;
	if (!in)
	    return;
	for (i = 0; i < s->cap; i++)
	    if (s->key[i] != -1)
	    {
		in[s->key[i]] = 1;
		size++;
	    }
	P_NavReachClose (s->nav, s->start, size);
    }
    else if (s->exhausted)
	P_NavReachFlood (s->nav, s->start);
}

// Up to most cells further; the cells it took.
static int Run (slot_t* s, int most)
{
    static int	next[16], add[16];
    asked_t	was;
    int		used = 0;
    boolean	there = false;
    derp_timing_group previous;

    if (s->done || most <= 0)
	return 0;
    if (P_NavCells () != s->cellsof)
    {
	s->serial = 0;
	return 0;
    }
    previous = P_DerpTimingBegin (DERP_TIMING_NAV);
    SetGlobals (s, &was);
    width = P_NavWidth ();
    if (P_NavBuild () != s->gen)
    {
	if (!s->restarts++)
	    Start (s);
	else
	{
	    s->mixed = true;
	    s->gen = P_NavBuild ();
	}
    }
    while (s->heapsize && s->searched < s->limit && used < most)
    {
	int	cell = Pop (s), n, i, here;
	if (cell == s->goal)
	{
	    there = true;
	    break;
	}
	if (Guess (cell, s->goal) < s->least)
	{
	    s->least = Guess (cell, s->goal);
	    s->nearest = cell;
	}
	s->searched++;
	used++;
	here = s->cost[Find (s, cell)];
	n = P_NavExpand (s->nav, cell, next, add);
	for (i = 0; i < n; i++)
	{
	    int	c = here + add[i], j = Find (s, next[i]);
	    if (s->key[j] == next[i])
	    {
		if (s->cost[j] <= c)
		    continue;
	    }
	    else
		j = Add (s, j, next[i]);
	    s->cost[j] = c;
	    s->parent[j] = cell;
	    Push (s, next[i], c + Guess (next[i], s->goal));
	}
    }
    navsearched += used;
    P_NavBudgetSpend (used);
    if (there || !s->heapsize || s->searched >= s->limit)
	Finish (s);
    PutGlobals (&was);
    P_DerpTimingEnd (previous);
    return used;
}

// The answer, as P_NavPath gives it; the slot is free after.
static int Deliver (slot_t* s, int* path, int max)
{
    int		goal = s->goal, n, i, cell;

    s->serial = 0;
    navexhausted = s->exhausted;
    navlongcells = s->searched;
    navlonglate = s->began != leveltime;
    if (!s->found)
    {
	if (!s->partial || s->nearest == s->start)
	    return 0;
	goal = s->nearest;
    }
    navwalk = s->cost[Find (s, goal)]*NAVCELL/10;
    for (n = 0, cell = goal; cell >= 0; cell = s->parent[Find (s, cell)])
	n++;
    for (i = n - 1, cell = goal; cell >= 0; cell = s->parent[Find (s, cell)], i--)
	if (i < max)
	    path[i] = cell;
    return n < max ? n : max;
}

static slot_t* Owned (int serial, int tag)
{
    int		i;

    for (i = 0; i < LONG_SLOTS; i++)
	if (slots[i].serial == serial && slots[i].tag == tag)
	    return &slots[i];
    return NULL;
}

static slot_t* NewSlot (void)
{
    slot_t*	old = NULL;
    int		i;

    for (i = 0; i < LONG_SLOTS; i++)
    {
	if (!slots[i].serial)
	    return &slots[i];
	if (leveltime - slots[i].asked >= LONG_EVICT && (!old || slots[i].asked < old->asked))
	    old = &slots[i];
    }
    return old;
}

static boolean Same (const slot_t* s, int nav, int start, int goal, int limit)
{
    // (Not where it keeps away from: a point ahead of him swings as he turns. The
    // way is the one from when it was asked, as with any way kept, p_navkept.c.)
    return s->nav == nav && s->limit == limit && s->cellsof == P_NavCells ()
	&& s->climb == navclimb && s->partial == navpartial
	&& s->badfrom == navbadfrom && s->badto == navbadto
	&& s->avoidr == navavoidr && (!navavoidr || s->avoidhard == navavoidhard)
	&& Apart (s->start, start) <= LONG_MOVED && Apart (s->goal, goal) <= LONG_DRIFT;
}

int P_NavLongPath (mobj_t* owner, int tag, int nav, fixed_t sx, fixed_t sy, fixed_t tx, fixed_t ty,
		   int* path, int max, int limit)
{
    slot_t*	s = owner && owner->serial ? Owned (owner->serial, tag) : NULL;
    int		left = P_NavBudgetLeft (), start, goal;

    navlonglate = false;
    // Nothing waits now: the search whole, there and then.
    if (!owner || !owner->serial || left == MAXINT || !P_NavCells ())
    {
	unsigned	before = navsearched;
	if (s)
	    s->serial = 0;
	start = P_NavPath (nav, sx, sy, tx, ty, path, max, limit);
	navlongcells = (int)(navsearched - before);
	return start;
    }
    width = P_NavWidth ();
    start = P_NavCellAt (nav, sx, sy);
    goal = P_NavCellAt (nav, tx, ty);
    if (start < 0 || goal < 0)
    {
	if (s)
	    s->serial = 0;
	navlongcells = 0;
	return 0;
    }
    if (s && !Same (s, nav, start, goal, limit))
    {
	s->serial = 0;
	s = NULL;
    }
    if (!s)
    {
	if (P_NavKnownUnreachable (nav, start, goal, limit, true))
	{
	    navlongcells = 0;
	    return 0;
	}
	if (!(s = NewSlot ()))
	    return NAV_DEFERRED;
	s->serial = owner->serial;
	s->tag = tag;
	s->nav = nav;
	s->start = start;
	s->goal = goal;
	s->limit = limit;
	s->climb = navclimb;
	s->partial = navpartial;
	s->badfrom = navbadfrom;
	s->badto = navbadto;
	s->avoidr = navavoidr;
	s->avoidhard = navavoidhard;
	s->avoidx = navavoidx;
	s->avoidy = navavoidy;
	s->restarts = 0;
	s->began = leveltime;
	Start (s);
    }
    s->asked = leveltime;
    s->ownerx = owner->x;
    s->ownery = owner->y;
    Run (s, left < LONG_FIRST ? left : LONG_FIRST);
    if (!s->serial || !s->done)
	return NAV_DEFERRED;
    return Deliver (s, path, max);
}

boolean P_NavLongPending (mobj_t* owner, int tag)
{
    return owner->serial && Owned (owner->serial, tag) != NULL;
}

void P_NavLongTic (void)
{
    slot_t*	order[LONG_SLOTS];
    fixed_t	dist[LONG_SLOTS];
    mobj_t*	him;
    int		i, j, n = 0, total, left;

    for (i = 0; i < LONG_SLOTS; i++)
    {
	slot_t*	s = &slots[i];
	fixed_t	d;
	if (!s->serial)
	    continue;
	if (leveltime - s->asked > LONG_IDLE || s->asked > leveltime)
	{
	    s->serial = 0;
	    continue;
	}
	if (s->done)
	    continue;
	// Nearest him first: his hesitation shows.
	him = P_DerpPlayer ();
	d = him ? P_AproxDistance (him->x - s->ownerx, him->y - s->ownery) : 0;
	for (j = n++; j > 0 && dist[j - 1] > d; j--)
	{
	    order[j] = order[j - 1];
	    dist[j] = dist[j - 1];
	}
	order[j] = s;
	dist[j] = d;
    }
    if (!n)
	return;
    left = P_NavBudgetLeft ();
    total = LONG_TIC;
    if (left < total)
	total = left > LONG_LEAST ? left : LONG_LEAST;
    // An even share each, what one leaves going on to those after it.
    for (i = 0; i < n; i++)
	total -= Run (order[i], total/(n - i));
}

int P_NavLongCatchUp (mobj_t* actor, int nav, int* path, int n)
{
    int		i, best = 0, bestoff = MAXINT;

    for (i = 0; i < n && i < CATCH_AHEAD; i++)
    {
	fixed_t	x, y, floor;
	int	off;
	P_NavPoint (nav, path[i], &x, &y, &floor);
	if (abs ((floor - actor->floorz)>>FRACBITS) > CATCH_RISE)
	    continue;
	off = P_AproxDistance (x - actor->x, y - actor->y)>>FRACBITS;
	if (off < bestoff)
	{
	    bestoff = off;
	    best = i;
	}
    }
    if (!best || bestoff > CATCH_OFF)
	return n;
    memmove (path, path + best, sizeof *path*(n - best));
    return n - best;
}
