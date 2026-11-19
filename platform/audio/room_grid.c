/* The listener's room from a baked grid. See room_grid.h. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "doomdef.h"
#include "doomstat.h"
#include "m_argv.h"
#include "p_local.h"
#include "r_state.h"
#include "../rooms.h"
#include "acoustic_graph.h"
#include "map.h"
#include "room_grid.h"
#include <SDL3/SDL_atomic.h>
#include <SDL3/SDL_cpuinfo.h>
#include <SDL3/SDL_mutex.h>
#include <SDL3/SDL_thread.h>
#include <SDL3/SDL_timer.h>

/* Map units between cells (dev: -gridcell). They sit half a cell past each
   multiple, off the walls that usually stand on 32s and 64s. At 64 the
   blend smeared the tube of a corridor into the room at its mouth too often
   (the guard, room_probe -grid); 32 matches the live measurement within
   what it changes itself one running step away nearly everywhere. */
static float CELL = 32;
static int BLOCK;         /* cells a side measured together, 256 units, sharing one gather of the lines */
/* Rays each cell is measured with (dev: -roomrays 128 or 256). 32 as the live
   measurement is what the guard checks; more keep a doorway far off from
   falling between them, judged by ear. */
static int rays = ROOM_RAYS;
#define INSIDE 0.75f      /* a cell this much in its own sector is in the level, not the void past its walls */
#define SETTLE_TICS 6     /* a sector still this long where it wasn't is settled: its cells measure again */
#define MOST_THREADS 4
#define REACH_MORE 16.0f  /* a ray stopped at a line still counts as reaching it */
#define PI_F 3.14159265f

enum { WAITING, MEASURED, EMPTY };
typedef struct { room_t room; room_outline_t outline; beyond_t beyond; float reach_most; } cell_t;

static cell_t *cells;     /* only the cells in the level: slot gives each grid spot's */
static int *slot;
static unsigned char *state, *dirty;
static int *cell_sector;
static int width, height, cell_count, dirty_count;
static float origin_x, origin_y;
static unsigned built = ~0u;
static int built_sectors;
static fixed_t *settled_floor, *settled_ceiling, *last_floor, *last_ceiling;
static int *still;
static int *glass_lines, glass_count, glass_capacity; /* panes changed, to mark once no job runs */

/* One job at a time: the whole level's bake, or cells measured again. */
static struct {
    int active, blocks, count, threads;
    int *units;                 /* blocks, or cells */
    unsigned char *glass;       /* whole panes when it started */
    SDL_AtomicInt next, running;
    SDL_Thread *thread[MOST_THREADS];
    Uint64 started;
} job;
static SDL_AtomicInt cancel;
static SDL_Mutex *lock;
static int log_on = -1;

static float cell_x(int c) { return origin_x + (c % width) * CELL; }
static float cell_y(int c) { return origin_y + (c / width) * CELL; }

/* Whether (x, y) is on the inner side of every seg of its subsector: a point
   past a wall into the void fails, though not always. */
static int in_subsector(float x, float y, int *sector)
{
    const subsector_t *ss = R_PointInSubsector((fixed_t)(x * FRACUNIT), (fixed_t)(y * FRACUNIT));
    *sector = (int)(ss->sector - sectors);
    for (int i = 0; i < ss->numlines; ++i)
    {
        const seg_t *s = &segs[ss->firstline + i];
        float ax = s->v1->x / (float)FRACUNIT, ay = s->v1->y / (float)FRACUNIT;
        float bx = s->v2->x / (float)FRACUNIT, by = s->v2->y / (float)FRACUNIT;
        float length = hypotf(bx - ax, by - ay);
        /* Segs have their sector on the right. */
        if (length > 0 && ((bx - ax) * (y - ay) - (by - ay) * (x - ax)) / length > 0.5f) return 0;
    }
    return 1;
}

