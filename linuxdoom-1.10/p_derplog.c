//
// What DERP's monsters do, a line each (p_derplog.h).
//

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "doomdef.h"
#include "doomstat.h"
#include "m_argv.h"
#include "p_local.h"
#include "p_derp.h"
#include "p_derplog.h"
#include "p_mind.h"
#include "p_tune.h"

static FILE*	logfile;
static int	asked = -1;
boolean		derplogon;

// The position track, for the AI graph's replay: this often.
#define TRACK_EVERY	5

// What each monster was last tic, by its serial: so a change is a line.
#define TRACKED		4096
typedef struct { int mind, tactic, kneel, pounce, health, sprinttic; } seen_t;
static seen_t	seen[TRACKED];

boolean P_DerpLogging (void)
{
    if (asked < 0)
    {
	int	p = M_CheckParm ("-derplog");
	asked = 0;
	if (p && p + 1 < myargc && (logfile = fopen (myargv[p + 1], "w")))
	    asked = derplogon = 1;
    }
    return asked && P_DerpAI ();
}

static const char* Name (mobj_t* mo)
{
    switch (mo->type)
    {
      case MT_POSSESSED: return "zombie";
      case MT_SHOTGUY: return "shotgunner";
      case MT_CHAINGUY: return "chaingunner";
      case MT_WOLFSS: return "ss";
      case MT_TROOP: return "imp";
      case MT_SERGEANT: return "pinky";
      case MT_SHADOWS: return "spectre";
      case MT_HEAD: return "caco";
      case MT_BRUISER: return "baron";
      default: return "monster";
    }
}

void P_DerpLog (mobj_t* mo, const char* fmt, ...)
{
    va_list	args;

    if (!P_DerpLogging ())
	return;
    if (mo)
	fprintf (logfile, "%6d %-11s #%-4d %6d %6d  ", leveltime, Name (mo), mo->serial,
		 mo->x>>FRACBITS, mo->y>>FRACBITS);
    else
	fprintf (logfile, "%6d %-11s %5s %6s %6s  ", leveltime, "-", "", "", "");
    va_start (args, fmt);
    vfprintf (logfile, fmt, args);
    va_end (args);
    fputc ('\n', logfile);
    fflush (logfile);
}

void P_DerpRuleFired (mobj_t* mo, const char* name)
{
    P_DerpLog (mo, "rule %s", name);
}

// A number changed by the AI graph while the game runs; -1: back to the source's.
#ifdef DOOM_TUNE
static void Tuned (const char* name, int value)
{
    P_DerpLog (NULL, "tune %s %d", name, value);
}
#endif

// Where it is and what it means to do. No flush: the next ordinary line does that.
// "@ angle health mind tactic lurk sees goalx goaly coverx covery peekx peeky"
static void Track (mobj_t* mo)
{
    mobj_t*	him = players[consoleplayer].mo;
    int		sees = him && him->health > 0 && P_CheckSight (mo, him);

    fprintf (logfile, "%6d %-11s #%-4d %6d %6d  @ %d %d %d %d %d %d %d %d %d %d %d %d\n", leveltime, Name (mo),
	     mo->serial, mo->x>>FRACBITS, mo->y>>FRACBITS, (int)(mo->angle/(ANG45/45)), mo->health, mo->mind,
	     mo->tactic, mo->lurk, sees, mo->goalx>>FRACBITS, mo->goaly>>FRACBITS, mo->coverx>>FRACBITS,
	     mo->covery>>FRACBITS, mo->peekx>>FRACBITS, mo->peeky>>FRACBITS);
}

void P_DerpLogLevel (void)
{
    thinker_t*	th;

    memset (seen, 0, sizeof seen);
    if (!P_DerpLogging ())
	return;
    if (gamemode == commercial)
	P_DerpLog (NULL, "level MAP%02d (map %d), skill %d", gamemap, gamemap, gameskill);
    else
	P_DerpLog (NULL, "level E%dM%d (map %d), skill %d", gameepisode, gamemap, gamemap, gameskill);
#ifdef DOOM_TUNE
    P_TuneWatch (Tuned);
#endif
    for (th = thinkercap.next; th != &thinkercap; th = th->next)
    {
	mobj_t*	mo = (mobj_t *)th;
	if (th->function.acp1 != (actionf_p1)P_MobjThinker || !(mo->flags & MF_COUNTKILL))
	    continue;
	P_DerpLog (mo, "starts, health %d%s", mo->health, (mo->flags & MF_AMBUSH) ? ", marked deaf" : "");
	seen[mo->serial % TRACKED].health = mo->health;
	Track (mo);
    }
}

void P_DerpLogThink (mobj_t* mo)
{
    seen_t*	s;
    int		kneel;

    if (mo->player && !(leveltime % TRACK_EVERY) && P_DerpLogging ())
	P_DerpLog (NULL, "player @ %d %d %d %d", mo->x>>FRACBITS, mo->y>>FRACBITS, (int)(mo->angle/(ANG45/45)),
		   mo->player->health);
    if (!(mo->flags & MF_COUNTKILL) || mo->player || !P_DerpLogging ())
	return;
    s = &seen[mo->serial % TRACKED];
    if (mo->health != s->health)
    {
	if (mo->health <= 0 && s->health > 0)
	    P_DerpLog (mo, "dies%s", mo->silent ? " silently" : "");
	else if (mo->health > 0)
	    P_DerpLog (mo, "health %d -> %d", s->health, mo->health);
	s->health = mo->health;
    }
    if (mo->health <= 0)
	return;
    if (mo->mind != s->mind)
    {
	P_DerpLog (mo, "mind %s -> %s", P_MindName (s->mind), P_MindName (mo->mind));
	s->mind = mo->mind;
	s->tactic = mo->tactic;
    }
    else if (mo->tactic != s->tactic)
    {
	P_DerpLog (mo, "%s step %d -> %d", P_MindName (mo->mind), s->tactic, mo->tactic);
	s->tactic = mo->tactic;
    }
    kneel = mo->kneeltic != 0;
    if (kneel != s->kneel)
    {
	P_DerpLog (mo, kneel ? "kneels" : "stands up");
	s->kneel = kneel;
    }
    if (mo->pounce != s->pounce)
    {
	static const char* phase[] = { "pounce over", "crouches to pounce", "leaps", "strikes in the air" };
	P_DerpLog (mo, "%s", mo->pounce >= 0 && mo->pounce < 4 ? phase[mo->pounce] : "?");
	s->pounce = mo->pounce;
    }
    if (!(leveltime % TRACK_EVERY) && (mo->mind || mo->target || mo->lurk))
	Track (mo);
}

// Sprints are logged once a second at most (P_DerpSprint calls this).
void P_DerpLogHit (mobj_t* target, mobj_t* source, int damage)
{
    if (!P_DerpLogging () || !source || source == target || source->player || !(target->flags & MF_COUNTKILL))
	return;
    P_DerpLog (target, "hit by #%d (type %d, %s) for %d%s", source->serial, source->type,
	       source->target == target ? "aimed" : source->gunwalk ? "walking" : "standing", damage,
	       damage >= target->health ? ", dead" : "");
}

void P_DerpLogSprint (mobj_t* mo)
{
    seen_t*	s;

    if (!P_DerpLogging ())
	return;
    s = &seen[mo->serial % TRACKED];
    if (leveltime - s->sprinttic >= TICRATE)
	P_DerpLog (mo, "sprints (%s)", P_MindName (mo->mind));
    s->sprinttic = leveltime;
}
