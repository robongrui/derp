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
enum Weather { WEATHER_NONE, WEATHER_RAIN, WEATHER_SNOW, WEATHER_NUKAGE, WEATHER_SAND, WEATHER_ASH, WEATHER_FOG, WEATHER_DRY_ICE, WEATHER_KINDS };
// Settings::timeOfDay and Settings::wind; Settings::temperature is degrees C
// (-30 to 50 in tens) or climateOwn.
enum ClimateTime { TIME_OWN, TIME_DAWN, TIME_MORNING, TIME_NOON, TIME_AFTERNOON, TIME_DUSK, TIME_NIGHT };
enum ClimateWind { WIND_OWN, WIND_STILL, WIND_BREEZE, WIND_STRONG, WIND_GALE };
constexpr int climateOwn=100;
// Settings::weather (0 clear, 1 the map's own, then rain on) as a Weather,
// -1 for the map's own.
inline int weatherOfSetting(int weather) {return weather==1?-1:weather<=0?WEATHER_NONE:weather-1;}
// The temperatures a kind falls or lies in.
inline void kindTemperatures(int kind,int &low,int &high) {
    low=-30;high=50;
    if(kind==WEATHER_RAIN||kind==WEATHER_NUKAGE) {low=0;high=40;}
    else if(kind==WEATHER_SNOW) high=0;
    else if(kind==WEATHER_SAND) low=10;
    else if(kind==WEATHER_FOG) {low=-10;high=20;}
    else if(kind==WEATHER_DRY_ICE) high=20;
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
}
#endif
