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
// An hour written as what it is: the sky's light (open shade) and what the sun adds to it on
// open ground, as levels under Doom's sun (Game_Sun's shade 0.3; another game's own sun bends
// them as it does its own day). The rule takes them as the outdoors' level L (outdoor, 1 at
// the most), shade = 1 - sky/L and gain = (sky + sun)/L - 1 (lightRule, bake_sun.cpp). They
// used to be set the other way round, a level and a softer shade: the softer shade of a low
// sun raised the sky's light by about what the lower level took, and dusk stood as bright in
// the shade as noon.
constexpr Hour hour(float light,float disc,std::array<float,3> tint,float skyLevel,float sunLevel,float outdoor,float glow,std::array<float,3> sky,bool up) {
    float level=outdoor<1?outdoor:1;
    return {light,disc,tint,(1-skyLevel/level)/0.3f,(skyLevel+sunLevel)/level-1,glow,outdoor,sky,up};
}
const Hour hours[]={
    //   light disc  sun's tint          sky     sun      outdoor glow  sky's tint
    hour(40,40,{1,1,1},                  0.7f,   0.3f,    1,      0,    {1,1,1},true),                 // the map's own (unused)
    hour(20,3, {1,0.72f,0.55f},          0.36f,  0.16f,   0.52f,  0.8f, {1.06f,0.86f,0.82f},true),     // dawn
    hour(35,22,{1,0.93f,0.84f},          0.665f, 0.3325f, 0.95f,  1,    {1,0.98f,0.96f},true),         // morning
    hour(68,68,{1,0.98f,0.94f},          0.625f, 0.525f,  1.1f,   1,    {1.04f,1.03f,1},true),         // noon
    hour(45,32,{1,0.9f,0.76f},           0.685f, 0.395f,  1,      1,    {1.05f,0.98f,0.9f},true),      // afternoon
    hour(20,3, {1,0.6f,0.4f},            0.32f,  0.16f,   0.48f,  0.9f, {1.1f,0.76f,0.62f},true),      // dusk
    hour(40,40,{1,1,1},                  0.25f,  0,       0.25f,  0,    {0.72f,0.8f,1},false),         // night
};
int hourNow=TIME_OWN;
bool explicitTemperature() {return climateNow().temperature!=climateOwn&&!Game_Phobos();}
}

