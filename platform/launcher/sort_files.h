/* One rule for every way files come in (dropped on the window or the icon,
 * Add buttons): each file goes to the slot it belongs to (sort_files.c). */
#ifndef DERP_LAUNCHER_SORT_FILES_H
#define DERP_LAUNCHER_SORT_FILES_H
#include "launcher.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int game_wads;   /* game WADs added to the games list */
    int addons;      /* custom WADs and patches added to the load list */
    int music;       /* music packs, music WADs and SoundFonts taken */
    int refused;
    char refusal[256]; /* the last refusal, one line */
    char note[256];    /* news that is no refusal (a pack kept for other games) */
} sort_result;

/* Sorts paths (files or folders) into state. replace: the files came on the
   program's icon, so they make up the load list instead of joining it, on a
   base game that fits them. The last game WAD among them is selected. */
void SortFiles(launcher_state *state, const char *const *paths, int count, int replace, sort_result *result);
/* The status line for a result. */
void SortFiles_Report(launcher_state *state, const sort_result *result);

#ifdef __cplusplus
}
#endif
#endif
