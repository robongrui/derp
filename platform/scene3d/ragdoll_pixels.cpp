/* A ragdoll's picture from its faces (ragdoll_card.cpp): F4's Ragdoll pixels.

   The card redraws the posed body into a sprite each time it moves. Every face is a texel of the
   drawing, so as the body turns a face covers no pixel centre in one picture and two in the next:
   thin parts (a rifle, an arm) flicker and the inside shuffles, the pixel crawl id never had, their
   frames being drawn one by one. Measured on arena kills (zombies and imps, rockets and the SSG,
   2026-11-16), outline blinks plus the colour blinks an eye sees, a share of the body a picture:

     Nearest            8.5-10.9%   each pixel the face at its centre (as it always was)
     Anchored           7.9-9.9%    the same, the hips on a pixel corner (ragdoll_card.cpp)
     Coverage           7.6-9.8%    4x4 samples a pixel, the colour most of them hold
     Coverage, steady   4.6-7.6%    and a pixel changes only when the new picture is sure
     Blended            6.1-9.4%    the samples' colours averaged to the nearest palette colour
     Blended, steady    4.3-7.3%    (a photo scaled down, as id's sprites were: softer)

   Steady: solid from 70% coverage, empty under 30%, between them as it was; a pixel keeps its
   colour while that still holds a quarter of its samples. Blended can't blend the outline: a
   sprite pixel is there or not. */
#include "internal.h"
#include "ragdoll_pixels.h"

namespace doom3d {
namespace {
const int fine=4;   // samples a side, past Anchored
// The nearest palette colour to an average, by 5 bits a channel, found once each.
unsigned char nearestPalette(int r,int g,int b) {
    static const unsigned char *palette=(const unsigned char*)W_CacheLumpName((char*)"PLAYPAL",PU_STATIC);
    static std::vector<int16_t> found(32768,-1);
    int key=(r>>3)<<10|(g>>3)<<5|(b>>3);
    if(found[key]<0) {
        int best=0,bestDistance=INT_MAX,cr=(r>>3)*8+4,cg=(g>>3)*8+4,cb=(b>>3)*8+4;
        for(int i=0;i<256;++i) {
            int dr=palette[i*3]-cr,dg=palette[i*3+1]-cg,db=palette[i*3+2]-cb,d=dr*dr+dg*dg+db*db;
            if(d<bestDistance) {bestDistance=d;best=i;}
        }
        found[key]=(int16_t)best;
    }
    return (unsigned char)found[key];
}
}

void cardPixels(const std::vector<CardQuad> &quads,int size,int mode,const std::vector<unsigned char> *prev,int dx,int dy,
                std::vector<unsigned char> &pixels) {
    int ss=mode>=CARD_COVERAGE?fine:1,n=size*ss;
    static std::vector<float> depth;depth.assign((size_t)n*n,1e9f);
    static std::vector<int16_t> sample;sample.assign((size_t)n*n,-1);
    for(const CardQuad &q:quads) {
        float px[4],py[4];
        for(int k=0;k<4;++k) {px[k]=q.x[k]*ss;py[k]=q.y[k]*ss;}
        for(int t=0;t<2;++t) {
            int i0=0,i1=t?2:1,i2=t?3:2;
            float area=(px[i1]-px[i0])*(py[i2]-py[i0])-(px[i2]-px[i0])*(py[i1]-py[i0]);
            if(std::abs(area)<1e-9f)continue;
            // The samples whose centres the triangle's box holds; most faces are a texel or two.
            int x0=std::max(0,(int)std::ceil(std::min({px[i0],px[i1],px[i2]})-0.5f)),x1=std::min(n-1,(int)std::floor(std::max({px[i0],px[i1],px[i2]})-0.5f));
            int y0=std::max(0,(int)std::ceil(std::min({py[i0],py[i1],py[i2]})-0.5f)),y1=std::min(n-1,(int)std::floor(std::max({py[i0],py[i1],py[i2]})-0.5f));
            float turn=area>0?1.0f:-1.0f;
            for(int j=y0;j<=y1;++j)for(int i=x0;i<=x1;++i) {
                float cx=i+0.5f,cy=j+0.5f;
                float e0=(px[i1]-cx)*(py[i2]-cy)-(px[i2]-cx)*(py[i1]-cy);
                if(e0*turn<0)continue;
                float e1=(px[i2]-cx)*(py[i0]-cy)-(px[i0]-cx)*(py[i2]-cy);
                if(e1*turn<0)continue;
                float w0=e0/area,w1=e1/area,w2=1-w0-w1;
                if(w2<0)continue;
                float d=w0*q.d[i0]+w1*q.d[i1]+w2*q.d[i2];
                size_t at=(size_t)j*n+i;
                if(d<depth[at]) {depth[at]=d;sample[at]=q.colour;}
            }
        }
    }
    pixels.assign((size_t)size*size*2,0);
    if(ss==1) {
        for(size_t at=0;at<(size_t)size*size;++at)if(sample[at]>=0) {pixels[at*2]=(unsigned char)sample[at];pixels[at*2+1]=255;}
        return;
    }
    static const unsigned char *palette=(const unsigned char*)W_CacheLumpName((char*)"PLAYPAL",PU_STATIC);
    bool steady=cardSteady(mode)&&prev&&prev->size()==pixels.size(),blended=mode>=CARD_BLENDED;
    for(int j=0;j<size;++j)for(int i=0;i<size;++i) {
        int colours[fine*fine],counts[fine*fine],kinds=0,covered=0,r=0,g=0,b=0;
        for(int y=0;y<ss;++y)for(int x=0;x<ss;++x) {
            int v=sample[(size_t)(j*ss+y)*n+i*ss+x];
            if(v<0)continue;
            ++covered;r+=palette[v*3];g+=palette[v*3+1];b+=palette[v*3+2];
            int k=0;while(k<kinds&&colours[k]!=v)++k;
            if(k==kinds) {colours[kinds]=v;counts[kinds++]=0;}
            ++counts[k];
        }
        int was=-1,pi=i-dx,pj=j-dy;
        if(steady&&pi>=0&&pj>=0&&pi<size&&pj<size&&(*prev)[((size_t)pj*size+pi)*2+1])was=(*prev)[((size_t)pj*size+pi)*2];
        int samples=ss*ss;
        bool solid=steady?(was>=0?covered*10>=samples*3:covered*10>=samples*7):covered*2>=samples;
        if(!solid||!covered)continue;
        int pick;
        if(blended)pick=nearestPalette(r/covered,g/covered,b/covered);
        else {int best=0;for(int k=1;k<kinds;++k)if(counts[k]>counts[best])best=k;pick=colours[best];}
        // Steady: kept while its colour still holds a quarter of the samples (blended: its average already calm).
        if(steady&&!blended&&was>=0)for(int k=0;k<kinds;++k)if(colours[k]==was&&counts[k]*4>=covered)pick=was;
        size_t at=(size_t)j*size+i;
        pixels[at*2]=(unsigned char)pick;pixels[at*2+1]=255;
    }
}
}
