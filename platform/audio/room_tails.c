/* Traced room tails. See room_tails.h. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "doomdef.h"
#include "doomstat.h"
#include "m_argv.h"
#include "r_state.h"
#include "../rooms.h"
#include "acoustic_graph.h"
#include "convolver.h"
#include "far_tail.h"
#include "reverb.h"
#include "room.h"
#include "room_ir.h"
#include "room_records.h"
#include "room_tails.h"
#include "room_trace.h"
#include <SDL3/SDL_atomic.h>
#include <SDL3/SDL_cpuinfo.h>
#include <SDL3/SDL_mutex.h>
#include <SDL3/SDL_thread.h>
#include <SDL3/SDL_timer.h>

#define DIVIDE 3             /* the tail's rate is the mixer's over this */
#define TAIL_SECONDS 3
#define RAYS 8000
#define SLOTS 3              /* one playing, one fading out, one being loaded */
#define MOST_WORKERS 4
#define FADE_SECONDS 0.4f
#define MATCH_SECONDS 4      /* how long reverb.c's tail is heard out to match its loudness */
#define PREROLL_SECONDS 1.5f /* reverb.c gliding to the room first */
#define EDGE_HZ 6000.0f      /* down to the tail's rate and back up, through this */
#define PI_F 3.14159265f
#define SETTLE_TICS 8        /* a sector still this long somewhere new has settled: its rooms trace again */

/* A room's tail: w, x and y, length samples each at the tail's rate, as
   16 bits times scale; generation counts the times the room was asked for. */
typedef struct { short *ir; int length; float scale; int generation; conv_slot_t *far; } tail_t;
typedef struct { int key, generation; float x, y; room_t room; } job_t;

static struct {
    int keys, running, job_count, job_next, baked;
    tail_t *tails;           /* the worker writes, under lock */
    unsigned char *queued;   /* game thread: asked for at least once */
    int *requested;          /* game thread: the last generation asked for */
    unsigned char *dirty;    /* game thread: to trace again, a door or a pane changed */
    job_t *jobs;             /* the game thread adds, the worker takes, under lock */
    unsigned char *pending;  /* under lock: a job waits for the room */
    unsigned char *glass;    /* each line's whole pane, the game thread writes, the workers read */
    short **retired;         /* tails traced over, under lock: freed when the level goes, as a slot may be loading one */
    int retired_count, retired_capacity, retraced;
    fixed_t *last_floor, *last_ceiling, *settled_floor, *settled_ceiling;  /* game thread: sectors settling */
    int *still;
    unsigned level; int sectors;
    double bake_ms;
    float near_x, near_y;    /* the listener, under lock: the nearest room is traced next */
    SDL_Thread *threads[MOST_WORKERS];
    int workers;
    SDL_Mutex *lock;
    SDL_Semaphore *wake;
    SDL_AtomicInt cancel;
} bake = {.level = ~0u};
static int enabled = 1, rate = 44100, tail_rate = 44100 / DIVIDE, parts;

/* Far responses no longer current, kept until no far tail can be playing
   them (RoomTails_Forget). Game thread and workers, under the lock. */
static conv_slot_t **graveyard;
static int graveyard_count, graveyard_capacity;
static void bury(conv_slot_t *far)
{
    if (!far) return;
    if (graveyard_count == graveyard_capacity)
    {
        int capacity = graveyard_capacity ? 2 * graveyard_capacity : 64;
        conv_slot_t **grown = realloc(graveyard, sizeof(*grown) * capacity);
        if (!grown) return;  /* leaked rather than freed under a far tail */
        graveyard = grown; graveyard_capacity = capacity;
    }
    graveyard[graveyard_count++] = far;
}

/* The space beyond (beyond.h) ringing as its room's traced tail, as one
   voice from where it is. Mixer state, set with the stream locked. */
static struct {
    far_tail_t *tail;
    const conv_slot_t *response;
    int key;
    float gain, left, right, now_gain, now_left, now_right;
    float gathered, before, after;
    int phase;
} beyond;

/* The convolver's slots, with the stream locked; a slot being loaded is
   the worker's until it says it is done. */
enum { FREE, LOADING, PLAYING };
typedef struct { conv_slot_t *conv; int key, generation, state; float gain, target; SDL_AtomicInt loaded; } slot_t;
static slot_t slots[SLOTS];
static SDL_AtomicInt load_request;  /* slot + 1 for a worker to load, minus that while one does, 0 none */

