// DERP's cover search (p_tactics.h): where a soldier shelters from him, the spot
// it leans out to, a hiding place out of his sight, a doorway to lie in wait by.

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "doomdef.h"
#include "doomstat.h"
#include "i_system.h"
#include "m_random.h"
#include "p_local.h"
#include "p_derp.h"
#include "p_nav.h"
#include "p_senses.h"
#include "p_tactics.h"
#include "p_decide.h"
#include "p_word.h"
#include "p_hang.h"
#include "p_derplog.h"
#include "p_corners.h"
#include "p_bullets.h"
#include "p_fire.h"
#include "p_director.h"
#include "p_portals.h"
#include "p_coverplan.h"
#include "p_gunwalk.h"
#include "p_roles.h"
#include "p_chatter.h"
#include "p_difficulty.h"
#include "p_barks.h"
#include "r_main.h"
#include "r_state.h"
#include "s_sound.h"
#include "tables.h"
#include "p_tune.h"
#include "p_mind.h"
#include "p_tactics_local.h"

#define COVER_REACH	TUNE (COVER_REACH, 320)
// The farthest walk a cover search floods for: 60 cells, some 12,000 cells of
// open floor at the very most.
#define COVER_FLOOD	960

// The walks from where it stands to the spots a cover search tries. A search
// each walked the same floor again, and one that failed took its whole limit:
// a lurker trying 24 corners a long way round cost 72,000 cells in one tic. So
// one flood of the floor round it (P_NavFlood) answers them: at once where the
// walk is capped anyway (eager), else once a search to one of them has found
// nothing, for that spot and every one after it. A spot farther than most on
// foot is then no walk: the game is too fast to go the long way round to cover.
typedef struct
{
    mobj_t*	actor;
    int		nav, most, limit;
    int		flooded;	// 0 not yet, 1 flooded, -1 no flood: a search each
    boolean	climb, eager;
} walks_t;

static void WalksBegin (walks_t* w, mobj_t* actor, int nav, int most, int limit, boolean eager)
{
    w->actor = actor;
    w->nav = nav;
    w->most = most;
    w->limit = limit;
    w->flooded = most > 0 ? 0 : -1;
    w->climb = P_DerpClimber (actor);
    w->eager = eager;
}

// The last flood, kept while no search has come since: the next cover search
// from the same spot (a retreat's then a plain one's, the doorways of an ambush
// one after another) asks it again rather than flood again.
// (navsearched unmoved since: no flood of anyone else's has taken its place.)
static fixed_t	lastx, lasty;
static int	lastnav = -1, lastmost;
static boolean	lastclimb;
static unsigned	lastsearched;

static boolean FloodFrom (mobj_t* actor, int nav, int most, boolean climb)
{
    boolean	flooded;

    if (nav == lastnav && actor->x == lastx && actor->y == lasty && climb == lastclimb && most <= lastmost
	&& navsearched == lastsearched && P_NavFloodWalk (nav, actor->x, actor->y) >= 0)
	return true;
    navclimb = climb;
    flooded = P_NavFlood (nav, actor->x, actor->y, most);
    navclimb = false;
    lastsearched = navsearched;
    lastnav = flooded ? nav : -1;
    lastx = actor->x;
    lasty = actor->y;
    lastmost = most;
    lastclimb = climb;
    return flooded;
}

static void WalksFlood (walks_t* w)
{
    w->flooded = FloodFrom (w->actor, w->nav, w->most, w->climb) ? 1 : -1;
}

// In units, -1 for no walk there. navexhausted says whether a search ran out.
static int WalksTo (walks_t* w, fixed_t x, fixed_t y)
{
    static int	path[2];
    int		walk;

    navexhausted = false;
    if (!w->flooded && w->eager)
	WalksFlood (w);
    if (w->flooded > 0)
    {
	walk = P_NavFloodWalk (w->nav, x, y);
	// (A flood kept from before may go farther than this one asks.)
	if (walk != -2)
	    return walk <= w->most ? walk : -1;
	// (Gone: a search since. A search each from here on.)
	w->flooded = -1;
    }
    navclimb = w->climb;
    walk = P_NavPath (w->nav, w->actor->x, w->actor->y, x, y, path, 2, w->limit) > 0 ? navwalk : -1;
    navclimb = false;
    if (walk < 0 && !w->flooded)
    {
	boolean	exhausted = navexhausted;
	WalksFlood (w);
	if (w->flooded > 0)
	{
	    walk = P_NavFloodWalk (w->nav, x, y);
	    if (walk > w->most)
		walk = -1;
	}
	navexhausted = exhausted && walk < 0;
    }
    return walk;
}

