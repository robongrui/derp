//
// Where DERP's monsters can go (p_nav.h).
//

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

#include "doomdef.h"
#include "doomstat.h"
#include "i_system.h"
#include "m_argv.h"
#include "m_bbox.h"
#include "p_local.h"
#include "p_spec.h"
#include "p_derp.h"
#include "p_derptiming.h"
#include "p_nav.h"
#include "p_navbudget.h"
#include "p_navkept.h"
#include "p_navlong.h"
#include "p_encounter.h"
#include "r_main.h"
#include "r_state.h"
#include "z_zone.h"

int		navbuildms;

// A cell's floor when nothing stands there.
#define SOLID		(-32768)
#define MAXCLASSES	8
// Cell flags: near a teleporter line.
#define NAV_TELE	2
// Cell flags: its floor hurts (nukage, slime, lava; P_DerpFloor's).
#define NAV_HURT	4
// What a cell whose floor hurts costs over its 10: a walker goes round it
// if it reasonably can.
#define HURT_COST	300
// How far, in cells, a body looks for dry floor (P_NavDryNear).
#define DRY_REACH	32
// What a door costs to go through, over a cell's 10 (a monster waits on it).
#define DOOR_COST	40
// A door sector (doorsector, a cell's door): one a monster opens, and one
// with a secret face, which only a climber (an imp) opens from that side
// (P_NavStep).
#define DOOR_OPENS	1
#define DOOR_SECRET	2
// A room's opening to another, at least this long in all, makes them one.
#define WIDE		128

typedef struct
{
    int			radius, height;	// units
    short*		floor;		// SOLID, or the floor where it stands
    short*		ceil;		// the lowest ceiling over it there
    signed char*	off;		// where in the cell: dx, dy from its middle
    byte*		door;		// whether it fits only with a door open (DOOR_*)
} navclass_t;

static boolean		ready;
static int		gridw, gridh;
static fixed_t		gridx, gridy;	// the corner of cell 0
static byte*		flags;
static navclass_t	classes[MAXCLASSES];
static int		numclasses;

// The heights the grid goes by: a door a monster opens stands open.
static fixed_t*		navfloor;
static fixed_t*		navceil;
static byte*		doorsector;	// DOOR_* bits
static line_t**		secretfaces;	// the secret door lines (special 1)
static int		numsecretfaces;
static byte*		moved;
static boolean		anymoved;
// The heights as the level loaded, doors open: what the rooms go by, so a
// lift halfway up does not regroup them.
static fixed_t*		roomfloor;
static fixed_t*		roomceil;

// Lines a monster that crosses them is teleported by.
static line_t**		teles;
static int		numteles;

static int*		roomof;
static int		numrooms;
static navlink_t*	links;
static int		numlinks;


//
// Memory: outside the zone (big maps need more than its 16 MB leave), all
// of a build's freed at once when the next is built.
//
static void**		allocs;
static int		numallocs, maxallocs;

static void* NavAlloc (size_t size)
{
    void*	p = malloc (size ? size : 1);

    if (!p)
	I_Error ("P_Nav: out of memory");
    if (numallocs == maxallocs)
    {
	maxallocs = maxallocs ? 2*maxallocs : 32;
	allocs = realloc (allocs, sizeof *allocs*maxallocs);
	if (!allocs)
	    I_Error ("P_Nav: out of memory");
    }
    allocs[numallocs++] = p;
    return p;
}

static int*	climbto[MAXCLASSES];
static short*	climbtop_[MAXCLASSES];	// the height each jump must clear (units)
static int*	climbgen[MAXCLASSES];

static void NavFreeAll (void)
{
    int		i;

    while (numallocs)
	free (allocs[--numallocs]);
    for (i = 0; i < MAXCLASSES; i++)
    {
	climbto[i] = climbgen[i] = NULL;
	climbtop_[i] = NULL;
    }
}


//
// The game's own rules for where a body fits (P_CheckPosition,
// PIT_CheckLine), without the things that move.
//
static fixed_t	fitbox[4];
static fixed_t	fitfloor, fitceil, fitdrop;
static boolean	fitblocked;
static int	fitdoor;	// the doors it fits only with open (doorsector's bits)

static boolean FitLine (line_t* ld)
{
    int		f, b;
    fixed_t	top, bottom, low;

    if (fitbox[BOXRIGHT] <= ld->bbox[BOXLEFT]
	|| fitbox[BOXLEFT] >= ld->bbox[BOXRIGHT]
	|| fitbox[BOXTOP] <= ld->bbox[BOXBOTTOM]
	|| fitbox[BOXBOTTOM] >= ld->bbox[BOXTOP])
	return true;
    if (P_BoxOnLineSide (fitbox, ld) != -1)
	return true;
    if (!ld->backsector || (ld->flags & (ML_BLOCKING|ML_BLOCKMONSTERS)))
    {
	fitblocked = true;
	return false;
    }
    f = ld->frontsector - sectors;
    b = ld->backsector - sectors;
    fitdoor |= doorsector[f] | doorsector[b];
    top = navceil[f] < navceil[b] ? navceil[f] : navceil[b];
    bottom = navfloor[f] > navfloor[b] ? navfloor[f] : navfloor[b];
    low = navfloor[f] < navfloor[b] ? navfloor[f] : navfloor[b];
    if (top < fitceil)
	fitceil = top;
    if (bottom > fitfloor)
	fitfloor = bottom;
    if (low < fitdrop)
	fitdrop = low;
    return true;
}

// Whether (x, y) lies inside sector's outline: a ray from it to the east
// crosses its lines an odd number of times.
static boolean InSector (fixed_t x, fixed_t y, sector_t* sec)
{
    int		i;
    boolean	in = false;
    double	px = (double)x, py = (double)y;

    for (i = 0; i < sec->linecount; i++)
    {
	line_t*	ld = sec->lines[i];
	double	x1 = ld->v1->x, y1 = ld->v1->y, x2 = ld->v2->x, y2 = ld->v2->y;
	// A line with the sector on both sides is no edge of it.
	if (ld->frontsector == ld->backsector)
	    continue;
	if ((y1 > py) != (y2 > py) && px < x1 + (py - y1)*(x2 - x1)/(y2 - y1))
	    in = !in;
    }
    return in;
}

// Whether (x, y) is outside the map, in the void: the BSP still finds a
// subsector there, but the point lies behind one of its walls (one-sided
// segs: only they bound the void), and outside its sector's outline. A
// node builder calls a subsector convex when its segs face each other,
// though its area may reach past a seg's end, behind the seg's line: by
// that alone a strip across E1M1's zigzag bridge was void (two-sided), and
// cells along walls on many maps (one-sided). A sector whose outline does
// not close counts every point as inside: never void by mistake.
static boolean Void (fixed_t x, fixed_t y, subsector_t* sub)
{
    int		i;

    for (i = 0; i < sub->numlines; i++)
    {
	seg_t*	sg = &segs[sub->firstline + i];
	if (!sg->backsector && R_PointOnSegSide (x, y, sg))
	    return !InSector (x, y, sub->sector);
    }
    return false;
}

// A thing the grid goes round: solid, not a monster or a player (they
// move and step aside): columns, lamps, trees, and barrels, whose going
// (they explode) or moving (blasts push them) P_NavThink follows.
static boolean Blocker (mobj_t* mo)
{
    return mo->thinker.function.acp1 == (actionf_p1)P_MobjThinker
	&& (mo->flags & MF_SOLID) && !(mo->flags & (MF_COUNTKILL|MF_NOBLOCKMAP))
	&& !mo->player;
}

// One in the way of the body at fitbox: box against box, as the game's
// things touch (PIT_CheckThing).
static boolean FitThing (mobj_t* mo)
{
    fixed_t	r = (fitbox[BOXRIGHT] - fitbox[BOXLEFT])/2;
    fixed_t	x = fitbox[BOXLEFT] + r, y = fitbox[BOXBOTTOM] + r;

    if (!Blocker (mo) || abs (mo->x - x) >= mo->radius + r || abs (mo->y - y) >= mo->radius + r)
	return true;
    fitblocked = true;
    return false;
}

