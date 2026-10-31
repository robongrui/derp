/* The in-game F4 settings panel. It draws into its own 640x400 image on a
 * grid of 8x16 cells, like a text-mode setup program, in the WAD's own small
 * font (STCFN), uppercase as in DOOM's messages; the renderer scales it by
 * whole pixels over the dimmed view. Changes apply when the panel closes. */
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>
extern "C" {
#include "doomtype.h"
#include "doomdef.h"
#include "s_sound.h"
#include "w_wad.h"
#include "z_zone.h"
extern boolean paused;
}
#include <SDL3/SDL.h>
#include "i_render3d.h"
#include "scene3d.h"
#include "settings_panel.h"
#include "game_profile.h"
#include "small_font.h"

using namespace doom3d;

namespace {
// One row of the current tab: a section heading (option null) or an option.
struct Line { const OptionSection *section; Option *option; };

struct Panel {
    bool open=false,wasPaused=false;
    Settings draft,defaults; // defaults: as R sets them, for the per-option reset and marks
    std::vector<OptionGroup> groups;
    std::vector<Line> lines;
    int group=0,tab=0,selected=0,scroll=0;
    std::vector<int> groupTab;                // the tab last shown in each group
    std::vector<std::pair<int,int>> groupSpans,tabSpans; // left and right pixel of each group's title and tab's bar
} panel;

// The image is a grid of 8x16 character cells, 80 columns by 25 rows.
const int width=DOOM_SETTINGS_WIDTH,height=DOOM_SETTINGS_HEIGHT,cellWidth=8,cellHeight=16;
const int groupRow=0,tabRow=1,listRow=3,visibleLines=16,descriptionRow=20,descriptionLines=4,helpRow=24;
const int leftColumn=2,rightColumn=78,sliderColumns=20,valueColumns=6;

// Colors as 0xAARRGGBB, the byte order of the B8G8R8A8 texture.
const unsigned backdrop=0xc8080808,separator=0xff3a3a3a,text=0xffe6e6e6,heading=0xffe8b040,dim=0xff686868,
               description=0xffb4b4b4,selectionBar=0xff7a1414,bright=0xffffffff,onColor=0xff8cd48c,
               tabBar=0xffa01818,track=0xff505050,trackFill=0xffd03030;

unsigned *image;

void fill(int x,int y,int w,int h,unsigned color) {
    for(int row=std::max(0,y);row<std::min(height,y+h);++row)
        std::fill(image+row*width+std::max(0,x),image+row*width+std::min(width,x+w),color);
}

// The game's small font (small_font.h), vertically centered in a row's cell.
int textWidth(const std::string &string) { return smallFont::width(string); }

// Draws from pixel x, vertically centered in the row's cell; returns the end.
int drawTextAt(const std::string &string,int x,int row,unsigned color) {
    return smallFont::draw(image,width,height,string,x,row*cellHeight+(cellHeight-smallFont::letterHeight())/2,color);
}

void drawText(const std::string &string,int column,int row,unsigned color) { drawTextAt(string,column*cellWidth,row,color); }

// Ends the text at the left edge of column.
void drawTextRight(const std::string &string,int column,int row,unsigned color) {
    drawTextAt(string,column*cellWidth-textWidth(string),row,color);
}

int rowY(int row) { return row*cellHeight; }

void drawSeparator(int row) { fill(leftColumn*cellWidth,rowY(row)+cellHeight/2,(rightColumn-leftColumn)*cellWidth,1,separator); }

// Splits text into lines at most lineWidth pixels wide; the last line ends
// in "..." when the text does not fit.
std::vector<std::string> wrap(const std::string &string,int lineWidth,size_t maxLines) {
    std::vector<std::string> lines;
    std::string line,word;
    auto flushWord=[&] {
        if(word.empty())return;
        if(line.empty())line=word;
        else if(textWidth(line+" "+word)<=lineWidth)line+=" "+word;
        else {lines.push_back(line);line=word;}
        word.clear();
    };
    for(char c:string) {if(c==' ')flushWord();else word+=c;}
    flushWord();
    if(!line.empty())lines.push_back(line);
    if(lines.size()>maxLines) {
        lines.resize(maxLines);
        std::string &last=lines.back();
        while(!last.empty()&&textWidth(last+"...")>lineWidth)last.pop_back();
        last+="...";
    }
    return lines;
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

std::vector<OptionTab> &groupTabs() { return panel.groups[(size_t)panel.group].tabs; }

void selectTab(int tab) {
    int count=(int)groupTabs().size();
    panel.tab=panel.groupTab[(size_t)panel.group]=(tab%count+count)%count;
    panel.lines.clear();
    for(OptionSection &section:groupTabs()[(size_t)panel.tab].sections) {
        panel.lines.push_back({&section,nullptr});
        for(Option &option:section.options)panel.lines.push_back({&section,&option});
    }
    panel.selected=1;panel.scroll=0;
}

// A group shows the tab it showed last.
void selectGroup(int group) {
    int count=(int)panel.groups.size();
    panel.group=(group%count+count)%count;
    selectTab(panel.groupTab[(size_t)panel.group]);
}

// Tab and Shift+Tab: the next or previous tab, on into the next or previous group.
void stepTab(int direction) {
    int tab=panel.tab+direction;
    if(tab>=0&&tab<(int)groupTabs().size()) {selectTab(tab);return;}
    int count=(int)panel.groups.size();
    panel.group=((panel.group+direction)%count+count)%count;
    selectTab(direction>0?0:(int)groupTabs().size()-1);
}

void keepSelectionVisible() {
    // The first option brings its section heading into view too.
    if(panel.selected==1)panel.scroll=0;
    if(panel.selected<panel.scroll)panel.scroll=panel.selected;
    if(panel.selected>=panel.scroll+visibleLines)panel.scroll=panel.selected-visibleLines+1;
}

void moveSelection(int direction) {
    int count=(int)panel.lines.size();
    do panel.selected=((panel.selected+direction)%count+count)%count;
    while(!panel.lines[(size_t)panel.selected].option);
    keepSelectionVisible();
    S_StartSound(NULL,Game_MenuSound(GAME_MENU_MOVE));
}

Option &selectedOption() { return *panel.lines[(size_t)panel.selected].option; }

void setSlider(Option &option,double value) { *option.number=option.snap(value); }

// The options point into panel.draft; this is the same member of panel.defaults.
template<class T> T *defaultOf(T *member) {
    return (T*)((char*)&panel.defaults+((char*)member-(char*)&panel.draft));
}

bool isDefault(const Option &option) {
    if(option.kind==OptionKind::Slider)return option.snap(*option.number)==option.snap(*defaultOf(option.number));
    return *option.flag==*defaultOf(option.flag);
}

void resetOption() {
    Option &option=selectedOption();
    if(isDefault(option)) {S_StartSound(NULL,Game_MenuSound(GAME_MENU_DENY));return;}
    if(option.kind==OptionKind::Slider)*option.number=*defaultOf(option.number);
    else *option.flag=*defaultOf(option.flag);
    S_StartSound(NULL,Game_MenuSound(GAME_MENU_CHANGE));
}

void changeOption(int direction) {
    Option &option=selectedOption();
    if(!option.isEnabled(panel.draft)) {S_StartSound(NULL,Game_MenuSound(GAME_MENU_DENY));return;}
    if(option.kind==OptionKind::Check)*option.flag=!*option.flag;
    else if(option.kind==OptionKind::Choice) {
        int count=(int)option.values.size(),index=0;
        for(int i=0;i<count;++i)if(option.values[(size_t)i]==*option.flag)index=i;
        // On to the next value that goes with the other settings.
        for(int step=1;step<count;++step) {
            int value=option.values[(size_t)(((index+direction*step)%count+count)%count)];
            if(option.isAllowed(panel.draft,value)) {*option.flag=value;break;}
        }
    } else setSlider(option,*option.number+direction*option.step);
    S_StartSound(NULL,Game_MenuSound(GAME_MENU_CHANGE));
}

void closePanel() {
    // A new Gameplay (Normal, Authored, Phobos) takes a map built for it: the game restarts the map if it plays differently.
    bool modeChanged=panel.draft.mode!=settings.mode;
    settings=panel.draft;
    paused=panel.wasPaused;
    panel.open=false;
    settingsChanged();
    if(modeChanged)Game_ModeChanged();
    S_StartSound(NULL,Game_MenuSound(GAME_MENU_CLOSE));
}

int sliderLeft() { return (rightColumn-valueColumns-sliderColumns)*cellWidth; }
int sliderWidth() { return (sliderColumns-1)*cellWidth; }

// A small triangle in the column right of the list, pointing where more options are.
void drawArrow(int row,bool up) {
    int x=(rightColumn+1)*cellWidth+cellWidth/2,y=rowY(row)+(cellHeight-4)/2;
    for(int i=0;i<4;++i)fill(x-(up?i:3-i),y+i,2*(up?i:3-i)+1,1,dim);
}

void drawSlider(const Option &option,int row,bool enabled) {
    int x=sliderLeft(),w=sliderWidth(),y=rowY(row)+cellHeight/2;
    int position=(int)std::lround((*option.number-option.low)/(option.high-option.low)*(w-1));
    fill(x,y-1,w,2,track);
    fill(x,y-1,position,2,enabled?trackFill:dim);
    fill(x+position-2,y-5,5,10,enabled?bright:dim);
}

// A small diamond in the margin left of an option changed from its default.
void drawChangedMark(int row,bool enabled) {
    int x=(leftColumn+1)*cellWidth+cellWidth/2,y=rowY(row)+cellHeight/2-2;
    for(int i=0;i<5;++i) {int half=2-std::abs(i-2);fill(x-half,y+i,2*half+1,1,enabled?heading:dim);}
}
}

void I_Render3DSettingsMenu(void) {
    if(panel.open)return;
    panel.draft=settings;
    panel.defaults=Settings{};fitGame(panel.defaults);
    panel.groups=optionGroups(panel.draft);
    panel.groupTab.resize(panel.groups.size(),0);
    for(size_t g=0;g<panel.groups.size();++g)panel.groupTab[g]=std::min(panel.groupTab[g],(int)panel.groups[g].tabs.size()-1);
    panel.group=std::min(panel.group,(int)panel.groups.size()-1);
    selectGroup(panel.group);
    panel.wasPaused=paused;paused=true;
    panel.open=true;
    S_StartSound(NULL,Game_MenuSound(GAME_MENU_OPEN));
}

int I_Render3DSettingsOpen(void) { return panel.open; }

void I_Render3DSettingsKey(int scancode,int shift) {
    if(!panel.open)return;
    switch(scancode) {
    case SDL_SCANCODE_UP: moveSelection(-1); break;
    case SDL_SCANCODE_DOWN: moveSelection(1); break;
    case SDL_SCANCODE_LEFT: changeOption(-1); break;
    case SDL_SCANCODE_RIGHT: case SDL_SCANCODE_RETURN: case SDL_SCANCODE_KP_ENTER: case SDL_SCANCODE_SPACE:
        changeOption(1); break;
    case SDL_SCANCODE_TAB: stepTab(shift?-1:1); S_StartSound(NULL,Game_MenuSound(GAME_MENU_MOVE)); break;
    case SDL_SCANCODE_PAGEUP: selectGroup(panel.group-1); S_StartSound(NULL,Game_MenuSound(GAME_MENU_MOVE)); break;
    case SDL_SCANCODE_PAGEDOWN: selectGroup(panel.group+1); S_StartSound(NULL,Game_MenuSound(GAME_MENU_MOVE)); break;
    case SDL_SCANCODE_BACKSPACE: case SDL_SCANCODE_DELETE: resetOption(); break;
    case SDL_SCANCODE_R: panel.draft=panel.defaults; S_StartSound(NULL,Game_MenuSound(GAME_MENU_CHANGE)); break;
    case SDL_SCANCODE_ESCAPE: case SDL_SCANCODE_F4: closePanel(); break;
    default:
        if(scancode>=SDL_SCANCODE_1&&scancode<SDL_SCANCODE_1+(int)groupTabs().size()) {
            selectTab(scancode-SDL_SCANCODE_1);S_StartSound(NULL,Game_MenuSound(GAME_MENU_MOVE));
        }
        break;
    }
}

void I_Render3DSettingsClick(int x,int y,int button) {
    if(!panel.open||x<0||y<0||x>=width||y>=height)return;
    int row=y/cellHeight;
    if(row==groupRow) {
        if(button!=1)return;
        for(size_t i=0;i<panel.groupSpans.size();++i)
            if(x>=panel.groupSpans[i].first&&x<panel.groupSpans[i].second) {
                if((int)i!=panel.group) {selectGroup((int)i);S_StartSound(NULL,Game_MenuSound(GAME_MENU_MOVE));}
                return;
            }
        return;
    }
    if(row==tabRow) {
        if(button!=1)return;
        for(size_t i=0;i<panel.tabSpans.size();++i)
            if(x>=panel.tabSpans[i].first&&x<panel.tabSpans[i].second) {
                if((int)i!=panel.tab) {selectTab((int)i);S_StartSound(NULL,Game_MenuSound(GAME_MENU_MOVE));}
                return;
            }
        return;
    }
    int index=panel.scroll+row-listRow;
    if(row<listRow||row>=listRow+visibleLines||index>=(int)panel.lines.size()||!panel.lines[(size_t)index].option)return;
    panel.selected=index;
    if(button==0)return; // Hovering highlights the option.
    Option &option=selectedOption();
    if(option.kind==OptionKind::Slider&&button==1&&x>=sliderLeft()-cellWidth) {
        if(!option.isEnabled(panel.draft)) {S_StartSound(NULL,Game_MenuSound(GAME_MENU_DENY));return;}
        double fraction=(double)(x-sliderLeft())/(sliderWidth()-1);
        setSlider(option,option.low+std::clamp(fraction,0.0,1.0)*(option.high-option.low));
        S_StartSound(NULL,Game_MenuSound(GAME_MENU_CHANGE));
    } else changeOption(button==1?1:-1);
}

void I_Render3DSettingsDraw(unsigned *pixels) {
    if(!panel.open)return;
    image=pixels;
    smallFont::load();
    fill(0,0,width,height,backdrop);

    // The groups across the top, the current one in gold and underlined, the panel's name at the right.
    panel.groupSpans.clear();
    int x=leftColumn*cellWidth;
    for(size_t i=0;i<panel.groups.size();++i) {
        std::string title=panel.groups[i].title;
        bool current=(int)i==panel.group;
        int w=textWidth(title);
        drawTextAt(title,x,groupRow,current?heading:dim);
        if(current)fill(x,rowY(groupRow)+cellHeight-3,w,1,heading);
        panel.groupSpans.push_back({x-cellWidth,x+w+cellWidth});
        x+=w+4*cellWidth;
    }
    drawTextRight("F4 Settings",rightColumn,groupRow,dim);
    // The group's tabs below them, the current one on a red bar.
    panel.tabSpans.clear();
    x=leftColumn*cellWidth;
    for(size_t i=0;i<groupTabs().size();++i) {
        std::string title=groupTabs()[i].title;
        bool current=(int)i==panel.tab;
        int w=textWidth(title);
        if(current)fill(x-cellWidth,rowY(tabRow),w+2*cellWidth,cellHeight,tabBar);
        drawTextAt(title,x,tabRow,current?bright:description);
        panel.tabSpans.push_back({x-cellWidth,x+w+cellWidth});
        x+=w+3*cellWidth;
    }
    drawSeparator(tabRow+1);

    for(int i=0;i<visibleLines&&panel.scroll+i<(int)panel.lines.size();++i) {
        const Line &line=panel.lines[(size_t)(panel.scroll+i)];
        int row=listRow+i;
        if(!line.option) {drawText(line.section->title,leftColumn,row,heading);continue;}
        const Option &option=*line.option;
        bool selected=panel.scroll+i==panel.selected,enabled=option.isEnabled(panel.draft);
        if(selected)fill((leftColumn+1)*cellWidth,rowY(row),(rightColumn-leftColumn)*cellWidth,cellHeight,selectionBar);
        unsigned color=!enabled?dim:selected?bright:text;
        drawText(option.title,leftColumn+2,row,color);
        if(!isDefault(option))drawChangedMark(row,enabled);
        unsigned valueColor=enabled&&option.kind==OptionKind::Check&&*option.flag?onColor:color;
        drawTextRight(valueText(option),rightColumn,row,valueColor);
        if(option.kind==OptionKind::Slider)drawSlider(option,row,enabled);
    }
    if(panel.scroll>0)drawArrow(listRow,true);
    if(panel.scroll+visibleLines<(int)panel.lines.size())drawArrow(listRow+visibleLines-1,false);
    drawSeparator(descriptionRow-1);

    const Option &option=selectedOption();
    std::vector<std::string> lines=wrap(option.tip?option.tip:"",(rightColumn-leftColumn)*cellWidth,descriptionLines);
    for(size_t i=0;i<lines.size();++i)drawText(lines[i],leftColumn,descriptionRow+(int)i,description);
    std::string help="Arrows select, change   Tab page   PgDn group   Backspace reset   R reset all   Esc done";
    drawTextAt(help,(width-textWidth(help))/2,helpRow,dim);
    // The renderer repeats the border texels beyond the image, so they stay plain backdrop.
    fill(0,0,width,1,backdrop);fill(0,height-1,width,1,backdrop);
    fill(0,0,1,height,backdrop);fill(width-1,0,1,height,backdrop);
}

void I_Render3DLoadingDraw(unsigned *pixels,const char *title,const char *stage,float fraction) {
    image=pixels;
    smallFont::load();
    const unsigned black=0xff000000;
    fill(0,0,width,height,black);
    const int titleRow=11,barY=rowY(titleRow+1)+4,barWidth=320,barX=(width-barWidth)/2,barHeight=6;
    drawTextAt(title,(width-textWidth(title))/2,titleRow,heading);
    fill(barX-2,barY-2,barWidth+4,barHeight+4,track);
    fill(barX-1,barY-1,barWidth+2,barHeight+2,black);
    fill(barX,barY,(int)std::lround(barWidth*std::clamp(fraction,0.0f,1.0f)),barHeight,trackFill);
    drawTextAt(stage,(width-textWidth(stage))/2,titleRow+2,description);
}

void I_Render3DSettings(void) { I_Render3DSettingsMenu(); }
