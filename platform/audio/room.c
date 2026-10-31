/* Measures the room around the listener from the map. Rays across the
 * map find the walls: one-sided lines, and openings too low to let
 * sound through (closed doors, raised lifts). Their distances give the
 * echoes; the room's floor area, height, sky share and the materials its
 * textures are named after give the tail, by Sabine's formula squeezed into
 * a game-like range: corridors slap, halls boom, the outdoors stays dry.
 * A long narrow tube (a sewer, a water channel) is told by its floor plan
 * and rings as its cross-section does, however long it is: a long dense
 * tail at once, a slight flutter between its side walls, and no clean
 * echo back off its far end.
 * How far outdoors the listener stands is the room map's (Map_Outdoors): a
 * porch sounds as the yard it opens onto, a hole in a hall's ceiling as the
 * hall. */
#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "doomdef.h"
#include "doomstat.h"
#include "p_local.h"
#include "r_data.h"
#include "w_wad.h"
#include "z_zone.h"
#include "map.h"
#include "relief.h"
#include "room.h"
#include "../game_materials.h"

#define RAYS ROOM_RAYS
#define SPEED 10700.0f  /* sound, in map units per second (a unit is ~3.2 cm) */
#define METERS 0.032f
#define EAR 41.0f       /* the listener's ear above their feet */
#define MAX_HITS 256

extern int numtextures, numflats;

static float snowing; /* how thick it snows (Room_Snow) */
void Room_Snow(float amount) { snowing = amount < 0 ? 0 : amount > 1 ? 1 : amount; }

/* absorb: share of sound a bounce loses; dark: how much of its treble;
   scatter: how rough it is to sound, as roughness is to light - smooth
   panels send an echo back clean and keep the room ringing, rough stone
   and dirt spread it and the room goes flat. Open air sends nothing back
   whole. seams: the seam share (relief.h) typical of the class's artwork
   in the IWADs; a texture with more raises its scatter by as much, so ribbed
   metal rings less than sheet metal and rubble less than plain blocks. Fewer
   seams never lower it: the reading misses some joints, and a smooth room
   ringing harder than its class wears on the ear. Liquids and open air have
   no relief. */
typedef struct { float absorb, dark, scatter, seams; } material_t;
static const material_t metal = {0.06f, 0.15f, 0.10f, 0.15f}, stone = {0.10f, 0.45f, 0.65f, 0.18f},
    wood = {0.22f, 0.70f, 0.45f, 0.18f}, flesh = {0.30f, 0.80f, 0.70f, 0.25f}, liquid = {0.03f, 0.30f, 0.05f, -1},
    soil = {0.40f, 0.75f, 0.85f, 0.25f}, fabric = {0.35f, 0.90f, 0.85f, 0.22f}, open_air = {1, 0, 1, -1};
#define RELIEF_REACH 0.25f  /* the most a texture's relief raises its class's scatter */

/* What the game's texture and flat names say they are made of
   (game_materials.h); names that match nothing sound like stone. */
static material_t surface_material(game_surface surface)
{
    switch (surface)
    {
    case GAME_SURFACE_METAL: return metal;
    case GAME_SURFACE_WOOD: return wood;
    case GAME_SURFACE_FLESH: return flesh;
    case GAME_SURFACE_SOIL: return soil;
    case GAME_SURFACE_LIQUID: return liquid;
    case GAME_SURFACE_FABRIC: return fabric;
    default: return stone;
    }
}
static material_t wall_material(const char *name) { return surface_material(Game_WallSurface(name)); }
static material_t flat_material(const char *name) { return surface_material(Game_FlatSurface(name)); }

/* A material with a texture's own relief: seams is its seam share, or -1. */
static material_t with_relief(material_t m, float seams)
{
    if (m.seams < 0 || seams < 0) return m;
    float shift = fmaxf(fminf(seams - m.seams, RELIEF_REACH), 0);
    m.scatter = fminf(fmaxf(m.scatter + shift, 0), 1);
    return m;
}

/* Per texture and flat, named once per WAD; their artwork is read the first
   time a ray meets them, so a level's start only reads what is near. */