// Whether a body of radius and height fits at (x, y), clear of walls and
// of the solid things that stay put (each spot tried in a cell judged on
// its own: one touching a barrel no longer marks a cell with room beside
// it); its floor and the ceiling over it if so.
static boolean Fits (fixed_t x, fixed_t y, fixed_t radius, fixed_t height, fixed_t* floor, fixed_t* ceil)
{
    int		s, bx, by, xl, xh, yl, yh;
    subsector_t*	sub = R_PointInSubsector (x, y);

    if (Void (x, y, sub))
	return false;
    s = sub->sector - sectors;
    fitfloor = fitdrop = navfloor[s];
    fitceil = navceil[s];
    fitblocked = false;
    fitdoor = doorsector[s];
    fitbox[BOXTOP] = y + radius;
    fitbox[BOXBOTTOM] = y - radius;
    fitbox[BOXRIGHT] = x + radius;
    fitbox[BOXLEFT] = x - radius;
    xl = (fitbox[BOXLEFT] - bmaporgx)>>MAPBLOCKSHIFT;
    xh = (fitbox[BOXRIGHT] - bmaporgx)>>MAPBLOCKSHIFT;
    yl = (fitbox[BOXBOTTOM] - bmaporgy)>>MAPBLOCKSHIFT;
    yh = (fitbox[BOXTOP] - bmaporgy)>>MAPBLOCKSHIFT;
    validcount++;
    for (bx = xl; bx <= xh; bx++)
	for (by = yl; by <= yh; by++)
	    if (!P_BlockLinesIterator (bx, by, FitLine))
		return false;
    // Things are in the block of their middle: as P_CheckPosition, the
    // blocks a thing's whole radius round the box.
    xl = (fitbox[BOXLEFT] - bmaporgx - MAXRADIUS)>>MAPBLOCKSHIFT;
    xh = (fitbox[BOXRIGHT] - bmaporgx + MAXRADIUS)>>MAPBLOCKSHIFT;
    yl = (fitbox[BOXBOTTOM] - bmaporgy - MAXRADIUS)>>MAPBLOCKSHIFT;
    yh = (fitbox[BOXTOP] - bmaporgy + MAXRADIUS)>>MAPBLOCKSHIFT;
    for (bx = xl; bx <= xh; bx++)
	for (by = yl; by <= yh; by++)
	    if (!P_BlockThingsIterator (bx, by, FitThing))
		return false;
    if (fitblocked || fitceil - fitfloor < height || fitfloor - fitdrop > 24*FRACUNIT)
	return false;
    *floor = fitfloor;
    *ceil = fitceil;
    return true;
}


// Where to try in a cell, nearest its middle first: every 4 units to 8 off,
// so a gap only 4 units wider than the body (a pinky in a 64-unit door) is
// still found.
static const signed char tries[][2] =
{
    {0,0}, {4,0}, {-4,0}, {0,4}, {0,-4}, {8,0}, {-8,0}, {0,8}, {0,-8},
    {4,4}, {-4,4}, {4,-4}, {-4,-4}, {8,4}, {8,-4}, {-8,4}, {-8,-4},
    {4,8}, {-4,8}, {4,-8}, {-4,-8}, {8,8}, {-8,8}, {8,-8}, {-8,-8}
};

// Nothing stands there: and nothing else said of it either, so the same map
// builds the same bytes.
static void Solid (navclass_t* c, int cell)
{
    c->floor[cell] = c->ceil[cell] = SOLID;
    c->door[cell] = 0;
    c->off[cell*2] = c->off[cell*2+1] = 0;
}

static void BuildCell (navclass_t* c, int cell)
{
    fixed_t	x = gridx + (cell % gridw)*NAVCELL*FRACUNIT + NAVCELL/2*FRACUNIT;
    fixed_t	y = gridy + (cell / gridw)*NAVCELL*FRACUNIT + NAVCELL/2*FRACUNIT;
    fixed_t	floor, ceil;
    int		i;

    // A body more than 8 units round that fits within 8 units covers the
    // middle, and a body over the void touches a wall: a middle in the void,
    // nothing fits. Most of a map's box is void.
    if (c->radius > 8 && Void (x, y, R_PointInSubsector (x, y)))
    {
	Solid (c, cell);
	return;
    }
    for (i = 0; i < (int)(sizeof tries/sizeof tries[0]); i++)
	if (Fits (x + tries[i][0]*FRACUNIT, y + tries[i][1]*FRACUNIT,
		  c->radius*FRACUNIT, c->height*FRACUNIT, &floor, &ceil))
	{
	    c->floor[cell] = floor>>FRACBITS;
	    c->ceil[cell] = ceil>>FRACBITS;
	    c->door[cell] = fitdoor;
	    c->off[cell*2] = tries[i][0];
	    c->off[cell*2+1] = tries[i][1];
	    return;
	}
    Solid (c, cell);
}


static int	navgen = 1;	// the grid's build: climbs are kept for one

int P_NavBuild (void)
{
    return navgen;
}

static void BuildArea (navclass_t* c, int x0, int y0, int x1, int y1)
{
    int		cx, cy;

    navgen++;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 >= gridw) x1 = gridw - 1;
    if (y1 >= gridh) y1 = gridh - 1;
    for (cy = y0; cy <= y1; cy++)
	for (cx = x0; cx <= x1; cx++)
	    BuildCell (c, cy*gridw + cx);
}


static void BuildClass (navclass_t* c)
{
    size_t	cells = (size_t)gridw*gridh;

    c->floor = NavAlloc (cells*sizeof *c->floor);
    c->ceil = NavAlloc (cells*sizeof *c->ceil);
    c->off = NavAlloc (cells*2);
    c->door = NavAlloc (cells);
    BuildArea (c, 0, 0, gridw - 1, gridh - 1);
}


int P_NavClassFor (int radius, int height)
{
    int		i;
    derp_timing_group previous;

    if (!ready)
	return -1;
    for (i = 0; i < numclasses; i++)
	if (classes[i].radius == radius && classes[i].height == height)
	    return i;
    if (numclasses == MAXCLASSES)
	return -1;
    classes[numclasses].radius = radius;
    classes[numclasses].height = height;
    previous = P_DerpTimingBegin (DERP_TIMING_NAV);
    BuildClass (&classes[numclasses]);
    P_DerpTimingEnd (previous);
    return numclasses++;
}


int P_NavClassOf (mobj_t* mo)
{
    if (mo->flags & (MF_FLOAT|MF_NOGRAVITY))
	return -1;
    return P_NavClassFor (mo->info->radius/FRACUNIT, mo->info->height/FRACUNIT);
}


//
// The heights the grid goes by, and which sectors are doors a monster
// opens: the back of a manual door line (vanilla's monsters open type 1;
// DERP's open no locked door). One with a secret face is a secret door: the
// game's monsters never open that face, so a walker's route goes in only
// from its other side; an imp's either way (it finds the catch).
//
static void SectorHeights (int s)
{
    sector_t*	sec = &sectors[s];

    navfloor[s] = sec->floorheight;
    navceil[s] = sec->ceilingheight;
    if (doorsector[s])
    {
	// As EV_VerticalDoor opens it.
	fixed_t	open = P_FindLowestCeilingSurrounding (sec) - 4*FRACUNIT;
	if (open > navceil[s])
	    navceil[s] = open;
    }
}


static boolean Teleporter (line_t* ld)
{
    return ld->special == 39 || ld->special == 97
	|| ld->special == 125 || ld->special == 126;
}


static void MarkTeleporters (void)
{
    int		i, cx, cy;

    numteles = 0;
    teles = NavAlloc (sizeof *teles*(numlines + 1));
    for (i = 0; i < numlines; i++)
    {
	line_t*	ld = &lines[i];
	int	x0, x1, y0, y1;

	if (!Teleporter (ld))
	    continue;
	teles[numteles++] = ld;
	// Every cell a step across it could start or end in.
	x0 = ((ld->bbox[BOXLEFT] - gridx)/FRACUNIT - 2*NAVCELL)/NAVCELL;
	x1 = ((ld->bbox[BOXRIGHT] - gridx)/FRACUNIT + 2*NAVCELL)/NAVCELL;
	y0 = ((ld->bbox[BOXBOTTOM] - gridy)/FRACUNIT - 2*NAVCELL)/NAVCELL;
	y1 = ((ld->bbox[BOXTOP] - gridy)/FRACUNIT + 2*NAVCELL)/NAVCELL;
	for (cy = y0 < 0 ? 0 : y0; cy <= y1 && cy < gridh; cy++)
	    for (cx = x0 < 0 ? 0 : x0; cx <= x1 && cx < gridw; cx++)
		flags[cy*gridw + cx] |= NAV_TELE;
    }
}


// The cells whose floor hurts (the sector at their middle: nukage, slime,
// lava, as P_DerpFloor bites).
static void MarkHurts (void)
{
    int		cell, cells = gridw*gridh;

    for (cell = 0; cell < cells; cell++)
    {
	fixed_t	x = gridx + (cell % gridw)*NAVCELL*FRACUNIT + NAVCELL/2*FRACUNIT;
	fixed_t	y = gridy + (cell / gridw)*NAVCELL*FRACUNIT + NAVCELL/2*FRACUNIT;
	switch (R_PointInSubsector (x, y)->sector->special)
	{
	  case 4: case 5: case 7: case 16:
	    flags[cell] |= NAV_HURT;
	    break;
	}
    }
}

// A jump from a cell or two along is the same jump, through the same window.
#define SAME_JUMP	6

boolean P_NavSameJump (int a, int b, int c, int d)
{
    return a >= 0 && b >= 0 && c >= 0 && d >= 0
	&& abs (a % gridw - c % gridw) <= SAME_JUMP && abs (a / gridw - c / gridw) <= SAME_JUMP
	&& abs (b % gridw - d % gridw) <= SAME_JUMP && abs (b / gridw - d / gridw) <= SAME_JUMP;
}

boolean P_NavHurts (int cell)
{
    return ready && cell >= 0 && cell < gridw*gridh && (flags[cell] & NAV_HURT);
}

boolean P_NavDryNear (int nav, fixed_t x, fixed_t y, fixed_t* dx, fixed_t* dy)
{
    int		cell = P_NavCellAt (nav, x, y), cx, cy, r, i, j;

    if (cell < 0)
	return false;
    cx = cell % gridw;
    cy = cell / gridw;
    for (r = 1; r <= DRY_REACH; r++)
	for (j = -r; j <= r; j++)
	    for (i = -r; i <= r; i++)
	    {
		int	tx = cx + i, ty = cy + j, t = ty*gridw + tx;
		if ((abs (i) != r && abs (j) != r) || tx < 0 || ty < 0 || tx >= gridw || ty >= gridh
		    || !P_NavStandable (nav, t) || (flags[t] & NAV_HURT)
		    || abs (classes[nav].floor[t] - classes[nav].floor[cell]) > 24)
		    continue;
		P_NavPoint (nav, t, dx, dy, NULL);
		return true;
	    }
    return false;
}


