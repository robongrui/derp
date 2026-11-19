/* What a body or a piece striking the map sounds like: its wet part by how hard
   it struck and how heavy it is (a big piece slaps, a small one thuds), and
   under a hard enough hit the surface's own knock (Game_WallSurface,
   Game_FlatSurface: what the steps and the room's echo go by). Soft ground
   gives only the wet part, and metal only rings on the hardest hits: on big
   floors it tires the ear. The moment of the player's own shot is the gun's;
   these land after it. */
#include "internal.h"
extern "C" {
#include "../audio/impact_synth.h"
#include "../audio/sound_options.h"
void R_TextureName(int texture,char *name);
}

namespace doom3d {
constexpr float contactLeast=0.3f,contactMost=1.3f,contactFull=9;   // the level's range; the speed that adds a whole level
constexpr float slapFrom=0.7f;                                      // heft: under it a thud
constexpr float knockHeft=0.4f,knockFrom=3,metalFrom=4.5f;          // what it takes for the surface to answer

static void contact(game_surface surface,float x,float y,float z,float speed,float heft) {
    float level=std::clamp(contactLeast+speed/contactFull,contactLeast,contactMost)*std::clamp(heft,0.3f,1.4f);
    I_MadeSoundLevel(heft<slapFrom?IMPACT_GORE_THUD:IMPACT_GORE_SLAP,x,y,z,level);
    if(heft<knockHeft||speed<knockFrom)return;
    switch(surface) {
    case GAME_SURFACE_STONE:I_MadeSoundLevel(IMPACT_BODY_STONE,x,y,z,level);break;
    case GAME_SURFACE_WOOD:I_MadeSoundLevel(IMPACT_BODY_WOOD,x,y,z,level);break;
    case GAME_SURFACE_METAL:if(speed>=metalFrom)I_MadeSoundLevel(IMPACT_BODY_METAL,x,y,z,level);break;
    default:break;
    }
}

void contactFloor(float x,float y,float z,float speed,float heft) {
    const sector_t *sector=R_PointInSubsector((fixed_t)(x*FRACUNIT),(fixed_t)(y*FRACUNIT))->sector;
    size_t index=sector-sectors;
    if(index<sectorMist.size()&&sectorMist[index].liquid&&!waterIceHolds(x,y))return;   // liquids answer with their splash
    char name[9];memcpy(name,lumpinfo[firstflat+sector->floorpic].name,8);name[8]=0;
    for(char *c=name;*c;++c)*c=(char)toupper((unsigned char)*c);
    contact(Game_FlatSurface(name),x,y,z,speed,heft);
}

void contactWall(const WallHit &hit,float x,float y,float z,float speed,float heft) {
    game_surface surface=GAME_SURFACE_STONE;
    WallSpan span;
    if(wallSpan(lines[hit.line],hit.side,hit.section,span)&&span.texture>0) {
        char name[9];R_TextureName(span.texture,name);
        surface=Game_WallSurface(name);
    }
    contact(surface,x,y,z,speed,heft);
}
}
