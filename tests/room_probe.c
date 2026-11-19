/* Measures what the reverb hears at chosen spots of a level, without sound
   or a window, for comparing rooms before and after a change to room.c:

     doom-room-probe <iwad> [-file pwad] [-warp e m | -warp m] [-out dir] [-name prefix]
                     [-at name:x,y,facing]... [-walk name:x0,y0,x1,y1,steps,facing]...

   Facings are degrees, as map angles turn (90 faces north). Each spot is
   measured as Room_MeasureAt and Beyond_Measure give it to the mixer, and
   written to <out>/<prefix>-<name>.room: the room_t field by field, one
   "key value" line each, the echoes as "tap k delay gain spread dark pan",
   the floor plan's volume and ray reaches, then the space beyond.
   doom-sfx-render -room plays such a file. A walk measures steps spots
   from (x0, y0) to (x1, y1), each to <prefix>-<name>-<step>.room. One line
   per spot goes to the console. The game's config is <out>/probe.cfg,
   never the player's.
   -grid bakes the level's room grid (room_grid.h) first and, at every spot,
   sets what the grid gives against the live measurement: one "grid" line
   each, the largest differences and whether they are within tolerance.
   -gridsample n adds n spots spread over the level's subsectors.
   -gridswitch first starts the grid's bake and loads the next map while its
   workers run (they must stop before the old map goes). */
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
#include "../platform/audio/beyond.h"
#include "../platform/audio/room.h"
#include "../platform/audio/room_grid.h"
#include "../platform/audio/room_records.h"
#include "../platform/rooms.h"
#include "r_state.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#define MAX_SPOTS 1024

typedef struct { char name[32]; float x, y, facing; } spot_t;
static spot_t spots[MAX_SPOTS];
static int spot_count;
static const char *out_dir = ".", *prefix = "probe";

static void write_room(const char *name, const spot_t *s, const room_t *room, const room_outline_t *outline,
                       const beyond_t *beyond)
{
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s-%s.room", out_dir, prefix, name);
    FILE *f = fopen(path, "w");
    if (!f) I_Error("Room probe: can't write %s", path);
    fprintf(f, "x %g\ny %g\nfacing %g\n", s->x, s->y, s->facing);
    fprintf(f, "decay %.6f\ndamping %.6f\nscatter %.6f\nlate %.6f\n", room->decay, room->damping, room->scatter, room->late);
    fprintf(f, "tunnel %.6f\naxis %.6f\nwidth %.1f\nheight %.1f\nflutter_delay %.6f\nflutter %.6f\n",
            room->tunnel, room->axis, room->width, room->height, room->flutter_delay, room->flutter);
    for (int k = 0; k < ROOM_TAPS; ++k)
        fprintf(f, "tap %d %.6f %.6f %.6f %.6f %.6f\n", k, room->delay[k], room->gain[k], room->spread[k], room->dark[k], room->pan[k]);
    fprintf(f, "volume %.1f\nreach", outline->volume);
    for (int i = 0; i < ROOM_RAYS; ++i) fprintf(f, " %.0f", outline->reach[i]);
    fprintf(f, "\nbeyond_coupling %.6f\nbeyond_decay %.6f\nbeyond_late %.6f\nbeyond_volume %.1f\n",
            beyond->coupling, beyond->room.decay, beyond->room.late, beyond->outline.volume);
    fclose(f);
    char full[96];
    snprintf(full, sizeof(full), "%s-%s", prefix, name);
    printf("spot %-36s decay %.2f late %.2f damping %.2f scatter %.2f  tunnel %.2f width %4.0f flutter %.2f  beyond %.2f\n",
           full, room->decay, room->late, room->damping, room->scatter, room->tunnel, room->width, room->flutter,
           beyond->coupling);
}

