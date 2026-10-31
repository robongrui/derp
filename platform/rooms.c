/* The room map (rooms.h): built once per level from the map's sectors and
 * lines at their load heights.
 * - Window frames: the glass's rule (windows.cpp places the panes on them).
 * - Sky openings: the sky sectors joined by open lines, for the sky's
 *   enclosure (bake_sun.cpp: a hole in a ceiling reads as indoors).
 * - Openings: door sectors (thin sectors a door special moves), window
 *   frames, sectors shut at load, and thin recesses left over after the
 *   joins.
 * - Joins: two sectors under a roof share a room when they open into each
 *   other through a real gap (at least 24 units and 60% of the lower one's
 *   height) along at least half the smaller one's perimeter, or along the
 *   whole of its long side and half the larger one's (a hall cut into
 *   strips of other light: no wall between them, one air; a doorway, a door
 *   step or a corridor meeting a room end on stays apart), or at one floor
 *   and ceiling across the whole width of both (a walkway cut into pieces
 *   at its corners).
 *   Nothing a line
 *   special can move joins (every tagged sector, the back of a manual door),
 *   nor a window frame.
 * - Sheltered: a room with a quarter of its edge open to the sky through a
 *   real gap (not its own skylight), at least 16 units deep behind it (its
 *   area over that edge): a porch, a covered walkway, an eave.
 * Render-only. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "doomdef.h"
#include "doomstat.h"
#include "r_state.h"
#include "rooms.h"
#include "game_profile.h"

static unsigned built = ~0u;
static int built_sectors = -1;
static unsigned char *kinds;
static int *space, spaces;
static int *yard, yard_count;
static unsigned char *doorless;   /* per sector: its room opens onto the outside with no door or glass between */
static unsigned char *openair;    /* per sector: the open air (Rooms_Outside): sky, porch, or open onto them with no door or glass */
static float *door_top;           /* per sector: the height a door's ceiling opens to; -1e9 for no door */
static float *enclosure;          /* per sector: how closed in, for heat and sound (Rooms_Enclosure) */
static rooms_frame *frames;
static int frame_count, *pane_frame, pane_lines;
static int *sky_opening, sky_count;
typedef struct { float edge; rooms_sky_indoor *indoor; int count, capacity; } sky_t;
static sky_t *skies;

static float units(int value) { return value / 65536.0f; }
static float floor_of(const sector_t *s) { return units(s->floorheight); }
static float ceiling_of(const sector_t *s) { return units(s->ceilingheight); }
static int sky(const sector_t *s) { return s->ceilingpic == skyflatnum; }
static int index_of(const sector_t *s) { return (int)(s - sectors); }
static float line_length(const line_t *line) { return hypotf(units(line->dx), units(line->dy)); }

/* Doors that open the sector behind them without a tag, and the door
   specials that move the sectors they tag (vanilla's). */
static int manual_door(int special)
{
    switch (special) { case 1: case 26: case 27: case 28: case 31: case 32: case 33: case 34: case 117: case 118: return 1; }
    return 0;
}
static int tagged_door(int special)
{
    switch (special)
    {
    case 2: case 3: case 4: case 16: case 29: case 42: case 46: case 50: case 61: case 63: case 75: case 76:
    case 86: case 90: case 103: case 105: case 106: case 107: case 108: case 109: case 110: case 111:
    case 112: case 113: case 114: case 115: case 116: case 133: case 134: case 135: case 136: case 137: return 1;
    }
    return 0;
}

static void sector_box(const sector_t *s, float box[4])
{
    box[0] = box[1] = 1e9f; box[2] = box[3] = -1e9f;
    for (int n = 0; n < s->linecount; ++n)
    {
        const vertex_t *ends[2] = {s->lines[n]->v1, s->lines[n]->v2};
        for (int e = 0; e < 2; ++e)
        {
            float x = units(ends[e]->x), y = units(ends[e]->y);
            if (x < box[0]) box[0] = x;
            if (y < box[1]) box[1] = y;
            if (x > box[2]) box[2] = x;
            if (y > box[3]) box[3] = y;
        }
    }
}

/* A window is a thin frame sector between two others, with a sill and a
   lintel: its floor above both neighbors and its ceiling below them (or
   under the sky beside it), too high a step to walk in or behind an
   impassable line, at least 32 units wide and 24 tall (E1M7's courtyard
   slit, sector 25) and at most 96 deep (deep recesses like E1M2's side
   windows, sectors 50 and 57, are 72; deeper ones are alcoves). Sectors
   that move or that line specials act on are left alone: a lift is not a
   window. Outer is the side facing the sky, or else the larger. */
