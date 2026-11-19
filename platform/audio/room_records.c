/* The rooms' own sound, measured at level start. See room_records.h. */
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "doomdef.h"
#include "doomstat.h"
#include "p_local.h"
#include "r_state.h"
#include "../rooms.h"
#include "map.h"
#include "room_records.h"
#include <SDL3/SDL_timer.h>

#define SPOTS 4          /* the biggest pieces of a room measured, the best kept */
#define TUNNEL 0.5f      /* reading this much as a tube, a room is one */
#define BUDGET_NS 2000000 /* measuring per tic, at most (at least one room): nearest rooms first */

typedef struct { float x, y, area; } spot_t;

static room_record_t *records;
static spot_t *spots;          /* SPOTS per key, biggest first */
static int *sector_key, *own_key, key_count;
static unsigned char *dirty, *measured;
static unsigned built = ~0u;
static int built_sectors;

static float units(fixed_t v) { return v / (float)FRACUNIT; }

/* A room of the room map, or a yard after them; -1 for a sector with no height. */
static int own(int sector)
{
    int room = Rooms_Of(sector);
    if (room >= 0) return room;
    int yard = Rooms_Yard(sector);
    return yard >= 0 ? Rooms_Count() + yard : -1;
}

/* A subsector's middle and roughly its size, from its corners. */
static void subsector_spot(const subsector_t *ss, spot_t *out)
{
    float x = 0, y = 0, area = 0;
    int n = ss->numlines;
    for (int i = 0; i < n; ++i)
    {
        const seg_t *a = &segs[ss->firstline + i], *b = &segs[ss->firstline + (i + 1) % n];
        float ax = units(a->v1->x), ay = units(a->v1->y), bx = units(b->v1->x), by = units(b->v1->y);
        x += ax; y += ay;
        area += ax * by - bx * ay;
    }
    out->x = n ? x / n : 0; out->y = n ? y / n : 0; out->area = fabsf(area) / 2;
}

static void keep_spot(int key, spot_t s)
{
    spot_t *list = &spots[key * SPOTS];
    int i = SPOTS;
    while (i > 0 && list[i - 1].area < s.area) --i;
    if (i == SPOTS) return;
    memmove(&list[i + 1], &list[i], (size_t)(SPOTS - 1 - i) * sizeof(spot_t));
    list[i] = s;
}

/* The room at its best spot: the most tube (where it rings longest), else
   the most space. */
static void measure(int key)
{
    room_record_t best = {0}, at;
    room_outline_t outline;
    int found = 0;
    float best_tunnel = 0;
    for (int i = 0; i < SPOTS; ++i)
    {
        const spot_t *s = &spots[key * SPOTS + i];
        if (s->area <= 0) break;
        Room_MeasureAt(s->x, s->y, 0, &at.room, &outline);
        at.x = s->x; at.y = s->y; at.volume = outline.volume;
        at.sector = (int)(R_PointInSubsector((fixed_t)(s->x * FRACUNIT), (fixed_t)(s->y * FRACUNIT))->sector - sectors);
        /* Among a tube's spots the one that rings longest: near a closed end
           a tube rings short, and the record is the tube's own ring. */
        float tunnel = at.room.tunnel >= TUNNEL ? at.room.tunnel : 0;
        int better = tunnel > 0 ? at.room.decay > best.room.decay : at.volume > best.volume;
        if (!found || tunnel > best_tunnel || (tunnel == best_tunnel && better))
        {
            best = at; best_tunnel = tunnel; found = 1;
        }
    }
    if (found) records[key] = best;
    measured[key] = (unsigned char)found;
    dirty[key] = 0;
}

/* A doorway or window frame sounds as the biggest measured room next to it. */
static void join_openings(void)
{
    for (int s = 0; s < numsectors; ++s) sector_key[s] = own_key[s];
    for (int i = 0; i < numlines; ++i)
    {
        const line_t *l = &lines[i];
        if (!l->frontsector || !l->backsector) continue;
        int a = (int)(l->frontsector - sectors), b = (int)(l->backsector - sectors);
        for (int side = 0; side < 2; ++side)
        {
            int opening = side ? b : a, other = side ? a : b;
            if (Rooms_Kind(opening) != ROOM_OPENING || Rooms_Kind(other) == ROOM_OPENING) continue;
            int there = own_key[other], now = sector_key[opening];
            if (there < 0 || !measured[there]) continue;
            if (now == own_key[opening] || (measured[now] && records[there].volume > records[now].volume))
                sector_key[opening] = there;
        }
    }
}

