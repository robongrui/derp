/* Blood on the gun (gore): drops that reach the player land on the weapon, a kill up
   close leaves spots, a chainsaw or a fist gets a coat. A splat belongs to the art it
   landed on: on the frame it struck, then found again on every other frame of the
   weapon by the patch of art under it (wound_attachment.h, as wounds follow a monster),
   so it goes round with the super shotgun's barrels as they break open, and is hidden
   on frames that don't show that part. Fresh it is bright, a run or two creeps down in
   whole pixels, it dries dark and is gone in under twenty seconds. The sights stay clear.
   Drawing only. */
#include "internal.h"
#include "../wound_attachment.h"

namespace doom3d {
constexpr int gunKeyBase=-0x6f000000;
constexpr int wetTics=105,dryTics=350,goneTics=525;   // 3 s bright, dark by 10, going from 15 (the user: it shouldn't be too obvious)
constexpr size_t mostBlobs=70,mostMelee=220;
constexpr float attachFit=0.85f;            // a patch matching worse than this isn't that part: hidden there
constexpr int attachPerFrame=6;             // finding a splat on a new frame costs: this many a frame drawn
struct Spot { float u=-1,v=-1; };           // on a frame (u below 0: not on it)
struct Blob {
    int source;float u,v;byte tint;int wet;
    std::vector<std::pair<signed char,signed char>> cells;   // texels of it, from its middle
    std::unordered_map<int,Spot> spots;                      // by frame lump
};
struct GunLayer { std::vector<Blob> blobs;int serial=0,stepped=0; };
static std::unordered_map<int,GunLayer> layers;
struct Picture { int serial=-1,weapon=-1; };
static std::unordered_map<int,Picture> pictures;   // by lump
static int frameLump=-1;

static doom_wounds::Frame art(const Image &image) {return {image.width,image.height,image.left,image.top,image.pixels.data()};}
static bool solid(const Image &image,int c,int r) {return c>=0&&r>=0&&c<image.width&&r<image.height&&image.pixels[((size_t)r*image.width+c)*2+1];}
static bool sights(const Image &image,int c,int r) {return r<image.height/4&&std::abs(c-image.width/2)<image.width/6;}

// A blob on the frame showing, somewhere it can be, radius in texels.
static void splat(int tint,int radius,size_t most) {
    if(frameLump<0||tint==BLOOD_NONE)return;
    const Image &image=lumpImage(frameLump,false);
    GunLayer &layer=layers[(int)players[consoleplayer].readyweapon];
    for(int tries=0;tries<12;++tries) {
        int c=(int)randomRange(0,image.width-0.01f),r=(int)randomRange(0,image.height-0.01f);
        if(!solid(image,c,r)||sights(image,c,r))continue;
        if(layer.blobs.size()>=most)layer.blobs.erase(layer.blobs.begin());
        Blob blob{frameLump,c+0.5f,r+0.5f,(byte)tint,leveltime,{},{}};
        uint32_t seed=hash32((uint32_t)c*73856093u^(uint32_t)r*19349663u^(uint32_t)leveltime);
        for(int dr=-radius;dr<=radius;++dr)for(int dc=-radius;dc<=radius;++dc)
            if(std::hypot((float)dc,(float)dr)<=radius*(0.55f+0.6f*hashUnit(dc,dr,seed)))blob.cells.push_back({(signed char)dc,(signed char)dr});
        blob.spots[frameLump]={blob.u,blob.v};
        layer.blobs.push_back(std::move(blob));
        ++layer.serial;
        return;
    }
}
void gunBloodDrop(int tint,float size) {
    static int tic=-1,count=0;
    if(tic!=leveltime) {tic=leveltime;count=0;}
    if(++count>3)return;
    splat(tint,size>=1.4f?2:1,mostBlobs);
}
void gunBloodMelee(int tint,bool blade) {
    for(int n=0,hits=blade?3:2;n<hits;++n)splat(tint,blade?3:2,mostMelee);
}
// A blob's place on a frame, found once: false while it hasn't been looked for yet (budget is what's left this frame).
static bool spotOn(Blob &blob,int lump,const Image &image,int &budget,Spot &spot) {
    auto found=blob.spots.find(lump);
    if(found!=blob.spots.end()) {spot=found->second;return true;}
    if(budget<=0)return false;
    --budget;
    const Image &source=lumpImage(blob.source,false);
    doom_wounds::Point at=doom_wounds::map(art(source),art(image),{blob.u,blob.v});
    int sx=(int)std::floor(blob.u),sy=(int)std::floor(blob.v),x=(int)std::floor(at.u),y=(int)std::floor(at.v);
    spot={};
    if(solid(image,x,y)&&doom_wounds::detail::patchCost(art(source),art(image),sx,sy,x,y)<=attachFit)spot={at.u,at.v};
    blob.spots[lump]=spot;
    return true;
}
// The runs while wet, every few tics, and old blobs going a texel at a time.
static void step(GunLayer &layer) {
    if(leveltime<layer.stepped)layer.stepped=leveltime;
    if(leveltime-layer.stepped<6)return;
    layer.stepped=leveltime;
    for(Blob &blob:layer.blobs) {
        if(blob.wet>leveltime)blob.wet=leveltime;   // a new game's clock
        int age=leveltime-blob.wet;
        if(age<wetTics&&!blob.cells.empty()&&hashUnit(blob.wet,leveltime,0x5eedu)<0.08f) {
            auto low=*std::max_element(blob.cells.begin(),blob.cells.end(),[](const auto &a,const auto &b){return a.second<b.second;});
            if(low.second<40)blob.cells.push_back({low.first,(signed char)(low.second+1)});
            ++layer.serial;
        }
        if(age>=goneTics&&!blob.cells.empty()) {blob.cells.erase(blob.cells.begin()+(size_t)(hashUnit(age,1,0xdeadu)*blob.cells.size()));++layer.serial;}
    }
    layer.blobs.erase(std::remove_if(layer.blobs.begin(),layer.blobs.end(),[](const Blob &b){return b.cells.empty();}),layer.blobs.end());
    if(leveltime%35<6&&!layer.blobs.empty())++layer.serial;   // darkening as it dries
}
// The picture over the frame lump: null while the weapon is clean.
Image *gunBloodImage(int lump) {
    frameLump=lump;
    if(!goreOn())return nullptr;
    int weapon=(int)players[consoleplayer].readyweapon;
    auto found=layers.find(weapon);
    if(found==layers.end()||found->second.blobs.empty())return nullptr;
    GunLayer &layer=found->second;
    step(layer);
    if(layer.blobs.empty())return nullptr;
    const Image &source=lumpImage(lump,false);
    int key=gunKeyBase-lump;
    Picture &picture=pictures[lump];
    auto image=images.find(key);
    if(image!=images.end()&&picture.serial==layer.serial&&picture.weapon==weapon)return &image->second;
    int w=source.width,h=source.height,budget=attachPerFrame;
    bool waiting=false;
    std::vector<byte> pixels((size_t)w*h*2,0);
    for(Blob &blob:layer.blobs) {
        Spot spot;
        if(!spotOn(blob,lump,source,budget,spot)) {waiting=true;continue;}
        if(spot.u<0)continue;
        const auto &shades=bloodPalette(blob.tint);
        if(shades.empty())continue;
        int age=leveltime-blob.wet,c0=(int)std::floor(spot.u),r0=(int)std::floor(spot.v);
        float dry=std::clamp((age-wetTics)/(float)(dryTics-wetTics),0.0f,1.0f),shade=0.45f+(0.22f-0.45f)*dry;
        for(const auto &cell:blob.cells) {
            int c=c0+cell.first,r=r0+cell.second;
            if(!solid(source,c,r))continue;
            float here=age<wetTics&&hashUnit(cell.first,cell.second,(uint32_t)blob.wet)<0.18f?0.82f:shade;   // wet: its bright specks
            pixels[((size_t)r*w+c)*2]=shades[(size_t)std::lround(here*(shades.size()-1))];
            pixels[((size_t)r*w+c)*2+1]=255;
        }
    }
    std::vector<byte> normals=spriteNormals(w,h,pixels.data());
    if(image==images.end())image=images.emplace(key,upload(w,h,pixels.data(),0,0,nullptr,normals.data())).first;
    else {
        std::vector<byte> packed((size_t)w*h*4);
        for(size_t i=0;i<(size_t)w*h;++i) {packed[4*i]=pixels[2*i];packed[4*i+1]=pixels[2*i+1];packed[4*i+2]=normals[2*i];packed[4*i+3]=normals[2*i+1];}
        gpuUpdateTexture(image->second.texture,GpuFormat::RGBA8,{{0,0,w,h}},packed.data(),w*4);
        image->second.pixels=std::move(pixels);
    }
    picture.serial=waiting?-1:layer.serial;picture.weapon=weapon;   // (some still to find: drawn again next frame)
    return &image->second;
}
}
