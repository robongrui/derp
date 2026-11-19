//
// What DERP's soldiers shout (p_barks.h).
//

#include <string.h>
#include <stdio.h>

#include "doomdef.h"
#include "doomstat.h"
#include "i_sound.h"
#include "m_random.h"
#include "p_local.h"
#include "p_barks.h"
#include "p_derp.h"
#include "p_derplog.h"
#include "p_mind.h"
#include "r_state.h"
#include "w_wad.h"
#include "z_zone.h"
#include "p_tune.h"

#define VOICES		4
#define VOICE_ZOMBIE	0
#define VOICE_SHOTGUN	1
#define VOICE_CHAINGUN	2
#define VOICE_IMP	3
#define MOST_TAKES	10
// Silence between one line and the next.
#define GAP		10
// How near a friend must be to answer, and how soon it does.
#define ANSWER_NEAR	640
#define ANSWER_AFTER	4
// How long a round going by takes to register: long enough that the next one,
// which may kill it, gets there first. A dead man says nothing.
#define REACTION	10
// How long before the same moment may be said again, by anyone.
#define AGAIN_SOON	(5*TICRATE)
#define AGAIN		(8*TICRATE)
#define AGAIN_LATE	(15*TICRATE)

static const struct
{
    const char*	name;
    int		again;
    int		loud;		// percent of a full shout
    bark_t	answer;		// NUMBARKS: none
} said[NUMBARKS] =
{
    { "contact",	AGAIN_SOON,	100,	NUMBARKS },
    { "noise",		AGAIN_LATE,	60,	NUMBARKS },
    { "nothing",	AGAIN_LATE,	60,	NUMBARKS },
    { "lostyou",	AGAIN,		80,	NUMBARKS },
    { "takingfire",	AGAIN_SOON,	100,	NUMBARKS },
    { "cover",		AGAIN_SOON,	100,	NUMBARKS },
    { "flanked",	AGAIN,		100,	NUMBARKS },
    { "bounding",	AGAIN_SOON,	100,	BARK_COVERINGBOUND },
    { "coveringbound",	AGAIN_SOON,	100,	NUMBARKS },
    { "suppressing",	AGAIN,		100,	NUMBARKS },
    { "pinned",		AGAIN_LATE,	100,	NUMBARKS },
    { "friendinline",	AGAIN,		100,	NUMBARKS },
    { "frienddies",	AGAIN_SOON,	100,	NUMBARKS },
    { "trap",		AGAIN_LATE,	40,	NUMBARKS },
    { "door",		AGAIN,		100,	BARK_WATCHDOOR },
    { "watchdoor",	AGAIN,		60,	NUMBARKS },
    { "reserve",	AGAIN_LATE,	50,	NUMBARKS },
    { "key",		AGAIN,		100,	NUMBARKS },
    { "fallback",	AGAIN,		100,	NUMBARKS },
    { "regroup",	AGAIN,		100,	NUMBARKS },
    { "lastalive",	AGAIN_LATE,	80,	NUMBARKS },
    { "pain",		0,		100,	NUMBARKS },
    { "idle",		0,		45,	NUMBARKS },
    { "gibbed",		0,		100,	NUMBARKS },
    { "death",		0,		100,	NUMBARKS },
    { "push",	AGAIN_SOON,	100,	BARK_COVERINGBOUND },
    { "pushhurt",	AGAIN_SOON,	100,	BARK_COVERINGBOUND },
    { "pushaway",	AGAIN_SOON,	100,	BARK_COVERINGBOUND },
    { "goround",	AGAIN,	100,	NUMBARKS },
    { "tooclose",	AGAIN,	100,	NUMBARKS },
    { "bait",	AGAIN,	100,	NUMBARKS },
    { "baitturn",	AGAIN_SOON,	100,	NUMBARKS },
    { "friendhit",	AGAIN,	100,	NUMBARKS },
    { "demonhit",	AGAIN,	100,	BARK_GETDEMON },
    { "getdemon",	AGAIN_SOON,	100,	NUMBARKS },
    { "pinkycharge",	AGAIN_SOON,	100,	NUMBARKS },
    { "pinkypound",	AGAIN_LATE,	70,	NUMBARKS },
    { "window",	AGAIN_SOON,	100,	NUMBARKS },
    { "tookitem",	AGAIN,	100,	NUMBARKS },
    { "trapsprung",	AGAIN_SOON,	100,	NUMBARKS },
    { "running",	AGAIN,	100,	NUMBARKS },
    { "headshot",	AGAIN,	100,	NUMBARKS },
    { "friendgibbed",	AGAIN_SOON,	100,	NUMBARKS },
    { "inslime",	AGAIN_LATE,	100,	NUMBARKS },
    { "noslime",	AGAIN_LATE,	80,	NUMBARKS },
    { "flashlight",	AGAIN_LATE,	100,	NUMBARKS },
    { "barrels",	AGAIN_LATE,	100,	NUMBARKS },
    { "fires",	AGAIN,	90,	NUMBARKS },
    { "lane",	AGAIN_LATE,	70,	NUMBARKS },
    { "waiting",	AGAIN_LATE,	70,	NUMBARKS },
    { "stillalive",	AGAIN,	80,	NUMBARKS },
    { "shrug",	AGAIN,	80,	NUMBARKS },
    { "jokemath",	0,	100,	NUMBARKS },
    { "jokefree",	0,	100,	NUMBARKS },
    { "jokepressure",	0,	100,	NUMBARKS },
    { "jokecantsee",	0,	100,	NUMBARKS },
    { "jokefeature",	0,	100,	NUMBARKS },
    { "jokereallife",	0,	100,	NUMBARKS },
    { "jokewanted",	0,	100,	NUMBARKS },
    { "jokelovegore",	0,	100,	NUMBARKS },
    { "jokeunnecessary",	0,	100,	NUMBARKS },
    { "jokesplash",	0,	100,	NUMBARKS },
    { "jokerightkeys",	0,	100,	NUMBARKS },
    { "jokewhatisit",	0,	100,	NUMBARKS },
    { "jokedonttalk",	0,	100,	NUMBARKS },
    { "joketwice",	0,	100,	NUMBARKS },
    { "impsees",	2*TICRATE,	100,	BARK_IMPANSWER },
    { "impanswer",	0,	90,	NUMBARKS },
    { "impclicks",	3*TICRATE,	45,	NUMBARKS },
    { "impwindup",	0,	90,	NUMBARKS },
    { "imppounce",	0,	100,	NUMBARKS },
    { "impswipe",	TICRATE,	80,	NUMBARKS },
    { "impthrow",	2*TICRATE,	70,	NUMBARKS },
    { "imppainsmall",	0,	90,	NUMBARKS },
    { "imppainbig",	0,	100,	NUMBARKS },
    { "impdeath",	0,	100,	NUMBARKS },
    { "impgibbed",	0,	100,	NUMBARKS },
    { "impheard",	5*TICRATE,	50,	NUMBARKS },
    { "impsniff",	AGAIN,	45,	NUMBARKS },
    { "imppackdeath",	AGAIN_SOON,	100,	NUMBARKS },
    { "impcornered",	AGAIN_SOON,	100,	NUMBARKS },
    { "impland",	0,	60,	NUMBARKS },
    { "impwindow",	0,	100,	NUMBARKS },
    { "impchitter",	0,	60,	BARK_IMPCHITTER },
    { "flankdoor",	AGAIN,	100,	NUMBARKS },
    { "flankother",	AGAIN,	100,	NUMBARKS },
    { "hearsrun",	AGAIN_LATE,	100,	NUMBARKS },
    { "bulletnear",	AGAIN,	100,	NUMBARKS },
    { "goeslook",	AGAIN,	100,	NUMBARKS },
    { "wordround",	AGAIN_SOON,	100,	NUMBARKS },
    { "wordanswer",	AGAIN_SOON,	90,	NUMBARKS },
    { "standsready",	AGAIN,	100,	NUMBARKS },
    { "standsdown",	AGAIN_LATE,	90,	NUMBARKS },
    { "hiddendoor",	AGAIN_LATE,	90,	NUMBARKS },
    { "guardsitem",	AGAIN_LATE,	90,	NUMBARKS },
    { "friendgone",	AGAIN_LATE,	100,	NUMBARKS },
    { "holdback",	AGAIN_LATE,	100,	NUMBARKS },
    { "goall",	AGAIN_LATE,	100,	NUMBARKS },
    { "impsquiet",	AGAIN_LATE,	100,	NUMBARKS },
    { "impover",	AGAIN_SOON,	100,	NUMBARKS },
    { "chatremember",	0,	75,	NUMBARKS },
    { "chatinside",	0,	75,	NUMBARKS },
    { "chatbody",	0,	75,	NUMBARKS },
    { "chathim",	0,	75,	NUMBARKS },
    { "chatjob",	0,	75,	NUMBARKS },
    { "chatclown",	0,	85,	NUMBARKS },
    { "talk01",	0,	75,	NUMBARKS },
    { "talk02",	0,	75,	NUMBARKS },
    { "talk03",	0,	75,	NUMBARKS },
    { "talk04",	0,	75,	NUMBARKS },
    { "talk05",	0,	75,	NUMBARKS },
    { "talk06",	0,	75,	NUMBARKS },
    { "talk07",	0,	75,	NUMBARKS },
    { "talk08",	0,	75,	NUMBARKS },
    { "talk09",	0,	75,	NUMBARKS },
    { "talk10",	0,	75,	NUMBARKS },
    { "implurk",	5*TICRATE,	45,	NUMBARKS },
    { "impscrape",	6*TICRATE,	55,	NUMBARKS },
    { "impcall",	5*TICRATE,	100,	BARK_IMPCALLBACK },
    { "impcallback",	0,	90,	NUMBARKS },
    { "impcrouch",	0,	90,	NUMBARKS },
    { "impair",	0,	100,	NUMBARKS },
    { "impoverbody",	6*TICRATE,	55,	NUMBARKS },
};