static void find_frames(void)
{
    frame_count = 0;
    for (int i = 0; i < numlines; ++i) pane_frame[i] = -1;
    if (!Game_HasEffect(GAME_EFFECT_WINDOW_GLASS)) return;
    unsigned char *tags = calloc(65536, 1), *excluded = calloc((size_t)numsectors, 1);
    float *area = malloc((size_t)numsectors * sizeof(float));
    int *sides = malloc((size_t)(numlines + 1) * sizeof(int));
    for (int i = 0; i < numlines; ++i) if (lines[i].special && lines[i].tag) tags[(unsigned short)lines[i].tag] = 1;
    for (int i = 0; i < numsectors; ++i) if (sectors[i].tag && tags[(unsigned short)sectors[i].tag]) excluded[i] = 1;
    for (int i = 0; i < numlines; ++i)
    {
        const line_t *line = &lines[i];
        if (!line->special || !line->frontsector || !line->backsector) continue;
        excluded[index_of(line->frontsector)] = excluded[index_of(line->backsector)] = 1;
    }
    for (int i = 0; i < numsectors; ++i) { float b[4]; sector_box(&sectors[i], b); area[i] = (b[2] - b[0]) * (b[3] - b[1]); }
    for (int s = 0; s < numsectors; ++s)
    {
        const sector_t *frame = &sectors[s];
        if (excluded[s] || sky(frame) || ceiling_of(frame) - floor_of(frame) < 24) continue;
        int window = 1, count = 0;
        for (int n = 0; n < frame->linecount && window; ++n)
        {
            const line_t *line = frame->lines[n];
            if (!line->frontsector || !line->backsector) continue;
            const sector_t *other = line->frontsector == frame ? line->backsector : line->frontsector;
            if (other == frame) continue;
            float bottom = fmaxf(floor_of(frame), floor_of(other)), top = fminf(ceiling_of(frame), ceiling_of(other));
            if (top - bottom < 16) continue;
            int walkable = floor_of(frame) - floor_of(other) <= 24 && top - bottom >= 56 && !(line->flags & ML_BLOCKING);
            int lintel = ceiling_of(frame) < ceiling_of(other) || sky(other);
            if (walkable || !lintel || floor_of(frame) <= floor_of(other)) { window = 0; break; }
            int o = index_of(other), seen = 0;
            for (int k = 0; k < count; ++k) seen |= sides[k] == o;
            if (!seen) sides[count++] = o;
        }
        if (!window || count < 2) continue;
        float box[4]; sector_box(frame, box);
        float w = box[2] - box[0], h = box[3] - box[1], thin = fminf(w, h), wide = fmaxf(w, h);
        if (thin > 96 || wide < 32) continue;
        int outer = sides[0], inner = -1;
        for (int k = 1; k < count; ++k)
        {
            int o = sides[k], better = sky(&sectors[o]) != sky(&sectors[outer]) ? sky(&sectors[o]) : area[o] > area[outer];
            if (better) outer = o;
        }
        for (int k = 0; k < count; ++k) if (sides[k] != outer && (inner < 0 || area[sides[k]] > area[inner])) inner = sides[k];
        frames[frame_count] = (rooms_frame){s, outer, inner};
        for (int n = 0; n < frame->linecount; ++n)
        {
            const line_t *line = frame->lines[n];
            if (!line->frontsector || !line->backsector) continue;
            const sector_t *other = line->frontsector == frame ? line->backsector : line->frontsector;
            if (index_of(other) != outer || pane_frame[line - lines] >= 0) continue;
            pane_frame[line - lines] = frame_count;
        }
        ++frame_count;
    }
    free(tags); free(excluded); free(area); free(sides);
}

/* The sky's openings: only a real gap counts (closed doors and ledges stay
   walls), and only under a short rim: a ceiling hole sits just above the
   room's ceiling, while cliffs around a canyon rise far above theirs. An
   opening ringed by roofs (nine tenths of its edge a real gap onto floor
   running on under it) is a hole in a ceiling however tall its shaft: a
   canyon's cliffs are walls (E1M6's slot, sector 167, rises 128 above the
   corridor round it). */
static void find_sky_openings(void)
{
    sky_count = 0;
    int *stack = malloc((size_t)(numsectors + 1) * sizeof(int));
    float *whole = NULL; int whole_capacity = 0;
    for (int i = 0; i < numsectors; ++i) sky_opening[i] = -1;
    for (int first = 0; first < numsectors; ++first)
    {
        if (!sky(&sectors[first]) || sky_opening[first] >= 0) continue;
        int id = sky_count++, top = 0;
        float ringed = 0;
        skies[id] = (sky_t){0, NULL, 0, 0};
        stack[top++] = first; sky_opening[first] = id;
        while (top > 0)
        {
            const sector_t *sector = &sectors[stack[--top]];
            for (int i = 0; i < sector->linecount; ++i)
            {
                const line_t *line = sector->lines[i];
                const sector_t *other = line->frontsector == sector ? line->backsector : line->frontsector;
                if (other == sector) continue;
                if (other && sky(other))
                {
                    int n = index_of(other);
                    if (sky_opening[n] < 0) { sky_opening[n] = id; stack[top++] = n; }
                    continue;
                }
                float length = line_length(line);
                skies[id].edge += length;
                if (!other || fminf(ceiling_of(sector), ceiling_of(other)) - fmaxf(floor_of(sector), floor_of(other)) < 24) continue;
                float rim = (128 - (ceiling_of(sector) - ceiling_of(other))) / 64;
                rim = rim < 0 ? 0.0f : rim > 1 ? 1.0f : rim;
                int runs_on = fabsf(floor_of(sector) - floor_of(other)) <= 24 && ceiling_of(other) <= ceiling_of(sector);
                if (runs_on) ringed += length;
                if (rim <= 0 && !runs_on) continue;
                sky_t *o = &skies[id];
                if (o->count == o->capacity)
                {
                    o->capacity = o->capacity ? o->capacity * 2 : 8;
                    o->indoor = realloc(o->indoor, (size_t)o->capacity * sizeof(rooms_sky_indoor));
                }
                if (o->count >= whole_capacity)
                {
                    whole_capacity = o->capacity;
                    whole = realloc(whole, (size_t)whole_capacity * sizeof(float));
                }
                whole[o->count] = length;
                o->indoor[o->count++] = (rooms_sky_indoor){index_of(other), length * rim};
            }
        }
        sky_t *o = &skies[id];
        int ring = o->edge > 0 && ringed >= 0.9f * o->edge, kept = 0;
        for (int k = 0; k < o->count; ++k)
        {
            if (ring) o->indoor[k].length = whole[k];
            if (o->indoor[k].length > 0) o->indoor[kept++] = o->indoor[k];
        }
        o->count = kept;
    }
    free(stack); free(whole);
}

