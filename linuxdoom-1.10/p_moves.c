//
// The Authored player's legs: a crouch, a slide out of a run, a low jump.
//
// Crouched he is half as tall and half as fast, and fits where half a man
// fits; he stands again where there is room to. The crouch key pressed at a
// run is a slide, as other shooters have taught hands to expect it: a burst
// well past his run that holds for the first stretch and then drags off, a
// second or so on a flat floor; the keys have no say in where he goes (his
// view is free: the kick, p_kick.c, wants it), and it ends at whatever
// stops him. A kick out of it that lands hands its speed to what it hit
// (P_SlideGiven): he is left with a quarter, still down. What makes it read as a slide is as much what stops: no steps,
// no bob (P_SlidingBody: the stride holds), his eye near the floor, the
// view tipped and widened (P_SlideView). The jump is a hop about a step and
// a half high: Doom's maps were built for feet on the floor.
//

#include <math.h>
#include <stdlib.h>

#include "doomdef.h"
#include "d_event.h"
#include "doomstat.h"
#include "p_local.h"
#include "p_derp.h"
#include "p_phobos.h"
#include "p_moves.h"
#include "p_focus.h"

#define UNIT(x)		((double)(x)/FRACUNIT)
#define FIX(x)		((fixed_t)((x)*FRACUNIT))

// Crouched: this much shorter, and his eye this much lower; down or up by
// DUCK_STEP a tic.
#define DUCK_MOST	(28*FRACUNIT)
#define DUCK_EYE	(21*FRACUNIT)
#define DUCK_STEP	(6*FRACUNIT)
// The slide: with the keys pushed past a walk's, from this speed over the
// floor (units a tic, as he went this last tic: Authored's run is about 9,
// its walk under 5; Doom's own, 16 and 8), pushed off by SLIDE_PUSH but
// no faster than SLIDE_MOST, keeping SLIDE_KEEP of it a tic for
// SLIDE_GLIDE tics and SLIDE_DRAG after, over below SLIDE_LEAST;
// SLIDE_REST tics before the next.
#define SLIDE_TICS	36
#define SLIDE_FROM	6.0
#define SLIDE_PUSH	1.8
#define SLIDE_MOST	20.0
#define SLIDE_GLIDE	14
#define SLIDE_DRAG	0.94
// Sliding, his eye is this much lower still, and he is down at once.
#define SLIDE_EYE	(6*FRACUNIT)
#define SLIDE_STEP	(10*FRACUNIT)
// A slide over, he stays down this long before he stands (the crouch key
// let go): a moment, or after a kick out of it long enough to get the shot
// off from down there (the user, 2026-11-14).
#define STAY_DOWN	8
// What a landed kick leaves him of his slide, and gives what it hit.
#define KICK_LEAVES	0.25
#define KICK_GIVES	1.15
// The kick lands 3 tics after the key (p_kick.c): a slide over in them (most
// often he ran into the man he kicked, his slide stopped by the body) still
// gives what it had, or the man went straight up (the user, 2026-11-16).
#define KICK_GRACE	4
#define STAY_KICKED	28
// The view's share of the slide: this much of the way there a tic.
#define VIEW_EASE	0.4f
#define WALK_FORWARD	0x19
#define WALK_SIDE	0x18
#define SLIDE_KEEP	0.985
#define SLIDE_LEAST	6.0
#define SLIDE_REST	18
// The hop: up 32 units.
#define JUMP_PUSH	(15*FRACUNIT/2)

typedef struct
{
    mobj_t*	body;		// whose
    fixed_t	duck;		// how far down he is
    boolean	ducked;		// his height is ours to give back
    int		slide;		// tics of it left
    int		rest;
    int		stay;		// tics he stays down after it
    boolean	kicked;		// he kicked out of this one
    double	vx, vy;		// the slide's speed (kept once it is over)
    int		endtic;		// when the last one ended (P_SlideGiven's grace)
    fixed_t	x, y;		// where he was a tic ago
    boolean	crouchHeld, jumpHeld, shootHeld;
    float	view, lastView;	// how far the view is in the slide, 0 to 1, now and a tic ago
} moves_t;

