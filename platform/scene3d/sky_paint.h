/* What sky_layers.cpp and its helpers (sky_city.cpp, sky_hell.cpp,
   sky_spire.cpp, sky_overlap.cpp, sky_relief.cpp) share: the sky texture as
   read (its palette indices, colours and vertical edges), and the fill that
   grows what the towers or buildings hide from the sky's own columns. */
#ifndef DOOM_SCENE3D_SKY_PAINT_H
#define DOOM_SCENE3D_SKY_PAINT_H
#include "internal.h"

namespace doom3d {
struct SkyRgb { float r,g,b; };
inline float skyDist(const SkyRgb &a,const SkyRgb &b) {return std::abs(a.r-b.r)+std::abs(a.g-b.g)+std::abs(a.b-b.b);}
struct SkyImage {
    int w,h;
    std::vector<byte> index;
    std::vector<SkyRgb> rgb;
    std::vector<float> edge;
    const byte *palette;
    SkyRgb color(int i) const {return {(float)palette[3*i],(float)palette[3*i+1],(float)palette[3*i+2]};}
    int nearest(const SkyRgb &c) const {
        int best=0;float bestDist=1e9f;
        for(int i=0;i<256;++i) {float d=skyDist(color(i),c);if(d<bestDist){bestDist=d;best=i;}}
        return best;
    }
};
uint32_t skyHash(uint32_t seed,int i);
// A layer (-1 empty): column x real from top[x] to have[x], on down to the
// bottom from donor columns (sky_layers.cpp's fill).
std::vector<int> skyFill(const SkyImage &s,const std::vector<int> &top,const std::vector<int> &have,bool crest,int haze,uint32_t seed);
// sky_city.cpp: a painted city (a skyline of dark buildings with lit
// windows), laid out as sky_layers.cpp's texture (width x height+skyLayerRows) with its
// buildings in the near towers' row; false when the painting is no city.
// maxTop, farTop: the tallest near and far building's top over the
// reference eye, each in its ring's radii.
bool bakeCity(const SkyImage &s,uint32_t seed,std::vector<byte> &texels,float &maxTop,float &farTop);
// sky_overlap.cpp: where a deep dip of a crest parts two mountains and one
// stands in front of the other, along an edge traced from the dip down: the
// front and back slices (crestPeaks' order; slice i runs from saddle i-1 to
// saddle i), the side of the dip the front one carries on to (1 right, 0
// left), and per column (-1 none) the row the front one starts at there.
struct SkyOverlap { int front,back,side; std::vector<int> top; };
std::vector<SkyOverlap> skyOverlaps(const SkyImage &s,const std::vector<int> &crest,const std::vector<int> &peaks,const std::vector<int> &saddles,float floorRow);
// sky_relief.cpp: each near-rock texel's shade (how far it lies back) into
// its mask byte's upper six bits (texels: sky_layers.cpp's texture).
void skyShadowRelief(const SkyImage &s,const std::vector<int> &nearLayer,std::vector<byte> &texels);
// sky_hell.cpp: whether the sky above line (the first rock row per column)
// is hell's; if so its flag, dark and bright go into settings (one texel).
// kind: a profile's, taken without asking (0 none).
bool bakeHell(const SkyImage &s,const std::vector<int> &line,byte *settings,int kind=0);
// sky_tendrils.cpp: a hanging hell's tendrils (crest: the first rock row
// per column), each its own, into sky_layers.cpp's texture: how far a texel
// swings with its tendril (G) and which (the mask byte's upper bits).
void bakeTendrils(const SkyImage &s,const std::vector<int> &crest,std::vector<byte> &texels);
// sky_spire.cpp: a spire standing in a sky of clouds alone, its columns per
// row (left > right: none), and it laid into sky_layers.cpp's texture.
bool findSpire(const SkyImage &s,std::vector<int> &left,std::vector<int> &right);
void spireTexels(const SkyImage &s,const std::vector<int> &left,const std::vector<int> &right,std::vector<byte> &texels);
// sky_trees.cpp: dead trees in a sky of fog, from row top down: per texel
// how much tree (0-255), and the painting without them; false when there
// are none to speak of. Then laid into sky_layers.cpp's texture (after the
// cloud sky's bake, which they stand in), with the valley mist over their feet.
bool findTrees(const SkyImage &s,int top,std::vector<byte> &alpha,SkyImage &clean);
void treeTexels(const SkyImage &s,const SkyImage &clean,int top,const std::vector<byte> &alpha,std::vector<byte> &texels);
// sky_clouds_bake.cpp: a sky of clouds alone, laid out as sky_layers.cpp's
// texture; skyRows, how far down the painting is sky.
void bakeCloudSky(const SkyImage &s,std::vector<byte> &texels,int skyRows);
void skyCloudFieldClear();
// sky_cloud_noise.cpp: the noise volume, made when a sky first wants it.
void skyCloudNoiseMake();
void skyCloudNoiseShutdown();
void skyCloudFieldUpdate();
} // namespace doom3d
#endif
