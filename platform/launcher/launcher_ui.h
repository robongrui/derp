/* The launcher's window (launcher_ui.c): one SDL_Renderer window with two
 * pages, Play (play_page.c) and Settings (settings_page.cpp), the games
 * list on the left of both. Mouse, keyboard and gamepad all work it. */
#ifndef DERP_LAUNCHER_UI_H
#define DERP_LAUNCHER_UI_H
#include "launcher.h"
#include "launcher_widgets.h"
#include "load_list.h"
#ifdef __cplusplus
extern "C" {
#endif

enum { PAGE_PLAY, PAGE_SETTINGS };
/* What a native file dialog was opened for. */
enum { DIALOG_ADD_FILES, DIALOG_ADD_GAME, DIALOG_CHANGE_GAME, DIALOG_SOUNDFONT, DIALOG_GAME_FOLDER };

typedef struct {
    launcher_state *state;
    ui_state ui;
    int page;
    int play;            /* Play was pressed and the checks passed */
    int quit;
    int focus_play;      /* the Play button takes the focus when next drawn */
    int notice;          /* a check before Play waits for an answer */
    int focus_notice;
    launcher_check check;
    ui_list games_list, addons_list;
} launcher_window;

/* Runs the window until Play (returns 1, the checks passed) or closing (0).
   play_focused: the files came on the icon, so Enter starts the game. */
int LauncherUI_Run(launcher_state *state, int play_focused);

/* Set before the run: draw into this PNG hidden and end, showing this page. */
extern const char *LauncherUI_Screenshot;
extern int LauncherUI_ScreenshotPage;
/* Input played before the picture is taken (launcher_ui.c play_script). */
extern const char *LauncherUI_ScreenshotInput;
/* Set before the run: the window hidden, Play pressed at once (a test of the whole way to the game). */
extern int LauncherUI_AutoPlay;

/* play_page.c */
void PlayPage_DrawGames(launcher_window *window, SDL_FRect area);
void PlayPage_Draw(launcher_window *window, SDL_FRect area);
/* Play pressed: the checks, then either play or a notice. */
void PlayPage_Play(launcher_window *window);
/* launcher_ui.c: opens a native dialog; its answer comes back as an event. */
void LauncherUI_Dialog(launcher_window *window, int purpose);

#ifdef __cplusplus
}
#endif
#endif
