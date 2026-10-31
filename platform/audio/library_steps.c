/* The library's steps (episode 0.5). See library_steps.h. */
#include <math.h>
#include <string.h>
#include "doomstat.h"
#include "p_local.h"
#include "impact_synth.h"
#include "library_steps.h"
#include "../game_profile.h"

#define WALKERS      32
#define WALKER_REACH 900.0f   /* how far off a walker is heard */
#define MOVING_TICS  6       /* a jump at least this often: still walking */
#define WALKER_LEVEL 0.6f     /* of his own footfall's level, before distance */
#define HEAVIER      0.45f    /* his footfall, tired out, this much louder */
#define DRAG         0.9f     /* the dragged foot's scuff, tired out, of the footfall */

static float units(fixed_t v) { return v / (float)FRACUNIT; }
static int grounded(const mobj_t *mo) { return mo->z <= mo->floorz + FRACUNIT / 2; }
static int dry(int kind) { return kind == IMPACT_STEP_STONE || kind == IMPACT_STEP_METAL || kind == IMPACT_STEP_SOFT; }

static int emit(step_event_t *events, int count, int max, int kind, const mobj_t *mo, float level)
{
    if (count >= max) return count;
    events[count] = (step_event_t){ kind, units(mo->x), units(mo->y), units(mo->z), level,
                                  mo == players[consoleplayer].mo };
    return count + 1;
}

int LibrarySteps_Floor(int kind)
{
    return Game_InLibrary() && dry(kind) ? IMPACT_STEP_WOOD : kind;
}

int LibrarySteps_Player(step_event_t *events, int count, int max, int kind, const mobj_t *mo, float force)
{
    if (!Game_InLibrary()) return -1;
    const player_t *player = mo->player;
    float tired = Game_LibraryTired();
    impact_layer_t layers[3];
    /* Tired, he puts his feet down harder, as though running, and the other drags. */
    int n = Step_Layers(kind, force + 0.5f * tired, player ? player->armortype : 0,
                        player ? Game_WeaponHeft(player->readyweapon) : 0, layers);
    layers[0].level *= 1 + HEAVIER * tired;
    for (int i = 0; i < n; ++i) count = emit(events, count, max, layers[i].kind, mo, layers[i].level);
    if (tired > 0.1f) count = emit(events, count, max, IMPACT_STEP_SOFT, mo, layers[0].level * DRAG * tired);
    return count;
}

/* The people walking near him: each followed while near, a step as a foot
   comes down in its walk (frames A and C of the cycle, A A B B C C D D),
   only while it is moving: standing, its stand frames are no steps. */
typedef struct { const mobj_t *mo; float x, y; int frame, moving, seen; } walker_t;
static walker_t walkers[WALKERS];

int LibrarySteps_People(step_event_t *events, int count, int max)
{
    static unsigned level = ~0u;
    if (!Game_InLibrary()) return count;
    if (level != r_levelserial) { level = r_levelserial; memset(walkers, 0, sizeof(walkers)); }
    const mobj_t *listener = players[consoleplayer].mo;
    if (!listener) return count;
    float lx = units(listener->x), ly = units(listener->y);
    impact_layer_t layers[3];
    Step_Layers(IMPACT_STEP_WOOD, 0, 0, 0, layers);
    float footfall = layers[0].level * WALKER_LEVEL;
    for (thinker_t *th = thinkercap.next; th && th != &thinkercap; th = th->next)
    {
        if (!Game_ThinkerIsMobj(th)) continue;
        const mobj_t *mo = (const mobj_t *)th;
        if (mo->player || !(mo->flags & MF_COUNTKILL) || mo->health <= 0 || !grounded(mo) || Game_ThingAsleep(mo)) continue;
        float x = units(mo->x), y = units(mo->y);
        if (hypotf(x - lx, y - ly) > WALKER_REACH) continue;
        int frame = mo->frame & FF_FRAMEMASK;
        walker_t *w = NULL, *free_slot = NULL;
        for (int i = 0; i < WALKERS && !w; ++i)
        {
            if (walkers[i].mo == mo) w = &walkers[i];
            else if (!walkers[i].mo && !free_slot) free_slot = &walkers[i];
        }
        if (!w)
        {
            if (free_slot) *free_slot = (walker_t){ mo, x, y, frame, 0, gametic };
            continue;
        }
        float moved = hypotf(x - w->x, y - w->y);
        w->x = x; w->y = y; w->seen = gametic;
        if (moved > 64) { w->frame = frame; continue; }  /* carried a storey, or put back */
        /* Monsters move in jumps, every few tics: moving while it jumped lately. */
        w->moving = moved > 0.5f ? MOVING_TICS : w->moving > 0 ? w->moving - 1 : 0;
        if (frame != w->frame && (frame == 0 || frame == 2) && w->moving > 0)
        {
            /* Hunting him (a gang's charge) it comes down harder. */
            int hunting = mo->target && mo->target->player;
            count = emit(events, count, max, hunting ? IMPACT_RUN_WOOD : IMPACT_STEP_WOOD, mo, footfall * (hunting ? 1.4f : 1));        }
        w->frame = frame;
    }
    /* Forget those gone out of reach, asleep, dead or removed. */
    for (int i = 0; i < WALKERS; ++i) if (walkers[i].mo && walkers[i].seen != gametic) walkers[i].mo = NULL;
    return count;
}
