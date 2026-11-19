//
// Shots as information (p_fire.h).
//

#include <stdlib.h>
#include <string.h>

#include "doomdef.h"
#include "doomstat.h"
#include "m_random.h"
#include "p_local.h"
#include "p_bullets.h"
#include "p_barks.h"
#include "p_derp.h"
#include "p_derplog.h"
#include "p_fire.h"
#include "p_navbudget.h"
#include "p_senses.h"
#include "r_state.h"
#include "tables.h"
#include "p_tune.h"

// How near a round must pass to count, and how near to be heard for what it is.
#define NEAR		TUNE (FIRE_NEAR, 64)
#define HEARD_NEAR	TUNE (HEARD_NEAR, 128)
// One of his hitting something is louder than one going by: this far off.
#define IMPACT_NEAR	TUNE (IMPACT_NEAR, 320)
// Each round near a soldier adds this, the rest of a blast in the same tic
// less (a shotgun's pellets are one fright). It fades a point every two tics;
// from SUPPRESSED it gets its head down.
#define ROUND		TUNE (ROUND, 35)
#define PELLET		TUNE (PELLET, 10)
#define SUPPRESSED	TUNE (SUPPRESSED, 50)
#define MOST		TUNE (MOST, 100)
// Heard back along the shot, this far from where it passed.
#define HEARD_BACK	TUNE (HEARD_BACK, 384)

// Enemy rounds near him: each adds this, fading a point every two tics.
#define HIM_NEAR	TUNE (HIM_NEAR, 128)
#define HIM_ROUND	TUNE (HIM_ROUND, 20)
#define HIM_PELLET	TUNE (HIM_PELLET, 5)
#define FIRE_HEAVY	TUNE (FIRE_HEAVY, 100)
#define FIRE_LIGHT	TUNE (FIRE_LIGHT, 20)
#define HIM_MOST	TUNE (HIM_MOST, 300)
// A fight is on while a round has come his way this lately.
#define FIGHT_ON	TUNE (FIGHT_ON, 10*TICRATE)
// And it is going on now: rounds this lately, a few of them, not one stray.
#define FIGHT_FRESH	TUNE (FIGHT_FRESH, 2*TICRATE)
#define FIGHT_HOT	TUNE (FIGHT_HOT, 40)
// Pinned: rounds this lately, for this long, and he has not gone this far.
#define PIN_QUIET	TUNE (PIN_QUIET, 2*TICRATE)
#define PIN_TIME	TUNE (PIN_TIME, 3*TICRATE)
#define PIN_MOVED	TUNE (PIN_MOVED, 128)

// The rounds of a burst that walk in, and how far off in units the first
// lands, each nearer. A gap this long starts another burst.
#define WALK_ROUNDS	TUNE (WALK_ROUNDS, 4)
#define WALK_STEP	TUNE (WALK_STEP, 24)
#define BURST_GAP	TUNE (BURST_GAP, 12)
// Where it lays fire is a place, not a pin: rounds land this far about it.
#define LAY_SPREAD	TUNE (LAY_SPREAD, 24)

static int	firevalue, firetic;
// (firetic moves with the fading; this one with the rounds.)
static int	himtic = -1000;
static int	lastround = -1000;
static int	pinsince;
static fixed_t	pinx, piny;


// The stretch being looked at.
static struct
{
    fixed_t	x1, y1, z1, x2, y2, z2;
    int		serial;
    boolean	his;
} shot;

// The point of the stretch nearest (x, y), and how far off it is.
static fixed_t Closest (fixed_t x, fixed_t y, fixed_t* cx, fixed_t* cy, fixed_t* cz)
{
    double	dx = shot.x2 - shot.x1, dy = shot.y2 - shot.y1, l = dx*dx + dy*dy;
    double	t = l > 0 ? ((double)(x - shot.x1)*dx + (double)(y - shot.y1)*dy)/l : 0;

    if (t < 0)
	t = 0;
    else if (t > 1)
	t = 1;
    *cx = shot.x1 + (fixed_t)(t*dx);
    *cy = shot.y1 + (fixed_t)(t*dy);
    *cz = shot.z1 + (fixed_t)(t*(shot.z2 - shot.z1));
    return P_AproxDistance (x - *cx, y - *cy);
}