static mobj_t* Stand (fixed_t x, fixed_t y);
static boolean StaysHidden (fixed_t x, fixed_t y, fixed_t z, mobj_t* threat);
static int OpenFloor (int nav, fixed_t x, fixed_t y);
static int RoomAt (fixed_t x, fixed_t y);
static boolean HideCorner (mobj_t* actor, mobj_t* threat, boolean away, fixed_t* cx, fixed_t* cy);


// Whether it can walk to (x, y) with no part of the way in his sight. The game is
// too fast to run from cover to cover under his gun: it is a sure thing or it stays.
#define WALK_CELLS	48

boolean P_TacSafeWalk (mobj_t* actor, mobj_t* player, fixed_t x, fixed_t y)
{
    static int	path[WALK_CELLS];
    int		nav = P_NavClassOf (actor), n, i;
    fixed_t	qx, qy, qf;

    if (nav < 0 || P_CheckSight (player, actor))
	return false;
    n = P_NavPath (nav, actor->x, actor->y, x, y, path, WALK_CELLS, 3000);
    if (n <= 0 || n > WALK_CELLS)
	return false;
    for (i = 0; i < n; i += 2)
    {
	P_NavPoint (nav, path[i], &qx, &qy, &qf);
	if (P_CoverSeen (qx, qy, qf, player))
	    return false;
    }
    return !P_CoverSeen (x, y, R_PointInSubsector (x, y)->sector->floorheight, player);
}


//
// Places: cover, hiding, an ambush beside a doorway.
//

// Near actor, a standable spot threat cannot see; with peek, one with a
// spot beside it (two cells off) that threat can; with away, farther from
// threat than actor is. The nearest such that actor can walk to.

// Behind a corner of the map: the nearest wall end near actor it can get
// behind, a body's width off the wall and SHELTER back from the end, hidden
// from threat there, with a way to lean past the end that sees threat. Not
// one a soldier has already (a soldier in cover holds its spot).
#define CORNERS_TRIED	16

boolean P_TacCoverTaken (mobj_t* actor, fixed_t x, fixed_t y)
{
    thinker_t*	th;

    for (th = thinkercap.next; th != &thinkercap; th = th->next)
    {
	mobj_t*	mo = (mobj_t *)th;
	if (th->function.acp1 != (actionf_p1)P_MobjThinker || mo == actor || !P_DerpMonster (mo))
	    continue;
	// A place is taken while its owner is alive and using it: dead, or
	// on to another mind, nothing has to be let go.
	if ((mo->mind == MIND_COVER || mo->mind == MIND_AMBUSH || mo->mind == MIND_RETREAT)
	    && P_AproxDistance (mo->coverx - x, mo->covery - y) < 48*FRACUNIT)
	    return true;
	// And where it leans out to.
	if (mo->mind == MIND_COVER && P_AproxDistance (mo->peekx - x, mo->peeky - y) < 32*FRACUNIT)
	    return true;
    }
    return false;
}


// How good the last corner P_DerpCornerCover gave is.
static int	cornertier;

int P_DerpCornerTier (void)
{
    return cornertier;
}

// Someone standing there.
static mobj_t* Stand (fixed_t x, fixed_t y)
{
    static mobj_t	him;

    memset (&him, 0, sizeof him);
    him.x = x;
    him.y = y;
    him.subsector = R_PointInSubsector (x, y);
    him.z = him.subsector->sector->floorheight;
    him.height = 56*FRACUNIT;
    return &him;
}

// Hidden from a step to either side of him too: a place hidden from one exact
// spot is gone as soon as he moves.
#define STEP_ASIDE	TUNE (STEP_ASIDE, 64)

