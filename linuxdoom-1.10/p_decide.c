//
// A monster's decisions: the loop over a kind's table of rules (p_decide.h).
//

#include "doomdef.h"
#include "doomstat.h"
#include "p_local.h"
#include "p_derp.h"
#include "p_director.h"
#include "p_tune.h"
#include "p_decide.h"

int P_DecideDuty (mobj_t* actor)
{
    int	duty = 0;

    if (actor->guard)
	duty |= DUTY_GUARD;
    if (leveltime < actor->holdtic)
	duty |= DUTY_HOLDS;
    if (P_DirectorGoIn ())
	duty |= DUTY_GOIN;
    return duty;
}

boolean P_DecideBegin (mobj_t* actor, boolean visible, decide_t* c)
{
    c->player = P_DerpPlayer ();
    if (!c->player)
	return false;
    c->visible = visible;
    c->health = actor->health*100/P_DerpHealth (actor->type);
    c->fear = 0;
    c->friends = 0;
    c->duty = P_DecideDuty (actor);
    return true;
}

boolean P_DecideOff (const char* name)
{
#ifdef DOOM_TUNE
    return P_Tune (name, 0) != 0;
#else
    return false;
#endif
}

boolean P_Decide (mobj_t* actor, const decision_t* rules, decide_t* c)
{
    const decision_t*	d;

    for (d = rules; d->rule; d++)
	if (!P_DecideOff (d->name) && d->rule (actor, c))
	    return true;
    return false;
}