// Whether a round at (x, y, z) passed in view of mo: not through the wall.
static boolean Clear (fixed_t x, fixed_t y, fixed_t z, mobj_t* mo)
{
    static mobj_t	round;

    memset (&round, 0, sizeof round);
    round.x = x;
    round.y = y;
    round.z = z;
    round.height = 4*FRACUNIT;
    round.subsector = R_PointInSubsector (x, y);
    return P_CheckSight (&round, mo);
}

int P_DerpSuppression (mobj_t* mo)
{
    int	left = mo->suppress - (leveltime - mo->suppresstic)/2;
    return left > 0 ? left : 0;
}

static void Suppress (mobj_t* mo)
{
    int		was = P_DerpSuppression (mo);
    // (A round's next stretch can pass it too, a tic later.)
    int		now = was + (leveltime - mo->suppresstic <= 1 ? PELLET : ROUND);

    mo->suppress = now < MOST ? now : MOST;
    mo->suppresstic = leveltime;
    // Past the mark: it thinks about it now, not at its next turn to. (Within
    // a few tics, by its number: one burst past a squad suppresses it all at
    // once, and they would think as one from then on.)
    if (was < SUPPRESSED && mo->suppress >= SUPPRESSED)
    {
	mo->decidetic = P_DecideSoon (mo, 4);
	P_DerpLog (mo, "is suppressed: rounds going by");
	P_DerpBarkSoon (mo, BARK_TAKINGFIRE);
    }
}

// Each thing near the stretch.
static int	heard;

static boolean Passing (mobj_t* mo)
{
    fixed_t	cx, cy, cz, d, off;

    if (!(mo->flags & MF_COUNTKILL) || mo->player || mo->health <= 0 || mo->serial == shot.serial)
	return true;
    d = Closest (mo->x, mo->y, &cx, &cy, &cz);
    off = abs (cz - (mo->z + mo->height/2));
    if (d > heard*FRACUNIT || off > heard*FRACUNIT || !Clear (cx, cy, cz, mo))
	return true;
    if (shot.his && P_DerpSoldier (mo->type) && d <= NEAR*FRACUNIT && off <= NEAR*FRACUNIT)
	Suppress (mo);
    // Heard: from back along the shot, once in a while.
    if (leveltime - mo->roundtic >= TICRATE)
    {
	fixed_t	dx = shot.x1 - shot.x2, dy = shot.y1 - shot.y2, l = P_AproxDistance (dx, dy);
	mo->roundtic = leveltime;
	if (l >= FRACUNIT)
	{
	    cx += FixedMul (FixedDiv (dx, l), HEARD_BACK*FRACUNIT);
	    cy += FixedMul (FixedDiv (dy, l), HEARD_BACK*FRACUNIT);
	    P_DerpRoundHeard (mo, shot.his ? P_DerpPlayer () : NULL, shot.his, cx, cy);
	}
    }
    return true;
}

static int FireNow (void)
{
    int	left = firevalue - (leveltime - firetic)/2;
    return left > 0 ? left : 0;
}

// An enemy round near him.
static void NearHim (mobj_t* player)
{
    fixed_t	cx, cy, cz;
    int		now;

    if (Closest (player->x, player->y, &cx, &cy, &cz) > HIM_NEAR*FRACUNIT
	|| abs (cz - (player->z + player->height/2)) > HIM_NEAR*FRACUNIT
	|| !Clear (cx, cy, cz, player))
	return;
    // The rest of a shell, or the next stretch of a round, counts for less.
    now = FireNow () + (leveltime - himtic <= 1 ? HIM_PELLET : HIM_ROUND);
    himtic = leveltime;
    firevalue = now < HIM_MOST ? now : HIM_MOST;
    firetic = leveltime;
    // Rounds after a quiet, or he has moved: it starts again from here.
    if (leveltime - lastround > PIN_QUIET
	|| P_AproxDistance (player->x - pinx, player->y - piny) > PIN_MOVED*FRACUNIT)
    {
	pinsince = leveltime;
	pinx = player->x;
	piny = player->y;
    }
    lastround = leveltime;
}

