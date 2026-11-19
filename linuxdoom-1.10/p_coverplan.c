//
// DERP's cover plan (p_coverplan.h).
//

#include <stdlib.h>
#include <string.h>

#include "doomdef.h"
#include "doomstat.h"
#include "p_local.h"
#include "p_corners.h"
#include "p_coverplan.h"
#include "p_nav.h"
#include "p_cover.h"
#include "r_main.h"
#include "r_state.h"
#include "z_zone.h"

// Lines of one way in lie this close together.
#define SAME_DOOR	128
// A mouth is this far inside the room, and the ground he crosses next this far on.
#define MOUTH_IN	32
#define GROUND_ON	96
// A shot further in than that, this far on and this far again: he is coming.
#define COMING_ON	224
#define COMING_MORE	352
// Too near the mouth is on top of him, too far no fight.
#define SPOT_NEAR	64
#define SPOT_FAR	768
// The best spot is about this far from the mouth, and a list this long is plenty.
#define SPOT_BEST	256
#define SPOTS_MOST	32
// Through a door costs this, on top of the walk to it.
#define THROUGH		64
// A second way in counts while its walk is no more than a quarter longer than
// the first, and this much: too close to call.
#define NEARLY_PLUS	128
#define FAR_AWAY	MAXINT
#define STEP_MOST	24
// Either side of a way in no wider than this: this far along the wall from the frame,
// one further than the other (not straight at each other), this far off it. Hidden
// from out there, straight on and to either side (windows), this far off.
#define FLANK_WIDE	256
#define FLANK_NEAR	44
#define FLANK_FAR	76
#define FLANK_OFF	28
#define FLANK_OUT	96
#define FLANK_OUT_FAR	224
#define FLANK_ASIDE	192

// One side of a way in: the other side is door^1.
typedef struct
{
    fixed_t	x, y;		// the mouth
    fixed_t	dx, dy;		// into the room (a unit long)
    fixed_t	half;		// half as wide as it is
    int		room;
    sector_t*	sector;		// the room's sector at the door
    boolean	door;		// a door shuts it
    sector_t*	doorsector;
    planspot_t*	spots;
    int		numspots;
} door_t;

static door_t*	doors;
static int	numdoors;
static fixed_t*	walked;		// P_CoverPlanDoors' workspace
static boolean*	settled;

static int RoomAt (fixed_t x, fixed_t y)
{
    return P_NavRoom ((int)(R_PointInSubsector (x, y)->sector - sectors));
}

static fixed_t Far (fixed_t ax, fixed_t ay, fixed_t bx, fixed_t by)
{
    return P_AproxDistance (ax - bx, ay - by);
}

// Sides a and a^1 of a line: rooma in front of it, roomb behind.
static void AddSides (const navlink_t* link, fixed_t mx, fixed_t my, fixed_t half)
{
    line_t*	ld = &lines[link->line];
    fixed_t	l = P_AproxDistance (ld->dx, ld->dy);
    fixed_t	nx = FixedDiv (ld->dy, l), ny = -FixedDiv (ld->dx, l);
    int		side;

    for (side = 0; side < 2; side++)
    {
	door_t*	d = &doors[numdoors++];
	d->dx = side ? -nx : nx;
	d->dy = side ? -ny : ny;
	d->half = half;
	d->x = mx + FixedMul (d->dx, MOUTH_IN*FRACUNIT);
	d->y = my + FixedMul (d->dy, MOUTH_IN*FRACUNIT);
	d->room = side ? link->roomb : link->rooma;
	d->sector = side ? ld->backsector : ld->frontsector;
	d->door = link->door;
	d->doorsector = NULL;
	if (link->door)
	    d->doorsector = P_NavSectorDoor ((int)(ld->backsector - sectors)) ? ld->backsector : ld->frontsector;
	d->spots = NULL;
	d->numspots = 0;
    }
}

