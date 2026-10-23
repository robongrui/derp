//
// Physically accurate Phobos: gravity, mag boots, friction, knockback and
// recoil for DOOM's first episode taken literally (see p_phobos.h).
//

#include <math.h>
#include <string.h>

#include "doomdef.h"
#include "d_event.h"
#include "doomstat.h"
#include "m_random.h"
#include "p_local.h"
#include "p_phobos.h"
#include "r_sky.h"
#include "r_state.h"
#include <stdio.h>

boolean		phobosphysics;

extern boolean	freeaim;
extern float	tourpitch;

// 0.0057 m/s^2 at 32 units a metre and 35 tics a second: 1.49e-4 units a
// tic a tic, 9.76 in fixed point.
#define PHOBOS_GRAVITY	10
// Sliding friction mu*g with mu 0.5: a body shoved at 0.19 units a tic (a
// zombieman's shotgun kill) slides on for a minute.
#define PHOBOS_FRICTION	5
// What a point of damage carries: 0.3 newton-seconds (a pistol bullet's ~3
// for its ~10 damage), as units a tic on a kilogram: 0.3 * 32/35.
#define PHOBOS_THRUST	17976
// A newton-second on a kilogram, as units a tic: 32/35.
#define PHOBOS_IMPULSE	59918
// How far a boot reaches for the floor: the highest step Doom walks down.
#define BOOT_REACH	(24*FRACUNIT)
// A metre a second, in units a tic.
#define MPS		(32.0f/TICRATE)
// The feet (or hands) push along only as far as friction lets them (mu 0.5,
// PHOBOS_FRICTION): with no weight, the contact itself is all that presses them on,
// so a push leaning more than 27 degrees off square slips. Every hop goes up as well.
#define FOOT_MU		0.5


boolean P_Phobos (void)
{
    return phobosphysics && gamemode != commercial && gameepisode == 1
	&& !demoplayback && !demorecording && !netgame;
}


// The barrels on Phobos: what goes off at a bullet with no air to burn in carries its
// own oxygen, a monopropellant such as the hydrazine a base's thrusters run on, and a
// touchy one. 200 litres of it and the steel drum: 225 kg (DOOM's 100 drifted off like
// foam). A crash that takes BARREL_JOLT (10 m/s) off its speed at once sets it off: a drum
// of dangerous liquid must survive a 1.8 m drop (the UN packaging test, 6 m/s), so a knock
// takes well past that; a body of air flinging it across a room into a wall does.
#define BARREL_MASS	225
#define BARREL_JOLT	(9.14*FRACUNIT)
static int	barrelMass = -1;

float P_Mass (mobj_t* mo)
{
    // Pickups as they would weigh (DOOM gives everything 100): what is in them and
    // their box or casing, in kilograms; by the map number, which DEHACKED leaves alone.
    switch (mo->info->doomednum)
    {
      case 2007: return 0.25f;	// clip: 10 rounds and the magazine
      case 2048: return 1.2f;	// box of 50 rounds
      case 2008: return 0.2f;	// 4 shells, 45 g each
      case 2049: return 1.2f;	// box of 20 shells
      case 2010: return 2.5f;	// a rocket
      case 2046: return 12;	// box of 5 rockets
      case 2047: return 0.5f;	// energy cell
      case 17:   return 5;	// cell pack
      case 8:    return 3;	// backpack
      case 2001: return 3.5f;	// shotgun
      case 82:   return 3.2f;	// super shotgun
      case 2002: return 15;	// chaingun
      case 2003: return 7;	// rocket launcher
      case 2004: return 8;	// plasma rifle
      case 2006: return 25;	// BFG 9000
      case 2005: return 6;	// chainsaw
      case 2018: return 8;	// green armour
      case 2019: return 12;	// blue armour
      case 2014: return 0.1f;	// health bonus
      case 2015: return 0.5f;	// armour bonus, a helmet
      case 5: case 6: case 13: return 0.05f;	// keycards
      case 38: case 39: case 40: return 1;	// skull keys
      case 2011: return 0.2f;	// stimpack
      case 2012: return 1.5f;	// medikit
      case 2025: return 4;	// radiation suit
      case 2026: return 0.3f;	// computer map
      case 2045: return 0.8f;	// light amplification visor
      case 2013: case 2022: case 2023: case 2024: case 83: return 1;	// the spheres, berserk
      default: break;
    }
    return mo->info->mass > 0 ? (float)mo->info->mass : 100;
}

void P_PhobosMasses (void)
{
    if (barrelMass < 0)
	barrelMass = mobjinfo[MT_BARREL].mass;	// DOOM's own, or a DEHACKED patch's
    mobjinfo[MT_BARREL].mass = P_Phobos () ? BARREL_MASS : barrelMass;
}

// Window glass takes about what a pistol round brings to break (DOOM's panes go at
// bullets): 500 J of a body's blow straight into it. A 100 kg body at 3.2 m/s, a barrel
// at 2.1, a body the air carries out of a breached room at far more.
#define GLASS_BREAK	500.0f

fixed_t P_Strike (mobj_t* mo, line_t* line, fixed_t hitx, fixed_t hity)
{
    float	ex, ey, len, nx, ny, vn, v, m, left;

    if (!P_Phobos () || !line || !R_GlassStruck || !P_GlassWhole (line)
	|| (mo->flags & (MF_MISSILE|MF_NOCLIP)))
	return -1;
    ex = (float)(line->v2->x - line->v1->x)/FRACUNIT; ey = (float)(line->v2->y - line->v1->y)/FRACUNIT;
    len = sqrtf (ex*ex + ey*ey);
    if (len < 1)
	return -1;
    // Its speed across the pane (units a tic, along the pane's normal), in metres a second.
    nx = ey/len; ny = -ex/len;
    vn = ((float)hitx*nx + (float)hity*ny)/FRACUNIT;
    if (vn < 0)
    {
	nx = -nx; ny = -ny; vn = -vn;
    }
    v = vn/MPS;
    m = P_Mass (mo);
    if (0.5f*m*v*v < GLASS_BREAK)
	return -1;
    R_GlassStruck (line, mo->x, mo->y, mo->z + mo->height/2, hitx, hity);
    // Through it, the poorer by what it took to break: the speed along the pane kept,
    // across it what is left of the blow.
    left = sqrtf (v*v - 2*GLASS_BREAK/m)*MPS;
    mo->momx = hitx - (fixed_t)((vn - left)*nx*FRACUNIT);
    mo->momy = hity - (fixed_t)((vn - left)*ny*FRACUNIT);
    return (fixed_t)((vn - left)*FRACUNIT);
}

