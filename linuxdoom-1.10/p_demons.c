//
// DERP's demons (p_demons.h).
//

#include <stdlib.h>

#include "doomdef.h"
#include "doomstat.h"
#include "m_random.h"
#include "p_local.h"
#include "p_derp.h"
#include "p_fire.h"
#include "p_portals.h"
#include "p_nav.h"
#include "p_navbudget.h"
#include "p_navlong.h"
#include "p_demons.h"
#include "p_derplog.h"
#include "p_senses.h"
#include "p_barks.h"
#include "p_charge.h"
#include "p_hang.h"
#include "p_hunters.h"
#include "p_director.h"
#include "p_tactics.h"
#include "r_main.h"
#include "r_state.h"
#include "s_sound.h"
#include "sounds.h"
#include "tables.h"
#include "p_tune.h"
#include "p_decide.h"

// How often an imp thinks again while it hunts.
#define DECIDE_EVERY	TUNE (DEMONS_DECIDE_EVERY, 24)
// A swipe up close: tics from the claws raised to the blow (vanilla's
// wind-up is 16; under a quarter second still reads as a blow coming), and
// between the blows of a flurry once it has begun.
#define QUICK_SWIPE	TUNE (QUICK_SWIPE, 8)
#define SWIPE_AGAIN	TUNE (SWIPE_AGAIN, 4)
// The pounce: from how near and how far, how long it crouches, how fast it
// leaps up (units a tic: in the air some 14 tics), how long before another.
#define POUNCE_NEAR	TUNE (POUNCE_NEAR, 112)
#define POUNCE_FAR	TUNE (POUNCE_FAR, 384)
#define CROUCH		TUNE (CROUCH, 10)
#define LEAP_UP		(7*FRACUNIT)
#define LEAP_FAST	(24*FRACUNIT)
#define POUNCE_AGAIN	TUNE (POUNCE_AGAIN, 4*TICRATE)
// Over his head: from how near and how far, landing how far behind him, the chance in
// 256 each time it thinks of it, with room overhead for his 56 units and the rise of
// a leap this fast up (some 78 units, in the air 24 tics).
#define VAULT_NEAR	TUNE (VAULT_NEAR, 128)
#define VAULT_FAR	TUNE (VAULT_FAR, 320)
#define VAULT_BEHIND	TUNE (VAULT_BEHIND, 128)
#define VAULT_ODDS	TUNE (VAULT_ODDS, 40)
#define VAULT_UP	(12*FRACUNIT)
#define VAULT_TICS	24
#define VAULT_HEAD	140
// Its last rush: when it has nowhere left to go. From this near him, this long at most,
// a chase step this many tics.
#define RUSH_NEAR	TUNE (RUSH_NEAR, 768)
#define RUSH_TIME	TUNE (RUSH_TIME, 6*TICRATE)
#define RUSH_TICS	TUNE (RUSH_TICS, 2)
// How near a dead soldier it clicks over it.
#define OVER_BODY	TUNE (OVER_BODY, 192)
// How hard a blow shoves the player (units a tic, before his friction).
#define SHOVE		(6*FRACUNIT)
// How long a hit and run takes it away at most, and a flight to heal.
#define FLANK_TIME	TUNE (FLANK_TIME, 6*TICRATE)
#define HEAL_TIME	TUNE (HEAL_TIME, 45*TICRATE)
// How long a flight is given before a player close by counts as cornering it.
#define ESCAPE_TIME	TUNE (ESCAPE_TIME, 3*TICRATE)
// Wounded and unseen this long, a demon starts to heal; and all it may heal
// in a level, a share of its full health (percent): one long flight to mend,
// not a monster the player undoes over and over.
#define HEAL_AFTER	TUNE (HEAL_AFTER, 5*TICRATE)
#define HEAL_ALLOWANCE	TUNE (HEAL_ALLOWANCE, 50)
// A shot its way (his gun within some 22 degrees of it) in the last this
// many tics: it saw the flash coming at it, not where he aims.
#define AIMED		(ANG45/2)
#define SHOT_SEEN	TUNE (SHOT_SEEN, 12)

#define NODIR		8

void A_FaceTarget (mobj_t* actor);
boolean P_CheckMeleeRange (mobj_t* actor);
boolean P_Move (mobj_t* actor);

static boolean Demons (void)
{
    return derpparts.senses && derpparts.demons && P_DerpAI ();
}


static boolean Imp (mobj_t* mo)
{
    return Demons () && P_DerpMonster (mo) && mo->type == MT_TROOP;
}

//
// A short teleport (a blink): the game's own fog and sound where it was
// and where it comes out, so the player sees where it went; a place it can
// stand within BLINK_FAR, once in BLINK_AGAIN. Only a badly hurt imp's way
// to its hiding place (one it could walk or jump to): as a way past "no way
// there" it took imps out of the cages maps keep them in (E1M3's, behind
// impassable windows) long before the map lets them out.
//
#define BLINK_FAR	TUNE (BLINK_FAR, 512)
#define BLINK_RARE	TUNE (BLINK_RARE, 60*TICRATE)	// by any imp on the level
#define BLINK_CLOSE	TUNE (BLINK_CLOSE, 256)
#define BLINK_ODDS	TUNE (BLINK_ODDS, 128)
static int	lastblink;

static boolean Blink (mobj_t* mo, fixed_t x, fixed_t y, const char* why)
{
    fixed_t	oldx = mo->x, oldy = mo->y, oldz = mo->z;
    mobj_t*	fog;
    angle_t	an;

    if (!P_CheckPosition (mo, x, y) || tmceilingz - tmfloorz < mo->height
	|| P_NavHurts (P_NavCellAt (P_NavClassOf (mo), x, y)))
	return false;
    if (!P_TeleportMove (mo, x, y))
	return false;
    mo->z = mo->floorz;
    mo->momx = mo->momy = mo->momz = 0;
    fog = P_SpawnMobj (oldx, oldy, oldz, MT_TFOG);
    S_StartSound (fog, sfx_telept);
    an = mo->angle >> ANGLETOFINESHIFT;
    fog = P_SpawnMobj (x + 20*finecosine[an], y + 20*finesine[an], mo->z, MT_TFOG);
    S_StartSound (fog, sfx_telept);
    mo->blinktic = leveltime;
    mo->leapready = false;
    mo->waytic = -1000;
    P_DerpLog (mo, "blinks %d units: %s", P_AproxDistance (x - oldx, y - oldy)>>FRACBITS, why);
    return true;
}

boolean P_DerpDemon (mobjtype_t type)
{
    switch (type)
    {
      // The possessed are dead bodies a demon drives: nothing to heal. The
      // bosses are the end of an episode: no war of attrition there.
      case MT_POSSESSED: case MT_SHOTGUY: case MT_CHAINGUY: case MT_WOLFSS:
      case MT_CYBORG: case MT_SPIDER: case MT_KEEN: case MT_BOSSBRAIN:
	return false;
      default:
	return (mobjinfo[type].flags & MF_COUNTKILL) != 0;
    }
}


//
// Its body: leaps, blows.
//

