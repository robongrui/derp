//
// A monster with its feet swept away: Authored's sidesweep (p_kick.c).
//
// It stays the monster it was: no doll (a doll is a corpse, and does not
// get up). Its feet go the way of the kick, it comes off the floor a little,
// tips over in SWEPT_FALL tics, lies SWEPT_DOWN, and is up again in
// SWEPT_RISE with a kip-up (SweptRise), in its pain frame all the while,
// doing nothing. The drawing
// turns its picture by P_SweptShare (body_motion.cpp); his shots strike it
// as low as it is drawn (P_SweptShown). It flies off with what the kick
// gave it (push: his slide's speed, handed over) and stays as solid as it
// was: let through, he slid under the man he had felled (the user,
// 2026-11-14). Killed on the way, its doll starts as it was drawn
// (ragdoll.cpp).
//

#include "doomdef.h"
#include "doomstat.h"
#include "info.h"
#include "p_local.h"
#include "d_player.h"
#include "p_swept.h"
#include "r_main.h"
#include "tables.h"

#define SWEPT_FALL	13
#define SWEPT_DOWN	38
#define SWEPT_RISE	19
// Off the floor (36 units up, some 17 tics in the air), and its feet's way
// a little, units a tic.
#define SWEPT_UP	(8*FRACUNIT)
#define SWEPT_ALONG	(2*FRACUNIT)
// Shot out of the air by a shotgun, it is thrown on this hard, units a tic.
#define SWEPT_BLAST	(7*FRACUNIT)
// Lying, it is this tall to his shots.
#define SWEPT_LOW	(20*FRACUNIT)
// It rolls over as it flies, over this many tics (P_SweptFacing).
#define SWEPT_SPIN	18
// Getting up (SweptRise): upright in this many tics of its hop, its own
// facing back in this many, and crouched from the landing for this many.
#define SWEPT_UPRIGHT	6
#define SWEPT_REFACE	4
#define SWEPT_LANDS	4
#define SWEPT_CROUCH	0.4f

