/* The climate the player picks in F4 (Game, Mode): the weather, the time of
   day, the temperature and the wind, each the map's own or one choice for
   every map, and which of them go together. The menu offers no choice that
   doesn't fit the others (climateFits); where the map's own clashes with a
   choice the player made, the map's gives way (scene3d/climate.cpp). Shared
   by the menu (settings_panel.cpp) and the renderer. */
#ifndef DOOM_CLIMATE_RULES_H
#define DOOM_CLIMATE_RULES_H

namespace doom3d {
// What falls under the sky (scene3d/weather.cpp). The number goes to the
// shaders as is (c.weather.w, WEATHER_* in shaders/common.glsl); a new kind
// adds its number there, its WeatherKind (its own file, like rain.cpp and
// snow.cpp) to weatherKinds in weather.cpp, its far sheet (its own
// sky/*.glsl), an F4 choice, a DERPINFO name and its range below.
enum Weather { WEATHER_NONE, WEATHER_RAIN, WEATHER_SNOW, WEATHER_NUKAGE, WEATHER_SAND, WEATHER_ASH, WEATHER_FOG, WEATHER_DRY_ICE, WEATHER_FIRESTORM, WEATHER_KINDS };
// Settings::timeOfDay and Settings::wind; Settings::temperature is degrees C
// (-30 to 50 in tens) or climateOwn.
enum ClimateTime { TIME_OWN, TIME_DAWN, TIME_MORNING, TIME_NOON, TIME_AFTERNOON, TIME_DUSK, TIME_NIGHT };
enum ClimateWind { WIND_OWN, WIND_STILL, WIND_BREEZE, WIND_STRONG, WIND_GALE };
constexpr int climateOwn=100;
// Settings::weather's Random: every map rolls its own climate (rollClimate).
constexpr int weatherRandom=10;
// Settings::weather (0 clear, 1 the map's own, then rain on) as a Weather,
// -1 for the map's own (and for Random, whose roll fits by itself).
inline int weatherOfSetting(int weather) {return weather==1||weather==weatherRandom?-1:weather<=0?WEATHER_NONE:weather-1;}
// The temperatures a kind falls or lies in.
inline void kindTemperatures(int kind,int &low,int &high) {
    low=-30;high=50;
    if(kind==WEATHER_RAIN||kind==WEATHER_NUKAGE) {low=0;high=40;}
    else if(kind==WEATHER_SNOW) high=0;
    else if(kind==WEATHER_SAND) low=10;
    else if(kind==WEATHER_FOG) {low=-10;high=20;}
    else if(kind==WEATHER_DRY_ICE) high=20;
    else if(kind==WEATHER_FIRESTORM) low=10;
}
// The hottest it gets at a time of day: the heat of the day needs the sun up.
inline int hottestAt(int time) {return time==TIME_DAWN||time==TIME_NIGHT?30:time==TIME_DUSK?40:50;}
// A sandstorm needs wind; fog and dry ice lie only in still air or a breeze.
inline bool kindWind(int kind,int wind) {
    if(wind==WIND_OWN)return true;
    if(kind==WEATHER_SAND)return wind!=WIND_STILL;
    if(kind==WEATHER_FOG||kind==WEATHER_DRY_ICE)return wind<=WIND_BREEZE;
    return true;
}
// Whether a combination goes together: kind a Weather or -1 (the map's own),
// the rest a choice or the map's own, which always fits.
inline bool climateFits(int kind,int time,int temperature,int wind) {
    if(temperature!=climateOwn) {
        if(time!=TIME_OWN&&temperature>hottestAt(time))return false;
        if(kind>=0) {int low,high;kindTemperatures(kind,low,high);if(temperature<low||temperature>high)return false;}
    }
    return kind<0||kindWind(kind,wind);
}
// The climate in force, in Settings' own numbers (weather 0 clear, 1 the
// map's own, 2 rain on; the rest as their Settings fields).
struct Climate { int weather,fallAmount,timeOfDay,temperature,wind,windFrom; };
// Random's roll for one map: seed mixes the playthrough's and the map's.
// Picked in the menu's order, each from what fits those before it, so it is
// never a stupid combination. Nukage rain is left out (a rule of the game,
// not a look); wind only where the game has it (no wind: the map's own).
inline Climate rollClimate(unsigned long long seed,bool wind) {
    auto next=[&seed]() {   // splitmix64
        seed+=0x9e3779b97f4a7c15ull;
        unsigned long long z=seed;
        z=(z^(z>>30))*0xbf58476d1ce4e5b9ull;z=(z^(z>>27))*0x94d049bb133111ebull;
        return z^(z>>31);
    };
    auto pick=[&next](const int *weights,int count) {
        int total=0;for(int i=0;i<count;++i)total+=weights[i];
        if(total<=0)return -1;
        int r=(int)(next()%(unsigned long long)total);
        for(int i=0;i<count;++i) {if(r<weights[i])return i;r-=weights[i];}
        return count-1;
    };
    Climate c{0,0,TIME_OWN,climateOwn,WIND_OWN,0};
    // Clear most often, then rain, snow and fog; the strange ones rarely.
    static const int kinds[]={WEATHER_NONE,WEATHER_RAIN,WEATHER_SNOW,WEATHER_FOG,WEATHER_SAND,WEATHER_ASH,WEATHER_FIRESTORM,WEATHER_DRY_ICE};
    static const int kindOdds[]={35,20,12,10,6,6,6,5};
    int kind=kinds[pick(kindOdds,8)];
    c.weather=kind==WEATHER_NONE?0:kind+1;
    if(kind!=WEATHER_NONE)c.fallAmount=1+(int)(next()%4);
    c.timeOfDay=TIME_DAWN+(int)(next()%6);
    // Mild more often than bitter or scorching, among what fits.
    static const int temperatureOdds[]={1,2,3,4,6,6,4,2,1};   // -30 to 50 C
    int odds[9];
    for(int i=0;i<9;++i)odds[i]=climateFits(kind,c.timeOfDay,-30+10*i,WIND_OWN)?temperatureOdds[i]:0;
    int t=pick(odds,9);
    c.temperature=t<0?climateOwn:-30+10*t;
    if(wind) {
        static const int windOdds[]={3,4,2,1};   // still, breeze, strong, gale
        int w[4];
        for(int i=0;i<4;++i)w[i]=climateFits(kind,c.timeOfDay,c.temperature,WIND_STILL+i)?windOdds[i]:0;
        int picked=pick(w,4);
        c.wind=picked<0?WIND_OWN:WIND_STILL+picked;
        c.windFrom=1+(int)(next()%8);
    }
    return c;
}
// scene3d/climate.cpp. climateNow: the climate in force on this map,
// Settings' own or under Random this map's roll; what is drawn reads it,
// never Settings' climate fields. climateRolled: this map's roll whatever
// Settings say; false with no map in play (the menu shows plain Random).
Climate climateNow();
bool climateRolled(Climate &climate);
}
#endif