// Off its feet: at speed along an, and up.
static void Leap (mobj_t* actor, angle_t an, fixed_t speed, fixed_t up)
{
    an >>= ANGLETOFINESHIFT;
    actor->momx = FixedMul (speed, finecosine[an]);
    actor->momy = FixedMul (speed, finesine[an]);
    actor->momz = up;
}

// A blow that moves him: a little, but enough off a ledge or into a pool.
static void Shove (mobj_t* actor, mobj_t* t)
{
    angle_t	an;

    if (!t->player)
	return;
    an = R_PointToAngle2 (actor->x, actor->y, t->x, t->y) >> ANGLETOFINESHIFT;
    t->momx += FixedMul (SHOVE, finecosine[an]);
    t->momy += FixedMul (SHOVE, finesine[an]);
}

// Whether its claws reach t from where it is, in the air or not: as far as
// vanilla's (P_CheckMeleeRange), with the heights overlapping.
static boolean Reach (mobj_t* actor, mobj_t* t)
{
    // As vanilla's P_CheckMeleeRange: near, and in sight (no claws through
    // a thin wall or a shutting door); and at its height, in the air.
    return P_AproxDistance (t->x - actor->x, t->y - actor->y) < MELEERANGE - 20*FRACUNIT + t->info->radius
	&& actor->z < t->z + t->height && t->z < actor->z + actor->height && P_CheckSight (actor, t);
}

static void Struck (mobj_t* actor)
{
    actor->striketic = leveltime;
    // It thinks again at once: away, or at him again.
    actor->decidetic = leveltime;
}

// Whether its claws landed in the last second (never, for one not yet
// struck: 0 is no blow, not one at the level's first tic).
static boolean Fresh (mobj_t* actor)
{
    return actor->striketic && leveltime - actor->striketic < TICRATE;
}

// A pounce's blow, as A_TroopAttack's claw.
static void Strike (mobj_t* actor, mobj_t* t)
{
    S_StartSound (actor, sfx_claw);
    P_DamageMobj (t, actor, actor, (P_Random ()%8 + 1)*3);
    Shove (actor, t);
    Struck (actor);
}


// How many tics a leap up at LEAP_UP stays in the air under a ceiling
// headroom over its head (P_ZMovement's rules: a ceiling stops it rising,
// and it falls from rest at twice gravity).
static int Flight (fixed_t headroom)
{
    fixed_t	z = 0, momz = LEAP_UP;
    int		tics = 0;

    while (tics < 64)
    {
	z += momz;
	tics++;
	if (z > headroom)
	{
	    z = headroom;
	    momz = 0;
	}
	if (z <= 0)
	    break;
	momz = momz ? momz - GRAVITY : -2*GRAVITY;
    }
    return tics;
}

//
// What it means to do.
//

// It sees him: it knows where he is.
static void Seen (mobj_t* actor, mobj_t* player)
{
    actor->knownx = player->x;
    actor->knowny = player->y;
    actor->knowntic = leveltime;
}

// After its blows, now and then: a leap back and away, to a spot near that
// he cannot see, and from there at him again by whatever way it takes.
static boolean HitAndRun (mobj_t* actor, mobj_t* player)
{
    angle_t	an;

    if (actor->mind == MIND_FLANK || actor->mind == MIND_HEAL || actor->z > actor->floorz
	|| P_Random () >= 96
	|| !P_DerpHideSpot (actor, player, true, &actor->coverx, &actor->covery))
	return false;
    P_MindBecome (actor, MIND_FLANK, STEP_NONE, "hunt.hit_and_run",
    		"Its blows just landed: a leap back to a hidden spot, to come "
    		"again from elsewhere");
    // Towards where it means to hide, not blindly back (over a ledge, say).
    an = R_PointToAngle2 (actor->x, actor->y, actor->coverx, actor->covery);
    Leap (actor, an, 10*FRACUNIT, 5*FRACUNIT);
    return true;
}

// A jump early on its way to (x, y) (a window, a drop): out of his sight
// the cool way, sooner than walking round.
static boolean JumpAway (mobj_t* actor, fixed_t x, fixed_t y)
{
    static int	path[16];
    int		nav = P_NavClassOf (actor), n, j;

    if (nav < 0)
	return false;
    P_DerpRouteBegin (actor);
    n = P_NavPath (nav, actor->x, actor->y, x, y, path, 16, 3000);
    P_DerpRouteEnd ();
    for (j = 1; j < n && j < 16; j++)
	if (P_NavJump (nav, path[j - 1], path[j]))
	    return true;
    return false;
}

// Whether its way to him on foot is far longer than the line to him.
#define WALLED_MOST	128
static boolean WalledOff (mobj_t* actor, mobj_t* player)
{
    static int	path[WALLED_MOST + 1];
    int		nav = P_NavClassOf (actor), cells, straight;

    straight = (P_AproxDistance (player->x - actor->x, player->y - actor->y)>>FRACBITS)/NAVCELL;
    if (nav < 0 || straight < 8)
	return false;
    // (A decision's rule: until its search is done, carried over tics, not
    // walled off; the answer at its next decision.)
    P_DerpRouteBegin (actor);
    cells = P_NavLongPath (actor, NAVLONG_WALLED, nav, actor->x, actor->y, player->x, player->y, path,
			   WALLED_MOST + 1, 20000);
    P_DerpRouteEnd ();
    return cells != NAV_DEFERRED && cells > 2*straight + 16;
}

// A flank round a choke (STEP_ROUND_CHOKE, mind MIND_FLANK): how far off that opening its way keeps.
#define ROUND_CHOKE_R	TUNE (ROUND_CHOKE_R, 192)

// Nowhere left to run: it comes at him flat out, nothing but claws, once.
static void LastRush (mobj_t* actor)
{
    P_MindStep (actor, STEP_NONE, "hunt.last_rush",
    		"Badly hurt with nowhere left to go: one flat-out rush at him, "
    		"nothing stops it but dying");
    P_DerpLog (actor, "last rush");
    actor->rushtic = leveltime;
    actor->rushed = true;
    actor->lurk = 0;
    actor->movecount = 0;
    P_DerpCry (actor, BARK_IMPCORNERED);
    P_DerpCry (actor, BARK_IMPAIR);
}

//
// Deciding (p_decide.h): the imps' rules, in the order they are asked
// (DECIDE_EVERY). The table at the end is the only place the order lives.
//

