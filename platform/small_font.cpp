/* The game's small font as brightness masks (small_font.h). */
#include "small_font.h"
#include <algorithm>
#include <cctype>
#include <vector>
extern "C" {
#include "doomtype.h"
#include "w_wad.h"
#include "z_zone.h"
}
#include "game_profile.h"

namespace smallFont {
namespace {
struct Glyph { int width=0,height=0,left=0,top=0;std::vector<unsigned char> shade; };
const int firstGlyph='!',lastGlyph='_',spaceWidth=4;
int tallest=7;
std::vector<Glyph> glyphs;

const Glyph *glyphFor(char c) {
    int upper=std::toupper((unsigned char)c);
    if(upper<firstGlyph||upper>lastGlyph)return nullptr;
    const Glyph &glyph=glyphs[(size_t)(upper-firstGlyph)];
    return glyph.width?&glyph:nullptr;
}

unsigned shaded(unsigned color,int shade) {
    unsigned a=(color>>16&255)*shade/255,b=(color>>8&255)*shade/255,c=(color&255)*shade/255;
    return (color&0xff000000)|a<<16|b<<8|c;
}
}

void load() {
    if(!glyphs.empty())return;
    glyphs.resize(lastGlyph-firstGlyph+1);
    const byte *palette=(const byte*)W_CacheLumpName((char*)"PLAYPAL",PU_CACHE);
    std::vector<int> brightness(256);
    for(int i=0;i<256;++i)brightness[i]=palette[3*i]*299+palette[3*i+1]*587+palette[3*i+2]*114;
    std::vector<std::vector<int>> raw(glyphs.size());
    int brightest=1,letters=0;
    for(int c=firstGlyph;c<=lastGlyph;++c) {
        int lump=Game_FontLump(c);
        if(lump<0)continue;
        const byte *data=(const byte*)W_CacheLumpNum(lump,PU_CACHE);
        int size=W_LumpLength(lump);
        if(size<8)continue;
        Glyph &glyph=glyphs[(size_t)(c-firstGlyph)];
        int w=(short)(data[0]|data[1]<<8),h=(short)(data[2]|data[3]<<8);
        if(w<=0||h<=0||w>32||h>32||8+4*w>size)continue;
        std::vector<int> &pixels=raw[(size_t)(c-firstGlyph)];
        pixels.assign((size_t)(w*h),-1);
        // Patch columns: posts of (top, length, pad, pixels, pad) up to 0xff.
        for(int x=0;x<w;++x) {
            int at=data[8+4*x]|data[9+4*x]<<8|data[10+4*x]<<16|data[11+4*x]<<24;
            while(at>=0&&at<size&&data[at]!=0xff) {
                if(at+2>=size)break;
                int top=data[at],length=data[at+1];
                if(at+3+length>size)break;
                for(int y=0;y<length;++y)if(top+y<h) {
                    int index=data[at+3+y];
                    pixels[(size_t)((top+y)*w+x)]=index;
                    brightest=std::max(brightest,brightness[(size_t)index]);
                }
                at+=length+4;
            }
        }
        glyph.width=w;glyph.height=h;
        if(c>='A'&&c<='Z')letters=std::max(letters,h);
        glyph.left=(short)(data[4]|data[5]<<8);glyph.top=(short)(data[6]|data[7]<<8);
    }
    if(letters)tallest=letters;
    for(size_t i=0;i<glyphs.size();++i) {
        Glyph &glyph=glyphs[i];
        glyph.shade.resize(raw[i].size());
        for(size_t p=0;p<raw[i].size();++p)
            glyph.shade[p]=raw[i][p]<0?0:(unsigned char)std::max(1,brightness[(size_t)raw[i][p]]*255/brightest);
    }
}

int letterHeight() {return tallest;}

int width(const std::string &text) {
    int w=0;
    for(char c:text) {const Glyph *glyph=glyphFor(c);w+=glyph?glyph->width+Game_FontSpacing():spaceWidth;}
    return w;
}

int draw(unsigned *image,int imageWidth,int imageHeight,const std::string &text,int x,int top,unsigned color) {
    for(char c:text) {
        const Glyph *glyph=glyphFor(c);
        if(!glyph) {x+=spaceWidth;continue;}
        int left=x-glyph->left,y0=top-glyph->top;
        for(int y=0;y<glyph->height;++y)
            for(int i=0;i<glyph->width;++i) {
                int shade=glyph->shade[(size_t)(y*glyph->width+i)],px=left+i,py=y0+y;
                if(shade&&px>=0&&px<imageWidth&&py>=0&&py<imageHeight)image[py*imageWidth+px]=shaded(color,shade);
            }
        x+=glyph->width+Game_FontSpacing();
    }
    return x;
}
}
