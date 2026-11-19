/* A spire standing in an open sky (sky depth, sky_layers.cpp; Heretic's
   third sky): something tall and hard-edged in a painting of clouds alone.
   The clouds go to the engine as any cloud sky's do; the spire is cut out
   along its edges, row by row, and sky/spire.glsl raises it as one real
   tower far off, round, as wide at each height as it is painted. */
#include "sky_paint.h"

namespace doom3d {
namespace {
// Each row's sky: the mean of its texels, and how far a texel may stray
// from it and still be sky (the row's typical spread, at least 60).
void rowSky(const SkyImage &s,int y,SkyRgb &mean,float &spread) {
    mean={0,0,0};
    for(int x=0;x<s.w;++x){const SkyRgb &c=s.rgb[y*s.w+x];mean.r+=c.r/s.w;mean.g+=c.g/s.w;mean.b+=c.b/s.w;}
    std::vector<float> d(s.w);
    for(int x=0;x<s.w;++x)d[x]=skyDist(s.rgb[y*s.w+x],mean);
    std::nth_element(d.begin(),d.begin()+s.w/2,d.end());
    spread=std::max(60.0f,3*d[s.w/2]);
}
}
// The spire's columns per row (left[y] > right[y]: none in that row): from
// the column with the longest hard vertical edge, each row's run of texels
// that stray from its sky, followed from the row below up, the run's middle
// carried on as the spire narrows or widens. False unless it stands from
// the bottom up through at least half the painting.
bool findSpire(const SkyImage &s,std::vector<int> &left,std::vector<int> &right) {
    if(s.w>256||s.h>s.w)return false;   // its columns are kept in bytes, its rows along a texture row
    int column=0,longest=0;
    for(int x=0;x<s.w;++x)for(int y=0,run=0;y<s.h;++y) {
        run=skyDist(s.rgb[y*s.w+x],s.rgb[y*s.w+(x+1)%s.w])>90?run+1:0;
        if(run>longest){longest=run;column=x;}
    }
    left.assign(s.h,1);right.assign(s.h,0);
    int middle=column,rows=0;
    for(int y=s.h-1;y>=0;--y) {
        SkyRgb mean;float spread;
        rowSky(s,y,mean,spread);
        auto stray=[&](int x){return skyDist(s.rgb[y*s.w+((x%s.w)+s.w)%s.w],mean)>spread;};
        int at=-1;
        for(int d=0;d<=6&&at<0;++d){if(stray(middle+d))at=middle+d;else if(stray(middle-d))at=middle-d;}
        if(at<0)break;
        int l=at,r=at;
        while(r-l<s.w/4&&stray(l-1))--l;
        while(r-l<s.w/4&&stray(r+1))++r;
        left[y]=l;right[y]=r;middle=(l+r)/2;++rows;
    }
    // Where ground stands behind its foot the edges come and go; a tower's
    // width doesn't change from row to row. Each row takes the median of those
    // round it.
    auto settle=[&](std::vector<int> &run) {
        std::vector<int> out=run;
        for(int y=0;y<s.h;++y) {
            if(right[y]<left[y])continue;
            std::vector<int> near;
            for(int d=-7;d<=7;++d)if(y+d>=0&&y+d<s.h&&right[y+d]>=left[y+d])near.push_back(run[y+d]);
            std::nth_element(near.begin(),near.begin()+near.size()/2,near.end());
            out[y]=near[near.size()/2];
        }
        return out;
    };
    std::vector<int> l=settle(left),r=settle(right);
    left=l;right=r;
    return rows>=s.h/2&&right[s.h-1]>=left[s.h-1];
}
// The spire into sky_layers.cpp's texture (w x h+skyLayerRows): its texels in the near
// layer (mask bit 2), each row's run in the near towers' row (R left, G
// right, A 255: columns from its foot's middle, 128 that middle, so a run
// may cross the painting's edge), and the settings' texel 6: R 1 (a spire),
// G the top row it reaches, B its foot's middle column.
void spireTexels(const SkyImage &s,const std::vector<int> &left,const std::vector<int> &right,std::vector<byte> &texels) {
    int top=s.h,foot=(left[s.h-1]+right[s.h-1])/2;
    for(int y=0;y<s.h;++y) {
        if(right[y]<left[y])continue;
        top=std::min(top,y);
        for(int x=left[y];x<=right[y];++x) {
            int c=((x%s.w)+s.w)%s.w;
            byte *t=&texels[4*(y*s.w+c)];
            t[2]=s.index[y*s.w+c];t[3]|=2;
        }
        byte *run=&texels[4*(s.w*(s.h+2)+y)];
        run[0]=(byte)std::clamp(left[y]-foot+128,0,255);run[1]=(byte)std::clamp(right[y]-foot+128,0,255);run[3]=255;
    }
    byte *p=&texels[4*(s.w*(s.h+1)+6)];
    p[0]=1;p[1]=(byte)std::min(top,255);p[2]=(byte)(((foot%s.w)+s.w)%s.w);
}
} // namespace doom3d
