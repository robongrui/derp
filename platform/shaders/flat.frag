#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"
// Debris: flat colored billboards; tint is the lit color, light the opacity.
// SOLID: the crosshair. HEAT: hot-air volumes lower the target's alpha to
// 1-heat (alpha-only writes, MIN blending); present.frag shimmers there.
#include "height_fog.glsl"
layout(location=0) out vec4 outColor;
#if !defined(SOLID)&&!defined(HEAT)
layout(std140,set=3,binding=0) uniform Camera CAMERA_BLOCK c;   // (the world's still bound; the bloom pass pushes its own: drawBloom)
#include "sky/sheet_cells.glsl"
#include "lamp_sheet.glsl"
#endif
void main() {
#ifdef SOLID
    outColor=vec4(0.85,1,0.85,0.8);
#elif defined(HEAT)
    outColor=vec4(0,0,0,1.0-saturate(vLight));
#else
    // The rain round a lamp (lamp_sheet.glsl), added over the scene.
    if((vMode&MODE_LAMP_SHEET)!=0u) {outColor=vec4(lampSheet(),1.0);return;}
    // Out under the sky the far walls meet the horizon's colour (world.frag's outdoor haze); particles out
    // there go the same way, or far off they stood dark against the haze, black specks in Doom 2's bright
    // first map (the user). (Without the yard's enclosure: a little more haze in a closed courtyard.)
    vec3 color=vTint;
    if((vMode&MODE_PARTICLE_OUTSIDE)!=0u&&c.haze.w>0.0) {
        float way=length(vWorld-c.eye.xyz);
        color=mix(color,c.haze.rgb*c.fx.x,c.haze.w*0.6*(1.0-exp(-max(way-256.0,0.0)/1400.0)));
    }
    // The fog between the eye and the particle, and the eye's exposure, as on the surfaces
    // behind it (world.frag): the fog's own grey, without the lights' glow in it. In a dark
    // room the fog greys the walls, and the motes stood before them pure black, unfogged.
    if(c.effects.z==0.0) {
        float way=length(vWorld-c.eye.xyz);
        if(c.materials.z>0.0&&way>0.01) {
            float through=exp(-heightFogTau(c.eye.z,vWorld.z,way,c.fog.x,c.fog.y,c.fog.z));
            color=color*through+fogHaze(c.fx.yzw)*(1.0-through);
        }
        // The weather is lit by daylight: at its scale (c.phobos.w), as the sky it falls through is,
        // then over the same tone curve (sky.frag). Exposed but left at the level's light, rain went a
        // dull grey against the sky when the eye closed down for the daylight (the user).
        if((vMode&MODE_PARTICLE_DAYLIT)!=0u) color=toneCurve(color*c.fx.x*max(c.phobos.w,1.0),c.phobos.w,c.shadowSkip.z>0.0);
        else color*=c.fx.x;    }
    outColor=vec4(color,vLight);
#endif
}
