//
// DERP's corners (p_corners.h): the places a wall ends, read off the map.
//

#include <math.h>
#include <stdlib.h>

#include "doomdef.h"
#include "doomstat.h"
#include "i_system.h"
#include "p_local.h"
#include "p_corners.h"
#include "r_state.h"

static corner_t*	corners;
static int		numcorners;

// A wall shorter than this hides nobody behind it.
#define SHORTEST	24
// A step up this high is a wall to who stands below it.
#define LEDGE		48

static void Add (fixed_t x, fixed_t y, double dx, double dy, double nx, double ny)
{
    corner_t*	c = &corners[numcorners++];

    c->x = x;
    c->y = y;
    c->dx = (fixed_t)(dx*FRACUNIT);
    c->dy = (fixed_t)(dy*FRACUNIT);
    c->nx = (fixed_t)(nx*FRACUNIT);
    c->ny = (fixed_t)(ny*FRACUNIT);
}

// A wall side: from one vertex to the next with the room on its right. A
// one-sided line, or the face of a ledge seen from below: a step this high
// hides a body behind it as a wall does. Lower ones are for sight to judge
// nothing by; a corner is only a candidate, checked against him when used.
typedef struct
{
    vertex_t*	from;
    vertex_t*	to;
    boolean	ledge;
} wall_t;

static boolean WallOf (line_t* ld, wall_t* w)
{
    fixed_t	step;

    w->ledge = false;
    w->from = ld->v1;
    w->to = ld->v2;
    if (!ld->backsector)
	return true;
    step = ld->frontsector->floorheight - ld->backsector->floorheight;
    if (abs (step) < LEDGE*FRACUNIT)
	return false;
    w->ledge = true;
    // The lower side is the room: the line's front is on its right.
    if (step > 0)
    {
	w->from = ld->v2;
	w->to = ld->v1;
    }
    return true;
}

// Along a wall side, a unit vector from..to and its length.
static double Along (const wall_t* w, double* x, double* y)
{
    double	l;

    *x = (double)(w->to->x - w->from->x)/FRACUNIT;
    *y = (double)(w->to->y - w->from->y)/FRACUNIT;
    l = sqrt (*x**x + *y**y);
    if (l >= 1)
    {
	*x /= l;
	*y /= l;
    }
    return l;
}

void P_CornersLevel (void)
{
    int*	starts;
    int*	ends;
    wall_t*	walls;
    boolean*	has;
    int		i;

    free (corners);
    corners = malloc (sizeof *corners*(numlines*4 + 1));
    starts = malloc (sizeof *starts*numvertexes);
    ends = malloc (sizeof *ends*numvertexes);
    walls = malloc (sizeof *walls*(numlines + 1));
    has = malloc (sizeof *has*(numlines + 1));
    if (!corners || !starts || !ends || !walls || !has)
	I_Error ("P_CornersLevel: out of memory");
    numcorners = 0;
    // The wall side starting and ending at each vertex.
    for (i = 0; i < numvertexes; i++)
	starts[i] = ends[i] = -1;
    for (i = 0; i < numlines; i++)
	if ((has[i] = WallOf (&lines[i], &walls[i])))
	{
	    starts[walls[i].from - vertexes] = i;
	    ends[walls[i].to - vertexes] = i;
	}
    for (i = 0; i < numlines; i++)
    {
	wall_t*	a = &walls[i];
	double	ax, ay, bx, by, la, lb;
	int	next;

	if (!has[i])
	    continue;
	la = Along (a, &ax, &ay);
	if (la < 1)
	    continue;
	// A ledge that starts from nothing (it rises out of the floor of an
	// opening): an end, the mirror of the one below.
	if (a->ledge && ends[a->from - vertexes] < 0 && la >= SHORTEST)
	    Add (a->from->x, a->from->y, ax, ay, ay, -ax);
	next = starts[a->to - vertexes];
	if (next < 0)
	{
	    // A ledge that stops: nothing goes on from here, an end too.
	    if (a->ledge && la >= SHORTEST)
		Add (a->to->x, a->to->y, -ax, -ay, ay, -ax);
	    continue;
	}
	lb = Along (&walls[next], &bx, &by);
	if (lb < 1)
	    continue;
	// One that ends where the next begins and turns left there, by 30
	// degrees or more, is a corner poking into the room: a wall end, a
	// door jamb, a pillar's edge.
	if (ax*by - ay*bx < 0.5)
	    continue;
	// Behind each of the two walls, its room side.
	if (la >= SHORTEST)
	    Add (a->to->x, a->to->y, -ax, -ay, ay, -ax);
	if (lb >= SHORTEST)
	    Add (a->to->x, a->to->y, bx, by, by, -bx);
    }
    free (starts);
    free (ends);
    free (walls);
    free (has);
}

int P_Corners (const corner_t** list)
{
    *list = corners;
    return numcorners;
}

boolean P_CornerFaces (const corner_t* c, fixed_t x, fixed_t y)
{
    return FixedMul (x - c->x, c->nx) + FixedMul (y - c->y, c->ny) < 0;
}