// What the draft added to a barrel's speed this tic (P_Windblown): held against a wall
// by the wind, the wall takes that off again every tic, a steady press and no knock.
#define MOST_BLOWN	32
static struct { mobj_t* mo; fixed_t push; int tic; } blown[MOST_BLOWN];

void P_Windblown (mobj_t* mo, fixed_t push)
{
    int		i, free = 0;

    if (mo->type != MT_BARREL)
	return;
    for (i = 0; i < MOST_BLOWN; ++i)
    {
	if (blown[i].mo == mo)
	{
	    free = i;
	    break;
	}
	if (blown[i].tic != leveltime)
	    free = i;
    }
    blown[free].mo = mo; blown[free].push = push; blown[free].tic = leveltime;
}

void P_Jolt (mobj_t* mo, fixed_t lost)
{
    int		i;

    if (!P_Phobos () || mo->type != MT_BARREL || mo->health <= 0 || (mo->flags & MF_CORPSE))
	return;
    if (lost < 0)
	lost = -lost;
    // Only the speed it brought: what the wind added this tic presses, it doesn't knock.
    for (i = 0; i < MOST_BLOWN; ++i)
	if (blown[i].mo == mo && blown[i].tic == leveltime)
	    lost -= blown[i].push;
    if (lost >= BARREL_JOLT)
	P_DamageMobj (mo, NULL, NULL, mo->health);
}


// The former humans (Phobos) are zombies: no mind to learn to move in a moon's pull,
// no boots. They walk as they did on Earth, and each step throws them off the floor
// (P_BodyStep). Aloft nothing turns a body but a push off its middle: the feet's
// behind it, a gun's kick above it, a bump at one end. So they tumble, slow and
// free, and shoot wherever they point. A body is a rod for this: 100 kg, 1.8 m,
// I = m L^2/12 = 27 kg m^2 about its middle, its ends 0.9 m out. The barrels tumble
// too once something sets them moving: a drum 0.9 m tall, 0.29 m round, 225 kg,
// I = m (3 r^2 + h^2)/12 = 20 kg m^2.
#define BODY_I		27.0f
#define BODY_HALF	0.9f
#define DRUM_HALF	0.45f
#define DRUM_I		(225.0f*(3*0.29f*0.29f + 0.9f*0.9f)/12)
// A knock (a wall, a window frame, a pane) never lands square on the middle: rims and
// corners catch, up to KNOCK_ARM off it.
#define KNOCK_ARM	0.1f
// Carried over a sill or under a lintel, the rim catches the edge and trips: a share of
// the speed there (a glancing catch, the edge doesn't stop it) at the edge's arm.
#define CATCH		0.3f
// The buffeting of a gale on a blunt body: its shove across the body's length, as a
// share of the drag (P_BodyDraft).
#define BUFFET		0.03f
#define TIC_S		(1.0f/TICRATE)
// A step: the legs straighten along the body at 0.4 m/s, a third of it leaning
// into the stride; the foot pushes from 0.2 m behind its middle.
#define STEP_SPEED	0.4f
#define STEP_LEAN	0.35f
#define STEP_BEHIND	0.2f
// A gun is held 0.3 m above the middle and 0.3 m out in front; the arms swing it up
// to 60 degrees off the way the body faces.
#define GUN_UP		0.3f
#define GUN_OUT		0.3f
#define ARM_REACH	1.047f
// Turning without anything to push on: flailing, as a falling cat rights itself (a
// reflex, no mind needed), it twists about its long axis, where it turns easiest, up
// to 30 degrees a second. That changes which way it faces, not how it spins.
#define TWIST		(0.5236f*TIC_S)
#define MOST_BODIES	64
typedef struct { mobj_t* mo; float f[3], u[3], pu[3], w[3], aim[3], half, inertia; boolean seen; } body_t;
static body_t	bodies[MOST_BODIES];

static float Dot (const float* a, const float* b) { return a[0]*b[0] + a[1]*b[1] + a[2]*b[2]; }
static void Cross (const float* a, const float* b, float* out)
{
    float c[3] = { a[1]*b[2] - a[2]*b[1], a[2]*b[0] - a[0]*b[2], a[0]*b[1] - a[1]*b[0] };
    memcpy (out, c, sizeof c);
}
static void Normalize (float* v)
{
    float len = sqrtf (Dot (v, v));
    if (len > 1e-6f) { v[0] /= len; v[1] /= len; v[2] /= len; }
}
// v turned about the unit axis by angle (Rodrigues).
static void Turn (float* v, const float* axis, float angle)
{
    float c = cosf (angle), s = sinf (angle), k = Dot (axis, v)*(1 - c), x[3];
    int i;
    Cross (axis, v, x);
    for (i = 0; i < 3; ++i)
	v[i] = v[i]*c + x[i]*s + axis[i]*k;
}

// A direction in radians as a Doom angle (wrapped first: a negative one cast straight
// to the unsigned angle is undefined, and came out east).
static angle_t ToAngle (double radians)
{
    double	turns = radians/6.283185307179586;

    turns -= floor (turns);
    return turns >= 1 ? 0 : (angle_t)(turns*4294967296.0);
}

boolean P_Brainless (mobj_t* mo)
{
    return P_Phobos () && mo->health > 0 && !(mo->flags & MF_CORPSE)
	&& (mo->type == MT_POSSESSED || mo->type == MT_SHOTGUY || mo->type == MT_CHAINGUY);
}

static boolean Drum (mobj_t* mo)
{
    return P_Phobos () && mo->type == MT_BARREL && mo->health > 0 && !(mo->flags & MF_CORPSE);
}

static body_t* BodyOf (mobj_t* mo)
{
    int		i, free = -1;
    double	a;

    for (i = 0; i < MOST_BODIES; ++i)
    {
	if (bodies[i].mo == mo)
	    return &bodies[i];
	if (!bodies[i].mo && free < 0)
	    free = i;
    }
    // A barrel only once something sets it moving: standing still it needs none.
    if (free < 0 || !(P_Brainless (mo)
		      || (Drum (mo) && (mo->momx || mo->momy || mo->momz || mo->z > mo->floorz))))
	return NULL;
    // Standing as the level has it: upright, facing its way, still.
    memset (&bodies[free], 0, sizeof bodies[free]);
    bodies[free].mo = mo;
    bodies[free].half = Drum (mo) ? DRUM_HALF : BODY_HALF;
    bodies[free].inertia = Drum (mo) ? DRUM_I : BODY_I;
    a = (double)mo->angle*(6.283185307179586/4294967296.0);
    bodies[free].f[0] = (float)cos (a); bodies[free].f[1] = (float)sin (a);
    bodies[free].u[2] = bodies[free].pu[2] = 1;
    bodies[free].seen = true;
    return &bodies[free];
}

