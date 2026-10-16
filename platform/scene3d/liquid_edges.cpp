/* Glowing liquid edges: the strip of liquid the game paints at the foot of
   its edge textures (DOOM's NUKE24 and NUKEDGE1), found in the WAD at runtime. */
#include "internal.h"
#include <cctype>

namespace doom3d {
// An edge texture glows in a liquid's kind along its foot and not at its top:
// rock or metal with the liquid lapping at it. Of those, the one with the
// tallest strip is the liquid's edge. Each row of the strip, counted up from
// the foot, holds its palette indices, how much each texel is the liquid's
// paint (the glowing texels, and the duller ones in its hue that fade into
// the rock) and its glow.
static EdgeStrip findStrip(int kind) {
    EdgeStrip best;
    const byte *palette=(const byte*)W_CacheLumpName((char*)"PLAYPAL",PU_CACHE);
    for(int t=0;t<numtextures;++t) {
        char name[9];R_TextureName(t,name);
        if(Game_GlowKind(name,0)!=kind)continue;
        int w,h;R_TextureDimensions(t,&w,&h);
        if(w<8||h<8||w>1024||h>1024)continue;
        std::vector<byte> pixels((size_t)w*h*2);R_CopyIndexedTexture(t,pixels.data());
        std::vector<float> coverage(h,0);float hue[3]={};
        std::vector<byte> glow((size_t)w*h);
        for(int y=0;y<h;++y)for(int x=0;x<w;++x) {
            size_t i=(size_t)y*w+x;const byte *c=palette+pixels[2*i]*3;
            glow[i]=doom_emissive_pixel(kind,c,pixels[2*i+1]);
            if(!glow[i])continue;
            coverage[y]+=1.0f/w;
            for(int k=0;k<3;++k)hue[k]+=c[k];
        }
        // Solidly lit along the foot, and the strip ends in the lower half
        // with dark rock over the top quarter: not a wall of drips or falls.
        if((coverage[h-1]+coverage[h-2]+coverage[h-3]+coverage[h-4])/4<0.9f)continue;
        bool topDark=true;
        for(int y=0;y<h/4;++y)topDark&=coverage[y]<0.02f;
        int rows=0;
        while(rows<h&&coverage[h-1-rows]>=0.02f)++rows;
        if(!topDark||rows*2>h+8)continue;
        // The paint fades a few rows past the last glowing one.
        rows=std::min({rows+rows/2+1,h-h/4,63});
        if(rows<=best.height)continue;
        int dominant=hue[1]>=hue[0]&&hue[1]>=hue[2]?1:hue[0]>=hue[2]?0:2;
        best.kind=kind;best.height=rows;best.texture=t;
        best.index.assign((size_t)64*rows,0);best.paint.assign((size_t)64*rows,0);best.glow.assign((size_t)64*rows,0);
        for(int r=0;r<rows;++r)for(int x=0;x<64;++x) {
            size_t i=(size_t)(h-1-r)*w+x%w,o=(size_t)r*64+x;
            const byte *c=palette+pixels[2*i]*3;
            int other=std::max(c[(dominant+1)%3],c[(dominant+2)%3]);
            best.index[o]=pixels[2*i];best.glow[o]=glow[i];
            best.paint[o]=glow[i]?255:(byte)std::clamp((c[dominant]-other-8)*255/40,0,255);
        }
    }
    return best;
}
const EdgeStrip &edgeStrip(int kind) {
    static std::map<int,EdgeStrip> strips;
    auto found=strips.find(kind);
    if(found==strips.end())found=strips.emplace(kind,findStrip(kind)).first;
    return found->second;
}
// The glow kind of a sector's liquid floor (DOOM_EMISSIVE_NONE when it is not
// a glowing liquid).
int liquidGlowKind(int sector) {
    if(!sectorMist[sector].liquid||sectors[sector].floorpic==skyflatnum)return DOOM_EMISSIVE_NONE;
    int lump=firstflat+flattranslation[sectors[sector].floorpic];
    char name[9];memcpy(name,lumpinfo[lump].name,8);name[8]=0;
    for(char &ch:name)ch=(char)toupper((unsigned char)ch);
    int kind=Game_GlowKind(name,1);
    return kind==DOOM_EMISSIVE_SLIME||kind==DOOM_EMISSIVE_LAVA||kind==DOOM_EMISSIVE_MAGMA?kind:DOOM_EMISSIVE_NONE;
}
// The level's strip: of the glowing liquids it has an edge texture for, the
// most common one.
const EdgeStrip *levelEdgeStrip() {
    std::map<int,int> use;
    for(int i=0;i<numsectors;++i)if(int kind=liquidGlowKind(i))++use[kind];
    const EdgeStrip *best=nullptr;int most=0;
    for(auto [kind,count]:use) {
        const EdgeStrip &strip=edgeStrip(kind);
        if(strip.height>0&&count>most) {best=&strip;most=count;}
    }
    return best;
}
} // namespace doom3d
