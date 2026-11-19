// What each soldier is for in a squad (PLAN_DERP_AI.md, section 11). Not a
// system of its own: a role bends what soldiers already do.
//
// The zombieman is the rifleman: long reach, little health. It stays back and
// off to the side, takes cover early, covers a move and falls back from him.
// The shotgunner is the assault man: no use far off, it gets in when it sees a
// chance, running, holding its fire until it is in range. The chaingunner holds
// a lane: it stays where it is and makes that way expensive.
//
// The rifleman's fire makes the shotgunner's chance: him pinned is one.
#ifndef __P_ROLES__
#define __P_ROLES__

#include "p_mobj.h"

typedef enum
{
    ROLE_RIFLE,
    ROLE_ASSAULT,
    ROLE_LANE
} role_t;

role_t P_RoleOf (const mobj_t* mo);

// Why now is the time for it to push in on him, NULL if it is not.
const char* P_RoleChance (mobj_t* actor, mobj_t* player, boolean visible);

#endif
