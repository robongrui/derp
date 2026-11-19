// What each gun fires under DERP's rules (PLAN-weapons.md): how fast, how it
// scatters, how far, what it loses on the way. The same round whoever fires
// it. Off with the travelling bullets: demos, multiplayer, DERP off.
//
// Scale: 32 units a metre, 35 tics a second. Every number is a first guess.
#ifndef __P_ROUNDS__
#define __P_ROUNDS__

#include "p_mobj.h"

typedef enum { ROUND_PISTOL, ROUND_ROTARY, ROUND_PELLET, NUMROUNDS } round_t;

// What shooter's gun fires now.
round_t P_RoundOf (mobj_t* shooter);
// Units a tic, and how far one goes at all.
fixed_t P_RoundSpeed (round_t kind);
fixed_t P_RoundReach (void);
// What is left of damage after flying that far.
int P_RoundDamage (round_t kind, int damage, fixed_t flown);
// What a hit does to target: the slayer shrugs off most of a pellet. It is
// his body, not the shell: the same shell, from anybody. And his pistol round
// through a soldier's head (P_DerpShotAt) ends it, in silence: the careful
// way through a level.
int P_RoundTaken (mobj_t* target, mobj_t* shooter, round_t kind, int damage);

// A body a round has just killed, and whether target is one. Doom's dead aren't
// shootable, so the rest of a volley flew on through at full strength; now it
// keeps a quarter (PTR_ShootTraverse). A living body stops a round outright.
void P_RoundKilled (mobj_t* target);
boolean P_RoundFresh (mobj_t* target);
int P_RoundThrough (int damage);

// A shot's own grouping about (angle, slope): the round's, nothing else.
void P_RoundScatter (round_t kind, angle_t* angle, fixed_t* slope);
// The player's: the grouping and the drift his trigger finger has built up.
void P_RoundPlayerAim (mobj_t* mo, angle_t* angle, fixed_t* slope);
// A monster's aim for this volley, then each round of it scatters as above.
// scale: 256ths of its usual error.
void P_RoundMonsterAim (angle_t* angle, fixed_t* slope, int scale);

// A pump's pellets. The double barrel keeps stock's (p_pspr.c).
#define PUMP_PELLETS	9

// A missile's speed: a rocket or plasma bolt's own, else as its info says.
fixed_t P_RoundMissileSpeed (mobj_t* missile);

// A BFG ball goes off at mo: branches from there to what it sees near it.
void P_RoundSpray (mobj_t* mo);

#endif