static void bare(float x, float y, float facing, room_t *room, room_outline_t *outline, void *glass)
{
    Room_MeasureBare(x, y, facing, glass, rays, room, outline, NULL);
}

static void measure_cell(int c)
{
    cell_t m;
    float inside;
    float x = cell_x(c), y = cell_y(c);
    Room_MeasureBare(x, y, 0, job.glass, rays, &m.room, &m.outline, &inside);
    int found = inside >= INSIDE && m.outline.volume > 0;
    if (found)
    {
        Beyond_MeasureWith(&m.room, &m.outline, 0, &m.beyond, bare, job.glass);
        m.reach_most = 0;
        for (int i = 0; i < ROOM_RAYS; ++i) m.reach_most = fmaxf(m.reach_most, m.outline.reach[i]);
    }
    SDL_LockMutex(lock);
    if (!SDL_GetAtomicInt(&cancel))
    {
        if (found) cells[slot[c]] = m;
        state[c] = found ? MEASURED : EMPTY;
    }
    SDL_UnlockMutex(lock);
}

/* The lines round block b's cells, for them all. */
static void gather_block(int b)
{
    int bw = (width + BLOCK - 1) / BLOCK, bx = b % bw, by = b / bw;
    float centre = (BLOCK - 1) * 0.5f;
    Map_GatherBlock(origin_x + (bx * BLOCK + centre) * CELL, origin_y + (by * BLOCK + centre) * CELL, centre * CELL + 1);
}
static int block_of(int c) { return (c / width) / BLOCK * ((width + BLOCK - 1) / BLOCK) + (c % width) / BLOCK; }

static void work(void)
{
    int gathered = -1;
    for (;;)
    {
        int u = SDL_AddAtomicInt(&job.next, 1);
        if (u >= job.count || SDL_GetAtomicInt(&cancel)) break;
        if (job.blocks)
        {
            int b = job.units[u], bw = (width + BLOCK - 1) / BLOCK, bx = b % bw, by = b / bw;
            gather_block(b);
            for (int j = by * BLOCK; j < by * BLOCK + BLOCK && j < height; ++j)
                for (int i = bx * BLOCK; i < bx * BLOCK + BLOCK && i < width; ++i)
                    if (state[j * width + i] == WAITING && !SDL_GetAtomicInt(&cancel)) measure_cell(j * width + i);
        }
        else
        {
            int c = job.units[u];
            if (block_of(c) != gathered) gather_block(gathered = block_of(c));
            measure_cell(c);
        }
    }
    Map_ReleaseBlock();
    SDL_AddAtomicInt(&job.running, -1);
}
static int SDLCALL worker(void *data)
{
    (void)data;
    SDL_SetCurrentThreadPriority(SDL_THREAD_PRIORITY_LOW);
    work();
    return 0;
}

static void start_job(int *units, int count, int blocks, int threads)
{
    job.units = units; job.count = count; job.blocks = blocks;
    job.glass = malloc((size_t)(numlines > 0 ? numlines : 1));
    if (job.glass)
        for (int i = 0; i < numlines; ++i) job.glass[i] = (unsigned char)Acoustic_GlassWhole(i);
    SDL_SetAtomicInt(&job.next, 0);
    job.started = SDL_GetTicksNS();
    job.active = 1;
    job.threads = 0;
    SDL_SetAtomicInt(&job.running, threads);
    for (int i = 0; i < threads; ++i)
    {
        job.thread[i] = SDL_CreateThread(worker, "room grid", NULL);
        if (job.thread[i]) ++job.threads;
        else SDL_AddAtomicInt(&job.running, -1);
    }
    /* No threads to be had: measure here. */
    if (!job.threads) { SDL_SetAtomicInt(&job.running, 1); work(); }
}

