/* Stage 0 of PLAN-convolution.md: a room's traced tail against today's
   reverb, offline, for listening. Loads a level hidden, as doom-room-probe:

     doom-room-ir <iwad> [-file pwad] [-warp e m | -warp m] [-out dir] [-name prefix]
                  [-rays n] [-pick] [-at name:x,y,facing]...

   Facings are degrees, as map angles turn (90 faces north). -pick chooses
   spots from the level's rooms (room_records.h): its most corridor-like
   room facing along it and across it, its biggest room, a small one and
   its most open yard. Each spot is measured as the mixer hears it now
   (room.c's echoes and reverb.c's tail: "classic") and traced
   (room_trace.c, then room_ir.c: "traced", room.c's echoes kept, the
   tail from the trace instead of reverb.c's, as loud as reverb.c's). Files
   are <out>/<prefix>-<spot>-<sound>-classic.wav and -traced.wav for an
   impulse, the pistol, the shotgun, an imp and a zombieman; one line per
   spot goes to the console and <out>/<prefix>-summary.txt: the tails'
   decay, how wide they are (the left and right tails' correlation, 1
   one sound in the middle .. 0 apart) and how loud the trace says the tail
   is next to reverb.c's. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "doomdef.h"
#include "doomstat.h"
#include "d_main.h"
#include "g_game.h"
#include "i_system.h"
#include "m_argv.h"
#include "w_wad.h"
#include "z_zone.h"
#include "r_state.h"
#include "../platform/audio/fft.h"
#include "../platform/audio/resample.h"
#include "../platform/audio/reverb.h"
#include "../platform/audio/room.h"
#include "../platform/audio/room_ir.h"
#include "../platform/audio/room_records.h"
#include "../platform/audio/room_trace.h"
#include "../platform/rooms.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#define RATE 44100
#define SFX_GAIN 0.75f      /* i_sdl_sound.c's */
#define LIMIT 0.9f
#define LIMIT_RELEASE_SECONDS 0.15f
#define TAIL_SECONDS 3
#define PREROLL_SECONDS 1.5f  /* the reverb glides to its room this long before the sound */
#define MAX_SPOTS 64
#define PI_F 3.14159265f

typedef struct { char name[32]; float x, y, facing; } spot_t;
static spot_t spots[MAX_SPOTS];
static int spot_count, pick, rays = 8000;
static const char *out_dir = ".", *prefix = "ir";
static FILE *summary;

static void put16(FILE *f, unsigned v) { fputc(v & 255, f); fputc(v >> 8 & 255, f); }
static void put32(FILE *f, unsigned v) { put16(f, v & 0xffff); put16(f, v >> 16); }
static void write_wav(const char *path, const float *stereo, long frames)
{
    FILE *f = fopen(path, "wb");
    if (!f) I_Error("Room IR: can't write %s", path);
    fwrite("RIFF", 1, 4, f); put32(f, 36 + frames * 4);
    fwrite("WAVEfmt ", 1, 8, f); put32(f, 16); put16(f, 1); put16(f, 2);
    put32(f, RATE); put32(f, RATE * 4); put16(f, 4); put16(f, 16);
    fwrite("data", 1, 4, f); put32(f, frames * 4);
    for (long i = 0; i < frames * 2; ++i)
    {
        float v = stereo[i] * 32767;
        put16(f, (unsigned)(short)(v >= 32767 ? 32767 : v <= -32768 ? -32768 : lrintf(v)));
    }
    fclose(f);
}

/* A DMX sound lump as floats at RATE; NULL if the WAD has none. */
static float *load_sound(const char *name, long *frames)
{
    int lump = W_CheckNumForName((char *)name);
    if (lump < 0) return NULL;
    const unsigned char *data = W_CacheLumpNum(lump, PU_STATIC);
    int length = W_LumpLength(lump);
    if (length < 8 || (data[0] | data[1] << 8) != 3) return NULL;
    int rate = data[2] | data[3] << 8;
    long count = data[4] | data[5] << 8 | data[6] << 16 | (long)data[7] << 24;
    if (count > length - 8) count = length - 8;
    *frames = (long)(count * (double)RATE / rate);
    float *out = calloc((size_t)*frames, sizeof(float));
    for (long i = 0; i < *frames; ++i) out[i] = Resample_At(data + 8, (int)count, i * (double)rate / RATE, RESAMPLE_ORIGINAL);
    return out;
}

