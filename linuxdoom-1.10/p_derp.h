// DERP's monsters: tougher, faster, with bodies that fall and get hurt like
// the player's (the plan: "DERP – Enemy AI and Combat Overhaul"). A rule of
// the game, decided here, so saves and the renderer see the same monsters.
//
// On with the Authored gameplay (F4) on id's own maps, where the encounters
// are known and tuned for; on any map with -derpai. Off with -noderpai, in demos or
// netgames (input-only demos would desync), never on Nightmare or with
// -respawn: monsters that come back would make hunters without end.

#ifndef __P_DERP__
#define __P_DERP__

#include "doomtype.h"
#include "p_mobj.h"

// F4 Gameplay: Authored (Game_SetAuthored).
extern boolean	derpauthored;
// Each part on its own, for trying them apart; all on by default.
typedef struct
{
    boolean	numbers;	// health, pain and speed per type
    boolean	ledges;		// a blow carries monsters over ledges
    boolean	falls;		// landing hard hurts
    boolean	floors;		// nukage and lava hurt monsters too
    boolean	senses;		// hearing, sight and memory (p_senses.h)
    boolean	tactics;	// squads and tactics (p_tactics.h)
    boolean	director;	// pacing and offstage moves (p_director.h)
    boolean	demons;		// imps' ways, demons healing (p_demons.h)
    boolean	strides;	// steps walked out over the tics (p_strides.h)
} derpparts_t;
extern derpparts_t	derpparts;

// Whether the map in play is id's own: its lumps from the IWAD (the first
// file loaded), and the IWAD not Freedoom. Authored looks and monsters
// are made for these maps only.
boolean P_StockMap (int episode, int map);
// At level setup: whether this map is id's own, before any thing spawns.
void P_DerpLevel (void);
// Whether DERP's monsters play now.
boolean P_DerpAI (void);
// Whether that changed since the level's monsters spawned (F4): the map
// starts again so they spawn as they now should.
boolean P_DerpChanged (void);

// What a monster of type spawns with; the arch-vile raises it to the same.
int P_DerpHealth (mobjtype_t type);
// The possessed soldiers: zombiemen, shotgunners, chaingunners, the SS.
boolean P_DerpSoldier (mobjtype_t type);
// What every part of the AI calls "him": the console player while alive, else NULL.
mobj_t* P_DerpPlayer (void);
// A live monster (NULL is none).
boolean P_DerpMonster (mobj_t* mo);
// Still in its look state: not yet roused.
boolean P_DerpAsleep (mobj_t* mo);
// Down on one knee (the renderer squashes its legs; p_tactics.c's Kneel).
boolean P_DerpKneeling (mobj_t* mo);
// Whether mo climbs: jumps up ledges and sills and down drops on its way
// (p_nav.h's navclimb), and opens secret doors (P_UseSpecialLine). The imps,
// while the demons' part is on.
boolean P_DerpClimber (mobj_t* mo);
// Whether a hit of damage throws mo into its pain state (a P_Random roll,
// as vanilla's, so nothing else in the game moves).
boolean P_DerpPain (mobj_t* mo, int damage);
// How far a monster steps each time it chases (fixed point).
fixed_t P_DerpStep (mobj_t* mo);
// The player's push (P_MovePlayer's thrust), and so his top speed, in DERP's
// game: Quake's ranger, not Doomguy. His top running speed (fixed point, a
// tic), for what measures itself against it.
int P_DerpPace (int thrust);
// The tuned movement's friction for the player under DERP's pace (a key
// held or not): a harder grip, so slower is not heavier.
fixed_t P_DerpGrip (boolean pressing, fixed_t friction);
// The view and weapon bob (speed squared) as at Doomguy's run speed.
fixed_t P_DerpBob (fixed_t bob);
fixed_t P_DerpRunSpeed (void);

// Whether mo is moving by momentum alone, as a body thrown: then a ledge
// no longer holds it (P_TryMove). Set by P_XYMovement around its moves.
extern boolean	derpmomentum;
boolean P_Shoved (mobj_t* mo);
// And it is in the air (or leaving the floor, or kicked off its feet): a line that
// only fences monsters in no longer holds it either (PIT_CheckLine).
boolean P_DerpThrown (mobj_t* mo);
// Whether mo is mid-jump of its own (a climber's leap, off the floor or
// taking off): it goes over a line that fences monsters in (P_TryMove).
boolean P_DerpJumping (mobj_t* mo);
// mo stands in other already, and a step to (x, y) takes it further out. Two that came
// down on one spot (a jump goes through friends) could not move at all: no step from
// there was clear of the other, and both stood until he killed or left them.
boolean P_DerpParting (mobj_t* mo, mobj_t* other, fixed_t x, fixed_t y);
// mo, an imp going over him, is clear of his head.
boolean P_DerpOverHim (mobj_t* mo, mobj_t* him);
// Whether it stands across this line already (a jump put it there): it may step off.
boolean P_DerpAstride (mobj_t* mo, line_t* ld);
// The share of its step a monster's next moves take, in 256ths.
void P_DerpStepShare (int share);
// Whether the door behind ld is up far enough for mo: a monster blocked
// there was stopped by something else (the frame, a friend), not the door,
// and its step is no step (P_Move).
boolean P_DerpDoorUp (mobj_t* mo, line_t* ld);
// Whether mo walks the navigation's route (set by the senses' walking):
// then it may stand over a drop of up to two stairs (P_TryMove), as a body
// does on a narrow staircase the route goes down.
extern boolean	derproute;
// Standing in a floor that hurts it (not the pool it was put in).
boolean P_DerpBurning (mobj_t* mo);
// Whether a step to (x, y) is off, being onto such a floor from a dry one.
boolean P_DerpKeepsOut (mobj_t* mo, fixed_t x, fixed_t y);
boolean P_DerpStairs (mobj_t* mo, fixed_t drop);
// A thrown body keeps sliding while it hangs half over an edge, as corpses
// do (P_XYMovement's friction).
boolean P_DerpSliding (mobj_t* mo);
// Off its feet in the air: a falling body cannot walk (P_Move).
boolean P_DerpAirborne (mobj_t* mo);
// A blow's maker, kept for a fall it causes (P_DamageMobj).
void P_DerpShove (mobj_t* mo, mobj_t* source);
// Each new thing's number (P_SpawnMobj), and the count kept past a loaded
// thing's so new ones stay apart from it (P_UnArchiveThinkers).
int P_DerpSerial (void);
void P_DerpLoaded (mobj_t* mo);
// mo hit the floor at momz (negative, fixed point): a hard landing hurts.
void P_DerpLanded (mobj_t* mo, fixed_t momz);
// Each tic for a monster on the floor: nukage, slime and lava hurt.
void P_DerpFloor (mobj_t* mo);

#endif
