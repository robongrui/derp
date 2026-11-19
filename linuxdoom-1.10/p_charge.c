//
// A pinky's charge (p_charge.h).
//

#include <stdlib.h>

#include "doomdef.h"
#include "doomstat.h"
#include "m_random.h"
#include "p_local.h"
#include "p_derp.h"
#include "p_barks.h"
#include "p_derplog.h"
#include "p_senses.h"
#include "r_main.h"
#include "s_sound.h"
#include "sounds.h"
#include "tables.h"
#include "p_charge.h"
#include "p_tune.h"

// From how near and how far, how often it weighs it, the odds of 256, and how
// long until the next.
#define NEAR		TUNE (CHARGE_NEAR, 160)
#define FAR		TUNE (FAR, 640)
#define WEIGH_EVERY	TUNE (WEIGH_EVERY, 8)
#define ODDS		TUNE (ODDS, 96)
#define AGAIN		TUNE (AGAIN, 7*TICRATE)
// Head down, then the run: units a tic (he runs some 9), for at most this long,
// turning this much a tic towards him.
#define WINDUP		TUNE (WINDUP, 14)
#define RUN_TICS	TUNE (RUN_TICS, 50)
#define SPEED		(13*FRACUNIT)
#define TURN		(ANG45/15)
// What it does to him, and how far ahead it looks for a clear run.
#define HIT_LEAST	TUNE (HIT_LEAST, 20)
#define HIT_MORE	TUNE (HIT_MORE, 20)
#define SAMPLE		TUNE (SAMPLE, 32)
#define STEP_MOST	TUNE (STEP_MOST, 24)
// Its pain frame, H: past the attack frames.
#define FRAME_PAIN	7
// A pinky's nose: this near it knows where he is, through walls. And at a way
// it cannot open, this near him, it beats on it, this often.
#define SMELL_NEAR	TUNE (SMELL_NEAR, 512)
#define SMELL_EVERY	TUNE (SMELL_EVERY, TICRATE)
#define POUND_NEAR	TUNE (POUND_NEAR, 512)
#define POUND_EVERY	TUNE (POUND_EVERY, 24)

static boolean Charger (const mobj_t* mo)
{
    return (mo->type == MT_SERGEANT || mo->type == MT_SHADOWS) && !mo->player && mo->health > 0
	&& P_DerpAI () && derpparts.demons;
}

boolean P_Charging (const mobj_t* mo)
{
    return mo->chargetic != 0;
}

// Floor the whole way that it could run along: no step too high or drop too
// deep, nothing that hurts.
static boolean ClearRun (mobj_t* mo, mobj_t* t, fixed_t dist)
{
    fixed_t	last = mo->subsector->sector->floorheight;
    int		n = dist/(SAMPLE*FRACUNIT), i;

    for (i = 1; i <= n; i++)
    {
	fixed_t		x = mo->x + (t->x - mo->x)/n*i, y = mo->y + (t->y - mo->y)/n*i;
	sector_t*	sec = R_PointInSubsector (x, y)->sector;
	if (abs (sec->floorheight - last) > STEP_MOST*FRACUNIT || P_DerpKeepsOut (mo, x, y))
	    return false;
	last = sec->floorheight;
    }
    return true;
}

void P_ChargeBegin (mobj_t* mo)
{
    mo->chargetic = leveltime;
    mo->chargeangle = mo->angle;
    P_SetMobjState (mo, S_SARG_ATK1);
    if (mo->info->seesound)
	S_StartSound (mo, mo->info->seesound);
    P_DerpLog (mo, "lowers its head: charges");
}

boolean P_ChargeStart (mobj_t* mo, boolean visible)
{
    mobj_t*	t = mo->target;
    fixed_t	dist;

    if (!visible || !Charger (mo) || !t || !t->player || (leveltime + mo->serial) % WEIGH_EVERY
	|| leveltime < mo->chargeagain)
	return false;
    dist = P_AproxDistance (t->x - mo->x, t->y - mo->y);
    if (dist < NEAR*FRACUNIT || dist > FAR*FRACUNIT || P_Random () >= ODDS || !ClearRun (mo, t, dist))
	return false;
    P_DerpRule (mo, "hunt.charge", "In sight, at the right distance, with clear floor all the way: it "
		"lowers its head and charges");
    P_ChargeBegin (mo);
    return true;
}

