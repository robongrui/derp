//
// What each soldier is for in a squad (p_roles.h).
//

#include "doomdef.h"
#include "doomstat.h"
#include "m_random.h"
#include "p_local.h"
#include "r_main.h"
#include "p_director.h"
#include "p_mind.h"
#include "p_fire.h"
#include "p_roles.h"
#include "p_tune.h"

// How far off it would go for him, and how many at once: not all of them.
#define PUSH_FAR	TUNE (PUSH_FAR, 1024)
#define PUSH_MOST	TUNE (PUSH_MOST, 2)
// Unseen by him, this near is a chance by itself.
#define PUSH_UNSEEN	TUNE (PUSH_UNSEEN, 640)
// It is this far out of the middle of his view.
#define LOOKS_AWAY	ANG45
#define HURTING		TUNE (ROLES_HURTING, 50)
// A chance is taken this often, of 256, each time it thinks.
#define ODDS_HOLD	TUNE (ODDS_HOLD, 96)
#define ODDS_PRESS	TUNE (ODDS_PRESS, 160)

role_t P_RoleOf (const mobj_t* mo)
{
    switch (mo->type)
    {
      case MT_SHOTGUY:
	return ROLE_ASSAULT;
      case MT_CHAINGUY:
	return ROLE_LANE;
      default:
	return ROLE_RIFLE;
    }
}

static int Pushers (void)
{
    thinker_t*	th;
    int		n = 0;

    for (th = thinkercap.next; th != &thinkercap; th = th->next)
	if (th->function.acp1 == (actionf_p1)P_MobjThinker && P_MindPushing ((mobj_t *)th))
	    n++;
    return n;
}

const char* P_RoleChance (mobj_t* actor, mobj_t* player, boolean visible)
{
    fixed_t	dist = P_AproxDistance (player->x - actor->x, player->y - actor->y);
    int		off = (int)(R_PointToAngle2 (player->x, player->y, actor->x, actor->y) - player->angle);
    const char*	why;

    if (P_RoleOf (actor) != ROLE_ASSAULT || actor->guard || dist > PUSH_FAR*FRACUNIT)
	return NULL;
    if (P_DerpPinned ())
	why = "he is pinned";
    else if (player->player && (player->player->damagecount || player->health < HURTING))
	why = "he is hurting";
    else if (off > (int)LOOKS_AWAY || off < -(int)LOOKS_AWAY)
	why = "he looks away";
    else if (!visible && dist < PUSH_UNSEEN*FRACUNIT)
	why = "he has not seen it";
    else
	return NULL;
    if (Pushers () >= PUSH_MOST || P_Random () >= (P_DirectorPressing () ? ODDS_PRESS : ODDS_HOLD))
	return NULL;
    return why;
}
