//
// DERP's openings (p_portals.h).
//

#include <math.h>
#include <stdlib.h>

#include "doomdef.h"
#include "doomstat.h"
#include "i_system.h"
#include "p_local.h"
#include "p_derp.h"
#include "p_derplog.h"
#include "p_director.h"
#include "p_nav.h"
#include "p_cover.h"
#include "p_portals.h"
#include "r_state.h"
#include "p_tune.h"

// How far from an opening the traffic rules start, along its way through.
#define ZONE		TUNE (ZONE, 128)
// Past its lip by no more than this it is in the frame: on through.
#define IN_FRAME	TUNE (IN_FRAME, 16)
// In this near it is at the mouth, and waits there if it must.
#define MOUTH		72
// Someone in the frame and moving, this lately: that way has the opening.
#define FRESH		TUNE (FRESH, TICRATE)
// One stopped in the frame is still waited for this long.
#define STOPPED		TUNE (STOPPED, 8*TICRATE)
// One way this long with others waiting: the traffic changes.
#define ONE_WAY		TUNE (ONE_WAY, 4*TICRATE)
// Those waiting stand off to a side of the way out.
#define WAIT_ASIDE	TUNE (WAIT_ASIDE, 24)
// A death in the opening keeps soldiers out this long, near it this near.
#define DEATH_NEAR	TUNE (DEATH_NEAR, 96)
#define DEATH_HOLD	TUNE (DEATH_HOLD, 5*TICRATE)
// A choke: he is about this near its far mouth, it this near its own, and
// someone fell there this lately (or he is right at it, and for every other
// one that is enough).
#define CHOKE_HIM	TUNE (CHOKE_HIM, 384)
#define CHOKE_RIGHT_AT	TUNE (CHOKE_RIGHT_AT, 192)
#define CHOKE_ME	TUNE (CHOKE_ME, 448)
#define CHOKE_ASIDE	TUNE (CHOKE_ASIDE, 320)
#define CHOKE_MEMORY	TUNE (CHOKE_MEMORY, 20*TICRATE)
// Openings one behind the other this close are one passage: two in it head
// to head have nowhere to go, whichever lines they stand between. Only narrow
// ones, in line with each other.
#define PASSAGE		TUNE (PASSAGE, 96)
#define NARROW		TUNE (NARROW, 64)

typedef struct
{
    double	x, y;		// its middle
    double	nx, ny;		// the way through
    double	half;		// half its width
    double	depth;		// half its length through (a short passage)
    int		frametic[2];	// when someone was last on the move in the frame, each way
    int		heretic[2];	// when someone was last there at all, moving or not
    int		side;		// the way that has it (+1: coming from along n, -1)
    int		sidetic;	// since when
    int		waitside;	// the other way's last waiter, and when
    int		waittic;
    int		deathtic;
} portal_t;

static portal_t*	portals;
static int		numportals;

static int Find (int* parent, int i)
{
    while (parent[i] != i)
	i = parent[i] = parent[parent[i]];
    return i;
}

// The door sector of a door link.
static int DoorOf (const line_t* ld)
{
    int	f = (int)(ld->frontsector - sectors), b = (int)(ld->backsector - sectors);
    return P_NavSectorDoor (b) ? b : f;
}

static boolean SameOpening (const navlink_t* a, const navlink_t* b)
{
    const line_t*	la = &lines[a->line];
    const line_t*	lb = &lines[b->line];

    if (a->door || b->door)
	return a->door && b->door && DoorOf (la) == DoorOf (lb);
    if (!((a->rooma == b->rooma && a->roomb == b->roomb) || (a->rooma == b->roomb && a->roomb == b->rooma)))
	return false;
    return la->v1 == lb->v1 || la->v1 == lb->v2 || la->v2 == lb->v1 || la->v2 == lb->v2;
}

