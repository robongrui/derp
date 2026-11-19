//
// Word among soldiers (p_word.h).
//

#include "doomdef.h"
#include "doomstat.h"
#include "m_random.h"
#include "p_local.h"
#include "p_barks.h"
#include "p_chatter.h"
#include "p_derp.h"
#include "p_derplog.h"
#include "p_director.h"
#include "p_mobj.h"
#include "p_senses.h"
#include "p_mind.h"
#include "p_word.h"
#include "p_tune.h"

// A voice carries this far, by the openings as any noise does, and is passed on this
// many times beyond that. (Not by squad: on E1M1 one squad is 19 soldiers across 3000
// units, and three a few steps up from another squad are not in it.)
#define WORD_NEAR	TUNE (WORD_NEAR, 768)
#define WORD_HOPS	TUNE (WORD_HOPS, 1)
// How long what it was told counts, and how long each voice takes to reach the next.
#define WORD_TIME	TUNE (WORD_TIME, 60*TICRATE)
#define WORD_BEAT	TUNE (WORD_BEAT, 20)
// News of the fight goes round again no sooner than this: it renews those waiting.
#define WORD_AGAIN	TUNE (WORD_AGAIN, 10*TICRATE)
// Up and busy when it came, and this long after: that word has gone by.
#define WORD_STALE	TUNE (WORD_STALE, 2*TICRATE)
// One who said it was going and never came back is missed this long after.
#define WORD_MISSED	TUNE (WORD_MISSED, 15*TICRATE)
#define WORD_MOST	64
#define NEWS_MOST	16

typedef struct { int serial; boolean fight; fixed_t x, y; } news_t;

static mobj_t*	reached[WORD_MOST];
static int	hops[WORD_MOST];
static news_t	news[NEWS_MOST];
static int	numnews, newstic;
static int	missedat;

static boolean Fresh (int tic)
{
    return tic && leveltime - tic < WORD_TIME;
}

static boolean Rang (mobj_t* mo)
{
    return mo->wordrang && leveltime - mo->wordrang < WORD_AGAIN;
}

static boolean Mate (mobj_t* mo, mobj_t* other)
{
    return other != mo && other->health > 0 && P_DerpSoldier (other->type)
	&& P_AproxDistance (other->x - mo->x, other->y - mo->y) <= WORD_NEAR*FRACUNIT
	&& P_NoiseDistance (mo->x, mo->y, other->x, other->y, WORD_NEAR) <= WORD_NEAR;
}

static boolean Listed (mobj_t* mo, int n)
{
    while (n--)
	if (reached[n] == mo)
	    return true;
    return false;
}

// Who a voice from first gets to, passed on, with how many voices it took (first is
// 0). If told, only those told who have not had this word yet.
static int Reach (mobj_t* first, boolean told)
{
    int		n = 1, i;
    thinker_t*	th;

    reached[0] = first;
    hops[0] = 0;
    for (i = 0; i < n; i++)
    {
	if (hops[i] > WORD_HOPS)
	    continue;
	for (th = thinkercap.next; th != &thinkercap; th = th->next)
	{
	    mobj_t*	mo = (mobj_t *)th;
	    if (th->function.acp1 != (actionf_p1)P_MobjThinker || n == WORD_MOST || Listed (mo, n)
		|| (told && (!Fresh (mo->wordtic) || Rang (mo))) || !Mate (reached[i], mo))
		continue;
	    reached[n] = mo;
	    hops[n++] = hops[i] + 1;
	}
    }
    return n;
}

static void Say (mobj_t* mo, boolean fight, fixed_t x, fixed_t y)
{
    if (numnews == NEWS_MOST)
	return;
    news[numnews].serial = mo->serial;
    news[numnews].fight = fight;
    news[numnews].x = x;
    news[numnews].y = y;
    numnews++;
    newstic = leveltime;
}

static void Ring (mobj_t* mo, fixed_t x, fixed_t y)
{
    if (Rang (mo))
	return;
    mo->wordrang = leveltime;
    mo->wordx = x;
    mo->wordy = y;
    Say (mo, true, x, y);
}

void P_WordOut (mobj_t* mo, boolean fight, fixed_t x, fixed_t y)
{
    if (!P_DerpSoldier (mo->type))
	return;
    Say (mo, false, 0, 0);
    // (The one who heard the gun and went was nobody's told: the room heard nothing
    // more, and he walked into it unready.)
    if (fight)
	Ring (mo, x, y);
}

void P_WordFight (mobj_t* mo, fixed_t x, fixed_t y)
{
    if (P_DerpSoldier (mo->type) && Fresh (mo->wordtic))
	Ring (mo, x, y);
}

