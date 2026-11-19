// A DERP soldier down on one knee (p_lanes.c's P_TacKneel): Doom has no frame for that, so the zombieman
// and the shotgun guy kneel in hand-made frames (kneel_frames_stock.h, stand-in art): aim, fire and pain,
// each drawn from all eight sides. The game's own frame picks which: its pain frame the pain, a fullbright
// one the shot, any other (aiming, or the walk it goes on playing in place) the aim. Lit as id's frames are
// (finishSpriteArt: normals, eyes, a firing frame's painted flash). Only where the soldier's own aim frame is
// id's (kneel_frames_stock.h keeps its size and hash): replaced art keeps body_motion.cpp's squashed legs.
// Before, every kneel was that squash, and it read as a short man standing (the user).
#include "internal.h"
#include "../kneel_frames_stock.h"
#include <cstring>
#include <unordered_map>

namespace doom3d {
namespace {
constexpr int kneelOffset=1<<30;
constexpr int soldiers=(int)(sizeof doom_kneel_soldiers/sizeof *doom_kneel_soldiers);
std::unordered_map<int,int> bySprite;   // sprite -> its row in the table, -1 none (art checked once)

int soldierOf(int sprite) {
    auto found=bySprite.find(sprite);
    if(found!=bySprite.end())return found->second;
    int which=-1;
    for(int n=0;n<soldiers&&which<0;++n) {
        const doom_kneel_art &art=doom_kneel_soldiers[n];
        if(strncmp(sprnames[sprite],art.sprite,4))continue;
        int lump=W_CheckNumForName((char*)art.aim);
        if(lump<0)break;
        const Image &image=lumpImage(lump,false);
        if(image.width!=art.width||image.height!=art.height)break;
        unsigned hash=2166136261u;
        for(byte b:image.pixels)hash=(hash^b)*16777619u;
        if(hash==art.hash)which=n;
    }
    bySprite[sprite]=which;
    return which;
}
}

bool kneelFrame(const mobj_t &thing,int &soldier,int &frame) {
    if(!Game_Kneeling(&thing)||thing.health<=0||(thing.flags&MF_CORPSE)||!thing.info||thing.height>=thing.info->height)return false;
    if(thing.sprite<0||thing.sprite>=numsprites)return false;
    soldier=soldierOf(thing.sprite);
    if(soldier<0)return false;
    int pain=thing.info->painstate>0?(int)(states[thing.info->painstate].frame&FF_FRAMEMASK):-1;
    frame=(int)(thing.frame&FF_FRAMEMASK)==pain?2:(thing.frame&FF_FULLBRIGHT)?1:0;
    return true;
}

int kneelKey(int soldier,int frame,unsigned seen) {return -1-(kneelOffset+soldier*64+frame*8+(int)(seen&7));}

Image &kneelImage(int soldier,int frame,unsigned seen,int lump) {
    int key=kneelKey(soldier,frame,seen);
    auto found=images.find(key);
    if(found!=images.end())return found->second;
    const doom_kneel_frame &art=doom_kneel_frames[soldier][frame][seen&7];
    SpriteArt sprite;
    sprite.w=art.width;sprite.h=art.height;sprite.left=art.left;sprite.top=art.top;
    sprite.pixels.assign((size_t)art.width*art.height*2,0);
    size_t at=0,most=(size_t)art.width*art.height;
    for(unsigned i=0;i+1<art.size;) {
        at+=art.runs[i];unsigned painted=art.runs[i+1];i+=2;
        for(unsigned n=0;n<painted&&i<art.size&&at<most;++n,++i,++at) {sprite.pixels[2*at]=art.runs[i];sprite.pixels[2*at+1]=255;}
    }
    // As the game's own frame in that view would be: its eyes, and on the shot its flash.
    char name[9];memcpy(name,lumpinfo[lump].name,8);name[8]=0;
    finishSpriteArt(sprite,name,playpal(),spriteEyes(lump),spriteFlash(lump));
    Image image=upload(sprite.w,sprite.h,sprite.pixels.data(),sprite.left,sprite.top,nullptr,sprite.normals.data());
    image.glow=sprite.glow;image.glowWeight=sprite.glowWeight;image.glowHeight=sprite.glowHeight;
    return images.emplace(key,std::move(image)).first->second;
}
} // namespace doom3d