// Whether b lies behind a, in line and near enough to be one passage with it.
static boolean InLine (const portal_t* a, const portal_t* b)
{
    double	dx = b->x - a->x, dy = b->y - a->y;
    double	along = dx*a->nx + dy*a->ny, across = dy*a->nx - dx*a->ny;

    return a->half < NARROW && b->half < NARROW && fabs (a->nx*b->nx + a->ny*b->ny) > 0.9
	&& fabs (across) < 32 && fabs (along) - a->depth - b->depth <= PASSAGE;
}

// a takes in b.
static void Join (portal_t* a, const portal_t* b)
{
    double	d = (b->x - a->x)*a->nx + (b->y - a->y)*a->ny;
    double	lo = d - b->depth < -a->depth ? d - b->depth : -a->depth;
    double	hi = d + b->depth > a->depth ? d + b->depth : a->depth;

    a->x += a->nx*(lo + hi)/2;
    a->y += a->ny*(lo + hi)/2;
    a->depth = (hi - lo)/2;
    if (b->half > a->half)
	a->half = b->half;
}

static void JoinPassages (void)
{
    boolean	joined = true;

    while (joined)
    {
	int	i, j;
	joined = false;
	for (i = 0; i < numportals && !joined; i++)
	    for (j = i + 1; j < numportals && !joined; j++)
		if (InLine (&portals[i], &portals[j]))
		{
		    Join (&portals[i], &portals[j]);
		    portals[j] = portals[--numportals];
		    joined = true;
		}
    }
}

void P_PortalsLevel (void)
{
    const navlink_t*	links;
    int			n = P_NavLinks (&links), i, j;
    int*		parent;
    int*		index;
    double*		length;
    boolean*		skip;

    free (portals);
    portals = NULL;
    numportals = 0;
    if (!n)
	return;
    parent = malloc (sizeof *parent*n);
    index = malloc (sizeof *index*n);
    length = calloc (n, sizeof *length);
    portals = calloc (n, sizeof *portals);
    skip = calloc (n, sizeof *skip);
    if (!parent || !index || !length || !portals || !skip)
	I_Error ("P_PortalsLevel: out of memory");
    for (i = 0; i < n; i++)
    {
	parent[i] = i;
	index[i] = -1;
    }
    for (i = 0; i < n; i++)
	for (j = 0; j < i; j++)
	    if (SameOpening (&links[i], &links[j]))
		parent[Find (parent, i)] = Find (parent, j);
    // A loop of lines round a patch of the same room (a lamp's lower ceiling, a
    // pool) is no opening: every vertex in it comes up twice.
    {
	byte*	odd = calloc (numvertexes, 1);
	if (!odd)
	    I_Error ("P_PortalsLevel: out of memory");
	for (i = 0; i < n; i++)
	{
	    boolean	open = false;
	    if (Find (parent, i) != i || links[i].door)
		continue;
	    for (j = 0; j < n; j++)
		if (Find (parent, j) == i)
		{
		    odd[lines[links[j].line].v1 - vertexes] ^= 1;
		    odd[lines[links[j].line].v2 - vertexes] ^= 1;
		}
	    for (j = 0; j < n; j++)
		if (Find (parent, j) == i)
		{
		    open |= odd[lines[links[j].line].v1 - vertexes] | odd[lines[links[j].line].v2 - vertexes];
		    odd[lines[links[j].line].v1 - vertexes] = odd[lines[links[j].line].v2 - vertexes] = 0;
		}
	    if (!open)
		skip[i] = true;
	}
	free (odd);
    }
    // The middle is the lines' middle, weighted by length. A door's width is
    // its longest face (its two faces are one after the other); a gap's, its
    // lines end to end.
    for (i = 0; i < n; i++)
    {
	const line_t*	ld = &lines[links[i].line];
	double		dx = ld->dx/65536.0, dy = ld->dy/65536.0, l = sqrt (dx*dx + dy*dy);
	int		r = Find (parent, i);
	portal_t*	p;
	if (l < 1 || skip[r])
	    continue;
	if (index[r] < 0)
	{
	    index[r] = numportals++;
	    p = &portals[index[r]];
	    p->nx = dy/l;
	    p->ny = -dx/l;
	}
	p = &portals[index[r]];
	p->x += (ld->v1->x/65536.0 + dx/2)*l;
	p->y += (ld->v1->y/65536.0 + dy/2)*l;
	length[index[r]] += l;
	if (links[i].door)
	{
	    if (l/2 > p->half)
		p->half = l/2;
	}
	else
	    p->half += l/2;
    }
    for (i = 0; i < numportals; i++)
    {
	portals[i].x /= length[i];
	portals[i].y /= length[i];
	portals[i].deathtic = -1000;
	portals[i].waittic = -1000;
	portals[i].frametic[0] = portals[i].frametic[1] = -1000;
	portals[i].heretic[0] = portals[i].heretic[1] = -1000;
    }
    JoinPassages ();
    free (parent);
    free (index);
    free (length);
    free (skip);
}