/* reverb.c over the send, after it has glided to its room on silence. */
static void classic(const room_t *room, const float *send, float *stereo, long frames)
{
    reverb_t *r = Reverb_New(RATE);
    if (!r) I_Error("Room IR: out of memory");
    Reverb_SetRoom(r, room);
    static float silence[512], scratch[1024];
    for (long n = 0; n < (long)(PREROLL_SECONDS * RATE); n += 512) Reverb_Process(r, silence, scratch, 512, 1);
    for (long n = 0; n < frames; n += 512)
        Reverb_Process(r, send + n, stereo + 2 * n, (int)(frames - n < 512 ? frames - n : 512), 1);
    Reverb_Free(r);
}

/* Adds send convolved with the left and right responses (ir frames long), times gain. */
static void convolve(const float *send, long frames, const float *left, const float *right, long ir, float gain, float *stereo)
{
    int n = Fft_Size((int)(frames + ir));
    float *sr = calloc(n, sizeof(float)), *si = calloc(n, sizeof(float));
    float *hr = calloc(n, sizeof(float)), *hi = calloc(n, sizeof(float));
    if (!sr || !si || !hr || !hi) I_Error("Room IR: out of memory");
    memcpy(sr, send, sizeof(float) * frames);
    Fft_Complex(sr, si, n, 0);
    for (int side = 0; side < 2; ++side)
    {
        memset(hr, 0, sizeof(float) * n); memset(hi, 0, sizeof(float) * n);
        memcpy(hr, side ? right : left, sizeof(float) * ir);
        Fft_Complex(hr, hi, n, 0);
        for (int i = 0; i < n; ++i)
        {
            float a = sr[i] * hr[i] - si[i] * hi[i], b = sr[i] * hi[i] + si[i] * hr[i];
            hr[i] = a; hi[i] = b;
        }
        Fft_Complex(hr, hi, n, 1);
        for (long i = 0; i < frames; ++i) stereo[2 * i + side] += gain * hr[i];
    }
    free(sr); free(si); free(hr); free(hi);
}

static void limit(float *stereo, long frames)
{
    float gain = 1, release = 1 - expf(-1.0f / (LIMIT_RELEASE_SECONDS * RATE));
    for (long n = 0; n < frames; ++n)
    {
        float p = fmaxf(fabsf(stereo[2 * n]), fabsf(stereo[2 * n + 1]));
        gain += (1 - gain) * release;
        if (p * gain > LIMIT) gain = LIMIT / p;
        stereo[2 * n] *= gain; stereo[2 * n + 1] *= gain;
    }
}

/* Energy and left-right correlation of a stereo signal from frame start on. */
static void measure_tail(const float *stereo, long frames, long start, double *energy, double *correlation)
{
    double ll = 0, rr = 0, lr = 0;
    for (long i = start; i < frames; ++i)
    {
        double l = stereo[2 * i], r = stereo[2 * i + 1];
        ll += l * l; rr += r * r; lr += l * r;
    }
    *energy = ll + rr;
    *correlation = ll > 0 && rr > 0 ? lr / sqrt(ll * rr) : 1;
}

static const struct { const char *lump, *name; } sounds[] = {
    {NULL, "impulse"}, {"DSPISTOL", "pistol"}, {"DSSHOTGN", "shotgun"}, {"DSBGSIT1", "imp"}, {"DSPOSIT1", "zombie"},
};

