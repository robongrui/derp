/* A room's tail traced from the map. See room_trace.h. */
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "p_local.h"
#include "r_state.h"
#include "map.h"
#include "room.h"
#include "room_trace.h"

#define SPEED 10700.0f        /* sound, map units per second, as room.c */
#define EAR 41.0f             /* the ear above the floor, as room.c */
#define CATCH 40.0f           /* the listener catches what passes this near the ear */
#define TRACED_SECONDS 1.2f   /* rays go this long; the tail after is how they were falling */
#define SPENT 1e-6f           /* a ray down to this of what it set out with is spent */
#define MOST_LEGS 4000
#define BLOCK 128.0f          /* the blockmap's */
#define PI_F 3.14159265f

/* Each map unit of air takes this much of a band, in dB: none of the lows,
   a little of the mids, more of the highs (about 0.03 dB a metre at 4 kHz). */
static const float air_db[TRACE_BANDS] = {0, 1.3e-4f, 9.6e-4f};

static float uniform(unsigned *s)
{
    *s ^= *s << 13; *s ^= *s >> 17; *s ^= *s << 5;
    return (*s >> 8) * (1.0f / 16777216.0f);
}

/* What a bounce keeps of each band off a surface room.c knows as absorb
   and dark: the lows lose less than the mids, the highs what dark takes too. */
static void keeps(float absorb, float dark, float keep[TRACE_BANDS])
{
    keep[0] = 1 - 0.7f * absorb;
    keep[1] = 1 - absorb;
    keep[2] = (1 - absorb) * (1 - 0.6f * dark);
}

/* The nearest line a ray from (x, y) along unit (ux, uy) crosses within
   reach, walking the blockmap: its number and distance, or -1. skip is
   the line the ray just left, not met again right where it starts. */
typedef struct { int *stamp, mark; } seen_t;
static int nearest_line(seen_t *seen, float x, float y, float ux, float uy, float reach, int skip, float *at)
{
    const float ox = bmaporgx / (float)FRACUNIT, oy = bmaporgy / (float)FRACUNIT;
    float fx = (x - ox) / BLOCK, fy = (y - oy) / BLOCK;
    int bx = (int)floorf(fx), by = (int)floorf(fy);
    int step_x = ux > 0 ? 1 : -1, step_y = uy > 0 ? 1 : -1;
    float ax = fabsf(ux), ay = fabsf(uy);
    float delta_x = ax > 1e-9f ? BLOCK / ax : 1e30f, delta_y = ay > 1e-9f ? BLOCK / ay : 1e30f;
    float next_x = ax > 1e-9f ? (ux > 0 ? bx + 1 - fx : fx - bx) * BLOCK / ax : 1e30f;
    float next_y = ay > 1e-9f ? (uy > 0 ? by + 1 - fy : fy - by) * BLOCK / ay : 1e30f;
    ++seen->mark;
    float best = 1e30f;
    int found = -1;
    while (bx >= 0 && by >= 0 && bx < bmapwidth && by < bmapheight)
    {
        for (const int32_t *list = blockmaplump + blockmap[by * bmapwidth + bx]; *list != -1; ++list)
        {
            int n = *list;
            if (n < 0 || n >= numlines || seen->stamp[n] == seen->mark) continue;
            seen->stamp[n] = seen->mark;
            const line_t *l = &lines[n];
            float lx = l->dx / (float)FRACUNIT, ly = l->dy / (float)FRACUNIT;
            float denom = ux * ly - uy * lx;
            if (fabsf(denom) < 1e-9f) continue;
            float px = l->v1->x / (float)FRACUNIT - x, py = l->v1->y / (float)FRACUNIT - y;
            float u = (px * ly - py * lx) / denom, s = (px * uy - py * ux) / denom;
            if (s < 0 || s > 1 || u <= 1e-3f || u >= best || (n == skip && u < 1)) continue;
            best = u; found = n;
        }
        float leave = fminf(next_x, next_y);
        if (best <= leave || leave > reach) break;
        if (next_x < next_y) { next_x += delta_x; bx += step_x; }
        else { next_y += delta_y; by += step_y; }
    }
    if (found < 0 || best > reach) return -1;
    *at = best;
    return found;
}

/* A new way off a surface: mirrored, or (as rough as it is) any way
   out of it, more often straight out. */