int P_Portals (void)
{
    return numportals;
}

// How far along the way through, and across it, (x, y) lies.
static void Offsets (const portal_t* p, double x, double y, double* along, double* across)
{
    double	dx = x - p->x, dy = y - p->y;

    *along = dx*p->nx + dy*p->ny;
    *across = dy*p->nx - dx*p->ny;
}

// Single file for mo: two of its size cannot pass in it.
static boolean SingleFile (const portal_t* p, const mobj_t* mo)
{
    return p->half*2 < 2*(2*mo->radius/65536.0 + 8);
}

static boolean Fresh (const portal_t* p, int side)
{
    return leveltime - p->frametic[side > 0] < FRESH;
}

// Whether one can stand back and lat from p's middle, in that room: then there. If
// seen, only where mo can see it: the same room can be the far side of a wall (beside a
// wide opening a zombie pushed at one for the five seconds it was held back). A queue
// at a door does not ask: its place is round the corner, out of the way.
static boolean Stand (const portal_t* p, mobj_t* mo, boolean seen, int nav, int room, int side, double back,
		      double lat, fixed_t* x, fixed_t* y)
{
    fixed_t	tx = (fixed_t)((p->x + side*back*p->nx - lat*p->ny)*65536.0);
    fixed_t	ty = (fixed_t)((p->y + side*back*p->ny + lat*p->nx)*65536.0);
    int		cell = P_NavCellAt (nav, tx, ty);

    if (cell < 0 || !P_NavStandable (nav, cell) || P_NavDoor (nav, cell) || P_NavHurts (cell)
	|| P_NavRoom ((int)(R_PointInSubsector (tx, ty)->sector - sectors)) != room
	|| (seen && !P_DerpExposedAt (tx, ty, R_PointInSubsector (tx, ty)->sector->floorheight, mo)))
	return false;
    *x = tx;
    *y = ty;
    return true;
}

// Where it waits on its side of p: at the mouth and off to a side of the way
// out (its own by its number, then the other), in the same room as the mouth.
// Else where it stands.
#define NODIR	8
static void Spot (const portal_t* p, mobj_t* actor, boolean seen, int side, fixed_t* x, fixed_t* y)
{
    static const int	backs[3] = { MOUTH, MOUTH/2, 28 };
    int		nav = P_NavClassOf (actor), i, k, w, room;
    double	mo_radius = actor->radius/65536.0;
    double	aside = p->half + WAIT_ASIDE + 16*(actor->serial % 3);
    int		sign = (actor->serial & 1) ? 1 : -1;
    double	mouth = p->depth + 24;

    *x = actor->x;
    *y = actor->y;
    room = P_NavRoom ((int)(R_PointInSubsector ((fixed_t)((p->x + side*mouth*p->nx)*65536.0),
						(fixed_t)((p->y + side*mouth*p->ny)*65536.0))->sector - sectors));
    // Nearer in for a shallow room (a corridor across the door's end: back from
    // it is another room), and in a narrow one just a body's width off the
    // line through. No room for that: straight back, out of the frame at least.
    for (w = 0; w < 3; w++)
    {
	double	lat0 = w ? 2*mo_radius + (w == 1 ? 8 : 4) : aside;
	for (k = 0; k < 3; k++)
	    for (i = 0; i < 2; i++)
		if (Stand (p, actor, seen, nav, room, side, p->depth + backs[k], (i ? -sign : sign)*lat0, x, y))
		    return;
    }
    Stand (p, actor, seen, nav, room, side, p->depth + MOUTH, 0, x, y);
}

