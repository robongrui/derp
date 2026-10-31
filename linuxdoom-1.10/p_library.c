//
// The library of episode 0.5: where the player stands in it, and the step a
// storey up or down (see p_library.h). The people, the shaft, the day and
// savegames are parts of their own (p_library_local.h).
//

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "doomdef.h"
#include "doomstat.h"
#include "i_system.h"
#include "m_argv.h"
#include "r_main.h"
#include "tables.h"
#include "../platform/derp_info.h"
#include "p_library_local.h"

// The books (scp_7179_b/README.md, The books): Borges' own, 410 pages of
// 40 lines of 80 characters over the 26 letters, space, comma and period.
// A floor is the galleries round its ring, a gallery its shelves (the
// stacks' faces and the back wall), a shelf the books on it as the
// bookcase texture shows them (layout.h).
static const library_format	format =
{
    "abcdefghijklmnopqrstuvwxyz ,.", 29, 410*40*80,
    2*LIBRARY_SIDE_GALLERIES, LIBRARY_GALLERY_SHELVES, LIBRARY_SHELF_BOOKS
};

static library_seed	seed;
#define LIBRARY_SEED	"DERP"		// everyone's library (-libraryseed: another, for tests)
static uint8_t*		floordigits;	// the floor he is on; then room for an address
static uint8_t*		topfloor;	// the highest floor whole, from the level's start
static long long	climbed;	// floors up from the one he started on, in 64 bits...
static long long	laps;		// ...and how often that has wrapped (below: negative)
static int		floorsbelow;	// floors under his, up to BELOW_MOST (the renderer's copies)
#define BELOW_MOST	8
static fixed_t		shiftx, shiftz;	// how far the stairs and the fall have carried things since the level began
static unsigned		renewed;	// times all round him was made anew (a floor, a morning); it may wrap
static boolean		logging;	// -librarylog: each gallery, as a note
static int		logged;		// the gallery last written
static char		note[LIBRARY_NOTE_SIZE];	// where he was at the kiosk last

// The map's fixtures, which a storey's step leaves where they are: what
// neither moves, nor can be shot, picked up or killed (the lamps).
#define MAXFIXED	512
static mobj_t*		fixed[MAXFIXED];
static int		numfixed;


boolean P_Library (void)
{
    // One built corridor stands for one player's gallery at a time.
    return DerpInfo_Map.library == 1 && !netgame;
}


const library_seed* L_Seed (void)
{
    return &seed;
}


const library_format* L_Format (void)
{
    return &format;
}


const uint8_t* L_Floor (void)
{
    return floordigits;
}


long long L_Climbed (void)
{
    return climbed;
}


int P_LibraryFloorsAbove (void)
{
    return !P_Library () ? 0 : laps || climbed < -1000000 ? 1000000 : (int)-climbed;
}


int P_LibraryFloorsBelow (void)
{
    return P_Library () ? floorsbelow : 0;
}


boolean L_Deep (void)
{
    return laps != 0;
}


// How many floors are under his: the floor number counts up from the
// bottom, 0, so only its last digit can be anything once the rest are 0.
// Cheap: the scan stops at the first digit that isn't (everywhere but the
// last few floors of the library, at its first).
static void Below (void)
{
    size_t	i;

    for (i = 0; i + 1 < format.length && !floordigits[i]; i++)
	;
    floorsbelow = i + 1 == format.length && floordigits[i] < BELOW_MOST ? floordigits[i] : BELOW_MOST;
}


// The gallery round the ring at (x, y): east along the near side from 0,
// then west along the far side; on a landing, the gallery beside it.
int L_Gallery (fixed_t x, fixed_t y)
{
    int		along = x/FRACUNIT - LIBRARY_LANDING;
    int		g = along < 0 ? 0 : along >= LIBRARY_SIDE ? LIBRARY_SIDE_GALLERIES-1 : along/LIBRARY_GALLERY;

    if (y/FRACUNIT > LIBRARY_NEAR_RAIL + LIBRARY_CHASM/2)
	return 2*LIBRARY_SIDE_GALLERIES-1 - g;
    return g;
}