/* The mixer's side. */
typedef struct { float b0, b1, b2, a1, a2, z1, z2; } biquad_t;
static biquad_t to_tail[2], from_tail[2][2];
static conv_input_t *input;
static float block_in[CONV_BLOCK], block_out[2][CONV_BLOCK], ears[2][3], facing_target;
static int phase, at, quiet = 1;
static Uint64 mix_ns;
/* On headphones a room's ringing is the same in both ears in the lows and
   comes apart only above about this: the directional part loses its lows. */
#define EARS_APART_HZ 500.0f
static int headphones;
static float apart_low[2];

static biquad_t lowpass(float hz, float q)
{
    float w = 2 * PI_F * hz / rate, c = cosf(w), alpha = sinf(w) / (2 * q), a0 = 1 + alpha;
    biquad_t f = {(1 - c) / 2 / a0, (1 - c) / a0, (1 - c) / 2 / a0, -2 * c / a0, (1 - alpha) / a0, 0, 0};
    return f;
}
static float run(biquad_t *f, float x)
{
    float y = f->b0 * x + f->z1;
    f->z1 = f->b1 * x - f->a1 * y + f->z2;
    f->z2 = f->b2 * x - f->a2 * y;
    return y;
}

void RoomTails_Init(int mixer_rate)
{
    rate = mixer_rate;
    tail_rate = rate / DIVIDE;
    parts = TAIL_SECONDS * tail_rate / CONV_BLOCK + 1;
    input = Conv_NewInput(parts);
    for (int s = 0; s < SLOTS; ++s) slots[s].conv = Conv_NewSlot(parts, CONV_CHANNELS);
    /* Fourth-order Butterworth, as two sections. */
    to_tail[0] = lowpass(EDGE_HZ, 0.5412f); to_tail[1] = lowpass(EDGE_HZ, 1.3066f);
    for (int c = 0; c < 2; ++c) { from_tail[c][0] = to_tail[0]; from_tail[c][1] = to_tail[1]; }
}

/* How loud reverb.c's tail is after a click in room, at the mixer's rate:
   the room against its echoes alone. */
static double classic_energy(const room_t *room)
{
    room_t echoes = *room;
    echoes.late = 0; echoes.tunnel = 0; echoes.flutter = 0;
    reverb_t *full = Reverb_New(rate), *bare = Reverb_New(rate);
    if (!full || !bare) { Reverb_Free(full); Reverb_Free(bare); return 0; }
    Reverb_SetRoom(full, room); Reverb_SetRoom(bare, &echoes);
    static float silence[512];
    float a[1024], b[1024], send[512];
    for (long n = 0; n < (long)(PREROLL_SECONDS * rate); n += 512)
    {
        Reverb_Process(full, silence, a, 512, 1);
        Reverb_Process(bare, silence, b, 512, 1);
    }
    double energy = 0;
    for (long n = 0; n < (long)MATCH_SECONDS * rate && !SDL_GetAtomicInt(&bake.cancel); n += 512)
    {
        memset(send, 0, sizeof(send));
        if (n == 0) send[0] = 1;
        memset(a, 0, sizeof(a)); memset(b, 0, sizeof(b));
        Reverb_Process(full, send, a, 512, 1);
        Reverb_Process(bare, send, b, 512, 1);
        for (int i = 0; i < 1024; ++i) energy += (double)(a[i] - b[i]) * (a[i] - b[i]);
    }
    Reverb_Free(full); Reverb_Free(bare);
    return energy;
}