// The chaingunner's jokes: now and then in place of a line, each once a level.
#define JOKE_ODDS	32	// of 256
static const struct
{
    bark_t	on;
    bark_t	joke;
} jokes[] =
{
    { BARK_FIRES, BARK_JOKEMATH },		{ BARK_FIRES, BARK_JOKEFREE },
    { BARK_SUPPRESSING, BARK_JOKEPRESSURE },	{ BARK_PINNED, BARK_JOKEPRESSURE },
    { BARK_LOSTYOU, BARK_JOKECANTSEE },		{ BARK_FRIENDHIT, BARK_JOKEFEATURE },
    { BARK_CONTACT, BARK_JOKEREALLIFE },	{ BARK_CONTACT, BARK_JOKEWANTED },
    { BARK_FRIENDGIBBED, BARK_JOKELOVEGORE },	{ BARK_FRIENDGIBBED, BARK_JOKEUNNECESSARY },
    { BARK_FRIENDGIBBED, BARK_JOKEDONTTALK },	{ BARK_INSLIME, BARK_JOKESPLASH },
    { BARK_KEY, BARK_JOKERIGHTKEYS },		{ BARK_PINKYPOUND, BARK_JOKEWHATISIT },
    { BARK_STILLALIVE, BARK_JOKETWICE },
};
#define NUMJOKES	((int)(sizeof jokes/sizeof *jokes))
static boolean	joked[NUMBARKS];

