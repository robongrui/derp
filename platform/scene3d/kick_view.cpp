/* The Authored player's kicking leg (p_kick.c): its frames, embedded (kick_frames_stock.h), as images
   the weapon draw (weapon.cpp) shows over the gun. */
#include "internal.h"
#include "../kick_frames_stock.h"

namespace doom3d {
// A frame's palette index and coverage pairs, from its runs: transparent, then painted pixels,
// row by row.
static std::vector<byte> kickPixels(const doom_kick_frame &art) {
    std::vector<byte> pixels((size_t)art.width*art.height*2,0);
    size_t at=0,most=(size_t)art.width*art.height;
    for(unsigned i=0;i+1<art.size;) {
        at+=art.runs[i];unsigned painted=art.runs[i+1];i+=2;
        for(unsigned n=0;n<painted&&i<art.size&&at<most;++n,++i,++at) {pixels[2*at]=art.runs[i];pixels[2*at+1]=255;}
    }
    return pixels;
}
// Relit as the guns are (MODE_RELIT, weapon.glsl), in their layout (relitWeaponArt): normals from
// the silhouette and the painted shading, but the paint kept as it is. Taken apart for relighting
// as id's gun art is (weapon_lighting.h) it came out washed out and jumped with every lamp (the
// user, 2026-11-14). Gloss by material: the boot's leather, brightest on the painted toe cap, and
// its dark grey upper; the olive trousers none, and calm in the gloss column's green, which keeps
// lamps from flaring over the cloth's edges.
static Image relitKickArt(const doom_kick_frame &art,const std::vector<byte> &pixels) {
    int w=art.width,h=art.height,stride=2*w+2;
    const byte *palette=(const byte*)W_CacheLumpName((char*)"PLAYPAL",PU_CACHE);
    std::vector<byte> normals;
    doom_weapon_normals(w,h,pixels.data(),palette,true,normals);
    std::vector<byte> packed((size_t)stride*h*4,0);
    for(int y=0;y<h;++y)for(int x=0;x<w;++x) {
        size_t i=(size_t)y*w+x;byte *texel=&packed[((size_t)y*stride+x)*4];
        texel[0]=pixels[2*i];texel[1]=pixels[2*i+1];texel[2]=normals[2*i];texel[3]=normals[2*i+1];
        if(!pixels[2*i+1])continue;
        const byte *c=palette+3*pixels[2*i];
        int r=c[0],g=c[1],b=c[2];float luma=(0.299f*r+0.587f*g+0.114f*b)/255;
        auto ramp=[](float from,float to,float v){float t=std::clamp((v-from)/(to-from),0.0f,1.0f);return t*t*(3-2*t);};
        float gloss=0;bool cloth=false;
        if(r-g>=14) gloss=0.4f+0.6f*ramp(0.4f,0.6f,luma);              // leather (and stray skin)
        else if(g>=r-10&&g-b>=8) cloth=true;                            // olive trousers
        else gloss=0.3f+0.4f*ramp(0.3f,0.45f,luma);                     // the boot's grey upper and laces
        byte *spare=&packed[((size_t)y*stride+w+2+x)*4];
        spare[0]=(byte)std::lround(gloss*255);spare[1]=cloth?255:0;
    }
    Image image;image.pixels=pixels;image.opaque=false;
    image.width=w;image.height=h;image.left=art.left;image.top=art.top;
    image.texture=gpuCreateTexture(GpuFormat::RGBA8,stride,h,packed.data(),stride*4);
    if(!image.texture) I_Error((char*)"Could not allocate a GPU texture");
    return image;
}
Image &kickImage(int frame,bool relit) {
    int count=(int)(sizeof doom_kick_frames/sizeof *doom_kick_frames);
    frame=std::clamp(frame,0,count-1);
    // Keyed just over the relit weapon frames' own (INT_MIN/2 and down): as painted, then relit.
    int key=INT_MIN/2+1+frame+(relit?count:0); auto found=images.find(key); if(found!=images.end()) return found->second;
    const doom_kick_frame &art=doom_kick_frames[frame];
    std::vector<byte> pixels=kickPixels(art);
    if(relit) return images.emplace(key,relitKickArt(art,pixels)).first->second;
    Image image=upload(art.width,art.height,pixels.data(),art.left,art.top);image.opaque=false;
    return images.emplace(key,std::move(image)).first->second;
}
} // namespace doom3d
