/* Capture an actual window before, during and after its break on the GPU.
   doom-glass-smoke <iwad> <output-dir> [warp-map | episode map]
   The output directory must exist. Uses its own configs and no desktop input. */
#include <algorithm>
#include <cmath>
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
#include "r_sky.h"
#include "r_state.h"
}
#include "../platform/scene3d.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

namespace {
struct Fixture {
    line_t *line=nullptr;
    float x=0,y=0,bottom=0,top=0,width=0;
    float cameraX=0,cameraY=0,cameraFloor=0;
};
Fixture fixture;
std::string outputDir,pendingCapture;
int stage=0,settled=0,placedAt=0,brokenAt=0,nextCapture=0,captures=0,lastTic=-1;
const int captureTics[]={0,2,5,10,20,40};
float sightStartZ,sightEndZ;

fixed_t fixed(float n) {return (fixed_t)std::lround(n*FRACUNIT);}
float units(fixed_t n) {return n/(float)FRACUNIT;}

boolean clearSight(intercept_t *hit) {
    const line_t *line=hit->d.line;
    if(!line->frontsector||!line->backsector)return false;
    float z=sightStartZ+(sightEndZ-sightStartZ)*units(hit->frac);
    float low=std::max(units(line->frontsector->floorheight),units(line->backsector->floorheight));
    float high=std::min(units(line->frontsector->ceilingheight),units(line->backsector->ceilingheight));
    return z>low+1&&z<high-1;
}

bool standingView(float x,float y,float targetX,float targetY,float targetZ,float &floor) {
    mobj_t *mo=players[consoleplayer].mo;
    int flags=mo->flags;
    mo->flags&=~MF_PICKUP;
    bool fits=P_CheckPosition(mo,fixed(x),fixed(y));
    mo->flags=flags;
    if(!fits||tmceilingz-tmfloorz<mo->height)return false;
    floor=units(tmfloorz);
    sightStartZ=std::min(floor+units(VIEWHEIGHT),units(tmceilingz)-4);
    sightEndZ=targetZ;
    return P_PathTraverse(fixed(x),fixed(y),fixed(targetX),fixed(targetY),PT_ADDLINES,clearSight);
}

bool frameMatches(const sector_t *s,float bottom,float top) {
    return s&&std::abs(units(s->floorheight)-bottom)<0.1f&&std::abs(units(s->ceilingheight)-top)<0.1f;
}

uint32_t paneSeed(int line) {
    // The pane's stable identifier resolves equally near front/back frame lines.
    uint32_t n=(uint32_t)line*2654435761u+7;
    n^=n>>16;n*=0x7feb352du;n^=n>>15;n*=0x846ca68bu;n^=n>>16;
    return n&0x7fffffffu;
}

bool findFixture() {
    // Unbroken panes are six vertices; no flying shards exist yet.
    const auto &vertices=doom3d::glassVertices;
    bool found=false;
    float bestArea=0;
    for(size_t first=0;first+5<vertices.size();first+=6) {
        const auto &a=vertices[first],&b=vertices[first+1];
        if(a.statics&0x80000000u)continue;
        float dx=b.x-a.x,dy=b.y-a.y,width=std::hypot(dx,dy);
        if(width<32)continue;
        float bottom=a.z,top=a.z;
        for(size_t i=first;i<first+6;++i) {bottom=std::min(bottom,vertices[i].z);top=std::max(top,vertices[i].z);}
        if(top-bottom<32||width*(top-bottom)<=bestArea)continue;
        dx/=width;dy/=width;
        float x=(a.x+b.x)/2,y=(a.y+b.y)/2;
        line_t *original=nullptr;
        float nearest=49;
        for(int i=0;i<numlines;++i) {
            line_t &line=lines[i];
            if(paneSeed(i)!=(a.statics&0x7fffffffu)||!line.frontsector||!line.backsector||
               (!frameMatches(line.frontsector,bottom,top)&&!frameMatches(line.backsector,bottom,top)))continue;
            float ax=units(line.v1->x),ay=units(line.v1->y),bx=units(line.v2->x),by=units(line.v2->y);
            float length=std::hypot(bx-ax,by-ay);
            if(std::abs(length-width)>0.1f||length<1)continue;
            if(std::abs(((bx-ax)*dx+(by-ay)*dy)/length)<0.999f)continue;
            if(std::abs(((ax+bx)/2-x)*dx+((ay+by)/2-y)*dy)>0.1f)continue;
            float distance=std::abs((ax-x)*dy-(ay-y)*dx);
            if(distance<=48&&distance<nearest) {nearest=distance;original=&line;}
        }
        if(!original)continue;
        for(float distance:{120.0f,160.0f,96.0f,192.0f})for(float side:{1.0f,-1.0f}) {
            float cameraX=x+dy*distance*side,cameraY=y-dx*distance*side,floor;
            if(!standingView(cameraX,cameraY,x,y,(bottom+top)/2,floor))continue;
            if(width*(top-bottom)>bestArea) {
                fixture={original,x,y,bottom,top,width,cameraX,cameraY,floor};
                bestArea=width*(top-bottom);found=true;
            }
        }
    }
    return found;
}

void placeCamera(bool ground=false) {
    player_t &player=players[consoleplayer];
    mobj_t *mo=player.mo;
    P_UnsetThingPosition(mo);
    mo->x=fixed(fixture.cameraX);mo->y=fixed(fixture.cameraY);
    P_SetThingPosition(mo);
    mo->floorz=mo->subsector->sector->floorheight;mo->ceilingz=mo->subsector->sector->ceilingheight;
    mo->z=mo->floorz;mo->momx=mo->momy=mo->momz=0;
    mo->angle=R_PointToAngle2(mo->x,mo->y,fixed(fixture.x),fixed(fixture.y));
    player.viewheight=VIEWHEIGHT;player.deltaviewheight=0;
    player.viewz=std::min(mo->z+VIEWHEIGHT,mo->ceilingz-4*FRACUNIT);
    player.damagecount=player.bonuscount=0;player.message=nullptr;
    float distance=std::hypot(fixture.x-fixture.cameraX,fixture.y-fixture.cameraY);
    float targetZ=ground?fixture.cameraFloor+1:(fixture.bottom+fixture.top)/2;
    doom3d::pitch=std::clamp(std::atan2(targetZ-units(player.viewz),distance*(ground?0.7f:1.0f)),-1.2f,1.2f);
    tourpitch=doom3d::pitch;
}

void capture(const char *name) {
    pendingCapture=outputDir+"/"+name;
    I_Render3DScreenshotTo(pendingCapture.c_str());
    ++captures;
}

void checkCapture() {
    if(pendingCapture.empty())return;
    FILE *file=std::fopen(pendingCapture.c_str(),"rb");
    unsigned char signature[8]={};
    bool valid=file&&std::fread(signature,1,sizeof(signature),file)==sizeof(signature)&&
        signature[0]==137&&signature[1]=='P'&&signature[2]=='N'&&signature[3]=='G';
    if(file)std::fclose(file);
    if(!valid)I_Error((char*)"Glass smoke: screenshot was not saved: %s",pendingCapture.c_str());
    pendingCapture.clear();
}
}

