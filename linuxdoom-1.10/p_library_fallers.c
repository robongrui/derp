//
// The others in the chasm (scp_7179_b/README.md, Chasm and falling): now and
// then someone falls past, out of the fog above and on into the fog below,
// and now and then one of those standing at the railing climbs over, where
// he can see it; so he learns that it can be done. Whatever is in the chasm
// falls on at a body's top speed and is gone deep in the fog below. Not
// from above floor 1: nothing is up there. (See p_library_local.h.)
//

#include "doomdef.h"
#include "doomstat.h"
#include "m_random.h"
#include "p_library_local.h"

#define TOP		(40*FRACUNIT)		// a falling body's top speed (p_library_shaft.c)
#define FROM_ABOVE	(1900*FRACUNIT)		// where one falling past starts, in the fog
#define GONE_BELOW	(-1900*FRACUNIT)	// and where anything falling is gone
#define NEAR		1536			// how far along the ring from him they fall
#define FIRST		(20*TICRATE)		// the first can come this soon
#define SPREAD		(40*TICRATE)		// and the next within this much more
#define LOOKING		TICRATE			// the railing's people are watched this often
#define JUMPS		40			// one of them in this many jumps each time

static int		nextpast;	// tics to the next falling past
static int		looking;


// Falling, as a body does: held in its flinch (no walking in the air), not
// in the way of anyone else's fall, and not a person of the floor any more.
static void Falling (mobj_t* mo)
{
    L_ForgetPerson (mo);
    P_SetMobjState (mo, mo->info->painstate);
    mo->tics = -1;
    mo->flags &= ~MF_SOLID;
    mo->flags |= MF_DROPOFF;
}


// One falling past him: somewhere across the chasm, near him along it.
static void FallPast (mobj_t* player)
{
    fixed_t	x = player->x + ((P_Random () - 128)*NEAR/128)*FRACUNIT;
    fixed_t	y = (LIBRARY_NEAR_RAIL + 24 + P_Random ()*(LIBRARY_CHASM - 48)/256)*FRACUNIT;
    fixed_t	west = (LIBRARY_LANDING + 24)*FRACUNIT, east = (LIBRARY_LANDING + LIBRARY_SIDE - 24)*FRACUNIT;
    mobj_t*	mo;

    x = x < west ? west : x > east ? east : x;
    mo = P_SpawnMobj (x, y, FROM_ABOVE, MT_POSSESSED);
    mo->angle = (angle_t)P_Random () << 24;
    mo->momz = -TOP;
    Falling (mo);
    L_Log ("someone fell past", player);
}


// One of those at the railing he can see climbs over: up onto it and out.
static void Jump (mobj_t* player)
{
    mobj_t*	mo = L_SeenAtTheRailing (player, P_Random ());
    boolean	far;

    if (!mo)
	return;
    far = mo->y > (LIBRARY_NEAR_RAIL + LIBRARY_CHASM/2)*FRACUNIT;
    Falling (mo);
    mo->flags |= MF_NOCLIP;	// over the parapet, whatever its lines say
    mo->angle = far ? ANG270 : ANG90;
    mo->momy = far ? -6*FRACUNIT : 6*FRACUNIT;
    mo->momz = 7*FRACUNIT;
    L_Log ("someone jumped", player);
}


void L_FallersLevel (void)
{
    nextpast = FIRST + P_Random ()*SPREAD/256;
    looking = 0;
}


void L_Fallers (player_t* player)
{
    mobj_t*	mo = player->mo;
    thinker_t*	th;
    thinker_t*	next;
    mobj_t*	other;

    // Whatever is in the chasm: at most a body's top speed, and gone deep
    // below. Once out over it, a jumper stops at nothing on the way down.
    for (th = thinkercap.next; th != &thinkercap; th = next)
    {
	next = th->next;
	if (!L_IsMobj (th) || (other = (mobj_t*)th) == mo || !L_InChasm (other))
	    continue;
	if (other->momz < -TOP)
	    other->momz = -TOP;
	if ((other->flags & MF_NOCLIP) && other->y > (LIBRARY_NEAR_RAIL + 40)*FRACUNIT
	    && other->y < (LIBRARY_FAR_RAIL - 40)*FRACUNIT)
	{
	    other->flags &= ~MF_NOCLIP;	// out over it: straight down from here
	    other->momx = other->momy = 0;
	}
	if (other->z < GONE_BELOW && P_LibraryCarried (other) && P_LibraryFloorsBelow () > 0)
	{
	    L_Forget (other);
	    L_Log ("gone below", mo);
	}
    }

    // Only to him standing on a floor: falling, he has his own to see to.
    if (L_InChasm (mo))
	return;
    if (--nextpast <= 0)
    {
	nextpast = FIRST + P_Random ()*SPREAD/256;
	if (P_LibraryFloorsAbove () > 0)
	    FallPast (mo);
    }
    if (++looking >= LOOKING)
    {
	looking = 0;
	if (P_Random () % JUMPS == 0)
	    Jump (mo);
    }
}


size_t L_FallersSaveSize (void)
{
    return 2*sizeof (int);
}


void L_ArchiveFallers (void)
{
    L_Put (&nextpast, sizeof nextpast);
    L_Put (&looking, sizeof looking);
}


void L_UnArchiveFallers (void)
{
    L_Get (&nextpast, sizeof nextpast);
    L_Get (&looking, sizeof looking);
}
