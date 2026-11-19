/* The re-release's TRAKINFO track list, and the SHA-1 its keys are made with
   (FIPS 180-4). The list is JSON: an object of 40-digit hex keys, each an
   object of string pairs ("MIDI", "Remixed", "volume"), with // comments. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "kex_tracks.h"

static uint32_t rotate(uint32_t x, int n) { return x << n | x >> (32 - n); }

static void sha1_block(uint32_t h[5], const unsigned char *p)
{
    uint32_t w[80];
    for (int i = 0; i < 16; ++i)
        w[i] = (uint32_t)p[i * 4] << 24 | (uint32_t)p[i * 4 + 1] << 16 | (uint32_t)p[i * 4 + 2] << 8 | p[i * 4 + 3];
    for (int i = 16; i < 80; ++i) w[i] = rotate(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
    for (int i = 0; i < 80; ++i)
    {
        uint32_t f, k;
        if (i < 20) { f = (b & c) | (~b & d); k = 0x5a827999; }
        else if (i < 40) { f = b ^ c ^ d; k = 0x6ed9eba1; }
        else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8f1bbcdc; }
        else { f = b ^ c ^ d; k = 0xca62c1d6; }
        uint32_t t = rotate(a, 5) + f + e + k + w[i];
        e = d; d = c; c = rotate(b, 30); b = a; a = t;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
}

void Sha1(const void *data, size_t size, unsigned char digest[20])
{
    uint32_t h[5] = { 0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476, 0xc3d2e1f0 };
    const unsigned char *p = data;
    size_t left = size;
    for (; left >= 64; left -= 64, p += 64) sha1_block(h, p);
    unsigned char last[128] = { 0 };
    memcpy(last, p, left);
    last[left] = 0x80;
    size_t blocks = left < 56 ? 1 : 2;
    uint64_t bits = (uint64_t)size * 8;
    for (int i = 0; i < 8; ++i) last[blocks * 64 - 1 - i] = (unsigned char)(bits >> (i * 8));
    for (size_t i = 0; i < blocks; ++i) sha1_block(h, last + i * 64);
    for (int i = 0; i < 20; ++i) digest[i] = (unsigned char)(h[i / 4] >> (24 - i % 4 * 8));
}

typedef struct
{
    unsigned char key[20];
    char original[9], remix[9];
    float volume;
} track_t;

static track_t *tracks;
static int track_count, track_room;

static int hex(int c)
{
    return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}

/* The next string in the text, past comments and punctuation; braces count depth. */
static int next_string(const char **at, const char *end, int *depth, char *out, size_t room)
{
    const char *p = *at;
    while (p < end)
    {
        if (p + 1 < end && p[0] == '/' && p[1] == '/') { while (p < end && *p != '\n') ++p; continue; }
        if (*p == '{') ++*depth;
        else if (*p == '}') --*depth;
        else if (*p == '"')
        {
            size_t n = 0;
            for (++p; p < end && *p != '"'; ++p)
                if (n + 1 < room) out[n++] = *p;
            out[n] = 0;
            *at = p + 1;
            return 1;
        }
        ++p;
    }
    *at = p;
    return 0;
}

static track_t *entry(const char *key)
{
    unsigned char digest[20];
    if (strlen(key) != 40) return NULL;
    for (int i = 0; i < 20; ++i)
    {
        int high = hex(key[i * 2]), low = hex(key[i * 2 + 1]);
        if (high < 0 || low < 0) return NULL;
        digest[i] = (unsigned char)(high << 4 | low);
    }
    for (int i = 0; i < track_count; ++i)
        if (!memcmp(tracks[i].key, digest, 20)) return &tracks[i];
    if (track_count == track_room)
    {
        int room = track_room ? track_room * 2 : 64;
        track_t *grown = realloc(tracks, (size_t)room * sizeof(*tracks));
        if (!grown) return NULL;
        tracks = grown;
        track_room = room;
    }
    track_t *track = &tracks[track_count++];
    memset(track, 0, sizeof(*track));
    memcpy(track->key, digest, 20);
    track->volume = 1;
    return track;
}

void KexTracks_Add(const char *text, size_t size)
{
    const char *at = text, *end = text + size;
    char word[64], value[64];
    int depth = 0;
    track_t *track = NULL;
    while (next_string(&at, end, &depth, word, sizeof(word)))
    {
        if (depth == 1) { track = entry(word); continue; }
        if (depth != 2 || !next_string(&at, end, &depth, value, sizeof(value)) || !track) continue;
        if (!strcmp(word, "MIDI")) strncpy(track->original, value, 8);
        else if (!strcmp(word, "Remixed")) strncpy(track->remix, value, 8);
        else if (!strcmp(word, "volume")) track->volume = (float)atof(value);
    }
}

static const track_t *lookup(const unsigned char digest[20])
{
    for (int i = 0; i < track_count; ++i)
        if (!memcmp(tracks[i].key, digest, 20)) return &tracks[i];
    return NULL;
}

const char *KexTracks_Find(const unsigned char score[20], int remixed)
{
    const track_t *track = lookup(score);
    if (!track) return NULL;
    const char *name = remixed ? track->remix : track->original;
    return name[0] ? name : NULL;
}

float KexTracks_Volume(const unsigned char recording[20])
{
    const track_t *track = lookup(recording);
    return track && track->volume > 0 ? track->volume : 1;
}
