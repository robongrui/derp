//
// The library's people (scp_7179_b/README.md, People): seeded per gallery from
// the floor, indifferent to him until hurt, and the dead remembered until
// the morning (see p_library.h, p_library_local.h).
//

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "doomdef.h"
#include "doomstat.h"
#include "m_argv.h"
#include "i_system.h"
#include "m_random.h"
#include "r_main.h"
#include "p_library_local.h"

// Across: the aisles between the stacks.
#define AISLES		(LIBRARY_GALLERY/LIBRARY_GALLERY_STACKS)	// from one aisle's middle to the next
#define LANDINGS_AT(n)	((n) ? LIBRARY_LANDING + LIBRARY_SIDE : 0)	// the two landings' west ends

typedef enum { AT_SHELF, WANDERING, AT_RAILING, IN_BED, AT_KIOSK, IN_GANG } mood_t;
typedef struct
{
    mobj_t*	mo;
    int		place;		// gallery on the floor, or LANDINGS + landing
    int		slot;		// which of that place's people
    mood_t	mood;
    boolean	flees;		// hurt, it runs instead of fighting back
} person_t;
#define MAXPEOPLE	256
#define LANDINGS	100000	// places past this are landings
static person_t		people[MAXPEOPLE];
static int		numpeople;
static uint8_t		floorkey[32];	// the floor's people come from this
static boolean		empty;		// -libraryempty: nobody, to try the walking alone
// Gangs: on about one floor in GANG_FLOORS, one or two of them; and after a
// fight with one, none at all for QUIET, so a fight stays something to dread.
#define GANG_FLOORS	5
#define QUIET		(10*60*TICRATE)
static boolean		fought;		// a gang has fought him...
static int		lastfight;	// ...and the leveltime it last did
static boolean		gangsheld;	// this floor's gangs kept back: too soon after a fight
static float		crowdnow = 1;	// how crowded the floor being placed is (L_Crowd)
// The dead, by floor, place and slot, until the morning: they lie where
// they fell when he comes back.
#define MAXDEAD		4096
typedef struct { uint8_t floor[8]; int place, slot; } dead_t;
static dead_t		dead[MAXDEAD];
static int		numdead, nextdead;


void L_PeopleLevel (void)
{
    empty = M_CheckParm ("-libraryempty") != 0;
    numpeople = 0;
    fought = false;
}


int L_PeopleNear (void)
{
    return numpeople;
}


int L_DeadRemembered (void)
{
    return numdead;
}


void L_ForgetPerson (mobj_t* gone)
{
    int		i;

    for (i = 0; i < numpeople; i++)
	if (people[i].mo == gone)
	    people[i--] = people[--numpeople];
}


// Which of the floor's people mo is (its place and slot there), if it is one.
boolean L_PersonOf (const mobj_t* mo, int* place, int* slot)
{
    int		i;

    for (i = 0; i < numpeople; i++)
	if (people[i].mo == mo)
	{
	    *place = people[i].place;
	    *slot = people[i].slot;
	    return true;
	}
    return false;
}


// One of those standing at the railing, unhurt, that he can see near
// him (within SEEN, about where the far fog closes), picked by r (0-255);
// or nobody.
#define SEEN		2048
mobj_t* L_SeenAtTheRailing (mobj_t* viewer, int r)
{
    int		i, seen = 0;
    mobj_t*	mo;

    for (i = 0; i < numpeople; i++)
    {
	mo = people[i].mo;
	if (people[i].mood == AT_RAILING && P_LibraryIndifferent (mo)
	    && P_AproxDistance (mo->x - viewer->x, mo->y - viewer->y) < SEEN*FRACUNIT
	    && P_CheckSight (viewer, mo))
	    seen++;
    }
    if (!seen)
	return NULL;
    r = r*seen/256;
    for (i = 0; i < numpeople; i++)
    {
	mo = people[i].mo;
	if (people[i].mood == AT_RAILING && P_LibraryIndifferent (mo)
	    && P_AproxDistance (mo->x - viewer->x, mo->y - viewer->y) < SEEN*FRACUNIT
	    && P_CheckSight (viewer, mo) && r-- == 0)
	    return mo;
    }
    return NULL;
}


void L_ForgetDead (void)
{
    numdead = nextdead = 0;
}


