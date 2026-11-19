/* Performance smoke test: plays through every map of a WAD with the GPU
   renderer, looks around from spots across each map and reports where frames
   get slow and why.

   doom-perf-smoke <iwad> [-maps MAP01,E1M2] [-spacing 512] [-budget 16.7]
                   [-out perf-report] [-strict] [-at x,y[,facing]] [-doors] [-lines 471,12]
                   [-features [-rounds 5]] [engine options]

   -at times just that spot (facing east, north, west or south, or all four),
   for re-checking a slow view the report found; use it with one map.

   -doors instead opens every door that opens by hand (no key) at once and
   times frames for a few seconds of play from the first spot (or -at), while
   the light around the moving doors re-bakes: wall time per frame, so work
   on other threads that slows the game shows too. -lines also sets off the
   walk-over lines given (by number: a lowering wall, a lift), as if the
   player crossed them. The report adds each second's mean frame, so a
   steady slowdown shows apart from a single hitch.

   -features [-rounds N] instead times, at the -at view (each of the four
   facings unless one is given), what each part of the world shader costs:
   the renderer switches one part off at a time (I_Render3DProfileKnob) and
   the report lists what that saves in the pieces that run the shader, the
   opaque world, the masked walls and the sprites, largest first. Appends to
   report.txt. Load the GPU (-width 3840 -height 2160) for steady timings.

   The world stays paused while the view moves from spot to spot, facing the
   four compass directions at each. Every frame waits for the GPU, so CPU and
   GPU times are measured separately; a pipelined game frame costs about the
   larger of the two. At the slowest view of each map the renderer leaves out
   one kind of work at a time (each kind of dynamic light, reflections, bloom,
   fog, mist), so the report can name what the time goes to, and it saves a
   screenshot of that view. Engine options such as -file, -deh, -skill,
   -width, -height, -fullscreen, -flashlight and -graphics pass through; the
   player's own graphics.cfg is used unless -graphics names another. */
#include <ctype.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "doomdef.h"
#include "doomstat.h"
#include "d_main.h"
#include "g_game.h"
#include "i_system.h"
#include "info.h"
#include "m_argv.h"
#include "p_local.h"
#include "r_main.h"
#include "r_sky.h"
#include "r_state.h"
#include "w_wad.h"
#include "../platform/i_render3d.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#define MAX_MAPS 160
#define TIMED_FRAMES 5   /* Single GPU frames spike; slow views are timed again. */
#define TOP_SPOTS 8
/* Pitfall thresholds. */
#define MANY_BLOCKERS 12       /* Wall tests per pixel for one light. */
#define MANY_VIEW_BLOCKERS 64  /* ...summed over the lights of one view. */
#define MANY_LIGHTS 16
#define MANY_TRIANGLES 250000  /* The whole map is drawn every frame. */
#define SLOW_LOAD_MS 3000
#define SLOW_MASKS_MS 2.0

typedef struct { int episode, map; char name[9]; } map_t;
typedef struct { fixed_t x, y; int sector; } spot_t;
typedef struct { int spot, yaw; I_Render3DFrameProfile frame; } sample_t;
typedef struct { const char *name; unsigned skip; double gpu, cpu; } ablation_t;
typedef struct { char name[9]; double worst, median, load; int over, spots; } summary_t;

static const char *kind_names[I_RENDER3D_LIGHT_KINDS] = {
    "flashlight", "weapon flashes", "exploding barrels", "decorations (torches, lamps)",
    "projectiles", "door spill", "glowing textures", "fog glow"};
static const char *facing_names[4] = {"east", "north", "west", "south"};

static map_t maps[MAX_MAPS];
static int map_count, map_index = -1;
static int spacing = 512, strict, skill_given;
static int at_x, at_y, at_yaw = -1, at_set; /* -at: one spot, optionally one facing. */
static double budget = 1000.0 / 60;
static const char *out_dir = "perf-report";
static const char *map_filter;
static FILE *report, *csv;
static summary_t summaries[MAX_MAPS];

static enum { START, LOADING, SETTLING, SAMPLING, CONFIRMING, ABLATING, PIECES, SHOT_PREP, SHOT, DOORS, FEATURES, FINISHED } state = START;
#define DOOR_SECONDS 6
static int doors;             /* -doors */
static int cross_lines[16], cross_count;   /* -lines */
static double door_wall[4096], door_cpu[4096], door_gpu[4096];
static int door_frames, doors_opened;
static uint64_t door_start, door_last;
static unsigned seen_serial, level_serial;
static uint64_t request_time;
static double load_ms;
static int settle_frames;
static spot_t *spots;
static int spot_count;
static sample_t *samples;
static int sample_count;
static int view;              /* SAMPLING: spot * 5 + phase; phase 0 settles. */
static int candidates[TOP_SPOTS], candidate_count, candidate; /* The slowest view of the slowest spots. */
static ablation_t ablations[16];
static int ablation_count, ablation;
static double timed_gpu[TIMED_FRAMES], timed_cpu[TIMED_FRAMES];
static int timed_frame = -1;
/* PIECES: the slowest view's GPU work piece by piece (I_Render3DProfilePieces),
   each piece's median over TIMED_FRAMES frames. */
static double piece_timed[I_RENDER3D_PIECES][TIMED_FRAMES], piece_ms[I_RENDER3D_PIECES];
static int piece_frame = -1, pieces_timed;
static int worst;             /* Index into samples. */
static char shot_path[512];

static void out(const char *format, ...)
{
    va_list args;
    va_start(args, format); vprintf(format, args); va_end(args);
    if (report) { va_start(args, format); vfprintf(report, format, args); va_end(args); }
}
static double cost(const I_Render3DFrameProfile *f) { return f->cpuMs > f->gpuMs ? f->cpuMs : f->gpuMs; }
static int compare_doubles(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return x < y ? -1 : x > y;
}
static double median(double *values, int count)
{
    qsort(values, count, sizeof(double), compare_doubles);
    return values[count / 2];
}
/* Times the view over TIMED_FRAMES frames after one that settles; true
   with the medians once done. */