// Gallery g's middle along the walkway, and the way on round the ring.
static void Walkway (int g, fixed_t* x, fixed_t* y, angle_t* way)
{
    boolean	far = g >= LIBRARY_SIDE_GALLERIES;

    if (far)
	g = 2*LIBRARY_SIDE_GALLERIES-1 - g;
    *x = (LIBRARY_LANDING + g*LIBRARY_GALLERY + LIBRARY_GALLERY/2)*FRACUNIT;
    *y = (far ? LIBRARY_FAR_WALL - LIBRARY_STACKS - LIBRARY_WALKWAY/2 : LIBRARY_STACKS + LIBRARY_WALKWAY/2)*FRACUNIT;
    *way = far ? ANG180 : 0;
}


boolean L_IsMobj (thinker_t* th)
{
    return th->function.acp1 == (actionf_p1)P_MobjThinker;
}


void L_Log (const char* what, mobj_t* mo)
{
    char		text[LIBRARY_NOTE_SIZE];
    library_place	place = {0, 0, 0};
    uint8_t*		address = floordigits + format.length;
    thinker_t*		th;
    int			living = 0, bodies = 0;

    if (!logging)
	return;
    for (th = thinkercap.next; th != &thinkercap; th = th->next)
    {
	if (L_IsMobj (th) && ((mobj_t*)th)->flags & MF_COUNTKILL && ((mobj_t*)th)->health > 0)
	    living++;
	if (L_IsMobj (th) && ((mobj_t*)th)->type == MT_POSSESSED && ((mobj_t*)th)->health <= 0)
	    bodies++;
    }
    place.gallery = L_Gallery (mo->x, mo->y);
    if (Library_Join (&format, floordigits, &place, address)
	&& Library_Note (&format, &seed, address, text))
	fprintf (stderr, "library: %s: %s (%d living near, %d of them people, %d bodies, %d dead remembered, %d health, ring gallery %d)\n",
		 what, text, living, L_PeopleNear (), bodies, L_DeadRemembered (), mo->health, place.gallery);
}


boolean P_LibraryCarried (const mobj_t* thing)
{
    int		i;

    if (!P_Library ())
	return false;
    for (i = 0; i < numfixed; i++)
	if (fixed[i] == thing)
	    return false;
    return true;
}


static void FindFixtures (void)
{
    thinker_t*	th;
    mobj_t*	mo;

    numfixed = 0;
    for (th = thinkercap.next; th != &thinkercap; th = th->next)
    {
	if (!L_IsMobj (th))
	    continue;
	mo = (mobj_t*)th;
	if (!mo->player && numfixed < MAXFIXED
	    && !(mo->flags & (MF_SHOOTABLE|MF_SPECIAL|MF_MISSILE|MF_COUNTKILL|MF_CORPSE)))
	    fixed[numfixed++] = mo;
    }
}


// The floor he is on: the top (floor 1, where everyone starts), stepped
// down by the floors he has gone down (climbed, never above 0).
// The floor, room for an address (the log's, the kiosk's), the top.
static void FloorRoom (void)
{
    if (!floordigits)
	floordigits = malloc (3*format.length);
    if (!floordigits)
	I_Error ("P_LibraryLevel: no room for the library's floor");
    topfloor = floordigits + 2*format.length;
    Library_TopFloor (&format, topfloor);
}


static void StartFloor (void)
{
    FloorRoom ();
    memcpy (floordigits, topfloor, format.length);
    if (climbed > 0 || (climbed && !Library_Step (&format, floordigits, climbed)))
	I_Error ("Bad savegame: the library has no floor %lld", 1 - climbed);
    Below ();
}