static int *root;
static int find(int s) { while (root[s] != s) s = root[s] = root[root[s]]; return s; }
typedef struct { int a, b; float length, height; } pair_t;
static int by_pair(const void *x, const void *y)
{
    const pair_t *p = x, *q = y;
    return p->a != q->a ? (p->a < q->a ? -1 : 1) : p->b != q->b ? (p->b < q->b ? -1 : 1) : 0;
}
static float height_of(int s) { return ceiling_of(&sectors[s]) - floor_of(&sectors[s]); }
/* A gap between two sectors under a roof that air and people pass freely. */
static int real_gap(int f, int k)
{
    float gap = fminf(ceiling_of(&sectors[f]), ceiling_of(&sectors[k])) - fmaxf(floor_of(&sectors[f]), floor_of(&sectors[k]));
    return gap >= 24 && gap >= 0.6f * fminf(height_of(f), height_of(k));
}
/* A gap onto the sky from s: at least 24 units and half its height. */
static int sky_gap(int s, int sky_sector)
{
    float gap = fminf(ceiling_of(&sectors[s]), ceiling_of(&sectors[sky_sector])) - fmaxf(floor_of(&sectors[s]), floor_of(&sectors[sky_sector]));
    return gap >= 24 && gap >= 0.5f * height_of(s);
}

