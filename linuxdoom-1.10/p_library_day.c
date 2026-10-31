//
// The library's day (scp_7179_b/README.md, Day, night and sleep): awake for
// AWAKE tics (30 minutes; -libraryday minutes for trying it), yawning from
// YAWNING of that, the eyes closing now and then from BLINKING, the lights
// dimming from EVENING, and past PASSING_OUT he drops where he stands.
// Asleep, in a bed or where he fell, or dead, the morning comes: the dead
// get up, the kills are forgotten, the lights come back, and he is rested
// (see p_library.h, p_library_local.h).
//

#include <stdlib.h>

#include "doomdef.h"
#include "doomstat.h"
#include "d_event.h"
#include "m_argv.h"
#include "m_random.h"
#include "r_main.h"
#include "tables.h"
#include "p_library_local.h"

#define DAY_PART(part)	((int)((long long)awake_for*(part)/100))
#define YAWNING		80
#define BLINKING	95
#define EVENING		85
#define PASSING_OUT	110
#define FADE		(2*TICRATE)		// a bed's fade, each way
#define DEATH		(3*TICRATE)		// dead before he wakes
static int		awake_for = 30*60*TICRATE;
static int		awake;			// tics since the morning
static int		yawns;			// how many so far: a yawn is due each time this grows
static int		dark;			// tics of the eyes closed still to come
static int		darkness;		// out of how many, for the fade
static enum { AWAKE, IN_A_BED, PASSED_OUT, COMING_TO, KNOCKED_OUT } sleep;  // what the dark is
static int		dead_for;		// tics dead
static int		knocks;			// edges hit since the program started, never saved: his head's crunch
static int		deaths;			// so far this game, an edge's knock too
static int		diedat = -1;		// the leveltime of the last
static boolean		pressed;		// use held since the last tic
static short*		daylight;		// the sectors' light by day


// Evening: the lights dim to two fifths of the day's, from EVENING until
// he would drop.
static void Lights (void)
{
    int		i, from = DAY_PART (EVENING), to = DAY_PART (PASSING_OUT);
    int		dim = awake <= from ? 0 : awake >= to ? 60 : (awake - from)*60/(to - from);

    for (i = 0; daylight && i < numsectors; i++)
	sectors[i].lightlevel = daylight[i] - daylight[i]*dim/100;
}


void L_DayLevel (void)
{
    int		p = M_CheckParm ("-libraryday");

    if (p && p < myargc-1 && atof (myargv[p+1]) > 0)
	awake_for = (int)(atof (myargv[p+1])*60*TICRATE);
    awake = yawns = dark = darkness = dead_for = deaths = 0;
    diedat = -1;
    sleep = AWAKE;
    free (daylight);
    daylight = malloc (numsectors*sizeof *daylight);
    for (p = 0; daylight && p < numsectors; p++)
	daylight[p] = sectors[p].lightlevel;
}


// The morning: the dead get up (everyone is drawn anew, the kills
// forgotten), the lights come back, and he is rested.
static void Morning (void)
{
    thinker_t*	th;
    thinker_t*	next;
    mobj_t*	me = players[consoleplayer].mo;

    for (th = thinkercap.next; th != &thinkercap; th = next)
    {
	next = th->next;
	if (L_IsMobj (th) && (mobj_t*)th != me && P_LibraryCarried ((mobj_t*)th))
	    L_Forget ((mobj_t*)th);
    }
    L_ForgetDead ();
    L_Renew ();
    L_PopulateAll ();
    awake = 0;
    Lights ();
    L_Log ("morning", me);
}


