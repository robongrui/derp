/* The 2024 re-release's track list (TRAKINFO lumps in extras.wad): which
   recording stands in for a score, found by the SHA-1 of the score's lump,
   and how loud a recording plays, found by the SHA-1 of its own lump. */
#ifndef DOOM_KEX_TRACKS_H
#define DOOM_KEX_TRACKS_H
#include <stddef.h>

void Sha1(const void *data, size_t size, unsigned char digest[20]);
/* Adds one TRAKINFO lump's entries (JSON with // comments). */
void KexTracks_Add(const char *text, size_t size);
/* The recording's lump name (8 characters, NUL-padded) for a score:
   remixed picks Andrew Hulshult's remix, else the recorded original. */
const char *KexTracks_Find(const unsigned char score[20], int remixed);
/* The gain a recording plays at (1 without an entry). */
float KexTracks_Volume(const unsigned char recording[20]);
#endif
