/* Mountains in front of mountains (sky depth, sky_layers.cpp): where two
   painted mountains meet in a deep dip of the crest, one stands in front of
   the other, and the painting shows it as an edge running down from the dip
   to the bottom: the front one's silhouette against the one behind (Doom's
   E2: the left peak in front of the big cliff, the cliff in front of the
   right one). The edge is traced as the strongest near-upright change in
   light from the dip down; the way it leans tells which is in front: it
   runs out over the mountain behind. The front one then carries on past the
   dip under that edge, nearer than the one behind (sky_layers.cpp's skirts,
   sky/depth.glsl), so walking past it slides across it. */
#include "sky_paint.h"

namespace doom3d {
std::vector<SkyOverlap> skyOverlaps(const SkyImage &s,const std::vector<int> &crest,const std::vector<int> &peaks,const std::vector<int> &saddles,float floorRow) {
    const int w=s.w,h=s.h,n=(int)peaks.size();
    std::vector<SkyOverlap> out;
    if(n<2)return out;
    auto wrap=[&](int x){return ((x%w)+w)%w;};
    auto height=[&](int x){return floorRow-crest[wrap(x)];};
    // The light, smoothed over three rows; the change across a texel.
    std::vector<float> l(w*h);
    for(int y=0;y<h;++y)for(int x=0;x<w;++x) {
        float sum=0;int k=0;
        for(int d=-1;d<=1;++d){if(y+d<0||y+d>=h)continue;const SkyRgb &c=s.rgb[(y+d)*w+x];sum+=0.299f*c.r+0.587f*c.g+0.114f*c.b;++k;}
        l[y*w+x]=sum/k;
    }
    auto edge=[&](int x,int y){return std::fabs(l[y*w+wrap(x+1)]-l[y*w+wrap(x-1)]);};
    double typical=0;int count=0;
    for(int y=0;y<h;y+=2)for(int x=0;x<w;x+=2){typical+=edge(x,y);++count;}
    typical/=std::max(count,1);
    constexpr int R=64;
    for(int i=0;i<n;++i) {
        int dip=saddles[i];
        // Only a deep dip parts two mountains: a notch near a tall one's top
        // is its own crevice.
        float taller=std::max(height(peaks[i]),height(peaks[(i+1)%n]));
        int y0=crest[dip];
        if(height(dip)>=0.85f*taller||y0>=h-8)continue;
        // The best path from the dip to the bottom, up to two columns a row,
        // R either way at most.
        std::vector<float> score(2*R+1,-1e30f),next(2*R+1);
        std::vector<std::vector<signed char>> back(h,std::vector<signed char>(2*R+1,0));
        for(int o=-2;o<=2;++o)score[o+R]=edge(dip+o,y0);
        for(int y=y0+1;y<h;++y) {
            for(int o=-R;o<=R;++o) {
                float best=-1e30f;int arg=0;
                for(int d=-2;d<=2;++d) {
                    int p=o-d;
                    if(p<-R||p>R||score[p+R]<-1e29f)continue;
                    float v=score[p+R]-1.5f*std::abs(d);
                    if(v>best){best=v;arg=d;}
                }
                next[o+R]=best<-1e29f?best:best+edge(dip+o,y);
                back[y][o+R]=(signed char)arg;
            }
            score.swap(next);
        }
        int o=0;
        for(int k=-R;k<=R;++k)if(score[k+R]>score[o+R])o=k;
        std::vector<int> path(h,0);
        for(int y=h-1;y>=y0;--y){path[y]=o;if(y>y0)o-=back[y][o+R];}
        float strength=0;
        for(int y=y0;y<h;++y)strength+=edge(dip+path[y],y);
        strength/=(h-y0);
        int lean=path[h-1];
        if(strength<2.5f*typical||std::abs(lean)<10)continue;
        // Leaning right, it runs out over the right one: the left one is in
        // front, and in each column it reaches, starts where it crosses.
        SkyOverlap ov;
        int step=lean>0?1:-1;
        ov.front=lean>0?i:(i+1)%n;ov.back=lean>0?(i+1)%n:i;ov.side=lean>0?1:0;
        ov.top.assign(w,-1);
        int reached=0;
        for(int y=y0;y<h;++y) {
            int far=path[y]*step;
            for(int c=reached+1;c<=far;++c)ov.top[wrap(dip+step*c)]=y;
            reached=std::max(reached,far);
        }
        out.push_back(std::move(ov));
    }
    return out;
}
} // namespace doom3d