/* -grid: the grid against live, the worst of each difference over all spots. */
static int grid_switch, grid_snap, grid_check, grid_samples, grid_spots, grid_served, grid_failed;
static double live_ns, grid_ns;
enum { D_DECAY, D_LATE, D_DAMPING, D_SCATTER, D_TUNNEL, D_FLUTTER, D_AXIS, D_DELAY, D_GAIN, D_PAN, D_SPREAD, D_DARK,
       D_COUPLING, D_COUNT };
static const char *d_names[D_COUNT] = { "decay%", "late", "damping", "scatter", "tunnel", "flutter", "axis", "delay_ms",
                                        "gain", "pan", "spread", "dark", "beyond" };
/* Within these the grid sounds as the live measurement (the guard). */
static const float d_tolerance[D_COUNT] = { 5, 0.03f, 0.03f, 0.03f, 0.05f, 0.05f, 0.2f, 2.5f, 0.025f, 0.15f, 0.1f, 0.1f, 0.15f };
static float d_worst[D_COUNT];
static char d_where[D_COUNT][48];

static void differ(const room_t *a, const beyond_t *ab, const room_t *b, const beyond_t *bb, float *d)
{
    memset(d, 0, sizeof(float) * D_COUNT);
    d[D_DECAY] = 100 * fabsf(a->decay - b->decay) / fmaxf(a->decay, 0.01f);
    d[D_LATE] = fabsf(a->late - b->late);
    d[D_DAMPING] = fabsf(a->damping - b->damping);
    d[D_SCATTER] = fabsf(a->scatter - b->scatter);
    d[D_TUNNEL] = fabsf(a->tunnel - b->tunnel);
    d[D_FLUTTER] = fabsf(a->flutter - b->flutter);
    /* The tube's axis runs both ways, and matters as much as it is a tube. */
    float axis = fabsf(remainderf(a->axis - b->axis, (float)M_PI));
    d[D_AXIS] = axis * fminf(a->tunnel, b->tunnel);
    for (int k = 0; k < ROOM_TAPS; ++k)
    {
        d[D_DELAY] = fmaxf(d[D_DELAY], 1000 * fabsf(a->delay[k] - b->delay[k]) * (a->gain[k] > 0.01f || b->gain[k] > 0.01f));
        d[D_GAIN] = fmaxf(d[D_GAIN], fabsf(a->gain[k] - b->gain[k]));
        d[D_PAN] = fmaxf(d[D_PAN], fabsf(a->pan[k] - b->pan[k]));
        d[D_SPREAD] = fmaxf(d[D_SPREAD], fabsf(a->spread[k] - b->spread[k]));
        d[D_DARK] = fmaxf(d[D_DARK], fabsf(a->dark[k] - b->dark[k]));
    }
    d[D_COUPLING] = fabsf(ab->coupling - bb->coupling);
}

/* The grid b against live a at spot s. Strictly within d_tolerance, or within
   it plus as much as live itself changes one running step (two tics, 32
   units) away, which is what the mixer heard between measurements. */
static int grid_strict_failed;
static void compare(const char *name, const spot_t *s, float facing, const room_t *a, const beyond_t *ab, const room_t *b,
                    const beyond_t *bb)
{
    float d[D_COUNT], jitter[D_COUNT] = {0}, n[D_COUNT];
    differ(a, ab, b, bb, d);
    static const float step[4][2] = {{32, 0}, {-32, 0}, {0, 32}, {0, -32}};
    for (int k = 0; k < 4; ++k)
    {
        room_t near_room;
        room_outline_t near_outline;
        beyond_t near_beyond;
        Room_MeasureAt(s->x + step[k][0], s->y + step[k][1], facing, &near_room, &near_outline);
        Beyond_Measure(&near_room, &near_outline, facing, &near_beyond);
        differ(a, ab, &near_room, &near_beyond, n);
        for (int i = 0; i < D_COUNT; ++i) jitter[i] = fmaxf(jitter[i], n[i]);
    }
    int ok = 1, strict = 1;
    char worst[256] = "";
    for (int i = 0; i < D_COUNT; ++i)
    {
        strict &= d[i] <= d_tolerance[i];
        ok &= d[i] <= d_tolerance[i] + jitter[i];
        if (d[i] > d_tolerance[i])
        {
            size_t used = strlen(worst);
            snprintf(worst + used, sizeof(worst) - used, " %s %.3f (live %.3f)", d_names[i], d[i], jitter[i]);
        }
        float over = d[i] - jitter[i];
        if (over > d_worst[i]) { d_worst[i] = over; snprintf(d_where[i], sizeof(d_where[i]), "%s", name); }
    }
    grid_failed += !ok;
    grid_strict_failed += !strict;
    printf("grid %-34s %s decay %.2f/%.2f late %.2f/%.2f tunnel %.2f/%.2f beyond %.2f/%.2f%s\n", name,
           strict ? "ok  " : ok ? "step" : "OVER", a->decay, b->decay, a->late, b->late, a->tunnel, b->tunnel, ab->coupling,
           bb->coupling, worst);
}

