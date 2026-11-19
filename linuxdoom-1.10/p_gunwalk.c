//
// A soldier firing on the move (p_gunwalk.h).
//

#include "doomdef.h"
#include "doomstat.h"
#include "p_local.h"
#include "p_derp.h"
#include "p_derplog.h"
#include "p_senses.h"
#include "p_mind.h"
#include "p_gunwalk.h"
#include "p_tune.h"

boolean P_Move (mobj_t* actor);

// A step every so many tics: a walk, whatever its run is.
#define STEP_EVERY	TUNE (STEP_EVERY, 4)
// How long it goes on firing blind along one heading (a chaingunner's burst
// can last): then it stands and finishes it.
#define MOST_TICS	TUNE (MOST_TICS, 2*TICRATE)
// How much wider its shots go.
#define AIM_MOVING	TUNE (AIM_MOVING, 2)
// Its aiming and firing frames: E and F on all three.
#define FRAME_AIM	4
#define FRAME_FIRE	5

static boolean Attacking (const mobj_t* mo)
{
    int	frame = mo->state->frame & FF_FRAMEMASK;

    return frame == FRAME_AIM || frame == FRAME_FIRE;
}

void P_GunWalkStart (mobj_t* actor)
{
    actor->gunwalk = 0;
    if (!P_DerpAI () || !derpparts.tactics || !P_DerpSoldier (actor->type) || actor->movedir >= 8
	|| actor->z > actor->floorz || P_DerpKneeling (actor))
	return;
    actor->gunwalk = leveltime;
    // A walk now, not a run.
    actor->gait = 0;
    P_DerpLog (actor, "fires on the move");
}

void P_GunWalkThink (mobj_t* mo)
{
    int	since;

    if (!mo->gunwalk)
	return;
    since = leveltime - mo->gunwalk;
    if (mo->health <= 0 || !Attacking (mo) || since > MOST_TICS)
    {
	mo->gunwalk = 0;
	return;
    }
    if (!since || since % STEP_EVERY)
	return;
    // There already, or stopped by a wall or a friend: it stands and shoots.
    if ((P_MindBounding (mo) && P_AproxDistance (mo->firex - mo->x, mo->firey - mo->y) < 24*FRACUNIT)
	|| mo->movedir >= 8 || !P_Move (mo))
    {
	mo->gunwalk = 0;
	return;
    }
    // Getting nowhere near its waypoint for a second is no stall (Walk).
    mo->movedtic = leveltime;
    mo->waydist = MAXINT;
}

boolean P_GunWalking (const mobj_t* mo)
{
    return mo->gunwalk && mo->health > 0 && Attacking (mo);
}

boolean P_GunWalked (mobj_t* mo)
{
    boolean	walked = mo->gunwalk != 0;

    mo->gunwalk = 0;
    return walked;
}

int P_GunWalkAim (const mobj_t* mo, int scale)
{
    return mo->gunwalk ? scale*AIM_MOVING : scale;
}