// An imp's call answered by another: who, and when.
static bark_t	impanswer = NUMBARKS;
static int	impanswertic, impcaller;
static fixed_t	impanswerx, impanswery;
// How near another imp hears one go down or answers it.
#define PACK_HEARS	768
// What a soldier takes note of about him, this near, once a second.
#define WATCH_NEAR	1024
#define BARREL_NEAR	128

static int	takes[VOICES][NUMBARKS];
static int	lasttake[VOICES][NUMBARKS];
static int	saidtic[NUMBARKS];
static fixed_t	saidx[NUMBARKS], saidy[NUMBARKS];
// Who is talking, and until when. One voice at a time, and the same line not again soon,
// within this of it: a soldier shouting across the map, whom nobody here can hear, kept
// the one beside him quiet.
#define BARK_APART	TUNE (BARK_APART, 1024)
#define TALKERS		8
static struct { fixed_t x, y; int serial, until; } talkers[TALKERS];
// "He got Harris!" is friend-dies take 2 in every voice: once a level, a reload or the
// next map and he can die again.
#define HARRIS_TAKE	2
static boolean	harris;
static int	answertic;
static bark_t	answer = NUMBARKS;
static fixed_t	answerx, answery;
static int	speaker;	// its serial: it does not answer itself
// A line it will say in a moment, if it is still alive to (P_DerpBarkSoon).
static bark_t	soon = NUMBARKS;
static int	soonserial, soontic;

