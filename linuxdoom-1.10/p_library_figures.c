//
// The people of the floors round his, for the renderer to draw on the
// storeys it shows above and below (Game_LibraryFigures), so every floor
// has someone: a floor up and a floor down all of them, where they stand
// when he gets there (the same seeding, L_FloorFigures); farther down only
// those who wander, walking up and down the walkway by the clock. Nothing
// of it is in the game: no thinker, no random number, drawing only. (See
// p_library.h, p_library_local.h.)
//

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "doomdef.h"
#include "doomstat.h"
#include "info.h"
#include "p_library_local.h"

#define WHOLE		1	// floors up and down shown with all their people
#define DEEP		8	// floors down shown at all (the renderer's copies)
#define WALK		256	// how far a wanderer goes either way along the walkway
#define PACE		70.0	// and how fast, in units a second (a zombieman's walk)
#define MOST		1024

typedef struct
{
    short		x, y;
    signed char		storey, angle;	// floors up from his; eighths
    boolean		wanders, sleeps;
} figure_t;

static figure_t		figures[MOST];
static int		numfigures;
static long long	figuresfor = -1;	// the floor they are for (his climbed)
static uint8_t		seedfor[32];		// and the seed

// Each floor's people, kept for a while: falling, a floor is laid out once,
// not each time it passes another place among those round him.
#define KEPT		24
#define FLOOR_MOST	128
typedef struct
{
    long long	climbed;
    uint8_t	seed[32];
    int		count, used;
    figure_t	people[FLOOR_MOST];
} kept_t;
static kept_t		kept[KEPT];
static int		uses;
static kept_t*		filling;


static void Add (int x, int y, int angle, boolean wanders, boolean sleeps)
{
    figure_t*	f;

    if (filling->count == FLOOR_MOST)
	return;
    f = &filling->people[filling->count++];
    f->x = (short)x;
    f->y = (short)y;
    f->angle = (signed char)angle;
    f->wanders = wanders;
    f->sleeps = sleeps;
}


static const kept_t* Floor (long long climbed)
{
    kept_t*	k = &kept[0];
    int		i;

    for (i = 0; i < KEPT; i++)
    {
	if (kept[i].used && kept[i].climbed == climbed && !memcmp (kept[i].seed, L_Seed ()->key, 32))
	{
	    kept[i].used = ++uses;
	    return &kept[i];
	}
	if (kept[i].used < k->used)
	    k = &kept[i];
    }
    k->climbed = climbed;
    memcpy (k->seed, L_Seed ()->key, 32);
    k->count = 0;
    k->used = ++uses;
    filling = k;
    L_FloorFigures (climbed, Add);
    return k;
}


// Laid out again when he changes floors: the floors round him, those above
// only as far as there are floors above.
static void LayOut (void)
{
    long long	climbed = L_Climbed ();
    int		above = P_LibraryFloorsAbove ();
    int		storey, i;

    if (figuresfor == climbed && !memcmp (seedfor, L_Seed ()->key, sizeof seedfor))
	return;
    figuresfor = climbed;
    memcpy (seedfor, L_Seed ()->key, sizeof seedfor);
    numfigures = 0;
    for (storey = -DEEP; storey <= (above < WHOLE ? above : WHOLE); storey++)
    {
	const kept_t*	k;

	if (!storey)
	    continue;
	k = Floor (climbed + storey);
	for (i = 0; i < k->count && numfigures < MOST; i++)
	    if (abs (storey) <= WHOLE || k->people[i].wanders)
	    {
		figures[numfigures] = k->people[i];
		figures[numfigures++].storey = (signed char)storey;
	    }
    }
}


// Each figure where it is at seconds of game time: standing in its first
// pose, or walking to and fro from where it stands, facing the way it goes.
int P_LibraryFigures (library_figure_t* out, int most, double seconds)
{
    int		i, n = 0;

    if (!P_Library ())
	return 0;
    LayOut ();
    for (i = 0; i < numfigures && n < most; i++)
    {
	const figure_t*	f = &figures[i];
	library_figure_t* o = &out[n++];
	const state_t*	st;

	o->x = f->x;
	o->y = f->y;
	o->storey = f->storey;
	o->type = MT_POSSESSED;
	if (f->wanders)
	{
	    // A triangle wave over 4*WALK, its phase from where it stands.
	    double	period = 4*WALK/PACE;
	    double	t = fmod (seconds + (f->x*7 + f->y*3 + f->storey*131)%1000*0.013, period)/period;
	    double	along = t < 0.5 ? -WALK + 4*WALK*t : 3*WALK - 4*WALK*t;
	    int		step = (int)(seconds*TICRATE/4 + f->x) & 7;

	    o->x = f->x + (float)along;
	    if (o->x < LIBRARY_LANDING + 32)
		o->x = LIBRARY_LANDING + 32;
	    if (o->x > LIBRARY_LANDING + LIBRARY_SIDE - 32)
		o->x = LIBRARY_LANDING + LIBRARY_SIDE - 32;
	    o->angle = t < 0.5 ? 0 : ANG180;
	    st = &states[S_POSS_RUN1 + step];
	}
	else
	{
	    o->angle = (angle_t)f->angle*ANG45;
	    st = &states[f->sleeps ? S_POSS_DIE5 : S_POSS_STND];
	}
	o->sprite = st->sprite;
	o->frame = st->frame;
    }
    return n;
}