//
// The rooms: union-find over the sectors.
//
static int Root (int s)
{
    while (roomof[s] != s)
	s = roomof[s] = roomof[roomof[s]];
    return s;
}

typedef struct { int a, b; fixed_t length; } opening_t;

static int CompareOpenings (const void* p, const void* q)
{
    const opening_t	*a = p, *b = q;
    if (a->a != b->a)
	return a->a < b->a ? -1 : 1;
    if (a->b != b->b)
	return a->b < b->b ? -1 : 1;
    return 0;
}

// Whether a monster walks through the line between two sectors (the
// smallest walker: a zombie's 56 units, a step of 24).
static boolean Walkable (line_t* ld)
{
    int		f = ld->frontsector - sectors, b = ld->backsector - sectors;
    fixed_t	top = roomceil[f] < roomceil[b] ? roomceil[f] : roomceil[b];
    fixed_t	bottom = roomfloor[f] > roomfloor[b] ? roomfloor[f] : roomfloor[b];

    return !(ld->flags & (ML_BLOCKING|ML_BLOCKMONSTERS)) && top - bottom >= 56*FRACUNIT
	&& abs (roomfloor[f] - roomfloor[b]) <= 24*FRACUNIT;
}

static void BuildRooms (void)
{
    opening_t*	open = NavAlloc (sizeof *open*(numlines + 1));
    int		n = 0, i, j, s;
    int*	number;

    roomof = NavAlloc (sizeof *roomof*numsectors);
    for (s = 0; s < numsectors; s++)
	roomof[s] = s;
    // The wide walkable openings between two sectors neither a door.
    for (i = 0; i < numlines; i++)
    {
	line_t*	ld = &lines[i];
	int	a, b;
	if (!ld->frontsector || !ld->backsector || ld->frontsector == ld->backsector || !Walkable (ld))
	    continue;
	a = ld->frontsector - sectors;
	b = ld->backsector - sectors;
	if (doorsector[a] || doorsector[b])
	    continue;
	open[n].a = a < b ? a : b;
	open[n].b = a < b ? b : a;
	open[n].length = P_AproxDistance (ld->dx, ld->dy);
	n++;
    }
    qsort (open, n, sizeof *open, CompareOpenings);
    for (i = 0; i < n; i = j)
    {
	fixed_t	length = 0;
	for (j = i; j < n && open[j].a == open[i].a && open[j].b == open[i].b; j++)
	    length += open[j].length;
	if (length >= WIDE*FRACUNIT)
	{
	    int	ra = Root (open[i].a), rb = Root (open[i].b);
	    // The lower number the root, so the rooms come out the same each build.
	    if (ra < rb)
		roomof[rb] = ra;
	    else if (rb < ra)
		roomof[ra] = rb;
	}
    }
    // Numbered in sector order.
    number = NavAlloc (sizeof *number*numsectors);
    for (s = 0; s < numsectors; s++)
	number[s] = -1;
    numrooms = 0;
    for (s = 0; s < numsectors; s++)
    {
	int	r = Root (s);
	if (number[r] < 0)
	    number[r] = numrooms++;
    }
    for (s = 0; s < numsectors; s++)
	roomof[s] = number[Root (s)];
    // The links: every walkable line, or door line, between two rooms.
    links = NavAlloc (sizeof *links*(numlines + 1));
    numlinks = 0;
    for (i = 0; i < numlines; i++)
    {
	line_t*	ld = &lines[i];
	int	a, b;
	boolean	door;
	if (!ld->frontsector || !ld->backsector)
	    continue;
	a = ld->frontsector - sectors;
	b = ld->backsector - sectors;
	if (roomof[a] == roomof[b])
	    continue;
	door = doorsector[a] || doorsector[b];
	if (!door && !Walkable (ld))
	    continue;
	links[numlinks].line = i;
	links[numlinks].rooma = roomof[a];
	links[numlinks].roomb = roomof[b];
	links[numlinks].door = door;
	numlinks++;
    }
}


static void RecordBlockers (void);

static void BuildAll (boolean fresh)
{
    int		i, s, numwalkers = 0;
    fixed_t	minx = MAXINT, miny = MAXINT, maxx = MININT, maxy = MININT;
    int		sizes[MAXCLASSES][2];
    clock_t	start = clock ();
    thinker_t*	th;

    for (i = 0; i < numvertexes; i++)
    {
	if (vertexes[i].x < minx) minx = vertexes[i].x;
	if (vertexes[i].x > maxx) maxx = vertexes[i].x;
	if (vertexes[i].y < miny) miny = vertexes[i].y;
	if (vertexes[i].y > maxy) maxy = vertexes[i].y;
    }
    gridx = (minx & ~(FRACUNIT - 1)) - 2*NAVCELL*FRACUNIT;
    gridy = (miny & ~(FRACUNIT - 1)) - 2*NAVCELL*FRACUNIT;
    gridw = (maxx - gridx)/(NAVCELL*FRACUNIT) + 3;
    gridh = (maxy - gridy)/(NAVCELL*FRACUNIT) + 3;
    NavFreeAll ();
    flags = NavAlloc ((size_t)gridw*gridh);
    memset (flags, 0, (size_t)gridw*gridh);

    navfloor = NavAlloc (sizeof *navfloor*numsectors);
    navceil = NavAlloc (sizeof *navceil*numsectors);
    doorsector = NavAlloc (numsectors);
    secretfaces = NavAlloc (sizeof *secretfaces*(numlines + 1));
    numsecretfaces = 0;
    moved = NavAlloc (numsectors);
    memset (doorsector, 0, numsectors);
    memset (moved, 0, numsectors);
    anymoved = false;
    for (i = 0; i < numlines; i++)
	if (lines[i].special == 1 && lines[i].backsector)
	{
	    doorsector[lines[i].backsector - sectors] |= (lines[i].flags & ML_SECRET) ? DOOR_SECRET : DOOR_OPENS;
	    if (lines[i].flags & ML_SECRET)
		secretfaces[numsecretfaces++] = &lines[i];
	}
    for (s = 0; s < numsectors; s++)
	SectorHeights (s);

    MarkTeleporters ();
    MarkHurts ();
    if (fresh || !roomfloor)
    {
	// Kept past the build's memory, for a rebuild of the same level.
	free (roomfloor);
	free (roomceil);
	roomfloor = malloc (sizeof *roomfloor*numsectors);
	roomceil = malloc (sizeof *roomceil*numsectors);
	if (!roomfloor || !roomceil)
	    I_Error ("P_Nav: out of memory");
	memcpy (roomfloor, navfloor, sizeof *roomfloor*numsectors);
	memcpy (roomceil, navceil, sizeof *roomceil*numsectors);
    }
    BuildRooms ();

    // A layer for each walker size on the map now.
    numclasses = 0;
    ready = true;
    RecordBlockers ();
    for (th = thinkercap.next; th != &thinkercap; th = th->next)
    {
	mobj_t*	mo = (mobj_t *)th;
	if (th->function.acp1 != (actionf_p1)P_MobjThinker
	    || !(mo->flags & MF_COUNTKILL) || (mo->flags & (MF_FLOAT|MF_NOGRAVITY)))
	    continue;
	for (i = 0; i < numwalkers; i++)
	    if (sizes[i][0] == mo->info->radius && sizes[i][1] == mo->info->height)
		break;
	if (i == numwalkers && numwalkers < MAXCLASSES)
	{
	    sizes[numwalkers][0] = mo->info->radius;
	    sizes[numwalkers][1] = mo->info->height;
	    numwalkers++;
	}
    }
    // Smallest first, so the layers come out numbered the same each build.
    for (i = 0; i < numwalkers; i++)
    {
	int	j, best = i;
	for (j = i + 1; j < numwalkers; j++)
	    if (sizes[j][0] < sizes[best][0] || (sizes[j][0] == sizes[best][0] && sizes[j][1] < sizes[best][1]))
		best = j;
	if (best != i)
	{
	    int	r = sizes[i][0], h = sizes[i][1];
	    sizes[i][0] = sizes[best][0]; sizes[i][1] = sizes[best][1];
	    sizes[best][0] = r; sizes[best][1] = h;
	}
	P_NavClassFor (sizes[i][0]/FRACUNIT, sizes[i][1]/FRACUNIT);
    }
    navbuildms = (int)((clock () - start)*1000/CLOCKS_PER_SEC);
}


void P_NavLevel (void)
{
    ready = false;
    numclasses = 0;
    P_NavKeptClear ();
    P_NavLongClear ();
    if (!P_DerpAI ())
    {
	NavFreeAll ();
	return;
    }
    BuildAll (true);
}


void P_NavRebuild (void)
{
    // A rebuild for the tests only, the rooms as the level loaded.
    ready = false;
    numclasses = 0;
    P_NavKeptClear ();
    P_NavLongClear ();
    if (P_DerpAI ())
	BuildAll (false);
}


boolean P_NavReady (void)
{
    return ready;
}