// Five sets of every sound (scripts/deepen_barks.py), a letter on the lump's name
// for all but the first, and a little pitch of its own: each soldier keeps one of
// each, by its number, so a squad is several men. Picked per shout it was one man
// losing his mind. Percent, the rate it is played at.
#define SETS		5
#define PITCH_LEAST	94
#define PITCH_SPREAD	13

static void LumpName (char* name, int voice, int moment, int take)
{
    // Past 99 a letter for the hundreds and tens: two places either way.
    if (moment < 100)
	snprintf (name, 9, "BK%c%02d%d", 'A' + voice, moment, take);
    else
	snprintf (name, 9, "BK%c%c%d%d", 'A' + voice, 'A' + (moment - 100)/10, moment%10, take);
}

// Its own set's take, the first set's if that one is not there.
static void ItsLump (char* name, const mobj_t* mo, int voice, int moment, int take)
{
    int		set = (int)((unsigned)mo->serial*37 % SETS);
    char	own[9];

    LumpName (name, voice, moment, take);
    if (!set)
	return;
    snprintf (own, 9, "%s%c", name, 'A' + set - 1);
    if (W_CheckNumForName (own) >= 0)
	memcpy (name, own, 9);
}

// F4 Sound's Monster voices (P_SetMonsterVoices): off, DERP's monsters make id's sounds.
static boolean voices = true;

void P_SetMonsterVoices (boolean on)
{
    voices = on;
}

static boolean Voiced (void)
{
    return voices && P_DerpAI () && derpparts.senses;
}

boolean P_DerpVoices (void)
{
    return Voiced ();
}

static int Pitch (const mobj_t* mo)
{
    // A pack is several animals: wider than a squad of men.
    if (mo->type == MT_TROOP)
	return 88 + (int)((unsigned)mo->serial*29 % 31);
    return PITCH_LEAST + (int)((unsigned)mo->serial*29 % PITCH_SPREAD);
}

void P_BarksLevel (void)
{
    int		v, m, t;
    char	name[9];

    memset (talkers, 0, sizeof talkers);
    harris = false;
    answer = NUMBARKS;
    soon = NUMBARKS;
    impanswer = NUMBARKS;
    memset (joked, 0, sizeof joked);
    for (v = 0; v < VOICES; v++)
	for (m = 0; m < NUMBARKS; m++)
	{
	    saidtic[m] = -AGAIN_LATE;
	    lasttake[v][m] = 0;
	    takes[v][m] = 0;
	    for (t = 1; t <= MOST_TAKES; t++)
	    {
		LumpName (name, v, m, t);
		if (W_CheckNumForName (name) < 0)
		    break;
		takes[v][m] = t;
	    }
	}
}

