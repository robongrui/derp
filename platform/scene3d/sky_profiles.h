/* What each stock sky asks of the cloud field (sky_cloud_field.cpp). Known
   paintings are matched by their composed texels, never by name or episode:
   SKY4 in a PWAD is somebody else's painting. Anything unknown has no
   profile and keeps the generic clouds of sky/clouds.glsl. */
#ifndef DOOM_SCENE3D_SKY_PROFILES_H
#define DOOM_SCENE3D_SKY_PROFILES_H
#include "sky_paint.h"

namespace doom3d {
struct SkyCloudProfile {
    float clear;        // where clear sky lies in each row's brightness order, 0 darkest to 1 brightest
    float darkCloud;    // how much cloud a texel darker than the clear sky is, per unit of darkness
    float brightCloud;  // and one brighter than it (a lit edge)
    float texel;        // a field texel's width, in near-ring radii
    float stretch;      // how much longer a texel is along the wind than across it
    float thickness;    // the shell's depth, in near-ring radii
    float fadeFrom,fadeTo;   // the rises between which the field gives way to the painting
    float opacity;      // of a full column seen straight up
    bool cirrus;        // the high layer stays
    int skyRows;        // how far down the painting is sky (0 all of it)
    float horizon;      // where the painting's horizon stands (0 where it always has)
    const unsigned char *ridge;   // the ground along its foot, first rock row per column
    float backdrop;     // how much of the painting shows behind it, the rest clear sky (0 all of it)
    const unsigned char *crest;   // where no edge finds its mountains: the first rock row per column
    bool paintedRock;   // its rock keeps its painted shading, hard against the sky: delit, this art lost its dark crevices
    float shrink;       // how much of its painted height its mountains are built at (0 the usual)
    float distance;     // more air in front of them, 0 to 1
    float bank;         // low cloud before and behind its mountains, and how much the rock takes the sky's light, 0 to 1
    float rampTop;      // how far up the painting's colours its clouds go (0 all the way): near white, fire came through as flecks
    float scud;         // low broken cloud under the deck, 0 to 1
    float rampLow;      // how dark its ramp's darkest end goes (0 as painted): a pale sky's clouds need shade its painting hasn't got
    float cover;        // cloud on top of what its painting shows: an even sky gives the noise's peaks alone, as rings
    int hellKind;       // hell's kind where the strokes don't tell it (sky_hell.cpp; 3: tendrils hanging from a black top), 0 by them
    int treeRow;        // dead trees in its fog from this row down (sky_trees.cpp), 0 none
};
const SkyCloudProfile *skyCloudProfile(const SkyImage &s);
// sky_cloud_field.cpp: the field baked (s: the painting without what stands in
// it) and its settings into texels.
void skyCloudFieldBake(const SkyImage &s,const SkyCloudProfile &p,std::vector<byte> &texels);
} // namespace doom3d
#endif
