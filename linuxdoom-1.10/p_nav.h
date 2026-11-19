// Where DERP's monsters can go (the plan's "room map" and navigation).
// Vanilla Doom has none: a monster steps one of eight ways at its target
// and P_TryMove says yes or no. Built at level load while DERP's monsters
// play (p_derp.h), from the map alone, so every run builds the same.
//
// The grid: 16-unit cells, one layer per walker size (radius x height).
// A cell is standable if the size fits somewhere within 8 units of its
// middle by the game's own line rules (P_CheckPosition's, without the
// things that move), and keeps where it fits and the floor there. Doors a
// monster opens count as open; static solid things (pillars, lamps) take
// their room. Moving sectors (lifts, raised floors) are rebuilt where they
// are when they move. Teleporter lines are never walked across.
//
// The rooms: sectors joined by wide openings a monster walks through; a
// door or a narrow gap is a link between two rooms.

#ifndef __P_NAV__
#define __P_NAV__

#include "doomtype.h"
#include "m_fixed.h"
#include "p_mobj.h"
#include "r_defs.h"

#define NAVCELL		16

// At the end of level setup: the rooms, and the grid for each walker size
// on the map. A no-op unless DERP's monsters play.
void P_NavLevel (void);
// Each tic: moved sectors rebuilt, every few tics.
void P_NavThink (void);
// Moved sectors rebuilt now (the tests: the grid as a fresh build would be).
void P_NavSettle (void);
// A sector's floor or ceiling moved (P_ChangeSector).
void P_NavSectorMoved (sector_t* sector);
// Whether the navigation exists for this level.
boolean P_NavReady (void);
// After a load: the grid brought to the sectors' heights as saved (floors
// and bridges that moved before the save, with no mover left to say so).
void P_NavSync (void);

// The layer for mo's size, built on first use; -1 for what does not walk
// (fliers) or without navigation.
int P_NavClassOf (mobj_t* mo);
// How many cells the grid has (each layer the same).
int P_NavCells (void);
// Which build of the grid this is: anything kept from the map's doors and
// floors holds for one.
int P_NavBuild (void);
int P_NavClassFor (int radius, int height);	// in units
// The cell under (x, y), or the nearest standable one within two cells;
// -1 if none.
int P_NavCellAt (int nav, fixed_t x, fixed_t y);
boolean P_NavStandable (int nav, int cell);
// Whether a body stands there only with a door open (a monster opens it on
// the way: it costs a wait).
boolean P_NavDoor (int nav, int cell);
// Where a body stands in the cell, and its floor there.
void P_NavPoint (int nav, int cell, fixed_t* x, fixed_t* y, fixed_t* floor);
// The height a climber's jump from one cell to another must clear (the
// higher floor, or a sill between); MININT if it is no jump known.
fixed_t P_NavJumpTop (int nav, int from, int to);
// Whether a cell's floor hurts (nukage, slime, lava): routes go round such
// cells if they reasonably can, no jump or blink lands there.
boolean P_NavHurts (int cell);
// A hop from (ax, ay) to about (bx, by), on much the same floor with nothing in the
// way: whether it can be done, and the landing.
boolean P_NavHop (int nav, fixed_t ax, fixed_t ay, fixed_t bx, fixed_t by, fixed_t* x, fixed_t* y, fixed_t* z);
// He has used a secret door: from now on it is a door, and they follow him through.
void P_NavSecretFound (line_t* ld);
// Whether jumps from a to b and from c to d are one and the same, near enough.
boolean P_NavSameJump (int a, int b, int c, int d);
// The nearest floor near (x, y) that does not hurt and is a step's height from it.
boolean P_NavDryNear (int nav, fixed_t x, fixed_t y, fixed_t* dx, fixed_t* dy);
// Whether two cells are neighbours (one of the eight round the other).
boolean P_NavNeighbours (int a, int b);
// Whether a two-sided line has nothing in it: no bars. (The renderer's glass is
// no part of the map.)
boolean P_NavOpenAir (const line_t* ld);
// Whether two cells next on a climber's path are a jump: no walkable step
// between them (they may be neighbours: a jump can land on the next cell).
boolean P_NavJump (int nav, int a, int b);
// Whether one cell steps to a neighbouring one (the eight around it).
boolean P_NavStep (int nav, int from, int to);
// Whether a climber jumps from one cell to the other on the grid as it is now
// (a kept way's jump looked at again after a rebuild, p_navkept.c).
boolean P_NavClimbs (int nav, int from, int to);
// The cheapest way from (sx, sy) to (tx, ty): its cells into path (at
// most max, from the start), their count, or 0 if there is none within
// limit cells searched.
int P_NavPath (int nav, fixed_t sx, fixed_t sy, fixed_t tx, fixed_t ty,
	       int* path, int max, int limit);
// Every cell within most units' walk of (sx, sy), as P_NavPath would cost the
// way (navclimb and the rest set around the call); false if it could not be
// done. Then P_NavFloodWalk: the walk to (x, y) in units, -1 if it is not
// within most, -2 if the flood is gone (any search since, or another nav).
boolean P_NavFlood (int nav, fixed_t sx, fixed_t sy, int most);
int P_NavFloodWalk (int nav, fixed_t x, fixed_t y);

// Whether P_NavPath plans for a climber (an imp): jumps up to 112 units up
// a ledge or a sill and 192 down, up to 4 cells off (16 over a floor that
// hurts); consecutive cells of its path with no step between are a jump
// (P_NavJump). Set around the call.
extern boolean	navclimb;
// Set around the call: no way there, a route to the nearest it can get.
extern boolean	navpartial;
// A jump P_NavPath leaves out for a climber that failed it (the cells it
// jumps from and to; -1 none). Set around the call.
extern int	navbadfrom, navbadto;
// Whether the last P_NavPath that found nothing ran out of its limit: a
// way may yet exist.
extern boolean	navexhausted;
// Cells every P_NavPath has searched, ever: what routes cost, for tests.
extern unsigned	navsearched;
// How far the way the last P_NavPath found goes, in units: a jump counts
// extra, so do a door, a hurting floor and a cell kept away from. Only after
// one that found a way.
extern int	navwalk;
// A point P_NavPath's way keeps away from, if navavoidr (units) is not 0:
// cells within it cost more, most at the point (an imp going round the
// player, not past him). Set around the call.
extern fixed_t	navavoidx, navavoidy;
extern int	navavoidr;
// And treats it as shut, not only as something to go round if that is cheap.
extern boolean	navavoidhard;

// The room sector is in (0 up), and how many there are.
int P_NavRoom (int sector);
int P_NavRooms (void);
// A link: the line between two rooms, and whether a door shuts it.
typedef struct { int line, rooma, roomb; boolean door; } navlink_t;
int P_NavLinks (const navlink_t** links);
// Whether sector s is a door a monster or a secret face opens.
boolean P_NavSectorDoor (int s);
// Whether it is a secret nobody has found yet.
boolean P_NavSectorSecret (int s);

// A digest of everything built (the rooms and every layer): the same map
// builds the same digest, run after run.
unsigned P_NavDigest (void);
// One part of it: -1 the grid, the rooms and the flags; 0 up a layer.
unsigned P_NavDigestOf (int part);
// Rebuild everything, as at level load (the tests).
void P_NavRebuild (void);
// Time the last full build took, in milliseconds (the tests' report).
extern int	navbuildms;

// The automap's overlay (-derpnav): links between rooms (kind 0, blue) and each
// awake monster's way to the player (kind 1, green), as map lines.
void P_NavDraw (void (*line)(fixed_t x1, fixed_t y1, fixed_t x2, fixed_t y2, int kind));

#endif
