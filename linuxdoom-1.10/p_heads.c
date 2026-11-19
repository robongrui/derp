//
// Where a soldier's head is drawn, for the pistol's silent kill (p_tactics.h).
//
// The head where he sees it: the frame showing, from the side he looks at it,
// mirrored as drawn, read off the picture itself. A fixed patch over the
// middle of the box missed heads that sit 8-12 units aside in the side
// views, the firing and the pain frames, while the wound landed on them
// (the user, 2026-11-09). Authored only: vanilla's bullets know no heads.
//

#include <stdlib.h>
#include <math.h>
#include "doomdef.h"
#include "doomstat.h"
#include "m_swap.h"
#include "p_local.h"
#include "p_derp.h"
#include "p_tactics.h"
#include "p_tactics_local.h"
#include "p_tune.h"
#include "r_defs.h"
#include "r_state.h"
#include "tables.h"
#include "w_wad.h"
#include "z_zone.h"

// From the top of the picture this far down: the face to the shoulders.
#define HEAD_TALL	TUNE (HEAD_TALL, 9)
// The head's columns: those whose paint starts within this many rows of the
// topmost. Lower down the shoulders start, and the arms.
#define HEAD_ROWS	6
// Each side of the head's own columns, for a round's edge.
#define HEAD_EDGE	TUNE (HEAD_EDGE, 1)

typedef struct
{
    short	top;		// units over the origin of its topmost row
    short	left, right;	// the head's first and last columns in the picture
    short	origin, width;	// the picture's origin column (leftoffset), its width
    boolean	known;
} head_t;

// One a sprite lump, measured the first time a round comes near it.
static head_t*	heads;

static void Measure (int lump, head_t* h)
{
    patch_t*	patch = W_CacheLumpNum (firstspritelump + lump, PU_CACHE);
    int		width = SHORT (patch->width);
    int		x, first = 256, best = -1;
    short*	tops;

    h->known = true;
    h->top = SHORT (patch->topoffset);
    h->origin = SHORT (patch->leftoffset);
    h->width = width;
    h->left = 0;
    h->right = -1;
    if (width <= 0)
	return;
    tops = malloc (width*sizeof *tops);
    for (x = 0; x < width; x++)
    {
	column_t*	col = (column_t *)((byte *)patch + LONG (patch->columnofs[x]));
	tops[x] = 256;
	if (col->topdelta != 0xff)
	    tops[x] = col->topdelta;
	if (tops[x] < first)
	{
	    first = tops[x];
	    best = x;
	}
    }
    if (best >= 0)
    {
	// The run of columns round the topmost one: a stray bit of gun or hand
	// up there elsewhere is not the head.
	int	l = best, r = best;
	while (l > 0 && tops[l - 1] <= first + HEAD_ROWS)
	    l--;
	while (r < width - 1 && tops[r + 1] <= first + HEAD_ROWS)
	    r++;
	h->top -= first;
	h->left = l;
	h->right = r;
    }
    free (tops);
}

// The picture of mo that viewer sees, and whether it is drawn mirrored
// (R_ProjectSprite's choice).
static const head_t* HeadOf (mobj_t* mo, mobj_t* viewer, boolean* flip)
{
    spriteframe_t*	sf;
    unsigned		rot = 0;
    int			lump;

    if (mo->sprite < 0 || mo->sprite >= numsprites
	|| (mo->frame & FF_FRAMEMASK) >= sprites[mo->sprite].numframes)
	return NULL;
    sf = &sprites[mo->sprite].spriteframes[mo->frame & FF_FRAMEMASK];
    if (sf->rotate)
	rot = (R_PointToAngle2 (viewer->x, viewer->y, mo->x, mo->y) - mo->angle
	       + (unsigned)(ANG45/2)*9) >> 29;
    lump = sf->lump[rot];
    *flip = sf->flip[rot];
    if (lump < 0 || lump >= numspritelumps)
	return NULL;
    if (!heads)
	heads = calloc (numspritelumps, sizeof *heads);
    if (!heads)
	return NULL;
    if (!heads[lump].known)
	Measure (lump, &heads[lump]);
    return &heads[lump];
}

void P_DerpShotAt (mobj_t* target, mobj_t* shooter, fixed_t x, fixed_t y, fixed_t z, fixed_t slope)
{
    const head_t*	h;
    boolean		flip;
    double		nx, ny, n, dx, dy, d, s, px, py, across, column, cz, top;

    if (!derpauthored || !P_TacticsOn () || !shooter || !shooter->player || !P_DerpMonster (target))
	return;
    h = HeadOf (target, shooter, &flip);
    if (!h || h->right < h->left)
	return;

    // The card the picture is drawn on: through the middle of the body,
    // square to the line from him to it.
    nx = (double)(target->x - shooter->x)/FRACUNIT;
    ny = (double)(target->y - shooter->y)/FRACUNIT;
    n = sqrt (nx*nx + ny*ny);
    dx = (double)trace.dx/FRACUNIT;
    dy = (double)trace.dy/FRACUNIT;
    d = sqrt (dx*dx + dy*dy);
    if (n < 1e-3 || d < 1e-6)
	return;
    nx /= n; ny /= n; dx /= d; dy /= d;
    if (fabs (dx*nx + dy*ny) < 1e-3)
	return;
    // Where the round crosses it, from where the trace met the box.
    s = ((double)(target->x - x)/FRACUNIT*nx + (double)(target->y - y)/FRACUNIT*ny)/(dx*nx + dy*ny);
    px = (double)(x - target->x)/FRACUNIT + s*dx;
    py = (double)(y - target->y)/FRACUNIT + s*dy;
    cz = (double)(z - target->z)/FRACUNIT + s*(double)slope/FRACUNIT;
    // To his right across the card, then the picture's column there: a
    // mirrored one is drawn over the same span, back to front (wounds.cpp).
    across = px*ny - py*nx;
    column = flip ? h->width - h->origin - across : across + h->origin;

    // Kneeling, the legs fold and the head comes down whole by what the
    // picture loses (P_DerpShown).
    top = h->top;
    if (P_DerpKneeling (target))
	top -= (double)(target->info->height - P_DerpShown (target))/FRACUNIT;
    if (cz > top || cz < top - HEAD_TALL
	|| column < h->left - HEAD_EDGE || column > h->right + 1 + HEAD_EDGE)
	return;
    target->headtic = leveltime;
}