void P_NavSectorMoved (sector_t* sector)
{
    if (!ready)
	return;
    moved[sector - sectors] = 1;
    anymoved = true;
}


//
// The blockers the grid went round, by serial number, to see them go or move.
//
typedef struct { int serial; fixed_t x, y; int radius; boolean seen; } blocker_t;
static blocker_t*	blockers;
static int		numblockers;

static int CompareBlockers (const void* a, const void* b)
{
    int	sa = ((const blocker_t *)a)->serial, sb = ((const blocker_t *)b)->serial;
    return sa < sb ? -1 : sa > sb;
}

static void RecordBlockers (void)
{
    thinker_t*	th;
    int		n = 0;

    for (th = thinkercap.next; th != &thinkercap; th = th->next)
	if (Blocker ((mobj_t *)th))
	    n++;
    blockers = NavAlloc (sizeof *blockers*(n + 1));
    numblockers = 0;
    for (th = thinkercap.next; th != &thinkercap; th = th->next)
    {
	mobj_t*	mo = (mobj_t *)th;
	if (!Blocker (mo))
	    continue;
	blockers[numblockers].serial = mo->serial;
	blockers[numblockers].x = mo->x;
	blockers[numblockers].y = mo->y;
	blockers[numblockers].radius = mo->info->radius>>FRACBITS;
	numblockers++;
    }
    qsort (blockers, numblockers, sizeof *blockers, CompareBlockers);
}

// The cells a blocker of radius at (x, y) could touch, rebuilt in every layer.
static void RebuildAround (fixed_t x, fixed_t y, int radius)
{
    int		i;

    for (i = 0; i < numclasses; i++)
    {
	int	reach = radius + classes[i].radius + 2*NAVCELL;
	BuildArea (&classes[i],
		   ((x - gridx)/FRACUNIT - reach)/NAVCELL, ((y - gridy)/FRACUNIT - reach)/NAVCELL,
		   ((x - gridx)/FRACUNIT + reach)/NAVCELL, ((y - gridy)/FRACUNIT + reach)/NAVCELL);
    }
}

static void CheckBlockers (void)
{
    thinker_t*	th;
    int		i;

    for (i = 0; i < numblockers; i++)
	blockers[i].seen = false;
    for (th = thinkercap.next; th != &thinkercap; th = th->next)
    {
	mobj_t*		mo = (mobj_t *)th;
	blocker_t	key, *b;
	if (!Blocker (mo))
	    continue;
	key.serial = mo->serial;
	b = bsearch (&key, blockers, numblockers, sizeof *blockers, CompareBlockers);
	if (!b)
	    continue;	// one that came since (rare): the next build has it
	b->seen = true;
	if (b->x != mo->x || b->y != mo->y)
	{
	    RebuildAround (b->x, b->y, b->radius);
	    RebuildAround (mo->x, mo->y, b->radius);
	    b->x = mo->x;
	    b->y = mo->y;
	}
    }
    // Gone: exploded, or no longer solid.
    for (i = 0; i < numblockers; i++)
	if (!blockers[i].seen && blockers[i].serial)
	{
	    RebuildAround (blockers[i].x, blockers[i].y, blockers[i].radius);
	    blockers[i].serial = 0;
	}
    // Serial 0 sorts first and is never looked up: the order holds.
    qsort (blockers, numblockers, sizeof *blockers, CompareBlockers);
}


void P_NavThink (void)
{
    derp_timing_group previous;

    // (The tic's route budget is set afresh at the top of P_Ticker.)
    if (!ready || !P_PHASE (PHASE_NAV))
	return;
    previous = P_DerpTimingBegin (DERP_TIMING_NAV);
    P_NavSettle ();
    CheckBlockers ();
    P_DerpTimingEnd (previous);
}


void P_NavSync (void)
{
    int		s, cell, cells;

    if (!ready)
	return;
    // Every sector whose heights the grid does not go by: rebuilt where it
    // lies, as a mover's would be (P_NavSettle).
    for (s = 0; s < numsectors; s++)
    {
	fixed_t	floor = navfloor[s], ceil = navceil[s];
	SectorHeights (s);
	if (navfloor[s] != floor || navceil[s] != ceil)
	{
	    navfloor[s] = floor;
	    navceil[s] = ceil;
	    moved[s] = 1;
	    anymoved = true;
	}
    }
    P_NavSettle ();
    // Floors that hurt as they are now (a special may have changed).
    cells = gridw*gridh;
    for (cell = 0; cell < cells; cell++)
	flags[cell] &= ~NAV_HURT;
    MarkHurts ();
    navgen++;	// (a climber's landings go by what hurts)
}


void P_NavSecretFound (line_t* ld)
{
    sector_t*	sec = ld->backsector;
    fixed_t	bbox[4];
    int		i, n = 0;
    derp_timing_group previous;

    if (!ready || !sec || !(doorsector[sec - sectors] & DOOR_SECRET))
	return;
    previous = P_DerpTimingBegin (DERP_TIMING_NAV);
    doorsector[sec - sectors] = DOOR_OPENS;
    // Its faces are doors now, to anyone.
    for (i = 0; i < numsecretfaces; i++)
    {
	if (secretfaces[i]->backsector == sec)
	    secretfaces[i]->flags &= ~ML_SECRET;
	else
	    secretfaces[n++] = secretfaces[i];
    }
    numsecretfaces = n;
    M_ClearBox (bbox);
    for (i = 0; i < sec->linecount; i++)
    {
	M_AddToBox (bbox, sec->lines[i]->v1->x, sec->lines[i]->v1->y);
	M_AddToBox (bbox, sec->lines[i]->v2->x, sec->lines[i]->v2->y);
    }
    for (i = 0; i < numclasses; i++)
    {
	int	reach = classes[i].radius + 2*NAVCELL;
	BuildArea (&classes[i],
		   ((bbox[BOXLEFT] - gridx)/FRACUNIT - reach)/NAVCELL,
		   ((bbox[BOXBOTTOM] - gridy)/FRACUNIT - reach)/NAVCELL,
		   ((bbox[BOXRIGHT] - gridx)/FRACUNIT + reach)/NAVCELL,
		   ((bbox[BOXTOP] - gridy)/FRACUNIT + reach)/NAVCELL);
    }
    P_DerpTimingEnd (previous);
}

void P_NavSettle (void)
{
    int		s, i;

    if (!ready || !anymoved)
	return;
    anymoved = false;
    // A door opens to its lowest neighbour's ceiling: a neighbour that moved
    // moves where the door opens to.
    for (s = 0; s < numsectors; s++)
	if (moved[s] == 1)
	    for (i = 0; i < sectors[s].linecount; i++)
	    {
		line_t*	ld = sectors[s].lines[i];
		sector_t*	other = ld->frontsector == &sectors[s] ? ld->backsector : ld->frontsector;
		if (other && doorsector[other - sectors] && !moved[other - sectors])
		    moved[other - sectors] = 2;
	    }
    for (s = 0; s < numsectors; s++)
    {
	fixed_t		oldfloor = navfloor[s], oldceil = navceil[s];
	fixed_t		bbox[4];
	sector_t*	sec = &sectors[s];

	if (!moved[s])
	    continue;
	moved[s] = 0;
	SectorHeights (s);
	if (navfloor[s] == oldfloor && navceil[s] == oldceil)
	    continue;	// an opening door: the grid has it open already
	M_ClearBox (bbox);
	for (i = 0; i < sec->linecount; i++)
	{
	    M_AddToBox (bbox, sec->lines[i]->v1->x, sec->lines[i]->v1->y);
	    M_AddToBox (bbox, sec->lines[i]->v2->x, sec->lines[i]->v2->y);
	}
	for (i = 0; i < numclasses; i++)
	{
	    int	reach = classes[i].radius + 2*NAVCELL;
	    BuildArea (&classes[i],
		       ((bbox[BOXLEFT] - gridx)/FRACUNIT - reach)/NAVCELL,
		       ((bbox[BOXBOTTOM] - gridy)/FRACUNIT - reach)/NAVCELL,
		       ((bbox[BOXRIGHT] - gridx)/FRACUNIT + reach)/NAVCELL,
		       ((bbox[BOXTOP] - gridy)/FRACUNIT + reach)/NAVCELL);
	}
    }
}


//
// Cells.
//
int P_NavCells (void)
{
    return ready ? gridw*gridh : 0;
}


boolean P_NavDoor (int nav, int cell)
{
    return P_NavStandable (nav, cell) && classes[nav].door[cell];
}


boolean P_NavStandable (int nav, int cell)
{
    return nav >= 0 && nav < numclasses && cell >= 0 && cell < gridw*gridh
	&& classes[nav].floor[cell] != SOLID;
}


int P_NavCellAt (int nav, fixed_t x, fixed_t y)
{
    int		cx, cy, r, dx, dy;

    if (nav < 0 || nav >= numclasses)
	return -1;
    cx = (x - gridx)/(NAVCELL*FRACUNIT);
    cy = (y - gridy)/(NAVCELL*FRACUNIT);
    for (r = 0; r <= 2; r++)
	for (dy = -r; dy <= r; dy++)
	    for (dx = -r; dx <= r; dx++)
	    {
		int	x2 = cx + dx, y2 = cy + dy;
		if ((abs (dx) != r && abs (dy) != r) || x2 < 0 || y2 < 0 || x2 >= gridw || y2 >= gridh)
		    continue;
		if (classes[nav].floor[y2*gridw + x2] != SOLID)
		    return y2*gridw + x2;
	    }
    return -1;
}


