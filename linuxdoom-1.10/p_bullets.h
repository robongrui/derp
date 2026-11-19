// Travelling bullets (PLAN-gunplay.md): under DERP's rules a gun's shot is still the
// game's hitscan, walked along its path a stretch each tic, so gore, wounds and
// impacts see what they always saw, a moment later.
#ifndef __P_BULLETS__
#define __P_BULLETS__

#include "p_mobj.h"

// Whether shots travel now: DERP's rules, never in demos or netgames.
boolean P_BulletsTravel (void);
// A shot on its way (P_LineAttack hands it here); its first stretch runs now, so point blank stays instant.
void P_FireBullet (mobj_t* shooter, angle_t angle, fixed_t distance, fixed_t slope, int damage, boolean freeaim);
// A shot at a point with no target, a corner or a doorway (cover fire).
void P_FireBulletAt (mobj_t* shooter, fixed_t x, fixed_t y, fixed_t z, int damage);
void P_RunBullets (void);        // each tic, after the thinkers
void P_ClearBullets (void);      // a level starts: nothing in flight (a save keeps none either)
void P_BulletsForget (mobj_t* mobj);   // it's being removed: what it fired goes with it

// Each stretch a bullet travelled, by its shooter's serial (the shooter may be gone by the time anything
// reads it), for suppression, noise and fire pressure. Null: nobody's listening.
extern void (*P_BulletPassed) (int serial, fixed_t x1, fixed_t y1, fixed_t z1, fixed_t x2, fixed_t y2, fixed_t z2);
// In that call: whether this stretch ended it, on a wall or a body.
extern boolean P_BulletStopped;

// For drawing: how many are in flight, and one's stretch this tic (false: none there).
int P_BulletCount (void);
boolean P_BulletStretch (int index, fixed_t from[3], fixed_t to[3]);

#endif
