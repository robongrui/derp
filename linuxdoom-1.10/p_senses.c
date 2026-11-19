//
// What DERP's monsters hear, see and remember (p_senses.h).
//

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "doomdef.h"
#include "doomstat.h"
#include "i_system.h"
#include "m_argv.h"
#include "m_random.h"
#include "p_local.h"
#include "p_derp.h"
#include "p_derptiming.h"
#include "p_nav.h"
#include "p_navbudget.h"
#include "p_navkept.h"
#include "p_navlong.h"
#include "p_senses.h"
#include "p_portals.h"
#include "p_barks.h"
#include "s_sound.h"
#include "sounds.h"
#include "p_difficulty.h"
#include "p_derplog.h"
#include "r_main.h"
#include "r_state.h"
#include "tables.h"
#include "p_tune.h"

// How long a search lasts before the monster gives up, and how often it
// picks a new place to look.
#define SEARCH_TIME	TUNE (SEARCH_TIME, 20*TICRATE)
// How long it tries to get home before settling where it is.
#define RETURN_TIME	TUNE (RETURN_TIME, 30*TICRATE)
#define LOOK_AGAIN	TUNE (LOOK_AGAIN, 4*TICRATE)
// How sure (of 1000) it must be that something is there to stop and watch.
#define WATCHING	TUNE (WATCHING, 300)
// How long it stands and looks round where he was before it searches.
#define LOOK_ROUND	TUNE (LOOK_ROUND, 2*TICRATE)
// Close enough to a goal to call it reached.
#define REACHED		(48*FRACUNIT)
// What a shut door and a sound-blocking line cost a noise, in units.
#define SHUT_DOOR	TUNE (SHUT_DOOR, 384)
#define SOUND_BLOCK	TUNE (SOUND_BLOCK, 512)
// How far from where it was last seen or heard a search reaches at most,
// and how fast that grows (units a tic: a guess, slower than he runs).
#define HUNT_REACH	TUNE (HUNT_REACH, 1536)
// Near enough to see or call to: what monsters know of each other.
#define NEARBY		TUNE (SENSES_NEARBY, 768)
#define HUNT_GROWTH	TUNE (HUNT_GROWTH, 6)

static const short loudness[NUMNOISES] =
{
    192,	// fists: a punch, a grunt
    768,	// chainsaw
    1024,	// pistol
    1536,	// shotgun
    1792,	// super shotgun
    1280,	// chaingun
    2048,	// rocket launcher, and where it goes off
    1024,	// plasma rifle
    1792,	// BFG
    224,	// footsteps, running
    512,	// a door opening
    768,	// a death scream
    1024,	// a monster's shout on spotting the player
    1024,	// a monster's gun
    400		// his bullet striking a wall
};

int P_NoiseLoudness (noise_t kind)
{
    return kind >= 0 && kind < NUMNOISES ? loudness[kind] : 0;
}


static boolean Senses (void)
{
    return derpparts.senses && P_DerpAI ();
}

// Him, as far as the senses go: not while he cannot be seen (P_Unseen).
static mobj_t* Player (void)
{
    mobj_t*	mo = P_DerpPlayer ();
    return mo && !P_Unseen (mo) ? mo : NULL;
}

boolean P_SensesOn (void)
{
    return Senses ();
}

mobj_t* P_SensesPlayer (void)
{
    return Player ();
}

// When the player's gun last flashed: it shows him for a moment.
static int	lastshot = -1000;
static fixed_t	lastshotx, lastshoty;
// A lone pistol shot (LONE_AGAIN since the last): one crack, and no telling
// from where. It turns those it reaches, past RESERVE_NEAR, and sends nobody;
// a second sends them. A careful shot stays careful, a fight still carries.
#define LONE_AGAIN	TUNE (LONE_AGAIN, 3*TICRATE)
static int	lastpistol = -1000;
static boolean	loneshot;


//
// The openings a noise travels through: every two-sided line between two
// sectors, its middle in map units, and each sector's openings.
//
static int*	openline;
static int*	openx;
static int*	openy;
static int	numopen;
static int*	secfirst;	// numsectors + 1
static int*	seclist;
static int*	opendist;
static int*	openstamp;
static int	openstampnow;
static int*	heap;
static int*	heapdist;
static int	heapsize, heapmax;

// The hunt: the cells round where the player was last seen or heard, by
// distance there, and when each cell was last looked at by a searcher.
static int*	huntcells;
static int*	huntdist;
static int	numhunt, huntmax;
static fixed_t	huntx, hunty;
static int	hunttic = -1;
static int*	looked;
static int*	lookedsquad;	// whose squad looked (p_tactics.h): only they know
static int	lookedcells;
// The smallest walker's layer, the hunt's grid (looked up each time: a
// rebuild may number the layers anew).
#define huntnav	P_NavClassFor (20, 56)

static void* Alloc (void* old, size_t size)
{
    void*	p = realloc (old, size ? size : 1);
    if (!p)
	I_Error ("P_Senses: out of memory");
    return p;
}


void P_SensesLevel (void)
{
    int		i, s, n;

    numopen = 0;
    numhunt = 0;
    hunttic = -1;
    lastshot = -1000;
    lastpistol = -1000;
    if (!Senses ())
	return;
    openline = Alloc (openline, sizeof *openline*(numlines + 1));
    openx = Alloc (openx, sizeof *openx*(numlines + 1));
    openy = Alloc (openy, sizeof *openy*(numlines + 1));
    opendist = Alloc (opendist, sizeof *opendist*(numlines + 1));
    openstamp = Alloc (openstamp, sizeof *openstamp*(numlines + 1));
    memset (openstamp, 0, sizeof *openstamp*(numlines + 1));
    openstampnow = 0;
    for (i = 0; i < numlines; i++)
    {
	line_t*	ld = &lines[i];
	if (!ld->frontsector || !ld->backsector || ld->frontsector == ld->backsector)
	    continue;
	openline[numopen] = i;
	openx[numopen] = (ld->v1->x/2 + ld->v2->x/2)>>FRACBITS;
	openy[numopen] = (ld->v1->y/2 + ld->v2->y/2)>>FRACBITS;
	numopen++;
    }
    // Each sector's openings, packed.
    secfirst = Alloc (secfirst, sizeof *secfirst*(numsectors + 1));
    memset (secfirst, 0, sizeof *secfirst*(numsectors + 1));
    for (i = 0; i < numopen; i++)
    {
	line_t*	ld = &lines[openline[i]];
	secfirst[ld->frontsector - sectors + 1]++;
	secfirst[ld->backsector - sectors + 1]++;
    }
    for (s = 0; s < numsectors; s++)
	secfirst[s + 1] += secfirst[s];
    seclist = Alloc (seclist, sizeof *seclist*(2*numopen + 1));
    {
	int*	fill = Alloc (NULL, sizeof *fill*(numsectors + 1));
	memcpy (fill, secfirst, sizeof *fill*(numsectors + 1));
	for (i = 0; i < numopen; i++)
	{
	    line_t*	ld = &lines[openline[i]];
	    seclist[fill[ld->frontsector - sectors]++] = i;
	    seclist[fill[ld->backsector - sectors]++] = i;
	}
	free (fill);
    }
    // The searchers' marks, over the smallest walker's grid.
    n = P_NavCells ();
    looked = Alloc (looked, sizeof *looked*(n + 1));
    lookedsquad = Alloc (lookedsquad, sizeof *lookedsquad*(n + 1));
    for (i = 0; i < n; i++)
    {
	looked[i] = -100000;
	lookedsquad[i] = 0;
    }
    lookedcells = n;
}


//
// Hearing.
//
static void HeapPush (int open, int d)
{
    int		i;

    if (heapsize == heapmax)
    {
	heapmax = heapmax ? 2*heapmax : 1024;
	heap = Alloc (heap, sizeof *heap*heapmax);
	heapdist = Alloc (heapdist, sizeof *heapdist*heapmax);
    }
    i = heapsize++;
    while (i > 0 && heapdist[(i - 1)/2] > d)
    {
	heap[i] = heap[(i - 1)/2];
	heapdist[i] = heapdist[(i - 1)/2];
	i = (i - 1)/2;
    }
    heap[i] = open;
    heapdist[i] = d;
}

static int HeapPop (int* d)
{
    int		top = heap[0], open, dd, i = 0;

    *d = heapdist[0];
    open = heap[--heapsize];
    dd = heapdist[heapsize];
    for (;;)
    {
	int	child = 2*i + 1;
	if (child >= heapsize)
	    break;
	if (child + 1 < heapsize && heapdist[child + 1] < heapdist[child])
	    child++;
	if (heapdist[child] >= dd)
	    break;
	heap[i] = heap[child];
	heapdist[i] = heapdist[child];
	i = child;
    }
    heap[i] = open;
    heapdist[i] = dd;
    return top;
}

static int Units (int dx, int dy)
{
    return P_AproxDistance (dx*FRACUNIT, dy*FRACUNIT)>>FRACBITS;
}

// What passing an opening costs: a shut door, a line the mapper marked.
static int Through (int open)
{
    line_t*	ld = &lines[openline[open]];
    fixed_t	top = ld->frontsector->ceilingheight < ld->backsector->ceilingheight
		    ? ld->frontsector->ceilingheight : ld->backsector->ceilingheight;
    fixed_t	bottom = ld->frontsector->floorheight > ld->backsector->floorheight
		    ? ld->frontsector->floorheight : ld->backsector->floorheight;
    int		cost = 0;

    if (top <= bottom)
	cost += SHUT_DOOR;
    if (ld->flags & ML_SOUNDBLOCK)
	cost += SOUND_BLOCK;
    return cost;
}

// Gunfire fills the room it goes off in, however big: its far side is not far.
// What counts is how far it goes from there. Crossing that room costs this share.
// (A whole fight in E1M1's zigzag room, and the rooms past it heard nothing: it
// was all counted from the gun, across a hall that rings.)
#define ROOM_FILL	TUNE (ROOM_FILL, 4)
// And a fight carries further than a shot, in halves of what a noise reaches.
#define FIGHT_CARRY	TUNE (FIGHT_CARRY, 3)

// Spreads a noise at (x, y) through the openings, as far as limit: each
// opening reached its distance (openstamp == openstampnow).
static int	spreadsector;
static int	spreadx, spready;
static int	spreadroom = -1;	// the room that noise fills, if it does