void P_NavPoint (int nav, int cell, fixed_t* x, fixed_t* y, fixed_t* floor)
{
    navclass_t*	c;

    if (nav < 0 || nav >= numclasses || cell < 0 || cell >= gridw*gridh)
    {
	*x = *y = 0;
	if (floor)
	    *floor = 0;
	return;
    }
    c = &classes[nav];

    *x = gridx + ((cell % gridw)*NAVCELL + NAVCELL/2 + c->off[cell*2])*FRACUNIT;
    *y = gridy + ((cell / gridw)*NAVCELL + NAVCELL/2 + c->off[cell*2+1])*FRACUNIT;
    if (floor)
	*floor = c->floor[cell]*FRACUNIT;
}


// Whether the step from a to b crosses a teleporter line.
static boolean Teleports (int nav, int a, int b)
{
    fixed_t	ax, ay, bx, by;
    int		i;

    P_NavPoint (nav, a, &ax, &ay, NULL);
    P_NavPoint (nav, b, &bx, &by, NULL);
    for (i = 0; i < numteles; i++)
    {
	line_t*	ld = teles[i];
	if (P_PointOnLineSide (ax, ay, ld) == P_PointOnLineSide (bx, by, ld))
	    continue;
	// Across its infinite line: across the line itself only if its ends
	// lie either side of the step.
	{
	    divline_t	step = { ax, ay, bx - ax, by - ay };
	    if (P_PointOnDivlineSide (ld->v1->x, ld->v1->y, &step)
		!= P_PointOnDivlineSide (ld->v2->x, ld->v2->y, &step))
		return true;
	}
    }
    return false;
}


fixed_t P_NavJumpTop (int nav, int from, int to)
{
    int		k;

    if (nav < 0 || nav >= numclasses || from < 0 || !climbto[nav] || climbgen[nav][from] != navgen)
	return MININT;
    for (k = 0; k < 8; k++)
	if (climbto[nav][from*8 + k] == to)
	    return climbtop_[nav][from*8 + k]*FRACUNIT;
    return MININT;
}

boolean P_NavNeighbours (int a, int b)
{
    return a >= 0 && b >= 0 && abs (a % gridw - b % gridw) <= 1 && abs (a / gridw - b / gridw) <= 1;
}

boolean P_NavJump (int nav, int a, int b)
{
    boolean	was = navclimb, step;

    // A climber's path takes a jump link only where the next cell is no
    // step (Climbs), so a pair that is no step is a jump, neighbours or not
    // (a jump may land on the very next cell, down a ledge's face).
    navclimb = true;
    step = P_NavStep (nav, a, b);
    navclimb = was;
    return !step;
}

boolean P_NavStep (int nav, int from, int to)
{
    navclass_t*	c;
    int		fx, fy, tx, ty;

    if (!P_NavStandable (nav, from) || !P_NavStandable (nav, to))
	return false;
    c = &classes[nav];
    fx = from % gridw; fy = from / gridw;
    tx = to % gridw; ty = to / gridw;
    if (abs (fx - tx) > 1 || abs (fy - ty) > 1 || from == to)
	return false;
    // A step up no higher than the game's 24; no lower either, as a walker
    // never steps off more than that (P_TryMove's dropoff).
    if (abs (c->floor[from] - c->floor[to]) > 24)
	return false;
    // P_TryMove measures the ceiling ahead from where the body stands now:
    // stepping down under a low ceiling, the head would hit it first.
    if (c->ceil[to] - c->floor[from] < c->height)
	return false;
    // Diagonally only past standable corners: no cutting round a wall.
    if (fx != tx && fy != ty
	&& (!P_NavStandable (nav, fy*gridw + tx) || !P_NavStandable (nav, ty*gridw + fx)))
	return false;
    if (((flags[from] | flags[to]) & NAV_TELE) && Teleports (nav, from, to))
	return false;
    // Into a secret door from its secret side (stepping from there): only a
    // climber (the game's monsters never open that face); from the other,
    // or on out, anyone.
    if ((c->door[to] & DOOR_SECRET) && !navclimb && !c->door[from])
    {
	fixed_t		x, y, bx, by;
	sector_t*	sec;
	int		i;
	P_NavPoint (nav, from, &x, &y, NULL);
	sec = R_PointInSubsector (x, y)->sector;
	P_NavPoint (nav, to, &bx, &by, NULL);
	// The secret face on its side, within a body's reach of the door cell.
	for (i = 0; i < numsecretfaces; i++)
	{
	    line_t*	li = secretfaces[i];
	    if (li->frontsector == sec
		&& bx > li->bbox[BOXLEFT] - 48*FRACUNIT && bx < li->bbox[BOXRIGHT] + 48*FRACUNIT
		&& by > li->bbox[BOXBOTTOM] - 48*FRACUNIT && by < li->bbox[BOXTOP] + 48*FRACUNIT)
		return false;
	}
    }
    return true;
}


//
// Climbing (navclimb, for the imps): from a cell at an edge, a jump to the
// first standable cell up to 4 cells off in each of the eight ways where the
// next cell that way is no place to stand or no step: up a ledge (more than
// a step, up to CLIMB_UP), down one (up to CLIMB_DOWN), or over a sill
// between (a window: its top up to CLIMB_UP over the lower floor, the
// opening above it the body's height). Never onto a floor that hurts: over
// a lower one, up to CLIMB_OVER cells off, onto dry floor no higher than a
// step up, if one arc gets it there. The headroom for a body at the higher
// floor at both ends, and nothing between that would stop it (a wall, a
// line monsters may not cross unless jumping down, a teleporter, an
// opening too low for the body). A hop over a gap or a pillar at one height
// is no climb: it walks round. Worked out on first need, kept until the
// grid is next rebuilt anywhere.
//
boolean		navclimb;
boolean		navpartial;
int		navbadfrom = -1, navbadto = -1;
boolean		navexhausted;
unsigned	navsearched;
int		navwalk;
fixed_t		navavoidx, navavoidy;
int		navavoidr;
boolean		navavoidhard;

// What a cell near the point to keep away from costs over its 10: 20 at
// the edge of navavoidr, 80 at the point (a way round at a distance beats
// one past him).
static int AvoidCost (int cell)
{
    int	x = (gridx>>FRACBITS) + (cell % gridw)*NAVCELL + NAVCELL/2 - (navavoidx>>FRACBITS);
    int	y = (gridy>>FRACBITS) + (cell / gridw)*NAVCELL + NAVCELL/2 - (navavoidy>>FRACBITS);
    int	d;

    x = abs (x);
    y = abs (y);
    d = x + y - (x < y ? x : y)/2;
    if (d >= navavoidr)
	return 0;
    // Hard: as good as shut. A way through it only if there is no other at all.
    return navavoidhard ? 4000 : 20 + 60*(navavoidr - d)/navavoidr;
}
#define CLIMB_UP	112
#define CLIMB_DOWN	192
#define CLIMB_OVER	16
#define CLIMB_COST	40
// Room to spare beside a body: a box the game checks to the unit.
#define BODY_SPARE	1

static fixed_t	climbtop, climblow, climbtall, climbsill;
static boolean	climbdown;

boolean P_NavOpenAir (const line_t* ld)
{
    return ld->backsector && !sides[ld->sidenum[0]].midtexture && !sides[ld->sidenum[1]].midtexture;
}

static boolean PTR_ClimbLine (intercept_t* in)
{
    line_t*	li = in->d.line;

    // A line monsters may not cross fences walkers in (imps on a perch):
    // one jumping down goes over it.
    // A line marked impassable with nothing in it is a window it fits through
    // (Doom 2 MAP01's, with him up behind it). Bars are a middle texture. Our
    // glass is not in the map at all: it breaks as the imp goes (P_DerpMantle).
    if (!(li->flags & ML_TWOSIDED) || !li->backsector
	|| ((li->flags & ML_BLOCKING) && !P_NavOpenAir (li))
	|| ((li->flags & ML_BLOCKMONSTERS) && !climbdown) || Teleporter (li))
	return false;
    P_LineOpening (li);
    if (openbottom > climbtop)
    {
	// A sill to go over: low enough to jump, the opening above it tall
	// enough for the body.
	if (openbottom - climblow > CLIMB_UP*FRACUNIT || opentop - openbottom < climbtall)
	    return false;
	if (openbottom > climbsill)
	    climbsill = openbottom;
	return true;
    }
    return opentop >= climbtop + climbtall;
}

// Lines across the body's width, either side of the middle: a jump along a
// window's frame caught the shoulder on it, however clear its middle was. The
// body is a square, so on a slant its corner reaches further out (an imp's 20
// is 28 at 45 degrees), and lines no further apart than BODY_STRIP: E1M2's
// window at (1400, 968) is a recess with 16-unit jambs, which a line at each
// shoulder never crossed, and an imp jumped it on the slant, twice, onto the
// wall beside it.
#define BODY_STRIP	8