static void bounce(float d[3], const float n[3], float scatter, unsigned *rng)
{
    if (uniform(rng) >= scatter)
    {
        float k = 2 * (d[0] * n[0] + d[1] * n[1] + d[2] * n[2]);
        for (int i = 0; i < 3; ++i) d[i] -= k * n[i];
        return;
    }
    float a[3] = {fabsf(n[0]) < 0.9f ? 1.0f : 0.0f, fabsf(n[0]) < 0.9f ? 0.0f : 1.0f, 0};
    float t[3] = {n[1] * a[2] - n[2] * a[1], n[2] * a[0] - n[0] * a[2], n[0] * a[1] - n[1] * a[0]};
    float tl = sqrtf(t[0] * t[0] + t[1] * t[1] + t[2] * t[2]);
    for (int i = 0; i < 3; ++i) t[i] /= tl;
    float b[3] = {n[1] * t[2] - n[2] * t[1], n[2] * t[0] - n[0] * t[2], n[0] * t[1] - n[1] * t[0]};
    float phi = 2 * PI_F * uniform(rng), r2 = uniform(rng), sine = sqrtf(r2), up = sqrtf(1 - r2);
    for (int i = 0; i < 3; ++i) d[i] = n[i] * up + (t[i] * cosf(phi) + b[i] * sinf(phi)) * sine;
}

typedef struct {
    room_trace_t *out;
    const unsigned char *glass;
    float x, y, ear, scale;
    int traced;
} listener_t;

/* What a leg from p along d, t long, leaves with the listener: the length
   of it inside the listener's ball, arriving path units after the sound. */
static void catch_leg(const listener_t *l, const float p[3], const float d[3], float t, float path, const float e[3])
{
    float rx = l->x - p[0], ry = l->y - p[1], rz = l->ear - p[2];
    float along = rx * d[0] + ry * d[1] + rz * d[2];
    if (along < -CATCH || along > t + CATCH) return;
    float cx = rx - d[0] * along, cy = ry - d[1] * along, cz = rz - d[2] * along;
    float miss = cx * cx + cy * cy + cz * cz;
    if (miss >= CATCH * CATCH) return;
    float half = sqrtf(CATCH * CATCH - miss);
    float inside = fminf(along + half, t) - fmaxf(along - half, 0);
    if (inside <= 0) return;
    int bin = (int)((path + fminf(fmaxf(along, 0), t)) / SPEED / TRACE_BIN);
    if (bin >= l->traced) return;
    /* From where it comes: back along the way it travels. */
    float flat = d[0] * d[0] + d[1] * d[1];
    float way = atan2f(-d[1], -d[0]) / (PI_F / 4);
    if (way < 0) way += TRACE_WAYS;
    int k0 = (int)way % TRACE_WAYS, k1 = (k0 + 1) % TRACE_WAYS;
    float f = way - floorf(way);
    for (int b = 0; b < TRACE_BANDS; ++b)
    {
        float energy = e[b] * inside * l->scale, round = energy * (1 - flat) / TRACE_WAYS;
        float *bins = l->out->energy[bin][b];
        bins[k0] += energy * flat * (1 - f);
        bins[k1] += energy * flat * f;
        for (int k = 0; k < TRACE_WAYS; ++k) bins[k] += round;
    }
}

