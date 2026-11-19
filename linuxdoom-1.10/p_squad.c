// DERP's squads (p_tactics.h): who is whose, nerve, the losses heard, a kill in
// silence and its body found, how alert that leaves them, the squad to join.

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "doomdef.h"
#include "doomstat.h"
#include "i_system.h"
#include "m_random.h"
#include "p_local.h"
#include "p_derp.h"
#include "p_nav.h"
#include "p_navbudget.h"
#include "p_senses.h"
#include "p_tactics.h"
#include "p_decide.h"
#include "p_word.h"
#include "p_hang.h"
#include "p_derplog.h"
#include "p_corners.h"
#include "p_bullets.h"
#include "p_fire.h"
#include "p_director.h"
#include "p_portals.h"
#include "p_coverplan.h"
#include "p_gunwalk.h"
#include "p_roles.h"
#include "p_chatter.h"
#include "p_difficulty.h"
#include "p_barks.h"
#include "r_main.h"
#include "r_state.h"
#include "s_sound.h"
#include "tables.h"
#include "p_tune.h"
#include "p_mind.h"
#include "p_tactics_local.h"

static void SawFall (mobj_t* target);
static boolean NextDoor (int a, int b);


// The bodies of soldiers and of silent deaths, waiting to be found (P_DerpKilled).
typedef struct { fixed_t x, y, z; int squad, serial; boolean found; } body_t;	// serial: whose
static body_t	bodies[64];
static int	numbodies;
static int	bodylevel = -1;


//
// Squads and nerve.
//

// Where the demons' squad numbers start (a room's soldiers have its number).
#define DEMON_SQUADS	0x10000

// Its squad (its home room, plus one; 0 none) and nerve, worked out once:
// nerve from its serial number, so no random number is drawn for it.
void P_TacKnow (mobj_t* mo)
{
    unsigned	h;

    if (mo->squad)
	return;
    mo->squad = P_NavRoom (mo->homesector) + 1;
    if (mo->squad <= 0)
	mo->squad = -1;	// no rooms here: a squad of its own
    // The possessed and the demons of one room are not one squad: they warn
    // each other (a shout, a shot, a death heard alerts all), but a demon's
    // death is no soldier's loss, an imp is no soldier's friend, and they do
    // not regroup with each other. Each kind its own numbers.
    if (!P_DerpSoldier (mo->type))
	mo->squad += mo->squad > 0 ? DEMON_SQUADS : -1;
    h = (unsigned)mo->serial*2654435761u;
    mo->nerve = 30 + (int)((h >> 24) % 71);
}

// At level setup, before anything else of the tactics: a restart or a load of
// the same map, and its old bodies are not there.
void P_SquadLevel (void)
{
    numbodies = 0;
    bodylevel = gamemap + 100*gameepisode;
}

// Every monster its squad and nerve, at level setup.
void P_SquadsKnown (void)
{
    thinker_t*	th;

    for (th = thinkercap.next; th != &thinkercap; th = th->next)
	if (th->function.acp1 == (actionf_p1)P_MobjThinker && P_DerpMonster ((mobj_t *)th))
	    P_TacKnow ((mobj_t *)th);
}


// A room's soldiers: most hold it. A share answers a far fight, a third, one at the
// least. All of them if he can be finished: hurt, and the director pressing.
#define GARRISON_SENDS	TUNE (GARRISON_SENDS, 3)
#define GARRISON_ENDS	TUNE (GARRISON_ENDS, 35)

boolean P_DerpGarrisonHolds (mobj_t* mo)
{
    mobj_t*	player = P_DerpPlayer ();
    thinker_t*	th;
    int		out = 0, all = 0;

    if (!P_TacticsOn () || !derpparts.director || !player || !P_DerpMonster (mo) || !P_DerpSoldier (mo->type))
	return false;
    if (player->health < GARRISON_ENDS && P_DirectorPressing ())
	return false;
    P_TacKnow (mo);
    for (th = thinkercap.next; th != &thinkercap; th = th->next)
    {
	mobj_t*	f = (mobj_t *)th;
	if (th->function.acp1 != (actionf_p1)P_MobjThinker || !P_DerpMonster (f) || !P_DerpSoldier (f->type))
	    continue;
	P_TacKnow (f);
	if (f->squad != mo->squad)
	    continue;
	all++;
	if (f != mo && f->target == player && f->mind != MIND_NONE && f->mind != MIND_AMBUSH)
	    out++;
    }
    return out >= (all/GARRISON_SENDS > 0 ? all/GARRISON_SENDS : 1);
}

