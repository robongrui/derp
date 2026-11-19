// What the clouds are doing today: the sky's profile says what it is, this says
// how the weather, how much of it, the hour and the wind have it. c.skyState: x
// how much weather (steady: gusts don't change the sky), y how far into night,
// z how much of the sun the mountains leave (sky_sun_block.cpp).
// Clear weather leaves the sky as its profile has it.
// The real cloud for each: rain falls from stratus and nimbostratus, low, dark
// and closing with the amount; snow from a pale flat sky; ash under a black
// ceiling; sand and fog veil what's up there. Dry ice lies under a clear sky.
vec3 fieldRamp(float x);
// How dark the painting already is, 0 pale to 1 near black: darkened again by
// weather or night, it went flat black with no cloud left to see. A sky of fire
// keeps its own rules.
float skyDarkness() {
    vec3 m=fieldRamp(0.5);
    return m.r>3.0*m.b?0.0:saturate(1.0-dot(m,vec3(0.299,0.587,0.114))*1.8);
}
// Light rain with the sun out (weather.cpp's sunshower): broken cloud, thin,
// its own colours, the light coming through.
bool sunShower() {return c.skyState.w>0.0;}
bool weatherClouds() {
    float kind=c.weather.w;
    return kind>0.0&&kind!=WEATHER_DRY_ICE;
}
// How much cloud the weather adds to what the sky has.
float weatherCover() {
    float kind=c.weather.w,a=c.skyState.x;
    if(!weatherClouds())return 0.0;
    if(sunShower())return 0.05;
    if(kind==WEATHER_RAIN||kind==WEATHER_NUKAGE)return 0.12+0.5*a;
    if(kind==WEATHER_SNOW)return 0.15+0.45*a;
    if(kind==WEATHER_FIRESTORM)return 0.2+0.3*a;
    if(kind==WEATHER_ASH)return 0.3+0.6*a;
    if(kind==WEATHER_SAND)return 0.05+0.25*a;
    return 0.3+0.4*a;   // fog
}
// How far it closes the gaps: none in a shower, a lid in a downpour.
float weatherLid() {return weatherClouds()&&c.weather.w!=WEATHER_SAND&&c.weather.w!=WEATHER_FIRESTORM?saturate((c.skyState.x-0.4)*1.5):0.0;}
// How much thicker it makes the cloud.
float weatherThick() {
    float kind=c.weather.w,a=c.skyState.x;
    if(!weatherClouds())return 1.0;
    if(sunShower())return 1.05;
    if(kind==WEATHER_RAIN||kind==WEATHER_NUKAGE)return 1.0+1.4*a;
    if(kind==WEATHER_FIRESTORM)return 1.3+0.5*a;
    if(kind==WEATHER_ASH)return 1.4+1.6*a;
    if(kind==WEATHER_SAND)return 0.7;
    return 1.0+0.8*a;
}
// The tone it pulls cloud toward on the painting's ramp, and how far.
vec2 weatherLook() {
    float kind=c.weather.w,a=c.skyState.x;
    if(sunShower())return vec2(0.45,0.1);
    if(kind==WEATHER_RAIN||kind==WEATHER_NUKAGE)return vec2(mix(0.3,0.08,a),0.35+0.55*a);
    if(kind==WEATHER_SNOW)return vec2(mix(0.6,0.72,a),0.4+0.45*a);
    if(kind==WEATHER_FIRESTORM)return vec2(0.22,0.45);   // smoke, and what lights it comes from below (cityUnderglow)
    if(kind==WEATHER_ASH)return vec2(0.05,0.7+0.25*a);
    if(kind==WEATHER_SAND)return vec2(0.55,0.5+0.4*a);
    return vec2(0.55,0.85);   // fog
}
vec2 volumeWeather() {
    if(!weatherClouds())return vec2(0.3,overcastDeck());
    float dark=skyDarkness();
    vec2 look=weatherLook();
    return vec2(look.x+0.25*dark,look.y*(1.0-0.45*dark));
}
// How much light gets through, past what the painting's own colours can say: a
// pale painting has no dark for a downpour to reach.
float weatherShadeOf() {
    float kind=c.weather.w,a=c.skyState.x;
    if(sunShower())return 0.96;
    if(kind==WEATHER_RAIN||kind==WEATHER_NUKAGE)return mix(0.8,0.36,a);
    if(kind==WEATHER_SNOW)return mix(0.95,0.8,a);
    if(kind==WEATHER_FIRESTORM)return 0.92;   // a fire's sky is lit by the fire
    if(kind==WEATHER_ASH)return mix(0.7,0.35,a);
    return kind==WEATHER_SAND?0.9:1.0;
}
float weatherShade() {return weatherClouds()?mix(weatherShadeOf(),1.0,0.8*skyDarkness()):1.0;}
// How far the deck breaks up: all the way in fair weather, less as the weather
// closes it, none under a lid. What the sun gets through.
float sunBreaks() {
    if(!weatherClouds())return 1.0-overcastDeck();
    if(sunShower())return 1.0;
    return (1.0-weatherLid())*(0.75-0.45*c.skyState.x);
}
// Low torn cloud under rain and snow, however clear the sky's own profile.
float weatherScud() {
    float kind=c.weather.w,a=c.skyState.x;
    if(sunShower())return 0.12;
    if(kind==WEATHER_RAIN||kind==WEATHER_NUKAGE)return 0.3+0.3*a;
    return kind==WEATHER_SNOW?0.2+0.2*a:0.0;
}
// How hard the wind draws cloud out along it and tears its edges: nothing up to
// a stiff breeze, all of it in a gale.
float windTear() {return saturate((c.skyWind.z-0.6)*2.5);}
// Sand and ash colour what's up there, and nukage rain falls from clouds of
// its own: the sickly green of what's in them, glowing where they're thin.
vec3 weatherTinted(vec3 cloud) {
    float kind=c.weather.w;
    if(kind==WEATHER_NUKAGE&&c.weatherTint.w>0.0) {
        float light=dot(cloud,vec3(0.299,0.587,0.114));
        return mix(cloud,c.weatherTint.rgb*(0.3+1.5*light),0.85);
    }
    if((kind!=WEATHER_SAND&&kind!=WEATHER_ASH)||c.fallColor.w<=0.0)return cloud;
    return mix(cloud,c.fallColor.rgb*(0.4+dot(cloud,vec3(0.299,0.587,0.114))),0.6);
}
// A low sun on what stands in the air: how strongly, what the weather lets by.
float lowLight() {return lowSun()*saturate(c.sun.w*1.5)*(1.0-0.9*volumeWeather().y);}
