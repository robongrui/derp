/* DERP, the launcher (PLAN-launcher.md): finds the game WADs, shows the
 * window, then starts the right game program with ordinary arguments and
 * ends. It links no engine, so it opens where the game can't and says why.
 *
 * Files given as arguments (dropped on the program's icon or a shortcut,
 * or Linux's %F) are sorted into their slots and the window opens with Play
 * focused: it never starts the game without the window. Other arguments
 * (-warp 1 3) become this time's extra options. -dryrun writes the game's
 * command line to launcher.log instead of starting it, and changes no
 * game's settings; -screenshot <file.png>
 * draws the window hidden into a picture and ends (-screenshotsettings: the
 * Settings page; -screenshotinput "<steps>" plays input first, see
 * launcher_ui.c; no game's settings are written meanwhile); -autoplay presses Play at once in a hidden window;
 * -launchercfg <file> keeps another launcher.cfg. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include "launcher.h"
#include "launcher_cfg.h"
#include "launcher_ui.h"
#include "find_wads.h"
#include "sort_files.h"
#include "game_args.h"
#include "games.h"
#include "settings_page.h"
#ifndef _WIN32
#include <unistd.h>
#endif

static int is_absolute(const char *path)
{
    if (path[0] == '/' || path[0] == '\\') return 1;
#ifdef _WIN32
    if (SDL_isalpha((unsigned char)path[0]) && path[1] == ':') return 1;
#endif
    return 0;
}

/* The option names whose values are files: the files are sorted, the names dropped. */
static int file_option(const char *arg)
{
    static const char *names[] = { "-iwad", "-file", "-deh", "-bex", "-soundfont", "-psxmusic", "-3domusic", "-extrasmusic" };
    for (size_t i = 0; i < SDL_arraysize(names); ++i)
        if (!SDL_strcasecmp(arg, names[i])) return 1;
    return 0;
}

static void log_line(const char *line)
{
    char *folder = SDL_GetPrefPath("Local Games", "DERP"), *path = NULL;
    if (folder && SDL_asprintf(&path, "%slauncher.log", folder) > 0)
    {
        FILE *file = fopen(path, "w");
        if (file) { fprintf(file, "%s\n", line); fclose(file); }
    }
    SDL_free(path);
    SDL_free(folder);
}

/* Starts the game program; returns only when that failed. */
static int start(game_args *args)
{
    /* The game sends its output to game.log in its save folder, as when double-clicked. */
    SDL_SetEnvironmentVariable(SDL_GetEnvironment(), "DERP_LAUNCHER", "1", true);
    SDL_setenv_unsafe("DERP_LAUNCHER", "1", 1);
#ifdef _WIN32
    SDL_PropertiesID properties = SDL_CreateProperties();
    SDL_SetPointerProperty(properties, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, args->argv);
    SDL_SetBooleanProperty(properties, SDL_PROP_PROCESS_CREATE_BACKGROUND_BOOLEAN, true);
    SDL_Process *process = SDL_CreateProcessWithProperties(properties);
    SDL_DestroyProperties(properties);
    if (!process) return 0;
    SDL_DestroyProcess(process);
    return 1;
#else
    /* The game takes this process's place (on the Mac, the app's). */
    SDL_Quit();
    execv(args->argv[0], args->argv);
    return 0;
#endif
}

