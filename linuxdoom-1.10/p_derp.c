//
// DERP's monsters (p_derp.h).
//

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "doomdef.h"
#include "doomstat.h"
#include "m_argv.h"
#include "m_random.h"
#include "p_local.h"
#include "m_bbox.h"
#include "p_phobos.h"
#include "p_derp.h"
#include "p_derptiming.h"
#include "p_barks.h"
#include "r_state.h"
#include "w_wad.h"
#include "p_tune.h"

boolean		derpauthored;
derpparts_t	derpparts = { true, true, true, true, true, true, true, true, true };
boolean		derpmomentum;
boolean		derproute;

// The last thing's number (P_DerpSerial).
static int	serials;
// Whether this level is id's own (P_DerpLevel), and the overrides (asked once).
static boolean	stocklevel;
static int	forced = -1;
static boolean	disabled;
// Whether the level's monsters spawned as DERP's.
static boolean	spawned;

uint64_t (*derp_timing_clock)(void);
static uint64_t timing[DERP_TIMING_COUNT];
static uint64_t timing_start;
static derp_timing_group timing_active;

void P_DerpTimingSet (uint64_t (*clock)(void))
{
    if (derp_timing_clock == clock)
	return;
    derp_timing_clock = clock;
    memset (timing, 0, sizeof timing);
}

void P_DerpTimingTake (uint64_t times[DERP_TIMING_COUNT])
{
    memcpy (times, timing, sizeof timing);
    memset (timing, 0, sizeof timing);
}

// Pause the enclosing group while the new group runs. A route looked up
// during a chase, for instance, only counts toward navigation.
derp_timing_group P_DerpTimingSwitch (derp_timing_group next)
{
    derp_timing_group previous = timing_active;

    if (previous != next)
    {
	uint64_t now = derp_timing_clock ();
	if (previous != DERP_TIMING_NONE)
	    timing[previous] += now - timing_start;
	timing_active = next;
	timing_start = now;
    }
    return previous;
}


boolean P_StockMap (int episode, int map)
{
    char	name[9];
    int		lump;

    if (episode == 0)
	return false;	// the library's E0M1 is none of the game's own
    if (gamemode == commercial)
	snprintf (name, sizeof name, "MAP%02d", map);
    else
	snprintf (name, sizeof name, "E%dM%d", episode, map);
    lump = W_CheckNumForName (name);
    return lump >= 0 && lumpinfo[lump].handle == lumpinfo[0].handle
	&& W_CheckNumForName ("FREEDOOM") < 0;
}


void P_DerpLevel (void)
{
    // Numbered afresh each level, so a monster's nerve (drawn from its
    // number) is the same each time the level is played; a load numbers on
    // from its things (P_DerpLoaded).
    serials = 0;
    stocklevel = P_StockMap (gameepisode, gamemap);
    spawned = P_DerpAI ();
}


boolean P_DerpChanged (void)
{
    return P_DerpAI () != spawned;
}


boolean P_DerpAI (void)
{
    if (demoplayback || demostarting || demorecording || netgame
	|| gameskill == sk_nightmare || respawnparm)
	return false;
    if (forced < 0)
    {
	forced = M_CheckParm ("-derpai") != 0;
	disabled = M_CheckParm ("-noderpai") != 0;
    }
    return !disabled && (forced || (derpauthored && stocklevel));
}


static boolean Numbers (void)
{
    return derpparts.numbers && P_DerpAI ();
}


// The possessed soldiers: dead bodies a demon drives. They feel no pain
// and do not heal, but they are still soldiers.
static boolean Soldier (mobjtype_t type)
{
    return type == MT_POSSESSED || type == MT_SHOTGUY || type == MT_CHAINGUY
	|| type == MT_WOLFSS;
}


mobj_t* P_DerpPlayer (void)
{
    mobj_t*	mo = players[consoleplayer].mo;
    return mo && mo->health > 0 ? mo : NULL;
}

boolean P_DerpMonster (mobj_t* mo)
{
    return mo && !mo->player && (mo->flags & MF_COUNTKILL) && mo->health > 0;
}

// Idle: in any frame of its look (two, for most), not just the first.
void A_Look (mobj_t* actor);	// p_enemy.c

boolean P_DerpAsleep (mobj_t* mo)
{
    return mo->state->action.acp1 == (actionf_p1)A_Look;
}

boolean P_DerpKneeling (mobj_t* mo)
{
    return mo->kneeltic != 0;
}

boolean P_DerpClimber (mobj_t* mo)
{
    return derpparts.senses && derpparts.demons && P_DerpAI () && P_DerpMonster (mo) && mo->type == MT_TROOP;
}