extern "C" void I_TestFrame(void) {
    if(gamestate!=GS_LEVEL||!players[consoleplayer].mo)return;
    checkCapture();
    // The single-tic loop bypasses TryRunTics, which normally calls this hook.
    // Call it once per tic here so render-only glass physics still advances.
    if(leveltime==lastTic)return;
    lastTic=leveltime;
    if(R_BeforeTic)R_BeforeTic();
    if(!I_Render3DIsAccelerated()||!R_ShotPassed)
        I_Error((char*)"Glass smoke: the GPU renderer is unavailable");
    if(stage==0) {
        if(++settled<8)return;
        if(!findFixture())I_Error((char*)"Glass smoke: no window has a clear standing view; choose another map");
        std::printf("Glass fixture: line %td, %.0fx%.0f pane, camera %.1f,%.1f\n",
            fixture.line-lines,fixture.width,fixture.top-fixture.bottom,fixture.cameraX,fixture.cameraY);
        I_Render3DProfileTeleported();
        placedAt=leveltime;stage=1;
    }
    placeCamera(stage==4);
    if(stage==1&&leveltime-placedAt>=12) {
        capture("glass-intact.png");stage=2;return;
    }
    if(stage==2) {
        float dx=fixture.x-fixture.cameraX,dy=fixture.y-fixture.cameraY;
        float length=std::hypot(dx,dy);
        R_ShotPassed(fixture.line,fixed(fixture.x),fixed(fixture.y),fixed((fixture.bottom+fixture.top)/2),fixed(dx/length),fixed(dy/length));
        brokenAt=leveltime;stage=3;
    }
    if(stage==3) {
        int elapsed=leveltime-brokenAt;
        if(elapsed==1) {
            uint32_t brokenSeed=paneSeed((int)(fixture.line-lines))|0x80000000u;
            bool broken=false;
            for(const auto &vertex:doom3d::glassVertices)if(vertex.statics==brokenSeed) {broken=true;break;}
            if(!broken)I_Error((char*)"Glass smoke: the fixture shot did not break its pane");
        }
        if(nextCapture<(int)(sizeof(captureTics)/sizeof(captureTics[0]))&&elapsed==captureTics[nextCapture]) {
            char name[64];std::snprintf(name,sizeof(name),"glass-tic-%02d.png",elapsed);
            capture(name);++nextCapture;
        }
        if(elapsed>=90) {placeCamera(true);capture("glass-ground.png");stage=4;}
        return;
    }
    if(stage==4) {
        std::printf("Glass GPU capture passed: %d screenshots in %s.\n",captures,outputDir.c_str());
        I_Quit();
    }
}

int main(int argc,char **argv) {
    if(argc<3||argc>5) {
        std::fprintf(stderr,"Usage: %s <iwad> <output-dir> [warp-map | episode map]\n",argv[0]);
        return 1;
    }
    outputDir=argv[2];
    std::string graphics=outputDir+"/graphics.cfg",config=outputDir+"/doom.cfg";
    FILE *file=std::fopen(graphics.c_str(),"w");
    if(!file) {std::fprintf(stderr,"Cannot write %s; create the output directory first.\n",graphics.c_str());return 1;}
    std::fputs("accelerated 1\nwindow_glass 1\nglass_reflections 1\neye_adaptation 0\ncrosshair 0\nfps 0\ndust_motes 0\nembers 0\n",file);
    std::fclose(file);
    file=std::fopen(config.c_str(),"w");
    if(!file)return 1;
    std::fputs("show_messages 0\n",file);std::fclose(file);
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER,"dummy");
    if(!SDL_Init(SDL_INIT_VIDEO))return 1;
    std::vector<std::string> args={"glass-smoke","-iwad",argv[1],"-config",config,"-graphics",graphics,
        "-nomonsters","-nomusic","-nosound","-width","960","-height","600","-warp"};
    if(argc==3) {args.push_back("1");args.push_back("1");}
    else for(int i=3;i<argc;++i)args.emplace_back(argv[i]);
    std::vector<char*> pointers;
    for(auto &arg:args)pointers.push_back(arg.data());
    myargc=(int)pointers.size();myargv=pointers.data();
    singletics=true;
    D_DoomMain();
    return 0;
}
