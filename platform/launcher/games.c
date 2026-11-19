/* The games table (games.h). Rows for games DERP cannot play yet are here
 * from the start, so their WADs are named properly and greyed out with the
 * reason instead of being taken for something else. */
#include <stdio.h>
#include <string.h>
#include <SDL3/SDL.h>
#include "games.h"
#include "launcher.h"
#include "../wad_probe.h"

/* Game_HasEffect per program (platform/games/<game>/profile.c): DOOM's
   says yes to all, Heretic's to none of GAME_EFFECT_WINDOW_GLASS through
   GAME_EFFECT_RANDOM_PITCH. */
static const launcher_program programs[] = {
#if defined(_WIN32)
    { "DOOM", "Play DERP.exe", ~0u },
    { "Heretic", "derp-heretic.exe", 0 },
#elif defined(__APPLE__)
    { "DOOM", "Play DERP", ~0u },
    { "Heretic", "derp-heretic", 0 },
#else
    { "DOOM", "play-derp", ~0u },
    { "Heretic", "derp-heretic", 0 },
#endif
};

enum { DOOM_PROGRAM, HERETIC_PROGRAM };
enum { ROW_DOOM, ROW_DOOM2, ROW_TNT, ROW_PLUTONIA, ROW_FREEDOOM1, ROW_FREEDOOM2, ROW_HERETIC,
       ROW_HEXEN, ROW_DOOM64, ROW_CHEX, ROW_OTHER_EPISODES, ROW_OTHER_NUMBERED };

/* The recorded packs (music_pack.c) stand in for DOOM's D_ songs: the
   PlayStation pack for every DOOM game, a 3DO pack per game (TNT's and
   Plutonia's own, else by map style, as music_pack.c picks it), the 2024
   re-release's extras.wad for any WAD's stock songs. Heretic's songs are
   MUS_ lumps, which no pack has. */
#define DOOM_PACKS (GAME_PACK(PACK_PSX) | GAME_PACK(PACK_3DO) | GAME_PACK(PACK_EXTRAS))

static const launcher_game games[] = {
    [ROW_DOOM] = { .id = "doom", .title = "DOOM", .program = DOOM_PROGRAM, .wad_kind = DOOM_WAD_DOOM,
                   .files = { "doom.wad", "doomu.wad", "doom1.wad", "bfgdoom.wad" },
                   .packs = DOOM_PACKS, .pack_3do = "UltimateDoom_3DO.wad", .music = "D_", .folder = "doom" },
    [ROW_DOOM2] = { .id = "doom2", .title = "DOOM II", .program = DOOM_PROGRAM, .wad_kind = DOOM_WAD_DOOM,
                    .files = { "doom2.wad", "doom2f.wad", "bfgdoom2.wad" },
                    .packs = DOOM_PACKS, .pack_3do = "Doom2_3DO.wad", .music = "D_", .folder = "doom" },
    [ROW_TNT] = { .id = "tnt", .title = "Final DOOM: TNT", .family = "Final DOOM", .program = DOOM_PROGRAM,
                  .wad_kind = DOOM_WAD_DOOM, .files = { "tnt.wad" },
                  .packs = DOOM_PACKS, .pack_3do = "TNT_3DO.wad", .music = "D_", .folder = "doom" },
    [ROW_PLUTONIA] = { .id = "plutonia", .title = "Final DOOM: Plutonia", .family = "Final DOOM", .program = DOOM_PROGRAM,
                       .wad_kind = DOOM_WAD_DOOM, .files = { "plutonia.wad" },
                       .packs = DOOM_PACKS, .pack_3do = "Plutonia_3DO.wad", .music = "D_", .folder = "doom" },
    [ROW_FREEDOOM1] = { .id = "freedoom1", .title = "Freedoom: Phase 1", .family = "Freedoom", .program = DOOM_PROGRAM,
                        .wad_kind = DOOM_WAD_DOOM, .files = { "freedoom1.wad" },
                        .packs = DOOM_PACKS, .pack_3do = "UltimateDoom_3DO.wad", .music = "D_", .folder = "freedoom" },
    [ROW_FREEDOOM2] = { .id = "freedoom2", .title = "Freedoom: Phase 2", .family = "Freedoom", .program = DOOM_PROGRAM,
                        .wad_kind = DOOM_WAD_DOOM, .files = { "freedoom2.wad", "freedm.wad" },
                        .packs = DOOM_PACKS, .pack_3do = "Doom2_3DO.wad", .music = "D_", .folder = "freedoom" },
    [ROW_HERETIC] = { .id = "heretic", .title = "Heretic", .program = HERETIC_PROGRAM, .wad_kind = DOOM_WAD_HERETIC,
                      .files = { "heretic.wad", "heretic1.wad" }, .music = "MUS_", .folder = "heretic" },
    [ROW_HEXEN] = { .id = "hexen", .title = "Hexen", .program = -1, .wad_kind = DOOM_WAD_HEXEN,
                    .files = { "hexen.wad" }, .reason = "Hexen cannot be played yet.", .folder = "hexen" },
    [ROW_DOOM64] = { .id = "doom64", .title = "DOOM 64", .program = -1, .wad_kind = -1,
                     .files = { "doom64.wad" }, .reason = "DOOM 64 cannot be played yet.", .folder = "doom" },
    [ROW_CHEX] = { .id = "chex", .title = "Chex Quest", .program = -1, .wad_kind = DOOM_WAD_DOOM,
                   .files = { "chex.wad", "chex3.wad" }, .reason = "Chex Quest needs its own game rules, not written yet.",
                   .folder = "chex" },
    /* Game WADs of no game above, by their maps: DOOM's program plays them. */
    [ROW_OTHER_EPISODES] = { .id = "other", .title = "Other game", .program = DOOM_PROGRAM, .wad_kind = DOOM_WAD_DOOM,
                             .packs = DOOM_PACKS, .pack_3do = "UltimateDoom_3DO.wad", .music = "D_" },
    [ROW_OTHER_NUMBERED] = { .id = "other2", .title = "Other game", .program = DOOM_PROGRAM, .wad_kind = DOOM_WAD_DOOM,
                             .packs = DOOM_PACKS, .pack_3do = "Doom2_3DO.wad", .music = "D_" },
};

