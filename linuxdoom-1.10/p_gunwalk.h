// A soldier firing on the move: a shot does not stop it. It goes on the way it
// was going through its attack, at a walk, and hits less for it, so stopping
// to shoot is still worth something.
//
// Its attack frames have no legs in them: the renderer draws walking legs under
// them (platform/scene3d/waist.cpp). Without that, it glides.
#ifndef __P_GUNWALK__
#define __P_GUNWALK__

#include "p_mobj.h"

// actor is about to fire: if it is on its way somewhere, it stays on it.
void P_GunWalkStart (mobj_t* actor);

// Each tic: a step, in the middle of its attack.
void P_GunWalkThink (mobj_t* mo);

// Whether it is walking and firing now.
boolean P_GunWalking (const mobj_t* mo);

// Its attack over: whether it walked it, which ends that. No chase step then,
// it is already on its way.
boolean P_GunWalked (mobj_t* mo);

// A shot's scatter (P_RoundMonsterAim's scale), as it is walking or not.
int P_GunWalkAim (const mobj_t* mo, int scale);

#endif
