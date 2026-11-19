//
// Two imps, two ways in (p_pincer.h).
//

#include <stdlib.h>

#include "doomdef.h"
#include "doomstat.h"
#include "p_local.h"
#include "p_barks.h"
#include "p_derplog.h"
#include "p_mobj.h"
#include "p_nav.h"
#include "p_navbudget.h"
#include "p_navlong.h"
#include "p_senses.h"
#include "p_mind.h"
#include "r_main.h"
#include "tables.h"
#include "p_pincer.h"
#include "p_tune.h"

// Worth it only from this far off him, and with a partner who set out this lately.
#define PINCER_FAR	TUNE (PINCER_FAR, 512)
#define PINCER_SINCE	TUNE (PINCER_SINCE, 5*TICRATE)
// Its way gives a wide berth to where its partner's comes in on him: the last stretch
// (PINCER_LAST cells out), this wide, then a little wider. The way there hardly
// matters: keeping clear of the middle of it, both still came in by the same door.
// And how many times as long that way may be.
#define PINCER_AVOID	TUNE (PINCER_AVOID, 160)
#define PINCER_WIDER	TUNE (PINCER_WIDER, 64)
#define PINCER_LONGER	TUNE (PINCER_LONGER, 3)
// The two ways must come in on him this far apart (degrees), seen from a stretch out.
#define PINCER_APART	TUNE (PINCER_APART, 60)
#define PINCER_LAST	TUNE (PINCER_LAST, 24)
// This many cells of its way from him the first one waits, out of his sight, for this
// long at most. Along its way, not in a line: the long way round starts right beside
// him, the other side of a wall.
#define PINCER_HOLD	TUNE (PINCER_HOLD, 40)
#define PINCER_WAIT	TUNE (PINCER_WAIT, 20*TICRATE)
#define ROUTE_MOST	1024
#define SEARCH_MOST	120000

static mobj_t* BySerial (int serial)
{
    thinker_t*	th;

    for (th = thinkercap.next; th != &thinkercap; th = th->next)
    {
	mobj_t*	mo = (mobj_t *)th;
	if (th->function.acp1 == (actionf_p1)P_MobjThinker && mo->serial == serial && mo->health > 0)
	    return mo;
    }
    return NULL;
}

// Another imp that has just set out after him, with nobody to go with.
static mobj_t* Partner (mobj_t* actor, mobj_t* him)
{
    thinker_t*	th;

    for (th = thinkercap.next; th != &thinkercap; th = th->next)
    {
	mobj_t*	mo = (mobj_t *)th;
	if (th->function.acp1 == (actionf_p1)P_MobjThinker && mo != actor && mo->type == actor->type
	    && mo->health > 0 && !mo->pincer && mo->target == him && P_MindStalks (mo)
	    && leveltime - mo->lurktic < PINCER_SINCE
	    && P_AproxDistance (him->x - mo->x, him->y - mo->y) >= PINCER_FAR*FRACUNIT)
	    return mo;
    }
    return NULL;
}

// mo's way to him, as one of owner's long searches (p_navlong.h): up to 120,000 cells,
// four of them as a pair set out and two each second after, so carried over tics;
// NAV_DEFERRED till it is done.
static int Way (mobj_t* owner, int tag, mobj_t* mo, mobj_t* him, int nav, int* path)
{
    int	n;

    P_DerpRouteBegin (mo);
    n = P_NavLongPath (owner, tag, nav, mo->x, mo->y, him->x, him->y, path, ROUTE_MOST, SEARCH_MOST);
    P_DerpRouteEnd ();
    return n;
}

// Where a way comes in on him from: its angle from him, a stretch short of the end.
static angle_t ComesIn (int nav, const int* path, int n, mobj_t* him)
{
    fixed_t	x, y;

    P_NavPoint (nav, path[n > PINCER_LAST ? n - PINCER_LAST : 0], &x, &y, NULL);
    return R_PointToAngle2 (him->x, him->y, x, y);
}