static person_t* Person (mobj_t* mo)
{
    int		i;

    for (i = 0; i < numpeople; i++)
	if (people[i].mo == mo)
	    return &people[i];
    return NULL;
}


static boolean Remembered (int place, int slot)
{
    int		i;

    for (i = 0; i < numdead; i++)
	if (dead[i].place == place && dead[i].slot == slot && !memcmp (dead[i].floor, floorkey, 8))
	    return true;
    return false;
}


// A floor's key, from the seed and how many floors up it is from the one
// he started on (the seed's start), not its whole number: that has a
// million digits, too many to hash at every floor.
static void KeyOf (long long climbed, uint8_t* key)
{
    shake256	shake;
    uint8_t	count[8];
    int		i;

    for (i = 0; i < 8; i++)
	count[i] = (uint8_t)((unsigned long long)climbed >> (8*i));
    Shake256_Begin (&shake);
    Shake256_Absorb (&shake, "DERP library floor", 19);
    Shake256_Absorb (&shake, L_Seed ()->key, sizeof L_Seed ()->key);
    Shake256_Absorb (&shake, count, sizeof count);
    Shake256_Squeeze (&shake, key, 32);
}


// The floor he is on now.
static void FloorKey (void)
{
    KeyOf (L_Climbed (), floorkey);
}


// One of the place's people at (x, y) facing angle (in eighths), on the far
// side mirrored across the chasm: alive and as its mood has it, or lying
// dead if he killed it earlier.
static void Spawn (int place, int slot, mood_t mood, boolean flees, int x, int y, int angle, boolean far)
{
    mobj_t*	mo;
    person_t*	p;

    if (numpeople == MAXPEOPLE)
	return;
    if (far)
    {
	y = LIBRARY_FAR_WALL - y;
	angle = 8 - angle;
    }
    mo = P_SpawnMobj (x*FRACUNIT, y*FRACUNIT, ONFLOORZ, MT_POSSESSED);
    if (!P_CheckPosition (mo, mo->x, mo->y))
    {
	P_RemoveMobj (mo);
	return;
    }
    mo->angle = (angle_t)(angle & 7) * ANG45;
    if (Remembered (place, slot))
    {
	// As P_KillMobj leaves a body, without the dying.
	P_SetMobjState (mo, S_POSS_DIE5);
	mo->flags &= ~(MF_SHOOTABLE|MF_FLOAT|MF_SKULLFLY|MF_SOLID|MF_COUNTKILL);
	mo->flags |= MF_CORPSE|MF_DROPOFF;
	mo->height >>= 2;
	mo->health = 0;
	return;
    }
    p = &people[numpeople++];
    p->mo = mo;
    p->place = place;
    p->slot = slot;
    p->mood = mood;
    p->flees = flees;
    // Known as one of them first: the walk's first step is theirs, not A_Chase's.
    if (mood == WANDERING)
	P_SetMobjState (mo, mo->info->seestate);
    // Standing (at the shelves, the railing, the kiosk): still, held in its
    // stand pose; its stand animation would walk on the spot. Hurt, it
    // wakes (L_Bury).
    if (mood == AT_SHELF || mood == AT_RAILING || mood == AT_KIOSK)
	mo->tics = -1;
    // Asleep: lying as a body does (the renderer leaves out its blood,
    // P_LibraryAsleep), deaf to all until hurt (L_Bury wakes it).
    if (mood == IN_BED)
    {
	P_SetMobjState (mo, S_POSS_DIE5);
	L_Log ("asleep in a bed", mo);    }
}


// The bytes a place's people on the floor of key are drawn from.
static void Draw (const uint8_t* key, int place, uint8_t* r, int n)
{
    shake256	shake;
    uint8_t	id[4] = {place, place >> 8, place >> 16, place >> 24};

    Shake256_Begin (&shake);
    Shake256_Absorb (&shake, "DERP library people", 20);
    Shake256_Absorb (&shake, key, 32);
    Shake256_Absorb (&shake, id, sizeof id);
    Shake256_Squeeze (&shake, r, n);
}


// Where a place's people are, to spawn them or to draw them from afar
// (L_FloorFigures): each put by place.
typedef void (*placer_t) (int place, int slot, mood_t mood, boolean flees, int x, int y, int angle, boolean far);

