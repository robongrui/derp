// What DERP's monsters hear, see and remember (the plan's phase 4), while
// DERP's monsters play (p_derp.h, the senses part of derpparts).
//
// Hearing: a noise has a kind and a loudness (units it carries in the
// open). It spreads through the map's openings (two-sided lines), as far as
// the way there; a shut door or a line the mapper marked to block sound
// costs it distance instead of stopping it dead. A monster that hears it
// knows roughly where it came from (worse the farther it travelled), wakes
// without a sound and goes to look; the map's deaf flag is not heeded.
//
// Vision: a monster in sight of the player is not spotted at once. An
// awareness meter fills, faster near, in the middle of its view, in light,
// at a running player or one whose gun just flashed. Spotting is a shout
// the others hear.
//
// Memory: a monster after the player that loses sight goes to where it
// last saw (or heard) him, searches round there where he could have gone
// and has not been looked for, and after a while gives up: back home, back
// to watching, warier than before. Monsters never know where the player is
// unless they see or hear him.

#ifndef __P_SENSES__
#define __P_SENSES__

#include "doomtype.h"
#include "d_player.h"
#include "p_mobj.h"
#include "r_defs.h"

#include "p_mind.h"

// The kinds of noise, each its own loudness.
typedef enum
{
    NOISE_FIST, NOISE_CHAINSAW, NOISE_PISTOL, NOISE_SHOTGUN, NOISE_SUPERSHOTGUN,
    NOISE_CHAINGUN, NOISE_MISSILE, NOISE_PLASMA, NOISE_BFG,
    NOISE_FOOTSTEP, NOISE_DOOR, NOISE_DEATH, NOISE_SHOUT, NOISE_MONSTERSHOT,
    NOISE_IMPACT,
    NUMNOISES
} noise_t;
// How far a noise carries in the open, in units.
int P_NoiseLoudness (noise_t kind);

// At the end of level setup, after the navigation (p_nav.h).
void P_SensesLevel (void);
// Each tic: where searching monsters looked is crossed off.
void P_SensesThink (void);

// A noise made by source (the player, or a monster) at (x, y).
void P_DerpNoise (mobj_t* source, noise_t kind, fixed_t x, fixed_t y);
// P_NoiseAlert under DERP: the player's gun, by weapon. True if handled.
boolean P_DerpNoiseAlert (mobj_t* target, mobj_t* emitter);
// The player running is heard close by (P_PlayerThink).
void P_DerpFootsteps (player_t* player);
// How far a noise at (sx, sy) travels to (lx, ly), in units with the cost
// of what it passes through; MAXINT if it does not get there within limit.
int P_NoiseDistance (fixed_t sx, fixed_t sy, fixed_t lx, fixed_t ly, int limit);
// As a gunshot goes: filling the room it goes off in.
int P_GunDistance (fixed_t sx, fixed_t sy, fixed_t lx, fixed_t ly, int limit);

// Whether the senses are on at all (DERP's monsters, the part switch).
boolean P_SensesOn (void);
// Him, as far as the senses go: alive and not unseen; else NULL.
mobj_t* P_SensesPlayer (void);
// For the brain (p_brain.h), from A_Look once nothing else took the tic: it
// looks, awareness fills, and it may make him out. Always true.
boolean P_SensesLook (mobj_t* actor);
// How many tics actor takes to spot mo in plain view: 0 at once, -1 not at
// all (behind its back).
int P_DerpSpotTics (mobj_t* actor, mobj_t* mo);
// For the brain, from A_Chase. P_SensesSaw: he is in sight, it knows where.
// P_SensesBelieve: never told where he is, it believes in where it was going,
// or where it stands. P_SensesGetOut: standing in something that hurts, out
// (true if it moved). P_SensesChase: out of his sight, the basic minds: to
// where he was, to a noise, searching, home (true if it moved actor).
void P_SensesSaw (mobj_t* actor, mobj_t* player);
void P_SensesBelieve (mobj_t* actor);
boolean P_SensesGetOut (mobj_t* actor);
boolean P_SensesChase (mobj_t* actor);
// Hurt by source (from the brain's P_BrainHit): where that came from, exactly
// if it sees him, else roughly.
void P_SensesHit (mobj_t* target, mobj_t* source);
// Told where he is (seen, a noise of his, a shot): it knows, from now.
void P_SensesTold (mobj_t* actor, fixed_t x, fixed_t y);
// How actor's alert hastens spotting: a share of the time, in percent.
int P_DerpAlertScale (mobj_t* actor);