/* One ray, until it is spent, escapes or runs out of time; what of it escaped. */
static float send_ray(const listener_t *l, seen_t *seen, sector_t *s, float d[3], unsigned *rng)
{
    float p[3] = {l->x, l->y, l->ear}, e[TRACE_BANDS] = {1, 1, 1}, path = 0;
    int order = 0, skip = -1;
    const float most = TRACED_SECONDS * SPEED;
    for (int leg = 0; leg < MOST_LEGS; ++leg)
    {
        float floor = Map_Floor(s), ceiling = Map_Ceiling(s);
        int sky = Map_Sky(s);
        if (sky && p[2] >= ceiling) break;
        float t_plane = 1e30f;
        int plane = 0;
        if (d[2] < -1e-6f) { t_plane = (floor - p[2]) / d[2]; plane = -1; }
        else if (d[2] > 1e-6f) { t_plane = (ceiling - p[2]) / d[2]; plane = 1; }
        if (t_plane < 0) t_plane = 0;
        float flat = sqrtf(d[0] * d[0] + d[1] * d[1]), u = 0;
        int n = flat > 1e-6f ? nearest_line(seen, p[0], p[1], d[0] / flat, d[1] / flat, fminf(t_plane * flat, 1e6f), skip, &u) : -1;
        float t = n >= 0 ? u / flat : t_plane;
        if (t >= 1e29f) break;  /* level ground nowhere: out of the map */
        if (order >= 2) catch_leg(l, p, d, t, path, e);
        for (int i = 0; i < 3; ++i) p[i] += d[i] * t;
        path += t;
        for (int b = 0; b < TRACE_BANDS; ++b) e[b] *= powf(10, -0.1f * air_db[b] * t);
        if (path > most) return 0;
        float normal[3] = {0, 0, 0}, absorb, dark, scatter;
        if (n < 0)
        {
            if (plane > 0 && sky) break;
            Room_Material(plane > 0 ? s->ceilingpic : s->floorpic, 1, &absorb, &dark, &scatter);
            normal[2] = (float)-plane;
            p[2] = plane > 0 ? ceiling - 0.01f : floor + 0.01f;
            skip = -1;
        }
        else
        {
            const line_t *line = &lines[n];
            float lx = line->dx / (float)FRACUNIT, ly = line->dy / (float)FRACUNIT, length = sqrtf(lx * lx + ly * ly);
            float front_x = ly / length, front_y = -lx / length;  /* the front side is on the right */
            int near_side = d[0] * front_x + d[1] * front_y < 0 ? 0 : 1;
            int near_def = line->sidenum[near_side] >= 0 ? line->sidenum[near_side] : line->sidenum[0];
            int far_def = line->sidenum[near_side ^ 1];
            sector_t *near = near_def >= 0 ? sides[near_def].sector : s;
            sector_t *far = far_def >= 0 && far_def != near_def ? sides[far_def].sector : NULL;
            int texture = near_def >= 0 ? sides[near_def].midtexture : 0;
            skip = n;
            if (far)
            {
                float bottom, top;
                Map_Gap(near, far, &bottom, &top);
                int pane = l->glass && l->glass[n] && p[2] > bottom && p[2] < top;
                if (!pane && top - bottom >= MAP_MIN_GAP && p[2] > bottom && p[2] < top)
                {
                    /* Through the opening, into the next sector. */
                    s = far;
                    if (p[2] < Map_Floor(s) + 0.01f) p[2] = Map_Floor(s) + 0.01f;
                    if (!Map_Sky(s) && p[2] > Map_Ceiling(s) - 0.01f) p[2] = Map_Ceiling(s) - 0.01f;
                    continue;
                }
                texture = near_def >= 0 ? (p[2] <= bottom ? sides[near_def].bottomtexture : sides[near_def].toptexture) : 0;
                if (pane) texture = -1;
            }
            s = near;
            if (texture < 0) Room_Pane(&absorb, &dark, &scatter);
            else Room_Material(texture, 0, &absorb, &dark, &scatter);
            float side = near_side == 0 ? 1.0f : -1.0f;
            normal[0] = front_x * side; normal[1] = front_y * side;
            p[0] += normal[0] * 0.01f; p[1] += normal[1] * 0.01f;
        }
        float keep[TRACE_BANDS], left = 0;
        keeps(absorb, dark, keep);
        for (int b = 0; b < TRACE_BANDS; ++b) { e[b] *= keep[b]; left = fmaxf(left, e[b]); }
        ++order;
        if (left < SPENT) return 0;
        bounce(d, normal, scatter, rng);
    }
    return (e[0] + e[1] + e[2]) / 3;
}

/* Rays are few next to a room's echoes: each bin's energy is smoothed over
   neighbours, wider as the tail goes on and the sound has spread, and its
   directions wider still. The tail's fall is fitted per band, and past where
   the rays thin out it goes on falling so. */
