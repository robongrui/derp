//
// Air on Phobos (p_phobos.h): every sector holds air, 0 (vacuum) to 1 (the
// base's pressure), and it vents through the openings between sectors. Under
// the sky is vacuum. Whole window glass and shut doors seal; a broken pane or
// an open door lets the air out, choked at the speed of sound while the
// pressure on the far side is under half, and the draft drags whatever is
// near the way it goes. Life support pumps sealed rooms full again.
//
// Volumes in units^3 (32 units a metre), flows as air * units^3 a tic.
//

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "doomdef.h"
#include "doomstat.h"
#include "p_local.h"
#include "p_phobos.h"
#include "r_sky.h"
#include "r_state.h"
#include "z_zone.h"

// Air through an opening at the speed of sound, as units^3 a tic per unit^2
// of opening (~140 m/s after the vena contracta): a 4 m^2 window empties a
// 770 m^3 room in about a second and a half.
#define THROAT		128.0f
// Under this share of the pressure behind it, the flow is choked.
#define CHOKED		0.528f
// Each substep evens neighbours out at most: a run of small sectors (a stair or a
// tunnel to space) passes air on one sector a substep, so few substeps fill such a
// passage instead of streaming through it (E1M1's secret tunnel drew a tenth of the
// choked flow at its door). Enough for one to carry the choked flow.
#define SUBSTEPS	64
// Life support: a sealed room fills again in about half a minute. One with
// an open way to space gets nothing: it empties and stays empty until sealed.
#define REFILL		(1.0f/(30*TICRATE))
// Drag on a body: rho/2 * Cd * area over its mass, in game units, for air at
// the base's pressure (1.2 kg/m^3, 0.7 m^2, Cd 1).
#define DRAG		0.0131f
// The most a draft adds in a tic (units a tic): past that it is a gale.
#define MOST_PUSH	3.0f
// Openings with less flow than this are calm (air * units^3 a tic), and so
// is a trickle under TRICKLE units^3 a tic per unit^2 of opening (about 2% of
// the base's pressure going out at the speed of sound): life support evening
// out a sealed room, no draft.
#define CALM		200.0f
#define TRICKLE		3.0f
// What a monster's boots hold against, units a tic a tic: about 1 g. Past it
// they tear loose (P_TearLoose).
#define BOOT_GRIP	0.26f
// The share of a draft that still moves the player along the floor.
#define PLAYER_NUDGE	0.25f
// How far above the floor the boots still hold (Doom's highest step).
#define BOOT_REACH	(24*FRACUNIT)
// How near the wall under a gap the air along the floor turns up into it.
#define TURN		96.0f

#define MAX(a,b)	((a) > (b) ? (a) : (b))
#define MIN(a,b)	((a) < (b) ? (a) : (b))

typedef struct
{
    int		line, a, b;
    float	flow;		// this tic, a to b (negative: b to a)
} vent_t;

static float*	air;
static float*	area;
static byte*	glass;		// per line: 0 none, 1 whole, 2 broken
static vent_t*	vents;
static int	ventcount, airsectors, airlines;
static boolean	settled;	// the starting air has been worked out
static boolean	panesKnown;	// the renderer has told us its window panes
static int	airTics;	// tics run since the level was set up or loaded
static int*	ventfirst;	// each sector's vents, packed
static int*	region;		// each sector's air space this tic (-1 under the sky)
static int*	iceTics;	// tics without air (counting up to frozen), or with it again once frozen
static byte*	iced;		// the floor frozen over (a slime pool: its damage frozen in with it)
static int*	active;		// the vents with a draft this tic
static int	activecount;
static int*	ventof;

static float units (fixed_t v) { return v/(float)FRACUNIT; }
static boolean sky (int s) { return sectors[s].ceilingpic == skyflatnum; }

// Every sector's floor area, from its lines (shoelace, each side by its winding).
static void SectorAreas (void)
{
    double*	sum = calloc (numsectors, sizeof(double));
    int		i;

    for (i = 0; i < numlines; ++i)
    {
	line_t*	l = &lines[i];
	double	cross = (double)units(l->v1->x)*units(l->v2->y) - (double)units(l->v2->x)*units(l->v1->y);
	if (l->frontsector) sum[l->frontsector - sectors] += cross;
	if (l->backsector) sum[l->backsector - sectors] -= cross;
    }
    for (i = 0; i < numsectors; ++i)
	area[i] = (float)fabs (sum[i])/2;
    free (sum);
}

