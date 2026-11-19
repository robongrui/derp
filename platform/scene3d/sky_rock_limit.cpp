/* How high the sky's rock can reach, so sky/depth.glsl can tell open sky from
   a ray that might hit something without marching it. Most of the sky is over
   every tower, and a march finds that out the dearest way there is: all its
   steps, for nothing.

   Per column the steepest rise from the reference eye any of its rock reaches,
   read off the very bytes the shader builds its towers from: a tower's top is
   no limit on this (nor the tallest top one: skirts, spikes and rounded depths
   stand past it), and rock leaning toward the eye rises steeper than its top.
   The rise is all in radii, so it holds for any map's rings.

   In skyRockLevels rows under the settings: level k holds, per column, the
   most over the 2^k columns from there, so any stretch is two texels. RG the
   near ring, BA the far, 16 bits each, of 8.0, rounded up. */
#include "sky_towers.h"

namespace doom3d {
namespace {
// A step of the walk out along a column misses a peak by this much at most.
constexpr float rockMargin=0.01f,rockMost=8.0f;

float columnLimit(const Towers &T,int x,int ring) {
    float r=ring?skyFarRing:1,most=0;
    for(int i=0;i<4096;++i) {
        float rho=r*(0.1f+2.9f*i/4095);
        most=std::max(most,T.column(x,rho,ring)/rho);
    }
    return most+rockMargin;
}
// A painted city's boxes rise sheer from where their fronts stand, which is
// where the reference eye sees its skyline (sky/city.glsl's buildingGround).
float buildingLimit(const Towers &T,int x,int ring) {
    const byte *t=T.texel(T.p.h+2+ring,x);
    if(!t[3])return 0;
    return std::max(T.rise(ring?t[2]:T.texel(T.p.h,x)[0]),0.0f)+rockMargin;
}
} // namespace

void skyRockLimits(SkyPaint &paint,bool city,bool far) {
    const int w=paint.w;
    Towers T{paint};
    for(int ring=0;ring<2;++ring) {
        // No towers of its own: a sky of clouds keeps its spire's runs in the
        // near row, and sky/spire.glsl traces that.
        bool stands=ring?far:city||paint.maxTop[0]>0;
        std::vector<float> limit(w,0.0f);
        if(stands)parallelFor(w,[&](int x){limit[x]=city?buildingLimit(T,x,ring):columnLimit(T,x,ring);});
        for(int level=0;level<skyRockLevels;++level) {
            byte *row=&paint.texels[4*(size_t)w*(paint.h+8+level)];
            for(int x=0;x<w;++x) {
                auto bits=(uint32_t)std::ceil(std::min(limit[x],rockMost)/rockMost*65535);
                row[4*x+2*ring]=(byte)(bits>>8);row[4*x+2*ring+1]=(byte)bits;
            }
            // On to twice as many, wrapping round the painting.
            std::vector<float> wider(limit);
            int run=1<<level;
            for(int x=0;x<w;++x)wider[x]=std::max(limit[x],limit[(x+run)%w]);
            limit.swap(wider);
        }
    }
}
} // namespace doom3d