/* One room on the worker: traced, made sound, as loud as reverb.c's tail. */
static void bake_one(const job_t *job, room_trace_t *trace)
{
    Uint64 start = SDL_GetTicksNS();
    unsigned seed = (unsigned)(job->x * 73856093.0f) ^ (unsigned)(job->y * 19349663.0f);
    RoomTrace_At(job->x, job->y, RAYS, seed, bake.glass, trace);
    if (SDL_GetAtomicInt(&bake.cancel)) return;
    int length = (int)(trace->bins * TRACE_BIN * tail_rate);
    if (length > TAIL_SECONDS * tail_rate) length = TAIL_SECONDS * tail_rate;
    if (length <= CONV_BLOCK) return;
    float *w = malloc(sizeof(float) * length * 3);
    short *ir = malloc(sizeof(short) * length * 3);
    if (!w || !ir) { free(w); free(ir); return; }
    float *x = w + length, *y = w + 2 * length;
    RoomIR_Build(trace, tail_rate, seed, w, x, y, length);
    /* As loud as reverb.c's tail, heard facing no way in particular. */
    float lw, lx, ly, rw, rx, ry;
    RoomIR_Ear(PI_F / 2, &lw, &lx, &ly);
    RoomIR_Ear(-PI_F / 2, &rw, &rx, &ry);
    double traced = 0;
    float peak = 0;
    for (int i = 0; i < length; ++i)
    {
        float l = lw * w[i] + lx * x[i] + ly * y[i], r = rw * w[i] + rx * x[i] + ry * y[i];
        traced += (double)l * l + (double)r * r;
        peak = fmaxf(peak, fmaxf(fabsf(w[i]), fmaxf(fabsf(x[i]), fabsf(y[i]))));
    }
    double classic = classic_energy(&job->room);
    if (SDL_GetAtomicInt(&bake.cancel) || peak <= 0 || traced <= 0) { free(w); free(ir); return; }
    float step = peak / 32767;
    for (int i = 0; i < 3 * length; ++i) ir[i] = (short)lrintf(w[i] / step);
    float scale = step * (float)sqrt(classic / traced);
    /* The same heard from afar: w alone at a quarter of the mixer's rate,
       half of it (as reverb.c's two sides are heard as one), as loud per
       sample at the slower rate. */
    int far_rate = rate / FAR_DOWN, far_length = (int)((double)length * far_rate / tail_rate);
    short *far_ir = malloc(sizeof(short) * (far_length > 0 ? far_length : 1));
    conv_slot_t *far = NULL;
    if (far_ir && far_length > CONV_BLOCK)
    {
        for (int j = 0; j < far_length; ++j)
        {
            /* Smoothed a little first: the slow rate keeps less treble. */
            float at = j * (float)tail_rate / far_rate;
            int i = (int)at;
            float f = at - i, a = 0, b = 0;
            for (int k = -1; k <= 1; ++k)
            {
                int ia = i + k, ib = i + 1 + k;
                float wk = k ? 0.25f : 0.5f;
                a += wk * (ia >= 0 && ia < length ? w[ia] : 0);
                b += wk * (ib >= 0 && ib < length ? w[ib] : 0);
            }
            float v = (a + (b - a) * f) / step;
            far_ir[j] = (short)lrintf(v > 32767 ? 32767 : v < -32767 ? -32767 : v);
        }
        far = Conv_NewSlot((far_length - 1) / CONV_BLOCK, 1);
        if (far)
        {
            const short *channel[CONV_CHANNELS] = {far_ir, NULL, NULL};
            Conv_Load(far, channel, far_length, 0.5f * scale * sqrtf((float)tail_rate / far_rate));
        }
    }
    free(far_ir);
    free(w);
    SDL_LockMutex(bake.lock);
    tail_t *t = &bake.tails[job->key];
    /* Two workers on one room: the later ask wins. */
    if (job->generation > t->generation)
    {
        if (t->ir)
        {
            if (bake.retired_count == bake.retired_capacity)
            {
                int capacity = bake.retired_capacity ? 2 * bake.retired_capacity : 32;
                short **grown = realloc(bake.retired, sizeof(*grown) * capacity);
                if (grown) { bake.retired = grown; bake.retired_capacity = capacity; }
            }
            /* Nowhere to keep it: leaked rather than freed under a slot loading it. */
            if (bake.retired_count < bake.retired_capacity) bake.retired[bake.retired_count++] = t->ir;
            ++bake.retraced;
        }
        else ++bake.baked;
        bury(t->far);
        *t = (tail_t){ir, length, scale, job->generation, far};
        ir = NULL; far = NULL;
    }
    bake.bake_ms += (SDL_GetTicksNS() - start) / 1e6;
    SDL_UnlockMutex(bake.lock);
    free(ir);
    Conv_FreeSlot(far);
}

