/* The status bar drawn apart from the rest of the 2D layer (F4 Status bar size and width).
   DOOM draws everything 2D into one 320x200 picture, which the renderer lays over a 4:3 frame
   as tall as the screen: on a 4K screen the bar comes out 346 pixels tall. Apart, the bar's
   rows of that picture get a place of their own at the bottom: smaller, at whole screen pixels
   per DOOM pixel so its pixels stay even, and across the whole width if asked, with the wide
   STBAR's sides (the newer IWADs' 576-column bar) and DOOM's border flat beyond them. The rest
   of the picture (the view's frame, messages, menus, the automap) is laid so its rows above the
   bar fill the screen above the bar, as the classic frame does with a bar of its own size.
   Only while nothing else is drawn over the bar's rows (Game_StatusBarTop).
   All of it is laid out in the screen's own pixels, not the render's: the present pass draws
   the 2D layer at the screen's resolution (gpu3d/frame.cpp, present.frag), so a 1080p render
   upscaled to 4K shows the same bar as a native 4K one, as crisp. */
#include "internal.h"

namespace doom3d {
void hourglassDraw(StatusBarDraw &draw,const unsigned *palette);   // hourglass.cpp
static float drop=0;
static int screenW=0,screenH=0;
float weaponDrop() {return drop;}
void setScreenSize(int width,int height) {screenW=width;screenH=height;}

ScreenLayout screenLayout(int w,int h) {
    ScreenLayout l;
    // In the screen's pixels; the frame goes back to the render's at the end, for the 3D view and the weapon.
    float sw=screenW>0?(float)screenW:(float)w,sh=screenH>0?(float)screenH:(float)h;
    l.uiWidth=std::min(sw,sh*4.0f/3);l.uiHeight=l.uiWidth*0.75f;l.uiX=(sw-l.uiWidth)/2;
    l.uiY=worldPending&&settings.widescreen?sh-l.uiHeight:(sh-l.uiHeight)/2;
    drop=0;
    bool changed=settings.hudSize<100||settings.hudWide;
    int top=changed?Game_StatusBarTop():0;
    StatusBarPlace &bar=l.bar;
    bar.screenW=sw;bar.screenH=sh;
    if(top>0&&top<SCREENHEIGHT) {
        // DOOM's pixels stand 1.2 times taller than wide: the classic frame's are, and so are the bar's.
        float classic=l.uiWidth/SCREENWIDTH;
        if(settings.hudSize>=100) {bar.scaleX=classic;bar.scaleY=classic*1.2f;}
        else {
            bar.scaleX=std::max(1.0f,std::round(classic*settings.hudSize/100));
            bar.scaleY=std::max(1.0f,std::round(bar.scaleX*1.2f));
        }
        bar.apart=true;bar.top=top;
        bar.w=SCREENWIDTH*bar.scaleX;bar.h=(SCREENHEIGHT-top)*bar.scaleY;
        bar.x=std::floor((sw-bar.w)/2);bar.y=sh-bar.h;
        bar.left=settings.hudWide?0:bar.x;bar.right=settings.hudWide?sw:bar.x+bar.w;
        // The frame: its rows above the bar's fill the screen above the bar (narrower screens: as wide as
        // the screen, its bottom on the bar).
        float above=bar.y;
        l.uiHeight=above*SCREENHEIGHT/top;l.uiWidth=l.uiHeight*4/3;
        if(l.uiWidth>sw) {l.uiWidth=sw;l.uiHeight=l.uiWidth*0.75f;}
        l.uiX=(sw-l.uiWidth)/2;l.uiY=above-l.uiHeight*top/SCREENHEIGHT;
        // The weapon sits on the view's middle as in DOOM, its bottom hidden under the bar. Beside a bar
        // narrower than the screen nothing hides it: it goes down until it reaches the screen's bottom, as
        // DOOM's full-screen view has it (never further: a gun at rest ends there).
        if(!settings.hudWide) {
            float bottom=(sh-l.uiY)/l.uiHeight*SCREENHEIGHT;   // the screen's bottom, in the frame's rows
            drop=std::max(0.0f,bottom-(viewwindowy+viewheight/2.0f+SCREENHEIGHT/2.0f));
        }
    }
    float toX=w/sw,toY=h/sh;
    l.uiX*=toX;l.uiWidth*=toX;l.uiY*=toY;l.uiHeight*=toY;
    return l;
}

// The bar's sides. With a wide STBAR (the newer IWADs) its own rock goes on outward, mirrored back
// and forth so no seam shows (present.frag); without one, the game's border flat (Game_BorderFlat:
// FLOOR7_2 in DOOM, GRNROCK in DOOM II), its colour taken out and the bar's put in at the bar's
// brightness: DOOM 1's olive rock read as moss beside the grey bar. As palette indices, painted
// again for a new palette (the gamma) or new WADs.
namespace {
struct Side { int lump=-1,width=0,height=0; std::vector<byte> index,cover; GpuTextureRef texture; };
Side statusBar,borderFlat;
unsigned paletteKey=0;
void loadPatch(Side &side,int lump) {
    side=Side{};side.lump=lump;
    if(lump<0)return;
    const patch_t *patch=(const patch_t*)W_CacheLumpNum(lump,PU_CACHE);
    int w=SHORT(patch->width),h=SHORT(patch->height);
    if(w<=0||h<=0)return;
    side.width=w;side.height=h;side.index.assign((size_t)w*h,0);side.cover.assign((size_t)w*h,0);
    for(int x=0;x<w;++x) {
        const column_t *column=(const column_t*)((const byte*)patch+LONG(patch->columnofs[x]));
        while(column->topdelta!=0xff) {
            const byte *source=(const byte*)column+3;
            for(int k=0;k<column->length;++k) {
                int y=column->topdelta+k;
                if(y<h) {side.index[(size_t)y*w+x]=source[k];side.cover[(size_t)y*w+x]=255;}
            }
            column=(const column_t*)((const byte*)column+column->length+4);
        }
    }
}
void loadFlat(Side &side,int lump) {
    side=Side{};side.lump=lump;
    if(lump<0||W_LumpLength(lump)<64*64)return;
    const byte *flat=(const byte*)W_CacheLumpNum(lump,PU_CACHE);
    side.width=side.height=64;side.index.assign(flat,flat+64*64);side.cover.assign(64*64,255);
}
float luma(const float c[3]) {return 0.299f*c[0]+0.587f*c[1]+0.114f*c[2];}
void colourOf(unsigned packed,float c[3]) {c[0]=(packed>>16&255)/255.0f;c[1]=(packed>>8&255)/255.0f;c[2]=(packed&255)/255.0f;}
// A side's average colour over what it covers.
void meanColour(const Side &side,const unsigned *palette,float mean[3]) {
    double sum[3]={};size_t n=0;
    for(size_t i=0;i<side.index.size();++i) if(side.cover[i]) {
        float c[3];colourOf(palette[side.index[i]],c);
        for(int k=0;k<3;++k)sum[k]+=c[k];
        ++n;
    }
    for(int k=0;k<3;++k)mean[k]=n?(float)(sum[k]/n):0.5f;
}
// tint: each texel's brightness against the side's own mean, times this colour (null: as drawn).
void paint(Side &side,const unsigned *palette,const float *tint=nullptr) {
    side.texture=nullptr;
    if(side.index.empty())return;
    float mean[3],level=1;
    if(tint) {meanColour(side,palette,mean);level=std::max(luma(mean),1e-3f);}
    std::vector<byte> rgba(side.index.size()*4);
    for(size_t i=0;i<side.index.size();++i) {
        float c[3];colourOf(palette[side.index[i]],c);
        if(tint) {float l=luma(c)/level;for(int k=0;k<3;++k)c[k]=tint[k]*l;}
        for(int k=0;k<3;++k)rgba[4*i+k]=(byte)std::lround(std::clamp(c[k],0.0f,1.0f)*255);
        rgba[4*i+3]=side.cover[i];
    }
    side.texture=gpuCreateTexture(GpuFormat::RGBA8,side.width,side.height,rgba.data(),side.width*4);
}
bool wide() {return statusBar.width>SCREENWIDTH;}
void updateSides(const unsigned *palette) {
    int bar=W_CheckNumForName((char*)"STBAR"),flat=W_CheckNumForName((char*)Game_BorderFlat());
    bool fresh=false;
    if(bar!=statusBar.lump) {loadPatch(statusBar,bar);fresh=true;}
    if(flat!=borderFlat.lump) {loadFlat(borderFlat,flat);fresh=true;}
    unsigned key=2166136261u;
    for(int i=0;i<256;++i)key=(key^palette[i])*16777619u;
    if(!fresh&&key==paletteKey)return;
    paletteKey=key;statusBar.texture=borderFlat.texture=nullptr;
    if(wide())paint(statusBar,palette);
    else if(!borderFlat.index.empty()) {
        float tint[3]={0.5f,0.5f,0.5f};
        if(!statusBar.index.empty())meanColour(statusBar,palette,tint);
        paint(borderFlat,palette,tint);
    }
}
}

StatusBarDraw statusBarDraw(const FrameView &view,const unsigned *palette) {
    StatusBarDraw draw;
    const StatusBarPlace &bar=view.bar;
    float sw=std::max(1.0f,bar.screenW),sh=std::max(1.0f,bar.screenH);
    float *u=draw.uniform;
    u[0]=bar.x/sw;u[1]=bar.y/sh;u[2]=bar.w/sw;u[3]=bar.h/sh;
    u[4]=bar.left/sw;u[5]=bar.right/sw;u[6]=bar.apart?1.0f:0.0f;u[7]=(float)bar.top/SCREENHEIGHT;
    hourglassDraw(draw,palette);   // Authored's slow-motion meter, beside the bar however it is laid
    if(!bar.apart||(bar.left>=bar.x&&bar.right<=bar.x+bar.w))return draw;
    updateSides(palette);
    if(statusBar.texture) {
        // The wide STBAR centred on the bar, at its scale; its sides go on past its ends (present.frag).
        float extra=(statusBar.width-SCREENWIDTH)/2.0f*bar.scaleX;
        u[8]=(bar.x-extra)/sw;u[9]=statusBar.width*bar.scaleX/sw;
        draw.wideTexture=statusBar.texture;
    }
    else if(borderFlat.texture) {
        // The flat's 64 DOOM pixels square at the bar's scale, lined up with the bar: repeats per screen uv.
        u[10]=sw/(64*bar.scaleX);u[11]=sh/(64*bar.scaleY);
        draw.flatTexture=borderFlat.texture;
    }
    return draw;
}
} // namespace doom3d
