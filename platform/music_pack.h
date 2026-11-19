/* Recorded soundtrack packs beside the game WADs; see music_pack.c. */
#ifndef DOOM_MUSIC_PACK_H
#define DOOM_MUSIC_PACK_H

/* PC: the WAD's own scores, no pack. IDKFA and ORIGINAL come from the
   re-release's extras.wad. */
enum { SOUNDTRACK_PC, SOUNDTRACK_PSX, SOUNDTRACK_3DO, SOUNDTRACK_IDKFA, SOUNDTRACK_ORIGINAL, SOUNDTRACK_COUNT };

/* Whether that soundtrack's pack is there (looked for on the first call). */
int MusicPack_Found(int soundtrack);
/* The recording standing in for a score, from the score's lump name and
   bytes (from_game_wad: the lump is the game WAD's own, not an add-on's):
   a copy from malloc of *size bytes, to play at *gain, or NULL when the
   soundtrack has none for it. */
void *MusicPack_Read(int soundtrack, const char *name, int from_game_wad,
                     const void *score, int score_size, int *size, float *gain);
#endif