static void classify(void)
{
    int n = numsectors;
    unsigned char *tags = calloc(65536, 1), *doors = calloc(65536, 1), *fixed = calloc((size_t)n, 1), *door = calloc((size_t)n, 1);
    float *area = calloc((size_t)n, sizeof(float)), *perimeter = calloc((size_t)n, sizeof(float));
    pair_t *pairs = malloc((size_t)(numlines + 1) * sizeof(pair_t));
    int pair_count = 0;
    root = malloc((size_t)n * sizeof(int));
    for (int i = 0; i < numlines; ++i)
        if (lines[i].special && lines[i].tag)
        {
            tags[(unsigned short)lines[i].tag] = 1;
            if (tagged_door(lines[i].special)) doors[(unsigned short)lines[i].tag] = 1;
        }
    for (int i = 0; i < n; ++i)
    {
        root[i] = i;
        kinds[i] = sky(&sectors[i]) ? ROOM_SKY : ROOM_INDOOR;
        if (sectors[i].tag && tags[(unsigned short)sectors[i].tag]) fixed[i] = 1;
        if (sectors[i].tag && doors[(unsigned short)sectors[i].tag]) door[i] = 1;
    }
    for (int i = 0; i < numlines; ++i)
        if (manual_door(lines[i].special) && lines[i].backsector) fixed[index_of(lines[i].backsector)] = door[index_of(lines[i].backsector)] = 1;
    /* A door opens to 4 under the lowest ceiling round it, as the game opens
       it (EV_DoDoor), whatever it is now. */
    for (int i = 0; i < n; ++i) door_top[i] = -1e9f;
    for (int i = 0; i < numlines; ++i)
    {
        const line_t *line = &lines[i];
        if (!line->frontsector || !line->backsector || line->frontsector == line->backsector) continue;
        for (int side = 0; side < 2; ++side)
        {
            const sector_t *s = side ? line->backsector : line->frontsector, *other = side ? line->frontsector : line->backsector;
            int k = index_of(s);
            if (!door[k]) continue;
            float top = ceiling_of(other) - 4;
            if (door_top[k] == -1e9f || top < door_top[k]) door_top[k] = top;
        }
    }
    /* Doors (thin: a big sector a door special raises is a room that moves)
       and window frames are openings. */
    for (int i = 0; i < n; ++i)
    {
        if (!door[i] || kinds[i] == ROOM_SKY) continue;
        float box[4]; sector_box(&sectors[i], box);
        if (fminf(box[2] - box[0], box[3] - box[1]) <= 64) kinds[i] = ROOM_OPENING;
    }
    for (int i = 0; i < frame_count; ++i) kinds[frames[i].sector] = ROOM_OPENING;
    /* A sector shut at load (no height: a closed door no special names, a
       crusher down) is a gap that may open, not a space. */
    for (int i = 0; i < n; ++i) if (kinds[i] == ROOM_INDOOR && height_of(i) <= 0) kinds[i] = ROOM_OPENING;
    for (int i = 0; i < numlines; ++i)
    {
        const line_t *line = &lines[i];
        float length = line_length(line), cross = units(line->v1->x) * units(line->v2->y) - units(line->v2->x) * units(line->v1->y);
        if (!line->frontsector) continue;
        int f = index_of(line->frontsector), k = line->backsector ? index_of(line->backsector) : -1;
        area[f] -= cross / 2; perimeter[f] += length;
        if (k < 0 || k == f) continue;
        area[k] += cross / 2; perimeter[k] += length;
        if (kinds[f] != ROOM_INDOOR || kinds[k] != ROOM_INDOOR || fixed[f] || fixed[k] || !real_gap(f, k)) continue;
        pairs[pair_count++] = (pair_t){f < k ? f : k, f < k ? k : f, length};
    }
    for (int i = 0; i < n; ++i) area[i] = fabsf(area[i]);
    qsort(pairs, (size_t)pair_count, sizeof(pair_t), by_pair);
    for (int i = 0; i < pair_count;)
    {
        int j = i; float open = 0;
        while (j < pair_count && pairs[j].a == pairs[i].a && pairs[j].b == pairs[i].b) open += pairs[j++].length;
        int a = pairs[i].a, b = pairs[i].b, small = area[a] < area[b] ? a : b, large = small == a ? b : a;
        float box[4], side[2];
        sector_box(&sectors[small], box); side[0] = fmaxf(box[2] - box[0], box[3] - box[1]);
        sector_box(&sectors[large], box); side[1] = fmaxf(box[2] - box[0], box[3] - box[1]);
        /* Or at one floor and ceiling across the whole width of both (their
           area over their long side): a walkway cut into pieces at a corner. */
        float width[2] = {area[small] / fmaxf(side[0], 1), area[large] / fmaxf(side[1], 1)};
        int level = sectors[a].floorheight == sectors[b].floorheight && sectors[a].ceilingheight == sectors[b].ceilingheight;
        if (open >= 0.5f * fminf(perimeter[a], perimeter[b]) || (open >= 0.9f * side[0] && open >= 0.5f * side[1]) ||
            (level && open >= 0.9f * width[0] && open >= 0.9f * width[1]))
            root[find(a)] = find(b);
        i = j;
    }
    /* Members per room, for the recess and shelter tests. */
    int *members = calloc((size_t)n, sizeof(int));
    for (int i = 0; i < n; ++i) if (kinds[i] == ROOM_INDOOR) ++members[find(i)];
    /* Thin recesses: a sector on its own, under a roof, between two spaces it
       opens into, 32 units deep or less (its area over the mean of its two
       widest mouths): doorways, frames of other light. */
    for (int s = 0; s < n; ++s)
    {
        if (kinds[s] != ROOM_INDOOR || members[find(s)] != 1 || fixed[s]) continue;
        int other[2] = {-1, -1}; float mouth[2] = {0, 0};
        int seen[64], seen_count = 0; float seen_length[64];
        const sector_t *sector = &sectors[s];
        for (int i = 0; i < sector->linecount; ++i)
        {
            const line_t *line = sector->lines[i];
            if (!line->frontsector || !line->backsector) continue;
            int o = index_of(line->frontsector == sector ? line->backsector : line->frontsector);
            if (o == s || !(sky(&sectors[o]) ? sky_gap(s, o) : real_gap(s, o))) continue;
            int k = 0;
            while (k < seen_count && seen[k] != o) ++k;
            if (k == seen_count) { if (seen_count == 64) continue; seen[seen_count] = o; seen_length[seen_count++] = 0; }
            seen_length[k] += line_length(line);
        }
        for (int k = 0; k < seen_count; ++k)
        {
            if (seen_length[k] > mouth[0]) { mouth[1] = mouth[0]; other[1] = other[0]; mouth[0] = seen_length[k]; other[0] = seen[k]; }
            else if (seen_length[k] > mouth[1]) { mouth[1] = seen_length[k]; other[1] = seen[k]; }
        }
        if (other[1] >= 0 && area[s] / ((mouth[0] + mouth[1]) / 2) <= 32) kinds[s] = ROOM_OPENING;
    }
    /* Sheltered: per room, its edge (lines with one side in it) and the part
       of that open to the sky. Not the sky of its own skylight: a sky
       opening most of whose edge is this one room, with the roof reaching
       deep behind its rim (the room's area at least 100 units per unit of
       rim, and at least as deep as the rim is tall: E1M9's 54 and 83, 500+
       under 176), is a hole in its ceiling, not
       the open air a porch faces. A ring of roof shallower than it is tall
       round a yard is a walkway round a courtyard (E1M6's 168 and 173: 30
       and 88; E1M4's 71, 122 under 224; E1M9's corner halls 5, 100 and 132,
       127-149 under 240): the sky reaches most of the floor under it. */
    float *edge = calloc((size_t)n, sizeof(float)), *open = calloc((size_t)n, sizeof(float)), *room_area = calloc((size_t)n, sizeof(float));
    pair_t *rims = malloc((size_t)(numlines + 1) * sizeof(pair_t));
    int rim_count = 0;
    for (int s = 0; s < n; ++s) if (kinds[s] == ROOM_INDOOR) room_area[find(s)] += area[s];
    for (int i = 0; i < numlines; ++i)
    {
        const line_t *line = &lines[i];
        if (!line->frontsector || !line->backsector) continue;
        int f = index_of(line->frontsector), k = index_of(line->backsector);
        if ((kinds[f] == ROOM_SKY) == (kinds[k] == ROOM_SKY)) continue;
        int s = kinds[f] == ROOM_SKY ? k : f, o = s == f ? k : f;
        if (kinds[s] == ROOM_INDOOR) rims[rim_count++] = (pair_t){sky_opening[o], find(s), line_length(line), line_length(line) * height_of(s)};
    }
    qsort(rims, (size_t)rim_count, sizeof(pair_t), by_pair);
    /* Courtyards the rules misread on the game's own maps (Game_Courtyards)
       are never a skylight. */
    int courtyards[8], courtyard_count = Game_Courtyards(gameepisode, gamemap, courtyards, 8), skylights = 0;
    for (int i = 0; i < rim_count;)
    {
        int j = i, courtyard = 0; float length = 0, height = 0;
        while (j < rim_count && rims[j].a == rims[i].a && rims[j].b == rims[i].b) { length += rims[j].length; height += rims[j++].height; }
        for (int c = 0; c < courtyard_count; ++c)
            courtyard |= courtyards[c] >= 0 && courtyards[c] < n && kinds[courtyards[c]] == ROOM_SKY && sky_opening[courtyards[c]] == rims[i].a;
        if (!courtyard && length >= 0.6f * skies[rims[i].a].edge && room_area[rims[i].b] >= fmaxf(100 * length, height))
            rims[skylights++] = (pair_t){rims[i].a, rims[i].b, 0, 0};
        i = j;
    }
    for (int i = 0; i < numlines; ++i)
    {
        const line_t *line = &lines[i];
        if (!line->frontsector) continue;
        int sides[2] = {index_of(line->frontsector), line->backsector ? index_of(line->backsector) : -1};
        float length = line_length(line);
        for (int side = 0; side < 2; ++side)
        {
            int s = sides[side], o = sides[1 - side];
            if (s < 0 || kinds[s] != ROOM_INDOOR) continue;
            if (o >= 0 && kinds[o] == ROOM_INDOOR && find(o) == find(s)) continue;
            edge[find(s)] += length;
            if (o < 0 || kinds[o] != ROOM_SKY || !sky_gap(s, o)) continue;
            pair_t own = {sky_opening[o], find(s), 0};
            if (!bsearch(&own, rims, (size_t)skylights, sizeof(pair_t), by_pair)) open[find(s)] += length;
        }
    }
    for (int s = 0; s < n; ++s)
    {
        int r = find(s);
        if (kinds[s] == ROOM_INDOOR && edge[r] > 0 && open[r] >= 0.25f * edge[r] && room_area[r] >= 16 * open[r])
            kinds[s] = ROOM_SHELTERED;
    }
    /* A passage between outdoor spaces: a room open to the sky (not its own
       skylight) or a porch on two opposite sides, with at least as much of
       its edge open that way as into the building and at most 384 deep behind
       them (its area over that edge), is a porch too, however little of its
       edge is open (a short tunnel through a wall between two yards). A hut,
       or a hall entered from outside (E1M8's star), stays a room. A door or
       frame leads indoors when anything indoors is behind it, open or shut.
       Again while that makes new porches. */
    typedef struct { int room; float x, y, nx, ny; } mouth_t;
    mouth_t *mouths = malloc((size_t)(2 * numlines + 1) * sizeof(mouth_t));
    float *out_len = malloc((size_t)n * sizeof(float)), *in_len = malloc((size_t)n * sizeof(float));
    for (int changed = 1; changed;)
    {
        changed = 0;
        int mouth_count = 0;
        for (int i = 0; i < n; ++i) out_len[i] = in_len[i] = 0;
        for (int i = 0; i < numlines; ++i)
        {
            const line_t *line = &lines[i];
            if (!line->frontsector || !line->backsector) continue;
            int sides[2] = {index_of(line->frontsector), index_of(line->backsector)};
            float length = line_length(line);
            for (int side = 0; side < 2; ++side)
            {
                int s = sides[side], o = sides[1 - side], r = find(s), out = 0;
                if (kinds[s] != ROOM_INDOOR || (kinds[o] == ROOM_INDOOR && find(o) == r)) continue;
                if (kinds[o] == ROOM_SKY)
                {
                    pair_t own = {sky_opening[o], r, 0};
                    out = sky_gap(s, o) && !bsearch(&own, rims, (size_t)skylights, sizeof(pair_t), by_pair);
                }
                else if (kinds[o] == ROOM_SHELTERED) out = real_gap(s, o);
                else if (kinds[o] == ROOM_INDOOR) { if (real_gap(s, o)) in_len[r] += length; }
                else
                    for (int k = 0; k < sectors[o].linecount; ++k)
                    {
                        const line_t *beyond = sectors[o].lines[k];
                        if (!beyond->backsector) continue;
                        int b = index_of(beyond->frontsector == &sectors[o] ? beyond->backsector : beyond->frontsector);
                        if (b != o && (kinds[b] == ROOM_OPENING || (kinds[b] == ROOM_INDOOR && find(b) != r))) { in_len[r] += length; break; }
                    }
                if (!out) continue;
                /* The front side is on the line's right: out of s is its left for the front, its right for the back. */
                float dx = units(line->dx) / length, dy = units(line->dy) / length;
                out_len[r] += length;
                mouths[mouth_count++] = (mouth_t){r, units(line->v1->x) + units(line->dx) / 2, units(line->v1->y) + units(line->dy) / 2,
                                                  side == 0 ? -dy : dy, side == 0 ? dx : -dx};
            }
        }
        for (int i = 0; i < mouth_count; ++i)
        {
            int r = mouths[i].room;
            if (kinds[r] != ROOM_INDOOR || out_len[r] < in_len[r] || room_area[r] > 384 * out_len[r]) continue;
            for (int j = i + 1; j < mouth_count; ++j)
            {
                /* Facing apart, with the room between them (not across a notch of sky it wraps round). */
                const mouth_t *a = &mouths[i], *b = &mouths[j];
                if (b->room != r || a->nx * b->nx + a->ny * b->ny > -0.7f || (b->x - a->x) * a->nx + (b->y - a->y) * a->ny >= 0) continue;
                for (int s = 0; s < n; ++s) if (kinds[s] == ROOM_INDOOR && find(s) == r) kinds[s] = ROOM_SHELTERED;
                changed = 1;
                break;
            }
        }
    }
    free(mouths); free(out_len); free(in_len);
    /* Spaces numbered in sector order: rooms and sheltered spaces by their
       join, each opening its own. */
    int *number = malloc((size_t)n * sizeof(int));
    for (int i = 0; i < n; ++i) number[i] = -1;
    spaces = 0;
    for (int s = 0; s < n; ++s)
    {
        if (kinds[s] == ROOM_SKY) { space[s] = -1; continue; }
        if (kinds[s] == ROOM_OPENING) { space[s] = spaces++; continue; }
        int r = find(s);
        if (number[r] < 0) number[r] = spaces++;
        space[s] = number[r];
    }
    /* Mouths: a room open to the outside with no door and no glass between,
       by a real gap straight onto a sky or sheltered sector, or through a
       doorless recess (an opening that is neither a door nor a window frame).
       Nobody heats such a room (drafts.cpp): a tunnel, a garage, a stairwell
       up to a yard. */
    unsigned char *framed = calloc((size_t)n, 1), *marked = calloc((size_t)n, 1);
    for (int i = 0; i < frame_count; ++i) framed[frames[i].sector] = 1;
    for (int i = 0; i < numlines; ++i)
    {
        const line_t *line = &lines[i];
        if (!line->frontsector || !line->backsector) continue;
        int sides[2] = {index_of(line->frontsector), index_of(line->backsector)};
        for (int side = 0; side < 2; ++side)
        {
            int a = sides[side], o = sides[1 - side];
            if (a == o || (kinds[o] != ROOM_SKY && kinds[o] != ROOM_SHELTERED)) continue;
            if (!(kinds[o] == ROOM_SKY ? sky_gap(a, o) : real_gap(a, o))) continue;
            if (kinds[a] == ROOM_INDOOR) marked[find(a)] = 1;
            else if (kinds[a] == ROOM_OPENING && !door[a] && !framed[a])
                for (int k = 0; k < sectors[a].linecount; ++k)
                {
                    const line_t *in = sectors[a].lines[k];
                    if (!in->backsector) continue;
                    int b = index_of(in->frontsector == &sectors[a] ? in->backsector : in->frontsector);
                    if (b != a && kinds[b] == ROOM_INDOOR && real_gap(a, b)) marked[find(b)] = 1;
                }
        }
    }
    for (int s = 0; s < n; ++s) doorless[s] = kinds[s] == ROOM_INDOOR && marked[find(s)];
    /* The open air: the sky, porches, and the doorless doorways and thin
       recesses straight off them (E1M6's 174): one ground with the yard for
       the weather (Rooms_Outside). A room behind them stays a room however
       it opens (E1M6's halls 122-125 open onto a yard through a doorless
       passage): the wind carries a little in at its mouth (open_air.cpp),
       the drafts the rest. */
    for (int s = 0; s < n; ++s) openair[s] = kinds[s] == ROOM_SKY || kinds[s] == ROOM_SHELTERED;
    for (int i = 0; i < numlines; ++i)
    {
        const line_t *line = &lines[i];
        if (!line->frontsector || !line->backsector) continue;
        int sides[2] = {index_of(line->frontsector), index_of(line->backsector)};
        for (int side = 0; side < 2; ++side)
        {
            int a = sides[side], o = sides[1 - side];
            if (a == o || kinds[a] != ROOM_OPENING || door[a] || framed[a]) continue;
            if (kinds[o] != ROOM_SKY && kinds[o] != ROOM_SHELTERED) continue;
            if (kinds[o] == ROOM_SKY ? sky_gap(a, o) : real_gap(a, o)) openair[a] = 1;
        }
    }
    if (getenv("ROOMSDEBUG"))
    {
        fprintf(stderr, "ROOMS open air indoors:");
        for (int s = 0; s < n; ++s) if (openair[s] && kinds[s] != ROOM_SKY && kinds[s] != ROOM_SHELTERED) fprintf(stderr, " %d", s);
        fprintf(stderr, "\n");
    }
    if (getenv("ROOMSDEBUG"))
        for (int s = 0; s < n; ++s)
        {
            if (!doorless[s] || find(s) != s) continue;
            fprintf(stderr, "  mouth space %d:", space[s]);
            for (int t = 0; t < n; ++t) if (doorless[t] && find(t) == s) fprintf(stderr, " %d", t);
            fprintf(stderr, "\n");
        }
    free(framed); free(marked);
    if (getenv("ROOMSDEBUG"))
    {
        int count[4] = {0, 0, 0, 0}, rooms = 0;
        for (int s = 0; s < n; ++s) ++count[kinds[s]];
        for (int s = 0; s < n; ++s) rooms += kinds[s] == ROOM_INDOOR && find(s) == s;
        fprintf(stderr, "ROOMS %d sectors: %d sky, %d indoor in %d rooms, %d openings (%d window frames), %d sheltered; %d spaces\n",
                n, count[ROOM_SKY], count[ROOM_INDOOR], rooms, count[ROOM_OPENING], frame_count, count[ROOM_SHELTERED], spaces);
        for (int s = 0; s < n; ++s)
        {
            if (find(s) != s || kinds[s] == ROOM_SKY || kinds[s] == ROOM_OPENING) continue;
            int many = 0;
            for (int t = 0; t < n; ++t) many += t != s && find(t) == s && kinds[t] == kinds[s];
            if (!many && kinds[s] != ROOM_SHELTERED) continue;
            fprintf(stderr, "  %s space %d:", kinds[s] == ROOM_SHELTERED ? "sheltered" : "room", space[s]);
            for (int t = 0; t < n; ++t) if (find(t) == s && kinds[t] == kinds[s]) fprintf(stderr, " %d", t);
            fprintf(stderr, kinds[s] == ROOM_SHELTERED ? " (%.0f%% open, %.0f deep)\n" : "\n", 100 * open[s] / edge[s], room_area[s] / fmaxf(open[s], 1));
        }
    }
    free(tags); free(doors); free(fixed); free(door); free(area); free(perimeter); free(pairs); free(root); root = NULL;
    free(members); free(edge); free(open); free(room_area); free(number); free(rims);
}