static int time_view(const I_Render3DFrameProfile *frame, double *gpu, double *cpu)
{
    if (timed_frame >= 0) { timed_gpu[timed_frame] = frame->gpuMs; timed_cpu[timed_frame] = frame->cpuMs; }
    if (++timed_frame < TIMED_FRAMES) return 0;
    *gpu = median(timed_gpu, TIMED_FRAMES); *cpu = median(timed_cpu, TIMED_FRAMES);
    timed_frame = -1;
    return 1;
}
static const char *thing_name(int type, char *buffer, size_t size)
{
    if (type < 0 || type >= NUMMOBJTYPES) return "";
    const char *sprite = sprnames[states[mobjinfo[type].spawnstate].sprite];
    snprintf(buffer, size, " %.4s #%d", sprite ? sprite : "????", mobjinfo[type].doomednum);
    return buffer;
}

static void find_maps(void)
{
    char wanted[512] = "";
    if (map_filter)
    {
        snprintf(wanted, sizeof(wanted), ",%s,", map_filter);
        for (char *c = wanted; *c; ++c) *c = (char)toupper((unsigned char)*c);
    }
    for (int e = 1; e <= (gamemode == commercial ? 1 : 9); ++e) /* SIGIL adds E5, SIGIL II E6. */
        for (int m = 1; m <= (gamemode == commercial ? 32 : 9); ++m)
        {
            char name[9], key[12];
            if (gamemode == commercial) snprintf(name, sizeof(name), "MAP%02d", m);
            else snprintf(name, sizeof(name), "E%dM%d", e, m);
            snprintf(key, sizeof(key), ",%s,", name);
            if (W_CheckNumForName(name) < 0 || (map_filter && !strstr(wanted, key))) continue;
            if (map_count == MAX_MAPS) break;
            maps[map_count].episode = e; maps[map_count].map = m;
            strcpy(maps[map_count++].name, name);
        }
    if (!map_count) I_Error("Performance smoke: no maps to test");
}

/* Whether the player could stand there, clear of walls and solid things. */
static boolean fits(fixed_t x, fixed_t y)
{
    mobj_t *mo = players[consoleplayer].mo;
    int flags = mo->flags;
    mo->flags &= ~MF_PICKUP; /* Checking must not collect items. */
    boolean clear = P_CheckPosition(mo, x, y);
    mo->flags = flags;
    return clear;
}

/* One spot per grid cell: the subsector center nearest the cell's middle
   where the player fits. */
static void find_spots(void)
{
    fixed_t low[2] = {INT32_MAX, INT32_MAX}, high[2] = {INT32_MIN, INT32_MIN};
    for (int i = 0; i < numvertexes; ++i)
    {
        if (vertexes[i].x < low[0]) low[0] = vertexes[i].x;
        if (vertexes[i].y < low[1]) low[1] = vertexes[i].y;
        if (vertexes[i].x > high[0]) high[0] = vertexes[i].x;
        if (vertexes[i].y > high[1]) high[1] = vertexes[i].y;
    }
    int columns = (int)(((int64_t)high[0] - low[0]) / FRACUNIT / spacing) + 1;
    int rows = (int)(((int64_t)high[1] - low[1]) / FRACUNIT / spacing) + 1;
    double *nearest = malloc(sizeof(double) * columns * rows);
    free(spots);
    spots = malloc(sizeof(spot_t) * columns * rows);
    if (!nearest || !spots) I_Error("Performance smoke: out of memory");
    for (int i = 0; i < columns * rows; ++i) nearest[i] = -1;
    for (int i = 0; i < numsubsectors; ++i)
    {
        const sector_t *sector = subsectors[i].sector;
        if (sector->ceilingheight - sector->floorheight < 56 * FRACUNIT || sector->floorpic == skyflatnum) continue;
        double x = 0, y = 0;
        int count = subsectors[i].numlines;
        if (count < 1) continue;
        for (int n = 0; n < count; ++n)
        {
            const seg_t *seg = &segs[subsectors[i].firstline + n];
            x += (double)seg->v1->x + seg->v2->x; y += (double)seg->v1->y + seg->v2->y;
        }
        fixed_t px = (fixed_t)(x / (2 * count)), py = (fixed_t)(y / (2 * count));
        if (R_PointInSubsector(px, py) != &subsectors[i] || !fits(px, py)) continue;
        double cx = ((double)px - low[0]) / FRACUNIT / spacing, cy = ((double)py - low[1]) / FRACUNIT / spacing;
        int cell = (int)cy * columns + (int)cx;
        double distance = hypot(cx - floor(cx) - 0.5, cy - floor(cy) - 0.5);
        if (nearest[cell] < 0 || distance < nearest[cell])
        {
            nearest[cell] = distance;
            spots[cell].x = px; spots[cell].y = py; spots[cell].sector = (int)(sector - sectors);
        }
    }
    spot_count = 0;
    for (int i = 0; i < columns * rows; ++i)
        if (nearest[i] >= 0) spots[spot_count++] = spots[i];
    if (at_set)
    {
        spots[0].x = at_x * FRACUNIT; spots[0].y = at_y * FRACUNIT;
        spots[0].sector = (int)(R_PointInSubsector(spots[0].x, spots[0].y)->sector - sectors);
        spot_count = 1;
    }
    free(nearest);
    free(samples);
    samples = malloc(sizeof(sample_t) * (spot_count * 4 + 1));
    if (!samples) I_Error("Performance smoke: out of memory");
    sample_count = 0;
}

static void place(const spot_t *spot, int yaw)
{
    player_t *player = &players[consoleplayer];
    mobj_t *mo = player->mo;
    P_UnsetThingPosition(mo);
    mo->x = spot->x; mo->y = spot->y;
    P_SetThingPosition(mo);
    mo->floorz = mo->subsector->sector->floorheight;
    mo->ceilingz = mo->subsector->sector->ceilingheight;
    mo->z = mo->floorz; mo->momx = mo->momy = mo->momz = 0;
    if (at_yaw >= 0) yaw = at_yaw;
    mo->angle = (angle_t)yaw * ANG90;
    player->viewheight = VIEWHEIGHT; player->deltaviewheight = 0;
    player->viewz = mo->z + VIEWHEIGHT;
    if (player->viewz > mo->ceilingz - 4 * FRACUNIT) player->viewz = mo->ceilingz - 4 * FRACUNIT;
}

