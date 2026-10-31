/* What the library's people say (Game_LibrarySays): a line over the head of one who speaks,
   in the game's small font made grey, each letter its own quad turned to the eye, lit by nothing (it is
   read in the dark), fogged as anything there is, its letters coming and going a few at a time.
   Small but readable from a few galleries off;
   drawn only near him. Drawing only. */
#include "internal.h"

namespace doom3d {
static constexpr float speechReach=900;   // map units: no farther than this
static constexpr float speechPixel=1.2f;  // map units to a pixel of the font
static constexpr int speechSpace=4;       // pixels to a space (small_font.cpp)
static constexpr int speechKeyBase=-0x58000000;   // image keys clear of walls, lumps, blood, puddles, cards
static constexpr int speechWhite=200;     // the brightest grey a letter gets (0-255)

// A letter in grey: the font's own (red in DOOM) with each pixel's brightness, against its
// brightest, set on the palette's nearest grey. Made once a letter.
static int greyLetter(int lump) {
    static std::map<int,int> made;
    auto found=made.find(lump);
    if(found!=made.end()&&images.count(found->second))return found->second;
    const Image &font=lumpImage(lump,false);
    const byte *pal=playpal();
    std::vector<int> greys;
    for(int i=0;i<256;++i) {
        int r=pal[3*i],g=pal[3*i+1],b=pal[3*i+2];
        if(std::abs(r-g)<8&&std::abs(g-b)<8&&r<=speechWhite+8)greys.push_back(i);
    }
    auto bright=[&](int i){return pal[3*i]*0.3f+pal[3*i+1]*0.59f+pal[3*i+2]*0.11f;};
    float brightest=1;
    for(size_t n=0;n<font.pixels.size()/2;++n)if(font.pixels[2*n+1])brightest=std::max(brightest,bright(font.pixels[2*n]));
    std::vector<byte> pixels(font.pixels);
    for(size_t n=0;n<pixels.size()/2;++n) {
        if(!pixels[2*n+1])continue;
        float want=bright(pixels[2*n])/brightest*speechWhite;
        int best=greys.empty()?pixels[2*n]:greys[0];
        for(int g:greys)if(std::abs(bright(g)-want)<std::abs(bright(best)-want))best=g;
        pixels[2*n]=(byte)best;
    }
    int key=speechKeyBase+lump;
    images[key]=upload(font.width,font.height,pixels.data(),font.left,font.top);
    made[lump]=key;
    return key;
}

void buildSpeech(const Uniforms &camera,float fraction) {
    float seconds=gameSeconds();
    float rx=camera.right[0],ry=camera.right[1],across=std::hypot(rx,ry);
    if(across<1e-4f)return;
    rx/=across;ry/=across;
    for(int i=0;i<numsectors;++i) for(mobj_t *thing=sectors[i].thinglist;thing;thing=thing->snext) {
        if(thing==players[displayplayer].mo||thing->health<=0||Game_ThingAsleep(thing))continue;   // the sleeping say nothing
        float shown=1;
        const char *line=Game_LibrarySays(thing,seconds,&shown);
        if(!line)continue;
        float x,y,z;interpolatedPosition(*thing,fraction,x,y,z);
        float dx=x-camera.eye[0],dy=y-camera.eye[1];
        if(dx*dx+dy*dy>speechReach*speechReach)continue;
        // The line's width, then its letters from the left, over the head.
        float width=0;
        for(const char *c=line;*c;++c) {
            int lump=*c==' '?-1:Game_FontLump(std::toupper((unsigned char)*c));
            width+=lump<0?speechSpace:lumpImage(lump,false).width+Game_FontSpacing();
        }
        float at=-width*speechPixel/2,top=z+units(thing->height)+18;
        unsigned who=(unsigned)(uintptr_t)thing*2654435761u;
        for(const char *c=line;*c;++c) {
            int lump=*c==' '?-1:Game_FontLump(std::toupper((unsigned char)*c));
            if(lump<0) {at+=speechSpace*speechPixel;continue;}
            Image &image=lumpImage(lump,false);
            // Coming and going a letter at a time, each at its own share of the way.
            unsigned h=(who^(unsigned)(c-line)*0x9e3779b9u)*0x85ebca6bu;h^=h>>13;
            if((h&1023)/1024.0f>=shown) {at+=(image.width+Game_FontSpacing())*speechPixel;continue;}
            // As DOOM sets its letters: each patch's own offset puts a full stop down on the line.
            float l=at,r=at+image.width*speechPixel,t=top+image.top*speechPixel,b=t-image.height*speechPixel;
            Vertex v[4]={{x+rx*l,y+ry*l,t,0,0,1,MODE_SPRITE},{x+rx*r,y+ry*r,t,(float)image.width,0,1,MODE_SPRITE},
                         {x+rx*r,y+ry*r,b,(float)image.width,(float)image.height,1,MODE_SPRITE},{x+rx*l,y+ry*l,b,0,(float)image.height,1,MODE_SPRITE}};
            for(Vertex &w:v)w.lampTint=0xFFFFFFFFu;
            quad(batches[greyLetter(lump)],v[0],v[1],v[2],v[3]);
            at=r+Game_FontSpacing()*speechPixel;
        }
    }
}
} // namespace doom3d
