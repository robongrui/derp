// The fog bank (scene3d/fog.cpp; WEATHER_FOG, WEATHER_DRY_ICE): weather
// that lies instead of falling. Under each sector it is whole up to its top
// and thins above it: fog by half every 16 units, dry ice (scene3d/dry_ice.cpp)
// by half every 4, a layer with a crisp top and clear air over it. Along a
// view ray the height over the bank's top runs straight from the eye's
// (c.bank.x) to the point's (its sector's entry), so its profile integrates
// in closed form, and the share of the bank lying there is the mean of the
// two ends'. What it hides fades toward the fog's colour (c.settleColor) in
// the mean of their light. Dry ice fades in Doom's sixteen light steps,
// hard, undithered; fog smoothly. Doom's fullbright cuts through: what
// glows keeps more. Expects the Camera block c.
float bankFalloff() {return c.weather.w==WEATHER_DRY_ICE?0.173:0.0433;}
// The profile from the top to h above it (negative: below, whole).
float bankRise(float h,float k) {return h<=0.0?h:(1.0-exp(-k*h))/k;}
// The mean of the profile along a straight path from h0 to h1 above the top.
float bankMean(float h0,float h1) {
    float k=bankFalloff();
    if(abs(h1-h0)<0.5) {float h=(h0+h1)*0.5;return h<=0.0?1.0:exp(-k*h);}
    return (bankRise(h1,k)-bankRise(h0,k))/(h1-h0);
}
// The bank's top out under the sky in a wind: two swells travelling downwind
// at a sixth of its speed, a step per tic, one straight along it (144 units crest to crest),
// one across it at a slant (101), 7 units up and down at 120 units a second
// of wind (a third of dry ice's 18-unit layer: less changes no hard step),
// so its steps roll over the yard as bands; fog's softer top heaves less.
// As much as the sector is open to the wind (open: 1 the open yard, less
// in a light well or under a porch's roof, none indoors). Fading
// out from 600 to 1500 units off: the far stretches are long and would
// alias it. Added to an outside sector's top only, never the film or the
// streams indoors.
float bankWave(vec2 p,float open) {
    if(c.weather.w!=WEATHER_DRY_ICE&&c.weather.w!=WEATHER_FOG) return 0.0;
    float wind=length(c.weather.yz);
    if(wind<1.0) return 0.0;
    vec2 along=c.weather.yz/wind,slant=along*0.8+vec2(-along.y,along.x)*0.6;
    float speed=wind/6.0,t=floor(c.fog.w*35.0)/35.0;   // a step per tic, as Doom moves
    float a=sin((dot(p,along)-speed*t)*(6.2832/144.0)),b=sin((dot(p,slant)-0.8*speed*t)*(6.2832/101.0)+1.7);
    float near=1.0-smoothstep(600.0,1500.0,distance(p,c.eye.xy));
    return (0.6*a+0.4*b)*7.0*clamp(wind/120.0,0.0,1.5)*near*open*(c.weather.w==WEATHER_FOG?0.6:1.0);
}
// Holes blown in the bank (scene3d/disturbances.cpp: a blast): c.clearing
// (x, y, radius, how open), strongest first, the rest zero. How much of the
// bank is left at p: none in the middle of an open one, its edge ragged in
// whole 4-unit steps so it is never a circle.
float bankClear(vec2 p) {
    float keep=1.0;
    for(int i=0;i<8;++i) {
        vec4 k=c.clearing[i];
        if(k.z<=0.0) break;
        vec2 d=p-k.xy;
        float ragged=0.85+0.15*sin(floor(d.x/4.0)*1.7+floor(d.y/4.0)*2.3+float(i)*5.0);
        keep*=1.0-k.w*(1.0-smoothstep(0.65,1.0,length(d)/(k.z*ragged)));
    }
    return keep;
}
// How much of what lies behind the bank hides, in steps or not.
float bankFogged(float tau) {
    float fogged=1.0-exp(-tau);
    return c.weather.w==WEATHER_DRY_ICE?floor(fogged*16.0+0.5)/16.0:fogged;
}
// From the eye to end, whose sector's bank tops out at top, with amount of
// it there in light light; cut, how much the point glows (0-1). rgb the fog
// it adds (before exposure), a how much of the point shows through.
// The end's own bank holds only over the ray's last 48 units: a bank keeps
// to its sector. Before that the ray runs through the bank between, its top
// sloping from the eye's toward the end's where that lies lower (down from a
// ledge), never up into a higher one ahead (a doorway's, a window frame's),
// as thick as the thicker of the two and lit by the brighter: a doorway or a
// room holding less, or lying darker under its roof, must not thin or dim
// the yard in front of it, or it shows through the yard's layer like a hole
// drawn over it. The two stretches lie one in front of the other: the near
// one's fog over the far one's.
vec4 bankAlong(vec3 end,float top,float amount,float light,float cut) {
    if(c.bank.w<=0.0) return vec4(0,0,0,1);
    float distance=length(end-c.eye.xyz),near=max(distance-48.0,0.0);
    float split=mix(c.eye.z,end.z,near/max(distance,1e-3)),toward=min(top,c.bank.x),keep=1.0-0.6*cut;
    float between=exp(-c.bank.w*near*max(c.bank.y,amount)*bankMean(c.eye.z-c.bank.x,split-toward)*keep);
    float last=exp(-c.bank.w*(distance-near)*amount*bankMean(split-top,end.z-top)*keep);
    float fogged=bankFogged(-log(max(between*last,1e-6)));
    vec3 lit=c.settleColor.rgb*(max(c.bank.z,light)*(1.0-between)+light*(1.0-last)*between)/max(1.0-between*last,1e-4);
    return vec4(lit*fogged,1.0-fogged);
}
// Seen from where the eye stands only (glass, mist sheets): the point taken
// to lie in the eye's own bank.
vec4 bankNear(vec3 end) {return bankAlong(end,c.bank.x,c.bank.y,c.bank.z,0.0);}
