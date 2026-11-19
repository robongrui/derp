/* Hell's skies (sky depth, sky_layers.cpp): a painting whose sky, above
   its skyline, is mostly a deep saturated red (Doom's second and third
   episodes, Doom 2's last sky, Heretic's second) is hell's, of two kinds.
   Painted in upright strokes, like Doom 2's writhing flesh, it writhes
   (sky/hell.glsl), smoke drifting under it in its darkest fifth, its glow
   breathing in its brightest tenth. Painted as clouds, like Doom's second
   episode, it gets clouds: the moving layers every mountain sky has
   (sky/clouds.glsl), in its own reds instead of white and grey. A third
   kind only a profile names: black tendrils hanging from a black top (the
   2025 Heretic's fourth), which curl from above as the flames sway from
   below. */
#include "sky_paint.h"

namespace doom3d {
// line: the first rock row per column. Only over rock: a sky of clouds
// alone (Doom's fourth, as red) has the engine's clouds already. Writes the
// kind (1 writhing, 2 red clouds, 3 hanging), the dark, the bright and the middle into
// settings (one texel of sky_layers.cpp's first settings row). kind, where a
// profile knows it (3: tendrils hanging from a black top, whose black and
// dark strokes keep the red under half the sky), skips the asking.
bool bakeHell(const SkyImage &s,const std::vector<int> &line,byte *settings,int kind) {
    std::vector<SkyRgb> sky;
    for(int x=0;x<s.w;++x)for(int y=0;y<std::min(line[x],s.h);++y)sky.push_back(s.rgb[y*s.w+x]);
    if(sky.size()<(size_t)(s.w*s.h/8))return false;
    size_t red=0;
    for(const SkyRgb &c:sky)red+=c.r>=60&&c.r>=1.8f*std::max(c.g,c.b);
    if(!kind&&red<sky.size()/2)return false;
    auto luma=[](const SkyRgb &c){return 0.299f*c.r+0.587f*c.g+0.114f*c.b;};
    auto meanOf=[&](size_t from,size_t to) {
        SkyRgb m{0,0,0};
        for(size_t i=from;i<to;++i){m.r+=sky[i].r/(to-from);m.g+=sky[i].g/(to-from);m.b+=sky[i].b/(to-from);}
        return s.nearest(m);
    };
    // Upright strokes change more across than down; clouds, drawn out
    // level, more down than across.
    double across=0,down=0;
    auto light=[&](int x,int y){return luma(s.rgb[y*s.w+(x%s.w+s.w)%s.w]);};
    for(int x=0;x<s.w;++x)for(int y=0;y+1<std::min(line[x],s.h);++y) {
        if(y<std::min(line[(x+1)%s.w],s.h))across+=std::fabs(light(x+1,y)-light(x,y));
        down+=std::fabs(light(x,y+1)-light(x,y));
    }
    size_t n=sky.size();
    std::sort(sky.begin(),sky.end(),[&](const SkyRgb &a,const SkyRgb &b){return luma(a)<luma(b);});
    settings[0]=kind?(byte)kind:across>1.15*down?1:2;
    settings[1]=(byte)meanOf(0,n/5);settings[2]=(byte)meanOf(n*9/10,n);settings[3]=(byte)meanOf(n*2/5,n*3/5);
    return true;
}
} // namespace doom3d
