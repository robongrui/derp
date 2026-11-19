/* The checks before Play, moved from sdl_main.c's choose_from_folder
 * (load_list.c), and what the list shows beside each custom file. */
#ifndef DERP_LAUNCHER_LOAD_LIST_H
#define DERP_LAUNCHER_LOAD_LIST_H
#include "launcher.h"
#ifdef __cplusplus
extern "C" {
#endif

enum {
    CHECK_OK,
    CHECK_NO_GAME,      /* no game WAD selected */
    CHECK_GAME_MISSING, /* the game WAD itself moved or deleted */
    CHECK_NOT_PLAYABLE, /* Hexen, DOOM 64...: the reason in text */
    CHECK_OTHER_GAME,   /* an add-on made for another game */
    CHECK_WRONG_MAPS,   /* MAP01 levels on an E1M1 base or the reverse; other_wad offers one that fits */
    CHECK_SHAREWARE,    /* custom WADs on a shareware game */
    CHECK_NO_PROGRAM,   /* the game's program is not beside the launcher */
    CHECK_MISSING       /* custom files gone: Play skips them after asking */
};
typedef struct {
    int problem;
    int other_wad;      /* CHECK_WRONG_MAPS: a listed game WAD that fits, else -1 */
    char text[300];
} launcher_check;

/* Reads an add-on's directory into its record (missing, map style, game). */
void LoadList_Probe(launcher_addon *addon);
/* The worst problem first; CHECK_MISSING last, since playing without them is allowed. */
void LoadList_Check(const launcher_state *state, launcher_check *check);
/* A note for the list row of one add-on ("missing", "MAP01 levels"...), "" for none. */
const char *LoadList_Note(const launcher_state *state, const launcher_wad *wad, int addon);
/* A listed game WAD of that kind (DOOM_WAD_*) with that map style (numbered: MAP01), in the games
   table's order; -1 for none. */
int LoadList_FittingWad(const launcher_state *state, int numbered, int kind);
/* Files added while no game WAD was selected join the one selected now. */
void LoadList_TakeLoose(launcher_state *state);
/* Moves the selected game WAD's load list, music and options to another game WAD and selects it. */
void LoadList_MoveTo(launcher_state *state, int wad);

#ifdef __cplusplus
}
#endif
#endif
