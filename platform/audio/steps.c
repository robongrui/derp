/* Footsteps. The player's fall with their stride (stride.h), which the
 * weapon bob keeps time with: about twice a second walking and three times
 * running, harder. Landing from a fall is a step too, harder the faster the
 * fall. Each step plays the footfall and the kit after it. Monsters are
 * followed while they wade, for their splashes, and DERP's soldiers on any
 * floor. See steps.h. */
#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "doomstat.h"
#include "p_local.h"
#include "w_wad.h"
#include "impact_synth.h"
#include "steps.h"
#include "library_steps.h"
#include "../game_profile.h"
#include "../stride.h"
#include "../game_materials.h"
#include "frozen.h"
#include "gore_floor.h"

#define WADERS 64
#define MONSTER_STRIDE 44.0f  /* monsters walk in jumps; a splash every so many units */
#define WADE_REACH 1024.0f
#define BOOT_REACH 1100.0f    /* a soldier's boots: you follow them round a room */
#define BOOT_LEVEL 0.6f
#define CLAW_REACH 900.0f     /* an imp's claws: a nearer warning than boots */
#define CLAW_LEVEL 0.6f

extern int numflats;


/* What a floor is made of (Game_FlatSurface), as a step: the same class
   the room's echo hears, so the two can't disagree. Water splashes light,
   other liquids thick. */
static int step_kind(const char *name)
{
    switch (Game_FlatSurface(name))
    {
    case GAME_SURFACE_LIQUID: return Game_LiquidFlat(name) == GAME_LIQUID_THICK ? IMPACT_SPLASH_THICK : IMPACT_SPLASH_WATER;
    case GAME_SURFACE_METAL: return IMPACT_STEP_METAL;
    case GAME_SURFACE_WOOD: return IMPACT_STEP_WOOD;
    case GAME_SURFACE_FLESH: case GAME_SURFACE_SOIL: case GAME_SURFACE_FABRIC: return IMPACT_STEP_SOFT;
    default: return IMPACT_STEP_STONE;
    }
}

/* What a floor flat sounds like underfoot, named once per WAD. */
static int floor_kind(int pic)
{
    static signed char *kinds;
    static int known;
    if (known != numflats)
    {
        free(kinds);
        kinds = malloc((size_t)(numflats > 0 ? numflats : 1));
        known = kinds ? numflats : 0;
        for (int i = 0; i < known; ++i)
        {
            char name[9];
            for (int c = 0; c < 8; ++c) name[c] = (char)toupper((unsigned char)lumpinfo[firstflat + i].name[c]);
            name[8] = 0;
            kinds[i] = (signed char)step_kind(name);
        }
    }
    return pic >= 0 && pic < known ? kinds[pic] : IMPACT_STEP_STONE;
}
static int wet(int kind) { return kind == IMPACT_SPLASH_WATER || kind == IMPACT_SPLASH_THICK; }
/* Water frozen over underfoot (frozen.h) steps as stone does. */
static int on_ice(int kind, float x, float y)
{
    return kind == IMPACT_SPLASH_WATER && Frozen_Water && Frozen_Water(x, y) ? IMPACT_STEP_STONE : kind;
}
static float units(fixed_t v) { return v / (float)FRACUNIT; }
static int grounded(const mobj_t *mo) { return mo->z <= mo->floorz + FRACUNIT / 2; }

static int emit(step_event_t *events, int count, int max, int kind, const mobj_t *mo, float level)
{
    if (count >= max) return count;
    events[count] = (step_event_t){ kind, units(mo->x), units(mo->y), units(mo->z), level,
                                  mo == players[consoleplayer].mo };
    return count + 1;
}
/* A step of the player's: the footfall and the kit rattling after it
   (Step_Layers), by how hard it lands and what they carry. */
static int emit_step(step_event_t *events, int count, int max, int kind, const mobj_t *mo, float force)
{
    int library = LibrarySteps_Player(events, count, max, kind, mo, force);
    if (library >= 0) return library;
    const player_t *player = mo->player;
    impact_layer_t layers[3];
    int layer_count = Step_Layers(kind, force, player ? player->armortype : 0,
                                  player ? Game_WeaponHeft(player->readyweapon) : 0, layers);
    for (int i = 0; i < layer_count; ++i) count = emit(events, count, max, layers[i].kind, mo, layers[i].level);
    /* Gore: in a pool or among the pieces, the boot squelches too. */
    if (Gore_Underfoot && Gore_Underfoot(units(mo->x), units(mo->y)))
        count = emit(events, count, max, IMPACT_GORE_SQUELCH, mo, 0.5f + 0.25f * fminf(force, 2));
    return count;
}

/* The player: a step at every footfall of their stride (stride.h), and on
   landing. */