/* -doors: every door the player could open by hand starts opening. */
static void open_doors(void)
{
    mobj_t *mo = players[consoleplayer].mo;
    players[consoleplayer].cheats |= CF_GODMODE;
    doors_opened = 0;
    for (int i = 0; i < numlines; ++i)
    {
        int special = lines[i].special;
        if ((special == 1 || special == 31 || special == 117 || special == 118) && lines[i].backsector)
            doors_opened += P_UseSpecialLine(mo, &lines[i], 0) ? 1 : 0;
    }
    for (int k = 0; k < cross_count; ++k)
        if (cross_lines[k] >= 0 && cross_lines[k] < numlines && lines[cross_lines[k]].special)
        {
            P_CrossSpecialLine(cross_lines[k], 0, mo);
            ++doors_opened;
        }
    paused = false;
    door_frames = 0;
    door_start = door_last = SDL_GetTicksNS();
}

static void report_doors(void)
{
    if (!door_frames) { out("== %s: no frames timed with doors moving\n", maps[map_index].name); return; }
    double total = 0, worst_wall = 0, second = 0;
    int in_second = 0;
    out("== %s doors, each second's mean frame:", maps[map_index].name);
    for (int i = 0; i < door_frames; ++i)
    {
        total += door_wall[i]; if (door_wall[i] > worst_wall) worst_wall = door_wall[i];
        second += door_wall[i]; ++in_second;
        if (second >= 1000 || i == door_frames - 1) { out(" %.1f", second / in_second); second = 0; in_second = 0; }
    }
    out(" ms\n");
    double mean = total / door_frames, cpu = median(door_cpu, door_frames), gpu = median(door_gpu, door_frames);
    double wall95 = (qsort(door_wall, door_frames, sizeof(double), compare_doubles), door_wall[door_frames * 95 / 100]);
    out("== %s doors (loaded and baked in %.1f s): %d opened, %d frames in %d s: %.1f FPS, frame %.1f ms mean, "
        "%.1f ms 95%%, %.1f ms worst; CPU median %.1f ms, GPU median %.1f ms\n", maps[map_index].name, load_ms / 1000,
        doors_opened, door_frames, DOOR_SECONDS, 1000 / mean, mean, wall95, worst_wall, cpu, gpu);
}

/* -features: the world shader's parts by what switching each off saves. GPU
   times drift as the clocks change, so no two numbers taken far apart are
   compared: a round runs baseline, knob 1, baseline, knob 2, ... knob K,
   baseline, and the rounds repeat. A knob's saving is the median of the
   baseline frames either side of it, over all rounds, less the median of its
   own; a saving of at most twice those baseline frames' median absolute
   deviation is within noise. Every measured frame follows an unmeasured one drawn the
   same way, so pipelines and caches have settled on it. */
#define FEATURE_PIECES 3
#define FEATURE_SETTLE 20      /* Pieced frames at the view before the first round. */
#define MAX_ROUNDS 64
static const int feature_piece[FEATURE_PIECES] = {I_RENDER3D_PIECE_OPAQUE, I_RENDER3D_PIECE_MASKED, I_RENDER3D_PIECE_CUTOUTS};
static int features, feature_rounds = 5;
static int feature_knobs;      /* Knobs but knob 0, which is none. */
static int feature_facing[4], feature_facings, feature_facing_at;
static int feature_round, feature_step, feature_warm, feature_settle, feature_legend;
static double *feature_ms;     /* [round][step][piece]: even steps the baseline, odd ones knob (step + 1) / 2. */

static int feature_steps(void) { return 2 * feature_knobs + 1; }
static double *feature_at(int round, int step) { return feature_ms + ((size_t)round * feature_steps() + step) * FEATURE_PIECES; }
static int feature_knob_of(int step) { return step % 2 ? (step + 1) / 2 : 0; }
/* The median absolute deviation from middle; reorders values. */
static double deviation(double *values, int count, double middle)
{
    for (int i = 0; i < count; ++i) values[i] = fabs(values[i] - middle);
    return median(values, count);
}

/* The facing now timed, from the first round on. */
static void begin_features(void)
{
    feature_round = feature_step = feature_settle = 0;
    feature_warm = 1;
    I_Render3DProfileKnob(0);
    I_Render3DProfileTeleported();
    place(&spots[0], feature_facing[feature_facing_at]);
}

static void start_features(void)
{
    feature_knobs = I_Render3DKnobCount() - 1;
    feature_facings = 0;
    if (at_yaw >= 0) feature_facing[feature_facings++] = at_yaw;
    else for (int k = 0; k < 4; ++k) feature_facing[feature_facings++] = k;
    feature_facing_at = 0;
    free(feature_ms);
    feature_ms = malloc(sizeof(double) * FEATURE_PIECES * feature_steps() * feature_rounds);
    if (!feature_ms || feature_knobs < 1) I_Error("Performance smoke: no feature knobs to time");
    I_Render3DProfilePieces(1);
    begin_features();
}

/* A piece's time with a mark when the piece did not run (0 in the baseline). */
static void feature_cell(double saved, int noisy, double base)
{
    if (base <= 0) out("  %10s ", "-");
    else out("  %+7.3f ms%s", saved, noisy ? "~" : " ");
}