/* Enclosure for heat and sound, per sector, once the kinds are known.
   Under the sky the sun's rule (bake_sun.cpp: the share of the opening's
   edge onto roofed floor under a short rim, 0.3 open to 0.6 closed in), but
   only rooms and openings close it in: a porch is outside, so a courtyard
   ringed by walkways is open air (MAP03's lower one read 0.95 by the sun's).
   A porch from 0.2 at its open edge, closing in by its depth (its area over
   its edge onto the sky) to half that depth, the mean way in, over 192
   units, as the cells do (scene3d/rooms.cpp). Rooms and openings are 1. */
static void find_enclosure(void)
{
    int n = numsectors;
    float *rooms = calloc((size_t)sky_count + 1, sizeof(float));
    for (int o = 0; o < sky_count; ++o)
        for (int k = 0; k < skies[o].count; ++k)
        {
            room_kind kind = (room_kind)kinds[skies[o].indoor[k].sector];
            if (kind == ROOM_INDOOR || kind == ROOM_OPENING) rooms[o] += skies[o].indoor[k].length;
        }
    float *area = calloc((size_t)spaces + 1, sizeof(float)), *open = calloc((size_t)spaces + 1, sizeof(float));
    for (int i = 0; i < numlines; ++i)
    {
        const line_t *line = &lines[i];
        if (!line->frontsector) continue;
        float cross = units(line->v1->x) * units(line->v2->y) - units(line->v2->x) * units(line->v1->y);
        int f = index_of(line->frontsector), k = line->backsector ? index_of(line->backsector) : -1;
        if (kinds[f] == ROOM_SHELTERED) area[space[f]] -= cross / 2;
        if (k < 0 || k == f) continue;
        if (kinds[k] == ROOM_SHELTERED) area[space[k]] += cross / 2;
        for (int side = 0; side < 2; ++side)
        {
            int s = side ? k : f, o = side ? f : k;
            if (kinds[s] == ROOM_SHELTERED && kinds[o] == ROOM_SKY && sky_gap(s, o)) open[space[s]] += line_length(line);
        }
    }
    for (int s = 0; s < n; ++s)
    {
        float e = 1;
        if (kinds[s] == ROOM_SKY)
        {
            int o = sky_opening[s];
            float t = o >= 0 && skies[o].edge > 0 ? (rooms[o] / skies[o].edge - 0.3f) / 0.3f : 0;
            t = t < 0 ? 0.0f : t > 1 ? 1.0f : t;
            e = t * t * (3 - 2 * t);
        }
        else if (kinds[s] == ROOM_SHELTERED)
        {
            float depth = fabsf(area[space[s]]) / fmaxf(open[space[s]], 1), t = depth * 0.5f / 192;
            t = t > 1 ? 1.0f : t;
            e = 0.2f + 0.8f * t * t * (3 - 2 * t);
        }
        enclosure[s] = e;
    }
    if (getenv("ROOMSDEBUG"))
        for (int o = 0; o < sky_count; ++o)
        {
            int first = 0;
            while (first < n && sky_opening[first] != o) ++first;
            float all = 0;
            for (int k = 0; k < skies[o].count; ++k) all += skies[o].indoor[k].length;
            float t = skies[o].edge > 0 ? (all / skies[o].edge - 0.3f) / 0.3f : 0;
            t = t < 0 ? 0.0f : t > 1 ? 1.0f : t;
            if (first < n && skies[o].count > 0)
                fprintf(stderr, "  sky opening %d (sector %d): enclosure %.2f, the sun's %.2f\n", o, first, enclosure[first], t * t * (3 - 2 * t));
        }
    free(rooms); free(area); free(open);
}

