// DERP's openings (PLAN_DERP_AI.md's C1): the doorways and narrow gaps between
// rooms, each one thing however many lines make it, and who goes through.
//
// Through a narrow one traffic runs one way at a time: while someone is on the
// move in the frame, those coming the other way wait at the mouth, off to a
// side so the way out stays clear. After a few seconds of one way with others
// waiting it changes. Going the same way is left to Walk's own care. A death
// there puts soldiers off for a few seconds. All of it goes by who was seen
// in the frame lately, so nobody holds anything: the dead and the stuck stop
// counting on their own.
//
// Not saved: a load starts with every opening free.
#ifndef __P_PORTALS__
#define __P_PORTALS__

#include "p_mobj.h"

// A level starts (after the navigation): the openings, from its links.
void P_PortalsLevel (void);
int P_Portals (void);

// actor is walking to (wx, wy). True: it waits, at (x, y), for an opening it
// is about to go through.
boolean P_PortalWait (mobj_t* actor, fixed_t wx, fixed_t wy, fixed_t* x, fixed_t* y);

// Whether mo stands in the frame of an opening too narrow to pass it in.
boolean P_PortalInFrame (mobj_t* mo);

// mo stands in such a frame: true, and where to go to be out of it and out of
// the way, on the side of (hx, hy).
boolean P_PortalClear (mobj_t* mo, fixed_t hx, fixed_t hy, fixed_t* x, fixed_t* y);

// Whether walking on would take actor through a choke: a narrow opening with
// him, by what it believes (hx, hy), just the other side, and someone fallen
// there or him right at it. A place out on his side, and on its own side
// one beside the mouth, off the line through.
boolean P_PortalChoke (mobj_t* actor, fixed_t hx, fixed_t hy, fixed_t* far_x, fixed_t* far_y, fixed_t* near_x, fixed_t* near_y);

// mo has died: at an opening, others take note.
void P_PortalDeath (mobj_t* mo);

// For the tests: how wide the opening nearest (x, y) is, in units.
int P_PortalWidth (fixed_t x, fixed_t y);

#endif
