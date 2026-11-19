/* A sky of clouds alone: its texture and settings (sky_layers.cpp lays out
   the rest). What weather the painting shows, and in what colours. */
#include "sky_paint.h"

namespace doom3d {
using Rgb=SkyRgb;
// A sky of clouds alone (no skyline on a hard edge): the painting stays, as
// the sky behind everything, and the engine's clouds (sky/clouds.glsl, or the
// painted field where the sky has one, sky_cloud_field.cpp) lie over it. Taken
// out, leaving each row's mean colour, it was a striped dome with noise on it.
// The painting's dark and light ends (the darkest and lightest fifth of the
// texels) colour the engine's clouds, and how much of it is cloud (texels
// standing clear of their row's mean) sets how much of the sky they fill in
// fair weather. Laid out as sky_layers.cpp's texture, with no towers; the
// first row of settings also holds (texel 1) the dark, light and middle
// palette indices and 1 for a cloud sky, (texel 2) the cover.
void bakeCloudSky(const SkyImage &s,std::vector<byte> &texels,int skyRows) {
    const int w=s.w,h=s.h;
    auto luma=[](const Rgb &c){return (0.299f*c.r+0.587f*c.g+0.114f*c.b)/255.0f;};
    texels.assign(4*w*(h+skyLayerRows),0);
    int cloudy=0;
    for(int y=0;y<h;++y) {
        float meanL=0;
        for(int x=0;x<w;++x)meanL+=luma(s.rgb[y*w+x])/w;
        for(int x=0;x<w;++x){texels[4*(y*w+x)]=s.index[y*w+x];cloudy+=std::abs(luma(s.rgb[y*w+x])-meanL)>0.06f;}
    }
    std::vector<int> order(w*h);
    for(int i=0;i<w*h;++i)order[i]=i;
    std::sort(order.begin(),order.end(),[&](int a,int b){return luma(s.rgb[a])<luma(s.rgb[b]);});
    auto meanOf=[&](int from,int to) {
        Rgb m{};
        for(int i=from;i<to;++i){const Rgb &c=s.rgb[order[i]];m.r+=c.r/(to-from);m.g+=c.g/(to-from);m.b+=c.b/(to-from);}
        return s.nearest(m);
    };
    int n=w*h,dark=meanOf(0,n/5),light=meanOf(n*4/5,n),middle=meanOf(n*2/5,n*3/5);
    Rgb horizon{};
    for(int y=skyRows*3/4;y<skyRows;++y)for(int x=0;x<w;++x){const Rgb &c=s.rgb[y*w+x];horizon.r+=c.r;horizon.g+=c.g;horizon.b+=c.b;}
    float count=(float)(w*(skyRows-skyRows*3/4));
    horizon.r/=count;horizon.g/=count;horizon.b/=count;
    byte *covered=&texels[4*w*h];
    for(int x=0;x<w;++x){covered[4*x]=covered[4*x+1]=covered[4*x+2]=covered[4*x+3]=255;}
    byte *p=&texels[4*w*(h+1)];
    p[0]=(byte)std::clamp((int)std::lround(0.70f*h-2),0,255);p[1]=(byte)std::max(6,h/10);
    p[2]=(byte)s.nearest(horizon);p[3]=1;
    // What weather the painting shows, rebuilt as such (sky/clouds.glsl):
    // 1 wind-sheared streaks (far more change down the painting than across
    // it: Doom's E4), 2 broken cloud whose edges a low sun lights warm (its
    // brightest texels warmer than the whole: Doom 2's first sky), 3 an
    // overcast with a bright break low down (a band in the lower half much
    // brighter than the top: Heretic's first). The rim colour is the
    // brightest tenth's; the break's row, the brightest of the lower half.
    double across=0,down=0;
    std::vector<float> rowLuma(h,0);
    for(int y=0;y<h;++y)for(int x=0;x<w;++x) {
        float l=luma(s.rgb[y*w+x]);rowLuma[y]+=l/w;
        across+=std::abs(luma(s.rgb[y*w+(x+1)%w])-l);
        if(y+1<h)down+=std::abs(luma(s.rgb[(y+1)*w+x])-l);
    }
    float top=0;for(int y=0;y<h/3;++y)top+=rowLuma[y]/(h/3);
    int breakRow=h/2;for(int y=h/2;y<h;++y)if(rowLuma[y]>rowLuma[breakRow])breakRow=y;
    Rgb bright{},all{};
    for(int i=n*9/10;i<n;++i){const Rgb &c=s.rgb[order[i]];bright.r+=c.r;bright.g+=c.g;bright.b+=c.b;}
    for(const Rgb &c:s.rgb){all.r+=c.r;all.g+=c.g;all.b+=c.b;}
    float tenth=(float)(n-n*9/10),warm=((bright.r-bright.b)/tenth-(all.r-all.b)/n)/255;
    int recipe=down/std::max(across,1e-6)>2.0?1:warm>0.03f?2:rowLuma[breakRow]>1.5f*top?3:2;
    bright.r/=tenth;bright.g/=tenth;bright.b/=tenth;
    p[4]=(byte)dark;p[5]=(byte)light;p[6]=(byte)middle;p[7]=(byte)recipe;
    p[8]=(byte)std::lround(std::clamp((float)cloudy/n,0.25f,0.85f)*255);
    p[9]=(byte)breakRow;p[10]=(byte)s.nearest(bright);
}
} // namespace doom3d