static void Passed (int serial, fixed_t x1, fixed_t y1, fixed_t z1, fixed_t x2, fixed_t y2, fixed_t z2)
{
    mobj_t*	player = P_DerpPlayer ();
    int		bx, by, left, right, bottom, top;
    fixed_t	reach;

    if (!player)
	return;
    heard = P_BulletStopped && serial == player->serial ? IMPACT_NEAR : HEARD_NEAR;
    reach = (heard + MAXRADIUS/FRACUNIT)*FRACUNIT;
    shot.x1 = x1;
    shot.y1 = y1;
    shot.z1 = z1;
    shot.x2 = x2;
    shot.y2 = y2;
    shot.z2 = z2;
    shot.serial = serial;
    shot.his = serial == player->serial;
    if (!shot.his)
	NearHim (player);
    left = ((x1 < x2 ? x1 : x2) - reach - bmaporgx)>>MAPBLOCKSHIFT;
    right = ((x1 > x2 ? x1 : x2) + reach - bmaporgx)>>MAPBLOCKSHIFT;
    bottom = ((y1 < y2 ? y1 : y2) - reach - bmaporgy)>>MAPBLOCKSHIFT;
    top = ((y1 > y2 ? y1 : y2) + reach - bmaporgy)>>MAPBLOCKSHIFT;
    for (bx = left; bx <= right; bx++)
	for (by = bottom; by <= top; by++)
	    P_BlockThingsIterator (bx, by, Passing);
}

void P_FireLevel (void)
{
    P_BulletPassed = Passed;
    firevalue = 0;
    firetic = 0;
    himtic = -1000;
    lastround = -1000;
    pinsince = 0;
}

// Whether what he has in his hands fires bullets, and they are travelling.
static boolean Rounds (void)
{
    weapontype_t	gun = players[consoleplayer].readyweapon;

    return P_BulletsTravel ()
	&& (gun == wp_pistol || gun == wp_shotgun || gun == wp_chaingun || gun == wp_supershotgun);
}

boolean P_DerpUnderFire (mobj_t* mo, boolean visible)
{
    if (Rounds ())
	return P_DerpSuppression (mo) >= SUPPRESSED;
    return visible && leveltime - P_DerpLastShot () < 50;
}

boolean P_DerpRoundsNear (mobj_t* mo, int tics)
{
    if (Rounds ())
	return mo->suppresstic && leveltime - mo->suppresstic < tics;
    return leveltime - P_DerpLastShot () < tics;
}

boolean P_FireHeavy (void)
{
    return FireNow () >= FIRE_HEAVY;
}

boolean P_FireLight (void)
{
    return leveltime - lastround < FIGHT_ON && FireNow () < FIRE_LIGHT;
}

boolean P_FightOn (void)
{
    return leveltime - lastround < FIGHT_FRESH && FireNow () >= FIGHT_HOT;
}

boolean P_DerpPinned (void)
{
    mobj_t*	player = P_DerpPlayer ();

    return player && leveltime - lastround <= PIN_QUIET && leveltime - pinsince >= PIN_TIME
	&& P_AproxDistance (player->x - pinx, player->y - piny) <= PIN_MOVED*FRACUNIT;
}

boolean P_DerpLaying (mobj_t* actor)
{
    return leveltime < actor->layfire && P_BulletsTravel ();
}

boolean P_DerpLayRounds (mobj_t* actor, int rounds)
{
    fixed_t	floor, x, y, z;

    if (!P_DerpLaying (actor))
	return false;
    floor = R_PointInSubsector (actor->knownx, actor->knowny)->sector->floorheight;
    // One place a shot: a shell's pellets scatter about it as any shell's do.
    x = actor->knownx + (P_Random () - 128)*LAY_SPREAD/128*FRACUNIT;
    y = actor->knowny + (P_Random () - 128)*LAY_SPREAD/128*FRACUNIT;
    z = floor + (16 + P_Random ()%32)*FRACUNIT;
    while (rounds-- > 0)
	P_FireBulletAt (actor, x, y, z, ((P_Random ()%5) + 1)*3);
    return true;
}

boolean P_DerpWalkFire (mobj_t* actor, angle_t* off)
{
    mobj_t*	player = P_DerpPlayer ();
    fixed_t	dist;
    int		left;

    if (!player || actor->target != player || !P_BulletsTravel ())
	return false;
    if (leveltime - actor->bursttic > BURST_GAP)
	actor->burst = 0;
    actor->bursttic = leveltime;
    left = WALK_ROUNDS - actor->burst;
    if (left <= 0)
	return false;
    actor->burst++;
    dist = P_AproxDistance (player->x - actor->x, player->y - actor->y);
    if (dist < 128*FRACUNIT)
	return false;
    // To the same side each time: the rounds come in along a line.
    *off = (angle_t)(((double)left*WALK_STEP*FRACUNIT/dist)*(ANG180/3.14159265358979));
    if (actor->serial & 1)
	*off = (angle_t)-(int)*off;
    return true;
}