// Badly hurt, by wounds since it last ran: away where he cannot see it,
// to heal. Away from him if it can, else just out of his sight.
static boolean Hurt (mobj_t* actor, decide_t* c)
{
    mobj_t*	player = c->player;
    int		full = P_DerpHealth (actor->type);

    if (!(actor->health*100 < full*40 && actor->healed*100 < full*HEAL_ALLOWANCE && !actor->rushed
	  && (!actor->retreathealth || actor->health < actor->retreathealth)
	  && (P_DerpHideFrom (actor, c->visible, true, &actor->coverx, &actor->covery)
	      || P_DerpHideFrom (actor, c->visible, false, &actor->coverx, &actor->covery))))
	return false;
    P_MindBecome (actor, MIND_HEAL, STEP_NONE, "any.hurt",
		  "Under 40% health with healing left: away where he cannot see it, "
		  "to heal");
    actor->retreathealth = actor->health;
    // Out through a window or off a ledge if that is on its way; else gone
    // in a puff of fog if it can (a short teleport to its hiding place);
    // else off at once, with a leap towards it if he is at its claws.
    if (JumpAway (actor, actor->coverx, actor->covery))
    {
	P_DerpLog (actor, "flees the cool way: a jump on its way");
	return true;
    }
    // (Now and then, with him on top of it: 4 of 5 wounded imps blinked in a
    // run, and it stopped being a surprise. The rest run for it.)
    if (lastblink > leveltime)
	lastblink = 0;
    if ((!lastblink || leveltime - lastblink >= BLINK_RARE)
	&& P_AproxDistance (player->x - actor->x, player->y - actor->y) < BLINK_CLOSE*FRACUNIT
	&& P_AproxDistance (actor->coverx - actor->x, actor->covery - actor->y) <= BLINK_FAR*FRACUNIT
	&& P_Random () < BLINK_ODDS
	&& Blink (actor, actor->coverx, actor->covery, "away to heal"))
    {
	lastblink = leveltime;
	return true;
    }
    if (actor->z <= actor->floorz
	&& P_AproxDistance (player->x - actor->x, player->y - actor->y) < 128*FRACUNIT)
	Leap (actor, R_PointToAngle2 (actor->x, actor->y, actor->coverx, actor->covery),
	      10*FRACUNIT, 5*FRACUNIT);
    return true;
}

// Badly hurt and it did not run: no healing left, or no place to go. Him in
// sight and near: all it has left is him.
static boolean Cornered (mobj_t* actor, decide_t* c)
{
    int	full = P_DerpHealth (actor->type);

    if (!(actor->health*100 < full*40 && !actor->rushed && c->visible
	  && P_AproxDistance (c->player->x - actor->x, c->player->y - actor->y) < RUSH_NEAR*FRACUNIT
	  && (actor->healed*100 >= full*HEAL_ALLOWANCE || !actor->retreathealth || actor->health < actor->retreathealth)))
	return false;
    LastRush (actor);
    return true;
}

// He is holding a choke: not through it one by one. Round by another way
// to where it believes he is, through a window or off a ledge if that is
// what it takes; he is watching the door.
static boolean RoundChoke (mobj_t* actor, decide_t* c)
{
    fixed_t	ox, oy, nx, ny;

    if (!(!c->visible && actor->mind == MIND_HUNT && actor->knowntic && leveltime - actor->knowntic < 10*TICRATE
	  && !(c->duty & DUTY_GOIN)))
	return false;
    if (!P_PortalChoke (actor, actor->knownx, actor->knowny, &ox, &oy, &nx, &ny))
	return false;
    P_MindBecome (actor, MIND_FLANK, STEP_ROUND_CHOKE, "hunt.round_choke",
		  "He holds a doorway and it cannot see him: round by "
		  "another way, through a window or off a ledge if it takes that");
    actor->avoidx = nx;
    actor->avoidy = ny;
    actor->coverx = actor->knownx;
    actor->covery = actor->knowny;
    P_DerpLog (actor, "goes round: he is holding a doorway");
    return true;
}

// It sees him, but through something: a railed window, a gap over a pit.
// Its way to him is a long way round, and circling him on this side of
// the wall (MAP01: a yard of them, him up behind the window) gets nowhere.
// It goes in.
static boolean ThroughAWall (mobj_t* actor, decide_t* c)
{
    if (!(c->visible && actor->mind == MIND_HUNT && WalledOff (actor, c->player)))
	return false;
    P_MindBecome (actor, MIND_FLANK, STEP_NONE, "hunt.walled_off",
		  "It sees him through something, and its way to him is far "
		  "longer than the line: it goes the long way in");
    actor->coverx = c->player->x;
    actor->covery = c->player->y;
    P_DerpLog (actor, "sees him through a wall: goes the long way in");
    return true;
}

// Its blows just landed: maybe away, to come again from elsewhere.
static boolean Landed (mobj_t* actor, decide_t* c)
{
    return c->visible && Fresh (actor) && HitAndRun (actor, c->player);
}

// The order.
static const decision_t decisions[] =
{
    { "any.hurt",		Hurt },
    { "hunt.last_rush",		Cornered },
    { "hunt.round_choke",	RoundChoke },
    { "hunt.walled_off",	ThroughAWall },
    { "hunt.hit_and_run",	Landed },
    { NULL, NULL }
};

static void Decide (mobj_t* actor, boolean visible)
{
    decide_t	c;

    if (!P_DecideBegin (actor, visible, &c))
	return;
    P_Decide (actor, decisions, &c);
}

// Badly hurt: to its hiding place, and there it heals (P_DemonThink).
static boolean Heal (mobj_t* actor, mobj_t* player, boolean visible)
{
    int		full = P_DerpHealth (actor->type);

    if (actor->health >= full*3/4 || actor->healed*100 >= full*HEAL_ALLOWANCE)
    {
	// Mended (or as mended as it may get): after him again; its next
	// wounds may send it off again while it has healing left.
	actor->retreathealth = 0;
	P_MindBecome (actor, MIND_HUNT, STEP_NONE, "heal.mended",
			"Mended to three quarters, or as far as it may: after him again");
	return false;
    }
    if (leveltime - actor->tactictic > HEAL_TIME)
    {
	// As mended as it gets: back, and not off again for these wounds.
	actor->retreathealth = actor->health;
	P_MindBecome (actor, MIND_HUNT, STEP_NONE, "heal.time_up",
			"Healed as long as it can: back, and not off again for these "
			"wounds");
	return false;
    }
    if (visible)
    {
	Seen (actor, player);
	// Cornered: still on him after it has had time to get away, it fights.
	if (leveltime - actor->tactictic > ESCAPE_TIME
	    && P_AproxDistance (player->x - actor->x, player->y - actor->y) < 192*FRACUNIT)
	{
	    P_MindBecome (actor, MIND_HUNT, STEP_NONE, "heal.cornered",
	    		"He is still on it after it had time to get away: "
	    		"cornered, it fights");
	    LastRush (actor);
	    return false;
	}
	// Seen where it meant to hide: somewhere else, farther.
	if (P_DerpTucked (actor))
	    P_DerpHideSpot (actor, player, true, &actor->coverx, &actor->covery);
    }
    if (!P_DerpTucked (actor))
	P_DerpWalkTo (actor, actor->coverx, actor->covery);
    else
    {
	actor->movedir = NODIR;
	P_DerpFaceDanger (actor);
    }
    return true;
}