int Games_Count(void) { return (int)SDL_arraysize(games); }
const launcher_game *Games_Get(int game) { return game >= 0 && game < Games_Count() ? &games[game] : NULL; }
int Games_ProgramCount(void) { return (int)SDL_arraysize(programs); }
const launcher_program *Games_Program(int program)
{
    return program >= 0 && program < Games_ProgramCount() ? &programs[program] : NULL;
}

char *Games_ProgramPath(int program)
{
    const launcher_program *row = Games_Program(program);
    char *folder = Launcher_ProgramFolder(), *path = NULL;
    if (!row || !folder) { SDL_free(folder); return NULL; }
    if (SDL_asprintf(&path, "%s%s", folder, row->program) < 0) path = NULL;
#ifdef __APPLE__
    /* In the build folder: inside Play DERP.app beside the launcher. */
    if (path && !Launcher_FileExists(path))
    {
        SDL_free(path);
        if (SDL_asprintf(&path, "%sPlay DERP.app/Contents/MacOS/%s", folder, row->program) < 0) path = NULL;
    }
#endif
    SDL_free(folder);
    if (path && !Launcher_FileExists(path)) { SDL_free(path); path = NULL; }
    return path;
}

static uint32_t little32(const unsigned char *p)
{
    return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

/* Whether the WAD's directory holds a lump of that name. */
static int has_lump(const char *path, const char *name)
{
    unsigned char header[12], entry[16];
    FILE *file = fopen(path, "rb");
    int found = 0;
    if (!file) return 0;
    if (fread(header, 1, 12, file) == 12 && !fseek(file, (long)little32(header + 8), SEEK_SET))
    {
        uint32_t count = little32(header + 4);
        size_t length = strlen(name);
        for (uint32_t i = 0; i < count && i < 100000 && !found; ++i)
        {
            if (fread(entry, 1, 16, file) != 16) break;
            found = !SDL_strncasecmp((const char *)entry + 8, name, length) && (length == 8 || !entry[8 + length]);
        }
    }
    fclose(file);
    return found;
}

int Games_Identify(const char *path)
{
    doom_wad_info info;
    if (!DOOM_ProbeWad(path, &info) || !info.is_iwad) return -1;
    const char *name = Launcher_FileName(path);
    for (int g = 0; g < Games_Count(); ++g)
        for (int f = 0; f < 6 && games[g].files[f]; ++f)
            if (!SDL_strcasecmp(name, games[g].files[f])) return g;
    if (!info.has_palette) return ROW_DOOM64;
    if (info.game == DOOM_WAD_HEXEN) return ROW_HEXEN;
    if (info.game == DOOM_WAD_HERETIC) return ROW_HERETIC;
    if (!info.episode_maps && !info.numbered_maps) return -1;
    if (has_lump(path, "FREEDOOM")) return info.numbered_maps ? ROW_FREEDOOM2 : ROW_FREEDOOM1;
    return info.numbered_maps ? ROW_OTHER_NUMBERED : ROW_OTHER_EPISODES;
}

/* music_pack.c's file names for the recorded packs. */
int Games_PackKind(const char *path)
{
    const char *name = Launcher_FileName(path);
    if (!SDL_strcasecmp(name, "HQPSXMUS.WAD")) return PACK_PSX;
    if (!SDL_strcasecmp(name, "extras.wad")) return PACK_EXTRAS;
    for (int g = 0; g < Games_Count(); ++g)
        if (games[g].pack_3do && !SDL_strcasecmp(name, games[g].pack_3do)) return PACK_3DO;
    return -1;
}

int Games_TakesPack(int game, const char *path)
{
    const launcher_game *row = Games_Get(game);
    int kind = Games_PackKind(path);
    if (!row || row->reason || kind < 0 || !(row->packs & GAME_PACK(kind))) return 0;
    return kind != PACK_3DO || (row->pack_3do && !SDL_strcasecmp(Launcher_FileName(path), row->pack_3do));
}

int Games_MusicKind(const char *path)
{
    unsigned char header[12], entry[16];
    FILE *file = fopen(path, "rb");
    if (!file) return -1;
    int kind = -1, count = 0;
    if (fread(header, 1, 12, file) == 12 && !memcmp(header, "PWAD", 4) && !fseek(file, (long)little32(header + 8), SEEK_SET))
    {
        uint32_t lumps = little32(header + 4);
        for (uint32_t i = 0; i < lumps && i < 4096; ++i, ++count)
        {
            if (fread(entry, 1, 16, file) != 16) { kind = -2; break; }
            /* Every lump a song, all named the way one game names them. */
            int this_kind = -2;
            for (int g = 0; g < Games_Count() && this_kind == -2; ++g)
                if (games[g].music && !SDL_strncasecmp((const char *)entry + 8, games[g].music, strlen(games[g].music)))
                    this_kind = games[g].wad_kind;
            if (this_kind == -2 || (kind >= 0 && kind != this_kind)) { kind = -2; break; }
            kind = this_kind;
        }
    }
    fclose(file);
    return count && kind >= 0 ? kind : -1;
}

const char *Games_UnplayedMusic(const char *path)
{
    static const char *files[] = { "heretic_mus_orig.wad", "heretic_mus_remix.wad", "hexen_mus_orig.wad", "hexen_mus_remix.wad" };
    for (size_t i = 0; i < SDL_arraysize(files); ++i)
        if (!SDL_strcasecmp(Launcher_FileName(path), files[i]))
            return "the re-release's recorded soundtrack, which DERP cannot play yet.";
    return NULL;
}

unsigned Games_ProgramPacks(int program)
{
    unsigned packs = 0;
    for (int g = 0; g < Games_Count(); ++g)
        if (games[g].program == program && !games[g].reason) packs |= games[g].packs;
    return packs;
}

void Games_ProgramTitles(int program, char *text, int size)
{
    const char *titles[16];
    int count = 0;
    for (int g = 0; g < Games_Count() && count < 16; ++g)
    {
        if (games[g].program != program || games[g].reason || !strncmp(games[g].id, "other", 5)) continue;
        const char *title = games[g].family ? games[g].family : games[g].title;
        int seen = 0;
        for (int k = 0; k < count; ++k) seen |= !strcmp(titles[k], title);
        if (!seen) titles[count++] = title;
    }
    text[0] = 0;
    for (int k = 0; k < count; ++k)
    {
        if (k) SDL_strlcat(text, k + 1 == count ? " and " : ", ", (size_t)size);
        SDL_strlcat(text, titles[k], (size_t)size);
    }
}

const char *Games_KindTitle(int wad_kind)
{
    for (int g = 0; g < Games_Count(); ++g)
        if (games[g].wad_kind == wad_kind && !games[g].family) return games[g].title;
    return "another game";
}
