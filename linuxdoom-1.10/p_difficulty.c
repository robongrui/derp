//
// What the skill levels mean to DERP's monsters (p_difficulty.h).
//

#include <stdlib.h>

#include "doomdef.h"
#include "doomstat.h"
#include "m_argv.h"
#include "m_random.h"
#include "p_local.h"
#include "p_derp.h"
#include "p_difficulty.h"

// Ultra-Violence: sharper than stock's aim (9.0) and quicker to spot him, every
// chance taken: with slow motion and headshots stock's row was too easy (the
// user played 6.5/100/75/0 on 2026-11-17: "stupidly hard", and kept it).
// Nightmare has no DERP monsters. The other rows are first guesses.
static const difficulty_t	rows[] =
{
    //  aim	trigger	eyes	pace
    {	13.0,	50,	170,	90 },	// I'm too young to die
    {	11.5,	70,	140,	50 },	// Hey, not too rough
    {	10.0,	85,	115,	20 },	// Hurt me plenty
    {	6.5,	100,	75,	0 },	// Ultra-Violence: as fast as the gun goes
};
#define ROWS	((int)(sizeof rows/sizeof *rows))

const difficulty_t* P_Difficulty (void)
{
    static difficulty_t	tuned;
    static int		asked;
    int			row = gameskill < ROWS ? gameskill : ROWS - 1;

    if (!asked)
    {
	int	p = M_CheckParm ("-derptune");
	asked = 1;
	if (p && p + 4 < myargc)
	{
	    tuned.aim = atof (myargv[p + 1]);
	    tuned.trigger = atoi (myargv[p + 2]);
	    tuned.eyes = atoi (myargv[p + 3]);
	    tuned.pace = atoi (myargv[p + 4]);
	    asked = 2;
	}
    }
    return asked == 2 ? &tuned : &rows[row < 0 ? 0 : row];
}

// With him in sight a soldier does not stop shooting. One chase step between
// shots all the same: that is where it thinks (P_BrainChase), and without it
// nothing it had in mind would ever start.
boolean P_DerpOverdue (mobj_t* actor)
{
    if (!P_DerpAI () || !actor->target || actor->player || !P_DerpSoldier (actor->type)
	|| actor->reactiontime || actor->chasetic <= actor->firetic
	|| leveltime - actor->firetic < P_Difficulty ()->pace)
	return false;
    return P_CheckSight (actor, actor->target);
}

boolean P_DerpTrigger (void)
{
    // (All of them taken, no die is thrown: vanilla's stream stays vanilla's.)
    if (!P_DerpAI () || P_Difficulty ()->trigger >= 100)
	return true;
    return P_Random ()*100 < P_Difficulty ()->trigger*256;
}