// Pairs being set up while their ways are searched (by the first imp's serial): the
// partner, which of the ways round it is on (0: the partner's own way first), and what
// the partner's way said. Asked each think while it stalks; past PAIR_WAITED unasked,
// forgotten (it stopped stalking).
#define PAIRINGS	8
#define PAIR_WAITED	(2*TICRATE)

typedef struct
{
    int		serial, mate, wide, n, tic;
    fixed_t	mx, my;
    angle_t	theirs;
} pairing_t;

static pairing_t	pairings[PAIRINGS];

static pairing_t* Pairing (const mobj_t* actor)
{
    int		i;

    for (i = 0; i < PAIRINGS; i++)
	if (pairings[i].serial && pairings[i].serial == actor->serial
	    && leveltime >= pairings[i].tic && leveltime - pairings[i].tic <= PAIR_WAITED)
	    return &pairings[i];
    return NULL;
}

// On with a pair's setting out as far as its searches have come.
static void PairOn (mobj_t* actor, mobj_t* him, pairing_t* p)
{
    static int	path[ROUTE_MOST];
    int		nav = P_NavClassOf (actor), m;
    mobj_t*	mate = BySerial (p->mate);

    p->tic = leveltime;
    // (The partner paired off with another meanwhile, or this one was taken as a
    // partner, or the partner's stalk is over: nothing to set out with.)
    if (nav < 0 || !mate || mate->pincer || actor->pincer || !P_MindStalks (mate))
    {
	p->serial = 0;
	return;
    }
    if (!p->wide)
    {
	p->n = Way (actor, NAVLONG_PAIRMATE, mate, him, nav, path);
	if (p->n == NAV_DEFERRED)
	    return;
	if (p->n <= PINCER_LAST)
	{
	    P_DerpLog (actor, "no way round with #%d: its way to him is %d cells", mate->serial, p->n);
	    p->serial = 0;
	    return;
	}
	p->theirs = ComesIn (nav, path, p->n, him);
	P_NavPoint (nav, path[p->n - PINCER_LAST], &p->mx, &p->my, NULL);
	p->wide = 1;
    }
    for (; p->wide <= 3; p->wide++)
    {
	int	off, wide = p->wide, n = p->n;
	navavoidx = p->mx;
	navavoidy = p->my;
	navavoidr = PINCER_AVOID + PINCER_WIDER*(wide - 1);
	navavoidhard = true;
	m = Way (actor, NAVLONG_PAIR, actor, him, nav, path);
	navavoidhard = false;
	navavoidr = 0;
	if (m == NAV_DEFERRED)
	    return;
	if (m <= 0 || m > n*PINCER_LONGER)
	{
	    P_DerpLog (actor, "no way round with #%d: %d cells against %d", mate->serial, m, n);
	    p->serial = 0;
	    return;
	}
	off = abs ((int)(ComesIn (nav, path, m, him) - p->theirs)/(int)(ANG45/45));
	if (off < PINCER_APART)
	{
	    P_DerpLog (actor, "no way round with #%d: only %d degrees apart", mate->serial, off);
	    continue;
	}
	p->serial = 0;
	actor->flankx = p->mx;
	actor->flanky = p->my;
	actor->flankr = PINCER_AVOID + PINCER_WIDER*(wide - 1);
	actor->pincer = mate->serial;
	mate->pincer = actor->serial;
	actor->flankwait = 0;
	mate->flankwait = 0;
	actor->flankleft = m;
	mate->flankleft = n;
	P_DerpRule (actor, "hunt/stalking.splits", "Another imp set out with it: it takes a way of its "
		    "own, to come at him from the other side");
	P_DerpLog (actor, "takes the other way: #%d comes from the front (%d degrees apart)", mate->serial, off);
	P_DerpCry (actor, BARK_IMPCALL);
	return;
    }
    p->serial = 0;
}