static float Volume (int s)
{
    float	h = units(sectors[s].ceilingheight - sectors[s].floorheight);
    return h > 1 ? area[s]*h : area[s];
}

// The open area of a vent now: its length by the gap between the floors and
// ceilings, none through whole glass or a shut door.
static float Opening (const vent_t* v)
{
    line_t*	l = &lines[v->line];
    float	bottom = units(MAX(sectors[v->a].floorheight, sectors[v->b].floorheight));
    float	top = units(MIN(sectors[v->a].ceilingheight, sectors[v->b].ceilingheight));
    if (glass && glass[v->line] == 1)
	return 0;
    if (top - bottom < 1)
	return 0;
    return hypotf (units(l->v2->x - l->v1->x), units(l->v2->y - l->v1->y))*(top - bottom);
}

void P_AirLevel (void)
{
    int		i, n;

    free (air); free (area); free (glass); free (vents); free (ventfirst); free (ventof); free (region); free (active);
    free (iceTics); free (iced);
    airsectors = numsectors; airlines = numlines;
    air = malloc (sizeof(float)*numsectors);
    area = malloc (sizeof(float)*numsectors);
    glass = calloc (numlines, 1);
    vents = malloc (sizeof(vent_t)*(numlines + 1));
    ventfirst = calloc (numsectors + 1, sizeof(int));
    region = malloc (sizeof(int)*numsectors);
    iceTics = calloc (numsectors, sizeof(int));
    iced = calloc (numsectors, 1);
    active = malloc (sizeof(int)*(numlines + 1));
    ventcount = activecount = 0;
    SectorAreas ();
    for (i = 0; i < numlines; ++i)
    {
	line_t*	l = &lines[i];
	int	a, b;
	if (!l->frontsector || !l->backsector || l->frontsector == l->backsector)
	    continue;
	a = l->frontsector - sectors; b = l->backsector - sectors;
	if (sky (a) && sky (b))
	    continue;
	vents[ventcount].line = i; vents[ventcount].a = a; vents[ventcount].b = b; vents[ventcount].flow = 0;
	++ventfirst[a + 1]; ++ventfirst[b + 1];
	++ventcount;
    }
    for (i = 0; i < numsectors; ++i)
	ventfirst[i + 1] += ventfirst[i];
    ventof = malloc (sizeof(int)*(2*ventcount + 1));
    {
	int*	fill = calloc (numsectors, sizeof(int));
	for (n = 0; n < ventcount; ++n)
	{
	    ventof[ventfirst[vents[n].a] + fill[vents[n].a]++] = n;
	    ventof[ventfirst[vents[n].b] + fill[vents[n].b]++] = n;
	}
	free (fill);
    }
    settled = false;
    panesKnown = false;
    airTics = 0;
}

// Before anything moves: rooms with an open way to the sky have long since
// emptied; the rest are at the base's pressure.
static void Settle (void)
{
    int*	queue = malloc (sizeof(int)*airsectors);
    int		head = 0, tail = 0, i, k, vacuum = 0;

    for (i = 0; i < airsectors; ++i)
    {
	air[i] = sky (i) ? 0 : 1;
	if (sky (i))
	    queue[tail++] = i;
    }
    while (head < tail)
    {
	int s = queue[head++];
	for (k = ventfirst[s]; k < ventfirst[s + 1]; ++k)
	{
	    vent_t*	v = &vents[ventof[k]];
	    int		o = v->a == s ? v->b : v->a;
	    if (air[o] > 0 && Opening (v) > 0)
	    {
		air[o] = 0;
		queue[tail++] = o;
		++vacuum;
	    }
	}
    }
    free (queue);
    settled = true;
    printf ("Phobos air: %d sectors, %d vents, %d sectors open to space from the start.\n", airsectors, ventcount, vacuum);
}

float P_SectorAir (sector_t* sector)
{
    int		s = sector - sectors;

    if (!P_Phobos ())
	return 1;
    if (sky (s))
	return 0;
    if (!air || airsectors != numsectors || s < 0 || s >= airsectors || !settled)
	return 1;
    return air[s];
}

