// The wind as what flies and lies sees it: scene3d/wind.cpp's windAmount,
// windGust and windGustRate and powder.cpp's lift, the same numbers
// repeated here for the far flakes (sky/snow.glsl) and what lies loose on
// the ground (world/wear.glsl, world/settled.glsl): change both sides
// together. Expects the Camera block c.
float windAmountNow() {return clamp(length(c.weather.yz)/230.0,0.0,1.0);}
float windTic() {return floor(c.fog.w*35.0)/35.0;}   // stepping at the tic, as Doom moves
// How far along the wind the gusts have shoved what flies at p, t seconds
// in: waves of it rolling downwind at the wind's speed.
float windGustAt(vec2 p,float t) {
    float a=windAmountNow(),speed=length(c.weather.yz);
    if(a<=0.0||speed<1.0) return 0.0;
    float along=dot(p,c.weather.yz)/speed-speed*t;
    return 56.0*a*a*(0.65*sin(along/220.0)+0.35*sin(along/97.0+1.7));
}
// How much faster than the steady wind the gusts blow at p now.
float windGustRateAt(vec2 p,float t) {
    float speed=length(c.weather.yz);
    return speed<1.0?0.0:clamp((windGustAt(p,t+0.05)-windGustAt(p,t))/0.05/speed,-1.0,1.0);
}
// How hard what lies loose lifts at p now: nothing in a lull or still air.
float windLiftAt(vec2 p,float t) {return clamp(windAmountNow()-0.3+3.0*windGustRateAt(p,t),0.0,1.5);}
