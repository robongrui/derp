/* How the library of episode 0.5 is built (scp_7179_b/README.md), in
   map units: one source for the game (linuxdoom-1.10/p_library.c) and the
   map's generator (scp_7179_b/mapgen), which reads these defines.

   A floor is a ring round the chasm, all of it built: a landing at the west
   end (x from 0), LIBRARY_SIDE_GALLERIES galleries along the near side
   (east), a landing at the east end, and as many back along the far side.
   The landings reach across the chasm's ends and close the ring; walking on
   round it comes back where it began. Its galleries are numbered round the
   ring, east along the near side and west along the far one.

   Across (north, y) from the near back wall: the stacks, freestanding
   shelves with books on both sides, four to a gallery with aisles between
   them (LIBRARY_STACKS deep); the walkway along their ends; the railing, the
   chasm, and the far side as its mirror. The landings have no stacks: their
   beds and kiosk stand against the near back wall. Behind each landing, a
   stairwell: one flight along east, a flat platform at each storey, so it
   repeats every storey; the platform at the walkway's own height opens onto
   the landing. Climbing past half a storey steps him a storey down the same
   flight, and the floor's count on. */
#ifndef LIBRARY_LAYOUT_H
#define LIBRARY_LAYOUT_H

#define LIBRARY_GALLERY        512   /* a gallery's length along the walkway */
#define LIBRARY_SIDE_GALLERIES 50    /* galleries along each side: 100 to a floor */
#define LIBRARY_SIDE           (LIBRARY_SIDE_GALLERIES * LIBRARY_GALLERY)
#define LIBRARY_LANDING        512   /* a landing's length, at each end */
#define LIBRARY_LENGTH         (LIBRARY_SIDE + 2 * LIBRARY_LANDING)   /* the ring, end to end */

#define LIBRARY_STACKS         192   /* the aisles' depth, from the back wall to the stacks' ends */
#define LIBRARY_STACK          32    /* a stack's thickness, books on both sides */
#define LIBRARY_STACK_HEIGHT   128   /* its top, under the ceiling: one bookcase texture high, as the back wall's */
#define LIBRARY_GALLERY_STACKS 4     /* stacks to a gallery, an aisle between each two */
#define LIBRARY_CORNICE        8     /* the ledge along the back walls, out from them: the stacks start there */

/* The books, as DOOM II's bookcase (PANBOOK, 64x128) shows them: six rows,
   each 10 texels tall with its top at a row of LIBRARY_BOOK_ROW_TOPS; 14
   spines of 3 texels a row, from texel 11 to 52 of each 64 (the carved frame
   round them, symmetric, so they fall alike whichever way a face runs). A
   stack's face holds three bookcases: 6 rows of 42 books. The back wall
   between two stacks, an aisle wide, shows a bookcase and a half: 6 rows of
   24, from the spine at texel 17 of one case to the one ending at 47 of the
   next (the wall's texture runs with the map, a case every 64 units, and
   each stretch starts 16 into one). The address counts books in shelves six
   across and six down, so both come out whole: 7 to a face, 4 to a stretch
   of wall, 72 to a gallery (its 8 faces and the 4 stretches from its west
   end on; the stretch at the ring's east end belongs to no gallery and is
   plain, the one at its west end reaches onto the landing). */
#define LIBRARY_BOOK_ROWS      6
#define LIBRARY_BOOK_ROW_TOPS  { 33, 46, 59, 72, 85, 97 }
#define LIBRARY_BOOK_HEIGHT    10
#define LIBRARY_BOOK_WIDTH     3
#define LIBRARY_BOOKS_FROM     11    /* the first spine, in each 64 texels */
#define LIBRARY_CASE_BOOKS     14    /* spines to a bookcase's row */
#define LIBRARY_ROW_BOOKS      (3 * LIBRARY_CASE_BOOKS)    /* along a stack's face */
#define LIBRARY_WALL_BOOKS     24    /* along the wall between two stacks */
#define LIBRARY_WALL_FROM      17    /* the wall's first spine, in the case it starts in */
#define LIBRARY_SHELF_ACROSS   6     /* books across a shelf */
#define LIBRARY_SHELF_BOOKS    (LIBRARY_BOOK_ROWS * LIBRARY_SHELF_ACROSS)
#define LIBRARY_FACE_SHELVES   (LIBRARY_ROW_BOOKS / LIBRARY_SHELF_ACROSS)
#define LIBRARY_WALL_SHELVES   (LIBRARY_WALL_BOOKS / LIBRARY_SHELF_ACROSS)
#define LIBRARY_GALLERY_SHELVES (2 * LIBRARY_GALLERY_STACKS * LIBRARY_FACE_SHELVES + LIBRARY_GALLERY_STACKS * LIBRARY_WALL_SHELVES)
#define LIBRARY_WALKWAY        128   /* from the stacks' ends to the railing */
#define LIBRARY_NEAR_RAIL      (LIBRARY_STACKS + LIBRARY_WALKWAY)   /* the railing, from the back wall */
#define LIBRARY_CHASM          384   /* across the chasm, railing to railing */
#define LIBRARY_FAR_RAIL       (LIBRARY_NEAR_RAIL + LIBRARY_CHASM)
#define LIBRARY_FAR_WALL       (LIBRARY_FAR_RAIL + LIBRARY_WALKWAY + LIBRARY_STACKS)   /* the far back wall */
#define LIBRARY_RAIL           8     /* the parapet along the edge: its width, on the walkway's side */
#define LIBRARY_RAIL_HEIGHT    28    /* and height: past a step (24), under the eye (41) */
#define LIBRARY_HEADROOM       160   /* walkway floor to its ceiling */
#define LIBRARY_STOREY         192   /* floor to floor */
#define LIBRARY_DEPTH          2048  /* the chasm as built, below and above the walkway */

#define LIBRARY_STEP           16    /* a stair's rise */
#define LIBRARY_TREAD          32    /* a stair's depth */
#define LIBRARY_STEPS          (LIBRARY_STOREY / LIBRARY_STEP)
#define LIBRARY_PLATFORM       192   /* the flat at each storey */
#define LIBRARY_FLIGHT         (LIBRARY_STEPS * LIBRARY_TREAD + LIBRARY_PLATFORM)  /* a storey, along the flight */
#define LIBRARY_STAIRWELL      96    /* the stairwell's width */
#define LIBRARY_WALL           16    /* the shelves' wall, between landing and stairwell */
#define LIBRARY_FLIGHTS        5     /* storeys of flight built: two below, two above */

/* On each landing, against the shelves' wall: a bed at each end, out from
   the wall, and the kiosk just past the stairwell's door. */
#define LIBRARY_BED_INSET      24    /* from the landing's end */
#define LIBRARY_BED_WIDTH      40
#define LIBRARY_BED_LENGTH     96
#define LIBRARY_DORM_BEDS      5     /* and a row along each landing's far wall, heads to it */
#define LIBRARY_DORM_FROM      48    /* the first's west side, from the landing's west end */
#define LIBRARY_DORM_STEP      88    /* from one to the next */
#define LIBRARY_KIOSK_FROM     16    /* past the door's east side */
#define LIBRARY_KIOSK_WIDTH    48
#define LIBRARY_KIOSK_DEPTH    24

#define LIBRARY_TAG_BED        1     /* sector tags the game finds them by */
#define LIBRARY_TAG_KIOSK      2

#endif
