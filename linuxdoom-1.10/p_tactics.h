// DERP's squads and tactics (the plan's phase 5), while DERP's monsters play
// and their senses are on (p_senses.h).
//
// Squads: the monsters a room holds as the level loads are a squad (the
// navigation's rooms, p_nav.h), soldiers and demons apart: they warn each
// other, but are no squad together. Each monster has nerve, from 0 to 100,
// rolled as it spawns, and keeps what it has heard: how many of its own it
// heard die. A death scream heard costs nerve; a headshot kills in silence,
// and a body is found only by seeing it, which puts the finder on alert:
// quicker to spot, from wider about.
//
// Tactics, for the soldiers (the possessed: zombiemen, shotgunners,
// chaingunners, the SS): what to do is scored from health, losses heard,
// nerve and whether friends are near, again on each blow, each loss and
// every couple of seconds:
// - fight: vanilla's chase and attack;
// - cover: under fire, to a spot the player cannot see with one beside it
//   that he can; there it ducks, peeks, fires, ducks again;
// - retreat: badly hurt and afraid, out of sight, away from him; back
//   later;
// - ambush: fellow soldiers heard dying (any squad's), the fight gone quiet
//   and the player not seen, it holds beside a doorway of its room, out of
//   the doorway's view, and waits for him;
// - regroup: alone, its squad heard dead, to the nearest other squad, and
//   one of them from there on.
// A soldier about to shoot a friend in the back makes room: the friend
// kneels (its real height lowered, so the shot passes over), or it steps
// aside.

#ifndef __P_TACTICS__
#define __P_TACTICS__

#include "p_corners.h"
#include "p_mind.h"
#include "p_derp.h"
#include "p_cover.h"
#include "doomtype.h"
#include "p_mobj.h"

// At the end of level setup, after the senses: squads and nerve.
void P_TacticsLevel (void);

// shooter's round met target's box at (x, y, z) climbing at slope, along
// trace (PTR_ShootTraverse): through the head as drawn to him (p_heads.c)?
void P_DerpShotAt (mobj_t* target, mobj_t* shooter, fixed_t x, fixed_t y, fixed_t z, fixed_t slope);
// target was killed by source (P_KillMobj): silent if by a headshot from his pistol.
void P_DerpKilled (mobj_t* target, mobj_t* source);
// Whether mo died without a sound (A_Scream).
boolean P_DerpSilent (mobj_t* mo);
// listener heard dead's death scream (p_senses.c).
void P_DerpLossHeard (mobj_t* listener, mobj_t* dead);
// actor, watching or searching, sees a body of its own not yet found: the
// alarm. True if it found one.
boolean P_DerpBodies (mobj_t* actor);