/* Yards: the outside (sky and sheltered sectors) joined into areas the way
   the rooms join the inside, across every line between two outside sectors
   with a real gap (24 units: a walkway onto its yard, a ledge over a pit,
   one yard running on into the next). A sector with no height (a wall built
   of sky sectors, E1M8's) is in none. Numbered in sector order. */
static void find_yards(void)
{
    int n = numsectors;
    int *group = malloc((size_t)(n + 1) * sizeof(int));
    for (int s = 0; s < n; ++s) group[s] = s;
    for (int i = 0; i < numlines; ++i)
    {
        const line_t *line = &lines[i];
        if (!line->frontsector || !line->backsector) continue;
        int f = index_of(line->frontsector), k = index_of(line->backsector);
        if (f == k || kinds[f] == ROOM_INDOOR || kinds[f] == ROOM_OPENING || kinds[k] == ROOM_INDOOR || kinds[k] == ROOM_OPENING) continue;
        float gap = fminf(ceiling_of(&sectors[f]), ceiling_of(&sectors[k])) - fmaxf(floor_of(&sectors[f]), floor_of(&sectors[k]));
        if (gap < 24) continue;
        while (group[f] != f) f = group[f] = group[group[f]];
        while (group[k] != k) k = group[k] = group[group[k]];
        if (f != k) group[k] = f;
    }
    int *number = malloc((size_t)(n + 1) * sizeof(int));
    for (int s = 0; s < n; ++s) number[s] = -1;
    yard_count = 0;
    for (int s = 0; s < n; ++s)
    {
        yard[s] = -1;
        if (kinds[s] == ROOM_INDOOR || kinds[s] == ROOM_OPENING || height_of(s) <= 0) continue;
        int r = s;
        while (group[r] != r) r = group[r];
        if (number[r] < 0) number[r] = yard_count++;
        yard[s] = number[r];
    }
    if (getenv("ROOMSDEBUG"))
    {
        fprintf(stderr, "ROOMS %d yards\n", yard_count);
        for (int y = 0; y < yard_count; ++y)
        {
            fprintf(stderr, "  yard %d:", y);
            for (int s = 0; s < n; ++s) if (yard[s] == y) fprintf(stderr, " %d%s", s, kinds[s] == ROOM_SHELTERED ? "p" : "");
            fprintf(stderr, "\n");
        }
    }
    free(group); free(number);
}