static boolean StaysHidden (fixed_t x, fixed_t y, fixed_t z, mobj_t* threat)
{
    angle_t	an = (R_PointToAngle2 (threat->x, threat->y, x, y) + ANG90)>>ANGLETOFINESHIFT;
    int		side;

    for (side = -1; side <= 1; side += 2)
    {
	mobj_t*	him = Stand (threat->x + side*STEP_ASIDE*finecosine[an], threat->y + side*STEP_ASIDE*finesine[an]);
	if (P_CheckSight (threat, him) && P_DerpExposedAt (x, y, z, him))
	    return false;
    }
    return true;
}

// Anything with its wall the right way round beats nothing. The best of the
// nearest few: hidden as he moves, hidden from where he is, hidden with no shot
// from it yet (he has to come round), hidden on one knee, or only a wall at its
// side; that last close by, since it is walked to in his sight. Out of its gun's
// range counts against one, as a half step down, and so does every so far of the
// walk there: the game is too fast to cross a room for something better.
#define ENOUGH_REACH	TUNE (ENOUGH_REACH, 160)
#define WALK_COST	TUNE (WALK_COST, 32)

// Whether it walks to the i-th of the nearest corners' spots: a search, until one
// finds nothing; then one flood (as walks_t's, but the lean-out searches between
// corners would end a flood kept for later, so every corner left is looked up in
// it at once). Out to twice the reach: corners are no farther than that as the
// crow flies.
static boolean CornerWalk (mobj_t* actor, int nav, const corner_t* list, const int* near, int found, int i,
			   fixed_t off, int* walkable, boolean* flooded)
{
    static int	path[2];
    fixed_t	px, py, pf, ex, ey;
    int		j, walk;

    if (walkable[i])
	return walkable[i] > 0;
    P_DerpCornerSpot (&list[near[i]], nav, off, &px, &py, &pf, &ex, &ey);
    if (P_NavPath (nav, actor->x, actor->y, px, py, path, 2, 3000) > 0)
	return true;
    if (*flooded)
	return false;
    *flooded = true;
    if (!FloodFrom (actor, nav, 2*COVER_REACH, false))
	return false;
    for (j = i; j < found; j++)
    {
	if (P_DerpCornerSpot (&list[near[j]], nav, off, &px, &py, &pf, &ex, &ey))
	    continue;
	walk = P_NavFloodWalk (nav, px, py);
	walkable[j] = walk >= 0 && walk <= 2*COVER_REACH ? 1 : walk == -2 ? 0 : -1;
    }
    return walkable[i] > 0;
}

