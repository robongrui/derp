/* Finding game WADs and music packs without asking (find_wads.c). */
#ifndef DERP_LAUNCHER_FIND_WADS_H
#define DERP_LAUNCHER_FIND_WADS_H
#include "launcher.h"
#ifdef __cplusplus
extern "C" {
#endif

/* Searches, in this order, duplicates dropped: the folders of the game WADs
   already known (launcher.cfg), the wads folder beside the program and the
   bundled default.wad, DOOMWADDIR and DOOMWADPATH, Steam libraries and GOG
   installs and the Linux game data folders (each game's folder word in
   games.c), and the folders the player added. */
void FindWads(launcher_state *state);
/* One folder and up to depth folders below it. */
void FindWads_In(launcher_state *state, const char *folder, int depth);

#ifdef __cplusplus
}
#endif
#endif
