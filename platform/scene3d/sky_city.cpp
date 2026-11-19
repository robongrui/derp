/* Sky depth for a painted city (Doom 2's burning skyline): the buildings
   built as real boxes, the way the mountains are towers (sky/depth.glsl).

   A city is told by its lit windows: many single bright texels between dark
   ones, low in the painting. Its skyline is cut where the dark silhouettes
   begin, not at the smoke above (which the mountains' cut followed), with
   steps nearly free so building edges stay sheer. The skyline is cut into
   buildings at its steps, each at its own seeded depth, as deep as wide.
   Their fronts are the painting texel for texel; what lies behind them, the
   smoke and the dim far buildings, is thrown out: stuck behind close
   buildings it read as a poster. A far city of plain boxes takes its place,
   as tall as the painting's buildings run, walls in the painting's dim far
   brown with some of its windows lit; the sky becomes a gradient from the
   painting's dark smoke overhead to its fires' glow at the horizon, and the
   engine raises smoke from the city (sky/city.glsl).

   Laid out as sky_layers.cpp's texture: the near layer's texels (B, bit 2
   in A) and the sky (R); the covered row's R and B the skyline; the settings
   row's texel 3 holds 1 for a city (R), the far walls', the windows' and
   the fires' glow palette indices (GBA), texel 4 the smoke's dark and
   middle tone, the near walls' and the explosions' (RGBA); per column in
   the near towers' row and the far's, how
   far in from its building's left edge (R), the building's width (G), the
   far building's top row (B) and its depth (A, as sky_layers.cpp's). */
#include "sky_paint.h"

