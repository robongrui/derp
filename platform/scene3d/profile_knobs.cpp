/* Feature knobs for the perf test's -features (tests/perf_smoke.c): what each
   part of the world shader (shaders/world.frag) costs per pixel. A knob
   switches one part off in the frames drawn while it is set, by clearing its
   feature flag or zeroing the camera uniform that gates it once the scene has
   built the frame (applyProfileKnob, the last thing prepareFrame does to the
   camera), so the scene's settings and its CPU work stay as they are and the
   GPU time saved is that part's. Dynamic lights and the mirrors have no gate
   in the camera: their knobs use the profiling skip bits instead, which take
   the lights or the mirror pass out on the CPU as well. Nothing here does
   anything unless profiling is on and a knob is set. */
#include "internal.h"
#include "../shaders/feature_flags.h"

namespace doom3d {
namespace {
struct Knob {
    const char *name;
    const char *clears;          // for the report: what it switches off, in the camera's terms
    unsigned flags;              // feature bits cleared (shaders/feature_flags.h)
    unsigned skip;               // profiling skip bits set (I_RENDER3D_SKIP_*, light kinds)
    void (*zero)(Uniforms &);    // uniforms zeroed, or none
    bool together;               // part of "everything off": what changes the texture reads is not
};
// Names in parentheses are the aggregates, last; the very last is every knob
// marked together at once, which leaves the shader's base: texture reads and
// the baked light lookups, which have no gate.
const Knob knobs[]={
    {"(none)","nothing",0,0,nullptr,false},
    // One for each feature bit world.frag reads.
    {"surface detail (bumps, gloss)","FEATURE_SURFACE_DETAIL",FEATURE_SURFACE_DETAIL,0,nullptr,true},
    {"varied highlights","FEATURE_VARIED_HIGHLIGHTS",FEATURE_VARIED_HIGHLIGHTS,0,nullptr,true},
    {"uneven floor tiles","FEATURE_UNEVEN_TILES",FEATURE_UNEVEN_TILES,0,nullptr,true},
    {"uneven walls, brick tone","FEATURE_UNEVEN_WALLS",FEATURE_UNEVEN_WALLS,0,nullptr,true},
    {"texel-aligned bake","FEATURE_TEXEL_BAKE (four bake taps become one)",FEATURE_TEXEL_BAKE,0,nullptr,true},
    {"light flow","FEATURE_LIGHT_FLOW",FEATURE_LIGHT_FLOW,0,nullptr,true},
    {"baked occlusion","FEATURE_BAKED_OCCLUSION",FEATURE_BAKED_OCCLUSION,0,nullptr,true},
    {"window light","FEATURE_WINDOW_LIGHT (the sky lift's path instead)",FEATURE_WINDOW_LIGHT,0,nullptr,true},
    {"glossy screens, stained glass","FEATURE_GLOSSY_SCREENS",FEATURE_GLOSSY_SCREENS,0,nullptr,true},
    {"live readouts","FEATURE_LIVE_READOUTS",FEATURE_LIVE_READOUTS,0,nullptr,true},
    {"caustics (all)","FEATURE_CAUSTICS",FEATURE_CAUSTICS,0,nullptr,true},
    {"caustics on ceilings","FEATURE_CEILING_CAUSTICS",FEATURE_CEILING_CAUSTICS,0,nullptr,true},
    {"caustics: computed layers","FEATURE_CAUSTICS_COMPUTED (one layer instead)",FEATURE_CAUSTICS_COMPUTED,0,nullptr,true},
    {"caustics: shapes grow","FEATURE_CAUSTICS_GROW",FEATURE_CAUSTICS_GROW,0,nullptr,true},
    {"caustics: by angle","FEATURE_CAUSTICS_ANGLE",FEATURE_CAUSTICS_ANGLE,0,nullptr,true},
    {"caustics: sway","FEATURE_CAUSTICS_SWAY",FEATURE_CAUSTICS_SWAY,0,nullptr,true},
    {"caustics on sprites","FEATURE_CAUSTICS_SPRITES",FEATURE_CAUSTICS_SPRITES,0,nullptr,true},
    {"caustics from shots","FEATURE_CAUSTICS_SHOTS",FEATURE_CAUSTICS_SHOTS,0,nullptr,true},
    {"damp shores","FEATURE_DAMP_SHORES",FEATURE_DAMP_SHORES,0,nullptr,true},
    {"glowing liquid edges","FEATURE_GLOW_EDGES",FEATURE_GLOW_EDGES,0,nullptr,true},
    {"window streaks","FEATURE_WINDOW_STREAKS",FEATURE_WINDOW_STREAKS,0,nullptr,true},
    {"stippled shadows","FEATURE_STIPPLED_SHADOWS",FEATURE_STIPPLED_SHADOWS,0,nullptr,true},
    {"classic flashlight","FEATURE_CLASSIC_FLASHLIGHT (the plain beam instead)",FEATURE_CLASSIC_FLASHLIGHT,0,nullptr,true},
    {"retro reflections","FEATURE_RETRO_REFLECTIONS (the smooth mirror instead)",FEATURE_RETRO_REFLECTIONS,0,nullptr,true},
    // The blocks the camera's uniforms gate.
    {"baked lamps","bake.x (lamp light, its tint and bake-only shaping)",0,0,[](Uniforms &c){c.bake[0]=0;},true},
    {"bake directions, flicker","bake.z (direction taps, flicker groups)",0,0,[](Uniforms &c){c.bake[2]=0;},true},
    {"bounce","bake.y, bake.w (bounce added and its tint; its taps stay)",0,0,[](Uniforms &c){c.bake[1]=c.bake[3]=0;},true},
    {"sun","sun.w (the sun in the light rule, its share and glints)",0,0,[](Uniforms &c){c.sun[3]=0;},true},
    {"sky light","fx2.y (the sky in the light rule)",0,0,[](Uniforms &c){c.fx2[1]=0;},true},
    {"seams and contact shading","map.w (seam blending, seam rule, contact creases)",0,0,[](Uniforms &c){c.map[3]=0;},true},
    {"glow masks","materials.x (emissive mask reads)",0,0,[](Uniforms &c){c.materials[0]=0;},true},
    {"dynamic lights (all)","skip all light kinds (CPU too: no light masks)",0,I_RENDER3D_SKIP_LIGHTS,nullptr,true},
    {"glowing-texture lights","skip the surface light kind (CPU too)",0,1u<<I_RENDER3D_LIGHT_SURFACE,nullptr,true},
    {"liquid and glass mirrors","skip reflections (the mirror passes go too)",0,I_RENDER3D_SKIP_REFLECTIONS,nullptr,true},
    {"wear records","right.w (frost, cracks, puddles, splashes, streaks, trails)",0,0,[](Uniforms &c){c.right[3]=0;},true},
    {"wet ground, lying snow","up.w (wetness and what settles under the sky)",0,0,[](Uniforms &c){c.up[3]=0;},true},
    {"height fog","materials.z (fogAlong and its fog lights)",0,0,[](Uniforms &c){c.materials[2]=0;},true},
    {"fog lights","the fog lights' count (FOG_BLOCK)",0,0,[](Uniforms &){fogLights.count=0;},true},
    {"fog bank","bank.w (the bank's 12-step march, streams)",0,0,[](Uniforms &c){c.bank[3]=0;},true},
    {"outdoor haze","haze.w",0,0,[](Uniforms &c){c.haze[3]=0;},true},
    {"whiteout","climate.z",0,0,[](Uniforms &c){c.climate[2]=0;},true},
    {"flashlight beam in the air","beamLight.w, beam.w (beam glow, glitter)",0,0,[](Uniforms &c){c.beamLight[3]=c.beam[3]=0;},true},
    {"open-air march","haze.w, climate.z, beamLight.w (the 8-step march)",0,0,
        [](Uniforms &c){c.haze[3]=c.climate[2]=c.beamLight[3]=0;},true},
    {"far fog","farFog.w",0,0,[](Uniforms &c){c.farFog[3]=0;},true},
    {"tone curve","phobos.w (only outside whole-level lighting)",0,0,[](Uniforms &c){c.phobos[3]=0;},true},
    // The texture reads themselves: the base the rest stands on.
    {"palette mips","texFilter.y (one level read)",0,0,[](Uniforms &c){c.texFilter[1]=0;},false},
    {"crisp texels","effects.x (nearest: one tap, no sharp bilinear)",0,0,[](Uniforms &c){c.effects[0]=0;},false},
    {"(all flags off)","every feature bit",~0u,0,nullptr,false},
    {"(everything off)","every knob above but the texture reads",0,0,nullptr,false},
};
constexpr int knobCount=(int)(sizeof(knobs)/sizeof(knobs[0]));
constexpr int everything=knobCount-1;
int activeKnob=0;
unsigned activeSkip=0;
unsigned knobFlags(int k) {
    if(k!=everything)return knobs[k].flags;
    unsigned flags=0;
    for(const Knob &knob:knobs)if(knob.together)flags|=knob.flags;
    return flags;
}
unsigned knobSkip(int k) {
    if(k!=everything)return knobs[k].skip;
    unsigned skip=0;
    for(const Knob &knob:knobs)if(knob.together)skip|=knob.skip;
    return skip;
}
} // namespace

void applyProfileKnob(Uniforms &camera) {
    if(!profile.enabled||activeKnob<=0)return;
    // The flags travel as a float, exact up to 2^24 (shaders/feature_flags.h).
    unsigned flags=(unsigned)camera.flashlightTint[3]&~knobFlags(activeKnob);
    camera.flashlightTint[3]=(float)flags;
    if(activeKnob==everything) {for(const Knob &knob:knobs)if(knob.together&&knob.zero)knob.zero(camera);}
    else if(knobs[activeKnob].zero)knobs[activeKnob].zero(camera);
}
} // namespace doom3d

int I_Render3DKnobCount(void) {return doom3d::knobCount;}
const char *I_Render3DKnobName(int knob) {return knob>=0&&knob<doom3d::knobCount?doom3d::knobs[knob].name:"?";}
const char *I_Render3DKnobClears(int knob) {return knob>=0&&knob<doom3d::knobCount?doom3d::knobs[knob].clears:"?";}
void I_Render3DProfileKnob(int knob) {
    using namespace doom3d;
    if(knob<0||knob>=knobCount)knob=0;
    // The skip bits are read as the scene is built (skipProfiledLights, the
    // mirrors' setup), so they go in now and count from the next frame on.
    unsigned skip=profile.enabled?knobSkip(knob):0;
    profile.skip=(profile.skip&~activeSkip)|skip;
    activeSkip=skip;activeKnob=knob;
}
