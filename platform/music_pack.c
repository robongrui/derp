/* Console soundtrack packs: WADs of MP3 lumps under the PC songs' names.
 * - PlayStation: HQPSXMUS.WAD, one pack for DOOM and DOOM II.
 * - 3DO: one pack per game, UltimateDoom_3DO.wad, Doom2_3DO.wad,
 *   TNT_3DO.wad and Plutonia_3DO.wad; the one for the game WAD in play.
 * - The 2024 re-release's extras.wad: Andrew Hulshult's IDKFA remix and
 *   recordings of the original scores, as Ogg Vorbis lumps listed in its
 *   TRAKINFO by the hash of each score (kex_tracks.c), so they stand in for
 *   any WAD's copy of a stock song, Sigil's and TNT's own included.
 * Each is looked for once, the first time music asks: given with
 * -psxmusic / -3domusic / -extrasmusic <file>, else beside the game WAD (-iwad) or an
 * add-on (-file), else in the wads folder beside the program, in each of
 * those folders or one folder down (a pack still in the folder it was
 * unpacked into). Only a pack's directory is kept; a song's lump is read
 * from the file when it starts. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "doomdef.h"
#include "m_argv.h"
#include "w_wad.h"
#include "music_pack.h"
#include "kex_tracks.h"
#include <SDL3/SDL.h>

typedef struct { char name[8]; uint32_t offset, size; } pack_lump_t;
typedef struct
{
    const char *title, *option;
    char path[4096];
    pack_lump_t *lumps;
    int count, state;	/* state: 0 not looked for, 1 found, -1 none */
} pack_t;

enum { PACK_PSX, PACK_3DO, PACK_EXTRAS, PACK_COUNT };
static pack_t packs[PACK_COUNT] = {
    [PACK_PSX] = { "PlayStation", "-psxmusic" },
    [PACK_3DO] = { "3DO", "-3domusic" },
    [PACK_EXTRAS] = { "Re-release", "-extrasmusic" },
};

/* The pack a soundtrack plays from, or -1 for the PC scores. */
static int pack_of(int soundtrack)
{
    switch (soundtrack)
    {
    case SOUNDTRACK_PSX: return PACK_PSX;
    case SOUNDTRACK_3DO: return PACK_3DO;
    case SOUNDTRACK_IDKFA: case SOUNDTRACK_ORIGINAL: return PACK_EXTRAS;
    default: return -1;
    }
}