int main(int argc, char **argv)
{
    SDL_SetAppMetadata("DERP", NULL, "io.github.robongrui.derp");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD))
    {
        fprintf(stderr, "Could not initialize SDL: %s\n", SDL_GetError());
        return 1;
    }
    launcher_state state;
    memset(&state, 0, sizeof(state));
    state.selected = -1;
    char *cfg = NULL;
    for (int i = 1; i + 1 < argc; ++i)
        if (!strcmp(argv[i], "-launchercfg")) cfg = SDL_strdup(argv[i + 1]);
    if (!cfg) cfg = LauncherCfg_Path();
    LauncherCfg_Read(&state, cfg);
    FindWads(&state);
    /* First start: the first playable game in the table's order (DOOM before the bundled game). */
    if (state.selected < 0)
        for (int i = 0; i < state.wad_count; ++i)
        {
            const launcher_game *game = Games_Get(state.wads[i].game);
            if (!game || game->reason || state.wads[i].missing || state.wads[i].shareware) continue;
            if (state.selected < 0 || state.wads[i].game < state.wads[state.selected].game) state.selected = i;
        }

    /* The files and options given. */
    char **files = SDL_calloc((size_t)argc + 1, sizeof(char *));
    char extra[LAUNCHER_EXTRA] = "";
    int file_count = 0, dry_run = 0;
    char *here = SDL_GetCurrentDirectory();
    for (int i = 1; i < argc && files; ++i)
    {
        if (!strcmp(argv[i], "-dryrun")) { dry_run = 1; continue; }
        if (!strcmp(argv[i], "-launchercfg") && i + 1 < argc) { ++i; continue; }
        if (!strcmp(argv[i], "-screenshot") && i + 1 < argc) { LauncherUI_Screenshot = argv[++i]; continue; }
        if (!strcmp(argv[i], "-screenshotsettings")) { LauncherUI_ScreenshotPage = PAGE_SETTINGS; continue; }
        if (!strcmp(argv[i], "-screenshotinput") && i + 1 < argc) { LauncherUI_ScreenshotInput = argv[++i]; continue; }
        if (!strcmp(argv[i], "-autoplay")) { LauncherUI_AutoPlay = 1; continue; }
        if (file_option(argv[i])) continue;
        char *path = NULL;
        if (!is_absolute(argv[i]) && here) SDL_asprintf(&path, "%s%s", here, argv[i]);
        else path = SDL_strdup(argv[i]);
        if (argv[i][0] != '-' && path && (Launcher_FileExists(path) || Launcher_IsFolder(path))) files[file_count++] = path;
        else
        {
            SDL_free(path);
            if (extra[0]) SDL_strlcat(extra, " ", sizeof(extra));
            int space = strchr(argv[i], ' ') != NULL;
            if (space) SDL_strlcat(extra, "\"", sizeof(extra));
            SDL_strlcat(extra, argv[i], sizeof(extra));
            if (space) SDL_strlcat(extra, "\"", sizeof(extra));
        }
    }
    SDL_free(here);
    if (file_count)
    {
        sort_result result;
        SortFiles(&state, (const char * const *)files, file_count, 1, &result);
        SortFiles_Report(&state, &result);
    }
    for (int i = 0; i < file_count; ++i) SDL_free(files[i]);
    SDL_free(files);
    if (extra[0]) SDL_strlcpy(Launcher_Current(&state)->extra, extra, LAUNCHER_EXTRA);
    if (!state.status[0])
        Launcher_Status(&state, 0, state.wad_count ? "Found %d game WAD%s. Enter plays." : "Drop a game WAD on this window to start.",
                        state.wad_count, state.wad_count == 1 ? "" : "s");

    int status = 0;
    for (;;)
    {
        if (!LauncherUI_Run(&state, file_count > 0)) break;
        game_args args;
        if (!GameArgs_Build(&state, &args)) break;
        launcher_wad *wad = Launcher_Current(&state);
        /* The launcher's music pick is where F4's Soundtrack starts; F4 changes it after. */
        if (wad->music_changed && !dry_run && Games_ProgramPacks(args.program))
            SettingsPage_SetValue(args.program, "soundtrack", args.soundtrack);
        wad->music_changed = 0;
        LauncherCfg_Write(&state, cfg);
        char *line = GameArgs_Line(&args);
        log_line(line ? line : "");
        if (dry_run)
        {
            SDL_free(line);
            GameArgs_Free(&args);
            break;
        }
        SDL_free(line);
        if (start(&args)) { GameArgs_Free(&args); break; }
        if (LauncherUI_AutoPlay) { GameArgs_Free(&args); status = 1; break; }
        Launcher_Status(&state, 1, "Could not start %s: %s", Launcher_FileName(args.argv[0]), SDL_GetError());
        GameArgs_Free(&args);
        status = 1;
    }
    LauncherCfg_Write(&state, cfg);
    SDL_free(cfg);
    Launcher_Free(&state);
    SDL_Quit();
    return status;
}
