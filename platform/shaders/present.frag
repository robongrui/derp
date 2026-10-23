#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"
layout(set=2,binding=0) uniform sampler2D image;
layout(set=2,binding=1) uniform sampler2D bloom;
layout(set=2,binding=2) uniform sampler2D hud;
layout(set=2,binding=3) uniform sampler3D paletteLUT;
layout(std140,set=3,binding=0) uniform HudRect { vec4 hudRect; };
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
void main() {
    vec4 color=textureLod(image,vUV,0.0);
    // Heat haze: hot-air volumes left 1-heat in alpha. Rows of Doom pixels
    // slide sideways by whole Doom pixels at the tic rate, like an old
    // raster wobble; samples outside the hot area (weapon, HUD) are kept.
    float heat=1.0-color.a;
    if(heat>0.01&&c.fx2.x>0.0) {
        vec2 size=vec2(textureSize(image,0));
        float row=floor(vUV.y*size.y/c.fx2.x),tic=floor(c.fog.w*35.0);
        float shift=round(heat*(1.4*sin(row*0.83+tic*0.61)+0.7*sin(row*0.29-tic*0.37)));
        if(shift!=0.0) {
            vec4 moved=textureLod(image,vUV+vec2(shift*c.fx2.x/size.x,0.0),0.0);
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
    if(c.materials.w>0.0) color.rgb=texelFetch(paletteLUT,ivec3(round(saturate(color.rgb)*31.0)),0).rgb;
    outColor=vec4(color.rgb,1.0);
}
