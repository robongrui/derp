/* The sky's towers on the CPU, read back from the bytes sky/depth.glsl reads:
   what lights them (sky_light.cpp), how high they can reach (sky_rock_limit.cpp)
   and whether the sun is behind them (sky_sun_block.cpp). */
#ifndef DOOM_SCENE3D_SKY_TOWERS_H
#define DOOM_SCENE3D_SKY_TOWERS_H
#include "internal.h"

namespace doom3d {
// The towers as sky/depth.glsl's towerGround builds them, in near-ring radii
// round the reference eye, read back from the layers texture's bytes.
struct Towers {
    const SkyPaint &p;
    float rise(int row) const {return skyShrink*(skyRowHorizon-row/(float)p.h)/skyRowScale;}
    const byte *texel(int row,int x) const {return &p.texels[4*(row*p.w+((x%p.w)+p.w)%p.w)];}
    float around(float x,float y) const {return -std::atan2(y,x)/(2*doomPi)*4*p.w;}
    static float profile(float s) {s=std::clamp(s,0.0f,1.0f);return (1-s)*(1-s)*(1+2*s);}
    static float fall(float s) {s=std::clamp(s,0.0f,1.0f);return 6*s*(1-s);}
    // The ground in column x, rho from the reference eye.
    float column(int x,float rho,int ring) const {
        float r=ring?skyFarRing:1,floor=-skyFloorDrop,best=floor;
        float painted=rise(texel(p.h,x)[ring]),shaped=rise(texel(p.h,x)[ring+2]);
        const byte *t=texel(p.h+2+ring,x);
        if(t[3]) {
            float depth=(0.5f+t[3]/255.0f*1.5f)*r,along=(t[0]-128)/127.0f,top=painted*depth,shape=shaped*depth;
            float reach=skyFootReach*(shape-floor)*(0.55f+0.45f*std::sqrt(std::max(1-along*along,0.0f)));
            float u=std::abs(rho-depth)/std::max(reach,1e-5f);
            if(top>floor&&u<1)best=std::min(floor+(shape-floor)*profile(u),top);
        }
        for(int k=0;k<2;++k) {
            const byte *s=texel(p.h+(ring?4:6)+k,x);
            if(!s[3])break;
            float depth=(0.5f+s[3]/255.0f*1.5f)*r,edge=rise(s[2])*depth;
            float beyond=s[1]?std::abs(s[0]-128.0f)/s[1]:0.0f;   // width 0: a mountain in front, no falling away
            float top=std::min(floor+(edge-floor)*profile(beyond),painted*depth);
            if(top<=best)continue;
            float reach=skyFootReach*(top-floor)*0.55f,u=std::abs(rho-depth)/std::max(reach,1e-5f);
            if(u<1)best=std::max(best,floor+(top-floor)*profile(u));
        }
        return best;
    }
    float ground(float qx,float qy,int ring) const {return column((int)std::floor(around(qx,qy)),std::hypot(qx,qy),ring);}
    float groundAll(float x,float y) const {return std::max(ground(x,y,0),p.far?ground(x,y,1):-skyFloorDrop);}
    // Whether the way from o along d is clear of every tower: steps closer
    // together near o, where a tower's own neighbours stand.
    bool clear(const float o[3],const float d[3],float reach,int steps) const {
        float tallest=std::max(p.maxTop[0],p.maxTop[1]*skyFarRing);
        for(int i=1;i<=steps;++i) {
            float t=reach*(float)(i*i)/(float)(steps*steps),q[3]={o[0]+d[0]*t,o[1]+d[1]*t,o[2]+d[2]*t};
            if(q[2]>tallest)return true;
            if(q[2]<groundAll(q[0],q[1]))return false;
        }
        return true;
    }
};
} // namespace doom3d
#endif
