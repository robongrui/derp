// What the skill levels mean to DERP's monsters: one row each, in p_difficulty.c.
// The numbers to change to make the game harder or easier, all in one place.
//
// Try a row without a rebuild: -derptune <aim> <trigger> <eyes> <pace>
// (for example -derptune 6 100 100 45 for sharper shots on any skill).
#ifndef __P_DIFFICULTY__
#define __P_DIFFICULTY__

#include "p_mobj.h"

typedef struct
{
    double	aim;		// a monster's aim error, degrees sideways: lower hits more
    int		trigger;	// percent of its chances to fire that it takes
    int		eyes;		// percent of the time it takes to spot him: lower is sooner
    int		pace;		// tics at most between a soldier's shots with him in sight
} difficulty_t;

// The row for the skill being played.
const difficulty_t* P_Difficulty (void);

// Whether actor takes this chance to fire (P_CheckMissileRange).
boolean P_DerpTrigger (void);
// Whether a soldier with him in sight has held its fire too long: it fires now,
// whatever the dice and its step say (A_Chase).
boolean P_DerpOverdue (mobj_t* actor);

#endif