static material_t *walls, *flats;
static unsigned char *walls_read, *flats_read;
static void name_materials(void)
{
    if (walls) return;
    walls = malloc(sizeof(*walls) * (numtextures > 0 ? numtextures : 1));
    flats = malloc(sizeof(*flats) * (numflats > 0 ? numflats : 1));
    walls_read = calloc(numtextures > 0 ? numtextures : 1, 1);
    flats_read = calloc(numflats > 0 ? numflats : 1, 1);
    char name[9];
    for (int i = 0; i < numtextures; ++i) { R_TextureName(i, name); walls[i] = wall_material(name); }
    for (int i = 0; i < numflats; ++i)
    {
        memcpy(name, lumpinfo[firstflat + i].name, 8); name[8] = 0;
        flats[i] = flat_material(name);
    }
}
static material_t wall(int texture)
{
    if (texture <= 0 || texture >= numtextures) return stone;
    if (!walls_read[texture])
    {
        walls_read[texture] = 1;
        int w, h;
        R_TextureDimensions(texture, &w, &h);
        unsigned char *pixels = w > 0 && h > 0 && w <= 4096 && h <= 4096 ? malloc((size_t)w * h * 2) : NULL;
        if (pixels)
        {
            R_CopyIndexedTexture(texture, pixels);
            walls[texture] = with_relief(walls[texture],
                Relief_Seams(pixels, w, h, (const unsigned char *)W_CacheLumpName("PLAYPAL", PU_CACHE)));
            free(pixels);
        }
    }
    return walls[texture];
}
static material_t flat(int pic)
{
    if (pic < 0 || pic >= numflats) return stone;
    if (!flats_read[pic])
    {
        flats_read[pic] = 1;
        if (W_LumpLength(firstflat + pic) >= 64 * 64)
        {
            static unsigned char pixels[64 * 64 * 2];
            const unsigned char *flat_data = W_CacheLumpNum(firstflat + pic, PU_CACHE);
            for (int i = 0; i < 64 * 64; ++i) { pixels[2 * i] = flat_data[i]; pixels[2 * i + 1] = 255; }
            flats[pic] = with_relief(flats[pic],
                Relief_Seams(pixels, 64, 64, (const unsigned char *)W_CacheLumpName("PLAYPAL", PU_CACHE)));
        }
    }
    return flats[pic];
}

/* One ray: how far it gets, off what, and the floors and ceilings it crosses. */
typedef struct {
    float distance; material_t surface;
    float length, sky_length, height, floor_absorb, floor_dark, ceiling_absorb, ceiling_dark;
    float floor_scatter, ceiling_scatter;
} ray_t;

static void cross_sector(ray_t *ray, sector_t *s, float length)
{
    material_t f = flat(s->floorpic), c = Map_Sky(s) ? open_air : flat(s->ceilingpic);
    ray->length += length;
    if (Map_Sky(s)) ray->sky_length += length;
    ray->height += length * (Map_Ceiling(s) - Map_Floor(s));
    ray->floor_absorb += length * f.absorb; ray->floor_dark += length * f.dark;
    ray->ceiling_absorb += length * c.absorb; ray->ceiling_dark += length * c.dark;
    ray->floor_scatter += length * f.scatter; ray->ceiling_scatter += length * c.scatter;
}

static ray_t cast(float x, float y, float cx, float cy, sector_t *start)
{
    static crossing_t hits[MAX_HITS];
    int count = Map_Cross(x, y, cx, cy, MAP_REACH, hits, MAX_HITS);
    ray_t ray = {MAP_REACH, open_air};
    sector_t *here = start;
    float travelled = 0;
    for (int i = 0; i < count; ++i)
    {
        const crossing_t *hit = &hits[i];
        cross_sector(&ray, here, hit->t - travelled);
        travelled = hit->t;
        line_t *line = hit->line;
        int side = line->sidenum[hit->near_side] >= 0 ? line->sidenum[hit->near_side] : line->sidenum[0];
        if (!hit->near || !hit->far)
        {
            ray.distance = travelled; ray.surface = side >= 0 ? wall(sides[side].midtexture) : stone;
            return ray;
        }
        float bottom, top;
        Map_Gap(hit->near, hit->far, &bottom, &top);
        if (top - bottom < MAP_MIN_GAP)
        {
            int raised = Map_Floor(hit->far) - Map_Floor(hit->near) > Map_Ceiling(hit->near) - Map_Ceiling(hit->far);
            ray.distance = travelled;
            ray.surface = side >= 0 ? wall(raised ? sides[side].bottomtexture : sides[side].toptexture) : stone;
            return ray;
        }
        here = hit->far;
    }
    cross_sector(&ray, here, MAP_REACH - travelled);
    return ray;
}