void P_ChargeSmell (mobj_t* mo)
{
    mobj_t*	t = mo->target;

    if (!Charger (mo) || !t || !t->player || (leveltime + mo->serial) % SMELL_EVERY
	|| P_AproxDistance (t->x - mo->x, t->y - mo->y) > SMELL_NEAR*FRACUNIT)
	return;
    if (mo->mind != MIND_HUNT)
    {
	P_MindBecome (mo, MIND_HUNT, STEP_NONE, "any.smells", "Close enough, through walls: it smells him "
		      "and hunts him");
	P_DerpLog (mo, "smells him");
    }
    mo->knownx = t->x;
    mo->knowny = t->y;
    mo->knowntic = leveltime;
}

// Nothing it can do about a door it cannot open. It lets him know.
static void Pound (mobj_t* mo)
{
    mobj_t*	t = mo->target;

    if (mo->wayx != mo->x || mo->wayy != mo->y || !Charger (mo) || !t || !t->player
	|| mo->mind != MIND_HUNT || (leveltime + mo->serial) % POUND_EVERY
	|| P_AproxDistance (t->x - mo->x, t->y - mo->y) > POUND_NEAR*FRACUNIT)
	return;
    S_StartSound (mo, sfx_punch);
    if (P_Random () < 48 && mo->info->activesound)
	S_StartSound (mo, mo->info->activesound);
    P_DerpBarkNear (mo->x, mo->y, POUND_NEAR, BARK_PINKYPOUND);
}

static void End (mobj_t* mo)
{
    P_DerpRuleIf (mo->chargetic, mo, "hunt/charging.over", "It hit something, was stopped, or ran out of "
		  "charge");
    mo->chargetic = 0;
    mo->chargeagain = leveltime + AGAIN;
}

void P_ChargeThink (mobj_t* mo)
{
    mobj_t*	t = mo->target;
    int		since, frame, off;
    fixed_t	x, y;

    if (!mo->chargetic)
    {
	Pound (mo);
	return;
    }
    since = leveltime - mo->chargetic;
    frame = mo->state->frame & FF_FRAMEMASK;
    // Hurt enough to flinch, dead, or he is gone: that is that.
    if (mo->health <= 0 || !t || t->health <= 0 || frame >= FRAME_PAIN)
    {
	End (mo);
	return;
    }
    if (since < WINDUP)
    {
	if (mo->tics < 2)
	    mo->tics = 2;
	mo->angle = R_PointToAngle2 (mo->x, mo->y, t->x, t->y);
	mo->chargeangle = mo->angle;
	return;
    }
    if (since == WINDUP)
    {
	P_SetMobjState (mo, mo->info->seestate);
	P_DerpBarkNear (mo->x, mo->y, 384, BARK_PINKYCHARGE);
    }
    if (since > WINDUP + RUN_TICS || mo->z > mo->floorz)
    {
	End (mo);
	return;
    }
    // Him, within reach: the whole of it.
    if (P_AproxDistance (t->x - mo->x, t->y - mo->y) < mo->radius + t->radius + 12*FRACUNIT
	&& mo->z < t->z + t->height && t->z < mo->z + mo->height)
    {
	if (mo->info->attacksound)
	    S_StartSound (mo, mo->info->attacksound);
	P_DamageMobj (t, mo, mo, HIT_LEAST + P_Random ()%HIT_MORE);
	P_DerpLog (mo, "the charge lands");
	End (mo);
	return;
    }
    off = (int)(R_PointToAngle2 (mo->x, mo->y, t->x, t->y) - mo->chargeangle);
    mo->chargeangle += off > (int)TURN ? TURN : off < -(int)TURN ? (angle_t)-(int)TURN : (angle_t)off;
    mo->angle = mo->chargeangle;
    x = mo->x + FixedMul (SPEED, finecosine[mo->chargeangle>>ANGLETOFINESHIFT]);
    y = mo->y + FixedMul (SPEED, finesine[mo->chargeangle>>ANGLETOFINESHIFT]);
    // The edge of what hurts: it pulls up. Anything else in the way, it hits.
    if (P_DerpKeepsOut (mo, x, y))
    {
	End (mo);
	return;
    }
    if (!P_TryMove (mo, x, y))
    {
	P_DerpLog (mo, "the charge hits something");
	End (mo);
	P_SetMobjState (mo, mo->info->painstate);
	if (mo->info->painsound)
	    S_StartSound (mo, mo->info->painsound);
    }
}