static void end_job(void)
{
    for (int i = 0; i < job.threads; ++i) SDL_WaitThread(job.thread[i], NULL);
    double ms = (SDL_GetTicksNS() - job.started) / 1e6;
    if (log_on && !SDL_GetAtomicInt(&cancel))
    {
        int measured = 0, empty = 0;
        for (int c = 0; c < cell_count; ++c) { measured += state[c] == MEASURED; empty += state[c] == EMPTY; }
        if (job.blocks)
            fprintf(stderr, "roomgrid: %d cells measured (%d empty) in %.0f ms on %d threads\n", measured, empty, ms, job.threads);
        else fprintf(stderr, "roomgrid: %d cells measured again in %.1f ms\n", job.count, ms);
    }
    free(job.units); free(job.glass);
    job.units = NULL; job.glass = NULL; job.threads = 0; job.active = 0;
}

static void forget(void)
{
    free(cells); free(slot); free(state); free(dirty); free(cell_sector);
    free(settled_floor); free(settled_ceiling); free(last_floor); free(last_ceiling); free(still);
    cells = NULL; slot = NULL; state = dirty = NULL; cell_sector = NULL;
    settled_floor = settled_ceiling = last_floor = last_ceiling = NULL; still = NULL;
    width = height = cell_count = dirty_count = glass_count = 0;
    built = ~0u;
}

void RoomGrid_Stop(void)
{
    if (job.active)
    {
        SDL_SetAtomicInt(&cancel, 1);
        end_job();
        SDL_SetAtomicInt(&cancel, 0);
    }
    forget();
}

static int threads_to_use(void)
{
    int cores = SDL_GetNumLogicalCPUCores() / 2;
    return cores < 1 ? 1 : cores > MOST_THREADS ? MOST_THREADS : cores;
}

