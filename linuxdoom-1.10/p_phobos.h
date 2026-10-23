// Physically accurate Phobos: Knee-Deep in the Dead taken literally. The
// first episode is on Phobos, so things that leave the floor fall at its
// gravity, slide on with almost no friction and are pushed by the real
// momentum of what hits them. Living walkers wear mag boots: they walk as
// in Doom, and the boots pull them onto a floor within a step of their feet.
//
// Scale: 32 units to the metre, 35 tics a second. Phobos pulls 0.0057 m/s^2,
// 1.49e-4 units a tic a tic; Doom's own GRAVITY is about 3.9 g.

#ifndef __P_PHOBOS__
#define __P_PHOBOS__

#include "d_player.h"
#include "r_defs.h"

// The switch (F4 or -phobos); P_Phobos says whether it applies now: DOOM's
// first episode, never in demos or netgames (input-only demos would desync).
extern boolean	phobosphysics;
boolean P_Phobos (void);
// Each tic (the mode switches with its F4 setting): the barrels' mass, on Phobos 225 kg
// of monopropellant and drum.
void P_PhobosMasses (void);
// What mo weighs on Phobos, in kilograms: pickups as they would (a box of shells 0.2,
// armour 8-12), the rest DOOM's mass.
float P_Mass (mobj_t* mo);
// A hard knock (lost: the speed it took off at once): a barrel goes off past 3 m/s.
void P_Jolt (mobj_t* mo, fixed_t lost);
// The draft's push on mo this tic (units a tic; P_Jolt leaves it out: a press, no knock).
void P_Windblown (mobj_t* mo, fixed_t push);
// Whether line holds whole window glass (the renderer's panes, P_PhobosGlass).
boolean P_GlassWhole (line_t* line);
// mo, going hitx, hity, stopped by line: glass breaks if the blow is as hard as a bullet's,
// and mo goes on through, slower by what breaking it took; that loss (-1: no glass broke).
fixed_t P_Strike (mobj_t* mo, line_t* line, fixed_t hitx, fixed_t hity);

// Whether mo walks on mag boots: a living player or monster on its feet.
boolean P_Booted (mobj_t* mo);
// A draft too strong for a monster's boots: it is lifted off its feet and
// drifts (not booted) until it comes down on a floor again.
void P_TearLoose (mobj_t* mo);
boolean P_Adrift (mobj_t* mo);
// Whether mo goes where it is carried (Phobos): a monster adrift, or a body or
// loose thing on the move. It may go over ledges and lines that only stop
// monsters walking, and slides along walls instead of stopping dead.
boolean P_Loose (mobj_t* mo);
// Loose and off the floor (or torn off its feet): the air takes it over a sill
// or under a lintel into any gap it fits (P_TryMove).
boolean P_Carried (mobj_t* mo);
// A draft (units a tic a tic) on a monster still in its boots: its feet hold, the
// top of it takes the pull; past grip for a moment the boots give (true: tear).
boolean P_Strain (mobj_t* mo, float x, float y, float z, float grip);
// The draft on a monster in its boots this tic, how hard (0: none) and its way.
float P_Draft (mobj_t* mo, float* way);
// The former humans as brainless zombies (Phobos): no boots; P_BodyStep (from
// P_Move) is a step off the floor that throws them off it, P_BodyThink turns the
// body each tic as it spins, P_BodyContact the bump of a floor or ceiling,
// P_BodyRecoil the last shot's kick (newton-seconds); P_BodyAim where it shoots (the
// angle, and the slope returned): at its target as far as the arms swing the gun off
// the body's way; P_BodyUp its up (interpolated by fraction) for the renderer.
boolean P_Brainless (mobj_t* mo);
void P_BodyThink (mobj_t* mo);
boolean P_BodyStep (mobj_t* mo);
void P_BodyContact (mobj_t* mo, fixed_t impact, boolean ceiling);
void P_BodyRecoil (mobj_t* mo, int impulse);
// Whether mo tumbles freely (a brainless zombie, a barrel); P_BodyDraft: the draft's
// pull on its top and bottom halves (as P_Air's Draft, times k) turns it.
boolean P_Tumbling (mobj_t* mo);
void P_BodyDraft (mobj_t* mo, const float* top, const float* bottom, float k);
// Carried over a sill (edge: its height above the bottom) or under a lintel (top: edge
// below the top): the rim catches and trips it, pitching the top on (or back).
void P_BodyCatch (mobj_t* mo, fixed_t edge, boolean top);
// A knock (a wall, a pane: lost, the speed it took off) spins a tumbling body or barrel.
void P_BodyKnock (mobj_t* mo, fixed_t lost);
fixed_t P_BodyAim (mobj_t* mo, int* angle);
boolean P_BodyUp (mobj_t* mo, float fraction, float* up);
// Each tic (P_AirThink): Begin, Land for every thing (stood up once it is down on a floor;
// adrift once a monster is up out of its boots' reach), End.
void P_AdriftBegin (void);
void P_AdriftLand (mobj_t* mo);
void P_AdriftEnd (void);