// An impulse (newton-seconds, world axes) on the body at r (metres from its middle):
// its speed, and the turn of what misses the middle.
static void Impulse (mobj_t* mo, body_t* b, const float* j, const float* r)
{
    float	m = P_Mass (mo), t[3];
    int		i;

    mo->momx += (fixed_t)(j[0]/m*MPS*FRACUNIT);
    mo->momy += (fixed_t)(j[1]/m*MPS*FRACUNIT);
    mo->momz += (fixed_t)(j[2]/m*MPS*FRACUNIT);
    Cross (r, j, t);
    for (i = 0; i < 3; ++i)
	b->w[i] += t[i]/b->inertia*TIC_S;
}

// The way from mo's middle to its target's.
static void ToTarget (mobj_t* mo, float* t)
{
    t[0] = (float)(mo->target->x - mo->x)/FRACUNIT;
    t[1] = (float)(mo->target->y - mo->y)/FRACUNIT;
    t[2] = (float)((mo->target->z + mo->target->height/2) - (mo->z + mo->height/2))/FRACUNIT;
    Normalize (t);
}

void P_BodyThink (mobj_t* mo)
{
    body_t*	b;
    float	spin, axis[3], d;
    int		i;

    if (!(P_Brainless (mo) || Drum (mo)) || !(b = BodyOf (mo)))
	return;
    b->seen = true;
    memcpy (b->pu, b->u, sizeof b->pu);
    spin = sqrtf (Dot (b->w, b->w));
    if (spin > 1e-7f)
    {
	for (i = 0; i < 3; ++i)
	    axis[i] = b->w[i]/spin;
	Turn (b->f, axis, spin);
	Turn (b->u, axis, spin);
	Normalize (b->u);
	d = Dot (b->f, b->u);
	for (i = 0; i < 3; ++i)
	    b->f[i] -= d*b->u[i];
	Normalize (b->f);
    }
    // Lying or pressed against a floor or ceiling, the rub of it stops the turning: a
    // zombie's limbs catch; a drum's rim, at a weight of a newton or so, barely.
    if (mo->z <= mo->floorz || mo->z + mo->height >= mo->ceilingz)
	for (i = 0; i < 3; ++i)
	    b->w[i] *= Drum (mo) ? 0.97f : 0.85f;
    // A barrel come to rest settles, slowly in this pull, on its end or its side,
    // whichever it leans nearer; upright again, it needs no body.
    if (Drum (mo))
    {
	if (mo->z <= mo->floorz && Dot (b->w, b->w) < 1e-6f
	    && P_AproxDistance (mo->momx, mo->momy) < FRACUNIT/8 && !mo->momz)
	{
	    float side[3] = { b->u[0], b->u[1], 0 }, want[3], axis[3], ang;
	    if (b->u[2] < 0)
		for (i = 0; i < 3; ++i)
		    b->u[i] = -b->u[i];	// a drum has two ends
	    if (b->u[2] > 0.57f)
		want[0] = want[1] = 0, want[2] = 1;
	    else
	    {
		Normalize (side);
		memcpy (want, side, sizeof want);
	    }
	    Cross (b->u, want, axis);
	    ang = asinf (fminf (sqrtf (Dot (axis, axis)), 1));
	    if (ang < 0.005f && want[2] > 0.5f)
	    {
		b->mo = NULL;	// standing again
		return;
	    }
	    if (ang > 0.005f)
	    {
		Normalize (axis);
		Turn (b->u, axis, fminf (ang, 0.5236f*TIC_S));
		Turn (b->f, axis, fminf (ang, 0.5236f*TIC_S));
	    }
	}
	return;	// it keeps its facing: a drum has no front to turn
    }
    // Twisting round about its long axis toward its target.
    if (mo->target)
    {
	float t[3], side[3], turn;
	ToTarget (mo, t);
	d = Dot (t, b->u);
	for (i = 0; i < 3; ++i)
	    t[i] -= d*b->u[i];
	if (Dot (t, t) > 1e-4f)
	{
	    Normalize (t);
	    Cross (b->f, t, side);
	    turn = atan2f (Dot (side, b->u), Dot (b->f, t));
	    turn = turn > TWIST ? TWIST : turn < -TWIST ? -TWIST : turn;
	    Turn (b->f, b->u, turn);
	}
    }
    // Where it faces, as the game knows it (its sprite's view, its shots).
    if (b->f[0]*b->f[0] + b->f[1]*b->f[1] > 0.02f)
	mo->angle = ToAngle (atan2 (b->f[1], b->f[0]));
}

boolean P_BodyStep (mobj_t* mo)
{
    body_t*	b = BodyOf (mo);
    float	n[3] = { 0, 0, 0 }, d[3], j[3], r[3], vn, t[3], tl, along, m;
    int		i;

    if (!b)
	return false;
    if (mo->z <= mo->floorz && mo->momz <= 0)
	n[2] = 1;
    else if (mo->z + mo->height >= mo->ceilingz && mo->momz >= 0)
	n[2] = -1;	// upside down against a ceiling, it walks off that
    else
	return false;	// aloft: its legs walk on in the air
    if (Dot (b->u, n) < 0.5f)
	return false;	// not on its feet: they push off nothing
    // Feet planted, it can twist round toward where it means to go.
    if (n[2] > 0 && Dot (b->u, n) > 0.8f && mo->movedir < 8)
    {
	float want = mo->movedir*(3.14159265f/4), have = atan2f (b->f[1], b->f[0]);
	float turn = remainderf (want - have, 6.28318531f);
	Turn (b->f, n, turn);
	Turn (b->u, n, turn);
    }
    for (i = 0; i < 3; ++i)
	d[i] = b->u[i] + STEP_LEAN*b->f[i];
    Normalize (d);
    // As far along the surface as the foot grips: with no weight, the push itself
    // is all that presses it down.
    vn = STEP_SPEED*Dot (d, n);
    for (i = 0; i < 3; ++i)
	t[i] = STEP_SPEED*d[i] - vn*n[i];
    tl = sqrtf (Dot (t, t));
    along = tl > FOOT_MU*vn ? FOOT_MU*vn/tl : 1;
    m = mo->info->mass > 0 ? (float)mo->info->mass : 100;
    for (i = 0; i < 3; ++i)
    {
	j[i] = m*(vn*n[i] + t[i]*along);
	r[i] = -BODY_HALF*b->u[i] - STEP_BEHIND*b->f[i];
    }
    Impulse (mo, b, j, r);
    // No mind to steady it: a stagger of its own, up to 8 degrees a second each way.
    for (i = 0; i < 3; ++i)
	b->w[i] += (P_Random () - 128)/128.0f*0.14f*TIC_S;
    return true;
}

