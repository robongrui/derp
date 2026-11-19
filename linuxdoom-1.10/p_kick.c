//
// The Authored player's kicks.
//
// The kick key: standing or crouched a kick straight ahead, that hurts a
// little, shoves, and puts what it lands on in its pain frame; out of a
// slide (p_moves.c) the same leg goes in low and wide, and takes the feet
// from under every man-sized monster in its arc (p_swept.c), shoving the
// rest, and what it lands on takes the slide's speed with it: they fly on
// ahead of him, he is left slow and low behind them with a shot to take. One leg for both (the user, 2026-11-14: the front kick is more than
// enough, the side sweep's drawing looked odd). It comes up over the gun,
// which stays in his hands and fires as ever: the kick is there to set up
// the shot.
//

#include <stdlib.h>

#include "doomdef.h"
#include "d_event.h"
#include "doomstat.h"
#include "m_random.h"
#include "p_local.h"
#include "p_derp.h"
#include "p_kick.h"
#include "p_moves.h"
#include "p_swept.h"
#include "r_main.h"
#include "s_sound.h"
#include "sounds.h"
#include "tables.h"

enum { KICK_NONE, KICK_FRONT, KICK_SWEEP };

// A kick's frames (platform/kick_frames_stock.h's order: C, D), how many
// tics each shows, and the one whose first tic lands it.
typedef struct
{
    int		count;
    int		frame[5];
    int		tics[5];
    int		lands;
} kickmove_t;

static const kickmove_t	kickmoves[] =
{
    { 0 },
    { 3, { 0, 1, 0 }, { 3, 5, 4 }, 1 },
    { 3, { 0, 1, 0 }, { 3, 5, 4 }, 1 },
};

// The front kick: a leg's reach, what it hurts by (and as much again at
// most), and how hard it shoves a man's weight, units a tic.
#define FRONT_REACH	MELEERANGE
#define FRONT_HURTS	10
#define FRONT_SHOVE	(8*FRACUNIT)
// The sweep: past its target's edge this far, this far to each side of
// where he looks, and no higher or lower than a step.
#define SWEEP_REACH	(56*FRACUNIT)
#define SWEEP_ARC	(ANG90/5*4)
#define SWEEP_STEP	(24*FRACUNIT)
#define SWEEP_HURTS	4
#define SWEEP_SHOVE	(5*FRACUNIT)

typedef struct
{
    mobj_t*	body;	// whose
    int		move;
    int		tic;	// tics into it
    boolean	held;
} kick_t;

static kick_t	kicks[MAXPLAYERS];

// It is pushed way, lighter things further, and flinches.
static void Shove (mobj_t* target, angle_t way, fixed_t push)
{
    int	mass = target->info->mass > 0 ? target->info->mass : 100;

    push = (fixed_t)((long long)push*100/mass);
    target->momx += FixedMul (push, finecosine[way>>ANGLETOFINESHIFT]);
    target->momy += FixedMul (push, finesine[way>>ANGLETOFINESHIFT]);
    if (target->health > 0 && target->info->painstate && !(target->flags & MF_SKULLFLY))
	P_SetMobjState (target, target->info->painstate);
}

static void FrontKick (mobj_t* mo)
{
    P_AimLineAttack (mo, mo->angle, FRONT_REACH);
    if (!linetarget)
	return;
    S_StartSound (mo, sfx_punch);
    P_DamageMobj (linetarget, mo, mo, FRONT_HURTS + P_Random ()%(FRONT_HURTS + 1));
    Shove (linetarget, mo->angle, FRONT_SHOVE);
}

