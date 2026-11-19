/* The Settings page (settings_page.h). It links F4's table (settings_panel.cpp,
 * settings_costs.cpp, settings_presets.cpp) and answers the few questions
 * that table asks of the game and the renderer, since the launcher links
 * neither: which effects the game has (the games table), which soundtrack
 * packs are there (the launcher's search), whether the display takes HDR
 * (SDL), and how many MSAA samples the card offers, which only the game can
 * know: the launcher offers 8x and the game lowers it at start, as now. */
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <SDL3/SDL.h>
#include "../settings_panel.h"
#include "../game_profile.h"
extern "C" {
#include "../audio/sound_options.h"
#include "settings_page.h"
#include "games.h"
#include "launcher_font.h"
}

using namespace doom3d;

int SettingsPage_ReadOnly;

namespace {
unsigned effects=~0u;      // the shown game's Game_HasEffect answers
int soundtrackFound[5]={1};

// The handful that matter before starting, shown first (fullscreen is the launcher's own).
const char *displayKeys[]={"preset","accelerated","fullscreen","window_size","scale","upscale","presentation","frame_limit","hdr"};

struct Page {
    int program=-1;
    Settings draft,defaults;
    std::vector<OptionGroup> groups;
    bool dirty=false,all=false,focusAll=false;
    ui_list list{};
    Uint64 resetAsked=0;
} page;

Option *findOption(std::vector<OptionGroup> &groups,const char *key) {
    for(OptionGroup &group:groups)for(OptionTab &tab:group.tabs)for(OptionSection &section:tab.sections)
        for(Option &option:section.options)if(!strcmp(option.key,key))return &option;
    return nullptr;
}

bool readFile(const char *path,Settings &s) {
    FILE *file=fopen(path,"r");
    if(!file)return false;
    char key[64];float value;
    while(fscanf(file,"%63s %f",key,&value)==2)readOption(s,key,value);
    finishReading(s);
    fclose(file);
    return true;
}

std::string configPath(int program) {
    char *folder=SettingsPage_Folder(program);
    std::string path=folder?std::string(folder)+"graphics.cfg":std::string();
    SDL_free(folder);
    return path;
}

bool writeFile(int program,Settings &s) {
    if(SettingsPage_ReadOnly)return false;
    std::string path=configPath(program);
    if(path.empty())return false;
    FILE *file=fopen(path.c_str(),"w");
    if(!file)return false;
    writeOptions(file,s);
    return fclose(file)==0;
}

unsigned effectsOf(int program) {
    const launcher_program *row=Games_Program(program);
    return row?row->effects:~0u;
}

// The game the table answers for: its effects and the soundtracks its games
// take at all (games.c). program -1: every effect and every soundtrack.
void useGame(int program) {
    effects=program<0?~0u:effectsOf(program);
    unsigned packs=program<0?~0u:Games_ProgramPacks(program);
    soundtrackFound[0]=1;
    soundtrackFound[1]=(packs&GAME_PACK(PACK_PSX))!=0;
    soundtrackFound[2]=(packs&GAME_PACK(PACK_3DO))!=0;
    soundtrackFound[3]=soundtrackFound[4]=(packs&GAME_PACK(PACK_EXTRAS))!=0;
}

// A game lacks an option where the option does something with every effect
// and soundtrack there and nothing with that game's: with the settings as
// they are, or as they come (free aim is off with free look off anywhere,
// but Heretic has none at all).
bool gameLacks(const Option &option,int program,const Settings &draft) {
    Settings defaults;
    bool lacks=false;
    for(const Settings *s:{&draft,(const Settings*)&defaults}) {
        useGame(-1);bool anywhere=option.isEnabled(*s);
        useGame(program);bool here=option.isEnabled(*s);
        lacks|=anywhere&&!here;
    }
    return lacks;
}

std::string valueText(const Option &option) {
    if(option.kind==OptionKind::Check)return *option.flag?"On":"Off";
    if(option.kind==OptionKind::Choice) {
        for(size_t i=0;i<option.values.size();++i)if(option.values[i]==*option.flag)return option.items[i];
        return option.items.front();
    }
    size_t step=(size_t)std::lround((*option.number-option.low)/option.step);
    if(step<option.items.size())return option.items[step];
    char value[16];
    snprintf(value,sizeof(value),option.step>=1?"%.0f":"%.2f",*option.number);
    return value;
}

const char *tipOf(const Option &option) {
    if(option.kind==OptionKind::Choice&&option.itemTips.size()==option.values.size())
        for(size_t i=0;i<option.values.size();++i)if(option.values[i]==*option.flag)return option.itemTips[i];
    return option.tip?option.tip:"";
}

// As F4 does it (settings_menu.cpp): the preset picked sets what it covers,
// any other change leaves the preset showing what the options come to.
void presetFollows(const Option &option) {
    followPresets(page.draft,option.key);
    page.dirty=true;
}

void stepChoice(Option &option,int direction) {
    int count=(int)option.values.size(),index=0;
    for(int i=0;i<count;++i)if(option.values[(size_t)i]==*option.flag)index=i;
    for(int step=1;step<count;++step) {
        int value=option.values[(size_t)(((index+direction*step)%count+count)%count)];
        if(option.isAllowed(page.draft,value)) {*option.flag=value;break;}
    }
}

// Removes a folder and what is in it.
void removeTree(const std::string &folder) {
    std::vector<std::string> names;
    SDL_EnumerateDirectory(folder.c_str(),[](void *data,const char *dir,const char *name) {
        ((std::vector<std::string>*)data)->push_back(std::string(dir)+name);
        return SDL_ENUM_CONTINUE;
    },&names);
    for(const std::string &name:names) {
        SDL_PathInfo info;
        if(SDL_GetPathInfo(name.c_str(),&info)&&info.type==SDL_PATHTYPE_DIRECTORY)removeTree(name+"/");
        else SDL_RemovePath(name.c_str());
    }
    SDL_RemovePath(folder.c_str());
}

void reset(launcher_state *state) {
    if(SettingsPage_ReadOnly)return;
    char *folder=SettingsPage_Folder(page.program);
    if(!folder)return;
    std::string base=folder;
    SDL_free(folder);
    SDL_RemovePath((base+"graphics.cfg").c_str());
    removeTree(base+"bake-cache/");
    int program=page.program;
    page.program=-1;
    SettingsPage_Select(program);
    Launcher_Status(state,0,"%s's settings are back to their defaults; its bake cache is gone.",Games_Program(program)->title);
}

// To the other games: every option both games have. One either game lacks
// (Heretic: window glass, the soundtrack packs, free aim...) stays as it was there.
void copyToAll(launcher_state *state) {
    SettingsPage_Save();
    int copied=0;
    for(int target=0;target<Games_ProgramCount();++target) {
        if(target==page.program)continue;
        Settings other;
        useGame(target);
        readFile(configPath(target).c_str(),other);
        std::vector<OptionGroup> theirs=optionGroups(other);
        for(OptionGroup &group:page.groups)for(OptionTab &tab:group.tabs)for(OptionSection &section:tab.sections)
            for(Option &option:section.options) {
                Option *to=findOption(theirs,option.key);
                if(!to||gameLacks(option,page.program,page.draft)||gameLacks(option,target,page.draft))continue;
                if(option.kind==OptionKind::Slider)*to->number=*option.number;
                else *to->flag=*option.flag;
            }
        useGame(target);
        fitGame(other);
        if(writeFile(target,other))++copied;
    }
    useGame(page.program);
    Launcher_Status(state,0,copied?"Copied %s's settings to the other games.":"There was no other game to copy to.",
                    Games_Program(page.program)->title);
}

const float rowHeight=34,rowGap=4;

struct Drawer {
    ui_state *ui;launcher_state *state;
    SDL_FRect view;float y;float valueX;
    void heading(const char *text,int scale) {
        y+=scale>=2?14:6;
        if(y>view.y-40&&y<view.y+view.h)Font_Draw(ui->renderer,view.x+4,y,text,scale,scale>=2?UI_ACCENT_COLOR:UI_DIM);
        y+=FONT_CELL*scale+8;
    }
    void option(Option &option) {
        SDL_FRect row={view.x,y,view.w-70,rowHeight};
        int flags=option.isEnabled(page.draft)?0:UI_DISABLED;
        int id=UI_NextId(ui);
        std::string value=valueText(option);
        if(!strcmp(option.key,"hdr")&&!gpuHdrOffered())value+=" (no HDR display)";
        if(option.kind==OptionKind::Check) {
            if(UI_Check(ui,row,option.title,option.flag,valueX,flags))presetFollows(option);
        } else if(option.kind==OptionKind::Choice) {
            int step=UI_Choice(ui,row,option.title,value.c_str(),valueX,flags);
            if(step) {stepChoice(option,step);presetFollows(option);}
        } else {
            float picked=0,fraction=(float)((*option.number-option.low)/(option.high-option.low));
            int step=UI_Slider(ui,row,option.title,value.c_str(),fraction,valueX,flags,&picked);
            if(step==2) {*option.number=option.snap(option.low+picked*(option.high-option.low));presetFollows(option);}
            else if(step) {*option.number=option.snap(*option.number+step*option.step);presetFollows(option);}
        }
        OptionCost cost=optionCost(option,page.draft);
        if(cost.cost!=Cost::None)UI_CostBars(ui,row.x+row.w+10,row.y+10,(int)cost.cost);
        if(UI_Focused(ui,id))ui->focus_tip=tipOf(option);
        y+=rowHeight+rowGap;
    }
    void fullscreen() {
        if(page.program<0||page.program>=LAUNCHER_PROGRAMS)return;
        SDL_FRect row={view.x,y,view.w-70,rowHeight};
        int id=UI_NextId(ui);
        UI_Check(ui,row,"Fullscreen",&state->fullscreen[page.program],valueX,0);
        if(UI_Focused(ui,id))ui->focus_tip="Starts the game fullscreen (-fullscreen). F11 switches in the game; "
                                           "on a Steam Deck the game is fullscreen anyway.";
        y+=rowHeight+rowGap;
    }
};
}