static int Across (int sector, int units)
{
    return spreadroom >= 0 && P_NavRoom (sector) == spreadroom ? units/ROOM_FILL : units;
}

static void Spread (fixed_t x, fixed_t y, int limit)
{
    int		s, k;

    spreadsector = R_PointInSubsector (x, y)->sector - sectors;
    spreadx = x>>FRACBITS;
    spready = y>>FRACBITS;
    if (++openstampnow == MAXINT)
    {
	memset (openstamp, 0, sizeof *openstamp*(numlines + 1));
	openstampnow = 1;
    }
    heapsize = 0;
    for (k = secfirst[spreadsector]; k < secfirst[spreadsector + 1]; k++)
    {
	int	o = seclist[k];
	int	d = Across (spreadsector, Units (openx[o] - spreadx, openy[o] - spready)) + Through (o);
	if (d > limit || (openstamp[o] == openstampnow && opendist[o] <= d))
	    continue;
	openstamp[o] = openstampnow;
	opendist[o] = d;
	HeapPush (o, d);
    }
    while (heapsize)
    {
	int	d, o = HeapPop (&d), side;
	if (d > opendist[o])
	    continue;	// pushed again since, cheaper
	for (side = 0; side < 2; side++)
	{
	    line_t*	ld = &lines[openline[o]];
	    s = (side ? ld->backsector : ld->frontsector) - sectors;
	    for (k = secfirst[s]; k < secfirst[s + 1]; k++)
	    {
		int	p = seclist[k], nd;
		if (p == o)
		    continue;
		nd = d + Across (s, Units (openx[p] - openx[o], openy[p] - openy[o])) + Through (p);
		if (nd > limit || (openstamp[p] == openstampnow && opendist[p] <= nd))
		    continue;
		openstamp[p] = openstampnow;
		opendist[p] = nd;
		HeapPush (p, nd);
	    }
	}
    }
}

// How far the last noise spread came to (x, y); MAXINT if it did not.
// heardvia: the opening of (x, y)'s sector it came in through (-1: in the
// noise's own sector).
static int	heardvia;

static int Heard (fixed_t x, fixed_t y)
{
    int		s = R_PointInSubsector (x, y)->sector - sectors, k, best = MAXINT;
    int		lx = x>>FRACBITS, ly = y>>FRACBITS;

    heardvia = -1;
    if (s == spreadsector)
	return Across (s, Units (lx - spreadx, ly - spready));
    for (k = secfirst[s]; k < secfirst[s + 1]; k++)
    {
	int	o = seclist[k];
	if (openstamp[o] == openstampnow)
	{
	    int	d = opendist[o] + Across (s, Units (lx - openx[o], ly - openy[o]));
	    if (d < best)
	    {
		best = d;
		heardvia = o;
	    }
	}
    }
    return best;
}


int P_NoiseDistance (fixed_t sx, fixed_t sy, fixed_t lx, fixed_t ly, int limit)
{
    if (!numopen)
	return MAXINT;
    spreadroom = -1;
    Spread (sx, sy, limit);
    return Heard (lx, ly);
}

int P_GunDistance (fixed_t sx, fixed_t sy, fixed_t lx, fixed_t ly, int limit)
{
    int	d;

    if (!numopen)
	return MAXINT;
    spreadroom = P_NavRoom ((int)(R_PointInSubsector (sx, sy)->sector - sectors));
    Spread (sx, sy, limit);
    d = Heard (lx, ly);
    spreadroom = -1;
    return d;
}


// A rough point near (x, y): the farther the noise came, the rougher.
static void Rough (fixed_t* x, fixed_t* y, int distance)
{
    int		error = distance/4;

    *x += (P_Random () - 128)*error/128*FRACUNIT;
    *y += (P_Random () - 128)*error/128*FRACUNIT;
}

// Whether mo is after him with fresh word of him (HUNT_FRESH).
#define HUNT_FRESH	TUNE (HUNT_FRESH, 5*TICRATE)

static boolean Hunting (mobj_t* mo, mobj_t* player)
{
    return mo->mind == MIND_HUNT && mo->target == player && mo->knowntic
	&& leveltime - mo->knowntic < HUNT_FRESH;
}

// No way where it was going: it stands at the wall this long before it goes
// back (what it makes of noises from beyond it then is the brain's, p_brain.c).
#define NOWAY_LISTEN	TUNE (NOWAY_LISTEN, 2*TICRATE)

// Whether it has a way to (x, y) at all, as it would walk it (a search running out is
// not no way, and nor is one put off with the tic's routes spent: a noise fanned out
// to every listener asked once each).
static boolean Reachable (mobj_t* mo, fixed_t x, fixed_t y)
{
    static int	path[2];
    int		nav = P_NavClassOf (mo), n;

    if (nav < 0)
	return true;
    P_DerpRouteBegin (mo);
    n = P_NavPathFor (mo, nav, mo->x, mo->y, x, y, path, 2, 8000);
    P_DerpRouteEnd ();
    if (n == NAV_DEFERRED)
	return true;
    return n > 0 || navexhausted;
}

// What the senses tell the brain (P_SensesHooks).
static senseshooks_t	hooks;

void P_SensesHooks (const senseshooks_t* h)
{
    hooks = *h;
}

void P_SensesRough (fixed_t* x, fixed_t* y, int distance)
{
    Rough (x, y, distance);
}

boolean P_SensesHunting (mobj_t* mo)
{
    return Hunting (mo, Player ());
}

boolean P_SensesReachable (mobj_t* mo, fixed_t x, fixed_t y)
{
    return Reachable (mo, x, y);
}

void P_SensesTold (mobj_t* actor, fixed_t x, fixed_t y)
{
    actor->knownx = x;
    actor->knowny = y;
    actor->knowntic = leveltime;
}

// Where a noise seems to come from, for the one that heard it: only while the
// hearing is being answered (heardvia is the opening it came through, from
// Spread). The brain asks when a rule needs it, once.
void P_SensesGuess (heard_t* h)
{
    mobj_t*	listener = h->listener;
    fixed_t	x = h->x, y = h->y, gx = x, gy = y;
    int		distance = h->distance;

    if (h->guessed)
	return;
    h->guessed = true;
    // A shout carries where the shouter thinks the player is.
    if (h->kind == NOISE_SHOUT && h->source)
    {
	gx = h->source->knownx;
	gy = h->source->knowny;
	Rough (&gx, &gy, distance);
    }
    else if (heardvia >= 0
	     && P_AproxDistance (listener->x - openx[heardvia]*FRACUNIT,
				 listener->y - openy[heardvia]*FRACUNIT) > 64*FRACUNIT)
    {
	// Through a doorway: that is where it seems to come from, so it goes
	// there first (not to a guess that may lie inside a wall); each hearer
	// to its own place along it (by its number, up to 24 units off the
	// middle), not all to one point to jostle there.
	line_t*	ld = &lines[openline[heardvia]];
	fixed_t	len = P_AproxDistance (ld->dx, ld->dy);
	int	along = (int)((unsigned)listener->serial*29 % 49) - 24;
	gx = openx[heardvia]*FRACUNIT;
	gy = openy[heardvia]*FRACUNIT;
	if (len > 48*FRACUNIT)
	{
	    gx += FixedMul (FixedDiv (ld->dx, len), along*FRACUNIT);
	    gy += FixedMul (FixedDiv (ld->dy, len), along*FRACUNIT);
	}
	// And through it, a few steps on towards the noise: there it looks
	// into the room the noise came from. (At the doorway itself it found
	// nothing, looked round, heard the fight again and came back: to and
	// fro, no plan to see.)
	{
	    fixed_t	d = P_AproxDistance (x - gx, y - gy);
	    if (d > 96*FRACUNIT)
	    {
		fixed_t	tx = gx + FixedMul (x - gx, FixedDiv (96*FRACUNIT, d));
		fixed_t	ty = gy + FixedMul (y - gy, FixedDiv (96*FRACUNIT, d));
		int	nav = P_NavClassOf (listener), cell = P_NavCellAt (nav, tx, ty);
		if (cell >= 0 && P_NavStandable (nav, cell))
		{
		    gx = tx;
		    gy = ty;
		}
	    }
	}
    }
    else
	Rough (&gx, &gy, distance);
    // A guess right beside it while the noise is far (an opening next to
    // it, a rough point that fell short): that way, not here. It looked
    // round on the spot at every noise, to and fro, and never went anywhere.
    if (P_AproxDistance (gx - listener->x, gy - listener->y) < 128*FRACUNIT
	&& P_AproxDistance (x - listener->x, y - listener->y) > 256*FRACUNIT)
    {
	gx = x;
	gy = y;
	Rough (&gx, &gy, distance);
    }
    h->guessx = gx;
    h->guessy = gy;
}

// listener heard a noise of kind from source at (x, y), distance units away:
// the brain makes of it what it will (P_BrainLevel's hooks).
static void Hear (mobj_t* listener, mobj_t* source, noise_t kind, fixed_t x, fixed_t y, int distance)
{
    heard_t	h;

    if (!Player () || listener == source || !P_DerpMonster (listener) || !hooks.heard)
	return;
    // That round went by it: it knows which way it came from, and the wall it hit
    // beyond it is no news.
    if (kind == NOISE_IMPACT && leveltime - listener->roundtic < TICRATE)
	return;
    h.listener = listener;
    h.source = source;
    h.kind = kind;
    h.x = x;
    h.y = y;
    h.distance = distance;
    h.lone = loneshot;
    h.player = Player ();
    h.guessed = false;
    h.guessx = h.guessy = 0;
    hooks.heard (&h);
}

void P_DerpRoundHeard (mobj_t* mo, mobj_t* source, boolean his, fixed_t x, fixed_t y)
{
    if (!Senses ())
	return;
    heardvia = -1;
    Hear (mo, source, his ? NOISE_PISTOL : NOISE_MONSTERSHOT, x, y,
	  P_AproxDistance (mo->x - x, mo->y - y)>>FRACBITS);
}


// A noise made while another is being heard (a monster woken by one opens a
// door on its first step): heard after it, as the spread is shared.
typedef struct { mobj_t* source; noise_t kind; fixed_t x, y; } pending_t;
static pending_t	pending[8];
static int		numpending;
static boolean		spreading;

static void Noise (mobj_t* source, noise_t kind, fixed_t x, fixed_t y);