// Out of a slide. Their feet go to his left: across his view, where the fall shows.
static boolean Sweep (mobj_t* mo, fixed_t pushx, fixed_t pushy)
{
    thinker_t*	th;
    thinker_t*	next;
    boolean	any = false;

    for (th = thinkercap.next; th != &thinkercap; th = next)
    {
	mobj_t*	target = (mobj_t *)th;
	angle_t	to, off;

	next = th->next;
	if (th->function.acp1 != (actionf_p1)P_MobjThinker || target == mo)
	    continue;
	if (!(target->flags & MF_SHOOTABLE) || target->health <= 0)
	    continue;
	if (P_AproxDistance (target->x - mo->x, target->y - mo->y) > SWEEP_REACH + target->radius + mo->radius)
	    continue;
	if (target->z > mo->z + SWEEP_STEP || target->z + target->height < mo->z)
	    continue;
	to = R_PointToAngle2 (mo->x, mo->y, target->x, target->y);
	off = to - mo->angle;
	if (off > SWEEP_ARC && off < (angle_t)-SWEEP_ARC)
	    continue;
	if (!P_CheckSight (mo, target))
	    continue;
	any = true;
	P_DamageMobj (target, mo, mo, SWEEP_HURTS + P_Random ()%(SWEEP_HURTS + 1));
	if (target->health > 0 && !P_Sweep (target, mo->angle + ANG90, pushx, pushy))
	    Shove (target, to, SWEEP_SHOVE);
	else if (target->health <= 0)
	{
	    // Killed by it: its body goes the same way.
	    target->momx += pushx;
	    target->momy += pushy;
	}
    }
    if (any)
	S_StartSound (mo, sfx_punch);
    return any;
}

void P_KickThink (player_t* player)
{
    kick_t*		k = &kicks[player - players];
    const kickmove_t*	move;
    boolean		down, press;
    int			i, at;

    if (k->body != player->mo)
    {
	kick_t	fresh = {0};
	*k = fresh;
	k->body = player->mo;
    }
    down = P_MovesOn () && !(player->cmd.buttons & BT_SPECIAL) && (player->cmd.buttons2 & BT2_KICK);
    press = down && !k->held;
    k->held = down;

    if (!k->move)
    {
	if (!press)
	    return;
	k->move = P_SlideKicks (player) ? KICK_SWEEP : KICK_FRONT;
	k->tic = 0;
    }
    else
	k->tic++;

    move = &kickmoves[k->move];
    for (i = 0, at = 0; i < move->count; at += move->tics[i], i++)
	if (i == move->lands && k->tic == at)
	{
	    if (R_KickLanded)
		R_KickLanded (player->mo, k->move == KICK_SWEEP);
	    if (k->move == KICK_SWEEP)
	    {
		fixed_t	pushx, pushy;

		P_SlideGiven (player, &pushx, &pushy);
		if (Sweep (player->mo, pushx, pushy))
		    P_MovesKicked (player);
	    }
	    else
		FrontKick (player->mo);
	}
    if (k->tic >= at - 1)
	k->move = KICK_NONE;
}

boolean P_KickShown (player_t* player, int* frame, float* dip)
{
    kick_t*		k = &kicks[player - players];
    const kickmove_t*	move = &kickmoves[k->move];
    int			i, at, all = 0;

    if (!k->move || k->body != player->mo)
	return false;
    for (i = 0; i < move->count; i++)
	all += move->tics[i];
    for (i = 0, at = 0; i < move->count - 1 && k->tic >= at + move->tics[i]; at += move->tics[i], i++)
	;
    *frame = move->frame[i];
    // The gun out of the leg's way: down in three tics, back over the last four.
    *dip = k->tic < 3 ? (k->tic + 1)/3.0f : all - k->tic < 4 ? (all - k->tic)/4.0f : 1;
    return true;
}

// A severed head kicked into a monster (the user, 2026-11-16: one hurt and its
// pain sprite, a nod to Blood). The head is the drawing's; only the blow is the
// game's, from the kicker, so the monster turns on him. Any monster's pain
// frame, whatever its pain chance; a 1-health one dies of it.
int P_HeadStrikes (fixed_t x, fixed_t y, fixed_t z, fixed_t radius, int player)
{
    thinker_t*	th;

    if (!derpauthored || demoplayback || netgame || player < 0 || player >= MAXPLAYERS
	|| !playeringame[player] || !players[player].mo)
	return 0;
    for (th = thinkercap.next; th != &thinkercap; th = th->next)
    {
	mobj_t*	target = (mobj_t *)th;

	if (th->function.acp1 != (actionf_p1)P_MobjThinker || target->player)
	    continue;
	if (!(target->flags & MF_SHOOTABLE) || target->health <= 0)
	    continue;
	if (abs (target->x - x) > target->radius + radius || abs (target->y - y) > target->radius + radius)
	    continue;
	if (z + radius < target->z || z - radius > target->z + target->height)
	    continue;
	P_DamageMobj (target, players[player].mo, players[player].mo, 1);
	if (target->health > 0 && target->info->painstate && !(target->flags & MF_SKULLFLY))
	    P_SetMobjState (target, target->info->painstate);
	return 1;
    }
    return 0;
}