void P_PhobosGlass (int line, int whole)
{
    if (!glass || airlines != numlines || line < 0 || line >= airlines)
	return;
    glass[line] = whole ? 1 : 2;
    // Panes are found as the level starts or loads: the starting air waits for them
    // (not by leveltime: a loaded game's is well past its start).
    if (!panesKnown || airTics < 2)
	settled = false;
    panesKnown = true;
    // A broken pane holds nothing back: what fits goes through, the window's whole frame
    // (the side of the glass with the higher floor, its sill): many a map walls a window
    // off with an impassable line on its room side, and with no glass left that is an
    // opening like any other.
    if (!whole && P_Phobos ())
    {
	line_t*		l = &lines[line];
	sector_t*	frame = l->frontsector;
	int		n;
	lines[line].flags &= ~(ML_BLOCKING|ML_BLOCKMONSTERS);
	if (l->backsector && l->backsector->floorheight > l->frontsector->floorheight)
	    frame = l->backsector;
	for (n = 0; frame && n < frame->linecount; ++n)
	    if (frame->lines[n]->backsector)
		frame->lines[n]->flags &= ~(ML_BLOCKING|ML_BLOCKMONSTERS);
    }
}

// The share of the choked flow through an opening with ratio of the pressure past it
// to the pressure behind it: an ideal nozzle's (air, gamma 1.4), which goes with the
// root of a small difference. Linear in it, a run of openings with a little drop each
// (a tunnel to space) passed a fraction of what the narrowest of them would.
static float Nozzle (float ratio)
{
    static float	choked;
    if (ratio <= CHOKED)
	return 1;
    if (!choked)
	choked = sqrtf (powf (CHOKED, 2/1.4f) - powf (CHOKED, 2.4f/1.4f));
    return sqrtf (MAX(powf (ratio, 2/1.4f) - powf (ratio, 2.4f/1.4f), 0))/choked;
}

// The flows, a substep at a time.
static void Vent (void)
{
    int		n, step, i;
    float*	vol = Z_Malloc (sizeof(float)*airsectors, PU_STATIC, NULL);
    float*	opens = Z_Malloc (sizeof(float)*(ventcount + 1), PU_STATIC, NULL);

    for (i = 0; i < airsectors; ++i)
	vol[i] = Volume (i);
    for (n = 0; n < ventcount; ++n)
    {
	vents[n].flow = 0;
	opens[n] = Opening (&vents[n]);
    }
    for (step = 0; step < SUBSTEPS; ++step)
	for (n = 0; n < ventcount; ++n)
	{
	    vent_t*	v = &vents[n];
	    float	open = opens[n], pa, pb, hi, lo, ratio, choke, flow, most;
	    int		up, down;
	    if (open <= 0)
		continue;
	    pa = sky (v->a) ? 0 : air[v->a];
	    pb = sky (v->b) ? 0 : air[v->b];
	    hi = MAX(pa, pb); lo = MIN(pa, pb);
	    if (hi - lo < 1e-5f)
		continue;
	    up = pa > pb ? v->a : v->b; down = pa > pb ? v->b : v->a;
	    ratio = lo/hi;
	    choke = Nozzle (ratio);
	    flow = THROAT*open*hi*choke/SUBSTEPS;
	    // No more than evens them out.
	    most = sky (down) ? hi*vol[up] : (hi - lo)/(1/vol[up] + 1/vol[down]);
	    if (flow > most)
		flow = most;
	    air[up] -= flow/vol[up];
	    if (!sky (down))
		air[down] += flow/vol[down];
	    v->flow += up == v->a ? flow : -flow;
	}
    // The air spaces: sectors joined by open vents, apart from the sky; one
    // with an open way to the sky is open to space and gets no refill.
    {
	int*	queue = Z_Malloc (sizeof(int)*airsectors, PU_STATIC, NULL);
	byte*	leaks = Z_Malloc (airsectors, PU_STATIC, NULL);
	int	spaces = 0, k;
	for (i = 0; i < airsectors; ++i)
	    region[i] = -1;
	for (i = 0; i < airsectors; ++i)
	{
	    int head = 0, tail = 0;
	    if (sky (i) || region[i] >= 0)
		continue;
	    leaks[spaces] = 0;
	    region[i] = spaces;
	    queue[tail++] = i;
	    while (head < tail)
	    {
		int s = queue[head++];
		for (k = ventfirst[s]; k < ventfirst[s + 1]; ++k)
		{
		    vent_t*	v = &vents[ventof[k]];
		    int		o = v->a == s ? v->b : v->a;
		    if (Opening (v) <= 0)
			continue;
		    if (sky (o))
			leaks[spaces] = 1;
		    else if (region[o] < 0)
		    {
			region[o] = spaces;
			queue[tail++] = o;
		    }
		}
	    }
	    ++spaces;
	}
	for (i = 0; i < airsectors; ++i)
	    if (!sky (i) && !leaks[region[i]])
		air[i] += (1 - air[i])*REFILL;
	Z_Free (leaks);
	Z_Free (queue);
    }
    activecount = 0;
    for (n = 0; n < ventcount; ++n)
	if (fabsf (vents[n].flow) >= CALM && Opening (&vents[n]) > 0 && fabsf (vents[n].flow)/Opening (&vents[n]) >= TRICKLE)
	    active[activecount++] = n;
    for (i = 0; i < airsectors; ++i)
	if (!sky (i))
	{
	    if (air[i] < 0) air[i] = 0;
	    if (air[i] > 1) air[i] = 1;
	}
    Z_Free (opens);
    Z_Free (vol);
}

