// Travelling bullets: see p_bullets.h. Real Doom missiles won't do: they stop at 30 units a
// tic and strike by bounding box, and everything gore knows comes from where a ray struck.
// So a bullet is that ray, its next stretch traced each tic by the game's own shot code.
#include <stdlib.h>

#include "doomdef.h"
#include "doomstat.h"
#include "m_random.h"
#include "p_local.h"
#include "r_state.h"
#include "tables.h"
#include "p_bullets.h"
#include "p_derp.h"
#include "p_rounds.h"
#include "p_senses.h"

#define MOSTBULLETS	256
// How much of the way a target is going a shooter allows for, in 256ths: reads him, not perfectly.
#define LEADLEAST	128

typedef struct {
    mobj_t*	shooter;
    int		serial;
    fixed_t	x, y, z, wasx, wasy, wasz;
    angle_t	angle;
    fixed_t	slope, left, speed, flown;
    int		damage, stepped;
    round_t	kind;
    boolean	freeaim, active;
    sector_t*	sector;
} bullet_t;

static bullet_t	bullets[MOSTBULLETS];
static int	count;
static int	struck = -1;	// the tic his last bullet was heard to strike

void (*P_BulletPassed) (int serial, fixed_t x1, fixed_t y1, fixed_t z1, fixed_t x2, fixed_t y2, fixed_t z2);
boolean P_BulletStopped;

boolean P_BulletsTravel (void)
{
    return P_DerpAI () && !demoplayback && !demorecording && !netgame;
}

// Its next stretch; false: it stopped, or ran out.
static boolean Step (bullet_t* b)
{
    fixed_t	go = b->left < b->speed ? b->left : b->speed;
    fixed_t	end[3];
    boolean	stopped, wall;

    b->wasx = b->x;
    b->wasy = b->y;
    b->wasz = b->z;
    b->stepped = leveltime;
    la_round = b->kind;
    la_spent = false;
    stopped = P_ShotStretch (b->shooter, b->x, b->y, b->z, b->angle, go, b->slope,
			     P_RoundDamage (b->kind, b->damage, b->flown), b->freeaim, &b->sector, end);
    la_round = -1;
    // Through a body: what is left of it, for the rest of its flight.
    if (la_spent)
	b->damage = P_RoundThrough (b->damage);
    b->flown += go;
    b->x = end[0];
    b->y = end[1];
    b->z = end[2];
    b->left -= go;
    wall = stopped && !la_hit;
    P_BulletStopped = stopped;
    if (P_BulletPassed)
	P_BulletPassed (b->serial, b->wasx, b->wasy, b->wasz, b->x, b->y, b->z);
    // His, into a wall: heard there, as his rocket is. A blast of pellets strikes once.
    if (wall && b->shooter && b->shooter->player && struck != leveltime)
    {
	struck = leveltime;
	P_DerpNoise (b->shooter, NOISE_IMPACT, b->x, b->y);
    }
    return !stopped && b->left > 0;
}

// Aimed where the target is going to be by the time the shot gets there.
static angle_t Lead (mobj_t* shooter, angle_t angle, fixed_t speed)
{
    mobj_t*	t = shooter->target;
    fixed_t	dist;
    int		tics, lead;
    angle_t	now, then;

    if (shooter->player || !t || (t->flags & MF_SHADOW))
	return angle;
    dist = P_AproxDistance (t->x - shooter->x, t->y - shooter->y);
    tics = dist/speed;
    if (tics < 1)
	return angle;
    lead = LEADLEAST + (P_Random () >> 1);
    now = R_PointToAngle2 (shooter->x, shooter->y, t->x, t->y);
    then = R_PointToAngle2 (shooter->x, shooter->y,
			    t->x + (fixed_t)((long long)t->momx*tics*lead/256),
			    t->y + (fixed_t)((long long)t->momy*tics*lead/256));
    return angle + (then - now);
}

static void Fire (mobj_t* shooter, fixed_t z, angle_t angle, fixed_t distance, fixed_t slope, int damage, boolean freeaim)
{
    bullet_t*	b;

    if (count >= MOSTBULLETS)
    {
	// Full: this one lands at once, as the game's own always did.
	la_freeaim = freeaim;
	P_LineAttackNow (shooter, angle, distance, slope, damage);
	return;
    }
    b = &bullets[count];
    b->shooter = shooter;
    b->serial = shooter->serial;
    b->x = b->wasx = shooter->x;
    b->y = b->wasy = shooter->y;
    b->z = b->wasz = z;
    b->angle = angle;
    b->slope = slope;
    b->kind = P_RoundOf (shooter);
    b->left = P_RoundReach ();
    b->speed = P_RoundSpeed (b->kind);
    b->flown = 0;
    b->damage = damage;
    b->freeaim = freeaim;
    b->sector = shooter->subsector->sector;
    b->active = true;
    ++count;
    if (!Step (b))
	b->active = false;
}

void P_FireBullet (mobj_t* shooter, angle_t angle, fixed_t distance, fixed_t slope, int damage, boolean freeaim)
{
    fixed_t	z = freeaim ? shooter->player->viewz : shooter->z + (shooter->height>>1) + 8*FRACUNIT;

    angle = Lead (shooter, angle, P_RoundSpeed (P_RoundOf (shooter)));
    Fire (shooter, z, angle, distance, slope, damage, freeaim);
}

void P_FireBulletAt (mobj_t* shooter, fixed_t x, fixed_t y, fixed_t z, int damage)
{
    fixed_t	from = shooter->z + (shooter->height>>1) + 8*FRACUNIT;
    fixed_t	dist = P_AproxDistance (x - shooter->x, y - shooter->y), slope;
    angle_t	angle = R_PointToAngle2 (shooter->x, shooter->y, x, y);

    if (dist < FRACUNIT)
	dist = FRACUNIT;
    slope = FixedDiv (z - from, dist);
    P_RoundScatter (P_RoundOf (shooter), &angle, &slope);
    Fire (shooter, from, angle, MISSILERANGE, slope, damage, false);
}

void P_RunBullets (void)
{
    int		i, kept = 0;

    // (One fired this tic has had its first stretch. An ended one stays until the tic after its last, to be drawn.)
    for (i = 0; i < count; ++i)
    {
	if (bullets[i].active && bullets[i].stepped != leveltime && !Step (&bullets[i]))
	    bullets[i].active = false;
    }
    for (i = 0; i < count; ++i)
    {
	if (bullets[i].active || bullets[i].stepped == leveltime)
	{
	    if (kept != i)
		bullets[kept] = bullets[i];
	    ++kept;
	}
    }
    count = kept;
}

void P_ClearBullets (void)
{
    count = 0;
}

void P_BulletsForget (mobj_t* mobj)
{
    int		i;

    for (i = 0; i < count; ++i)
    {
	if (bullets[i].shooter == mobj)
	{
	    bullets[i].active = false;
	    bullets[i].stepped = -1;
	    bullets[i].shooter = NULL;
	}
    }
}

int P_BulletCount (void)
{
    return count;
}

boolean P_BulletStretch (int index, fixed_t from[3], fixed_t to[3])
{
    const bullet_t*	b = &bullets[index];

    // Its stretch of the tic just run (leveltime has moved on since).
    if (index < 0 || index >= count || b->stepped != leveltime - 1)
	return false;
    from[0] = b->wasx;
    from[1] = b->wasy;
    from[2] = b->wasz;
    to[0] = b->x;
    to[1] = b->y;
    to[2] = b->z;
    return true;
}