boolean P_PortalWait (mobj_t* actor, fixed_t wx, fixed_t wy, fixed_t* x, fixed_t* y)
{
    double	ax = actor->x/65536.0, ay = actor->y/65536.0, bx = wx/65536.0, by = wy/65536.0;
    double	bestd = ZONE + 1;
    portal_t*	p = NULL;
    int		i, side = 1;

    for (i = 0; i < numportals; i++)
    {
	portal_t*	q = &portals[i];
	double		along, across, wa, wc, d;
	int		s;
	Offsets (q, ax, ay, &along, &across);
	if (fabs (along) > q->depth + ZONE || fabs (across) > q->half + 48)
	    continue;
	Offsets (q, bx, by, &wa, &wc);
	// Going through it: its waypoint on along the way through and in line
	// with the opening, and it not yet out of the far lip. Which way it
	// came from is by where it is going, not which half it is in: past
	// the middle it is still in the frame.
	if (fabs (wa - along) < 8 || fabs (wc) > q->half + 96)
	    continue;
	s = wa < along ? 1 : -1;
	if (along*s < -q->depth - IN_FRAME)
	    continue;
	d = along*s - q->depth;
	if (d < 0)
	    d = 0;
	if (d < bestd)
	{
	    bestd = d;
	    p = q;
	    side = s;
	}
    }
    if (!p)
	return false;
    if (bestd < IN_FRAME)
    {
	boolean	facing = leveltime - p->heretic[side < 0] < FRESH/2;
	p->heretic[side > 0] = leveltime;
	// Head to head in it, even standing nose to nose: whichever way had it
	// goes on, the other backs out, or nobody gets anywhere.
	if (SingleFile (p, actor) && facing && p->side != side)
	{
	    Spot (p, actor, false, side, x, y);
	    return true;
	}
	if (p->side != side)
	{
	    p->side = side;
	    p->sidetic = leveltime;
	}
	// (Standing waiting for a door or a friend is not on the move.)
	if (actor->movedir < NODIR && leveltime - actor->movedtic < FRESH)
	    p->frametic[side > 0] = leveltime;
	return false;
    }
    if (bestd > MOUTH)
	return false;
    // Someone just fell there: a soldier does not walk into that.
    if (P_DerpSoldier (actor->type) && leveltime - p->deathtic < DEATH_HOLD && !P_DirectorGoIn ())
    {
	Spot (p, actor, true, side, x, y);
	return true;
    }
    if (!SingleFile (p, actor))
	return false;
    // Someone coming the other way: on the move, or stopped in there (very
    // likely by this one standing in its way out) for a while yet.
    if (Fresh (p, -side)
	|| (leveltime - p->heretic[side < 0] < FRESH/2 && leveltime - p->sidetic < STOPPED))
    {
	p->waitside = side;
	p->waittic = leveltime;
	Spot (p, actor, false, side, x, y);
	return true;
    }
    // Others have waited long enough: this way stops, and the frame empties.
    if (p->side == side && p->waitside == -side && leveltime - p->waittic < TICRATE
	&& leveltime - p->sidetic > ONE_WAY)
    {
	Spot (p, actor, false, side, x, y);
	return true;
    }
    return false;
}

