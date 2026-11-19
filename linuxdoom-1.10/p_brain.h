// The brain: the order in which a DERP monster's parts get their say.
//
// A_Look and A_Chase (p_enemy.c) come here first. Nothing here decides
// anything itself: it asks the senses, the charge, the tactics, the imps'
// ways and the basic minds in a fixed order, and the first that acts has the
// tic. That order is behaviour, so it lives in this one file and nowhere else;
// a new part gets its place in it here, with a line saying why there.

#ifndef __P_BRAIN__
#define __P_BRAIN__

#include "doomtype.h"
#include "p_mobj.h"

// A_Look under DERP: true if handled (then the vanilla look does not run).
boolean P_BrainLook (mobj_t* actor);
// A_Chase under DERP, after the attacks: true if it moved actor (then the
// vanilla chase step does not run).
boolean P_BrainChase (mobj_t* actor);
// Hurt by source (P_DamageMobj): what it makes of that.
void P_BrainHit (mobj_t* target, mobj_t* source);
// At level setup, after the senses: the brain's answers to what they hear
// (p_senses.h's hooks). What a listener makes of a noise is a table of rules
// here, as the decisions are.
void P_BrainLevel (void);

#endif