// Away after a blow: there, or long enough, and back at him.
static boolean Flank (mobj_t* actor, mobj_t* player, boolean visible)
{
    if (visible)
	Seen (actor, player);
    if (P_AproxDistance (actor->x - actor->coverx, actor->y - actor->covery) < 32*FRACUNIT
	|| leveltime - actor->tactictic > FLANK_TIME
	|| (visible && leveltime - actor->tactictic > ESCAPE_TIME
	    && P_AproxDistance (player->x - actor->x, player->y - actor->y) < 128*FRACUNIT))
    {
	P_MindBecome (actor, MIND_HUNT, STEP_NONE, "flank.done",
			"There, or long enough, or he caught it: back at him");
	return false;
    }
    // Round a choke: the way keeps off that opening, if there is another.
    if (actor->tactic == STEP_ROUND_CHOKE)
    {
	navavoidx = actor->avoidx;
	navavoidy = actor->avoidy;
	navavoidr = ROUND_CHOKE_R;
    }
    P_DerpWalkTo (actor, actor->coverx, actor->covery);
    navavoidr = 0;
    return true;
}

// From a few metres, now and then: the pounce's wind-up (P_DemonThink does
// the rest).
static boolean Pounce (mobj_t* actor, mobj_t* player)
{
    fixed_t	dist = P_AproxDistance (player->x - actor->x, player->y - actor->y);

    fixed_t	ceiling = actor->ceilingz < player->ceilingz ? actor->ceilingz : player->ceilingz;

    // Only as far as a leap under this ceiling carries (a low room's is short).
    if (dist < POUNCE_NEAR*FRACUNIT || dist > POUNCE_FAR*FRACUNIT
	|| actor->z > actor->floorz || player->z > actor->z + 24*FRACUNIT
	|| leveltime - actor->pouncetic < POUNCE_AGAIN
	|| dist > (Flight (ceiling - actor->height - actor->z) - 2)*LEAP_FAST
	|| P_Random () >= 40)
	return false;
    // A short rasp and its arms up (its attack's first frame): the player
    // has a moment to see it coming.
    P_DerpRule (actor, "hunt.pounce", "A few metres off, on the floor, room to leap, and the dice say so: "
		"it crouches to pounce");
    actor->pounce = 1;
    actor->pouncetic = leveltime;
    if (!P_DerpCry (actor, BARK_IMPPOUNCE))
	S_StartSound (actor, sfx_bgact);
    P_SetMobjState (actor, S_TROO_ATK1);
    return true;
}

// Where a jump over him comes down: behind him as it sees it, somewhere it can land.
static boolean VaultSpot (mobj_t* actor, mobj_t* player, fixed_t* x, fixed_t* y)
{
    angle_t	an = R_PointToAngle2 (actor->x, actor->y, player->x, player->y)>>ANGLETOFINESHIFT;
    fixed_t	tx = player->x + VAULT_BEHIND*finecosine[an], ty = player->y + VAULT_BEHIND*finesine[an], z;
    sector_t*	there;

    if (!P_NavHop (P_NavClassOf (actor), actor->x, actor->y, tx, ty, x, y, &z) || P_DerpCrowdedAt (actor, *x, *y))
	return false;
    there = R_PointInSubsector (*x, *y)->sector;
    return there->ceilingheight - z >= VAULT_HEAD*FRACUNIT;
}

// He looks its way, a few metres off, with height for it: over his head and down
// behind him, sooner than round him across his sights. (Of 32 runs round him, 14 ended
// shot on the way.)
static boolean Vault (mobj_t* actor, mobj_t* player)
{
    fixed_t	dist = P_AproxDistance (player->x - actor->x, player->y - actor->y);
    fixed_t	ceiling = actor->ceilingz < player->ceilingz ? actor->ceilingz : player->ceilingz;
    angle_t	off = R_PointToAngle2 (player->x, player->y, actor->x, actor->y) - player->angle;

    if (dist < VAULT_NEAR*FRACUNIT || dist > VAULT_FAR*FRACUNIT || actor->z > actor->floorz
	|| abs (player->z - actor->z) > 24*FRACUNIT || (off > ANG45 && off < (angle_t)-ANG45)
	|| leveltime - actor->pouncetic < POUNCE_AGAIN
	|| ceiling - actor->z < VAULT_HEAD*FRACUNIT
	|| P_Random () >= VAULT_ODDS || !VaultSpot (actor, player, &actor->landx, &actor->landy))
	return false;
    P_DerpLog (actor, "crouches to jump over him");
    actor->pounce = 1;
    actor->vault = true;
    actor->pouncetic = leveltime;
    P_MindStep (actor, STEP_NONE, "hunt.vaults",
    		"He looks its way, a few metres off, with room above and behind him: "
    		"it crouches to jump over him");
    if (!P_DerpCry (actor, BARK_IMPCROUCH))
	S_StartSound (actor, sfx_bgact);
    P_SetMobjState (actor, S_TROO_ATK1);
    return true;
}

// A shot fired its way: a step aside, now one way, now the other. Not every
// time: it still has to close in.
static boolean Sidestep (mobj_t* actor, mobj_t* player)
{
    angle_t	off = R_PointToAngle2 (player->x, player->y, actor->x, actor->y) - player->angle;
    angle_t	an;
    int		i;

    if ((off > AIMED && off < (angle_t)-AIMED)
	|| leveltime - P_DerpLastShot () > SHOT_SEEN
	|| P_AproxDistance (player->x - actor->x, player->y - actor->y) < 128*FRACUNIT
	|| P_Random () >= 128)
	return false;
    an = R_PointToAngle2 (actor->x, actor->y, player->x, player->y)
	+ (((actor->serial + leveltime/TICRATE) & 1) ? ANG90 : ANG270);
    for (i = 0; i < 2; i++, an += ANG180)
    {
	actor->movedir = ((an + ANG45/2)>>29) & 7;
	if (P_Move (actor))
	{
	    // Still counting down to its next throw, as a chase step does.
	    if (actor->movecount > 0)
		actor->movecount--;
	    return true;
	}
    }
    return false;
}

//
// The pack: imps after him near each other hunt together. They share the
// freshest word of where he is, and each closes in from its own side of
// where they think he is, so they come at him round about, not in a file.
//

// How near imps must be to hunt as one, how many at most, how far out the
// circle round him is drawn, and how near him an imp goes straight in.
#define PACK_NEAR	TUNE (PACK_NEAR, 1024)
#define PACK_MOST	8
#define PACK_CIRCLE	TUNE (PACK_CIRCLE, 256)
#define PACK_IN		TUNE (PACK_IN, 224)
// Nearer than this it fights rather than goes round, and a friend who saw him this
// long ago is still fighting him.
#define CLOSE_IN	TUNE (CLOSE_IN, 192)
#define ENGAGED		TUNE (ENGAGED, TICRATE)
// An imp of the pack on its way round him (STEP_CIRCLING and the rest, its time
// in tactictic).
// How long a way round may take (from plantic): past it the imp goes
// straight in for as long again, no orbit without end.
#define PLAN_TIME	TUNE (PLAN_TIME, 25*TICRATE)
// Where an imp going round is behind him: more than this off his facing.
#define BEHIND		(ANG90 + ANG45*2/3)

