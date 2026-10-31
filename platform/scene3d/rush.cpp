/* The fall's rush (Game_LibraryFall: the library's chasm): the faster he falls, the more the
   picture smears along lines from the point he falls toward (rush.glsl, in the present pass),
   the wider the view opens and the harder it shakes, so the fall feels as fast as it is. From
   about a third of a falling body's top speed to all of it, diving. And the far fog darkens:
   along the chasm everything past its end is the fog's flat colour, lighter than the dark
   walls streaming by, a still column the streaks run along that gave the trick away. Falling
   fast, the dust goes dark and the column with it. Drawing only. */
#include "internal.h"

namespace doom3d {
static constexpr float rushFrom=12,rushFull=62;   // units a tic: from nothing to all of it
static constexpr float rushWiden=0.3f;             // the view's widening at all of it
static constexpr float rushShake=0.006f;           // and its shake, in radians
static constexpr float rushDarken=0.8f;            // and how much of the far fog's colour goes

// view: the world view's place on the screen (uv: x, y, width, height).
void fallRush(Uniforms &camera,const float view[4]) {
    for(float &r:camera.rush)r=0;
    float v[3];
    if(!Game_LibraryFall(v))return;
    float speed=std::sqrt(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]);
    float amount=std::clamp((speed-rushFrom)/(rushFull-rushFrom),0.0f,1.0f);
    if(amount<=0)return;
    for(int k=0;k<3;++k)camera.farFog[k]*=1-rushDarken*amount;
    // The view opens up.
    camera.projection[0]/=1+rushWiden*amount;camera.projection[1]/=1+rushWiden*amount;
    // It shakes, a new jolt each tic.
    unsigned tic=(unsigned)leveltime*2654435761u;
    float yaw=((tic>>8&255)/127.5f-1)*rushShake*amount,pitch=((tic>>16&255)/127.5f-1)*rushShake*amount;
    for(int k=0;k<3;++k) {
        float f=camera.forward[k],r=camera.right[k],u=camera.up[k];
        camera.forward[k]=f+r*yaw+u*pitch;camera.right[k]=r-f*yaw;camera.up[k]=u-f*pitch;
    }
    // The point he falls toward, as world.vert would put it on the screen, in the view's place.
    float d[3]={v[0]/speed,v[1]/speed,v[2]/speed};
    float x=(d[0]*camera.right[0]+d[1]*camera.right[1]+d[2]*camera.right[2])*camera.projection[0];
    float y=(d[0]*camera.up[0]+d[1]*camera.up[1]+d[2]*camera.up[2])*camera.projection[1];
    float w=d[0]*camera.forward[0]+d[1]*camera.forward[1]+d[2]*camera.forward[2];
    float p[3]={view[0]*w+view[2]*(0.5f*w+0.5f*x),view[1]*w+view[3]*(0.5f*w-0.5f*y),w};
    float length=std::sqrt(p[0]*p[0]+p[1]*p[1]+p[2]*p[2]);
    for(int k=0;k<3;++k)camera.rush[k]=p[k]/length;
    camera.rush[3]=amount;
}
} // namespace doom3d