void P_DerpNoise (mobj_t* source, noise_t kind, fixed_t x, fixed_t y)
{
    derp_timing_group previous;

    if (!Senses () || !numopen)
	return;
    if (spreading)
    {
	if (numpending < 8)
	{
	    pending[numpending].source = source;
	    pending[numpending].kind = kind;
	    pending[numpending].x = x;
	    pending[numpending].y = y;
	    numpending++;
	}
	return;
    }
    previous = P_DerpTimingBegin (DERP_TIMING_SENSE);
    spreading = true;
    Noise (source, kind, x, y);
    while (numpending)
    {
	pending_t	next = pending[0];
	memmove (pending, pending + 1, sizeof *pending*(--numpending));
	Noise (next.source, next.kind, next.x, next.y);
    }
    spreading = false;
    P_DerpTimingEnd (previous);
}

static void Noise (mobj_t* source, noise_t kind, fixed_t x, fixed_t y)
{
    int		limit = P_NoiseLoudness (kind);
    boolean	gun = kind == NOISE_MONSTERSHOT || (kind >= NOISE_PISTOL && kind <= NOISE_BFG);
    thinker_t*	th;

    if (gun && hooks.fighton && hooks.fighton ())
	limit = limit*FIGHT_CARRY/2;
    // (One careful shot does not ring: only a fight does.)
    spreadroom = gun && !loneshot ? P_NavRoom ((int)(R_PointInSubsector (x, y)->sector - sectors)) : -1;
    Spread (x, y, limit);
    for (th = thinkercap.next; th != &thinkercap; th = th->next)
    {
	mobj_t*	mo = (mobj_t *)th;
	int	d;
	if (th->function.acp1 != (actionf_p1)P_MobjThinker || !P_DerpMonster (mo))
	    continue;
	// Never nearer than in a straight line: most are out of reach at once.
	if (P_AproxDistance (mo->x - x, mo->y - y) > limit*FRACUNIT)
	    continue;
	d = Heard (mo->x, mo->y);
	// Gunfire carries through walls too, dulled: the straight line and
	// half again. (By the openings alone a route ran 1.5 to 3 times the
	// straight line, and a pistol reached under half of those 600 units
	// off.) Steps, doors and fists do not: creeping stays creeping.
	if (kind == NOISE_MONSTERSHOT || kind == NOISE_IMPACT || (kind >= NOISE_PISTOL && kind <= NOISE_BFG))
	{
	    int	wall = (P_AproxDistance (mo->x - x, mo->y - y)>>FRACBITS)*3/2;
	    if (wall < d)
	    {
		d = wall;
		heardvia = -1;
	    }
	}
	// The room filling says it hears. How far off it seems is still how far off it
	// is: near or far, a reserve or a wave, how rough its guess.
	if (d <= limit)
	{
	    int	far = P_AproxDistance (mo->x - x, mo->y - y)>>FRACBITS;
	    Hear (mo, source, kind, x, y, d > far ? d : far);
	}
    }
    spreadroom = -1;
}


boolean P_DerpNoiseAlert (mobj_t* target, mobj_t* emitter)
{
    static const noise_t guns[NUMWEAPONS] =
    {
	NOISE_FIST, NOISE_PISTOL, NOISE_SHOTGUN, NOISE_CHAINGUN, NOISE_MISSILE,
	NOISE_PLASMA, NOISE_BFG, NOISE_CHAINSAW, NOISE_SUPERSHOTGUN
    };
    noise_t	kind = NOISE_PISTOL;

    if (!Senses ())
	return false;
    if (target && target->player && target->player->readyweapon < NUMWEAPONS)
	kind = guns[target->player->readyweapon];
    if (kind != NOISE_FIST && kind != NOISE_CHAINSAW)
    {
	lastshot = leveltime;
	lastshotx = emitter->x;
	lastshoty = emitter->y;
    }
    if (kind == NOISE_PISTOL)
    {
	loneshot = leveltime - lastpistol > LONE_AGAIN;
	lastpistol = leveltime;
    }
    P_DerpLog (NULL, "player fires (noise kind %d) at (%d,%d), health %d", kind, emitter->x>>FRACBITS,
	       emitter->y>>FRACBITS, target && target->player ? target->player->health : 0);
    P_DerpNoise (target, kind, emitter->x, emitter->y);
    loneshot = false;
    return true;
}


void P_DerpFootsteps (player_t* player)
{
    mobj_t*	mo = player->mo;

    if (!Senses () || !mo || mo->health <= 0 || !P_PHASE (PHASE_STEPS) || mo->z > mo->floorz)
	return;
    // Running: well past a walk's pace (5/8 of his top speed; a walk is half).
    if (P_AproxDistance (mo->momx, mo->momy) > P_DerpRunSpeed ()*5/8)
	P_DerpNoise (mo, NOISE_FOOTSTEP, mo->x, mo->y);
}


//
// Vision.
//
#define FACE_NEAR	TUNE (FACE_NEAR, 320)
#define FACE_TICS	TUNE (FACE_TICS, 6)

int P_DerpSpotTics (mobj_t* actor, mobj_t* mo)
{
    boolean	near;
    angle_t	an = R_PointToAngle2 (actor->x, actor->y, mo->x, mo->y) - actor->angle;
    fixed_t	dist = P_AproxDistance (mo->x - actor->x, mo->y - actor->y);
    int		tics, light;

    if (dist <= MELEERANGE)
	return 0;		// at arm's length, at once, from any side
    if (an > ANG90 && an < ANG270)
	return -1;		// behind its back
    near = dist <= FACE_NEAR*FRACUNIT && (an < ANG45/2 || an > ANG270 + ANG45 + ANG45/2);
    // Quick: a guard's eyes are on the doorways (about half a second at
    // 400 units in a lit room).
    tics = 4 + (dist>>FRACBITS)/48;
    // The edge of its view: half as quick again, unless it is on alert.
    if (an > ANG45 && an < ANG270 + ANG45 && !actor->alert)
	tics = tics*3/2;
    tics = tics*P_DerpAlertScale (actor)/100;
    // Light: as his sector is lit, or his gun has just flashed.
    light = mo->subsector->sector->lightlevel;
    if ((mo->player && mo->player->extralight) || leveltime - lastshot < 6)
	light = 255;
    if (light < 0)
	light = 0;
    tics = tics*288/(light + 32);
    // A running player catches the eye; one standing still less so.
    dist = P_AproxDistance (mo->momx, mo->momy);
    if (dist > P_DerpRunSpeed ()*2/3)
	tics = tics*2/3;
    else if (dist < FRACUNIT)
	tics = tics*4/3;
    tics = tics*P_Difficulty ()->eyes/100;
    // Right in front of it, near: no pondering, however dim. A moment's doubt
    // at the edge of its view looks real; two seconds to see a man standing
    // before it looks broken.
    if (near && tics > FACE_TICS)
	tics = FACE_TICS;
    return tics > 1 ? tics : 1;
}


// Past this a monster that has not been told of him cannot make him out.
#define SPOT_FAR	TUNE (SPOT_FAR, 1152)

// actor has spotted the player: after him, and the others are told.
static void Spot (mobj_t* actor, mobj_t* player)
{
    P_DerpLog (actor, "spots the player %d units off",
	       P_AproxDistance (player->x - actor->x, player->y - actor->y)>>FRACBITS);
    actor->target = player;
    P_MindBecome (actor, MIND_HUNT, STEP_NONE, "none.spots",
		  "It has made him out: after him, and it shouts to tell the others");
    actor->knownx = player->x;
    actor->knowny = player->y;
    actor->knowntic = leveltime;
    actor->awareness = 0;
    // Sure of him: at him at once, not vanilla's second of dawdling
    // (reactiontime 8 chase steps, a random movecount) before its gun.
    actor->reactiontime = 1;
    actor->movecount = 0;
    // "Contact!" takes the place of its sight sound.
    if (!P_DerpBark (actor, BARK_CONTACT))
	P_SeeYou (actor);
    P_DerpNoise (actor, NOISE_SHOUT, actor->x, actor->y);
}


boolean P_SensesLook (mobj_t* actor)
{
    mobj_t*	player;
    int		elapsed, tics;

    elapsed = leveltime - actor->looktic;
    if (elapsed < 1 || elapsed > TICRATE)
	elapsed = elapsed < 1 ? 1 : TICRATE;
    actor->looktic = leveltime;
    player = Player ();
    if (player && P_CheckSight (actor, player)
	&& (tics = P_DerpSpotTics (actor, player)) >= 0)
    {
	int	before = actor->awareness;
	actor->awareness = tics ? actor->awareness + 1000*elapsed/tics : 1000;
	// Too far to be sure, however long it looks: something moved over there.
	if (actor->awareness >= 1000
	    && P_AproxDistance (player->x - actor->x, player->y - actor->y) > SPOT_FAR*FRACUNIT)
	    actor->awareness = 999;
	if (actor->awareness >= 1000)
	    Spot (actor, player);
	else if (actor->awareness >= WATCHING)
	{
	    // Something there: it stops what it was at and turns to watch,
	    // a step at a time (which makes it quicker to be sure).
	    angle_t	to = R_PointToAngle2 (actor->x, actor->y, player->x, player->y);
	    angle_t	off = to - actor->angle;
	    if (before < WATCHING)
		P_DerpLog (actor, "notices something, turns to watch");
	    if (off < ANG45/2 || off > (angle_t)-(ANG45/2))
		actor->angle = to;
	    else
		actor->angle += off < ANG180 ? ANG45/2 : (angle_t)-(ANG45/2);
	}
	return true;
    }
    // Out of sight it calms down, slowly.
    actor->awareness -= 4*elapsed;
    if (actor->awareness < 0)
	actor->awareness = 0;
    return true;
}


void P_SensesHit (mobj_t* target, mobj_t* source)
{
    // Where from: exactly if it sees him, else the way the shot came, the
    // rougher the farther.
    target->knownx = source->x;
    target->knowny = source->y;
    if (!P_CheckSight (target, source))
	Rough (&target->knownx, &target->knowny,
	       P_AproxDistance (source->x - target->x, source->y - target->y)>>FRACBITS);
    target->knowntic = leveltime;
}

int P_DerpAlertScale (mobj_t* actor)
{
    if (!(derpparts.tactics && Senses ()))
	return 100;
    return actor->alert >= 2 ? 60 : actor->alert == 1 ? 75 : 100;
}

//
// The hunt.
//

