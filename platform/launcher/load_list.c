/* The launcher's records (launcher.h) and the checks before Play (load_list.h). */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL3/SDL.h>
#include "launcher.h"
#include "load_list.h"
#include "games.h"
#include "../wad_probe.h"

launcher_wad *Launcher_Current(launcher_state *state)
{
    return state->selected >= 0 && state->selected < state->wad_count ? &state->wads[state->selected] : &state->loose;
}

int Launcher_FindWad(const launcher_state *state, const char *path)
{
    for (int i = 0; i < state->wad_count; ++i)
        if (Launcher_SamePath(state->wads[i].path, path)) return i;
    return -1;
}

int Launcher_AddWad(launcher_state *state, const char *path, int game)
{
    int index = Launcher_FindWad(state, path);
    if (index >= 0) return index;
    if (state->wad_count == state->wad_capacity)
    {
        int capacity = state->wad_capacity ? state->wad_capacity * 2 : 16;
        launcher_wad *wads = SDL_realloc(state->wads, (size_t)capacity * sizeof(*wads));
        if (!wads) return -1;
        state->wads = wads;
        state->wad_capacity = capacity;
    }
    launcher_wad *wad = &state->wads[state->wad_count];
    memset(wad, 0, sizeof(*wad));
    SDL_strlcpy(wad->path, path, sizeof(wad->path));
    Launcher_CleanPath(wad->path);
    wad->game = game;
    doom_wad_info info;
    if (DOOM_ProbeWad(path, &info))
    {
        wad->numbered_maps = info.numbered_maps;
        wad->episode_maps = info.episode_maps;
        wad->shareware = info.episode_maps && !info.full_game && !info.numbered_maps;
    }
    else wad->missing = 1;
    return state->wad_count++;
}

void LoadList_Probe(launcher_addon *addon)
{
    doom_wad_info info;
    addon->missing = !Launcher_FileExists(addon->path);
    addon->numbered_maps = addon->episode_maps = 0;
    addon->game = DOOM_WAD_DOOM;
    if (addon->kind == ADDON_WAD && DOOM_ProbeWad(addon->path, &info))
    {
        addon->numbered_maps = info.numbered_maps;
        addon->episode_maps = info.episode_maps;
        addon->game = info.game;
    }
}

void Launcher_AddAddon(launcher_wad *wad, const char *path, int kind)
{
    for (int i = 0; i < wad->addon_count; ++i)
        if (Launcher_SamePath(wad->addons[i].path, path)) return;
    if (wad->addon_count == wad->addon_capacity)
    {
        int capacity = wad->addon_capacity ? wad->addon_capacity * 2 : 8;
        launcher_addon *addons = SDL_realloc(wad->addons, (size_t)capacity * sizeof(*addons));
        if (!addons) return;
        wad->addons = addons;
        wad->addon_capacity = capacity;
    }
    launcher_addon *addon = &wad->addons[wad->addon_count++];
    memset(addon, 0, sizeof(*addon));
    SDL_strlcpy(addon->path, path, sizeof(addon->path));
    Launcher_CleanPath(addon->path);
    addon->kind = kind;
    LoadList_Probe(addon);
}

void Launcher_RemoveAddon(launcher_wad *wad, int index)
{
    if (index < 0 || index >= wad->addon_count) return;
    memmove(&wad->addons[index], &wad->addons[index + 1], (size_t)(wad->addon_count - index - 1) * sizeof(*wad->addons));
    --wad->addon_count;
}

void Launcher_MoveAddon(launcher_wad *wad, int index, int direction)
{
    int other = index + direction;
    if (index < 0 || index >= wad->addon_count || other < 0 || other >= wad->addon_count) return;
    launcher_addon swap = wad->addons[index];
    wad->addons[index] = wad->addons[other];
    wad->addons[other] = swap;
}

void Launcher_ClearAddons(launcher_wad *wad) { wad->addon_count = 0; }

void Launcher_AddPack(launcher_state *state, int kind, const char *path)
{
    for (int i = 0; i < state->pack_count; ++i)
        if (Launcher_SamePath(state->packs[i].path, path)) return;
    if (state->pack_count == state->pack_capacity)
    {
        int capacity = state->pack_capacity ? state->pack_capacity * 2 : 8;
        launcher_pack *packs = SDL_realloc(state->packs, (size_t)capacity * sizeof(*packs));
        if (!packs) return;
        state->packs = packs;
        state->pack_capacity = capacity;
    }
    state->packs[state->pack_count].kind = kind;
    SDL_strlcpy(state->packs[state->pack_count].path, path, LAUNCHER_PATH);
    Launcher_CleanPath(state->packs[state->pack_count].path);
    ++state->pack_count;
}

