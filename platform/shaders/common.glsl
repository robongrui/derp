// Shared declarations. Sources are Vulkan GLSL; Metal gets MSL translated by
// SPIRV-Cross (scripts/compile_gpu_shaders.cmake).
// SDL_gpu resource sets: vertex 0 resources / 1 uniforms, fragment 2 / 3.
#include "feature_flags.h"
#include "vertex_modes.h"
// Clip space is y up; SDL_gpu flips the Vulkan viewport to match Metal.
// baked: the part of the light already in the bake maps (0: dynamic only).
struct Flash { vec4 position; float strength; uint first,count; float baked; vec4 color,direction; };
struct LightBlocker { vec4 line,opening; };
// right.w: where the wear records start in the sector buffer (0: none);
// forward.w, up.w: how wet the windows and the ground under the sky are.
// mood: x how bright the sky is drawn, yzw its tint (the episode's mood, Game_Mood).
// weather: x how hard it falls (0: dry), yz the wind blowing it (units/s), w which kind
// falls (WEATHER_*, scene3d/internal.h's Weather): with snow, sand or ash the ground
// (up.w) lies in it (settleColor) and the windows (forward.w) frost or dust over.
// weatherTint: rgb the colour the falling kind tints the wet ground, glass drops and far
// sky (nukage rain), w 1 when it does.
// fallColor: rgb the colour of the farthest rain drops, sand grains or ash flakes round
// the eye, which the far sheets in the sky take on (sky.frag), w the fog bank's top as the
// sky judges it (the open air's dry ice near the eye, less the walk's bob) when settleColor.w is 1 (sky.frag).
// settleColor: rgb the albedo of what lies where it settles (snow, sand, ash: wear.glsl,
// glass.frag) and of the haze sand and ash raise over the sky, w 1 when fallColor.w holds
// the dry ice top outside.
// phobos: x 1 when there is no air outside (sky.frag draws space), y how plainly its stars
// show, z how far frost has crept over the helmet's visor (present.frag), w unused.
// bank: the fog bank (WEATHER_FOG, WEATHER_DRY_ICE; scene3d/fog.cpp, fog_bank.glsl) where
// the eye stands: x the bank's top there, y how much of it there is (0-1), z the light
// there, w the bank's density (0: none). bankColor: rgb the fog's colour as lit outdoors,
// w where the per-sector bank entries start in the sector buffer (0: none).
// climate: x the heat of the day (0 under 30 C - 1 at 50 C: glare, bleached sky, sky.frag),
// y the cold frosting clear-weather windows (0 at 0 C - 1 at -20 C, glass.frag), zw unused
// (scene3d/climate.cpp).
// fx: exposure, sky light tint; fx2: render-target pixels per Doom pixel,
// sky light level (0: off), sun yaw, sun disc strength (0: off); texFilter:
// sharp bilinear edge softness in texels, palette mipmaps (0: off);
// flicker: the current light of the baked flicker groups, group 0 steady.
#define WEATHER_RAIN 1.0
#define WEATHER_SNOW 2.0
#define WEATHER_NUKAGE 3.0
#define WEATHER_SAND 4.0
#define WEATHER_ASH 5.0
#define WEATHER_FOG 6.0
#define WEATHER_DRY_ICE 7.0
#define CAMERA_BLOCK { vec4 eye,right,forward,up,projection,effects,materials,flashlightTint,fog,map,water,sun,bake,fx,fx2,texFilter,outdoor,haze,mood,weather,phobos,weatherTint,fallColor,settleColor,bank,bankColor,climate,beam,beamAim,beamLight,farFog,farFogRange; vec4 flicker[4]; vec4 clearing[8]; vec4 rush; vec4 eyes; }
#define FLASH_BLOCK { uint flashCount,flashReserved0,flashReserved1,flashReserved2; Flash lights[63]; }
// count, then four light indices, then padding (see doom3d::FogLights).
#define FOG_BLOCK { uvec4 fogA,fogB; }
// Fog haze: the cool grey under a neutral sky, leaning toward the sky's
// color as it saturates (Inferno's red). skyTint is paled halfway to white.
vec3 fogHaze(vec3 skyTint) {
    vec3 hue=clamp(skyTint*2.0-1.0,0.0,1.0);
    float saturation=max(hue.r,max(hue.g,hue.b))-min(hue.r,min(hue.g,hue.b));
    return mix(vec3(0.045,0.055,0.065),0.07*mix(vec3(1),hue,0.65),saturation);
}

#ifdef VERTEX
#define VARYING out
#else
#define VARYING in
#endif
layout(location=0) VARYING vec2 vUV;
layout(location=1) VARYING float vLight;
layout(location=2) VARYING float vDistance;
layout(location=3) flat VARYING uint vMode;
layout(location=4) VARYING vec3 vWorld;
layout(location=5) flat VARYING vec3 vTint;
layout(location=6) flat VARYING uvec2 vLightMask;
layout(location=7) VARYING vec2 vSun;
// Sprites with bake-only lights: static light direction (xyz) and share (w), unorm bytes.
// Walls: a seed per wall section for their uneven tiles (tiles.glsl).
layout(location=8) flat VARYING uint vStatic;
// Sprites: the sector light's lamp color (lampTint in baked_lighting.h), unorm bytes.
layout(location=9) flat VARYING uint vLampTint;
// A storey's copy (the library's floors above and below, scene3d geometry):
// how far it is lifted from where it is in the map. vWorld is where it is
// drawn; what reads the map by height (the sector's floor and ceiling)
// reads vWorld.z less this.
layout(location=10) flat VARYING float vLift;

float saturate(float x) {return clamp(x,0.0,1.0);}
vec3 saturate(vec3 x) {return clamp(x,vec3(0.0),vec3(1.0));}
// GLSL leaves % undefined for negative operands.
ivec2 wrapTexel(ivec2 p,ivec2 size) {return p-size*ivec2(floor(vec2(p)/vec2(size)));}
// Ordered 4x4 dither threshold in (0,1), for retro fades instead of smooth alpha.
float bayer4(ivec2 p) {
    const float m[16]=float[16](0.0,8.0,2.0,10.0,12.0,4.0,14.0,6.0,3.0,11.0,1.0,9.0,15.0,7.0,13.0,5.0);
    ivec2 q=p&3;
    return (m[q.y*4+q.x]+0.5)/16.0;
}