namespace doom3d {
namespace {
float luma(const SkyRgb &c) {return (0.299f*c.r+0.587f*c.g+0.114f*c.b)/255.0f;}
constexpr float dark=0.12f;
// The mean colour of the texels at (x, y) that pass, or fallback.
template<class Pass> SkyRgb meanOf(const SkyImage &s,Pass pass,SkyRgb fallback) {
    double r=0,g=0,b=0;int n=0;
    for(int y=0;y<s.h;++y)for(int x=0;x<s.w;++x) if(pass(x,y)) {const SkyRgb &c=s.rgb[y*s.w+x];r+=c.r;g+=c.g;b+=c.b;++n;}
    return n?SkyRgb{(float)(r/n),(float)(g/n),(float)(b/n)}:fallback;
}

// Lit windows low in the painting: bright texels with dark two to either side.
int litWindows(const SkyImage &s) {
    int count=0;
    for(int y=s.h/2;y<s.h;++y)for(int x=0;x<s.w;++x)
        if(luma(s.rgb[y*s.w+x])>0.4f&&luma(s.rgb[y*s.w+(x+s.w-2)%s.w])<dark&&luma(s.rgb[y*s.w+(x+2)%s.w])<dark)++count;
    return count;
}

// The skyline along the dark silhouettes: per column the row below which
// texels are dark (windows and fires inside count only lightly against),
// one cut per column, a step between columns all but free.
std::vector<int> darkSkyline(const SkyImage &s) {
    const int w=s.w,h=s.h;
    const float step=0.08f;
    std::vector<std::vector<float>> costs(w,std::vector<float>(h+1));
    for(int x=0;x<w;++x) {
        float c=0;
        for(int y=0;y<h;++y)c+=0.35f*(luma(s.rgb[y*w+x])<dark?0.0f:1.0f);
        costs[x][0]=c;
        for(int y=0;y<h;++y) {
            float d=luma(s.rgb[y*w+x])<dark?1.0f:0.0f;
            c+=d-0.35f*(1-d);costs[x][y+1]=c;
        }
    }
    std::vector<float> acc=costs[0],best(h+1);
    std::vector<std::vector<int>> back(w);
    for(int x=1;x<w;++x) {
        std::vector<int> arg(h+1);
        for(int y=0;y<=h;++y){best[y]=acc[y];arg[y]=y;}
        for(int y=1;y<=h;++y) if(best[y-1]+step<best[y]){best[y]=best[y-1]+step;arg[y]=arg[y-1];}
        for(int y=h-1;y>=0;--y) if(best[y+1]+step<best[y]){best[y]=best[y+1]+step;arg[y]=arg[y+1];}
        for(int y=0;y<=h;++y)acc[y]=best[y]+costs[x][y];
        back[x]=std::move(arg);
    }
    std::vector<int> line(w);
    line[w-1]=(int)(std::min_element(acc.begin(),acc.end())-acc.begin());
    for(int x=w-1;x>0;--x)line[x-1]=back[x][line[x]];
    return line;
}
} // namespace

bool bakeCity(const SkyImage &s,uint32_t seed,std::vector<byte> &texels,float &maxTop,float &farTop) {
    const int w=s.w,h=s.h;
    if(litWindows(s)<30)return false;
    std::vector<int> line=darkSkyline(s);
    for(int &v:line)v=std::min(v,h-1);
    // Buildings: the skyline cut where it steps three rows or more; anything
    // under three columns (an antenna, a notch) joins the building left of
    // it, and none wider than forty.
    std::vector<int> starts;
    for(int x=0;x<w;++x)if(std::abs(line[x]-line[(x+w-1)%w])>=3)starts.push_back(x);
    if(starts.empty())starts.push_back(0);
    std::vector<std::pair<int,int>> buildings;   // left column, width
    for(size_t i=0;i<starts.size();++i) {
        int left=starts[i],width=((starts[(i+1)%starts.size()]-left)%w+w)%w;
        if(!width)width=w;
        if(width<3&&!buildings.empty()){buildings.back().second+=width;continue;}
        for(int parts=(width+39)/40,k=0;k<parts;++k)buildings.push_back({(left+width*k/parts)%w,width*(k+1)/parts-width*k/parts});
    }
    texels.assign(4*w*(h+skyLayerRows),0);
    std::vector<int> nearTop(line);
    for(int i=0;i<w*h;++i) {
        int x=i%w,y=i/w;
        if(y>=nearTop[x]){texels[4*i+2]=s.index[i];texels[4*i+3]=2;}
    }
    // The painting's colours for what replaces its background: its dark
    // smoke (the darker texels of the top quarter) and that smoke's middle
    // tone, its fires (warm bright texels under the skyline), the dim far
    // buildings just over the skyline and its lit windows.
    auto L=[&](int x,int y){return luma(s.rgb[y*w+x]);};
    auto warm=[&](int x,int y){const SkyRgb &c=s.rgb[y*w+x];return c.r>c.g&&c.g>c.b;};
    SkyRgb smokeDark=meanOf(s,[&](int x,int y){return y<h/4&&L(x,y)<0.3f;},SkyRgb{40,36,34});
    SkyRgb smokeMid=meanOf(s,[&](int x,int y){return y<h/2&&L(x,y)>=0.2f&&L(x,y)<0.55f;},SkyRgb{90,80,72});
    SkyRgb glow=meanOf(s,[&](int x,int y){return y>=line[x]&&L(x,y)>0.5f&&warm(x,y);},SkyRgb{230,150,60});
    SkyRgb farWall=meanOf(s,[&](int x,int y){return y<line[x]&&y>=line[x]-12&&L(x,y)>=0.06f&&L(x,y)<0.3f;},SkyRgb{60,45,35});
    SkyRgb window=meanOf(s,[&](int x,int y){return y>=h/2&&L(x,y)>0.4f&&L((x+w-2)%w,y)<dark&&L((x+2)%w,y)<dark;},SkyRgb{220,180,90});
    // The near buildings' walls: their dark silhouettes, a touch lifted (black
    // walls lose every window grid they carry).
    SkyRgb nearWall=meanOf(s,[&](int x,int y){return y>=line[x]&&L(x,y)<dark;},SkyRgb{20,16,14});
    nearWall.r=nearWall.r*0.6f+farWall.r*0.4f;nearWall.g=nearWall.g*0.6f+farWall.g*0.4f;nearWall.b=nearWall.b*0.6f+farWall.b*0.4f;
    // The sky layer itself is no longer shown (sky/city.glsl paints the
    // gradient); its smoke colour where something reads it.
    for(int i=0;i<w*h;++i)texels[4*i]=(byte)s.nearest(smokeMid);
    SkyRgb low=smokeMid;
    byte *covered=&texels[4*w*h];
    for(int x=0;x<w;++x){covered[4*x]=covered[4*x+2]=(byte)line[x];covered[4*x+1]=covered[4*x+3]=255;}
    byte *p=&texels[4*w*(h+1)];
    p[0]=(byte)std::clamp(h*3/4,0,255);p[1]=(byte)std::max(6,h/10);p[2]=(byte)s.nearest(low);p[3]=1;
    p[12]=1;p[13]=(byte)s.nearest(farWall);p[14]=(byte)s.nearest(window);p[15]=(byte)s.nearest(glow);
    // Its explosions: the brightest twentieth of the upper half.
    std::vector<std::pair<float,int>> upper;
    for(int y=0;y<h/2;++y)for(int x=0;x<w;++x)upper.push_back({L(x,y),y*w+x});
    std::sort(upper.begin(),upper.end());
    SkyRgb blast{};int nb=0;
    for(size_t i=upper.size()*19/20;i<upper.size();++i){const SkyRgb &c=s.rgb[upper[i].second];blast.r+=c.r;blast.g+=c.g;blast.b+=c.b;++nb;}
    if(nb){blast.r/=nb;blast.g/=nb;blast.b/=nb;}
    p[16]=(byte)s.nearest(smokeDark);p[17]=(byte)s.nearest(smokeMid);p[18]=(byte)s.nearest(nearWall);p[19]=(byte)s.nearest(blast);
    byte *row=&texels[4*w*(h+2)];
    maxTop=0;
    for(size_t i=0;i<buildings.size();++i) {
        auto [left,width]=buildings[i];
        float depth=0.85f+0.35f*((skyHash(seed+31,(int)i*7+left)&0xFFFF)/65535.0f);
        byte a=(byte)std::clamp((int)std::lround((depth-0.5f)/1.5f*255),1,255);
        for(int k=0;k<width;++k) {
            int x=(left+k)%w;
            byte *t=row+4*x;
            t[0]=(byte)std::min(k,255);t[1]=(byte)std::min(width,255);t[3]=a;
            maxTop=std::max(maxTop,skyShrink*(0.70f-line[x]/(float)h)/0.68f*depth);
        }
    }
    // The far city: boxes three to ten columns wide, each as tall as a
    // building drawn from the painting's own skyline, a little lower (they
    // stand further off), at its own depth.
    std::vector<int> tall;
    const int horizon=(int)std::lround(0.70f*h);
    for(int x=0;x<w;++x)tall.push_back(std::max(2,horizon-line[x]));
    byte *farRow=&texels[4*w*(h+3)];
    farTop=0;
    for(int x=0,i=0;x<w;++i) {
        uint32_t r=skyHash(seed+97,i);
        int width=std::min(3+(int)(r%8),w-x),height=std::max(2,(int)std::lround(tall[(r>>8)%w]*(0.45f+0.4f*((r>>16)&255)/255.0f)));
        float depth=0.85f+0.3f*(((r>>24)&255)/255.0f);
        int topRow=std::clamp(horizon-height,0,h-1);
        byte a=(byte)std::clamp((int)std::lround((depth-0.5f)/1.5f*255),1,255);
        for(int k=0;k<width;++k){byte *t=farRow+4*(x+k);t[0]=(byte)k;t[1]=(byte)width;t[2]=(byte)topRow;t[3]=a;}
        farTop=std::max(farTop,skyShrink*(0.70f-topRow/(float)h)/0.68f*depth);
        x+=width;
    }
    return true;
}
} // namespace doom3d

namespace doom3d {
// Whether the level's sky is a painted city, read straight from the sky
// texture (no bake: weather.cpp asks while placing the weather, before the
// sky's layers exist), once a sky.
bool skyIsCity() {
    static int checked=-1;
    static bool city=false;
    if(checked==skytexture)return city;
    checked=skytexture;city=false;
    const Image &image=wallImage(skytexture);
    if(image.width<16||image.height<16||(int)image.pixels.size()<2*image.width*image.height)return city;
    SkyImage s;s.w=image.width;s.h=image.height;
    s.palette=(const byte*)W_CacheLumpName((char*)"PLAYPAL",PU_CACHE);
    s.rgb.resize(s.w*s.h);
    for(int i=0;i<s.w*s.h;++i)s.rgb[i]=s.color(image.pixels[2*i+1]?image.pixels[2*i]:0);
    city=litWindows(s)>=30;
    return city;
}
} // namespace doom3d