static uint32_t little32(const unsigned char *p)
{
    return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

/* The game WAD's file name, without its folder. */
static const char *game_wad(void)
{
    int arg = M_CheckParm("-iwad");
    if (!arg || arg + 1 >= myargc) return "";
    const char *name = myargv[arg + 1];
    for (const char *p = name; *p; ++p)
        if (*p == '/' || *p == '\\') name = p + 1;
    return name;
}

/* The pack's file name for the game in play. */
static const char *pack_name(int kind)
{
    if (kind == PACK_PSX) return "HQPSXMUS.WAD";
    if (kind == PACK_EXTRAS) return "extras.wad";
    const char *wad = game_wad();
    if (!SDL_strcasecmp(wad, "tnt.wad")) return "TNT_3DO.wad";
    if (!SDL_strcasecmp(wad, "plutonia.wad")) return "Plutonia_3DO.wad";
    return W_CheckNumForName("MAP01") >= 0 ? "Doom2_3DO.wad" : "UltimateDoom_3DO.wad";
}

/* Reads a pack's directory; its lumps are read when a song starts. */
static int read_pack(pack_t *pack, const char *path)
{
    unsigned char header[12];
    FILE *file = fopen(path, "rb");
    if (!file) return 0;
    int ok = fread(header, 1, 12, file) == 12 && !memcmp(header + 1, "WAD", 3);
    uint32_t count = ok ? little32(header + 4) : 0, offset = ok ? little32(header + 8) : 0;
    ok = ok && count && count < 100000 && !fseek(file, (long)offset, SEEK_SET);
    pack_lump_t *lumps = ok ? malloc(count * sizeof(*lumps)) : NULL;
    for (uint32_t i = 0; lumps && i < count; ++i)
    {
        unsigned char entry[16];
        if (fread(entry, 1, 16, file) != 16) { free(lumps); lumps = NULL; break; }
        lumps[i].offset = little32(entry);
        lumps[i].size = little32(entry + 4);
        memcpy(lumps[i].name, entry + 8, 8);
    }
    fclose(file);
    if (!lumps) return 0;
    pack->lumps = lumps;
    pack->count = (int)count;
    snprintf(pack->path, sizeof(pack->path), "%s", path);
    return 1;
}

/* A copy of a lump of the pack (from malloc). */
static void *read_lump(const pack_t *pack, const pack_lump_t *lump)
{
    FILE *file = fopen(pack->path, "rb");
    void *bytes = file && lump->size ? malloc(lump->size) : NULL;
    if (bytes && (fseek(file, (long)lump->offset, SEEK_SET) ||
                  fread(bytes, 1, lump->size, file) != lump->size))
    { free(bytes); bytes = NULL; }
    if (file) fclose(file);
    return bytes;
}

static const pack_lump_t *find_lump(const pack_t *pack, const char *name)
{
    for (int i = 0; i < pack->count; ++i)
        if (!SDL_strncasecmp(pack->lumps[i].name, name, 8)) return &pack->lumps[i];
    return NULL;
}

/* extras.wad lists its tracks; without the list it holds none we can use. */
static int read_track_list(pack_t *pack)
{
    int lists = 0;
    for (int i = 0; i < pack->count; ++i)
    {
        if (SDL_strncasecmp(pack->lumps[i].name, "TRAKINFO", 8)) continue;
        char *text = read_lump(pack, &pack->lumps[i]);
        if (!text) continue;
        KexTracks_Add(text, pack->lumps[i].size);
        free(text);
        ++lists;
    }
    return lists > 0;
}

/* The pack in a folder (or the folder a file is in) or one below it, under any case. */
static int look_beside(pack_t *pack, const char *name, const char *path, int is_folder)
{
    char folder[4096], pattern[64];
    snprintf(folder, sizeof(folder), "%s", path);
    if (!is_folder)
    {
        char *cut = NULL;
        for (char *p = folder; *p; ++p)
            if (*p == '/' || *p == '\\') cut = p;
        if (cut) *cut = 0;
        else snprintf(folder, sizeof(folder), ".");
    }
    int found = 0;
    for (int depth = 0; depth < 2 && !found; ++depth)
    {
        int count = 0;
        snprintf(pattern, sizeof(pattern), "%s%s", depth ? "*/" : "", name);
        char **names = SDL_GlobDirectory(folder, pattern, SDL_GLOB_CASEINSENSITIVE, &count);
        if (names && count)
        {
            char file[4096];
            snprintf(file, sizeof(file), "%s/%s", folder, names[0]);
            found = read_pack(pack, file);
        }
        SDL_free(names);
    }
    return found;
}

static pack_t *pack_ready(int kind)
{
    if (kind < 0) return NULL;
    pack_t *pack = &packs[kind];
    if (pack->state) return pack->state > 0 ? pack : NULL;
    pack->state = -1;
    const char *name = pack_name(kind);
    int arg = M_CheckParm((char *)pack->option);
    int found = arg && arg + 1 < myargc && read_pack(pack, myargv[arg + 1]);
    /* Beside the game WAD and the add-ons. */
    for (int i = 1; !found && i + 1 < myargc; ++i)
        if (!SDL_strcasecmp(myargv[i], "-iwad")) found = look_beside(pack, name, myargv[i + 1], 0);
    arg = M_CheckParm("-file");
    for (int i = arg + 1; arg && !found && i < myargc && myargv[i][0] != '-'; ++i)
        found = look_beside(pack, name, myargv[i], 0);
    if (!found)
    {
        const char *base = SDL_GetBasePath();
        char folder[4096];
        snprintf(folder, sizeof(folder), "%swads", base ? base : "./");
        found = look_beside(pack, name, folder, 1);
    }
    if (found && kind == PACK_EXTRAS) found = read_track_list(pack);
    if (!found) return NULL;
    pack->state = 1;
    printf("%s music: %s\n", pack->title, pack->path);
    return pack;
}

void *MusicPack_Read(int soundtrack, const char *name, int from_game_wad,
                     const void *score, int score_size, int *size, float *gain)
{
    int kind = pack_of(soundtrack);
    pack_t *pack = pack_ready(kind);
    if (!pack) return NULL;
    const pack_lump_t *lump = NULL;
    *gain = 1;
    if (kind == PACK_EXTRAS)
    {
        unsigned char digest[20];
        Sha1(score, (size_t)score_size, digest);
        const char *track = KexTracks_Find(digest, soundtrack == SOUNDTRACK_IDKFA);
        lump = track ? find_lump(pack, track) : NULL;
    }
    /* The consoles' songs go by name: only in place of the game WAD's own. */
    else if (from_game_wad) lump = find_lump(pack, name);
    void *bytes = lump ? read_lump(pack, lump) : NULL;
    if (!bytes) return NULL;
    *size = (int)lump->size;
    if (kind == PACK_EXTRAS)
    {
        unsigned char digest[20];
        Sha1(bytes, lump->size, digest);
        *gain = KexTracks_Volume(digest);
    }
    return bytes;
}

int MusicPack_Found(int soundtrack) { return pack_ready(pack_of(soundtrack)) != NULL; }
