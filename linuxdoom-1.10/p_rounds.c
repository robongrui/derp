//
// What each gun fires (p_rounds.h).
//

#include <math.h>
#include <stdlib.h>

#include "doomdef.h"
#include "doomstat.h"
#include "info.h"
#include "m_random.h"
#include "p_local.h"
#include "p_bullets.h"
#include "p_derp.h"
#include "p_rounds.h"
#include "p_barks.h"
#include "p_difficulty.h"
#include "tables.h"

// D90: the full angle holding nine in ten, in degrees. Not for pellets, below.
typedef struct
{
    int		speed;		// units a tic
    double	d90;
    int		fades;		// by this far it has lost lose percent,
    int		lose;		// and no more than most,
    int		most;
} roundinfo_t;

static const roundinfo_t rounds[NUMROUNDS] =
{
    { 343, 0.4, 2048, 15, 50 },		// 375 m/s
    { 366, 0.6, 2048, 15, 50 },		// 400 m/s, a longer barrel
    { 343, 0.0, 1280, 50, 50 },		// buckshot: each pellet slows on its own
};

// A shotgun keeps its job: forgiving up close, no use far off. Stock's own
// spread, 5.6 degrees either side, with half that up and down: real buckshot
// at the reference's 2 degrees played as a laser beam (the user, 2026-11-04),
// on bodies 1.25 m wide. Triangular, as stock's.
#define PELLET_SIDE	5.6
#define PELLET_UP	2.8

// Bullets don't stop at the game's 2048: that was a trace's length.
#define REACH		8192

// A Gaussian's sigma from its D90 (two axes).
#define SIGMA(d90)	((d90)/4.292)
#define BAM_DEGREE	11930464.71
#define DEGREE		0.01745329252

// The trigger held: each shot adds to the drift, which fades in about a
// second from the most. Separate from the round's grouping (D90 too).
#define PISTOL_DRIFT	1.2
#define PISTOL_MOST	1.5
#define CHAIN_DRIFT	1.7
#define CHAIN_MOST	6.0

// A monster's aim, sigma in degrees, is by skill (p_difficulty.c). Stock's
// spread (22.4 either side, triangular) has a sigma of 9.1, so 9 is today's
// soldiers. Up and down it is far better, in the same proportion: stock has none.
#define AIM_UP_SHARE	(1.0/9)

// The slayer takes a third from a pellet: a full shell at him averages 27,
// what stock's three traces did.
#define PELLET_TAKEN	3

// Rockets and plasma: not the reference's 165 and 110. A Cyberdemon's rocket
// is the player's, and he walks at 9.2; and over 30 a missile goes through
// walls (P_XYMovement).
#define ROCKET_SPEED	30
#define PLASMA_SPEED	30

// The BFG: what stock's 40 rays do between them, shared from the impact.
#define SPRAY_RAYS	40
#define SPRAY_REACH	1024
#define SPRAY_MOST	32
#define SPRAY_ONE	1200
#define SPRAY_SHOW	3


round_t P_RoundOf (mobj_t* shooter)
{
    if (shooter->player)
    {
	weapontype_t	gun = shooter->player->readyweapon;
	return gun == wp_shotgun || gun == wp_supershotgun ? ROUND_PELLET
	    : gun == wp_chaingun ? ROUND_ROTARY : ROUND_PISTOL;
    }
    switch (shooter->type)
    {
      case MT_SHOTGUY:	return ROUND_PELLET;
      case MT_CHAINGUY:
      case MT_WOLFSS:
      case MT_SPIDER:	return ROUND_ROTARY;
      default:		return ROUND_PISTOL;
    }
}

fixed_t P_RoundSpeed (round_t kind)
{
    return rounds[kind].speed*FRACUNIT;
}

fixed_t P_RoundReach (void)
{
    return REACH*FRACUNIT;
}

int P_RoundDamage (round_t kind, int damage, fixed_t flown)
{
    const roundinfo_t*	r = &rounds[kind];
    int			lost = (int)((long long)(flown>>FRACBITS)*r->lose/r->fades);

    if (lost > r->most)
	lost = r->most;
    if (!damage)
	return 0;
    damage = (damage*(100 - lost) + 50)/100;
    return damage > 0 ? damage : 1;
}

int P_RoundTaken (mobj_t* target, mobj_t* shooter, round_t kind, int damage)
{
    if (shooter && shooter->player && kind == ROUND_PISTOL
	&& P_DerpSoldier (target->type) && target->headtic == leveltime)
    {
	P_DerpBarkSawFall (target, 640, BARK_HEADSHOT);
	return target->health;
    }
    if (!target->player || kind != ROUND_PELLET)
	return damage;
    damage = (damage + PELLET_TAKEN/2)/PELLET_TAKEN;
    return damage > 0 ? damage : 1;
}

// Those killed this tic. Addresses to compare, never to follow, so one gone
// does no harm, and nothing here needs saving.
#define FRESH_MOST	16
static mobj_t*	fresh[FRESH_MOST];
static int	freshtic[FRESH_MOST];
static int	freshnext;

void P_RoundKilled (mobj_t* target)
{
    fresh[freshnext] = target;
    freshtic[freshnext] = leveltime;
    freshnext = (freshnext + 1) % FRESH_MOST;
}

boolean P_RoundFresh (mobj_t* target)
{
    int		i;

    for (i = 0; i < FRESH_MOST; i++)
	if (fresh[i] == target && freshtic[i] == leveltime)
	    return true;
    return false;
}