boolean P_DerpSoldier (mobjtype_t type)
{
    return Soldier (type);
}


int P_DerpHealth (mobjtype_t type)
{
    if (Numbers ())
	switch (type)
	{
	  // Twice vanilla's 20: a hit or two to show its wounds before it falls.
	  case MT_POSSESSED:	return 40;
	  case MT_SHOTGUY:	return 50;
	  case MT_CHAINGUY:	return 90;
	  default:		break;
	}
    return mobjinfo[type].spawnhealth;
}


// A blow this hard in one tic (a shotgun's seven pellets at arm's length, a
// rocket) throws even a soldier off its stride.
#define STAGGER		TUNE (STAGGER, 30)
// How long pain makes a demon quicker, and by how much (percent).
#define ANGRY_TIME	TUNE (ANGRY_TIME, 2*TICRATE)
#define ANGRY		TUNE (ANGRY, 130)
// An imp's last rush: this much longer a step, and its steps come quicker (p_demons.c).
// Together a little faster than he runs.
#define RUSH_STEP	TUNE (RUSH_STEP, 170)

boolean P_DerpPain (mobj_t* mo, int damage)
{
    // One roll either way, so the random numbers run as in vanilla.
    int		roll = P_Random ();

    // In its last rush nothing stops it but dying.
    if (mo->rushtic)
	return false;
    if (!Numbers ())
	return roll < mo->info->painchance;
    if (!Soldier (mo->type))
    {
	// Demons feel pain, and it makes them angry: imps and pinkies come
	// back quicker for a moment (P_DerpStep).
	boolean	pain = roll < mo->info->painchance;
	if (pain && (mo->type == MT_TROOP || mo->type == MT_SERGEANT || mo->type == MT_SHADOWS))
	    mo->angrytic = leveltime + ANGRY_TIME;
	return pain;
    }
    if (mo->hurttic != leveltime)
    {
	mo->hurttic = leveltime;
	mo->hurt = 0;
    }
    mo->hurt += damage;
    // What does not move the body, the body walks through.
    return mo->hurt >= STAGGER || roll < 8;
}


// The share of a step the next P_Move takes, in 256ths (a sprint's gait).
static int	stepshare = 256;

fixed_t P_DerpStep (mobj_t* mo)
{
    int		percent = 100;

    if (Numbers ())
	switch (mo->type)
	{
	  // Brisker: Doomguy runs eight times as fast as a zombie walks. A
	  // quarter more is a brisk walk (some 2.7 m/s); half again looked
	  // sped up in play (the user, 2026-11-02).
	  case MT_POSSESSED: case MT_SHOTGUY: case MT_CHAINGUY: case MT_WOLFSS:
	    percent = 125;
	    break;
	  case MT_TROOP:
	    percent = 150;
	    break;
	  case MT_SERGEANT: case MT_SHADOWS:
	    percent = 120;
	    break;
	  default:
	    break;
	}
    if (Numbers () && mo->angrytic > leveltime)
	percent = percent*ANGRY/100;
    if (Numbers () && mo->rushtic)
	percent = percent*RUSH_STEP/100;
    // Multiplied first, in 64 bits: at 100 exactly vanilla's speed*FRACUNIT,
    // whatever speed a DeHackEd patch gives.
    return (fixed_t)((long long)mo->info->speed*FRACUNIT*percent/100*stepshare/256);
}

void P_DerpStepShare (int share)
{
    stepshare = share;
}


// The player at Quake's pace: its ranger runs at 320 units a second
// (sv_maxspeed) against Doomguy's 583, both 56 units tall, so his top speed
// is cut to 55%. Hunters that take time to spot him and walk at a man's
// pace are a fair hunt only if he is not a car (the user, 2026-11-03).
// Slower, not heavier ("walks through molasses"): with the tuned movement
// he grips harder (P_DerpGrip: 0.6 a tic with a key held, was 0.75; 0.5
// let go, was 0.625) and pushes harder to the same top speed, 40% of it in
// the first tic (was 25), 90% in 5 (was 8), stopped in 7 (was 10), a
// reverse in 6 (was 11).
#define PACE		TUNE (PACE, 55)
#define GRIP		0x999a	// 0.6
#define BRAKE		0x8000	// 0.5

int P_DerpPace (int thrust)
{
    if (!Numbers ())
	return thrust;
    // The same top speed, push/(1 - friction), at the harder grip.
    if (P_UseResponsiveMovement ())
	return (int)((long long)thrust*PACE*8/(100*5));
    return thrust*PACE/100;
}