// The cells walkable from (x, y), nearest first, as far as HUNT_REACH.
static void HuntFrom (fixed_t x, fixed_t y)
{
    int		cells = P_NavCells (), start, head = 0;
    static int*	seen;
    static int	seencells, seenstamp;

    huntx = x;
    hunty = y;
    hunttic = leveltime;
    numhunt = 0;
    if (huntnav < 0 || !cells)
	return;
    start = P_NavCellAt (huntnav, x, y);
    if (start < 0)
	return;
    if (seencells != cells)
    {
	seen = Alloc (seen, sizeof *seen*cells);
	memset (seen, 0, sizeof *seen*cells);
	seencells = cells;
	seenstamp = 0;
    }
    if (++seenstamp == MAXINT)
    {
	memset (seen, 0, sizeof *seen*cells);
	seenstamp = 1;
    }
    if (huntmax < 1024)
    {
	huntmax = 1024;
	huntcells = Alloc (huntcells, sizeof *huntcells*huntmax);
	huntdist = Alloc (huntdist, sizeof *huntdist*huntmax);
    }
    // Breadth first over the grid's steps: in cells, NAVCELL units each.
    huntcells[numhunt] = start;
    huntdist[numhunt++] = 0;
    seen[start] = seenstamp;
    while (head < numhunt)
    {
	int	cell = huntcells[head], d = huntdist[head++], n;
	fixed_t	cx, cy;
	if ((d + 1)*NAVCELL > HUNT_REACH)
	    continue;
	P_NavPoint (huntnav, cell, &cx, &cy, NULL);
	for (n = 0; n < 8; n++)
	{
	    static const int around[8][2] = {{1,0},{-1,0},{0,1},{0,-1},{1,1},{1,-1},{-1,1},{-1,-1}};
	    int	next = P_NavCellAt (huntnav, cx + around[n][0]*NAVCELL*FRACUNIT, cy + around[n][1]*NAVCELL*FRACUNIT);
	    if (next < 0 || seen[next] == seenstamp || !P_NavStep (huntnav, cell, next))
		continue;
	    seen[next] = seenstamp;
	    if (numhunt == huntmax)
	    {
		huntmax *= 2;
		huntcells = Alloc (huntcells, sizeof *huntcells*huntmax);
		huntdist = Alloc (huntdist, sizeof *huntdist*huntmax);
	    }
	    huntcells[numhunt] = next;
	    huntdist[numhunt++] = d + 1;
	}
    }
}

// Where actor looks next: a cell he could have reached since (as far as a
// guess grows with time), not looked at lately, not where another searcher
// is going, and not far from actor. Falls back on where he was.
static void PickSearch (mobj_t* actor)
{
    int		reach, best = -1, bestscore = MININT, i, k;
    thinker_t*	th;

    if (hunttic < 0 || P_AproxDistance (huntx - actor->knownx, hunty - actor->knowny) > 128*FRACUNIT
	|| actor->knowntic > hunttic)
	HuntFrom (actor->knownx, actor->knowny);
    // Where it thinks he was is nowhere it can walk (beyond a wall, say): it searches
    // round itself, not stands staring at its own feet for twenty seconds.
    // And the same when its last place had no way to it: what it can walk to, this time.
    if (!numhunt || actor->nearestonly)
	HuntFrom (actor->x, actor->y);
    reach = 128 + HUNT_GROWTH*(leveltime - actor->knowntic);
    if (reach > HUNT_REACH)
	reach = HUNT_REACH;
    for (i = 0; i < numhunt && huntdist[i]*NAVCELL <= reach; i++)
	;
    for (k = 0; k < 12 && i > 0; k++)
    {
	int	n = (P_Random ()*256 + P_Random ()) % i, cell = huntcells[n], score;
	fixed_t	cx, cy;
	P_NavPoint (huntnav, cell, &cx, &cy, NULL);
	score = 0;
	// Not looked at lately, as far as it knows: by it or its own squad
	// (what a squad in another room checked, it cannot know).
	if (cell < lookedcells && (leveltime - looked[cell] > 15*TICRATE
				   || (lookedsquad[cell] && lookedsquad[cell] != actor->squad)))
	    score += 1000;
	score -= (P_AproxDistance (cx - actor->x, cy - actor->y)>>FRACBITS)/4;
	// Not where another searcher is going: one near enough to see or call
	// to (a searcher across the map is no one it knows of).
	for (th = thinkercap.next; th != &thinkercap; th = th->next)
	{
	    mobj_t*	mo = (mobj_t *)th;
	    if (th->function.acp1 == (actionf_p1)P_MobjThinker && mo != actor
		&& mo->mind == MIND_SEARCH && P_AproxDistance (mo->goalx - cx, mo->goaly - cy) < 128*FRACUNIT
		&& P_AproxDistance (mo->x - actor->x, mo->y - actor->y) < NEARBY*FRACUNIT)
		score -= 300;
	}
	if (score > bestscore)
	{
	    bestscore = score;
	    best = cell;
	}
    }
    if (best >= 0)
	P_NavPoint (huntnav, best, &actor->goalx, &actor->goaly, NULL);
    else
    {
	actor->goalx = actor->knownx;
	actor->goaly = actor->knowny;
    }
    actor->goaltic = leveltime;
    actor->waytic = -1000;
}


// Whether mo sees a body standing at (x, y) on floor z.
static boolean SeesSpot (mobj_t* mo, fixed_t x, fixed_t y, fixed_t z)
{
    static mobj_t	spot;

    memset (&spot, 0, sizeof spot);
    spot.x = x;
    spot.y = y;
    spot.z = z;
    spot.height = 56*FRACUNIT;
    spot.subsector = R_PointInSubsector (x, y);
    return P_CheckSight (mo, &spot);
}

void P_SensesThink (void)
{
    thinker_t*	th;
    derp_timing_group previous;

    if (!Senses () || !P_PHASE (PHASE_LOOK) || huntnav < 0 || !lookedcells)
	return;
    previous = P_DerpTimingBegin (DERP_TIMING_SENSE);
    // What a searcher can see near it, in front, in its own room, is looked
    // at: in its sight, not behind a pillar (seen, not just in front). Each
    // pass looks at every other cell not looked at in the last 2 s.
    for (th = thinkercap.next; th != &thinkercap; th = th->next)
    {
	mobj_t*	mo = (mobj_t *)th;
	int	cx, cy, r, room;
	if (th->function.acp1 != (actionf_p1)P_MobjThinker || !P_DerpMonster (mo)
	    || (mo->mind != MIND_SEARCH && mo->mind != MIND_INVESTIGATE))
	    continue;
	room = P_NavRoom (mo->subsector->sector - sectors);
	r = 256/NAVCELL;
	for (cy = -r; cy <= r; cy++)
	    for (cx = -r; cx <= r; cx++)
	    {
		int	c = P_NavCellAt (huntnav, mo->x + cx*NAVCELL*FRACUNIT, mo->y + cy*NAVCELL*FRACUNIT);
		fixed_t	px, py, pf;
		angle_t	an;
		if (c < 0 || c >= lookedcells || ((cx + cy + (leveltime >> 3)) & 1)
		    || (looked[c] && leveltime - looked[c] < 2*TICRATE))
		    continue;
		P_NavPoint (huntnav, c, &px, &py, &pf);
		an = R_PointToAngle2 (mo->x, mo->y, px, py) - mo->angle;
		if (an > ANG90 && an < ANG270)
		    continue;
		if (P_NavRoom (R_PointInSubsector (px, py)->sector - sectors) != room
		    || !SeesSpot (mo, px, py, pf))
		    continue;
		looked[c] = leveltime;
		lookedsquad[c] = mo->squad;
	    }
    }
    P_DerpTimingEnd (previous);
}


//
// Moving to a goal: by the navigation's route where there is one.
//

// From p_enemy.c: a step in actor->movedir (0-7 the eight ways, 8 none).
boolean P_Move (mobj_t* actor);
#define NODIR		8

static int DirTo (fixed_t dx, fixed_t dy)
{
    angle_t	an = R_PointToAngle2 (0, 0, dx, dy);
    return ((an + ANG45/2)>>29) & 7;
}

// The farthest of the route's first n cells actor can walk to in a straight
// line: every cell before it lies within 10 units of that line, so a
// route round a corner is followed round it, not cut through the wall.
static int Straight (int nav, mobj_t* actor, const int* path, int n)
{
    int		k, j;

    for (k = n - 1; k > 1; k--)
    {
	fixed_t	bx, by;
	double	ax = actor->x/(double)FRACUNIT, ay = actor->y/(double)FRACUNIT, dx, dy, len;
	boolean	straight = true;
	P_NavPoint (nav, path[k], &bx, &by, NULL);
	dx = bx/(double)FRACUNIT - ax;
	dy = by/(double)FRACUNIT - ay;
	len = dx*dx + dy*dy;
	if (len < 1)
	    return k;
	len = sqrt (len);
	for (j = 1; j < k && straight; j++)
	{
	    fixed_t	px, py;
	    P_NavPoint (nav, path[j], &px, &py, NULL);
	    // Its distance off the line: the cross product over the length.
	    if (fabs ((px/(double)FRACUNIT - ax)*dy - (py/(double)FRACUNIT - ay)*dx)/len > 10)
		straight = false;
	}
	if (straight)
	    return k;
    }
    return 1;
}

void P_DerpWalkTo (mobj_t* actor, fixed_t x, fixed_t y);

boolean P_DerpWalkStep (mobj_t* actor)
{
    if (!P_Move (actor))
	return false;
    actor->movecount = P_Random () & 15;
    return true;
}

int P_DerpLastShot (void)
{
    return lastshot;
}

static void Walk (mobj_t* actor, fixed_t gx, fixed_t gy);

// How low it is, a share of FRACUNIT: gathering for a jump, or taking a landing.
#define LEAP_WINDUP	TUNE (LEAP_WINDUP, 10)
#define LAND_TICS	TUNE (LAND_TICS, 10)
#define CROUCH_MOST	(FRACUNIT*6/10)
#define LANDED_MOST	(FRACUNIT/2)

fixed_t P_DerpCrouch (const mobj_t* mo)
{
    int	since = leveltime - mo->landtic;

    if (mo->health <= 0)
	return 0;
    if (mo->leapwind > leveltime)
	return CROUCH_MOST*(LEAP_WINDUP - (mo->leapwind - leveltime) + 1)/LEAP_WINDUP;
    if (mo->landtic && since < LAND_TICS)
	return LANDED_MOST*(LAND_TICS - since)/LAND_TICS;
    return 0;
}