static moves_t	moves[MAXPLAYERS];

boolean P_MovesOn (void)
{
    return derpauthored && !netgame && !P_Phobos () && !P_Flying ();
}

// player's legs, fresh for a new body (a new level, a new life).
static moves_t* Moves (player_t* player)
{
    moves_t*	m = &moves[player - players];

    if (m->body != player->mo)
    {
	moves_t	fresh = {0};
	*m = fresh;
	m->body = player->mo;
    }
    return m;
}

static void EndSlide (moves_t* m)
{
    m->slide = 0;
    m->endtic = leveltime;
    m->rest = SLIDE_REST;
    m->stay = m->kicked ? STAY_KICKED : STAY_DOWN;
    m->kicked = false;
}

static void Slide (moves_t* m, mobj_t* mo, boolean grounded, boolean jump)
{
    double	speed = sqrt (m->vx*m->vx + m->vy*m->vy);
    double	went = sqrt (UNIT (mo->x - m->x)*UNIT (mo->x - m->x) + UNIT (mo->y - m->y)*UNIT (mo->y - m->y));

    // Stopped (a wall, a body): he went less than half as far as he meant to.
    if (m->slide < SLIDE_TICS && went < speed/2)
    {
	EndSlide (m);
	return;
    }
    m->vx *= SLIDE_TICS - m->slide < SLIDE_GLIDE ? SLIDE_KEEP : SLIDE_DRAG;
    m->vy *= SLIDE_TICS - m->slide < SLIDE_GLIDE ? SLIDE_KEEP : SLIDE_DRAG;
    if (!--m->slide || speed < SLIDE_LEAST || jump)
    {
	EndSlide (m);
	return;
    }
    // Doom's friction would have it over in a few tics: on the floor his
    // speed is the slide's. Off a ledge it is his fall's.
    if (grounded)
    {
	mo->momx = FIX (m->vx);
	mo->momy = FIX (m->vy);
    }
}

void P_MovesLegs (player_t* player, boolean grounded, int* forward, int* side)
{
    mobj_t*	mo = player->mo;
    ticcmd_t*	cmd = &player->cmd;
    moves_t*	m;
    boolean	special = (cmd->buttons & BT_SPECIAL) != 0;
    boolean	crouch = !special && (cmd->buttons2 & BT2_CROUCH), jump = !special && (cmd->buttons & BT_JUMP);
    boolean	shoot = !special && (cmd->buttons2 & BT2_SHOOTSLIDE);
    boolean	crouchPress, jumpPress, shootPress;
    fixed_t	want, low;

    if (!P_MovesOn ())
	return;
    m = Moves (player);
    crouchPress = crouch && !m->crouchHeld;
    jumpPress = jump && !m->jumpHeld;
    shootPress = shoot && !m->shootHeld;
    m->crouchHeld = crouch;
    m->jumpHeld = jump;
    m->shootHeld = shoot;
    if (m->rest)
	m->rest--;
    if (m->stay && !m->slide)
	m->stay--;
    if (jumpPress)
	m->stay = 0;

    // The shootslide is the same slide out of a run, into slow motion (p_focus.c).
    if ((crouchPress || shootPress) && grounded && !m->slide && !m->rest
	&& (abs (cmd->forwardmove) > WALK_FORWARD || abs (cmd->sidemove) > WALK_SIDE))
    {
	// His momentum is what friction left of it (well under half, Authored's
	// grip): how fast he is going is how far he went.
	double	vx = UNIT (mo->x - m->x), vy = UNIT (mo->y - m->y);

	double	speed = sqrt (vx*vx + vy*vy), push = SLIDE_PUSH;

	if (speed >= SLIDE_FROM)
	{
	    if (speed*push > SLIDE_MOST)
		push = speed > SLIDE_MOST ? 1 : SLIDE_MOST/speed;
	    m->slide = SLIDE_TICS;
	    m->kicked = false;
	    m->vx = vx*push;
	    m->vy = vy*push;
	    if (shootPress)
		P_FocusShootslide (player);
	}
    }
    if (m->slide)
	Slide (m, mo, grounded, jumpPress);
    m->x = mo->x;
    m->y = mo->y;
    m->lastView = m->view;
    m->view += ((m->slide ? 1 : 0) - m->view)*VIEW_EASE;
    if (!m->slide && m->view < 0.01f)
	m->view = 0;

    // Up he goes, where there is room for all of him.
    if (jumpPress && grounded && mo->ceilingz - mo->z >= mo->info->height + JUMP_PUSH)
	mo->momz = JUMP_PUSH;

    want = crouch || m->slide || m->stay ? DUCK_MOST : 0;
    if (m->duck < want)
    {
	fixed_t	step = m->slide ? SLIDE_STEP : DUCK_STEP;

	m->duck = m->duck + step < want ? m->duck + step : want;
    }
    else if (m->duck > want)
	m->duck = m->duck - DUCK_STEP > want ? m->duck - DUCK_STEP : want;
    // No room to stand: he stays as far down as the ceiling keeps him.
    low = mo->info->height - (mo->ceilingz - mo->z);
    if (low > DUCK_MOST)
	low = DUCK_MOST;
    if (m->duck < low)
	m->duck = low;
    if (m->duck || m->ducked)
	mo->height = mo->info->height - m->duck;
    m->ducked = m->duck != 0;

    if (m->slide)
	*forward = *side = 0;
    else if (m->duck)
    {
	*forward /= 2;
	*side /= 2;
    }
}

