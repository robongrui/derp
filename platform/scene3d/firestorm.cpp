/* Firestorm (a WeatherKind; weather.cpp says when it blows and how thick):
   the air over a burning city. Sparks carried off the fires on the wind, rising
   on the heat in wavering paths, yellow-orange and cooling through orange
   to a dull red as they go out, glowing into the bloom as a monster's eyes
   do (too small to light anything); among them flakes of soot, black,
   drifting down slowly. Doom pixels, stepping at the tic rate. On the open
   ground ash lies in ragged patches, thinner than an eruption's, a pale
   grey (firestormGround, world/settled.glsl); nothing wets, nothing blows
   in: the fires are elsewhere. Over the open ground the
   smoke's haze, lit orange from beneath (firestormHazeUniforms); past the
   map's sky the sparks fly on in sheets (sky/firestorm.glsl). Render-only,
   with its own random numbers. */
#include "internal.h"

namespace doom3d {
namespace {
constexpr float carried=0.8f;  // of the wind's speed
constexpr float sparkShare=0.45f;
const std::array<float,3> soot={0.07f,0.06f,0.055f};
uint32_t firestormSeed() {return hash32((uint32_t)gameepisode*7919u+(uint32_t)gamemap*104729u+0x2f1a9c37u);}
// A spark's glow at heat (1 fresh off the fire, 0 out): yellow, orange,
// a dull red, flickering every other tic.
std::array<float,3> sparkColor(float heat,uint32_t seed,float now) {
    static const std::array<float,3> ramp[3]={{{0.7f,0.2f,0.04f}},{{1.0f,0.52f,0.12f}},{{1.0f,0.86f,0.45f}}};
    float f=std::clamp(heat,0.0f,1.0f)*2,flicker=0.7f+0.3f*hashUnit((int)(now*TICRATE)/2,5,seed);
    int i=std::min(1,(int)f);
    float t=f-i;
    std::array<float,3> color;
    for(int c=0;c<3;++c)color[c]=std::min(1.0f,(ramp[i][c]+(ramp[i+1][c]-ramp[i][c])*t)*flicker);
    return color;
}
// A wavering path about its line, units, at age seconds: sparks swirl in the
// hot air, wider than ash sways.
void waver(float age,uint32_t seed,float &x,float &y,float &z) {
    float phase=hashUnit(7,8,seed)*2*(float)doomPi;
    x=9*std::sin(age*2.3f+phase);y=9*std::cos(age*1.7f+phase*1.3f);z=6*std::sin(age*3.1f+phase*2.1f);
}
}

// The smoke's haze over the open ground (c.haze, world.frag): far surfaces
// under the sky fade into a dark brown smoke lit amber from beneath (red
// read as lava), as over a city on fire (sky/firestorm.glsl's firestormHaze).
// And the light the sky gives the map (c.fx.yzw, its share of each
// surface's light): the smoke overhead, lit by the fires below, throws
// their orange back down, pulsing slowly as they flare and die back.
void firestormHazeUniforms(Uniforms &camera) {
    float amount=weatherNow(WEATHER_FIRESTORM);
    if(amount<=0)return;
    static const float smoke[3]={0.085f,0.07f,0.055f},glow[3]={0.13f,0.078f,0.024f},fire[3]={1.4f,0.86f,0.46f};
    float share=std::min(1.0f,amount*1.25f);
    for(int c=0;c<3;++c)camera.haze[c]+=(smoke[c]+glow[c]*amount-camera.haze[c])*share;
    if(settings.fog)camera.haze[3]=std::max(camera.haze[3],1.2f*amount);
    float now=std::floor(gameSeconds()*TICRATE)/TICRATE;
    float pulse=0.85f+0.3f*valueNoise(now,0,3,firestormSeed()^0x4f1bbcdcu);
    for(int c=0;c<3;++c)camera.fx[1+c]+=(fire[c]*pulse-camera.fx[1+c])*share;
}

// The sparks and soot round the eye: in a share of the 24-unit cells out
// to 768 units as thick as they fly, placed round the eye and downwind of
// it (flyArea), fading toward the edge where the far sheets take over. In
// sky sectors only, each living a few seconds: a spark sets out low on the
// wind and rises, cooling; soot sets out high and sinks.
static void buildFirestorm(const Uniforms &camera) {
    constexpr float reach=768,cell=24,typical=4;
    float now=std::floor(gameSeconds()*TICRATE)/TICRATE;
    float density=0.35f*fallNow;
    uint32_t base=firestormSeed();
    float windX=windNow[0]*carried,windY=windNow[1]*carried;
    const FlyArea area=flyArea(camera,windX,windY,typical);
    const float outer=reach+area.grow;
    int gx0=(int)std::floor((area.x-outer)/cell),gx1=(int)std::floor((area.x+outer)/cell);
    int gy0=(int)std::floor((area.y-outer)/cell),gy1=(int)std::floor((area.y+outer)/cell);
    for(int gy=gy0;gy<=gy1;++gy)for(int gx=gx0;gx<=gx1;++gx) {
        if(hashUnit(gx,gy,base)>=density) continue;
        float x0=(gx+hashUnit(gx,gy,base^1u))*cell,y0=(gy+hashUnit(gx,gy,base^2u))*cell;
        if(std::hypot(x0-area.x,y0-area.y)>outer||flyOutOfReach(camera,x0,y0,windX,windY,typical,reach)||flyBehind(camera,x0,y0,windX,windY,typical)) continue;
        const sector_t *sector=weatherSectorAt(x0,y0);
        if(!sector||!underSky(int(sector-sectors))) continue;
        float floor=floorZ(sector),top=skyFallTop(*sector,168),height=top-floor;
        if(height<16) continue;
        uint32_t own0=hash32((uint32_t)gx*73856093u^(uint32_t)gy*19349663u^base);
        bool spark=hashUnit(gx,gy,base^3u)<sparkShare;
        float life=(spark?2.5f:5.0f)*(0.75f+0.5f*hashUnit(gx,gy,base^4u));
        float clock=now+hashUnit(gx,gy,base^5u)*life*7;
        int cycle=(int)std::floor(clock/life);
        float age=clock-cycle*life,share=age/life;
        uint32_t own=own0^(uint32_t)cycle*2654435761u;
        // Through its cell at the middle of its life, on the wind.
        float early=age-life*0.5f,wx,wy,wz;
        waver(age,own,wx,wy,wz);
        float x=x0+windX*early+wx,y=y0+windY*early+wy;
        float z=spark?floor+8+height*(0.15f+0.3f*hashUnit(cycle,1,own))+(25+20*hashUnit(cycle,2,own))*age+wz
                     :floor+height*(0.4f+0.5f*hashUnit(cycle,1,own))-12*age+wz*0.5f;
        if(z<=floor+1||z>=top||!fliesOutside(*sector,x,y,z)) continue;
        float distance=std::hypot(x-camera.eye[0],y-camera.eye[1]);
        if(distance>reach) continue;
        // In and out over the first and last fifth of its life, so neither shows.
        float fade=std::min({1.0f,share*5,(1-share)*5})*flyFade(distance,reach-192,reach)*std::min(1.0f,fallNow*2);
        float size=distance<128?1.1f:0.6f;
        if(spark) pixelBlock(glowVertices,camera,x,y,z,size,1,sparkColor(1-share,own,now),false,nullptr,false,nullptr,0.95f*fade);
        else pixelBlock(particleVertices,camera,x,y,z,size,1,weatherColor(*sector,x,y,z,soot),false,&soot,true,nullptr,0.8f*fade);
    }
    // The far sheets' soot (sky/firestorm.glsl) at the outdoors' usual light.
    float light=std::clamp(0.6f*levelMood().outdoor,0.12f,1.0f);
    std::array<float,3> usual;
    for(int c=0;c<3;++c)usual[c]=soot[c]*light*1.15f+0.03f;
    farFall.settle(usual);
}

namespace {
// How deep the ash lies: in patches with the ground showing between, more
// at the foot of a wall facing the wind, little in a wall's lee and under
// a roof's edge.
float firestormGround(const WeatherGround &cell) {
    float n=valueNoise(cell.x,cell.y,48,cell.seed),deep=n<0.35f?0.3f:n<0.7f?0.45f:0.6f;
    if(cell.windward)deep=0.7f;
    if(cell.lee)deep=cell.roofed?0.05f:0.25f;
    return deep;
}
void quiet(float) {}
// Burnt-out ash, pale grey with darker cinders through it (settled.glsl):
// sooty dark it vanished on the city's dark brick and read as dirt on its
// pale paving.
std::array<float,3> ashLies() {return {0.46f,0.45f,0.43f};}
}
// A firestorm's sparks ride the air rather than fall; its ash lies as
// a slow fall slants, outside only, grey as
// ash wets nothing; it is hot out in it.
const WeatherKind firestormWeather={"firestorm",2.0f,20,30,true,firestormGround,buildFirestorm,quiet,nullptr,32,false,nullptr,ashLies,false,true};
} // namespace doom3d