void P_BodyContact (mobj_t* mo, fixed_t impact, boolean ceiling)
{
    body_t*	b;
    float	n[3] = { 0, 0, ceiling ? -1.0f : 1.0f }, v, m, j[3], r[3], s;
    int		i;

    if (!(P_Brainless (mo) || Drum (mo)) || !(b = BodyOf (mo)))
	return;
    // The end toward the surface takes the bump: no turn when it comes down on its
    // feet or its head, the more the further over it leans.
    v = (float)(impact < 0 ? -impact : impact)/FRACUNIT/MPS;
    m = P_Mass (mo);
    s = Dot (b->u, n) > 0 ? -1.0f : 1.0f;
    for (i = 0; i < 3; ++i)
    {
	j[i] = 0;
	r[i] = s*b->half*b->u[i];
    }
    // A rod stopped at one end gives way by turning: the bump is that of a mass
    // 1/(1/m + arm^2/I), arm how far off the surface's line the end is.
    {
	float arm2 = b->half*b->half*(1 - Dot (b->u, n)*Dot (b->u, n));
	j[2] = v/(1/m + arm2/b->inertia)*n[2];
    }
    {
	float t[3];
	Cross (r, j, t);
	for (i = 0; i < 3; ++i)
	    b->w[i] += t[i]/b->inertia*TIC_S;
    }
}

boolean P_Tumbling (mobj_t* mo)
{
    return P_Brainless (mo) || Drum (mo);
}

void P_BodyDraft (mobj_t* mo, const float* top, const float* bottom, float k)
{
    body_t*	b;
    float	m, f[3], t[3], arm, most = 3;	// MOST_PUSH a half, as the whole
    float	a[2][3];
    int		i, h;

    if (!P_Tumbling (mo) || !(b = BodyOf (mo)))
	return;
    m = P_Mass (mo);
    for (i = 0; i < 3; ++i)
    {
	a[0][i] = top[i]*k; a[1][i] = bottom[i]*k;
    }
    for (h = 0; h < 2; ++h)
    {
	float len = sqrtf (Dot (a[h], a[h]));
	if (len > most)
	    for (i = 0; i < 3; ++i)
		a[h][i] *= most/len;
    }
    // Each half's pull (newtons) at its middle, a quarter of the length out along the
    // body: the difference turns it about its own middle.
    for (i = 0; i < 3; ++i)
	f[i] = m/2*(a[0][i] - a[1][i])*TICRATE*TICRATE/32;
    arm = b->half/2;
    Cross (b->u, f, t);
    for (i = 0; i < 3; ++i)
	b->w[i] += arm*t[i]/b->inertia*TIC_S*TIC_S;
    // Buffeting: air tearing past a blunt body sheds eddies that shove it unevenly, a few
    // hundredths of the drag across its length, every way in turn. What spins debris in
    // a gale; hard by a breach, next to nothing in a gentle current.
    {
	float pull = m*sqrtf ((a[0][0] + a[1][0])*(a[0][0] + a[1][0]) + (a[0][1] + a[1][1])*(a[0][1] + a[1][1])
			      + (a[0][2] + a[1][2])*(a[0][2] + a[1][2]))/2*TICRATE*TICRATE/32;
	float axis[3], kick = BUFFET*pull*2*b->half/b->inertia*TIC_S*TIC_S;
	for (i = 0; i < 3; ++i)
	    axis[i] = (P_Random () - 128)/128.0f;
	Normalize (axis);
	for (i = 0; i < 3; ++i)
	    b->w[i] += axis[i]*kick;
    }
}

void P_BodyCatch (mobj_t* mo, fixed_t edge, boolean top)
{
    body_t*	b;
    float	v, dx, dy, middle, arm, l, axis[3];
    int		i;

    if (!(P_Brainless (mo) || Drum (mo)) || !(b = BodyOf (mo)))
	return;
    v = (float)P_AproxDistance (mo->momx, mo->momy)/FRACUNIT;
    if (v < 1e-3f)
	return;
    dx = (float)mo->momx/FRACUNIT/v; dy = (float)mo->momy/FRACUNIT/v;
    // The rim catches the edge so far off its middle, and the catch takes a share of its
    // speed there: the top pitched on over a sill, back under a lintel.
    middle = (float)mo->height/FRACUNIT/2;
    arm = (middle - (float)edge/FRACUNIT)/32;
    if (arm <= 0)
	return;
    l = CATCH*P_Mass (mo)*v/MPS*arm;
    axis[0] = top ? dy : -dy; axis[1] = top ? -dx : dx; axis[2] = 0;	// up x way (top: way x up)
    for (i = 0; i < 3; ++i)
	b->w[i] += axis[i]*l/b->inertia*TIC_S;
}

void P_BodyKnock (mobj_t* mo, fixed_t lost)
{
    body_t*	b;
    float	axis[3], l;
    int		i;

    if (!(P_Brainless (mo) || Drum (mo)) || !(b = BodyOf (mo)) || lost <= 0)
	return;
    // The knock's spin: mass times the speed it took off, at up to KNOCK_ARM off the
    // middle, about whichever way the rim or corner caught.
    for (i = 0; i < 3; ++i)
	axis[i] = (P_Random () - 128)/128.0f;
    Normalize (axis);
    l = P_Mass (mo)*(float)lost/FRACUNIT/MPS*KNOCK_ARM*(P_Random ()/255.0f);
    for (i = 0; i < 3; ++i)
	b->w[i] += axis[i]*l/b->inertia*TIC_S;
}

void P_BodyRecoil (mobj_t* mo, int impulse)
{
    body_t*	b;
    float	j[3], r[3];
    int		i;

    if (!P_Brainless (mo) || !(b = BodyOf (mo)))
	return;
    // Kicked back along the barrel (as the last shot went, P_BodyAim), from above and
    // in front of its middle.
    for (i = 0; i < 3; ++i)
    {
	j[i] = -impulse*b->aim[i];
	r[i] = GUN_UP*b->u[i] + GUN_OUT*b->aim[i];
    }
    Impulse (mo, b, j, r);
}

