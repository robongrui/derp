/* Where game WADs are looked for (find_wads.h). Only game WADs and the
 * recorded music packs are taken from these folders; custom WADs are the
 * player's to add. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL3/SDL.h>
#include "find_wads.h"
#include "games.h"
#include "../wad_probe.h"
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#define MOST_ENTRIES 4000 /* per folder searched, so a huge folder can't hold up the start */

typedef struct { launcher_state *state; int depth; int entries; int packs_only; } search_t;

static void search(search_t *search_state, const char *folder);

static int pack_kind(const char *name) { return Games_PackKind(name); }

static SDL_EnumerationResult SDLCALL visit(void *data, const char *folder, const char *name)
{
    search_t *s = data;
    char *path = NULL;
    if (++s->entries > MOST_ENTRIES) return SDL_ENUM_SUCCESS;
    if (SDL_asprintf(&path, "%s%s", folder, name) < 0) return SDL_ENUM_CONTINUE;
    if (Launcher_HasExtension(name, "wad"))
    {
        int pack = pack_kind(name);
        if (pack >= 0) Launcher_AddPack(s->state, pack, path);
        else if (!s->packs_only)
        {
            int game = Games_Identify(path);
            if (game >= 0)
            {
                int index = Launcher_AddWad(s->state, path, game);
                if (index >= 0) s->state->wads[index].found = 1;
            }
        }
    }
    else if (s->depth > 0 && Launcher_IsFolder(path))
    {
        search_t inner = *s;
        --inner.depth;
        inner.entries = 0;
        search(&inner, path);
        s->entries += inner.entries / 4;
    }
    SDL_free(path);
    return SDL_ENUM_CONTINUE;
}

static void search(search_t *s, const char *folder)
{
    char *with_slash = NULL;
    size_t length = strlen(folder);
    if (!length) return;
    int slash = folder[length - 1] == '/' || folder[length - 1] == '\\';
    if (SDL_asprintf(&with_slash, "%s%s", folder, slash ? "" : "/") < 0) return;
    SDL_EnumerateDirectory(with_slash, visit, s);
    SDL_free(with_slash);
}

void FindWads_In(launcher_state *state, const char *folder, int depth)
{
    search_t s = { state, depth, 0, 0 };
    if (Launcher_IsFolder(folder)) search(&s, folder);
}

/* The packs beside a known game WAD, or one folder down. */
static void packs_beside(launcher_state *state, const char *wad)
{
    char folder[LAUNCHER_PATH];
    SDL_strlcpy(folder, wad, sizeof(folder));
    char *name = (char *)Launcher_FileName(folder);
    *name = 0;
    search_t s = { state, 1, 0, 1 };
    if (folder[0] && Launcher_IsFolder(folder)) search(&s, folder);
}

/* A list of folders in an environment variable, ; or : apart. */
static void search_list(launcher_state *state, const char *list)
{
#ifdef _WIN32
    const char separator = ';';
#else
    const char separator = ':';
#endif
    if (!list) return;
    char *copy = SDL_strdup(list), *start = copy;
    if (!copy) return;
    for (char *p = copy; ; ++p)
    {
        if (*p && *p != separator) continue;
        int end = !*p;
        *p = 0;
        if (*start) FindWads_In(state, start, 1);
        if (end) break;
        start = p + 1;
    }
    SDL_free(copy);
}

/* Whether a game's install folder could hold one of the games in the table (their folder words). */
static int likely_game_folder(const char *name)
{
    for (int g = 0; g < Games_Count(); ++g)
    {
        const char *word = Games_Get(g)->folder;
        if (word && SDL_strcasestr(name, word)) return 1;
    }
    return 0;
}

static SDL_EnumerationResult SDLCALL visit_games(void *data, const char *folder, const char *name)
{
    launcher_state *state = data;
    if (!likely_game_folder(name)) return SDL_ENUM_CONTINUE;
    char *path = NULL;
    if (SDL_asprintf(&path, "%s%s", folder, name) > 0 && Launcher_IsFolder(path)) FindWads_In(state, path, 2);
    SDL_free(path);
    return SDL_ENUM_CONTINUE;
}

/* A folder of installed games (Steam's common, GOG's): the ones named like ours. */
static void search_games_folder(launcher_state *state, const char *folder)
{
    char *with_slash = NULL;
    if (!Launcher_IsFolder(folder) || SDL_asprintf(&with_slash, "%s/", folder) < 0) return;
    SDL_EnumerateDirectory(with_slash, visit_games, state);
    SDL_free(with_slash);
}

/* A Steam folder: its own library and the ones libraryfolders.vdf lists. */
static void search_steam(launcher_state *state, const char *steam)
{
    char path[LAUNCHER_PATH];
    snprintf(path, sizeof(path), "%s/steamapps/common", steam);
    search_games_folder(state, path);
    snprintf(path, sizeof(path), "%s/steamapps/libraryfolders.vdf", steam);
    size_t size = 0;
    char *text = SDL_LoadFile(path, &size);
    if (!text) return;
    for (char *p = text; (p = strstr(p, "\"path\"")); )
    {
        p += 6;
        char *open = strchr(p, '"');
        if (!open) break;
        char library[LAUNCHER_PATH];
        size_t length = 0;
        for (p = open + 1; *p && *p != '"' && length + 1 < sizeof(library); ++p)
        {
            if (*p == '\\' && p[1] == '\\') ++p; /* "D:\\SteamLibrary" */
            library[length++] = *p;
        }
        library[length] = 0;
        if (!Launcher_SamePath(library, steam))
        {
            snprintf(path, sizeof(path), "%s/steamapps/common", library);
            search_games_folder(state, path);
        }
    }
    SDL_free(text);
}

