// Imps are opportunistic hunters, not a wave. They do not answer a far noise: they
// find somewhere out of his sight and wait for him, and only a few are after him at
// any one time. A lurking imp breaks cover when he comes close or keeps looking at
// it, and until then holds its fire: a fireball gives an ambush away. It may also
// see an opening (he looks away, he has his hands full, he is hurt) and stalk him,
// round out of his view. Imps have a rough sense of him, through walls. Rough only:
// they get anywhere on their feet like anybody else, and the clicking tells on them.
#ifndef __P_HUNTERS__
#define __P_HUNTERS__

#include "p_mobj.h"

// It heard something far off: true if it means to lurk rather than go.
boolean P_HunterHeard (mobj_t* mo, int distance);

// At its own look, having decided to lurk: up, and off to a place to wait.
boolean P_HunterWakes (mobj_t* actor);

// In its chase: true if lurking or stalking has it this tic.
boolean P_HunterThink (mobj_t* actor, boolean visible);

// Whether it is lying in wait, stalking, or done with that: p_mind.h's
// P_MindLurks, P_MindLurkWaits, P_MindStalks, P_MindLurkOver.

// He hit it: no more hiding.
void P_HunterHit (mobj_t* mo);

#endif