static void FindDoors (void)
{
    const navlink_t*	links;
    int			n = P_NavLinks (&links), i, j;
    int*		used = Z_Malloc (sizeof *used*(n + 1), PU_STATIC, NULL);

    doors = Z_Malloc (sizeof *doors*2*(n + 1), PU_LEVEL, NULL);
    numdoors = 0;
    memset (used, 0, sizeof *used*(n + 1));
    for (i = 0; i < n; i++)
    {
	line_t*	ld = &lines[links[i].line];
	double	sx, sy;
	int	count = 1;
	fixed_t	lox, loy, hix, hiy;
	if (used[i] || P_AproxDistance (ld->dx, ld->dy) < FRACUNIT)
	    continue;
	sx = (ld->v1->x/2 + ld->v2->x/2)/65536.0;
	sy = (ld->v1->y/2 + ld->v2->y/2)/65536.0;
	lox = ld->v1->x < ld->v2->x ? ld->v1->x : ld->v2->x;
	hix = ld->v1->x > ld->v2->x ? ld->v1->x : ld->v2->x;
	loy = ld->v1->y < ld->v2->y ? ld->v1->y : ld->v2->y;
	hiy = ld->v1->y > ld->v2->y ? ld->v1->y : ld->v2->y;
	// The lines of one way in make one door, at the middle of them.
	for (j = i + 1; j < n; j++)
	{
	    line_t*	lj = &lines[links[j].line];
	    fixed_t	mx = lj->v1->x/2 + lj->v2->x/2, my = lj->v1->y/2 + lj->v2->y/2;
	    if (used[j] || !((links[j].rooma == links[i].rooma && links[j].roomb == links[i].roomb)
			     || (links[j].rooma == links[i].roomb && links[j].roomb == links[i].rooma))
		|| Far (mx, my, ld->v1->x/2 + ld->v2->x/2, ld->v1->y/2 + ld->v2->y/2) > SAME_DOOR*FRACUNIT)
		continue;
	    used[j] = 1;
	    sx += mx/65536.0;
	    sy += my/65536.0;
	    count++;
	    if (lj->v1->x < lox) lox = lj->v1->x;
	    if (lj->v2->x < lox) lox = lj->v2->x;
	    if (lj->v1->x > hix) hix = lj->v1->x;
	    if (lj->v2->x > hix) hix = lj->v2->x;
	    if (lj->v1->y < loy) loy = lj->v1->y;
	    if (lj->v2->y < loy) loy = lj->v2->y;
	    if (lj->v1->y > hiy) hiy = lj->v1->y;
	    if (lj->v2->y > hiy) hiy = lj->v2->y;
	}
	AddSides (&links[i], (fixed_t)(sx/count*65536.0), (fixed_t)(sy/count*65536.0),
		  P_AproxDistance (hix - lox, hiy - loy)/2);
    }
    Z_Free (used);
}

// A stand-in for him at the mouth, or further in.
static mobj_t* Him (fixed_t x, fixed_t y)
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

// The best cover first, and of those the one at the best distance.
static int Worse (const planspot_t* a, const planspot_t* b, const door_t* d)
{
    fixed_t	da = abs (Far (a->x, a->y, d->x, d->y) - SPOT_BEST*FRACUNIT);
    fixed_t	db = abs (Far (b->x, b->y, d->x, d->y) - SPOT_BEST*FRACUNIT);

    return a->rank != b->rank ? a->rank > b->rank : da > db;
}

// Whether a gun at (x, y) on floor z has a line on him this far into the room.
static boolean Reaches (const door_t* d, fixed_t on, fixed_t x, fixed_t y, fixed_t z)
{
    fixed_t	gx = d->x + FixedMul (d->dx, on), gy = d->y + FixedMul (d->dy, on);

    return RoomAt (gx, gy) == d->room && P_DerpShootsAt (x, y, z, Him (gx, gy));
}

// How good corner c is against door d, and where it fires from; -1 no use.
static int Rate (const door_t* d, const corner_t* c, fixed_t px, fixed_t py, fixed_t pf, fixed_t ex, fixed_t ey,
		 fixed_t* kx, fixed_t* ky)
{
    fixed_t	gx = d->x + FixedMul (d->dx, GROUND_ON*FRACUNIT), gy = d->y + FixedMul (d->dy, GROUND_ON*FRACUNIT);
    boolean	hidden, over;
    int		k;

    if (RoomAt (gx, gy) != d->room)
    {
	gx = d->x;
	gy = d->y;
    }
    hidden = !P_DerpExposedAt (px, py, pf, Him (d->x, d->y));
    // Out to the end and a little past it, the least that sees the ground.
    for (k = 0; k <= 24; k += 12)
    {
	*kx = ex - k*c->dx;
	*ky = ey - k*c->dy;
	if (P_DerpShootsAt (*kx, *ky, pf, Him (gx, gy)) || P_DerpShootsAt (*kx, *ky, pf, Him (d->x, d->y)))
	    break;
    }
    if (hidden)
    {
	if (k <= 24)
	    return PLAN_ENTRANCE;
	// No shot at the mouth: one at him further in, or it is no use to anyone.
	for (k = 0; k <= 24; k += 12)
	{
	    fixed_t	on = COMING_ON*FRACUNIT, more = COMING_MORE*FRACUNIT;
	    *kx = ex - k*c->dx;
	    *ky = ey - k*c->dy;
	    if (Reaches (d, on, *kx, *ky, pf) || Reaches (d, more, *kx, *ky, pf))
		return PLAN_HIDDEN;
	}
	return -1;
    }
    over = P_DerpShootsAt (px, py, pf, Him (gx, gy));
    if (k > 24 && !over)
	return -1;
    if (over)
    {
	*kx = px;
	*ky = py;
    }
    return P_DerpExposedLowAt (px, py, pf, Him (d->x, d->y)) ? PLAN_ENOUGH : PLAN_LOW;
}