// How many of actor's squad it knows still stand: those near enough to see
// or hear (not a count of the living across the map).
int P_TacFriends (mobj_t* actor)
{
    thinker_t*	th;
    int		n = 0;

    P_TacKnow (actor);
    for (th = thinkercap.next; th != &thinkercap; th = th->next)
    {
	mobj_t*	mo = (mobj_t *)th;
	if (th->function.acp1 == (actionf_p1)P_MobjThinker && mo != actor && P_DerpMonster (mo))
	{
	    P_TacKnow (mo);
	    if (mo->squad == actor->squad
		&& P_AproxDistance (mo->x - actor->x, mo->y - actor->y) < NEARBY*FRACUNIT)
		n++;
	}
    }
    return n;
}


//
// Silent deaths and bodies.
//

// Soldiers in the fight who see one of theirs drop, with or without a sound:
// that is them next. Down, and from now on careful how they come at him.
#define FALL_SEEN	TUNE (FALL_SEEN, 1024)
#define FALL_FRIGHT	TUNE (FALL_FRIGHT, 70)

static void SawFall (mobj_t* target)
{
    thinker_t*	th;

    if (!P_DerpSoldier (target->type))
	return;
    for (th = thinkercap.next; th != &thinkercap; th = th->next)
    {
	mobj_t*	mo = (mobj_t *)th;
	if (th->function.acp1 != (actionf_p1)P_MobjThinker || mo == target || !P_DerpMonster (mo)
	    || !P_DerpSoldier (mo->type) || mo->mind == MIND_NONE
	    || P_AproxDistance (mo->x - target->x, mo->y - target->y) > FALL_SEEN*FRACUNIT
	    || !P_CheckSight (mo, target))
	    continue;
	mo->fallen++;
	mo->suppress = FALL_FRIGHT;
	mo->suppresstic = leveltime;
	// It thinks about it soon, each in its own few tics: all who saw it go
	// down thought on the same tic, and every DECIDE_EVERY after.
	mo->decidetic = P_DecideSoon (mo, 8);
	// Whatever did that is about there: better word than none.
	if (!mo->knowntic || leveltime - mo->knowntic > COVER_BELIEF)
	{
	    mo->knownx = target->x;
	    mo->knowny = target->y;
	    mo->knowntic = leveltime;
	}
	P_DerpLog (mo, "sees a friend go down");
    }
}

void P_DerpKilled (mobj_t* target, mobj_t* source)
{
    P_DerpCutOff (target);
    P_WordGone (target);
    P_HangEnd (target);
    if (!P_TacticsOn () || !(target->flags & MF_COUNTKILL))
	return;
    P_PortalDeath (target);
    if (source && source->player)
	P_DirectorFell ();
    // Dying on one knee: the body falls at its full height (P_KillMobj
    // quarters it; an arch-vile raises it four times again).
    if (target->kneeltic)
    {
	target->height = target->info->height;
	target->kneeltic = 0;
    }
    target->silent = 0;
    SawFall (target);
    // The bosses die out loud whatever hit them. And it is the pistol's trick:
    // nine pellets find a head by chance (9 of 12 silent deaths in a run of the
    // user's), and then nobody hears anybody die.
    // A headshot: no scream. Nobody knows until somebody sees the body.
    if (source && source->player && source->player->readyweapon == wp_pistol
	&& target->headtic == leveltime
	&& target->type != MT_CYBORG && target->type != MT_SPIDER)
	target->silent = 1;
    // Silent or not, a dead soldier lying there tells whoever comes on it that
    // something is wrong.
    if (!target->silent && !P_DerpSoldier (target->type))
	return;
    if (bodylevel != gamemap + 100*gameepisode)
    {
	numbodies = 0;
	bodylevel = gamemap + 100*gameepisode;
    }
    if (numbodies < 64)
    {
	P_TacKnow (target);
	bodies[numbodies].x = target->x;
	bodies[numbodies].y = target->y;
	bodies[numbodies].z = target->z;
	bodies[numbodies].squad = target->squad;
	bodies[numbodies].serial = target->serial;
	bodies[numbodies].found = false;
	numbodies++;
    }
}