static void finish(room_trace_t *out, int traced)
{
    static const int ways = TRACE_WAYS;
    float (*total)[TRACE_BANDS] = calloc(TRACE_BINS, sizeof(*total));
    float (*smooth)[TRACE_BANDS] = calloc(TRACE_BINS, sizeof(*smooth));
    float (*share)[TRACE_BANDS][TRACE_WAYS] = calloc(TRACE_BINS, sizeof(*share));
    if (!total || !smooth || !share) { free(total); free(smooth); free(share); return; }
    int first = traced;
    for (int i = 0; i < traced; ++i)
        for (int b = 0; b < TRACE_BANDS; ++b)
        {
            for (int k = 0; k < ways; ++k) total[i][b] += out->energy[i][b][k];
            if (total[i][b] > 0 && i < first) first = i;
        }
    for (int i = first; i < traced; ++i)
    {
        int wide = 1 + (i - first) / 12, wider = 4 + (i - first) / 4;
        for (int b = 0; b < TRACE_BANDS; ++b)
        {
            float sum = 0, dir[TRACE_WAYS] = {0}, all = 0;
            int count = 0;
            for (int j = i - wide; j <= i + wide; ++j)
                if (j >= first && j < traced) { sum += total[j][b]; ++count; }
            smooth[i][b] = sum / count;
            for (int j = i - wider; j <= i + wider; ++j)
                if (j >= first && j < traced)
                    for (int k = 0; k < ways; ++k) { dir[k] += out->energy[j][b][k]; all += out->energy[j][b][k]; }
            for (int k = 0; k < ways; ++k) share[i][b][k] = all > 0 ? dir[k] / all : 1.0f / ways;
        }
    }
    /* Where the rays still say something: down 60 dB from the loudest. */
    float peak = 0;
    for (int i = first; i < traced; ++i) peak = fmaxf(peak, smooth[i][1]);
    int end = first;
    for (int i = first; i < traced; ++i) if (smooth[i][1] > peak * 1e-6f) end = i;
    int from = first + (int)(0.05f / TRACE_BIN);
    if (from < (int)(0.08f / TRACE_BIN)) from = (int)(0.08f / TRACE_BIN);
    for (int b = 0; b < TRACE_BANDS; ++b)
    {
        double sx = 0, sy = 0, sxx = 0, sxy = 0;
        int count = 0;
        for (int i = from; i <= end; ++i)
        {
            if (smooth[i][b] <= 0) continue;
            double tx = i * TRACE_BIN, ty = 10 * log10(smooth[i][b]);
            sx += tx; sy += ty; sxx += tx * tx; sxy += tx * ty; ++count;
        }
        double slope = count >= 10 ? (count * sxy - sx * sy) / (count * sxx - sx * sx) : 0;
        out->decay[b] = slope < -1 ? (float)(-60 / slope) : b ? out->decay[b - 1] : 0.5f;
        if (out->decay[b] < 0.1f) out->decay[b] = 0.1f;
        if (out->decay[b] > 6) out->decay[b] = 6;
    }
    /* The tail past where the rays thin out, as it was falling there. */
    int tail_from = end - (int)(0.1f / TRACE_BIN);
    if (tail_from < from) tail_from = end;
    float level[TRACE_BANDS], dir[TRACE_BANDS][TRACE_WAYS];
    for (int b = 0; b < TRACE_BANDS; ++b)
    {
        level[b] = 0;
        int count = 0;
        for (int i = tail_from; i <= end; ++i)
        {
            /* Each bin brought forward to end along its band's fall. */
            level[b] += smooth[i][b] * powf(10, -6 * (end - i) * TRACE_BIN / out->decay[b]);
            ++count;
        }
        level[b] = count ? level[b] / count : 0;
        for (int k = 0; k < ways; ++k) dir[b][k] = share[end][b][k];
    }
    for (int i = first; i < TRACE_BINS; ++i)
        for (int b = 0; b < TRACE_BANDS; ++b)
        {
            float energy = i <= end ? smooth[i][b] : level[b] * powf(10, -6 * (i - end) * TRACE_BIN / out->decay[b]);
            for (int k = 0; k < ways; ++k) out->energy[i][b][k] = energy * (i <= end ? share[i][b][k] : dir[b][k]);
        }
    out->traced = end + 1;
    out->bins = TRACE_BINS;
    for (int i = end; i < TRACE_BINS; ++i)
    {
        float all = 0;
        for (int b = 0; b < TRACE_BANDS; ++b)
            for (int k = 0; k < ways; ++k) all += out->energy[i][b][k];
        if (all < peak * 1e-7f) { out->bins = i; break; }
    }
    for (int i = out->bins; i < TRACE_BINS; ++i) memset(out->energy[i], 0, sizeof(out->energy[i]));
    free(total); free(smooth); free(share);
}

void RoomTrace_At(float x, float y, int rays, unsigned seed, const unsigned char *glass, room_trace_t *out)
{
    memset(out, 0, sizeof(*out));
    out->rays = rays;
    if (!numlines || rays <= 0) return;
    sector_t *start = R_PointInSubsector((fixed_t)(x * FRACUNIT), (fixed_t)(y * FRACUNIT))->sector;
    float ear = Map_Floor(start) + EAR;
    if (!Map_Sky(start) && ear > Map_Ceiling(start) - 1) ear = 0.5f * (Map_Floor(start) + Map_Ceiling(start));
    seen_t seen = {calloc((size_t)numlines, sizeof(int)), 0};
    if (!seen.stamp) return;
    /* A ray's energy over the ball's volume, to the sound heard TRACE_REFERENCE away. */
    float ball = 4.0f / 3 * PI_F * CATCH * CATCH * CATCH;
    listener_t l = {out, glass, x, y, ear, 4 * PI_F * TRACE_REFERENCE * TRACE_REFERENCE / ball / rays, (int)(TRACED_SECONDS / TRACE_BIN)};
    unsigned rng = (seed * 2654435761u + 0x9e3779b9u) | 1;
    float turn = 2 * PI_F * uniform(&rng), escaped = 0;
    for (int r = 0; r < rays; ++r)
    {
        /* Evenly every way round (a Fibonacci sphere), turned by the seed. */
        float up = 1 - 2 * (r + 0.5f) / rays, round = sqrtf(fmaxf(0, 1 - up * up)), phi = r * 2.39996323f + turn;
        float d[3] = {round * cosf(phi), round * sinf(phi), up};
        escaped += send_ray(&l, &seen, start, d, &rng);
    }
    out->escaped = escaped / rays;
    free(seen.stamp);
    finish(out, l.traced);
}
