// What the soldier files share and nobody else needs (p_tactics.c, p_squad.c,
// p_guards.c, p_hide.c, p_fireteam.c, p_lanes.c): their helpers, by the names
// they have inside, with P_Tac in front; and the numbers more than one of them
// reads.

#ifndef __P_TACTICS_LOCAL__
#define __P_TACTICS_LOCAL__

#include "doomtype.h"
#include "m_fixed.h"
#include "p_mobj.h"
#include "p_tune.h"

// Near enough to see or call to: the friends a monster knows it has.
#define NEARBY		TUNE (TACTICS_NEARBY, 768)
// How long it holds its post before a fight next door brings it, and from how
// far off (straight: a sound's way round is half as long again).
#define BREACH_AFTER	TUNE (BREACH_AFTER, 6*TICRATE)
#define COVER_BELIEF	TUNE (COVER_BELIEF, 4*TICRATE)
#define PUSH_HOME(hi)	((hi)*2/3)

// At level setup (P_TacticsLevel): each file's own.
void P_SquadLevel (void);
void P_SquadsKnown (void);
void P_GuardsLevel (void);
void P_FireteamLevel (void);

// The helpers, and whether the tactics are on at all (P_TacticsOn).
boolean P_TacAt (mobj_t* actor, fixed_t x, fixed_t y);
boolean P_TacBait (mobj_t* actor, mobj_t* player);
boolean P_TacBlocksWay (mobj_t* actor, fixed_t x, fixed_t y);
boolean P_TacBound (mobj_t* actor);
boolean P_TacBoundPosition (mobj_t* actor, mobj_t* player, int lo, int hi, fixed_t* fx, fixed_t* fy);
boolean P_TacBreach (mobj_t* actor);
boolean P_TacCoverServes (mobj_t* actor, mobj_t* player);
boolean P_TacCoverTaken (mobj_t* actor, fixed_t x, fixed_t y);
boolean P_TacFindAmbush (mobj_t* actor, fixed_t x, fixed_t y, fixed_t* ax, fixed_t* ay, angle_t* facing);
boolean P_TacFindCover (mobj_t* actor, mobj_t* threat, boolean peek, boolean away,
			  fixed_t* cx, fixed_t* cy, fixed_t* kx, fixed_t* ky);
boolean P_TacFindCoverNear (mobj_t* actor, fixed_t ox, fixed_t oy, mobj_t* threat, boolean peek,
			      boolean away, fixed_t* cx, fixed_t* cy, fixed_t* kx, fixed_t* ky);
boolean P_TacFindSquad (mobj_t* actor, fixed_t* gx, fixed_t* gy, int* squad);
boolean P_TacHeldUp (mobj_t* actor);
boolean P_TacInCover (mobj_t* actor);
boolean P_TacKeepRange (mobj_t* actor, mobj_t* player);
boolean P_TacLayFire (mobj_t* actor, boolean visible);
boolean P_TacNoWay (mobj_t* actor, mobj_t* player);
boolean P_TacOtherWay (mobj_t* actor, fixed_t dx, fixed_t dy);
boolean P_TacOutOfRange (mobj_t* actor, mobj_t* player);
boolean P_TacPostServes (mobj_t* actor, fixed_t x, fixed_t y, fixed_t nx, fixed_t ny);
boolean P_TacPush (mobj_t* actor, mobj_t* player, boolean visible);
boolean P_TacRound (mobj_t* actor, boolean visible);
boolean P_TacSafeWalk (mobj_t* actor, mobj_t* player, fixed_t x, fixed_t y);
boolean P_TacWayOut (mobj_t* actor, mobj_t* player, fixed_t* x, fixed_t* y);
boolean P_TacticsOn (void);
int P_TacFriends (mobj_t* actor);
int P_TacTeam (mobj_t* actor, boolean* bounding);
mobj_t* P_TacBelieved (mobj_t* actor, mobj_t* player, boolean visible);
mobj_t* P_TacInTheWay (mobj_t* actor);
mobj_t* P_TacLane (mobj_t* actor, fixed_t x, fixed_t y);
mobj_t* P_TacPoint (fixed_t x, fixed_t y);
mobj_t* P_TacSpread (mobj_t* actor);
mobj_t* P_TacWaiter (mobj_t* actor);
void P_TacBackToPost (mobj_t* actor);
void P_TacKneel (mobj_t* mo);
void P_TacKnow (mobj_t* mo);
void P_TacRange (mobj_t* actor, int* lo, int* hi, int* aside);
void P_TacStandUp (mobj_t* mo);

#endif