static boolean BodyPasses (const navclass_t* c, fixed_t ax, fixed_t ay, fixed_t bx, fixed_t by)
{
    double	dx = (bx - ax)/65536.0, dy = (by - ay)/65536.0, len = sqrt (dx*dx + dy*dy);
    double	half, at;
    int		strips;

    if (len < 1)
	return true;
    half = c->radius*(fabs (dx) + fabs (dy))/len + BODY_SPARE;
    strips = (int)ceil (half/BODY_STRIP);
    for (at = -half; at <= half + 1e-6; at += half/strips)
    {
	fixed_t	ox = (fixed_t)(-dy/len*at*65536.0), oy = (fixed_t)(dx/len*at*65536.0);
	if (!P_PathTraverse (ax + ox, ay + oy, bx + ox, by + oy, PT_ADDLINES, PTR_ClimbLine))
	    return false;
    }
    return true;
}

boolean P_NavHop (int nav, fixed_t ax, fixed_t ay, fixed_t bx, fixed_t by, fixed_t* x, fixed_t* y, fixed_t* z)
{
    const navclass_t*	c = &classes[nav];
    int			a = P_NavCellAt (nav, ax, ay), b = P_NavCellAt (nav, bx, by), top, low;

    if (a < 0 || b < 0 || c->floor[b] == SOLID || (flags[b] & NAV_HURT) || abs (c->floor[a] - c->floor[b]) > 24)
	return false;
    P_NavPoint (nav, b, x, y, z);
    top = c->floor[a] > c->floor[b] ? c->floor[a] : c->floor[b];
    low = c->floor[a] < c->floor[b] ? c->floor[a] : c->floor[b];
    climbtop = top*FRACUNIT;
    climblow = low*FRACUNIT;
    climbtall = c->height*FRACUNIT;
    climbsill = MININT;
    climbdown = true;
    return P_PathTraverse (ax, ay, *x, *y, PT_ADDLINES, PTR_ClimbLine)
	&& BodyPasses (c, ax, ay, *x, *y) && climbsill == MININT
	&& P_EncounterMayJump (R_PointInSubsector (ax, ay)->sector, R_PointInSubsector (*x, *y)->sector);
}

// The jumps from cell (each way's landing, -1 none).
static const int* Climbs (int nav, int cell)
{
    static const int	way[8][2] = {{1,0},{-1,0},{0,1},{0,-1},{1,1},{1,-1},{-1,1},{-1,-1}};
    navclass_t*		c = &classes[nav];
    int			cells = gridw*gridh, k, d, cx = cell % gridw, cy = cell / gridw;
    int*		to;
    boolean		over;

    if (!climbto[nav])
    {
	climbto[nav] = NavAlloc ((size_t)cells*8*sizeof (int));
	climbtop_[nav] = NavAlloc ((size_t)cells*8*sizeof (short));
	climbgen[nav] = NavAlloc ((size_t)cells*sizeof (int));
	memset (climbgen[nav], 0, (size_t)cells*sizeof (int));
    }
    to = &climbto[nav][cell*8];
    if (climbgen[nav][cell] == navgen)
	return to;
    climbgen[nav][cell] = navgen;
    for (k = 0; k < 8; k++)
    {
	int	nx = cx + way[k][0], ny = cy + way[k][1], next;
	to[k] = -1;
	if (nx < 0 || ny < 0 || nx >= gridw || ny >= gridh)
	    continue;
	// Room to walk that way (a step up or down): no jump needed. A
	// ledge's edge right there: the jump may land on it.
	next = c->floor[ny*gridw + nx];
	if (next != SOLID && abs (next - c->floor[cell]) <= 24)
	    continue;
	for (d = next == SOLID ? 2 : 1, over = false; d <= (over ? CLIMB_OVER : 4); d++)
	{
	    int		tx = cx + way[k][0]*d, ty = cy + way[k][1]*d, t, rise, top;
	    fixed_t	ax, ay, bx, by;
	    if (tx < 0 || ty < 0 || tx >= gridw || ty >= gridh)
		break;
	    t = ty*gridw + tx;
	    if (c->floor[t] == SOLID)
		continue;
	    // Never a jump into nukage or lava: over it, if it is lower.
	    if (flags[t] & NAV_HURT)
	    {
		if (c->floor[t] >= c->floor[cell])
		    break;
		over = true;
		continue;
	    }
	    rise = c->floor[t] - c->floor[cell];
	    top = rise > 0 ? c->floor[t] : c->floor[cell];
	    if (rise > CLIMB_UP || -rise > CLIMB_DOWN
		|| c->ceil[cell] - top < c->height || c->ceil[t] - top < c->height)
		break;
	    if (over)
	    {
		// One arc over it (Leap's: up 7 a drop, then a unit a tic down,
		// at most 16 units a tic across), no mantle at the far end.
		double	up = rise > 0 ? sqrt (2.0*(rise + 26)) : 7;
		double	t_ = up + sqrt (up*up - 2.0*rise);
		if (rise > 24 || d*NAVCELL*(way[k][0] && way[k][1] ? 1.41 : 1) > 16*t_)
		    break;
	    }
	    P_NavPoint (nav, cell, &ax, &ay, NULL);
	    P_NavPoint (nav, t, &bx, &by, NULL);
	    climbtop = top*FRACUNIT;
	    climblow = (rise > 0 ? c->floor[cell] : c->floor[t])*FRACUNIT;
	    climbtall = c->height*FRACUNIT;
	    climbsill = MININT;
	    // Down, or out of a floor that hurts, or over one: over a monsters'
	    // fence. (E1M1's zigzag: the fence along the walkways is what kept imps
	    // from hopping from one to the next across the nukage.)
	    climbdown = rise < 0 || (flags[cell] & NAV_HURT) || over;
	    // (Not out of a trap the map has not sprung, p_encounter.h.)
	    if (P_PathTraverse (ax, ay, bx, by, PT_ADDLINES, PTR_ClimbLine)
		&& (abs (rise) > 24 || climbsill > climbtop || over || (flags[cell] & NAV_HURT))
		&& P_EncounterMayJump (R_PointInSubsector (ax, ay)->sector, R_PointInSubsector (bx, by)->sector)
		&& BodyPasses (c, ax, ay, bx, by))
	    {
		to[k] = t;
		climbtop_[nav][cell*8 + k] = (short)((climbsill > climbtop ? climbsill : climbtop)>>FRACBITS);
	    }
	    break;
	}
    }
    return to;
}

boolean P_NavClimbs (int nav, int from, int to)
{
    const int*	links;
    int		k;

    if (nav < 0 || nav >= numclasses || from < 0 || from >= gridw*gridh)
	return false;
    links = Climbs (nav, from);
    for (k = 0; k < 8; k++)
	if (links[k] == to)
	    return true;
    return false;
}


//
// The way: A* over the cells, eight neighbours, a straight step 10, a
// diagonal one 14, a door 40 more; for a climber, the jumps too, each 10 a
// cell across and CLIMB_COST more.
//
static int*	cost;
static int*	parent;
static int*	stamp;
static int	stampnow;
static int*	heap;
static int*	heapcost;
static int	heapsize, heapmax, cellsalloc;

// A search that found no way and still had cells to spare has been everywhere
// its start leads: every cell it marked is all there is. Kept, it answers a
// later search before it starts: from any cell among them (all that one can
// reach, the first could too) to a cell not among them there is no way, for
// the same body on the same grid (navgen: any rebuild drops them all). A
// soldier looking for a post tried some sixty spots it could not reach, each
// a whole walk of its floor (E1M2: 450,000 cells, 90 ms, a hitch per soldier
// as the squad woke); monsters cut off from the player each asked again.
#define REACHSETS	8
// The search it answers for would have run out of its limit first, and said
// so (navexhausted: a way may yet exist), on a set at least as large; on one
// under half of it, never (E1M2, E1M4, E1M6: run out at 0.71 to 0.97 of it).
typedef struct
{
    byte*	in;
    int		gen, nav, badfrom, badto, size, origin;
    boolean	climb;
} reachset_t;
static reachset_t	reachsets[REACHSETS];
static int		reachnext;

static boolean SameBody (const reachset_t* r, int nav)
{
    return r->gen == navgen && r->nav == nav && r->climb == navclimb
	&& r->badfrom == navbadfrom && r->badto == navbadto;
}

static boolean KnownUnreachable (int nav, int start, int goal, int limit)
{
    int		i;

    if (navpartial)
	return false;
    for (i = 0; i < REACHSETS; i++)
    {
	const reachset_t*	r = &reachsets[i];
	// (Near the limit, the search may yet run out of it first: a cell is
	// taken again when a cheaper way to it turns up, and counted again.)
	// (And only from where it was walked from: one inside it may reach less,
	// a climber's drop not climbed back, so no telling it would run out.)
	if (SameBody (r, nav) && r->in[start] && !r->in[goal]
	    && ((r->size >= limit && start == r->origin) || 2*r->size < limit))
	{
	    navexhausted = r->size >= limit;
	    return true;
	}
    }
    return false;
}

static void KeepReached (int nav, int start, int cells)
{
    reachset_t*	r = &reachsets[reachnext];
    int		i;

    reachnext = (reachnext + 1) % REACHSETS;
    r->origin = start;
    r->size = 0;
    for (i = 0; i < cells; i++)
	r->size += r->in[i] = stamp[i] == stampnow;
    r->gen = navgen;
    r->nav = nav;
    r->climb = navclimb;
    r->badfrom = navbadfrom;
    r->badto = navbadto;
}

// A search that ran out of its limit found nothing either way: then the whole
// of where its start leads is walked once, the steps and jumps the search
// takes, no costs, no order, and kept as above. A climber has the run of the
// level (an imp off to lie in wait tried spot after spot, each 8,000 cells,
// 43 ms as it woke).
static int*	floodq;