// How many tics a DMX sound lump lasts.
static int Length (const char* name)
{
    int			lump = W_CheckNumForName ((char *)name);
    const byte*		data;

    if (lump < 0 || W_LumpLength (lump) < 8)
	return 0;
    data = W_CacheLumpNum (lump, PU_STATIC);
    return (int)(((long long)data[4] | data[5]<<8 | data[6]<<16 | (long long)data[7]<<24)*TICRATE
		 /(data[2] | data[3]<<8));
}

// Its voice for that moment: a chaingunner has his own lines, and the shotgun
// guys' for the rest.
static int Voice (const mobj_t* mo, bark_t moment)
{
    switch (mo->type)
    {
      case MT_SHOTGUY:
	return VOICE_SHOTGUN;
      case MT_CHAINGUY:
	return takes[VOICE_CHAINGUN][moment] ? VOICE_CHAINGUN : VOICE_SHOTGUN;
      case MT_TROOP:
	return VOICE_IMP;
      default:
	return VOICE_ZOMBIE;
    }
}

// A joke in place of that line, now and then: not in the authored game, where
// it would break the mood.
static bark_t Joke (const mobj_t* actor, bark_t moment)
{
    int		i, at;

    if (actor->type != MT_CHAINGUY || derpauthored || P_Random () >= JOKE_ODDS)
	return moment;
    at = P_Random ()%NUMJOKES;
    for (i = 0; i < NUMJOKES; i++)
    {
	int	k = (at + i)%NUMJOKES;
	if (jokes[k].on == moment && !joked[jokes[k].joke] && takes[VOICE_CHAINGUN][jokes[k].joke])
	{
	    joked[jokes[k].joke] = true;
	    return jokes[k].joke;
	}
    }
    return moment;
}

// A take of the moment, not the last one: 0, none.
static int Take (int voice, bark_t moment)
{
    int	count = takes[voice][moment], take;

    if (!count)
	return 0;
    take = 1 + P_Random ()%count;
    if (take == lasttake[voice][moment])
	take = take%count + 1;
    lasttake[voice][moment] = take;
    return take;
}

static boolean Speaks (const mobj_t* mo)
{
    return mo && !mo->player && mo->health > 0 && P_DerpSoldier (mo->type);
}

static boolean Busy (const mobj_t* mo)
{
    int	i;

    for (i = 0; i < TALKERS; i++)
	if (leveltime < talkers[i].until
	    && P_AproxDistance (talkers[i].x - mo->x, talkers[i].y - mo->y) <= BARK_APART*FRACUNIT)
	    return true;
    return false;
}

static boolean TooSoon (const mobj_t* mo, bark_t moment)
{
    return leveltime - saidtic[moment] < said[moment].again
	&& P_AproxDistance (saidx[moment] - mo->x, saidy[moment] - mo->y) <= BARK_APART*FRACUNIT;
}

// It talks until then: in the place of whoever finished longest ago.
static void Talks (const mobj_t* mo, int until)
{
    int	i, at = 0;

    for (i = 1; i < TALKERS; i++)
	if (talkers[i].until < talkers[at].until)
	    at = i;
    talkers[at].x = mo->x;
    talkers[at].y = mo->y;
    talkers[at].serial = mo->serial;
    talkers[at].until = until;
}

// Out loud, and kept: dying stops it (P_DerpCutOff).
static void Voice_ (mobj_t* actor, const char* name, bark_t line)
{
    // What he is meant to make out, he makes out, through a door too.
    if (line >= BARK_FLANKDOOR && line <= BARK_TALK10)
	I_BarkClear ();
    I_BarkLump (name, actor->x, actor->y, actor->z + actor->height*3/4, said[line].loud, Pitch (actor));
    actor->barkvoice = I_BarkVoice ();
    actor->barkuntil = leveltime + Length (name);
}

