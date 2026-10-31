#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"
layout(set=2,binding=0) uniform sampler2D image;
layout(set=2,binding=1) uniform sampler2D bloom;
layout(set=2,binding=2) uniform sampler2D hud;
layout(set=2,binding=3) uniform sampler3D paletteLUT;
// hudRect: the HUD's place; upscale: x the mode (0 smooth, 1 crisp, 2 sharp
// bilinear), yz screen pixels per render pixel, w a local dev tool's glitch (0 off).
layout(std140,set=3,binding=0) uniform Stretch { vec4 hudRect; vec4 upscale; };
layout(std140,set=3,binding=1) uniform Camera CAMERA_BLOCK c;
layout(location=0) out vec4 outColor;
// Phobos (c.phobos.z): frost creeping in over the helmet's visor from its rim, crystals reaching
// inward unevenly, a Doom pixel at a time; never over the status bar.
float frostHash(vec2 p) {return fract(sin(dot(p,vec2(127.1,311.7)))*43758.5453);}
float frostNoise(vec2 p) {
    vec2 i=floor(p),f=fract(p);f=f*f*(3.0-2.0*f);
    return mix(mix(frostHash(i),frostHash(i+vec2(1,0)),f.x),mix(frostHash(i+vec2(0,1)),frostHash(i+vec2(1,1)),f.x),f.y);
}
vec3 visorFrost(vec3 color) {
    vec2 size=vec2(textureSize(image,0)),cell=floor(vUV*size/max(c.fx2.x,1.0));
    vec2 uv=(cell+0.5)*max(c.fx2.x,1.0)/size;
    vec2 hudUV=(uv-hudRect.xy)/hudRect.zw;
    if(all(greaterThanEqual(hudUV,vec2(0)))&&all(lessThanEqual(hudUV,vec2(1)))&&textureLod(hud,hudUV,0.0).a>0.0) return color;
    vec2 d=min(uv,1.0-uv);
    float rim=min(d.x*1.4,d.y);
    // A band at most a fifth of the way in; thick at the rim, broken into crystals toward its edge.
    float reach=c.phobos.z*0.2;
    if(rim>=reach) return color;
    float depth=1.0-rim/max(reach,1e-3);
    float crystal=0.6*frostNoise(cell*0.33)+0.4*frostNoise(cell*0.9);
    float cover=saturate((depth*1.4-0.4+(crystal-0.5)*0.9)*3.0);
    vec3 ice=vec3(0.72,0.81,0.9)*(0.6+0.4*crystal);
    return mix(color,ice+color*0.3,cover*0.7);
}
// A signal glitch (local dev tools, upscale.w: the step's seed in its whole part, how strong in its
// fraction): bands of Doom-pixel rows torn sideways by whole Doom pixels and wrapped round, some
// with their colour channels slipped apart or swapped, one now and then copied from elsewhere on
// the screen or dropped out black. A new pattern each step; HUD and all, like a real signal.
vec3 glitch(vec3 color) {
    // On DOOM's own 320 x 200 grid, whatever the render size: chunky, as the game's pixels are.
    float seed=floor(upscale.w),amount=fract(upscale.w),pixel=1.0/320.0;
    float row=floor(vUV.y*200.0);
    float band=floor(row/(4.0+floor(frostHash(vec2(seed,1.0))*9.0)));
    if(frostHash(vec2(band,seed))>amount*0.55) return color;
    float shift=round((frostHash(vec2(band,seed+7.0))-0.5)*2.0*amount*30.0);
    vec2 at=vec2(fract(vUV.x+shift*pixel),vUV.y);
    if(frostHash(vec2(band,seed+3.0))<0.15) at.y=fract(at.y+(frostHash(vec2(seed,band+5.0))-0.5)*0.3);
    if(frostHash(vec2(band,seed+17.0))<0.05*amount) return vec3(0.0);
    vec3 moved=textureLod(image,at,0.0).rgb;
    if(frostHash(vec2(band,seed+11.0))<0.4*amount) {
        float slip=(1.0+round(2.0*amount))*pixel;
        moved=vec3(textureLod(image,vec2(fract(at.x+slip),at.y),0.0).r,moved.g,textureLod(image,vec2(fract(at.x-slip),at.y),0.0).b);
    }
    if(frostHash(vec2(band,seed+13.0))<0.15*amount) moved=moved.gbr;
    return moved;
}
#include "overlay_cover.glsl"
#include "rush.glsl"
#include "eyes.glsl"
// Sharp bilinear: each render pixel stays flat, and the linear sampler blends
// only across the screen pixel straddling the step to the next.
vec2 sharpUV(vec2 uv) {
    vec2 size=vec2(textureSize(image,0)),texel=uv*size,pixels=max(upscale.yz,vec2(1.0));
    vec2 offset=fract(texel)-0.5,hold=0.5-0.5/pixels;
    return (floor(texel)+0.5+(offset-clamp(offset,-hold,hold))*pixels)/size;
}
void main() {
    vec2 uv=upscale.x==2.0?sharpUV(vUV):vUV;
    vec4 color=textureLod(image,uv,0.0);
    // Heat haze: hot-air volumes left 1-heat in alpha. Rows of Doom pixels
    // slide sideways by whole Doom pixels at the tic rate, like an old
    // raster wobble; samples outside the hot area (weapon, HUD) are kept.
    float heat=1.0-color.a;
    if(heat>0.01&&c.fx2.x>0.0) {
        vec2 size=vec2(textureSize(image,0));
        float row=floor(vUV.y*size.y/c.fx2.x),tic=floor(c.fog.w*35.0);
        float shift=round(heat*(1.4*sin(row*0.83+tic*0.61)+0.7*sin(row*0.29-tic*0.37)));
        if(shift!=0.0) {
            vec4 moved=textureLod(image,uv+vec2(shift*c.fx2.x/size.x,0.0),0.0);
            if(moved.a<0.999) color.rgb=moved.rgb;
        }
    }
    if(c.materials.y>0.0) {
        vec2 uv=(vUV-hudRect.xy)/hudRect.zw;
        float coverage=all(greaterThanEqual(uv,vec2(0)))&&all(lessThanEqual(uv,vec2(1)))?textureLod(hud,uv,0.0).a:0.0;
        // Screen-like additive glow retains detail without clipping to white.
        vec3 glow=textureLod(bloom,vUV,0.0).rgb*(1.0-coverage)*0.8;
        color.rgb=1.0-(1.0-color.rgb)*exp(-glow);
    }
    if(c.phobos.z>0.0) color.rgb=visorFrost(color.rgb);
    if(c.rush.w>0.0) color.rgb=rush(color.rgb,uv);
    if(c.eyes.x>0.0||c.eyes.y>0.0) color.rgb=closedEyes(color.rgb);
    if(upscale.w>0.0) color.rgb=glitch(color.rgb);
    if(c.materials.w>0.0) color.rgb=texelFetch(paletteLUT,ivec3(round(saturate(color.rgb)*31.0)),0).rgb;
    outColor=vec4(color.rgb,1.0);
}
