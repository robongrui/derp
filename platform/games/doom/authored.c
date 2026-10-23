/* DOOM's authored episodes (game_profile.h Game_Mood, Game_BrokenWindows): looks hand-picked for
   id's own maps. Only maps the IWAD itself holds get them; Freedoom's maps
   (other maps under the same names) and maps from PWADs keep the derived
   look, sun included. */
#include <stdio.h>
#include "doomdef.h"
#include "doomstat.h"
#include "w_wad.h"
#include "p_phobos.h"
#include "../../game_profile.h"

/* The map's lumps come from the IWAD (the first file loaded), and the IWAD
   is id's, not Freedoom. */
static int stockMap(int episode, int map)
{
    char name[9];
    if (gamemode == commercial) snprintf(name, sizeof name, "MAP%02d", map);
    else snprintf(name, sizeof name, "E%dM%d", episode, map);
    int lump = W_CheckNumForName(name);
    return lump >= 0 && lumpinfo[lump].handle == lumpinfo[0].handle && W_CheckNumForName((char *)"FREEDOOM") < 0;
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
           and the sky a touch warmer. */
        if (map == 2) *mood = (game_mood){ 0.65f, 0, 1, 1, 113, 0.5f, { 1.08f, 1, 0.9f } };
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
