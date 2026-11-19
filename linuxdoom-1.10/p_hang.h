// Imps that hang from the ceiling (the user's idea, 2026-11-08). Off to lie in wait, one may
// go up over his way in to where it hides instead, let him walk under it, and drop on
// his back. It breathes up there, a claw clicks, it hisses before it lets go: he can hear
// it if he listens. Seen, it drops at once.
#ifndef __P_HANG__
#define __P_HANG__

#include "p_mobj.h"

// Its lurking spot: on his walk to where it hides, near there, where he does not see it up
// there as he comes (just past a lintel, best); else near where it hides, out of his sight;
// if there is one and the dice say so (then it means to hang there).
void P_HangSpot (mobj_t* actor);
// On its way there, each think: his way in, searched over tics, may have come since; then
// the spot as P_HangSpot would have chosen it.
void P_HangWaits (mobj_t* actor);
// Arrived at its lurking spot: up it goes, if it means to and he does not see it. True if so.
boolean P_HangUp (mobj_t* actor);
// Each think while it climbs, hangs or is about to drop: true if that is all it does.
boolean P_HangThink (mobj_t* actor, boolean visible);
// Its pose for the renderer: 0 upright, 1 upside down, between as it turns over.
float P_HangRoll (const mobj_t* mo, float fraction);
// One of the two hangs clear above the other's head: they do not touch.
boolean P_HangAbove (const mobj_t* a, const mobj_t* b);
// Down again, or dead: an ordinary thing.
void P_HangEnd (mobj_t* mo);
// Whether (x, y) is a place to hang (high enough, not sky): for the tests.
boolean P_HangPlace (fixed_t x, fixed_t y);

#endif