static void report_features(void)
{
    int rounds = feature_rounds, knobs = feature_knobs, baselines = (knobs + 1) * rounds;
    double around[2 * MAX_ROUNDS], own[MAX_ROUNDS];
    double *all = malloc(sizeof(double) * baselines);
    double *saved = malloc(sizeof(double) * FEATURE_PIECES * (knobs + 1));
    double *left = malloc(sizeof(double) * FEATURE_PIECES * (knobs + 1));
    int *noisy = malloc(sizeof(int) * FEATURE_PIECES * (knobs + 1));
    int *order = malloc(sizeof(int) * knobs);
    double base[FEATURE_PIECES], base_spread[FEATURE_PIECES];
    if (!all || !saved || !left || !noisy || !order) I_Error("Performance smoke: out of memory");
    const spot_t *spot = &spots[0];
    int facing = feature_facing[feature_facing_at];

    if (!feature_legend)
    {
        feature_legend = 1;
        out("\n== World shader feature knobs (each clears, for its frames only):\n");
        for (int k = 1; k <= knobs; ++k) out("   %-34s %s\n", I_Render3DKnobName(k), I_Render3DKnobClears(k));
    }
    for (int p = 0; p < FEATURE_PIECES; ++p)
    {
        int n = 0;
        for (int r = 0; r < rounds; ++r)
            for (int s = 0; s < feature_steps(); s += 2) all[n++] = feature_at(r, s)[p];
        base[p] = median(all, n);
        base_spread[p] = deviation(all, n, base[p]);
        for (int k = 1; k <= knobs; ++k)
        {
            for (int r = 0; r < rounds; ++r)
            {
                around[2 * r] = feature_at(r, 2 * k - 2)[p];
                around[2 * r + 1] = feature_at(r, 2 * k)[p];
                own[r] = feature_at(r, 2 * k - 1)[p];
            }
            /* Round by round, the knob against the two baseline frames right
               beside it: the GPU's time drifts (its clock steps up and down),
               and medians taken apart put that drift into the saving. */
            double mine = median(own, rounds);
            for (int r = 0; r < rounds; ++r) around[r] = (around[2 * r] + around[2 * r + 1]) / 2 - feature_at(r, 2 * k - 1)[p];
            double gain = median(around, rounds), spread = deviation(around, rounds, gain);
            saved[k * FEATURE_PIECES + p] = gain;
            left[k * FEATURE_PIECES + p] = mine;
            noisy[k * FEATURE_PIECES + p] = fabs(gain) <= 2 * spread;
        }
    }
    /* Single knobs by what they save in the opaque world, largest first; the
       aggregates (names in parentheses) after them. */
    int singles = 0;
    for (int k = 1; k <= knobs; ++k) if (I_Render3DKnobName(k)[0] != '(') order[singles++] = k;
    for (int i = 0; i < singles; ++i)
        for (int j = i + 1; j < singles; ++j)
            if (saved[order[j] * FEATURE_PIECES] > saved[order[i] * FEATURE_PIECES]) { int t = order[i]; order[i] = order[j]; order[j] = t; }
    int listed = singles;
    for (int k = 1; k <= knobs; ++k) if (I_Render3DKnobName(k)[0] == '(') order[listed++] = k;

    out("\n== %s world shader features at (%d, %d) facing %s: %d rounds, each knob between two baseline frames\n",
        maps[map_index].name, spot->x >> FRACBITS, spot->y >> FRACBITS, facing_names[facing], rounds);
    out("   Saved by switching one off (median over the rounds against the baseline frames beside it;\n"
        "   ~ within noise: at most twice their median absolute deviation; - the piece drew nothing):\n");
    out("   %-34s  %-18s  %-11s  %-11s\n", "Feature", "opaque world, %", "masked walls", "sprites");
    for (int i = 0; i < listed; ++i)
    {
        int k = order[i];
        if (i == singles) out("   Together:\n");
        const double *s = &saved[k * FEATURE_PIECES];
        const int *q = &noisy[k * FEATURE_PIECES];
        out("   %-34s", I_Render3DKnobName(k));
        feature_cell(s[0], q[0], base[0]);
        if (base[0] > 0) out(" %5.1f%%", 100 * s[0] / base[0]); else out(" %6s", "");
        feature_cell(s[1], q[1], base[1]);
        feature_cell(s[2], q[2], base[2]);
        out("\n");
    }
    out("   Baseline (median of %d frames, median absolute deviation):", baselines);
    for (int p = 0; p < FEATURE_PIECES; ++p)
        out("%s %s %.3f ms (%.3f)", p ? "," : "", I_Render3DPieceName(feature_piece[p]), base[p], base_spread[p]);
    out("\n   Everything off leaves (the shader's base: texture reads, baked light lookups):");
    for (int p = 0; p < FEATURE_PIECES; ++p)
    {
        double rest = left[knobs * FEATURE_PIECES + p];
        out("%s %s %.3f ms", p ? "," : "", I_Render3DPieceName(feature_piece[p]), rest);
        if (base[p] > 0) out(" (%.0f%%)", 100 * rest / base[p]);
    }
    out("\n");
    if (report) fflush(report);
    free(all); free(saved); free(left); free(noisy); free(order);
}

static void request_map(void)
{
    const map_t *map = &maps[map_index];
    printf("Loading %s (%d of %d)...\n", map->name, map_index + 1, map_count);
    fflush(stdout);
    level_serial = r_levelserial;
    request_time = SDL_GetTicksNS();
    G_DeferedInitNew(skill_given ? gameskill : sk_hard, map->episode, map->map);
    state = LOADING;
}

static void record(int spot, int yaw, const I_Render3DFrameProfile *frame)
{
    sample_t *sample = &samples[sample_count++];
    sample->spot = spot; sample->yaw = yaw; sample->frame = *frame;
    int most = 0;
    for (int i = 0; i < frame->lights; ++i)
        if (frame->light[i].blockers > most) most = frame->light[i].blockers;
    const spot_t *s = &spots[spot];
    fprintf(csv, "%s,%d,%d,%d,%d,%s,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%d,%d,%d,%d,%d,%d,%d\n",
            maps[map_index].name, spot, s->x >> FRACBITS, s->y >> FRACBITS, s->sector, facing_names[yaw],
            cost(frame), frame->cpuMs, frame->gpuMs, frame->prepareMs, frame->geometryMs, frame->lightsMs,
            frame->spritesMs, frame->masksMs, frame->lights, frame->blockers, most, frame->triangles,
            frame->litTriangles, frame->lightTriangles, frame->reflection);
}

/* The slowest view of each of the slowest spots, by their single frames. */
static void pick_candidates(void)
{
    candidate_count = 0;
    while (candidate_count < TOP_SPOTS)
    {
        int best = -1;
        for (int i = 0; i < sample_count; ++i)
        {
            int taken = 0;
            for (int n = 0; n < candidate_count; ++n) taken |= samples[i].spot == samples[candidates[n]].spot;
            if (!taken && (best < 0 || cost(&samples[i].frame) > cost(&samples[best].frame))) best = i;
        }
        if (best < 0) break;
        candidates[candidate_count++] = best;
    }
    candidate = 0;
}

