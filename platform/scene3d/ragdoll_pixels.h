/* A ragdoll's picture from its faces (ragdoll_pixels.cpp), as F4's Ragdoll pixels says. */
#pragma once
#include <vector>

namespace doom3d {
// One relief face in the picture: its corners in pixels, their depth, its palette colour.
struct CardQuad { float x[4],y[4],d[4];unsigned char colour; };
// F4 Ragdoll pixels (Settings::ragdollPixels).
enum CardPixels { CARD_NEAREST,CARD_ANCHORED,CARD_COVERAGE,CARD_COVERAGE_STEADY,CARD_BLENDED,CARD_BLENDED_STEADY };
inline bool cardAnchored(int mode) {return mode!=CARD_NEAREST;}
inline bool cardSteady(int mode) {return mode==CARD_COVERAGE_STEADY||mode==CARD_BLENDED_STEADY;}
// The quads into pixels (2 bytes a pixel: colour, solid), size x size, nearest face winning. prev: the
// last picture of the same camera step, for the steady modes, its content (dx,dy) pixels from this one's.
void cardPixels(const std::vector<CardQuad> &quads,int size,int mode,const std::vector<unsigned char> *prev,int dx,int dy,
                std::vector<unsigned char> &pixels);
}