static void TellOut (mobj_t* mo)
{
    int	n, i;

    n = Reach (mo, false);
    for (i = 1; i < n; i++)
	reached[i]->wordtic = leveltime;
    if (n > 1)
	mo->wordleft = leveltime;
    P_DerpBark (mo, n > 1 ? BARK_GOESLOOK : BARK_NOISE);
    P_DerpRuleIf (n > 1, mo, "none.tells_squad", "Off to look: those in earshot know it went");
    if (n > 1)
	P_DerpLog (mo, "tells %d of its own it is going", n - 1);
}

static void TellFight (mobj_t* mo, fixed_t x, fixed_t y)
{
    int	n, i;

    n = Reach (mo, true);
    // Whoever was talking stops: that is how he knows.
    P_ChatterStop ();
    if (n > 1)
	P_DerpBark (mo, BARK_WORDROUND);
    for (i = 1; i < n; i++)
    {
	mobj_t*	to = reached[i];
	to->wordrang = leveltime;
	to->wordgo = leveltime + hops[i]*WORD_BEAT + to->serial % WORD_BEAT;
	// (Told, not heard: a rough idea where.)
	to->wordx = x + (P_Random () - 128)*FRACUNIT;
	to->wordy = y + (P_Random () - 128)*FRACUNIT;
    }
    if (n > 1)
	P_DerpLog (mo, "passes the word: a fight, %d of its own told", n - 1);
}

void P_WordGone (mobj_t* mo)
{
    if (P_DerpSoldier (mo->type) && Fresh (mo->wordleft))
	missedat = leveltime + WORD_MISSED;
}

// One told it was going, near him, still waiting for it.
static void Missed (void)
{
    mobj_t*	him = players[consoleplayer].mo;
    mobj_t*	best = NULL;
    fixed_t	bestd = 1024*FRACUNIT;
    thinker_t*	th;

    if (!him)
	return;
    for (th = thinkercap.next; th != &thinkercap; th = th->next)
    {
	mobj_t*	mo = (mobj_t *)th;
	fixed_t	d;
	if (th->function.acp1 != (actionf_p1)P_MobjThinker || mo->health <= 0 || !P_DerpSoldier (mo->type)
	    || !Fresh (mo->wordtic) || (mo->mind != MIND_NONE && mo->mind != MIND_AMBUSH))
	    continue;
	d = P_AproxDistance (mo->x - him->x, mo->y - him->y);
	if (d < bestd)
	{
	    bestd = d;
	    best = mo;
	}
    }
    if (best)
	P_DerpBark (best, BARK_FRIENDGONE);
}

void P_WordThink (void)
{
    int	i;

    // (Left over from the last level, past its time.)
    if (missedat - leveltime > WORD_MISSED)
	missedat = 0;
    if (missedat && leveltime >= missedat)
    {
	missedat = 0;
	Missed ();
    }
    // (Left over from the last level.)
    if (leveltime - newstic > 1)
	numnews = 0;
    for (i = 0; i < numnews; i++)
    {
	thinker_t*	th;
	for (th = thinkercap.next; th != &thinkercap; th = th->next)
	{
	    mobj_t*	mo = (mobj_t *)th;
	    if (th->function.acp1 != (actionf_p1)P_MobjThinker || mo->serial != news[i].serial || mo->health <= 0)
		continue;
	    if (news[i].fight)
		TellFight (mo, news[i].x, news[i].y);
	    else
		TellOut (mo);
	    break;
	}
    }
    numnews = 0;
}

boolean P_WordReaches (mobj_t* actor)
{
    mobj_t*	player = players[consoleplayer].mo;
    boolean	stale = leveltime - actor->wordgo > WORD_STALE;

    if (!actor->wordgo || leveltime < actor->wordgo)
	return false;
    actor->wordgo = 0;
    if (stale || !player)
	return false;
    P_DerpBark (actor, BARK_WORDANSWER);
    if (!actor->guard && P_DirectorCalls ())
    {
	P_MindBecome (actor, MIND_INVESTIGATE, STEP_NONE, "none.word_goes",
		      "Word of a fight, and the director says go: it goes to look");
	P_DerpLog (actor, "word of a fight: goes in");
	actor->target = player;
	actor->goalx = actor->wordx;
	actor->goaly = actor->wordy;
	actor->goaltic = leveltime;
	P_SetMobjState (actor, actor->info->seestate);
	return true;
    }
    P_DerpRule (actor, "none.word_ready", "Word of a fight from its own: it turns that way, ready");
    P_DerpLog (actor, "word of a fight: gets ready");
    P_MindReady (actor, actor->wordx, actor->wordy);
    return false;
}

boolean P_WordHolds (mobj_t* actor)
{
    return Fresh (actor->wordrang);
}

boolean P_WordCalled (mobj_t* actor)
{
    return !actor->guard && Fresh (actor->wordrang) && P_DirectorCalls ();
}