void Rooms_Build(void)
{
    if (built == r_levelserial && built_sectors == numsectors) return;
    built = r_levelserial; built_sectors = numsectors;
    for (int i = 0; i < sky_count; ++i) free(skies[i].indoor);
    free(kinds); free(space); free(yard); free(doorless); free(openair); free(door_top); free(enclosure); free(frames); free(pane_frame); free(sky_opening); free(skies);
    kinds = malloc((size_t)numsectors + 1);
    space = malloc((size_t)(numsectors + 1) * sizeof(int));
    yard = malloc((size_t)(numsectors + 1) * sizeof(int));
    doorless = malloc((size_t)numsectors + 1);
    openair = calloc((size_t)numsectors + 1, 1);
    door_top = malloc((size_t)(numsectors + 1) * sizeof(float));
    enclosure = malloc((size_t)(numsectors + 1) * sizeof(float));
    frames = malloc((size_t)(numsectors + 1) * sizeof(rooms_frame));
    pane_frame = malloc((size_t)(numlines + 1) * sizeof(int)); pane_lines = numlines;
    sky_opening = malloc((size_t)(numsectors + 1) * sizeof(int));
    skies = malloc((size_t)(numsectors + 1) * sizeof(sky_t));
    sky_count = frame_count = spaces = yard_count = 0;
    clock_t start = clock();
    find_frames();
    find_sky_openings();
    classify();
    find_enclosure();
    find_yards();
    if (getenv("ROOMSDEBUG")) fprintf(stderr, "ROOMS built in %.2f ms\n", (clock() - start) * 1000.0 / CLOCKS_PER_SEC);
}

