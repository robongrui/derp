//
// Weather as a rule of the game (p_weather.h).
//

#include "doomdef.h"
#include "doomstat.h"
#include "p_local.h"
#include "p_phobos.h"
#include "p_weather.h"
#include "r_sky.h"
#include "r_state.h"
#include "../platform/derp_info.h"

int		weatherchoice = 1;

// Nukage rain's bite every 32 tics: gentler than a nukage floor's 5
// (P_PlayerInSpecialSector), so open ground wears you down, not out.
#define NUKAGE_DAMAGE	2

void P_KillMobj (mobj_t* source, mobj_t* target);

boolean P_NukageRain (void)
{
    if (demoplayback || demorecording || netgame || P_Phobos ())
	return false;
    return weatherchoice == 4 || (weatherchoice == 1 && DerpInfo_Map.weather == 3);
}

// Whether mo is alive and out in the rain: a player or a monster (not a
// barrel, a corpse or an item) under the sky, or where the wind blows it in
// at a broken window (R_RainedIn).
static boolean Exposed (mobj_t* mo)
{
    if (mo->health <= 0 || !(mo->flags & MF_SHOOTABLE) || (mo->flags & MF_CORPSE) || mo->type == MT_BARREL)
	return false;
    return mo->subsector->sector->ceilingpic == skyflatnum || (R_RainedIn && R_RainedIn (mo));
}

void P_WeatherThink (void)
{
    thinker_t*	th;

    if ((leveltime & 0x1f) || !P_NukageRain ())
	return;
    for (th = thinkercap.next; th != &thinkercap; th = th->next)
    {
	mobj_t*	mo;
	if (th->function.acp1 != (actionf_p1)P_MobjThinker)
	    continue;
	mo = (mobj_t*)th;
	if (!Exposed (mo))
	    continue;
	if (mo->player)
	{
	    if (!mo->player->powers[pw_ironfeet])
		P_DamageMobj (mo, NULL, NULL, NUKAGE_DAMAGE);
	    continue;
	}
	// A monster takes it as it stands: no pain state, no grudge.
	mo->health -= NUKAGE_DAMAGE;
	if (mo->health <= 0)
	    P_KillMobj (NULL, mo);
    }
}