// Where on floor 1 he starts: anywhere. In a demo (recorded, played or in
// the scenarios) where the seed's own address is, so it plays the same again.
// A played demo is known by its command line: DOOM sets demoplayback only
// once the demo's level is loaded (G_DoPlayDemo, after G_InitNew).
static int StartGallery (void)
{
    library_place	place;
    uint8_t*		address;
    uint8_t*		floor;

    if (!demoplayback && !demorecording && !M_CheckParm ("-playdemo") && !M_CheckParm ("-timedemo"))
	return (int)((unsigned)time (NULL) * 2654435761u >> 8) % format.galleries;
    address = malloc (2*format.length);
    if (!address)
	return 0;
    floor = address + format.length;
    Library_Start (&format, &seed, address);
    Library_Split (&format, address, floor, &place);
    free (address);
    return place.gallery;
}


void P_LibraryLevel (void)
{
    mobj_t*		mo = players[consoleplayer].mo;
    int			p;

    if (!P_Library () || !mo)
	return;

    p = M_CheckParm ("-libraryseed");
    Library_Seed (p && p < myargc-1 ? myargv[p+1] : LIBRARY_SEED, &seed);
    logging = M_CheckParm ("-librarylog") != 0;
    climbed = laps = 0;
    L_NewTime ();
    StartFloor ();
    // -librarydown n: start n floors down, to try what only shows below the
    // top (the floors above drawn as copies). Nothing near any book: the
    // floors run on for some 10^1.9 million more.
    p = M_CheckParm ("-librarydown");
    for (p = p && p < myargc-1 ? atoi (myargv[p+1]) : 0; p > 0 && L_ChangeFloor (-1); p--)
	;

    shiftx = shiftz = 0;
    FindFixtures ();
    L_PeopleLevel ();
    L_PopulateAll ();
    L_DayLevel ();
    L_BooksLevel ();
    L_ReaderLevel ();
    L_FallersLevel ();
    L_KioskLevel ();

    // The middle of his gallery's walkway, looking on round the ring.
    P_UnsetThingPosition (mo);
    Walkway (StartGallery (), &mo->x, &mo->y, &mo->angle);
    P_SetThingPosition (mo);
    P_CheckPosition (mo, mo->x, mo->y);
    mo->floorz = tmfloorz;
    mo->ceilingz = tmceilingz;
    mo->z = mo->floorz;

    // -libraryplace x y angle: somewhere else in the built part, to try
    // things there (the stairs, the railing).
    p = M_CheckParm ("-libraryplace");
    if (p && p < myargc-3)
    {
	P_UnsetThingPosition (mo);
	mo->x = atoi (myargv[p+1])*FRACUNIT;
	mo->y = atoi (myargv[p+2])*FRACUNIT;
	P_SetThingPosition (mo);
	mo->angle = (angle_t)(atoi (myargv[p+3]) / 45) * ANG45;
	P_CheckPosition (mo, mo->x, mo->y);
	mo->floorz = tmfloorz;
	mo->ceilingz = tmceilingz;
	mo->z = mo->floorz;
	players[consoleplayer].viewz = mo->z + players[consoleplayer].viewheight;
    }
    L_ShaftLevel (mo);

    logged = L_Gallery (mo->x, mo->y);
    L_Log ("start", mo);
}


void L_Forget (mobj_t* gone)
{
    thinker_t*	th;
    mobj_t*	mo;
    int		i;

    for (th = thinkercap.next; th != &thinkercap; th = th->next)
    {
	if (!L_IsMobj (th))
	    continue;
	mo = (mobj_t*)th;
	if (mo->target == gone)
	    mo->target = NULL;
	if (mo->tracer == gone)
	    mo->tracer = NULL;
    }
    for (i = 0; i < numsectors; i++)
	if (sectors[i].soundtarget == gone)
	    sectors[i].soundtarget = NULL;
    for (i = 0; i < MAXPLAYERS; i++)
	if (players[i].attacker == gone)
	    players[i].attacker = NULL;
    L_ForgetPerson (gone);
    P_RemoveMobj (gone);
}


