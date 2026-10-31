/* The library's books on screen (Game_LibraryAim, Game_LibraryPage): the book
   the view points at outlined on its shelf, and an open book's page over the
   view, 80 characters by 40 lines in the game's small font, a whole page of
   Borges' size in the 640x400 panel image (frame.cpp), its real words red. */
#include "internal.h"
#include "../small_font.h"

#include <cctype>

namespace doom3d {
// The outline: four thin strips just in front of the shelf's face, pale for
// a book not yet read, dark red for one already opened.
void buildBookAim(const Uniforms &camera) {
    (void)camera;
    float c[4][3];int read=0;
    if(!Game_LibraryAim(c,&read))return;
    const std::array<float,3> color=read?std::array<float,3>{0.45f,0.12f,0.08f}:std::array<float,3>{0.85f,0.75f,0.45f};
    const float thick=0.75f;
    auto vertex=[&](float x,float y,float z) {Vertex v={x,y,z,0,0,1,0};v.red=color[0];v.green=color[1];v.blue=color[2];return v;};
    for(int e=0;e<4;++e) {
        const float *a=c[e],*b=c[(e+1)%4];
        bool across=a[2]==b[2];   // along the shelf (top, bottom), else up its side
        // Inward: down from the top, up from the bottom, in from a side (along y on a stack's
        // face, along x on the back wall).
        const float *o=c[(e+2)%4];
        auto inward=[&](float from,float to){return std::abs(to-from)<1e-3f?0.0f:thick*(to>from?1:-1);};
        float ox=across?0:inward(a[0],o[0]),oy=across?0:inward(a[1],o[1]),oz=across?inward(a[2],o[2]):0;
        quad(particleVertices,vertex(a[0],a[1],a[2]),vertex(b[0],b[1],b[2]),vertex(b[0]+ox,b[1]+oy,b[2]+oz),vertex(a[0]+ox,a[1]+oy,a[2]+oz));
    }
}
} // namespace doom3d

using namespace doom3d;
// A book open: its page in the panel image (frame.cpp), when F4's panel isn't.
static char pageLines[GAME_PAGE_LINES*(GAME_PAGE_LINE+1)],pageMarks[GAME_PAGE_LINES*(GAME_PAGE_LINE+1)];
int I_Render3DPageOpen(void) {return Game_LibraryPage(pageLines,pageMarks)>0;}
void I_Render3DPageDraw(unsigned *pixels) {
    const unsigned paper=0xf01a1612,ink=0xffcfc4a8,word=0xffe02818;
    // A clear band above and below the page: where the panel reaches the screen's top, the game's
    // messages (the page number, whose book it is) still show over the view there.
    const int margin=20,cellWidth=DOOM_SETTINGS_WIDTH/GAME_PAGE_LINE,cellHeight=(DOOM_SETTINGS_HEIGHT-2*margin)/GAME_PAGE_LINES;
    if(!Game_LibraryPage(pageLines,pageMarks))return;
    smallFont::load();
    for(int i=0;i<DOOM_SETTINGS_WIDTH*DOOM_SETTINGS_HEIGHT;++i) {
        int y=i/DOOM_SETTINGS_WIDTH;
        pixels[i]=y>=margin&&y<DOOM_SETTINGS_HEIGHT-margin?paper:0;
    }
    int top=margin+(cellHeight-smallFont::letterHeight())/2;
    for(int l=0;l<GAME_PAGE_LINES;++l)for(int i=0;i<GAME_PAGE_LINE;++i) {
        char ch=(char)std::toupper((unsigned char)pageLines[l*(GAME_PAGE_LINE+1)+i]);
        if(ch==' ')continue;
        std::string letter(1,ch);
        int x=i*cellWidth+(cellWidth-smallFont::width(letter))/2;
        smallFont::draw(pixels,DOOM_SETTINGS_WIDTH,DOOM_SETTINGS_HEIGHT,letter,x,l*cellHeight+top,pageMarks[l*(GAME_PAGE_LINE+1)+i]?word:ink);
    }
    // A clear edge all round: the panel is stretched from its edge pixels over the rest of the
    // screen, and there it must leave the view, the messages and the status bar as they are.
    for(int x=0;x<DOOM_SETTINGS_WIDTH;++x)pixels[x]=pixels[(DOOM_SETTINGS_HEIGHT-1)*DOOM_SETTINGS_WIDTH+x]=0;
    for(int y=0;y<DOOM_SETTINGS_HEIGHT;++y)pixels[y*DOOM_SETTINGS_WIDTH]=pixels[y*DOOM_SETTINGS_WIDTH+DOOM_SETTINGS_WIDTH-1]=0;
}