// What the table asks of the game and the renderer (see the top of this file).
extern "C" int Game_HasEffect(game_effect effect) {return (effects>>(unsigned)effect)&1u;}
extern "C" int I_SoundtrackFound(int soundtrack) {return soundtrack>=0&&soundtrack<5&&soundtrackFound[soundtrack];}
// No map in play: Weather's and Time of day's Random show as Random (climate_rules.h).
namespace doom3d {bool climateRolled(Climate&) {return false;} bool climateHourRolled(int,int&) {return false;}}
int gpuMaxSamples() {return 8;}
bool gpuHdrOffered() {
    SDL_PropertiesID properties=SDL_GetDisplayProperties(SDL_GetPrimaryDisplay());
    return properties&&SDL_GetBooleanProperty(properties,SDL_PROP_DISPLAY_HDR_ENABLED_BOOLEAN,false);
}

char *SettingsPage_Folder(int program) {
    const launcher_program *row=Games_Program(program);
    return row?SDL_GetPrefPath("Local Games",row->title):nullptr;
}

void SettingsPage_Select(int program) {
    if(program==page.program)return;
    SettingsPage_Save();
    page.program=program;
    page.dirty=false;
    page.list=ui_list{};
    page.resetAsked=0;
    page.groups.clear();
    if(program<0)return;
    useGame(program);
    page.draft=Settings{};
    readFile(configPath(program).c_str(),page.draft);
    page.defaults=Settings{};fitGame(page.defaults);
    matchPresets(page.draft);
    page.groups=optionGroups(page.draft);
    dropHidden(page.groups);
    for(OptionGroup &group:page.groups)
        group.tabs.erase(std::remove_if(group.tabs.begin(),group.tabs.end(),
            [](const OptionTab &tab){return tab.bindings;}),group.tabs.end());
}