static void listen(const spot_t *s)
{
    float facing = s->facing * PI_F / 180;
    room_t room, early;
    room_outline_t outline;
    Room_MeasureAt(s->x, s->y, facing, &room, &outline);
    /* The echoes alone: reverb.c's tail, and what it adds down a tube, off. */
    early = room; early.late = 0; early.tunnel = 0; early.flutter = 0;

    static room_trace_t trace;
    Uint64 start = SDL_GetTicksNS();
    unsigned seed = (unsigned)(s->x * 73856093.0f) ^ (unsigned)(s->y * 19349663.0f);
    RoomTrace_At(s->x, s->y, rays, seed, NULL, &trace);
    double traced_ms = (SDL_GetTicksNS() - start) / 1e6;
    long ir = (long)TAIL_SECONDS * RATE;
    float *w = malloc(sizeof(float) * ir), *x = malloc(sizeof(float) * ir), *y = malloc(sizeof(float) * ir);
    float *left = malloc(sizeof(float) * ir), *right = malloc(sizeof(float) * ir);
    if (!w || !x || !y || !left || !right) I_Error("Room IR: out of memory");
    RoomIR_Build(&trace, RATE, seed, w, x, y, (int)ir);
    float lw, lx, ly, rw, rx, ry;
    RoomIR_Ear(facing + PI_F / 2, &lw, &lx, &ly);
    RoomIR_Ear(facing - PI_F / 2, &rw, &rx, &ry);
    for (long i = 0; i < ir; ++i)
    {
        left[i] = lw * w[i] + lx * x[i] + ly * y[i];
        right[i] = rw * w[i] + rx * x[i] + ry * y[i];
    }

    /* How loud and wide each tail is after one click. */
    long frames = (long)(TAIL_SECONDS + 1) * RATE;
    float *send = calloc(frames, sizeof(float));
    float *full = calloc(frames * 2, sizeof(float)), *echoes = calloc(frames * 2, sizeof(float));
    float *traced = calloc(frames * 2, sizeof(float));
    send[0] = 1;
    classic(&room, send, full, frames);
    classic(&early, send, echoes, frames);
    for (long i = 0; i < 2 * frames; ++i) full[i] -= echoes[i];
    double classic_energy, classic_correlation, trace_energy, trace_correlation;
    measure_tail(full, frames, 0, &classic_energy, &classic_correlation);
    convolve(send, frames, left, right, ir, 1, traced);
    measure_tail(traced, frames, 0, &trace_energy, &trace_correlation);
    float gain = trace_energy > 0 ? (float)sqrt(classic_energy / trace_energy) : 0;
    /* How much of the traced tail (mids, past 80 ms) comes from ahead or behind
       rather than the sides. */
    double ahead = 0, all = 0;
    for (int i = (int)(0.08f / TRACE_BIN); i < trace.bins; ++i)
        for (int k = 0; k < TRACE_WAYS; ++k)
        {
            double e = trace.energy[i][1][k];
            all += e;
            if (fabsf(cosf(k * PI_F / 4 - facing)) > 0.75f) ahead += e;
        }
    char line[512];
    snprintf(line, sizeof(line),
             "%s-%-16s at %6.0f,%6.0f facing %3.0f  classic: decay %.2f s corr %.2f tunnel %.2f  "
             "traced: decay %.2f/%.2f/%.2f s corr %.2f, ahead+behind %.0f%%, %+.1f dB vs classic, %.0f%% escaped  %.0f ms",
             prefix, s->name, s->x, s->y, s->facing, room.decay, classic_correlation, room.tunnel, trace.decay[0],
             trace.decay[1], trace.decay[2], trace_correlation, all > 0 ? 100 * ahead / all : 0,
             10 * log10(fmax(trace_energy, 1e-12) / fmax(classic_energy, 1e-12)), 100 * trace.escaped, traced_ms);
    printf("%s\n", line);
    if (summary) fprintf(summary, "%s\n", line);
    free(send); free(full); free(echoes); free(traced);

    for (size_t k = 0; k < sizeof(sounds) / sizeof(sounds[0]); ++k)
    {
        long length = 0;
        float *sound = NULL;
        if (sounds[k].lump) { sound = load_sound(sounds[k].lump, &length); if (!sound) continue; }
        long lead = RATE / 4, total = lead + length + (long)TAIL_SECONDS * RATE;
        float *dry = calloc(total * 2, sizeof(float)), *send2 = calloc(total, sizeof(float));
        if (sound)
            for (long i = 0; i < length; ++i)
            {
                dry[2 * (lead + i)] = dry[2 * (lead + i) + 1] = SFX_GAIN * sound[i];
                send2[lead + i] = SFX_GAIN * sound[i];
            }
        else send2[lead] = 0.5f;
        for (int version = 0; version < 2; ++version)
        {
            float *mix = malloc(sizeof(float) * total * 2);
            memcpy(mix, dry, sizeof(float) * total * 2);
            if (version == 0) classic(&room, send2, mix, total);
            else { classic(&early, send2, mix, total); convolve(send2, total, left, right, ir, gain, mix); }
            limit(mix, total);
            char path[1024];
            snprintf(path, sizeof(path), "%s/%s-%s-%s-%s.wav", out_dir, prefix, s->name, sounds[k].name,
                     version ? "traced" : "classic");
            write_wav(path, mix, total);
            free(mix);
        }
        free(dry); free(send2); free(sound);
    }
    free(w); free(x); free(y); free(left); free(right);
}

