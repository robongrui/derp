/* stump_rim.cpp: what a cut leaves of the card (ragdoll_card.cpp), per texel of the drawing: which
   texels went, and what the torn end shows (PLAN-tear-fibres.md 2 and 2b). */
#pragma once
#include "ragdoll_doll.h"

namespace doom3d {
// keep: the face's own colour (and its wounds); gone: not drawn; flap: the colour of the stump's texel
// from[] (a lip or a flap hanging past the cut, on texels that were the lost part's); colour: colour[].
enum { STUMP_KEEP,STUMP_GONE,STUMP_FLAP,STUMP_COLOUR };
struct StumpRim { bool on=false;std::vector<byte> paint,colour;std::vector<int32_t> from; };
// rims: the card's blood colours (dark, bright, brightest, deep), before the tint. Kept per doll until its
// cuts change; stumpRimDrop forgets a doll's.
const StumpRim &stumpRim(const Ragdoll &doll,int tint,const byte rims[4]);
void stumpRimDrop(const Ragdoll &doll);
// For a part flying off (flying_piece.cpp): what bone the drawing has at its texel, for its torn end. 0 none,
// 1 bone, 2 marrow (a hole in the bone, or the end broken off short where the stump kept the splinter).
byte stumpPieceBone(const Ragdoll &doll,size_t texel);
}
