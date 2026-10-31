/* Wind (weather.cpp starts it with the level and steps it each tic): one
   wind of its own for the whole level, apart from the sun, blowing the way
   the episode's mood says (rainAngle) or else the way the level's hash
   picks, at its own steady speed: DERPINFO's windamount (0 still air, 0.5
   a steady breeze, 1 a gale), or else a breeze the level's hash picks. What
   falls rides it (rain.cpp, snow.cpp), the ground lies wet or white by it
   (bakeWetness, slanting the flatter the harder it blows), the openings
   facing it are pressed by it (weatherPressOn) and the drafts run with it
   (drafts.cpp), the far sky's rain and snow slant by it (sky.frag), and
   the wind is heard as hard as it blows (I_AmbienceWind). On top of the
   steady wind roll its gusts (windGust), which only what flies feels
   (air_push.cpp) and the wind's sound swells with.
   Render-only, with its own random numbers. */
#include "internal.h"
extern "C" {
#include "../audio/sound_options.h"
}

namespace doom3d {
float windNow[2]={0,0};
namespace {
float windSpeed=0,ownSpeed=0;
constexpr float breeze=115;   // units a second: windamount 0.5, the middle of what the hash picks
}
// Where the wind blows (unit): the player's (climate.cpp), else the episode
// mood's rain angle, or else the level's own, picked by its hash. Never the
// sun's: the two are apart.
void windHeading(float &x,float &y) {
    float authored=levelMood().rainAngle,picked;
    if(climateWindHeading(picked))authored=picked;
    float angle=authored>=0?authored*(float)doomPi/180:hashUnit(1,2,weatherSeed())*2*(float)doomPi;
    x=std::cos(angle);y=std::sin(angle);
}
void windNewLevel() {
    ownSpeed=DerpInfo_Map.windAmount>=0?2*breeze*DerpInfo_Map.windAmount:90+50*hashUnit(3,4,weatherSeed());
    windSpeed=climateWindSpeed(ownSpeed);
    windNow[0]=windNow[1]=0;
}
// The wind at t seconds into the level: along its heading, steady (the
// gusts never reach it: where the drafts lay the snow stays put,
// drafts.cpp), as hard as the player's climate has it (climate.cpp).
void windTick(float) {
    windSpeed=climateWindSpeed(ownSpeed);
    float x,y;
    windHeading(x,y);
    windNow[0]=x*windSpeed;windNow[1]=y*windSpeed;
    // Heard swelling and easing as the gusts roll past the player.
    float swell=0;
    if(const mobj_t *mo=players[displayplayer].mo)swell=0.6f*windGustRate(units(mo->x),units(mo->y),levelSeconds());
    I_AmbienceWind(std::clamp(windAmount()*(1+swell),0.0f,1.0f));
}
// How much of its press on an opening the wind has: full from the breezes
// the hash picks up (90 units a second), less in a lighter air, none in
// still air (nothing is pressed in at a window then).
float windSpeedNow() {return windSpeed;}
float windStrength() {return std::clamp(windSpeed/90,0.0f,1.0f);}
// How hard it blows as DERPINFO's windamount says it: 0 still air, 0.5 a
// breeze, 1 a gale (what the wind sounds like, I_AmbienceWind).
float windAmount() {return std::clamp(windSpeed/(2*breeze),0.0f,1.0f);}
// How far what the wind carries gets, against a breeze: a quarter in a
// light air, twice in a gale (how high sand blows, how far it is blown in).
float windCarry() {return std::clamp(windSpeed/breeze,0.25f,2.0f);}
// The gusts: how far (units, along the wind) they have shoved what flies at
// (x, y), t seconds in. Waves of it rolling downwind at the wind's speed,
// one long and one short, nothing in still air and a hard shove in a gale.
// What flies rides it on top of the steady wind (snow.cpp; sky/snow.glsl's
// snowGust repeats it for the far flakes: change both); nothing that lies
// or is solved sees it (the drafts and the bake keep the steady wind).
float windGust(float x,float y,float t) {
    float a=windAmount();
    if(a<=0||windSpeed<1)return 0;
    float along=(x*windNow[0]+y*windNow[1])/windSpeed;
    return 56*a*a*(0.65f*std::sin((along-windSpeed*t)/220)+0.35f*std::sin((along-windSpeed*t)/97+1.7f));
}
// How much faster than the steady wind the gusts blow at (x, y) now, as a
// share of it (the wind's sound swells with it, windTick).
float windGustRate(float x,float y,float t) {
    if(windSpeed<1)return 0;
    return std::clamp((windGust(x,y,t+0.05f)-windGust(x,y,t))/0.05f/windSpeed,-1.0f,1.0f);
}
// A kind's slant (units it falls for one across in a breeze) in this wind:
// flatter in a gale, nearly straight down in still air.
float windSlant(float slant) {return std::min(slant*breeze/std::max(windSpeed,4.0f),40.0f);}
} // namespace doom3d
