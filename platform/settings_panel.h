/* The F4 settings, in groups (Game, Controls, Graphics, Sound) of tabs and sections,
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
    std::vector<const char*> itemTips; // Choice: a tip per item, shown for the one picked instead of tip (the presets)
    float *number=nullptr;          // Slider
    double low=0,high=1,step=0.05;
    OptionCondition enabled=nullptr;
    bool isEnabled(const Settings &s) const {return !enabled||enabled(s);}
    // Choice: whether a value goes with the other settings as they are; the
    // panel steps past the ones that don't (the climate, climate_rules.h).
    bool (*allows)(const Settings&,int value)=nullptr;
    bool isAllowed(const Settings &s,int value) const {return !allows||allows(s,value);}
    // Choice: a value to show in place of the flag's, when it returns true
    // (Weather's Random: this map's roll; -1 shows "None", any other value not
    // among values "Random").
    bool (*shows)(const Settings&,int &value)=nullptr;
    // Kept in graphics.cfg but left out of the menus (an unfinished feature).
    bool hidden=false;
    // Slider: value moved onto the nearest step within range.
    float snap(double value) const {return (float)std::clamp(low+std::round((value-low)/step)*step,low,high);}
};
struct OptionSection { const char *title; std::vector<Option> options; };
// bindings: the Bindings page (settings_menu.cpp lists the game's actions there, bindings.h), no options.
// hidden: kept in graphics.cfg but not shown (a debugging switch).
struct OptionTab { const char *title; std::vector<OptionSection> sections; bool bindings=false,hidden=false; };
struct OptionGroup { const char *title; std::vector<OptionTab> tabs; };

// The options, bound to the members of s.
std::vector<OptionGroup> optionGroups(Settings &s);
// The groups as a menu shows them: hidden tabs and options left out, and the sections that leaves empty.
void dropHidden(std::vector<OptionGroup> &groups);
// graphics.cfg lines are "key value". readOption stores one in s if the key
// is known and the value valid (a choice's values, a slider's range);
// writeOptions writes them all.
void readOption(Settings &s,const char *key,double value);
// graphics.cfg's first line, "config_version N". A file without the current number is from a
// release whose defaults it no longer fits: readOption skips its lines and the game starts from
// the defaults. Raise configVersion when old files should go again.
constexpr const char *configVersionKey="config_version";
constexpr int configVersion=1;
// Before reading a file named on the command line (-graphics, the tests' and benches' own
// files): its lines count whatever version it has. finishReading puts the check back.
void readAnyVersion();
// After the last readOption: a file from before the combined Combat choices (Deaths, Hit reactions,
// Blood) gets them from its old switches.
void finishReading(Settings &s);
void writeOptions(FILE *file,Settings &s);
// Switches off the effects the game does without (Game_HasEffect), and puts
// back to the map's own any climate choice that clashes with the others.
void fitGame(Settings &s);

// What an option costs each frame as it is set now (settings_costs.cpp), in three steps, and
// where: half the costly ones only cost in some views (a window, a liquid, open sky, a fight).
enum class Cost { None, Low, Some, High };
struct OptionCost { Cost cost=Cost::None; const char *where=nullptr; };
OptionCost optionCost(const Option &option,const Settings &s);

// The presets (settings_presets.cpp): 1 Lowest, 2 Handheld, 3 Balanced, 4 High, 5 Everything.
// applyPreset sets the options a preset covers; matchPreset says which one s is set as, 0 for none (Custom).
void applyPreset(Settings &s,int preset);
int matchPreset(Settings &s);
// F4 Sound's Sounds row, the same way: 1 DOOM's own, 2 Default, 0 Custom.
void applySoundStyle(Settings &s,int style);
int matchSoundStyle(const Settings &s);
// Both rows as the options now come to; and after an option changed, the row picked
// sets what it covers first (the menus' presetFollows).
void matchPresets(Settings &s);
void followPresets(Settings &s,const char *changed);
}
#endif
