/* Dropped or added files into their slots (sort_files.h):
 *   game WAD of a known game     -> games list, selected
 *   PWAD with maps or graphics   -> custom WADs, end of the list
 *   .deh / .bex                  -> custom WADs (sent as -deh)
 *   PWAD beside a same-named .deh-> both
 *   PSX / 3DO / re-release pack  -> music, selected, where the game takes it (games.c);
 *                                   kept for the games that do otherwise
 *   WAD of music lumps alone     -> music (sent last in -file, so it wins), when they are
 *                                   the selected game's songs; refused otherwise
 *   .sf2                         -> SoundFont
 *   folder                       -> every file in it, by the same rules
 *   .pk3, .zip, anything else    -> refused, one line saying why */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL3/SDL.h>
#include "sort_files.h"
#include "load_list.h"
#include "games.h"
#include "../wad_probe.h"

#define FOLDER_DEPTH 2
#define FOLDER_FILES 512

typedef struct { char **paths; int count, capacity; } path_list;

static void push(path_list *list, const char *path)
{
    if (list->count == list->capacity)
    {
        int capacity = list->capacity ? list->capacity * 2 : 32;
        char **paths = SDL_realloc(list->paths, (size_t)capacity * sizeof(*paths));
        if (!paths) return;
        list->paths = paths;
        list->capacity = capacity;
    }
    list->paths[list->count++] = SDL_strdup(path);
}

static int compare_paths(const void *a, const void *b)
{
    return SDL_strcasecmp(*(char * const *)a, *(char * const *)b);
}

typedef struct { path_list *files; int depth; const char *folder; } walk_t;

static SDL_EnumerationResult SDLCALL walk(void *data, const char *folder, const char *name);

static void walk_folder(path_list *files, const char *folder, int depth)
{
    walk_t walker = { files, depth, folder };
    SDL_EnumerateDirectory(folder, walk, &walker);
}

static SDL_EnumerationResult SDLCALL walk(void *data, const char *folder, const char *name)
{
    walk_t *walker = data;
    char *path = NULL;
    if (walker->files->count >= FOLDER_FILES) return SDL_ENUM_SUCCESS;
    if (SDL_asprintf(&path, "%s%s", folder, name) < 0) return SDL_ENUM_CONTINUE;
    if (Launcher_IsFolder(path))
    {
        if (walker->depth + 1 < FOLDER_DEPTH)
        {
            char *inner = NULL;
            if (SDL_asprintf(&inner, "%s/", path) > 0) walk_folder(walker->files, inner, walker->depth + 1);
            SDL_free(inner);
        }
    }
    /* In a folder only what the launcher takes counts; the rest is not refused one by one. */
    else if (Launcher_HasExtension(path, "wad") || Launcher_HasExtension(path, "deh") ||
             Launcher_HasExtension(path, "bex") || Launcher_HasExtension(path, "sf2"))
        push(walker->files, path);
    SDL_free(path);
    return SDL_ENUM_CONTINUE;
}

/* The paths with folders opened, folders' files in name order. */
static void expand(path_list *files, const char *const *paths, int count)
{
    for (int i = 0; i < count; ++i)
    {
        if (!Launcher_IsFolder(paths[i])) { push(files, paths[i]); continue; }
        path_list inner = { 0 };
        size_t length = strlen(paths[i]);
        char *folder = NULL;
        int slash = length && (paths[i][length - 1] == '/' || paths[i][length - 1] == '\\');
        if (SDL_asprintf(&folder, "%s%s", paths[i], slash ? "" : "/") < 0) continue;
        walk_folder(&inner, folder, 0);
        SDL_free(folder);
        if (inner.count) qsort(inner.paths, (size_t)inner.count, sizeof(*inner.paths), compare_paths);
        for (int k = 0; k < inner.count; ++k) { push(files, inner.paths[k]); SDL_free(inner.paths[k]); }
        SDL_free(inner.paths);
    }
}

static void refuse(sort_result *result, const char *path, const char *why)
{
    snprintf(result->refusal, sizeof(result->refusal), "%s: %s", Launcher_FileName(path), why);
    ++result->refused;
}


/* Many releases ship their DEHACKED patch beside the WAD: map.wad + map.deh. */
static void add_companion(launcher_wad *wad, const char *path)
{
    static const char *extensions[] = { ".deh", ".DEH", ".bex", ".BEX", ".Deh" };
    char patch[LAUNCHER_PATH];
    const char *name = Launcher_FileName(path), *dot = strrchr(name, '.');
    size_t length = dot ? (size_t)(dot - path) : strlen(path);
    if (length + 5 > sizeof(patch)) return;
    for (size_t i = 0; i < SDL_arraysize(extensions); ++i)
    {
        memcpy(patch, path, length);
        strcpy(patch + length, extensions[i]);
        if (Launcher_FileExists(patch)) { Launcher_AddAddon(wad, patch, ADDON_PATCH); return; }
    }
}

/* Icon drops without a game WAD: the selected one if it fits the maps, else one that does. */
static void pick_base(launcher_state *state, const path_list *files)
{
    int numbered = 0, episodes = 0, kind = -1;
    for (int i = 0; i < files->count; ++i)
    {
        doom_wad_info info;
        if (DOOM_ProbeWad(files->paths[i], &info) && !info.is_iwad && !info.music_only && (info.numbered_maps || info.episode_maps))
        {
            numbered |= info.numbered_maps; episodes |= info.episode_maps;
            if (kind < 0) kind = info.game;
        }
    }
    if (!numbered && !episodes) return;
    /* Most add-ons carry nothing that tells their game (a Heretic one is taken for DOOM's), so
       the selected game keeps them when its levels fit, unless they are plainly another game's. */
    const launcher_wad *current = state->selected >= 0 ? &state->wads[state->selected] : NULL;
    const launcher_game *row = current ? Games_Get(current->game) : NULL;
    if (current && row && !current->shareware && (numbered ? current->numbered_maps : current->episode_maps) &&
        (kind == DOOM_WAD_DOOM || kind == row->wad_kind)) return;
    int other = LoadList_FittingWad(state, numbered, kind);
    if (other >= 0) state->selected = other;
}