static unsigned SweptHash (unsigned x)
{
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

// Each kick's own draw (by its tic and the body's number: the same in every
// run, for the hop is play).
static unsigned SweptSeed (const mobj_t* mo)
{
    return SweptHash ((unsigned)mo->swepttic*2654435761u ^ (unsigned)mo->serial*40503u);
}

// The kip-up (the user, 2026-11-17: like an imp's jump, not the slow hinge
// back up on the spot it was): lying a tic to three more as it gathers
// itself (push, tics into SWEPT_RISE), then a hop of hop units a tic, up
// some 15, upright in its first SWEPT_UPRIGHT tics; it lands (land, tics
// into SWEPT_RISE) and is crouched a moment. Each body its own.
static void SweptRise (const mobj_t* mo, int* push, fixed_t* hop, float* land)
{
    unsigned	seed = SweptSeed (mo);

    *push = 1 + (int)((seed >> 8) % 3);
    *hop = 9*FRACUNIT/2 + (fixed_t)((seed >> 12) % (FRACUNIT + 1));
    // Set off the tic after, up by hop, down a unit a tic: back on the floor
    // some 2*hop + 2 tics after push.
    *land = *push + 2*(float)*hop/FRACUNIT + 2;
}

boolean P_Sweepable (mobj_t* mo)
{
    if (mo->health <= 0 || mo->swepttic || mo->z > mo->floorz || mo->hang || mo->pounce)
	return false;
    // Two legs, a man's size.
    switch (mo->type)
    {
      case MT_POSSESSED:
      case MT_SHOTGUY:
      case MT_CHAINGUY:
      case MT_WOLFSS:
      case MT_TROOP:
	return true;
      default:
	return false;
    }
}

boolean P_Sweep (mobj_t* mo, angle_t way, fixed_t pushx, fixed_t pushy)
{
    if (!P_Sweepable (mo))
	return false;
    mo->swepttic = leveltime > 0 ? leveltime : 1;
    mo->sweptx = finecosine[way>>ANGLETOFINESHIFT];
    mo->swepty = finesine[way>>ANGLETOFINESHIFT];
    mo->momz = SWEPT_UP;
    mo->momx += FixedMul (SWEPT_ALONG, mo->sweptx) + pushx;
    mo->momy += FixedMul (SWEPT_ALONG, mo->swepty) + pushy;
    // The step it was taking is over.
    mo->stridetics = 0;
    if (mo->info->painstate)
	P_SetMobjState (mo, mo->info->painstate);
    return true;
}

boolean P_Swept (const mobj_t* mo)
{
    return mo->swepttic && mo->health > 0;
}

void P_SweptThink (mobj_t* mo)
{
    int		push;
    fixed_t	hop;
    float	land;

    if (leveltime - mo->swepttic >= SWEPT_FALL + SWEPT_DOWN + SWEPT_RISE || leveltime < mo->swepttic)
    {
	mo->swepttic = 0;
	P_SetMobjState (mo, mo->info->seestate);
	return;
    }
    // Up with a hop, off the floor (still falling, it only rights itself);
    // straight up, its feet planted: what slide it had left is gone (with it, a
    // hop in the air carried it out of a pool over its fence, P_DerpThrown).
    SweptRise (mo, &push, &hop, &land);
    if (leveltime - mo->swepttic == SWEPT_FALL + SWEPT_DOWN + push && mo->z <= mo->floorz)
    {
	mo->momz = hop;
	mo->momx = mo->momy = 0;
    }
    // Shot, it may have been put in another frame: back in its pain frame,
    // without another cry.
    if (mo->info->painstate && mo->state != &states[mo->info->painstate])
    {
	state_t*	pain = &states[mo->info->painstate];

	mo->state = pain;
	mo->sprite = pain->sprite;
	mo->frame = pain->frame;
	mo->tics = pain->tics;
    }
}

// The reward the kick sets up: a shotgun on a body in the air needs one hit
// (the user, 2026-11-14). Any pellet of it; the rest of the volley strikes
// the corpse.
boolean P_SweptAirKill (const mobj_t* mo, const mobj_t* with, const mobj_t* by)
{
    // (Its kip-up's hop is no flight: that one is shot as ever.)
    if (!P_Swept (mo) || mo->z <= mo->floorz || !by || !by->player || with != by
	|| leveltime - mo->swepttic >= SWEPT_FALL + SWEPT_DOWN)
	return false;
    return by->player->readyweapon == wp_shotgun || by->player->readyweapon == wp_supershotgun;
}

void P_SweptBlast (mobj_t* mo, const mobj_t* by)
{
    angle_t	way = R_PointToAngle2 (by->x, by->y, mo->x, mo->y);

    mo->momx += FixedMul (SWEPT_BLAST, finecosine[way>>ANGLETOFINESHIFT]);
    mo->momy += FixedMul (SWEPT_BLAST, finesine[way>>ANGLETOFINESHIFT]);
}

float P_SweptShare (const mobj_t* mo, float fraction, float* dx, float* dy)
{
    return P_Swept (mo) ? P_SweptTilt (mo, fraction, dx, dy) : 0;
}

// Alive or just killed: how far it was over (the doll starts from there).
float P_SweptTilt (const mobj_t* mo, float fraction, float* dx, float* dy)
{
    float	t, s, land;
    int		push;
    fixed_t	hop;

    if (!mo->swepttic || leveltime < mo->swepttic || leveltime - mo->swepttic >= SWEPT_FALL + SWEPT_DOWN + SWEPT_RISE)
	return 0;
    t = (float)(leveltime - 1 - mo->swepttic) + fraction;
    if (t < 0)
	t = 0;
    SweptRise (mo, &push, &hop, &land);
    if (t < SWEPT_FALL)
	s = t/SWEPT_FALL;
    else if (t < SWEPT_FALL + SWEPT_DOWN + push)
	s = 1;
    else
	s = 1 - (t - SWEPT_FALL - SWEPT_DOWN - push)/SWEPT_UPRIGHT;
    if (s < 0)
	s = 0;
    *dx = (float)mo->sweptx/FRACUNIT;
    *dy = (float)mo->swepty/FRACUNIT;
    // Eased at both ends.
    return s*s*(3 - 2*s);
}

// Rolled over in the air (the user, 2026-11-16): the pain drawing's facing turned through its eight
// views, front, side, back, half a turn to a turn and a half, its own way for each body, fast off the
// kick and easing out onto a side view to the one who kicked it, so it lies on its back or its face
// with its feet still the way they went (on its side to him, it lay awkwardly). Turned back to its own
// facing in its kip-up's hop. Drawing only: nothing in play faces any other way.
// The whole roll in the air, degrees, the body's own way round from base to the side view it ends on
// (target), at least half a turn.
static double SweptRollTotal (const mobj_t* mo, angle_t base, angle_t* target)
{
    unsigned	seed = SweptSeed (mo);
    int		dir = (seed & 2) ? 1 : -1;
    double	total;

    // Side on to him: his feet's way, or the other.
    *target = R_PointToAngle2 (0, 0, mo->sweptx, mo->swepty) + ((seed & 1) ? ANG180 : 0);
    total = (double)(int)(*target - base)*(360.0/4294967296.0);
    if (dir > 0 && total < 0)
	total += 360;
    else if (dir < 0 && total > 0)
	total -= 360;
    if (total*dir < 180)
	total += 360*dir;
    return total;
}

angle_t P_SweptFacing (const mobj_t* mo, angle_t base)
{
    int		t, push;
    double	total, s;
    angle_t	target;
    fixed_t	hop;
    float	land;

    if (!mo->swepttic || leveltime < mo->swepttic || leveltime - mo->swepttic >= SWEPT_FALL + SWEPT_DOWN + SWEPT_RISE)
	return base;
    t = leveltime - mo->swepttic;
    total = SweptRollTotal (mo, base, &target);
    SweptRise (mo, &push, &hop, &land);
    // Getting up: back the short way round in its hop.
    if (t >= SWEPT_FALL + SWEPT_DOWN + push)
    {
	s = (double)(t - SWEPT_FALL - SWEPT_DOWN - push)/SWEPT_REFACE;
	if (s > 1)
	    s = 1;
	s = s*s*(3 - 2*s);
	return base + (angle_t)(int)((double)(int)(target - base)*(1 - s));
    }
    if (t >= SWEPT_SPIN)
	return target;
    s = (double)t/SWEPT_SPIN;
    s = 1 - (1 - s)*(1 - s);
    return base + (angle_t)(long long)(total*s/360.0*4294967296.0);
}

// How fast that roll is going now, radians a tic (the facing's own sense): what a body killed in
// it keeps (ragdoll.cpp), or it stopped dead at the kill (the user, 2026-11-16).
float P_SweptRoll (const mobj_t* mo)
{
    int		t;
    angle_t	target;

    if (!mo->swepttic || leveltime < mo->swepttic)
	return 0;
    t = leveltime - mo->swepttic;
    if (t >= SWEPT_SPIN)
	return 0;
    return (float)(SweptRollTotal (mo, mo->angle, &target)*2*(1 - (double)t/SWEPT_SPIN)/SWEPT_SPIN*(3.14159265358979/180));
}

// Landed from its kip-up: crouched, 0 to 1 (body_motion.cpp's squash), a moment.
float P_SweptCrouch (const mobj_t* mo, float fraction)
{
    float	r, land;
    int		push;
    fixed_t	hop;

    if (!P_Swept (mo) || leveltime < mo->swepttic)
	return 0;
    r = (float)(leveltime - 1 - mo->swepttic - SWEPT_FALL - SWEPT_DOWN) + fraction;
    SweptRise (mo, &push, &hop, &land);
    if (r < land || r >= land + SWEPT_LANDS)
	return 0;
    return SWEPT_CROUCH*(1 - (r - land)/SWEPT_LANDS);
}

fixed_t P_SweptShown (const mobj_t* mo)
{
    float	dx, dy, share = P_SweptShare (mo, 1, &dx, &dy);

    return mo->info->height - (fixed_t)((mo->info->height - SWEPT_LOW)*share);
}