// Dead: a few seconds, or use, and he wakes where he died, whole, and it is
// morning. The body stays no more than he does.
extern void P_SpawnPlayer (mapthing_t* mthing);
static void Death (player_t* player)
{
    mobj_t*	body = player->mo;
    mapthing_t	spot;

    if (player->playerstate == PST_LIVE)
    {
	dead_for = 0;
	return;
    }
    if (dead_for == 0)
	deaths++, diedat = leveltime;
    if (++dead_for < DEATH && player->playerstate != PST_REBORN)
	return;
    spot.x = body->x >> FRACBITS;
    spot.y = body->y >> FRACBITS;
    spot.angle = (short)((body->angle >> ANGLETOFINESHIFT)*360/FINEANGLES);
    spot.type = consoleplayer + 1;
    spot.options = 7;
    player->playerstate = PST_REBORN;
    P_SpawnPlayer (&spot);
    body->player = NULL;
    L_Forget (body);
    dead_for = 0;
    dark = darkness = FADE;
    sleep = COMING_TO;
    L_Log ("woke from death", player->mo);
    Morning ();
}


// Hit an edge falling (p_library_shaft.c), already in the bed he wakes in:
// black at once for five to ten seconds, his ears ringing, then coming to,
// and it is morning, as after any death.
void L_KnockOut (player_t* player)
{
    (void)player;
    dark = darkness = (5 + P_Random ()*5/256)*TICRATE + FADE;
    sleep = KNOCKED_OUT;
    knocks++;
    deaths++;
    diedat = leveltime;
    Morning ();
}


// His ears after hitting an edge (0-1): ringing through the dark, fading
// over its last seconds.
// How tired he is, for his steps' sound: 0 until the yawns, 1 as he is
// about to drop where he stands.
float P_LibraryTired (void)
{
    int		from = DAY_PART (YAWNING), to = DAY_PART (PASSING_OUT);

    if (!P_Library () || awake <= from || dark)
	return 0;
    return awake >= to ? 1 : (awake - from)/(float)(to - from);
}


int P_LibraryKnocks (void)
{
    return knocks;
}


float P_LibraryRinging (void)
{
    if (!P_Library () || sleep != KNOCKED_OUT || !dark)
	return 0;
    return dark >= 3*TICRATE ? 1 : dark/(3.f*TICRATE);
}


static boolean UsePressed (player_t* player)
{
    boolean	now = (player->cmd.buttons & BT_USE) != 0;
    boolean	edge = now && !pressed;

    pressed = now;
    return edge;
}


// The sector a little ahead of him, where use reaches.
static sector_t* Ahead (mobj_t* mo)
{
    int		an = mo->angle >> ANGLETOFINESHIFT;

    return R_PointInSubsector (mo->x + 40*finecosine[an], mo->y + 40*finesine[an])->sector;
}


// The day's clock, and sleep: in a bed when tired enough, or dropping
// where he stands when too tired.
static void Day (player_t* player)
{
    mobj_t*	mo = player->mo;
    int		before = yawns;

    if (dark)
    {
	// Asleep, or passed out: no moving, the world going on; at the turn of
	// a bed's fade, or at the end of passing out, the morning.
	mo->reactiontime = 2;
	dark--;
	if ((sleep == IN_A_BED && dark == FADE) || (sleep == PASSED_OUT && !dark))
	    Morning ();
	if (!dark)
	{
	    if (sleep == KNOCKED_OUT)
		L_Log ("came to", mo);
	    sleep = AWAKE;
	}
	return;
    }

    awake++;
    if (awake >= DAY_PART (YAWNING))
	yawns = 1 + (awake - DAY_PART (YAWNING)) / (20*TICRATE);
    // Each yawn told in words (a yawn's sound is beyond synthesis).
    if (yawns != before)
    {
	player->message = "YOU ARE GETTING TIRED";
	L_Log ("yawn", mo);
    }
    Lights ();

    if (awake >= DAY_PART (PASSING_OUT))
    {
	dark = darkness = DEATH + FADE;		// as long as dying: no longer (owner, 2026-10-30)
	sleep = PASSED_OUT;
	L_Log ("passed out", mo);
	return;
    }
    if (UsePressed (player))
    {
	// A bed he lies on, or one in front of him; only tired enough to sleep.
	if (mo->subsector->sector->tag == LIBRARY_TAG_BED || Ahead (mo)->tag == LIBRARY_TAG_BED)
	{
	    if (awake < DAY_PART (60))
		player->message = "YOU ARE NOT TIRED YET.";
	    else
	    {
		dark = darkness = 2*FADE;
		sleep = IN_A_BED;
		L_Log ("to bed", mo);
	    }
	}
	else if (Ahead (mo)->tag == LIBRARY_TAG_KIOSK)
	    L_Kiosk (player);
    }
}