static void plan_ablations(void)
{
    const I_Render3DFrameProfile *f = &samples[worst].frame;
    unsigned kinds = 0;
    for (int i = 0; i < f->lights; ++i) kinds |= 1u << f->light[i].kind;
    ablation_count = 0;
    ablations[ablation_count++] = (ablation_t){"(as played)", 0};
    if (kinds) ablations[ablation_count++] = (ablation_t){"all dynamic lights", I_RENDER3D_SKIP_LIGHTS};
    if (kinds & (kinds - 1))
        for (int k = 0; k < I_RENDER3D_LIGHT_KINDS; ++k)
            if (kinds & (1u << k)) ablations[ablation_count++] = (ablation_t){kind_names[k], 1u << k};
    if (f->reflection) ablations[ablation_count++] = (ablation_t){"liquid reflections", I_RENDER3D_SKIP_REFLECTIONS};
    ablations[ablation_count++] = (ablation_t){"bloom", I_RENDER3D_SKIP_BLOOM};
    ablations[ablation_count++] = (ablation_t){"fog", I_RENDER3D_SKIP_FOG};
    ablations[ablation_count++] = (ablation_t){"mist and sunbeams", I_RENDER3D_SKIP_MIST};
    ablation = 0;
    I_Render3DProfile(1, ablations[0].skip);
}

static void describe_lights(const I_Render3DFrameProfile *f, const char *indent)
{
    int done[64] = {0};
    for (int i = 0; i < f->lights; ++i)
    {
        if (done[i]) continue;
        int count = 0, least = INT32_MAX, most = 0, triangles = 0;
        float radius = 0, per_pixel = 0;
        for (int j = i; j < f->lights; ++j)
        {
            const I_Render3DLightProfile *l = &f->light[j];
            if (done[j] || l->kind != f->light[i].kind || l->thing != f->light[i].thing) continue;
            done[j] = 1; ++count; triangles += l->triangles;
            if (l->blockers < least) least = l->blockers;
            if (l->blockers > most) most = l->blockers;
            if (l->radius > radius) radius = l->radius;
            if (l->perPixel > per_pixel) per_pixel = l->perPixel;
        }
        char name[32];
        const char *kind = f->light[i].kind < I_RENDER3D_LIGHT_KINDS ? kind_names[f->light[i].kind] : "?";
        if (least == most)
            out("%s%2d x %s%s: radius %.0f, %d wall blockers each (up to %.1f per pixel), %d triangles in reach\n", indent, count, kind,
                thing_name(f->light[i].thing, name, sizeof(name)), radius, most, per_pixel, triangles);
        else
            out("%s%2d x %s%s: radius up to %.0f, %d-%d wall blockers each (up to %.1f per pixel), %d triangles in reach\n", indent, count, kind,
                thing_name(f->light[i].thing, name, sizeof(name)), radius, least, most, per_pixel, triangles);
    }
}

static void spot_line(int sample, const char *indent)
{
    const sample_t *s = &samples[sample];
    const spot_t *spot = &spots[s->spot];
    const I_Render3DFrameProfile *f = &s->frame;
    out("%s%6.1f ms (CPU %.1f, GPU %.1f) at (%d, %d) sector %d facing %s: %d lights, %d blockers\n", indent,
        cost(f), f->cpuMs, f->gpuMs, spot->x >> FRACBITS, spot->y >> FRACBITS, spot->sector,
        facing_names[s->yaw], f->lights, f->blockers);
}

