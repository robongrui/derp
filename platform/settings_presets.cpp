/* The F4 presets (Graphics, Display): the options that cost frame time, set
 * in one go. A preset covers only the options below, picked from the costs in
 * PLAN-settings-costs.md (settings_costs.cpp): what is expensive and can go
 * without the picture falling apart. Taste (filtering, scanlines), the
 * resolution, the weather and blood are never touched, and Emissive textures
 * stays on in all of them: the lamps' light comes from it.
 *
 * Balanced is Settings' own defaults, so a new config is Balanced. Handheld to
 * Everything change nothing a light bake reads, so switching between them
 * keeps the bake cache (bake_cache.cpp's settingsKey); Lowest also drops the
 * fog and the sky's depth, which the bakes read, and bakes each map once more. */
#include <cstring>
#include "settings_panel.h"

namespace doom3d {
namespace {
const int presetCount=5;
struct PresetLine { const char *key; double value[presetCount]; };   // Lowest, Handheld, Balanced, High, Everything
const PresetLine lines[]={
    {"msaa",              {0,0,0,1,2}},   // steps: off, 2x, 4x
    {"surface_detail",    {0,1,1,1,1}},
    {"texel_lighting",    {0,0,1,1,1}},
    {"bloom",             {0,0,1,1,1}},
    {"fog",               {0,1,1,1,1}},
    {"sky_layers",        {0,1,1,1,1}},
    {"reflections",       {0,1,1,1,1}},
    {"retro_reflections", {1,1,1,1,0}},
    {"glass_reflections", {0,0,1,1,1}},
    {"shore_ripples",     {2,2,1,1,1}},   // 2: in the liquid's own shader, at no cost
    {"shadow_volumes",    {0,1,1,1,1}},
    {"deaths",            {1,2,4,4,4}},   // varied; procedural falls; ragdolls, Hollywood
};
// Calls visit(option,line) for each option a preset covers.
template<class Visit> void eachLine(Settings &s,Visit visit) {
    for(OptionGroup &group:optionGroups(s))for(OptionTab &tab:group.tabs)
        for(OptionSection &section:tab.sections)for(Option &option:section.options)
            for(const PresetLine &line:lines)if(!strcmp(line.key,option.key))visit(option,line);
}
}

void applyPreset(Settings &s,int preset) {
    if(preset<1||preset>presetCount)return;
    eachLine(s,[&](Option &option,const PresetLine &line) {
        if(option.kind==OptionKind::Slider)*option.number=option.snap(line.value[preset-1]);
        else *option.flag=(int)line.value[preset-1];
    });
    s.preset=preset;
    fitGame(s);
}

int matchPreset(Settings &s) {
    bool fits[presetCount];
    for(bool &fit:fits)fit=true;
    eachLine(s,[&](Option &option,const PresetLine &line) {
        for(int p=0;p<presetCount;++p)
            fits[p]&=option.kind==OptionKind::Slider?option.snap(*option.number)==option.snap(line.value[p]):*option.flag==(int)line.value[p];
    });
    // The one it was set as, where two come out alike (anti-aliasing on a chip without it).
    if(s.preset>=1&&s.preset<=presetCount&&fits[s.preset-1])return s.preset;
    for(int p=0;p<presetCount;++p)if(fits[p])return p+1;
    return 0;
}
}