#ifdef _WIN32
static int registry_string(HKEY root, const char *key, const char *value, char *out, DWORD size)
{
    return RegGetValueA(root, key, value, RRF_RT_REG_SZ, NULL, out, &size) == ERROR_SUCCESS;
}

/* GOG Galaxy and the GOG installers list each game under GOG.com\Games. */
static void search_gog_registry(launcher_state *state)
{
    HKEY games;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\WOW6432Node\\GOG.com\\Games", 0, KEY_READ, &games) != ERROR_SUCCESS &&
        RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\GOG.com\\Games", 0, KEY_READ, &games) != ERROR_SUCCESS) return;
    char id[256];
    for (DWORD i = 0; ; ++i)
    {
        DWORD length = sizeof(id);
        if (RegEnumKeyExA(games, i, id, &length, NULL, NULL, NULL, NULL) != ERROR_SUCCESS) break;
        char name[512] = "", path[LAUNCHER_PATH] = "";
        registry_string(games, id, "gameName", name, sizeof(name));
        if (registry_string(games, id, "path", path, sizeof(path)) &&
            (likely_game_folder(name) || likely_game_folder(Launcher_FileName(path))))
            FindWads_In(state, path, 2);
    }
    RegCloseKey(games);
}
#endif

static void search_stores(launcher_state *state)
{
#ifdef _WIN32
    char steam[LAUNCHER_PATH];
    if (registry_string(HKEY_CURRENT_USER, "Software\\Valve\\Steam", "SteamPath", steam, sizeof(steam))) search_steam(state, steam);
    else search_steam(state, "C:/Program Files (x86)/Steam");
    search_gog_registry(state);
    search_games_folder(state, "C:/GOG Games");
    search_games_folder(state, "C:/Program Files (x86)/GOG Galaxy/Games");
    search_games_folder(state, "C:/Program Files/GOG Galaxy/Games");
#else
    const char *home = SDL_GetUserFolder(SDL_FOLDER_HOME);
    char path[LAUNCHER_PATH];
    if (!home) return;
#ifdef __APPLE__
    snprintf(path, sizeof(path), "%sLibrary/Application Support/Steam", home);
    search_steam(state, path);
#else
    static const char *steams[] = { ".steam/steam", ".local/share/Steam", ".var/app/com.valvesoftware.Steam/.local/share/Steam" };
    for (size_t i = 0; i < SDL_arraysize(steams); ++i)
    {
        snprintf(path, sizeof(path), "%s%s", home, steams[i]);
        char *real = Launcher_IsFolder(path) ? realpath(path, NULL) : NULL;
        int seen = 0;
        /* ~/.steam/steam is usually a link to ~/.local/share/Steam. */
        if (real && i == 1)
        {
            char first[LAUNCHER_PATH];
            snprintf(first, sizeof(first), "%s%s", home, steams[0]);
            char *other = realpath(first, NULL);
            seen = other && !strcmp(other, real);
            free(other);
        }
        if (real && !seen) search_steam(state, real);
        free(real);
    }
    /* GOG's Linux installers put games in ~/GOG Games. */
    snprintf(path, sizeof(path), "%sGOG Games", home);
    search_games_folder(state, path);
#endif
#endif
}

void FindWads(launcher_state *state)
{
    /* 1. The folders of the game WADs remembered from earlier: their packs. */
    int remembered = state->wad_count;
    for (int i = 0; i < remembered; ++i)
    {
        launcher_wad *wad = &state->wads[i];
        wad->missing = !Launcher_FileExists(wad->path);
        if (!wad->missing) packs_beside(state, wad->path);
    }
    /* 2. The wads folder beside the program, the bundled game. */
    const char *base = SDL_GetBasePath();
    char path[LAUNCHER_PATH];
    snprintf(path, sizeof(path), "%swads", base ? base : "./");
    FindWads_In(state, path, 1);
    snprintf(path, sizeof(path), "%sdefault.wad", base ? base : "./");
    if (Launcher_FileExists(path))
    {
        int game = Games_Identify(path);
        int index = game >= 0 ? Launcher_AddWad(state, path, game) : -1;
        if (index >= 0) state->wads[index].found = 1;
    }
    /* 3. The folders other ports look in. */
    const char *dir = SDL_getenv("DOOMWADDIR");
    if (dir) FindWads_In(state, dir, 1);
    search_list(state, SDL_getenv("DOOMWADPATH"));
    /* 4. Steam and GOG. */
    search_stores(state);
    /* 5. Linux's game data folders, one per game's folder word (doom, heretic...). */
#if !defined(_WIN32) && !defined(__APPLE__)
    const char *home = SDL_GetUserFolder(SDL_FOLDER_HOME);
    for (int g = 0; g < Games_Count(); ++g)
    {
        const char *word = Games_Get(g)->folder;
        int seen = 0;
        for (int k = 0; k < g; ++k) seen |= Games_Get(k)->folder && word && !strcmp(Games_Get(k)->folder, word);
        if (!word || seen) continue;
        static const char *roots[] = { "/usr/share/games/", "/usr/local/share/games/", "/usr/share/" };
        for (size_t r = 0; r < SDL_arraysize(roots); ++r)
        {
            snprintf(path, sizeof(path), "%s%s", roots[r], word);
            FindWads_In(state, path, 1);
        }
        if (home)
        {
            snprintf(path, sizeof(path), "%s.local/share/games/%s", home, word);
            FindWads_In(state, path, 1);
        }
    }
#endif
    /* 6. The folders the player added. */
    for (int i = 0; i < state->folder_count; ++i) FindWads_In(state, state->folders[i], 2);
    /* Packs beside the game WADs found now. */
    for (int i = remembered; i < state->wad_count; ++i) packs_beside(state, state->wads[i].path);
}
