#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"
layout(set=2,binding=0) uniform sampler2D image;
layout(set=2,binding=1) uniform sampler2D bloom;
layout(set=2,binding=2) uniform sampler2D hud;
layout(set=2,binding=3) uniform sampler3D paletteLUT;
// The panels over the picture (gpu3d/frame.cpp): the settings panel or a book's page, F4's
// readout, a local dev tool's overlay.
layout(set=2,binding=4) uniform sampler2D panelImage;
layout(set=2,binding=5) uniform sampler2D statsImage;
layout(set=2,binding=6) uniform sampler2D devImage;
// The status bar's sides (scene3d/status_bar.cpp): DOOM's border flat (repeating) and the wide STBAR.
layout(set=2,binding=7) uniform sampler2D barFlat;
layout(set=2,binding=8) uniform sampler2D barWide;
// Authored's slow-motion hourglass beside the bar (scene3d/hourglass.cpp).
layout(set=2,binding=9) uniform sampler2D barGlassImage;
// hudRect: the HUD's place; upscale: x the mode (0 smooth, 1 crisp, 2 sharp
// bilinear), yz screen pixels per render pixel, w a local dev tool's glitch (0 off);
// hdr: the display's encoding and headroom (hdr_output.glsl); panelRect, statsRect, devRect:
// where the panels lie in screen uv (left, top, width, height; no width: not shown); tint: a
// palette flash (damage, pickup, radiation suit) as colour and amount, over the whole picture;
// slow.x: how far into slow motion (slow_motion.c), for its colour pop. bar, barSpan, barSide: the
// status bar drawn apart from the HUD's frame (scene3d/status_bar.cpp; overlay_cover.glsl); barGlass:
// the hourglass's place and size in DOOM pixels from the bar's top left (z 0: none).
layout(std140,set=3,binding=0) uniform Stretch { vec4 hudRect; vec4 upscale; vec4 hdr; vec4 panelRect; vec4 statsRect; vec4 devRect; vec4 tint; vec4 slow; vec4 bar; vec4 barSpan; vec4 barSide; vec4 barGlass; };
layout(std140,set=3,binding=1) uniform Camera CAMERA_BLOCK c;
layout(location=0) out vec4 outColor;
layout(location=11) flat in float vBloomKept; // screen.vert: what the view's overall glow leaves of the bloom.
layout(location=12) flat in float vHdrRoom;   // screen.vert: how much of the headroom the view's overall glow leaves.
#include "hdr_output.glsl"
#include "overlay_cover.glsl"
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
    if(overlayCovers(uv)) return color;
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
    vec4 layer=overlayColor(at);moved=mix(moved,layer.rgb,layer.a);   // the 2D layer torn with it
    if(frostHash(vec2(band,seed+11.0))<0.4*amount) {
        float slip=(1.0+round(2.0*amount))*pixel;
        moved=vec3(textureLod(image,vec2(fract(at.x+slip),at.y),0.0).r,moved.g,textureLod(image,vec2(fract(at.x-slip),at.y),0.0).b);
    }
    if(frostHash(vec2(band,seed+13.0))<0.15*amount) moved=moved.gbr;
    return moved;
}
#include "rush.glsl"
#include "eyes.glsl"
// Sharp bilinear: each render pixel stays flat, and the linear sampler blends
// only across the screen pixel straddling the step to the next.
vec2 sharpUV(vec2 uv) {
    vec2 size=vec2(textureSize(image,0)),texel=uv*size,pixels=max(upscale.yz,vec2(1.0));
    vec2 offset=fract(texel)-0.5,hold=0.5-0.5/pixels;
    return (floor(texel)+0.5+(offset-clamp(offset,-hold,hold))*pixels)/size;
}
// A panel at rect (screen uv), nearest; clamped: beyond it its border texels (the settings
// panel's dimming), else nothing.
vec4 panelOver(sampler2D image,vec4 rect,bool clamped) {
    vec2 at=(vUV-rect.xy)/rect.zw;
    if(!clamped&&(any(lessThan(at,vec2(0)))||any(greaterThanEqual(at,vec2(1))))) return vec4(0);
    return textureLod(image,at,0.0);
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
    // The 2D layer (the status bar, messages, menus, the automap) over the view, at the screen's own
    // resolution whatever the render's; nothing under it shimmers (the heat haze above).
    vec4 layer=overlayColor(vUV);
    color.rgb=mix(color.rgb,layer.rgb,layer.a);
    // HDR: what the glow would add past white, kept for the headroom (the picture itself stays
    // as in SDR); small sources get it, a view mostly aglow does not (vHdrRoom).
    vec3 above=vec3(0);
    // HDR in the scene: the view itself kept what is past white (a float target, toneCurve);
    // below white it goes on as SDR draws it.
    if(hdr.x>0.5) {above=max(color.rgb-1.0,0.0)*hdr.w*vHdrRoom*(1.0-layer.a);color.rgb=min(color.rgb,vec3(1.0));}
    if(c.materials.y>0.0) {
        float coverage=layer.a;
        // Screen-like additive glow retains detail without clipping to white.
        vec3 glow=textureLod(bloom,vUV,0.0).rgb*(1.0-coverage)*0.8;
        if(hdr.x>0.5) above+=max(color.rgb+glow*vBloomKept-1.0,0.0)*hdr.w*vHdrRoom;
        color.rgb=1.0-(1.0-color.rgb)*exp(-glow*vBloomKept);
    }
    // What darkens the picture after this (frost, the fall, closed eyes) darkens the light over white as much.
    float before=dot(color.rgb,vec3(0.299,0.587,0.114));
    if(c.phobos.z>0.0) color.rgb=visorFrost(color.rgb);
    if(c.rush.w>0.0) color.rgb=rush(color.rgb,uv);
    if(c.eyes.x>0.0||c.eyes.y>0.0) color.rgb=closedEyes(color.rgb);
    if(upscale.w>0.0) color.rgb=glitch(color.rgb);
    // Slow motion: the colours pop a little (sepia was too much, the user) and the corners
    // darken, so a cut between full speed and slow reads at a glance.
    if(slow.x>0.0) {
        float l=dot(color.rgb,vec3(0.299,0.587,0.114));
        color.rgb=max(vec3(l)+(color.rgb-l)*(1.0+0.3*slow.x),0.0);
        vec2 d=vUV-0.5;
        color.rgb*=1.0-0.3*slow.x*smoothstep(0.08,0.32,dot(d,d));
    }
    // As Doom's flash palettes blend every colour toward one, on the picture rather than the textures.
    if(tint.w>0.0) color.rgb=mix(color.rgb,tint.rgb,tint.w);
    if(c.materials.w>0.0) {color.rgb=texelFetch(paletteLUT,ivec3(round(saturate(color.rgb)*31.0)),0).rgb;above=vec3(0);}
    above*=saturate((dot(color.rgb,vec3(0.299,0.587,0.114))+0.01)/(before+0.01));
    // The panels, blended here as SDR blends them, before the encoding: over an HDR swapchain
    // the settings panel's dimming came out a grey veil. What they cover loses its light over white.
    if(devRect.z>0.0) {vec4 o=panelOver(devImage,devRect,false);color.rgb=mix(color.rgb,o.rgb,o.a);above*=1.0-o.a;}
    if(statsRect.z>0.0) {vec4 o=panelOver(statsImage,statsRect,false);color.rgb=mix(color.rgb,o.rgb,o.a);above*=1.0-o.a;}
    if(panelRect.z>0.0) {vec4 o=panelOver(panelImage,panelRect,true);color.rgb=mix(color.rgb,o.rgb,o.a);above*=1.0-o.a;}
    outColor=vec4(encodeOutput(color.rgb,above,hdr),1.0);
}
