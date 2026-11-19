/* Gore's halves (PLAN-gore.md 11): the legs drawn from the monster's own
   death frames with all above the cut cut away, the cut's face showing the
   monster's insides (gore_guts.cpp), what still hangs there of the upper half
   left out (gore_cuts_stock.h). Drawn as wound holes are (woundedQuads), the
   shot holes over it. */
#include "internal.h"
#include "../gore_cuts_stock.h"

namespace doom3d {
// Gore's halves (ragdoll_gore.cpp): the legs drawn from the monster's own death frames, the rest of it
// cut away: every texel above the cut a hole, the cut's edge torn (a dark edge, then red), a row
// higher or lower per column (seeded). cutZ: the cut's height on the monster standing; each drawing is
// cut at that share of its own height (the attachment's rule: a body falling back shortens). The shot
// holes go over it.
bool cutMask(const mobj_t &thing,const Image &image,bool flipped,float rx,float ry,float cutZ,uint32_t seed,std::vector<byte> &mask) {
    if(image.pixels.empty())return false;
    float tall=std::max(1.0f,standingTop(thing)),share=std::clamp(cutZ/tall,0.05f,0.95f);
    int w=image.width,h=image.height;
    mask.assign((size_t)w*h,0);
    auto mark=[&](size_t i,float beyond) {   // beyond: how far past the cut (texels; under 0 short of it)
        if(!image.pixels[i*2+1])return;
        mask[i]=beyond>0?1:beyond>-2.2f?255:beyond>-3.2f?3:0;   // 255: the cut's face, painted below
    };
    // By height in every frame, the lying ones too: Doom's corpses mostly lie feet toward the eye, not
    // across the picture, and cut along the body from one end (as the holes go on lying drawings) the
    // zombieman's thrown and lying frames lost a whole leg down the middle.
    for(int c=0;c<w;++c) {
        float edge=share*image.top+1.6f*(hashUnit(c,5,seed)-0.5f);
        for(int r=0;r<h;++r)mark((size_t)r*w+c,image.top-(r+0.5f)-edge);
    }
    // Under the cut, what is still the upper half's: the hands hanging beside the hips (authored for the
    // stock monsters, gore_cuts_stock.h; neither the pieces the cut leaves nor their colours told the
    // zombieman's hand from his boots).
    std::vector<byte> drop;
    if(doom_stock_cut_drops(w,h,image.pixels.data(),drop))
        for(size_t i=0;i<drop.size();++i)if(drop[i]&&image.pixels[i*2+1])mask[i]=1;
    // The cut's face, seen from a little above: the monster's own meat (gore_guts.cpp), the skin's edge
    // dark at either end of each row, and the two thigh bones where the legs part, pale in its top row.
    const GutsPatch &guts=gutsOf(thing);
    int lo=w,hi=-1;
    for(size_t i=0;i<mask.size();++i)if(mask[i]==255) {int c=(int)(i%w);lo=std::min(lo,c);hi=std::max(hi,c);}
    float femurs[2]={lo+0.3f*(hi-lo),lo+0.7f*(hi-lo)};
    for(size_t i=0;i<mask.size();++i) {
        if(mask[i]!=255)continue;
        int c=(int)(i%w),r=(int)(i/w);
        auto open=[&](int cc,int rr){return cc<0||rr<0||cc>=w||rr>=h||!image.pixels[((size_t)rr*w+cc)*2+1]||mask[(size_t)rr*w+cc]==1;};
        bool top=open(c,r-1),edge=open(c-1,r)||open(c+1,r);
        bool femur=top&&(std::abs(c+0.5f-femurs[0])<0.8f||std::abs(c+0.5f-femurs[1])<0.8f);
        mask[i]=edge?2:femur?guts.bone:gutsColour(guts,c,r,seed);
        if(mask[i]<16&&mask[i]!=2)mask[i]=std::max<byte>(16,guts.dark);   // (palette's first ramp: kept off the states)
    }
    std::vector<byte> holes;
    if(woundMask(thing,image,flipped,rx,ry,holes)) {
        static const byte priority[6]={0,5,4,2,3,1};
        auto rank=[&](byte state){return state>=16?priority[2]:priority[state];};   // a cut face ranks as an edge
        for(size_t i=0;i<mask.size();++i)if(rank(holes[i])>rank(mask[i]))mask[i]=holes[i];
    }
    return true;
}
}