boolean P_DerpCornerCover (mobj_t* actor, mobj_t* threat, fixed_t* cx, fixed_t* cy, fixed_t* kx, fixed_t* ky)
{
    const corner_t*	list;
    int			n = P_Corners (&list), nav = P_NavClassOf (actor), i, j, k, found = 0;
    int			near[CORNERS_TRIED], best = MAXINT, lo, hi, aside;
    int			walkable[CORNERS_TRIED];
    boolean		flooded = false;
    fixed_t		dist[CORNERS_TRIED];
    fixed_t		off;
    static int		path[2];

    if (nav < 0)
	return false;
    for (i = 0; i < CORNERS_TRIED; i++)
	walkable[i] = 0;
    off = actor->radius + 4*FRACUNIT;
    P_TacRange (actor, &lo, &hi, &aside);
    // The nearest few: a sight check or several each, so few.
    for (i = 0; i < n; i++)
    {
	fixed_t	d = P_AproxDistance (list[i].x - actor->x, list[i].y - actor->y);
	if (d > COVER_REACH*FRACUNIT || (found == CORNERS_TRIED && d >= dist[found - 1])
	    || !P_CornerFaces (&list[i], threat->x, threat->y))
	    continue;
	j = found < CORNERS_TRIED ? found++ : found - 1;
	for (; j > 0 && dist[j - 1] > d; j--)
	{
	    dist[j] = dist[j - 1];
	    near[j] = near[j - 1];
	}
	dist[j] = d;
	near[j] = i;
    }
    for (i = 0; i < found; i++)
    {
	const corner_t*	c = &list[near[i]];
	fixed_t		ex, ey, px, py, pf, lx, ly;
	boolean		over;
	int		tier, score;
	if (P_DerpCornerSpot (c, nav, off, &px, &py, &pf, &ex, &ey))
	    continue;
	// Hidden there, and a step towards the end and out from the wall. A
	// body on one knee does not sway about.
	if (!P_DerpExposedAt (px, py, pf, threat) && !P_DerpExposedAt (px - 12*c->dx, py - 12*c->dy, pf, threat)
	    && !P_DerpExposedAt (px + 12*c->nx, py + 12*c->ny, pf, threat))
	    tier = StaysHidden (px, py, pf, threat) ? TIER_SOLID : TIER_HIDDEN;
	else if (!P_DerpExposedLowAt (px, py, pf, threat))
	    tier = TIER_LOW;
	else if (P_AproxDistance (px - actor->x, py - actor->y) <= ENOUGH_REACH*FRACUNIT)
	    tier = TIER_ENOUGH;
	else
	    continue;
	if (tier*2 >= best)
	    continue;
	// Over the top, where it stands, if its gun clears it from there. Else
	// out to the end and a little past it, the least that sees him.
	over = tier >= TIER_LOW && P_CoverShoots (px, py, pf, threat);
	for (k = 0; !over && k <= 24; k += 12)
	{
	    fixed_t	qx = ex - k*c->dx, qy = ey - k*c->dy;
	    if (P_CoverShoots (qx, qy, pf, threat) && P_NavPath (nav, px, py, qx, qy, path, 2, 300) > 0)
		break;
	}
	if (!over && k > 24)
	{
	    if (tier > TIER_HIDDEN || TIER_WAITS*2 >= best)
		continue;
	    tier = TIER_WAITS;
	    k = 0;
	}
	if (P_TacCoverTaken (actor, px, py) || !CornerWalk (actor, nav, list, near, found, i, off, walkable, &flooded))
	    continue;
	lx = over ? px : ex - k*c->dx;
	ly = over ? py : ey - k*c->dy;
	score = tier*2 + (P_AproxDistance (lx - threat->x, ly - threat->y) > hi*FRACUNIT)
	    + (P_AproxDistance (px - actor->x, py - actor->y)>>FRACBITS)/WALK_COST;
	if (score >= best)
	    continue;
	best = score;
	cornertier = tier;
	*cx = px;
	*cy = py;
	*kx = lx;
	*ky = ly;
	if (!score)
	    break;
    }
    return best != MAXINT;
}

// Somewhere to hide: a corner with its wall between it and him, tucked in, not the
// first patch of floor he cannot see (a wounded imp stopped in the open, a little
// short of nowhere in particular).
#define HIDE_TRIED	TUNE (HIDE_TRIED, 24)
// A cell of open floor round the spot counts as this much walk, and another room
// than his as this much less: a corner on a long open wall hides from where he is
// and from nothing else (a zombie at 10 health, in full view of the way in).
#define NOOK_WEIGHT	TUNE (NOOK_WEIGHT, 6)
#define OTHER_ROOM	TUNE (OTHER_ROOM, 192)
#define OPEN_LOOK	12

static int OpenFloor (int nav, fixed_t x, fixed_t y)
{
    static const int	way[8][2] = { {1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}, {0, -1}, {1, -1} };
    int			i, k, n = 0;

    for (i = 0; i < 8; i++)
	for (k = 1; k <= OPEN_LOOK; k++)
	{
	    int	cell = P_NavCellAt (nav, x + k*way[i][0]*NAVCELL*FRACUNIT, y + k*way[i][1]*NAVCELL*FRACUNIT);
	    if (cell < 0 || !P_NavStandable (nav, cell))
		break;
	    n++;
	}
    return n;
}

static int RoomAt (fixed_t x, fixed_t y)
{
    return P_NavRoom ((int)(R_PointInSubsector (x, y)->sector - sectors));
}

