/* Ash (a WeatherKind; weather.cpp says when it falls and how thick): the
   fall from an eruption. Flakes of one Doom pixel, a few near the eye of
   two, dark grey, drifting down slower than snow on half the wind, swaying
   wider and tumbling from a lighter face to a darker one as they go,
   stepping at the tic rate. One in twenty is an ember still glowing as it
   leaves the sky: yellow-orange, cooling as it falls through orange and
   deep red to ash, flickering, as bright in the dark as in the light and
   glowing into the bloom as a monster's eyes do (too small to light
   anything). They lie a moment where they land; on liquids they are gone
   at once. Through a broken window the wind presses on (or the eddies
   bring round to) they blow in, the drafts carry them on through the rooms
   (drafts.cpp), and indoors ash lies as the floor film: where they blew in,
   and wherever the outside air gets in (ashIndoors, blown_in.cpp). On the
   ground it lies grey all over (ashGround,
   world/settled.glsl), the glass greys over (glass.frag), and past the
   map's sky it falls on under the eruption's pall (sky/ash.glsl).
   Render-only, with its own random numbers. */
#include "internal.h"

namespace doom3d {
namespace {
constexpr float drift=0.5f;   // of the wind's speed
constexpr float speedDown=30; // units a second (sky/ash.glsl's too)
constexpr float emberShare=0.05f;
// Ash is dark grey with a breath of the sky's tint, unlit; the flashlight
// beam lights it from this too. Lying, a shade darker.
std::array<float,3> albedo={0.26f,0.25f,0.24f};
std::array<float,3> ashAlbedo() {
    for(int c=0;c<3;++c)albedo[c]=0.24f+0.03f*skyTint[c];
    return albedo;
}
std::array<float,3> ashLies() {auto a=ashAlbedo();for(float &v:a)v*=0.85f;return a;}
uint32_t ashSeed() {return hash32((uint32_t)gameepisode*7919u+(uint32_t)gamemap*104729u+0x3e5b0c4du);}
bool liquidSector(const sector_t &sector) {
    size_t index=&sector-sectors;
    return index<sectorMist.size()&&sectorMist[index].liquid;
}
// A flake's sway about its path, units, at age seconds: wider than snow's.
void sway(float age,uint32_t seed,float &x,float &y) {
    float phase=hashUnit(7,8,seed)*2*(float)doomPi;
    x=5*std::sin(age*1.9f+phase);y=5*std::cos(age*1.4f+phase*1.7f);
}
// An ember's glow at heat (1 just out of the fire, 0 gone to ash):
// yellow-orange, orange, deep red, flickering every other tic.
std::array<float,3> emberColor(float heat,uint32_t seed,float now) {
    static const std::array<float,3> ramp[3]={{{0.85f,0.16f,0.04f}},{{1.0f,0.48f,0.1f}},{{1.0f,0.8f,0.38f}}};
    float f=std::clamp(heat,0.0f,1.0f)*2,flicker=0.8f+0.2f*hashUnit((int)(now*TICRATE)/2,7,seed);
    int i=std::min(1,(int)f);
    float t=f-i;
    std::array<float,3> color;
    for(int c=0;c<3;++c)color[c]=std::min(1.0f,(ramp[i][c]+(ramp[i+1][c]-ramp[i][c])*t)*flicker);
    return color;
}
// One flake at age seconds: a Doom pixel (two up close for the bigger
// ones), its lighter face or its darker one as it tumbles; resting, it
// stands on the floor, its darker face up.
void flake(const Uniforms &camera,float x,float y,float z,float distance,const sector_t &sector,uint32_t seed,float age,bool resting,float alpha) {
    float size=distance<96&&hashUnit(9,9,seed)<0.3f?1.1f:0.6f;
    bool light=!resting&&std::fmod(age*(0.8f+0.6f*hashUnit(10,9,seed))+hashUnit(11,9,seed),1.0f)<0.5f;
    auto color=weatherColor(sector,x,y,z,albedo);
    for(float &v:color)v=std::min(1.0f,v*(light?1.2f:0.85f));
    pixelBlock(particleVertices,camera,x,y,z,size,1,color,resting,&albedo,true,nullptr,alpha);
}
// An ember: glowing as a monster's eyes do (the glow list, drawn into the
// bloom too), its own colour rather than the palette's nearest, a size up
// near the eye so the glow has something to grow from. It lights nothing.
void ember(const Uniforms &camera,float x,float y,float z,float heat,uint32_t seed,float now,float alpha) {
    float near=std::hypot(x-camera.eye[0],y-camera.eye[1])<128?1.1f:0.6f;
    pixelBlock(glowVertices,camera,x,y,z,near,1,emberColor(heat,seed,now),false,nullptr,false,nullptr,alpha);
}
}

// The eruption's pall over the open ground too (the outdoor haze, c.haze,
// world.frag): surfaces under the sky fade with distance into the pall and
// its red underglow as the sky does toward the horizon (sky/ash.glsl's
// ashHaze), so a yard's far walls no longer stand crisp against it.
void ashHazeUniforms(Uniforms &camera) {
    float amount=weatherNow(WEATHER_ASH);
    if(amount<=0)return;
    float luma=0.299f*camera.haze[0]+0.587f*camera.haze[1]+0.114f*camera.haze[2];
    std::array<float,3> lies=ashLies();
    static const float glow[3]={0.16f,0.045f,0.012f};
    for(int c=0;c<3;++c)camera.haze[c]+=(lies[c]*(0.35f+0.9f*luma)+glow[c]*amount-camera.haze[c])*std::min(1.0f,amount*1.25f);
    if(settings.fog)camera.haze[3]=std::max(camera.haze[3],1.5f*amount);
}

// The ash round the eye: two flakes per 16-unit cell in a share of the
// cells as thick as it falls (the same cells first as it thickens), out to
// 512 units, then four per 32-unit cell out to 1024, fading out toward
// that edge: the sky's far sheets (sky/ash.glsl) fall right down to the
// skyline, so a yard's far walls past 512 units stood bare under a sky full
// of ash. Placed round the eye and downwind of it, the rings by where they
// land, drawn and faded by where they are now (flyArea). In sky sectors only, each drifting down its sector's height on
// the wind, landing in its cell and lying there a little over a second; an
// ember glows for the first part of its fall. Then the broken windows the
// wind blows it in by.
static void buildAsh(const Uniforms &camera) {
    constexpr float reach=512,far=1024,rest=1.2f,typical=168.0f/speedDown;   // a fall from skyFallTop's height
    float now=std::floor(gameSeconds()*TICRATE)/TICRATE;
    float density=0.8f*fallNow;
    uint32_t base=ashSeed();
    ashAlbedo();
    float windX=windNow[0]*drift,windY=windNow[1]*drift;
    const FlyArea area=flyArea(camera,windX,windY,typical);
    for(int ring=0;ring<2;++ring) {
    const float cell=ring?32.0f:16.0f,inner=ring?reach:0.0f,outer=ring?far+area.grow:reach;
    const int flakes=ring?4:2;
    int gx0=(int)std::floor((area.x-outer)/cell),gx1=(int)std::floor((area.x+outer)/cell);
    int gy0=(int)std::floor((area.y-outer)/cell),gy1=(int)std::floor((area.y+outer)/cell);
    for(int gy=gy0;gy<=gy1;++gy)for(int gx=gx0;gx<=gx1;++gx)for(int k=0;k<flakes;++k) {
        uint32_t seed=base^(uint32_t)k*0x9e3779b9u^(ring?0x3c6ef372u:0u);
        if(hashUnit(gx,gy,seed)>=density) continue;
        float x0=(gx+hashUnit(gx,gy,seed^1u))*cell,y0=(gy+hashUnit(gx,gy,seed^2u))*cell;
        float placed=std::hypot(x0-area.x,y0-area.y);
        // Behind the eye only near it, where a glance down still shows them.
        if(placed<inner||placed>outer||flyOutOfReach(camera,x0,y0,windX,windY,typical,far)||flyBehind(camera,x0,y0,windX,windY,typical)) continue;
        const sector_t *sector=weatherSectorAt(x0,y0);
        if(!sector||!underSky(int(sector-sectors))) continue;
        float floor=floorZ(sector),top=skyFallTop(*sector,168),height=top-floor;
        if(height<8) continue;
        uint32_t own0=hash32((uint32_t)gx*73856093u^(uint32_t)gy*19349663u^seed);
        float speed=speedDown*(0.8f+0.4f*hashUnit(gx,gy,seed^3u)),fall=height/speed,period=fall+rest;
        float clock=now+hashUnit(gx,gy,seed^4u)*period*7;
        int cycle=(int)std::floor(clock/period);
        float age=clock-cycle*period;
        uint32_t own=own0^(uint32_t)cycle*2654435761u;
        // It lands in its cell, coming in on the wind from upwind of it,
        // swaying; on the way it may cross other open sky, or pass over a
        // roof, never through a room.
        float lx=x0+(hashUnit(cycle,1,own)-0.5f)*cell*0.5f,ly=y0+(hashUnit(cycle,2,own)-0.5f)*cell*0.5f;
        if(weatherSectorAt(lx,ly)!=sector) continue;
        float flying=std::min(age,fall),early=flying-fall,sx,sy,ex,ey;
        sway(flying,own,sx,sy);sway(fall,own,ex,ey);
        float x=lx+windX*early+sx-ex,y=ly+windY*early+sy-ey,z=top-speed*flying;
        if(!fliesOutside(*sector,x,y,z)) continue;
        // The air's push (air_push.cpp): the gusts, landing still in its cell, and any blast's shove.
        if(age<fall) {
            float push[3];
            airShift(int(sector-sectors),x,y,z,now,now-early,push);
            airKeep(x,y,push,[&](float px,float py){return fliesOutside(*sector,px,py,z+push[2]);});
            x+=push[0];y+=push[1];z+=push[2];
        }
        float distance=std::hypot(x-camera.eye[0],y-camera.eye[1]);   // where it is now
        if(distance>far) continue;
        float alpha=(0.85f-0.45f*std::min(distance/reach,1.0f))*flyFade(distance,far-192,far)*std::min(1.0f,fallNow*2);
        if(age<fall) {
            if(z<=floor+1) continue;
            // Faded in over its first half second, so where it starts never shows.
            float fadeIn=std::min(1.0f,flying*2);
            float heat=hashUnit(cycle,11,own)<emberShare?1-1.6f*flying/fall:0;
            if(heat>0) {ember(camera,x,y,z,heat,own,now,0.95f*fadeIn);continue;}
            if(distance>reach*0.5f) farFall.see(weatherColor(*sector,x,y,z,albedo));
            flake(camera,x,y,z,distance,*sector,own,flying,false,alpha*fadeIn);
        }
        else if(distance<256&&!liquidSector(*sector)) flake(camera,lx,ly,floor,distance,*sector,own,0,true,alpha*(1-(age-fall)/rest));
    }
    }
    // Until any show on a level, the far sheet takes ash at the outdoors'
    // usual light.
    float light=std::clamp(0.6f*levelMood().outdoor,0.12f,1.0f);
    std::array<float,3> usual;
    for(int c=0;c<3;++c)usual[c]=albedo[c]*light*1.15f+0.04f;
    farFall.settle(usual);
    // Through broken windows: flakes blown in across the opening, coming
    // through as the flakes outside fly (half the wind its way, falling as
    // fast, swaying as wide), then slowing as the wind drops inside and
    // settling on the sill or the floor. As many as the window is wide, the
    // ash thick and the wind blowing in.
    for(int i=0;i<windowPaneCount();++i) {
        PaneView p;
        if(!windowPane(i,p)||!letsWeatherIn(p)||p.top-p.bottom<4) continue;
        float in=paneInward(p);
        float ex=p.bx-p.ax,ey=p.by-p.ay,len=std::hypot(ex,ey);
        float mx=(p.ax+p.bx)/2,my=(p.ay+p.by)/2,distance=std::hypot(mx-camera.eye[0],my-camera.eye[1]);
        if(len<4||distance>reach) continue;
        float draft=paneDraft(p),reachIn=0.5f+0.7f*draft;   // a draft through the room carries it in, else it only puffs in
        int count=std::min(24,(int)std::lround(len/8*fallNow*in*(0.3f+0.7f*draft)));
        // The outside flakes' pace, and at least a little of it inward.
        float vx=windNow[0]*drift,vy=windNow[1]*drift,into=vx*p.inX+vy*p.inY;
        if(into<10) {vx+=p.inX*(10-into);vy+=p.inY*(10-into);}
        for(int n=0;n<count;++n) {
            uint32_t seed=p.seed^(uint32_t)n*0x9e3779b9u^0x6b43a9b5u;
            float period=3.5f+1.5f*hashUnit(n,0,seed),clock=now+hashUnit(n,1,seed)*period*5;
            int cycle=(int)std::floor(clock/period);
            float age=clock-cycle*period;
            uint32_t own=seed^(uint32_t)cycle*2654435761u;
            float u=hashUnit(cycle,1,own)*len,z0=p.bottom+(p.top-p.bottom)*(0.2f+0.8f*hashUnit(cycle,2,own));
            float spread=0.85f+0.3f*hashUnit(cycle,3,own),sink=speedDown*(0.8f+0.4f*hashUnit(cycle,4,own));
            // In on the wind, losing it over about a second, sinking all the while.
            auto inside=[&](float t,float &x,float &y) {
                float carried=0.9f*(1-std::exp(-t/0.9f))*spread*reachIn,sx,sy;
                sway(t,own,sx,sy);
                x=p.ax+ex/len*u+vx*carried+sx;y=p.ay+ey/len*u+vy*carried+sy;
            };
            float x,y,z=z0-sink*age;
            inside(age,x,y);
            paneShove(p,x,y,z,now);   // a blast's shove (air_push.cpp)
            const sector_t *at=weatherSectorAt(x,y);
            if(!at||(at!=&sectors[p.frame]&&at!=&sectors[p.inner>=0?p.inner:p.frame]&&at!=&sectors[p.outer])) continue;
            float floor=floorZ(at);
            if(z>floor+1) {flake(camera,x,y,z,distance,*at,own,age,false,0.75f);continue;}
            // Landed: when it reached the floor, then lying there a moment.
            float landed=std::max(0.0f,z0-floor)/sink,lx,ly;
            inside(landed,lx,ly);
            const sector_t *under=weatherSectorAt(lx,ly);
            if(under&&age-landed<rest&&!liquidSector(*under))
                flake(camera,lx,ly,floorZ(under),distance,*under,own,0,true,0.75f*(1-(age-landed)/rest));
        }
    }
}

namespace {
// A flake riding a draft through the rooms (drafts.cpp); now and then one
// still glowing, only near the opening where the draft still carries most
// of it (alpha): in the room's air it is ash before it gets far.
void ashCarried(const Uniforms &camera,float x,float y,float z,const sector_t &sector,uint32_t seed,float alpha) {
    float now=std::floor(gameSeconds()*TICRATE)/TICRATE;
    ashAlbedo();
    if(alpha>0.6f&&hashUnit(12,13,seed)<emberShare) {ember(camera,x,y,z,0.35f*(alpha-0.6f)/0.15f,seed,now,alpha);return;}
    flake(camera,x,y,z,128,sector,seed,now,false,alpha);
}
// What blows in lies as the floor film and stays: it sticks where it lands
// (no creeping, no pouring), two units deep lying full grey.
const FilmRules ashFilm={0,0,INFINITY,INFINITY,INFINITY,INFINITY,1,2,true,1};
// How deep ash settles indoors from the air itself: by the room's share of
// the outside air (the drafts', the slow exchange through every opening
// too: one broken pane greys its room in a few minutes, a doorless tunnel
// is as grey as outside, the rooms behind it less and less), none under a
// twentieth of it; where the air moves (the indoor wind, by the openings)
// a little less settles, so the still corners grey first. A dusting at
// most, the floor showing through (six tenths of full): only where it
// blows in does it lie solid.
float ashIndoors(int sector,float x,float y) {
    float share=draftOutsideShare(sector);
    if(std::isnan(share))return 0;
    int cx=(int)std::floor((x-mapOrigin[0])/mapCell),cy=(int)std::floor((y-mapOrigin[1])/mapCell);
    float wind=cx>=0&&cy>=0&&cx<mapWidth&&cy<mapHeight?roomWind((size_t)cy*mapWidth+cx):0.0f;
    if(std::isnan(wind))wind=0;
    return 0.6f*ashFilm.full*std::clamp((share-0.05f)/0.6f,0.0f,1.0f)*(1-0.6f*std::clamp(wind,0.0f,1.0f));
}
// How deep ash lies: it comes down heavy and steep, so it blankets all the
// open ground, deepest at the foot of a wall facing the wind, thinner in a
// wall's lee, and barely under a roof's edge or a ledge overhead.
float ashGround(const WeatherGround &cell) {
    float n=valueNoise(cell.x,cell.y,56,cell.seed),deep=n<0.3f?0.75f:n<0.65f?0.88f:1.0f;
    if(cell.windward)deep=1;
    if(cell.lee)deep=cell.roofed?0.15f:0.55f;
    return deep;
}
void quiet(float) {}
}
// Ash falls steeply, two and a half units for one across, greys the glass
// in about 12 seconds and lies under a broken window in about 20. It is
// warm out in it, and it never melts. Light, it rides the drafts far, the
// eddies bring it round to windows out of the wind, and it settles
// wherever the outside air gets in (ashIndoors).
const WeatherKind ashWeather={"ash",2.5f,12,20,true,ashGround,buildAsh,quiet,nullptr,24,false,ashCarried,ashLies,true,true,
                              &ashFilm,true,4.5f,0.35f,INFINITY,0.1f,128,ashIndoors};
} // namespace doom3d