// Weather's Random: this map's roll, the same every time for the same map in
// the same playthrough (Game_ClimateSeed: a new game rolls anew, a death or a
// save loaded keeps it). Wind only where the game has it.
namespace {
Climate roll() {
    static Climate rolled{};static unsigned long long rolledFor=~0ull;
    bool wind=Game_HasEffect(GAME_EFFECT_WIND)!=0;
    unsigned long long key=((unsigned long long)Game_ClimateSeed()<<32)^((unsigned long long)gameepisode<<16)^((unsigned long long)gamemap<<1)^(wind?1:0);
    if(key!=rolledFor) {
        rolledFor=key;rolled=rollClimate(key,wind);
        fprintf(stderr,"3D climate: Random rolled weather %d, amount %d, hour %d, %d C, wind %d from %d.\n",
                rolled.weather,rolled.fallAmount,rolled.timeOfDay,rolled.temperature,rolled.wind,rolled.windFrom);
    }
    return rolled;
}
}
bool climateRolled(Climate &climate) {
    if(gamestate!=GS_LEVEL||Game_Phobos())return false;
    climate=roll();
    return true;
}
// Time of day's Random: the same seed as Weather's, salted so the hour doesn't
// follow the weather Random would have rolled.
bool climateHourRolled(int temperature,int &hour) {
    if(gamestate!=GS_LEVEL||Game_Phobos())return false;
    const derp_map &own=mapInfo();
    if(temperature==climateOwn&&own.temperatureSet)temperature=own.temperature;
    unsigned long long key=((unsigned long long)Game_ClimateSeed()<<32)^((unsigned long long)gameepisode<<16)^((unsigned long long)gamemap<<1)
                          ^((unsigned long long)(temperature+1000)<<40)^0x686f7572ull;   // "hour"
    hour=rollHour(key,temperature);   // a handful of steps: the menu's draft and the game's settings each ask
    return true;
}
// Where the player leaves the hour or the temperature to the map, the map's own DERPINFO (or
// DERP mode's authored look, mapInfo) may give them.
Climate climateNow() {
    if(settings.weather==weatherRandom)return roll();
    Climate c{settings.weather,settings.fallAmount,settings.timeOfDay,settings.temperature,settings.wind,settings.windFrom};
    if(gamestate==GS_LEVEL&&!Game_Phobos()) {
        const derp_map &own=mapInfo();
        if(c.timeOfDay==TIME_OWN&&own.time>TIME_OWN&&own.time<=TIME_NIGHT)c.timeOfDay=own.time;
        if(c.temperature==climateOwn&&own.temperatureSet)c.temperature=own.temperature;
    }
    if(c.timeOfDay==timeRandom&&!climateHourRolled(c.temperature,c.timeOfDay))c.timeOfDay=TIME_OWN;
    return c;
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
    if(climateNow().fallAmount>0&&climateNow().fallAmount<=4)mood.rainAmount=climateNow().fallAmount/4.0f;
    int hour=climateNow().timeOfDay;
    if(hour==TIME_OWN&&explicitTemperature()&&!mood.sun&&mood.outdoor<0.8f&&climateNow().temperature>hottestAt(TIME_NIGHT))
        hour=climateNow().temperature>hottestAt(TIME_DUSK)?TIME_AFTERNOON:TIME_DUSK;
    hourNow=hour;
    static int told=-1;
    if(settings.timeOfDay==timeRandom&&hour!=told) {told=hour;fprintf(stderr,"3D climate: Random rolled hour %d.\n",hour);}
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
float climateTemperature(Weather kind) {return explicitTemperature()?(float)climateNow().temperature:weatherKind(kind).outside;}
// The heat of the day: 0 up to 30 C, 1 at 50 C (only the player's: no
// weather of a map's own is that hot). The cold on clear windows: 0 at 0 C,
// 1 at -20 C, only while nothing falls (what falls frosts them itself).
float climateHeat() {return explicitTemperature()?std::clamp((climateNow().temperature-30)/20.0f,0.0f,1.0f):0.0f;}
float climateCold() {return explicitTemperature()&&falling()==WEATHER_NONE?std::clamp(-climateNow().temperature/20.0f,0.0f,1.0f):0.0f;}
// The map's own weather under the player's temperature and wind: snow above
// freezing falls as rain, rain below it as snow; the rest that doesn't fit
// (rain past 40 C, fog in the heat, a sandstorm in still air) clears.
// Nukage rain is a rule of the game, kept as the map has it.
Weather climateWeather(Weather own) {
    if(own==WEATHER_NUKAGE||own==WEATHER_NONE||Game_Phobos())return own;
    if(explicitTemperature()) {
        int low,high,t=climateNow().temperature;kindTemperatures(own,low,high);
        if(t<low||t>high) {
            if(own==WEATHER_SNOW&&t<=40)own=WEATHER_RAIN;
            else if(own==WEATHER_RAIN&&t<0)own=WEATHER_SNOW;
            else own=WEATHER_NONE;
        }
    }
    return kindWind(own,climateNow().wind)?own:WEATHER_NONE;
}
// The wind's speed (units a second) for the player's choice, or the map's
// own (own); a map's own wind under weather the player picked gives way to a
// breeze where it doesn't fit (a sandstorm in still air, fog in a gale).
float climateWindSpeed(float own) {
    static const float speeds[]={0,0,115,172,230};   // still, breeze, strong, gale
    if(climateNow().wind>WIND_OWN&&climateNow().wind<=WIND_GALE)return speeds[climateNow().wind];
    Weather kind=falling();
    if(climateNow().weather!=1&&kind!=WEATHER_NONE) {
        int band=own<57.5f?WIND_STILL:own<143.75f?WIND_BREEZE:own<201.25f?WIND_STRONG:WIND_GALE;
        if(!kindWind(kind,band))return speeds[WIND_BREEZE];
    }
    return own;
}
// The way the player's wind blows to (map degrees: 0 east, 90 north), from
// the compass point it comes from; false for the map's own.
bool climateWindHeading(float &degrees) {
    if(climateNow().windFrom<1||climateNow().windFrom>8)return false;
    degrees=90-45*(climateNow().windFrom-1)+180.0f;
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