static void build(void)
{
    forget();
    if (!lock) lock = SDL_CreateMutex();
    if (log_on < 0) log_on = M_CheckParm("-levelsoundlog") > 0;
    int cell_arg = M_CheckParm("-gridcell");
    if (cell_arg && cell_arg + 1 < myargc && atoi(myargv[cell_arg + 1]) >= 16) CELL = (float)atoi(myargv[cell_arg + 1]);
    int rays_arg = M_CheckParm("-roomrays");
    if (rays_arg && rays_arg + 1 < myargc) rays = atoi(myargv[rays_arg + 1]);
    float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
    for (int i = 0; i < numvertexes; ++i)
    {
        float x = vertexes[i].x / (float)FRACUNIT, y = vertexes[i].y / (float)FRACUNIT;
        x0 = fminf(x0, x); y0 = fminf(y0, y); x1 = fmaxf(x1, x); y1 = fmaxf(y1, y);
    }
    if (x1 < x0 || !lock) return;
    origin_x = floorf((x0 - CELL / 2) / CELL) * CELL + CELL / 2;
    origin_y = floorf((y0 - CELL / 2) / CELL) * CELL + CELL / 2;
    width = (int)((x1 - origin_x) / CELL) + 1;
    height = (int)((y1 - origin_y) / CELL) + 1;
    cell_count = width * height;
    BLOCK = (int)(256 / CELL) > 1 ? (int)(256 / CELL) : 1;
    int sectors_n = numsectors > 0 ? numsectors : 1;
    slot = malloc((size_t)cell_count * sizeof(int));
    state = calloc((size_t)cell_count, 1);
    dirty = calloc((size_t)cell_count, 1);
    cell_sector = malloc((size_t)cell_count * sizeof(int));
    settled_floor = malloc(sectors_n * sizeof(fixed_t)); settled_ceiling = malloc(sectors_n * sizeof(fixed_t));
    last_floor = malloc(sectors_n * sizeof(fixed_t)); last_ceiling = malloc(sectors_n * sizeof(fixed_t));
    still = calloc(sectors_n, sizeof(int));
    int blocks_wide = (width + BLOCK - 1) / BLOCK, block_count = blocks_wide * ((height + BLOCK - 1) / BLOCK);
    int *blocks = malloc((size_t)block_count * sizeof(int));
    float *distance = malloc((size_t)block_count * sizeof(float));
    if (!slot || !state || !dirty || !cell_sector || !settled_floor || !settled_ceiling || !last_floor || !last_ceiling
        || !still || !blocks || !distance)
    { free(blocks); free(distance); forget(); return; }
    built = r_levelserial; built_sectors = numsectors;
    for (int s = 0; s < numsectors; ++s)
    {
        settled_floor[s] = last_floor[s] = sectors[s].floorheight;
        settled_ceiling[s] = last_ceiling[s] = sectors[s].ceilingheight;
    }
    /* What rays read must be ready before the workers start. */
    Uint64 prepare = SDL_GetTicksNS();
    Rooms_Build();
    Uint64 rooms_done = SDL_GetTicksNS();
    Room_Prepare();
    if (log_on) fprintf(stderr, "roomgrid: rooms ready in %.0f ms, materials in %.0f ms\n", (rooms_done - prepare) / 1e6, (SDL_GetTicksNS() - rooms_done) / 1e6);
    int inside = 0;
    for (int c = 0; c < cell_count; ++c)
    {
        state[c] = in_subsector(cell_x(c), cell_y(c), &cell_sector[c]) ? WAITING : EMPTY;
        slot[c] = state[c] == WAITING ? inside++ : -1;
    }
    cells = malloc((size_t)(inside > 0 ? inside : 1) * sizeof(*cells));
    if (!cells) { free(blocks); free(distance); forget(); return; }
    /* The blocks with a cell in the level, nearest the listener first. */
    const mobj_t *listener = players[consoleplayer].mo;
    float lx = listener ? listener->x / (float)FRACUNIT : 0, ly = listener ? listener->y / (float)FRACUNIT : 0;
    int count = 0;
    for (int b = 0; b < block_count; ++b)
    {
        int bx = b % blocks_wide, by = b / blocks_wide, any = 0;
        for (int j = by * BLOCK; j < by * BLOCK + BLOCK && j < height && !any; ++j)
            for (int i = bx * BLOCK; i < bx * BLOCK + BLOCK && i < width && !any; ++i) any = state[j * width + i] == WAITING;
        if (!any) continue;
        float d = hypotf(origin_x + (bx + 0.5f) * BLOCK * CELL - lx, origin_y + (by + 0.5f) * BLOCK * CELL - ly);
        int k = count++;
        for (; k > 0 && distance[k - 1] > d; --k) { blocks[k] = blocks[k - 1]; distance[k] = distance[k - 1]; }
        blocks[k] = b; distance[k] = d;
    }
    free(distance);
    start_job(blocks, count, 1, threads_to_use());
}

