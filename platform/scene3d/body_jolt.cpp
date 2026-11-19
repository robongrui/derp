/* The struck part jolts (gore, drawing only): a limb or a head a shot lands on snaps
   out of the drawing a few whole texels and comes back over a few tics, the rest of
   the body staying where it is. The skeleton says which texels are that part. Each
   shifted picture is made once and kept: frames, parts and shifts are few. */
#include "internal.h"

namespace doom3d {
constexpr int joltKeyBase=-0x72000000;
constexpr int joltFull=2,joltBack=8;       // tics at full, tics coming back
constexpr int joltLeast=2,joltMost=3;      // texels at its furthest
struct Jolt { int bone,tic,peak; };
static std::unordered_map<const mobj_t*,Jolt> jolts;
static std::map<std::tuple<int,int,int,int>,int> pictures;   // lump, bone, shift across, shift up: the picture's key

void bodyJolt(const mobj_t *thing,int bone,int damage) {
    if(!thing||thing->health<=0||bone<0||bone==SB_PELVIS||bone==SB_SPINE)return;   // the trunk has the flinch
    jolts[thing]={bone,leveltime,std::clamp(1+damage/8,joltLeast,joltMost)};
}
void forgetJolt(const mobj_t *thing) {jolts.erase(thing);}
// What moves with a bone: it and what hangs from it.
static bool moves(int bone,int owner) {
    if(owner==bone)return true;
    return (bone==SB_UPPER0&&owner==SB_FORE0)||(bone==SB_UPPER1&&owner==SB_FORE1)||(bone==SB_THIGH0&&owner==SB_SHIN0)||(bone==SB_THIGH1&&owner==SB_SHIN1);
}
static int picture(int lump,const Image &image,const SpriteSkeleton &s,int bone,int dx,int dy) {
    auto id=std::make_tuple(lump,bone,dx,dy);
    auto found=pictures.find(id);
    if(found!=pictures.end())return found->second;
    int w=image.width,h=image.height;
    auto solid=[&](int c,int r){return c>=0&&r>=0&&c<w&&r<h&&image.pixels[((size_t)r*w+c)*2+1];};
    auto part=[&](int c,int r){return moves(bone,s.owner[(size_t)r*w+c]);};
    std::vector<byte> pixels=image.pixels;
    // The part lifted out, but for its edge against the body (no gap at the joint) ...
    for(int r=0;r<h;++r)for(int c=0;c<w;++c) {
        if(!solid(c,r)||!part(c,r))continue;
        bool joint=false;
        for(int j=-1;j<=1&&!joint;++j)for(int i=-1;i<=1;++i)if(solid(c+i,r+j)&&!part(c+i,r+j)) {joint=true;break;}
        if(!joint)pixels[((size_t)r*w+c)*2+1]=0;
    }
    // ... and laid down moved.
    for(int r=0;r<h;++r)for(int c=0;c<w;++c) {
        int c2=c+dx,r2=r+dy;
        if(!solid(c,r)||!part(c,r)||c2<0||r2<0||c2>=w||r2>=h)continue;
        pixels[((size_t)r2*w+c2)*2]=image.pixels[((size_t)r*w+c)*2];pixels[((size_t)r2*w+c2)*2+1]=255;
    }
    std::vector<byte> normals=spriteNormals(w,h,pixels.data());
    int key=joltKeyBase-(int)pictures.size();
    images.emplace(key,upload(w,h,pixels.data(),image.left,image.top,nullptr,normals.data()));
    pictures[id]=key;
    return key;
}
// The picture to draw this drawing with now (0: its own).
int bodyJoltKey(const mobj_t &thing,int lump,const Image &image) {
    if(jolts.empty())return 0;
    auto found=jolts.find(&thing);
    if(found==jolts.end())return 0;
    const Jolt jolt=found->second;
    int age=leveltime-jolt.tic;
    if(age<0||age>joltFull+joltBack||thing.health<=0) {jolts.erase(found);return 0;}
    float left=age<joltFull?1:1-(age-joltFull)/(float)joltBack;
    int shift=(int)std::lround(jolt.peak*left*left);
    if(shift<=0)return 0;
    const SpriteSkeleton *s=spriteSkeleton(image);
    if(!s||s->owner.size()!=(size_t)image.width*image.height||image.pixels.empty())return 0;
    // Out from the body's middle in this drawing; a head back and up a little.
    int tip=skeletonBoneEnds[jolt.bone][1];
    float out=s->joint[tip][0]-s->joint[SJ_PELVIS][0];
    int dx=out>=0?shift:-shift,dy=jolt.bone==SB_HEAD||jolt.bone==SB_UPPER0||jolt.bone==SB_UPPER1?-(shift+1)/2:0;
    return picture(lump,image,*s,jolt.bone,dx,dy);
}
void joltsShutdown() {
    jolts.clear();
    for(const auto &entry:pictures) {
        images.erase(entry.second);
        for(auto *group:{&batches,&mirrorBatches})group->erase(entry.second);
    }
    pictures.clear();
}
}
