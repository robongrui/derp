// A soldier firing as it walks (p_gunwalk.h): Doom has no frame for that. Its
// attack frame down to the waist, its run frames below, each from its own
// rotation: the legs turned the way it walks, the chest the way it faces. One
// image of the two, so everything done to a sprite (bends, wounds, light) does
// not know the difference. Each frame has a hip and a cut marked by hand
// (devtools/waist_marks, its export writes waist_marks.inc): the legs keep what
// is below theirs, the torso what is above its own, the hips joined. Feet stay.
#include "internal.h"
#include <algorithm>
#include <cstring>
#include <map>
#include <tuple>
#include <vector>

namespace doom3d {
namespace {
// A frame's cut above its feet, and its hip's column. Unmarked: its offset
// point at this height.
constexpr int waist=26;
constexpr int firstKey=(1<<29)+(1<<28);
struct Mark { const char *name;int hip,cut; };
const Mark marks[]={
#include "waist_marks.inc"
    {nullptr,0,0}
};
Mark markOf(int lump,const Image &image) {
    for(const Mark *m=marks;m->name;++m)if(!strncmp(lumpinfo[lump].name,m->name,8))return *m;
    return {nullptr,image.left,waist};
}
// Where a column lands on screen: a mirrored frame turns inside its own box,
// its offsets as they are.
int screenX(const Image &image,bool flip,int column) {return -image.left+(flip?image.width-1-column:column);}
struct Pair { int torso;bool torsoFlip;int legs;bool legsFlip; };
std::map<std::tuple<int,bool,int,bool>,int> known;
std::vector<Pair> pairs;
}

int waistKey(int torso,bool torsoFlip,int legs,bool legsFlip) {
    auto at=std::make_tuple(torso,torsoFlip,legs,legsFlip);
    auto found=known.find(at);
    if(found!=known.end())return found->second;
    int key=-1-(firstKey+(int)pairs.size());
    pairs.push_back({torso,torsoFlip,legs,legsFlip});
    known.emplace(at,key);
    return key;
}

Image &waistImage(int key) {
    auto found=images.find(key);
    if(found!=images.end())return found->second;
    const Pair &pair=pairs[(size_t)(-1-key-firstKey)];
    const Image &torso=lumpImage(pair.torso,false),&legs=lumpImage(pair.legs,false);
    Mark hipLegs=markOf(pair.legs,legs),hipTorso=markOf(pair.torso,torso);
    // The torso moved onto the legs: they stay where the game has them.
    int shift=screenX(legs,pair.legsFlip,hipLegs.hip)-screenX(torso,pair.torsoFlip,hipTorso.hip);
    int lift=hipLegs.cut-hipTorso.cut;
    int from=std::min(-torso.left+shift,-legs.left),to=std::max(-torso.left+torso.width+shift,-legs.left+legs.width);
    int top=std::max(torso.top+lift,legs.top),bottom=std::min(torso.top-torso.height+lift,legs.top-legs.height);
    int w=to-from,h=top-bottom;
    std::vector<byte> pixels((size_t)w*h*2,0),glow((size_t)w*h,0);
    auto paint=[&](const Image &source,bool flip,bool upper,int cut,int dx,int dy) {
        bool glows=source.emission.size()*2==source.pixels.size();
        for(int y=0;y<source.height;++y) {
            int above=source.top-y;
            if((above>cut)!=upper)continue;
            for(int x=0;x<source.width;++x) {
                size_t i=(size_t)y*source.width+x;
                if(!source.pixels[2*i+1])continue;
                int across=screenX(source,flip,x)+dx-from;
                // The image is mirrored as a whole when drawn, for the torso's sake.
                if(pair.torsoFlip)across=w-1-across;
                size_t o=(size_t)(top-above-dy)*w+across;
                pixels[2*o]=source.pixels[2*i];pixels[2*o+1]=source.pixels[2*i+1];
                glow[o]=glows?source.emission[i]:0;
            }
        }
    };
    paint(legs,pair.legsFlip,false,hipLegs.cut,0,0);
    paint(torso,pair.torsoFlip,true,hipTorso.cut,shift,lift);
    auto normals=spriteNormals(w,h,pixels.data());
    return images.emplace(key,upload(w,h,pixels.data(),-from,top,glow.data(),normals.data())).first->second;
}
}