static boolean Covered (int nav, int start)
{
    int		i;

    for (i = 0; i < REACHSETS; i++)
	if (SameBody (&reachsets[i], nav) && reachsets[i].in[start])
	    return true;
    return false;
}

static void KeepFlood (int nav, int start, int cells)
{
    reachset_t*	r = &reachsets[reachnext];
    int		head = 0, tail = 0;

    memset (r->in, 0, cells);
    r->in[start] = 1;
    floodq[tail++] = start;
    while (head < tail)
    {
	int	cell = floodq[head++], cx = cell % gridw, cy = cell / gridw, dx, dy;
	for (dy = -1; dy <= 1; dy++)
	    for (dx = -1; dx <= 1; dx++)
	    {
		int	next;
		if ((!dx && !dy) || cx + dx < 0 || cy + dy < 0 || cx + dx >= gridw || cy + dy >= gridh)
		    continue;
		next = (cy + dy)*gridw + cx + dx;
		if (r->in[next] || !P_NavStep (nav, cell, next))
		    continue;
		r->in[next] = 1;
		floodq[tail++] = next;
	    }
	if (navclimb)
	{
	    const int*	to = Climbs (nav, cell);
	    int		k;
	    for (k = 0; k < 8; k++)
	    {
		int	next = to[k];
		if (next < 0 || r->in[next]
		    || (navbadfrom >= 0 && P_NavSameJump (navbadfrom, navbadto, cell, next)))
		    continue;
		r->in[next] = 1;
		floodq[tail++] = next;
	    }
	}
    }
    reachnext = (reachnext + 1) % REACHSETS;
    r->origin = start;
    r->size = tail;
    r->gen = navgen;
    r->nav = nav;
    r->climb = navclimb;
    r->badfrom = navbadfrom;
    r->badto = navbadto;
}

static void Push (int cell, int f)
{
    int		i;

    if (heapsize == heapmax)
    {
	heapmax = heapmax ? 2*heapmax : 4096;
	heap = realloc (heap, sizeof *heap*heapmax);
	heapcost = realloc (heapcost, sizeof *heapcost*heapmax);
	if (!heap || !heapcost)
	    I_Error ("P_Nav: out of memory");
    }
    i = heapsize++;

    while (i > 0 && heapcost[(i - 1)/2] > f)
    {
	heap[i] = heap[(i - 1)/2];
	heapcost[i] = heapcost[(i - 1)/2];
	i = (i - 1)/2;
    }
    heap[i] = cell;
    heapcost[i] = f;
}

static int Pop (void)
{
    int		top = heap[0], cell, f, i = 0;

    cell = heap[--heapsize];
    f = heapcost[heapsize];
    for (;;)
    {
	int	child = 2*i + 1;
	if (child >= heapsize)
	    break;
	if (child + 1 < heapsize && heapcost[child + 1] < heapcost[child])
	    child++;
	if (heapcost[child] >= f)
	    break;
	heap[i] = heap[child];
	heapcost[i] = heapcost[child];
	i = child;
    }
    heap[i] = cell;
    heapcost[i] = f;
    return top;
}

static int Guess (int a, int b)
{
    int		dx = abs (a % gridw - b % gridw), dy = abs (a / gridw - b / gridw);
    return dx < dy ? 14*dx + 10*(dy - dx) : 14*dy + 10*(dx - dy);
}

int P_NavPath (int nav, fixed_t sx, fixed_t sy, fixed_t tx, fixed_t ty,
	       int* path, int max, int limit)
{
    int		start, goal, searched = 0, n, cell, i, nearest, least;
    int		cells = gridw*gridh;
    derp_timing_group previous = P_DerpTimingBegin (DERP_TIMING_NAV);

    start = P_NavCellAt (nav, sx, sy);
    goal = P_NavCellAt (nav, tx, ty);
    if (start < 0 || goal < 0)
    {
	P_DerpTimingEnd (previous);
	return 0;
    }
    if (cellsalloc != cells)
    {
	// Kept from search to search, as long as the grid's size stays.
	free (cost);
	free (parent);
	free (stamp);
	cost = malloc (sizeof *cost*cells);
	parent = malloc (sizeof *parent*cells);
	stamp = calloc (cells, sizeof *stamp);
	if (!cost || !parent || !stamp)
	    I_Error ("P_Nav: out of memory");
	for (i = 0; i < REACHSETS; i++)
	{
	    free (reachsets[i].in);
	    reachsets[i].in = malloc (cells);
	    reachsets[i].gen = 0;
	    if (!reachsets[i].in)
		I_Error ("P_Nav: out of memory");
	}
	free (floodq);
	floodq = malloc (sizeof *floodq*cells);
	if (!floodq)
	    I_Error ("P_Nav: out of memory");
	cellsalloc = cells;
	stampnow = 0;
    }
    if (KnownUnreachable (nav, start, goal, limit))
    {
	P_DerpTimingEnd (previous);
	return 0;
    }
    if (++stampnow == 0x7fffffff)
    {
	memset (stamp, 0, sizeof *stamp*cells);
	stampnow = 1;
    }
    heapsize = 0;
    stamp[start] = stampnow;
    cost[start] = 0;
    parent[start] = -1;
    Push (start, Guess (start, goal));
    nearest = start;
    least = Guess (start, goal);
    while (heapsize && searched < limit)
    {
	int	cx, cy, dx, dy;
	cell = Pop ();
	if (cell == goal)
	    break;
	if (Guess (cell, goal) < least)
	{
	    least = Guess (cell, goal);
	    nearest = cell;
	}
	searched++;
	cx = cell % gridw;
	cy = cell / gridw;
	for (dy = -1; dy <= 1; dy++)
	    for (dx = -1; dx <= 1; dx++)
	    {
		int	next, c;
		if ((!dx && !dy) || cx + dx < 0 || cy + dy < 0 || cx + dx >= gridw || cy + dy >= gridh)
		    continue;
		next = (cy + dy)*gridw + cx + dx;
		if (!P_NavStep (nav, cell, next))
		    continue;
		c = cost[cell] + (dx && dy ? 14 : 10) + (classes[nav].door[next] ? DOOR_COST : 0)
		    + ((flags[next] & NAV_HURT) && !navclimb ? HURT_COST : 0) + (navavoidr ? AvoidCost (next) : 0);
		if (stamp[next] == stampnow && cost[next] <= c)
		    continue;
		stamp[next] = stampnow;
		cost[next] = c;
		parent[next] = cell;
		Push (next, c + Guess (next, goal));
	    }
	if (navclimb)
	{
	    const int*	to = Climbs (nav, cell);
	    int		k;
	    for (k = 0; k < 8; k++)
	    {
		int	next = to[k], c, across;
		if (next < 0 || (navbadfrom >= 0 && P_NavSameJump (navbadfrom, navbadto, cell, next)))
		    continue;
		across = abs (next % gridw - cx) > abs (next / gridw - cy)
		    ? abs (next % gridw - cx) : abs (next / gridw - cy);
		c = cost[cell] + 10*across + CLIMB_COST;
		if (stamp[next] == stampnow && cost[next] <= c)
		    continue;
		stamp[next] = stampnow;
		cost[next] = c;
		parent[next] = cell;
		Push (next, c + Guess (next, goal));
	    }
	}
    }
    navsearched += searched;
    P_NavBudgetSpend (searched);
    navexhausted = stamp[goal] != stampnow && heapsize && searched >= limit;
    if (stamp[goal] != stampnow && !navexhausted)
	KeepReached (nav, start, cells);
    else if (navexhausted && !Covered (nav, start))
	KeepFlood (nav, start, cells);
    if (stamp[goal] != stampnow)
    {
	// No way there: the nearest it can get, if it was asked for.
	if (!navpartial || nearest == start)
	{
	    P_DerpTimingEnd (previous);
	    return 0;
	}
	goal = nearest;
    }
    // 10 a cell straight, 14 across: in units, about.
    navwalk = cost[goal]*NAVCELL/10;
    // Back from the goal, then turned round.
    for (n = 0, cell = goal; cell >= 0; cell = parent[cell])
	n++;
    for (i = n - 1, cell = goal; cell >= 0; cell = parent[cell], i--)
	if (i < max)
	    path[i] = cell;
    P_DerpTimingEnd (previous);
    return n < max ? n : max;
}


//
// For the long searches that carry over tics (p_navlong.c): the steps of the
// search above, cell by cell, and its reach sets, so the two never disagree.
//
int P_NavWidth (void)
{
    return ready ? gridw : 0;
}

// What P_NavPath's loop takes from a cell, in its order: each neighbour it steps
// to and what that step adds, then a climber's jumps.
int P_NavExpand (int nav, int cell, int* next, int* add)
{
    int		cx = cell % gridw, cy = cell / gridw, dx, dy, n = 0;

    for (dy = -1; dy <= 1; dy++)
	for (dx = -1; dx <= 1; dx++)
	{
	    int	to;
	    if ((!dx && !dy) || cx + dx < 0 || cy + dy < 0 || cx + dx >= gridw || cy + dy >= gridh)
		continue;
	    to = (cy + dy)*gridw + cx + dx;
	    if (!P_NavStep (nav, cell, to))
		continue;
	    next[n] = to;
	    add[n++] = (dx && dy ? 14 : 10) + (classes[nav].door[to] ? DOOR_COST : 0)
		+ ((flags[to] & NAV_HURT) && !navclimb ? HURT_COST : 0) + (navavoidr ? AvoidCost (to) : 0);
	}
    if (navclimb)
    {
	const int*	to = Climbs (nav, cell);
	int		k;
	for (k = 0; k < 8; k++)
	{
	    int	across;
	    if (to[k] < 0 || (navbadfrom >= 0 && P_NavSameJump (navbadfrom, navbadto, cell, to[k])))
		continue;
	    across = abs (to[k] % gridw - cx) > abs (to[k] / gridw - cy)
		? abs (to[k] % gridw - cx) : abs (to[k] / gridw - cy);
	    next[n] = to[k];
	    add[n++] = 10*across + CLIMB_COST;
	}
    }
    return n;
}

