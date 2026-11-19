/* The selected game WAD as a game program's arguments (game_args.c):
 *   <program> -iwad <game WAD> -file <custom WADs> <music WAD> -deh <patches>
 *   [-psxmusic | -3domusic | -extrasmusic <pack>] [-soundfont <bank>]
 *   [-fullscreen] <extra options>
 * Missing custom files are left out (the check before Play asked). */
#ifndef DERP_LAUNCHER_GAME_ARGS_H
#define DERP_LAUNCHER_GAME_ARGS_H
#include "launcher.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char **argv;  /* argc entries and a NULL; argv[0] the program's path */
    int argc;
    int program;  /* games.h program index */
    int soundtrack; /* the F4 Soundtrack value the music choice comes to */
} game_args;

/* Whether the selected game WAD can be turned into arguments (a program, a game WAD). */
int GameArgs_Build(const launcher_state *state, game_args *args);
void GameArgs_Free(game_args *args);
/* Splits typed options at spaces, keeping "quoted parts" whole; adds them to args. */
void GameArgs_AddTyped(game_args *args, const char *text);
/* The arguments as one line, for the log and the tests (from SDL_malloc). */
char *GameArgs_Line(const game_args *args);

#ifdef __cplusplus
}
#endif
#endif
