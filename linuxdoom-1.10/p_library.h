//
// The library of episode 0.5, scp_7179_b (scp_7179_b/README.md):
// where the player stands in it, and the step that lets one built floor
// stand in for all of them.
//
// The map (DERPINFO library = true) is built as platform/library/layout.h
// says, by scp_7179_b/mapgen: one floor, a ring of galleries round the
// chasm with a landing at each end, all of it built. Up and down it is
// stepped: past half a storey on the stairs, or a storey's fall through the
// chasm, he is stepped a storey back, which looks the same, and the
// library's count of floors moves on instead.
//
// What shares his stairwell or the chasm is stepped with him (monsters,
// shots, corpses, pickups), so the game never sees a seam; the map's own
// fixtures (the lamps), built alike on every storey, stay. What else is
// carried was on the floor he left, and is gone.
//

#ifndef __P_LIBRARY__
#define __P_LIBRARY__

#include "p_mobj.h"
#include "../platform/library/layout.h"

// Tics since a moment of leveltime, right across the clock's wrap
// (unsigned, modulo 2^32): every timer of the library counts this way, never
// by comparing moments, and keeps "never" as a flag of its own, not -1.
#define L_SINCE(when)	((unsigned)leveltime - (unsigned)(when))

// Whether the map in play is the library.
boolean P_Library (void);

// After P_SetupLevel: the seed's start, and the player put there.
void P_LibraryLevel (void);

// After the thinkers each tic: the wrap along the walkway; the stairs, a
// storey at a time; use at the railing, and over it; the fall through the
// chasm, and catching a floor's edge on the way down (p_library.c).
void P_LibraryTicker (void);

// How far the steps have carried everything they carry in all since the
// level began, in map units: whatever smooths movement between tics (the
// renderer, the footsteps) adds what this grew by to where a carried thing
// was. Up and down (z, with x along the stairs' flight) by storeys, on the
// stairs and falling through the chasm.
void P_LibraryShift (float *dx, float *dy, float *dz);

// The times all round him was made anew since the game began: a floor
// changed (the stairs, the fall, waking) or a morning came. What the
// renderer keeps of the floor he was on (blood, marks) goes when it grows.
int P_LibraryRenewed (void);

// Whether sector repeats every storey above and below (the walkway's side
// of the shelves' wall: walkways, landings, the chasm, the far side, and the
// stairwell's door in the wall; not the stairwells behind it, which climb
// along their flight), for the renderer to draw the
// floors above and below as copies; and if so, a storey's height and its
// band's bottom (the slab under the walkway).
boolean P_LibraryStorey (int sector, fixed_t* storey, fixed_t* bottom);

// How many floors are above his (none on floor 1, the top), for the
// renderer's copies of the storeys above.
int P_LibraryFloorsAbove (void);

// The books (p_library_books.c). P_LibraryBooks, each tic before he moves
// (P_PlayerThink): the book he aims at, named; use opens it; reading, he
// stands still, his moves turn the pages, and true is returned. For the
// renderer: the book aimed at (its outline on the shelf, corners clockwise
// from the top, and whether he has read it); the open page's lines (each
// LIBRARY_LINE characters and a 0) with marks laid out alike (1: a letter of
// a real word), and its number from 1, or 0.
#define LIBRARY_LINE		80
#define LIBRARY_PAGE_LINES	40
boolean P_LibraryBooks (player_t* player);
boolean P_LibraryAim (float corners[4][3], boolean* read);
int P_LibraryPage (char* lines, char* marks);

// The reader (p_library_reader.c), for the questions he is asked when he
// first stands in the library (m_library.c): whether they are due (a game
// he plays, not recorded, not answered yet), whether one answer could be
// one (the facts in order: name, year of birth, birthplace, hometown, star
// sign, pet or nothing, favourite food), and all of them given.
#define LIBRARY_QUESTIONS	7
boolean P_LibraryNeedsReader (void);
boolean P_LibraryAnswerFits (int fact, const char* answer);
boolean P_LibrarySetReader (const char* answers[LIBRARY_QUESTIONS]);

// The modulus his book is found by: RSA-2048 unless code in the program
// sets another (decimal; NULL: back to RSA-2048). The game never does;
// only local dev tools, to find a book on a modulus they can factor.
void P_LibraryModulus (const char* decimal);