boolean P_DerpMantleLean (mobj_t* mo, fixed_t* dx, fixed_t* dy)
{
    angle_t	an;
    fixed_t	how;

    // Mid-climb up a ledge: reaching up, then bent forward over the edge.
    if (!mo->leaptic || leveltime - mo->leaptic > 2*TICRATE || mo->z <= mo->floorz
	|| mo->leapsill <= mo->floorz + 24*FRACUNIT || mo->health <= 0)
	return false;
    an = R_PointToAngle2 (mo->x, mo->y, mo->landx, mo->landy);
    how = mo->z >= mo->leapsill - 32*FRACUNIT ? FRACUNIT*2 : FRACUNIT/2;
    *dx = FixedMul (how, finecosine[an>>ANGLETOFINESHIFT]);
    *dy = FixedMul (how, finesine[an>>ANGLETOFINESHIFT]);
    return true;
}

// Glass in the way of a leap goes, the whole window at once: a body through
// it, not a shot, and a pane takes three of those (the renderer knows which
// openings are glazed).
static mobj_t*	glassby;

static boolean PTR_LeapGlass (intercept_t* in)
{
    line_t*	li = in->d.line;
    fixed_t	x, y;

    if (!R_GlassStruck || !(li->flags & ML_TWOSIDED))
	return true;
    x = glassby->x - glassby->momx + FixedMul (glassby->momx, in->frac);
    y = glassby->y - glassby->momy + FixedMul (glassby->momy, in->frac);
    R_GlassStruck (li, x, y, glassby->z + glassby->height/2, glassby->momx, glassby->momy);
    return true;
}

// Down on something short of the landing and above it, a window's sill or the
// lip it left: not landed, in the middle of it. It goes on across (with no room
// to hop in a window its own height). It stood there for good, the glass
// smashed: E1M1's big window is a strip with a blocking line either side, and
// an arc coming down on it has no other way off.
static boolean OnSill (mobj_t* mo)
{
    // (At the landing with its body still over the sill counts: stopped there by
    // a blow or by him, it was a failed jump, and two of those shut the window.)
    return mo->jumping && mo->z <= mo->floorz && leveltime - mo->leaptic <= 2*TICRATE
	&& mo->floorz > mo->landz + 8*FRACUNIT;
}

void P_DerpMantle (mobj_t* mo)
{
    angle_t	an;
    boolean	sill = OnSill (mo);

    if (!mo->leaptic || leveltime - mo->leaptic > 2*TICRATE || (mo->z <= mo->floorz && !sill))
	return;
    // Through what it passed this tic: a window's glass breaks.
    if (R_GlassStruck && (mo->momx || mo->momy))
    {
	glassby = mo;
	P_PathTraverse (mo->x - mo->momx, mo->y - mo->momy, mo->x, mo->y, PT_ADDLINES, PTR_LeapGlass);
    }
    // Up a ledge or a sill, high enough now (the game lets a body onto a
    // floor up to 24 above it): over the edge, bent forward over it.
    if (!sill && (mo->leapsill <= mo->floorz + 24*FRACUNIT || mo->z < mo->leapsill - 20*FRACUNIT
		  || P_AproxDistance (mo->landx - mo->x, mo->landy - mo->y) < 8*FRACUNIT))
	return;
    // (The way it set off: at the landing, the way to it says nothing.)
    an = sill ? R_PointToAngle2 (mo->leapx, mo->leapy, mo->landx, mo->landy)
	: R_PointToAngle2 (mo->x, mo->y, mo->landx, mo->landy);
    mo->momx = FixedMul (6*FRACUNIT, finecosine[an>>ANGLETOFINESHIFT]);
    mo->momy = FixedMul (6*FRACUNIT, finesine[an>>ANGLETOFINESHIFT]);
}

// Off the take-off for the landing (a climber's jump, Walk). Up a ledge, a
// mantle: straight up, high enough to clear it with some to spare, and over
// the edge only once high enough (P_DerpMantle; a push at the ledge's face
// from below would only stop it there). Down, or across a gap: one arc,
// fast enough across to be over the landing as it comes down. Gravity is a
// unit a tic each tic.
// A jump that did not land (P_DerpJumped): tried this often, it is no way
// for that climber for a while (Walk plans without it).
#define JUMP_TRIES	TUNE (JUMP_TRIES, 2)
#define JUMP_FORGET	TUNE (JUMP_FORGET, 30*TICRATE)
// Tics it takes to drift onto its take-off, rising (P_DerpMantle takes over).
#define LEAP_SETTLE	TUNE (LEAP_SETTLE, 4)
// How far a hop down gets before it is back at the height it left: past the
// edge and its own width, or it comes down on the lip with its middle out
// over the drop, and no step is allowed off a lip.
#define LEAP_CLEAR	TUNE (LEAP_CLEAR, 64)

// The lowest top of the openings on the way: a jump goes no higher than lets it
// in. Too high and its head is past the top as it gets there (E1M3 has a window
// exactly an imp's height), and it drops back, again and again.
static fixed_t	leaptop;

static boolean PTR_LeapTop (intercept_t* in)
{
    if (!(in->d.line->flags & ML_TWOSIDED))
	return false;
    P_LineOpening (in->d.line);
    if (opentop < leaptop)
	leaptop = opentop;
    return true;
}

static void Leap (mobj_t* actor)
{
    double	rise = (double)(actor->landz - actor->z)/FRACUNIT, up, t, across;
    double	clear = (double)(actor->leapsill - actor->z)/FRACUNIT, room;
    angle_t	an = R_PointToAngle2 (actor->x, actor->y, actor->landx, actor->landy);
    fixed_t	speed;

    // As high as the ledge or the sill between, and more: a show, even going
    // down. (Not much more: a window has a top.)
    leaptop = MAXINT;
    P_PathTraverse (actor->x, actor->y, actor->landx, actor->landy, PT_ADDLINES, PTR_LeapTop);
    room = (double)(leaptop - actor->height - actor->z)/FRACUNIT;
    up = clear > 0 ? sqrt (2*(clear + 26 < room ? clear + 26 : room > 4 ? room - 4 : 1)) : 7;
    t = up + sqrt (up*up - 2*rise);
    across = (double)P_AproxDistance (actor->landx - actor->x, actor->landy - actor->y)/FRACUNIT;
    speed = (fixed_t)(across/(t > 1 ? t : 1)*1.15*FRACUNIT);
    if (speed < 4*FRACUNIT)
	speed = 4*FRACUNIT;
    if (rise < -24 && speed < (fixed_t)(LEAP_CLEAR/(2*up)*FRACUNIT))
	speed = (fixed_t)(LEAP_CLEAR/(2*up)*FRACUNIT);
    if (speed > 20*FRACUNIT)
	speed = 20*FRACUNIT;
    if (clear > 24)
	speed = 0;	// over the edge (or the sill) later, from up there
    else
	actor->leapsill = MININT;	// one arc: no edge to get over (P_DerpMantle)
    actor->momx = FixedMul (speed, finecosine[an>>ANGLETOFINESHIFT]);
    actor->momy = FixedMul (speed, finesine[an>>ANGLETOFINESHIFT]);
    actor->momz = (fixed_t)(up*FRACUNIT);
    // Straight up, it drifts onto the take-off as it rises: it may have jumped from
    // anywhere near it, and the way through is laid out from there.
    if (clear > 24)
    {
	actor->momx = (actor->leapx - actor->x)/LEAP_SETTLE;
	actor->momy = (actor->leapy - actor->y)/LEAP_SETTLE;
    }
    actor->angle = an;
    actor->movedir = NODIR;
    actor->leapready = false;
    actor->leaptic = leveltime;
    actor->jumping = true;
    actor->waytic = -1000;
    if (clear > 24 && clear > rise + 8)
    {
	P_DerpCry (actor, BARK_IMPWINDOW);
	P_DerpBarkNear (actor->x, actor->y, 768, BARK_WINDOW);
    }
    P_DerpLog (actor, clear > 24 && clear > rise + 8 ? "jumps through a window (%d units up)" : rise > 24
	       ? "mantles up a ledge (%d units)" : "drops down (%d units)",
	       (int)(clear > 24 && clear > rise + 8 ? clear : rise));
}

// That jump is off: its failure noted, a new plan.
static void JumpOff (mobj_t* mo)
{
    mo->badtic = leveltime;
    mo->leapready = false;
    mo->waytic = -1000;
}

void P_DerpJumped (mobj_t* mo)
{
    fixed_t	d, from;

    // Still in the air (a jump takes off the tic after Leap), for up to 3 s.
    if (!mo->jumping || leveltime - mo->leaptic < 2
	|| (mo->z > mo->floorz && leveltime - mo->leaptic < 3*TICRATE) || OnSill (mo))
	return;
    mo->jumping = false;
    mo->landtic = leveltime;
    P_DerpCry (mo, BARK_IMPLAND);
    // Down where it meant to be: the landing's height, near the landing, and
    // nearer that than where it left (a short level jump through a window,
    // back on its own side, is within reach of the landing too).
    d = P_AproxDistance (mo->landx - mo->x, mo->landy - mo->y);
    from = P_AproxDistance (mo->leapx - mo->x, mo->leapy - mo->y);
    if (mo->z <= mo->floorz && d < 96*FRACUNIT && d < from
	&& mo->floorz > mo->landz - 24*FRACUNIT && mo->floorz < mo->landz + 24*FRACUNIT)
    {
	if (P_NavSameJump (mo->badfrom, mo->badto, mo->jumpfrom, mo->jumpto))
	    mo->badfails = 0;
	return;
    }
    // Not there (back down below a ledge, short of a gap, caught on
    // something): counted against that jump, and a new plan.
    if (mo->badfails && P_NavSameJump (mo->badfrom, mo->badto, mo->jumpfrom, mo->jumpto))
	mo->badfails++;
    else
    {
	mo->badfrom = mo->jumpfrom;
	mo->badto = mo->jumpto;
	mo->badfails = 1;
    }
    // Down on the lip, its middle out over the drop it meant: on over it.
    if (mo->badfails < JUMP_TRIES && mo->z <= mo->floorz && mo->landz < mo->floorz - 24*FRACUNIT
	&& mo->subsector->sector->floorheight < mo->floorz - 24*FRACUNIT)
    {
	P_DerpLog (mo, "hangs on the lip: on over it");
	Leap (mo);
	return;
    }
    JumpOff (mo);
    P_DerpLog (mo, mo->badfails >= JUMP_TRIES ? "a jump failed again: another way" : "a jump failed");
}

