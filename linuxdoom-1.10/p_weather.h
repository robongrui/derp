// Weather that is a rule of the game (DERP): the renderer draws all of it
// (platform/scene3d/weather.cpp); what hurts is decided here, so it plays
// the same for everyone in the game.

#ifndef __P_WEATHER__
#define __P_WEATHER__

#include "doomtype.h"

// F4 Weather: 0 never, 1 authored (the map's DERPINFO), 2 rain, 3 snow, 4 nukage rain.
extern int	weatherchoice;
// Whether nukage rain falls on this level: asked for (F4, or the map's
// DERPINFO under Authored), never on Phobos (no air to fall through), in
// demos or netgames (input-only demos would desync).
boolean P_NukageRain (void);
// Each tic: nukage rain hurts what lives under the open sky, or where it
// blows in at a broken window, 2 every 32 tics (a nukage floor's 5 was too much); a radiation suit keeps it off a player,
// and the monsters take it without flinching.
void P_WeatherThink (void);

#endif