void P_DerpCutOff (mobj_t* mo)
{
    int	i;

    if (leveltime >= mo->barkuntil)
	return;
    I_StopSound (mo->barkvoice);
    mo->barkuntil = 0;
    for (i = 0; i < TALKERS; i++)
	if (talkers[i].serial == mo->serial)
	    talkers[i].until = 0;
    P_DerpLog (mo, "cut off");
    // Nobody answers what it never finished.
    if (answer != NUMBARKS && speaker == mo->serial)
	answer = NUMBARKS;
}

boolean P_DerpBark (mobj_t* actor, bark_t moment)
{
    char	name[9];
    int		voice, take, tics;
    bark_t	line;

    if (!Voiced () || !Speaks (actor) || Busy (actor) || TooSoon (actor, moment))
	return false;
    line = Joke (actor, moment);
    voice = Voice (actor, line);
    take = Take (voice, line);
    if (!take)
	return false;
    // There is only one Harris, and he can only die once a go.
    if (line == BARK_FRIENDDIES && take == HARRIS_TAKE)
    {
	if (harris)
	    take = 1;
	else
	    P_DerpLog (actor, "says Harris");
	harris = true;
    }
    ItsLump (name, actor, voice, line, take);
    // How long it lasts by the lump, not by whether it could be played: with
    // the sound off the timing, and the log, are the same.
    tics = Length (name);
    if (!tics)
	return false;
    Voice_ (actor, name, line);
    saidtic[moment] = leveltime;
    saidx[moment] = actor->x;
    saidy[moment] = actor->y;
    Talks (actor, leveltime + tics + GAP);
    P_DerpLog (actor, "shouts: %s", said[line].name);
    if (said[moment].answer != NUMBARKS)
    {
	answer = said[moment].answer;
	answertic = leveltime + tics + ANSWER_AFTER;
	answerx = actor->x;
	answery = actor->y;
	speaker = actor->serial;
	Talks (actor, answertic);
    }
    return true;
}

boolean P_DerpHushed (mobj_t* actor)
{
    return Voiced () && (P_DerpSoldier (actor->type) || actor->type == MT_TROOP);
}

int P_DerpTakes (const mobj_t* actor, bark_t moment)
{
    return takes[Voice (actor, moment)][moment];
}

boolean P_DerpSpeaking (const mobj_t* mo)
{
    return Busy (mo);
}

boolean P_DerpSayTake (mobj_t* actor, bark_t moment, int take)
{
    char	name[9];
    int		voice = Voice (actor, moment);

    if (!Voiced () || !Speaks (actor) || Busy (actor))
	return false;
    LumpName (name, voice, moment, take);
    if (W_CheckNumForName (name) < 0)
	return false;
    ItsLump (name, actor, voice, moment, take);
    Voice_ (actor, name, moment);
    Talks (actor, leveltime + Length (name) + GAP);
    P_DerpLog (actor, "says: %s %d", said[moment].name, take);
    return true;
}

// Its cry, whether or not one is due. answered: an answer gets none itself.
static boolean Cry (mobj_t* actor, bark_t moment, boolean answered)
{
    char	name[9];
    int		take, tics;

    if (!Voiced () || !actor || actor->player || actor->type != MT_TROOP
	|| !takes[VOICE_IMP][moment])
	return false;
    if (said[moment].again && leveltime - saidtic[moment] < said[moment].again)
	return true;
    take = Take (VOICE_IMP, moment);
    LumpName (name, VOICE_IMP, moment, take);
    Voice_ (actor, name, moment);
    saidtic[moment] = leveltime;
    P_DerpLog (actor, "cries: %s", said[moment].name);
    tics = Length (name);
    if (!answered && said[moment].answer != NUMBARKS)
    {
	impanswer = said[moment].answer;
	impanswertic = leveltime + tics + ANSWER_AFTER;
	impanswerx = actor->x;
	impanswery = actor->y;
	impcaller = actor->serial;
    }
    return true;
}

boolean P_DerpCry (mobj_t* actor, bark_t moment)
{
    return Cry (actor, moment, false);
}

