/* Snow (a WeatherKind; weather.cpp says when it snows and how thick): flakes of one Doom
   pixel, a few near the eye of two, drifting down slowly on the rain's wind
   (a third of its speed), swaying as they go and stepping at the tic rate.
   They fall from the sky ceiling, lie a moment where they land and are gone;
   on liquids they are gone at once. Through a broken window facing the wind
   they blow in, sink and settle on the floor inside (wear.glsl draws it
   whitening there). On the ground it lies in drifts (snowGround), the glass
   frosts (glass.frag). Past the map's sky, sky.frag draws the far flakes.
   Render-only, with its own random numbers. */
#include "internal.h"
#include "../rooms.h"
extern "C" {
#include "../audio/sound_options.h"
}

namespace doom3d {
namespace {
constexpr float drift=0.33f; // of the wind's speed in a breeze, nearly all of it in a gale (carried)
// Snow is white with a breath of blue, unlit; the flashlight beam lights it from this too.
const std::array<float,3> snowAlbedo={0.84f,0.86f,0.9f};
uint32_t snowSeed() {return hash32((uint32_t)gameepisode*7919u+(uint32_t)gamemap*104729u+0x51ed270bu);}
bool liquidSector(const sector_t &sector) {
    size_t index=&sector-sectors;
    return index<sectorMist.size()&&sectorMist[index].liquid;
}
// How much of the wind's speed a flake rides: a third in a breeze, nearly
// all of it in a gale. sky/snow.glsl's snowCarried repeats it for the far
// flakes: change both, or the sky's snow falls apart from the near.
float carried() {return drift+1.2f*std::max(0.0f,windAmount()-0.5f);}
// A flake's sway about its path, units, at age seconds: wider and quicker
// the harder it blows (tumbling in a gale).
void sway(float age,uint32_t seed,float &x,float &y) {
    float phase=hashUnit(7,8,seed)*2*(float)doomPi,wind=windAmount(),size=3*(1+2*wind),quick=1+wind;
    x=size*std::sin(age*1.6f*quick+phase);y=size*std::cos(age*1.25f*quick+phase*1.7f);
}
// How much of a blizzard it is: how hard it snows times how hard it blows
// (0 a still fall, about 0.9 Extreme in a gale). It thickens the flakes,
// streaks them and whites out the distance (snowHazeUniforms); sky/snow.glsl
// repeats it (snowStorm) for the far flakes and the sky.
float storm() {return std::clamp(fallNow*windAmount(),0.0f,1.0f);}
// One flake: a Doom pixel (two up close for the bigger ones); resting, it
// stands on the floor. Flying fast in a storm it streaks: a pixel more
// behind it for each one it moves in a tic (up to four), edge to edge
// along its way (spread over the whole tic's way, close up they were a
// dotted row of squares).
void flake(const Uniforms &camera,float x,float y,float z,float distance,const sector_t &sector,uint32_t seed,bool resting,float alpha,float vx=0,float vy=0) {
    float size=distance<96&&hashUnit(9,9,seed)<0.3f?1.1f:0.6f;
    std::array<float,3> color=weatherColor(sector,x,y,z,snowAlbedo);
    if(!resting&&distance>96)beamGlint(x,y,z,seed,0.06f,color);   // ice catching the flashlight, small far flakes only (beam_air.cpp)
    pixelBlock(particleVertices,camera,x,y,z,size,1,color,resting,&snowAlbedo,true,nullptr,alpha);
    float step=std::hypot(vx,vy)/TICRATE,unit=std::max(distance,4.0f)/(100*camera.projection[1]);
    if(resting||storm()<0.35f||step<=unit)return;
    int pieces=std::min(4,(int)(step/unit));
    float pixel=std::max(unit,size*0.8f);   // a block's width behind the last
    for(int k=1;k<=pieces;++k) {
        float back=k*pixel/step;   // share of the tic's way
        float px=x-vx/TICRATE*back,py=y-vy/TICRATE*back,pz=z+1.2f*back;
        if(!fliesOutside(sector,px,py,pz))break;   // the streak stops at a wall
        pixelBlock(particleVertices,camera,px,py,pz,size,1,color,false,&snowAlbedo,true,nullptr,alpha*(1-0.15f*k));
    }
}
}

// The snow around the eye: two flakes per 16-unit cell in a share of the
// cells as thick as it snows (the same cells first as it thickens), placed
// round the eye and downwind of it (flyArea), drawn out to 512 units where
// they are now and fading out past 384 into the sky's far sheets, in sky
// sectors only, each drifting down its sector's height on the wind, landing
// in its cell and lying there a little over a second. Then the broken
// windows the wind blows it in by.
static void buildSnow(const Uniforms &camera) {
    constexpr float cell=16,reach=512,fade=384,rest=1.2f,typical=168.0f/42;   // a fall from skyFallTop's height
    float now=std::floor(gameSeconds()*TICRATE)/TICRATE;
    // A blizzard thickens it: every cell full (two flakes each) in the worst of it.
    float density=std::min(1.0f,0.6f*fallNow*(1+0.9f*storm()));
    const int perCell=2;
    uint32_t base=snowSeed();
    float windX=windNow[0]*carried(),windY=windNow[1]*carried();
    const FlyArea area=flyArea(camera,windX,windY,typical);
    const float placed=reach+area.grow;
    int gx0=(int)std::floor((area.x-placed)/cell),gx1=(int)std::floor((area.x+placed)/cell);
    int gy0=(int)std::floor((area.y-placed)/cell),gy1=(int)std::floor((area.y+placed)/cell);
    for(int gy=gy0;gy<=gy1;++gy)for(int gx=gx0;gx<=gx1;++gx)for(int k=0;k<perCell;++k) {
        uint32_t seed=base^(uint32_t)k*0x9e3779b9u;
        if(hashUnit(gx,gy,seed)>=density) continue;
        float x0=(gx+hashUnit(gx,gy,seed^1u))*cell,y0=(gy+hashUnit(gx,gy,seed^2u))*cell;
        // Behind the eye only near it, where a glance down still shows them.
        if(std::hypot(x0-area.x,y0-area.y)>placed||flyOutOfReach(camera,x0,y0,windX,windY,typical,reach)||flyBehind(camera,x0,y0,windX,windY,typical)) continue;
        const sector_t *sector=weatherSectorAt(x0,y0);
        if(!sector||!underSky(int(sector-sectors))) continue;
        float floor=floorZ(sector),top=skyFallTop(*sector,168),height=top-floor;
        if(height<8) continue;
        uint32_t own0=hash32((uint32_t)gx*73856093u^(uint32_t)gy*19349663u^seed);
        float speed=42*(0.8f+0.4f*hashUnit(gx,gy,seed^3u)),fall=height/speed,period=fall+rest;
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
        // The air's push (air_push.cpp): the gusts as they roll past, so it
        // still lands in its cell, and any blast's shove; never into a wall.
        if(age<fall) {
            float push[3];
            airShift(int(sector-sectors),x,y,z,now,now-early,push);
            airKeep(x,y,push,[&](float px,float py){return fliesOutside(*sector,px,py,z+push[2]);});
            x+=push[0];y+=push[1];z+=push[2];
        }
        float distance=std::hypot(x-camera.eye[0],y-camera.eye[1]);   // where it is now
        if(distance>reach) continue;
        float alpha=(0.85f-0.45f*distance/reach)*flyFade(distance,fade,reach)*std::min(1.0f,fallNow*2);
        if(age<fall) {
            // Faded in over its first half second, so where it starts never shows.
            if(z>floor+1) flake(camera,x,y,z,distance,*sector,own,false,alpha*std::min(1.0f,flying*2),windX,windY);
        }
        else if(distance<256&&!liquidSector(*sector)) flake(camera,lx,ly,floor,distance,*sector,own,true,alpha*(1-(age-fall)/rest));
    }
    // Through broken windows: flakes blown in across the opening, coming
    // through as the flakes outside fly (the same share of the wind its way,
    // falling as fast, swaying as wide) so none changes pace at the glass,
    // then slowing as the wind drops inside and settling on the sill or the
    // floor. As many as the window is wide, the snow thick and the wind
    // blowing in.
    for(int i=0;i<windowPaneCount();++i) {
        PaneView p;
        if(!windowPane(i,p)||!p.broken||!p.sky||p.top-p.bottom<4) continue;
        float in=paneInward(p);
        if(in<=0.05f) continue;
        // The outside flakes' pace, and at least a little of it inward.
        float vx=windNow[0]*carried(),vy=windNow[1]*carried(),into=vx*p.inX+vy*p.inY;
        if(into<10) {vx+=p.inX*(10-into);vy+=p.inY*(10-into);}
        float ex=p.bx-p.ax,ey=p.by-p.ay,len=std::hypot(ex,ey);
        float mx=(p.ax+p.bx)/2,my=(p.ay+p.by)/2,distance=std::hypot(mx-camera.eye[0],my-camera.eye[1]);
        if(len<4||distance>reach) continue;
        float draft=paneDraft(p),reachIn=0.5f+0.7f*draft;   // a draft through the room carries it in, else it only puffs in
        int count=std::min(24,(int)std::lround(len/8*fallNow*in*(0.3f+0.7f*draft)));
        for(int n=0;n<count;++n) {
            uint32_t seed=p.seed^(uint32_t)n*0x9e3779b9u^0x2c1b3c6du;
            float period=3.5f+1.5f*hashUnit(n,0,seed),clock=now+hashUnit(n,1,seed)*period*5;
            int cycle=(int)std::floor(clock/period);
            float age=clock-cycle*period;
            uint32_t own=seed^(uint32_t)cycle*2654435761u;
            float u=hashUnit(cycle,1,own)*len,z0=p.bottom+(p.top-p.bottom)*(0.2f+0.8f*hashUnit(cycle,2,own));
            float spread=0.85f+0.3f*hashUnit(cycle,3,own),sink=42*(0.8f+0.4f*hashUnit(cycle,4,own));
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
            if(z>floor+1) {flake(camera,x,y,z,distance,*at,own,false,0.75f);continue;}
            // Landed: when it reached the floor, then lying there a moment.
            float landed=std::max(0.0f,z0-floor)/sink,lx,ly;
            inside(landed,lx,ly);
            const sector_t *under=weatherSectorAt(lx,ly);
            if(under&&age-landed<rest&&!liquidSector(*under))
                flake(camera,lx,ly,floorZ(under),distance,*under,own,true,0.75f*(1-(age-landed)/rest));
        }
    }
}

// A blizzard whites out the distance (scene.cpp, after the sky's horizon
// haze): the far open ground and walls fade toward snow lit as the open air
// is, and past a third of a storm everything close by too (c.climate.zw,
// world.frag), as thick as the storm (sky/snow.glsl's snowHaze does the sky).
void snowHazeUniforms(Uniforms &camera) {
    if(falling()!=WEATHER_SNOW||!settings.weather)return;
    float amount=std::max(storm(),whiteoutNow());   // falling snow whitens still air too (whiteout.cpp)
    if(amount<=0.05f)return;
    float luma=0.299f*camera.haze[0]+0.587f*camera.haze[1]+0.114f*camera.haze[2];
    for(int c=0;c<3;++c)camera.haze[c]+=(snowAlbedo[c]*(0.45f+0.7f*luma)-camera.haze[c])*std::min(1.0f,amount*1.2f);
    if(settings.fog)camera.haze[3]=std::max(camera.haze[3],1.6f*amount);
}

namespace {
// How deep snow lies on the ground: it comes down far more slanted on the
// wind and gathers where the rain would soak, deep against the foot of a
// wall facing the wind and in a drift on the lee side of walls, where the
// wind drops it, with a strip of nearly bare ground right under them; over
// the open ground in ragged patches thin, lying and deep. Sheltered by a
// roof's edge or a ledge overhead there is no wall to drift against: the
// ground there stays nearly bare.
// How hard it falls lays it all deeper: light snow a dusting the ground
// shows through, extreme a blanket (heavy is the old look). How hard it
// blows shapes it: in still air it lies evenly in round patches, in a gale
// the open ground is scoured into long bare streaks and deep ones drawn
// out along the wind, and only a wind piles it against walls.
float snowGround(const WeatherGround &cell) {
    float stretch=1+3*cell.wind;
    float along=cell.x*cell.hx+cell.y*cell.hy,across=cell.y*cell.hx-cell.x*cell.hy;
    float n=valueNoise(along/stretch,across,64,cell.seed);
    n=0.5f+(n-0.5f)*(0.5f+cell.wind)+0.8f*(cell.amount-0.75f)-0.4f*std::max(0.0f,cell.wind-0.5f);   // a gale scours the open ground
    float deep=n<0.08f?0.1f:n<0.3f?0.25f:n<0.62f?0.6f:n<0.95f?0.85f:1.0f;
    if(valueNoise(along/stretch,across,36,cell.seed^0x165667b1u)>0.72f+0.4f*(0.75f-cell.amount))deep=1;
    if(cell.windward&&cell.wind>0.1f)deep=std::max(deep,std::min(1.0f,0.6f+0.5f*cell.wind));
    if(cell.lee)deep=cell.leeAt<=12||cell.roofed?0.15f:std::max(deep,std::min(1.0f,0.7f+0.4f*cell.wind));
    return deep;
}
}
// What snow blows in by a door opened during play sticks where it lands
// (it is the wet kind that clumps), three units deep lying full, melting in
// a minute and a half in a heated room and lasting as cold as outside.
const FilmRules snowFilm={0,0,INFINITY,INFINITY,INFINITY,90,40,3,true};
// Snow falls a unit and a half for one across, frosts the glass in about 20
// seconds and settles white under a broken window in about half a minute.
// It is cold out in it, and what it lays down melts in the warm. Light
// enough that the eddies bring a little round to a window out of the wind.
const WeatherKind snowWeather={"snow",1.5f,20,30,true,snowGround,buildSnow,I_AmbienceSnow,nullptr,-5,true,nullptr,nullptr,true,true,
                               &snowFilm,false,2.5f,0.15f,INFINITY,0.1f,48};
} // namespace doom3d
