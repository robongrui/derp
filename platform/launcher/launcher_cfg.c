/* Reading and writing launcher.cfg (launcher_cfg.h):
 *   selected <game WAD path>
 *   folder <path>
 *   pack <psx|3do|extras> <path>
 *   fullscreen <program title> <0|1>
 *   wad <game id> <path>      a game WAD; the lines after it are its own:
 *   file <path> / patch <path> / music <choice> / musicwad <path>
 *   soundfont <path> / extra <options> */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL3/SDL.h>
#include "launcher_cfg.h"
#include "load_list.h"
#include "games.h"

static const char *music_names[MUSIC_CHOICES] = { "own", "psx", "3do", "idkfa", "original", "wad" };
static const char *pack_names[PACK_KINDS] = { "psx", "3do", "extras" };

char *LauncherCfg_Path(void)
{
    char *folder = SDL_GetPrefPath("Local Games", "DERP"), *path = NULL;
    if (folder && SDL_asprintf(&path, "%slauncher.cfg", folder) < 0) path = NULL;
    SDL_free(folder);
    return path;
}

static int game_by_id(const char *id)
{
    for (int g = 0; g < Games_Count(); ++g)
        if (!strcmp(Games_Get(g)->id, id)) return g;
    return -1;
}

/* "word rest": splits off the first word; rest may be empty. */
static char *split(char *line)
{
    char *space = strchr(line, ' ');
    if (!space) return line + strlen(line);
    *space = 0;
    return space + 1;
}

int LauncherCfg_Read(launcher_state *state, const char *path)
{
    size_t size = 0;
    char *text = path ? SDL_LoadFile(path, &size) : NULL;
    if (!text) return 0;
    char selected[LAUNCHER_PATH] = "";
    launcher_wad *wad = NULL;
    for (char *line = text, *next; line && *line; line = next)
    {
        next = strpbrk(line, "\r\n");
        if (next) { while (*next == '\r' || *next == '\n') *next++ = 0; }
        if (!*line || *line == '#') continue;
        char *value = split(line);
        if (!strcmp(line, "selected")) SDL_strlcpy(selected, value, sizeof(selected));
        else if (!strcmp(line, "folder") && *value) Launcher_AddFolder(state, value);
        else if (!strcmp(line, "pack"))
        {
            char *file = split(value);
            for (int k = 0; k < PACK_KINDS; ++k)
                if (!strcmp(value, pack_names[k]) && *file) Launcher_AddPack(state, k, file);
        }
        else if (!strcmp(line, "fullscreen"))
        {
            /* The title may hold spaces: the switch is the last word. */
            char *last = strrchr(value, ' ');
            if (!last) continue;
            *last = 0;
            for (int p = 0; p < Games_ProgramCount() && p < LAUNCHER_PROGRAMS; ++p)
                if (!strcmp(Games_Program(p)->title, value)) state->fullscreen[p] = atoi(last + 1) != 0;
        }
        else if (!strcmp(line, "wad"))
        {
            char *file = split(value);
            int game = game_by_id(value);
            int index = game >= 0 && *file ? Launcher_AddWad(state, file, game) : -1;
            wad = index >= 0 ? &state->wads[index] : NULL;
        }
        else if (!wad) continue;
        else if (!strcmp(line, "file")) Launcher_AddAddon(wad, value, ADDON_WAD);
        else if (!strcmp(line, "patch")) Launcher_AddAddon(wad, value, ADDON_PATCH);
        else if (!strcmp(line, "music"))
        {
            for (int m = 0; m < MUSIC_CHOICES; ++m)
                if (!strcmp(value, music_names[m])) wad->music = m;
        }
        else if (!strcmp(line, "musicwad")) SDL_strlcpy(wad->music_wad, value, sizeof(wad->music_wad));
        else if (!strcmp(line, "soundfont")) SDL_strlcpy(wad->soundfont, value, sizeof(wad->soundfont));
        else if (!strcmp(line, "extra")) SDL_strlcpy(wad->extra, value, sizeof(wad->extra));
    }
    SDL_free(text);
    state->selected = selected[0] ? Launcher_FindWad(state, selected) : -1;
    return 1;
}

int LauncherCfg_Write(const launcher_state *state, const char *path)
{
    char *temporary = NULL;
    if (!path || SDL_asprintf(&temporary, "%s.new", path) < 0) return 0;
    FILE *file = fopen(temporary, "w");
    if (!file) { SDL_free(temporary); return 0; }
    fprintf(file, "# DERP launcher: the game WADs and what to load with each.\n");
    if (state->selected >= 0 && state->selected < state->wad_count)
        fprintf(file, "selected %s\n", state->wads[state->selected].path);
    for (int i = 0; i < state->folder_count; ++i) fprintf(file, "folder %s\n", state->folders[i]);
    for (int i = 0; i < state->pack_count; ++i)
        fprintf(file, "pack %s %s\n", pack_names[state->packs[i].kind], state->packs[i].path);
    for (int p = 0; p < Games_ProgramCount() && p < LAUNCHER_PROGRAMS; ++p)
        fprintf(file, "fullscreen %s %d\n", Games_Program(p)->title, state->fullscreen[p]);
    for (int i = 0; i < state->wad_count; ++i)
    {
        const launcher_wad *wad = &state->wads[i];
        const launcher_game *game = Games_Get(wad->game);
        if (!game) continue;
        fprintf(file, "\nwad %s %s\n", game->id, wad->path);
        for (int k = 0; k < wad->addon_count; ++k)
            fprintf(file, "%s %s\n", wad->addons[k].kind == ADDON_PATCH ? "patch" : "file", wad->addons[k].path);
        if (wad->music) fprintf(file, "music %s\n", music_names[wad->music]);
        if (wad->music_wad[0]) fprintf(file, "musicwad %s\n", wad->music_wad);
        if (wad->soundfont[0]) fprintf(file, "soundfont %s\n", wad->soundfont);
        if (wad->extra[0]) fprintf(file, "extra %s\n", wad->extra);
    }
    int ok = !ferror(file);
    ok = !fclose(file) && ok;
    ok = ok && SDL_RenamePath(temporary, path);
    if (!ok) SDL_RemovePath(temporary);
    SDL_free(temporary);
    return ok;
}
