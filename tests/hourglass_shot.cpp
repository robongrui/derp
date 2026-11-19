/* A screenshot of Authored's hourglass beside the status bar (scene3d/hourglass.cpp), on the GPU.
   doom-hourglass-shot <iwad> <output-dir> <name> <hud_width 0|1> <hud_size> [-slowmotionon]
   With -slowmotionon the meter runs down by hand from the start and the shot shows the stream.
   The output directory must exist. Uses its own configs and no desktop input. */
#include <cstdio>
#include <string>
#include <vector>
extern "C" {
#include "doomdef.h"
#include "doomstat.h"
#include "d_main.h"
#include "i_system.h"
#include "m_argv.h"
#include "p_local.h"
#include "p_focus.h"
#include "../platform/i_render3d.h"
void Game_SetAuthored(int on);
}
#include "../platform/scene3d.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

namespace {
std::string outputDir,name,pending;
int stage=0,lastTic=-1,waited=0;
}

extern "C" void I_TestFrame(void) {
    if(gamestate!=GS_LEVEL||!players[consoleplayer].mo)return;
    if(leveltime==lastTic)return;
    lastTic=leveltime;
    if(stage==0) {Game_SetAuthored(1);stage=1;return;}
    if(stage==1) {
        if(++waited<70)return;
        std::printf("Hourglass: %d of %d, %s.\n",P_FocusLeft(),FOCUS_MOST,P_FocusFlowing()?"running":"still");
        pending=outputDir+"/"+name+".png";
        I_Render3DScreenshotTo(pending.c_str());
        stage=2;return;
    }
    if(stage==2&&++waited>74) {
        FILE *file=std::fopen(pending.c_str(),"rb");
        if(!file)I_Error((char*)"Hourglass shot: no screenshot at %s",pending.c_str());
        std::fclose(file);
        std::printf("Hourglass shot: %s\n",pending.c_str());
        I_Quit();
    }
}

int main(int argc,char **argv) {
    if(argc<6) {std::fprintf(stderr,"Usage: %s <iwad> <output-dir> <name> <hud_width> <hud_size> [-slowmotionon]\n",argv[0]);return 1;}
    outputDir=argv[2];name=argv[3];
    std::string graphics=outputDir+"/"+name+"-graphics.cfg",config=outputDir+"/"+name+"-doom.cfg";
    FILE *file=std::fopen(graphics.c_str(),"w");
    if(!file) {std::fprintf(stderr,"Cannot write %s; create the output directory first.\n",graphics.c_str());return 1;}
    std::fprintf(file,"accelerated 1\nwidescreen 1\nhud_width %s\nhud_size %s\ncrosshair 0\nfps 0\n",argv[4],argv[5]);
    std::fclose(file);
    file=std::fopen(config.c_str(),"w");
    if(!file)return 1;
    std::fputs("show_messages 0\n",file);std::fclose(file);
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER,"dummy");
    if(!SDL_Init(SDL_INIT_VIDEO))return 1;
    std::vector<std::string> args={"hourglass-shot","-iwad",argv[1],"-config",config,"-graphics",graphics,
        "-nomonsters","-nomusic","-nosound","-width","1280","-height","720","-warp","1","1","-derpai","-hidden"};
    for(int i=6;i<argc;++i)args.emplace_back(argv[i]);
    std::vector<char*> pointers;
    for(auto &arg:args)pointers.push_back(arg.data());
    myargc=(int)pointers.size();myargv=pointers.data();
    singletics=true;
    D_DoomMain();
    return 0;
}
