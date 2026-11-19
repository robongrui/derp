/* Shot wakes, slow motion only (slow_motion.c): every bullet and pellet in flight leaves a
   trail of rippling air along its path, F.E.A.R.-style. It is the lava's raster shimmer
   (present.frag) in a ribbon that turns to face the eye, hottest down its middle: born at
   once behind the bullet, settling over a second and a half of slow time, and gone with
   the slow motion. The tracer (tracers.cpp) is the bullet; this is the air it went
   through. Drawing only. */
#include "internal.h"

extern "C" double SlowMotion_Share(void);

namespace doom3d {
constexpr int wakeTics=14;           // how long a stretch shimmers (1.6 s at a quarter speed)
constexpr float wakePiece=48;        // ribbon pieces along a stretch, units
constexpr float wakeHalf=5;          // half its width in units, and at least
constexpr float wakePixels=4;        // this many Doom pixels: the shimmer only moves pixels inside it
constexpr float wakeNearest=32;      // nothing this close to the eye, full from twice as far
constexpr size_t mostWakes=192;
struct Wake { float from[3],to[3];int tic; };
static std::vector<Wake> wakes;
static int seenTic=-1;
static unsigned seenLevel=0;

static void ribbon(const Uniforms &camera,const float a[3],const float b[3],float heatA,float heatB) {
    float d[3]={b[0]-a[0],b[1]-a[1],b[2]-a[2]};
    auto edge=[&](const float p[3],float heat,float out[2][3]) {
        float e[3]={camera.eye[0]-p[0],camera.eye[1]-p[1],camera.eye[2]-p[2]};
        float s[3]={d[1]*e[2]-d[2]*e[1],d[2]*e[0]-d[0]*e[2],d[0]*e[1]-d[1]*e[0]};
        float sl=std::sqrt(s[0]*s[0]+s[1]*s[1]+s[2]*s[2]);
        float depth=-(e[0]*camera.forward[0]+e[1]*camera.forward[1]+e[2]*camera.forward[2]);
        float unit=std::max(depth,4.0f)/(100*camera.projection[1]);
        float half=std::max(wakeHalf,wakePixels*unit)/std::max(sl,1e-4f);
        for(int c=0;c<3;++c) {out[0][c]=p[c]-s[c]*half;out[1][c]=p[c]+s[c]*half;}
        float near=std::sqrt(e[0]*e[0]+e[1]*e[1]+e[2]*e[2]);
        return heat*std::clamp((near-wakeNearest)/wakeNearest,0.0f,1.0f);
    };
    float ea[2][3],eb[2][3];
    float ha=edge(a,heatA,ea),hb=edge(b,heatB,eb);
    if(ha<=0.01f&&hb<=0.01f)return;
    auto v=[](const float p[3],float heat){return Vertex{p[0],p[1],p[2],0,0,heat,0};};
    // Edge to middle to edge: cool at its sides, hot down the shot's line.
    for(int side=0;side<2;++side) {
        Vertex outA=v(ea[side],0),outB=v(eb[side],0),midA=v(a,ha),midB=v(b,hb);
        heatVertices.insert(heatVertices.end(),{outA,outB,midB,outA,midB,midA});
    }
}

void buildShotWakes(const Uniforms &camera) {
    if(seenLevel!=r_levelserial) {wakes.clear();seenLevel=r_levelserial;seenTic=-1;}
    float share=(float)SlowMotion_Share();
    if(share<=0) {wakes.clear();return;}
    if(seenTic!=leveltime) {
        seenTic=leveltime;
        float from[3],to[3];
        for(int i=0,count=Game_BulletCount();i<count;++i) {
            if(!Game_BulletStretch(i,from,to))continue;
            // The player's own start at the gun, as the tracer does.
            if(std::hypot(from[0]-camera.eye[0],from[1]-camera.eye[1])<24)
                for(int c=0;c<3;++c)from[c]+=camera.forward[c]*24-camera.up[c]*5;
            if(wakes.size()>=mostWakes)wakes.erase(wakes.begin());
            Wake w;std::copy(from,from+3,w.from);std::copy(to,to+3,w.to);w.tic=leveltime;
            wakes.push_back(w);
        }
        wakes.erase(std::remove_if(wakes.begin(),wakes.end(),[](const Wake &w){return leveltime-w.tic>=wakeTics||leveltime<w.tic;}),wakes.end());
    }
    for(const Wake &w:wakes) {
        // Fast hit, slow recovery: full at once, easing out.
        float age=(leveltime-w.tic+renderFraction)/wakeTics;
        if(age>=1)continue;
        float heat=share*(1-age)*(1-age);
        // The newest stretch only as far as its bullet has got.
        float reach=w.tic==leveltime?renderFraction:1;
        float d[3]={w.to[0]-w.from[0],w.to[1]-w.from[1],w.to[2]-w.from[2]};
        float l=std::sqrt(d[0]*d[0]+d[1]*d[1]+d[2]*d[2])*reach;
        if(l<1)continue;
        int pieces=std::max(1,(int)std::ceil(l/wakePiece));
        float a[3],b[3];
        for(int k=0;k<pieces;++k) {
            float t0=reach*k/pieces,t1=reach*(k+1)/pieces;
            for(int c=0;c<3;++c) {a[c]=w.from[c]+d[c]*t0;b[c]=w.from[c]+d[c]*t1;}
            ribbon(camera,a,b,heat,heat);
        }
    }
}
} // namespace doom3d