static int valid(int sector) { return built == r_levelserial && sector >= 0 && sector < built_sectors; }
room_kind Rooms_Kind(int sector) { return valid(sector) ? (room_kind)kinds[sector] : ROOM_SKY; }
int Rooms_Outside(int sector) { return valid(sector) ? openair[sector] : 1; }
int Rooms_Of(int sector) { return valid(sector) ? space[sector] : -1; }
int Rooms_Count(void) { return built == r_levelserial ? spaces : 0; }
int Rooms_Yard(int sector) { return valid(sector) ? yard[sector] : -1; }
int Rooms_YardCount(void) { return built == r_levelserial ? yard_count : 0; }
int Rooms_Mouth(int sector) { return valid(sector) ? doorless[sector] : 0; }
float Rooms_Enclosure(int sector) { return valid(sector) ? enclosure[sector] : 0; }
int Rooms_Door(int sector, float *top)
{
    if (!valid(sector) || door_top[sector] == -1e9f) return 0;
    if (top) *top = door_top[sector];
    return 1;
}
int Rooms_FrameCount(void) { return built == r_levelserial ? frame_count : 0; }
const rooms_frame *Rooms_Frame(int index) { return index >= 0 && index < Rooms_FrameCount() ? &frames[index] : NULL; }
int Rooms_PaneFrame(int line) { return built == r_levelserial && line >= 0 && line < pane_lines ? pane_frame[line] : -1; }
int Rooms_SkyOpening(int sector) { return valid(sector) ? sky_opening[sector] : -1; }
int Rooms_SkyOpeningCount(void) { return built == r_levelserial ? sky_count : 0; }
float Rooms_SkyEdge(int opening) { return opening >= 0 && opening < Rooms_SkyOpeningCount() ? skies[opening].edge : 0; }
int Rooms_SkyIndoorCount(int opening) { return opening >= 0 && opening < Rooms_SkyOpeningCount() ? skies[opening].count : 0; }
const rooms_sky_indoor *Rooms_SkyIndoor(int opening, int index)
{
    return index >= 0 && index < Rooms_SkyIndoorCount(opening) ? &skies[opening].indoor[index] : NULL;
}
