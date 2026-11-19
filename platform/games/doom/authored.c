/* DOOM's authored episodes (game_profile.h Game_Mood, Game_BrokenWindows): looks hand-picked for
   id's own maps. Only maps the IWAD itself holds get them; Freedoom's maps
   (other maps under the same names) and maps from PWADs keep the derived
   look, sun included. */
#include <stdio.h>
#include <string.h>
#include "doomdef.h"
#include "doomstat.h"
#include "w_wad.h"
#include "p_phobos.h"
#include "p_derp.h"
#include "../../game_profile.h"

/* The map's lumps come from the IWAD (the first file loaded), and the IWAD
   is id's, not Freedoom: the game's own check (p_derp.h), which DERP's
   monsters go by too. */
static int stockMap(int episode, int map) { return P_StockMap(episode, map); }

/* The rest of each map's look, one file per IWAD (authored/<iwad>.txt, baked in by
   CMakeLists.txt): the maps of DOOM II, TNT and Plutonia share their names. */
#include "authored_info.h"
const char *Game_AuthoredInfo(int episode, int map, size_t *length, const char **file)
{
    const char *text = gamemission == pack_tnt ? authored_tnt : gamemission == pack_plut ? authored_plutonia
                     : gamemode == commercial ? authored_doom2 : authored_doom;
    if (file)
        *file = gamemission == pack_tnt ? "platform/games/doom/authored/tnt.txt"
              : gamemission == pack_plut ? "platform/games/doom/authored/plutonia.txt"
              : gamemode == commercial ? "platform/games/doom/authored/doom2.txt" : "platform/games/doom/authored/doom.txt";
    if (!stockMap(episode, map)) return NULL;
    *length = strlen(text);
    return text;
}

void Game_Mood(int episode, int map, game_mood *mood)
{
    /* Physically accurate Phobos: the base on Phobos' night side, facing
       Mars. No air, so no rain; the sun is behind the moon and lights Mars
       full in the black sky (sky.frag), whose glow is all the light outside;
       the lit rooms shine out through their windows into the dark. Any map
       of the episode, a PWAD's too: it is the physics, not a look. */
    if (P_Phobos ())
    {
        *mood = (game_mood){ 0.55f, 0, 1, 0, -1, 1, { 1, 1, 1 } };
        return;
    }
    if (!stockMap(episode, map)) return;
    /* Knee-Deep in the Dead is a base in a storm that passes as the morning
       goes on: the courtyards lie dark under a sky with no sun, the rain
       blows in from the south-east (into Hangar's broken window), and the
       lit rooms throw their light out through the windows and doors. */
    if (gamemode != commercial && episode == 1) {
        /* Hangar: before dawn, in a downpour. */
        *mood = (game_mood){ 0.55f, 0, 1, 1, 113, 1, { 1, 1, 1 } };
        /* Nuclear Plant: later, a little lighter out, the rain easing off
           and the sky a touch warmer, the wind now straight from the east,
           square onto the end pane of the window bay past the red key
           (sector 62's east end). */
        if (map == 2) *mood = (game_mood){ 0.65f, 0, 1, 1, 180, 0.5f, { 1.08f, 1, 0.9f } };
    }
    /* One painted sky over a whole episode: each map its own share of its
       weather, a little more cloud or less, and on a few the painting goes
       altogether and the clouds are the sky. */
    if (gamemode != commercial && episode == 4 && map >= 1 && map <= 9) {
        static const float cover[9] = { 0, 0.06f, -0.04f, 0.1f, 0.02f, -0.06f, 0.12f, 0.04f, -0.02f };
        mood->cloudCover = cover[map - 1];
        if (map == 3 || map == 7) mood->cloudBackdrop = 0.01f;
    }
    if (gamemode == commercial && map >= 1 && map <= 11) {
        static const float cover[11] = { 0, 0.08f, -0.05f, 0.12f, 0, 0.06f, -0.08f, 0.1f, 0.02f, -0.04f, 0.14f };
        mood->cloudCover = cover[map - 1];
        if (map == 2 || map == 7) mood->cloudBackdrop = 0.01f;
    }
}

/* A barrel went off by the window of Hangar's FLAT14 room and took the
   north pane out: rain has been blowing in since. */
int Game_BrokenWindows(int episode, int map, game_spot *spots, int max)
{
    /* On Phobos the panes held: a broken one would have emptied the room. */
    if (P_Phobos () || !stockMap(episode, map) || gamemode == commercial || episode != 1 || map != 1 || max < 1) return 0;
    spots[0] = (game_spot){ 1360, -3152 };
    return 1;
}
