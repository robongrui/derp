/* The player's stride. See stride.h. */
#include <math.h>
#include "doomstat.h"
#include "p_local.h"
#include "stride.h"
#include "game_profile.h"  /* Game_ShiftGrew */

#define PLAYERS 8

typedef struct {
    stride_t s;
    const mobj_t *mo;
    float x, y, z, walked, shift_x, shift_y, shift_z;
    int grounded;
} tracked_t;
static tracked_t tracked[PLAYERS];

static float units(fixed_t v) { return v / (float)FRACUNIT; }

void Stride_Tic(int player, const mobj_t *mo, const float *shift)
{
    if (player < 0 || player >= PLAYERS || !mo) return;
    tracked_t *t = &tracked[player];
    stride_t *s = &t->s;
    float x = units(mo->x), y = units(mo->y), z = units(mo->z), shift_x, shift_y, shift_z;
    s->last_swing = s->swing;
    /* Carried through geometry built alike (the library): no step taken, no fall. */
    shift_x = shift ? shift[0] : 0; shift_y = shift ? shift[1] : 0; shift_z = shift ? shift[2] : 0;
    t->x += Game_ShiftGrew(shift_x, t->shift_x); t->y += Game_ShiftGrew(shift_y, t->shift_y);
    t->z += Game_ShiftGrew(shift_z, t->shift_z);
    t->shift_x = shift_x; t->shift_y = shift_y; t->shift_z = shift_z;
    int ground = mo->z <= mo->floorz + FRACUNIT / 2;
    if (mo != t->mo)
    {
        t->mo = mo; t->x = x; t->y = y; t->z = z; t->grounded = ground; t->walked = 0;
        s->phase = 0; s->swing = s->last_swing = (float)(s->steps & 1);
        return;
    }
    float moved = hypotf(x - t->x, y - t->y), fall = t->z - z;
    t->x = x; t->y = y; t->z = z;
    if (moved > 64) { t->walked = 0; t->grounded = ground; s->phase = 0; s->swing = s->last_swing = (float)(s->steps & 1); return; }  /* teleported */
    /* Landing: harder the faster the fall (the last tic's drop). */
    if (ground && !t->grounded && fall > 6) { ++s->landings; s->landing_force = 1 + fminf((fall - 6) / 14, 1); }
    t->grounded = ground;
    /* In the air the stride holds where it was. */
    if (ground)
    {
        if (moved < 0.5f) { t->walked = 0; s->phase = 0; }
        else
        {
            /* Steps a second: about two walking (8 units a tic), three running (16). */
            float stride = moved * TICRATE / (1.4f + 0.1f * moved);
            if (t->walked == 0) t->walked = 0.6f * stride;  /* the first comes soon after setting off */
            t->walked += moved;
            /* Force: 0 at a walk (8 units a tic), 1 at a run (16). */
            if (t->walked >= stride) { t->walked -= stride; ++s->steps; s->step_force = fminf(fmaxf((moved - 8) / 8, 0), 1); }
            s->phase = fminf(t->walked / stride, 1);
        }
    }
    s->swing = (float)(s->steps & 1) + s->phase;
}

const stride_t *Stride_Of(int player)
{
    return player >= 0 && player < PLAYERS ? &tracked[player].s : &tracked[0].s;
}

float Stride_Swing(int player, float fraction)
{
    const stride_t *s = Stride_Of(player);
    float moved = s->swing - s->last_swing;
    if (moved < -1) moved += 2;
    /* A jump (setting off, stopping, a teleport) is taken whole. */
    if (fabsf(moved) > 0.5f) return s->swing;
    float swing = s->last_swing + moved * fraction;
    return swing >= 2 ? swing - 2 : swing;
}
