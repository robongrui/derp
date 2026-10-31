/* Duct tape for id's own maps (game_profile.h Game_Courtyards): spots where
   the derived rules misread a map and a hand-picked answer is simpler than
   a rule. Not the authored layer: always on, whatever F4 says. Only maps the
   IWAD itself holds get them; the sector numbers mean nothing in a PWAD's or
   Freedoom's map of the same name. */
#include <stdio.h>
#include "doomdef.h"
#include "doomstat.h"
#include "w_wad.h"
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

int Game_Courtyards(int episode, int map, int *sectors, int max)
{
    if (!stockMap(episode, map) || gamemode == commercial || max < 1) return 0;
    /* E1M3: the courtyard 175 round the pillar, ringed by a low walkway (38,
       113 deep under 80) that the rules take for a hall round a skylight. */
    if (episode == 1 && map == 3) { sectors[0] = 175; return 1; }
    return 0;
}
