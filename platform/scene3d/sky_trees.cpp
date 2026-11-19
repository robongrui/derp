/* Dead trees standing in a sky of fog (sky depth, sky_layers.cpp; the 2025
   Heretic's fifth sky, over the Stagnant Demesne): thin dark trunks and
   hanging moss painted into the mist along the painting's foot. Cut out
   here as a soft matte, so the engine's clouds behind them cannot wash them
   out, and laid back where they were painted (sky/trees.glsl), a flat
   picture on the horizon, the valley mist drifting across their feet as
   among Doom's first mountains; the painting behind is filled with the fog
   they stood in. (On rings round the map, sliding against each other as
   cards, they read wrong.) */
#include "sky_paint.h"

namespace doom3d {
namespace {
float luma(const SkyRgb &c) {return 0.299f*c.r+0.587f*c.g+0.114f*c.b;}
// A min or max over a box (rx columns each way round the horizon, ry rows
// each way, clamped), separable.
std::vector<float> boxed(const std::vector<float> &a,int w,int h,int rx,int ry,bool high) {
    std::vector<float> t(w*h),out(w*h);
    auto pick=[&](float x,float y){return high?std::max(x,y):std::min(x,y);};
    for(int y=0;y<h;++y)for(int x=0;x<w;++x) {
        float v=a[y*w+x];
        for(int d=-rx;d<=rx;++d)v=pick(v,a[y*w+((x+d)%w+w)%w]);
        t[y*w+x]=v;
    }
    for(int y=0;y<h;++y)for(int x=0;x<w;++x) {
        float v=t[y*w+x];
        for(int d=-ry;d<=ry;++d)v=pick(v,t[std::clamp(y+d,0,h-1)*w+x]);
        out[y*w+x]=v;
    }
    return out;
}
// The same closing on each colour channel: what the texel would be with
// everything thin and dark taken out of it, the fog it stands in.
std::vector<float> closed(const std::vector<float> &a,int w,int h,int r) {return boxed(boxed(a,w,h,r,r,true),w,h,r,r,false);}
}

// Per texel: how much tree there is (0-255); clean is the painting with the
// trees taken out. Trees are
// what a closing over five texels lifts by more than a few steps of
// brightness (thin and darker than the fog round them), or green (moss),
// from row top down. Specks of fewer than six texels drop out; the faint
// edge of a tree stays if it touches its core.
bool findTrees(const SkyImage &s,int top,std::vector<byte> &alpha,SkyImage &clean) {
    const int w=s.w,h=s.h;
    std::vector<float> light(w*h),r(w*h),g(w*h),b(w*h);
    for(int i=0;i<w*h;++i){light[i]=luma(s.rgb[i]);r[i]=s.rgb[i].r;g[i]=s.rgb[i].g;b[i]=s.rgb[i].b;}
    std::vector<float> fog=closed(light,w,h,2);
    std::vector<float> a(w*h,0);
    for(int y=std::max(top,0);y<h;++y)for(int x=0;x<w;++x) {
        int i=y*w+x;
        const SkyRgb &c=s.rgb[i];
        float thin=(fog[i]-light[i]-4.0f)/10.0f,moss=(c.g-std::max(c.r,c.b)-3.0f)/5.0f;
        a[i]=std::clamp(std::max(thin,moss),0.0f,1.0f);
    }
    // The cores, by component (8-connected, round the horizon).
    std::vector<int> label(w*h,-1),size;
    for(int i=0;i<w*h;++i) {
        if(a[i]<0.3f||label[i]>=0)continue;
        int id=(int)size.size(),count=0;
        std::vector<int> stack{i};label[i]=id;
        while(!stack.empty()) {
            int j=stack.back();stack.pop_back();++count;
            int x=j%w,y=j/w;
            for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx) {
                int ny=y+dy;if(ny<0||ny>=h)continue;
                int k=ny*w+((x+dx)%w+w)%w;
                if(a[k]>=0.3f&&label[k]<0){label[k]=id;stack.push_back(k);}
            }
        }
        size.push_back(count);
    }
    alpha.assign(w*h,0);
    int kept=0;
    for(int i=0;i<w*h;++i) {
        int id=label[i];
        // A faint texel belongs to the strongest core next to it.
        if(id<0&&a[i]>=0.12f) {
            int x=i%w,y=i/w;float best=0;
            for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx) {
                int ny=y+dy;if(ny<0||ny>=h)continue;
                int k=ny*w+((x+dx)%w+w)%w;
                if(label[k]>=0&&size[label[k]]>=6&&a[k]>best){best=a[k];id=label[k];}
            }
        }
        if(id<0||size[id]<6)continue;
        alpha[i]=(byte)std::lround(255*a[i]);
        ++kept;
    }
    if(kept<w)return false;   // a handful of specks is no forest
    // The fog behind: each colour closed the same way, blended in as far as
    // the texel was tree.
    std::vector<float> fr=closed(r,w,h,2),fg=closed(g,w,h,2),fb=closed(b,w,h,2);
    clean=s;
    for(int i=0;i<w*h;++i) {
        if(!alpha[i])continue;
        float k=std::min(1.0f,alpha[i]/255.0f*1.5f);
        SkyRgb c={s.rgb[i].r+(fr[i]-s.rgb[i].r)*k,s.rgb[i].g+(fg[i]-s.rgb[i].g)*k,s.rgb[i].b+(fb[i]-s.rgb[i].b)*k};
        clean.index[i]=(byte)s.nearest(c);clean.rgb[i]=s.color(clean.index[i]);
    }
    return true;
}

