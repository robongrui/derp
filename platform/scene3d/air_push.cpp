/* The air's push on what flies: one displacement every weather particle
   builder adds to its own path (snow, rain, sand, ash, what the drafts and
   the doorways carry in). Particles are stateless, worked out from the
   clock each frame, so the push is a displacement, not a force.
   - Out in the open the wind's gusts (wind.cpp's windGust: waves rolling
     downwind at its speed), as much as the sector is open to them: all in
     a yard, less in a light well or under a porch, none indoors. Taken as
     the difference from where they leave it when it lands, so it still
     lands in its cell and where things lie never jitters.
   - Indoors the stir of a draft's jet on what rides it: tight at the
     opening, wandering further in, swirling where the jet turns, at the
     opening's edges and round its end, where what it carries comes down.
   - Anywhere the shoves of what happens (disturbances.cpp), dying away so
     what was shoved goes back to its path; what blows in at a broken
     window too (paneShove).
   A push that would carry it into a wall is cut back (airKeep: all, half,
   none), so it stays in its room instead of blinking out. Only what flies
   sees any of it: the drafts' solve and the ground's bake keep the steady
   wind. Render-only. */
#include "internal.h"
#include "../rooms.h"

namespace doom3d {
float gustShare(int sector) {return sector>=0&&sector<numsectors&&Rooms_Outside(sector)?1-Rooms_Enclosure(sector):0.0f;}

bool fliesOutside(const sector_t &home,float x,float y,float z) {
    constexpr float margin=6;
    auto clear=[&](float px,float py) {
        const sector_t *at=weatherSectorAt(px,py);
        return at&&(at==&home||underSky(int(at-sectors))||z>=ceilZ(at)+roofClear);
    };
    return clear(x,y)&&clear(x+margin,y)&&clear(x-margin,y)&&clear(x,y+margin)&&clear(x,y-margin);
}
void airShift(int sector,float x,float y,float z,float t,float until,float out[3]) {
    out[0]=out[1]=out[2]=0;
    float share=gustShare(sector),speed=std::hypot(windNow[0],windNow[1]);
    if(share>0&&speed>1) {
        float g=share*(windGust(x,y,t)-windGust(x,y,until));
        out[0]=windNow[0]/speed*g;out[1]=windNow[1]/speed*g;
    }
    disturbanceShove(x,y,z,t,out);
}

void paneShove(const PaneView &p,float &x,float &y,float &z,float t) {
    float push[3]={0,0,0};
    disturbanceShove(x,y,z,t,push);
    if(push[0]==0&&push[1]==0&&push[2]==0)return;
    airKeep(x,y,push,[&](float px,float py) {
        const sector_t *at=weatherSectorAt(px,py);
        return at&&(at==&sectors[p.frame]||(p.inner>=0&&at==&sectors[p.inner])||at==&sectors[p.outer]);
    });
    x+=push[0];y+=push[1];z+=push[2];
}
void airStir(float along,float length,float width,float dx,float dy,float turn,float side,float t,uint32_t seed,float out[3]) {
    float f=std::clamp(along/std::max(length,1.0f),0.0f,1.0f);
    float phase=hashUnit((int)(seed&0xffff),13,seed)*6.2832f,rate=0.7f+0.6f*hashUnit((int)(seed>>16),14,seed);
    // Wandering across the jet, tight at the opening, ragged further in.
    float wander=width*0.35f*f*(0.6f*std::sin(t*1.7f*rate+phase)+0.4f*std::sin(t*3.1f*rate+phase*2.3f));
    // Swirling: where it turns, at the opening's edges, and round its end.
    float edge=std::max(0.0f,std::fabs(side)-0.6f)/0.4f*(1-std::min(1.0f,along/48));
    float end=std::clamp((f-0.8f)/0.2f,0.0f,1.0f);
    float r=std::min(24.0f,18*std::fabs(turn)+10*edge+12*end),spin=t*(2.5f+2*rate)+phase;
    out[0]=-dy*wander+r*std::cos(spin);
    out[1]=dx*wander+r*std::sin(spin);
    out[2]=0.4f*r*std::sin(spin*1.3f);
}
} // namespace doom3d