// The nearest other imp, alive, within range.
static mobj_t* NearImp (fixed_t x, fixed_t y, int range, int except)
{
    thinker_t*	th;
    mobj_t*	best = NULL;
    fixed_t	bestd = range*FRACUNIT;

    for (th = thinkercap.next; th != &thinkercap; th = th->next)
    {
	mobj_t*	mo = (mobj_t *)th;
	fixed_t	d;
	if (th->function.acp1 != (actionf_p1)P_MobjThinker || mo->type != MT_TROOP || mo->health <= 0
	    || mo->serial == except || mo->mind == MIND_NONE)
	    continue;
	d = P_AproxDistance (mo->x - x, mo->y - y);
	if (d < bestd)
	{
	    bestd = d;
	    best = mo;
	}
    }
    return best;
}

// The imp's own for what its body does, and what the pack makes of a death.
static boolean ImpBody (mobj_t* actor, bark_t moment)
{
    bark_t	cry = moment == BARK_PAIN
	? (actor->health > actor->info->spawnhealth/2 ? BARK_IMPPAINSMALL : BARK_IMPPAINBIG)
	: moment == BARK_GIBBED ? BARK_IMPGIBBED : BARK_IMPDEATH;

    if (cry == BARK_IMPDEATH || cry == BARK_IMPGIBBED)
	Cry (NearImp (actor->x, actor->y, PACK_HEARS, actor->serial), BARK_IMPPACKDEATH, true);
    return Cry (actor, cry, false);
}

boolean P_DerpBody (mobj_t* actor, bark_t moment)
{
    char	name[9];
    int		voice, take;

    if (Voiced () && actor && !actor->player && actor->type == MT_TROOP)
	return ImpBody (actor, moment);
    if (!Voiced () || !actor || actor->player || !P_DerpSoldier (actor->type))
	return false;
    // He barely felt that, or wants you to think so.
    if (moment == BARK_PAIN && actor->type == MT_CHAINGUY && P_Random () < 64 && P_DerpBark (actor, BARK_SHRUG))
	return true;
    voice = Voice (actor, moment);
    take = Take (voice, moment);
    if (!take)
	return false;
    ItsLump (name, actor, voice, moment, take);
    Voice_ (actor, name, moment);
    return true;
}

void P_DerpBarkSoon (mobj_t* actor, bark_t moment)
{
    if (soon == NUMBARKS && Speaks (actor))
    {
	soon = moment;
	soonserial = actor->serial;
	soontic = leveltime + REACTION;
    }
}

// The nearest soldier awake to (x, y) within range; that sees him, if he is given.
static mobj_t* NearSoldier (fixed_t x, fixed_t y, int range, mobj_t* him)
{
    thinker_t*	th;
    mobj_t*	best = NULL;
    fixed_t	bestd = range*FRACUNIT;

    for (th = thinkercap.next; th != &thinkercap; th = th->next)
    {
	mobj_t*	mo = (mobj_t *)th;
	fixed_t	d;
	if (th->function.acp1 != (actionf_p1)P_MobjThinker || !Speaks (mo) || mo->mind == MIND_NONE)
	    continue;
	d = P_AproxDistance (mo->x - x, mo->y - y);
	if (d < bestd && mo != him && (!him || P_CheckSight (mo, him)))
	{
	    bestd = d;
	    best = mo;
	}
    }
    return best;
}

void P_DerpBarkNear (fixed_t x, fixed_t y, int range, bark_t moment)
{
    mobj_t*	mo;

    if (Voiced () && (mo = NearSoldier (x, y, range, NULL)) != NULL)
	P_DerpBark (mo, moment);
}

void P_DerpBarkSawFall (mobj_t* victim, int range, bark_t moment)
{
    mobj_t*	mo;

    if (Voiced () && (mo = NearSoldier (victim->x, victim->y, range, victim)) != NULL)
	P_DerpBark (mo, moment);
}

