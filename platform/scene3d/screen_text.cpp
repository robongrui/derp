/* Text running down dim monitors (Doom II's SPACEW3, the one texture
   wallImage's dim-screen rule finds): each screen is a terminal whose lines
   of green text scroll down, a texel at a time, at its own pace.

   The artwork paints a line or two of dark green text on each of its eight
   screens. Those are taken out of the engine's copy of the texture and
   lines of the same kind are written in their place: a text line is one
   texel row with an empty row under it, a few words of one to five texels
   with gaps, some a step dimmer, set by a hash of the screen and the line's
   number, so a line keeps its words all the way down. Inside a margin, so
   the bezel and the corner the tube's glare is painted in stay clear.

   Drawn on the CPU and uploaded: the texture is 64 by 128, a screen steps
   every 4 to 9 tics, and only when one has stepped is the top level sent
   again (the mip levels keep the painted text; at their distance a texel
   of text is not seen to move). No shader, no state on the GPU. It follows
   the tic, so it stops with the game, and F4 Live readouts switches it. */
#include "internal.h"
#include "../screen_glass.h"

namespace doom3d {
namespace {
struct Screen { int x,y,w,h,pace; uint32_t seed; };
struct Wall {
    int index,w,h;
    std::vector<byte> blank;     // 4 a texel as uploaded, the painted text taken out
    std::vector<byte> shown;
    std::vector<Screen> screens;
    byte bright,dim;             // palette entries of the text
    int step=-1;
    GpuTextureRef into;          // the texture last written: a new one is written at once
};
std::vector<Wall> walls;
constexpr int margin=2,linePitch=2;

uint32_t mix(uint32_t a,uint32_t b) {
    uint32_t h=a*2654435761u^b*40503u;
    h^=h>>13;h*=0x5bd1e995u;h^=h>>15;
    return h;
}
// Whether the texel column of a line is lit, and how: 0 dark, 1 dim, 2 bright.
int lineTexel(uint32_t line,int column,int width) {
    if((line&7u)==0u)return 0;                       // an empty line now and then
    int at=(int)((line>>3)&3u);                      // the indent
    for(int word=0;word<4&&at<width;++word) {
        uint32_t h=mix(line,(uint32_t)word+1);
        int length=1+(int)(h%5u),gap=1+(int)((h>>8)&1u);
        if(column>=at&&column<at+length&&column<width)return (h>>16)%4u?2:1;
        at+=length+gap;
        if(((h>>12)&3u)==0u)break;                   // a short line
    }
    return 0;
}
}

void screenTextRegister(int index,int w,int h,const byte *pixels,const byte *mask,const byte *glass,const byte *frame,const byte *palette) {
    walls.erase(std::remove_if(walls.begin(),walls.end(),[&](const Wall &o) {return o.index==index;}),walls.end());
    Wall wall;wall.index=index;wall.w=w;wall.h=h;
    wall.blank.resize((size_t)w*h*4);
    std::map<std::pair<int,int>,Screen> found;
    int brightest=-1;
    for(int y=0;y<h;++y)for(int x=0;x<w;++x) {
        size_t i=(size_t)y*w+x;
        byte *out=&wall.blank[4*i];
        out[0]=pixels[2*i];out[1]=pixels[2*i+1];out[2]=mask[i];out[3]=glass[i];
        if(glass[i]==DOOM_GLASS_NONE)continue;
        const byte *f=&frame[4*i];
        found.emplace(std::make_pair(x-f[0],y-f[1]),Screen{x-f[0],y-f[1],f[2],f[3],0,0});
        if(mask[i]) {
            const byte *c=palette+pixels[2*i]*3;
            int level=std::max({c[0],c[1],c[2]});
            if(level>brightest) {brightest=level;wall.bright=pixels[2*i];}
        }
    }
    if(brightest<0||found.empty())return;
    // The dimmer step: the palette's nearest to two thirds of the text's color.
    const byte *c=palette+wall.bright*3;long best=LONG_MAX;wall.dim=wall.bright;
    for(int p=0;p<256;++p) {
        const byte *q=palette+p*3;long d=0;
        for(int k=0;k<3;++k) {long e=q[k]-c[k]*2/3;d+=e*e;}
        if(d<best) {best=d;wall.dim=(byte)p;}
    }
    for(auto &[origin,screen]:found) {
        // Its glass without the text: the color most of it has.
        int uses[256]={},most=0;
        for(int y=0;y<screen.h;++y)for(int x=0;x<screen.w;++x) {
            size_t i=(size_t)(screen.y+y)*w+screen.x+x;
            if(!mask[i]&&++uses[pixels[2*i]]>uses[most])most=pixels[2*i];
        }
        for(int y=0;y<screen.h;++y)for(int x=0;x<screen.w;++x) {
            size_t i=(size_t)(screen.y+y)*w+screen.x+x;
            if(mask[i]) {wall.blank[4*i]=(byte)most;wall.blank[4*i+2]=0;}
        }
        screen.seed=mix((uint32_t)origin.first*131u+(uint32_t)origin.second,(uint32_t)index+7u);
        screen.pace=4+(int)(screen.seed%6u);
        wall.screens.push_back(screen);
    }
    walls.push_back(std::move(wall));
}

// Once a frame: the screens that have stepped are written anew.
void screenTextUpdate() {
    for(size_t n=0;n<walls.size();) {
        Wall &wall=walls[n];
        auto image=images.find(wall.index);
        if(image==images.end()||!image->second.texture) {walls.erase(walls.begin()+n);continue;}
        ++n;
        // Its step: the sum of the screens' own, so it changes when any of them moves.
        int step=0;
        if(settings.liveReadouts)for(const Screen &s:wall.screens)step+=leveltime/s.pace;
        if(step==wall.step&&wall.into==image->second.texture)continue;
        wall.step=step;wall.into=image->second.texture;
        wall.shown=wall.blank;
        for(const Screen &s:wall.screens) {
            int scrolled=settings.liveReadouts?leveltime/s.pace:0,width=s.w-2*margin;
            for(int y=margin;y<s.h-margin;++y) {
                int row=y-scrolled;               // the text moves down: a row shows what stood above it
                if(((row%linePitch)+linePitch)%linePitch)continue;
                uint32_t line=mix(s.seed,(uint32_t)(row/linePitch));
                for(int x=0;x<width;++x) {
                    int lit=lineTexel(line,x,width);
                    if(!lit)continue;
                    byte *out=&wall.shown[4*((size_t)(s.y+y)*wall.w+s.x+margin+x)];
                    out[0]=lit==2?wall.bright:wall.dim;out[2]=255;
                }
            }
        }
        gpuUpdateTexture(wall.into,GpuFormat::RGBA8,{{0,0,wall.w,wall.h}},wall.shown.data(),wall.w*4);
    }
}
void screenTextShutdown() {walls.clear();}
} // namespace doom3d