// Whether gallery g of the floor of key has a gang: on one floor in
// GANG_FLOORS, one or two galleries, from the floor's key.
static boolean Ganged (const uint8_t* key, int g)
{
    int		galleries = 2*LIBRARY_SIDE_GALLERIES;

    if (key[0] % GANG_FLOORS || key[4] >= crowdnow*256)
	return false;
    return g == key[1] % galleries || ((key[2] & 1) && g == key[3] % galleries);
}


// Gatherings: a floor's people aren't spread evenly but bunch here and
// there round the ring, long empty stretches between (owner, 2026-10-30):
// a few to a floor, each a centre gallery, a reach either side and how many
// people a gallery at its centre has; any gallery a few besides (BASE).
// All of it times how crowded the floor is (L_Crowd).
#define GATHERINGS	(LANDINGS + 2)	// the place their bytes are drawn for
#define BASE		0.3f
#define MOSTHERE	4		// people to a gallery, at the most

static float Busy (const uint8_t* key, int g)
{
    uint8_t	r[1 + 3*5];
    int		galleries = 2*LIBRARY_SIDE_GALLERIES, n, i;
    float	busy = BASE;

    Draw (key, GATHERINGS, r, sizeof r);
    n = 2 + r[0] % 4;
    for (i = 0; i < n; i++)
    {
	int	centre = r[1+3*i] % galleries, reach = 1 + r[2+3*i] % 4, d = abs (g - centre);
	float	most = 1.5f + 2.5f*r[3+3*i]/255;

	if (d > galleries/2)
	    d = galleries - d;
	if (d <= reach)
	    busy += most*(1 - d/(float)(reach + 1));
    }
    return busy*crowdnow;
}


// A gallery's people on the floor of key: mostly nobody, one or two at the
// shelves, wandering or at the railing; on some floors a gang of three or
// four that hunts him. Gallery g round the ring: along the near side, then
// back along the far.
static void PlaceGallery (const uint8_t* key, int g, placer_t Spawn)
{
    uint8_t	r[32];
    boolean	far = g >= LIBRARY_SIDE_GALLERIES;
    int		west = LIBRARY_LANDING + (far ? 2*LIBRARY_SIDE_GALLERIES-1 - g : g)*LIBRARY_GALLERY;
    int		count, k, along;
    float	busy;

    Draw (key, g, r, sizeof r);
    if (Ganged (key, g) && !gangsheld)
    {
	int	gang = 3 + (r[3] & 1);
	int	facing = r[4] & 1 ? 0 : 4;

	for (k = 0; k < gang; k++)
	    Spawn (g, k, IN_GANG, false, west + 160 + 48*k, LIBRARY_STACKS + 24 + 24*k, facing, far);
	return;
    }
    busy = Busy (key, g);
    count = (int)busy + (r[0] < (busy - (int)busy)*256);
    if (count > MOSTHERE)
	count = MOSTHERE;
    for (k = 0; k < count; k++)
    {
	uint8_t	kind = r[5 + 3*k], where = r[6 + 3*k], how = r[7 + 3*k];
	mood_t	mood = kind < 140 ? AT_SHELF : kind < 190 ? WANDERING : AT_RAILING;

	// Along the gallery; at the shelves, in one of its aisles, facing
	// the books on either side, a little way in from its mouth, so that
	// passing on the walkway one is seen (owner, 2026-10-30).
	along = 32 + where*448/256;
	if (mood == AT_SHELF)
	    Spawn (g, k, mood, how < 102,
		   west + (where & 3)*AISLES + (how & 1 ? 12 : -12),
		   LIBRARY_STACKS - 24 - (how >> 1)*24/128,
		   how & 1 ? 0 : 4, far);
	else
	    Spawn (g, k, mood, how < 102,
		   west + along,
		   mood == AT_RAILING ? LIBRARY_NEAR_RAIL - LIBRARY_RAIL - 24 : LIBRARY_STACKS + LIBRARY_WALKWAY/2,
		   mood == AT_RAILING ? 2 : r[8] & 4, far);
    }
}


// A landing, at either end of the ring: the floor's common room. Its beds,
// two by the near wall and a row along the far (layout.h), each slept in
// now and then, the more of them the more tired he is (P_LibraryTired: the
// library keeps his hours); and before the kiosk sometimes nobody,
// sometimes one, now and then a few standing about it, some pairs facing
// each other as if talking. All of it as crowded as the floor (L_Crowd).
#define KIOSK_LIKELY	0.7f	// people at the kiosk at the top, besides a group
#define SLEEP_LIKELY	0.12f	// a bed slept in, wide awake...
#define SLEEP_TIRED	0.55f	// ...and how much more, worn out
#define ROUND_KIOSK	80	// how far from its front they stand: four, a body apart