fixed_t P_MovesDuck (player_t* player)
{
    moves_t*	m = &moves[player - players];

    if (!P_MovesOn () || m->body != player->mo)
	return 0;
    return FixedMul (m->duck, FixedDiv (DUCK_EYE, DUCK_MOST)) + (fixed_t)(SLIDE_EYE*m->view);
}

void P_SlideGiven (player_t* player, fixed_t* x, fixed_t* y)
{
    moves_t*	m = &moves[player - players];

    *x = *y = 0;
    if (m->body != player->mo || (!m->slide && leveltime - m->endtic > KICK_GRACE))
	return;
    *x = FIX (m->vx*KICK_GIVES);
    *y = FIX (m->vy*KICK_GIVES);
}

void P_MovesKicked (player_t* player)
{
    moves_t*	m = &moves[player - players];

    if (m->body != player->mo)
	return;
    // Over in the kick's swing (KICK_GRACE): he is stopped already, only the longer stay down is left.
    if (!m->slide)
    {
	if (leveltime - m->endtic <= KICK_GRACE)
	    m->stay = STAY_KICKED;
	return;
    }
    m->kicked = true;
    m->vx *= KICK_LEAVES;
    m->vy *= KICK_LEAVES;
    player->mo->momx = FIX (m->vx);
    player->mo->momy = FIX (m->vy);
    // Where he was is no measure of this tic's going: not "stopped by a wall".
    m->x = player->mo->x - FIX (m->vx);
    m->y = player->mo->y - FIX (m->vy);
}

boolean P_SlidingBody (const mobj_t* mo)
{
    return mo->player && P_Sliding (mo->player);
}

float P_SlideView (player_t* player, float fraction)
{
    moves_t*	m = &moves[player - players];

    if (!P_MovesOn () || m->body != player->mo)
	return 0;
    return m->lastView + (m->view - m->lastView)*fraction;
}

boolean P_Sliding (player_t* player)
{
    moves_t*	m = &moves[player - players];

    return P_MovesOn () && m->body == player->mo && m->slide > 0;
}

boolean P_SlideKicks (player_t* player)
{
    moves_t*	m = &moves[player - players];

    return P_Sliding (player) || (P_MovesOn () && m->body == player->mo && m->endtic > 0 && leveltime - m->endtic <= 1);
}
