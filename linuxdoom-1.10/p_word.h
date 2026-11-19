// Word among soldiers. One going off to look tells those in earshot. If gunfire then
// reaches any of them, the word goes round all who were told, a voice at a time: they
// get ready, and go in only if the director says so.
#ifndef __P_WORD__
#define __P_WORD__

#include "p_mobj.h"

// mo is leaving for a noise: those in earshot know it went. For gunfire (fight) at
// (x, y) it has heard the fight itself, and says so.
void P_WordOut (mobj_t* mo, boolean fight, fixed_t x, fixed_t y);
// mo heard gunfire at (x, y): if it was told, the word goes round.
void P_WordFight (mobj_t* mo, fixed_t x, fixed_t y);
// mo died: if it had said it was going, it is missed in a while.
void P_WordGone (mobj_t* mo);
// Once a tic: what was said during noises (a voice is one, and they share a spread).
void P_WordThink (void);
// At its look: the word reaches it. True if it set off.
boolean P_WordReaches (mobj_t* actor);
// Waiting ready: whether the word is why, and still good.
boolean P_WordHolds (mobj_t* actor);
// Waiting ready on that word: whether the director sends it in now.
boolean P_WordCalled (mobj_t* actor);

#endif
