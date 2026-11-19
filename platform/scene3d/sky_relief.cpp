/* The painted rock's shadows as depth (sky depth, sky_layers.cpp): what is
   dark in a mountain is in shade, a crevice or a face turned away, so it
   lies back. Each near-rock texel's shade (how much darker it is than the
   lit rock round it, its brightest fifth thirteen texels across, smoothed
   over three) goes into its mask byte's upper six bits, and sky/depth.glsl
   sets the rock's face back by it: the crevices become grooves that shift
   as you walk. Taken from the painting itself: the near layer is de-lit. */
#include "sky_paint.h"

namespace doom3d {
void skyShadowRelief(const SkyImage &s,const std::vector<int> &nearLayer,std::vector<byte> &texels) {
    const int w=s.w,h=s.h;
    std::vector<float> l(w*h),shade(w*h,0);
    for(int i=0;i<w*h;++i){const SkyRgb &c=s.rgb[i];l[i]=0.299f*c.r+0.587f*c.g+0.114f*c.b;}
    std::vector<float> around;
    for(int y=0;y<h;++y)for(int x=0;x<w;++x) {
        if(nearLayer[y*w+x]<0)continue;
        around.clear();
        for(int dy=-6;dy<=6;++dy) {
            int yy=y+dy;if(yy<0||yy>=h)continue;
            for(int dx=-6;dx<=6;++dx){int i=yy*w+((x+dx)%w+w)%w;if(nearLayer[i]>=0)around.push_back(l[i]);}
        }
        size_t k=around.size()*4/5;
        std::nth_element(around.begin(),around.begin()+k,around.end());
        shade[y*w+x]=std::clamp(1-l[y*w+x]/std::max(around[k],8.0f),0.0f,1.0f);
    }
    for(int y=0;y<h;++y)for(int x=0;x<w;++x) {
        if(nearLayer[y*w+x]<0)continue;
        float sum=0;int n=0;
        for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx) {
            int yy=y+dy;if(yy<0||yy>=h)continue;
            int i=yy*w+((x+dx)%w+w)%w;
            if(nearLayer[i]>=0){sum+=shade[i];++n;}
        }
        byte &mask=texels[4*(y*w+x)+3];
        mask=(byte)((mask&3)|(std::lround(sum/n*63)<<2));
    }
}
} // namespace doom3d