void SettingsPage_ShowAll(int all) {page.all=all!=0;page.list=ui_list{};}

int SettingsPage_Save(void) {
    if(page.program<0||!page.dirty)return 1;
    useGame(page.program);
    fitGame(page.draft);
    if(!writeFile(page.program,page.draft))return 0;
    page.dirty=false;
    return 1;
}

int SettingsPage_SetValue(int program,const char *key,int value) {
    if(SettingsPage_ReadOnly)return 0;
    std::string path=configPath(program);
    if(path.empty())return 0;
    // Saved first, so the shown page's own changes are not lost under it.
    if(program==page.program)SettingsPage_Save();
    // The version line first. A file of an older release (no current configVersion line) is
    // left behind: the game would skip its lines anyway, and with them this one key.
    const std::string versionLine=std::string(configVersionKey)+" "+std::to_string(configVersion);
    std::string text=versionLine+"\n",line;
    size_t size=0;
    char *old=(char*)SDL_LoadFile(path.c_str(),&size);
    bool found=false;
    if(old) {
        std::string all(old,size);
        SDL_free(old);
        if(all.compare(0,versionLine.size(),versionLine)!=0||
           (all.size()>versionLine.size()&&all[versionLine.size()]!='\n'&&all[versionLine.size()]!='\r'))all.clear();
        else all.erase(0,versionLine.size());
        size_t start=0;
        while(start<all.size()) {
            size_t end=all.find('\n',start);
            if(end==std::string::npos)end=all.size();
            line=all.substr(start,end-start);
            if(!line.empty()&&line.back()=='\r')line.pop_back();
            if(line.compare(0,strlen(key)+1,std::string(key)+" ")==0) {line=std::string(key)+" "+std::to_string(value);found=true;}
            if(!line.empty())text+=line+"\n";
            start=end+1;
        }
    }
    if(!found)text+=std::string(key)+" "+std::to_string(value)+"\n";
    FILE *file=fopen(path.c_str(),"w");
    if(!file)return 0;
    fputs(text.c_str(),file);
    bool ok=fclose(file)==0;
    // The page shows the file as it is now.
    if(program==page.program) {page.program=-1;SettingsPage_Select(program);}
    return ok;
}

