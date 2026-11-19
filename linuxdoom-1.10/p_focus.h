//
// The slayer's focus (p_focus.c): what slow motion costs in Authored, and what
// pays it back. Counted in tics of slowed world; one charge is one shootslide,
// a kill gives half a charge back and a clean kill a whole one. Outside
// Authored slow motion stays free.
//

#ifndef __P_FOCUS__
#define __P_FOCUS__

#include "d_player.h"

// What a shootslide costs and a kill gives back, in tics of slowed world by hand. Four in
// the meter.
#define FOCUS_CHARGE	48
// How long a shootslide's own slow motion lasts: its burst and glide, about 2.3 real
// seconds at a quarter speed (the whole slide and a hold, 48, was too long; the user).
#define FOCUS_SLIDE	20
#define FOCUS_CHARGES	4
#define FOCUS_MOST	(FOCUS_CHARGE*FOCUS_CHARGES)

// Set by the private dev tools (-slowmotionfree): slow motion costs nothing,
// for filming. Never set in a published build.
extern int P_FocusFree;

// Authored: slow motion has a meter.
boolean P_FocusRules (void);
// ...and it is paid for (not the dev tools' free filming).
boolean P_FocusCosts (void);
// The console player's meter, in tics.
int P_FocusLeft (void);
// A shootslide wants the world slow now.
boolean P_FocusAsked (void);
// The meter ran down this last tic (the hourglass's stream).
boolean P_FocusFlowing (void);

// From the platform each frame (slow_motion.c through the game profile):
// whether the world is slowed by hand (the key, a shot), and the game clock's
// speed (1 full, 0.25 slowest).
void P_FocusHand (boolean slow);
void P_FocusClock (double scale);

// A new life or a new game: a full meter.
void P_FocusReborn (player_t* player);
// P_KillMobj: the player's kill puts half a charge back, a clean one (a head
// shot, a kill while sliding) a whole one.
void P_FocusKilled (mobj_t* source, mobj_t* target);
// p_moves.c: a slide started from the shootslide button.
void P_FocusShootslide (player_t* player);
// Each tic of a live player (P_PlayerThink).
void P_FocusTic (player_t* player);
// The weapon's frames for this tic: once, or in slow motion as often as its
// own clock asks (switching at full speed, firing at the root of the slowing).
void P_FocusPsprites (player_t* player);

#endif
