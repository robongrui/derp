//
// DERP's corners: where the map's walls end, each side of it, worked out
// once a level. Where a soldier gets behind something and leans out
// (p_tactics.c), a place from the architecture, not one a search happened
// on.
//

#ifndef __P_CORNERS__
#define __P_CORNERS__

#include "m_fixed.h"
#include "doomtype.h"

typedef struct
{
    fixed_t	x, y;	// the corner
    fixed_t	dx, dy;	// along the wall, away from the corner (a unit long)
    fixed_t	nx, ny;	// out of the wall, into the room (a unit long)
} corner_t;

// At level setup.
void P_CornersLevel (void);
// The level's corners: how many, the list in *list. Two a corner, one for
// each wall meeting there.
int P_Corners (const corner_t** list);
// Whether its own wall stands between it and (x, y): the one thing that makes
// it cover from there. Anything else hiding it there is luck.
boolean P_CornerFaces (const corner_t* c, fixed_t x, fixed_t y);

#endif