// actor's pack, itself among them, in order of their numbers (so each has
// the same place in it whichever of them asks): how many.
static int Pack (mobj_t* actor, mobj_t** pack)
{
    static mobj_t*	all[PACK_MOST*8];
    thinker_t*		th;
    int			n = 0, i, j, me = 0, start;

    // Every imp hunting with it (itself always), in order of their numbers;
    // more than PACK_MOST, they hunt as packs of PACK_MOST in that order,
    // itself in its own (one left out thought itself the first, the front).
    all[n++] = actor;
    for (th = thinkercap.next; th != &thinkercap && n < PACK_MOST*8; th = th->next)
    {
	mobj_t*	mo = (mobj_t *)th;
	if (th->function.acp1 != (actionf_p1)P_MobjThinker || mo == actor || !Imp (mo)
	    || mo->target != actor->target
	    || P_AproxDistance (mo->x - actor->x, mo->y - actor->y) > PACK_NEAR*FRACUNIT
	    || (mo->mind != MIND_HUNT && mo->mind != MIND_INVESTIGATE && mo->mind != MIND_SEARCH))
	    continue;
	all[n++] = mo;
    }
    for (i = 1; i < n; i++)
	for (j = i; j > 0 && all[j]->serial < all[j - 1]->serial; j--)
	{
	    mobj_t*	t = all[j]; all[j] = all[j - 1]; all[j - 1] = t;
	}
    for (i = 0; i < n; i++)
	if (all[i] == actor)
	    me = i;
    start = me/PACK_MOST*PACK_MOST;
    n = n - start < PACK_MOST ? n - start : PACK_MOST;
    for (i = 0; i < n; i++)
	pack[i] = all[start + i];
    return n;
}

// Word through the pack: the freshest sighting any of them has.
static void PackNews (mobj_t* actor, mobj_t** pack, int n)
{
    int		i;

    for (i = 0; i < n; i++)
	if (pack[i]->knowntic > actor->knowntic)
	{
	    actor->knownx = pack[i]->knownx;
	    actor->knowny = pack[i]->knowny;
	    actor->knowntic = pack[i]->knowntic;
	}
}

// Opportunistic hunters: each to its place round where it thinks he is,
// about the front (where the fight comes at him from). Two imps split up:
// one goes at him from the front, the other round behind where he was; more
// take his flanks too. One imp with soldiers fighting him goes round behind
// him while they hold his eyes; one alone goes straight in.
//   The front is the soldiers fighting him and the pack's first imp (its
// own bearing: it goes straight in); the others' places are taken from it
// by their order, so as they go round, the front stays put.
// It gets there going round him, not cutting in: each step at most 35
// degrees further round, keeping its distance (a spiral); on its side, in
// to its place, then straight at him (false). Very near him, straight in.
static const int	places[PACK_MOST + 1] = { 0, 180, 90, -90, 45, -45, 135, -135, 0 };

static boolean Circling (mobj_t* mo)
{
    return (mo->tactic == STEP_CIRCLING || mo->tactic == STEP_CIRCLING_BACK) && leveltime - mo->tactictic < TICRATE;
}

// (Standing still is not running: at its place on the ring, waiting for the
// angle to come right, it may as well throw. For a moment: throwing all the
// time, it would never get going again.)

// Shot on its way round him: the plan can wait. A run that ignored being shot
// was a back to shoot at. It turns on him, and for a while goes straight at him.
#define ANSWER_TIME	TUNE (ANSWER_TIME, 3*TICRATE)
// Going round him: how often it looks at the ground it has covered, how much is
// getting somewhere, and how many looks with none before it gives that up.
#define FLANK_STALL	TUNE (FLANK_STALL, 2*TICRATE)
#define FLANK_MOVED	TUNE (FLANK_MOVED, 24)
#define FLANK_STALLS	TUNE (FLANK_STALLS, 3)

void P_DerpShotRunning (mobj_t* mo)
{
    if (!Imp (mo) || !Circling (mo))
	return;
    P_MindStep (mo, STEP_NONE, "hunt/circling+hunt/circling_back.shot",
    		"Shot on its way round: it turns on him and "
    		"fights a while");
    mo->answertic = leveltime + ANSWER_TIME;
    mo->movecount = 0;
    P_DerpLog (mo, "shot on its way round: turns on him");
}

boolean P_DerpShooting (mobj_t* actor)
{
    return Imp (actor) && actor->tactic == STEP_SHOOTING && leveltime - actor->tactictic < TICRATE;
}