static void report_map(void)
{
    const map_t *map = &maps[map_index];
    summary_t *summary = &summaries[map_index];
    strcpy(summary->name, map->name);
    summary->load = load_ms; summary->spots = spot_count;
    int things = 0;
    for (int i = 0; i < numsectors; ++i)
        for (mobj_t *thing = sectors[i].thinglist; thing; thing = thing->snext) ++things;
    out("\n== %s: %d spots, %d lines, %d sectors, %d things, loaded and baked in %.1f s\n",
        map->name, spot_count, numlines, numsectors, things, load_ms / 1000);
    if (!sample_count) { out("   No spots with room for the player.\n"); return; }
    double *costs = malloc(sizeof(double) * sample_count);
    for (int i = 0; i < sample_count; ++i)
    {
        costs[i] = cost(&samples[i].frame);
        if (costs[i] > budget) ++summary->over;
    }
    qsort(costs, sample_count, sizeof(double), compare_doubles);
    summary->median = costs[sample_count / 2];
    summary->worst = cost(&samples[worst].frame);
    out("   One frame per view: median %.1f ms, 95%% %.1f ms; %d of %d views over the %.1f ms budget\n",
        summary->median, costs[(int)(sample_count * 0.95)], summary->over, sample_count, budget);
    free(costs);

    const I_Render3DFrameProfile *f = &samples[worst].frame;
    out("   Slowest view (median of %d frames), %s-bound:\n", TIMED_FRAMES, f->cpuMs > f->gpuMs ? "CPU" : "GPU");
    spot_line(worst, "     ");
    out("     CPU: scene %.1f ms (geometry %.1f: light wall blockers %.1f, sprite lighting %.1f; light masks %.1f)\n",
        f->prepareMs, f->geometryMs, f->lightsMs, f->spritesMs, f->masksMs);
    out("     %d world triangles, %d lit by dynamic lights (%d light-triangle pairs)%s\n", f->triangles,
        f->litTriangles, f->lightTriangles, f->reflection ? ", liquid reflection pass" : "");
    if (f->lights) { out("     Lights:\n"); describe_lights(f, "       "); }
    out("     Saved by leaving out (GPU / CPU, median of %d frames):\n", TIMED_FRAMES);
    double base_gpu = ablations[0].gpu, base_cpu = ablations[0].cpu, light_saving = 0;
    out("       %-30s %6.1f / %5.1f ms\n", ablations[0].name, base_gpu, base_cpu);
    for (int i = 1; i < ablation_count; ++i)
    {
        int nested = ablations[i].skip && ablations[i].skip < I_RENDER3D_SKIP_LIGHTS;
        out("       %s%-*s %+6.1f / %+5.1f ms\n", nested ? "  " : "", nested ? 28 : 30, ablations[i].name,
            ablations[i].gpu - base_gpu, ablations[i].cpu - base_cpu);
        if (ablations[i].skip == I_RENDER3D_SKIP_LIGHTS) light_saving = base_gpu - ablations[i].gpu;
    }
    if (pieces_timed)
    {
        /* Each piece waited for alone, so their sum runs over the frame's GPU time above. */
        double sum = 0;
        out("     GPU by piece (each submitted and waited for alone, median of %d frames):\n", TIMED_FRAMES);
        for (int i = 0; i < I_RENDER3D_PIECES; ++i)
        {
            if (piece_ms[i] <= 0) continue;
            out("       %-30s %6.2f ms\n", I_Render3DPieceName(i), piece_ms[i]);
            sum += piece_ms[i];
        }
        out("       %-30s %6.2f ms\n", "(sum of the medians)", sum);
        pieces_timed = 0;
    }
    out("     Screenshot: %s\n", shot_path);

    /* Pitfalls over the whole map. */
    int warnings = 0;
#define WARN(...) do { out(warnings++ ? "     ! " : "   Pitfalls:\n     ! "); out(__VA_ARGS__); } while (0)
    if (summary->worst > budget)
        WARN("over budget: %.1f ms at the slowest view\n", summary->worst);
    if (light_saving > 2 && light_saving > 0.4 * base_gpu)
        WARN("dynamic lights take %.0f%% of the GPU frame there (%.1f of %.1f ms)\n", 100 * light_saving / base_gpu, light_saving, base_gpu);
    for (int i = 1; i < ablation_count; ++i)
        if (!(ablations[i].skip & I_RENDER3D_SKIP_LIGHTS) && base_gpu - ablations[i].gpu > 0.25 * base_gpu && base_gpu - ablations[i].gpu > 2)
            WARN("%s cost %.1f ms of GPU there\n", ablations[i].name, base_gpu - ablations[i].gpu);
    if (f->cpuMs > budget)
        WARN("CPU-bound: building the frame takes %.1f ms (%s)\n", f->cpuMs,
             f->spritesMs > f->lightsMs && f->spritesMs > f->masksMs ? "mostly sprite lighting: things x lights x blockers" :
             f->lightsMs > f->masksMs ? "mostly finding the wall blockers of each light" : "mostly light masks: triangles x lights");
    if (f->masksMs > SLOW_MASKS_MS) WARN("light masks take %.1f ms on the CPU (%d triangles x %d lights)\n", f->masksMs, f->triangles, f->lights);
    if (f->triangles > MANY_TRIANGLES) WARN("%d world triangles: the whole map is drawn every frame\n", f->triangles);
    if (load_ms > SLOW_LOAD_MS) WARN("level load and bakes take %.1f s\n", load_ms / 1000);
    int busiest = -1, crowded = -1, most_lights = 0, heavy = -1, heavy_light = 0;
    float most = 0;
    for (int i = 0; i < sample_count; ++i)
    {
        const I_Render3DFrameProfile *s = &samples[i].frame;
        float per_pixel = 0;
        for (int n = 0; n < s->lights; ++n) per_pixel += s->light[n].perPixel;
        if (per_pixel > most) { most = per_pixel; busiest = i; }
        if (s->lights > most_lights) { most_lights = s->lights; crowded = i; }
        for (int n = 0; n < s->lights; ++n)
            if (s->light[n].perPixel > (heavy < 0 ? 0 : samples[heavy].frame.light[heavy_light].perPixel)) { heavy = i; heavy_light = n; }
    }
    if (heavy >= 0 && samples[heavy].frame.light[heavy_light].perPixel >= MANY_BLOCKERS)
    {
        const I_Render3DLightProfile *l = &samples[heavy].frame.light[heavy_light];
        char name[32];
        WARN("a %s%s light at (%.0f, %.0f) tests %.1f of its %d wall blockers in every pixel it reaches (radius %.0f)\n",
             kind_names[l->kind], thing_name(l->thing, name, sizeof(name)), l->x, l->y, l->perPixel, l->blockers, l->radius);
    }
    if (busiest >= 0 && most >= MANY_VIEW_BLOCKERS)
    {
        WARN("a pixel lit by all the lights of one view tests %.0f wall blockers:\n", most);
        spot_line(busiest, "         ");
        describe_lights(&samples[busiest].frame, "         ");
    }
    if (crowded >= 0 && most_lights >= MANY_LIGHTS && samples[crowded].spot != samples[worst].spot &&
        (busiest < 0 || samples[crowded].spot != samples[busiest].spot))
    {
        WARN("%d dynamic lights at once:\n", most_lights);
        spot_line(crowded, "         ");
    }
    if (!warnings) out("   No pitfalls found.\n");
#undef WARN

    /* The other timed spots, slowest first. */
    if (candidate_count > 1) out("   Other slow spots (median of %d frames):\n", TIMED_FRAMES);
    for (int i = 0; i < candidate_count; ++i)
        for (int j = i + 1; j < candidate_count; ++j)
            if (cost(&samples[candidates[j]].frame) > cost(&samples[candidates[i]].frame))
            { int t = candidates[i]; candidates[i] = candidates[j]; candidates[j] = t; }
    for (int i = 0; i < candidate_count; ++i)
        if (candidates[i] != worst) spot_line(candidates[i], "     ");
    fflush(report); fflush(csv);
}

static void report_summary(void)
{
    int over = 0;
    out("\n== Summary, slowest first (budget %.1f ms)\n   Map       Worst  Median  Views over  Load\n", budget);
    int order[MAX_MAPS];
    for (int i = 0; i < map_count; ++i) order[i] = i;
    for (int i = 0; i < map_count; ++i)
        for (int j = i + 1; j < map_count; ++j)
            if (summaries[order[j]].worst > summaries[order[i]].worst) { int t = order[i]; order[i] = order[j]; order[j] = t; }
    for (int i = 0; i < map_count; ++i)
    {
        const summary_t *s = &summaries[order[i]];
        out("   %-8s %6.1f  %6.1f  %10d  %4.1f s%s\n", s->name, s->worst, s->median, s->over, s->load / 1000,
            s->worst > budget ? "  !" : "");
        over += s->worst > budget;
    }
    out("\n%d of %d maps have views over budget. Report: %s/report.txt, every view: %s/views.csv\n",
        over, map_count, out_dir, out_dir);
    fclose(report); fclose(csv); report = csv = NULL;
    if (strict && over) I_Error("Performance smoke: %d maps over budget", over);
}