void P_DerpBarkHit (mobj_t* target, mobj_t* source, int damage)
{
    if (!Voiced ())
	return;
    // He is done for: the pack has seen it.
    if (target->player && damage >= target->health)
    {
	Cry (NearImp (target->x, target->y, WATCH_NEAR, 0), BARK_IMPCHITTER, false);
	return;
    }
    if (!source || source == target || source->player || damage >= target->health || !Speaks (target))
	return;
    P_DerpBarkSoon (target, P_DerpSoldier (source->type) ? BARK_FRIENDHIT : BARK_DEMONHIT);
}

// Standing in something that hurts.
static boolean Burning (const mobj_t* him)
{
    int	special = him->subsector->sector->special;

    return (special == 4 || special == 5 || special == 7 || special == 16) && him->z <= him->floorz;
}

static boolean ByABarrel (const mobj_t* him)
{
    thinker_t*	th;

    for (th = thinkercap.next; th != &thinkercap; th = th->next)
    {
	mobj_t*	mo = (mobj_t *)th;
	if (th->function.acp1 == (actionf_p1)P_MobjThinker && mo->type == MT_BARREL && mo->health > 0
	    && P_AproxDistance (mo->x - him->x, mo->y - him->y) < BARREL_NEAR*FRACUNIT)
	    return true;
    }
    return false;
}

// What a soldier who has him in sight says about him, of what he is doing.
static void Watch (void)
{
    mobj_t*	him = players[consoleplayer].mo;
    mobj_t*	mo;
    bark_t	line = NUMBARKS;

    if (!him || him->health <= 0 || !him->subsector)
	return;
    if (Burning (him) && leveltime - saidtic[BARK_INSLIME] >= said[BARK_INSLIME].again)
	line = BARK_INSLIME;
    else if (R_Flashlight && R_Flashlight () && leveltime - saidtic[BARK_FLASHLIGHT] >= said[BARK_FLASHLIGHT].again)
	line = BARK_FLASHLIGHT;
    else if (leveltime - saidtic[BARK_BARRELS] >= said[BARK_BARRELS].again && ByABarrel (him))
	line = BARK_BARRELS;
    if (line != NUMBARKS && (mo = NearSoldier (him->x, him->y, WATCH_NEAR, him)) != NULL)
	P_DerpBark (mo, line);
}

// The one it was to be, if it is alive.
static void SaySoon (void)
{
    thinker_t*	th;
    bark_t	line = soon;

    soon = NUMBARKS;
    for (th = thinkercap.next; th != &thinkercap; th = th->next)
    {
	mobj_t*	mo = (mobj_t *)th;
	if (th->function.acp1 == (actionf_p1)P_MobjThinker && mo->serial == soonserial)
	{
	    P_DerpBark (mo, line);
	    return;
	}
    }
}

void P_BarksThink (void)
{
    thinker_t*	th;
    mobj_t*	best = NULL;
    fixed_t	bestd = ANSWER_NEAR*FRACUNIT;
    bark_t	line = answer;

    if (soon != NUMBARKS && leveltime >= soontic)
	SaySoon ();
    if (Voiced () && leveltime % TICRATE == 0)
	Watch ();
    if (impanswer != NUMBARKS && leveltime >= impanswertic)
    {
	Cry (NearImp (impanswerx, impanswery, PACK_HEARS, impcaller), impanswer, true);
	impanswer = NUMBARKS;
    }
    if (line == NUMBARKS || leveltime < answertic)
	return;
    answer = NUMBARKS;
    for (th = thinkercap.next; th != &thinkercap; th = th->next)
    {
	mobj_t*	mo = (mobj_t *)th;
	fixed_t	d;
	if (th->function.acp1 != (actionf_p1)P_MobjThinker || mo->serial == speaker || !Speaks (mo)
	    || mo->mind == MIND_NONE)
	    continue;
	d = P_AproxDistance (mo->x - answerx, mo->y - answery);
	if (d < bestd)
	{
	    bestd = d;
	    best = mo;
	}
    }
    if (best)
	P_DerpBark (best, line);
}