static boolean Encircle (mobj_t* actor, mobj_t** pack, int n, boolean visible)
{
    static int	path[2];
    int		nav = P_NavClassOf (actor), i, me = 0, off, delta, k, soldiers = 0;
    double	sx = 0, sy = 0;
    angle_t	base, slot, from, an;
    fixed_t	cx = actor->knownx, cy = actor->knowny, px, py, pf, r;
    fixed_t	dist = P_AproxDistance (actor->x - cx, actor->y - cy);
    thinker_t*	th;

    // Right on him it fights: it does not turn its back to get round him.
    if (nav < 0 || dist < CLOSE_IN*FRACUNIT || leveltime < actor->answertic)
	return false;
    for (i = 0; i < n; i++)
	if (pack[i] == actor)
	    me = i;
    // The soldiers fighting him near there: the front.
    for (th = thinkercap.next; th != &thinkercap; th = th->next)
    {
	mobj_t*	mo = (mobj_t *)th;
	angle_t	b;
	// (One that has him in its sights now: going round only works if someone
	// else has his eyes, and one behind a wall has not.)
	if (th->function.acp1 != (actionf_p1)P_MobjThinker || !P_DerpMonster (mo) || !P_DerpSoldier (mo->type)
	    || mo->target != actor->target || (mo->mind != MIND_HUNT && mo->mind != MIND_COVER)
	    || P_AproxDistance (mo->x - cx, mo->y - cy) > PACK_NEAR*FRACUNIT
	    || leveltime - mo->knowntic > ENGAGED)
	    continue;
	b = R_PointToAngle2 (cx, cy, mo->x, mo->y);
	sx += finecosine[b>>ANGLETOFINESHIFT];
	sy += finesine[b>>ANGLETOFINESHIFT];
	soldiers++;
    }
    if (!soldiers)
    {
	// No soldiers: the first imp is the front; alone, it just goes in.
	// With one on its way round, it holds him from where it is, its
	// fireballs on him, until that one is there (or dead, or gave up).
	angle_t	b;
	if (n < 2)
	    return false;
	if (me == 0)
	{
	    if (!visible || dist < 128*FRACUNIT || dist > PACK_NEAR*FRACUNIT)
		return false;
	    for (i = 1; i < n; i++)
		if (Circling (pack[i]))
		{
		    if (actor->tactic != STEP_SHOOTING)
		    {
			P_MindStep (actor, STEP_SHOOTING, "hunt.hold_him", "First of its pack, "
				    "with one on its way round: it holds him with its fireballs");
			P_DerpLog (actor, "holds him with its fireballs: one goes round");
		    }
		    actor->tactictic = leveltime;
		    actor->movedir = NODIR;
		    actor->movecount = 0;
		    actor->angle = R_PointToAngle2 (actor->x, actor->y, cx, cy);
		    return true;
		}
	    return false;
	}
	// The front has to be fighting him for there to be a way round it.
	if (leveltime - pack[0]->knowntic > ENGAGED)
	    return false;
	b = R_PointToAngle2 (cx, cy, pack[0]->x, pack[0]->y);
	sx = finecosine[b>>ANGLETOFINESHIFT];
	sy = finesine[b>>ANGLETOFINESHIFT];
    }
    base = R_PointToAngle2 (0, 0, (fixed_t)sx, (fixed_t)sy);
    // With soldiers in front, every imp takes a place from behind on.
    off = places[soldiers ? me + 1 : me];
    slot = base + (angle_t)off*(ANG45/45);
    from = R_PointToAngle2 (cx, cy, actor->x, actor->y);
    delta = (int)((double)(int)(slot - from)*360/4294967296.0);
    // Going round already: on the same way round, though its place (taken
    // from the others, who move) swings past behind him; turning back there
    // is a dither that gets it nowhere.
    if ((actor->tactic == STEP_CIRCLING || actor->tactic == STEP_CIRCLING_BACK)
	&& leveltime - actor->tactictic < TICRATE && (delta > 120 || delta < -120))
    {
	if (actor->tactic == STEP_CIRCLING && delta < 0)
	    delta += 360;
	else if (actor->tactic == STEP_CIRCLING_BACK && delta > 0)
	    delta -= 360;
    }
    // Near its side the plan is done; a way round that took too long ends,
    // straight in for a while.
    if (delta > -45 && delta < 45)
	actor->plantic = 0;
    else if (actor->plantic && leveltime - actor->plantic > PLAN_TIME && !P_DerpPinned ())
    {
	if (leveltime - actor->plantic < 2*PLAN_TIME)
	    return false;
	actor->plantic = 0;
    }
    // On its side: in to its place, and from there at him; near him and
    // near its side, straight in (far round, it keeps going round).
    if (delta > -20 && delta < 20 && dist < PACK_CIRCLE*FRACUNIT + 48*FRACUNIT)
	return false;
    if (delta > -60 && delta < 60 && dist < PACK_IN*FRACUNIT)
	return false;
    // Its place, else a little nearer or farther (walls cut a circle drawn
    // in a room): the first it can stand on and walk to. The way there
    // keeps off him (navavoid): round, by the grid, through the next room
    // if that is the way, not past his front. (A spiral drawn round him,
    // re-aimed every few tics, cut in at him or drifted off round walls.)
    // Far round, a quarter on its way round first, so the way has a side
    // (to a place right behind him either side is as short).
    an = delta > 90 ? from + ANG90 : delta < -90 ? from - ANG90 : slot;
    r = PACK_CIRCLE*FRACUNIT;
    // That way walled off (he stands by a wall on that side): round the other.
    // It stood at the ring facing the wall, going nowhere, for as long as the
    // first choice of side held.
    for (k = 0; k < 6; k++)
    {
	if (k == 3)
	{
	    if (delta > -90 && delta < 90)
		break;
	    an += ANG180;
	    delta = -delta;
	}
	static const int	scale[3] = { 8, 6, 11 };
	fixed_t			rr = r/8*scale[k%3];
	int			cell = P_NavCellAt (nav, cx + FixedMul (rr, finecosine[an>>ANGLETOFINESHIFT]),
						    cy + FixedMul (rr, finesine[an>>ANGLETOFINESHIFT]));
	// (A place in the nukage will do: it does not mind.)
	if (cell < 0 || P_NavDoor (nav, cell))
	    continue;
	P_NavPoint (nav, cell, &px, &py, &pf);
	// Its way may be a jump (off a perch, through a window), as it will walk it.
	P_DerpRouteBegin (actor);
	i = P_NavPathFor (actor, nav, actor->x, actor->y, px, py, path, 2, 6000);
	P_DerpRouteEnd ();
	// The tic's routes spent: one already going round keeps on by its waypoint
	// (its clocks as they were), one not yet does what it would without a
	// place; either asks again next step.
	if (i == NAV_DEFERRED)
	{
	    if (!Circling (actor) || (actor->wayx == actor->x && actor->wayy == actor->y))
		return false;
	    navavoidx = cx;
	    navavoidy = cy;
	    navavoidr = PACK_CIRCLE;
	    P_DerpWalkTo (actor, actor->wayx, actor->wayy);
	    navavoidr = 0;
	    return true;
	}
	if (i <= 0)
	    continue;
	if (P_AproxDistance (actor->x - px, actor->y - py) < 48*FRACUNIT)
	    return false;
	navavoidx = cx;
	navavoidy = cy;
	navavoidr = PACK_CIRCLE;
	P_DerpWalkTo (actor, px, py);
	navavoidr = 0;
	// Getting nowhere: it fights from there, and at last that plan is off,
	// straight for him by any way there is, a window if that is the way. It
	// stood marked as running, not throwing. (By the ground it covered: its
	// place round him keeps moving, so nearer its waypoint says nothing.)
	if (!Circling (actor))
	{
	    actor->stalltic = 0;
	    actor->stalls = 0;
	}
	else if (!actor->stalltic)
	{
	    actor->stalltic = leveltime;
	    actor->stallx = actor->x;
	    actor->stally = actor->y;
	}
	else if (leveltime - actor->stalltic >= FLANK_STALL)
	{
	    boolean	stuck = P_AproxDistance (actor->x - actor->stallx, actor->y - actor->stally) < FLANK_MOVED*FRACUNIT;
	    actor->stalltic = leveltime;
	    actor->stallx = actor->x;
	    actor->stally = actor->y;
	    actor->stalls = stuck ? actor->stalls + 1 : 0;
	    if (actor->stalls == 1)
		actor->movecount = 0;
	    if (actor->stalls >= FLANK_STALLS)
	    {
		P_MindStep (actor, STEP_NONE, "hunt/circling+hunt/circling_back.stalled",
				"It gets nowhere going "
				"round: it comes straight");
		actor->stalltic = 0;
		actor->stalls = 0;
		actor->answertic = leveltime + ANSWER_TIME;
		actor->movecount = 0;
		P_DerpLog (actor, "gets nowhere going round: comes straight");
		return false;
	    }
	}
	// Going round (not yet on its side): no pounce at him from here while
	// he faces it (P_DerpHunt).
	if (delta <= -45 || delta >= 45)
	{
	    if (!actor->plantic)
	    {
		actor->plantic = leveltime;
		P_DerpLog (actor, "runs round him (%d degrees to go)", delta);
	    }
	    if (actor->tactic != (delta > 0 ? STEP_CIRCLING : STEP_CIRCLING_BACK))
		P_MindStep (actor, delta > 0 ? STEP_CIRCLING : STEP_CIRCLING_BACK, "hunt.runs_round",
			    "Others hold him: it runs round to its place on his far side");
	    actor->tactictic = leveltime;
	    // Out of his sight it talks to the others: he can hear where it is.
	    if (!visible)
		P_DerpCry (actor, BARK_IMPCLICKS);
	}
	return true;
    }
    return false;
}