fixed_t P_BodyAim (mobj_t* mo, int* angle)
{
    body_t*	b = BodyOf (mo);
    float	t[3], n[3], c, flat;
    int		i;

    if (!b)
	return 0;
    memcpy (b->aim, b->f, sizeof b->aim);
    // At its target, as far as the arms swing the gun off the body's way.
    if (mo->target)
    {
	ToTarget (mo, t);
	c = Dot (b->f, t);
	if (c >= cosf (ARM_REACH))
	    memcpy (b->aim, t, sizeof b->aim);
	else
	{
	    for (i = 0; i < 3; ++i)
		n[i] = t[i] - c*b->f[i];
	    Normalize (n);
	    for (i = 0; i < 3; ++i)
		b->aim[i] = b->f[i]*cosf (ARM_REACH) + n[i]*sinf (ARM_REACH);
	}
    }
    *angle = (int)ToAngle (atan2 (b->aim[1], b->aim[0]));
    flat = sqrtf (b->aim[0]*b->aim[0] + b->aim[1]*b->aim[1]);
    if (flat < 0.1f)
	flat = 0.1f;	// straight up or down: as steep as a shot goes
    return (fixed_t)(b->aim[2]/flat*FRACUNIT);
}

boolean P_BodyUp (mobj_t* mo, float fraction, float* up)
{
    body_t*	b;
    int		i;

    for (i = 0; i < MOST_BODIES; ++i)
	if (bodies[i].mo == mo)
	    break;
    if (i == MOST_BODIES || !(P_Brainless (mo) || Drum (mo)))
	return false;
    b = &bodies[i];
    for (i = 0; i < 3; ++i)
	up[i] = b->pu[i] + (b->u[i] - b->pu[i])*fraction;
    Normalize (up);
    return true;
}


// Monsters whose boots a draft tore loose: adrift until they come down on a
// floor, where they get up (P_AdriftLand). seen: still in the level this tic.
#define MOST_ADRIFT	64
static mobj_t*	adrift[MOST_ADRIFT];
static boolean	adriftSeen[MOST_ADRIFT];

boolean P_Adrift (mobj_t* mo)
{
    int		i;

    for (i = 0; i < MOST_ADRIFT; ++i)
	if (adrift[i] == mo)
	    return true;
    return false;
}

// Off its feet: no walking, no aiming (A_Chase), and whatever attack it was in
// the middle of is lost with its footing.
static boolean Unfoot (mobj_t* mo)
{
    int		i, free = -1;

    for (i = 0; i < MOST_ADRIFT; ++i)
    {
	if (adrift[i] == mo)
	    return false;
	if (!adrift[i] && free < 0)
	    free = i;
    }
    if (free < 0)
	return false;
    adrift[free] = mo;
    adriftSeen[free] = true;
    if (mo->health > 0 && mo->info->seestate)
	P_SetMobjState (mo, mo->info->seestate);
    return true;
}

void P_TearLoose (mobj_t* mo)
{
    if (!Unfoot (mo))
	return;
    // Off its feet: up off the floor a unit, so the boots no longer reach for it at once.
    mo->z = mo->floorz + FRACUNIT;
    if (mo->momz < FRACUNIT/2)
	mo->momz = FRACUNIT/2;
}

// The drafts on monsters still in their boots: which way and how hard (units
// a tic a tic) this tic, for the doll that leans into it (Game_Draft), and how
// much strain the boots have taken. Their feet hold, so the draft takes the top
// of the body first; past the grip for a moment, the boots give.
#define TEAR_STRAIN	4.0f
typedef struct { mobj_t* mo; float x, y, z, strain; int tic; } strain_t;
static strain_t	strains[MOST_ADRIFT];

static strain_t* StrainOf (mobj_t* mo, boolean make)
{
    int		i, free = -1;

    for (i = 0; i < MOST_ADRIFT; ++i)
    {
	if (strains[i].mo == mo)
	    return &strains[i];
	if (free < 0 && (!strains[i].mo || leveltime - strains[i].tic > 1))
	    free = i;
    }
    if (!make || free < 0)
	return NULL;
    memset (&strains[free], 0, sizeof strains[free]);
    strains[free].mo = mo;
    return &strains[free];
}

boolean P_Strain (mobj_t* mo, float x, float y, float z, float grip)
{
    strain_t*	s = StrainOf (mo, true);
    float	pull = sqrtf (x*x + y*y + z*z);

    if (!s)
	return pull > grip;
    s->x = x; s->y = y; s->z = z; s->tic = leveltime;
    s->strain = pull > grip ? s->strain + pull/grip : (s->strain > 1 ? s->strain - 1 : 0);
    return s->strain >= TEAR_STRAIN;
}

float P_Draft (mobj_t* mo, float* way)
{
    strain_t*	s = StrainOf (mo, false);
    float	pull;

    if (!s || leveltime - s->tic > 1)
	return 0;
    pull = sqrtf (s->x*s->x + s->y*s->y + s->z*s->z);
    if (pull > 0)
    {
	way[0] = s->x/pull; way[1] = s->y/pull; way[2] = s->z/pull;
    }
    return pull;
}

void P_AdriftBegin (void)
{
    int		i;

    memset (adriftSeen, 0, sizeof adriftSeen);
    for (i = 0; i < MOST_BODIES; ++i)
	bodies[i].seen = false;
}

void P_AdriftLand (mobj_t* mo)
{
    int		i;

    // Up out of its boots' reach however it got there (a blow, a ledge, a landing at
    // speed that bounced it off again): adrift as much as if a draft tore it loose,
    // not a floating gunner.
    if ((mo->flags & MF_COUNTKILL) && !(mo->flags & (MF_FLOAT|MF_NOGRAVITY)) && mo->health > 0
	&& !P_Brainless (mo) && mo->z - mo->floorz > BOOT_REACH && Unfoot (mo))
	return;

    for (i = 0; i < MOST_ADRIFT; ++i)
	if (adrift[i] == mo)
	{
	    adriftSeen[i] = true;
	    if (mo->z > mo->floorz)
		return;
	    // Down on a floor: the boots take hold and it gets up, a moment as if hurt.
	    adrift[i] = NULL;
	    if (mo->health > 0 && mo->info->painstate)
		P_SetMobjState (mo, mo->info->painstate);
	    return;
	}
}

void P_AdriftEnd (void)
{
    int		i;

    for (i = 0; i < MOST_ADRIFT; ++i)
	if (!adriftSeen[i])
	    adrift[i] = NULL;	// gone from the level
    for (i = 0; i < MOST_BODIES; ++i)
	if (!bodies[i].seen)
	    bodies[i].mo = NULL;	// gone, or dead (its ragdoll takes over)
}

boolean P_Booted (mobj_t* mo)
{
    if (mo->flags & (MF_CORPSE|MF_FLOAT|MF_NOGRAVITY|MF_MISSILE|MF_SKULLFLY))
	return false;
    if (mo->health <= 0)
	return false;
    if (!mo->player && P_Adrift (mo))
	return false;
    if (P_Brainless (mo))
	return false;	// the zombies have none either (P_BodyStep)
    // The player wears none: no grip on the floor past mu*g, nothing holding him
    // against a draft; he gets about by jumping (P_Jump) and the suit's thrusters.
    return !mo->player && (mo->flags & MF_COUNTKILL);
}