/* A slot the game thread asked for: its room's tail into the convolver. */
static void load_slot(int s)
{
    int key = slots[s].key;
    SDL_LockMutex(bake.lock);
    tail_t t = key >= 0 && key < bake.keys ? bake.tails[key] : (tail_t){0};
    SDL_UnlockMutex(bake.lock);
    slots[s].generation = t.generation;
    if (t.ir)
    {
        const short *channel[CONV_CHANNELS] = {t.ir, t.ir + t.length, t.ir + 2 * t.length};
        Conv_Load(slots[s].conv, channel, t.length, t.scale);
    }
    else Conv_Load(slots[s].conv, (const short *const[CONV_CHANNELS]){NULL, NULL, NULL}, 0, 0);
}

static int SDLCALL worker(void *data)
{
    (void)data;
    SDL_SetCurrentThreadPriority(SDL_THREAD_PRIORITY_LOW);
    room_trace_t *trace = malloc(sizeof(*trace));
    while (trace && !SDL_GetAtomicInt(&bake.cancel))
    {
        int s = SDL_GetAtomicInt(&load_request);
        if (s > 0 && SDL_CompareAndSwapAtomicInt(&load_request, s, -s))
        {
            load_slot(s - 1);
            SDL_SetAtomicInt(&slots[s - 1].loaded, 1);
            SDL_SetAtomicInt(&load_request, 0);
            continue;
        }
        job_t job;
        int got = 0;
        SDL_LockMutex(bake.lock);
        if (bake.job_next < bake.job_count)
        {
            /* The waiting room nearest the listener. */
            int best = bake.job_next;
            float best_distance = 1e30f;
            for (int j = bake.job_next; j < bake.job_count; ++j)
            {
                float dx = bake.jobs[j].x - bake.near_x, dy = bake.jobs[j].y - bake.near_y, d = dx * dx + dy * dy;
                if (d < best_distance) { best_distance = d; best = j; }
            }
            job = bake.jobs[best];
            bake.jobs[best] = bake.jobs[bake.job_next++];
            bake.pending[job.key] = 0;
            got = 1;
        }
        SDL_UnlockMutex(bake.lock);
        if (got) bake_one(&job, trace);
        else SDL_WaitSemaphoreTimeout(bake.wake, 100);
    }
    free(trace);
    return 0;
}

static void start(void)
{
    bake.level = r_levelserial; bake.sectors = numsectors;
    bake.keys = Rooms_Count() + Rooms_YardCount();
    if (bake.keys <= 0 || !input) return;
    bake.tails = calloc((size_t)bake.keys, sizeof(*bake.tails));
    bake.queued = calloc((size_t)bake.keys, 1);
    bake.jobs = calloc((size_t)bake.keys, sizeof(*bake.jobs));
    bake.requested = calloc((size_t)bake.keys, sizeof(int));
    bake.dirty = calloc((size_t)bake.keys, 1);
    bake.pending = calloc((size_t)bake.keys, 1);
    bake.glass = calloc((size_t)(numlines > 0 ? numlines : 1), 1);
    bake.last_floor = malloc(sizeof(fixed_t) * numsectors); bake.last_ceiling = malloc(sizeof(fixed_t) * numsectors);
    bake.settled_floor = malloc(sizeof(fixed_t) * numsectors); bake.settled_ceiling = malloc(sizeof(fixed_t) * numsectors);
    bake.still = calloc((size_t)numsectors, sizeof(int));
    if (!bake.tails || !bake.queued || !bake.jobs || !bake.requested || !bake.dirty || !bake.pending || !bake.glass
        || !bake.last_floor || !bake.last_ceiling || !bake.settled_floor || !bake.settled_ceiling || !bake.still)
    { RoomTails_Stop(); return; }
    for (int i = 0; i < numlines; ++i) bake.glass[i] = (unsigned char)Acoustic_GlassWhole(i);
    for (int i = 0; i < numsectors; ++i)
    {
        bake.last_floor[i] = bake.settled_floor[i] = sectors[i].floorheight;
        bake.last_ceiling[i] = bake.settled_ceiling[i] = sectors[i].ceilingheight;
    }
    bake.retraced = 0;
    if (!bake.lock) bake.lock = SDL_CreateMutex();
    if (!bake.wake) bake.wake = SDL_CreateSemaphore(0);
    bake.job_count = bake.job_next = bake.baked = 0;
    bake.bake_ms = 0;
    Room_Prepare();
    SDL_SetAtomicInt(&bake.cancel, 0);
    SDL_SetAtomicInt(&load_request, 0);
    int cores = SDL_GetNumLogicalCPUCores() / 2;
    int want = cores < 1 ? 1 : cores > MOST_WORKERS ? MOST_WORKERS : cores;
    bake.workers = 0;
    if (bake.lock && bake.wake)
        for (int i = 0; i < want; ++i)
            if ((bake.threads[bake.workers] = SDL_CreateThread(worker, "room tails", NULL))) ++bake.workers;
    bake.running = bake.workers > 0;
}