static void measure(const char *name, const spot_t *s)
{
    room_t room;
    room_outline_t outline;
    beyond_t beyond;
    float facing = s->facing * (float)M_PI / 180;
    Uint64 start = SDL_GetTicksNS();
    Room_MeasureAt(s->x, s->y, facing, &room, &outline);
    Beyond_Measure(&room, &outline, facing, &beyond);
    live_ns += SDL_GetTicksNS() - start;
    write_room(name, s, &room, &outline, &beyond);
    if (!grid_check) return;
    room_t grid;
    beyond_t grid_beyond;
    ++grid_spots;
    start = SDL_GetTicksNS();
    int served = RoomGrid_Listener(s->x, s->y, facing, &grid, &grid_beyond);
    grid_ns += SDL_GetTicksNS() - start;
    if (!served) { printf("grid %-34s none (measured live)\n", name); return; }
    ++grid_served;
    char full[96];
    snprintf(full, sizeof(full), "%s-%s", prefix, name);
    compare(full, s, facing, &room, &beyond, &grid, &grid_beyond);
}

/* -gridsample: n spots at subsector middles spread over the level, facing round. */
static void samples(void)
{
    for (int n = 0; n < grid_samples && numsubsectors > 0; ++n)
    {
        const subsector_t *ss = &subsectors[(int)((n + 0.5) * numsubsectors / grid_samples)];
        float x = 0, y = 0;
        for (int i = 0; i < ss->numlines; ++i)
        {
            x += segs[ss->firstline + i].v1->x / (float)FRACUNIT;
            y += segs[ss->firstline + i].v1->y / (float)FRACUNIT;
        }
        spot_t s = { "", x / ss->numlines, y / ss->numlines, (float)(n * 47 % 360) };
        /* -gridsnap: on the nearest cell's own spot (32 past each 64). */
        if (grid_snap) { s.x = floorf(s.x / 64) * 64 + 32; s.y = floorf(s.y / 64) * 64 + 32; }
        snprintf(s.name, sizeof(s.name), "sample-%03d", n);
        measure(s.name, &s);
    }
}

/* -records: the rooms as an authored level bakes them (room_records.h), one
   line each, keyed by the room map. */
static int print_records;
static void records(void)
{
    RoomRecords_Finish();
    int keys = Rooms_Count() + Rooms_YardCount();
    for (int key = 0; key < keys; ++key)
    {
        const room_record_t *r = RoomRecords_Get(key);
        if (!r) continue;
        printf("record %3d at %6.0f,%6.0f sector %3d  decay %.2f late %.2f  tunnel %.2f width %4.0f flutter %.2f  volume %.0f\n",
               key, r->x, r->y, r->sector, r->room.decay, r->room.late, r->room.tunnel, r->room.width, r->room.flutter, r->volume);
    }
    for (int s = 0; s < numsectors; ++s) printf("sector %3d sounds as %d\n", s, RoomRecords_Key(s));
}

