/* The F4 settings, in groups (Game, Graphics, Sound) of tabs and sections,
 * as the in-game panel (settings_menu.cpp) shows them. This table is the one list of
 * options: each also names its graphics.cfg key, so adding an option here
 * (with its Settings member) is all it takes to show, load and save it. */
#ifndef DOOM_SETTINGS_PANEL_H
#define DOOM_SETTINGS_PANEL_H
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>
#include "scene3d.h"

namespace doom3d {
enum class OptionKind { Check, Choice, Slider };
// Whether an option has an effect with the other settings as they are.
using OptionCondition=bool(*)(const Settings&);
struct Option {
    OptionKind kind;
    const char *key,*title,*tip; // key: the graphics.cfg name
    int *flag=nullptr;              // Check and Choice
    std::vector<const char*> items; // Choice: one item per entry in values; Slider: a label per step, if any
    std::vector<int> values;
    float *number=nullptr;          // Slider
    double low=0,high=1,step=0.05;
    OptionCondition enabled=nullptr;
    bool isEnabled(const Settings &s) const {return !enabled||enabled(s);}
    // Choice: whether a value goes with the other settings as they are; the
    // panel steps past the ones that don't (the climate, climate_rules.h).
    bool (*allows)(const Settings&,int value)=nullptr;
    bool isAllowed(const Settings &s,int value) const {return !allows||allows(s,value);}
    // Slider: value moved onto the nearest step within range.
    float snap(double value) const {return (float)std::clamp(low+std::round((value-low)/step)*step,low,high);}
};
struct OptionSection { const char *title; std::vector<Option> options; };
struct OptionTab { const char *title; std::vector<OptionSection> sections; };
struct OptionGroup { const char *title; std::vector<OptionTab> tabs; };

// The options, bound to the members of s.
std::vector<OptionGroup> optionGroups(Settings &s);
// graphics.cfg lines are "key value". readOption stores one in s if the key
// is known and the value valid (a choice's values, a slider's range);
// writeOptions writes them all.
void readOption(Settings &s,const char *key,double value);
// After the last readOption: a file from before the combined Combat choices (Deaths, Hit reactions,
// Blood) gets them from its old switches.
void finishReading(Settings &s);
void writeOptions(FILE *file,Settings &s);
// Switches off the effects the game does without (Game_HasEffect), and puts
// back to the map's own any climate choice that clashes with the others.
void fitGame(Settings &s);
}
#endif
