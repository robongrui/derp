/* Whether the sky's mountains stand between the eye and the sun. The sun's
   disc goes behind them by itself, its ray hitting rock in sky.frag; what it
   casts through the air has no ray of its own to hit anything, and went on
   shining from behind a mountain: god rays (sky/rays.glsl), sunbeams and the
   dust in them (atmosphere.cpp). They ask here.

   The disc is where the eye sees the sun, which at a low hour stands well
   under the height the world's light comes from. */
#include "sky_towers.h"

namespace doom3d {
float skySunClear=1;
namespace {
constexpr float discHalf=0.02f;   // sky.frag's disc, across its soft edge

// The steepest rise of the rock from o toward yaw, walking out past both rings.
float rockRiseToward(const Towers &T,const float o[3],float yaw) {
    float dx=std::cos(yaw),dy=std::sin(yaw),most=-1e9f;
    const float out=3.0f*skyFarRing;
    for(int i=1;i<=512;++i) {
        float t=out*i/512,x=o[0]+dx*t,y=o[1]+dy*t,ground=-skyFloorDrop;
        if(T.p.maxTop[0]>0)ground=std::max(ground,T.ground(x,y,0));
        if(T.p.far)ground=std::max(ground,T.ground(x,y,1));
        most=std::max(most,(ground-o[2])/t);
    }
    return most;
}
float smooth(float from,float to,float v) {v=std::clamp((v-from)/(to-from),0.0f,1.0f);return v*v*(3-2*v);}
} // namespace

// Rock stands thousands of units off: it moves against the sun only as the
// eye walks a good way, so the walk out is only taken again then.
void skySunUpdate(const Uniforms &camera,const SkyPaint &paint,bool towers) {
    static int cell[2]={1<<30,1<<30};
    static unsigned serial=~0u;
    static float yaw=1e9f,height=1e9f;
    if(!towers) {skySunClear=1;serial=~0u;return;}
    int at[2]={(int)std::floor(camera.eye[0]/32),(int)std::floor(camera.eye[1]/32)};
    if(at[0]==cell[0]&&at[1]==cell[1]&&serial==paint.serial&&yaw==sunYaw&&height==sunDiscElevation)return;
    cell[0]=at[0];cell[1]=at[1];serial=paint.serial;yaw=sunYaw;height=sunDiscElevation;
    Towers T{paint};
    float r=camera.skyShape[0],o[3];
    for(int k=0;k<3;++k)o[k]=(camera.eye[k]-camera.skyLayers[k])/r;
    float rise=std::tan(sunDiscElevation),clear=0;
    // Across the disc, so a peak's edge passes over it gradually.
    for(int k=-1;k<=1;++k)clear+=smooth(-discHalf,discHalf,rise-rockRiseToward(T,o,sunYaw+k*discHalf*0.75f));
    skySunClear=clear/3;
}
} // namespace doom3d