fixed_t P_Gravity (mobj_t* mo)
{
    if (!P_Phobos ())
	return GRAVITY;
    // Stepping down a stair the boots pull as hard as Doom's own gravity;
    // off a ledge it floats until the floor comes within reach.
    if (P_Booted (mo) && mo->momz <= 0 && mo->z - mo->floorz <= BOOT_REACH)
	return GRAVITY;
    return PHOBOS_GRAVITY;
}


void P_PhobosSlide (mobj_t* mo)
{
    fixed_t	speed = P_AproxDistance (mo->momx, mo->momy);
    fixed_t	keep;

    if (speed <= PHOBOS_FRICTION)
    {
	mo->momx = mo->momy = 0;
	return;
    }
    keep = FixedDiv (speed - PHOBOS_FRICTION, speed);
    mo->momx = FixedMul (mo->momx, keep);
    mo->momy = FixedMul (mo->momy, keep);
}


fixed_t P_PhobosThrust (int damage, int mass)
{
    return mass > 0 ? damage*PHOBOS_THRUST/mass : 0;
}


// The player's body turning from his guns' kicks, radians a tic about his sideways
// axis (up: pitching back); the view's pitch is the renderer's, so it takes the turn
// from P_LookKick. A shouldered gun's line runs 0.45 m above his middle.
#define PLAYER_GUN_UP	0.45f
static float	playerSpin[MAXPLAYERS], lookKick;
static boolean	viewFlipped;	// his view over the top: facing about, upside down
static mobj_t*	flippedBody;	// whose (a new level or life starts upright)
// On a floor or ceiling he rights himself, 60 degrees a second.
#define RIGHTING	(1.047f*TIC_S)

void P_Recoil (player_t* player, int impulse, fixed_t slope)
{
    mobj_t*	mo = player->mo;
    double	speed, up, along, angle;

    if (!P_Phobos () || !mo || (P_Booted (mo) && mo->z <= mo->floorz) || mo->info->mass <= 0)
	return;
    // Missiles aim by their own slope; the kick follows the view.
    if (slope == RECOIL_VIEW)
	slope = freeaim ? (fixed_t)(tan (tourpitch)*FRACUNIT) : 0;
    speed = (double)impulse*PHOBOS_IMPULSE/mo->info->mass;
    up = (double)slope/FRACUNIT;
    along = 1/sqrt (1 + up*up);
    angle = (double)mo->angle*(6.283185307179586/4294967296.0);
    mo->momx -= (fixed_t)(speed*along*cos (angle));
    mo->momy -= (fixed_t)(speed*along*sin (angle));
    mo->momz -= (fixed_t)(speed*along*up);
    // And the turn, as a zombie's (P_BodyRecoil): the kick's line runs PLAYER_GUN_UP
    // above his middle, so it pitches him back, the view going up with him.
    if (!P_Booted (mo))
	playerSpin[player - players] += (float)(impulse*PLAYER_GUN_UP*along/BODY_I*TIC_S);
}

void P_PlayerSpin (player_t* player)
{
    mobj_t*	mo = player->mo;
    float*	spin = &playerSpin[player - players];

    if (!P_Phobos () || !mo || mo->health <= 0)
    {
	*spin = 0;
	return;
    }
    // Against a floor or ceiling the rub of it stops the turning, as a zombie's, and
    // over the top he settles with his feet to it, round the nearer way: on a floor
    // upright again, against a ceiling (where his feet are) upside down and level.
    // Upright, his look is his own.
    if (mo->z <= mo->floorz || mo->z + mo->height >= mo->ceilingz)
    {
	*spin *= 0.85f;
	if (player == &players[consoleplayer] && P_ViewFlipped ())
	{
	    // How far over he is (0 upright and level, pi upside down and level).
	    float over = P_ViewFlipped () ? 3.14159265f - tourpitch : tourpitch;
	    float want = mo->z <= mo->floorz ? 0 : 3.14159265f, off = remainderf (want - over, 6.28318531f);
	    if (off > RIGHTING || off < -RIGHTING)
		lookKick += off > 0 ? RIGHTING : -RIGHTING;
	}
    }
    // He twists against it as a falling cat rights itself (TWIST, as the zombies): that
    // changes which way he faces, not how he spins, and keeps up with a spin up to 30
    // degrees a second; only what is past that turns his view.
    // His arms are part of the twist: holding a gun on target (firing) leaves him his hips
    // and legs, a third of it.
    if (player == &players[consoleplayer])
    {
	float twist = (player->cmd.buttons & (BT_SPECIAL|BT_ATTACK)) == BT_ATTACK ? TWIST/3 : TWIST;
	lookKick += *spin > twist ? *spin - twist : *spin < -twist ? *spin + twist : 0;
    }
}

boolean P_ViewFlipped (void)
{
    if (viewFlipped && (players[consoleplayer].mo != flippedBody || !P_Phobos ()))
	viewFlipped = false;
    return viewFlipped;
}

void P_FlipView (void)
{
    mobj_t*	mo = players[consoleplayer].mo;

    viewFlipped = !viewFlipped;
    flippedBody = mo;
    if (mo)
	mo->angle += ANG180;
}

float P_LookKick (void)
{
    float	kick = lookKick;

    lookKick = 0;
    return kick;
}


// 11.3 m/s at 32 units a metre and 35 tics a second.
#define ESCAPE	((fixed_t)(10.3*FRACUNIT))

static const char* Called (mobj_t* mo)
{
    switch (mo->type)
    {
      case MT_POSSESSED: return "ZOMBIEMAN";
      case MT_SHOTGUY: return "SHOTGUN GUY";
      case MT_TROOP: return "IMP";
      case MT_SERGEANT: return "DEMON";
      case MT_SHADOWS: return "SPECTRE";
      case MT_HEAD: return "CACODEMON";
      case MT_BRUISER: return "BARON";
      case MT_SKULL: return "LOST SOUL";
      case MT_BARREL: return "BARREL";
      default: return mo->flags & MF_SPECIAL ? "PICKUP" : "PIECE OF THE BASE";
    }
}

boolean P_Escaped (mobj_t* mo)
{
    static char	told[80];

    if (!P_Phobos () || mo->player || (mo->flags & MF_MISSILE)
	|| mo->subsector->sector->ceilingpic != skyflatnum || mo->momz < ESCAPE)
	return false;
    snprintf (told, sizeof told, "%s %s REACHED ESCAPE VELOCITY",
	      mo->health <= 0 && (mo->flags & MF_CORPSE) ? "A DEAD" : "A", Called (mo));
    players[consoleplayer].message = told;
    P_RemoveMobj (mo);
    return true;
}