static int player_steps(step_event_t *events, int count, int max)
{
    static const mobj_t *last_mo;
    static unsigned steps, landings;
    const mobj_t *mo = players[consoleplayer].mo;
    if (!mo) return count;
    const stride_t *stride = Stride_Of(consoleplayer);
    /* A new body, or a game loaded: only footfalls from here on. */
    if (mo != last_mo) { last_mo = mo; steps = stride->steps; landings = stride->landings; return count; }
    int kind = LibrarySteps_Floor(on_ice(floor_kind(mo->subsector->sector->floorpic), units(mo->x), units(mo->y)));
    if (stride->landings != landings) count = emit_step(events, count, max, kind, mo, stride->landing_force);
    if (stride->steps != steps) count = emit_step(events, count, max, kind, mo, stride->step_force);
    steps = stride->steps; landings = stride->landings;
    return count;
}

/* DERP's soldiers wear boots: heard on any floor, so they can be heard coming. */
static int booted(const mobj_t *mo) { return Game_MonsterBooted(mo); }

/* A soldier's step: the footfall and its kit, all quieter than the player's. */
static int emit_boot(step_event_t *events, int count, int max, int kind, const mobj_t *mo)
{
    impact_layer_t layers[3];
    int layer_count = Step_Layers(kind, 0, 0, 1, layers);
    for (int i = 0; i < layer_count; ++i) count = emit(events, count, max, layers[i].kind, mo, layers[i].level * BOOT_LEVEL);
    return count;
}

/* An imp's step: its claws, ticking on hard floors, nearly silent on soft. */
static int clawed(const mobj_t *mo) { return Game_MonsterClawed(mo); }
static int emit_claw(step_event_t *events, int count, int max, int kind, const mobj_t *mo)
{
    return emit(events, count, max, kind == IMPACT_STEP_SOFT ? IMPACT_CLAW_SOFT : IMPACT_CLAW_HARD, mo, CLAW_LEVEL);
}

/* Monsters wading: tracked while they stand in a liquid near the listener. */
typedef struct { const mobj_t *mo; float x, y, walked; int seen; } wader_t;
static wader_t waders[WADERS];

static int monster_splashes(step_event_t *events, int count, int max)
{
    const mobj_t *listener = players[consoleplayer].mo;
    if (!listener) return count;
    float lx = units(listener->x), ly = units(listener->y);
    for (thinker_t *th = thinkercap.next; th && th != &thinkercap; th = th->next)
    {
        if (!Game_ThinkerIsMobj(th)) continue;
        const mobj_t *mo = (const mobj_t *)th;
        if (mo->player || !(mo->flags & MF_SHOOTABLE) || (mo->flags & MF_CORPSE) || mo->health <= 0 || !grounded(mo)) continue;
        float x = units(mo->x), y = units(mo->y);
        int kind = on_ice(floor_kind(mo->subsector->sector->floorpic), x, y);
        int boots = !wet(kind) && booted(mo), claws = !wet(kind) && clawed(mo);
        if ((!wet(kind) && !boots && !claws)
            || hypotf(x - lx, y - ly) > (boots ? BOOT_REACH : claws ? CLAW_REACH : WADE_REACH)) continue;
        wader_t *w = NULL, *free_slot = NULL;
        for (int i = 0; i < WADERS && !w; ++i)
        {
            if (waders[i].mo == mo) w = &waders[i];
            else if (!waders[i].mo && !free_slot) free_slot = &waders[i];
        }
        if (!w)
        {
            if (!free_slot) continue;
            *free_slot = (wader_t){ mo, x, y, 0.5f * MONSTER_STRIDE, gametic };
            continue;
        }
        float moved = hypotf(x - w->x, y - w->y);
        w->x = x; w->y = y; w->seen = gametic;
        if (moved > 64) continue;
        w->walked += moved;
        if (w->walked >= MONSTER_STRIDE)
        {
            w->walked -= MONSTER_STRIDE;
            count = boots ? emit_boot(events, count, max, kind, mo)
                : claws ? emit_claw(events, count, max, kind, mo) : emit(events, count, max, kind, mo, 0.5f);
        }
    }
    /* Forget those that left the liquid, died or were removed. */
    for (int i = 0; i < WADERS; ++i) if (waders[i].mo && waders[i].seen != gametic) waders[i].mo = NULL;
    return count;
}

int Steps_Tic(step_event_t *events, int max)
{
    static unsigned level = ~0u;
    if (gamestate != GS_LEVEL || !players[consoleplayer].mo || !players[consoleplayer].mo->subsector) return 0;
    if (level != r_levelserial) { level = r_levelserial; memset(waders, 0, sizeof(waders)); }
    int count = player_steps(events, 0, max);
    count = monster_splashes(events, count, max);
    return LibrarySteps_People(events, count, max);
}