void P_DerpRouteBegin (mobj_t* actor)
{
    navclimb = P_DerpClimber (actor);
    if (navclimb && actor->badfails >= JUMP_TRIES && leveltime - actor->badtic < JUMP_FORGET)
    {
	navbadfrom = actor->badfrom;
	navbadto = actor->badto;
    }
}

void P_DerpRouteEnd (void)
{
    navclimb = false;
    navbadfrom = navbadto = -1;
}

// Another monster just in front on the way (its middle within 40 units of
// a point 40 ahead): a walker steps out to the side of it rather than file
// along behind (the conga line).
static mobj_t*	crowdself;
static mobj_t*	crowdby;	// the one in front, if crowded
static fixed_t	crowdx, crowdy;
static boolean	crowded;

static boolean PIT_Crowd (mobj_t* mo)
{
    if (mo == crowdself || !(mo->flags & MF_COUNTKILL) || mo->health <= 0)
	return true;
    if (P_AproxDistance (mo->x - crowdx, mo->y - crowdy) < 40*FRACUNIT)
    {
	crowded = true;
	crowdby = mo;
	return false;
    }
    return true;
}

// Whether mo stands in a doorway (a door cell).
static boolean InDoorway (int nav, mobj_t* mo)
{
    return nav >= 0 && P_NavDoor (nav, P_NavCellAt (nav, mo->x, mo->y));
}

// A door cell where it stands or 32 units on that way.
static boolean DoorAhead (int nav, mobj_t* actor, int dir)
{
    angle_t	an = (angle_t)dir << 29;
    fixed_t	x = actor->x + FixedMul (32*FRACUNIT, finecosine[an>>ANGLETOFINESHIFT]);
    fixed_t	y = actor->y + FixedMul (32*FRACUNIT, finesine[an>>ANGLETOFINESHIFT]);

    return nav >= 0 && (P_NavDoor (nav, P_NavCellAt (nav, actor->x, actor->y))
			|| P_NavDoor (nav, P_NavCellAt (nav, x, y)));
}

// Whether another monster stands within 40 units of (x, y).
static boolean CrowdedAt (mobj_t* actor, fixed_t x, fixed_t y)
{
    int		bx, by, xl, xh, yl, yh;

    crowdx = x;
    crowdy = y;
    crowdself = actor;
    crowded = false;
    xl = (crowdx - 40*FRACUNIT - bmaporgx)>>MAPBLOCKSHIFT;
    xh = (crowdx + 40*FRACUNIT - bmaporgx)>>MAPBLOCKSHIFT;
    yl = (crowdy - 40*FRACUNIT - bmaporgy)>>MAPBLOCKSHIFT;
    yh = (crowdy + 40*FRACUNIT - bmaporgy)>>MAPBLOCKSHIFT;
    for (bx = xl; bx <= xh && !crowded; bx++)
	for (by = yl; by <= yh && !crowded; by++)
	    P_BlockThingsIterator (bx, by, PIT_Crowd);
    return crowded;
}

static boolean Crowded (mobj_t* actor, int dir)
{
    angle_t	an = (angle_t)dir << 29;

    return CrowdedAt (actor, actor->x + FixedMul (40*FRACUNIT, finecosine[an>>ANGLETOFINESHIFT]),
		      actor->y + FixedMul (40*FRACUNIT, finesine[an>>ANGLETOFINESHIFT]));
}

// Held up behind a friend: a climber goes over it, to clear floor on the far side.
// In a corridor or on a walkway there is no way round, and they queued.
#define HOP_AFTER	TUNE (HOP_AFTER, 12)
#define HOP_AGAIN	TUNE (HOP_AGAIN, 3*TICRATE)

static boolean Hop (int nav, mobj_t* actor, fixed_t wx, fixed_t wy)
{
    static const int	reach[3] = { 128, 96, 160 };
    int			dir = DirTo (wx - actor->x, wy - actor->y), k;
    angle_t		an = (angle_t)dir << 29;

    if (!P_DerpClimber (actor) || actor->leapready || (actor->leaptic && leveltime - actor->leaptic < HOP_AGAIN)
	|| !Crowded (actor, dir))
	return false;
    for (k = 0; k < 3; k++)
    {
	fixed_t	x, y, z;
	if (!P_NavHop (nav, actor->x, actor->y, actor->x + FixedMul (reach[k]*FRACUNIT, finecosine[an>>ANGLETOFINESHIFT]),
		       actor->y + FixedMul (reach[k]*FRACUNIT, finesine[an>>ANGLETOFINESHIFT]), &x, &y, &z)
	    || CrowdedAt (actor, x, y))
	    continue;
	actor->leapx = actor->x;
	actor->leapy = actor->y;
	actor->landx = x;
	actor->landy = y;
	actor->landz = z;
	actor->leapsill = z;
	actor->jumpfrom = P_NavCellAt (nav, actor->x, actor->y);
	actor->jumpto = P_NavCellAt (nav, x, y);
	actor->leapready = true;
	P_DerpLog (actor, "hops past a friend in the way");
	return true;
    }
    return false;
}

boolean P_DerpCrowdedAt (mobj_t* actor, fixed_t x, fixed_t y)
{
    return CrowdedAt (actor, x, y);
}

// Along the route: down stairs it says are walkable (P_DerpStairs).
static void WalkTo (mobj_t* actor, fixed_t gx, fixed_t gy)
{
    // (A walk to as near as it gets is no route: that one took a monster out across a
    // pool it had no reason to be in.)
    derproute = !actor->nearestonly;
    Walk (actor, gx, gy);
    derproute = false;
}

// Walk's route to (gx, gy) into path (16 cells): the count, 0 for no way, or
// NAV_DEFERRED with the tic's routes spent (and then all as it was). A long
// route is kept whole and its next 16 cells handed out from there.
static int WalkRoute (mobj_t* actor, int nav, fixed_t gx, fixed_t gy, int* path)
{
    static int	longway[NAV_KEPT_CELLS];
    int		n, i;
    boolean	nearestonly = actor->nearestonly, partial = false, longer = false, straight;

    P_DerpRouteBegin (actor);
    // A long way found before and still good (p_navkept.h): on along it, no
    // search. A short one is searched again at each waypoint, as it always was.
    n = P_NavKeptNext (actor, nav, gx, gy, path, 16);
    if (n != 0)
    {
	P_DerpRouteEnd ();
	if (n != NAV_DEFERRED)
	    actor->nearestonly = false;
	return n;
    }
    actor->nearestonly = false;
    // Its last search to about there needed the long look: straight to that,
    // not 8,000 cells first to find out again. Its long look under way, over
    // tics: on with that, not the short one again each tic it waits.
    straight = P_NavKeptLong (actor, nav, gx, gy) || P_NavLongPending (actor, NAVLONG_WALK);
    // Its long look to there found no way a moment ago: not again yet, on
    // towards as near as it gets.
    if (P_NavKeptFailed (actor, gx, gy))
    {
	n = 0;
	straight = false;
	navexhausted = false;
	longer = true;
    }
    else
    n = straight ? 0 : P_NavPathFor (actor, nav, actor->x, actor->y, gx, gy, path, 16, NAV_KEPT_SHORT);
    // Out of search, not out of ways: a longer look before it says so. As
    // long as it was: at 20,000 the far routes of a big map (E2M2) were never
    // found, and their walkers asked again every few tics, 30,000 cells a
    // time for seconds on end. What it finds is kept and followed, so a long
    // route is one search of up to 40,000, not one each waypoint. And it
    // carries over tics (p_navlong.h): until it is done, on as it was.
    if (n == 0 && (straight || navexhausted))
    {
	n = P_NavLongPath (actor, NAVLONG_WALK, nav, actor->x, actor->y, gx, gy, longway, NAV_KEPT_CELLS, 40000);
	longer = n != NAV_DEFERRED;
	// (Found from where it stood some tics ago: on from where it stands.)
	if (n > 0 && navlonglate)
	    n = P_NavLongCatchUp (actor, nav, longway, n);
	if (longer)
	{
	    // (Gone straight to the long look and found it short, the walker
	    // nearly there: a short route again, searched as short ones are.)
	    boolean	islong = n == 0 || navlongcells > NAV_KEPT_SHORT;
	    P_NavKeptMarkLong (actor, nav, gx, gy, islong);
	    if (n == 0)
		P_NavKeptMarkFailed (actor, gx, gy);
	    if (n > 0 && islong)
		P_NavKeep (actor, nav, gx, gy, longway, n);
	}
	if (n > 16)
	    n = 16;
	for (i = 0; i < n; i++)
	    path[i] = longway[i];
    }
    // No way at all: as near as it can get, not standing where it heard
    // it. (They stood through whole fights, a wall from a noise.)
    if (n == 0 && P_AproxDistance (gx - actor->x, gy - actor->y) > 64*FRACUNIT)
    {
	// (On foot: no jump to a ledge that goes nowhere only to be nearer.)
	navclimb = false;
	navpartial = true;
	// After the longer look, a short one, and one that never waits: else the
	// two before it were spent for a wait, and spent again at its next step.
	if (longer)
	    n = P_NavPath (nav, actor->x, actor->y, gx, gy, path, 16, 2000);
	else
	    n = P_NavPathFor (actor, nav, actor->x, actor->y, gx, gy, path, 16, 8000);
	navpartial = false;
	partial = true;
    }
    P_DerpRouteEnd ();
    if (n == NAV_DEFERRED)
    {
	actor->nearestonly = nearestonly;
	return NAV_DEFERRED;
    }
    if (partial)
    {
	actor->nearestonly = true;
	derproute = false;
    }
    return n;
}

