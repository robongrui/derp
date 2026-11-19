/* launcher.cfg in the launcher's pref folder (launcher_cfg.c): plain text,
 * one "key value" a line. The search folders, the game WADs known, the one
 * selected, the music packs, and per game WAD its load list, music,
 * SoundFont and extra options. Files are never copied, only their paths kept. */
#ifndef DERP_LAUNCHER_CFG_H
#define DERP_LAUNCHER_CFG_H
#include "launcher.h"
#ifdef __cplusplus
extern "C" {
#endif

/* The file's path (from SDL_malloc): Local Games/DERP/launcher.cfg. */
char *LauncherCfg_Path(void);
int LauncherCfg_Read(launcher_state *state, const char *path);
int LauncherCfg_Write(const launcher_state *state, const char *path);

#ifdef __cplusplus
}
#endif
#endif
