#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "doomdef.h"
#include "d_main.h"
#include "m_argv.h"
#include "i_compat.h"
#include "game_profile.h"
#include "wad_probe.h"
#ifdef DOOM_RENDER3D
#include "i_render3d.h"
#endif
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#ifdef __APPLE__
#include <limits.h>
#include <mach-o/dyld.h>
#endif
#ifndef _WIN32
#include <unistd.h>
#endif

static boolean is_absolute(const char *path)
{
    if (path[0] == '/' || path[0] == '\\') return true;
#ifdef _WIN32
    if (SDL_isalpha((unsigned char)path[0]) && path[1] == ':') return true;
#endif
    return false;
}

/* Launchers such as Doom Runner pass paths relative to the folder they start
 * the game in, but the game then runs from its save folder: make them absolute
 * while they still resolve. -file and -deh take every path up to the next option. */
static void absolute_path_args(int argc, char **argv)
{
    static const char *single[] = { "-iwad", "-config", "-record", "-soundfont", "-frametrace" };
    static const char *lists[] = { "-file", "-deh", "-bex" };
    char *folder = SDL_GetCurrentDirectory();
    if (!folder) return;
    for (int i = 1; i < argc; ++i)
    {
        boolean path = false, list = false;
        for (size_t k = 0; k < SDL_arraysize(single); ++k)
            if (!SDL_strcasecmp(argv[i], single[k])) path = true;
        for (size_t k = 0; k < SDL_arraysize(lists); ++k)
            if (!SDL_strcasecmp(argv[i], lists[k])) path = list = true;
        while (path && i + 1 < argc && argv[i + 1][0] != '-')
        {
            char *full;
            ++i;
            if (!is_absolute(argv[i]) && SDL_asprintf(&full, "%s%s", folder, argv[i]) > 0) argv[i] = full;
            path = list;
        }
    }
    SDL_free(folder);
}

/* The folder this program runs from (in the Mac app: Contents/MacOS). */
static char *program_folder(void)
{
#ifdef __APPLE__
    char path[PATH_MAX], real[PATH_MAX];
    uint32_t size = sizeof(path);
    if (_NSGetExecutablePath(path, &size) || !realpath(path, real)) return NULL;
    char *slash = strrchr(real, '/');
    if (!slash) return NULL;
    slash[1] = 0;
    return SDL_strdup(real);
#else
    const char *base = SDL_GetBasePath();
    return base ? SDL_strdup(base) : NULL;
#endif
}

/* The launcher beside this program (platform/launcher, DERP) chooses when no
 * game WAD is given, or one another game's program plays: the arguments go
 * along, so files given are sorted in and options kept. Returns only when
 * it could not be started. */
static void start_launcher(int argc, char **argv)
{
#if defined(_WIN32)
    const char *name = "DERP.exe";
#elif defined(__APPLE__)
    const char *name = "DERP";
#else
    const char *name = "derp";
#endif
    char *folder = program_folder(), *path = NULL;
    SDL_PathInfo info;
    if (!folder || SDL_asprintf(&path, "%s%s", folder, name) < 0) path = NULL;
    SDL_free(folder);
    if (!path || !SDL_GetPathInfo(path, &info) || info.type != SDL_PATHTYPE_FILE) { SDL_free(path); return; }
    char **args = calloc((size_t)argc + 1, sizeof(char *));
    if (!args) { SDL_free(path); return; }
    args[0] = path;
    for (int i = 1; i < argc; ++i) args[i] = argv[i];
#ifdef _WIN32
    SDL_PropertiesID properties = SDL_CreateProperties();
    SDL_SetPointerProperty(properties, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, args);
    SDL_SetBooleanProperty(properties, SDL_PROP_PROCESS_CREATE_BACKGROUND_BOOLEAN, true);
    SDL_Process *process = SDL_CreateProcessWithProperties(properties);
    SDL_DestroyProperties(properties);
    if (process)
    {
        SDL_DestroyProcess(process);
        SDL_Quit();
        exit(0);
    }
#else
    /* The launcher takes this process's place (on the Mac, the app's). */
    SDL_Quit();
    execv(path, args);
    SDL_Init(SDL_INIT_VIDEO);
#endif
    free(args);
    SDL_free(path);
}

int main(int argc, char **argv)
{
    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        fprintf(stderr, "Could not initialize SDL: %s\n", SDL_GetError());
        return 1;
    }
    absolute_path_args(argc, argv);
    const char *wad = NULL;
    for (int i = 1; i + 1 < argc; ++i)
        if (!strcmp(argv[i], "-iwad")) wad = argv[i + 1];
    doom_wad_info wad_info;
    if (!wad || (DOOM_ProbeWad(wad, &wad_info) && wad_info.game != Game_WadKind()))
        start_launcher(argc, argv);
    /* No launcher beside this program: the bundled game, if there is one. */
    char *bundled = NULL;
    if (!wad)
    {
        const char *base = SDL_GetBasePath();
        SDL_PathInfo info;
        if (SDL_asprintf(&bundled, "%sdefault.wad", base ? base : "./") < 0 ||
            !SDL_GetPathInfo(bundled, &info) || info.type != SDL_PATHTYPE_FILE)
        {
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, Game_Title(),
                "The launcher (DERP) is missing beside this program. Start it, or give the game WAD with -iwad.", NULL);
            SDL_Quit(); return 1;
        }
    }
    else if (DOOM_ProbeWad(wad, &wad_info) && wad_info.game != Game_WadKind())
    {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, Game_Title(),
            "This WAD belongs to another game, and the launcher (DERP) that starts it is missing beside this program.", NULL);
        SDL_Quit(); return 1;
    }
    char *preferences = SDL_GetPrefPath("Local Games", Game_Title());
    if (!preferences)
    {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, Game_Title(), "Could not create the game save folder.", NULL);
        SDL_Quit(); return 1;
    }
#ifdef _WIN32
    if (_chdir(preferences))
#else
    if (chdir(preferences))
#endif
    {
        char message[4352];
        snprintf(message, sizeof(message), "Could not open the game save folder:\n%s", preferences);
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, Game_Title(), message, NULL);
        SDL_free(preferences); SDL_Quit(); return 1;
    }
    SDL_free(preferences);
    /* Started by a double-click or the launcher (which sets DERP_LAUNCHER): the output goes to game.log. */
    if (argc == 1 || SDL_getenv("DERP_LAUNCHER"))
    {
        FILE *log = fopen("game.log", "w");
        if (log) fclose(log);
        freopen("game.log", "a", stdout);
        freopen("game.log", "a", stderr);
    }
    FILE *file = fopen(Game_ConfigFile(), "r");
    if (file) fclose(file);
    else
    {
        file = fopen(Game_ConfigFile(), "w");
        if (file)
        {
            fputs(Game_ConfigDefaults(), file);
            fclose(file);
        }
    }
    char **args = calloc((size_t)argc + 6, sizeof(char*));
    if (!args) { SDL_Quit(); return 1; }
    int count = 0;
    for (int i = 0; i < argc; ++i) args[count++] = argv[i];
    if (bundled) { args[count++] = "-iwad"; args[count++] = bundled; }
    int has_config = 0;
    for (int i = 1; i + 1 < argc; ++i)
        if (!strcmp(argv[i], "-config")) has_config = 1;
    if (!has_config) { args[count++] = "-config"; args[count++] = (char *)Game_ConfigFile(); }
    myargc = count;
    myargv = args;
#ifdef DOOM_RENDER3D
    I_Render3DEarlySettings();
#endif
    D_DoomMain();
    return 0;
}
