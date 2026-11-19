// DERP's director (the plan's phase 5): pacing, and the moves nobody sees.
//
// The director knows where the player is and how the last minutes went,
// which no monster does. It never tells a monster where he is. It keeps a
// tension, raised by the damage he takes and by monsters on his heels,
// falling while it is quiet. After a long quiet it nudges one idle monster
// nearby to go and look round his room (not at him: somewhere in it).
// While he is badly hurt and nobody sees him, searchers hold back.
//
// Offstage moves: Doomguy runs eight times as fast as a zombie walks, so a
// monster on its way somewhere (to look, home, to an ambush, to its squad)
// that the player cannot see takes up to three steps a chase step, still
// slower than he runs. Seen, it walks.

#ifndef __P_DIRECTOR__
#define __P_DIRECTOR__

#include "doomtype.h"
#include "p_mobj.h"

// At level setup, and each tic.
void P_DirectorLevel (void);
void P_DirectorThink (void);
// Whether one more may set off for a far noise: counted in if so. False:
// enough are after him for now; it readies where it is.
boolean P_DirectorAdmit (void);
// The same for one going in on a fight it can hear: past the budget, because
// his hands are full, but still a few at a time.
boolean P_DirectorBreach (void);
// The same for one holding ready on word of a fight: when it goes in on him, a
// couple each time it turns to press him.
boolean P_DirectorCalls (void);
// What it has decided for now: to press him or hold back (true with the
// director off), and whether that makes this the moment to go in on a fight
// (P_FightOn). P_DirectorStance fixes it, for the tests.
boolean P_DirectorPressing (void);
// One of his killed: losing them fast, the stance goes to holding back.
void P_DirectorFell (void);
boolean P_DirectorGoIn (void);
void P_DirectorStance (boolean press);

// The tension, 0 (calm) to 1000.
int P_DirectorTension (void);
// Whether searchers hold back now (the player badly hurt, unseen).
boolean P_DirectorHoldBack (void);
// How many steps actor may take this chase step: 1, or more offstage.
int P_DirectorSteps (mobj_t* actor);
// Nudge now, as after a long quiet (the tests): the monster nudged, or NULL.
mobj_t* P_DirectorNudge (void);

#endif