static void Walk (mobj_t* actor, fixed_t gx, fixed_t gy)
{
    static int	path[16];
    int		nav = P_NavClassOf (actor), dir, i, side, stuck, prev = actor->movedir, aside = 5;
    fixed_t	wx = gx, wy = gy;

    // There, within a step: it stops, rather than step past and back.
    if (P_AproxDistance (gx - actor->x, gy - actor->y) < P_DerpStep (actor))
    {
	actor->movedir = NODIR;
	return;
    }
    // A jump on its way (a climber's route, below): at its take-off, off it
    // goes; in the air, nothing to walk on.
    if (actor->z > actor->floorz || actor->jumping)
    {
	actor->movedir = NODIR;
	return;
    }
    if (actor->leapready
	&& P_AproxDistance (actor->leapx - actor->x, actor->leapy - actor->y) < 24*FRACUNIT)
    {
	// Someone on the landing: it would stop dead in the air and fall
	// short. It waits; two seconds of that and it looks for another.
	if (CrowdedAt (actor, actor->landx, actor->landy))
	{
	    actor->movedir = NODIR;
	    if (leveltime - actor->movedtic > 2*TICRATE)
	    {
		actor->badfrom = actor->jumpfrom;
		actor->badto = actor->jumpto;
		actor->badfails = JUMP_TRIES;
		JumpOff (actor);
		actor->movedtic = leveltime;
	    }
	    return;
	}
	// It gathers itself first, low, with a rasp: he sees it coming.
	if (!actor->leapwind || actor->leapwind + TICRATE < leveltime)
	{
	    actor->leapwind = leveltime + LEAP_WINDUP;
	    actor->angle = R_PointToAngle2 (actor->x, actor->y, actor->landx, actor->landy);
	    if (!P_DerpCry (actor, BARK_IMPWINDUP))
		S_StartSound (actor, sfx_bgact);
	}
	if (leveltime < actor->leapwind)
	{
	    actor->movedir = NODIR;
	    actor->movedtic = leveltime;
	    return;
	}
	actor->leapwind = 0;
	Leap (actor);
	return;
    }
    if (nav >= 0)
    {
	// No way there, found within the last second (its waypoint left where
	// it stands): it stays put, as below.
	boolean	noway = actor->wayx == actor->x && actor->wayy == actor->y;
	boolean	fresh = leveltime - actor->waytic <= 3*TICRATE;
	boolean	deferred = false;
	int	n = 0;
	// A waypoint some 8 cells on, the route asked for again when it is
	// near, blocked, or a few seconds have passed: not every second, as two
	// ways round a pillar may each be the better from one step to the next,
	// and a route asked for again each second flips between them. (The tic's
	// routes spent: on as it was, and asked again at its next step.)
	if ((!fresh || (!noway && P_AproxDistance (actor->wayx - actor->x, actor->wayy - actor->y) < 32*FRACUNIT))
	    && !(deferred = (n = WalkRoute (actor, nav, gx, gy, path)) == NAV_DEFERRED))
	{
	    // A climber (an imp) plans with jumps up and down ledges.
	    boolean	climber = P_DerpClimber (actor);
	    if (n == 0 && P_AproxDistance (gx - actor->x, gy - actor->y) > 64*FRACUNIT)
	    {
		// No way there (heard through a window, past a door it cannot
		// open): it stands facing that way, and asks again in a second,
		// rather than push at the wall.
		if (!noway)
		{
		    P_DerpLog (actor, "no way to where it is going: stands");
		    actor->nowaytic = leveltime;
		    actor->nowayx = gx;
		    actor->nowayy = gy;
		}
		if (actor->movecount > 0)
		    actor->movecount--;
		actor->wayx = actor->x;
		actor->wayy = actor->y;
		actor->waytic = leveltime;
		actor->movedir = NODIR;
		actor->angle = R_PointToAngle2 (actor->x, actor->y, gx, gy);
		return;
	    }
	    actor->leapready = false;
	    if (n > 1)
	    {
		int	k, j, upto = n > 9 ? 9 : n;
		// The first jump on the way (two cells with no step between):
		// its take-off is as far as the waypoint goes, the jump kept.
		for (j = 1; j < upto; j++)
		    if (climber && P_NavJump (nav, path[j - 1], path[j]))
		    {
			P_NavPoint (nav, path[j - 1], &actor->leapx, &actor->leapy, NULL);
			P_NavPoint (nav, path[j], &actor->landx, &actor->landy, &actor->landz);
			actor->jumpfrom = path[j - 1];
			actor->jumpto = path[j];
			actor->leapsill = P_NavJumpTop (nav, path[j - 1], path[j]);
			if (actor->leapsill < actor->landz)
			    actor->leapsill = actor->landz;
			actor->leapready = true;
			upto = j;
			break;
		    }
		k = upto > 1 ? Straight (nav, actor, path, upto) : 0;
		P_NavPoint (nav, path[k], &wx, &wy, NULL);
		// Not the middle of a cell beside it: aiming there zigzags.
		if (P_AproxDistance (wx - actor->x, wy - actor->y) < 20*FRACUNIT && k + 1 < upto)
		    P_NavPoint (nav, path[k + 1], &wx, &wy, NULL);
	    }
	    // A new waypoint (not the one it was stuck short of): a fresh start.
	    if (P_AproxDistance (wx - actor->wayx, wy - actor->wayy) > 32*FRACUNIT)
	    {
		actor->movedtic = leveltime;
		actor->waydist = MAXINT;
	    }
	    actor->wayx = wx;
	    actor->wayy = wy;
	    actor->waytic = leveltime;
	}
	else if (noway)
	{
	    // (Counting down to its next shot all the same, as a chase step does:
	    // they stood and glared at him and never threw anything.)
	    if (actor->movecount > 0)
		actor->movecount--;
	    actor->movedir = NODIR;
	    return;
	}
	else if (deferred && (!fresh || P_AproxDistance (actor->wayx - actor->x, actor->wayy - actor->y) < 32*FRACUNIT))
	{
	    // Its waypoint reached or stale, and no new one yet: at what it is
	    // going for, a step or two, as one with no route would.
	    wx = gx;
	    wy = gy;
	}
	else
	{
	    wx = actor->wayx;
	    wy = actor->wayy;
	}
    }
    // An opening ahead with its turn not come: it waits back from it, not in
    // the frame (p_portals.h). Waiting is no stall.
    {
	fixed_t	px, py;
	if (P_PortalWait (actor, wx, wy, &px, &py))
	{
	    // (And it may step away from its waypoint: back on its way, that
	    // is no lack of progress.)
	    actor->movedtic = leveltime;
	    actor->waydist = MAXINT;
	    if (P_AproxDistance (px - actor->x, py - actor->y) > 16*FRACUNIT)
	    {
		// Straight there, else round whoever is in the way: standing
		// where it is, it may be what the other one is waiting for.
		static const int	round[5] = { 0, 1, 7, 2, 6 };
		int	to = DirTo (px - actor->x, py - actor->y), k;
		for (k = 0; k < 5; k++)
		{
		    actor->movedir = (to + round[k]) & 7;
		    if (P_Move (actor))
			return;
		}
	    }
	    actor->movedir = NODIR;
	    return;
	}
    }
    // Getting nearer its waypoint (by 8 units: a shuffle along a wall gets a
    // few nearer each step and is not) is progress. None for a second (a door
    // opening, a pocket, a friend in the way): it stands rather than shuffle
    // to and fro (at a door it keeps trying the step: the door up, the
    // friend through, on at once), and asks for its route again each second;
    // none for three, it tries any way at all to get loose (below).
    {
	fixed_t	d = P_AproxDistance (wx - actor->x, wy - actor->y);
	if (d + 8*FRACUNIT < actor->waydist)
	{
	    actor->waydist = d;
	    actor->movedtic = leveltime;
	}
	stuck = leveltime - actor->movedtic;
	if (stuck > HOP_AFTER && Hop (nav, actor, wx, wy))
	{
	    actor->movedir = NODIR;
	    return;
	}
	if (stuck > TICRATE && stuck <= 3*TICRATE)
	{
	    if (leveltime - actor->waytic > TICRATE)
		actor->waytic = -1000;
	    // At a door, straight, else half aside (off the frame's corner),
	    // its own side first.
	    int	k, d = DirTo (wx - actor->x, wy - actor->y), s = (actor->serial & 1) ? 1 : 7;
	    for (k = 0; k < 3 && DoorAhead (nav, actor, d); k++)
	    {
		actor->movedir = (d + (k == 0 ? 0 : k == 1 ? s : 8 - s)) & 7;
		if (P_Move (actor))
		    return;
	    }
	    actor->movedir = NODIR;
	    return;
	}
    }
    dir = DirTo (wx - actor->x, wy - actor->y);
    side = (actor->serial & 1) ? 1 : 7;
    // One in front: out to a side first (which side by its number). At a
    // doorway, where aside is the wall, one through at a time, looking
    // further on: one in the opening and on the move, it waits outside
    // (waiting is no stall); two in it head to head, the higher number
    // backs off and stands clear, not back in at once (that was to and fro,
    // hundreds of times). One not moving for a couple of seconds is no
    // traffic to wait for.
    {
	boolean	door = DoorAhead (nav, actor, dir), near = Crowded (actor, dir);
	angle_t	an = (angle_t)dir << 29;
	if (near && !door)
	{
	    // Round it, not away from it: with no room for that (a narrow
	    // ridge) it stands behind it. A full step aside bounced it to
	    // and fro behind a friend for as long as the friend stood there.
	    // Meeting one coming the other way, or stopped, it keeps to its
	    // right whatever its number: each to its own number, two head on
	    // could step the same way and stand nose to nose for good.
	    int	facing = (crowdby->movedir - dir) & 7;
	    if (crowdby->movedir >= NODIR || (facing >= 3 && facing <= 5))
		side = 7;
	    dir = (dir + side) & 7;
	    aside = 3;
	}
	else if (door && (near || CrowdedAt (actor, actor->x + FixedMul (80*FRACUNIT, finecosine[an>>ANGLETOFINESHIFT]),
					     actor->y + FixedMul (80*FRACUNIT, finesine[an>>ANGLETOFINESHIFT])))
		 && InDoorway (nav, crowdby) && leveltime - crowdby->movedtic < 2*TICRATE)
	{
	    int	facing = (crowdby->movedir - dir) & 7;
	    if (!InDoorway (nav, actor))
	    {
		actor->movedir = NODIR;
		actor->movedtic = leveltime;
		return;
	    }
	    if (actor->serial > crowdby->serial && crowdby->movedir < NODIR && facing >= 3 && facing <= 5)
	    {
		if (!near)
		{
		    actor->movedir = NODIR;
		    return;
		}
		dir = (dir + 4) & 7;
	    }
	}
    }
    // Straight there, else half and then full aside, its own side first
    // (the same each step), but never a way aside that turns back on its
    // last step (a pocket's walls would bounce it to and fro). Nothing: it
    // stands, and only stuck for a second does it try any way at all to get
    // loose.
    for (i = 0; i < 9; i++)
    {
	static const int	turn[5] = { 0, 1, -1, 2, -2 };
	if (i < 5)
	{
	    if (i >= aside)
		continue;
	    actor->movedir = (dir + turn[i]*(side == 1 ? 1 : -1) + 8) & 7;
	    if (i && prev < NODIR && stuck <= TICRATE
		&& ((actor->movedir - prev) & 7) >= 3 && ((actor->movedir - prev) & 7) <= 5)
		continue;
	}
	else if (stuck > 3*TICRATE)
	    actor->movedir = P_Random () & 7;
	else
	    break;
	if (P_Move (actor))
	{
	    // Unseen, on its way somewhere: a step or two more (the director's
	    // offstage moves, never faster than the player runs).
	    int	k, more = hooks.steps ? hooks.steps (actor) : 1;
	    fixed_t	step = P_DerpStep (actor);
	    for (k = 1; k < more; k++)
	    {
		// Not past the waypoint: the heading is good only up to it.
		if (P_AproxDistance (wx - actor->x, wy - actor->y) < 2*step || !P_Move (actor))
		    break;
	    }
	    // As vanilla's chase step leaves it: a while before it may shoot.
	    // A way aside keeps the route (asking again each such step flips
	    // the waypoint between two cells); a random way to get loose does not.
	    actor->movecount = P_Random () & 15;
	    if (i >= 5)
		actor->waytic = -1000;
	    return;
	}
    }
    actor->movedir = NODIR;
    actor->waytic = -1000;
}