void RoomTails_Stop(void)
{
    if (bake.workers)
    {
        SDL_SetAtomicInt(&bake.cancel, 1);
        for (int i = 0; i < bake.workers; ++i) SDL_SignalSemaphore(bake.wake);
        for (int i = 0; i < bake.workers; ++i) SDL_WaitThread(bake.threads[i], NULL);
        bake.workers = 0;
    }
    SDL_SetAtomicInt(&load_request, 0);
    /* A slot left loading is no one's now; the mixer never plays one. */
    for (int s = 0; s < SLOTS; ++s) if (slots[s].state == LOADING) slots[s].state = FREE;
    for (int k = 0; bake.tails && k < bake.keys; ++k) { free(bake.tails[k].ir); bury(bake.tails[k].far); }
    for (int i = 0; i < bake.retired_count; ++i) free(bake.retired[i]);
    free(bake.tails); free(bake.queued); free(bake.jobs); free(bake.requested); free(bake.dirty); free(bake.pending);
    free(bake.glass); free(bake.retired);
    free(bake.last_floor); free(bake.last_ceiling); free(bake.settled_floor); free(bake.settled_ceiling); free(bake.still);
    bake.tails = NULL; bake.queued = NULL; bake.jobs = NULL; bake.requested = NULL; bake.dirty = bake.pending = NULL;
    bake.glass = NULL; bake.retired = NULL; bake.retired_count = bake.retired_capacity = 0;
    bake.last_floor = bake.last_ceiling = bake.settled_floor = bake.settled_ceiling = NULL; bake.still = NULL;
    bake.keys = bake.running = 0;
    bake.level = ~0u;
}

void RoomTails_Set(int on)
{
    enabled = on != 0;
    if (!enabled) RoomTails_Stop();
}

/* Game thread: room key traced (again) with the level as it is now. A
   room already waiting has its job brought up to date instead. */
static void ask(int key)
{
    const room_record_t *r = RoomRecords_Get(key);
    if (!r) return;
    job_t job = {key, ++bake.requested[key], r->x, r->y, r->room};
    bake.queued[key] = 1;
    SDL_LockMutex(bake.lock);
    if (bake.pending[key])
    {
        for (int j = bake.job_next; j < bake.job_count; ++j) if (bake.jobs[j].key == key) bake.jobs[j] = job;
    }
    else
    {
        /* Each room waits at most once, so the queue never outgrows the rooms. */
        if (bake.job_count == bake.keys)
        {
            memmove(bake.jobs, bake.jobs + bake.job_next, sizeof(job_t) * (bake.job_count - bake.job_next));
            bake.job_count -= bake.job_next; bake.job_next = 0;
        }
        bake.jobs[bake.job_count++] = job;
        bake.pending[key] = 1;
    }
    SDL_UnlockMutex(bake.lock);
}

/* The rooms a sector opens or closes between: its own and those across its lines. */
static void mark_sector(int s)
{
    int key = RoomRecords_Key(s);
    if (key >= 0 && key < bake.keys) bake.dirty[key] = 1;
    const sector_t *sec = &sectors[s];
    for (int i = 0; i < sec->linecount; ++i)
    {
        const line_t *line = sec->lines[i];
        for (int side = 0; side < 2; ++side)
        {
            const sector_t *other = side ? line->backsector : line->frontsector;
            if (!other) continue;
            key = RoomRecords_Key((int)(other - sectors));
            if (key >= 0 && key < bake.keys) bake.dirty[key] = 1;
        }
    }
}

/* A door, lift or floor that comes to rest somewhere new. */
static void watch_sectors(void)
{
    for (int s = 0; s < numsectors; ++s)
    {
        const sector_t *sec = &sectors[s];
        if (sec->floorheight != bake.last_floor[s] || sec->ceilingheight != bake.last_ceiling[s])
        {
            bake.last_floor[s] = sec->floorheight; bake.last_ceiling[s] = sec->ceilingheight; bake.still[s] = 0;
            continue;
        }
        if (++bake.still[s] < SETTLE_TICS
            || (sec->floorheight == bake.settled_floor[s] && sec->ceilingheight == bake.settled_ceiling[s]))
            continue;
        bake.settled_floor[s] = sec->floorheight; bake.settled_ceiling[s] = sec->ceilingheight;
        mark_sector(s);
    }
}