static boolean HideCorner (mobj_t* actor, mobj_t* threat, boolean away, fixed_t* cx, fixed_t* cy)
{
    const corner_t*	list;
    int			n = P_Corners (&list), nav = P_NavClassOf (actor), i, tried = 0;
    fixed_t		reach = (away ? 2*COVER_REACH : COVER_REACH)*FRACUNIT, off;
    int			best = MAXINT, hisroom = RoomAt (threat->x, threat->y);
    fixed_t		here = P_AproxDistance (actor->x - threat->x, actor->y - threat->y);
    walks_t		walks;

    if (nav < 0)
	return false;
    off = actor->radius + 4*FRACUNIT;
    // No corner farther than reach on foot will do, so the flood goes no farther
    // and answers every corner tried exactly.
    WalksBegin (&walks, actor, nav, reach>>FRACBITS, 3000, true);
    for (i = 0; i < n && tried < HIDE_TRIED; i++)
    {
	const corner_t*	c = &list[i];
	fixed_t		d = P_AproxDistance (c->x - actor->x, c->y - actor->y), px, py, pf, ex, ey;
	int		reached, score;
	if (d > reach || !P_CornerFaces (c, threat->x, threat->y)
	    || P_DerpCornerSpot (c, nav, off, &px, &py, &pf, &ex, &ey))
	    continue;
	if (away && P_AproxDistance (px - threat->x, py - threat->y) <= here + 64*FRACUNIT)
	    continue;
	score = (d>>FRACBITS) + OpenFloor (nav, px, py)*NOOK_WEIGHT - (RoomAt (px, py) != hisroom ? OTHER_ROOM : 0);
	if (score >= best)
	    continue;
	tried++;
	if (P_DerpExposedAt (px, py, pf, threat) || P_DerpExposedAt (px - 12*c->dx, py - 12*c->dy, pf, threat)
	    || P_DerpExposedAt (px + 12*c->nx, py + 12*c->ny, pf, threat) || P_TacCoverTaken (actor, px, py))
	    continue;
	reached = WalksTo (&walks, px, py);
	// Near on foot, not as the crow flies: a corner in the yard below its
	// ledge was a walk round the building and out of a window (an imp on
	// E1M2, 1800 units from him, all of it out of his sight anyway).
	if (reached < 0 || reached > reach>>FRACBITS)
	    continue;
	score += reached - (d>>FRACBITS);
	if (score >= best)
	    continue;
	best = score;
	*cx = px;
	*cy = py;
    }
    return best != MAXINT;
}

boolean P_TacFindCover (mobj_t* actor, mobj_t* threat, boolean peek, boolean away,
			  fixed_t* cx, fixed_t* cy, fixed_t* kx, fixed_t* ky)
{
    // A place to lean out from: a corner of the map if one serves, else
    // what a search of the floor round it finds.
    if (peek && !away && P_DerpCornerCover (actor, threat, cx, cy, kx, ky))
    {
	int	tier = cornertier;
	// Only a wall at its side: anywhere the search finds out of his sight
	// is better. Failing that, it is better than nothing.
	if (tier == TIER_ENOUGH)
	{
	    fixed_t	fx, fy, gx, gy;
	    if (P_TacFindCoverNear (actor, actor->x, actor->y, threat, peek, away, &fx, &fy, &gx, &gy))
	    {
		*cx = fx;
		*cy = fy;
		*kx = gx;
		*ky = gy;
		tier = TIER_HIDDEN;
	    }
	}
	P_DerpLog (actor, tier == TIER_LOW ? "takes a low wall for cover"
		   : tier == TIER_ENOUGH ? "takes a wall at its side: better than nothing" : "takes a corner for cover");
	if (cx == &actor->coverx)
	    actor->lowcover = tier == TIER_LOW;
	return true;
    }
    if (!peek && HideCorner (actor, threat, away, cx, cy))
    {
	*kx = *cx;
	*ky = *cy;
    }
    else if (!P_TacFindCoverNear (actor, actor->x, actor->y, threat, peek, away, cx, cy, kx, ky))
	return false;
    if (cx == &actor->coverx)
	actor->lowcover = false;
    return true;
}