// An eighth of a turn, from (x, y) toward (tx, ty).
static int Toward (int x, int y, int tx, int ty)
{
    return (int)floorf (atan2f ((float)(ty - y), (float)(tx - x))/(3.14159265f/4) + 8.5f) & 7;
}

static void PlaceLanding (const uint8_t* key, int n, placer_t Spawn)
{
    uint8_t	r[32];
    int		place = LANDINGS + n, west = LANDINGS_AT (n), b, k, count;
    int		kx = west + LIBRARY_LANDING/2 + LIBRARY_PLATFORM/2 + LIBRARY_KIOSK_FROM + LIBRARY_KIOSK_WIDTH/2;
    int		ky = LIBRARY_CORNICE + LIBRARY_KIOSK_DEPTH, x[4], y[4], facing[4];
    float	sleeps = (SLEEP_LIKELY + SLEEP_TIRED*P_LibraryTired ())*crowdnow, kiosk = KIOSK_LIKELY*crowdnow;

    Draw (key, place, r, sizeof r);
    for (b = 0; b < 2 + LIBRARY_DORM_BEDS; b++)
    {
	int	bx, by;

	if (r[b] >= sleeps*256)
	    continue;
	if (b < 2)
	{
	    bx = LIBRARY_CORNICE + LIBRARY_BED_INSET + LIBRARY_BED_WIDTH/2;
	    bx = west + (b ? LIBRARY_LANDING - bx : bx);
	    by = LIBRARY_CORNICE + LIBRARY_BED_LENGTH/2;
	}
	else
	{
	    bx = west + LIBRARY_DORM_FROM + (b - 2)*LIBRARY_DORM_STEP + LIBRARY_BED_WIDTH/2;
	    by = LIBRARY_FAR_WALL - LIBRARY_CORNICE - LIBRARY_BED_LENGTH/2;
	}
	Spawn (place, 10 + b, IN_BED, r[16 + b%8] < 102, bx, by, 2, false);
    }
    // Round the kiosk's front, in a loose half circle on the door's side
    // (the bed's on the other).
    count = (int)kiosk + (r[24] < (kiosk - (int)kiosk)*256);
    if (r[25] < 48*crowdnow)
	count += 1 + (r[26] & 1);
    if (count > 4)
	count = 4;
    for (k = 0; k < count; k++)
    {
	float	a = (75 + 30*k + r[27 + k] % 15)*3.14159265f/180;

	x[k] = kx + (int)(ROUND_KIOSK*cosf (a));
	y[k] = ky + (int)(ROUND_KIOSK*sinf (a));
	facing[k] = Toward (x[k], y[k], kx, ky);
    }
    for (k = 1; k < count; k += 2)
	if (r[31] & (1 << k))
	{
	    facing[k-1] = Toward (x[k-1], y[k-1], x[k], y[k]);
	    facing[k] = Toward (x[k], y[k], x[k-1], y[k-1]);
	}
    for (k = 0; k < count; k++)
	Spawn (place, k, AT_KIOSK, r[16 + k] < 102, x[k], y[k], facing[k], false);
}


// The bottom of the library, its last floor: on the shaft's floor a
// Cyberdemon stands, doing nothing at all. Nobody will ever come this far
// (falling, for longer than anything lasts). Any from a floor before is
// gone first.
static void Bottom (void)
{
    thinker_t*	th;
    thinker_t*	next;
    mobj_t*	mo;

    for (th = thinkercap.next; th != &thinkercap; th = next)
    {
	next = th->next;
	if (L_IsMobj (th) && ((mobj_t*)th)->type == MT_CYBORG)
	    P_RemoveMobj ((mobj_t*)th);
    }
    if (P_LibraryFloorsBelow () != 0)
	return;
    mo = P_SpawnMobj ((LIBRARY_LANDING + LIBRARY_SIDE/2)*FRACUNIT,
		      (LIBRARY_NEAR_RAIL + LIBRARY_FAR_RAIL)/2*FRACUNIT, ONFLOORZ, MT_CYBORG);
    mo->flags &= ~(MF_SHOOTABLE|MF_COUNTKILL);
    mo->angle = ANG180;
    mo->tics = -1;
    L_Log ("at the bottom", mo);
}