void SortFiles(launcher_state *state, const char *const *paths, int count, int replace, sort_result *result)
{
    path_list files = { 0 };
    memset(result, 0, sizeof(*result));
    expand(&files, paths, count);
    /* Game WADs first, so the files that come with them join their list. */
    int had_selection = state->selected >= 0, game_dropped = 0;
    for (int i = 0; i < files.count; ++i)
    {
        int game = Games_Identify(files.paths[i]);
        if (game < 0) continue;
        int index = Launcher_AddWad(state, files.paths[i], game);
        if (index < 0) continue;
        state->wads[index].found = 1;
        state->selected = index;
        game_dropped = 1;
        ++result->game_wads;
    }
    if (replace && !game_dropped) pick_base(state, &files);
    if (!had_selection) LoadList_TakeLoose(state);
    launcher_wad *wad = Launcher_Current(state);
    int cleared = 0;
    for (int i = 0; i < files.count; ++i)
    {
        const char *path = files.paths[i];
        doom_wad_info info;
        if (Launcher_HasExtension(path, "pk3") || Launcher_HasExtension(path, "pk7") || Launcher_HasExtension(path, "ipk3") ||
            Launcher_HasExtension(path, "zip") || Launcher_HasExtension(path, "7z"))
        { refuse(result, path, "zipped packages cannot be loaded yet; unzip the .wad inside it."); continue; }
        if (Launcher_HasExtension(path, "sf2"))
        {
            SDL_strlcpy(wad->soundfont, path, sizeof(wad->soundfont));
            Launcher_CleanPath(wad->soundfont);
            ++result->music;
            continue;
        }
        int patch = Launcher_HasExtension(path, "deh") || Launcher_HasExtension(path, "bex");
        if (!patch && !DOOM_ProbeWad(path, &info))
        {
            refuse(result, path, Launcher_HasExtension(path, "wad") ? "this WAD is damaged or unsupported."
                   : "not a WAD, a DEHACKED patch (.deh, .bex) or a SoundFont (.sf2).");
            continue;
        }
        if (!patch && info.is_iwad)
        {
            if (Games_Identify(path) < 0) refuse(result, path, "a game WAD of no game the launcher knows.");
            continue;
        }
        const char *unplayed = patch ? NULL : Games_UnplayedMusic(path);
        if (unplayed) { refuse(result, path, unplayed); continue; }
        int pack = patch ? -1 : Games_PackKind(path);
        int songs = patch || pack >= 0 ? -1 : Games_MusicKind(path);
        if (!patch && (pack >= 0 || info.music_only || songs >= 0))
        {
            const launcher_game *row = state->selected >= 0 ? Games_Get(wad->game) : NULL;
            if (pack >= 0)
            {
                /* Kept for the games that take it; the selected one plays it if it is one of them. */
                Launcher_AddPack(state, pack, path);
                if (!row || Games_TakesPack(wad->game, path))
                {
                    wad->music = pack == PACK_PSX ? MUSIC_PSX : pack == PACK_3DO ? MUSIC_3DO : MUSIC_IDKFA;
                    wad->music_changed = 1;
                }
                else snprintf(result->note, sizeof(result->note), "%s is recorded music for other games than %s; kept for those.",
                              Launcher_FileName(path), row->title);
            }
            else
            {
                /* D_ songs alone (wad_probe.c) are DOOM's; Games_MusicKind knows the rest by their names. */
                if (songs < 0) songs = DOOM_WAD_DOOM;
                if (row && (row->reason || row->wad_kind != songs))
                {
                    char why[160];
                    snprintf(why, sizeof(why), "songs for %s, not %s.", Games_KindTitle(songs), row->title);
                    refuse(result, path, why);
                    continue;
                }
                SDL_strlcpy(wad->music_wad, path, sizeof(wad->music_wad));
                Launcher_CleanPath(wad->music_wad);
                wad->music = MUSIC_WAD;
                wad->music_changed = 1;
            }
            ++result->music;
            continue;
        }
        if (replace && !cleared) { Launcher_ClearAddons(wad); cleared = 1; }
        int before = wad->addon_count;
        Launcher_AddAddon(wad, path, patch ? ADDON_PATCH : ADDON_WAD);
        if (!patch) add_companion(wad, path);
        result->addons += wad->addon_count - before;
    }
    for (int i = 0; i < files.count; ++i) SDL_free(files.paths[i]);
    SDL_free(files.paths);
}

void SortFiles_Report(launcher_state *state, const sort_result *result)
{
    int taken = result->game_wads + result->addons + result->music;
    if (result->refused && !taken) { Launcher_Status(state, 1, "%s", result->refusal); return; }
    if (result->refused)
    {
        Launcher_Status(state, 1, "Added %d file%s. %s%s", taken, taken == 1 ? "" : "s", result->refusal,
                        result->refused > 1 ? " (and more refused)" : "");
        return;
    }
    if (!taken) { Launcher_Status(state, 0, "Nothing there to load."); return; }
    if (result->note[0]) { Launcher_Status(state, 0, "%s", result->note); return; }
    Launcher_Status(state, 0, "Added %d file%s.", taken, taken == 1 ? "" : "s");
}
