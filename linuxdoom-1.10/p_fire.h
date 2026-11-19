// Shots as information (PLAN_DERP_AI.md's Stage S): what a travelling bullet
// (p_bullets.h) tells the monsters as it passes. Off with the bullets: demos,
// multiplayer, DERP off.
//
// His rounds near a soldier suppress it, that one and not the next. A round
// near any monster is a noise with a direction, back along the shot. Enemy
// rounds near him are the pressure he is under, and if he stays put under
// them, he is pinned. Monsters never dodge a bullet in flight: all of this
// comes after it has gone by.
#ifndef __P_FIRE__
#define __P_FIRE__

#include "p_mobj.h"

// A level starts: the hook on, nothing heard yet.
void P_FireLevel (void);

// mo's suppression now, fading since the last round.
int P_DerpSuppression (mobj_t* mo);
// Whether mo is under fire: rounds near it; for what is not a bullet (a
// rocket, plasma, no bullets at all), he in its sight and firing.
boolean P_DerpUnderFire (mobj_t* mo, boolean visible);
// Whether it has been shot at lately: for a step aside, a run.
boolean P_DerpRoundsNear (mobj_t* mo, int tics);

// How hard the rounds are coming in on him. Heavy: hold the next ones back.
// Light with a fight on: they can do with more.
boolean P_FireHeavy (void);
boolean P_FireLight (void);
// Rounds are coming in on him right now: he has his hands full. Heard from
// next door, it is the moment to go in. (Him shooting at nobody shooting back,
// picking them off at a door, is not that.)
boolean P_FightOn (void);
// He has stayed put with rounds coming in for a few seconds.
boolean P_DerpPinned (void);

// Whether actor is laying fire on where it believes he is (p_tactics.c starts
// it), and the rounds of one shot, at that place.
boolean P_DerpLaying (mobj_t* actor);
boolean P_DerpLayRounds (mobj_t* actor, int rounds);

// A chaingunner's burst walks onto him: how far off this round starts, or
// false for its usual spread.
boolean P_DerpWalkFire (mobj_t* actor, angle_t* off);

#endif
