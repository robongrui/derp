//
// DERP's monsters walk their steps out (p_strides.h).
//

#include "doomdef.h"
#include "doomstat.h"
#include "p_local.h"
#include "p_derp.h"
#include "p_phobos.h"
#include "p_strides.h"

// Steps one chase step may take (the director's three, a sprint's one),
// and the point it began at.
#define STRIDE_POINTS	5

static boolean Walker (mobj_t* mo)
{
    return derpparts.strides && P_DerpAI () && !mo->player && (mo->flags & MF_COUNTKILL)
	&& !(mo->flags & (MF_FLOAT|MF_NOGRAVITY)) && mo->health > 0 && !P_Phobos ();
}

static void Stop (mobj_t* mo)
{
    mo->strides = 0;
    mo->stridetics = 0;
}

extern fixed_t	tmdropoffz;

// mo at (x, y) without walking there (the real step crossed the lines
// already), if it fits there as a walker would (not hanging over a drop
// either, as far as the route lets it on stairs: P_DerpStairs' 48, or a
// walk down narrow stairs is put back where it began): true if it did. back: back to where its steps began, as far
// down a stair as they went.
static boolean Place (mobj_t* mo, fixed_t x, fixed_t y, boolean back)
{
    if (!P_CheckPosition (mo, x, y)
	|| tmceilingz - tmfloorz < mo->height
	|| (!back && tmfloorz - mo->z > 24*FRACUNIT)
	|| tmfloorz - tmdropoffz > 48*FRACUNIT)
	return false;
    P_UnsetThingPosition (mo);
    mo->x = x;
    mo->y = y;
    P_SetThingPosition (mo);
    mo->floorz = tmfloorz;
    mo->ceilingz = tmceilingz;
    mo->z = mo->floorz;
    return true;
}

// The k-th share of stridetics along the way its steps went. Where the
// straight way clips something its ends do not (a step diagonally past a
// corner or a pillar), it goes to the end at once, as vanilla's jump did:
// false then (the walk is over), or if the end is taken too (it stays).
static boolean Along (mobj_t* mo, int k)
{
    fixed_t	length = 0, want, d, ex = mo->stridex[mo->strides - 1], ey = mo->stridey[mo->strides - 1];
    int		i;

    for (i = 1; i < mo->strides; i++)
	length += P_AproxDistance (mo->stridex[i] - mo->stridex[i - 1], mo->stridey[i] - mo->stridey[i - 1]);
    if (k >= mo->stridetics)
	return Place (mo, ex, ey, false);
    want = (fixed_t)((long long)length*k/mo->stridetics);
    for (i = 1; i < mo->strides; i++)
    {
	d = P_AproxDistance (mo->stridex[i] - mo->stridex[i - 1], mo->stridey[i] - mo->stridey[i - 1]);
	if (want <= d && d > 0)
	{
	    if (Place (mo, mo->stridex[i - 1] + FixedMul (mo->stridex[i] - mo->stridex[i - 1], FixedDiv (want, d)),
		       mo->stridey[i - 1] + FixedMul (mo->stridey[i] - mo->stridey[i - 1], FixedDiv (want, d)), false))
		return true;
	    Place (mo, ex, ey, false);
	    return false;
	}
	want -= d;
    }
    return Place (mo, ex, ey, false);
}

void P_DerpStepping (mobj_t* actor)
{
    if (!Walker (actor))
    {
	Stop (actor);
	return;
    }
    // Another step of this chase step: the way goes on.
    if (actor->strides && actor->stridetic == leveltime && !actor->stridetics)
	return;
    // A walk still under way (its state cut short): to its end now.
    if (actor->stridetics)
	Along (actor, actor->stridetics);
    actor->stridex[0] = actor->x;
    actor->stridey[0] = actor->y;
    actor->strides = 1;
    actor->stridetic = leveltime;
    actor->stridetics = 0;
}

void P_DerpStepped (mobj_t* actor, fixed_t x, fixed_t y)
{
    int		last = actor->strides - 1;

    if (!Walker (actor) || last < 0 || actor->stridetic != leveltime || actor->stridetics)
	return;
    // Not where the step went: a teleport line crossed. Not walked.
    if (actor->x != x || actor->y != y)
    {
	Stop (actor);
	return;
    }
    if (actor->strides < STRIDE_POINTS)
	last = actor->strides++;
    actor->stridex[last] = actor->x;
    actor->stridey[last] = actor->y;
}

void P_DerpStridesEnd (mobj_t* mo)
{
    int		tics = mo->tics;

    if (!mo->strides || mo->stridetic != leveltime || mo->stridetics)
	return;
    // Nothing walked, or nothing to walk it out over (a state of a tic).
    if (mo->strides < 2 || !Walker (mo) || tics < 2 || mo->momx || mo->momy)
    {
	Stop (mo);
	return;
    }
    // Back to where the chase step began, and the first share of the way:
    // the last share lands at its end the tic before its next chase step.
    if (!Place (mo, mo->stridex[0], mo->stridey[0], true))
    {
	Stop (mo);
	return;
    }
    mo->stridetics = tics;
    if (!Along (mo, 1))
	Stop (mo);
}

void P_DerpStrides (mobj_t* mo)
{
    int		k;

    if (!mo->stridetics)
	return;
    // Off its feet: where it is.
    if (mo->z > mo->floorz)
    {
	Stop (mo);
	return;
    }
    // Thrown (before the blow moves it), dead, or the part switched off: at
    // the end of its steps first, where the game had it, then as before.
    if (!Walker (mo) || mo->momx || mo->momy)
    {
	Along (mo, mo->stridetics);
	Stop (mo);
	return;
    }
    k = leveltime - mo->stridetic + 1;
    if (!Along (mo, k) || k >= mo->stridetics)
	Stop (mo);
}