// (Only once a search has set the arrays up.)
static boolean ReachReady (void)
{
    return ready && cellsalloc == gridw*gridh;
}

boolean P_NavKnownUnreachable (int nav, int start, int goal, int limit, boolean loose)
{
    int		i;

    if (!ReachReady ())
	return false;
    if (KnownUnreachable (nav, start, goal, limit))
	return true;
    if (!loose || navpartial)
	return false;
    for (i = 0; i < REACHSETS; i++)
    {
	const reachset_t*	r = &reachsets[i];
	if (SameBody (r, nav) && r->in[start] && !r->in[goal] && r->size < limit)
	{
	    navexhausted = false;
	    return true;
	}
    }
    return false;
}

byte* P_NavReachOpen (void)
{
    if (!ReachReady ())
	return NULL;
    memset (reachsets[reachnext].in, 0, cellsalloc);
    return reachsets[reachnext].in;
}

void P_NavReachClose (int nav, int start, int size)
{
    reachset_t*	r = &reachsets[reachnext];

    reachnext = (reachnext + 1) % REACHSETS;
    r->origin = start;
    r->size = size;
    r->gen = navgen;
    r->nav = nav;
    r->climb = navclimb;
    r->badfrom = navbadfrom;
    r->badto = navbadto;
}

void P_NavReachFlood (int nav, int start)
{
    if (ReachReady () && !Covered (nav, start))
	KeepFlood (nav, start, cellsalloc);
}


// Every cell within most units' walk of (sx, sy), costed as P_NavPath costs
// a way (doors, hurting floors, a climber's jumps, navavoid): one flood that
// answers "how far a walk to there" for many spots, where a search each walks
// the same floor again (an imp waking tried spot after spot round its hiding
// place, 8,000 cells each). Kept until the next search of any kind.
static int	floodstamp = -1;
static int	floodnav = -1;

boolean P_NavFlood (int nav, fixed_t sx, fixed_t sy, int most)
{
    int		start, searched = 0, cell;
    int		cells = gridw*gridh;
    derp_timing_group previous;

    floodstamp = -1;
    // (Only once a search has set the arrays up; else no flood, a search each.)
    if (!ready || nav < 0 || nav >= numclasses || cellsalloc != cells)
	return false;
    start = P_NavCellAt (nav, sx, sy);
    if (start < 0)
	return false;
    previous = P_DerpTimingBegin (DERP_TIMING_NAV);
    if (++stampnow == 0x7fffffff)
    {
	memset (stamp, 0, sizeof *stamp*cells);
	stampnow = 1;
    }
    heapsize = 0;
    stamp[start] = stampnow;
    cost[start] = 0;
    parent[start] = -1;
    Push (start, 0);
    while (heapsize)
    {
	int	cx, cy, dx, dy;
	cell = Pop ();
	searched++;
	cx = cell % gridw;
	cy = cell / gridw;
	for (dy = -1; dy <= 1; dy++)
	    for (dx = -1; dx <= 1; dx++)
	    {
		int	next, c;
		if ((!dx && !dy) || cx + dx < 0 || cy + dy < 0 || cx + dx >= gridw || cy + dy >= gridh)
		    continue;
		next = (cy + dy)*gridw + cx + dx;
		if (!P_NavStep (nav, cell, next))
		    continue;
		c = cost[cell] + (dx && dy ? 14 : 10) + (classes[nav].door[next] ? DOOR_COST : 0)
		    + ((flags[next] & NAV_HURT) && !navclimb ? HURT_COST : 0) + (navavoidr ? AvoidCost (next) : 0);
		if (c*NAVCELL/10 > most || (stamp[next] == stampnow && cost[next] <= c))
		    continue;
		stamp[next] = stampnow;
		cost[next] = c;
		parent[next] = cell;
		Push (next, c);
	    }
	if (navclimb)
	{
	    const int*	to = Climbs (nav, cell);
	    int		k;
	    for (k = 0; k < 8; k++)
	    {
		int	next = to[k], c, across;
		if (next < 0 || (navbadfrom >= 0 && P_NavSameJump (navbadfrom, navbadto, cell, next)))
		    continue;
		across = abs (next % gridw - cx) > abs (next / gridw - cy)
		    ? abs (next % gridw - cx) : abs (next / gridw - cy);
		c = cost[cell] + 10*across + CLIMB_COST;
		if (c*NAVCELL/10 > most || (stamp[next] == stampnow && cost[next] <= c))
		    continue;
		stamp[next] = stampnow;
		cost[next] = c;
		parent[next] = cell;
		Push (next, c);
	    }
	}
    }
    navsearched += searched;
    P_NavBudgetSpend (searched);
    floodstamp = stampnow;
    floodnav = nav;
    P_DerpTimingEnd (previous);
    return true;
}

int P_NavFloodWalk (int nav, fixed_t x, fixed_t y)
{
    int		cell;

    if (floodstamp < 0 || floodstamp != stampnow || nav != floodnav)
	return -2;
    cell = P_NavCellAt (nav, x, y);
    if (cell < 0 || stamp[cell] != stampnow)
	return -1;
    return cost[cell]*NAVCELL/10;
}


//
// Rooms.
//
int P_NavRoom (int sector)
{
    return ready && sector >= 0 && sector < numsectors ? roomof[sector] : -1;
}

int P_NavRooms (void)
{
    return ready ? numrooms : 0;
}

boolean P_NavSectorDoor (int s)
{
    return ready && doorsector[s] != 0;
}

boolean P_NavSectorSecret (int s)
{
    return ready && (doorsector[s] & DOOR_SECRET);
}

int P_NavLinks (const navlink_t** out)
{
    *out = links;
    return ready ? numlinks : 0;
}


unsigned P_NavDigestOf (int part)
{
    unsigned	h = 2166136261u;
    int		i, n, cells = gridw*gridh;

#define MIX(v)	(h = (h ^ (unsigned)(v))*16777619u)
    if (!ready)
	return 0;
    if (part < 0)
    {
	MIX (gridw); MIX (gridh); MIX (gridx); MIX (gridy); MIX (numrooms); MIX (numlinks);
	for (i = 0; i < numsectors; i++)
	    MIX (roomof[i]);
	for (i = 0; i < cells; i++)
	    MIX (flags[i]);
	return h;
    }
    if (part >= numclasses)
	return 0;
    n = part;
    MIX (classes[n].radius); MIX (classes[n].height);
    for (i = 0; i < cells; i++)
    {
	MIX (classes[n].floor[i]);
	MIX (classes[n].ceil[i]);
	MIX (classes[n].door[i]);
	MIX (classes[n].off[i*2]);
	MIX (classes[n].off[i*2+1]);
    }
#undef MIX
    return h;
}


unsigned P_NavDigest (void)
{
    unsigned	h = P_NavDigestOf (-1);
    int		n;

    for (n = 0; n < numclasses; n++)
	h = (h ^ P_NavDigestOf (n))*16777619u;
    return h;
}


//
// The automap's overlay.
//
void P_NavDraw (void (*line)(fixed_t, fixed_t, fixed_t, fixed_t, int))
{
    static int		route[512];
    thinker_t*		th;
    mobj_t*		player = players[consoleplayer].mo;
    int			i, drawn = 0;
    static int		asked = -1;

    if (asked < 0)
	asked = M_CheckParm ("-derpnav") != 0;
    if (!asked || !ready || !player)
	return;
    for (i = 0; i < numlinks; i++)
    {
	line_t*	ld = &lines[links[i].line];
	line (ld->v1->x, ld->v1->y, ld->v2->x, ld->v2->y, 0);
    }
    // The ways of the nearest awake monsters to the player.
    for (th = thinkercap.next; th != &thinkercap && drawn < 16; th = th->next)
    {
	mobj_t*	mo = (mobj_t *)th;
	int	nav, n, k;
	if (th->function.acp1 != (actionf_p1)P_MobjThinker || !(mo->flags & MF_COUNTKILL)
	    || mo->health <= 0 || mo->target != player
	    || P_AproxDistance (mo->x - player->x, mo->y - player->y) > 2048*FRACUNIT)
	    continue;
	nav = P_NavClassOf (mo);
	if (nav < 0)
	    continue;
	n = P_NavPath (nav, mo->x, mo->y, player->x, player->y, route, 512, 20000);
	for (k = 1; k < n; k++)
	{
	    fixed_t	x1, y1, x2, y2;
	    P_NavPoint (nav, route[k - 1], &x1, &y1, NULL);
	    P_NavPoint (nav, route[k], &x2, &y2, NULL);
	    line (x1, y1, x2, y2, 1);
	}
	drawn++;
    }
}