// Him in sight a floor above or below (more than a step): vanilla's chase
// would walk into the ledge; its route goes up or down it (a climber's
// jumps, p_nav.h).
static boolean Climb (mobj_t* actor, mobj_t* player)
{
    fixed_t	dz = player->z - actor->z;

    // A plain chase with no step left to take: he is in sight over a fence, or across
    // what it cannot walk straight over. It goes by its route for a while. (Two stood
    // on a walkway from when they landed until he died, fireballs and all.)
    if (actor->movedir == NODIR && leveltime >= actor->answertic)
	actor->answertic = leveltime + ANSWER_TIME;
    // (Or fighting him after a run came to nothing: by its route, jumps and all,
    // since a wall or a window between them stops a plain chase.)
    if (((dz <= 24*FRACUNIT && dz >= -24*FRACUNIT) && leveltime >= actor->answertic)
	|| P_AproxDistance (player->x - actor->x, player->y - actor->y) < 64*FRACUNIT)
	return false;
    P_DerpWalkTo (actor, player->x, player->y);
    return true;
}

// Straight at him while there is a straight, else by its way. Steps too quick to think:
// every one of them turned to him afresh.
static boolean Rushing (mobj_t* actor, mobj_t* player, boolean visible)
{
    if (leveltime - actor->rushtic > RUSH_TIME)
    {
	P_DerpRule (actor, "hunt.rush_over", "Its rush spent: it fights on as it can, and does not run");
	actor->rushtic = 0;
	return false;
    }
    actor->tics = RUSH_TICS;
    actor->movecount = 0;
    if (visible && actor->movedir != NODIR)
	return false;
    P_DerpWalkTo (actor, player->x, player->y);
    return true;
}

boolean P_DerpHunt (mobj_t* actor, boolean visible)
{
    mobj_t*	pack[PACK_MOST];
    int		n;
    mobj_t*	player = P_DerpPlayer ();

    if (!Imp (actor) || !player || actor->target != player)
	return false;
    actor->combo = 0;
    // On the ceiling, going up, about to let go.
    if (actor->hang && P_HangThink (actor, visible))
	return true;
    // In the air (a leap): nothing to walk on until it lands.
    if (actor->z > actor->floorz)
    {
	actor->movedir = NODIR;
	return true;
    }
    if (actor->rushtic)
	return Rushing (actor, player, visible);
    // (As a soldier's, p_tactics.c: the tic's routes spent, it thinks next tic;
    // let through, its rules' routes run, up to a near monster's tic of them.)
    if (leveltime >= actor->decidetic && !P_MindLurks (actor) && P_NavBudgetSpent (actor))
	actor->decidetic = leveltime + 1;
    else if (leveltime >= actor->decidetic && !P_MindLurks (actor))
    {
	actor->decidetic = P_DecideNext (actor, DECIDE_EVERY);
	P_NavBudgetHold ();
	Decide (actor, visible);
	P_NavBudgetRelease ();
    }
    // Waiting for him, or stalking him.
    if (P_HunterThink (actor, visible))
	return true;
    switch (actor->mind)
    {
      case MIND_HEAL:
	return Heal (actor, player, visible);
      case MIND_FLANK:
	return Flank (actor, player, visible);
      default:
	break;
    }
    n = Pack (actor, pack);
    if (!visible)
    {
	// Out of sight: the pack's word of him, and its place round there.
	if (actor->mind != MIND_HUNT)
	    return false;
	PackNews (actor, pack, n);
	return Encircle (actor, pack, n, false);
    }
    Seen (actor, player);
    if (actor->mind != MIND_HUNT)
	P_MindBecome (actor, MIND_HUNT, STEP_NONE, "any.in_sight", "He is in plain sight: after him");
    // Opportunistic, not reckless: on its way round him, a pounce only when
    // it is behind him (more than 120 degrees off where he looks); there,
    // any time.
    // Holding him with its fireballs, no pounce either. On its way round,
    // no side-steps from his shots: it runs (one that dodged got nowhere).
    if (Circling (actor)
	&& (angle_t)(R_PointToAngle2 (player->x, player->y, actor->x, actor->y) - player->angle + BEHIND)
	   < (angle_t)(2*BEHIND))
	return Vault (actor, player) || Encircle (actor, pack, n, true) || Climb (actor, player);
    if (P_DerpShooting (actor))
	return Sidestep (actor, player) || Encircle (actor, pack, n, true) || Climb (actor, player);
    return Pounce (actor, player) || Vault (actor, player) || Sidestep (actor, player)
	|| Encircle (actor, pack, n, true) || Climb (actor, player);
}


//
// Its claws.
//

boolean P_DerpFleeing (mobj_t* actor)
{
    // (Hanging over him is no swipe either.)
    return Imp (actor) && (actor->mind == MIND_HEAL || actor->mind == MIND_FLANK || (actor->hang >= 2 && actor->hang <= 4));
}

boolean P_DerpMelee (mobj_t* actor)
{
    if (!Imp (actor) || !actor->target)
	return false;
    // Its blows just landed: now and then away instead of another.
    if (Fresh (actor) && actor->target == P_DerpPlayer () && !actor->rushed
	&& HitAndRun (actor, actor->target))
	return true;
    // Straight to the claws raised: the blow comes in a tenth of a second.
    if (!P_DerpCry (actor, BARK_IMPSWIPE) && actor->info->attacksound)
	S_StartSound (actor, actor->info->attacksound);
    actor->combo = 0;
    P_SetMobjState (actor, S_TROO_ATK2);
    actor->tics = QUICK_SWIPE;
    P_DerpLog (actor, "swipes");
    return true;
}

void P_DerpClawed (mobj_t* actor)
{
    if (!Imp (actor) || !actor->target)
	return;
    // The first blow of a flurry shoves him; the rest land where he is, so
    // one hit does not carry him into all the others.
    if (!actor->combo)
	Shove (actor, actor->target);
    Struck (actor);
    // Another swipe, three at most: the striking frame again, at once. The
    // action runs as its state is set; the outer state change sees the new
    // state and its tics, and leaves it.
    if (actor->combo < 2 && actor->target->health > 0
	&& P_CheckMeleeRange (actor) && P_Random () < 160)
    {
	actor->combo++;
	P_SetMobjState (actor, S_TROO_ATK2);
	actor->tics = SWIPE_AGAIN;
	P_DerpLog (actor, "swipes again (%d)", actor->combo + 1);
    }
}