void P_PincerPair (mobj_t* actor, mobj_t* him)
{
    int		nav = P_NavClassOf (actor), i;
    mobj_t*	mate = Partner (actor, him);
    pairing_t*	p;

    if (nav < 0 || !mate)
	return;
    if (P_AproxDistance (him->x - actor->x, him->y - actor->y) < PINCER_FAR*FRACUNIT)
    {
	P_DerpLog (actor, "no way round with #%d: too near him", mate->serial);
	return;
    }
    // (A free place, else the oldest; one from a level before is the oldest.)
    if (!(p = Pairing (actor)))
	for (p = &pairings[0], i = 0; i < PAIRINGS; i++)
	{
	    if (!pairings[i].serial)
	    {
		p = &pairings[i];
		break;
	    }
	    if (pairings[i].tic < p->tic || pairings[i].tic > leveltime)
		p = &pairings[i];
	}
    p->serial = actor->serial;
    p->mate = mate->serial;
    p->wide = 0;
    PairOn (actor, him, p);
}

void P_PincerRoute (mobj_t* actor)
{
    if (actor->pincer && actor->flankr && actor->flankleft > PINCER_HOLD)
    {
	navavoidx = actor->flankx;
	navavoidy = actor->flanky;
	navavoidr = actor->flankr;
	navavoidhard = true;
    }
}

// How far it still has to go, by its own way, looked at once a second. And where its
// partner comes in now: he moves, and a way clear of where it came in when they set
// out was no way round at all (both dropped down the same ledges). The two searches
// carry over tics, one after the other (its own waits on where the partner's comes in):
// its own pending means the partner's is done. The second counts from when both are.
static void Measure (mobj_t* actor, mobj_t* mate, mobj_t* him)
{
    static int	path[ROUTE_MOST];
    int		n;
    boolean	own = P_NavLongPending (actor, NAVLONG_MEASURE);

    if (!own && leveltime - actor->flanktic < TICRATE)
	return;
    if (!own && actor->flankr)
    {
	n = Way (actor, NAVLONG_MEASUREMATE, mate, him, P_NavClassOf (mate), path);
	if (n == NAV_DEFERRED)
	    return;
	if (n > PINCER_LAST)
	    P_NavPoint (P_NavClassOf (mate), path[n - PINCER_LAST], &actor->flankx, &actor->flanky, NULL);
    }
    P_PincerRoute (actor);
    n = Way (actor, NAVLONG_MEASURE, actor, him, P_NavClassOf (actor), path);
    navavoidr = 0;
    navavoidhard = false;
    if (n == NAV_DEFERRED)
	return;
    actor->flanktic = leveltime;
    if (n > 0)
	actor->flankleft = n;
}

boolean P_PincerHold (mobj_t* actor, mobj_t* him, boolean visible)
{
    mobj_t*	mate;
    pairing_t*	p;

    if (!actor->pincer)
    {
	// Still setting out with one: on with that, as its searches come in.
	if ((p = Pairing (actor)) != NULL)
	    PairOn (actor, him, p);
	return false;
    }
    mate = BySerial (actor->pincer);
    // Dead, or its stalk is over: on its own from here.
    if (!mate || !P_MindStalks (mate))
    {
	actor->pincer = 0;
	return false;
    }
    Measure (actor, mate, him);
    if (actor->flankleft > PINCER_HOLD)
	return false;
    // Both close, or he has found one of them: the call, the answer, and in.
    if (visible || mate->flankleft <= PINCER_HOLD)
    {
	P_DerpRule (actor, "hunt/stalking.called", "Both close, or he found one of them: it calls, the other "
		    "answers, and they go in together");
	P_DerpCry (actor, BARK_IMPCLICKS);
	P_DerpCry (mate, BARK_IMPANSWER);
	P_DerpLog (actor, "calls to #%d: in, together", mate->serial);
	P_MindLurkOver (mate, "goes in");
	P_MindLurkOver (actor, "goes in");
	return false;
    }
    if (!actor->flankwait)
	actor->flankwait = leveltime;
    if (leveltime - actor->flankwait > PINCER_WAIT)
    {
	actor->pincer = 0;
	mate->pincer = 0;
	P_DerpLog (actor, "its partner never came: goes in alone");
	return false;
    }
    actor->movedir = 8;
    actor->angle = R_PointToAngle2 (actor->x, actor->y, him->x, him->y);
    return true;
}