// Whether (px, py) on floor pf is hidden from one standing out there, if he can.
static boolean HiddenFrom (int nav, fixed_t ox, fixed_t oy, fixed_t px, fixed_t py, fixed_t pf)
{
    int	cell = P_NavCellAt (nav, ox, oy);

    return cell < 0 || !P_NavStandable (nav, cell) || !P_DerpExposedAt (px, py, pf, Him (ox, oy));
}

// Against the wall beside the way in, on side (1 or -1), along units from the frame.
// (No corner there, and the mouth is past the frame: the plain list never has it.)
static boolean Flank (const door_t* d, int nav, int side, int along, planspot_t* spot)
{
    // The line itself, and along it.
    fixed_t	lx = d->x - FixedMul (d->dx, MOUTH_IN*FRACUNIT), ly = d->y - FixedMul (d->dy, MOUTH_IN*FRACUNIT);
    fixed_t	ax = -d->dy*side, ay = d->dx*side;
    fixed_t	on = d->half + along*FRACUNIT;
    fixed_t	x = lx + FixedMul (ax, on) + FixedMul (d->dx, FLANK_OFF*FRACUNIT);
    fixed_t	y = ly + FixedMul (ay, on) + FixedMul (d->dy, FLANK_OFF*FRACUNIT);
    fixed_t	nearx = lx - FixedMul (d->dx, FLANK_OUT*FRACUNIT), neary = ly - FixedMul (d->dy, FLANK_OUT*FRACUNIT);
    fixed_t	farx = lx - FixedMul (d->dx, FLANK_OUT_FAR*FRACUNIT), fary = ly - FixedMul (d->dy, FLANK_OUT_FAR*FRACUNIT);
    fixed_t	sidex = FixedMul (ax, FLANK_ASIDE*FRACUNIT), sidey = FixedMul (ay, FLANK_ASIDE*FRACUNIT);
    fixed_t	pf;
    int		cell = P_NavCellAt (nav, x, y);

    if (cell < 0 || !P_NavStandable (nav, cell))
	return false;
    P_NavPoint (nav, cell, &x, &y, &pf);
    if (RoomAt (x, y) != d->room || !P_DerpShootsAt (x, y, pf, Him (d->x, d->y))
	|| !HiddenFrom (nav, nearx, neary, x, y, pf) || !HiddenFrom (nav, farx, fary, x, y, pf)
	|| !HiddenFrom (nav, farx + sidex, fary + sidey, x, y, pf)
	|| !HiddenFrom (nav, farx - sidex, fary - sidey, x, y, pf))
	return false;
    spot->x = spot->kx = x;
    spot->y = spot->ky = y;
    spot->corner = -1;
    spot->rank = PLAN_FLANK;
    return true;
}

static void FillDoor (door_t* d, int nav, fixed_t off)
{
    const corner_t*	list;
    int			n = P_Corners (&list), i, j, found = 0;
    planspot_t		spots[SPOTS_MOST];

    for (i = 0; i < n; i++)
    {
	const corner_t*	c = &list[i];
	fixed_t		px, py, pf, ex, ey, near;
	planspot_t	spot;
	if (!P_CornerFaces (c, d->x, d->y) || P_DerpCornerSpot (c, nav, off, &px, &py, &pf, &ex, &ey)
	    || RoomAt (px, py) != d->room)
	    continue;
	near = Far (px, py, d->x, d->y);
	if (near < SPOT_NEAR*FRACUNIT || near > SPOT_FAR*FRACUNIT)
	    continue;
	spot.rank = Rate (d, c, px, py, pf, ex, ey, &spot.kx, &spot.ky);
	if (spot.rank < 0)
	    continue;
	spot.x = px;
	spot.y = py;
	spot.corner = i;
	// In order as it goes, the worst falling off the end.
	if (found == SPOTS_MOST && Worse (&spot, &spots[found - 1], d))
	    continue;
	j = found < SPOTS_MOST ? found++ : found - 1;
	for (; j > 0 && Worse (&spots[j - 1], &spot, d); j--)
	    spots[j] = spots[j - 1];
	spots[j] = spot;
    }
    // Beside it, first on the list.
    if (d->half <= FLANK_WIDE/2*FRACUNIT)
	for (i = 0; i < 2; i++)
	{
	    planspot_t	spot;
	    if (!Flank (d, nav, i ? -1 : 1, i ? FLANK_FAR : FLANK_NEAR, &spot))
		continue;
	    j = found < SPOTS_MOST ? found++ : found - 1;
	    for (; j > 0; j--)
		spots[j] = spots[j - 1];
	    spots[0] = spot;
	}
    if (!found)
	return;
    d->spots = Z_Malloc (sizeof *d->spots*found, PU_LEVEL, NULL);
    memcpy (d->spots, spots, sizeof *d->spots*found);
    d->numspots = found;
}