const char *Launcher_PackFor(const launcher_state *state, int kind, const launcher_wad *wad)
{
    /* Only the packs the game's row takes (games.c): none for Heretic, its own 3DO pack for each DOOM game. */
    if (!wad) return NULL;
    for (int i = 0; i < state->pack_count; ++i)
    {
        const launcher_pack *pack = &state->packs[i];
        if (pack->kind != kind || !Games_TakesPack(wad->game, pack->path) || !Launcher_FileExists(pack->path)) continue;
        return pack->path;
    }
    return NULL;
}

void Launcher_AddFolder(launcher_state *state, const char *path)
{
    for (int i = 0; i < state->folder_count; ++i)
        if (Launcher_SamePath(state->folders[i], path)) return;
    char **folders = SDL_realloc(state->folders, (size_t)(state->folder_count + 1) * sizeof(*folders));
    if (!folders) return;
    state->folders = folders;
    char *copy = SDL_strdup(path);
    if (copy) Launcher_CleanPath(copy);
    state->folders[state->folder_count++] = copy;
}

void Launcher_Status(launcher_state *state, int error, const char *format, ...)
{
    va_list list;
    va_start(list, format);
    SDL_vsnprintf(state->status, sizeof(state->status), format, list);
    va_end(list);
    state->status_error = error;
}

void Launcher_Free(launcher_state *state)
{
    for (int i = 0; i < state->wad_count; ++i) SDL_free(state->wads[i].addons);
    SDL_free(state->wads);
    SDL_free(state->loose.addons);
    SDL_free(state->packs);
    for (int i = 0; i < state->folder_count; ++i) SDL_free(state->folders[i]);
    SDL_free(state->folders);
    memset(state, 0, sizeof(*state));
    state->selected = -1;
}

/* An add-on made for another game than the base's program plays. */
static int other_game(const launcher_wad *wad, const launcher_addon *addon)
{
    const launcher_game *game = Games_Get(wad->game);
    return addon->kind == ADDON_WAD && !addon->missing && game && addon->game != DOOM_WAD_DOOM && addon->game != game->wad_kind;
}

/* Levels named the other way than the base's: 1 the add-on has MAP01 ones, 2 E1M1 ones; 0 fits. */
static int wrong_maps(const launcher_wad *wad, const launcher_addon *addon)
{
    if (addon->kind != ADDON_WAD || addon->missing) return 0;
    if (addon->numbered_maps && !wad->numbered_maps) return 1;
    if (addon->episode_maps && !wad->episode_maps) return 2;
    return 0;
}

const char *LoadList_Note(const launcher_state *state, const launcher_wad *wad, int index)
{
    static char note[64];
    const launcher_addon *addon = &wad->addons[index];
    (void)state;
    if (addon->missing) return "missing";
    if (other_game(wad, addon)) { snprintf(note, sizeof(note), "for %s", Games_KindTitle(addon->game)); return note; }
    switch (wrong_maps(wad, addon))
    {
    case 1: return "MAP01 levels";
    case 2: return "E1M1 levels";
    }
    if (addon->kind == ADDON_PATCH) return "patch";
    return "";
}

/* A listed, playable game WAD of that kind with that map style; the games
   table's order says which first (DOOM II before Final DOOM and Freedoom). */
int LoadList_FittingWad(const launcher_state *state, int numbered, int kind)
{
    int best = -1;
    for (int i = 0; i < state->wad_count; ++i)
    {
        const launcher_wad *wad = &state->wads[i];
        const launcher_game *game = Games_Get(wad->game);
        if (wad->missing || wad->shareware || !game || game->reason || game->wad_kind != kind) continue;
        if (numbered ? !wad->numbered_maps : !wad->episode_maps) continue;
        if (best < 0 || wad->game < state->wads[best].game) best = i;
    }
    return best;
}