// What a listener heard, for the brain (p_brain.c) to answer: who, what,
// where, how far, whether a lone crack (one shot, nothing since), him; and
// where it seems to come from, worked out on asking (P_SensesGuess).
typedef struct
{
    mobj_t*	listener;
    mobj_t*	source;
    noise_t	kind;
    fixed_t	x, y;
    int		distance;
    boolean	lone;
    mobj_t*	player;
    boolean	guessed;
    fixed_t	guessx, guessy;
} heard_t;
// The brain's answers to the senses, set at level setup (P_BrainLevel): a
// noise heard; how many steps a walker may take unseen (the director's
// offstage moves); whether searchers hold back; whether a fight is on. The
// senses include nothing above them: they ask through these.
typedef struct
{
    void	(*heard) (heard_t* h);
    int		(*steps) (mobj_t* actor);
    boolean	(*holdback) (void);
    boolean	(*fighton) (void);	// a fight is on: gunfire carries farther
} senseshooks_t;
void P_SensesHooks (const senseshooks_t* hooks);
// The guess of where a heard noise came from, for its listener: a shout
// carries the shouter's belief, a doorway it came through is where it seems
// to be, else a rough point. Rolls the dice, so only when the answer needs it;
// once per noise (guessed).
void P_SensesGuess (heard_t* h);
// A rough point near (x, y): the farther, the rougher. Rolls the dice.
void P_SensesRough (fixed_t* x, fixed_t* y, int distance);
// Whether mo is after him with fresh word of him.
boolean P_SensesHunting (mobj_t* mo);
// Whether mo has a way to (x, y) at all, as it would walk it.
boolean P_SensesReachable (mobj_t* mo, fixed_t x, fixed_t y);

// The automap's overlay (-derpnav): each monster's goal (kind 2).
void P_SensesDraw (void (*line)(fixed_t x1, fixed_t y1, fixed_t x2, fixed_t y2, int kind));

// From p_enemy.c: the sight sound and the chase state, as A_Look's.
void P_SeeYou (mobj_t* actor);
// Each tic for a climber mid-jump up a ledge: over the edge once high
// enough (a mantle; P_DemonThink).
void P_DerpMantle (mobj_t* mo);
// Each tic for a climber after its jump: down where it meant to be, or a
// failure counted against that jump (twice, Walk plans without it for a
// while, and no blink stands in for it).
void P_DerpJumped (mobj_t* mo);
// Whether mo is mid-mantle, and its lean (towards the ledge; a unit way
// reaching up, two bent over the edge): the renderer tips the body.
boolean P_DerpMantleLean (mobj_t* mo, fixed_t* dx, fixed_t* dy);
// For the tactics: a step towards (x, y) along the navigation's route, a
// step in actor->movedir, and when the player's gun last flashed.
void P_DerpWalkTo (mobj_t* actor, fixed_t x, fixed_t y);
boolean P_DerpWalkStep (mobj_t* actor);
int P_DerpLastShot (void);
// Around a route asked for: as actor would walk it, its jumps and without the
// one it has given up on (the walk and whatever asks if there is a way must agree).
void P_DerpRouteBegin (mobj_t* actor);
void P_DerpRouteEnd (void);
// How low a climber is, gathering for a jump or taking a landing (FRACUNIT: all the way).
fixed_t P_DerpCrouch (const mobj_t* mo);
// A round arrived by mo, from about (x, y), back along the shot (p_fire.h):
// his or another's. A noise made to it alone.
void P_DerpRoundHeard (mobj_t* mo, mobj_t* source, boolean his, fixed_t x, fixed_t y);
// Someone on that spot, to land on.
boolean P_DerpCrowdedAt (mobj_t* actor, fixed_t x, fixed_t y);

#endif
