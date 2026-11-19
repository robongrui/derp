/* The lines around the listener, gathered from the blockmap, and the
 * crossings of straight paths through them. Each thread keeps its own
 * gathered lines, so the room grid (room_grid.h) measures on workers while
 * the game thread hears. */
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "p_local.h"
#include "map.h"
#include "../rooms.h"
#include <SDL3/SDL_thread.h>

typedef struct { float x, y, dx, dy; line_t *line; } segment_t;
/* One gather: the lines within reach of a point. */
typedef struct {
    segment_t *lines; int count, capacity;
    float x, y; int tic; line_t *of;
} gathered_t;
/* A thread's: the last gather round a point, and a block one that the
   points near its middle share (Map_GatherBlock). */
typedef struct {
    gathered_t point, block;
    float block_half;          /* the block's points lie this near its middle */
    const gathered_t *current; /* what Map_Cross walks */
    int *seen; int seen_size, seen_stamp;
} thread_lines_t;

static SDL_TLSID lines_tls;

static void free_lines(void *p)
{
    thread_lines_t *t = p;
    if (!t) return;
    free(t->point.lines); free(t->block.lines); free(t->seen); free(t);
}
static thread_lines_t *mine(void)
{
    thread_lines_t *t = SDL_GetTLS(&lines_tls);
    if (t) return t;
    t = calloc(1, sizeof(*t));
    if (!t) abort();
    t->point.tic = t->block.tic = -1;
    t->current = &t->point;
    SDL_SetTLS(&lines_tls, t, free_lines);
    return t;
}

static void gather(thread_lines_t *t, gathered_t *g, float x, float y, float reach)
{
    g->of = lines; g->tic = gametic; g->x = x; g->y = y;
    if (t->seen_size != numlines)
    {
        free(t->seen); t->seen = calloc(numlines > 0 ? numlines : 1, sizeof(*t->seen));
        t->seen_size = numlines; t->seen_stamp = 0;
    }
    ++t->seen_stamp;
    g->count = 0;
    float origin_x = bmaporgx / (float)FRACUNIT, origin_y = bmaporgy / (float)FRACUNIT;
    int x0 = (int)floorf((x - reach - origin_x) / MAPBLOCKUNITS), x1 = (int)floorf((x + reach - origin_x) / MAPBLOCKUNITS);
    int y0 = (int)floorf((y - reach - origin_y) / MAPBLOCKUNITS), y1 = (int)floorf((y + reach - origin_y) / MAPBLOCKUNITS);
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 >= bmapwidth) x1 = bmapwidth - 1;
    if (y1 >= bmapheight) y1 = bmapheight - 1;
    for (int by = y0; by <= y1; ++by)
        for (int bx = x0; bx <= x1; ++bx)
            for (int32_t *list = blockmaplump + blockmap[by * bmapwidth + bx]; *list != -1; ++list)
            {
                if (*list < 0 || *list >= numlines || t->seen[*list] == t->seen_stamp) continue;
                t->seen[*list] = t->seen_stamp;
                if (g->count == g->capacity)
                {
                    g->capacity = g->capacity ? g->capacity * 2 : 1024;
                    g->lines = realloc(g->lines, sizeof(*g->lines) * g->capacity);
                    if (!g->lines) abort();
                }
                line_t *line = &lines[*list];
                g->lines[g->count++] = (segment_t){line->v1->x / (float)FRACUNIT, line->v1->y / (float)FRACUNIT,
                    line->dx / (float)FRACUNIT, line->dy / (float)FRACUNIT, line};
            }
}

void Map_Gather(float x, float y)
{
    thread_lines_t *t = mine();
    gathered_t *b = &t->block;
    if (b->of == lines && b->tic >= 0 && fabsf(x - b->x) <= t->block_half && fabsf(y - b->y) <= t->block_half)
    {
        t->current = b;
        return;
    }
    gathered_t *g = &t->point;
    t->current = g;
    if (g->of == lines && g->tic == gametic && fabsf(x - g->x) < 16 && fabsf(y - g->y) < 16)
        return;
    gather(t, g, x, y, MAP_REACH);
}

void Map_GatherBlock(float x, float y, float half)
{
    thread_lines_t *t = mine();
    t->block_half = half;
    gather(t, &t->block, x, y, MAP_REACH + half);
    t->current = &t->block;
}

void Map_ReleaseBlock(void)
{
    thread_lines_t *t = mine();
    t->block.tic = -1; t->block.of = NULL;
    t->current = &t->point;
}

int Map_Cross(float x, float y, float cx, float cy, float length, crossing_t *out, int max)
{
    const gathered_t *g = mine()->current;
    int count = 0;
    for (int i = 0; i < g->count; ++i)
    {
        const segment_t *s = &g->lines[i];
        float denominator = cx * s->dy - cy * s->dx;
        if (fabsf(denominator) < 1e-6f) continue;
        float wx = s->x - x, wy = s->y - y;
        float t = (wx * s->dy - wy * s->dx) / denominator, u = (wx * cy - wy * cx) / denominator;
        if (t <= 0.5f || t >= length || u < 0 || u > 1) continue;
        int j;
        if (count < max) j = count++;
        else if (out[max - 1].t > t) j = max - 1;
        else continue;
        for (; j > 0 && out[j - 1].t > t; --j) out[j] = out[j - 1];
        /* Lines face right; a path turning left of one walks into its back. */
        line_t *line = s->line;
        int near_side = denominator < 0 ? 0 : 1;
        sector_t *near = near_side ? line->backsector : line->frontsector;
        sector_t *far = near_side ? line->frontsector : line->backsector;
        if (!(line->flags & ML_TWOSIDED)) far = NULL;
        out[j] = (crossing_t){t, line, near, far, near_side};
    }
    return count;
}

float Map_Outdoors(const sector_t *s)
{
    Rooms_Build();
    int i = (int)(s - sectors);
    return Rooms_Outside(i) ? 1 - Rooms_Enclosure(i) : 0;
}
int Map_SameRoom(int a, int b)
{
    Rooms_Build();
    room_kind kind = Rooms_Kind(a);
    return a == b || ((kind == ROOM_INDOOR || kind == ROOM_SHELTERED) && Rooms_Kind(b) == kind && Rooms_Of(a) == Rooms_Of(b));
}