void I_TestFrame(void)
{
    if (state == FINISHED) return;
    const I_Render3DFrameProfile *frame = I_Render3DLastFrame();
    int fresh = frame->serial != seen_serial && frame->world && frame->gpuMs > 0;
    seen_serial = frame->serial;
    switch (state)
    {
    case START:
        if (gamestate != GS_LEVEL || !players[consoleplayer].mo) return;
        if (!I_Render3DIsAccelerated()) I_Error("Performance smoke: the GPU renderer is off");
        find_maps();
        SDL_CreateDirectory(out_dir);
        char path[512];
        snprintf(path, sizeof(path), "%s/report.txt", out_dir);
        /* -features adds to the report and leaves views.csv alone: it times no views. */
        report = fopen(path, features ? "a" : "w");
        if (!features)
        {
            snprintf(path, sizeof(path), "%s/views.csv", out_dir);
            csv = fopen(path, "w");
        }
        if (!report || (!features && !csv)) I_Error("Performance smoke: cannot write to %s", out_dir);
        if (csv)
            fputs("map,spot,x,y,sector,facing,frame_ms,cpu_ms,gpu_ms,scene_ms,geometry_ms,light_blockers_ms,"
                  "sprite_light_ms,light_masks_ms,lights,blockers,most_light_blockers,triangles,lit_triangles,"
                  "light_triangles,reflection\n", csv);
        {
            int count = 0, w = 0, h = 0;
            SDL_Window **windows = SDL_GetWindows(&count);
            if (windows && count) SDL_GetWindowSizeInPixels(windows[0], &w, &h);
            SDL_free(windows);
            int p = M_CheckParm("-graphics");
            out("DOOM performance smoke test: %d maps, window %dx%d pixels, spots every %d units, budget %.1f ms\n",
                map_count, w, h, spacing, budget);
            out("Graphics settings: %s\n", p && p + 1 < myargc ? myargv[p + 1] : "defaults (no graphics.cfg)");
            out("Each frame waits for the GPU, so CPU and GPU times are separate; a played frame costs about the larger.\n");
        }
        I_Render3DProfile(1, 0);
        map_index = 0;
        request_map();
        return;
    case LOADING:
        if (gameaction != ga_nothing || gamestate != GS_LEVEL || r_levelserial == level_serial || !players[consoleplayer].mo)
            return;
        paused = true; /* G_InitNew unpauses. */
        settle_frames = 0;
        state = SETTLING;
        return;
    case SETTLING:
        /* The first frames bake the level's light. */
        if (fresh && ++settle_frames == 3)
        {
            load_ms = (SDL_GetTicksNS() - request_time) / 1e6;
            find_spots();
            if (!spot_count) { worst = 0; report_map(); goto next_map; }
            if (features) { start_features(); state = FEATURES; return; }
            view = 0;
            state = doors ? DOORS : SAMPLING;
            I_Render3DProfileTeleported();
            place(&spots[0], 0);
            if (doors) open_doors();
        }
        return;
    case DOORS:
    {
        place(&spots[0], 0);
        uint64_t now = SDL_GetTicksNS();
        if (fresh && door_frames < 4096)
        {
            door_wall[door_frames] = (now - door_last) / 1e6;
            door_cpu[door_frames] = frame->cpuMs; door_gpu[door_frames] = frame->gpuMs;
            ++door_frames;
        }
        door_last = now;
        if (now - door_start < (uint64_t)DOOR_SECONDS * 1000000000u) return;
        paused = true;
        report_doors();
        if (++map_index < map_count) { request_map(); return; }
        fclose(report); fclose(csv); report = csv = NULL;
        state = FINISHED;
        I_Render3DProfile(0, 0);
        I_Quit();
        return;
    }
    case SAMPLING:
        if (!fresh) { place(&spots[view / 5], view % 5 ? view % 5 - 1 : 0); return; }
        if (view % 5) record(view / 5, view % 5 - 1, frame);
        if (++view == spot_count * 5)
        {
            pick_candidates();
            state = CONFIRMING;
            I_Render3DProfileTeleported();
            place(&spots[samples[candidates[0]].spot], samples[candidates[0]].yaw);
            printf("  %d spots, %d views; timing the slowest again\n", spot_count, sample_count);
            fflush(stdout);
            return;
        }
        if (view % 5 == 0)
        {
            I_Render3DProfileTeleported();
            if ((view / 5) % 20 == 0) { printf("  %d of %d spots\r", view / 5, spot_count); fflush(stdout); }
        }
        place(&spots[view / 5], view % 5 ? view % 5 - 1 : 0);
        return;
    case CONFIRMING:
    {
        sample_t *sample = &samples[candidates[candidate]];
        place(&spots[sample->spot], sample->yaw);
        if (!fresh || !time_view(frame, &sample->frame.gpuMs, &sample->frame.cpuMs)) return;
        if (++candidate < candidate_count)
        {
            I_Render3DProfileTeleported();
            place(&spots[samples[candidates[candidate]].spot], samples[candidates[candidate]].yaw);
            return;
        }
        worst = candidates[0];
        for (int i = 1; i < candidate_count; ++i)
            if (cost(&samples[candidates[i]].frame) > cost(&samples[worst].frame)) worst = candidates[i];
        plan_ablations();
        state = ABLATING;
        I_Render3DProfileTeleported();
        place(&spots[samples[worst].spot], samples[worst].yaw);
        return;
    }
    case ABLATING:
        place(&spots[samples[worst].spot], samples[worst].yaw);
        if (!fresh || !time_view(frame, &ablations[ablation].gpu, &ablations[ablation].cpu)) return;
        if (++ablation < ablation_count) { I_Render3DProfile(1, ablations[ablation].skip); return; }
        I_Render3DProfile(1, 0);
        I_Render3DProfilePieces(1);
        piece_frame = -1;
        state = PIECES;
        return;
    case PIECES:
        /* As time_view: one frame settles, then TIMED_FRAMES are kept. */
        place(&spots[samples[worst].spot], samples[worst].yaw);
        if (!fresh || !frame->pieced) return;
        if (piece_frame >= 0)
            for (int i = 0; i < I_RENDER3D_PIECES; ++i) piece_timed[i][piece_frame] = frame->pieceMs[i];
        if (++piece_frame < TIMED_FRAMES) return;
        for (int i = 0; i < I_RENDER3D_PIECES; ++i) piece_ms[i] = median(piece_timed[i], TIMED_FRAMES);
        piece_frame = -1;
        pieces_timed = 1;
        I_Render3DProfilePieces(0);
        /* The screenshot frame runs unpaused so no PAUSE sign covers it. */
        paused = false;
        state = SHOT_PREP;
        return;
    case SHOT_PREP:
        paused = true;
        place(&spots[samples[worst].spot], samples[worst].yaw);
        snprintf(shot_path, sizeof(shot_path), "%s/%s.png", out_dir, maps[map_index].name);
        I_Render3DScreenshotTo(shot_path);
        state = SHOT;
        return;
    case SHOT:
        report_map();
    next_map:
        if (++map_index < map_count) { request_map(); return; }
        report_summary();
        state = FINISHED;
        I_Render3DProfile(0, 0);
        I_Quit();
        return;
    case FEATURES:
        place(&spots[0], feature_facing[feature_facing_at]);
        if (!fresh || !frame->pieced) return;
        if (feature_settle < FEATURE_SETTLE) { ++feature_settle; return; }
        /* The frame after a knob changes is drawn but not kept. */
        if (feature_warm) { feature_warm = 0; return; }
        {
            double *ms = feature_at(feature_round, feature_step);
            for (int p = 0; p < FEATURE_PIECES; ++p) ms[p] = frame->pieceMs[feature_piece[p]];
        }
        feature_warm = 1;
        if (++feature_step == feature_steps())
        {
            feature_step = 0; ++feature_round;
            printf("  %s facing %s: round %d of %d\n", maps[map_index].name,
                   facing_names[feature_facing[feature_facing_at]], feature_round, feature_rounds);
            fflush(stdout);
        }
        if (feature_round < feature_rounds) { I_Render3DProfileKnob(feature_knob_of(feature_step)); return; }
        I_Render3DProfileKnob(0);
        report_features();
        if (++feature_facing_at < feature_facings) { begin_features(); return; }
        I_Render3DProfilePieces(0);
        if (++map_index < map_count) { request_map(); return; }
        if (report) fclose(report);
        report = NULL;
        state = FINISHED;
        I_Render3DProfile(0, 0);
        I_Quit();
        return;
    case FINISHED:
        return;
    }
}

