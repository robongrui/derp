//
// DERP's director (p_director.h).
//

#include "doomdef.h"
#include "doomstat.h"
#include "m_random.h"
#include "p_local.h"
#include "p_derp.h"
#include "p_barks.h"
#include "p_director.h"
#include "p_fire.h"
#include "p_nav.h"
#include "p_navbudget.h"
#include "p_senses.h"
#include "p_mind.h"
#include "p_derplog.h"
#include "r_main.h"
#include "r_state.h"
#include "p_tune.h"

// How long it must be quiet before a nudge, and what counts as quiet.
#define QUIET_NUDGE	TUNE (QUIET_NUDGE, 45*TICRATE)
#define CALM		TUNE (CALM, 100)
// Vanilla Doomguy's running speed, units a tic, rounded down: offstage no
// faster. Not the player's own (P_DerpRunSpeed: Quake's pace in DERP's
// game): the director is there to make it cool, not fair (the user).
#define RUN_SPEED	TUNE (RUN_SPEED, 16)
// Only this far from him: a soldier next door at double speed was at the door
// before his shot had died away, and looked as if it knew where he was.
#define OFFSTAGE_FAR	TUNE (OFFSTAGE_FAR, 1024)
// Below this health, unseen, the player is let be for a while: this long.
#define BADLY_HURT	TUNE (BADLY_HURT, 25)
#define HOLD_BACK	TUNE (HOLD_BACK, 20*TICRATE)

// The pressure on him: monsters after him, in the fight or on their way
// (counted every few tics, and at once as each is sent). At PRESSURE_MOST a
// far noise readies the rest instead of bringing them (P_DirectorAdmit), so
// the first fight does not empty the level; for SURGE_TIME in every
// SURGE_EVERY the limit is off, and the fight spills over.
#define PRESSURE_MOST	TUNE (PRESSURE_MOST, 8)
#define SURGE_EVERY	TUNE (SURGE_EVERY, 45*TICRATE)
#define SURGE_TIME	TUNE (SURGE_TIME, 6*TICRATE)
// The stance: every so often it decides, for them all, whether this is the
// moment to press him or to hold back. Hurt, he invites it; with enough on him
// already, less so. Rules that would pull apart (rounds on him: go in, or
// leave him be?) ask this instead of each deciding for itself, so a fight
// goes in surges and lulls and they act together.
#define STANCE_EVERY	TUNE (STANCE_EVERY, 10*TICRATE)
#define STANCE_EVEN	TUNE (STANCE_EVEN, 128)
#define STANCE_SWAY	TUNE (STANCE_SWAY, 64)
#define STANCE_HURT	TUNE (STANCE_HURT, 50)
// Pressing him is no plan if he is picking them off: this many down inside this
// long and it holds back at once, and is slow to press again. (It pressed for 80
// seconds on E1M1, eight soldiers fed into him one by one.)
#define LOSING_MOST	TUNE (LOSING_MOST, 2)
#define LOSING_TIME	TUNE (LOSING_TIME, 10*TICRATE)
// And they set off a few at a time: not the whole of the budget on one noise.
#define WAVE_MOST	TUNE (WAVE_MOST, 4)
#define WAVE_TIME	TUNE (WAVE_TIME, 3*TICRATE)
// Those ready on word of a fight it sends in, each time it turns to press him.
#define WORD_SENDS	TUNE (WORD_SENDS, 2)

static int	tension, quiet, lasthealth = -1, pressure, wave, wavetic;
static int	stancetic;
static int	losses, losstic;
static int	sent;
static boolean	pressing;
static boolean	seen;
// When the holding back began (-1: not holding back).
static int	holdtic = -1;
// The tension the log last heard of.
static int	logged;

static boolean Director (void)
{
    return derpparts.senses && derpparts.director && P_DerpAI ();
}

void A_Look (mobj_t* actor);


void P_DirectorLevel (void)
{
    wave = 0;
    wavetic = 0;
    stancetic = 0;
    sent = 0;
    losses = 0;
    losstic = 0;
    pressing = false;
    tension = 0;
    quiet = 0;
    lasthealth = -1;
    seen = false;
    holdtic = -1;
    pressure = 0;
    logged = 0;
}

boolean P_DirectorAdmit (void)
{
    if (!Director ())
	return true;
    if (leveltime - wavetic >= WAVE_TIME)
    {
	wave = 0;
	wavetic = leveltime;
    }
    // The rounds coming in on him are the real measure, where there are
    // any: hard enough, fewer do; few, with a fight on, more do.
    if ((pressure >= (P_FireHeavy () && !pressing ? PRESSURE_MOST/2 : P_FireLight () ? PRESSURE_MOST*3/2 : PRESSURE_MOST)
	 || wave >= WAVE_MOST)
	&& leveltime % SURGE_EVERY < SURGE_EVERY - SURGE_TIME)
	return false;
    wave++;
    pressure++;
    return true;
}