// As FindCover, round (ox, oy) rather than round actor.
boolean P_TacFindCoverNear (mobj_t* actor, fixed_t ox, fixed_t oy, mobj_t* threat, boolean peek,
			      boolean away, fixed_t* cx, fixed_t* cy, fixed_t* kx, fixed_t* ky)
{
    int		nav = P_NavClassOf (actor), dx, dy, tries = 0;
    // A retreat looks twice as far.
    int		reach = away ? 2*COVER_REACH : COVER_REACH, r = reach/NAVCELL, step = r/8 > 3 ? r/8 : 3;
    fixed_t	here = P_AproxDistance (actor->x - threat->x, actor->y - threat->y);
    int		cand[512], score[512], n = 0, i, j;
    // A route searched as far as the spots looked at need: farther for a
    // doorway some way off (FindAmbush searches round it, not round actor).
    int		apart = P_AproxDistance (ox - actor->x, oy - actor->y)>>FRACBITS;
    boolean	faroff = apart > 2*reach;
    // Flooded once a walk to one is not found: a cover reach past the farthest
    // spot, but no farther than a flood that stays a fraction of a tic's cells.
    // Not round a doorway some way off: that flood would be the whole way there.
    int		most = apart + reach + COVER_REACH < COVER_FLOOD ? apart + reach + COVER_REACH : COVER_FLOOD;
    walks_t	walks;

    if (nav < 0)
	return false;
    WalksBegin (&walks, actor, nav, faroff ? 0 : most, faroff ? 20000 : 3000, false);
    for (dy = -r; dy <= r; dy += step)
	for (dx = -r; dx <= r; dx += step)
	{
	    int		cell = P_NavCellAt (nav, ox + dx*NAVCELL*FRACUNIT, oy + dy*NAVCELL*FRACUNIT);
	    fixed_t	px, py, pf, d;
	    if (cell < 0 || P_NavDoor (nav, cell) || P_NavHurts (cell) || n == 512)
		continue;
	    P_NavPoint (nav, cell, &px, &py, &pf);
	    d = P_AproxDistance (px - ox, py - oy);
	    if (d > reach*FRACUNIT)
		continue;
	    if (away && P_AproxDistance (px - threat->x, py - threat->y) <= here + 64*FRACUNIT)
		continue;
	    cand[n] = cell;
	    score[n] = d>>FRACBITS;
	    n++;
	}
    // Nearest first.
    for (i = 1; i < n; i++)
	for (j = i; j > 0 && score[j] < score[j - 1]; j--)
	{
	    int	t = score[j]; score[j] = score[j - 1]; score[j - 1] = t;
	    t = cand[j]; cand[j] = cand[j - 1]; cand[j - 1] = t;
	}
    for (i = 0; i < n && tries < 24; i++)
    {
	fixed_t	px, py, pf;
	int	k;
	tries++;
	P_NavPoint (nav, cand[i], &px, &py, &pf);
	if (P_TacCoverTaken (actor, px, py))
	    continue;
	// Hidden there and a step round it: a body walking in steps of a dozen
	// units stops near the spot, not on it.
	if (P_DerpExposedAt (px, py, pf, threat) || P_DerpExposedAt (px + 16*FRACUNIT, py, pf, threat)
	    || P_DerpExposedAt (px - 16*FRACUNIT, py, pf, threat) || P_DerpExposedAt (px, py + 16*FRACUNIT, pf, threat)
	    || P_DerpExposedAt (px, py - 16*FRACUNIT, pf, threat))
	    continue;
	{
	fixed_t	qkx = px, qky = py;
	if (peek)
	{
	    static const int around[8][2] = {{2,0},{-2,0},{0,2},{0,-2},{2,2},{2,-2},{-2,2},{-2,-2}};
	    boolean	found = false;
	    for (k = 0; k < 8 && !found; k++)
	    {
		int	kc = P_NavCellAt (nav, px + around[k][0]*NAVCELL*FRACUNIT, py + around[k][1]*NAVCELL*FRACUNIT);
		fixed_t	qx, qy, qf;
		if (kc < 0 || kc == cand[i])
		    continue;
		P_NavPoint (nav, kc, &qx, &qy, &qf);
		if (P_CoverShoots (qx, qy, qf, threat))
		{
		    // Not a walk out to it: the least lean towards it that sees
		    // him (8 to 24 units out), else the spot itself.
		    int	t;
		    fixed_t	d = P_AproxDistance (qx - px, qy - py);
		    qkx = qx;
		    qky = qy;
		    for (t = 8; t <= 24 && d > 0; t += 8)
		    {
			fixed_t	lx = px + FixedMul (qx - px, FixedDiv (t*FRACUNIT, d));
			fixed_t	ly = py + FixedMul (qy - py, FixedDiv (t*FRACUNIT, d));
			if (P_CoverShoots (lx, ly, pf, threat))
			{
			    qkx = lx;
			    qky = ly;
			    break;
			}
		    }
		    found = true;
		}
	    }
	    if (!found)
		continue;
	}
	// Reachable (a doorway for an ambush may be some way off; a climber's
	// way may be a jump, through a window or off a ledge): the outputs are
	// written only for a spot that passed everything.
	// A doorway some way off that a search ran out on the way to: so would the
	// next spot beside it, 20,000 cells again each. The next doorway instead.
	if (WalksTo (&walks, px, py) < 0)
	{
	    if (faroff && navexhausted)
		return false;
	    continue;
	}
	*cx = px;
	*cy = py;
	*kx = qkx;
	*ky = qky;
	return true;
	}
    }
    return false;
}

