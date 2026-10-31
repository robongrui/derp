/* The climate the player picks (F4 Weather, Time of day, Temperature, Wind,
   Wind from; climate_rules.h): what it makes of each level. The hour sets
   the sun's height, colour and strength (placeSun) and the episode's mood
   (levelMood): how bright the outdoors and the sky are, the sky's tint, and
   at night no sun and the lit rooms shining out, as in the authored E1. The
   heat of the day, from 30 C, makes the sun stronger, harder and warmer,
   bleaches the sky, raises a warm haze and shimmers the far ground
   (ground_heat.cpp); cold under a clear sky frosts the windows. The wind's
   speed and heading go to wind.cpp. Where the map's own weather, hour or wind
   clashes with a choice the player made, the map's gives way to the nearest
   that fits. Never on Phobos: there is no air. Render-only. */
#include "internal.h"

namespace doom3d {
namespace {
// An hour of the day: the sun's light and drawn heights (degrees), its tint,
// how much harder its shadows (times Game_Sun's shade) and how much more it
// adds, the disc's least glow; the outdoors' light (the mood's outdoor), the
// sky's tint; whether the sun is up.
struct Hour { float light,disc; std::array<float,3> tint; float shade,gain,glow,outdoor; std::array<float,3> sky; bool up; };
const Hour hours[]={
    {40,40,{1,1,1},1,0,0,1,{1,1,1},true},                                // the map's own (unused)
    {20,3,{1,0.72f,0.55f},0.75f,0,0.8f,0.8f,{1.06f,0.86f,0.82f},true},     // dawn
    {35,22,{1,0.93f,0.84f},1,0.05f,1,0.95f,{1,0.98f,0.96f},true},         // morning
    {68,68,{1,0.98f,0.94f},1.25f,0.15f,1,1.1f,{1.04f,1.03f,1},true},      // noon
    {45,32,{1,0.9f,0.76f},1.05f,0.08f,1,1,{1.05f,0.98f,0.9f},true},       // afternoon
    {20,3,{1,0.6f,0.4f},0.75f,0,0.9f,0.72f,{1.1f,0.76f,0.62f},true},      // dusk
    {40,40,{1,1,1},1,0,0,0.5f,{0.72f,0.8f,1},false},                      // night
};
int hourNow=TIME_OWN;
bool explicitTemperature() {return settings.temperature!=climateOwn&&!Game_Phobos();}
}

// The hour the mood was last worked out for (TIME_OWN: as derived).
int climateHour() {levelMood();return hourNow;}
// The level's mood under the player's hour and heat. A night of the map's
// own (no sun, the outdoors dark: the authored E1) gives way to dusk or
// the afternoon when the player made it hotter than a night gets.
void climateMood(game_mood &mood) {
    hourNow=TIME_OWN;
    if(Game_Phobos())return;
    // How hard it falls: a quarter of a downpour up to all of it.
    if(settings.fallAmount>0&&settings.fallAmount<=4)mood.rainAmount=settings.fallAmount/4.0f;
    int hour=settings.timeOfDay;
    if(hour==TIME_OWN&&explicitTemperature()&&!mood.sun&&mood.outdoor<0.8f&&settings.temperature>hottestAt(TIME_NIGHT))
        hour=settings.temperature>hottestAt(TIME_DUSK)?TIME_AFTERNOON:TIME_DUSK;
    hourNow=hour;
    if(hour!=TIME_OWN) {
        const Hour &h=hours[hour];
        mood.outdoor=h.outdoor;mood.sun=h.up;mood.shineOut=!h.up;
        for(int c=0;c<3;++c)mood.skyTint[c]=h.sky[c];
    }
    float heat=climateHeat();
    static const float warm[3]={1.06f,1,0.88f};
    mood.outdoor+=0.1f*heat;
    for(int c=0;c<3;++c)mood.skyTint[c]*=1+(warm[c]-1)*heat;
}
// The sun as the sky paints it, under the player's hour and heat: the
// direction stays the sky's, the hour sets how high it stands and how it
// shines, the heat makes it stronger, harder and warmer.
void climateSun(float &elevation,float &discElevation,std::array<float,3> &tint,float &shade,float &gain,float &glow) {
    int hour=climateHour();
    if(hour!=TIME_OWN) {
        const Hour &h=hours[hour];
        elevation=h.light*doomPi/180;discElevation=h.disc*doomPi/180;
        for(int c=0;c<3;++c)tint[c]+=(h.tint[c]-tint[c])*0.7f;
        shade*=h.shade;gain+=h.gain;glow=std::max(glow,h.glow);
    }
    float heat=climateHeat();
    static const float hot[3]={1,0.9f,0.72f};
    for(int c=0;c<3;++c)tint[c]+=(hot[c]-tint[c])*0.35f*heat;
    shade*=1+0.3f*heat;gain+=0.15f*heat;glow=std::max(glow,heat);
}
// The temperature outside (C): the player's, or the falling kind's own.
float climateTemperature(Weather kind) {return explicitTemperature()?(float)settings.temperature:weatherKind(kind).outside;}
// The heat of the day: 0 up to 30 C, 1 at 50 C (only the player's: no
// weather of a map's own is that hot). The cold on clear windows: 0 at 0 C,
// 1 at -20 C, only while nothing falls (what falls frosts them itself).
float climateHeat() {return explicitTemperature()?std::clamp((settings.temperature-30)/20.0f,0.0f,1.0f):0.0f;}
float climateCold() {return explicitTemperature()&&falling()==WEATHER_NONE?std::clamp(-settings.temperature/20.0f,0.0f,1.0f):0.0f;}
// The map's own weather under the player's temperature and wind: snow above
// freezing falls as rain, rain below it as snow; the rest that doesn't fit
// (rain past 40 C, fog in the heat, a sandstorm in still air) clears.
// Nukage rain is a rule of the game, kept as the map has it.
Weather climateWeather(Weather own) {
    if(own==WEATHER_NUKAGE||own==WEATHER_NONE||Game_Phobos())return own;
    if(explicitTemperature()) {
        int low,high,t=settings.temperature;kindTemperatures(own,low,high);
        if(t<low||t>high) {
            if(own==WEATHER_SNOW&&t<=40)own=WEATHER_RAIN;
            else if(own==WEATHER_RAIN&&t<0)own=WEATHER_SNOW;
            else own=WEATHER_NONE;
        }
    }
    return kindWind(own,settings.wind)?own:WEATHER_NONE;
}
// The wind's speed (units a second) for the player's choice, or the map's
// own (own); a map's own wind under weather the player picked gives way to a
// breeze where it doesn't fit (a sandstorm in still air, fog in a gale).
float climateWindSpeed(float own) {
    static const float speeds[]={0,0,115,172,230};   // still, breeze, strong, gale
    if(settings.wind>WIND_OWN&&settings.wind<=WIND_GALE)return speeds[settings.wind];
    Weather kind=falling();
    if(settings.weather!=1&&kind!=WEATHER_NONE) {
        int band=own<57.5f?WIND_STILL:own<143.75f?WIND_BREEZE:own<201.25f?WIND_STRONG:WIND_GALE;
        if(!kindWind(kind,band))return speeds[WIND_BREEZE];
    }
    return own;
}
// The way the player's wind blows to (map degrees: 0 east, 90 north), from
// the compass point it comes from; false for the map's own.
bool climateWindHeading(float &degrees) {
    if(settings.windFrom<1||settings.windFrom>8)return false;
    degrees=90-45*(settings.windFrom-1)+180.0f;
    return true;
}
// The shaders' share (c.climate), and the warm haze over the far outdoors
// (c.haze, after scene.cpp fills it from the sky's horizon).
void climateUniforms(Uniforms &camera) {
    float heat=climateHeat();
    camera.climate[0]=heat;camera.climate[1]=climateCold();camera.climate[2]=camera.climate[3]=0;
    if(heat<=0)return;
    float luma=0.299f*camera.haze[0]+0.587f*camera.haze[1]+0.114f*camera.haze[2];
    static const float warm[3]={1,0.93f,0.8f};
    for(int c=0;c<3;++c)camera.haze[c]+=((luma*1.25f+0.15f)*warm[c]-camera.haze[c])*0.6f*heat;
    if(settings.fog)camera.haze[3]=std::max(camera.haze[3],0.7f*heat);
}
} // namespace doom3d
