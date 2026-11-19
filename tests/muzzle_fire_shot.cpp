/* Screenshots of muzzle fire (scene3d/muzzle_fire.cpp) on the GPU: a zombieman, a shotgun guy and a
   chaingunner ahead of the player firing at him, the moment after and a few tics on (the smoke), then
   the player's own shotgun. doom-muzzle-fire-shot <iwad> <output-dir> [-slowmotionon]
   The output directory must exist. Uses its own configs and no desktop input. */
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
extern "C" {
#include "doomdef.h"
#include "doomstat.h"
#include "d_main.h"
#include "i_system.h"
#include "m_argv.h"
#include "p_local.h"
#include "r_main.h"
#include "r_draw.h"
#include "w_wad.h"
#include "r_defs.h"
#include "z_zone.h"
#include "m_swap.h"
#include "../platform/i_render3d.h"
void Game_SetAuthored(int on);
void P_SetPsprite(player_t *player,int position,statenum_t stnum);
}
#include "../platform/scene3d.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

namespace {
std::string outputDir;
int lastTic=-1,start=-1,shots=0;
std::vector<mobj_t*> soldiers;
mobj_t *marker=nullptr;

std::string pending;   // the renderer keeps a pointer to it until the shot is written
void capture(const char *name) {
    pending=outputDir+"/"+name+".png";
    std::printf("  at %.0f,%.0f z %.0f view %.0f pitch %.2f health %d\n",players[0].mo->x/65536.0,players[0].mo->y/65536.0,
                players[0].mo->z/65536.0,players[0].viewz/65536.0,doom3d::pitch,players[0].health);
    I_Render3DScreenshotTo(pending.c_str());
    std::printf("Muzzle fire shot: %s\n",pending.c_str());
    ++shots;
}

void placeSoldiers() {
    mobj_t *me=players[0].mo;
    angle_t an=me->angle>>ANGLETOFINESHIFT,side=(me->angle+ANG90)>>ANGLETOFINESHIFT;
    const mobjtype_t types[3]={MT_POSSESSED,MT_SHOTGUY,MT_CHAINGUY};
    for(int i=0;i<3;++i) {
        fixed_t x=me->x+176*finecosine[an]+(i-1)*72*finecosine[side],y=me->y+176*finesine[an]+(i-1)*72*finesine[side];
        mobj_t *mo=P_SpawnMobj(x,y,ONFLOORZ,types[i]);
        mo->target=me;mo->angle=R_PointToAngle2(mo->x,mo->y,me->x,me->y);
        soldiers.push_back(mo);
    }
    // A fourth, nearer and off to the right, firing across the view at a marker on the left: side-on.
    fixed_t x=me->x+120*finecosine[an]-56*finecosine[side],y=me->y+120*finesine[an]-56*finesine[side];
    mobj_t *across=P_SpawnMobj(x,y,ONFLOORZ,MT_SHOTGUY);
    marker=P_SpawnMobj(me->x+120*finecosine[an]+400*finecosine[side],me->y+120*finesine[an]+400*finesine[side],ONFLOORZ,MT_TELEPORTMAN);
    soldiers.push_back(across);
}
// One at a time, so each shot shows its own fire and only its own light.
void fire(int i) {
    const statenum_t attack[4]={S_POSS_ATK2,S_SPOS_ATK2,S_CPOS_ATK2,S_SPOS_ATK2};
    soldiers[i]->target=i==3?marker:players[0].mo;
    P_SetMobjState(soldiers[i],attack[i]);
}
}

