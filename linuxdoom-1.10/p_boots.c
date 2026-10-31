//
// Grav boots (Physically accurate Phobos, F4's grav boots preset; key_boots switches
// them off and on): the player walks on floors and ceilings the way he walks in Doom. The jump key pushes him
// off the way he looks, and the boots grab the floor or ceiling he comes to.
//
// On a ceiling he stands upside down: the renderer rolls the view over about
// where he looks (Game_BootsUpside), so he keeps looking where he looked, and
// once it is over his turning and strafing go as they look (P_ViewUpside).
// Flying across a room it rolls as he passes its middle. Walls are left as Doom
// has them: thin lines with nothing to stand on.
//

#include <math.h>

#include "doomdef.h"
#include "d_event.h"
#include "doomstat.h"
#include "p_local.h"
#include "p_phobos.h"
#include "r_state.h"

enum { OFF_SURFACE, ON_FLOOR, ON_CEILING };

#define UNIT(x)		((double)(x)/FRACUNIT)
#define FIX(x)		((fixed_t)((x)*FRACUNIT))

// The jump: the legs' whole push, 4 m/s (P_Jump's), the way he looks; never
// flatter to what he stands on than JUMP_LEAST (a push off it, not along).
#define JUMP_SPEED	3.66
#define JUMP_LEAST	0.3
// How far the boots reach for a floor or ceiling: a step.
#define BOOT_REACH	(24*FRACUNIT)
// His eye catches up, turned over onto a ceiling or back, by EYE_CATCH of the way a tic.
#define EYE_CATCH	0.2
// Doom's walking (p_mobj.c).
#define STOPSPEED	(0x1000/65536.0)
#define FRICTION	(0xe800/65536.0)

typedef struct
{
    boolean	off;		// switched off (G; on with the grav boots preset)
    int		surface;
    boolean	upside;		// the view's to be upside down (on a ceiling, or near one)
    boolean	headDown;	// his eye below his middle (P_BootsEye)
    boolean	jumpHeld;
    fixed_t	lag;		// how far his eye is still behind, up or down
    mobj_t*	body;		// whose
} boots_t;

static boots_t	boots[MAXPLAYERS];
// The renderer's roll of the view, past half way over (P_BootsRolled).
static boolean	rolled;

// Whether b is worn: the grav boots preset (F4 Phobos), not switched off.
static boolean Worn (boots_t* b)
{
    return phobosplayer == PHOBOS_BOOTS && !b->off;
}

// player's boots; NULL for one not in the game (a test's).
static boots_t* BootsOf (player_t* player)
{
    return player >= players && player < players + MAXPLAYERS ? &boots[player - players] : NULL;
}


// What the boots hold to this tic: the floor or ceiling he came to (not one he is
// leaving); nothing once the one he stood on drops (rises) away past a step.
static void Contact (boots_t* b, mobj_t* mo)
{
    if (b->surface == OFF_SURFACE)
    {
	if (mo->z <= mo->floorz && mo->momz <= 0)
	    b->surface = ON_FLOOR;
	else if (mo->z + mo->height >= mo->ceilingz && mo->momz >= 0 && !P_OpenSky (mo))
	    b->surface = ON_CEILING;	// never the sky: he goes on up through it
	return;
    }
    if (b->surface == ON_CEILING && P_OpenSky (mo))
    {
	b->surface = OFF_SURFACE;	// walked on under the sky
	return;
    }
    if (b->surface == ON_FLOOR ? mo->z - mo->floorz > BOOT_REACH
	: mo->ceilingz - (mo->z + mo->height) > BOOT_REACH)
    {
	// Off the edge: the boots' pull (Doom's gravity, up or down) lets go with them.
	b->surface = OFF_SURFACE;
	mo->momz = 0;
    }
}

// Walking on a ceiling as Doom walks on a floor: thrust the way he faces (his strafing
// already turned about with the view, p_user.c) and Doom's friction.
static void WalkCeiling (player_t* player)
{
    mobj_t*	mo = player->mo;
    ticcmd_t*	cmd = &player->cmd;
    double	a = (double)mo->angle*(6.283185307179586/4294967296.0);
    double	thrust = (P_UseResponsiveMovement () ? 2048*8/3 : 2048)/65536.0;
    double	fwd = cmd->forwardmove*thrust, side = cmd->sidemove*thrust;
    double	vx = UNIT(mo->momx), vy = UNIT(mo->momy);
    boolean	keys = cmd->forwardmove || cmd->sidemove;
    double	friction = P_UseResponsiveMovement () ? (keys ? 0.75 : 0.625) : FRICTION;

    if (P_UseResponsiveMovement ())
    {
	// Diagonals as on the floor (P_DiagonalKeep).
	double	keep = P_DiagonalKeep (player);

	fwd *= keep;
	side *= keep;
    }
    if (!keys && sqrt (vx*vx + vy*vy) < STOPSPEED)
	vx = vy = 0;
    vx = vx*friction + fwd*cos (a) + side*sin (a);
    vy = vy*friction + fwd*sin (a) - side*cos (a);
    mo->momx = FIX(vx);
    mo->momy = FIX(vy);
    if (mo->momz < 0)
	mo->momz = 0;	// the boots hold him up to it
    if (keys && mo->state == &states[S_PLAY])
	P_SetMobjState (mo, S_PLAY_RUN1);
}