void L_PopulateAll (void)
{
    int		g;

    Bottom ();
    FloorKey ();
    if (empty)
	return;
    crowdnow = L_Crowd (L_Climbed ());
    gangsheld = fought && L_SINCE (lastfight) < QUIET;
    for (g = 0; g < 2*LIBRARY_SIDE_GALLERIES; g++)
	PlaceGallery (floorkey, g, Spawn);
    gangsheld = false;
    PlaceLanding (floorkey, 0, Spawn);
    PlaceLanding (floorkey, 1, Spawn);
}


// Another floor's people as figures to draw (p_library_figures.c): where
// each stands, facing which way, and whether it wanders. Nobody is spawned.
static l_figure_f	figure;

static void Figure (int place, int slot, mood_t mood, boolean flees, int x, int y, int angle, boolean far)
{
    (void)place; (void)slot; (void)flees;
    if (far)
    {
	y = LIBRARY_FAR_WALL - y;
	angle = 8 - angle;
    }
    figure (x, y, angle & 7, mood == WANDERING, mood == IN_BED);
}


void L_FloorFigures (long long climbed, l_figure_f each)
{
    uint8_t	key[32];
    int		g;

    if (empty)
	return;
    KeyOf (climbed, key);
    figure = each;
    crowdnow = L_Crowd (climbed);
    for (g = 0; g < 2*LIBRARY_SIDE_GALLERIES; g++)
	PlaceGallery (key, g, Figure);
    PlaceLanding (key, 0, Figure);
    PlaceLanding (key, 1, Figure);
}


// Those of his people dead since the last tic: remembered, and no longer
// people (their bodies are carried like anything else).
void L_Bury (void)
{
    int		i;
    player_t*	me = &players[consoleplayer];
    person_t*	hurt = me->attacker && me->damagecount ? Person (me->attacker) : NULL;

    // A fight with a gang: one of them hurt, or hurting him. The quiet after
    // it counts from its last tic.
    if (hurt && hurt->mood == IN_GANG)
	fought = true, lastfight = leveltime;
    for (i = 0; i < numpeople; i++)
	if (people[i].mood == IN_GANG && people[i].mo->health < people[i].mo->info->spawnhealth)
	    fought = true, lastfight = leveltime;

    for (i = 0; i < numpeople; i++)
    {
	mobj_t*	mo = people[i].mo;

	// Woken by being hurt: by the pain, or where pain didn't wake it,
	// up, at whoever did it, or only up. Then one of those about, no
	// longer abed.
	if (people[i].mood == IN_BED && mo->health > 0 && mo->health < mo->info->spawnhealth)
	{
	    if (P_LibraryAsleep (mo))
	    {
		P_SetMobjState (mo, mo->target ? mo->info->seestate : mo->info->spawnstate);
		if (!mo->target)
		    mo->tics = -1;	// up, and standing still
	    }
	    people[i].mood = AT_SHELF;
	    L_Log ("woke in a bed", mo);
	}
	// Standing still and hurt where pain didn't wake it: at whoever did it.
	if (mo->tics == -1 && mo->state == &states[mo->info->spawnstate] && mo->target
	    && mo->health > 0 && mo->health < mo->info->spawnhealth)
	    P_SetMobjState (mo, mo->info->seestate);
	if (mo->health > 0)
	    continue;
	memcpy (dead[nextdead].floor, floorkey, 8);
	dead[nextdead].place = people[i].place;
	dead[nextdead].slot = people[i].slot;
	nextdead = (nextdead + 1) % MAXDEAD;
	if (numdead < MAXDEAD)
	    numdead++;
	people[i--] = people[--numpeople];
    }
}


// Asleep in a bed: alive, and lying as the zombieman's body does (a
// sleeper here, or one drawn on another floor, P_LibraryFigures). Nobody
// else alive is ever in that frame.
boolean P_LibraryAsleep (const mobj_t* mo)
{
    return P_Library () && mo->health > 0 && mo->sprite == SPR_POSS
	&& (mo->frame & FF_FRAMEMASK) == states[S_POSS_DIE5].frame;
}