void SettingsPage_Draw(ui_state *ui,launcher_state *state,SDL_FRect area) {
    launcher_wad *wad=Launcher_Current(state);
    const launcher_game *game=Games_Get(wad->game);
    int program=state->selected>=0&&game?game->program:-1;
    SettingsPage_Select(program);
    if(program<0) {
        char text[160];
        if(state->selected>=0&&game&&game->reason)snprintf(text,sizeof(text),"%s cannot be played yet, so there is nothing to set up.",game->title);
        else snprintf(text,sizeof(text),"Choose a game on the left to set it up.");
        UI_Label(ui,area.x,area.y+10,text,2,UI_DIM);
        return;
    }
    // As the game's own F4 shows it: its effects, and a soundtrack where a pack it takes was found.
    useGame(program);
    soundtrackFound[1]=Launcher_PackFor(state,PACK_PSX,wad)!=nullptr;
    soundtrackFound[2]=Launcher_PackFor(state,PACK_3DO,wad)!=nullptr;
    soundtrackFound[3]=soundtrackFound[4]=Launcher_PackFor(state,PACK_EXTRAS,wad)!=nullptr;

    char title[128],shared[200],titles[160];
    snprintf(title,sizeof(title),"Settings for: %s",Games_Program(program)->title);
    Font_Draw(ui->renderer,area.x,area.y,title,3,UI_TEXT);
    // Which games share these, from the games table: one save folder per program.
    Games_ProgramTitles(program,titles,sizeof(titles));
    if(strchr(titles,',')||strstr(titles," and "))snprintf(shared,sizeof(shared),"%s share these.",titles);
    else shared[0]=0;
    Font_Draw(ui->renderer,area.x,area.y+32,shared,1,UI_DIM);

    // The buttons and the focused option's words stay at the bottom; the options scroll above them.
    const float tipHeight=96,buttonsHeight=44;
    SDL_FRect view={area.x,area.y+48,area.w,area.h-48-tipHeight-buttonsHeight-16};
    // A wheel notch goes three rows.
    UI_ListBegin(ui,&page.list,view,3*(rowHeight+rowGap),1);
    SDL_SetRenderClipRect(ui->renderer,nullptr);
    SDL_Rect clip={(int)view.x-4,(int)view.y,(int)view.w+8,(int)view.h};
    SDL_SetRenderClipRect(ui->renderer,&clip);
    ui->clipped=1;ui->clip=view;
    Drawer draw{ui,state,view,view.y-page.list.scroll,view.x+view.w*0.42f};
    draw.heading("DISPLAY",2);
    for(const char *key:displayKeys) {
        if(!strcmp(key,"fullscreen")) {draw.fullscreen();continue;}
        if(Option *option=findOption(page.groups,key))draw.option(*option);
    }
    if(page.all)
        for(OptionGroup &group:page.groups) {
            draw.heading(group.title,2);
            for(OptionTab &tab:group.tabs)for(OptionSection &section:tab.sections) {
                std::string name=std::string(tab.title)+(section.title[0]&&strcmp(section.title,tab.title)?std::string(": ")+section.title:"");
                draw.heading(name.c_str(),1);
                for(Option &option:section.options)draw.option(option);
            }
        }
    // The content ends with the last row's gap below it.
    float content=draw.y+page.list.scroll-view.y;
    ui->clipped=0;
    SDL_SetRenderClipRect(ui->renderer,nullptr);
    UI_ListEnd(ui,&page.list,view,content);

    float y=view.y+view.h+12;
    SDL_FRect allButton={area.x,y,250,buttonsHeight};
    // The button keeps the focus as the list above it grows or shrinks.
    if(page.focusAll) {ui->focus=UI_NextId(ui);page.focusAll=false;}
    if(UI_Button(ui,allButton,page.all?"Display only":"All settings...",0)) {
        page.all=!page.all;page.list.scroll=page.list.drawn=0;
        page.focusAll=true;ui->redraw=1;
    }
    SDL_FRect copyButton={area.x+264,y,280,buttonsHeight};
    if(UI_Button(ui,copyButton,"Copy to all games",Games_ProgramCount()>1?0:UI_DISABLED))copyToAll(state);
    SDL_FRect resetButton={area.x+558,y,280,buttonsHeight};
    Uint64 now=SDL_GetTicks();
    bool asking=page.resetAsked&&now-page.resetAsked<4000;
    int id=UI_NextId(ui);
    if(UI_Button(ui,resetButton,asking?"Really reset?":"Reset to defaults",0)) {
        if(asking) {page.resetAsked=0;reset(state);}
        else {
            page.resetAsked=now;
            Launcher_Status(state,1,"Press again to delete %s's graphics.cfg and its bake cache.",Games_Program(program)->title);
        }
    }
    if(UI_Focused(ui,id))ui->focus_tip="Deletes this game's graphics.cfg and its bake cache, so it starts as it came: "
                                       "the cure for a setting that keeps the game from starting properly.";

    SDL_FRect tip={area.x,y+buttonsHeight+12,area.w,tipHeight-8};
    UI_Fill(ui,tip,SDL_Color{28,27,26,255});
    if(ui->focus_tip&&*ui->focus_tip) {
        int scale=Font_Width(ui->focus_tip,2)<=(tip.w-20)*3?2:1;
        Font_DrawWrapped(ui->renderer,tip.x+10,tip.y+8,ui->focus_tip,scale,UI_DIM,tip.w-20,scale==2?3:7);
    }
}

int SettingsPage_GameHas(int program,const char *key) {
    Settings s;
    std::vector<OptionGroup> groups=optionGroups(s);
    Option *option=findOption(groups,key);
    bool has=option&&!gameLacks(*option,program,s);
    useGame(page.program);
    return has;
}