// The draft on a thing: from every venting opening of its sector, toward it
// where the air leaves and away from it where the air comes in, as fast as
// the opening's flow spread over a half sphere round it (no faster than
// through the opening itself), dragging as hard as the air there is thick.
// Fixtures stay where they are: solid decorations (lamps, pillars, columns)
// are bolted down and hanging ones hang from the ceiling. Anyone, anything
// shootable, pickups and loose clutter go with the air.
static boolean Bolted (mobj_t* mo)
{
    if (mo->player || (mo->flags & (MF_COUNTKILL|MF_CORPSE|MF_SHOOTABLE|MF_SPECIAL|MF_DROPPED)))
	return false;
    return (mo->flags & (MF_SOLID|MF_NOGRAVITY|MF_SPAWNCEILING)) != 0;
}

// The draft's pull (units a tic a tic, per unit of DRAG x face x thickness over mass)
// on what spans low..high at x, y in sector s: every venting opening of its air space.
static void Draft (mobj_t* mo, int s, float x, float y, float low, float high, float across, float* a)
{
    int		k;

    a[0] = a[1] = a[2] = 0;
    for (k = 0; k < activecount; ++k)
    {
	vent_t*		v = &vents[active[k]];
	line_t*		l = &lines[v->line];
	int		up = v->flow > 0 ? v->a : v->b;
	float		open = Opening (v), upair, q, ex, ey, len2, t, px, py, dx, dy, r, speed, push;
	float		bottom, top, flat, rise, len;
	// The openings the air leaves the thing's own sector by: what streams past it is
	// that sector's outflow (the network has the whole space's flow through every
	// sector). Counting every opening of the air space on the way to its breach
	// counted that same air once for each, many times over down a corridor (E1M1's
	// armour room was dragged out through the start room's window).
	if (up != s || (v->a != s && v->b != s))
	    continue;
	upair = sky (up) ? 1e-3f : air[up];
	if (upair < 1e-3f)
	    upair = 1e-3f;
	q = fabsf (v->flow)/upair;
	ex = units(l->v2->x - l->v1->x); ey = units(l->v2->y - l->v1->y);
	len2 = ex*ex + ey*ey;
	t = len2 > 0 ? ((x - units(l->v1->x))*ex + (y - units(l->v1->y))*ey)/len2 : 0;
	t = t < 0 ? 0 : t > 1 ? 1 : t;
	px = units(l->v1->x) + ex*t; py = units(l->v1->y) + ey*t;
	bottom = units(MAX(sectors[v->a].floorheight, sectors[v->b].floorheight));
	top = units(MIN(sectors[v->a].ceilingheight, sectors[v->b].ceilingheight));
	dx = px - x; dy = py - y;
	flat = sqrtf (dx*dx + dy*dy);
	// Up or down only toward a gap the thing is not level with (below a window's
	// sill, above its lintel), and from the floor only near the wall under it (within
	// TURN): the air runs along the floor and turns up there. A gap it is level with,
	// a doorway or a border inside a room, draws it along, never up or down.
	rise = low < bottom ? bottom - low + 1 : high > top ? top - high - 1 : 0;
	if (rise > 0 && mo->z <= mo->floorz && flat > 1)
	    rise *= flat < TURN ? 1 - flat/TURN : 0;
	r = sqrtf (flat*flat + rise*rise);
	if (r < 16) r = 16;
	// Spread over a half sphere round the opening, but no wider than the room it
	// crosses: further off in a closed room all its air streams on toward the opening
	// (E1M1's computer room, open at its side, stood still 15 m in).
	speed = q/MIN(2*3.14159265f*r*r, across);
	if (speed > q/open) speed = q/open;
	push = speed*speed;
	// On the side the air comes from, drawn to the opening; past it, blown on away from it.
	if (P_PointOnLineSide (mo->x, mo->y, l) != (up == v->b))
	    push = -push;
	len = sqrtf (flat*flat + rise*rise);
	if (len < 1) len = 1;
	a[0] += push*dx/len; a[1] += push*dy/len; a[2] += push*rise/len;
    }
}