int main(int argc, char **argv)
{
    if (argc < 2 || argv[1][0] == '-')
    {
        fprintf(stderr, "Usage: %s <iwad> [-maps MAP01,E1M2] [-spacing units] [-budget ms] [-out dir] [-strict] [-at x,y[,facing]] [-doors] [-lines n,n] [-features [-rounds N]] [engine options]\n", argv[0]);
        return 1;
    }
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
    if (!SDL_Init(SDL_INIT_VIDEO)) return 1;
    static char *args[256];
    int count = 0, graphics = 0;
    static char graphics_path[1024];
    args[count++] = "perf"; args[count++] = "-iwad"; args[count++] = argv[1];
    for (int i = 2; i < argc && count < 240; ++i)
    {
        if (!strcmp(argv[i], "-maps") && i + 1 < argc) map_filter = argv[++i];
        else if (!strcmp(argv[i], "-spacing") && i + 1 < argc) spacing = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-at") && i + 1 < argc)
        {
            char facing[16] = "";
            at_set = sscanf(argv[++i], "%d,%d,%15s", &at_x, &at_y, facing) >= 2;
            for (int k = 0; k < 4; ++k) if (!strcmp(facing, facing_names[k])) at_yaw = k;
        }
        else if (!strcmp(argv[i], "-budget") && i + 1 < argc) budget = atof(argv[++i]);
        else if (!strcmp(argv[i], "-out") && i + 1 < argc) out_dir = argv[++i];
        else if (!strcmp(argv[i], "-strict")) strict = 1;
        else if (!strcmp(argv[i], "-doors")) doors = 1;
        else if (!strcmp(argv[i], "-features")) features = 1;
        else if (!strcmp(argv[i], "-rounds") && i + 1 < argc) feature_rounds = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-lines") && i + 1 < argc)
        {
            for (char *c = argv[++i]; *c && cross_count < 16;)
            {
                char *end; long n = strtol(c, &end, 10);
                if (end == c) break;
                cross_lines[cross_count++] = (int)n;
                c = *end == ',' ? end + 1 : end;
            }
            doors = 1;
        }
        else
        {
            if (!strcmp(argv[i], "-skill")) skill_given = 1;
            if (!strcmp(argv[i], "-graphics")) graphics = 1;
            args[count++] = argv[i];
        }
    }
    if (spacing < 32 || budget <= 0) { fprintf(stderr, "Invalid -spacing or -budget\n"); return 1; }
    if (features && !at_set) { fprintf(stderr, "-features times one view: give it -at x,y[,facing]\n"); return 1; }
    if (features && doors) { fprintf(stderr, "-features and -doors are separate runs\n"); return 1; }
    if (feature_rounds < 1 || feature_rounds > MAX_ROUNDS) { fprintf(stderr, "-rounds runs from 1 to %d\n", MAX_ROUNDS); return 1; }
    if (!graphics)
    {
        /* The player's settings, read but never written. */
        char *preferences = SDL_GetPrefPath("Local Games", "DOOM");
        if (preferences)
        {
            snprintf(graphics_path, sizeof(graphics_path), "%sgraphics.cfg", preferences);
            SDL_free(preferences);
            FILE *file = fopen(graphics_path, "r");
            if (file) { fclose(file); args[count++] = "-graphics"; args[count++] = graphics_path; }
        }
    }
    if (!skill_given) { args[count++] = "-skill"; args[count++] = "4"; }
    args[count++] = "-warp"; args[count++] = "1"; args[count++] = "1";
    args[count++] = "-config"; args[count++] = "perf.cfg";
    args[count++] = "-nomusic"; args[count++] = "-nosound";
    myargc = count; myargv = args;
    D_DoomMain();
    return 0;
}