static void dry(room_t *room) { memset(room, 0, sizeof(*room)); room->decay = room->room_decay = 0.2f; }

/* A tube is this many times longer than it is across (or high) at the
   least, and wholly one from TUBE_FULL on; and narrow: up to TUBE_WIDE
   across, and none at twice that - a long hall is no sewer. */
#define TUBE_START 3.5f
#define TUBE_FULL 7.0f
#define TUBE_WIDE 256.0f
#define TUBE_SIDE_RAYS 9     /* each side: the rays 45 degrees either way of straight across */
#define TUBE_SIDE_READ 3     /* each side's width is read this low among them (from 0) */
#define TUBE_GRAZING 2.0f    /* how much longer sound lasts running along a tube than Sabine's diffuse room */
#define TUBE_LONGER 0.5f     /* seconds a tube's tail may run past a room's longest */
#define TUBE_MOST 2.5f       /* and the most times the room's own tail it may last */
#define TUBE_SIZE 0.8f       /* how loud a tube's tail is, as a room's size (0-1) sets it */
#define TUBE_FAR_CUT 0.7f    /* how much of the echoes from along a tube is spread away */
typedef struct { float tunnel, axis, width, length, absorb, scatter; } tube_t;

static float smoothstep01(float x) { x = x < 0 ? 0 : x > 1 ? 1 : x; return x * x * (3 - 2 * x); }

/* Whether the floor plan is a long narrow tube. Its axis is the longest pair
   of opposite rays; how far each side wall is, the rays meeting it measured
   straight across, read low among them, so a doorway or an alcove in it
   doesn't widen the tube (a corner of a box, its walls close only near the
   corner, stays no tube). Open sky above takes it away: a canyon or a
   street is no tunnel. So does width: a long hall is no tunnel either. */
static tube_t tube(const ray_t *rays, float height, float sky_share)
{
    const float step = 2 * (float)M_PI / RAYS;
    tube_t t = {0};
    int axis = 0;
    for (int i = 0; i < RAYS / 2; ++i)
    {
        float length = rays[i].distance + rays[i + RAYS / 2].distance;
        if (length > t.length) { t.length = length; axis = i; }
    }
    float absorb = 0, scatter = 0;
    for (int side = 0; side < 2; ++side)
    {
        float across[TUBE_SIDE_RAYS];
        for (int j = 0; j < TUBE_SIDE_RAYS; ++j)
        {
            const ray_t *r = &rays[(axis + side * RAYS / 2 + RAYS / 8 + j) % RAYS];
            float v = r->distance * fabsf(sinf((RAYS / 8 + j) * step));
            int k = j;
            for (; k > 0 && across[k - 1] > v; --k) across[k] = across[k - 1];
            across[k] = v;
            absorb += r->surface.absorb; scatter += r->surface.scatter;
        }
        t.width += across[TUBE_SIDE_READ];
    }
    t.absorb = absorb / (2 * TUBE_SIDE_RAYS);
    t.scatter = scatter / (2 * TUBE_SIDE_RAYS);
    t.axis = axis * step;
    float across = fmaxf(fmaxf(t.width, height), 1);
    t.tunnel = smoothstep01((t.length / across - TUBE_START) / (TUBE_FULL - TUBE_START)) * (1 - sky_share)
               * (1 - smoothstep01((t.width - TUBE_WIDE) / TUBE_WIDE));
    return t;
}

/* The room around (x, y) in start, heard by an ear at height ear facing
   facing (radians); its floor plan to outline. */