// There: within REACHED, or near and stuck a second (a friend already on
// the spot, the way to it full) after getting nearer since it was set:
// as near as it gets.
static boolean Reached (mobj_t* actor, fixed_t x, fixed_t y, int since)
{
    fixed_t	d = P_AproxDistance (actor->x - x, actor->y - y);

    return d < REACHED || (d < 128*FRACUNIT && actor->movedtic > since
			   && leveltime - actor->movedtic > TICRATE);
}


// Arrived where he was (or where it heard him) and found nothing: a look
// round first (MIND_SEARCH; tactic and tactictic are free outside the
// tactics' minds).
static void LookRound (mobj_t* actor)
{
    P_MindStep (actor, STEP_LOOKING, NULL, NULL);
    actor->tactictic = leveltime;
    P_DerpLog (actor, "nothing there: looks round");
    P_DerpBark (actor, BARK_NOTHING);
    P_DerpCry (actor, BARK_IMPSNIFF);
}

// Given up: back to watching where it stands, warier than before.
static void Settle (mobj_t* actor)
{
    P_MindBecome (actor, MIND_NONE, STEP_NONE, NULL, NULL);
    actor->target = NULL;
    actor->awareness = 500;
    P_SetMobjState (actor, actor->info->spawnstate);
}

// Home is where the map put it; one born in play (a pain elemental's soul, a boss
// brain's spawn) has none, and stays.
static void GoHome (mobj_t* actor)
{
    if (!actor->spawnpoint.type)
    {
	Settle (actor);
	return;
    }
    P_MindBecome (actor, MIND_RETURN, STEP_NONE, NULL, NULL);
    actor->goalx = actor->spawnpoint.x<<FRACBITS;
    actor->goaly = actor->spawnpoint.y<<FRACBITS;
    actor->goaltic = leveltime;
}


// Standing in something that hurts: out, before anything else it had in mind
// (it still shoots, that comes first in A_Chase).
static boolean GetOut (mobj_t* actor)
{
    fixed_t	x, y;

    if (!P_DerpBurning (actor) || !P_NavDryNear (P_NavClassOf (actor), actor->x, actor->y, &x, &y))
	return false;
    P_DerpWalkTo (actor, x, y);
    return true;
}

void P_SensesSaw (mobj_t* actor, mobj_t* player)
{
    actor->knownx = player->x;
    actor->knowny = player->y;
    actor->knowntic = leveltime;
}

void P_SensesBelieve (mobj_t* actor)
{
    // Never told where he is, only sent to look: it believes in where it was going,
    // or failing that where it stands. Left at nothing, its watching and searching
    // went to the map's origin (a zombie stared at it through a wall for ten seconds).
    if (!actor->knownx && !actor->knowny)
    {
	boolean	going = actor->goalx || actor->goaly;
	actor->knownx = going ? actor->goalx : actor->x;
	actor->knowny = going ? actor->goaly : actor->y;
    }
}

boolean P_SensesGetOut (mobj_t* actor)
{
    return GetOut (actor);
}

boolean P_SensesChase (mobj_t* actor)
{
    switch (actor->mind)
    {
      case MIND_NONE:
	// Woken some vanilla way with nothing known: as vanilla.
	return false;

      case MIND_HUNT:
	actor->goalx = actor->knownx;
	actor->goaly = actor->knowny;
	if (Reached (actor, actor->knownx, actor->knowny, actor->knowntic)
	    || leveltime - actor->knowntic > 10*TICRATE)
	{
	    P_MindBecome (actor, MIND_SEARCH, STEP_NONE, "hunt.lost_him",
			  "It reached where he was, or has had no word of him for a while: it looks round");
	    P_DerpBark (actor, BARK_LOSTYOU);
	    actor->goaltic = leveltime;
	    actor->searchtic = leveltime;
	    LookRound (actor);
	    PickSearch (actor);
	}
	break;

      case MIND_INVESTIGATE:
	// As near as it can get, and a wall in the way. It listens a moment, and
	// there is nothing for it to do here.
	if (actor->nowaytic && leveltime - actor->nowaytic >= NOWAY_LISTEN
	    && actor->wayx == actor->x && actor->wayy == actor->y)
	{
	    P_DerpRule (actor, "investigate.no_way", "As near as it can get and a wall in the way: "
			"nothing for it to do here, it goes back");
	    P_DerpLog (actor, "cannot get there: goes back");
	    GoHome (actor);
	    if (actor->mind == MIND_NONE)
		return true;
	    break;
	}
	if (Reached (actor, actor->goalx, actor->goaly, actor->goaltic)
	    || leveltime - actor->goaltic > 15*TICRATE)
	{
	    // Nothing here, and the shooting still going on: on towards it, not
	    // a look round in the middle of a corridor.
	    if (leveltime - lastshot < 2*TICRATE
		&& P_AproxDistance (lastshotx - actor->x, lastshoty - actor->y) > 192*FRACUNIT)
	    {
		P_DerpRule (actor, "investigate.shooting_on", "Nothing here and the shooting goes on: on "
			    "towards it");
		actor->goalx = lastshotx;
		actor->goaly = lastshoty;
		Rough (&actor->goalx, &actor->goaly,
		       P_AproxDistance (lastshotx - actor->x, lastshoty - actor->y)>>FRACBITS);
		actor->goaltic = leveltime;
		actor->waytic = -1000;
		break;
	    }
	    // Nothing there: search round it, as if he had been seen there.
	    P_MindBecome (actor, MIND_SEARCH, STEP_NONE, "investigate.nothing_there",
			  "It reached the place and found nothing: it looks round");
	    actor->knownx = actor->goalx;
	    actor->knowny = actor->goaly;
	    actor->knowntic = leveltime;
	    actor->searchtic = leveltime;
	    LookRound (actor);
	    PickSearch (actor);
	}
	break;

      case MIND_SEARCH:
	// Where he was, and nothing: it stands and looks one way, then the
	// other, before it searches (its search's time not used up meanwhile).
	if (actor->tactic == STEP_LOOKING)
	{
	    if (leveltime - actor->tactictic < LOOK_ROUND)
	    {
		actor->movedir = NODIR;
		actor->angle += (((leveltime - actor->tactictic)/16) & 1) ? ANG45/4 : (angle_t)-(ANG45/4);
		actor->searchtic = leveltime;
		actor->goaltic = leveltime;
		return true;
	    }
	    P_MindStep (actor, STEP_NONE, "search/looking.done", "It has looked both ways: now it searches");
	}
	// The player badly hurt and out of everyone's sight: it lingers, its
	// search held where it was (not used up while it waits).
	if (hooks.holdback && hooks.holdback ())
	{
	    actor->movedir = 8;
	    actor->searchtic++;
	    actor->goaltic++;
	    return true;
	}
	if (leveltime - actor->searchtic > SEARCH_TIME)
	{
	    if (!actor->spawnpoint.type)
	    {
		P_DerpRule (actor, "search.settles", "Given up, and it has no home (born in play): it "
			    "stays where it is");
		Settle (actor);
		return true;
	    }
	    P_DerpRule (actor, "search.gives_up", "Searched long enough: it goes home");
	    GoHome (actor);
	}
	else if (Reached (actor, actor->goalx, actor->goaly, actor->goaltic)
		 || leveltime - actor->goaltic > LOOK_AGAIN || actor->nearestonly)
	    PickSearch (actor);
	break;

      case MIND_RETURN:
	if (Reached (actor, actor->goalx, actor->goaly, actor->goaltic))
	{
	    // Home: back to watching as the map placed it.
	    actor->angle = ANG45*(actor->spawnpoint.angle/45);
	    P_DerpRule (actor, "return.home", "Home: back to watching as the map placed it");
	    Settle (actor);
	    return true;
	}
	// No way home (through a teleporter it came by, say): where it is will do.
	if (leveltime - actor->goaltic > RETURN_TIME)
	{
	    P_DerpRule (actor, "return.no_way_home", "Too long on its way home: where it is will do");
	    Settle (actor);
	    return true;
	}
	break;
    }
    WalkTo (actor, actor->goalx, actor->goaly);
    return true;
}


void P_DerpWalkTo (mobj_t* actor, fixed_t x, fixed_t y)
{
    WalkTo (actor, x, y);
}


void P_SensesDraw (void (*line)(fixed_t, fixed_t, fixed_t, fixed_t, int))
{
    thinker_t*	th;
    static int	asked = -1;

    if (asked < 0)
	asked = M_CheckParm ("-derpnav") != 0;
    if (!asked || !Senses ())
	return;
    for (th = thinkercap.next; th != &thinkercap; th = th->next)
    {
	mobj_t*	mo = (mobj_t *)th;
	if (th->function.acp1 != (actionf_p1)P_MobjThinker || !P_DerpMonster (mo)
	    || (mo->mind != MIND_INVESTIGATE && mo->mind != MIND_SEARCH && mo->mind != MIND_RETURN))
	    continue;
	line (mo->x, mo->y, mo->goalx, mo->goaly, 2);
    }
}
