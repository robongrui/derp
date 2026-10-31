//
// Inside the library of episode 0.5 (p_library.h): what its parts ask of
// each other. Each keeps its own state:
//
//  p_library.c		where he is: the seed, the floor and its count, the
//			gallery round the ring, the fixtures, the step a storey
//			up or down, the log, the kiosk, and the tic that runs
//			it all
//  p_library_people.c	the people: who is where, the dead remembered
//  p_library_shaft.c	up and down: the stairs, the railing, the fall
//  p_library_fallers.c	the others in the chasm: falling past, jumping
//  p_library_figures.c	the people of the floors round his, to draw
//  p_library_day.c	the day: tiredness, the lights, sleep, the morning
//  p_library_books.c	the books: aimed at, opened, read, remembered
//  p_library_reader.c	the reader: his answers, and whose a book is
//  p_library_kiosk.c	the kiosk: what he takes there, endless ammunition
//  p_library_save.c	savegames, each part writing its own
//

#ifndef __P_LIBRARY_LOCAL__
#define __P_LIBRARY_LOCAL__

#include "p_local.h"
#include "p_library.h"
#include "../platform/library/library.h"

// p_library.c
boolean L_IsMobj (thinker_t* th);
// Gone from the built part of the floor: nothing may still point at it.
void L_Forget (mobj_t* gone);
// Up (1) or down (-1) a floor, if the library goes on that way.
boolean L_ChangeFloor (int by);
// The floor's count has moved on a storey: what shares the player's shaft
// is stepped with him by dx and dz; what else is carried is gone.
void L_Lift (mobj_t* player, boolean (*shaft) (mobj_t*), int dx, int dz);
// All round him is made anew (a morning): what the renderer keeps goes.
void L_Renew (void);
// A note of what happened, with where (-librarylog; nothing without it).
void L_Log (const char* what, mobj_t* mo);
// Where he is, as a note to share (the kiosk's).
void L_PlaceNote (player_t* player);
const library_seed* L_Seed (void);
const library_format* L_Format (void);
const uint8_t* L_Floor (void);	// the floor he is on, as its digits
// How crowded the floor climbed floors up from his first is, as a share
// of the top floor's crowd (p_library_census.c): 1 at the top, thinning
// with depth, nobody at all below where the first of us could have got.
float L_Crowd (long long climbed);
long long L_Climbed (void);
// Whether the count of floors climbed has wrapped: so far down only the
// floor number says where he is (p_library.c).
boolean L_Deep (void);
size_t L_FloorSaveSize (void);
// A loaded clock renumbered back by off tics (p_library_save.c): each part's
// moments of it with it; and a new game's time spent here, none yet.
void L_RebasePeople (int off);
void L_RebaseDay (int off);
void L_RebaseKiosk (int off);
void L_NewTime (void);	// floors up from the one he started on
// The gallery round the ring at (x, y): east along the near side from 0,
// then west along the far side.
int L_Gallery (fixed_t x, fixed_t y);

// p_library_people.c
void L_PeopleLevel (void);	// a new level: nobody yet, or -libraryempty
void L_PopulateAll (void);	// a new floor: its key, and all its people
void L_Bury (void);		// the dead since the last tic, remembered
void L_ForgetPerson (mobj_t* gone);
void L_ForgetDead (void);	// the morning: the dead get up
int L_PeopleNear (void);
int L_DeadRemembered (void);
mobj_t* L_SeenAtTheRailing (mobj_t* viewer, int r);
boolean L_PersonOf (const mobj_t* mo, int* place, int* slot);
// The people of the floor climbed floors up from his start, each where it
// stands (angle in eighths) and whether it wanders; nobody is spawned.
typedef void (*l_figure_f) (int x, int y, int angle, boolean wanders, boolean sleeps);
void L_FloorFigures (long long climbed, l_figure_f each);

// p_library_books.c
void L_BooksLevel (void);

// p_library_kiosk.c
void L_KioskLevel (void);
void L_Kiosk (player_t* player);	// use facing it: its menu
void L_KioskTic (player_t* player);	// each tic: endless ammunition

// p_library_reader.c
void L_ReaderLevel (void);	// a new level: not answered, or -libraryreader
// Whose the book is, by its digits: nobody's (nearly all), a wording of his
// life that isn't his book, or his.
enum { BOOK_NOBODYS, BOOK_NEARLY, BOOK_HIS };
int L_Whose (const uint8_t* digits);

// p_library_shaft.c
void L_ShaftLevel (mobj_t* mo);
void L_Shaft (player_t* player);	// each tic: the stairs, the railing, the fall
boolean L_InChasm (mobj_t* mo);

// p_library_fallers.c
void L_FallersLevel (void);
void L_Fallers (player_t* player);	// each tic: the others falling, and someone now and then

// p_library_day.c
void L_DayLevel (void);
void L_Day (player_t* player);	// each tic: death, or the day's clock and sleep
void L_KnockOut (player_t* player);	// hit an edge falling: black, ringing, then the morning

// p_library_save.c: what the parts write and read, in order, and the mobjs
// by their place among those saved.
void L_Put (const void* data, size_t size);
void L_Get (void* data, size_t size);
int L_MobjOrdinal (mobj_t* mo);
mobj_t* L_NthMobj (int ordinal);
void L_ArchiveFloor (void);
void L_UnArchiveFloor (void);
void L_ArchiveShaft (void);
void L_UnArchiveShaft (void);
void L_ArchivePeople (void);
void L_UnArchivePeople (void);
void L_ArchiveDay (void);
void L_UnArchiveDay (void);
void L_ArchiveBooks (void);
void L_UnArchiveBooks (void);
void L_ArchiveReader (void);
void L_ArchiveKiosk (void);
void L_UnArchiveKiosk (void);
void L_ArchiveFallers (void);
void L_UnArchiveFallers (void);
size_t L_FallersSaveSize (void);
void L_UnArchiveReader (void);
size_t L_PeopleSaveSize (void);
size_t L_BooksSaveSize (void);
size_t L_ReaderSaveSize (void);

#endif