static void Drag (mobj_t* mo)
{
    int		s = mo->subsector->sector - sectors;
    float	x = units(mo->x), y = units(mo->y);
    float	ax = 0, ay = 0, az = 0, thick, mass, face, across;

    if (sky (s) || !phobosair)
	return;
    thick = air[s];
    mass = P_Mass (mo);
    // The air pushes on what it meets: a body's 0.7 m^2 (DRAG), and a smaller thing of
    // the same build as its size, its mass to the two thirds (a 1 kg box: 18 cm a side).
    face = powf (mass/100, 2.0f/3);
    // The room's cross-section the air streams through: its width (as if square) by its height.
    across = sqrtf (area[s])*units(sectors[s].ceilingheight - sectors[s].floorheight);
    if (across < 1024) across = 1024;
    {
	// Gently (F4): a quarter of it, enough to drift things toward the opening.
	float	a[3], k = DRAG*face*thick/mass*(phobosair == 1 ? 0.25f : 1);
	Draft (mo, s, x, y, units(mo->z), units(mo->z + mo->height), across, a);
	ax = a[0]*k; ay = a[1]*k; az = a[2]*k;
	// A tumbling body or barrel: the air streams faster nearer the opening, so its two
	// halves are pulled apart and it turns (P_BodyDraft).
	if (P_Tumbling (mo))
	{
	    float mid = units(mo->z + mo->height/2), top[3], bottom[3];
	    Draft (mo, s, x, y, mid, units(mo->z + mo->height), across, top);
	    Draft (mo, s, x, y, units(mo->z), mid, across, bottom);
	    P_BodyDraft (mo, top, bottom, k);
	}
    }
    {
	float	total = sqrtf (ax*ax + ay*ay + az*az);
	if (total < 1e-4f)
	    return;
	if (total > MOST_PUSH)
	{
	    ax *= MOST_PUSH/total; ay *= MOST_PUSH/total; az *= MOST_PUSH/total;
	}
	// On (or within a step of) the floor in boots: the player's are the best on the base and never
	// let go (the gust only nudges him along); a monster's tear loose past
	// BOOT_GRIP, and until then they hold it down. Aloft, or once torn
	// loose, the whole draft carries it.
	if (P_Booted (mo) && (mo->player || mo->z - mo->floorz <= BOOT_REACH))
	{
	    if (mo->player)
	    {
		ax *= PLAYER_NUDGE; ay *= PLAYER_NUDGE; az = 0;
	    }
	    else if (P_Strain (mo, ax, ay, az, BOOT_GRIP))
		P_TearLoose (mo);
	    else
		az = 0;
	}
	P_Windblown (mo, (fixed_t)(sqrtf (ax*ax + ay*ay + az*az)*FRACUNIT));
	mo->momx += (fixed_t)(ax*FRACUNIT);
	mo->momy += (fixed_t)(ay*FRACUNIT);
	// Up off the floor, or down while aloft; the floor takes the rest.
	if (az > 0 || mo->z > mo->floorz)
	    mo->momz += (fixed_t)(az*FRACUNIT);
    }
}

// A flame decoration: in vacuum it goes out (its frames stop, unlit).
static boolean Flame (mobj_t* mo)
{
    switch (mo->sprite)
    {
      case SPR_FCAN: case SPR_CAND: case SPR_CBRA: case SPR_TBLU: case SPR_TGRN: case SPR_TRED:
      case SPR_SMBT: case SPR_SMGT: case SPR_SMRT:
	return true;
      default:
	return false;
    }
}

// What lies on the floor freezes as the renderer shows it (scene3d/decompression.cpp):
// open to space, frozen for ages; without air, frozen over FREEZE_TICS later; with air
// again, thawed THAW_TICS after that.
#define FREEZE_TICS	(8*TICRATE)
#define THAW_TICS	(57*TICRATE)

