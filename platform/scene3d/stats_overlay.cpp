/* F4's "Show FPS" readout in the top left corner, in the game's small font,
   so a screenshot says what a report needs. FPS: one line, the frame rate,
   the average and the worst frame time. Full: also
   - a graph of the last 180 frame times (green near the average, yellow
     past one and a half times it, red past two and a half),
   - where a frame's CPU time goes (frame_stats.cpp) and the wait for the
     display, the CPU load,
   - how frames reach the display, the GPU, the sizes, the version and the map,
   - the systems at work now, each with its CPU time per frame and its worst
     frame, and what it does (the weather's particles, the film's cells...);
     a system that sleeps is left out, so one listed while nothing happens is
     a bug.
   Redrawn when frame_stats.cpp has a new view, four times a second. */
#include "internal.h"
#include "../small_font.h"
#include <cctype>
#include <cstdarg>
extern "C" {
#include "hu_stuff.h"
#include "../audio/sound_options.h"
}

namespace doom3d {
namespace {
constexpr int canvasW=460,canvasH=340,pad=4,graphW=(int)statsHistory,graphH=36;
// Colors as 0xAABBGGRR, the byte order of the RGBA8 texture.
constexpr unsigned textColor=0xffe6e6e6,headingColor=0xff40b0e8,dimColor=0xff9a9a9a,
    goodColor=0xff6cc46c,slowColor=0xff40c8e8,badColor=0xff4050e0,backing=0xa8000000,graphBack=0xd8000000;

struct Canvas {
    std::vector<unsigned> pixels=std::vector<unsigned>((size_t)canvasW*canvasH,0);
    int right=0,bottom=0;   // the backing's extent
    void fill(int x,int y,int w,int h,unsigned color) {
        for(int py=std::max(0,y);py<std::min(canvasH,y+h);++py)
            for(int px=std::max(0,x);px<std::min(canvasW,x+w);++px)pixels[(size_t)py*canvasW+px]=color;
    }
    int text(const std::string &s,int x,int y,unsigned color) {
        int end=smallFont::draw(pixels.data(),canvasW,canvasH,s,x,y,color);
        right=std::max(right,end);bottom=std::max(bottom,y+smallFont::letterHeight());
        return end;
    }
    void textRight(const std::string &s,int x,int y,unsigned color) {text(s,x-smallFont::width(s),y,color);}
};

std::string format(const char *f,...) {
    char line[160];
    va_list args;va_start(args,f);vsnprintf(line,sizeof line,f,args);va_end(args);
    return line;
}

const char *statName(Stat stat) {
    static const char *names[]={"WORLD","SPRITES","LIGHTS","MIST","BAKES","WEATHER","DRAFTS","FILM","FOG",
                                "DISTURBANCES","WEAR","GLASS","BLOOD","BODIES","LIQUIDS","SHOT LIGHTS"};
    static_assert(sizeof(names)/sizeof(names[0])==(size_t)Stat::Count,"A name per system");
    return names[(size_t)stat];
}

// What a system does now, or nothing while it sleeps (items: its count from statsCount).
std::string statDetail(Stat stat,int items) {
    switch(stat) {
    case Stat::World: {
        size_t vertices=0;
        for(const auto &batch:batches)vertices+=batch.second.size();
        return format("%zu TRIANGLES%s",vertices/3,reflectionActive?", WATER MIRROR":"");
    }
    case Stat::Lights: return flashes.count?format("%u DYNAMIC, %zu BLOCKERS",flashes.count,lightBlockers.size()):"";
    case Stat::Bakes: return relightRunning()?"RELIGHTING":"";
    case Stat::Weather: {
        Weather now=settings.weather?falling():WEATHER_NONE;
        if(now==WEATHER_NONE)return "";
        std::string kind=weatherKind(now).name;
        for(char &c:kind)c=(char)toupper((unsigned char)c);
        std::string text=format("%s %.0f%%, WIND %.0f%%, %.0fC",kind.c_str(),fallNow*100,windAmount()*100,climateTemperature(now));
        return items>0?text+format(", %d PARTICLES",items):text;
    }
    case Stat::Drafts: {int links=draftLinkCount();return links>0?format("%d OPENINGS",links):"";}
    case Stat::Film: {
        int awake=filmAwake(),lying=filmLying();
        return awake||lying?format("%d CELLS AWAKE, %d LYING",awake,lying):"";
    }
    case Stat::Fog: return dryIceReady()?"DRY ICE":fogBankCount()>0?"BANK":"";
    case Stat::Disturbances: {int n=disturbanceCount();return n?format("%d ALIVE",n):"";}
    case Stat::Glass: {
        int panes=windowPaneCount();
        return panes?format("%d PANES%s",panes,glassMirrorActive?", MIRROR":""):"";
    }
    case Stat::Bodies: {int moving=ragdollsMoving();return moving?format("%d RAGDOLLS MOVING",moving):"";}
    default: return "";
    }
}

unsigned frameColor(float ms,float average) {
    return ms>average*2.5f?badColor:ms>average*1.5f?slowColor:goodColor;
}

void drawGraph(Canvas &c,const StatsView &v,int x,int y) {
    // The scale: two and a half times the average, rounded up to 5 ms, at least 10.
    float top=std::max(10.0f,std::ceil(v.frameMs*2.5f/5)*5);
    c.fill(x,y,graphW,graphH,graphBack);
    for(int i=0;i<graphW;++i) {
        float ms=v.recent[(size_t)i];
        if(ms<=0)continue;
        int h=std::clamp((int)std::lround(ms/top*graphH),1,graphH);
        c.fill(x+i,y+graphH-h,1,h,frameColor(ms,v.frameMs));
    }
    int average=y+graphH-std::clamp((int)std::lround(v.frameMs/top*graphH),0,graphH);
    for(int i=0;i<graphW;i+=2)c.fill(x+i,average,1,1,textColor);
    c.text(format("%.0f MS",top),x+graphW+pad,y,dimColor);
    c.text("0",x+graphW+pad,y+graphH-smallFont::letterHeight(),dimColor);
    c.right=std::max(c.right,x+graphW);c.bottom=std::max(c.bottom,y+graphH);
}

void draw(Canvas &c,const StatsView &v,bool full,int renderW,int renderH,int displayW,int displayH) {
    int line=smallFont::letterHeight()+4,y=pad;
    int x=c.text(format("%.0f FPS",v.fps),pad,y,headingColor);
    c.text(format("  %.1f MS  WORST %.1f",v.frameMs,v.worstMs),x,y,frameColor(v.worstMs,v.frameMs)==badColor?badColor:textColor);
    y+=line;
    if(!full)return;
    drawGraph(c,v,pad,y+1);
    y+=graphH+line/2+2;
    float cpu=v.gameMs+v.effectsMs+v.sceneMs+v.submitMs;
    c.text(format("CPU %.1f MS  GAME %.1f  EFFECTS %.1f  SCENE %.1f  SUBMIT %.1f",cpu,v.gameMs,v.effectsMs,v.sceneMs,v.submitMs),pad,y,textColor);
    y+=line;
    std::string load=v.load>=0?format("  LOAD %.0f%% OF %d CORES",v.load,v.cores):"";
    if(v.systemLoad>=0)load+=format(", SYSTEM %.0f%%",v.systemLoad);
    c.text(format("WAIT %.1f MS",v.waitMs)+load,pad,y,textColor);
    y+=line;
    static const char *limits[]={"","DISPLAY RATE","30","60","120","144","240"};
    std::string shown=gpuPresentationName();
    if(settings.presentation>=2&&settings.frameLimit>0&&settings.frameLimit<7)shown+=format(", LIMIT %s",limits[settings.frameLimit]);
    c.text(shown,pad,y,textColor);
    y+=line;
    std::string gpu=gpuDeviceName();
    gpu+=format("%s%s",gpu.empty()?"":"  ",gpuApiName());
    if(*gpuDriverVersion())gpu+=format("  DRIVER %s",gpuDriverVersion());
    c.text(gpu,pad,y,textColor);
    y+=line;
    c.text(format("%dX%d, RENDER %dX%d",displayW,displayH,renderW,renderH),pad,y,textColor);
    y+=line;
    const char *level=gamestate==GS_LEVEL?HU_LevelTitle():nullptr;
    c.text(format("DERP %s  %s%s%s",DOOM_SHOWN_VERSION,Game_Title(),level&&*level?"  ":"",level?level:""),pad,y,dimColor);
    y+=line+line/2;
    // The systems at work, their CPU time per frame and their worst frame (ms).
    int detailX=pad+smallFont::width("DISTURBANCES")+3*pad,timeX=canvasW-pad;
    c.textRight("MS  WORST",timeX,y,dimColor);
    c.text("AT WORK",pad,y,dimColor);
    y+=line;
    for(int i=0;i<(int)Stat::Count&&y+line<=canvasH;++i) {
        Stat stat=(Stat)i;
        std::string detail=gamestate==GS_LEVEL?statDetail(stat,v.items[(size_t)i]):"";
        float ms=v.systemMs[(size_t)i];
        if(ms<0.005f&&detail.empty())continue;
        c.text(statName(stat),pad,y,headingColor);
        c.text(detail,detailX,y,textColor);
        c.textRight(format("%.2f  %.2f",ms,v.systemWorst[(size_t)i]),timeX,y,ms>=1?slowColor:textColor);
        y+=line;
    }
    int voices=I_SoundVoices();
    if(voices&&y+line<=canvasH) {
        c.text("SOUND",pad,y,headingColor);
        c.text(format("%d OF 32 VOICES",voices),detailX,y,textColor);
    }
    c.right=canvasW;
}
}

GpuTextureRef statsOverlay(int &width,int &height,int renderWidth,int renderHeight,int displayWidth,int displayHeight) {
    static GpuTextureRef texture;
    static unsigned drawnSerial=~0u;
    static int drawnMode=-1;
    if(!settings.accelerated||settings.fps<2)return nullptr;
    const StatsView &v=statsView();
    if(v.serial==0)return nullptr;
    if(v.serial!=drawnSerial||settings.fps!=drawnMode||!texture) {
        smallFont::load();
        Canvas c,text;
        draw(text,v,settings.fps>=3,renderWidth,renderHeight,displayWidth,displayHeight);
        // A dim backing behind the text, so it reads anywhere; the rest stays clear.
        c.fill(0,0,std::min(canvasW,text.right+pad),std::min(canvasH,text.bottom+pad),backing);
        for(size_t i=0;i<c.pixels.size();++i)if(text.pixels[i])c.pixels[i]=text.pixels[i];
        if(!texture)texture=gpuCreateTexture(GpuFormat::RGBA8,canvasW,canvasH,c.pixels.data(),canvasW*4);
        else gpuUpdateTexture(texture,GpuFormat::RGBA8,{{0,0,canvasW,canvasH}},c.pixels.data(),canvasW*4);
        drawnSerial=v.serial;drawnMode=settings.fps;
    }
    width=canvasW;height=canvasH;
    return texture;
}
} // namespace doom3d
