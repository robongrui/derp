/* Textures from the WAD: upload with palette mips (sprites with their normals), bump
   relief, emissive masks and monitor glass; average and brightest colors. */
#include "internal.h"
#include "../sprite_flash.h"

namespace doom3d {
std::vector<unsigned> paletteLUTColors(const unsigned *palette) {
    std::vector<unsigned> colors(32*32*32);
    for(int b=0;b<32;++b)for(int g=0;g<32;++g)for(int r=0;r<32;++r) {
        int red=(r*255+15)/31,green=(g*255+15)/31,blue=(b*255+15)/31,best=0,bestDistance=INT_MAX;
        for(int i=0;i<256;++i) {
            int dr=red-int((palette[i]>>16)&255),dg=green-int((palette[i]>>8)&255),db=blue-int(palette[i]&255);
            int distance=dr*dr+dg*dg+db*db;
            if(distance<bestDistance) {bestDistance=distance;best=i;}
        }
        colors[(b*32+g)*32+r]=palette[best];
    }
    return colors;
}
void buildCloudTexture() {
    // Periodic, deterministic low-frequency cloud texture; no downloaded assets.
    std::array<byte,64*64> pixels;
    for(int y=0;y<64;++y)for(int x=0;x<64;++x) {
        float u=x*doomPi/32,v=y*doomPi/32;
        float cloud=0.5f+0.19f*sin(u+sin(v))+0.16f*cos(v*2-u)+0.10f*sin(u*3+v*2);
        pixels[y*64+x]=(byte)(std::clamp(cloud,0.0f,1.0f)*255);
    }
    cloudTexture=gpuCreateTexture(GpuFormat::R8,64,64,pixels.data(),64);
    if(!cloudTexture)I_Error((char*)"Could not allocate cloud texture");
}
// Palette mip levels for walls and flats, as software Quake built them: each
// level texel averages the colors it covers (weighted by coverage) and snaps
// back to the nearest PLAYPAL entry, so the shader keeps applying the live
// palette (damage and pickup flashes). Coverage and the emissive mask
// average; monitor glass takes the middle texel. Returns every level after
// the first, packed at the texture's channel count, and counts all levels.
static std::vector<byte> paletteMips(int w,int h,const byte *base,int channels,int &levels) {
    const byte *palette=(const byte*)W_CacheLumpName((char*)"PLAYPAL",PU_CACHE);
    // Nearest entries by 5-bit color cell, filled as cells are first used.
    static std::array<byte,768> cachedPalette={};
    static std::vector<short> nearest;
    if(nearest.empty()||memcmp(cachedPalette.data(),palette,768)) {
        memcpy(cachedPalette.data(),palette,768);nearest.assign(32*32*32,-1);
    }
    auto snap=[&](float r,float g,float b) {
        int cell=(std::min(31,(int)(r/8))<<10)|(std::min(31,(int)(g/8))<<5)|std::min(31,(int)(b/8));
        if(nearest[cell]<0) {
            float cr=(cell>>10)*8+4.0f,cg=((cell>>5)&31)*8+4.0f,cb=(cell&31)*8+4.0f,best=1e30f;
            for(int i=0;i<256;++i) {
                float dr=palette[i*3]-cr,dg=palette[i*3+1]-cg,db=palette[i*3+2]-cb;
                float d=2*dr*dr+4*dg*dg+3*db*db;
                if(d<best) {best=d;nearest[cell]=(short)i;}
            }
        }
        return (byte)nearest[cell];
    };
    std::vector<byte> out;levels=1;
    for(int lw=w,lh=h;lw>1||lh>1;++levels) {
        lw=std::max(1,lw/2);lh=std::max(1,lh/2);
        for(int y=0;y<lh;++y) for(int x=0;x<lw;++x) {
            int x0=x*w/lw,x1=std::max(x0+1,(x+1)*w/lw),y0=y*h/lh,y1=std::max(y0+1,(y+1)*h/lh);
            double rgb[3]={},coverage=0,mask=0;int count=0;
            for(int sy=y0;sy<y1;++sy) for(int sx=x0;sx<x1;++sx) {
                const byte *p=base+((size_t)sy*w+sx)*channels;
                for(int c=0;c<3;++c)rgb[c]+=palette[p[0]*3+c]*(double)p[1];
                coverage+=p[1];if(channels==4)mask+=p[2];++count;
            }
            const byte *middle=base+((size_t)((y0+y1)/2)*w+(x0+x1)/2)*channels;
            out.push_back(coverage>0?snap(float(rgb[0]/coverage),float(rgb[1]/coverage),float(rgb[2]/coverage)):middle[0]);
            out.push_back((byte)std::lround(coverage/count));
            if(channels==4) {out.push_back((byte)std::lround(mask/count));out.push_back(middle[3]);}
        }
    }
    return out;
}
// Channels: palette index, coverage, then the emissive mask (or sprite
// normal x) and, on walls and flats, monitor glass height (or sprite normal y).
// mipmaps: walls and flats also get palette mip levels.
Image upload(int w,int h,const byte *pixels,int left,int top,const byte *mask,const byte *normals,const byte *glass,bool mipmaps) {
    Image image; image.pixels.assign(pixels,pixels+(size_t)w*h*2); image.width=w; image.height=h; image.left=left; image.top=top;
    for(size_t i=0;i<(size_t)w*h;++i)if(pixels[2*i+1]!=255) {image.opaque=false;break;}
    std::vector<byte> packed;
    bool wide=mask||normals||glass;
    if(wide) {
        packed.resize((size_t)w*h*4);
        for(size_t i=0;i<(size_t)w*h;++i) {
            packed[4*i]=pixels[2*i];packed[4*i+1]=pixels[2*i+1];
            packed[4*i+2]=normals?normals[2*i]:mask?mask[i]:0;
            packed[4*i+3]=normals?normals[2*i+1]:glass?glass[i]:DOOM_GLASS_NONE;
        }
    }
    if(glass)image.screens.assign(glass,glass+(size_t)w*h); // Monitor glass, for shots (shot_lights.cpp).
    if(mask) {
        int lump=W_GetNumForName((char*)"PLAYPAL");
        if(W_LumpLength(lump)<768) I_Error((char*)"Invalid emissive palette");
        const byte *palette=(const byte*)W_CacheLumpNum(lump,PU_CACHE);
        double weight=0;
        for(int y=0;y<h;++y) for(int x=0;x<w;++x) {
            size_t i=(size_t)y*w+x;float e=mask[i]/255.0f;
            weight+=e;image.emissionU+=(x+0.5f)*e;image.emissionV+=(y+0.5f)*e;
            for(int c=0;c<3;++c) image.emissionColor[c]+=palette[pixels[2*i]*3+c]/255.0f*e;
        }
        if(weight>0) {
            image.emission.assign(mask,mask+(size_t)w*h); // Where shots break the glow (shot_lights.cpp).
            image.emissionU/=weight;image.emissionV/=weight;
            float peak=*std::max_element(image.emissionColor.begin(),image.emissionColor.end());
            for(float &c:image.emissionColor)c=std::max(0.04f,c/std::max(peak,0.001f));
            image.emissionCoverage=(float)weight/(w*h);
            image.emissionWeight=std::clamp(image.emissionCoverage*8.0f,0.25f,1.0f);
        }
    }
    int channels=wide?4:2,levels=1;
    const byte *levelData=wide?packed.data():pixels;
    std::vector<byte> chain;
    if(mipmaps) {
        std::vector<byte> mips=paletteMips(w,h,levelData,channels,levels);
        chain.assign(levelData,levelData+(size_t)w*h*channels);chain.insert(chain.end(),mips.begin(),mips.end());
        levelData=chain.data();
    }
    image.texture=gpuCreateTexture(wide?GpuFormat::RGBA8:GpuFormat::RG8,w,h,levelData,w*channels,levels);
    if(!image.texture) I_Error((char*)"Could not allocate a GPU texture");
    return image;
}
// Screen glass heights, or empty when the texture has no screens; frame
// receives where each glass pixel sits on its screen (doom_screen_glass),
// and each readout texel where it sits on its readout (doom_screen_readouts;
// empty when there are neither). Switches keep their lamps still, so the
// lit state stays plain.
static std::vector<byte> screenGlass(const char *name,bool flat,int w,int h,const byte *pixels,std::vector<byte> &frame) {
    frame.clear();
    if(!Game_ScreenTexture(name,flat))return {};
    const byte *palette=(const byte*)W_CacheLumpName((char*)"PLAYPAL",PU_CACHE);
    std::vector<byte> glass((size_t)w*h);frame.assign((size_t)w*h*4,0);
    int screens=doom_screen_glass(w,h,pixels,palette,glass.data(),frame.data());
    bool switches=!strncmp(name,"SW1",3)||!strncmp(name,"SW2",3);
    int readouts=switches?0:doom_screen_readouts(w,h,pixels,palette,glass.data(),frame.data());
    if(!readouts&&!screens)frame.clear();
    if(!screens)return {};
    return glass;
}
// Uploads the screen frame beside the image the shader refracts through it.
static Image &withGlassFrame(Image &image,const std::vector<byte> &frame) {
    if(frame.empty())return image;
    image.glassFrame=gpuCreateTexture(GpuFormat::RGBA8,image.width,image.height,frame.data(),image.width*4);
    if(!image.glassFrame)I_Error((char*)"Could not allocate a GPU texture");
    return image;
}
// Bump relief for walls and flats (Image::relief), from the artwork's log
// luminance, so a dark texture's seams count as much as a bright one's (in
// CEIL3_5 they are a single palette step). R/G: the slope along u and v, 128
// level and 255 just under 45 degrees; B: how grainy the texels around are,
// for varied highlights. Seams and mortar get real depth: the height is
// averaged 11 texels along each axis before taking its slope across, so long
// straight bands stand out while blotches a few texels wide wash out instead
// of turning into craters. A weak per-texel Sobel adds only fine pitting.
// Where the tile finder (tiles) splits the artwork into panels, bricks or
// plates, only their seams keep that depth: inside a tile the long bands are
// painted streaks and grime, not grooves, and stay shallow (seamWeight), so
// baked light from the side no longer corrugates a panel (E1M1's walls).
// Repeats like the texture. tiles: finds the painted tiles of a wall or flat
// (painted_tiles.h) so each gets a crooked lie; the relief is then stored at
// 16 bits with the anchor offset in alpha (noPaintedTile: none), the other
// channels as at 8 bits.
using Tiler=std::vector<uint16_t>(*)(const std::vector<float> &,int,int);
// depth scales the broad bands (seams, streaks); smooth leaves no grain
// (glassy highlights) and halves the per-texel tilt.
// How much of the broad bands' depth each texel keeps: 1 on and beside a seam
// between two tiles (or a tile and untiled artwork), fading over a texel,
// interior inside a tile; all 1 where nothing is tiled.
static std::vector<float> seamWeight(const std::vector<uint16_t> &anchors,int w,int h) {
    size_t count=(size_t)w*h;
    std::vector<float> weight(count,1.0f);
    if(anchors.size()!=count||std::none_of(anchors.begin(),anchors.end(),[](uint16_t a){return a!=noPaintedTile;}))return weight;
    constexpr float interior=0.15f;
    // Which tile a texel belongs to, the same for each of its texels in one repeat.
    auto tile=[&](int x,int y)->int64_t {
        x=(x%w+w)%w;y=(y%h+h)%h;uint16_t a=anchors[(size_t)y*w+x];
        if(a==noPaintedTile)return -1;
        int dx=(a>>8)-128,dy=(a&255)-128;
        int64_t ax=dx==noTileAxis?w:((x+dx)%w+w)%w,ay=dy==noTileAxis?h:((y+dy)%h+h)%h;
        return ax*65536+ay;
    };
    std::vector<int> distance(count,99);
    for(int y=0;y<h;++y)for(int x=0;x<w;++x) {
        int64_t own=tile(x,y);
        if(own!=tile(x+1,y)||own!=tile(x-1,y)||own!=tile(x,y+1)||own!=tile(x,y-1))distance[(size_t)y*w+x]=0;
    }
    for(int pass=1;pass<=2;++pass)
        for(int y=0;y<h;++y)for(int x=0;x<w;++x) {
            int &d=distance[(size_t)y*w+x];
            if(d<pass)continue;
            for(int k=0;k<4&&d>pass;++k) {
                int nx=x+(k==0)-(k==1),ny=y+(k==2)-(k==3);
                if(distance[(size_t)((ny%h+h)%h)*w+(nx%w+w)%w]==pass-1)d=pass;
            }
        }
    for(size_t i=0;i<count;++i)weight[i]=distance[i]<=1?1.0f:distance[i]==2?0.5f*(1+interior):interior;
    return weight;
}
// The relief texels (RGBA8 as described above) and the tiles' anchors (empty
// without a tile finder); also sets the image's brush direction and blankness.
static std::vector<byte> reliefCells(Image &image,Tiler tiles,float depth,bool smooth,std::vector<uint16_t> &anchors,bool seamsOnly=true) {
    int w=image.width,h=image.height;size_t count=(size_t)w*h;
    const byte *palette=(const byte*)W_CacheLumpName((char*)"PLAYPAL",PU_CACHE);
    std::vector<float> height(count),across(count),down(count),broad(count),grain(count),rough(count),scratch(count);
    for(size_t i=0;i<count;++i) {
        const byte *rgb=palette+image.pixels[2*i]*3;
        height[i]=std::log((0.299f*rgb[0]+0.587f*rgb[1]+0.114f*rgb[2])/255.0f+0.03f);
    }
    anchors=tiles?tiles(height,w,h):std::vector<uint16_t>();
    std::vector<float> seams=seamsOnly?seamWeight(anchors,w,h):std::vector<float>(count,1.0f);
    // Wrapped texel places from tables (the taps reach five texels out at most): the four
    // divisions a tap were a fifth of a texture's load.
    constexpr int wrapPad=8;
    std::vector<size_t> wrapX(w+2*wrapPad),wrapY(h+2*wrapPad);
    for(int i=0;i<w+2*wrapPad;++i)wrapX[i]=(size_t)(((i-wrapPad)%w+w)%w);
    for(int i=0;i<h+2*wrapPad;++i)wrapY[i]=(size_t)(((i-wrapPad)%h+h)%h)*w;
    auto at=[&](const std::vector<float> &v,int x,int y) {return v[wrapY[y+wrapPad]+wrapX[x+wrapPad]];};
    // Separable filter: taps along x into scratch, then along y into to.
    auto filter=[&](const std::vector<float> &from,std::vector<float> &to,const std::vector<float> &alongX,const std::vector<float> &alongY) {
        int rx=(int)alongX.size()/2,ry=(int)alongY.size()/2;
        for(int y=0;y<h;++y)for(int x=0;x<w;++x) {
            float sum=0;for(int k=-rx;k<=rx;++k)sum+=alongX[k+rx]*at(from,x+k,y);
            scratch[(size_t)y*w+x]=sum;
        }
        for(int y=0;y<h;++y)for(int x=0;x<w;++x) {
            float sum=0;for(int k=-ry;k<=ry;++k)sum+=alongY[k+ry]*at(scratch,x,y+k);
            to[(size_t)y*w+x]=sum;
        }
    };
    const std::vector<float> line(11,1/11.0f),tent={0.25f,0.5f,0.25f},binomial={1/16.0f,4/16.0f,6/16.0f,4/16.0f,1/16.0f};
    filter(height,across,tent,line); // Vertical bands, for the slope along u.
    filter(height,down,line,tent);   // Horizontal bands, for the slope along v.
    filter(height,broad,binomial,binomial);
    for(size_t i=0;i<count;++i)grain[i]=std::fabs(height[i]-broad[i]);
    filter(grain,rough,binomial,binomial);
    // Faint seams: some artwork paints its seams only a few palette steps
    // off the surface (FLAT18, COMPSPAN), so they barely tilt. Where the
    // strongest bands (99th percentile) clearly stand out from the typical
    // texel, they are deepened toward seamDepth; grain and organic artwork
    // without straight seams keep their relief. Only bands well above the
    // typical one deepen, so averaged speckle inside a slab stays shallow
    // instead of turning into streaks.
    std::vector<float> bands(count);
    for(int y=0;y<h;++y)for(int x=0;x<w;++x)
        bands[(size_t)y*w+x]=std::hypot(at(across,x+1,y)-at(across,x-1,y),at(down,x,y+1)-at(down,x,y-1))*1.5f;
    std::vector<float> ranked=bands;
    std::nth_element(ranked.begin(),ranked.begin()+count/2,ranked.end());float typical=ranked[count/2];
    std::nth_element(ranked.begin(),ranked.begin()+count*99/100,ranked.end());float strongest=ranked[count*99/100];
    const float seamDepth=0.35f,deepest=3.0f,standout=4.0f;
    float gain=strongest>0&&strongest>=standout*typical?std::clamp(seamDepth/strongest,1.0f,deepest):1.0f;
    std::vector<byte> cells(count*4);
    for(int y=0;y<h;++y)for(int x=0;x<w;++x) {
        float fx=(at(height,x+1,y-1)+2*at(height,x+1,y)+at(height,x+1,y+1)-at(height,x-1,y-1)-2*at(height,x-1,y)-at(height,x-1,y+1))*0.25f;
        float fy=(at(height,x-1,y+1)+2*at(height,x,y+1)+at(height,x+1,y+1)-at(height,x-1,y-1)-2*at(height,x,y-1)-at(height,x+1,y-1))*0.25f;
        float t=std::clamp((bands[(size_t)y*w+x]-2*typical)/(2*typical+1e-6f),0.0f,1.0f);
        float deepen=(1+(gain-1)*t*t*(3-2*t))*seams[(size_t)y*w+x];
        float fine=smooth?0.125f:0.25f;
        float sx=fine*fx+1.5f*deepen*depth*(at(across,x+1,y)-at(across,x-1,y));
        float sy=fine*fy+1.5f*deepen*depth*(at(down,x,y+1)-at(down,x,y-1));
        float soften=1/(1+std::sqrt(sx*sx+sy*sy)); // Steep edges ease toward 45 degrees.
        byte *cell=&cells[((size_t)y*w+x)*4];
        cell[0]=(byte)std::lround(128+127*sx*soften);cell[1]=(byte)std::lround(128+127*sy*soften);
        cell[2]=smooth?0:(byte)std::lround(std::clamp(rough[(size_t)y*w+x]/0.25f,0.0f,1.0f)*255);cell[3]=255;
    }
    // Brush direction for bare metal: the artwork's streaks run the way it
    // changes least from texel to texel. Only a clear lead along v turns it;
    // blank plates keep the default along u.
    float alongU=0,alongV=0;
    for(int y=0;y<h;++y)for(int x=0;x<w;++x) {
        alongU+=std::fabs(at(height,x+1,y)-at(height,x,y));alongV+=std::fabs(at(height,x,y+1)-at(height,x,y));
    }
    image.brushedDown=alongV*1.25f<alongU;
    // How much of it is blank plate (smooth texels, by the grain below):
    // busy panels (COMPTALL about 0.4) are not brushed, plain ones (COMPSPAN
    // about 0.8) are.
    size_t blankTexels=0;
    for(size_t i=0;i<count;++i)blankTexels+=rough[i]/0.25f<0.2f;
    image.blank=(float)blankTexels/count;
    return cells;
}
// The relief a wall (or flat) texture gets, RGBA8, for dev tools' previews;
// seamsOnly false: bands as deep inside tiles as at their seams.
std::vector<byte> reliefPreview(Image &image,bool flat,bool seamsOnly) {
    std::vector<uint16_t> anchors;
    return reliefCells(image,image.opaque?(flat?flatTiles:wallTiles):nullptr,1,false,anchors,seamsOnly);
}
static Image &withRelief(Image &image,Tiler tiles=nullptr,float depth=1,bool smooth=false) {
    int w=image.width,h=image.height;size_t count=(size_t)w*h;
    std::vector<uint16_t> anchors;
    std::vector<byte> cells=reliefCells(image,tiles,depth,smooth,anchors);
    if(std::any_of(anchors.begin(),anchors.end(),[](uint16_t a){return a!=noPaintedTile;})) {
        std::vector<uint16_t> wide(count*4);
        for(size_t i=0;i<count;++i) {
            for(int c=0;c<3;++c)wide[i*4+c]=(uint16_t)(cells[i*4+c]*257);
            wide[i*4+3]=anchors[i];
        }
        image.relief=gpuCreateTexture(GpuFormat::RGBA16Uint,w,h,wide.data(),w*8);
    } else image.relief=gpuCreateTexture(GpuFormat::RGBA8,w,h,cells.data(),w*4);
    if(!image.relief)I_Error((char*)"Could not allocate a GPU texture");
    return image;
}
// Leaded stained glass (Image::leaded): its relief in place of withRelief's.
// The glowing panes are the shards, split by the dark lead between them
// (4-connected groups of glowing texels): each shard is flat glass leaning a
// little its own way, by a hash of its first texel, so a light glints off
// some shards and not their neighbors. The lead is a groove: its texels
// slope down from the panes on either side, so its edges catch the light.
// RGBA8 as withRelief's (no tiles); B (grain) 0: glass is smooth.
static Image &withLeadedRelief(Image &image) {
    int w=image.width,h=image.height;size_t count=(size_t)w*h;
    const std::vector<byte> &mask=image.emission;
    std::vector<int> shard(count,-1);std::vector<size_t> stack;
    auto pane=[&](int x,int y){x=(x%w+w)%w;y=(y%h+h)%h;return !mask.empty()&&mask[(size_t)y*w+x]>0;};
    for(size_t start=0;start<count;++start) {
        if(shard[start]>=0||mask.empty()||!mask[start])continue;
        shard[start]=(int)start;stack.assign(1,start);
        while(!stack.empty()) {
            size_t i=stack.back();stack.pop_back();int x=(int)(i%w),y=(int)(i/w);
            const int steps[4][2]={{1,0},{-1,0},{0,1},{0,-1}};
            for(const auto &s:steps) {
                int nx=x+s[0],ny=y+s[1];if(nx<0||ny<0||nx>=w||ny>=h)continue;
                size_t j=(size_t)ny*w+nx;
                if(shard[j]<0&&mask[j]) {shard[j]=(int)start;stack.push_back(j);}
            }
        }
    }
    std::vector<byte> cells(count*4);
    for(int y=0;y<h;++y)for(int x=0;x<w;++x) {
        size_t i=(size_t)y*w+x;float sx=0,sy=0;
        if(shard[i]>=0) {
            uint32_t hash=(uint32_t)shard[i]*2654435761u;hash^=hash>>13;hash*=0x5bd1e995u;hash^=hash>>15;
            sx=((hash&1023)/1023.0f*2-1)*0.18f;sy=(((hash>>10)&1023)/1023.0f*2-1)*0.18f;
        } else {
            // Down from the panes: the slope points away from them.
            sx=0.45f*((float)pane(x-1,y)-(float)pane(x+1,y));sy=0.45f*((float)pane(x,y-1)-(float)pane(x,y+1));
        }
        byte *cell=&cells[i*4];
        cell[0]=(byte)std::lround(128+127*std::clamp(sx,-1.0f,1.0f));cell[1]=(byte)std::lround(128+127*std::clamp(sy,-1.0f,1.0f));
        cell[2]=0;cell[3]=255;
    }
    image.relief=gpuCreateTexture(GpuFormat::RGBA8,w,h,cells.data(),w*4);
    if(!image.relief)I_Error((char*)"Could not allocate a GPU texture");
    return image;
}
// Masks live beside graphics.cfg, or in the directory selected by -emissive.
static std::vector<byte> emissionMask(const char *name,bool flat,int w,int h,const byte *pixels) {
    std::vector<byte> mask((size_t)w*h,0);
    int paletteLump=W_GetNumForName((char*)"PLAYPAL");
    if(W_LumpLength(paletteLump)<768) I_Error((char*)"Invalid emissive palette");
    const byte *palette=(const byte*)W_CacheLumpNum(paletteLump,PU_CACHE);
    int kind=Game_GlowKind(name,flat);
    if(kind==DOOM_EMISSIVE_LAMP&&!flat&&doom_key_door(name))kind=DOOM_EMISSIVE_KEY;
    for(size_t i=0;i<mask.size();++i)
        mask[i]=doom_emissive_pixel(kind,palette+pixels[2*i]*3,pixels[2*i+1]);
    // A switch's lamp is a block of lit pixels; a single row of them is the bright top edge
    // painted on its red button (SW1COMP, SW1EXIT, SW1GRAY, SW1STON6: 9 by 1 under an unlit
    // lamp), which glowed alone in the dark as if the button were the light.
    if(kind==DOOM_EMISSIVE_SWITCH) {
        std::vector<byte> lit=mask;
        for(int y=0;y<h;++y)for(int x=0;x<w;++x)
            if(lit[(size_t)y*w+x]&&!(y>0&&lit[(size_t)(y-1)*w+x])&&!(y+1<h&&lit[(size_t)(y+1)*w+x]))mask[(size_t)y*w+x]=0;
    }
    std::string root=graphicsConfig;
    size_t slash=root.find_last_of("/\\");
    root=slash==std::string::npos?"Emissive":root.substr(0,slash+1)+"Emissive";
    int option=M_CheckParm((char*)"-emissive");
    if(option && option+1<myargc) root=myargv[option+1];
    while(root.size()>1&&(root.back()=='/'||root.back()=='\\'))root.pop_back();
    std::string file=root+(flat?"/flats/":"/walls/")+name+".png";
    if(FILE *probe=fopen(file.c_str(),"rb")) {
        fclose(probe);
        std::vector<float> values;
        if(!platformReadMask(file.c_str(),w,h,values)) {
            fprintf(stderr,"Emissive mask must be %dx%d: %s\n",w,h,file.c_str());
        } else {
            for(size_t i=0;i<mask.size();++i)
                mask[i]=pixels[i*2+1]?(byte)std::clamp(values[i]*255.0f,0.0f,255.0f):0;
            fprintf(stderr,"Loaded emissive mask: %s\n",file.c_str());
        }
    }
    return mask;
}
// Sprite lumps a living monster shows: those its spawn, chase, pain and attack
// states lead through, read from the state table so DEHACKED monsters count.
// MONSTER_ORDINARY frames get eyes (death frames never do); MONSTER_FIRING are
// its fullbright ones, unless it is always bright (the lost soul: geometry.cpp).
enum {MONSTER_ORDINARY=1,MONSTER_FIRING=2};
static bool livingMonsterLump(int lump,int kind) {
    static std::vector<char> living;
    if(living.empty()) {
        living.assign(numlumps,0);
        for(const mobjinfo_t &info:mobjinfo) {
            if(!(info.flags&MF_COUNTKILL))continue;
            bool alwaysBright=states[info.spawnstate].frame&FF_FULLBRIGHT;
            for(int start:{info.spawnstate,info.seestate,info.painstate,info.meleestate,info.missilestate})
                for(int s=start,steps=0;s>0&&s<NUMSTATES&&steps<64;s=states[s].nextstate,++steps) {
                    const state_t &state=states[s];int frame=state.frame&FF_FRAMEMASK;
                    bool bright=state.frame&FF_FULLBRIGHT;
                    if((bright&&alwaysBright)||state.sprite<0||state.sprite>=numsprites||frame>=sprites[state.sprite].numframes)continue;
                    for(short rotation:sprites[state.sprite].spriteframes[frame].lump)
                        if(rotation>=0&&firstspritelump+rotation<numlumps)living[firstspritelump+rotation]|=bright?MONSTER_FIRING:MONSTER_ORDINARY;
                }
        }
    }
    return lump>=0&&lump<(int)living.size()&&(living[lump]&kind);
}
// The 0.25 weight floor lets small fixtures (key-door strips, console
// screens) light their rooms. Computer and tech walls merely dotted with
// LEDs (TEKWALL*, COMPTALL, COMPWERD: under 0.6% glows) keep their glowing
// artwork but light only as much as those LEDs; with the floor E1M2's
// TEKWALL4 room went red from its few red LEDs. Stained glass glows over
// most of its area but is a window, not a lamp: it lights the room softly,
// most of its color reaching the floor as projected light (glass_light.cpp).
// Diffuser ceiling panels (CEIL3_4) light at half weight.
static Image &speckledPanel(Image &image,const char *name,bool flat) {
    int kind=Game_GlowKind(name,flat);
    if(image.emissionCoverage>0&&image.emissionCoverage<0.006f&&kind==DOOM_EMISSIVE_PANEL)
        image.emissionWeight=image.emissionCoverage*8.0f;
    if(kind==DOOM_EMISSIVE_GLASS) {image.emissionWeight*=0.2f;image.leaded=!image.emission.empty();}
    if(kind==DOOM_EMISSIVE_DIFFUSE)image.emissionWeight*=0.5f;   // diffuser panels: a dim, broad light
    if(kind==DOOM_EMISSIVE_STEP)image.emissionWeight*=0.08f;     // step lights: a glow on the riser, barely any light in the room
    if(kind==DOOM_EMISSIVE_SWITCH) {image.emissionWeight*=0.12f;image.indicator=image.emissionWeight>0;}   // a switch's lamp: seen, and a faint spill beside it
    return image;
}
// Wall textures with oversized bricks (rebrickedWall), loaded again when
// Settings::rebrick changes.
static std::unordered_set<int> rebrickable;
static int rebrickLoaded=-1;
void rebrickSwitched() {
    if(rebrickLoaded>=0&&rebrickLoaded!=settings.rebrick)for(int index:rebrickable)images.erase(index);
    rebrickLoaded=settings.rebrick;
}
/* At level load: every wall texture a side names, shown or not, and the
   other face of each switch. The level's first frame is built whole, but a
   shut door's track and a pressed switch have no face then: each cost a
   frame of some 30 ms when it first showed. */
extern "C" { extern int switchlist[]; extern int numswitches; }
void precacheWalls() {
    std::vector<char> want(numtextures,0);
    auto add=[&](int texture) {
        if(texture<=0||texture>=numtextures)return;
        want[texture]=1;
        int shown=texturetranslation[texture];
        if(shown>0&&shown<numtextures)want[shown]=1;
    };
    for(int i=0;i<numsides;++i) {add(sides[i].toptexture);add(sides[i].midtexture);add(sides[i].bottomtexture);}
    // Only the switches the map has: both faces of each.
    for(int i=0;i+1<numswitches*2;i+=2)
        if((switchlist[i]>0&&switchlist[i]<numtextures&&want[switchlist[i]])||(switchlist[i+1]>0&&switchlist[i+1]<numtextures&&want[switchlist[i+1]])) {
            add(switchlist[i]);add(switchlist[i+1]);
        }
    if(getenv("DERP_DIMSCREENS"))for(int texture=1;texture<numtextures;++texture) {
        char name[9];R_TextureName(texture,name);
        if(Game_ScreenTexture(name,0))want[texture]=1;
    }
    for(int texture=1;texture<numtextures;++texture)if(want[texture])wallImage(texture);
}
/* Texture keys: wall index >=0, WAD lump = -1-lump. */
Image &wallImage(int index) {
    auto found=images.find(index); if(found!=images.end()) return found->second;
    loadingTick();
    int w,h; R_TextureDimensions(index,&w,&h);
    if(w<1||h<1||w>8192||h>8192) I_Error((char*)"Unsupported texture dimensions");
    std::vector<byte> pixels((size_t)w*h*2); R_CopyIndexedTexture(index,pixels.data());
    char name[9]; R_TextureName(index,name);
    auto mask=emissionMask(name,false,w,h,pixels.data());
    bool glowing=std::any_of(mask.begin(),mask.end(),[](byte p){return p!=0;});
    std::vector<byte> frame;auto glass=screenGlass(name,false,w,h,pixels.data(),frame);
    // Lamps painted dim (doom_dim_fixture): each glowing texel is drawn at twice its level, in
    // its own hue from the palette, so the lamp keeps its shading, and glows in full.
    bool dimFixture=glowing&&doom_dim_fixture(name);
    if(dimFixture) {
        const byte *palette=(const byte*)W_CacheLumpName((char*)"PLAYPAL",PU_CACHE);
        for(size_t i=0;i<mask.size();++i) {
            if(!mask[i])continue;
            const byte *c=palette+pixels[2*i]*3;
            int hue=doom_readout_hue(c),to=pixels[2*i];long best=LONG_MAX;
            if(hue!=1) {mask[i]=0;continue;}   // its lamps are green: a stray red texel on the pipes is paint
            for(int p=0;p<256;++p) {
                const byte *q=palette+p*3;
                if(doom_readout_hue(q)!=hue)continue;
                long d=0;
                for(int k=0;k<3;++k) {long e=q[k]-std::min(255,c[k]*2);d+=e*e;}
                if(d<best) {best=d;to=p;}
            }
            pixels[2*i]=(byte)to;mask[i]=255;
        }
    }
    // Screens that are on, only dimly (SPACEW3's four monitors: dark green text, far under any
    // lamp rule): on a texture nothing else lights, what is painted in a readout's hue on its
    // screens' glass glows, as a tube does, and is drawn in a brighter step of its own hue from
    // the palette (its strongest channel brought to dimScreenLevel): at the paint's own level
    // the text showed, and no more. The PlayStation port repainted these screens lit. They
    // light their room as a small fixture does.
    // DERP_DIMSCREENS: names each texture this finds (precacheWalls then loads every screen texture).
    bool dimScreens=false;
    if(!glowing&&!glass.empty()) {
        const byte *palette=(const byte*)W_CacheLumpName((char*)"PLAYPAL",PU_CACHE);
        constexpr int dimScreenLevel=150;
        int brighter[256];std::fill(brighter,brighter+256,-1);
        size_t count=0;
        for(size_t i=0;i<mask.size();++i) {
            const byte *c=palette+pixels[2*i]*3;
            int hue=doom_readout_hue(c);
            if(glass[i]==DOOM_GLASS_NONE||!pixels[2*i+1]||!hue)continue;
            int &to=brighter[pixels[2*i]],hi=std::max({c[0],c[1],c[2]});
            if(to<0) {
                to=pixels[2*i];
                if(hi<dimScreenLevel) {
                    long best=LONG_MAX;
                    for(int p=0;p<256;++p) {
                        const byte *q=palette+p*3;
                        if(doom_readout_hue(q)!=hue)continue;
                        long d=0;
                        for(int k=0;k<3;++k) {long e=q[k]-c[k]*dimScreenLevel/hi;d+=e*e;}
                        if(d<best) {best=d;to=p;}
                    }
                }
            }
            pixels[2*i]=(byte)to;mask[i]=255;++count;
        }
        glowing=dimScreens=count>0;
        if(dimScreens&&getenv("DERP_DIMSCREENS"))fprintf(stderr,"dim screens: %s, %zu texels\n",name,count);
    }
    // Masked walls (bars, fences) show what lies behind instead of tiles;
    // animated ones would change tiles every frame.
    int next;bool animated=P_PicAnimationNext(true,index,&next)>0;
    // Oversized painted bricks laid anew at half the size (relaid_walls.cpp). Stone only: the
    // brick finder takes any one-size grid for bricks, and crates, computer panels, metal
    // plates and wood planks came out cut up with their logos torn (2026-11-20, E2M2's crates).
    if(!glowing&&glass.empty()&&!animated&&Game_WallSurface(name)==GAME_SURFACE_STONE) {
        std::vector<byte> smaller=rebrickedWall(w,h,pixels,2);
        if(!smaller.empty()) {rebrickable.insert(index);if(settings.rebrick)pixels.swap(smaller);}
    }
    Image &image=speckledPanel(images.emplace(index,upload(w,h,pixels.data(),0,0,glowing?mask.data():nullptr,nullptr,
        glass.empty()?nullptr:glass.data(),true)).first->second,name,false);
    if(dimFixture)image.emissionWeight=0.05f;   // a faint glow round the gauge: at 0.25 Underhalls' 26 of them lit the sewer
    if(dimScreens) {
        image.emissionWeight=0.25f;
        if(!frame.empty())screenTextRegister(index,w,h,pixels.data(),mask.data(),glass.data(),frame.data(),
                                             (const byte*)W_CacheLumpName((char*)"PLAYPAL",PU_CACHE));
    }
    if(image.leaded)return withLeadedRelief(withGlassFrame(image,frame));
    return withRelief(withGlassFrame(image,frame),image.opaque&&!animated?wallTiles:nullptr);
}
// Phobos: glowing slime frozen solid (decompression.cpp poolIce): the art's first frame,
// each texel moved toward ice of its own brightness and snapped back to the palette, from
// frosted (grey 0) to plain white ice (grey 16), its glow mask at glow eighths. Flats take the ice surface's smooth grooved relief. Kept
// under keys of their own (frozenFlatKey, frozenWallKey) so frozen surfaces are batched,
// and lit as ice, apart from the live ones.
static Image &frozenCopy(const Image &source,int key,int grey,int glowStep,bool flat) {
    auto found=images.find(key);
    if(found!=images.end())return found->second;
    const byte *palette=(const byte*)W_CacheLumpName((char*)"PLAYPAL",PU_CACHE);
    float dead=grey/16.0f,glow=glowStep/8.0f;
    std::array<byte,256> icy;
    for(int c=0;c<256;++c) {
        float rgb[3]={palette[3*c]/255.0f,palette[3*c+1]/255.0f,palette[3*c+2]/255.0f},l=0.3f*rgb[0]+0.59f*rgb[1]+0.11f*rgb[2];
        const float ice[3]={0.62f,0.70f,0.78f},white[3]={0.80f,0.86f,0.92f};
        float target[3];
        for(int k=0;k<3;++k) {
            // A flat (the ice sheet) frosts over; on a wall it is the slime painted on it that froze.
            float frosted=rgb[k]+(ice[k]*(0.45f+0.8f*l)-rgb[k])*(flat?0.45f:0.6f);
            // Its glow gone it is plain ice: white, not grey (shore.glsl's strip the same).
            target[k]=frosted+(white[k]*(0.55f+0.6f*l)-frosted)*dead*0.8f;
        }
        int best=0;float bestDistance=1e9f;
        for(int i=0;i<256;++i) {
            float d=0;for(int k=0;k<3;++k) {float e=palette[3*i+k]/255.0f-target[k];d+=e*e;}
            if(d<bestDistance) {bestDistance=d;best=i;}
        }
        icy[c]=(byte)best;
    }
    // On a wall only its slime freezes: the texels that glowed (the emission mask); the dry
    // wall above keeps its paint.
    std::vector<byte> pixels(source.pixels);
    bool masked=!flat&&source.emission.size()==pixels.size()/2;
    for(size_t i=0;i<pixels.size()/2;++i)
        if(!masked||source.emission[i]>=32)pixels[2*i]=icy[pixels[2*i]];
    std::vector<byte> mask;
    if(glow>0&&source.emission.size()==pixels.size()/2) {
        mask=source.emission;
        for(byte &m:mask)m=(byte)std::lround(m*glow);
    }
    Image &image=images.emplace(key,upload(source.width,source.height,pixels.data(),source.left,source.top,mask.empty()?nullptr:mask.data(),
                                           nullptr,nullptr,true)).first->second;
    // Its light dims with the glow left, in the slime's own color: upload's weight (coverage
    // times eight, capped at one) held a mostly glowing flat at full strength until its last
    // step, and the whitened texels would pale its color.
    if(!mask.empty()) {image.emissionWeight=source.emissionWeight*glow;image.emissionColor=source.emissionColor;}
    // An ice sheet is drawn with gaps where it has not frozen yet (world.frag): a cutout, so
    // the depth prepass doesn't hide the liquid under the gaps.
    if(flat)image.opaque=false;
    return flat?withRelief(image,nullptr,2.0f,true):withRelief(image);
}
// Phobos: a liquid flat stilled on its first frame as ice starts to form over it (no
// animation, no crossfade), kept under its own key (stillFlatKey).
Image &stillFlat(int lump) {
    int key=stillFlatKey(lump);
    auto found=images.find(key);
    if(found!=images.end())return found->second;
    const Image &source=lumpImage(lump,true);
    return images.emplace(key,upload(source.width,source.height,source.pixels.data(),0,0,
                                     source.emission.size()*2==source.pixels.size()?source.emission.data():nullptr,nullptr,nullptr,true)).first->second;
}
// Paint lit as a lamp (lit_paint.cpp): every texel glows in its own colour, so the flat
// shows lit and lights as a pool (poolSurface: it glows all over).
Image &litPaintFlat(int lump) {
    int key=litPaintKey(lump);
    auto found=images.find(key);
    if(found!=images.end())return found->second;
    const Image &source=lumpImage(lump,true);
    std::vector<byte> mask(source.pixels.size()/2,255);
    Image &image=images.emplace(key,upload(source.width,source.height,source.pixels.data(),0,0,mask.data(),nullptr,nullptr,true)).first->second;
    // It lights as bright as it is painted, not as a white lamp: FLOOR1_6's reds are
    // under half of full, and at full weight its room went red from wall to wall.
    const byte *palette=(const byte*)W_CacheLumpName((char*)"PLAYPAL",PU_CACHE);
    float paint=0;
    for(size_t i=0;i<mask.size();++i) {const byte *c=palette+3*source.pixels[2*i];paint+=std::max({c[0],c[1],c[2]})/255.0f;}
    image.emissionWeight*=paint/std::max<size_t>(mask.size(),1);
    return withRelief(image,flatTiles);
}
// The library: one asleep in a bed (Game_ThingAsleep) lies as a body does,
// without its blood. What is strongly red (the blood's ramp, found by colour)
// takes the body's colour around it, filled in from its edges, so the chest
// keeps its shape; what has no body beside it (spatter, the pool) goes.
Image &sleepingImage(int lump) {
    int key=sleepingKey(lump);
    auto found=images.find(key);
    if(found!=images.end())return found->second;
    const Image &source=lumpImage(lump,false);
    const byte *palette=(const byte*)W_CacheLumpName((char*)"PLAYPAL",PU_CACHE);
    int w=source.width,h=source.height;
    std::vector<byte> pixels(source.pixels);
    std::vector<char> blood((size_t)w*h);
    for(size_t i=0;i<blood.size();++i) {
        const byte *c=palette+3*pixels[2*i];
        blood[i]=pixels[2*i+1]&&c[0]>=48&&c[1]*3<=c[0]&&c[2]*3<=c[0];
    }
    const int step[4][2]={{1,0},{-1,0},{0,1},{0,-1}};
    for(bool filled=true;filled;) {
        filled=false;
        std::vector<std::pair<size_t,byte>> fills;
        for(int y=0;y<h;++y) for(int x=0;x<w;++x) {
            size_t i=(size_t)y*w+x;
            if(!blood[i])continue;
            byte beside[4];int n=0;
            for(const auto &d:step) {
                int bx=x+d[0],by=y+d[1];
                if(bx<0||by<0||bx>=w||by>=h)continue;
                size_t j=(size_t)by*w+bx;
                if(pixels[2*j+1]&&!blood[j])beside[n++]=pixels[2*j];
            }
            if(n<2)continue;
            // The commonest colour beside it, the first on a tie.
            int best=0,most=0;
            for(int a=0;a<n;++a) {int same=0;for(int b=0;b<n;++b)same+=beside[a]==beside[b];if(same>most) {most=same;best=a;}}
            fills.push_back({i,beside[best]});
        }
        for(const auto &f:fills) {pixels[2*f.first]=f.second;blood[f.first]=0;filled=true;}
    }
    for(size_t i=0;i<blood.size();++i)if(blood[i])pixels[2*i]=pixels[2*i+1]=0;
    auto normals=spriteNormals(w,h,pixels.data());
    return images.emplace(key,upload(w,h,pixels.data(),source.left,source.top,nullptr,normals.data())).first->second;
}
Image &frozenFlat(int lump,int grey,int glow) {return frozenCopy(lumpImage(lump,true),frozenFlatKey(lump,grey,glow),grey,glow,true);}
Image &frozenWall(int texture,int grey,int glow) {return frozenCopy(wallImage(texture),frozenWallKey(texture,grey,glow),grey,glow,false);}
Image &lumpImage(int lump,bool flat) {
    int key=-1-lump; auto found=images.find(key); if(found!=images.end()) return found->second;
    loadingTick();
    int size=W_LumpLength(lump);
    const byte *data=(const byte*)W_CacheLumpNum(lump,PU_CACHE);
    if(flat) {
        if(size<4096) I_Error((char*)"Invalid floor texture");
        std::vector<byte> pixels(64*64*2);
        for(int i=0;i<4096;++i) {pixels[2*i]=data[i];pixels[2*i+1]=255;}
        char name[9];memcpy(name,lumpinfo[lump].name,8);name[8]=0;
        auto mask=emissionMask(name,true,64,64,pixels.data());
        bool glowing=std::any_of(mask.begin(),mask.end(),[](byte p){return p!=0;});
        std::vector<byte> frame;auto glass=screenGlass(name,true,64,64,pixels.data(),frame);
        // Animated flats (liquids, lights) change their tiles every frame.
        int next;bool animated=P_PicAnimationNext(false,lump-firstflat,&next)>0;
        // Ice: its painted streaks cut grooves twice as deep, in a smooth,
        // glassy surface; no crooked tiles.
        if(Game_IceFlat(name))
            return withRelief(images.emplace(key,upload(64,64,pixels.data(),0,0,nullptr,nullptr,nullptr,true)).first->second,nullptr,2.0f,true);
        return withRelief(withGlassFrame(speckledPanel(images.emplace(key,upload(64,64,pixels.data(),0,0,glowing?mask.data():nullptr,nullptr,
            glass.empty()?nullptr:glass.data(),true)).first->second,name,true),frame),animated?nullptr:flatTiles);
    }
    SpriteArt art;
    if(const char *why=decodeSprite(data,size,art)) I_Error((char*)"%s",why);
    int paletteLump=W_GetNumForName((char*)"PLAYPAL");
    if(W_LumpLength(paletteLump)<768) I_Error((char*)"Invalid lighting palette");
    const byte *palette=(const byte*)W_CacheLumpNum(paletteLump,PU_CACHE);
    char name[9];memcpy(name,lumpinfo[lump].name,8);name[8]=0;
    finishSpriteArt(art,name,palette,spriteEyes(lump),spriteFlash(lump),spriteSlime(lump));
    return keepSprite(lump,art);
}
const char *decodeSprite(const byte *data,int size,SpriteArt &art) {
    if(size<8) return "Invalid sprite texture";
    const patch_t *patch=(const patch_t*)data;
    int w=SHORT(patch->width),h=SHORT(patch->height);
    if(w<1||h<1||w>8192||h>8192||8+(size_t)w*4>(size_t)size)
        return "Invalid sprite dimensions";
    std::vector<byte> pixels((size_t)w*h*2);
    for(int x=0;x<w;++x) {
        size_t pos=(uint32_t)LONG(patch->columnofs[x]); int previous=-1;
        while(pos<(size_t)size && data[pos]!=255) {
            if(pos+4>(size_t)size||pos+4+data[pos+1]>(size_t)size) return "Invalid sprite column";
            int top=data[pos]; if(top<=previous) top+=previous; previous=top;
            for(int y=0;y<data[pos+1];++y) if(top+y>=0&&top+y<h) {
                size_t pixel=((size_t)(top+y)*w+x)*2;
                pixels[pixel]=data[pos+3+y]; pixels[pixel+1]=255;
            }
            pos+=data[pos+1]+4;
        }
    }
    art.w=w;art.h=h;art.left=SHORT(patch->leftoffset);art.top=SHORT(patch->topoffset);
    art.pixels.swap(pixels);
    return nullptr;
}
bool spriteEyes(int lump) {return livingMonsterLump(lump,MONSTER_ORDINARY);}
bool spriteFlash(int lump) {return livingMonsterLump(lump,MONSTER_FIRING);}
// Sprite lumps a barrel shows standing: its spawn loop, read from the state
// table so DEHACKED barrels count; not its blast. Doom's only (Heretic's pod is
// green all over).
bool spriteSlime(int lump) {
    static std::vector<char> standing;
    if(standing.empty()) {
        standing.assign(numlumps,0);
        if(Game_HasEffect(GAME_EFFECT_SLIME_BARRELS))for(int type=0;type<NUMMOBJTYPES;++type) {
            if(Game_ThingRole(type)!=GAME_THING_BARREL)continue;
            for(int s=mobjinfo[type].spawnstate,steps=0;s>0&&s<NUMSTATES&&steps<64;s=states[s].nextstate,++steps) {
                const state_t &state=states[s];int frame=state.frame&FF_FRAMEMASK;
                if(state.sprite<0||state.sprite>=numsprites||frame>=sprites[state.sprite].numframes)continue;
                for(short rotation:sprites[state.sprite].spriteframes[frame].lump)
                    if(rotation>=0&&firstspritelump+rotation<numlumps)standing[firstspritelump+rotation]=1;
            }
        }
    }
    return lump>=0&&lump<(int)standing.size()&&standing[lump];
}
void finishSpriteArt(SpriteArt &art,const char *name,const byte *palette,bool eyes,bool flash,bool slime) {
    int w=art.w,h=art.h;const byte *pixels=art.pixels.data();
    std::vector<byte> &normals=art.normals;
    normals=spriteNormals(w,h,pixels);
    // Glowing eyes take normals no silhouette has: x all the way right
    // and y over halfway down, the glow in y's top quarter (eyeGlow).
    // Stock monsters' eyes are authored (sprite_eyes_stock.h); other
    // artwork is searched for them (sprite_eyes.h).
    if(eyes) {
        std::vector<byte> found;
        if(!doom_stock_eyes(name,w,h,pixels,found))found=doom_sprite_eyes(w,h,pixels,palette);
        for(size_t i=0;i<found.size();++i)if(found[i]) {normals[2*i]=255;normals[2*i+1]=(byte)(192+found[i]*63/255);}
    }
    // A barrel's slime top glows as eyes do (eyeGlow): the nukage rule
    // (emissive.h) over its top quarter, where the slime is painted; the
    // olive body bands under it fail the rule.
    if(slime)for(int y=0;y<(h+3)/4;++y)for(int x=0;x<w;++x) {
        size_t i=(size_t)y*w+x;
        byte glow=doom_emissive_pixel(DOOM_EMISSIVE_SLIME,palette+pixels[2*i]*3,pixels[2*i+1]);
        if(glow) {normals[2*i]=255;normals[2*i+1]=(byte)(192+glow*63/255);}
    }
    // The flash painted on a firing frame: x all the way right, y all the way up (firingFlash).
    if(flash) {
        auto painted=doom_sprite_flash(w,h,pixels,palette);
        for(size_t i=0;i<painted.size();++i)if(painted[i]) {normals[2*i]=255;normals[2*i+1]=0;}
    }
    art.glowWeight=doom_sprite_glow(pixels,(size_t)w*h,palette,art.glow.data());
    art.glowHeight=doom_sprite_glow_height(pixels,w,h,palette,art.glow.data());
}
Image &keepSprite(int lump,SpriteArt &art) {
    Image image=upload(art.w,art.h,art.pixels.data(),art.left,art.top,nullptr,art.normals.data());
    image.glow=art.glow;image.glowWeight=art.glowWeight;image.glowHeight=art.glowHeight;
    return images.emplace(-1-lump,std::move(image)).first->second;
}
// What I_SetPalette (i_sdl_video.c) makes of each palette in PLAYPAL at the
// present gamma, so the lookups built ahead match the ones the frames ask for.
std::vector<std::array<unsigned,256>> gamePalettes() {
    std::vector<std::array<unsigned,256>> out;
    int lump=W_CheckNumForName((char*)"PLAYPAL");
    if(lump<0)return out;
    int count=W_LumpLength(lump)/768,gamma=std::clamp(usegamma,0,4);
    const byte *data=(const byte*)W_CacheLumpNum(lump,PU_CACHE);
    for(int p=0;p<count;++p) {
        std::array<unsigned,256> palette;
        for(int i=0;i<256;++i) {
            const byte *c=data+p*768+i*3;
            palette[i]=0xff000000u|(unsigned)gammatable[gamma][c[0]]<<16|(unsigned)gammatable[gamma][c[1]]<<8|gammatable[gamma][c[2]];
        }
        out.push_back(palette);
    }
    return out;
}
const std::array<float,3> &averageColor(Image &image) {
    if(image.averaged)return image.average;
    const byte *palette=(const byte*)W_CacheLumpName((char*)"PLAYPAL",PU_CACHE);
    double sum[3]={},count=0;
    for(size_t i=0;i+1<image.pixels.size();i+=2) if(image.pixels[i+1]) {
        for(int c=0;c<3;++c)sum[c]+=palette[image.pixels[i]*3+c]/255.0;
        ++count;
    }
    for(int c=0;c<3;++c)image.average[c]=count?(float)(sum[c]/count):0.5f;
    image.averaged=true;return image.average;
}
// Particles are chunky: opaque blocks of whole Doom pixels in WAD palette
// colors, taken from random texels of the art they come from.
// The game's palette, copied once: looking PLAYPAL up by name for every
// particle block (thousands a frame in a sandstorm) cost milliseconds.
const byte *playpal() {
    static byte palette[768];static bool copied=false;
    if(!copied) {memcpy(palette,W_CacheLumpName((char*)"PLAYPAL",PU_CACHE),sizeof(palette));copied=true;}
    return palette;
}
std::array<float,3> paletteColor(const std::array<float,3> &color) {
    static std::vector<int16_t> nearest;
    if(nearest.empty())nearest.assign(32*32*32,-1);
    int cell[3];for(int c=0;c<3;++c)cell[c]=std::clamp((int)std::lround(color[c]*31),0,31);
    int16_t &index=nearest[(cell[0]*32+cell[1])*32+cell[2]];
    const byte *palette=playpal();
    if(index<0) {
        int best=1<<30;
        for(int i=0;i<256;++i) {
            int d=0;for(int c=0;c<3;++c){int e=palette[i*3+c]-cell[c]*255/31;d+=e*e;}
            if(d<best){best=d;index=(int16_t)i;}
        }
    }
    return {palette[index*3]/255.0f,palette[index*3+1]/255.0f,palette[index*3+2]/255.0f};
}
// The brightest opaque texel's color.
const std::array<float,3> &brightestColor(Image &image) {
    if(image.brightened)return image.brightest;
    const byte *palette=(const byte*)W_CacheLumpName((char*)"PLAYPAL",PU_CACHE);
    int best=-1,bestSum=-1;
    for(size_t i=0;i+1<image.pixels.size();i+=2) if(image.pixels[i+1]) {
        int index=image.pixels[i],sum=palette[index*3]+palette[index*3+1]+palette[index*3+2];
        if(sum>bestSum){bestSum=sum;best=index;}
    }
    image.brightest=best<0?averageColor(image):std::array<float,3>{palette[best*3]/255.0f,palette[best*3+1]/255.0f,palette[best*3+2]/255.0f};
    image.brightened=true;return image.brightest;
}
std::array<float,3> texelColor(Image &image) {
    const byte *palette=(const byte*)W_CacheLumpName((char*)"PLAYPAL",PU_CACHE);
    size_t count=image.pixels.size()/2;
    for(int tries=0;count&&tries<8;++tries) {
        size_t i=std::min(count-1,(size_t)randomRange(0,(float)count));
        if(image.pixels[2*i+1])return {palette[image.pixels[2*i]*3]/255.0f,palette[image.pixels[2*i]*3+1]/255.0f,palette[image.pixels[2*i]*3+2]/255.0f};
    }
    return averageColor(image);
}
void imagesShutdown() {screenTextShutdown();images.clear();rebrickable.clear();cloudTexture=nullptr;}
} // namespace doom3d
