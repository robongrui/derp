// DERP's monsters walk their steps out, while DERP's monsters play (the
// strides part of derpparts).
//
// A Doom monster moves in one jump a chase step, every three or four tics,
// and stands still between. Vanilla's 8 units hardly show; DERP's 12, a
// sprint's 24 and the offstage moves' 36 dart. So the steps are taken as
// ever (the AI's choices, the lines crossed, what blocks, all as before),
// and at the end of the thing's tic it is put back where its chase step
// began and walks the way out over the tics until its next, a share a tic.
// Something stepping into its way stops it short; a teleport is not walked.

#ifndef __P_STRIDES__
#define __P_STRIDES__

#include "p_mobj.h"

// P_Move, before a step (a walk still under way ends at once), and after
// one taken.
void P_DerpStepping (mobj_t* actor);
void P_DerpStepped (mobj_t* actor, fixed_t x, fixed_t y);
// P_MobjThinker: first (before a blow's momentum moves it), the next share
// of the way; at its end, this tic's steps walked out from where they began.
void P_DerpStrides (mobj_t* mo);
void P_DerpStridesEnd (mobj_t* mo);

#endif