// A change of mind (P_MindBecome): what a soldier leaves behind. gone: it is
// leaving cover, so a knee behind a low wall straightens.
void P_DerpCoverLeft (mobj_t* actor, boolean gone);
// In P_BrainChase, before the hunt: true if a tactic moved actor this tic.
// visible: whether it sees the player now.
boolean P_DerpTactics (mobj_t* actor, boolean visible);
// In A_Chase, about to fire: true if a friend is in the way (then it makes
// room instead of firing).
// Whether it may stop to fire now: the one check before any attack from a
// chase. Not on the way round him, on a bound, running to cover or away, or
// in a doorway, unless he is right on top of it.
boolean P_DerpMayFire (mobj_t* actor);
// Whether one of its side stands in its line of fire now (a burst stops).
boolean P_DerpLaneBlocked (mobj_t* actor);
boolean P_DerpAllyInTheWay (mobj_t* actor);
// A corner of the map near actor to shelter behind from threat, and the
// spot to lean out to that sees it (p_corners.h). False if none serves.
boolean P_DerpCornerCover (mobj_t* actor, mobj_t* threat, fixed_t* cx, fixed_t* cy, fixed_t* kx, fixed_t* ky);
// How good the last one it gave is, best first.
enum { TIER_SOLID, TIER_HIDDEN, TIER_WAITS, TIER_LOW, TIER_ENOUGH };
int P_DerpCornerTier (void);
// Monsters guarding a key (given out at P_TacticsLevel). Whether a noise at
// (x, y) is too far from its key to leave it for; if so it only readies
// (alert, facing it; a reserve the director holds does too, and a soldier
// of either then takes a post at its own look, P_DerpGuardPost).
// P_DerpLeash: led too far and out of his sight, back.
// P_DerpKeyTaken: the duty ends, with an alarm.
boolean P_DerpGuarding (mobj_t* mo, fixed_t x, fixed_t y);
// P_DerpOnDuty: it holds something (a key, a pickup, a switch, a hidden door).
boolean P_DerpOnDuty (const mobj_t* mo);
// A dead soldier lies this near.
boolean P_DerpBodyNear (fixed_t x, fixed_t y, int range);
boolean P_DerpGuardPost (mobj_t* actor);
void P_DerpLeash (mobj_t* actor, boolean visible);
void P_DerpKeyTaken (mobj_t* key);
// In A_Chase after a shot: a soldier within its range holds its fire
// position (no step, as vanilla's chase takes after each attack).
boolean P_DerpHolding (mobj_t* actor);
// In A_Chase after its step: a soldier in a fight faces the player whichever
// way it stepped (backing off, aside, into position), unless it is running.
void P_DerpFacing (mobj_t* actor);
// The slope a soldier's shot takes (its attacks): at its target for DERP's
// soldiers, else vanilla's aim (P_AimLineAttack).
fixed_t P_DerpAim (mobj_t* actor, angle_t angle);
// A spot near actor that threat cannot see (away: farther from threat than
// actor is), that actor can walk to. The imps hide there too (p_demons.h).
boolean P_DerpHideSpot (mobj_t* actor, mobj_t* threat, boolean away, fixed_t* x, fixed_t* y);
// In A_Chase, after its step: a soldier under fire runs for it, a second
// step (dashing to cover, running to hide, or in the open while the player
// shoots and sees it). True if it took one.
boolean P_DerpSprint (mobj_t* actor);
// A far fight heard: whether its room keeps this one in, enough having gone.
boolean P_DerpGarrisonHolds (mobj_t* mo);
// He pressed a switch: its guards are free.
void P_DerpSwitchUsed (line_t* ld);
// Somewhere near it hidden from `from`.
boolean P_DerpHide (mobj_t* actor, mobj_t* from, fixed_t* x, fixed_t* y);
// The way a monster is drawn facing: its angle, a moment late.
angle_t P_DerpDrawAngle (const mobj_t* mo);
// As P_DerpHideSpot, from the player as actor believes him: where it last
// saw or heard him unless it sees him now (visible).
boolean P_DerpHideFrom (mobj_t* actor, boolean visible, boolean away, fixed_t* x, fixed_t* y);
// An imp lying in wait, its place about to be seen from where it believes him (from
// there, or from a step to either side): SLIP_HIDDEN, it is not; SLIP_GOES, there is
// a new hidden place (*x, *y) it can walk to with none of the way in his sight;
// SLIP_STAYS, there is none, and it keeps still (no hopping under his gun).
enum { SLIP_HIDDEN, SLIP_GOES, SLIP_STAYS };
int P_DerpLurkSlip (mobj_t* actor, boolean visible, fixed_t* x, fixed_t* y);
// Each tic for each thing: a kneeling soldier stands up again in time.
void P_DerpBodyThink (mobj_t* mo);
// Whether mo leans out of cover to shoot, and which way and how far (a unit
// way at the lean, fixed point): the renderer tips the body that way.
boolean P_DerpLean (mobj_t* mo, fixed_t* dx, fixed_t* dy);
// How tall it is drawn: kneeling, more than the half body the game keeps.
// The player's shots go by this; the soldiers' by mo->height.
fixed_t P_DerpShown (mobj_t* mo);
// Whether source hitting target is one soldier's stray shot into another:
// it hurts, but the hit one does not turn on the shooter (P_DamageMobj).
boolean P_DerpFriendlyFire (mobj_t* target, mobj_t* source);
// A demon has hit a soldier, who turned on it: some of its squad do too.
void P_DerpSquadAnswers (mobj_t* target, mobj_t* source);
// An arch-vile raised mo: it comes back knowing nothing (A_VileChase).
void P_DerpRaised (mobj_t* mo);
// actor is about to fire (A_Chase): a peek ends after its shot.
void P_DerpFiring (mobj_t* actor);

// Looks towards where it thinks he is.
void P_DerpFaceDanger (mobj_t* actor);
// Right up against the place it means to hide.
boolean P_DerpTucked (mobj_t* actor);
// No bodies lying about (for tests that stage one).
void P_DerpForgetBodies (void);
// He is in sight: a new sighting starts the beat before its first shot.
void P_DerpSighted (mobj_t* actor);

#endif
