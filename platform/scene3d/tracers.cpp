/* Tracers: travelling bullets (p_bullets.c) drawn as they fly. A bullet crosses 120 units
   and more a tic, so a mark at its head alone is gone before the eye has it: each tic's
   stretch is kept as a dashed line of Doom pixels and fades over a few tics, a streak
   hanging where the shot went. The player's own start at the gun, not the eye (from there
   they'd run straight into the crosshair and show nothing). Drawing only. */
#include "internal.h"

extern "C" double SlowMotion_Share(void);

namespace doom3d {
constexpr int tracerTics=3;                // how long a stretch hangs
constexpr float tracerStep=9;              // units between its pixels
constexpr float tracerSize=1.1f,tracerLeast=0.3f,tracerMost=0.55f;   // its pixels; their strength, far off and at most
constexpr int tracerVolley=2;              // of a tic's many bullets (a shotgun's pellets), only so many show
constexpr float tracerNearest=20;          // none this close to the eye
constexpr size_t mostStreaks=96;
struct Streak { float from[3],to[3];int tic; };
static std::vector<Streak> streaks;
static int seenTic=-1;
static unsigned seenLevel=0;

void buildTracers(const Uniforms &camera) {
    if(seenLevel!=r_levelserial) {streaks.clear();seenLevel=r_levelserial;seenTic=-1;}
    if(seenTic!=leveltime) {
        seenTic=leveltime;
        float from[3],to[3];
        int fresh=0;
        for(int i=0,count=Game_BulletCount();i<count;++i) {
            if(!Game_BulletStretch(i,from,to))continue;
            if(++fresh>tracerVolley)continue;
            // The first stretch of the player's own: from the gun's muzzle, straight below the eye.
            if(std::hypot(from[0]-camera.eye[0],from[1]-camera.eye[1])<24) {
                for(int c=0;c<3;++c)from[c]+=camera.forward[c]*24-camera.up[c]*5;
            }
            if(streaks.size()>=mostStreaks)streaks.erase(streaks.begin());
            Streak s;std::copy(from,from+3,s.from);std::copy(to,to+3,s.to);s.tic=leveltime;
            streaks.push_back(s);
        }
        streaks.erase(std::remove_if(streaks.begin(),streaks.end(),[](const Streak &s){return leveltime-s.tic>=tracerTics||leveltime<s.tic;}),streaks.end());
    }
    // Slow motion: the newest stretch grows as its bullet flies, a hot head on it, instead of
    // the whole tic's 343 units popping in at once seven frames apart (slow_motion.c).
    bool flying=SlowMotion_Share()>0;
    for(const Streak &s:streaks) {
        float dx=s.to[0]-s.from[0],dy=s.to[1]-s.from[1],dz=s.to[2]-s.from[2],l=std::sqrt(dx*dx+dy*dy+dz*dz);
        if(l<1)continue;
        float left=1-(leveltime-s.tic+renderFraction)/tracerTics;
        if(left<=0)continue;
        std::array<float,3> colour{1.0f,0.72f+0.2f*left,0.3f+0.4f*left};
        float reach=flying&&s.tic==leveltime?renderFraction:1;
        if(reach<1) {
            float x=s.from[0]+dx*reach,y=s.from[1]+dy*reach,z=s.from[2]+dz*reach;
            float depth=(x-camera.eye[0])*camera.forward[0]+(y-camera.eye[1])*camera.forward[1]+(z-camera.eye[2])*camera.forward[2];
            float unit=std::max(depth,4.0f)/(100*camera.projection[1]);
            if(std::hypot(x-camera.eye[0],y-camera.eye[1])>=tracerNearest)
                pixelBlock(glowVertices,camera,x,y,z,std::min(2*tracerSize,2*unit),1,{1.0f,0.95f,0.8f},false,nullptr,false,nullptr,1.0f);
        }
        // A pixel wide at any distance and a pixel or two apart: near the eye a set size and step came apart into dots.
        for(float d=0,step=1;d<l*reach;d+=step) {
            float t=d/l,x=s.from[0]+dx*t,y=s.from[1]+dy*t,z=s.from[2]+dz*t;
            float depth=(x-camera.eye[0])*camera.forward[0]+(y-camera.eye[1])*camera.forward[1]+(z-camera.eye[2])*camera.forward[2];
            float unit=std::max(depth,4.0f)/(100*camera.projection[1]);
            step=std::clamp(2*unit,0.5f,tracerStep);
            if(std::hypot(x-camera.eye[0],y-camera.eye[1])<tracerNearest)continue;
            float size=std::min(tracerSize,unit),strength=std::max(tracerLeast,tracerMost*distantShare(camera,x,y,z,tracerSize))*left;
            pixelBlock(glowVertices,camera,x,y,z,size,1,colour,false,nullptr,false,nullptr,strength);
        }
    }
}
}
