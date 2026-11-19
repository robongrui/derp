//
// What a map keeps in until it lets it out (p_encounter.h).
//

#include <string.h>

#include "doomdef.h"
#include "doomstat.h"
#include "p_local.h"
#include "p_nav.h"
#include "p_encounter.h"
#include "r_main.h"
#include "r_state.h"
#include "z_zone.h"
#include "p_tune.h"

#define K_DOOR		1	// a door he opens by hand: open, as far as walking goes
#define K_LIFT		2	// goes up and down between floors: no step too high
#define K_GATE		4	// a walk-over line opens it

// A body's height: less than this between floor and ceiling is shut.
#define BODY		TUNE (BODY, 56)
#define STEP		TUNE (STEP, 24)

static byte*	kinds;
// Per sector, lazily: the sectors a body walks to from it, a bit each, and
// the grid's build that was true for.
static byte**	rows;
static int*	rowbuild;
static int*	stack;
static int	rowsize;

static boolean ByHand (int special)
{
    switch (special)
    {
      case 1: case 26: case 27: case 28: case 31: case 32: case 33: case 34:
      case 117: case 118:
	return true;
    }
    return false;
}

static boolean Lift (int special)
{
    switch (special)
    {
      case 10: case 21: case 62: case 88: case 120: case 121: case 122: case 123:
	return true;
    }
    return false;
}

// Walking over it opens something: a door, a floor going down, a lift, a
// ceiling going up.
static boolean Springs (int special)
{
    switch (special)
    {
      case 2: case 4: case 86: case 90: case 105: case 106: case 108: case 109:
      case 19: case 36: case 37: case 38: case 82: case 83: case 84: case 98:
      case 10: case 88: case 120: case 121:
      case 40:
	return true;
    }
    return false;
}

static void MarkTagged (int tag, int kind)
{
    int	s;

    for (s = 0; s < numsectors; s++)
	if (sectors[s].tag == tag)
	    kinds[s] |= kind;
}

void P_EncounterLevel (void)
{
    int	i;

    kinds = Z_Malloc (numsectors, PU_LEVEL, NULL);
    rows = Z_Malloc (numsectors*sizeof *rows, PU_LEVEL, NULL);
    rowbuild = Z_Malloc (numsectors*sizeof *rowbuild, PU_LEVEL, NULL);
    stack = Z_Malloc (numsectors*sizeof *stack, PU_LEVEL, NULL);
    memset (kinds, 0, numsectors);
    memset (rows, 0, numsectors*sizeof *rows);
    rowsize = (numsectors + 7)/8;
    for (i = 0; i < numlines; i++)
    {
	const line_t*	ld = &lines[i];
	if (ByHand (ld->special) && ld->backsector)
	    kinds[ld->backsector - sectors] |= K_DOOR;
	if (!ld->tag)
	    continue;
	if (Lift (ld->special))
	    MarkTagged (ld->tag, K_LIFT);
	if (Springs (ld->special))
	    MarkTagged (ld->tag, K_GATE);
    }
}

#define HAS(row, s)	((row)[(s) >> 3] & (1 << ((s) & 7)))

// Whether a body gets from one sector to the next over this line. Down any
// drop; up a step; a monsters' fence is nothing to him, and it is what he can
// do that says which places a map has joined.
static boolean Walks (const line_t* ld, const sector_t* from, const sector_t* to)
{
    int	a = (int)(from - sectors), b = (int)(to - sectors);

    if (ld->flags & ML_BLOCKING)
	return false;
    if ((kinds[a] | kinds[b]) & K_DOOR)
	return true;
    if ((from->ceilingheight < to->ceilingheight ? from->ceilingheight : to->ceilingheight)
	- (from->floorheight > to->floorheight ? from->floorheight : to->floorheight) < BODY*FRACUNIT)
	return false;
    return ((kinds[a] | kinds[b]) & K_LIFT) || to->floorheight - from->floorheight <= STEP*FRACUNIT;
}

static const byte* Row (int from)
{
    int		build = P_NavBuild (), top = 0, i;
    byte*	row = rows[from];

    if (row && rowbuild[from] == build)
	return row;
    if (!row)
	row = rows[from] = Z_Malloc (rowsize, PU_LEVEL, NULL);
    rowbuild[from] = build;
    memset (row, 0, rowsize);
    row[from >> 3] |= 1 << (from & 7);
    stack[top++] = from;
    while (top)
    {
	const sector_t*	sec = &sectors[stack[--top]];
	for (i = 0; i < sec->linecount; i++)
	{
	    const line_t*	ld = sec->lines[i];
	    const sector_t*	next = ld->frontsector == sec ? ld->backsector : ld->frontsector;
	    int			n;
	    if (!next || next == sec || HAS (row, next - sectors) || !Walks (ld, sec, next))
		continue;
	    n = (int)(next - sectors);
	    row[n >> 3] |= 1 << (n & 7);
	    stack[top++] = n;
	}
    }
    return row;
}

// Out of his reach, with a walk-over line waiting at its edge.
static boolean Trap (int s, const byte* row)
{
    const sector_t*	start = R_PointInSubsector (playerstarts[0].x << FRACBITS,
						    playerstarts[0].y << FRACBITS)->sector;
    int			k, i;

    if (HAS (Row ((int)(start - sectors)), s))
	return false;
    for (k = 0; k < numsectors; k++)
    {
	if (!HAS (row, k))
	    continue;
	for (i = 0; i < sectors[k].linecount; i++)
	{
	    const line_t*	ld = sectors[k].lines[i];
	    const sector_t*	next = ld->frontsector == &sectors[k] ? ld->backsector : ld->frontsector;
	    if (next && (kinds[next - sectors] & K_GATE) && !HAS (row, next - sectors))
		return true;
	}
    }
    return false;
}

boolean P_EncounterMayJump (const sector_t* from, const sector_t* to)
{
    int		a = (int)(from - sectors), b = (int)(to - sectors);
    const byte*	row;

    if (a == b)
	return true;
    row = Row (a);
    if (HAS (row, b) || HAS (Row (b), a))
	return true;
    return !Trap (a, row);
}