boolean P_DerpHideSpot (mobj_t* actor, mobj_t* threat, boolean away, fixed_t* x, fixed_t* y)
{
    fixed_t	kx, ky;
    return P_TacFindCover (actor, threat, false, away, x, y, &kx, &ky);
}

// The player as actor believes him: himself if it sees him, else a stand-in
// where it last saw or heard him (it hides from where it thinks he is, and
// may hide on the wrong side of a pillar from where he really is).
mobj_t* P_TacBelieved (mobj_t* actor, mobj_t* player, boolean visible)
{
    return visible ? player : P_TacPoint (actor->knownx, actor->knowny);
}

boolean P_DerpHideFrom (mobj_t* actor, boolean visible, boolean away, fixed_t* x, fixed_t* y)
{
    mobj_t*	player = P_DerpPlayer ();
    fixed_t	kx, ky;
    return player && P_TacFindCover (actor, P_TacBelieved (actor, player, visible), false, away, x, y, &kx, &ky);
}

// The soldiers' rules for a lurker: hidden from where it thinks he is, and from a step
// aside of that; when it no longer is, somewhere that is, but only by a walk he sees
// none of.
int P_DerpLurkSlip (mobj_t* actor, boolean visible, fixed_t* x, fixed_t* y)
{
    mobj_t*	player = P_DerpPlayer ();
    mobj_t*	from;
    fixed_t	kx, ky;

    if (!player)
	return SLIP_HIDDEN;
    from = P_TacBelieved (actor, player, visible);
    if (!P_DerpExposedAt (actor->x, actor->y, actor->floorz, from)
	&& StaysHidden (actor->x, actor->y, actor->floorz, from))
	return SLIP_HIDDEN;
    if (!P_TacFindCover (actor, from, false, false, x, y, &kx, &ky) || P_TacAt (actor, *x, *y)
	|| !P_TacSafeWalk (actor, player, *x, *y))
	return SLIP_STAYS;
    return SLIP_GOES;
}

// Beside a doorway of actor's room, out of view of one coming through it
// (from a point some way outside it): he is seen as he steps through. The
// doorways nearest to (x, y), where the danger was: the nearest with anywhere
// to wait beside it (a door in the middle of a bare wall has nowhere).
#define DOORWAYS_TRIED	TUNE (DOORWAYS_TRIED, 3)

