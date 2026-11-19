//
// Idle chatter (p_chatter.h).
//

#include <string.h>

#include "doomdef.h"
#include "doomstat.h"
#include "m_random.h"
#include "p_local.h"
#include "p_barks.h"
#include "p_chatter.h"
#include "p_derp.h"
#include "p_director.h"
#include "p_fire.h"
#include "p_senses.h"
#include "p_tune.h"

// Only this near him: past it he could not hear them. The first soon after he arrives, as
// the first shots end it for the rooms round about (in a run of 200 seconds nobody near him
// was at rest after the first six). A gap between one and the next, and a bit more by chance.
// A friend this near to talk to, and the odds in 256 that they do.
#define CHAT_NEAR	TUNE (CHAT_NEAR, 1400)
#define CHAT_FIRST	TUNE (CHAT_FIRST, 3*TICRATE)
#define CHAT_GAP	TUNE (CHAT_GAP, 15*TICRATE)
#define CHAT_SPREAD	TUNE (CHAT_SPREAD, 15*TICRATE)
#define CHAT_FRIEND	TUNE (CHAT_FRIEND, 512)
#define CHAT_TALK	TUNE (CHAT_TALK, 112)
#define TALKS		10
#define TALK_PARTS	3
#define SAYINGS		5
#define MOST_LINES	11

static const bark_t	sayings[SAYINGS] = { BARK_CHATREMEMBER, BARK_CHATINSIDE, BARK_CHATBODY, BARK_CHATHIM, BARK_CHATJOB };

// No line twice on a map.
static boolean	saidline[SAYINGS + 1][MOST_LINES];
static boolean	talked[TALKS];
static int	nexttic, count;
// A talk going on: its two, by number, which one, and the part it is at (0: none).
static int	first, second, talk, part;
// One line, said by this one.
static int	alone;

void P_ChatterLevel (void)
{
    memset (saidline, 0, sizeof saidline);
    memset (talked, 0, sizeof talked);
    nexttic = CHAT_FIRST;
    count = 0;
    first = second = alone = 0;
    talk = part = 0;
}

int P_ChatterSaid (void)
{
    return count;
}

static mobj_t* Serial (int serial)
{
    thinker_t*	th;

    if (!serial)
	return NULL;
    for (th = thinkercap.next; th != &thinkercap; th = th->next)
    {
	mobj_t*	mo = (mobj_t *)th;
	if (th->function.acp1 == (actionf_p1)P_MobjThinker && mo->serial == serial)
	    return mo;
    }
    return NULL;
}

// At rest, and he is not looking at it.
static boolean AtEase (mobj_t* mo, mobj_t* him)
{
    return mo && mo->health > 0 && P_DerpSoldier (mo->type) && mo->mind == MIND_NONE && !mo->tactic
	&& !P_CheckSight (mo, him);
}

static void Finish (void)
{
    first = second = alone = 0;
    talk = part = 0;
    nexttic = leveltime + CHAT_GAP + P_Random ()*CHAT_SPREAD/256;
}

void P_ChatterStop (void)
{
    mobj_t*	mo;

    if ((mo = Serial (first)) != NULL)
	P_DerpCutOff (mo);
    if ((mo = Serial (second)) != NULL)
	P_DerpCutOff (mo);
    if ((mo = Serial (alone)) != NULL)
	P_DerpCutOff (mo);
    if (first || alone)
	Finish ();
}

// A line of its own it has not said on this map. (The chaingunner has lines nobody else has.)
static boolean Saying (mobj_t* mo)
{
    int		at = P_Random ()%SAYINGS, i, k;
    int		clown = mo->type == MT_CHAINGUY;

    for (i = 0; i < (clown ? 1 : SAYINGS); i++)
    {
	int	set = clown ? SAYINGS : (at + i)%SAYINGS;
	bark_t	moment = clown ? BARK_CHATCLOWN : sayings[set];
	int	takes = P_DerpTakes (mo, moment);
	for (k = 1; k <= takes && k < MOST_LINES; k++)
	{
	    int	take = 1 + (k + at)%takes;
	    if (!saidline[set][take] && P_DerpSayTake (mo, moment, take))
	    {
		saidline[set][take] = true;
		alone = mo->serial;
		count++;
		return true;
	    }
	}
    }
    return false;
}

// Two voices, one of each: every talk is written for a zombie and a shotgun guy.
static boolean Starts (mobj_t* a, mobj_t* b)
{
    int	at = P_Random ()%TALKS, i;

    for (i = 0; i < TALKS; i++)
    {
	int	k = (at + i)%TALKS;
	if (talked[k])
	    continue;
	if (P_DerpSayTake (a, BARK_TALK01 + k, 1) || P_DerpSayTake (b, BARK_TALK01 + k, 1))
	{
	    talked[k] = true;
	    first = a->serial;
	    second = b->serial;
	    talk = k;
	    part = 2;
	    count++;
	    return true;
	}
    }
    return false;
}

static void Goes (mobj_t* him)
{
    thinker_t*	th;
    mobj_t*	best = NULL, *friend = NULL;
    fixed_t	bestd = CHAT_NEAR*FRACUNIT, friendd = CHAT_FRIEND*FRACUNIT;

    for (th = thinkercap.next; th != &thinkercap; th = th->next)
    {
	mobj_t*	mo = (mobj_t *)th;
	fixed_t	d;
	if (th->function.acp1 != (actionf_p1)P_MobjThinker || !P_DerpSoldier (mo->type))
	    continue;
	d = P_AproxDistance (mo->x - him->x, mo->y - him->y);
	if (d < bestd && AtEase (mo, him))
	{
	    bestd = d;
	    best = mo;
	}
    }
    if (!best || P_DerpSpeaking (best))
	return;
    for (th = thinkercap.next; th != &thinkercap; th = th->next)
    {
	mobj_t*	mo = (mobj_t *)th;
	fixed_t	d;
	if (th->function.acp1 != (actionf_p1)P_MobjThinker || mo == best || !P_DerpSoldier (mo->type)
	    || mo->type == best->type || mo->type == MT_CHAINGUY || best->type == MT_CHAINGUY)
	    continue;
	d = P_AproxDistance (mo->x - best->x, mo->y - best->y);
	if (d < friendd && AtEase (mo, him))
	{
	    friendd = d;
	    friend = mo;
	}
    }
    if (friend && P_Random () < CHAT_TALK && Starts (best, friend))
	return;
    if (!Saying (best))
	nexttic = leveltime + CHAT_GAP;
}

void P_ChatterThink (void)
{
    mobj_t*	him = players[consoleplayer].mo;

    if (!P_DerpAI () || !him || him->health <= 0)
	return;
    // Going on: it stops as soon as they have something better to do.
    if (first || alone)
    {
	mobj_t*	a = Serial (first ? first : alone);
	mobj_t*	b = Serial (second);
	if (P_FightOn () || !AtEase (a, him) || (first && !AtEase (b, him)))
	{
	    P_ChatterStop ();
	    return;
	}
	if (P_DerpSpeaking (a))
	    return;
	if (alone)
	    Finish ();
	else if (part > TALK_PARTS || (!P_DerpSayTake (a, BARK_TALK01 + talk, part) && !P_DerpSayTake (b, BARK_TALK01 + talk, part)))
	    Finish ();
	else
	    part++;
	return;
    }
    if (leveltime < nexttic || (leveltime & 15) || P_FightOn ())
	return;
    Goes (him);
}
