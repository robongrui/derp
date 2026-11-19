//
// DERP's cover plan: which way he will come into a room, and where to be when
// he does. Worked out once a level, from the rooms and the corners: for each
// way into each room, the covers that answer it, best first. All a soldier does
// later is ask which way he is coming and take the first that is free.
//
// Not saved: a load works it out again.
//

#ifndef __P_COVERPLAN__
#define __P_COVERPLAN__

#include "m_fixed.h"
#include "doomtype.h"

// How good a cover is against a way in, best first. Its own wall has to be
// between it and the way in to be on the list at all.
enum
{
    PLAN_FLANK,		// against the wall beside the way in: he walks past it (no corner: -1)
    PLAN_ENTRANCE,	// hidden from the mouth, a shot at him as he comes through
    PLAN_HIDDEN,	// hidden from the mouth, the shot comes when he is further in
    PLAN_LOW,		// hidden on one knee
    PLAN_ENOUGH		// seen from there, but better a wall at its side than none
};

typedef struct
{
    fixed_t	x, y;		// where it waits
    fixed_t	kx, ky;		// where it fires from: the same, or the end of the wall
    int		corner;		// the corner (p_corners.h)
    int		rank;
} planspot_t;

// At level setup, after the corners.
void P_CoverPlanLevel (void);

// The ways someone at (hx, hy) would come into room, the shortest walk first
// and any others nearly as short: into doors, most of them, how many. None: he
// is in it already, or has no way in. A door he has no key for and a secret
// nobody has found are not ways.
int P_CoverPlanDoors (int room, fixed_t hx, fixed_t hy, int* doors, int most);

// That way in: its covers, how many, and its mouth (just inside the room).
int P_CoverPlanSpots (int door, const planspot_t** list);
void P_CoverPlanMouth (int door, fixed_t* x, fixed_t* y);

#endif