void P_DerpForgetBodies (void)
{
    numbodies = 0;
}

boolean P_DerpSilent (mobj_t* mo)
{
    return P_TacticsOn () && mo->silent;
}

void P_DerpLossHeard (mobj_t* listener, mobj_t* dead)
{
    // A monster's death: a barrel going up is a noise, not a loss.
    if (!P_TacticsOn () || !P_DerpMonster (listener) || !dead || dead == listener
	|| !(dead->flags & MF_COUNTKILL))
	return;
    P_TacKnow (listener);
    P_TacKnow (dead);
    P_MindAlert (listener, 1);
    // A fellow soldier gone, whoever's squad: the fight next door is going
    // badly, and he may come this way next (the ambush, below).
    if (P_DerpSoldier (dead->type))
    {
	P_DerpBark (listener, P_TacFriends (listener) ? BARK_FRIENDDIES : BARK_LASTALIVE);
	listener->fallen++;
	listener->decidetic = leveltime + listener->serial % TICRATE;
    }
    if (dead->squad != listener->squad)
	return;
    // One of its own: it counts them (its fear, Decide; its nerve is who it
    // is, and stays: a loss counted there too would count twice).
    listener->losses++;
    // Thinks it over soon, each at its own moment: not the whole squad in one tic.
    listener->decidetic = leveltime + listener->serial % TICRATE;
}

// A stand-in for a point to see, half a body tall.
mobj_t* P_TacPoint (fixed_t x, fixed_t y)
{
    static mobj_t	spot;

    memset (&spot, 0, sizeof spot);
    spot.x = x;
    spot.y = y;
    spot.subsector = R_PointInSubsector (x, y);
    spot.z = spot.subsector->sector->floorheight;
    spot.height = 28*FRACUNIT;
    return &spot;
}

void A_Look (mobj_t* actor);

boolean P_DerpBodies (mobj_t* actor)
{
    int		i;
    mobj_t*	player = P_DerpPlayer ();

    if (!P_TacticsOn () || !P_DerpMonster (actor) || bodylevel != gamemap + 100*gameepisode)
	return false;
    P_TacKnow (actor);
    for (i = 0; i < numbodies; i++)
    {
	body_t*	b = &bodies[i];
	if (b->found || P_AproxDistance (b->x - actor->x, b->y - actor->y) > 1024*FRACUNIT
	    || !P_CheckSight (actor, P_TacPoint (b->x, b->y)))
	    continue;
	// Found: the alarm, worse for coming late.
	P_DerpLog (actor, "finds a body%s", b->squad == actor->squad ? " of its own squad" : "");
	b->found = true;
	P_MindAlert (actor, 2);
	actor->awareness = 0;
	actor->fallen++;
	if (b->squad == actor->squad)
	    actor->losses++;
	if (player)
	{
	    actor->target = player;
	    P_MindBecome (actor, MIND_INVESTIGATE, STEP_NONE, "none+investigate+search+cover/watching.finds_body",
			  "It finds a body: it shouts, and goes to where it lies [all]");
	    actor->goalx = b->x;
	    actor->goaly = b->y;
	    actor->goaltic = leveltime;
	    actor->knownx = b->x;
	    actor->knowny = b->y;
	    actor->knowntic = leveltime;
	    // The others hear it: a shout about where the body lies.
	    P_DerpNoise (actor, NOISE_SHOUT, actor->x, actor->y);
	    if (actor->state->action.acp1 == (actionf_p1)A_Look)
		P_SetMobjState (actor, actor->info->seestate);
	}
	return true;
    }
    return false;
}

boolean P_DerpBodyNear (fixed_t x, fixed_t y, int range)
{
    int	i;

    for (i = 0; i < numbodies; i++)
	if (P_AproxDistance (bodies[i].x - x, bodies[i].y - y) <= range*FRACUNIT)
	    return true;
    return false;
}

// Whether rooms a and b are one, or meet at a link: next door.
static boolean NextDoor (int a, int b)
{
    const navlink_t*	links;
    int			n = P_NavLinks (&links), i;

    if (a == b)
	return true;
    for (i = 0; i < n; i++)
	if ((links[i].rooma == a && links[i].roomb == b) || (links[i].rooma == b && links[i].roomb == a))
	    return true;
    return false;
}