boolean P_LibraryIndifferent (mobj_t* actor)
{
    person_t*	p = P_Library () ? Person (actor) : NULL;

    // Set on by him, it is indifferent no more; anyone else's stray shot it
    // shrugs off (no fighting among themselves: only he starts one).
    return p && p->mood != IN_GANG && !(actor->target && actor->target->player);
}


extern boolean P_Move (mobj_t* actor);
extern boolean P_TryWalk (mobj_t* actor);
boolean P_LibraryChase (mobj_t* actor)
{
    person_t*	p = P_Library () ? Person (actor) : NULL;
    angle_t	away;
    int		dir, turn;

    if (!p || p->mood == IN_GANG)
	return false;

    // Wandering, minding nobody: along the walkway, turning now and then.
    if (P_LibraryIndifferent (actor))
    {
	if (--actor->movecount < 0 || !P_Move (actor))
	{
	    actor->movedir = P_Random () & 1 ? 0 : 4;	// DI_EAST, DI_WEST
	    actor->movecount = 8 + (P_Random () & 31);
	}
	actor->angle = (angle_t)actor->movedir << 29;
	return true;
    }

    // Hurt and of the kind that runs: away from what hurt it.
    if (p->flees && actor->target)
    {
	away = R_PointToAngle2 (actor->target->x, actor->target->y, actor->x, actor->y);
	dir = ((away + ANG45/2) >> 29) & 7;
	for (turn = 0; turn < 8; turn++)
	{
	    actor->movedir = (dir + (turn & 1 ? (turn + 1)/2 : -(turn/2))) & 7;
	    if (P_TryWalk (actor))
		break;
	}
	actor->angle = (angle_t)actor->movedir << 29;
	return true;
    }
    return false;
}


//
// Savegames (p_library_save.c): the people as their mobjs' places among
// the saved ones, and the dead remembered. The floor's key comes again
// from the floor's count, read before.
//
size_t L_PeopleSaveSize (void)
{
    return MAXPEOPLE*5*sizeof(int) + sizeof dead + 3*sizeof(int);
}


void L_ArchivePeople (void)
{
    int		i, number, flag;

    L_Put (&numpeople, sizeof numpeople);
    for (i = 0; i < numpeople; i++)
    {
	number = L_MobjOrdinal (people[i].mo);
	L_Put (&number, sizeof number);
	L_Put (&people[i].place, sizeof people[i].place);
	L_Put (&people[i].slot, sizeof people[i].slot);
	number = people[i].mood;
	L_Put (&number, sizeof number);
	flag = people[i].flees;
	L_Put (&flag, sizeof flag);
    }
    L_Put (&fought, sizeof fought);
    L_Put (&lastfight, sizeof lastfight);
    L_Put (&numdead, sizeof numdead);
    L_Put (&nextdead, sizeof nextdead);
    L_Put (dead, numdead*sizeof dead[0]);
}


void L_UnArchivePeople (void)
{
    int		i, number, flag;

    FloorKey ();
    L_Get (&numpeople, sizeof numpeople);
    if (numpeople < 0 || numpeople > MAXPEOPLE)
	I_Error ("Bad savegame: %d of the library's people", numpeople);
    for (i = 0; i < numpeople; i++)
    {
	L_Get (&number, sizeof number);
	people[i].mo = L_NthMobj (number);
	if (!people[i].mo)
	    I_Error ("Bad savegame: one of the library's people is missing");
	L_Get (&people[i].place, sizeof people[i].place);
	L_Get (&people[i].slot, sizeof people[i].slot);
	L_Get (&number, sizeof number);
	if (number < AT_SHELF || number > IN_GANG)
	    I_Error ("Bad savegame: one of the library's people is in no mood");
	people[i].mood = number;
	L_Get (&flag, sizeof flag);
	people[i].flees = flag != 0;
    }
    L_Get (&fought, sizeof fought);
    fought = fought != 0;
    L_Get (&lastfight, sizeof lastfight);
    L_Get (&numdead, sizeof numdead);
    L_Get (&nextdead, sizeof nextdead);
    if (numdead < 0 || numdead > MAXDEAD || nextdead < 0 || nextdead >= MAXDEAD)
	I_Error ("Bad savegame: the library's dead");
    L_Get (dead, numdead*sizeof dead[0]);
}


void L_RebasePeople (int off)
{
    lastfight -= off;
}