int P_RoundThrough (int damage)
{
    return (damage + 3)/4;
}

// Near enough a normal roll, sigma 1: four bytes of a generator of its own.
// P_Random's table is 256 long, and a shot's rolls fall on the same few spots
// of it (81% inside the D90, not 90). Demos never come here, nor do netgames.
static unsigned		rollstate = 0x9e3779b9;

static double Roll (void)
{
    unsigned	r = rollstate;

    r ^= r << 13;
    r ^= r >> 17;
    r ^= r << 5;
    rollstate = r;
    return ((int)(r & 255) + (int)(r >> 8 & 255) - (int)(r >> 16 & 255) - (int)(r >> 24))/147.8;
}

// Both axes, sigma in degrees.
static void Scatter (double side, double up, angle_t* angle, fixed_t* slope)
{
    *angle += (angle_t)(int)(Roll ()*side*BAM_DEGREE);
    *slope += (fixed_t)(Roll ()*up*DEGREE*FRACUNIT);
}

// A byte less a byte, -1 to 1: stock's (P_Random()-P_Random()).
static double Triangle (void)
{
    unsigned	r;

    Roll ();
    r = rollstate;
    return ((int)(r & 255) - (int)(r >> 8 & 255))/255.0;
}

void P_RoundScatter (round_t kind, angle_t* angle, fixed_t* slope)
{
    double	s = SIGMA (rounds[kind].d90);

    if (kind == ROUND_PELLET)
    {
	*angle += (angle_t)(int)(Triangle ()*PELLET_SIDE*BAM_DEGREE);
	*slope += (fixed_t)(Triangle ()*PELLET_UP*DEGREE*FRACUNIT);
	return;
    }
    Scatter (s, s, angle, slope);
}

static double	drift;
static int	drifttic;

void P_RoundPlayerAim (mobj_t* mo, angle_t* angle, fixed_t* slope)
{
    round_t	kind = P_RoundOf (mo);
    double	most = kind == ROUND_ROTARY ? CHAIN_MOST : PISTOL_MOST;
    double	s;

    P_RoundScatter (kind, angle, slope);
    if (kind == ROUND_PELLET)
	return;
    // Faded since the last.
    drift -= (leveltime - drifttic)*most/TICRATE;
    if (drift < 0 || leveltime < drifttic)
	drift = 0;
    drifttic = leveltime;
    s = SIGMA (drift);
    Scatter (s, s, angle, slope);
    drift += kind == ROUND_ROTARY ? CHAIN_DRIFT : PISTOL_DRIFT;
    if (drift > most)
	drift = most;
}

void P_RoundMonsterAim (angle_t* angle, fixed_t* slope, int scale)
{
    double	aim = P_Difficulty ()->aim;

    Scatter (aim*scale/256, aim*AIM_UP_SHARE*scale/256, angle, slope);
}

fixed_t P_RoundMissileSpeed (mobj_t* missile)
{
    if (P_BulletsTravel ())
    {
	if (missile->type == MT_ROCKET)
	    return ROCKET_SPEED*FRACUNIT;
	if (missile->type == MT_PLASMA || missile->type == MT_ARACHPLAZ)
	    return PLASMA_SPEED*FRACUNIT;
    }
    return missile->info->speed;
}

// Those the ball sees near it: gathered first, as a kill moves things.
static mobj_t*	spraying;
static mobj_t*	sprayed[SPRAY_MOST];
static int	numsprayed;

static boolean SprayFind (mobj_t* thing)
{
    if (!(thing->flags & MF_SHOOTABLE) || thing->player || thing == spraying->target
	|| numsprayed == SPRAY_MOST
	|| P_AproxDistance (thing->x - spraying->x, thing->y - spraying->y)
	   > (SPRAY_REACH*FRACUNIT + thing->radius)
	|| !P_CheckSight (spraying, thing))
	return true;
    sprayed[numsprayed++] = thing;
    return true;
}

void P_RoundSpray (mobj_t* mo)
{
    fixed_t	reach = SPRAY_REACH*FRACUNIT + MAXRADIUS;
    int		left = (mo->x - reach - bmaporgx)>>MAPBLOCKSHIFT;
    int		right = (mo->x + reach - bmaporgx)>>MAPBLOCKSHIFT;
    int		bottom = (mo->y - reach - bmaporgy)>>MAPBLOCKSHIFT;
    int		top = (mo->y + reach - bmaporgy)>>MAPBLOCKSHIFT;
    int		bx, by, i, j, budget = 0, share;

    spraying = mo;
    numsprayed = 0;
    for (bx = left; bx <= right; bx++)
	for (by = bottom; by <= top; by++)
	    P_BlockThingsIterator (bx, by, SprayFind);
    if (!numsprayed)
	return;
    for (i = 0; i < SPRAY_RAYS*15; i++)
	budget += (P_Random () & 7) + 1;
    share = budget/numsprayed;
    if (share > SPRAY_ONE)
	share = SPRAY_ONE;
    for (i = 0; i < numsprayed; i++)
    {
	mobj_t*	t = sprayed[i];
	if (t->health <= 0)
	    continue;
	for (j = 0; j < SPRAY_SHOW; j++)
	    P_SpawnMobj (t->x, t->y, t->z + (t->height>>2), MT_EXTRABFG);
	P_DamageMobj (t, mo->target, mo->target, share);
    }
}