void RoomTails_Glass(int line, int whole)
{
    if (!bake.running || line < 0 || line >= numlines) return;
    bake.glass[line] = whole != 0;
    for (int side = 0; side < 2; ++side)
    {
        const sector_t *s = side ? lines[line].backsector : lines[line].frontsector;
        int key = s ? RoomRecords_Key((int)(s - sectors)) : -1;
        if (key >= 0 && key < bake.keys) bake.dirty[key] = 1;
    }
}

void RoomTails_Follow(void)
{
    if (!enabled || gamestate != GS_LEVEL || !numsectors) return;
    if (bake.level != r_levelserial || bake.sectors != numsectors) { RoomTails_Stop(); start(); }
    if (!bake.running) return;
    const mobj_t *listener = players[consoleplayer].mo;
    if (listener)
    {
        SDL_LockMutex(bake.lock);
        bake.near_x = listener->x / (float)FRACUNIT; bake.near_y = listener->y / (float)FRACUNIT;
        SDL_UnlockMutex(bake.lock);
    }
    /* The rooms measured since, and those a door or pane changed (a room
       not asked for yet will be traced as the level is then anyway). */
    watch_sectors();
    int added = 0;
    for (int key = 0; key < bake.keys; ++key)
    {
        int again = bake.dirty[key] && bake.queued[key];
        bake.dirty[key] = 0;
        if ((bake.queued[key] && !again) || !RoomRecords_Get(key)) continue;
        ask(key);
        ++added;
    }
    for (int i = 0; added && i < bake.workers; ++i) SDL_SignalSemaphore(bake.wake);
    static int log = -1;
    if (log < 0) log = M_CheckParm("-levelsoundlog") > 0;
    if (log && gametic % TICRATE == 0)
    {
        SDL_LockMutex(bake.lock);
        int baked = bake.baked, queued = bake.job_count - bake.job_next, again = bake.retraced;
        double ms = bake.bake_ms;
        SDL_UnlockMutex(bake.lock);
        int playing = -1;
        for (int s = 0; s < SLOTS; ++s) if (slots[s].state == PLAYING && slots[s].target > 0) playing = slots[s].key;
        fprintf(stderr, "roomtails tic %d: %d of %d rooms traced, %d again (%d waiting) on %d threads, %.0f ms each;"
                " playing %d, beyond %d at %.2f; mixing %.2f ms/s\n", gametic, baked, bake.keys, again, queued, bake.workers,
                baked + again ? ms / (baked + again) : 0.0, playing, beyond.response ? beyond.key : -1, beyond.now_gain,
                mix_ns / 1e6);
        mix_ns = 0;
    }
}

int RoomTails_Listen(int key, float facing)
{
    facing_target = facing;
    int have = 0, generation = 0;
    if (enabled && bake.running && key >= 0 && key < bake.keys)
    {
        SDL_LockMutex(bake.lock);
        have = bake.tails[key].ir != NULL;
        generation = bake.tails[key].generation;
        SDL_UnlockMutex(bake.lock);
    }
    int busy = SDL_GetAtomicInt(&load_request) != 0, mine = -1;
    for (int s = 0; s < SLOTS; ++s)
    {
        /* Loaded for a room the listener has left already. */
        if (slots[s].state == LOADING && SDL_GetAtomicInt(&slots[s].loaded) && slots[s].key != key) slots[s].state = FREE;
        /* A room traced again crossfades from its old tail into its new one. */
        if (slots[s].state != FREE && slots[s].key == key && (slots[s].state == LOADING || slots[s].generation == generation))
            mine = have ? s : -1;
    }
    if (have && mine < 0 && !busy)
    {
        /* A free slot, or else the quietest one fading out. */
        for (int s = 0; s < SLOTS && mine < 0; ++s) if (slots[s].state == FREE) mine = s;
        for (int s = 0; s < SLOTS && mine < 0; ++s) if (slots[s].state == PLAYING && slots[s].target == 0) mine = s;
        if (mine >= 0)
        {
            slots[mine].key = key; slots[mine].state = LOADING; slots[mine].gain = 0;
            SDL_SetAtomicInt(&slots[mine].loaded, 0);
            SDL_SetAtomicInt(&load_request, mine + 1);
            SDL_SignalSemaphore(bake.wake);
        }
    }
    if (mine >= 0 && slots[mine].state == LOADING && SDL_GetAtomicInt(&slots[mine].loaded)) slots[mine].state = PLAYING;
    for (int s = 0; s < SLOTS; ++s) slots[s].target = s == mine ? 1.0f : 0.0f;
    return have;
}