// The kiosk (p_library_kiosk.c; its menu: m_kiosk.c). What can be taken
// there, sent as a tic command (BT_SPECIAL | BTS_KIOSK | order) and taken in
// G_Ticker; the leveltime he last used it (the menu opens then; -1 never);
// whether ammunition runs out; whether the game has a weapon; his place
// as the kiosk last showed it (P_LibraryNote).
enum
{
    KIOSK_WEAPON = 0,			// + weapontype_t
    KIOSK_AMMO = 16,			// + ammotype_t
    KIOSK_ALL_AMMO = 20,
    KIOSK_BACKPACK,
    KIOSK_HEALTH,
    KIOSK_ARMOR,
    KIOSK_ENDLESS,			// on or off
    KIOSK_EVERYTHING
};
void P_LibraryKiosk (player_t* player, int order);
boolean P_LibraryKioskVisited (int* when);
boolean P_LibraryEndless (void);
boolean P_LibraryHasWeapon (int weapon);

// His deaths so far this game (an edge's knock too), and in *when the
// leveltime of the last (-1: none), for the menu's word after the first.
int P_LibraryDeaths (int* when);

// What one of the library's people says over its head at seconds of game
// time, or NULL, and how much of it shows (0-1, coming and going)
// (p_library_babel.c): drawing only.
const char* P_LibrarySays (const mobj_t* mo, double seconds, float* shown);

// Saving or loading (g_game.c): where the save's buffer ends, so the
// library's state is never written or read past it; and the tics spent in
// the library in all, across the level clock's renumberings
// (p_library_save.c).
void P_LibraryBufferEnd (byte* end);
long long P_LibraryTicsHere (void);

// How many floors are under his (0 at the bottom, the last floor; more
// than a few counted as BELOW_MOST, 8), for the renderer's copies. And
// straight to the bottom, for local dev tools only: the game never goes
// there but by falling, for longer than anything lasts.
int P_LibraryFloorsBelow (void);
void P_LibraryToBottom (void);

// His ears ringing after hitting an edge falling (0-1), for the sound.
float P_LibraryRinging (void);
int P_LibraryKnocks (void);

// Whether he is falling through the chasm, and how fast (units a tic).
boolean P_LibraryFall (float* x, float* y, float* z);
// How fast he fell at the last tic, for the sound (safe from its thread):
// 0 out of the chasm, paused or in a menu.
float P_LibraryFallSpeed (void);
// How open he stands to the chasm (0-1) and on which side of him it lies
// as he faces (*side: -1 left .. 1 right), for the sound; and how tired he
// is (0 until the yawns .. 1 about to drop), for his steps. Safe from the
// sound's thread; 0 out of the library, paused or in a menu.
float P_LibraryChasm (float* side);
float P_LibraryTired (void);

// The people of the floors round his, for the renderer to draw on the
// storeys above and below (p_library_figures.c): each where it is at
// seconds of game time (x, y on its floor, as on his), facing angle, its
// sprite and frame, its kind, and its floor (storey: up from his, from -8).
// Drawing only: they are in no thinker.
typedef struct
{
    float	x, y;
    angle_t	angle;
    int		sprite, frame, type, storey;
} library_figure_t;
int P_LibraryFigures (library_figure_t* out, int most, double seconds);

// Savegames (G_DoSaveGame, G_DoLoadGame): the library's state after the
// specials, on the library only; P_LibrarySaveSize is its most.
size_t P_LibrarySaveSize (void);
void P_ArchiveLibrary (void);
void P_UnArchiveLibrary (void);

// Whether the wrap carries thing (everything but the map's fixtures).
boolean P_LibraryCarried (const mobj_t* thing);

// The library's people (p_library.c): seeded per gallery from the floor,
// indifferent to him until hurt (A_Look does nothing for them), then
// fighting back or, for some, running away; a rare gang hunts him as any
// monster would. P_LibraryChase does A_Chase's work for those it handles
// (wandering, fleeing) and says so.
boolean P_LibraryIndifferent (mobj_t* actor);
boolean P_LibraryChase (mobj_t* actor);
// Whether one lies asleep in a bed (a figure too): drawn without the
// body frame's blood.
boolean P_LibraryAsleep (const mobj_t* mo);

// The day (p_library.c): how closed his eyes are (lids, 0 to 1: tired
// blinks, falling asleep) and how dark it is for him (black, 0 to 1: asleep,
// passed out, dead), for the renderer to draw; false off the library.
boolean P_LibraryEyes (float* lids, float* black);
// The yawns so far: one is due each time this grows (for the sound).
int P_LibraryYawns (void);
// His legs' push, less as he tires (P_MovePlayer).
fixed_t P_LibraryLegs (fixed_t thrust);
// Where he was at the kiosk last, as a note to copy ("" before).
const char* P_LibraryNote (void);

#endif
