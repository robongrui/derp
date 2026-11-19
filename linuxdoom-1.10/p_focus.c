//
// The slayer's focus: slow motion paid for in Authored (PLAN-shootslide.md).
//
// Two ways into slow motion, one meter. By hand (the slow-motion key, a shot
// with When shooting) the meter runs down a tic for each tic of slowed world.
// The shootslide takes one charge at once and asks for its slide's worth of
// slow motion. A kill gives half a charge back; a clean one, a shot through
// the head or a kill while sliding, the whole charge, so a shootslide that kills
// one breaks even and sloppy slow motion runs the hourglass dry (the user,
// 2026-11-17: a whole charge for every kill made Ultra-Violence too easy).
// Empty, the key does nothing and the shootslide is a slide.
//
// In slow motion his gun keeps closer to his own time than the world's: a
// switch takes the same real time as at full speed, and firing runs at the
// root of the slowing (half speed at a quarter), as far as the sound drops.
//

#include <math.h>

#include "doomdef.h"
#include "doomstat.h"
#include "p_local.h"
#include "p_derp.h"
#include "p_focus.h"
#include "p_moves.h"

// Most gun steps in one tic, whatever the clock says.
#define MOSTSTEPS	8

int		P_FocusFree;

static boolean	hand;		// slowed by hand this frame
static double	clock = 1;	// the game clock's speed
static boolean	flowing;

void A_Lower (player_t* player, pspdef_t* psp);
void A_Raise (player_t* player, pspdef_t* psp);

boolean P_FocusRules (void)
{
    return derpauthored && !netgame && !demoplayback && !demorecording;
}

boolean P_FocusCosts (void)
{
    return P_FocusRules () && !P_FocusFree;
}

int P_FocusLeft (void)
{
    return players[consoleplayer].focus;
}

boolean P_FocusAsked (void)
{
    return P_FocusRules () && players[consoleplayer].focusasked > 0
	&& players[consoleplayer].playerstate == PST_LIVE;
}

boolean P_FocusFlowing (void)
{
    return flowing;
}

void P_FocusHand (boolean slow)
{
    hand = slow;
}

void P_FocusClock (double scale)
{
    clock = scale < 0.05 ? 0.05 : scale > 1 ? 1 : scale;
}

void P_FocusReborn (player_t* player)
{
    player->focus = FOCUS_MOST;
    player->focusasked = 0;
    player->focusstep = 0;
}

void P_FocusKilled (mobj_t* source, mobj_t* target)
{
    player_t*	p;

    if (!P_FocusRules () || !source || !source->player || !(target->flags & MF_COUNTKILL))
	return;
    p = source->player;
    // Clean: his shot found the head this tic (p_heads.c), or he is sliding or
    // still in a shootslide's slow motion.
    p->focus += target->headtic == leveltime || P_Sliding (p) || p->focusasked > 0
	? FOCUS_CHARGE : FOCUS_CHARGE/2;
    if (p->focus > FOCUS_MOST)
	p->focus = FOCUS_MOST;
}

void P_FocusShootslide (player_t* player)
{
    if (!P_FocusRules () || player != &players[consoleplayer])
	return;
    // Already slowed by hand: a slide, and the hand's meter runs on.
    if (hand)
	return;
    if (P_FocusCosts ())
    {
	if (player->focus <= 0)
	    return;
	player->focus -= player->focus < FOCUS_CHARGE ? player->focus : FOCUS_CHARGE;
    }
    player->focusasked = FOCUS_SLIDE;
}

void P_FocusTic (player_t* player)
{
    if (player != &players[consoleplayer])
	return;
    flowing = false;
    if (!P_FocusRules ())
	return;
    if (player->focusasked > 0)
	player->focusasked--;
    if (hand && P_FocusCosts () && player->focus > 0)
    {
	player->focus--;
	flowing = true;
    }
}

void P_FocusPsprites (player_t* player)
{
    state_t*	state = player->psprites[ps_weapon].state;
    boolean	switching;
    double	rate;
    int		steps;

    if (!P_FocusRules () || clock > 0.999 || player != &players[consoleplayer])
    {
	player->focusstep = 0;
	P_MovePsprites (player);
	return;
    }
    switching = state && (state->action.acp2 == (actionf_p2)A_Lower
			  || state->action.acp2 == (actionf_p2)A_Raise);
    rate = switching ? 1/clock : 1/sqrt (clock);
    player->focusstep += (int)(rate*1024 + 0.5);
    steps = player->focusstep >> 10;
    player->focusstep &= 1023;
    if (steps > MOSTSTEPS)
	steps = MOSTSTEPS;
    while (steps-- > 0)
	P_MovePsprites (player);
}