void P_CoverPlanLevel (void)
{
    int	nav = P_NavClassFor (20, 56), i;

    doors = NULL;
    numdoors = 0;
    if (nav < 0)
	return;
    FindDoors ();
    walked = Z_Malloc (sizeof *walked*(numdoors + 1), PU_LEVEL, NULL);
    settled = Z_Malloc (sizeof *settled*(numdoors + 1), PU_LEVEL, NULL);
    for (i = 0; i < numdoors; i++)
	FillDoor (&doors[i], nav, 24*FRACUNIT);
}

// Whether the door sector takes a key he has not got.
static boolean Locked (const sector_t* sec)
{
    const player_t*	p = &players[consoleplayer];
    int			i;

    for (i = 0; i < sec->linecount; i++)
	switch (sec->lines[i]->special)
	{
	  case 26: case 32: case 99: case 133:
	    if (!p->cards[it_bluecard] && !p->cards[it_blueskull])
		return true;
	    break;
	  case 27: case 34: case 136: case 137:
	    if (!p->cards[it_yellowcard] && !p->cards[it_yellowskull])
		return true;
	    break;
	  case 28: case 33: case 134: case 135:
	    if (!p->cards[it_redcard] && !p->cards[it_redskull])
		return true;
	    break;
	}
    return false;
}

// Whether he can go from one side of a way in to the other.
static boolean Passes (const door_t* from, const door_t* to)
{
    if (from->door)
	return !P_NavSectorSecret ((int)(from->doorsector - sectors)) && !Locked (from->doorsector);
    return to->sector->floorheight - from->sector->floorheight <= STEP_MOST*FRACUNIT;
}

int P_CoverPlanDoors (int room, fixed_t hx, fixed_t hy, int* out, int most)
{
    int		from = RoomAt (hx, hy), i, n = 0;
    fixed_t	least = FAR_AWAY;

    if (!numdoors || room < 0 || from < 0 || from == room)
	return 0;
    // The walk to every door, by way of the rooms; never through this one, or
    // the far door would look like a way in.
    for (i = 0; i < numdoors; i++)
    {
	walked[i] = doors[i].room == from ? Far (doors[i].x, doors[i].y, hx, hy) : FAR_AWAY;
	settled[i] = false;
    }
    for (;;)
    {
	int	best = -1, twin;
	for (i = 0; i < numdoors; i++)
	    if (!settled[i] && walked[i] != FAR_AWAY && (best < 0 || walked[i] < walked[best]))
		best = i;
	if (best < 0)
	    break;
	settled[best] = true;
	if (doors[best].room == room)
	    continue;
	twin = best^1;
	if (!settled[twin] && Passes (&doors[best], &doors[twin]) && walked[best] + THROUGH*FRACUNIT < walked[twin])
	    walked[twin] = walked[best] + THROUGH*FRACUNIT;
	// Across the room it is in.
	for (i = 0; i < numdoors; i++)
	    if (doors[i].room == doors[best].room && !settled[i])
	    {
		fixed_t	d = walked[best] + Far (doors[i].x, doors[i].y, doors[best].x, doors[best].y);
		if (d < walked[i])
		    walked[i] = d;
	    }
    }
    for (i = 0; i < numdoors; i++)
	if (doors[i].room == room && walked[i] < least)
	    least = walked[i];
    if (least == FAR_AWAY)
	return 0;
    // Nearest first; a short list, a short sort.
    for (i = 0; i < numdoors; i++)
    {
	int	j;
	if (doors[i].room != room || walked[i] == FAR_AWAY
	    || walked[i] > least + least/4 + NEARLY_PLUS*FRACUNIT)
	    continue;
	j = n < most ? n++ : n - 1;
	for (; j > 0 && walked[out[j - 1]] > walked[i]; j--)
	    out[j] = out[j - 1];
	out[j] = i;
    }
    return n;
}

int P_CoverPlanSpots (int door, const planspot_t** list)
{
    *list = doors[door].spots;
    return doors[door].numspots;
}

void P_CoverPlanMouth (int door, fixed_t* x, fixed_t* y)
{
    *x = doors[door].x;
    *y = doors[door].y;
}
