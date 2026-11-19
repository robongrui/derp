// DERP's demons (the plan's "Imps up close", "Demons heal" and the imps'
// place as hunters), while DERP's monsters play and their senses are on
// (p_senses.h).
//
// The imps are hunters: alone, quick, close. Up close a swipe comes almost
// at once and may come again, two or three in a row, each shoving the
// player a little (off a ledge, into a pool). From a few metres one may
// pounce: a rasp, arms up, then a leap at where he will be, claws out.
// After its blows it may leap back and away out of his sight, to come again
// from wherever its way leads. Badly hurt, it runs where he cannot see it
// and heals there. Looked at down a gun barrel, it side-steps. Its
// fireballs lead a moving player, more or less.
//
// Demons heal: any demon (not the possessed soldiers, whose bodies are
// dead, nor the bosses) left wounded out of the player's sight for a while
// heals slowly, to three quarters of its health, with a low rasp now and
// then that tells where it is.

#ifndef __P_DEMONS__
#define __P_DEMONS__

#include "doomtype.h"
#include "p_mobj.h"

// Whether a monster of type heals out of sight.
boolean P_DerpDemon (mobjtype_t type);

// In P_BrainChase, after the tactics: true if it moved actor this tic.
// visible: whether it sees the player now.
boolean P_DerpHunt (mobj_t* actor, boolean visible);
// In A_Chase, the player in reach of its claws: true if it took over
// (a swipe at once, or a leap away).
boolean P_DerpMelee (mobj_t* actor);
// Whether actor is getting away (to heal, or after its blows): it does not
// stop to swipe (A_Chase).
boolean P_DerpFleeing (mobj_t* actor);
// He shot it: an imp on its way round him stops and turns on him.
void P_DerpShotRunning (mobj_t* mo);
// An imp of a pack on its way round him runs, no stopping for a fireball
// (A_Chase): p_mind.h's P_MindRunningRound. One holding him while it does:
// it stands and shoots, no step after a shot.
boolean P_DerpShooting (mobj_t* actor);
// A_TroopAttack's claw landed: a shove, and maybe another swipe.
void P_DerpClawed (mobj_t* actor);
// A_TroopAttack threw missile: aimed again where its target is going.
void P_DerpLead (mobj_t* actor, mobj_t* missile);
// Each tic for each thing: a pounce in the air, and healing out of sight.
void P_DemonThink (mobj_t* mo);

#endif
