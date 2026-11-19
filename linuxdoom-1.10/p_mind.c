//
// The one door a DERP monster's mind changes through (p_mind.h).
//

#include "doomdef.h"
#include "doomstat.h"
#include "p_local.h"
#include "r_main.h"
#include "p_derp.h"
#include "p_mind.h"
#include "p_derplog.h"

static const char* minds[NUMMINDS] = { "none", "investigate", "hunt", "search", "return",
				       "cover", "retreat", "ambush", "regroup", "heal", "flank" };
static const char* steps[NUMSTEPS] = { "none", "readied", "looking", "bounding", "pushing", "rounding",
				       "circling", "circling_back", "shooting", "going", "ducked", "leaning",
				       "watching", "back", "seen", "bait", "waiting", "round_choke" };

static void (*leaving) (mobj_t* actor, boolean gone);

void P_MindLeaving (void (*left) (mobj_t* actor, boolean gone))
{
    leaving = left;
}

void P_MindChange (mobj_t* actor, int mind, int step, const char* rule)
{
    if (derplogon && rule)
	P_DerpRuleFired (actor, rule);
    // What a soldier leaves in its cover: its knee, its wall, its post.
    if (leaving)
	leaving (actor, mind != MIND_COVER);
    actor->mind = mind;
    actor->tactic = step;
    actor->tactictic = leveltime;
    actor->waytic = -1000;	// a fresh route for whatever comes next
}

void P_MindStepTo (mobj_t* actor, int step, const char* rule)
{
    if (derplogon && rule)
	P_DerpRuleFired (actor, rule);
    actor->tactic = step;
}

void P_MindReset (mobj_t* actor)
{
    actor->mind = MIND_NONE;
    actor->tactic = STEP_NONE;
    actor->tactictic = 0;
}

boolean P_MindBounding (const mobj_t* mo)
{
    return mo->mind == MIND_HUNT && mo->tactic == STEP_BOUNDING;
}

boolean P_MindPushing (const mobj_t* mo)
{
    return P_DerpMonster ((mobj_t*)mo) && mo->mind == MIND_HUNT && mo->tactic == STEP_PUSHING && P_DerpSoldier (mo->type);
}

boolean P_MindRounding (const mobj_t* mo)
{
    return mo->mind == MIND_HUNT && mo->tactic == STEP_ROUNDING && P_DerpSoldier (mo->type);
}

boolean P_MindRunningRound (const mobj_t* mo)
{
    return P_DerpMonster ((mobj_t*)mo) && mo->type == MT_TROOP
	&& (mo->tactic == STEP_CIRCLING || mo->tactic == STEP_CIRCLING_BACK) && leveltime - mo->tactictic < TICRATE
	&& !(mo->stalls && leveltime - mo->stalltic < TICRATE);
}

boolean P_MindLurks (const mobj_t* mo)
{
    return mo->lurk != LURK_NONE;
}

boolean P_MindLurkWaits (const mobj_t* mo)
{
    return mo->lurk != LURK_NONE && mo->lurk != LURK_STALKING;
}

boolean P_MindStalks (const mobj_t* mo)
{
    return mo->lurk == LURK_STALKING;
}

void P_MindLurkOver (mobj_t* mo, const char* why)
{
    mo->lurk = LURK_NONE;
    mo->pincer = 0;
    mo->movecount = 0;
    P_DerpLog (mo, why);
}

void P_MindAlert (mobj_t* mo, int level)
{
    if (mo->alert < level)
	mo->alert = level;
    mo->alerttic = leveltime;
}

void P_MindReady (mobj_t* mo, fixed_t x, fixed_t y)
{
    P_MindAlert (mo, 1);
    if (mo->mind == MIND_NONE)
    {
	mo->angle = R_PointToAngle2 (mo->x, mo->y, x, y);
	// Where it came from, for its own next look (P_DerpGuardPost).
	mo->goalx = x;
	mo->goaly = y;
	mo->goaltic = leveltime;
	if (mo->tactic != STEP_READIED)
	    P_MindStep (mo, STEP_READIED, "none.readied", "A far fight is heard: it turns that way, "
			"ready [all]");
    }
}

const char* P_MindName (int mind)
{
    return mind >= 0 && mind < NUMMINDS ? minds[mind] : "?";
}

const char* P_StepName (int step)
{
    return step >= 0 && step < NUMSTEPS ? steps[step] : "?";
}