static void Freeze (void)
{
    int		i;

    for (i = 0; i < airsectors; ++i)
    {
	if (sky (i))
	{
	    iced[i] = 1;
	    continue;
	}
	if (air[i] < 0.25f)
	{
	    if (iced[i])
		iceTics[i] = 0;
	    else if (++iceTics[i] >= FREEZE_TICS)
	    {
		iced[i] = 1;
		iceTics[i] = 0;
	    }
	}
	else if (iced[i])
	{
	    if (++iceTics[i] >= THAW_TICS)
	    {
		iced[i] = 0;
		iceTics[i] = 0;
	    }
	}
	else
	    iceTics[i] = 0;
    }
}

boolean P_SectorIced (sector_t* sector)
{
    int		s = sector - sectors;

    if (!P_Phobos () || !iced || airsectors != numsectors || s < 0 || s >= airsectors)
	return false;
    return iced[s] || sky (s);
}

void P_AirThink (void)
{
    thinker_t*	th;

    P_PhobosMasses ();
    if (!P_Phobos () || !air || airsectors != numsectors)
	return;
    // A few tics for the window panes to be found (none without the 3D renderer).
    if (++airTics < 4 && !panesKnown)
	return;
    if (!settled)
    {
	int i;
	Settle ();
	// What starts the level without air has long been frozen.
	for (i = 0; i < airsectors; ++i)
	    iced[i] = air[i] < 0.25f;
    }
    Vent ();
    Freeze ();
    P_AdriftBegin ();
    for (th = thinkercap.next; th != &thinkercap; th = th->next)
    {
	mobj_t*	mo;
	if (th->function.acp1 != (actionf_p1)P_MobjThinker)
	    continue;
	mo = (mobj_t*)th;
	P_AdriftLand (mo);
	P_BodyThink (mo);
	if ((mo->flags & (MF_NOCLIP|MF_MISSILE|MF_NOSECTOR)) || Bolted (mo))
	    continue;
	Drag (mo);
	// Nothing here breathes: the zombies are dead already, the demons are
	// demons. Only fire needs the air.
	if (Flame (mo) && P_SectorAir (mo->subsector->sector) < 0.25f)
	{
	    mo->tics = -1;
	    mo->frame &= ~FF_FULLBRIGHT;
	}
    }
    P_AdriftEnd ();
}

int P_AirVents (phobos_vent_t* out, int most)
{
    int		n, count = 0;

    if (!P_Phobos () || !air || airsectors != numsectors)
	return 0;
    for (n = 0; n < ventcount && count < most; ++n)
    {
	vent_t*	v = &vents[n];
	line_t*	l = &lines[v->line];
	float	open;
	int	from;
	// A last trickle (under about 2% of the base's pressure behind it) is not listed.
	if (fabsf (v->flow) < CALM || (open = Opening (v)) <= 0 || fabsf (v->flow)/open < TRICKLE)
	    continue;
	from = v->flow > 0 ? v->a : v->b;
	out[count].x = units(l->v1->x + l->v2->x)/2;
	out[count].y = units(l->v1->y + l->v2->y)/2;
	out[count].z = (units(MAX(sectors[v->a].floorheight, sectors[v->b].floorheight))
			+ units(MIN(sectors[v->a].ceilingheight, sectors[v->b].ceilingheight)))/2;
	out[count].dx = -units(l->v2->y - l->v1->y);	// across the line, front to back
	out[count].dy = units(l->v2->x - l->v1->x);
	{
	    float len = sqrtf (out[count].dx*out[count].dx + out[count].dy*out[count].dy);
	    if (len > 0) { out[count].dx /= len; out[count].dy /= len; }
	}
	if (v->flow < 0) { out[count].dx = -out[count].dx; out[count].dy = -out[count].dy; }
	// How much air comes through, as how fast it would at the base's
	// pressure: the choked flow's sonic speed times the pressure behind it,
	// so it dies away as the room empties.
	out[count].speed = fabsf (v->flow)/open;
	out[count].width = sqrtf (open);
	out[count].sector = from;
	out[count].line = v->line;
	++count;
    }
    return count;
}


boolean P_GlassWhole (line_t* line)
{
    int		i = line - lines;

    return glass && airlines == numlines && i >= 0 && i < airlines && glass[i] == 1;
}


boolean P_AirPasses (line_t* line)
{
    int		i = line - lines;

    if (!line->frontsector || !line->backsector || !air || airlines != numlines || i < 0 || i >= airlines)
	return false;
    if (glass && glass[i] == 1)
	return false;
    return MIN(line->frontsector->ceilingheight, line->backsector->ceilingheight)
	 - MAX(line->frontsector->floorheight, line->backsector->floorheight) >= FRACUNIT;
}
