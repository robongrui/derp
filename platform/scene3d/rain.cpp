/* Rain (a WeatherKind; weather.cpp says when it rains and how hard):
   streaks of whole Doom pixels stepping at the tic rate, like the debris,
   falling from the sky ceiling and splashing where they land; on liquids
   they ring. Through a broken window facing the wind it blows in and
   splashes on the sill and floor (wear.cpp soaks them), and while it rains
   the cracked ceilings leak (wear.cpp). On the ground it lies wet in steps
   (rainGround), the glass runs with drops (glass.frag). Render-only, with
   its own random numbers. */
#include "internal.h"
extern "C" {
#include "../audio/sound_options.h"
}

namespace doom3d {
namespace {
// Rain is pale grey-blue catching the sky's tint, unlit; the flashlight
// beam lights it from this too.
std::array<float,3> rainAlbedo() {return {0.5f+0.12f*skyTint[0],0.5f+0.12f*skyTint[1],0.53f+0.12f*skyTint[2]};}
}
namespace {
// The drops being drawn: their colour, and how bright they stay in the dark
// (buildRainDrops).
std::array<float,3> dropAlbedo;
float dropGlow=0;
std::array<float,3> rainColor(const sector_t &sector,float x,float y,float z) {
    auto color=weatherColor(sector,x,y,z,dropAlbedo);
    for(int c=0;c<3;++c)color[c]=std::max(color[c],dropAlbedo[c]*dropGlow);
    return color;
}
const sector_t *sectorAt(float x,float y) {return weatherSectorAt(x,y);}
// A streak of rain whose head is at (x, y, z), moving along (vx, vy, vz):
// length units of it trail behind the head, drawn in pieces of whole Doom
// pixels, so a slant shows as a stair the way an old sprite would draw it.
void streak(const Uniforms &camera,float x,float y,float z,float vx,float vy,float vz,float length,int pieces,
            const std::array<float,3> &color,float alpha) {
    float speed=std::sqrt(vx*vx+vy*vy+vz*vz);
    if(speed<1) return;
    float step=length/pieces,dx=vx/speed,dy=vy/speed,dz=vz/speed;
    for(int k=0;k<pieces;++k) {
        float back=step*(k+0.5f);
        pixelBlock(particleVertices,camera,x-dx*back,y-dy*back,z-dz*back,0.55f,step/0.55f,color,false,&dropAlbedo,true,nullptr,alpha);
    }
}
// A drop landing at (x, y, z): on a floor two specks jump aside over three
// tics; on a liquid (or a puddle) a ring of pixels grows round it.
void splash(const Uniforms &camera,float x,float y,float z,float tics,const sector_t &sector,uint32_t seed,bool puddle=false) {
    size_t index=&sector-sectors;
    bool liquid=puddle||(index<sectorMist.size()&&sectorMist[index].liquid);
    auto color=rainColor(sector,x,y,z);
    int tic=(int)tics;
    if(liquid) {
        if(tic>3) return;
        float radius=1.5f+1.5f*tic;
        int count=4+2*tic;
        for(int n=0;n<count;++n) {
            float a=(n+hashUnit(n,tic,seed)*0.5f)*2*(float)doomPi/count;
            pixelBlock(particleVertices,camera,x+std::cos(a)*radius,y+std::sin(a)*radius,z+0.3f,0.5f,1,color,true,nullptr,true,nullptr,0.55f-0.1f*tic);
        }
        return;
    }
    if(tic>2) return;
    float angle=hashUnit(5,6,seed)*2*(float)doomPi,out=1.0f+1.2f*tic,up=tic==1?1.5f:0.5f;
    for(int side=-1;side<=1;side+=2)
        pixelBlock(particleVertices,camera,x+std::cos(angle)*out*side,y+std::sin(angle)*out*side,z+up,0.5f,1,color,true,nullptr,true,nullptr,0.6f);
}
}

// The rain around the eye: one drop per 16-unit cell in a share of the
// cells as heavy as the rain (the same cells first as it thickens, so it
// grows rather than shuffles), out to 640 units, in sky sectors only, each
// falling from the sky down its sector's height and splashing. A streak is
// as long as the drop falls in a tic, so each tic's takes up where the last
// left off and the fall reads unbroken rather than jumping, and it fades
// with distance into the far sheet sky.frag draws past the map's sky, which
// takes on the colour of the farthest drops. Then the broken windows the
// wind blows it in by.
void buildRainDrops(const Uniforms &camera,const std::array<float,3> &albedo,float glow) {
    dropAlbedo=albedo;dropGlow=glow;
    const uint32_t levelSeed=weatherSeed();
    constexpr float cell=16,reach=640;
    float now=std::floor(gameSeconds()*TICRATE)/TICRATE;
    float density=0.7f*fallNow;
    int gx0=(int)std::floor((camera.eye[0]-reach)/cell),gx1=(int)std::floor((camera.eye[0]+reach)/cell);
    int gy0=(int)std::floor((camera.eye[1]-reach)/cell),gy1=(int)std::floor((camera.eye[1]+reach)/cell);
    float flat=std::hypot(camera.forward[0],camera.forward[1]);
    float fx=flat>1e-3f?camera.forward[0]/flat:0,fy=flat>1e-3f?camera.forward[1]/flat:0;
    for(int gy=gy0;gy<=gy1;++gy)for(int gx=gx0;gx<=gx1;++gx) {
        if(hashUnit(gx,gy,levelSeed)>=density) continue;
        float x0=(gx+hashUnit(gx,gy,levelSeed^1u))*cell,y0=(gy+hashUnit(gx,gy,levelSeed^2u))*cell;
        float dx=x0-camera.eye[0],dy=y0-camera.eye[1],distance=std::hypot(dx,dy);
        // Behind the eye only near it, where a glance down still shows them.
        if(distance>reach||(distance>96&&(dx*fx+dy*fy)<-0.3f*distance&&flat>0.5f)) continue;
        const sector_t *sector=sectorAt(x0,y0);
        if(!sector||!underSky(int(sector-sectors))) continue;
        float floor=floorZ(sector),top=skyFallTop(*sector,208),height=top-floor;
        if(height<8) continue;
        uint32_t seed=hash32((uint32_t)gx*73856093u^(uint32_t)gy*19349663u^levelSeed);
        float speed=520*(0.85f+0.3f*hashUnit(gx,gy,levelSeed^3u)),fall=height/speed,period=fall+0.15f;
        float clock=now+hashUnit(gx,gy,levelSeed^4u)*period*7;
        int cycle=(int)std::floor(clock/period);
        float age=clock-cycle*period;
        uint32_t own=seed^(uint32_t)cycle*2654435761u;
        // It lands in its cell, coming in on the wind from upwind of it; on
        // the way it may cross other open sky, or pass over a roof, never
        // through a room.
        float lx=x0+(hashUnit(cycle,1,own)-0.5f)*cell*0.5f,ly=y0+(hashUnit(cycle,2,own)-0.5f)*cell*0.5f;
        float early=std::min(age,fall)-fall;
        float x=lx+windNow[0]*early,y=ly+windNow[1]*early;
        const sector_t *landing=sectorAt(lx,ly);
        if(landing!=sector) continue;
        float zHere=top-speed*std::min(age,fall);
        if(!fliesOutside(*sector,x,y,zHere)) continue;
        // The air's push (air_push.cpp): the gusts, landing still in its cell, and any blast's shove.
        if(age<fall) {
            float push[3];
            airShift(int(sector-sectors),x,y,zHere,now,now-early,push);
            airKeep(x,y,push,[&](float px,float py){return fliesOutside(*sector,px,py,zHere+push[2]);});
            x+=push[0];y+=push[1];zHere+=push[2];
        }
        if(age<fall) {
            float z=zHere;
            if(z<floor+1) continue;
            float alpha=(0.32f+0.25f*hashUnit(cycle,3,own))*(1-0.45f*distance/reach);
            auto color=rainColor(*sector,x,y,z);
            if(distance>reach*0.5f) farFall.see(color);
            streak(camera,x,y,z,windNow[0],windNow[1],-speed,speed/TICRATE,distance<320?3:distance<520?2:1,color,alpha*std::min(1.0f,fallNow*2));
        }
        else if(distance<384) splash(camera,lx,ly,floor,(age-fall)*TICRATE,*sector,own);
    }
    // Until any show on a level, the far sheet takes rain at the outdoors'
    // usual light.
    float light=std::clamp(0.6f*levelMood().outdoor,0.12f,1.0f);
    std::array<float,3> usual;
    for(int c=0;c<3;++c)usual[c]=std::max(dropAlbedo[c]*light*1.15f+0.04f,dropAlbedo[c]*dropGlow);
    farFall.settle(usual);
    // Through broken windows: drops blown in across the opening, slow at
    // first and falling ever faster, to splash on the sill or the floor
    // inside. As many as the window is wide, the rain heavy and the wind
    // blowing in; none through a window the rain slants away from.
    for(int i=0;i<windowPaneCount();++i) {
        PaneView p;
        if(!windowPane(i,p)||!letsWeatherIn(p)||p.top-p.bottom<4) continue;
        float ex=p.bx-p.ax,ey=p.by-p.ay,len=std::hypot(ex,ey);
        float mx=(p.ax+p.bx)/2,my=(p.ay+p.by)/2;
        if(len<4||std::hypot(mx-camera.eye[0],my-camera.eye[1])>reach) continue;
        float in=paneInward(p);
        // A draft through the room carries more in; with nowhere to go only a few.
        int count=std::min(32,(int)std::lround(len/5*fallNow*in*(0.3f+0.7f*paneDraft(p))));
        // Through as the drops outside fall: on the whole wind its way (at
        // least a little inward) and as fast, so none changes pace at the glass.
        float vx=windNow[0],vy=windNow[1],into=vx*p.inX+vy*p.inY;
        if(into<30) {vx+=p.inX*(30-into);vy+=p.inY*(30-into);}
        for(int n=0;n<count;++n) {
            uint32_t seed=p.seed^(uint32_t)n*0x9e3779b9u;
            float period=0.55f+0.3f*hashUnit(n,0,seed),clock=now+hashUnit(n,1,seed)*period*5;
            int cycle=(int)std::floor(clock/period);
            float age=clock-cycle*period;
            uint32_t own=seed^(uint32_t)cycle*2654435761u;
            float u=hashUnit(cycle,1,own)*len,z0=p.bottom+(p.top-p.bottom)*(0.2f+0.8f*hashUnit(cycle,2,own));
            float fall=520*(0.85f+0.3f*hashUnit(cycle,3,own));
            float x=p.ax+ex/len*u+vx*age,y=p.ay+ey/len*u+vy*age,z=z0-fall*age;
            paneShove(p,x,y,z,now);   // a blast's shove (air_push.cpp)
            const sector_t *at=sectorAt(x,y);
            if(!at||(at!=&sectors[p.frame]&&at!=&sectors[p.inner>=0?p.inner:p.frame]&&at!=&sectors[p.outer])) continue;
            float floor=floorZ(at);
            if(z>floor+1) {
                streak(camera,x,y,z,vx,vy,-fall,fall/TICRATE,2,rainColor(*at,x,y,z),0.45f);
                continue;
            }
            // Landed: when it reached the floor, then the splash from there.
            float landed=std::max(0.0f,z0-floor)/fall;
            float lx=p.ax+ex/len*u+vx*landed,ly=p.ay+ey/len*u+vy*landed;
            const sector_t *under=sectorAt(lx,ly);
            if(under)splash(camera,lx,ly,floorZ(under),(age-landed)*TICRATE,*under,own);
        }
    }
    // Leaks (wear.cpp): a drop swells at the crack, lets go, falls and
    // rings the puddle below, one about every second.
    for(const CeilingLeak &l:ceilingLeaks()) {
        if(std::hypot(l.x-camera.eye[0],l.y-camera.eye[1])>reach)continue;
        const sector_t &sector=sectors[l.sector];
        float top=ceilZ(&sector),floor=floorZ(&sector),fall=std::sqrt(2*std::max(0.0f,top-floor)/800);
        float period=0.8f+0.5f*hashUnit(3,0,l.seed)+fall,clock=now+hashUnit(4,0,l.seed)*period,age=clock-std::floor(clock/period)*period;
        float hang=period-fall-0.15f;
        auto color=rainColor(sector,l.x,l.y,top);
        if(age<hang) {
            if(age>hang*0.4f)pixelBlock(particleVertices,camera,l.x,l.y,top-0.6f,0.5f,1,color,false,nullptr,true,nullptr,0.6f);
        }
        else if(age<hang+fall) {
            float t=age-hang,z=top-400*t*t;
            streak(camera,l.x,l.y,z,0,0,-800*t-1,3,1,rainColor(sector,l.x,l.y,z),0.6f);
        }
        else splash(camera,l.x,l.y,floor,(age-hang-fall)*TICRATE,sector,l.seed,true);
    }
}

// How wet the ground lies under the rain: the foot of a wall facing it runs
// soaked, the lee of a wall, roof or ledge stays nearly dry; elsewhere in
// patches damp, wet and soaked with puddles between.
float rainGround(const WeatherGround &cell) {
    float n=valueNoise(cell.x,cell.y,72,cell.seed),wet=n<0.4f?0.35f:n<0.68f?0.6f:0.85f;
    if(valueNoise(cell.x,cell.y,40,cell.seed^0x165667b1u)>0.7f)wet=1;
    if(cell.windward)wet=std::max(wet,0.9f);
    if(cell.lee)wet=0.12f;
    return wet;
}
static void buildRain(const Uniforms &camera) {buildRainDrops(camera,rainAlbedo(),0);}
// What rain blows in by a door opened during play runs out over the floor
// as a wet film: it spreads, runs down steps, drains past two units and
// dries in four minutes; a unit and a half deep is soaked.
const FilmRules rainFilm={1.0f,4,0.2f,0,2,240,1,1.5f,true};
// Rain falls about four and a half units for one across, wets the glass in
// about 6 seconds and soaks the floor under a broken window in about 8; by
// a door it soaks in fast and close to it.
const WeatherKind rainWeather={"rain",4.5f,6,8,false,rainGround,buildRain,I_AmbienceRain,nullptr,6,false,nullptr,nullptr,true,true,
                               &rainFilm,false,2.5f,0,INFINITY,0.2f,32};
} // namespace doom3d