// The nearest other squad it knows of: one of its living members it can
// see, or one in its room or the next (it has heard them about, or saw them
// on its way); not one across the map it has no word of.
boolean P_TacFindSquad (mobj_t* actor, fixed_t* gx, fixed_t* gy, int* squad)
{
    thinker_t*	th;
    fixed_t	bestd = MAXINT;
    int		room = P_NavRoom (actor->subsector->sector - sectors);

    P_TacKnow (actor);
    for (th = thinkercap.next; th != &thinkercap; th = th->next)
    {
	mobj_t*	mo = (mobj_t *)th;
	fixed_t	d;
	if (th->function.acp1 != (actionf_p1)P_MobjThinker || mo == actor || !P_DerpMonster (mo))
	    continue;
	P_TacKnow (mo);
	if (mo->squad == actor->squad || P_DerpSoldier (mo->type) != P_DerpSoldier (actor->type))
	    continue;
	d = P_AproxDistance (mo->x - actor->x, mo->y - actor->y);
	if (d >= bestd
	    || (!NextDoor (room, P_NavRoom (mo->subsector->sector - sectors)) && !P_CheckSight (actor, mo)))
	    continue;
	if (d < bestd)
	{
	    bestd = d;
	    *gx = mo->x;
	    *gy = mo->y;
	    *squad = mo->squad;
	}
    }
    return bestd != MAXINT;
}


boolean P_DerpFriendlyFire (mobj_t* target, mobj_t* source)
{
    return P_TacticsOn () && source && !source->player && P_DerpMonster (target)
	&& P_DerpSoldier (target->type) && P_DerpSoldier (source->type);
}

// A demon hit one of them: it is no friend of theirs, and the squad goes after
// it, those near who see it. Only their target changes here: their own next
// chase does the rest (nothing runs another's state machine from a hit).
#define SQUAD_ANSWERS	TUNE (SQUAD_ANSWERS, 2)
#define SQUAD_ANGER	TUNE (SQUAD_ANGER, 100)

void P_DerpSquadAnswers (mobj_t* target, mobj_t* source)
{
    thinker_t*	th;
    int		n = 0;

    if (!P_TacticsOn () || !source || source->player || !P_DerpMonster (source) || !P_DerpMonster (target)
	|| !P_DerpSoldier (target->type) || P_DerpSoldier (source->type))
	return;
    for (th = thinkercap.next; th != &thinkercap && n < SQUAD_ANSWERS; th = th->next)
    {
	mobj_t*	mo = (mobj_t *)th;
	if (th->function.acp1 != (actionf_p1)P_MobjThinker || mo == target || !P_DerpMonster (mo)
	    || !P_DerpSoldier (mo->type) || !mo->target || !mo->target->player || mo->threshold
	    || P_AproxDistance (mo->x - target->x, mo->y - target->y) >= NEARBY*FRACUNIT
	    || !P_CheckSight (mo, source))
	    continue;
	mo->target = source;
	mo->threshold = SQUAD_ANGER;
	P_DerpLog (mo, "goes after #%d: it hit one of the squad", source->serial);
	P_DerpBark (mo, BARK_GETDEMON);
	n++;
    }
}

void P_DerpRaised (mobj_t* mo)
{
    int		i;

    // Up again (an arch-vile): no body there to be found any more.
    if (bodylevel == gamemap + 100*gameepisode)
	for (i = 0; i < numbodies; i++)
	    if (bodies[i].serial == mo->serial)
		bodies[i].found = true;
    mo->silent = 0;
    mo->kneeltic = 0;
    mo->lowcover = 0;
    mo->fightsfrom = 0;
    P_MindReset (mo);
    mo->losses = 0;
    mo->fallen = 0;
    mo->firepostic = 0;
    mo->healed = 0;
    mo->boundtic = 0;
    mo->holdtic = 0;
    mo->alert = 0;
    mo->ambushed = 0;
    mo->retreathealth = 0;
    mo->combo = 0;
    mo->pounce = 0;
    mo->vault = 0;
    mo->rushtic = 0;
    mo->rushed = 0;
    mo->hang = 0;
    mo->striketic = 0;
    mo->angrytic = 0;
    mo->strides = 0;
    mo->stridetics = 0;
}