void L_Renew (void)
{
    renewed++;
}


// Up (1) or down (-1) a floor, if the library goes on that way: past the
// lowest the step fails, past the highest whole floor (topfloor) it goes
// back. Both stop at the first digit that settles it, so a floor change
// costs little however long the number is.
boolean L_ChangeFloor (int by)
{
    long long	next;

    if (!Library_Step (&format, floordigits, by))
	return false;
    if (by > 0 && memcmp (floordigits, topfloor, format.length) > 0)
    {
	Library_Step (&format, floordigits, -by);
	return false;
    }
    // Floors climbed is 64 bits: some 40 billion years of falling. Past
    // them it wraps and laps counts how often; the floor number goes on,
    // to the bottom.
    next = (long long)((unsigned long long)climbed + (unsigned long long)(long long)by);
    if (by < 0 && next > climbed)
	laps--;
    if (by > 0 && next < climbed)
	laps++;
    climbed = next;
    renewed++;
    Below ();
    return true;
}


void P_LibraryToBottom (void)
{
    if (!P_Library () || !floordigits)
	return;
    memset (floordigits, 0, format.length);
    climbed = 0;
    laps = -1;		// more laps than anything can count
    renewed++;
    Below ();
    L_PopulateAll ();
}


#define WRAP		(8192*FRACUNIT)	// GAME_SHIFT_WRAP

static fixed_t Wrapped (fixed_t shift)
{
    while (shift >= WRAP/2)
	shift -= WRAP;
    while (shift < -WRAP/2)
	shift += WRAP;
    return shift;
}


void L_Lift (mobj_t* player, boolean (*shaft) (mobj_t*), int dx, int dz)
{
    thinker_t*	th;
    thinker_t*	next;
    mobj_t*	mo;

    for (th = thinkercap.next; th != &thinkercap; th = next)
    {
	next = th->next;
	if (!L_IsMobj (th) || !P_LibraryCarried ((mobj_t*)th))
	    continue;
	mo = (mobj_t*)th;
	if (mo != player && !shaft (mo))
	{
	    L_Forget (mo);
	    continue;
	}
	P_UnsetThingPosition (mo);
	mo->x += dx*FRACUNIT;
	P_SetThingPosition (mo);
	mo->z += dz*FRACUNIT;
	P_CheckPosition (mo, mo->x, mo->y);
	mo->floorz = tmfloorz;
	mo->ceilingz = tmceilingz;
	if (mo->player)
	    mo->player->viewz += dz*FRACUNIT;
    }
    // Kept wrapped round GAME_SHIFT_WRAP (8192) units, as the renderer takes
    // only what it grew by: a fall goes on for ever, and fixed point overflowed
    // after some 170 storeys.
    shiftx = Wrapped (shiftx + dx*FRACUNIT);
    shiftz = Wrapped (shiftz + dz*FRACUNIT);
}


// His place, as the kiosk shows it (p_library_kiosk.c, m_kiosk.c): a note
// without its seed when the library is everyone's.
void L_PlaceNote (player_t* player)
{
    library_place	place = {0, 0, 0};
    char*		rest;

    place.gallery = L_Gallery (player->mo->x, player->mo->y);
    if (!Library_Join (&format, floordigits, &place, floordigits + format.length)
	|| !Library_Note (&format, &seed, floordigits + format.length, note))
	note[0] = 0;
    else if (!strcmp (seed.text, LIBRARY_SEED) && (rest = strstr (note, " / ")) != NULL)
	memmove (note, rest + 3, strlen (rest + 3) + 1);
}


const char* P_LibraryNote (void)
{
    return note;
}