// Into sky_layers.cpp's texture: per painting texel the tree's palette index
// (B) and how much of it there is (A); in the first settings row, texel 7: 1,
// the first row they reach, and the fog's colour among them. The valley mist
// (texel 0, sky/depth.glsl's ringMist) lies over their feet in the pale fog of
// the painting's last rows: from the row three quarters of the trees stand
// above, thickening down to their lowest.
void treeTexels(const SkyImage &s,const SkyImage &clean,int top,const std::vector<byte> &alpha,std::vector<byte> &texels) {
    const int w=s.w,h=s.h;
    SkyRgb air{0,0,0};float n=0;
    std::vector<float> perRow(h,0);
    for(int i=0;i<w*h;++i) {
        byte *t=&texels[4*i];
        t[2]=alpha[i]?s.index[i]:0;t[3]=alpha[i];
        perRow[i/w]+=alpha[i];
        if(i/w>=top&&!alpha[i]){air.r+=clean.rgb[i].r;air.g+=clean.rgb[i].g;air.b+=clean.rgb[i].b;n+=1;}
    }
    if(n>0){air.r/=n;air.g/=n;air.b/=n;}
    SkyRgb ground{0,0,0};
    for(int i=w*(h-h/8);i<w*h;++i){ground.r+=clean.rgb[i].r/(w*(h/8));ground.g+=clean.rgb[i].g/(w*(h/8));ground.b+=clean.rgb[i].b/(w*(h/8));}
    float all=0;for(float v:perRow)all+=v;
    int feet=h-1,lowest=0;
    float sum=0;
    for(int y=0;y<h;++y){sum+=perRow[y];if(sum<0.75f*all)feet=y+1;if(perRow[y]>0)lowest=y;}
    byte *mist=&texels[4*w*(h+1)];
    mist[0]=(byte)std::clamp(feet,0,255);mist[1]=(byte)std::clamp((lowest-feet)/2,6,255);mist[2]=(byte)s.nearest(ground);
    byte *p=&texels[4*(w*(h+1)+7)];
    p[0]=1;p[1]=(byte)std::clamp(top,0,255);p[2]=(byte)s.nearest(air);
}
} // namespace doom3d
