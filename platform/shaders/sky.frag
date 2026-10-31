#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"
// Far-plane sky in uncovered pixels; early depth skips fog behind geometry.
// SURFACE: sky ceilings and the open band above lower sky neighbors, drawn
// into depth so the level beyond them stays hidden as in vanilla.
layout(early_fragment_tests) in;
layout(set=2,binding=0) uniform sampler2D image;
layout(set=2,binding=1) uniform sampler2D palette;
layout(std140,set=3,binding=0) uniform Camera CAMERA_BLOCK c;
layout(std140,set=3,binding=1) uniform Lights FLASH_BLOCK;
layout(std140,set=3,binding=2) uniform Fog FOG_BLOCK;
layout(location=0) out vec4 outColor;
#include "indexed.glsl"
// The flashlight beam ends at geometry; through the sky it would gather its
// whole length of fog and seem to light the sky itself.
#define NO_BEAM_FOG
#include "lighting.glsl"
#include "wind.glsl"
#include "fog_bank.glsl"
#include "beam_air.glsl"
// Physically accurate Phobos (c.phobos.x; scene3d/space.cpp): no air, and no painted sky. Black
// space a Doom pixel at a time, anchored to the world: steady stars (no air to twinkle them) that
// come out as the eye settles to the dark (c.phobos.y), Mars, and the sun when it is up.
uint pcgHash(uint v) {uint s=v*747796405u+2891336453u;uint w=((s>>((s>>28u)+4u))^s)*277803737u;return (w>>22u)^w;}
float hash3(ivec3 p) {return float(pcgHash(uint(p.x)+pcgHash(uint(p.y)+pcgHash(uint(p.z)))))/4294967295.0;}
float noise3(vec3 p) {
    ivec3 i=ivec3(floor(p));vec3 f=fract(p);f=f*f*(3.0-2.0*f);
    float a=mix(hash3(i),hash3(i+ivec3(1,0,0)),f.x),b=mix(hash3(i+ivec3(0,1,0)),hash3(i+ivec3(1,1,0)),f.x);
    float d=mix(hash3(i+ivec3(0,0,1)),hash3(i+ivec3(1,0,1)),f.x),e=mix(hash3(i+ivec3(0,1,1)),hash3(i+ivec3(1,1,1)),f.x);
    return mix(mix(a,b,f.y),mix(d,e,f.y),f.z);
}
// Mars: Phobos is locked to it, so it hangs at one place, 42 degrees across, high up. With the sun
// up it stands round from it, a little over half lit; with the sun hidden (the night side, facing
// Mars, Game_Mood) the sun is behind the moon and lights Mars nearly full. A day on Mars is a long time on
// Phobos: its ground stays put. Rust and ochre plains, dark uplands, white caps at its poles, and
// on the day side the faint rim of its thin air. Its night side hides the stars behind it.
const float marsAround=2.6,marsUp=0.70,marsRadius=0.367;
vec3 space(vec3 ray,vec3 sunWay,out bool behindMars) {
    float pixel=160.0*c.projection.x; // Doom pixels per radian
    vec3 cell=floor(ray*pixel),q=normalize((cell+0.5)/pixel);
    vec3 color=vec3(0);
    float yaw=c.fx2.z+marsAround;
    vec3 mars=vec3(cos(yaw)*cos(marsUp),sin(yaw)*cos(marsUp),sin(marsUp));
    float a=acos(clamp(dot(q,mars),-1.0,1.0));
    behindMars=a<marsRadius;
    if(behindMars) {
        float s=sin(a)/sin(marsRadius);
        vec3 across=q-mars*dot(q,mars);across=length(across)>1e-6?normalize(across):vec3(0);
        vec3 n=-mars*sqrt(max(1.0-s*s,0.0))+across*s;
        vec3 light=c.sun.w>0.0?sunWay:normalize(-mars+cross(mars,vec3(0,0,1))*0.35+vec3(0,0,0.15));
        float lit=max(dot(n,light),0.0);
        vec3 p=n*3.0;
        float g=0.55*noise3(p*2.0)+0.3*noise3(p*5.0+7.0)+0.15*noise3(p*11.0+13.0);
        vec3 ground=mix(vec3(0.42,0.23,0.14),vec3(0.80,0.50,0.31),smoothstep(0.32,0.68,g));
        // The caps: small, ragged at the edge, whole pixels.
        float cap=max(n.z,-n.z-0.02)+0.04*(g-0.5);
        ground=cap>0.95?vec3(0.90,0.88,0.86):ground;
        color=ground*lit+vec3(0.9,0.6,0.45)*0.25*smoothstep(0.85,1.0,s)*sqrt(lit);
        return color;
    }
    float h=hash3(ivec3(cell));
    if(h<0.006) {
        float bright=0.25+0.75*pow(hash3(ivec3(cell)+ivec3(17)),3.0);
        vec3 tint=mix(vec3(1.0,0.86,0.72),vec3(0.78,0.86,1.0),hash3(ivec3(cell)+ivec3(29)));
        color=tint*bright*0.7*c.phobos.y;
    }
    return color;
}
#include "sky/sheet.glsl"
#include "sky/rain.glsl"
#include "sky/snow.glsl"
#include "sky/sand.glsl"
#include "sky/ash.glsl"
void main() {
    // A level's far fog (DERPINFO farfog) is all there is behind the world.
    if(c.farFog.w>0.0) {outColor=vec4(c.farFog.rgb,1.0);return;}
    vec2 size=vec2(textureSize(image,0));
#ifdef SURFACE
    vec3 ray=normalize(vWorld-c.eye.xyz);
#else
    vec2 screen=vUV*2.0-1.0;
    vec3 ray=normalize(c.forward.xyz+c.right.xyz*screen.x/c.projection.x-c.up.xyz*screen.y/c.projection.y);
#endif
    // World-anchored cylinder: yaw and height come from the view ray, so the
    // sky no longer shears with pitch or stretches with FOV. Scaled a little
    // toward vanilla proportions; the horizon row moves down with it so the
    // painted top edge stays near 46 degrees up.
    float angle=atan(ray.y,ray.x);
    float rise=ray.z/max(length(ray.xy),1e-4);
    float v=0.70-rise*0.68;
    vec2 uv=vec2(-angle/6.2831853*size.x*4.0,clamp(v,0.001,0.999)*size.y);
    bool filtered=c.effects.x>0.0;
    if(c.phobos.x>0.0) {
        vec3 sunWay=vec3(cos(c.fx2.z)*cos(c.outdoor.w),sin(c.fx2.z)*cos(c.outdoor.w),sin(c.outdoor.w));
        bool behindMars;
        vec3 sky=space(ray,sunWay,behindMars);
        // The sun, when it is up: a small white disc and only the eye's own tight glare round it,
        // no air to glow in.
        if(c.fx2.w>0.0&&!behindMars) {
            float a=acos(clamp(dot(ray,sunWay),-1.0,1.0));
            sky=max(sky,vec3(1.0)*(1.0-smoothstep(0.010,0.013,a)))+c.sun.rgb*exp(-a/0.02)*0.5;
        }
        outColor=vec4(sky*c.fx.x,1.0);
        if(c.water.z==0.0&&c.water.w>0.0) outColor.a=0.35;
        return;
    }
    // Light distance softening: blend in a 2x2 texel box so the sky sits a
    // touch softer than the crisp walls in front of it. Taps stay inside the
    // texture rows so the top and bottom don't wrap into each other.
    vec4 color=indexed(image,palette,uv,filtered,true);
    vec3 soft=vec3(0);
    for(int i=0;i<4;++i) {
        vec2 o=vec2(i&1,i>>1)-0.5;
        soft+=indexed(image,palette,vec2(uv.x+o.x,clamp(uv.y+o.y,0.5,size.y-0.5)),filtered,true).rgb;
    }
    color.rgb=mix(color.rgb,soft/4.0,0.6);
    // The sky's own low-row color, the tint of air at the horizon.
    vec3 low=vec3(0);
    for(int i=0;i<8;++i)
        low+=indexed(image,palette,vec2((float(i)+0.5)*size.x/8.0,size.y*0.92),false,true).rgb;
    low/=8.0;
    // Aerial perspective: contrast and saturation fall toward the horizon,
    // pulled toward a grey-tinted mix of that low color. Restrained overall.
    float air=0.12+0.33*(1.0-smoothstep(0.0,0.6,rise));
    float luma=dot(color.rgb,vec3(0.299,0.587,0.114));
    color.rgb=mix(color.rgb,mix(vec3(luma),low,0.6),air);
    // High-altitude haze in the sky's own upper-row color hides the clamped,
    // stretched top rows. It begins just above the frame top when looking
    // level and is opaque a little past the painted edge.
    float high=smoothstep(0.12,-0.08,v);
    if(high>0.0) {
        vec3 cap=vec3(0);
        for(int i=0;i<8;++i)
            for(int j=0;j<2;++j)
                cap+=indexed(image,palette,vec2((float(i)+0.5)*size.x/8.0,size.y*(0.03+0.08*float(j))),false,true).rgb;
        color.rgb=mix(color.rgb,cap/16.0,high);
    }
    // Past the painted bottom edge, fade into the low color the same way so
    // the clamped bottom row doesn't streak when looking down from a ledge.
    color.rgb=mix(color.rgb,low,smoothstep(0.88,1.02,v));
    // Haze toward the horizon in the sky's own low-row color, so distant
    // silhouettes meet a soft band rather than a hard painted edge.
    if(c.materials.z>0.0&&c.effects.z==0.0)
        color.rgb=mix(color.rgb,low,0.55*(1.0-smoothstep(-0.05,0.3,ray.z)));
    // The sun at the baked yaw and the height the sky paints it (c.outdoor.w;
    // bakeSun in scene3d/bake_sun.cpp, 40 degrees for DOOM): a small
    // disc and a wide glow in its tint. Smooth, like the softened sky around
    // it; a dither here shimmers over a large part of the sky.
    if(c.fx2.w>0.0) {
        vec3 toward=vec3(cos(c.fx2.z)*cos(c.outdoor.w),sin(c.fx2.z)*cos(c.outdoor.w),sin(c.outdoor.w));
        float a=acos(clamp(dot(ray,toward),-1.0,1.0));
        float disc=1.0-smoothstep(0.016,0.024,a),glow=exp(-a*a/0.012)*0.45+exp(-a/0.35)*0.12;
        float amount=saturate(disc*0.75+glow)*c.fx2.w;
        color.rgb=mix(color.rgb,max(color.rgb,c.sun.rgb),saturate(disc*c.fx2.w))+c.sun.rgb*amount*(1.0-disc*0.5);
    }
    // The heat of the day (c.climate.x, scene3d/climate.cpp): the sky bleaches
    // toward a warm white, the most toward the horizon, and the sun's glare
    // spreads wide. Smooth, like the sun's glow.
    if(c.climate.x>0.0) {
        float heat=c.climate.x,luma=dot(color.rgb,vec3(0.299,0.587,0.114));
        float bleach=heat*(0.18+0.32*(1.0-smoothstep(-0.05,0.45,rise)));
        color.rgb=mix(color.rgb,max(color.rgb,(luma*1.3+0.25)*vec3(1.0,0.95,0.86)),bleach);
        if(c.fx2.w>0.0) {
            vec3 toward=vec3(cos(c.fx2.z)*cos(c.outdoor.w),sin(c.fx2.z)*cos(c.outdoor.w),sin(c.outdoor.w));
            float a=acos(clamp(dot(ray,toward),-1.0,1.0));
            color.rgb+=c.sun.rgb*(exp(-a/0.5)*0.3+exp(-a*a/0.03)*0.35)*heat*c.fx2.w;
        }
    }
    // Far weather (sky/*.glsl, a file a kind): its sheets past the map's
    // sky, and the haze sand and ash raise over it. Snow brightens the sky
    // behind its flakes; rain, sand and ash take the colour of the eye's
    // farthest near ones (c.fallColor), laid over the fog and exposure as
    // those are drawn (falling, below), and ash's embers glow over all.
    // Only above the horizon: below it the sky shows only where the map
    // lets the void under it through (a seam, the gap under a wall), and
    // nothing is drawn below the map, no weather, no haze, no tint (a pale
    // or speckled sky behind the cracks showed every seam). Hiding it is
    // simpler than mending every seam. A hairline fade keeps the horizon
    // from stepping. The map's own sky surfaces (SURFACE) are no void: a
    // closed strip of sky past a low wall, seen from a rooftop, lies below the
    // horizon and stood clear of the storm and fog round it (DOOM 2's cities).
#ifdef SURFACE
    float above=1.0;
#else
    float above=smoothstep(-0.02,0.0,rise);
#endif
    float falling=0.0,ember=0.0,hazed=above;
    if(c.weather.x>0.0&&above>0.0) {
        if(c.weather.w==WEATHER_SNOW) {color.rgb=mix(color.rgb,snowHaze(color.rgb,low,rise),hazed);color.rgb=mix(color.rgb,max(color.rgb*1.6,low*1.4+0.12),farSnow(ray,angle,rise)*above);}
        else if(c.weather.w==WEATHER_RAIN||c.weather.w==WEATHER_NUKAGE) falling=farRain(ray,angle,rise);
        else if(c.weather.w==WEATHER_SAND) {color.rgb=mix(color.rgb,sandHaze(color.rgb,low,ray,rise),hazed);falling=farSand(ray,angle,rise);}
        else if(c.weather.w==WEATHER_ASH) {color.rgb=mix(color.rgb,ashHaze(color.rgb,low,ray,rise),hazed);falling=farAsh(ray,angle,rise,ember);}
        falling*=above;ember*=above;
    }
    // Nukage rain (c.weatherTint) hangs in the air: the sky takes a green cast
    // of its colour, as strong as it falls.
    if(c.weatherTint.w>0.0&&c.weather.x>0.0)
        color.rgb=mix(color.rgb,c.weatherTint.rgb*max(dot(color.rgb,vec3(0.3,0.59,0.11))*1.6,0.1),0.35*saturate(c.weather.x*1.25)*above);
    // An episode whose outdoors lie dark (Game_Mood) dims its sky with them,
    // and its hour can tint it.
    color.rgb*=c.mood.x*c.mood.yzw;
    // A blizzard's whiteout (whiteout.glsl) as thick as it gets far off: the
    // walls fade into it with distance, and a sky clearer than the farthest
    // wall stood out dark behind the pale tops of the buildings.
    if(c.climate.z>0.0) color.rgb=mix(color.rgb,c.haze.rgb,c.climate.z*above);
    // The flashlight's beam lighting the air on the way (beam_air.glsl), as
    // much as there is of the open air round the eye: the sky's own surfaces
    // at their place, the rest out to the beam's reach.
    if(c.beamLight.w>0.0) {
#ifdef SURFACE
        float reach=min(length(vWorld-c.eye.xyz),c.beam.w);
#else
        float reach=c.beam.w;
#endif
        float lit=0.0;
        for(int i=0;i<8;++i) lit+=beamAir(c.eye.xyz+ray*reach*(float(i)+0.5)/8.0);
        color.rgb+=beamGlow(reach,lit/8.0*c.climate.w)*above;
    }
    vec4 fog=fogAlong(c.eye.xyz+ray*2000.0);
    color.rgb=(color.rgb*fog.a+fog.rgb+(c.haze.rgb-fogHaze(c.fx.yzw))*(1.0-fog.a)*weatherAir()*above)*c.fx.x;   // the weather's air (lighting.glsl)
    // The fog bank (fog_bank.glsl) hides the sky: fog in a pall overhead in
    // its colour as the open air lights it (c.bankColor), and the bank itself
    // thickening to the horizon, smooth; dry ice only where its layer comes
    // over the eye, in steps, the sky clear above it. Dry ice lying higher
    // outside nearby (c.fallColor.w when c.settleColor.w is set) covers the
    // sky seen low over it, as from a room through a window.
    if(c.bank.w>0.0) {
        float pall=c.weather.w==WEATHER_FOG?0.6*saturate(c.weather.x*1.25):0.0;
        float top=c.settleColor.w>0.0?c.fallColor.w:c.bank.x;   // fog.cpp's fogBankUniforms: with the bob taken out
#ifdef SURFACE
        float far=length(vWorld-c.eye.xyz);
#else
        float far=2000.0;
#endif
        float fogged=1.0-(1.0-pall)*exp(-c.bank.w*far*bankMean(c.eye.z-top,c.eye.z+ray.z*far-top));
        if(c.weather.w==WEATHER_DRY_ICE) fogged=floor(fogged*16.0+0.5)/16.0;
        color.rgb=mix(color.rgb,c.bankColor.rgb*c.fx.x,fogged*above);   // none below the map
    }
    // What falls far off unfogged and unexposed, as the near ones are drawn.
    color.rgb=mix(color.rgb,c.fallColor.rgb,falling);
    color.rgb=mix(color.rgb,emberGlow,ember);
    // Mirror pass (world.frag): the sky is far, but stays a faint reflection.
    if(c.water.z==0.0&&c.water.w>0.0) color.a=0.35;
    outColor=color;
}
