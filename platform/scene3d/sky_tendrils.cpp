/* A hanging hell (sky_hell.cpp's third kind; the 2025 Heretic's fourth sky):
   black tendrils hanging from a black top into a red sky. Each tendril is
   found here and moves on its own (sky/hell.glsl): its root in the black
   stays, the further down the more it swings, each at its own pace, and the
   red round it goes along with it, fading out before the next tendril's, so
   nothing tears. One wave through the whole sky moved them all as one sheet. */
#include "sky_paint.h"

namespace doom3d {
namespace {
std::vector<float> boxMax(const std::vector<float> &a,int w,int h,int rx,int ry) {
    std::vector<float> t(w*h),out(w*h);
    for(int y=0;y<h;++y)for(int x=0;x<w;++x){float v=a[y*w+x];for(int d=-rx;d<=rx;++d)v=std::max(v,a[y*w+((x+d)%w+w)%w]);t[y*w+x]=v;}
    for(int y=0;y<h;++y)for(int x=0;x<w;++x){float v=t[y*w+x];for(int d=-ry;d<=ry;++d)v=std::max(v,t[std::clamp(y+d,0,h-1)*w+x]);out[y*w+x]=v;}
    return out;
}
std::vector<float> boxMean(const std::vector<float> &a,int w,int h,int rx,int ry) {
    std::vector<float> t(w*h),out(w*h);
    for(int y=0;y<h;++y)for(int x=0;x<w;++x){float v=0;for(int d=-rx;d<=rx;++d)v+=a[y*w+((x+d)%w+w)%w];t[y*w+x]=v/(2*rx+1);}
    for(int y=0;y<h;++y)for(int x=0;x<w;++x){float v=0;for(int d=-ry;d<=ry;++d)v+=t[std::clamp(y+d,0,h-1)*w+x];out[y*w+x]=v/(2*ry+1);}
    return out;
}
}

// crest: the first rock row per column. Into texels (sky_layers.cpp's
// texture): G how far each texel swings with its tendril (0-255), the mask
// byte's upper six bits which tendril (1-63) where no rock is.
void bakeTendrils(const SkyImage &s,const std::vector<int> &crest,std::vector<byte> &texels) {
    const int w=s.w,h=s.h;
    std::vector<float> red(w*h);
    for(int i=0;i<w*h;++i)red[i]=s.rgb[i].r;
    // The black they hang from: the rows nearly all black.
    int top=0;
    for(;top<h;++top){int black=0;for(int x=0;x<w;++x)black+=red[top*w+x]<40;if(black<=0.85f*w)break;}
    // A tendril: well darker than the brightest red about it, over a span
    // about as wide as a tendril.
    std::vector<float> ref=boxMean(boxMax(red,w,h,7,4),w,h,4,3);
    std::vector<byte> dark(w*h,0);
    for(int y=top;y<h;++y)for(int x=0;x<w;++x) {
        int i=y*w+x;
        dark[i]=y<crest[x]-2&&ref[i]>50&&red[i]<0.62f*ref[i];
    }
    // Each one its own component (8-connected, round the horizon); specks
    // under 25 texels stay put.
    std::vector<int> label(w*h,0);
    std::vector<std::vector<int>> parts;
    for(int i=0;i<w*h;++i) {
        if(!dark[i]||label[i])continue;
        std::vector<int> part,stack{i};label[i]=-1;
        while(!stack.empty()) {
            int j=stack.back();stack.pop_back();part.push_back(j);
            int x=j%w,y=j/w;
            for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx) {
                int ny=y+dy;if(ny<0||ny>=h)continue;
                int k=ny*w+((x+dx)%w+w)%w;
                if(dark[k]&&!label[k]){label[k]=-1;stack.push_back(k);}
            }
        }
        if(part.size()>=25)parts.push_back(part);
    }
    // Round each, the nearest tendril and how far (d1), and how far the next
    // nearest (d2): a texel swings with its nearest, fully up to two texels
    // off it, fading out by ten, and to nothing where the next one's reach
    // begins.
    const float reach=10;
    std::vector<float> d1(w*h,1e9f),d2(w*h,1e9f);
    std::vector<int> nearest(w*h,0);
    for(size_t p=0;p<parts.size();++p) {
        int lo=h,hi=0;
        for(int j:parts[p]){lo=std::min(lo,j/w);hi=std::max(hi,j/w);}
        std::vector<float> d(w*h,1e9f);
        for(int j:parts[p]) {
            int px=j%w,py=j/w;
            for(int dy=-(int)reach;dy<=(int)reach;++dy) {
                int y=py+dy;if(y<0||y>=h)continue;
                for(int dx=-(int)reach;dx<=(int)reach;++dx) {
                    int k=y*w+((px+dx)%w+w)%w;
                    d[k]=std::min(d[k],std::sqrt((float)(dx*dx+dy*dy)));
                }
            }
        }
        for(int i=0;i<w*h;++i) {
            if(d[i]>=1e8f)continue;
            if(d[i]<d1[i]){d2[i]=d1[i];d1[i]=d[i];nearest[i]=(int)p+1;}
            else d2[i]=std::min(d2[i],d[i]);
        }
    }
    for(int i=0;i<w*h;++i) {
        byte *t=&texels[4*i];
        int y=i/w;
        if(!nearest[i]||(t[3]&3)||y<top)continue;
        float near=d1[i]<=2?1.0f:std::clamp(1-(d1[i]-2)/(reach-2),0.0f,1.0f);
        float apart=std::clamp((d2[i]-d1[i])/4.0f,0.0f,1.0f);
        // Still at the black, swinging more the further down.
        float down=std::pow(std::clamp((y-top)/60.0f,0.0f,1.0f),1.2f);
        t[1]=(byte)std::lround(255*near*apart*down);
        t[3]=(byte)(t[3]|((1+(nearest[i]-1)%63)<<2));
    }
}
} // namespace doom3d