void LoadList_Check(const launcher_state *state, launcher_check *check)
{
    memset(check, 0, sizeof(*check));
    check->other_wad = -1;
    if (state->selected < 0 || state->selected >= state->wad_count)
    {
        check->problem = CHECK_NO_GAME;
        snprintf(check->text, sizeof(check->text), "Choose a game WAD first: drop one here or press Add game WAD.");
        return;
    }
    const launcher_wad *wad = &state->wads[state->selected];
    const launcher_game *game = Games_Get(wad->game);
    if (wad->missing || !Launcher_FileExists(wad->path))
    {
        check->problem = CHECK_GAME_MISSING;
        snprintf(check->text, sizeof(check->text), "%s is not there any more. Press Change to find it.", Launcher_FileName(wad->path));
        return;
    }
    if (!game || game->reason || game->program < 0)
    {
        check->problem = CHECK_NOT_PLAYABLE;
        snprintf(check->text, sizeof(check->text), "%s", game && game->reason ? game->reason : "This game cannot be played yet.");
        return;
    }
    int missing = 0, wads = 0;
    for (int i = 0; i < wad->addon_count; ++i)
    {
        const launcher_addon *addon = &wad->addons[i];
        if (addon->missing) { ++missing; continue; }
        ++wads;
        if (other_game(wad, addon))
        {
            check->problem = CHECK_OTHER_GAME;
            snprintf(check->text, sizeof(check->text), "%s is made for %s, not %s. Remove it to play.",
                     Launcher_FileName(addon->path), Games_KindTitle(addon->game), game->title);
            return;
        }
        int maps = wrong_maps(wad, addon);
        if (maps)
        {
            check->problem = CHECK_WRONG_MAPS;
            /* Named by the game the add-on is for and the base selected, never a fixed game. */
            check->other_wad = LoadList_FittingWad(state, maps == 1, addon->game);
            snprintf(check->text, sizeof(check->text), "%s has %s levels, %s has %s ones%s.", Launcher_FileName(addon->path),
                     maps == 1 ? "MAP01" : "E1M1", game->title, maps == 1 ? "E1M1" : "MAP01",
                     check->other_wad >= 0 ? "" : "; no game WAD listed has its kind");
            return;
        }
    }
    if (wads && wad->shareware)
    {
        check->problem = CHECK_SHAREWARE;
        snprintf(check->text, sizeof(check->text), "The shareware %s cannot load custom WADs. Use the full game instead.", game->title);
        return;
    }
    char *program = Games_ProgramPath(game->program);
    if (!program)
    {
        check->problem = CHECK_NO_PROGRAM;
        snprintf(check->text, sizeof(check->text), "The program for this game (%s) is missing beside the launcher.",
                 Games_Program(game->program)->program);
        return;
    }
    SDL_free(program);
    if (missing)
    {
        check->problem = CHECK_MISSING;
        snprintf(check->text, sizeof(check->text), missing == 1 ? "%d custom file is missing. Play without it?"
                 : "%d custom files are missing. Play without them?", missing);
    }
}

void LoadList_MoveTo(launcher_state *state, int index)
{
    if (index < 0 || index >= state->wad_count || index == state->selected) return;
    launcher_wad *from = Launcher_Current(state), *to = &state->wads[index];
    Launcher_ClearAddons(to);
    for (int i = 0; i < from->addon_count; ++i) Launcher_AddAddon(to, from->addons[i].path, from->addons[i].kind);
    to->music = from->music;
    SDL_strlcpy(to->music_wad, from->music_wad, sizeof(to->music_wad));
    SDL_strlcpy(to->soundfont, from->soundfont, sizeof(to->soundfont));
    SDL_strlcpy(to->extra, from->extra, sizeof(to->extra));
    state->selected = index;
}

void LoadList_TakeLoose(launcher_state *state)
{
    launcher_wad *wad = Launcher_Current(state);
    if (wad == &state->loose) return;
    for (int i = 0; i < state->loose.addon_count; ++i)
        Launcher_AddAddon(wad, state->loose.addons[i].path, state->loose.addons[i].kind);
    /* The music comes along only where this game takes it: its own packs, its own songs. */
    int fits = state->loose.music == MUSIC_WAD
        ? Games_Get(wad->game) && Games_MusicKind(state->loose.music_wad) == Games_Get(wad->game)->wad_kind
        : state->loose.music == MUSIC_PSX ? Launcher_PackFor(state, PACK_PSX, wad) != NULL
        : state->loose.music == MUSIC_3DO ? Launcher_PackFor(state, PACK_3DO, wad) != NULL
        : Launcher_PackFor(state, PACK_EXTRAS, wad) != NULL;
    if (state->loose.music && fits)
    {
        wad->music = state->loose.music;
        wad->music_changed = 1;
        SDL_strlcpy(wad->music_wad, state->loose.music_wad, sizeof(wad->music_wad));
    }
    if (state->loose.soundfont[0]) SDL_strlcpy(wad->soundfont, state->loose.soundfont, sizeof(wad->soundfont));
    Launcher_ClearAddons(&state->loose);
    state->loose.music = 0;
    state->loose.music_wad[0] = state->loose.soundfont[0] = 0;
}
