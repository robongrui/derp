// Two imps stalking him at once come at him from two sides: the second takes a way
// clear of the first's, the one there first waits out of his sight, and when both
// are close they call to each other and go together.
#ifndef __P_PINCER__
#define __P_PINCER__

#include "p_mobj.h"

// actor has just set out to stalk him: if another did too, a way of its own.
void P_PincerPair (mobj_t* actor, mobj_t* him);
// Each step of a stalk: true if it holds where it is, waiting for its partner.
boolean P_PincerHold (mobj_t* actor, mobj_t* him, boolean visible);
// Around the walk: its way keeps clear of its partner's.
void P_PincerRoute (mobj_t* actor);

#endif