boolean P_AirlockCycle (sector_t* door)
{
    float	least = 1, most = 0;
    int		i;

    if (!P_Phobos ())
	return false;
    for (i = 0; i < door->linecount; ++i)
    {
	line_t*		l = door->lines[i];
	sector_t*	other = l->frontsector == door ? l->backsector : l->frontsector;
	float		a;
	if (!other || other == door)
	    continue;
	a = P_SectorAir (other);
	if (a < least) least = a;
	if (a > most) most = a;
    }
    return most - least > 0.3f;
}


// The legs' push off the floor, as units a tic: 4 m/s. A standing jump on Earth
// pushes at about 2.2 g over 40 cm and leaves at 3 m/s; with no weight to hold up,
// all of that push goes into speed (4.2 m/s), a little less for the gear.
#define JUMP_SPEED	3.66
// Held movement keys hop him gently, a push each time he touches down: no higher than
// HOP_HEIGHT (30 cm: 6 cm/s up, and 20 s in the air in Phobos' pull), steering his
// speed along toward 2.2 m/s the way they point, as far as such a light touch grips:
// a few cm/s a hop. Low and slow on purpose; speed comes off walls (P_Jump's kick).
// Under a low ceiling he hops no harder than to reach it in HOP_RISE tics and pushes
// back down off it with his hands (P_CeilingHop).
#define HOP_HEIGHT	9.6
#define HOP_TOP		2.0
#define HOP_RISE	18.0
// Kicking off a wall within KICK_REACH of his side (Use with the movement keys): the
// whole leg push (JUMP_SPEED) goes along the floor, since a wall pushes back sideways;
// along the wall only as far as the foot grips. How astronauts cross a space station.
#define KICK_REACH	12
static boolean	jumpHeld[MAXPLAYERS], kickHeld[MAXPLAYERS];
static double	landedWith[MAXPLAYERS];	// the speed he last came down (or up) on a surface with

// The run key held with the movement keys fires the suit's thrusters instead of
// hopping (P_Jetpack): running moves the keys past a walk's 25 forward, 24 sideways.
static boolean Thrusting (player_t* player)
{
    ticcmd_t*	cmd = &player->cmd;
    return cmd->forwardmove > 25 || cmd->forwardmove < -25 || cmd->sidemove > 24 || cmd->sidemove < -24;
}

// The way the movement keys point, flat; false when none are held.
static boolean Heading (player_t* player, double* dx, double* dy)
{
    ticcmd_t*	cmd = &player->cmd;
    double	f = cmd->forwardmove/50.0, s = cmd->sidemove/40.0, len = sqrt (f*f + s*s), angle;

    if (len <= 0)
	return false;
    f /= len; s /= len;
    angle = (double)player->mo->angle*(6.283185307179586/4294967296.0);
    *dx = f*cos (angle) + s*sin (angle); *dy = f*sin (angle) - s*cos (angle);
    return true;
}

// A push off a surface: away from it at across (way 1 up off a floor, -1 down off a
// ceiling), and the speed along it steered toward top the way (dx, dy) points, as far
// as the contact's friction reaches: mu times the speed it came in with and leaves with.
static void Push (mobj_t* mo, double dx, double dy, double across, double into, int way, double top)
{
    double	vx = (double)mo->momx/FRACUNIT, vy = (double)mo->momy/FRACUNIT;
    double	wx = dx*top - vx, wy = dy*top - vy, want = sqrt (wx*wx + wy*wy);
    double	most = FOOT_MU*(into + across);

    if (want > most)
    {
	wx *= most/want; wy *= most/want;
    }
    mo->momx += (fixed_t)(wx*FRACUNIT);
    mo->momy += (fixed_t)(wy*FRACUNIT);
    mo->momz = (fixed_t)(way*across*FRACUNIT);
}

// How hard a hop pushes away from a surface with room to the one across: never so
// hard that it gets there in under HOP_RISE tics.
static double HopAcross (mobj_t* mo)
{
    double	room = (double)(mo->ceilingz - mo->floorz - mo->height)/FRACUNIT;
    double	most = sqrt (2*HOP_HEIGHT*PHOBOS_GRAVITY/(double)FRACUNIT);

    return room/HOP_RISE < most ? room/HOP_RISE : most;
}

// The wall to kick off: the solid line within reach (one-sided, impassable, whole glass,
// or no gap at his height) whose face turns most the way the keys point; its face's way
// (out from the wall) in kickx, kicky.
static mobj_t*	kicker;
static double	kickdx, kickdy, kickx, kicky, kickbest;

static boolean PIT_Kick (line_t* ld)
{
    mobj_t*	mo = kicker;
    double	ax = (double)ld->v1->x/FRACUNIT, ay = (double)ld->v1->y/FRACUNIT;
    double	ex = (double)ld->dx/FRACUNIT, ey = (double)ld->dy/FRACUNIT, len2 = ex*ex + ey*ey;
    double	px = (double)mo->x/FRACUNIT, py = (double)mo->y/FRACUNIT, t, cx, cy, d, nx, ny, face;
    boolean	solid = !ld->backsector || (ld->flags & ML_BLOCKING) || P_GlassWhole (ld);

    if (!solid)
    {
	fixed_t	f = ld->frontsector->floorheight, b = ld->backsector->floorheight;
	fixed_t	c = ld->frontsector->ceilingheight, e = ld->backsector->ceilingheight;
	fixed_t	bottom = f > b ? f : b, top = c < e ? c : e;
	solid = bottom > mo->z + 16*FRACUNIT || top < mo->z + mo->height;
    }
    if (!solid || len2 < 1)
	return true;
    t = ((px - ax)*ex + (py - ay)*ey)/len2;
    t = t < 0 ? 0 : t > 1 ? 1 : t;
    cx = ax + ex*t; cy = ay + ey*t;
    d = sqrt ((px - cx)*(px - cx) + (py - cy)*(py - cy));
    if (d > (double)mo->radius/FRACUNIT + KICK_REACH || d < 1e-3)
	return true;
    nx = (px - cx)/d; ny = (py - cy)/d;
    face = nx*kickdx + ny*kickdy;
    if (face > kickbest)
    {
	kickbest = face; kickx = nx; kicky = ny;
    }
    return true;
}