extern "C" void I_TestFrame(void) {
    if(gamestate!=GS_LEVEL||!players[consoleplayer].mo)return;
    if(leveltime==lastTic)return;
    lastTic=leveltime;
    if(start<0) {Game_SetAuthored(1);players[0].cheats|=CF_GODMODE;start=leveltime;return;}
    int t=leveltime-start;
    // Held still and level: their shots knock him about.
    static angle_t facing=players[0].mo->angle;
    doom3d::pitch=0;players[0].mo->momx=players[0].mo->momy=0;players[0].mo->angle=facing;
    // A screenshot is written a few frames after it is asked for, and a newer request replaces it:
    // each one gets its own shot, 20 tics apart, taken the tic after the trigger.
    const char *names[4]={"zombie","sergeant","chaingunner","side-on"};
    if(t==30)placeSoldiers();
    for(int i=0;i<4;++i) {
        if(t==40+20*i)fire(i);
        if(t==41+20*i)capture(names[i]);
    }
    if(t==108)capture("smoke");
    if(t==120) {
        for(mobj_t *mo:soldiers)P_RemoveMobj(mo);
        soldiers.clear();
        player_t *p=&players[0];
        p->weaponowned[wp_shotgun]=true;p->ammo[am_shell]=50;p->readyweapon=wp_shotgun;p->pendingweapon=wp_nochange;
        P_SetPsprite(p,ps_weapon,(statenum_t)weaponinfo[wp_shotgun].readystate);
    }
    if(t==140)P_SetPsprite(&players[0],ps_weapon,S_SGUN2);
    if(t==141) {
        const pspdef_t &f=players[0].psprites[ps_flash];
        if(f.state) {
            int lump=firstspritelump+sprites[f.state->sprite].spriteframes[f.state->frame&FF_FRAMEMASK].lump[0];
            const patch_t *patch=(const patch_t*)W_CacheLumpNum(lump,PU_CACHE);
            std::printf("  flash %.8s frame %d sx %.1f sy %.1f patch %dx%d left %d top %d; view y %d h %d\n",lumpinfo[lump].name,
                f.state->frame&FF_FRAMEMASK,f.sx/65536.0,f.sy/65536.0,SHORT(patch->width),SHORT(patch->height),
                SHORT(patch->leftoffset),SHORT(patch->topoffset),viewwindowy,viewheight);
        }
        capture("player");
    }
    // Two more of his own: each shot its own shape, turn and size.
    if(t==160)P_SetPsprite(&players[0],ps_weapon,S_SGUN2);
    if(t==161)capture("player-2");
    if(t==180)P_SetPsprite(&players[0],ps_weapon,S_SGUN2);
    if(t==181)capture("player-3");
    if(t==200)P_SetPsprite(&players[0],ps_weapon,S_SGUN2);
    if(t==208)capture("player-smoke");
    if(t>=230) {std::printf("Muzzle fire shots: %d\n",shots);I_Quit();}
}

int main(int argc,char **argv) {
    if(argc<3) {std::fprintf(stderr,"Usage: %s <iwad> <output-dir> [-slowmotionon]\n",argv[0]);return 1;}
    outputDir=argv[2];
    std::string graphics=outputDir+"/muzzle-graphics.cfg",config=outputDir+"/muzzle-doom.cfg";
    FILE *file=std::fopen(graphics.c_str(),"w");
    if(!file) {std::fprintf(stderr,"Cannot write %s; create the output directory first.\n",graphics.c_str());return 1;}
    // MUZZLE_FIRE=0 in the environment: the same shots without it, to compare.
    const char *fire=std::getenv("MUZZLE_FIRE");
    std::fprintf(file,"accelerated 1\nwidescreen 1\nmuzzle_fire %s\nlook 0\ncrosshair 0\nfps 0\n",fire&&*fire=='0'?"0":"1");
    std::fclose(file);
    file=std::fopen(config.c_str(),"w");
    if(!file)return 1;
    std::fputs("show_messages 0\n",file);std::fclose(file);
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER,"dummy");
    if(!SDL_Init(SDL_INIT_VIDEO))return 1;
    std::vector<std::string> args={"muzzle-fire-shot","-iwad",argv[1],"-config",config,"-graphics",graphics,
        "-nomonsters","-nomusic","-nosound","-width","1280","-height","720","-warp","1","1","-derpai","-hidden"};
    for(int i=3;i<argc;++i)args.emplace_back(argv[i]);
    std::vector<char*> pointers;
    for(auto &arg:args)pointers.push_back(arg.data());
    myargc=(int)pointers.size();myargv=pointers.data();
    singletics=true;
    D_DoomMain();
    return 0;
}