/* Whether the segment (ax, ay)-(bx, by) crosses (cx, cy)-(dx, dy). */
static int crosses(float ax, float ay, float bx, float by, float cx, float cy, float dx, float dy)
{
    float rx = bx - ax, ry = by - ay, sx = dx - cx, sy = dy - cy, d = rx * sy - ry * sx;
    if (fabsf(d) < 1e-6f) return 0;
    float t = ((cx - ax) * sy - (cy - ay) * sx) / d, u = ((cx - ax) * ry - (cy - ay) * rx) / d;
    return t >= 0 && t <= 1 && u >= 0 && u <= 1;
}
static int reaches(const room_outline_t *o, float most, const line_t *l)
{
    float ax = l->v1->x / (float)FRACUNIT, ay = l->v1->y / (float)FRACUNIT;
    float bx = l->v2->x / (float)FRACUNIT, by = l->v2->y / (float)FRACUNIT;
    /* Too far for any ray: the nearest point of the line. */
    float lx = bx - ax, ly = by - ay, length2 = lx * lx + ly * ly;
    float t = length2 > 0 ? ((o->x - ax) * lx + (o->y - ay) * ly) / length2 : 0;
    t = t < 0 ? 0 : t > 1 ? 1 : t;
    if (hypotf(ax + t * lx - o->x, ay + t * ly - o->y) > most + REACH_MORE) return 0;
    for (int i = 0; i < ROOM_RAYS; ++i)
    {
        float a = i * 2 * PI_F / ROOM_RAYS, r = o->reach[i] + REACH_MORE;
        if (crosses(o->x, o->y, o->x + cosf(a) * r, o->y + sinf(a) * r, ax, ay, bx, by)) return 1;
    }
    return 0;
}
static void mark(int c)
{
    if (!dirty[c]) { dirty[c] = 1; ++dirty_count; }
}
/* The measured cells whose rays (or their space beyond's) reach line. */
static void mark_line(const line_t *l)
{
    SDL_LockMutex(lock);
    for (int c = 0; c < cell_count; ++c)
    {
        if (state[c] != MEASURED || dirty[c]) continue;
        const cell_t *m = &cells[slot[c]];
        if (reaches(&m->outline, m->reach_most, l)) mark(c);
        else if (m->beyond.coupling > 0 && reaches(&m->beyond.outline, MAP_REACH, l)) mark(c);
    }
    SDL_UnlockMutex(lock);
}
static void mark_sector(int s)
{
    for (int c = 0; c < cell_count; ++c)
        if (cell_sector[c] == s && state[c] != EMPTY) mark(c);
    for (int i = 0; i < sectors[s].linecount; ++i) mark_line(sectors[s].lines[i]);
}

/* Doors, lifts and floors that have stopped somewhere new. */
static void watch_sectors(void)
{
    for (int s = 0; s < numsectors; ++s)
    {
        const sector_t *sec = &sectors[s];
        if (sec->floorheight != last_floor[s] || sec->ceilingheight != last_ceiling[s])
        {
            last_floor[s] = sec->floorheight; last_ceiling[s] = sec->ceilingheight; still[s] = 0;
            continue;
        }
        if (++still[s] < SETTLE_TICS || (sec->floorheight == settled_floor[s] && sec->ceilingheight == settled_ceiling[s]))
            continue;
        settled_floor[s] = sec->floorheight; settled_ceiling[s] = sec->ceilingheight;
        mark_sector(s);
    }
}

void RoomGrid_Glass(int line)
{
    if (built != r_levelserial || line < 0 || line >= numlines) return;
    if (glass_count == glass_capacity)
    {
        int capacity = glass_capacity ? glass_capacity * 2 : 64;
        int *grown = realloc(glass_lines, (size_t)capacity * sizeof(int));
        if (!grown) return;
        glass_lines = grown; glass_capacity = capacity;
    }
    glass_lines[glass_count++] = line;
}

static void start_dirty(void)
{
    int *units = malloc((size_t)dirty_count * sizeof(int)), count = 0;
    if (!units) return;
    /* By block, so the worker gathers each block's lines once. */
    for (int b = 0, blocks = ((width + BLOCK - 1) / BLOCK) * ((height + BLOCK - 1) / BLOCK); b < blocks; ++b)
    {
        int bw = (width + BLOCK - 1) / BLOCK, bx = b % bw, by = b / bw;
        for (int j = by * BLOCK; j < by * BLOCK + BLOCK && j < height; ++j)
            for (int i = bx * BLOCK; i < bx * BLOCK + BLOCK && i < width; ++i)
                if (dirty[j * width + i]) { units[count++] = j * width + i; dirty[j * width + i] = 0; }
    }
    dirty_count = 0;
    start_job(units, count, 0, 1);
}

void RoomGrid_Follow(void)
{
    if (gamestate != GS_LEVEL || !numsectors) return;
    if (built != r_levelserial || built_sectors != numsectors) { RoomGrid_Stop(); build(); }
    if (!cells) return;
    if (job.active && SDL_GetAtomicInt(&job.running) == 0) end_job();
    watch_sectors();
    if (job.active) return;
    /* Panes that changed while a job ran (it had the old ones). */
    for (int i = 0; i < glass_count; ++i) mark_line(&lines[glass_lines[i]]);
    glass_count = 0;
    if (dirty_count) start_dirty();
}