boolean P_DirectorPressing (void)
{
    return !Director () || pressing;
}

boolean P_DirectorGoIn (void)
{
    return P_FightOn () && P_DirectorPressing ();
}

void P_DirectorStance (boolean press)
{
    pressing = press;
    sent = 0;
    stancetic = MAXINT;
}

static void Stance (const player_t* p)
{
    boolean	was = pressing;
    int		odds = STANCE_EVEN;

    if (p->health < STANCE_HURT)
	odds += STANCE_SWAY;
    if (pressure >= PRESSURE_MOST)
	odds -= STANCE_SWAY;
    if (losses >= LOSING_MOST && leveltime - losstic < 2*LOSING_TIME)
	odds -= 2*STANCE_SWAY;
    pressing = P_Random () < odds;
    stancetic = leveltime + STANCE_EVERY;
    if (pressing != was)
    {
	sent = 0;
	P_DerpLog (NULL, pressing ? "director: press him" : "director: hold back");
	// Said aloud, by whoever is up near him: he hears the fight turn.
	if (p->mo && (!pressing || p->health < STANCE_HURT))
	    P_DerpBarkNear (p->mo->x, p->mo->y, 1024, pressing ? BARK_GOALL : BARK_HOLDBACK);
    }
}

void P_DirectorFell (void)
{
    if (!Director ())
	return;
    if (leveltime - losstic > LOSING_TIME)
	losses = 0;
    losstic = leveltime;
    losses++;
    if (pressing && losses >= LOSING_MOST)
    {
	pressing = false;
	stancetic = leveltime + STANCE_EVERY;
	P_DerpLog (NULL, "director: hold back, he is picking them off");
    }
}

boolean P_DirectorBreach (void)
{
    if (!Director ())
	return true;
    if (leveltime - wavetic >= WAVE_TIME)
    {
	wave = 0;
	wavetic = leveltime;
    }
    if (wave >= WAVE_MOST && leveltime % SURGE_EVERY < SURGE_EVERY - SURGE_TIME)
	return false;
    wave++;
    pressure++;
    return true;
}

boolean P_DirectorCalls (void)
{
    if (!Director () || !P_DirectorGoIn () || sent >= WORD_SENDS || !P_DirectorBreach ())
	return false;
    sent++;
    return true;
}

int P_DirectorTension (void)
{
    return tension;
}

boolean P_DirectorHoldBack (void)
{
    if (!Director () || !players[consoleplayer].mo
	|| players[consoleplayer].health >= BADLY_HURT || seen)
    {
	holdtic = -1;
	return false;
    }
    if (holdtic < 0)
	holdtic = leveltime;
    // A while, not for good: then the hunt goes on.
    return leveltime - holdtic < HOLD_BACK;
}


// The last nudge put off its route searches: not done, to be tried again.
static boolean	nudgewaits;

void P_DirectorThink (void)
{
    player_t*	p = &players[consoleplayer];
    thinker_t*	th;
    int		chasers = 0, after = 0;

    if (!Director () || !p->mo)
	return;
    // The damage he takes: tension, at once.
    if (lasthealth >= 0 && p->health < lasthealth)
	tension += (lasthealth - p->health)*8;
    lasthealth = p->health;
    if (!P_PHASE (PHASE_DIRECTOR))
	return;
    // Monsters on his heels, seeing him.
    for (th = thinkercap.next; th != &thinkercap; th = th->next)
    {
	mobj_t*	mo = (mobj_t *)th;
	if (th->function.acp1 != (actionf_p1)P_MobjThinker || !(mo->flags & MF_COUNTKILL)
	    || mo->health <= 0 || mo->target != p->mo)
	    continue;
	// (Not one that has no way to him, shut in a trap or behind a door: it is no pressure.)
	if ((mo->mind == MIND_HUNT || mo->mind == MIND_COVER || mo->mind == MIND_INVESTIGATE)
	    && !(mo->wayx == mo->x && mo->wayy == mo->y) && !P_MindLurkWaits (mo))
	    after++;
	if (!P_DerpAsleep (mo) && P_CheckSight (mo, p->mo))
	    chasers++;
    }
    pressure = after;
    if (leveltime >= stancetic)
	Stance (p);
    seen = chasers > 0;
    tension += chasers*6 - 10;
    if (tension < 0)
	tension = 0;
    if (tension > 1000)
	tension = 1000;
    // The log hears it cross each hundred, with the stance it holds.
    if (tension/100 != logged/100)
    {
	logged = tension;
	P_DerpLog (NULL, "director: tension %d, %s", tension, pressing ? "pressing" : "holding back");
    }
    // A long quiet: somebody comes to look. Not while he hides badly hurt:
    // then he is let be.
    if (tension < CALM && !chasers && p->health >= BADLY_HURT)
	quiet += 8;
    else
	quiet = 0;
    // (Its route searches put off with the tic's routes spent: the quiet goes on,
    // and it nudges at its next turn.)
    if (quiet >= QUIET_NUDGE && !P_NavBudgetSpent (NULL))
    {
	nudgewaits = false;
	P_DirectorNudge ();
	if (!nudgewaits)
	    quiet = 0;
    }
}