static void build(void)
{
    free(records); free(spots); free(sector_key); free(own_key); free(dirty); free(measured);
    records = NULL; spots = NULL; sector_key = own_key = NULL; dirty = measured = NULL;
    Rooms_Build();
    key_count = Rooms_Count() + Rooms_YardCount();
    int keys = key_count > 0 ? key_count : 1, count = numsectors > 0 ? numsectors : 1;
    records = calloc((size_t)keys, sizeof(*records));
    spots = calloc((size_t)keys * SPOTS, sizeof(*spots));
    sector_key = malloc((size_t)count * sizeof(int));
    own_key = malloc((size_t)count * sizeof(int));
    dirty = calloc((size_t)keys, 1);
    measured = calloc((size_t)keys, 1);
    built = r_levelserial; built_sectors = numsectors;
    if (!records || !spots || !sector_key || !own_key || !dirty || !measured) { key_count = 0; return; }
    for (int s = 0; s < numsectors; ++s) own_key[s] = own(s);
    for (int i = 0; i < numsubsectors; ++i)
    {
        int key = own_key[subsectors[i].sector - sectors];
        if (key < 0 || key >= key_count) continue;
        spot_t s;
        subsector_spot(&subsectors[i], &s);
        if (s.area > 0) keep_spot(key, s);
    }
    /* Every room, doorways too (one with no room beside it keeps its own),
       measured over the next tics, nearest first, so no tic stalls. */
    for (int key = 0; key < key_count; ++key) dirty[key] = spots[key * SPOTS].area > 0;
    join_openings();
}

/* The dirty room nearest the listener, or -1. */
static int nearest_dirty(void)
{
    const mobj_t *listener = players[consoleplayer].mo;
    float lx = listener ? units(listener->x) : 0, ly = listener ? units(listener->y) : 0, best = 0;
    int found = -1;
    for (int key = 0; key < key_count; ++key)
    {
        if (!dirty[key]) continue;
        float d = fabsf(spots[key * SPOTS].x - lx) + fabsf(spots[key * SPOTS].y - ly);
        if (found < 0 || d < best) { found = key; best = d; }
    }
    return found;
}

static void measure_dirty(Uint64 budget)
{
    Uint64 start = SDL_GetTicksNS();
    int done = 0, key;
    while ((key = nearest_dirty()) >= 0)
    {
        measure(key);
        ++done;
        if (budget && SDL_GetTicksNS() - start >= budget) break;
    }
    if (done) join_openings();
}

void RoomRecords_Reset(void) { built = ~0u; }

void RoomRecords_Follow(void)
{
    if (gamestate != GS_LEVEL || !numsectors) return;
    if (built != r_levelserial || built_sectors != numsectors) build();
    measure_dirty(BUDGET_NS);
}

void RoomRecords_Finish(void)
{
    if (gamestate != GS_LEVEL || !numsectors) return;
    if (built != r_levelserial || built_sectors != numsectors) build();
    measure_dirty(0);
}

int RoomRecords_KeyAt(float x, float y)
{
    if (gamestate != GS_LEVEL || !numnodes) return -1;
    return RoomRecords_Key((int)(R_PointInSubsector((fixed_t)(x * FRACUNIT), (fixed_t)(y * FRACUNIT))->sector - sectors));
}

int RoomRecords_Key(int sector)
{
    if (built != r_levelserial || !sector_key || sector < 0 || sector >= built_sectors) return -1;
    int key = sector_key[sector];
    return key >= 0 && key < key_count && measured[key] ? key : -1;
}

const room_record_t *RoomRecords_Get(int key)
{
    if (built != r_levelserial || key < 0 || key >= key_count || !measured[key]) return NULL;
    return &records[key];
}

int RoomRecords_Neighbours(int key, int *keys, float *widths, int max)
{
    if (built != r_levelserial || !sector_key || key < 0 || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < numlines; ++i)
    {
        const line_t *l = &lines[i];
        if (!l->frontsector || !l->backsector) continue;
        int a = sector_key[l->frontsector - sectors], b = sector_key[l->backsector - sectors];
        if (a == b || (a != key && b != key)) continue;
        int other = a == key ? b : a;
        if (other < 0 || !measured[other]) continue;
        float bottom, top;
        Map_Gap(l->frontsector, l->backsector, &bottom, &top);
        if (top - bottom < MAP_MIN_GAP) continue;
        float width = hypotf(units(l->dx), units(l->dy));
        int k = 0;
        while (k < count && keys[k] != other) ++k;
        if (k == count)
        {
            if (count == max) continue;
            keys[count] = other; widths[count++] = 0;
        }
        widths[k] += width;
    }
    /* Widest first. */
    for (int i = 1; i < count; ++i)
        for (int j = i; j > 0 && widths[j] > widths[j - 1]; --j)
        {
            int t = keys[j]; keys[j] = keys[j - 1]; keys[j - 1] = t;
            float w = widths[j]; widths[j] = widths[j - 1]; widths[j - 1] = w;
        }
    return count;
}

void RoomRecords_Glass(int line)
{
    if (built != r_levelserial || !own_key || line < 0 || line >= numlines) return;
    const line_t *l = &lines[line];
    const sector_t *sides[2] = { l->frontsector, l->backsector };
    for (int i = 0; i < 2; ++i)
    {
        if (!sides[i]) continue;
        int s = (int)(sides[i] - sectors), key = sector_key[s];
        if (key >= 0) dirty[key] = 1;
        /* A pane in a frame: the room behind the frame changes too. */
        if (own_key[s] >= 0 && own_key[s] != key) dirty[own_key[s]] = 1;
    }
}
