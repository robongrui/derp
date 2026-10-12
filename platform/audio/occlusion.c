/* Muffling. A sound in plain sight only loses treble with distance. One out
 * of sight is muffled: a little when sound has an open way round to the
 * listener (a corner, an open door, a window ledge), a lot when only closed
 * doors and solid walls lie between. The volume stays vanilla's, so hearing
 * still tells as much as ever about where monsters are. */
#include <math.h>
#include <stdlib.h>
#include "p_local.h"
#include "map.h"
#include "occlusion.h"

#define EAR 41.0f
#define NEAR 200.0f       /* closer than this, distance takes nothing */
#define FAR 1200.0f       /* vanilla's clipping distance */
#define FAR_HZ 3500.0f
#define LAST_HZ 2500.0f   /* E?M8 sounds carry farther still */
#define AROUND_HZ 1800.0f /* out of sight, with an open way round or over a ledge */
#define SEALED_HZ 500.0f  /* behind closed doors and walls only */
#define MAX_CROSSINGS 128

static float distance_cutoff(float distance)
{
    if (distance <= NEAR) return OCCLUSION_OPEN_HZ;
    float cutoff = OCCLUSION_OPEN_HZ * powf(FAR_HZ / OCCLUSION_OPEN_HZ, (distance - NEAR) / (FAR - NEAR));
    return cutoff > LAST_HZ ? cutoff : LAST_HZ;
}

/* Sound leaks through any gap at all, if muffled through narrow ones; only
   shut doors and lifts stop it. A shut one sounds from inside its own closed
   sector, and sound that reaches it counts as having arrived. */
static int open_gap(float bottom, float top) { return top - bottom >= 1; }
static int shut(const sector_t *s) { return Map_Ceiling(s) - Map_Floor(s) < 1; }

/* What lies on the straight line from ear to sound: SIGHT nothing, LEDGE only
   openings it passes above or below (a window sill, a lintel) or narrow
   slits, WALL a wall or closed door. */
enum { SIGHT, LEDGE, WALL };
static int in_sight(float x, float y, float ear, const sector_t *there, float sx, float sy, float sz, float distance)
{
    static crossing_t hits[MAX_CROSSINGS];
    float cx = (sx - x) / distance, cy = (sy - y) / distance;
    int count = Map_Cross(x, y, cx, cy, distance, hits, MAX_CROSSINGS), seen = SIGHT;
    for (int i = 0; i < count; ++i)
    {
        if (!hits[i].near || !hits[i].far) return WALL;
        if (hits[i].far == there && shut(there)) return seen;
        float bottom, top, z = ear + (sz - ear) * hits[i].t / distance;
        Map_Gap(hits[i].near, hits[i].far, &bottom, &top);
        if (!open_gap(bottom, top)) return WALL;
        if (top - bottom < MAP_MIN_GAP || z <= bottom || z >= top) seen = LEDGE;
    }
    return seen;
}

/* Whether sound can walk from one sector to the other through openings
   (no closed doors) within budget units, passing line midpoints. */
typedef struct { float cost, x, y; sector_t *sector; } step_t;
static step_t *heap; static int heap_size, heap_capacity;
static int *settled; static int settled_size, settled_stamp;
static void push(step_t step)
{
    if (heap_size == heap_capacity)
    {
        heap_capacity = heap_capacity ? heap_capacity * 2 : 256;
        heap = realloc(heap, sizeof(*heap) * heap_capacity);
    }
    int i = heap_size++;
    for (; i > 0 && heap[(i - 1) / 2].cost > step.cost; i = (i - 1) / 2) heap[i] = heap[(i - 1) / 2];
    heap[i] = step;
}
static step_t pop(void)
{
    step_t top = heap[0], last = heap[--heap_size];
    int i = 0;
    for (;;)
    {
        int child = 2 * i + 1;
        if (child >= heap_size) break;
        if (child + 1 < heap_size && heap[child + 1].cost < heap[child].cost) ++child;
        if (heap[child].cost >= last.cost) break;
        heap[i] = heap[child]; i = child;
    }
    if (heap_size) heap[i] = last;
    return top;
}
static int way_round(sector_t *from, float x, float y, sector_t *to, float sx, float sy, float budget)
{
    if (settled_size != numsectors)
    {
        free(settled); settled = calloc(numsectors > 0 ? numsectors : 1, sizeof(*settled));
        settled_size = numsectors; settled_stamp = 0;
    }
    ++settled_stamp;
    heap_size = 0;
    push((step_t){0, x, y, from});
    while (heap_size)
    {
        step_t step = pop();
        int index = (int)(step.sector - sectors);
        if (settled[index] == settled_stamp) continue;
        settled[index] = settled_stamp;
        if (step.sector == to) return step.cost + hypotf(sx - step.x, sy - step.y) <= budget;
        for (int i = 0; i < step.sector->linecount; ++i)
        {
            line_t *line = step.sector->lines[i];
            if (!(line->flags & ML_TWOSIDED) || !line->frontsector || !line->backsector) continue;
            sector_t *next = line->frontsector == step.sector ? line->backsector : line->frontsector;
            if (settled[next - sectors] == settled_stamp) continue;
            float mx = (line->v1->x + line->v2->x) / (2.0f * FRACUNIT), my = (line->v1->y + line->v2->y) / (2.0f * FRACUNIT);
            float cost = step.cost + hypotf(mx - step.x, my - step.y);
            if (next == to && shut(to) && cost + hypotf(sx - mx, sy - my) <= budget) return 1;
            float bottom, top;
            Map_Gap(step.sector, next, &bottom, &top);
            if (!open_gap(bottom, top)) continue;
            if (cost <= budget) push((step_t){cost, mx, my, next});
        }
    }
    return 0;
}

float Occlusion_Cutoff(fixed_t x, fixed_t y, fixed_t z, int walls, int distance)
{
    mobj_t *listener = players[consoleplayer].mo;
    if (gamestate != GS_LEVEL || !listener || !listener->subsector || !numlines) return OCCLUSION_OPEN_HZ;
    float lx = listener->x / (float)FRACUNIT, ly = listener->y / (float)FRACUNIT;
    float sx = x / (float)FRACUNIT, sy = y / (float)FRACUNIT;
    float apart = hypotf(sx - lx, sy - ly);
    float cutoff = distance ? distance_cutoff(apart) : OCCLUSION_OPEN_HZ;
    if (!walls || apart < 1 || apart > MAP_REACH) return cutoff;

    /* Things sound from about their middle; sector sounds (doors, lifts) from
       the middle of their sector's height. */
    sector_t *here = listener->subsector->sector, *there = R_PointInSubsector(x, y)->sector;
    float floor = Map_Floor(there), ceiling = Map_Ceiling(there);
    float sz = z / (float)FRACUNIT + 32;
    if (sz < floor + 8 || sz > ceiling - 8) sz = 0.5f * (floor + ceiling);
    float ear = listener->z / (float)FRACUNIT + EAR;
    Map_Gather(lx, ly);
    int sight = in_sight(lx, ly, ear, there, sx, sy, sz, apart);
    if (sight == SIGHT) return cutoff;
    float muffled = sight == LEDGE || way_round(here, lx, ly, there, sx, sy, 1.6f * apart + 384) ? AROUND_HZ : SEALED_HZ;
    return muffled < cutoff ? muffled : cutoff;
}