boolean P_PortalInFrame (mobj_t* mo)
{
    double	x = mo->x/65536.0, y = mo->y/65536.0;
    int		i;

    for (i = 0; i < numportals; i++)
    {
	const portal_t*	p = &portals[i];
	double		along, across;
	Offsets (p, x, y, &along, &across);
	if (fabs (along) <= p->depth + IN_FRAME && fabs (across) <= p->half && SingleFile (p, mo))
	    return true;
    }
    return false;
}

boolean P_PortalClear (mobj_t* mo, fixed_t hx, fixed_t hy, fixed_t* x, fixed_t* y)
{
    double	mx = mo->x/65536.0, my = mo->y/65536.0;
    int		i;

    for (i = 0; i < numportals; i++)
    {
	const portal_t*	p = &portals[i];
	double		along, across;
	Offsets (p, mx, my, &along, &across);
	if (fabs (along) > p->depth + IN_FRAME || fabs (across) > p->half || !SingleFile (p, mo))
	    continue;
	Offsets (p, hx/65536.0, hy/65536.0, &along, &across);
	Spot (p, mo, false, along < 0 ? -1 : 1, x, y);
	return *x != mo->x || *y != mo->y;
    }
    return false;
}

boolean P_PortalChoke (mobj_t* actor, fixed_t hx, fixed_t hy, fixed_t* far_x, fixed_t* far_y, fixed_t* near_x, fixed_t* near_y)
{
    double	ax = actor->x/65536.0, ay = actor->y/65536.0, bx = hx/65536.0, by = hy/65536.0;
    int		i;

    for (i = 0; i < numportals; i++)
    {
	const portal_t*	p = &portals[i];
	double		a, ac, k, kc;
	int		s;
	if (!SingleFile (p, actor))
	    continue;
	Offsets (p, ax, ay, &a, &ac);
	Offsets (p, bx, by, &k, &kc);
	if (a*k >= 0 || fabs (a) - p->depth > CHOKE_ME || fabs (k) - p->depth > CHOKE_HIM
	    || fabs (ac) > p->half + CHOKE_ASIDE || fabs (kc) > p->half + CHOKE_ASIDE)
	    continue;
	if (leveltime - p->deathtic >= CHOKE_MEMORY
	    && !(fabs (k) - p->depth <= CHOKE_RIGHT_AT && (actor->serial & 1)))
	    continue;
	s = a >= 0 ? 1 : -1;
	*far_x = (fixed_t)((p->x - s*(p->depth + 96)*p->nx)*65536.0);
	*far_y = (fixed_t)((p->y - s*(p->depth + 96)*p->ny)*65536.0);
	// Beside the mouth, off the line through: a shut door hides anywhere
	// in front of it, until it opens.
	Spot (p, actor, false, s, near_x, near_y);
	return true;
    }
    return false;
}

void P_PortalDeath (mobj_t* mo)
{
    double	x = mo->x/65536.0, y = mo->y/65536.0;
    int		i;

    for (i = 0; i < numportals; i++)
    {
	portal_t*	p = &portals[i];
	double		along, across;
	Offsets (p, x, y, &along, &across);
	if (fabs (along) <= p->depth + DEATH_NEAR && fabs (across) <= p->half + 32)
	{
	    p->deathtic = leveltime;
	    P_DerpLog (mo, "falls at an opening: soldiers hold back");
	}
    }
}

int P_PortalWidth (fixed_t x, fixed_t y)
{
    double	bestd = 1e18, width = 0, px = x/65536.0, py = y/65536.0;
    int		i;

    for (i = 0; i < numportals; i++)
    {
	double	dx = portals[i].x - px, dy = portals[i].y - py, d = dx*dx + dy*dy;
	if (d < bestd)
	{
	    bestd = d;
	    width = portals[i].half*2;
	}
    }
    return (int)width;
}
