/* The fat wobbles (gore, drawing only): a shot pushes a fat monster's flesh in where it
   lands, it swings back out and rings down over a second or so, fast hit, slow recovery.
   Whole texels, a picture per amount, stepped with the tics; nothing is drawn while
   it isn't moving. The texels' colour tells flesh from metal, so the cannons and their
   hoses stay still in every drawing with no rig to say which is which. */
#include "internal.h"

namespace doom3d {
constexpr int wobbleKeyBase=-0x74000000;
constexpr float wobblePeriod=12,wobbleFade=22;   // tics a swing takes, and to lose two thirds of it
constexpr int wobbleLeast=1,wobbleMost=3;        // texels at its furthest
constexpr float wobbleReach=6,wobbleReachMore=1;   // how far round it the flesh moves, and more per texel of swing
constexpr int wobbleCell=4;                      // hits this close share their pictures
struct Wobble { float across,height;int tic,peak; };
static std::unordered_map<const mobj_t*,Wobble> wobbles;
static std::map<std::tuple<int,int,int,int>,int> pictures;   // lump, cell across, cell up, swing: the picture's key

static int swingAt(const Wobble &w,int age) {
    return (int)std::lround(-w.peak*std::cos(2*(float)doomPi*age/wobblePeriod)*std::exp(-age/wobbleFade));
}
// across, height: where it landed, from the drawing's origin and its feet.
void fatHit(const mobj_t *thing,float across,float height,int damage) {
    if(!thing||thing->health<=0||!goreOn()||!Game_BodyFat(thing->type))return;
    int peak=std::clamp(1+damage/8,wobbleLeast,wobbleMost);
    auto found=wobbles.find(thing);
    if(found!=wobbles.end()&&std::abs(swingAt(found->second,leveltime-found->second.tic))>peak)return;   // already swinging harder
    wobbles[thing]={across,height,leveltime,peak};
}
void forgetWobble(const mobj_t *thing) {wobbles.erase(thing);}

// Where a texel's colour comes from at this swing: out from the hit, most a reach away, nothing at the hit itself
// or far off, looked up backward, nearest texel. False: it doesn't move.
static bool lookup(float cx,float cz,int swing,int c,int r,int &sc,int &sr) {
    float reach=wobbleReach+wobbleReachMore*std::abs(swing),dx=c+0.5f-cx,dz=r+0.5f-cz,d=std::hypot(dx,dz),t=d/reach;
    if(d<1e-3f||t>2.2f)return false;
    float move=swing*t*std::exp(0.5f*(1-t*t))/d;
    sc=(int)std::floor(c+0.5f-dx*move);sr=(int)std::floor(r+0.5f-dz*move);
    return true;
}
static bool metalAt(const Image &image,const byte *palette,int c,int r) {
    if(c<0||r<0||c>=image.width||r>=image.height||!image.pixels[((size_t)r*image.width+c)*2+1])return false;
    const byte *rgb=palette+image.pixels[((size_t)r*image.width+c)*2]*3;
    int hi=std::max({rgb[0],rgb[1],rgb[2]}),lo=std::min({rgb[0],rgb[1],rgb[2]});
    return hi-lo<24&&hi>=48;
}
static int picture(int lump,const Image &image,int cellX,int cellZ,int swing) {
    auto id=std::make_tuple(lump,cellX,cellZ,swing);
    auto found=pictures.find(id);
    if(found!=pictures.end())return found->second;
    const byte *palette=(const byte*)W_CacheLumpName((char*)"PLAYPAL",PU_CACHE);
    int w=image.width,h=image.height;
    auto solid=[&](int c,int r){return c>=0&&r>=0&&c<w&&r<h&&image.pixels[((size_t)r*w+c)*2+1];};
    auto metal=[&](int c,int r) {return metalAt(image,palette,c,r);};
    float cx=(cellX+0.5f)*wobbleCell,cz=(cellZ+0.5f)*wobbleCell;
    std::vector<byte> pixels=image.pixels;
    for(int r=0;r<h;++r)for(int c=0;c<w;++c) {
        if(metal(c,r))continue;
        int sc,sr;
        if(!lookup(cx,cz,swing,c,r,sc,sr))continue;
        size_t i=((size_t)r*w+c)*2;
        if(solid(sc,sr)&&!metal(sc,sr)) {pixels[i]=image.pixels[((size_t)sr*w+sc)*2];pixels[i+1]=255;}
        else if(solid(c,r)&&!solid(sc,sr))pixels[i+1]=0;
    }
    std::vector<byte> normals=spriteNormals(w,h,pixels.data());
    int key=wobbleKeyBase-(int)pictures.size();
    images.emplace(key,upload(w,h,pixels.data(),image.left,image.top,nullptr,normals.data()));
    pictures[id]=key;
    return key;
}
// The picture to draw this drawing with now (0: its own).
int fatWobbleKey(const mobj_t &thing,int lump,const Image &image) {
    if(wobbles.empty())return 0;
    auto found=wobbles.find(&thing);
    if(found==wobbles.end())return 0;
    const Wobble &wobble=found->second;
    int age=leveltime-wobble.tic;
    if(age<0||age>4*wobbleFade||thing.health<=0) {wobbles.erase(found);return 0;}
    int swing=swingAt(wobble,age);
    if(!swing||image.pixels.empty())return 0;
    int c=(int)std::floor((image.left+wobble.across)/wobbleCell),r=(int)std::floor((image.top-wobble.height)/wobbleCell);
    return picture(lump,image,c,r,swing);
}
// The wounds ride the flesh: their mask looked up as the picture was.
void fatWobbleMask(const mobj_t &thing,const Image &image,std::vector<byte> &mask) {
    if(wobbles.empty())return;
    auto found=wobbles.find(&thing);
    if(found==wobbles.end())return;
    int swing=swingAt(found->second,leveltime-found->second.tic),w=image.width,h=image.height;
    if(!swing||mask.size()!=(size_t)w*h)return;
    const byte *palette=(const byte*)W_CacheLumpName((char*)"PLAYPAL",PU_CACHE);
    float cx=(std::floor((image.left+found->second.across)/wobbleCell)+0.5f)*wobbleCell;
    float cz=(std::floor((image.top-found->second.height)/wobbleCell)+0.5f)*wobbleCell;
    std::vector<byte> moved=mask;
    for(int r=0;r<h;++r)for(int c=0;c<w;++c) {
        int sc,sr;
        if(metalAt(image,palette,c,r)||!lookup(cx,cz,swing,c,r,sc,sr))continue;
        moved[(size_t)r*w+c]=sc>=0&&sr>=0&&sc<w&&sr<h&&!metalAt(image,palette,sc,sr)?mask[(size_t)sr*w+sc]:0;
    }
    mask.swap(moved);
}
void wobblesShutdown() {
    wobbles.clear();
    for(const auto &entry:pictures) {
        images.erase(entry.second);
        for(auto *group:{&batches,&mirrorBatches})group->erase(entry.second);
    }
    pictures.clear();
}
}