fixed_t P_DerpGrip (boolean pressing, fixed_t friction)
{
    return Numbers () ? (pressing ? GRIP : BRAKE) : friction;
}

fixed_t P_DerpBob (fixed_t bob)
{
    // As at Doomguy's run (bob goes by speed squared: 30% of it at his pace).
    return Numbers () ? (fixed_t)((long long)bob*10000/(PACE*PACE)) : bob;
}

fixed_t P_DerpRunSpeed (void)
{
    // Vanilla's top speed: the run key's push over what friction takes, some
    // 16.7 units a tic.
    fixed_t	run = (50*FRACUNIT)/3;
    return Numbers () ? run*PACE/100 : run;
}

// A living monster that walks: the bodies DERP lets fall and get hurt.
static boolean Body (mobj_t* mo)
{
    return P_DerpAI () && !mo->player && (mo->flags & MF_COUNTKILL)
	&& mo->health > 0 && !(mo->flags & (MF_FLOAT|MF_NOGRAVITY))
	&& !P_Phobos ();
}


boolean P_Shoved (mobj_t* mo)
{
    return derpmomentum && derpparts.ledges && Body (mo);
}

// The fence round a pool is for walking: id put "block monsters" round many of
// Doom 1's shallow nukage pools (half of E1M1's edges) so nothing wanders in, and
// a body kicked at one stopped dead at the edge in mid-air (the user,
// 2026-11-17). On the floor a blow's shove still keeps to it. (Leaving the floor:
// the kick's first move comes before its rise, its momz already set. Any swept
// body counted too, and one lying in a pool slid out over the fence.)
boolean P_DerpThrown (mobj_t* mo)
{
    return P_Shoved (mo) && (mo->z > mo->floorz || mo->momz > 0);
}


boolean P_DerpDoorUp (mobj_t* mo, line_t* ld)
{
    sector_t*	door = ld->backsector;

    return Body (mo) && door && door->ceilingheight - door->floorheight >= mo->height;
}


// Come down across a line that stops it: vanilla's monsters never stand there, so
// nothing lets them off it, and every step, even away, touched it. It stood there
// till it died. It may step off.
boolean P_DerpAstride (mobj_t* mo, line_t* ld)
{
    fixed_t	box[4];

    // (One that has jumped: nothing else gets there, and the grid goes by that.)
    if (!Body (mo) || !mo->leaptic)
	return false;
    box[BOXTOP] = mo->y + mo->radius;
    box[BOXBOTTOM] = mo->y - mo->radius;
    box[BOXRIGHT] = mo->x + mo->radius;
    box[BOXLEFT] = mo->x - mo->radius;
    return P_BoxOnLineSide (box, ld) == -1;
}

boolean P_DerpParting (mobj_t* mo, mobj_t* other, fixed_t x, fixed_t y)
{
    fixed_t	reach = mo->radius + other->radius;

    return derpmomentum && Body (mo) && (other->flags & MF_COUNTKILL) && !other->player && other->health > 0
	&& abs (mo->x - other->x) < reach && abs (mo->y - other->y) < reach
	&& P_AproxDistance (x - other->x, y - other->y) > P_AproxDistance (mo->x - other->x, mo->y - other->y);
}

boolean P_DerpOverHim (mobj_t* mo, mobj_t* him)
{
    // (Down again and still on him, hit in the air: it walks off him.)
    return mo->vault && him->player && (mo->z >= him->z + him->height || mo->z <= mo->floorz);
}

boolean P_DerpJumping (mobj_t* mo)
{
    return derpmomentum && Body (mo) && mo->leaptic && leveltime - mo->leaptic < 3*TICRATE
	&& (mo->z > mo->floorz || mo->momz > 0 || mo->jumping);
}

// Vanilla's 24 units keep a body off any step it would hang over: on narrow
// stairs (16-unit treads) a 40-unit body spans three and stands "over" 32,
// so it gets down only where its steps happen to land just so. The route
// says the stairs are walkable (each step 24 at most): on it, 48 will do.
boolean P_DerpStairs (mobj_t* mo, fixed_t drop)
{
    return derproute && drop <= 48*FRACUNIT && Body (mo);
}


boolean P_DerpSliding (mobj_t* mo)
{
    return derpparts.ledges && Body (mo);
}


boolean P_DerpAirborne (mobj_t* mo)
{
    // (Or in the middle of a jump, down on a sill: a step from there snapped it
    // back to the floor it left.)
    return derpparts.ledges && (mo->z > mo->floorz || mo->jumping) && Body (mo);
}


void P_DerpShove (mobj_t* mo, mobj_t* source)
{
    if (!source || source == mo || !Body (mo))
	return;
    mo->shover = source->serial;
    mo->shovetic = leveltime;
}


