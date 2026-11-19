/* What each F4 option costs a frame, for the bars the panel draws beside it
 * (settings_menu.cpp). From the sweep in PLAN-settings-costs.md: median CPU
 * and GPU time over 344 views of E1M1 and E1M2 on the PC, a frame of 3.9 ms
 * of GPU. Low: measurable, under 0.15 ms. Some: 0.15 to 0.4 ms. High: 0.4 ms
 * and up, a tenth of that frame. Options inside the sweep's noise, and those
 * not measured yet (sound, blood and impacts in a fight), have no line here
 * and get no bars.
 *
 * where: the option costs little in most views and its mark only where it
 * applies, so the panel says where. A weather kind marked "open sky" without
 * a measurement is from the Steam Deck, where weather is the heaviest effect;
 * the sweep's maps are indoors. */
#include <cmath>
#include <cstring>
#include "settings_panel.h"

namespace doom3d {
namespace {
const char *const windows="near windows",*const liquids="near liquids",*const sky="under open sky",*const fights="in fights";
// value: a check's 1 (on) or 0 (off: for the options that save time when on), a
// choice's value, a slider's step.
struct CostLine { const char *key; int value; Cost cost; const char *where; };
const CostLine lines[]={
    {"msaa",1,Cost::Some,nullptr},{"msaa",2,Cost::High,nullptr},{"msaa",3,Cost::High,nullptr},
    {"emissive",1,Cost::High,nullptr},
    {"weather",6,Cost::High,nullptr},{"weather",3,Cost::High,nullptr},{"weather",9,Cost::High,nullptr},{"weather",5,Cost::High,nullptr},
    {"weather",8,Cost::Some,nullptr},{"weather",weatherRandom,Cost::High,nullptr},   // Random: as dear as its dearest roll
    {"weather",1,Cost::Some,sky},{"weather",2,Cost::Some,sky},{"weather",4,Cost::Some,sky},{"weather",7,Cost::Some,sky},
    {"explosions",1,Cost::Low,fights},{"explosions",2,Cost::High,fights},{"explosions",3,Cost::Low,fights},   // measured on test blasts: under 2 ms a tic and 2 ms a picture while one lasts
    {"deaths",3,Cost::High,fights},{"deaths",4,Cost::High,fights},
    {"ragdoll_pixels",2,Cost::Some,fights},{"ragdoll_pixels",3,Cost::Some,fights},{"ragdoll_pixels",4,Cost::Some,fights},{"ragdoll_pixels",5,Cost::Some,fights},   // 1.1 ms a new picture against 0.25 (arena, 2026-11-16)
    {"shore_ripples",1,Cost::High,liquids},
    {"reflections",1,Cost::High,liquids},   // Low with Retro reflections (optionCost)
    {"surface_detail",1,Cost::Some,nullptr},
    {"fog",1,Cost::Some,nullptr},
    {"sky_cache",0,Cost::Some,sky},{"sky_cache",1,Cost::Some,sky},{"sky_cache",2,Cost::Some,sky},   // cached is cheaper on the PC, dearer on the Deck
    {"baked_bumps",1,Cost::Some,nullptr},
    {"texel_lighting",1,Cost::Some,nullptr},
    {"bloom",1,Cost::Some,nullptr},
    {"filter",2,Cost::Some,nullptr},{"filter",1,Cost::Low,nullptr},
    {"glass_reflections",1,Cost::Some,windows},
    {"bake_only_lights",0,Cost::High,nullptr},   // off, the lamps are lit live per pixel
    {"baked_lights",0,Cost::Some,nullptr},       // off, light is computed live
    {"light_model",1,Cost::Low,nullptr},
    {"hdr_scene",1,Cost::Low,nullptr},
    {"shadow_volumes",1,Cost::Low,fights},
    {"palette_mipmaps",1,Cost::Low,nullptr},
    {"damp_shores",1,Cost::Low,liquids},
    {"window_light",1,Cost::Low,nullptr},
    {"soft_light",1,Cost::Low,nullptr},
    {"eye_adaptation",1,Cost::Low,nullptr},
    {"window_streaks",1,Cost::Low,windows},
    {"window_glass",1,Cost::Low,windows},
    {"sun_disc",1,Cost::Low,nullptr},
};
}

OptionCost optionCost(const Option &option,const Settings &s) {
    if(!option.isEnabled(s))return {};
    int value=option.kind==OptionKind::Slider?(int)std::lround((*option.number-option.low)/option.step):*option.flag;
    for(const CostLine &line:lines) {
        if(line.value!=value||strcmp(line.key,option.key))continue;
        // The PS1-style mirror is an eighth of the resolution: small by design.
        if(!strcmp(option.key,"reflections")&&s.retroReflections)return {Cost::Low,line.where};
        return {line.cost,line.where};
    }
    return {};
}
}
