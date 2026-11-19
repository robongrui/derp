// What DERP's soldiers say when nothing is happening: idle chatter, and two of them
// talking. It carries no plan. It says someone is in there, and when it stops mid-word
// they have heard something: that is the point of it.
#ifndef __P_CHATTER__
#define __P_CHATTER__

#include "doomtype.h"

// At level start.
void P_ChatterLevel (void);
// Every tic.
void P_ChatterThink (void);
// The room has word: whoever is talking stops there.
void P_ChatterStop (void);
// Lines said this level: for the tests.
int P_ChatterSaid (void);

#endif
