// What a map keeps in until it lets it out (PLAN_DERP_AI.md's Stage B).
//
// Doors and walls hold monsters by themselves, and what opens them lets them
// out: nothing here needs to know a release happened. What gets past that is
// a jump, over a sill or through a window, so a jump asks here first.
//
// Two places a body could walk between, one way or the other, are one place:
// any jump goes. Between two that are shut off from each other, a jump is
// refused out of a trap: somewhere he cannot walk to from his start, with a
// walk-over line waiting to open its way. A way opened by a switch is only a
// locked yard (Doom 2 MAP01's, whose imps come through the window). A map's
// geometry cannot tell those two apart, its triggers can. Not perfectly: a
// trap sprung by a switch is let out early.
//
// All of it from the map as it stands, so nothing is saved.
#ifndef __P_ENCOUNTER__
#define __P_ENCOUNTER__

#include "r_defs.h"

// A level starts: which sectors are doors, lifts and gates, by the lines.
void P_EncounterLevel (void);

// Whether a jump from one sector to another is allowed.
boolean P_EncounterMayJump (const sector_t* from, const sector_t* to);

#endif
