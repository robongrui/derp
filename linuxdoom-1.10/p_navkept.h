// Kept ways: a walker's long route, searched once and then followed. A route
// whose search needs more than the short look (8,000 cells) costs up to
// 48,000 cells in one tic, about 9 ms on the frame's thread, and the walker
// asked for it again every few steps (it keeps only its next waypoint). Here
// the whole of such a way is kept, by the walker's serial, and its next
// stretch handed out at each waypoint for as long as it holds: the same body
// and the same way of searching, the walker still on it, the steps ahead
// still steps on the grid as it is now, and what it walks to no further than
// a short search from the way's end (a hunter after a moving player: the end
// patched, not the whole searched again). And a goal that took the long look
// once goes straight to it next time, not first through the short one that
// failed. Only the long routes are kept: a short one is searched as it was.
#ifndef __P_NAVKEPT__
#define __P_NAVKEPT__

#include "doomtype.h"
#include "m_fixed.h"
#include "p_mobj.h"

// The most of a way kept (512 cells of 16 units: 8,192 units of walk).
#define NAV_KEPT_CELLS		512
// The short look's cells: a way that needed more is a long one.
#define NAV_KEPT_SHORT		8000

// At level load (the serials start again): nothing kept.
void P_NavKeptClear (void);
// Inside P_DerpRouteBegin/End, with the caller's navavoid set as for its search:
// the next stretch of actor's kept way to (gx, gy) into path (up to max cells,
// path[0] the cell it stands at or by), its count; 0 if there is none that
// holds (then it is dropped, and the caller searches); NAV_DEFERRED if the way's
// end needed patching and the tic's routes are spent (on as it was, then).
int P_NavKeptNext (mobj_t* actor, int nav, fixed_t gx, fixed_t gy, int* path, int max);
// A way to (gx, gy) found by the long look (path[0] where actor stands, n cells;
// n of NAV_KEPT_CELLS: more of it not kept). Same bracket as above.
void P_NavKeep (mobj_t* actor, int nav, fixed_t gx, fixed_t gy, const int* path, int n);
// Whether actor's last search to near (gx, gy) needed the long look.
boolean P_NavKeptLong (mobj_t* actor, int nav, fixed_t gx, fixed_t gy);
// Remember that it did (islong), or forget it.
void P_NavKeptMarkLong (mobj_t* actor, int nav, fixed_t gx, fixed_t gy, boolean islong);

// A long look that found no way at all is not asked again for a while: its
// walker went on with the short "as near as it gets" search, reached that in
// a few steps and asked again, 40,000 cells a time. Whether actor's long look
// to near (gx, gy) failed within the last few seconds, and the note that it did.
boolean P_NavKeptFailed (mobj_t* actor, fixed_t gx, fixed_t gy);
void P_NavKeptMarkFailed (mobj_t* actor, fixed_t gx, fixed_t gy);

#endif