void I_TestFrame(void)
{
    if (gamestate != GS_LEVEL || gametic < 2) return;
    if (grid_switch)
    {
        grid_switch = 0;
        RoomGrid_Follow();
        SDL_Delay(20);
        printf("grid switch: next map while the bake runs\n");
        G_InitNew(gameskill, gameepisode, gamemap < 9 ? gamemap + 1 : 1);
        return;
    }
    if (print_records) records();
    if (grid_check)
    {
        double seconds = RoomGrid_Finish();
        printf("grid baked in %.0f ms\n", seconds * 1000);
    }
    for (int i = 0; i < spot_count; ++i) measure(spots[i].name, &spots[i]);
    if (grid_check) samples();
    /* The walks. */
    for (int i = 1; i < myargc - 1; ++i)
    {
        char name[32];
        float x0, y0, x1, y1, facing;
        int steps;
        if (strcmp(myargv[i], "-walk")
            || sscanf(myargv[i + 1], "%31[^:]:%f,%f,%f,%f,%d,%f", name, &x0, &y0, &x1, &y1, &steps, &facing) != 7)
            continue;
        for (int n = 0; n < steps; ++n)
        {
            float t = steps > 1 ? n / (float)(steps - 1) : 0;
            spot_t s = { "", x0 + (x1 - x0) * t, y0 + (y1 - y0) * t, facing };
            snprintf(s.name, sizeof(s.name), "%s-%02d", name, n);
            measure(s.name, &s);
        }
    }
    if (grid_check)
    {
        printf("grid summary: %d spots, %d from the grid: %d within tolerance, %d more within live's own step, %d over;"
               " live %.1f us, grid %.2f us a spot\n", grid_spots, grid_served, grid_served - grid_strict_failed,
               grid_strict_failed - grid_failed, grid_failed, grid_spots ? live_ns / 1000 / grid_spots : 0,
               grid_spots ? grid_ns / 1000 / grid_spots : 0);
        for (int i = 0; i < D_COUNT; ++i)
            printf("grid worst %-9s %8.3f past live's step (tolerance %.3f) at %s\n", d_names[i], d_worst[i], d_tolerance[i],
                   d_where[i]);
        RoomGrid_Stop();
    }
    fflush(stdout);
    I_Quit();
}

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: %s <iwad> [-file pwad] [-warp ...] [-out dir] [-name prefix] [-at name:x,y,facing]... [-walk name:x0,y0,x1,y1,steps,facing]...\n", argv[0]); return 2; }
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
    if (!SDL_Init(SDL_INIT_VIDEO)) return 1;
    static char *args[256], config[1024];
    int count = 0;
    args[count++] = "probe"; args[count++] = "-iwad"; args[count++] = argv[1];
    for (int i = 2; i < argc; ++i)
    {
        if (!strcmp(argv[i], "-out") && i + 1 < argc) out_dir = argv[++i];
        else if (!strcmp(argv[i], "-name") && i + 1 < argc) prefix = argv[++i];
        else if (!strcmp(argv[i], "-records")) print_records = 1;
        else if (!strcmp(argv[i], "-grid")) grid_check = 1;
        else if (!strcmp(argv[i], "-gridsnap")) grid_snap = 1;
        else if (!strcmp(argv[i], "-gridswitch")) grid_switch = 1;
        else if (!strcmp(argv[i], "-gridsample") && i + 1 < argc) grid_samples = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-at") && i + 1 < argc)
        {
            spot_t *s = &spots[spot_count];
            if (spot_count >= MAX_SPOTS || sscanf(argv[++i], "%31[^:]:%f,%f,%f", s->name, &s->x, &s->y, &s->facing) != 4)
            { fprintf(stderr, "Bad spot %s\n", argv[i]); return 2; }
            ++spot_count;
        }
        else if (count < 240) args[count++] = argv[i];
    }
    snprintf(config, sizeof(config), "%s/probe.cfg", out_dir);
    args[count++] = "-config"; args[count++] = config;
    args[count++] = "-nosound"; args[count++] = "-nomusic"; args[count++] = "-nomonsters";
    args[count] = NULL;
    myargc = count; myargv = args;
    D_DoomMain();
    return 0;
}