const conv_slot_t *RoomTails_Far(int key)
{
    if (!enabled || !bake.running || key < 0 || key >= bake.keys) return NULL;
    SDL_LockMutex(bake.lock);
    const conv_slot_t *far = bake.tails[key].far;
    SDL_UnlockMutex(bake.lock);
    return far;
}

void RoomTails_Forget(void)
{
    if (bake.lock) SDL_LockMutex(bake.lock);
    for (int i = 0; i < graveyard_count; ++i) Conv_FreeSlot(graveyard[i]);
    graveyard_count = 0;
    if (bake.lock) SDL_UnlockMutex(bake.lock);
    beyond.response = NULL; beyond.key = -1;
}

#define PAN_POWER (1 + (126.0f / 127) * (126.0f / 127))  /* as the mixer pans (i_sdl_sound.c) */
int RoomTails_Beyond(int key, int separation)
{
    const conv_slot_t *far = RoomTails_Far(key);
    if (!beyond.tail) beyond.tail = FarTail_New(TAIL_SECONDS, rate / FAR_DOWN);
    if (!beyond.tail) return 0;
    separation = separation < 1 ? 1 : separation > 254 ? 254 : separation;
    float side = fminf(1.0f, (float)(separation < 127 ? separation : 254 - separation) / 127.0f), near = sqrtf(PAN_POWER - side * side);
    beyond.left = separation < 128 ? near : side;
    beyond.right = separation < 128 ? side : near;
    /* A space with no tail fades out on the one it had. */
    if (far) { beyond.response = far; beyond.key = key; }
    beyond.gain = far ? 1.0f : 0.0f;
    return far != NULL;
}

void RoomTails_ProcessBeyond(const float *send, float *stereo, int frames, float amount)
{
    if (!beyond.tail || (!beyond.response && beyond.now_gain <= 0)) return;
    if (beyond.gain <= 0 && beyond.now_gain < 1e-4f) { beyond.now_gain = 0; beyond.response = NULL; return; }
    float slow_in[1024 / FAR_DOWN + 1], slow_out[1024 / FAR_DOWN + 1];
    float glide = 1 - expf(-frames / (0.3f * rate));
    for (int done = 0; done < frames; done += 1024)
    {
        int count = frames - done < 1024 ? frames - done : 1024, start = beyond.phase, n = 0;
        for (int k = 0; k < count; ++k)
        {
            beyond.gathered += send[done + k];
            if (++beyond.phase == FAR_DOWN) { slow_in[n++] = beyond.gathered / FAR_DOWN; beyond.gathered = 0; beyond.phase = 0; }
        }
        FarTail_Run(beyond.tail, beyond.response, slow_in, slow_out, n);
        float gain = beyond.now_gain, left = beyond.now_left, right = beyond.now_right;
        beyond.now_gain += (beyond.gain - beyond.now_gain) * glide;
        beyond.now_left += (beyond.left - beyond.now_left) * glide;
        beyond.now_right += (beyond.right - beyond.now_right) * glide;
        float step = 1.0f / count;
        int phase = start, m = 0;
        for (int k = 0; k < count; ++k)
        {
            if (++phase == FAR_DOWN) { phase = 0; beyond.before = beyond.after; beyond.after = slow_out[m++]; }
            float t = k * step, g = amount * (gain + (beyond.now_gain - gain) * t);
            float value = (beyond.before + (beyond.after - beyond.before) * (phase / (float)FAR_DOWN)) * g;
            stereo[2 * (done + k)] += value * (left + (beyond.now_left - left) * t);
            stereo[2 * (done + k) + 1] += value * (right + (beyond.now_right - right) * t);
        }
    }
}