double RoomGrid_Finish(void)
{
    Uint64 start = SDL_GetTicksNS();
    RoomGrid_Follow();
    while (job.active)
    {
        SDL_Delay(1);
        RoomGrid_Follow();
    }
    return (SDL_GetTicksNS() - start) / 1e9;
}

/* How far the floor plan reaches from its point toward (dx, dy), between its rays. */
static float reach_toward(const room_outline_t *o, float dx, float dy)
{
    float a = atan2f(dy, dx);
    if (a < 0) a += 2 * PI_F;
    float f = a / (2 * PI_F) * ROOM_RAYS, t = f - floorf(f);
    int i = (int)f % ROOM_RAYS;
    return o->reach[i] * (1 - t) + o->reach[(i + 1) % ROOM_RAYS] * t;
}

int RoomGrid_Listener(float x, float y, float facing, room_t *room, beyond_t *beyond)
{
    if (!cells || built != r_levelserial) return 0;
    float fx = (x - origin_x) / CELL, fy = (y - origin_y) / CELL;
    int i0 = (int)floorf(fx), j0 = (int)floorf(fy);
    float tx = fx - i0, ty = fy - j0;
    int corner[4];
    float weight[4], total = 0;
    SDL_LockMutex(lock);
    for (int k = 0; k < 4; ++k)
    {
        int i = i0 + (k & 1), j = j0 + (k >> 1);
        corner[k] = -1; weight[k] = 0;
        if (i < 0 || j < 0 || i >= width || j >= height || state[j * width + i] != MEASURED) continue;
        int c = j * width + i;
        const room_outline_t *o = &cells[slot[c]].outline;
        float dx = x - o->x, dy = y - o->y;
        /* A cell that can't see the listener is another room's. */
        if (hypotf(dx, dy) >= reach_toward(o, dx, dy)) continue;
        corner[k] = c;
        weight[k] = (k & 1 ? tx : 1 - tx) * (k >> 1 ? ty : 1 - ty);
        total += weight[k];
    }
    if (total < 1e-4f) { SDL_UnlockMutex(lock); return 0; }
    /* Blended field by field (room_t is all floats); the tube's axis by its
       doubled angle (it runs both ways), weighted by how much a tube. */
    enum { FIELDS = sizeof(room_t) / sizeof(float) };
    float sum[FIELDS] = {0}, axis_x = 0, axis_y = 0, coupling = 0;
    int nearest = -1;
    for (int k = 0; k < 4; ++k)
    {
        if (corner[k] < 0) continue;
        const cell_t *m = &cells[slot[corner[k]]];
        const room_t *r = &m->room;
        const float *f = (const float *)r;
        float w = weight[k] / total;
        for (int n = 0; n < FIELDS; ++n) sum[n] += w * f[n];
        float tube = w * (r->tunnel + 1e-3f);
        axis_x += tube * cosf(2 * r->map_axis); axis_y += tube * sinf(2 * r->map_axis);
        coupling += w * m->beyond.coupling;
        if (nearest < 0 || weight[k] > weight[nearest]) nearest = k;
    }
    memcpy(room, sum, sizeof(*room));
    /* The space beyond: the nearest cell's, as open as the cells around find it. */
    *beyond = cells[slot[corner[nearest]]].beyond;
    if (beyond->coupling > 0) beyond->coupling = coupling;
    SDL_UnlockMutex(lock);
    float axis = 0.5f * atan2f(axis_y, axis_x);
    room->map_axis = axis < 0 ? axis + PI_F : axis;
    Room_Face(room, facing);
    Room_Hush(room);
    Room_Face(&beyond->room, facing);
    Room_Hush(&beyond->room);
    return 1;
}
