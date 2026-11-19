/* Gore's insides (PLAN-gore.md E): what a cut shows. The guts library is a
   monster's own gib drawing, the last of its xdeath frames (the heap of meat
   id drew for it), used as a texture of its meat's colours; a monster with
   none borrows the first that has one, in its own blood's tint. With it a
   palette colour for bone (spine and femur dots in a cut, the edge of a torn
   skull). Built once per monster type, from the WAD. */
#include "internal.h"

namespace doom3d {
static byte nearestColour(int r,int g,int b) {
    static const byte *palette=(const byte*)W_CacheLumpName((char*)"PLAYPAL",PU_STATIC);
    int best=0,bestDistance=INT_MAX;
    for(int i=0;i<256;++i) {int dr=palette[3*i]-r,dg=palette[3*i+1]-g,db=palette[3*i+2]-b,d=dr*dr+dg*dg+db*db;if(d<bestDistance){bestDistance=d;best=i;}}
    return (byte)best;
}
// The resting frame of a death sequence (or 0).
static int restingState(int state) {
    for(int n=0;n<64&&state>0;++n) {
        if(states[state].tics<0)return state;
        state=states[state].nextstate;
    }
    return 0;
}
static bool gibsOf(int type,GutsPatch &patch) {
    int state=type>=0&&type<NUMMOBJTYPES?restingState(mobjinfo[type].xdeathstate):0;
    if(!state||states[state].sprite<0||states[state].sprite>=numsprites)return false;
    const auto &sprite=sprites[states[state].sprite];int frame=states[state].frame&FF_FRAMEMASK;
    if(frame>=sprite.numframes)return false;
    const Image &image=lumpImage(firstspritelump+sprite.spriteframes[frame].lump[0],false);
    patch.w=image.width;patch.h=image.height;patch.colour.assign((size_t)patch.w*patch.h,-1);
    int any=0;
    for(size_t i=0;i<patch.colour.size();++i)if(image.pixels[i*2+1]) {patch.colour[i]=image.pixels[i*2];++any;}
    return any>8;
}

const GutsPatch &gutsOf(const mobj_t &thing) {
    static std::unordered_map<int,GutsPatch> made;
    auto found=made.find(thing.type);
    if(found!=made.end())return found->second;
    GutsPatch &patch=made[thing.type];
    patch.bone=nearestColour(160,136,104);   // ivory, not white (the palette's near one to 214,204,176 was 199 grey)patch.dark=nearestColour(56,6,6);
    if(!gibsOf(thing.type,patch)) {
        // Borrowed: the first monster with gibs, its reds in this one's blood (blue, green, none).
        for(int type=0;type<NUMMOBJTYPES;++type)if(gibsOf(type,patch))break;
        patch.tint=bloodTint(&thing);
    }
    return patch;
}

byte gutsColour(const GutsPatch &patch,int x,int y,uint32_t seed) {
    if(patch.w<=0||patch.h<=0)return patch.dark;
    // A seeded place in the heap, then the next texel that has meat on it.
    int ox=(int)(hash32(seed)%(uint32_t)patch.w),oy=(int)(hash32(seed^0x51ed27u)%(uint32_t)patch.h);
    int sx=((x+ox)%patch.w+patch.w)%patch.w,sy=((y+oy)%patch.h+patch.h)%patch.h;
    size_t n=patch.colour.size(),at=(size_t)sy*patch.w+sx;
    for(size_t k=0;k<n;++k) {
        int c=patch.colour[(at+k)%n];
        if(c>=0)return patch.tint==BLOOD_RED?(byte)c:bloodRemap((byte)c,patch.tint);
    }
    return patch.dark;
}
}
