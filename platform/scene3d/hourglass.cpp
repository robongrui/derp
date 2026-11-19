/* The hourglass beside the status bar: Authored's slow-motion meter (p_focus.c, PLAN-shootslide.md).
   Drawn in DOOM pixels from the WAD's own palette, just left of the classic bar: on the wide bar's
   side, or beside a 4:3 bar over the view. The sand in the top bulb is the meter, with a notch cut
   into the frame for each charge; while slow motion runs it down by hand a thin stream falls through
   the neck. A shootslide drops a charge's worth at once, a kill puts one back. Repainted only when
   what it shows changes (present.frag lays it, overlay_cover.glsl). */
#include "internal.h"

namespace doom3d {
namespace {
constexpr int glassW=13,glassH=28;
constexpr int glassLeft=-(glassW+3),glassTop=2;   // DOOM pixels from the bar's top left
constexpr int neckX=6;
// Half the glass's inside width, row by row from the top bulb's first row (2) to the neck (13);
// the bottom bulb mirrors it.
constexpr int upper[12]={3,4,4,4,4,4,3,3,2,1,0,0};
int halfWidth(int y) {
    if(y>=2&&y<=13)return upper[y-2];
    if(y>=14&&y<=25)return upper[25-y];
    return -1;
}
// Rows that hold sand (the neck's single pixels only carry the stream).
bool topBulb(int y) {return y>=2&&y<=11;}
bool bottomBulb(int y) {return y>=16&&y<=25;}
int capacity() {int n=0;for(int y=2;y<=11;++y)n+=2*halfWidth(y)+1;return n;}

GpuTextureRef texture;
unsigned shownKey=0;
bool made=false;

unsigned nearest(const unsigned *palette,int r,int g,int b) {
    int best=0,bestD=1<<30;
    for(int i=0;i<256;++i) {
        int pr=palette[i]>>16&255,pg=palette[i]>>8&255,pb=palette[i]&255;
        int d=(pr-r)*(pr-r)*3+(pg-g)*(pg-g)*4+(pb-b)*(pb-b)*2;
        if(d<bestD) {bestD=d;best=i;}
    }
    return palette[best];
}

// Which inside pixels of a bulb are sand: the top bulb drains from the middle (a funnel), the bottom
// one piles up in the middle (a mound). count: how many, filling from the neck's side outward.
void fillBulb(std::vector<char> &sand,bool top,int count) {
    for(int k=0;k<12&&count>0;++k) {
        int y=top?11-k:25-k;
        if(top?!topBulb(y):!bottomBulb(y))continue;
        int hw=halfWidth(y),width=2*hw+1;
        if(count>=width) {for(int x=neckX-hw;x<=neckX+hw;++x)sand[(size_t)y*glassW+x]=1;count-=width;continue;}
        // A row part full: the top bulb keeps its sides (the middle runs first), the bottom one its middle.
        for(int i=0;i<count;++i) {
            int x=top?((i&1)?neckX+hw-i/2:neckX-hw+i/2):((i&1)?neckX-(i+1)/2:neckX+(i+1)/2);
            if(x>=neckX-hw&&x<=neckX+hw)sand[(size_t)y*glassW+x]=1;
        }
        count=0;
    }
}

void paintGlass(const unsigned *palette,int fill,int most,bool flowing) {
    const unsigned woodLight=nearest(palette,150,100,56),wood=nearest(palette,108,70,40),woodDark=nearest(palette,66,42,24);
    const unsigned edge=nearest(palette,150,158,170),empty=nearest(palette,26,26,32);
    const unsigned shine=nearest(palette,64,66,78),sand=nearest(palette,204,150,72),grain=nearest(palette,168,118,52);
    int cap=capacity(),top=most>0?(int)std::lround((double)cap*std::clamp(fill,0,most)/most):0;
    std::vector<char> sandAt((size_t)glassW*glassH,0);
    fillBulb(sandAt,true,top);
    fillBulb(sandAt,false,cap-top);
    std::vector<unsigned> image((size_t)glassW*glassH,0);
    auto put=[&](int x,int y,unsigned c) {if(x>=0&&x<glassW&&y>=0&&y<glassH)image[(size_t)y*glassW+x]=c|0xff000000u;};
    // The caps and the two posts.
    for(int x=0;x<glassW;++x) {put(x,0,woodLight);put(x,1,wood);put(x,26,wood);put(x,27,woodDark);}
    for(int y=2;y<=25;++y) {put(0,y,woodDark);put(glassW-1,y,wood);}
    // The glass: its rim, its inside (sand, or the dark of the empty glass with a glint down its left).
    for(int y=2;y<=25;++y) {
        int hw=halfWidth(y);
        put(neckX-hw-1,y,edge);put(neckX+hw+1,y,edge);
        for(int x=neckX-hw;x<=neckX+hw;++x) {
            bool grainy=((x*3+y*5)&3)==0;
            put(x,y,sandAt[(size_t)y*glassW+x]?(grainy?grain:sand):x==neckX-hw&&hw>0?shine:empty);
        }
    }
    // The stream: through the neck and down to the pile, a pixel on, a pixel off, moving each tic.
    if(flowing&&top>0) {
        int pile=26;
        for(int y=16;y<=25;++y) if(sandAt[(size_t)y*glassW+neckX]) {pile=y;break;}
        for(int y=12;y<pile;++y) if(y<=15||((y+leveltime)&1)) put(neckX,y,sand);
    }
    // A notch cut into the right post at each charge's level in the top bulb (dark wood: a brass mark
    // there read as sand that had got out of the glass).
    int charges=std::max(1,most/48);   // a charge is 48 tics (p_focus.h's FOCUS_CHARGE)
    for(int c=1;c<charges;++c) {
        int want=(int)std::lround((double)cap*c/charges),seen=0;
        for(int y=11;y>=2;--y) {
            seen+=2*halfWidth(y)+1;
            if(seen>=want) {put(glassW-1,y,woodDark);break;}
        }
    }
    std::vector<byte> rgba(image.size()*4);
    for(size_t i=0;i<image.size();++i) {
        unsigned c=image[i];
        rgba[4*i]=(byte)(c>>16);rgba[4*i+1]=(byte)(c>>8);rgba[4*i+2]=(byte)c;rgba[4*i+3]=(byte)(c>>24);
    }
    if(!texture)texture=gpuCreateTexture(GpuFormat::RGBA8,glassW,glassH,rgba.data(),glassW*4);
    else gpuUpdateTexture(texture,GpuFormat::RGBA8,{{0,0,glassW,glassH}},rgba.data(),glassW*4);
}
}

// Fills the draw's hourglass: its place in DOOM pixels from the bar's top left and its size
// (uniform 12-15; no size: not shown) and its picture.
void hourglassDraw(StatusBarDraw &draw,const unsigned *palette) {
    int fill=0,most=0,flowing=0;
    if(!Game_SlowGlass(&fill,&most,&flowing)||Game_StatusBarTop()<=0)return;
    unsigned key=2166136261u;
    for(int i=0;i<256;++i)key=(key^palette[i])*16777619u;
    for(int v:{fill,most,flowing,flowing?(int)(leveltime&1):0})key=(key^(unsigned)v)*16777619u;
    if(!made||key!=shownKey) {paintGlass(palette,fill,most,flowing!=0);shownKey=key;made=true;}
    float *u=draw.uniform;
    u[12]=(float)glassLeft;u[13]=(float)glassTop;u[14]=(float)glassW;u[15]=(float)glassH;
    draw.glassTexture=texture;
}
} // namespace doom3d