// He is a zombieman, as everyone here (scp_7179_b/README.md, The
// player): drawn so (the renderer shows his body only as his shadow; the
// bar's face is st_library.c). The player's frames are the zombieman's up
// to the pain frame; past it the zombieman has fewer dying ones.
static void Body (mobj_t* mo)
{
    int		frame = mo->frame & FF_FRAMEMASK;

    if (mo->sprite != SPR_PLAY)
	return;
    mo->sprite = SPR_POSS;
    if (frame >= 14)
	frame -= 2;			// gibbed: O-W as M-U
    else if (frame > 11)
	frame = 11;			// dying: H-N as H-L
    mo->frame = (mo->frame & ~FF_FRAMEMASK) | frame;
}


void P_LibraryTicker (void)
{
    mobj_t*	mo = players[consoleplayer].mo;

    if (!P_Library () || !mo || !floordigits)
	return;

    L_Bury ();
    L_Day (&players[consoleplayer]);
    mo = players[consoleplayer].mo;
    L_Shaft (&players[consoleplayer]);
    L_Fallers (&players[consoleplayer]);
    L_KioskTic (&players[consoleplayer]);
    Body (players[consoleplayer].mo);

    if (logging && L_Gallery (mo->x, mo->y) != logged)
    {
	logged = L_Gallery (mo->x, mo->y);
	L_Log ("gallery", mo);
    }
}


void P_LibraryShift (float* dx, float* dy, float* dz)
{
    *dx = shiftx/(float)FRACUNIT;
    *dy = 0;
    *dz = shiftz/(float)FRACUNIT;
}


int P_LibraryRenewed (void)
{
    return renewed;
}


boolean P_LibraryStorey (int sector, fixed_t* storey, fixed_t* bottom)
{
    if (!P_Library () || sector < 0 || sector >= numsectors
	|| sectors[sector].soundorg.y <= -LIBRARY_WALL*FRACUNIT)
	return false;
    *storey = LIBRARY_STOREY*FRACUNIT;
    *bottom = (LIBRARY_HEADROOM - LIBRARY_STOREY)*FRACUNIT;
    return true;
}


// Savegames (p_library_save.c): the seed and the floors climbed (the
// floor's million-digit number isn't written: it comes again from them),
// the steps so far, and the kiosk's note.
// The floor's share of a save: the floor number in full past the count's
// 64 bits (a megabyte or so; nobody falls that far, a digger might).
size_t L_FloorSaveSize (void)
{
    return sizeof seed.text + 2*sizeof climbed + (laps ? format.length : 0) + 64 + sizeof note;
}


void L_ArchiveFloor (void)
{
    L_Put (seed.text, sizeof seed.text);
    L_Put (&climbed, sizeof climbed);
    L_Put (&laps, sizeof laps);
    // Past the count's 64 bits only the floor number says where he is.
    if (laps)
	L_Put (floordigits, format.length);
    L_Put (&shiftx, sizeof shiftx);
    L_Put (&shiftz, sizeof shiftz);
    L_Put (&renewed, sizeof renewed);
    L_Put (note, sizeof note);
}


// The lamps are the loaded ones now.
void L_UnArchiveFloor (void)
{
    char	text[sizeof seed.text];

    L_Get (text, sizeof text);
    text[sizeof text - 1] = 0;
    Library_Seed (text, &seed);
    L_Get (&climbed, sizeof climbed);
    L_Get (&laps, sizeof laps);
    if (!laps)
	StartFloor ();
    else
    {
	size_t	i;

	FloorRoom ();
	L_Get (floordigits, format.length);
	for (i = 0; i < format.length; i++)
	    if (floordigits[i] >= format.symbols)
		I_Error ("Bad savegame: the library's floor number");
	if (memcmp (floordigits, topfloor, format.length) > 0)
	    I_Error ("Bad savegame: the library has no floor above its top");
	Below ();
    }
    L_Get (&shiftx, sizeof shiftx);
    L_Get (&shiftz, sizeof shiftz);
    shiftx = Wrapped (shiftx);
    shiftz = Wrapped (shiftz);
    L_Get (&renewed, sizeof renewed);
    L_Get (note, sizeof note);
    note[sizeof note - 1] = 0;
    FindFixtures ();
}