void L_Day (player_t* player)
{
    Death (player);
    if (players[consoleplayer].playerstate == PST_LIVE)
	Day (&players[consoleplayer]);
}


boolean P_LibraryEyes (float* lids, float* black)
{
    int		blink = DAY_PART (BLINKING), t;

    *lids = *black = 0;
    if (!P_Library ())
	return false;
    if (dark)
    {
	// A bed fades out and back in, coming to only in; passing out is dark
	// through until the last of it.
	t = sleep == IN_A_BED ? (dark > FADE ? 2*FADE - dark : dark) : dark < FADE ? dark : FADE;
	*black = t >= FADE ? 1 : t/(float)FADE;
	*lids = *black;
	return true;
    }
    if (players[consoleplayer].playerstate != PST_LIVE)
    {
	*black = dead_for/(float)DEATH;
	return true;
    }
    if (awake >= blink)
    {
	// A blink every few seconds, longer the closer he is to dropping.
	t = (awake - blink) % (4*TICRATE);
	if (t < 12 + (awake - blink)/(20*TICRATE))
	    *lids = t < 6 ? t/6.f : 1;
    }
    return true;
}


// His deaths so far this game, and the leveltime of the last (-1: none).
int P_LibraryDeaths (int* when)
{
    *when = diedat;
    return deaths;
}


int P_LibraryYawns (void)
{
    return yawns;
}


fixed_t P_LibraryLegs (fixed_t thrust)
{
    int		from = DAY_PART (YAWNING);

    // Tired legs: down to four fifths of their push by the time he drops.
    if (!P_Library () || awake <= from)
	return thrust;
    return thrust - (fixed_t)((long long)thrust*(awake - from)/(DAY_PART (PASSING_OUT) - from)/5);
}


void L_ArchiveDay (void)
{
    int		number, flag;

    L_Put (&awake, sizeof awake);
    L_Put (&yawns, sizeof yawns);
    L_Put (&dark, sizeof dark);
    L_Put (&darkness, sizeof darkness);
    number = sleep;
    L_Put (&number, sizeof number);
    L_Put (&dead_for, sizeof dead_for);
    L_Put (&deaths, sizeof deaths);
    L_Put (&diedat, sizeof diedat);
    flag = pressed;
    L_Put (&flag, sizeof flag);
}


// The day's light is the loaded level's lamps' (set up afresh), dimmed as
// the hour has it.
void L_UnArchiveDay (void)
{
    int		number, flag;

    L_Get (&awake, sizeof awake);
    L_Get (&yawns, sizeof yawns);
    L_Get (&dark, sizeof dark);
    L_Get (&darkness, sizeof darkness);
    L_Get (&number, sizeof number);
    sleep = number;
    L_Get (&dead_for, sizeof dead_for);
    L_Get (&deaths, sizeof deaths);
    L_Get (&diedat, sizeof diedat);
    L_Get (&flag, sizeof flag);
    pressed = flag != 0;
    if (awake < 0 || yawns < 0 || dark < 0 || darkness < 0 || dead_for < 0 || deaths < 0
	|| number < AWAKE || number > KNOCKED_OUT)
	I_Error ("Bad savegame: the library's day");
    Lights ();
}


void L_RebaseDay (int off)
{
    diedat -= off;
}