void P_DerpLead (mobj_t* actor, mobj_t* missile)
{
    mobj_t*	t = actor->target;
    fixed_t	speed, px, py, dist;
    int		tics, lead;
    angle_t	an;

    // A missile that burst at once (spawned in a wall) is no longer one.
    if (!Imp (actor) || !t || !missile || !(missile->flags & MF_MISSILE)
	|| (t->flags & MF_SHADOW))
	return;
    speed = missile->info->speed;
    if (speed <= 0)
	return;
    // A second's worth at most: farther off it would only be guessing.
    tics = P_AproxDistance (t->x - actor->x, t->y - actor->y)/speed;
    if (tics > TICRATE)
	tics = TICRATE;
    // Half to all of the way he is going: it reads him, not perfectly.
    lead = 128 + (P_Random () >> 1);
    px = t->x + (fixed_t)((long long)t->momx*tics*lead/256);
    py = t->y + (fixed_t)((long long)t->momy*tics*lead/256);
    an = R_PointToAngle2 (actor->x, actor->y, px, py);
    missile->angle = an;
    missile->momx = FixedMul (speed, finecosine[an>>ANGLETOFINESHIFT]);
    missile->momy = FixedMul (speed, finesine[an>>ANGLETOFINESHIFT]);
    dist = P_AproxDistance (px - actor->x, py - actor->y)/speed;
    if (dist < 1)
	dist = 1;
    missile->momz = (t->z - actor->z)/dist;
}


//
// Each tic.
//

// The pounce, from the crouch to the landing.
static void Pouncing (mobj_t* mo)
{
    mobj_t*	t = mo->target;

    // Anything else befell it (pain, death, its target gone): no pounce.
    if (!Imp (mo) || !t || t->health <= 0 || t != P_DerpPlayer () || P_Unseen (t)
	|| (mo->state != &states[S_TROO_ATK1] && mo->state != &states[S_TROO_ATK2]))
    {
	P_DerpRule (mo, "hunt/pouncing.broken", "Pain, or its target gone: no pounce");
	// Caught in the air: the shriek stops with it.
	if (mo->vault)
	    P_DerpCutOff (mo);
	mo->pounce = 0;
	return;
    }
    // Its frame held while the pounce lasts.
    if (mo->tics < 2)
	mo->tics = 2;
    if (mo->pounce == 1)
    {
	fixed_t	px, py, dist, ceiling;
	int	flight;
	A_FaceTarget (mo);
	if (leveltime - mo->pouncetic < CROUCH)
	    return;
	if (mo->vault)
	{
	    // Where he is now, if that will do; else where he was.
	    VaultSpot (mo, t, &mo->landx, &mo->landy);
	    Leap (mo, R_PointToAngle2 (mo->x, mo->y, mo->landx, mo->landy),
		  P_AproxDistance (mo->landx - mo->x, mo->landy - mo->y)/VAULT_TICS, VAULT_UP);
	    P_DerpCry (mo, BARK_IMPAIR);
	    P_DerpBarkNear (mo->x, mo->y, 640, BARK_IMPOVER);
	    P_SetMobjState (mo, S_TROO_ATK2);
	    mo->tics = 2;
	    mo->pounce = 2;
	    mo->pouncetic = leveltime;
	    return;
	}
	// Under the lower of its ceiling and his: a low room's leap is short.
	ceiling = mo->ceilingz < t->ceilingz ? mo->ceilingz : t->ceilingz;
	flight = Flight (ceiling - mo->height - mo->z);
	if (flight < 4)
	    flight = 4;
	// At where he will be as it comes down, half of the way he is going:
	// a run sideways can still dodge it.
	// Aimed at him, not short of him: his body stops it, and the floor's
	// friction takes a tenth off its first tic.
	px = t->x + t->momx*flight/2;
	py = t->y + t->momy*flight/2;
	dist = P_AproxDistance (px - mo->x, py - mo->y)/(flight - 2);
	if (dist < 6*FRACUNIT)
	    dist = 6*FRACUNIT;
	if (dist > LEAP_FAST)
	    dist = LEAP_FAST;
	Leap (mo, R_PointToAngle2 (mo->x, mo->y, px, py), dist, LEAP_UP);
	// Claws out.
	P_SetMobjState (mo, S_TROO_ATK2);
	mo->tics = 2;
	mo->pounce = 2;
	mo->pouncetic = leveltime;
	return;
    }
    // In the air: a blow as soon as he is in reach, one only.
    if (mo->pounce == 2 && !mo->vault && Reach (mo, t))
    {
	Strike (mo, t);
	mo->pounce = 3;
    }
    // Down again (or too long at it): back to the hunt, and a while before
    // the next.
    if ((leveltime - mo->pouncetic > 2 && mo->z <= mo->floorz && mo->momz <= 0)
	|| leveltime - mo->pouncetic > 3*TICRATE)
    {
	P_DerpRule (mo, "hunt/pouncing.landed", "Down again: back to the hunt, and a while before the "
		    "next");
	mo->pounce = 0;
	mo->pouncetic = leveltime;
	// Behind him: round, and at him at once.
	if (mo->vault)
	{
	    A_FaceTarget (mo);
	    P_DerpLog (mo, "lands behind him");
	    P_SetMobjState (mo, mo->info->missilestate);
	    return;
	}
	P_SetMobjState (mo, mo->info->seestate);
    }
}

void P_DemonThink (mobj_t* mo)
{
    mobj_t*	player;
    int		full, more;

    if (mo->pounce)
	Pouncing (mo);
    // Dropped from the ceiling and down: an ordinary thing again.
    if (mo->hang == 5 && mo->z <= mo->floorz && leveltime - mo->hangtic > 2)
	P_HangEnd (mo);
    // Down, and off him: an ordinary thing again.
    if (mo->vault && !mo->pounce && mo->z <= mo->floorz && (player = P_DerpPlayer ()) != NULL
	&& P_AproxDistance (player->x - mo->x, player->y - mo->y) >= mo->radius + player->radius)
	mo->vault = false;
    P_ChargeThink (mo);
    // Mid-climb up a ledge: over the edge once high enough.
    if (Imp (mo))
    {
	P_DerpMantle (mo);
	P_DerpJumped (mo);
    }
    // Once a second each, spread over the second by their numbers.
    if ((leveltime + mo->serial) % TICRATE || !Demons () || !P_DerpMonster (mo) || !P_DerpDemon (mo->type))
	return;
    // By a dead soldier, out of his sight: it is pleased.
    if (Imp (mo) && (player = P_DerpPlayer ()) != NULL && P_DerpBodyNear (mo->x, mo->y, OVER_BODY) && !P_CheckSight (player, mo))
	P_DerpCry (mo, BARK_IMPOVERBODY);
    full = P_DerpHealth (mo->type);
    if (mo->health >= full*3/4 || mo->healed*100 >= full*HEAL_ALLOWANCE)
	return;
    player = P_DerpPlayer ();
    if (!player || P_CheckSight (player, mo))
    {
	mo->seentic = leveltime;
	return;
    }
    if (leveltime - mo->seentic < HEAL_AFTER)
	return;
    // A sixtieth of its health a second: an imp from badly hurt to mended
    // in some twenty seconds, a baron no sooner.
    more = full/60 > 1 ? full/60 : 1;
    if (mo->health + more > full*3/4)
	more = full*3/4 - mo->health;
    mo->health += more;
    mo->healed += more;
    // A low rasp every few seconds: heard through the walls, it says where
    // it is, and that time is running out.
    if (!(((leveltime + mo->serial)/TICRATE) & 3) && mo->info->activesound && P_DerpVoices () && !P_DerpHushed (mo))
	S_StartSound (mo, mo->info->activesound);
}