static void measure(float x, float y, float ear, sector_t *start, float facing, room_t *room, room_outline_t *outline)
{
    name_materials();
    Map_Gather(x, y);

    static ray_t rays[RAYS];
    const float step = 2 * (float)M_PI / RAYS;
    for (int i = 0; i < RAYS; ++i) rays[i] = cast(x, y, cosf(i * step), sinf(i * step), start);
    if (outline)
    {
        outline->x = x; outline->y = y;
        for (int i = 0; i < RAYS; ++i) outline->reach[i] = rays[i].distance;
    }

    /* The floor plan the rays outline, and its walls. */
    float area = 0, perimeter = 0, wall_absorb = 0, wall_dark = 0, wall_scatter = 0, open = 0;
    ray_t total = {0};
    for (int i = 0; i < RAYS; ++i)
    {
        const ray_t *a = &rays[i], *b = &rays[(i + 1) % RAYS];
        area += 0.5f * a->distance * b->distance * sinf(step);
        float chord = sqrtf(a->distance * a->distance + b->distance * b->distance
                            - 2 * a->distance * b->distance * cosf(step));
        perimeter += chord;
        wall_absorb += chord * 0.5f * (a->surface.absorb + b->surface.absorb);
        wall_dark += chord * 0.5f * (a->surface.dark + b->surface.dark);
        wall_scatter += chord * 0.5f * (a->surface.scatter + b->surface.scatter);
        if (a->surface.absorb >= 1) open += 1.0f / RAYS;
        total.length += a->length; total.sky_length += a->sky_length; total.height += a->height;
        total.floor_absorb += a->floor_absorb; total.floor_dark += a->floor_dark;
        total.ceiling_absorb += a->ceiling_absorb; total.ceiling_dark += a->ceiling_dark;
        total.floor_scatter += a->floor_scatter; total.ceiling_scatter += a->ceiling_scatter;
    }
    float length = fmaxf(total.length, 1);
    float height = fmaxf(total.height / length, 16), sky_share = total.sky_length / length;
    float floor_absorb = total.floor_absorb / length, ceiling_absorb = total.ceiling_absorb / length;
    perimeter = fmaxf(perimeter, 1);

    /* Sabine: the tail lasts 0.161 V / A seconds, A the room's absorbing area. */
    float floor_m2 = area * METERS * METERS, height_m = height * METERS, walls_m2 = perimeter * METERS * height_m;
    float volume = floor_m2 * height_m;
    if (outline) outline->volume = volume;
    float absorbing = floor_m2 * (floor_absorb + ceiling_absorb) + walls_m2 * wall_absorb / perimeter;
    float sabine = 0.161f * volume / fmaxf(absorbing, 0.01f);
    /* Down a tube Sabine's diffuse room no longer holds: sound running along
       it meets the walls at grazing angles and lasts. Its tail is set by the
       cross-section alone, Sabine over one slice of it with the walls
       absorbing as little as they do at a graze; how long the tube is
       doesn't enter. A water floor's low absorption shows here on its own. */
    tube_t shape = tube(rays, height, sky_share);
    float width_m = shape.width * METERS;
    float slice = width_m * (floor_absorb + ceiling_absorb) + 2 * height_m * shape.absorb;
    float along = TUBE_GRAZING * 0.161f * width_m * height_m / fmaxf(slice, 0.01f);
    float tubed = sabine + (fminf(along, TUBE_MOST * sabine) - sabine) * shape.tunnel;
    /* Standing out in the open air, most of the sound never comes back. */
    float out = Map_Outdoors(start), outdoors = 1 - 0.6f * out;
    room->decay = fminf(fmaxf(0.5f * tubed * (1 - 0.4f * out), 0.2f), 2.5f + TUBE_LONGER * shape.tunnel);
    room->room_decay = fminf(fmaxf(0.5f * sabine * (1 - 0.4f * out), 0.2f), 2.5f);
    float dark_area = floor_m2 * (total.floor_dark + total.ceiling_dark) / length + walls_m2 * wall_dark / perimeter;
    room->damping = fminf(dark_area / fmaxf(2 * floor_m2 + walls_m2, 0.01f), 1);
    float rough_area = floor_m2 * (total.floor_scatter + total.ceiling_scatter) / length + walls_m2 * wall_scatter / perimeter;
    room->scatter = fminf(rough_area / fmaxf(2 * floor_m2 + walls_m2, 0.01f), 1);
    /* Bigger rooms ring louder; sky and open air above and around take it away. */
    float size = fminf(fmaxf((log10f(fmaxf(volume, 1)) - 1.7f) / 2.0f, 0), 1);
    size = size * size * (3 - 2 * size);
    /* A tube rings loud however small it is: closed all round, little gets out. */
    size += (fmaxf(TUBE_SIZE, size) - size) * shape.tunnel;
    room->late = outdoors * (0.35f + 0.65f * size) * (1 - 0.8f * sky_share) * (1 - 0.6f * open);

    /* An echo off the walls in each eighth of the circle, panned to where it is
       as the listener faces; Doom's angles turn left, so left is +90 degrees. */
    const int per_tap = RAYS / 8;
    for (int k = 0; k < 8; ++k)
    {
        /* The walls that answer set the delay; open air only weakens the echo. */
        float distance = 0, reflect = 0, rough = 0, dull = 0;
        for (int i = 0; i < per_tap; ++i)
        {
            const ray_t *r = &rays[k * per_tap + i];
            distance += r->distance * (1 - r->surface.absorb);
            reflect += (1 - r->surface.absorb) / per_tap;
            rough += r->surface.scatter * (1 - r->surface.absorb);
            dull += r->surface.dark * (1 - r->surface.absorb);
        }
        room->spread[k] = reflect > 0 ? rough / (reflect * per_tap) : 0;
        room->dark[k] = reflect > 0 ? dull / (reflect * per_tap) : 0;
        distance = reflect > 0 ? distance / (reflect * per_tap) : MAP_REACH;
        float bearing = (k * per_tap + 0.5f * (per_tap - 1)) * step - facing;
        room->delay[k] = 2 * distance / SPEED;
        room->gain[k] = reflect * 0.35f / (1 + distance / 320);
        room->pan[k] = -sinf(bearing);
        /* Down a tube the far reaches answer late and spread thin along its
           walls: no clean copy off the far end, a smear into the tail. The
           echoes off the side walls stay, close and dense. */
        float lined_up = smoothstep01((fabsf(cosf(bearing + facing - shape.axis)) - 0.5f) / 0.44f) * shape.tunnel;
        room->gain[k] *= 1 - TUBE_FAR_CUT * lined_up;
        room->spread[k] += (1 - room->spread[k]) * lined_up;
    }
    /* Across a tube the sound goes back and forth between its side walls: a
       flutter, as strong as it is a tube. Rough walls blur it only partly
       (brick still flutters in the mids), and a smooth floor such as water
       carries it. */
    room->tunnel = shape.tunnel;
    room->axis = shape.axis - facing;
    room->width = shape.width;
    room->height = height;
    room->flutter_delay = 2 * shape.width / SPEED;
    float floor_scatter = total.floor_scatter / length;
    room->flutter = fminf(shape.tunnel * (1 - 0.5f * shape.scatter) * (0.85f + 0.3f * (1 - floor_scatter)), 1);
    /* And one off the ceiling, unless it is sky. */
    float above = Map_Ceiling(start) - ear;
    int ceiling = ROOM_TAPS - 1;
    room->delay[ceiling] = 2 * fmaxf(above, 8) / SPEED;
    room->gain[ceiling] = Map_Sky(start) ? 0 : (1 - flat(start->ceilingpic).absorb) * 0.25f / (1 + fmaxf(above, 8) / 320);
    room->pan[ceiling] = 0;
    room->spread[ceiling] = flat(start->ceilingpic).scatter;
    room->dark[ceiling] = flat(start->ceilingpic).dark;
    /* Out in the snow it swallows what little comes back, the treble first:
       a hush, the echoes off the walls faint and dull. */
    float hush = snowing * out;
    if (hush > 0)
    {
        room->late *= 1 - 0.6f * hush;
        room->damping = fmaxf(room->damping, 0.85f * hush);
        room->scatter = fmaxf(room->scatter, 0.8f * hush);
        for (int k = 0; k < ROOM_TAPS; ++k)
        {
            room->gain[k] *= 1 - 0.55f * hush;
            room->dark[k] = fmaxf(room->dark[k], 0.85f * hush);
        }
    }
}

static void dry_outline(room_outline_t *outline, float x, float y)
{
    if (!outline) return;
    memset(outline, 0, sizeof(*outline));
    outline->x = x; outline->y = y;
}

void Room_Measure(room_t *room, room_outline_t *outline)
{
    mobj_t *listener = players[consoleplayer].mo;
    if (gamestate != GS_LEVEL || !listener || !listener->subsector || !numlines)
    { dry(room); dry_outline(outline, 0, 0); return; }
    float x = listener->x / (float)FRACUNIT, y = listener->y / (float)FRACUNIT;
    measure(x, y, listener->z / (float)FRACUNIT + EAR, listener->subsector->sector,
            listener->angle * (2 * (float)M_PI / 4294967296.0f), room, outline);
}

void Room_MeasureAt(float x, float y, float facing, room_t *room, room_outline_t *outline)
{
    if (gamestate != GS_LEVEL || !numlines) { dry(room); dry_outline(outline, x, y); return; }
    sector_t *sector = R_PointInSubsector((fixed_t)(x * FRACUNIT), (fixed_t)(y * FRACUNIT))->sector;
    measure(x, y, Map_Floor(sector) + EAR, sector, facing, room, outline);
}