// The pull on mo this tic, for P_ZMovement.
fixed_t P_Gravity (mobj_t* mo);

// Friction for a thing on the floor that is not booted: mu*g off its speed,
// down to a dead stop, no snapping to zero below STOPSPEED.
void P_PhobosSlide (mobj_t* mo);

// Air (p_air.c): how much a sector holds, 0 (vacuum) to 1 (the base's
// pressure); off Phobos always 1. P_AirLevel after a level is set up,
// P_AirThink every tic (venting, refilling, the drafts on things),
// P_PhobosGlass for each window pane as it is found or breaks.
float P_SectorAir (sector_t* sector);
void P_AirLevel (void);
void P_AirThink (void);
void P_PhobosGlass (int line, int whole);
// Whether air passes line: two-sided, open, no whole pane in it. What the air
// carries goes through even where the map blocks walking (a window's line).
boolean P_AirPasses (line_t* line);
// Whether a sector's floor is frozen over (Phobos): its slime's damage frozen in with it.
boolean P_SectorIced (sector_t* sector);
// Whether a shut door sector stands between air and vacuum (or much
// thinner air), so it cycles for AIRLOCK_TICS before it opens: creeping
// up at 1/AIRLOCK_CREEP of its speed while the air hisses through, never
// sitting still (a stall read as a stuck door).
#define AIRLOCK_TICS	(TICRATE*3/2)
#define AIRLOCK_CREEP	6
boolean P_AirlockCycle (sector_t* door);
// The openings venting now: where (map units), the way the air goes, how
// fast through them (units a tic), how wide, from which sector.
typedef struct { float x, y, z, dx, dy, speed, width; int sector, line; } phobos_vent_t;
int P_AirVents (phobos_vent_t* out, int most);

// Whether mo, at the sky over it, rose faster than Phobos' escape velocity
// (10.3 units a tic): then it is removed and the player told.
boolean P_Escaped (mobj_t* mo);
// Whether the sky over mo is open (Phobos): no ceiling stops it going up.
boolean P_OpenSky (mobj_t* mo);

// The push of a hit of damage on something of mass, units a tic.
fixed_t P_PhobosThrust (int damage, int mass);

// A shot's kick on a player (on the floor boots would take it; the player has none):
// impulse in newton-seconds, opposite to where the shot goes; slope RECOIL_VIEW
// for a missile: the kick follows the view.
#define RECOIL_VIEW	MAXINT
void P_Recoil (player_t* player, int impulse, fixed_t slope);
// Each tic: the player's body turning from his guns' kicks (P_Recoil) while nothing
// holds it; P_LookKick hands the view's share to the renderer (radians up since asked).
void P_PlayerSpin (player_t* player);
float P_LookKick (void);
// The view over the top (Phobos, Guns turn you): upside down and facing about; P_FlipView
// turns it over (the renderer, as his spin carries the view past straight up or down).
boolean P_ViewFlipped (void);
void P_FlipView (void);

// The movement keys fire the suit's thrusters along the view (its pitch too):
// gentle, enough to get about, come down off a ledge or steer in the air. Without
// boots only with the run key held too (the keys alone hop, P_Jump).
void P_Jetpack (player_t* player);
// On the floor: the jump key (BT_JUMP) jumps, held movement keys hop gently (30 cm);
// both lean into the way the movement keys point as far as the feet grip. Use with the
// movement keys by a wall (on the floor or aloft): a kick off it, along the floor.
void P_Jump (player_t* player, boolean onground);
// Hitting the ceiling while hopping (movement keys held): he pushes back down off
// it with his hands (true), as off a floor; false: it stops him (P_Touchdown).
boolean P_CeilingHop (mobj_t* mo);
// A body (not in boots) hitting a floor or ceiling at impact (its momz): the
// friction of the landing brakes its speed along it; hopping, the legs take it.
void P_Touchdown (mobj_t* mo, fixed_t impact);

#endif