static boolean WallKick (mobj_t* mo, double dx, double dy)
{
    fixed_t	r = mo->radius + KICK_REACH*FRACUNIT;
    int		bx, by, xl = (mo->x - r - bmaporgx)>>MAPBLOCKSHIFT, xh = (mo->x + r - bmaporgx)>>MAPBLOCKSHIFT;
    int		yl = (mo->y - r - bmaporgy)>>MAPBLOCKSHIFT, yh = (mo->y + r - bmaporgy)>>MAPBLOCKSHIFT;
    double	vn, tx, ty, tl, keep;

    kicker = mo; kickdx = dx; kickdy = dy; kickbest = 0.2;	// pushing away from it, not along it
    ++validcount;
    for (bx = xl; bx <= xh; ++bx)
	for (by = yl; by <= yh; ++by)
	    P_BlockLinesIterator (bx, by, PIT_Kick);
    if (kickbest <= 0.2)
	return false;
    // The push straight off the face, and along it as far as the foot grips.
    vn = JUMP_SPEED*kickbest;
    tx = JUMP_SPEED*dx - vn*kickx; ty = JUMP_SPEED*dy - vn*kicky;
    tl = sqrt (tx*tx + ty*ty);
    keep = tl > FOOT_MU*vn ? FOOT_MU*vn/tl : 1;
    mo->momx += (fixed_t)((vn*kickx + tx*keep)*FRACUNIT);
    mo->momy += (fixed_t)((vn*kicky + ty*keep)*FRACUNIT);
    return true;
}

void P_Jump (player_t* player, boolean onground)
{
    ticcmd_t*	cmd = &player->cmd;
    mobj_t*	mo = player->mo;
    int		p = player - players;
    boolean	down = (cmd->buttons & (BT_SPECIAL|BT_JUMP)) == BT_JUMP;
    boolean	press = down && !jumpHeld[p], heading;
    boolean	use = (cmd->buttons & (BT_SPECIAL|BT_USE)) == BT_USE, kick = use && !kickHeld[p];
    double	dx = 0, dy = 0, across;

    jumpHeld[p] = down;
    kickHeld[p] = use;
    if (!P_Phobos () || !mo)
	return;
    heading = Heading (player, &dx, &dy);
    // A wall within reach, on the floor or aloft: kick off it.
    if (kick && heading && WallKick (mo, dx, dy))
	return;
    if (!onground || mo->momz > 0)
	return;
    if (press)
    {
	// The jump: all the legs give, leaning into the way the keys point as far as the feet grip.
	if (heading)
	    Push (mo, dx, dy, JUMP_SPEED/sqrt (1 + FOOT_MU*FOOT_MU), 0, 1, 1e3);
	else
	    mo->momz = (fixed_t)(JUMP_SPEED*FRACUNIT);
	landedWith[p] = 0;
	return;
    }
    if (!heading || Thrusting (player) || (across = HopAcross (mo)) < 0.02)
	return;
    Push (mo, dx, dy, across, landedWith[p], 1, HOP_TOP);
    landedWith[p] = 0;
}

boolean P_CeilingHop (mobj_t* mo)
{
    player_t*	player = mo->player;
    double	dx, dy, across;

    if (!P_Phobos () || !player || P_Booted (mo) || mo->health <= 0 || !Heading (player, &dx, &dy)
	|| Thrusting (player) || (across = HopAcross (mo)) < 0.02)
	return false;
    Push (mo, dx, dy, across, (double)mo->momz/FRACUNIT, -1, HOP_TOP);
    return true;
}


// Coming down on a floor (or up against a ceiling) presses a body into it for a
// moment, and that is what friction brakes with: mu times the speed it hit with
// comes off its speed along the surface. With next to no weight, it is the only
// real friction it gets.
void P_Touchdown (mobj_t* mo, fixed_t impact)
{
    fixed_t	speed, brake;

    double	dx, dy;

    if (!P_Phobos () || P_Booted (mo) || (mo->flags & (MF_MISSILE|MF_SKULLFLY|MF_NOGRAVITY)))
	return;
    // Hopping, his legs take the landing: no skid, the push off it steers (P_Jump).
    if (mo->player && mo->health > 0 && Heading (mo->player, &dx, &dy))
    {
	landedWith[mo->player - players] = (double)(impact < 0 ? -impact : impact)/FRACUNIT;
	return;
    }
    speed = P_AproxDistance (mo->momx, mo->momy);
    brake = (fixed_t)(FOOT_MU*(impact < 0 ? -impact : impact));
    if (speed <= brake)
    {
	mo->momx = mo->momy = 0;
	return;
    }
    mo->momx = FixedMul (mo->momx, FixedDiv (speed - brake, speed));
    mo->momy = FixedMul (mo->momy, FixedDiv (speed - brake, speed));
}


// The suit's thrusters: 0.8 m/s^2 at full push (running), as units a tic a tic.
#define JETPACK		0.021

void P_Jetpack (player_t* player)
{
    ticcmd_t*	cmd = &player->cmd;
    mobj_t*	mo = player->mo;
    double	f, s, len, angle, pitch, push;

    if (!P_Phobos () || !mo || (!cmd->forwardmove && !cmd->sidemove)
	|| (!P_Booted (mo) && !Thrusting (player)))
	return;
    // Running is full thrust, walking half; never faster on a diagonal.
    f = cmd->forwardmove/50.0; s = cmd->sidemove/40.0;
    len = sqrt (f*f + s*s);
    if (len > 1) { f /= len; s /= len; }
    angle = (double)mo->angle*(6.283185307179586/4294967296.0);
    pitch = freeaim ? tourpitch : 0;
    push = JETPACK*FRACUNIT;
    mo->momx += (fixed_t)(push*(f*cos (angle)*cos (pitch) + s*sin (angle)));
    mo->momy += (fixed_t)(push*(f*sin (angle)*cos (pitch) - s*cos (angle)));
    mo->momz += (fixed_t)(push*f*sin (pitch));
}


boolean P_Loose (mobj_t* mo)
{
    if (!P_Phobos () || (mo->flags & (MF_MISSILE|MF_SKULLFLY)) || mo->player)
	return false;
    // Torn off its feet, or a body, barrel or pickup carried by a draft or a blow.
    return P_Adrift (mo) || (!P_Booted (mo) && (mo->momx || mo->momy || mo->z > mo->floorz));
}


boolean P_Carried (mobj_t* mo)
{
    return P_Loose (mo) && (P_Adrift (mo) || mo->z > mo->floorz);
}


boolean P_OpenSky (mobj_t* mo)
{
    return P_Phobos () && !mo->player && !(mo->flags & (MF_MISSILE|MF_FLOAT|MF_NOGRAVITY|MF_SKULLFLY))
	&& mo->subsector->sector->ceilingpic == skyflatnum;
}