/* The listener's room's side alone. */
static void clear_listener(void)
{
    if (input) Conv_ClearInput(input);
    memset(block_in, 0, sizeof(block_in)); memset(block_out, 0, sizeof(block_out));
    for (int i = 0; i < 2; ++i) { to_tail[i].z1 = to_tail[i].z2 = 0; from_tail[0][i].z1 = from_tail[0][i].z2 = from_tail[1][i].z1 = from_tail[1][i].z2 = 0; }
    phase = at = 0;
    apart_low[0] = apart_low[1] = 0;
    for (int s = 0; s < SLOTS; ++s) if (slots[s].state == PLAYING) { slots[s].state = FREE; slots[s].gain = 0; }
}

void RoomTails_Clear(void)
{
    clear_listener();
    if (beyond.tail) FarTail_Clear(beyond.tail);
    beyond.gathered = beyond.before = beyond.after = 0; beyond.phase = 0;
    beyond.now_gain = beyond.gain = 0; beyond.response = NULL; beyond.key = -1;
}

/* A block at the tail's rate: in through the history, out through each
   playing room's tail, turned to the listener. */
static void run_block(void)
{
    Conv_Push(input, block_in);
    memset(block_out, 0, sizeof(block_out));
    float target[2][3];
    RoomIR_Ear(facing_target + PI_F / 2, &target[0][0], &target[0][1], &target[0][2]);
    RoomIR_Ear(facing_target - PI_F / 2, &target[1][0], &target[1][1], &target[1][2]);
    const float fade = CONV_BLOCK / (FADE_SECONDS * tail_rate), ramp = 1.0f / CONV_BLOCK;
    static float out[CONV_CHANNELS][CONV_BLOCK], all_round[2][CONV_BLOCK], toward[2][CONV_BLOCK];
    memset(all_round, 0, sizeof(all_round)); memset(toward, 0, sizeof(toward));
    for (int s = 0; s < SLOTS; ++s)
    {
        slot_t *slot = &slots[s];
        if (slot->state != PLAYING) continue;
        Conv_Run(input, slot->conv, out);
        float from = slot->gain, to = slot->target > from ? fminf(from + fade, slot->target) : fmaxf(from - fade, slot->target);
        for (int i = 0; i < CONV_BLOCK; ++i)
        {
            float t = i * ramp, g = from + (to - from) * t;
            for (int side = 0; side < 2; ++side)
            {
                const float *e = ears[side], *f = target[side];
                float gw = e[0] + (f[0] - e[0]) * t, gx = e[1] + (f[1] - e[1]) * t, gy = e[2] + (f[2] - e[2]) * t;
                all_round[side][i] += g * gw * out[0][i];
                toward[side][i] += g * (gx * out[1][i] + gy * out[2][i]);
            }
        }
        slot->gain = to;
        if (slot->target == 0 && to <= 0) slot->state = FREE;
    }
    memcpy(ears, target, sizeof(ears));
    float keep = expf(-2 * PI_F * EARS_APART_HZ / tail_rate);
    for (int side = 0; side < 2; ++side)
        for (int i = 0; i < CONV_BLOCK; ++i)
        {
            float d = toward[side][i];
            if (headphones)
            {
                apart_low[side] = d + (apart_low[side] - d) * keep + 1e-18f - 1e-18f;
                d -= apart_low[side];
            }
            block_out[side][i] = all_round[side][i] + d;
        }
}

void RoomTails_SetEars(int on) { headphones = on != 0; }

void RoomTails_Process(const float *send, float *stereo, int frames, float amount)
{
    int any = 0;
    for (int s = 0; s < SLOTS; ++s) any |= slots[s].state == PLAYING;
    if (!any || !input)
    {
        if (!quiet) { clear_listener(); quiet = 1; }
        return;
    }
    quiet = 0;
    Uint64 start = SDL_GetTicksNS();
    for (int i = 0; i < frames; ++i)
    {
        float v = run(&to_tail[1], run(&to_tail[0], send[i])), l = 0, r = 0;
        if (++phase >= DIVIDE)
        {
            phase = 0;
            block_in[at] = v;
            l = DIVIDE * block_out[0][at]; r = DIVIDE * block_out[1][at];
            if (++at == CONV_BLOCK) { at = 0; run_block(); }
        }
        l = run(&from_tail[0][1], run(&from_tail[0][0], l));
        r = run(&from_tail[1][1], run(&from_tail[1][0], r));
        stereo[2 * i] += amount * l;
        stereo[2 * i + 1] += amount * r;
    }
    mix_ns += SDL_GetTicksNS() - start;
}
