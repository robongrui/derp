/* The Settings page (settings_page.cpp): F4's own option table
 * (settings_panel.h) drawn with the launcher's widgets, read from and
 * written to the selected game's graphics.cfg. No second list of options:
 * an option added to F4 shows up here with the same words, costs and presets. */
#ifndef DERP_LAUNCHER_SETTINGS_PAGE_H
#define DERP_LAUNCHER_SETTINGS_PAGE_H
#include <SDL3/SDL.h>
#include "launcher.h"
#include "launcher_widgets.h"
#ifdef __cplusplus
extern "C" {
#endif

/* Shows that game program's settings (games.h), reading its graphics.cfg. */
void SettingsPage_Select(int program);
void SettingsPage_Draw(ui_state *ui, launcher_state *state, SDL_FRect area);
/* Writes graphics.cfg if anything changed; 1 when it is as shown. */
int SettingsPage_Save(void);
/* A game program's save folder, where its graphics.cfg is (from SDL_malloc). */
char *SettingsPage_Folder(int program);
/* Sets one graphics.cfg key in a game's file, the rest of it kept. */
int SettingsPage_SetValue(int program, const char *key, int value);
/* Whether a game program has an F4 option (its key) at all: one its game lacks (Heretic's
   window glass, soundtrack) is greyed on the page and never copied to or from it. */
int SettingsPage_GameHas(int program, const char *key);
/* The whole tree (1) or Display only (0), from the top. */
void SettingsPage_ShowAll(int all);
/* Set: no game's files are written or deleted (the launcher's hidden test pictures). */
extern int SettingsPage_ReadOnly;

#ifdef __cplusplus
}
#endif
#endif