int P_DerpSerial (void)
{
    return ++serials;
}


void P_DerpLoaded (mobj_t* mo)
{
    if (mo->serial > serials)
	serials = mo->serial;
    // A save keeps no target (P_UnArchiveThinkers): nor, then, what it
    // meant to do about it (the loader's P_MindReset). A walk under way ends
    // where it got to.
    mo->strides = 0;
    mo->stridetics = 0;
}


// Who a fall or a floor kills it for: whoever threw it, if that was in the
// last few seconds and they are still about. Looked up by number among the
// living things: rare (a hard landing, a floor's bite), so the walk is cheap.
static mobj_t* Blame (mobj_t* mo)
{
    thinker_t*	th;

    if (!mo->shover || leveltime - mo->shovetic > 5*TICRATE)
	return NULL;
    for (th = thinkercap.next; th != &thinkercap; th = th->next)
	if (th->function.acp1 == (actionf_p1)P_MobjThinker
	    && ((mobj_t *)th)->serial == mo->shover)
	    return (mobj_t *)th;
    mo->shover = 0;
    return NULL;
}


// Landing slower than this is a step: a drop of about 72 units.
#define SAFE_LANDING	12.0f
// Damage per (units a tic over SAFE_LANDING) squared, for a 100 kg body: a
// zombieman survives a storey (128 units, about 6) and dies from two (256,
// about 45).
#define LANDING_HURT	0.4f

void P_DerpLanded (mobj_t* mo, fixed_t momz)
{
    float	speed = -(float)momz/FRACUNIT, over, heft;
    int		damage;

    if (!derpparts.falls || !Body (mo) || speed <= SAFE_LANDING)
	return;
    // A climber's own jump down (an imp off a ledge on its way): it meant
    // to, and lands on its feet.
    if (mo->leaptic && leveltime - mo->leaptic < 3*TICRATE)
	return;
    over = speed - SAFE_LANDING;
    // Heavier bodies land harder, though not in proportion: a baron's legs
    // carry its weight.
    heft = sqrtf ((float)mo->info->mass/100);
    if (heft < 0.5f)
	heft = 0.5f;
    if (heft > 4)
	heft = 4;
    damage = (int)(LANDING_HURT*over*over*heft);
    if (damage > 0)
	P_DamageMobj (mo, NULL, Blame (mo), damage);
}


static boolean Hurts (const sector_t* sector)
{
    switch (sector->special)
    {
      case 4: case 5: case 7: case 16:
	return true;
    }
    return false;
}

// Imps do not mind what they walk through: it is where they come from. It keeps them
// out of every rule about staying dry, on every map to come.
static boolean Bitten (mobj_t* mo)
{
    return Body (mo) && mo->type != MT_TROOP;
}

boolean P_DerpBurning (mobj_t* mo)
{
    sector_t*	here = mo->subsector->sector;

    return derpparts.floors && Bitten (mo) && Hurts (here) && here - sectors != mo->homesector
	&& mo->z == here->floorheight;
}

boolean P_DerpKeepsOut (mobj_t* mo, fixed_t x, fixed_t y)
{
    sector_t*	to;

    // Its route goes round if there is any way round (p_nav.c's HURT_COST): one
    // that still goes through means there is none.
    if (!derpparts.floors || derproute || !Bitten (mo))
	return false;
    to = R_PointInSubsector (x, y)->sector;
    if (!Hurts (to) || to - sectors == mo->homesector || to == mo->subsector->sector
	|| P_DerpBurning (mo))
	return false;
    mo->drytic = leveltime;
    P_DerpBark (mo, BARK_NOSLIME);
    return true;
}

void P_DerpFloor (mobj_t* mo)
{
    sector_t*	sector;
    int		damage;

    // As the player's floors bite: every 32 tics, standing on it.
    if ((leveltime & 31) || !derpparts.floors || !Bitten (mo))
	return;
    // On the floor of the sector its middle is in, as the player's check:
    // not on a ledge whose edge it leans over the pool from.
    sector = mo->subsector->sector;
    if (mo->z != sector->floorheight)
	return;
    switch (sector->special)
    {
      case 5:	damage = 10; break;	// hellslime
      case 7:	damage = 5; break;	// nukage
      case 4:	damage = 20; break;	// strobe hurt
      case 16:	damage = 20; break;	// super hellslime
      default:	return;
    }
    // id put monsters in nukage pools on purpose: there, at home, it does not
    // dissolve its own encounter.
    if (sector - sectors == mo->homesector)
	return;
    P_DamageMobj (mo, NULL, Blame (mo), damage);
}