boolean P_TacFindAmbush (mobj_t* actor, fixed_t x, fixed_t y, fixed_t* ax, fixed_t* ay, angle_t* facing)
{
    const navlink_t*	links;
    int			n = P_NavLinks (&links), i, room, tried;
    fixed_t		last = -1;
    fixed_t		kx, ky;

    room = P_NavRoom (actor->subsector->sector - sectors);
    if (room < 0)
	return false;
    for (tried = 0; tried < DOORWAYS_TRIED; tried++)
    {
	int	best = -1;
	fixed_t	bestd = MAXINT;
	for (i = 0; i < n; i++)
	{
	    line_t*	ld = &lines[links[i].line];
	    fixed_t	mx = ld->v1->x/2 + ld->v2->x/2, my = ld->v1->y/2 + ld->v2->y/2, d;
	    if (links[i].rooma != room && links[i].roomb != room)
		continue;
	    d = P_AproxDistance (mx - x, my - y);
	    if (d > last && d < bestd)
	    {
		bestd = d;
		best = i;
	    }
	}
	if (best < 0)
	    return false;
	last = bestd;
	{
	    line_t*	ld = &lines[links[best].line];
	    fixed_t	mx = ld->v1->x/2 + ld->v2->x/2, my = ld->v1->y/2 + ld->v2->y/2;
	    fixed_t	len = P_AproxDistance (ld->dx, ld->dy), nx, ny, ox, oy, ix, iy;
	    if (len < FRACUNIT)
		continue;
	    // The normal to the line's front; outside is the side not in the room.
	    nx = FixedDiv (ld->dy, len);
	    ny = -FixedDiv (ld->dx, len);
	    if (P_NavRoom (ld->frontsector - sectors) == room)
	    {
		nx = -nx;
		ny = -ny;
	    }
	    ox = mx + FixedMul (nx, 96*FRACUNIT);
	    oy = my + FixedMul (ny, 96*FRACUNIT);
	    ix = mx - FixedMul (nx, 64*FRACUNIT);
	    iy = my - FixedMul (ny, 64*FRACUNIT);
	    if (!P_TacFindCoverNear (actor, ix, iy, P_TacPoint (ox, oy), false, false, ax, ay, &kx, &ky))
		continue;
	    *facing = R_PointToAngle2 (*ax, *ay, mx, my);
	    return true;
	}
    }
    return false;
}

// Its cover still serves: hidden there from him (and a step round it),
// and the fight still on (it saw him in the last few seconds). A cover
// that serves is kept: a soldier changes place for a reason (he came
// close, he flanked it, the fight went quiet), not for a better spot.
boolean P_TacCoverServes (mobj_t* actor, mobj_t* player)
{
    fixed_t	z;

    if (!actor->coverx && !actor->covery)
	return false;
    if (!actor->knowntic || leveltime - actor->knowntic > COVER_BELIEF)
	return false;
    z = R_PointInSubsector (actor->coverx, actor->covery)->sector->floorheight;
    return !(actor->lowcover ? P_DerpExposedLowAt : P_DerpExposedAt) (actor->coverx, actor->covery, z, player)
	&& P_AproxDistance (player->x - actor->coverx, player->y - actor->covery) >= 160*FRACUNIT;
}

//
// Doing.
//
boolean P_TacAt (mobj_t* actor, fixed_t x, fixed_t y)
{
    return P_AproxDistance (actor->x - x, actor->y - y) < 24*FRACUNIT;
}

// In its cover. A low wall's shadow is thin: near enough for a corner is still
// in his sight, so right up to the spot, unless something stops it short.
boolean P_TacInCover (mobj_t* actor)
{
    return P_TacAt (actor, actor->coverx, actor->covery)
	&& (!actor->lowcover || P_AproxDistance (actor->x - actor->coverx, actor->y - actor->covery) < 12*FRACUNIT
	    || leveltime - actor->movedtic > TICRATE/2);
}


// Eyes on where it thinks he will come from, not the wall it walked up to.
void P_DerpFaceDanger (mobj_t* actor)
{
    if (actor->knownx || actor->knowny)
	actor->angle = R_PointToAngle2 (actor->x, actor->y, actor->knownx, actor->knowny);
}

// Tucked in behind what it hid at: a corner hides only right up to it. Near enough
// for a spot in the open is still in his sight here.
boolean P_DerpTucked (mobj_t* actor)
{
    fixed_t	d = P_AproxDistance (actor->x - actor->coverx, actor->y - actor->covery);

    return d < 12*FRACUNIT || (d < 32*FRACUNIT && leveltime - actor->movedtic > TICRATE/2);
}

boolean P_DerpHide (mobj_t* actor, mobj_t* from, fixed_t* x, fixed_t* y)
{
    fixed_t	kx, ky;

    return P_TacFindCover (actor, from, false, false, x, y, &kx, &ky);
}