/* -pick: spots from the level's rooms. */
static void add_spot(const char *name, float x, float y, float facing)
{
    if (spot_count >= MAX_SPOTS) return;
    spot_t *s = &spots[spot_count++];
    snprintf(s->name, sizeof(s->name), "%s", name);
    s->x = x; s->y = y; s->facing = facing;
}
static void pick_spots(void)
{
    RoomRecords_Finish();
    int keys = Rooms_Count() + Rooms_YardCount();
    int corridor = -1, hall = -1, small = -1, yard = -1;
    float best_long = 0, best_volume = 0, least_volume = 1e30f, best_open = 0.5f, corridor_facing = 0;
    for (int key = 0; key < keys; ++key)
    {
        const room_record_t *r = RoomRecords_Get(key);
        /* Not a door or lift shut on its spot. */
        if (!r || r->sector < 0 || sectors[r->sector].ceilingheight - sectors[r->sector].floorheight < 64 * FRACUNIT) continue;
        if (r->room.outdoors > best_open) { best_open = r->room.outdoors; yard = key; }
        if (r->room.outdoors > 0.2f) continue;
        if (r->volume > best_volume) { best_volume = r->volume; hall = key; }
        if (r->volume > 20 && r->volume < least_volume) { least_volume = r->volume; small = key; }
        /* How long against how wide, from the floor plan round its spot. */
        room_t room;
        room_outline_t o;
        Room_MeasureAt(r->x, r->y, 0, &room, &o);
        float longest = 0, narrowest = 1e30f;
        int along = 0;
        for (int i = 0; i < ROOM_RAYS / 2; ++i)
        {
            float span = o.reach[i] + o.reach[i + ROOM_RAYS / 2];
            if (span > longest) { longest = span; along = i; }
            narrowest = fminf(narrowest, span);
        }
        float ratio = longest / fmaxf(narrowest, 1);
        if (narrowest < 320 && ratio > best_long) { best_long = ratio; corridor = key; corridor_facing = along * 360.0f / ROOM_RAYS; }
    }
    const room_record_t *r;
    if (corridor >= 0 && (r = RoomRecords_Get(corridor)))
    {
        add_spot("corridor-along", r->x, r->y, corridor_facing);
        add_spot("corridor-across", r->x, r->y, corridor_facing + 90);
    }
    if (hall >= 0 && (r = RoomRecords_Get(hall))) add_spot("hall", r->x, r->y, 90);
    if (small >= 0 && small != hall && (r = RoomRecords_Get(small))) add_spot("small", r->x, r->y, 90);
    if (yard >= 0 && (r = RoomRecords_Get(yard))) add_spot("yard", r->x, r->y, 90);
}

void I_TestFrame(void)
{
    if (gamestate != GS_LEVEL || gametic < 2) return;
    Room_Prepare();
    if (pick) pick_spots();
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s-summary.txt", out_dir, prefix);
    summary = fopen(path, "w");
    for (int i = 0; i < spot_count; ++i) listen(&spots[i]);
    if (summary) fclose(summary);
    fflush(stdout);
    I_Quit();
}

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        fprintf(stderr, "usage: %s <iwad> [-file pwad] [-warp ...] [-out dir] [-name prefix] [-rays n] [-pick] [-at name:x,y,facing]...\n", argv[0]);
        return 2;
    }
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
    if (!SDL_Init(SDL_INIT_VIDEO)) return 1;
    static char *args[256], config[1024];
    int count = 0;
    args[count++] = "room-ir"; args[count++] = "-iwad"; args[count++] = argv[1];
    for (int i = 2; i < argc; ++i)
    {
        if (!strcmp(argv[i], "-out") && i + 1 < argc) out_dir = argv[++i];
        else if (!strcmp(argv[i], "-name") && i + 1 < argc) prefix = argv[++i];
        else if (!strcmp(argv[i], "-rays") && i + 1 < argc) rays = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-pick")) pick = 1;
        else if (!strcmp(argv[i], "-at") && i + 1 < argc)
        {
            spot_t *s = &spots[spot_count];
            if (spot_count >= MAX_SPOTS || sscanf(argv[++i], "%31[^:]:%f,%f,%f", s->name, &s->x, &s->y, &s->facing) != 4)
            { fprintf(stderr, "Bad spot %s\n", argv[i]); return 2; }
            ++spot_count;
        }
        else if (count < 240) args[count++] = argv[i];
    }
    snprintf(config, sizeof(config), "%s/room-ir.cfg", out_dir);
    args[count++] = "-config"; args[count++] = config;
    args[count++] = "-nosound"; args[count++] = "-nomusic"; args[count++] = "-nomonsters";
    args[count] = NULL;
    myargc = count; myargv = args;
    D_DoomMain();
    return 0;
}
