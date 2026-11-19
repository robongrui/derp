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

// F4 Sound's Sounds row: DOOM's own turns off every made-up sound and every change to how sounds
// carry; Default puts them back as a new config has them. Music, the output and the volumes are
// never touched, nor random pitch (earliest DOOM had it: taste).
namespace {
struct SoundLine { int Settings::*field; int value[2]; };   // DOOM's own, Default
const SoundLine soundLines[]={
    {&Settings::madeImpacts,   {0,1}},
    {&Settings::footsteps,     {0,1}},
    {&Settings::monsterVoices, {0,1}},
    {&Settings::ambience,      {0,0}},
    {&Settings::roomAcoustics, {0,0}},
    {&Settings::farRooms,      {0,0}},
    {&Settings::muffleWalls,   {0,1}},
    {&Settings::muffleDistance,{0,1}},
    {&Settings::naturalFalloff,{0,1}},
    {&Settings::sfxResampling, {0,0}},
};
void setSounds(Settings &s,int style) {for(const SoundLine &line:soundLines)s.*line.field=line.value[style-1];}
}
void applySoundStyle(Settings &s,int style) {
    if(style<1||style>2)return;
    setSounds(s,style);
    s.soundStyle=style;
    fitGame(s);
}
// Against what the style comes to in this game (fitGame: no natural falloff in Heretic).
int matchSoundStyle(const Settings &s) {
    for(int style=1;style<=2;++style) {
        Settings t=s;setSounds(t,style);fitGame(t);
        bool fits=true;
        for(const SoundLine &line:soundLines)fits&=s.*line.field==t.*line.field;
        if(fits)return style;
    }
    return 0;
}
void matchPresets(Settings &s) {s.preset=matchPreset(s);s.soundStyle=matchSoundStyle(s);}
void followPresets(Settings &s,const char *changed) {
    if(!strcmp(changed,"preset"))applyPreset(s,s.preset);
    else if(!strcmp(changed,"sound_style"))applySoundStyle(s,s.soundStyle);
    matchPresets(s);
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