// A spot in the player's room, near him but not at him: where to send one
// to look. Somewhere a monster could stand; NULL-free: false if none.
static boolean SpotInHisRoom (mobj_t* player, int nav, fixed_t* x, fixed_t* y)
{
    int		room = P_NavRoom (player->subsector->sector - sectors), k;

    for (k = 0; k < 32; k++)
    {
	fixed_t	px = player->x + ((P_Random () - 128)*3)*FRACUNIT;
	fixed_t	py = player->y + ((P_Random () - 128)*3)*FRACUNIT;
	int	cell = P_NavCellAt (nav, px, py);
	fixed_t	cx, cy, cf;
	if (cell < 0)
	    continue;
	P_NavPoint (nav, cell, &cx, &cy, &cf);
	if (P_NavRoom (R_PointInSubsector (cx, cy)->sector - sectors) != room)
	    continue;
	if (P_AproxDistance (cx - player->x, cy - player->y) < 128*FRACUNIT)
	    continue;	// not at him
	*x = cx;
	*y = cy;
	return true;
    }
    return false;
}

mobj_t* P_DirectorNudge (void)
{
    mobj_t*	player = players[consoleplayer].mo;
    thinker_t*	th;
    mobj_t*	best = NULL;
    static int	path[2];
    mobj_t*	cand[64];
    fixed_t	dist[64];
    int		n = 0, i, j;

    if (!Director () || !player || player->health <= 0)
	return NULL;
    // The idle ones 512-2048 units off, nearest first. Not the deaf (they
    // hold where the mapper put them), not the bosses, not one that can see
    // him (woken, it would know where he is without ever spotting him).
    for (th = thinkercap.next; th != &thinkercap && n < 64; th = th->next)
    {
	mobj_t*	mo = (mobj_t *)th;
	fixed_t	d;
	if (th->function.acp1 != (actionf_p1)P_MobjThinker || !(mo->flags & MF_COUNTKILL)
	    || mo->health <= 0 || !P_DerpAsleep (mo) || mo->guard
	    || mo->type == MT_CYBORG || mo->type == MT_SPIDER || P_NavClassOf (mo) < 0)
	    continue;
	d = P_AproxDistance (mo->x - player->x, mo->y - player->y);
	if (d < 512*FRACUNIT || d > 2048*FRACUNIT || P_CheckSight (mo, player))
	    continue;
	for (j = n++; j > 0 && dist[j - 1] > d; j--)
	{
	    cand[j] = cand[j - 1];
	    dist[j] = dist[j - 1];
	}
	cand[j] = mo;
	dist[j] = d;
    }
    // The first few with a way to him: a route search each, so few.
    for (i = 0; i < n && i < 6 && !best; i++)
    {
	int	way = P_NavPathFor (cand[i], P_NavClassOf (cand[i]), cand[i]->x, cand[i]->y, player->x, player->y,
				    path, 2, 8000);
	if (way == NAV_DEFERRED)
	{
	    nudgewaits = true;
	    return NULL;
	}
	if (way > 0)
	    best = cand[i];
    }
    if (!best)
	return NULL;
    {
	fixed_t	gx, gy;
	if (!SpotInHisRoom (player, P_NavClassOf (best), &gx, &gy))
	    return NULL;
	best->target = player;
	P_MindBecome (best, MIND_INVESTIGATE, STEP_NONE, "none.nudged",
		      "All quiet for too long: the director sends it to look round his room");
	best->goalx = gx;
	best->goaly = gy;
	best->goaltic = leveltime;
	P_SetMobjState (best, best->info->seestate);	// up without a word
	P_DerpLog (best, "nudged to look round the player's room");
    }
    return best;
}


int P_DirectorSteps (mobj_t* actor)
{
    mobj_t*	player = players[consoleplayer].mo;
    fixed_t	step;
    int		steps, tics;

    // Walkers only (a flier's height corrections would be done thrice).
    if (!Director () || !player
	|| (actor->mind != MIND_INVESTIGATE && actor->mind != MIND_RETURN
	    && actor->mind != MIND_AMBUSH && actor->mind != MIND_REGROUP
	    && actor->mind != MIND_HEAL && actor->mind != MIND_FLANK)
	|| P_NavClassOf (actor) < 0 || P_CheckSight (player, actor)
	|| P_AproxDistance (player->x - actor->x, player->y - actor->y) < OFFSTAGE_FAR*FRACUNIT)
	return 1;
    // As many as keep it slower than he runs, three at most.
    step = P_DerpStep (actor);
    tics = actor->state->tics > 0 ? actor->state->tics : 1;
    if (step <= 0)
	return 1;
    steps = RUN_SPEED*tics*FRACUNIT/step;
    return steps < 1 ? 1 : steps > 3 ? 3 : steps;
}
