/* Nukage rain (a WeatherKind; weather.cpp says when it falls, the game
   p_weather.c what it does): rain in the colour of the game's own nukage
   (Game_RainLiquidFlat), its streaks and splashes rain.cpp's, a little
   brighter than rain in the dark as the nukage it is. The ground lies wet
   in the same steps, tinted (wear.glsl), the glass runs with its drops
   (glass.frag), the far sky streaks with it (sky.frag), and it sounds as
   rain does. The monsters out in it it slowly eats away where its drops
   land, the bites smoking (craters.cpp). Render-only, with its own random numbers. */
#include "internal.h"
extern "C" {
#include "../audio/sound_options.h"
}

namespace doom3d {
namespace {
std::array<float,3> colour={0.3f,0.6f,0.2f};
unsigned colourLevel=~0u;
}
// The average of the nukage flat's pixels, brightened so its strongest
// channel reads like rain does (a liquid's average is darker than its drops).
std::array<float,3> nukageColor() {
    if(colourLevel==r_levelserial)return colour;
    colourLevel=r_levelserial;
    const char *name=Game_RainLiquidFlat();
    int lump=name?W_CheckNumForName((char*)name):-1;
    if(lump<0||W_LumpLength(lump)<4096)return colour={0.3f,0.6f,0.2f};
    const byte *flat=(const byte*)W_CacheLumpNum(lump,PU_CACHE);
    const byte *palette=(const byte*)W_CacheLumpName((char*)"PLAYPAL",PU_CACHE);
    std::array<float,3> sum={0,0,0};
    for(int pixel=0;pixel<4096;++pixel)for(int c=0;c<3;++c)sum[c]+=palette[flat[pixel]*3+c]/(4096.0f*255.0f);
    float peak=std::max({sum[0],sum[1],sum[2],1e-3f});
    for(int c=0;c<3;++c)colour[c]=sum[c]/peak*0.75f;
    return colour;
}

namespace {
void buildNukageRain(const Uniforms &camera) {
    buildRainDrops(camera,nukageColor(),0.35f);
    watchCraters(camera);
}
}
// Nukage rain falls as rain: the same slant, glass and soaking, the same
// wet ground (rainGround), tinted.
const WeatherKind nukageWeather={"nukage rain",4.5f,6,8,false,rainGround,buildNukageRain,I_AmbienceRain,nukageColor,6,false,nullptr,nullptr,true,true,
                                 &rainFilm,false,2.5f,0,INFINITY,0.2f,32};
} // namespace doom3d