// The jump key: off the floor or ceiling, the way he looks (Doom's view: his facing
// and the mouselook's pitch).
static void Jump (player_t* player, boots_t* b)
{
    mobj_t*	mo = player->mo;
    double	a = (double)mo->angle*(6.283185307179586/4294967296.0), p = freeaim ? tourpitch : 0;
    double	v[3] = {cos (a)*cos (p), sin (a)*cos (p), sin (p)};
    double	out = b->surface == ON_FLOOR ? 1 : -1, d = v[2]*out;

    if (d < JUMP_LEAST)
    {
	v[2] += out*(JUMP_LEAST - d);
	d = sqrt (v[0]*v[0] + v[1]*v[1] + v[2]*v[2]);
	v[0] /= d; v[1] /= d; v[2] /= d;
    }
    mo->momx += FIX(v[0]*JUMP_SPEED);
    mo->momy += FIX(v[1]*JUMP_SPEED);
    mo->momz += FIX(v[2]*JUMP_SPEED);
    b->surface = OFF_SURFACE;
}

boolean P_BootsMove (player_t* player)
{
    mobj_t*	mo = player->mo;
    boots_t*	b = BootsOf (player);
    ticcmd_t*	cmd = &player->cmd;
    boolean	down = (cmd->buttons & (BT_SPECIAL|BT_JUMP)) == BT_JUMP, press;

    if (!b || !P_Phobos () || !mo || player != &players[consoleplayer])
	return false;
    press = down && !b->jumpHeld;
    b->jumpHeld = down;
    if (b->body != mo)
    {
	b->body = mo;
	b->surface = OFF_SURFACE;
	b->upside = false;
	b->lag = 0;
    }
    b->lag -= (fixed_t)(b->lag*EYE_CATCH);
    if (!Worn (b) || mo->health <= 0)
    {
	b->surface = OFF_SURFACE;
	b->upside = false;
	return false;
    }

    Contact (b, mo);
    if (b->surface != OFF_SURFACE)
    {
	b->upside = b->surface == ON_CEILING;
	if (press)
	{
	    Jump (player, b);
	    return true;
	}
	// On the floor Doom walks him (P_Booted); on a ceiling the same, upside down.
	if (b->surface == ON_FLOOR)
	    return false;
	WalkCeiling (player);
	return true;
    }
    // Aloft: the view rolls over as he rises past the middle of the room, back as he
    // comes down past it; the suit's thrusters (run), a kick off a wall (Use), his guns' spin.
    {
	fixed_t	middle = mo->z + mo->height/2, half = mo->floorz/2 + mo->ceilingz/2;
	if (mo->momz > 0 && middle > half && !P_OpenSky (mo))
	    b->upside = true;
	if (mo->momz < 0 && middle < half)
	    b->upside = false;
    }
    P_Jump (player, false);
    P_Jetpack (player);
    P_PlayerSpin (player);
    return true;
}

void P_BootsEye (player_t* player)
{
    boots_t*	b = BootsOf (player);
    mobj_t*	mo = player->mo;
    fixed_t	z;

    if (!b || player != &players[consoleplayer] || !P_BootsWorn (player) || mo->health <= 0)
    {
	if (b)
	    b->headDown = false;
	return;
    }
    // Upside down his eye is Doom's height below his top (on a ceiling, below it). Doom's
    // squat on a landing wears off here (as in P_CalcHeight, whose walking path the
    // boots leave out on a ceiling). Turned over (or back) his eye stays where it was
    // and follows.
    if (b->upside != b->headDown)
    {
	fixed_t	other = mo->z + mo->height - player->viewheight;
	b->lag += b->upside ? player->viewz - other : other - player->viewz;
	b->headDown = b->upside;
    }
    if (b->upside)
    {
	player->viewheight += player->deltaviewheight;
	if (player->viewheight >= VIEWHEIGHT)
	{
	    player->viewheight = VIEWHEIGHT;
	    player->deltaviewheight = 0;
	}
	if (player->deltaviewheight)
	    player->deltaviewheight += FRACUNIT/4;
	player->viewz = mo->z + mo->height - player->viewheight;
    }
    if (!b->lag)
	return;
    z = player->viewz + b->lag;
    if (z > mo->ceilingz - 4*FRACUNIT)
	z = mo->ceilingz - 4*FRACUNIT;
    if (z < mo->floorz + 4*FRACUNIT)
	z = mo->floorz + 4*FRACUNIT;
    player->viewz = z;
}

boolean P_ToggleBoots (player_t* player)
{
    boots_t*	b = BootsOf (player);

    if (!b || !P_Phobos () || !player->mo || player->mo->health <= 0)
	return false;
    if (phobosplayer != PHOBOS_BOOTS)
    {
	player->message = "NO GRAV BOOTS: SEE F4, PHOBOS";
	return true;
    }
    b->off = !b->off;
    b->surface = OFF_SURFACE;
    player->message = b->off ? "GRAV BOOTS OFF" : "GRAV BOOTS ON";
    return true;
}

boolean P_BootsWorn (player_t* player)
{
    boots_t*	b = BootsOf (player);

    return b && P_Phobos () && Worn (b) && player->mo == b->body;
}

int P_BootsSurface (player_t* player)
{
    boots_t*	b = BootsOf (player);

    if (!b || !P_Phobos () || !Worn (b) || player->mo != b->body || !player->mo || player->mo->health <= 0)
	return 0;
    return b->surface;
}

boolean P_BootsUpside (void)
{
    player_t*	player = &players[consoleplayer];

    return P_BootsWorn (player) && player->mo->health > 0 && !P_Flying () && boots[consoleplayer].upside;
}

void P_BootsRolled (boolean over)
{
    rolled = over;
}

boolean P_ViewUpside (void)
{
    return P_ViewFlipped () != (rolled && P_Phobos ());
}
