/* A blow landing on the player, heard before the grunt: on armour a hard knock, on
   the body a deep thud, a claw or a bite wetter and closer, fire a thump with a
   sizzle. Heavy and a little inhuman, as the player is. It comes from the side it
   struck, as hard as it was. Drawing only: the game's damage is its own. */
#include "internal.h"
extern "C" {
#include "../audio/impact_synth.h"
#include "../audio/sound_options.h"
}

namespace doom3d {
constexpr float hitOffset=24;              // the sound stands this far off the player, toward the blow
constexpr float clawReach=96;              // an attacker this near who struck with itself: a claw or a bite
void playerHit(mobj_t *target,mobj_t *inflictor,mobj_t *source,int damage) {
    if(!target||!target->player||target->player!=&players[consoleplayer]||damage>=10000)return;
    const mobj_t *from=inflictor?inflictor:source;
    if(!from)return;                       // the floor, a crusher: not a blow
    static int tic=-1;
    if(tic==leveltime)return;              // a shotgun's pellets are one blow
    tic=leveltime;
    float x=units(target->x),y=units(target->y),z=units(players[consoleplayer].viewz)-8;
    float dx=units(from->x)-x,dy=units(from->y)-y,l=std::hypot(dx,dy);
    if(l>1)  {x+=dx/l*hitOffset;y+=dy/l*hitOffset;}
    int kind=target->player->armorpoints>0?IMPACT_PLAYER_ARMOR:IMPACT_PLAYER_BODY;
    if(Game_ThingRole(from->type)==GAME_THING_BURN_SHOT)kind=IMPACT_PLAYER_BURN;
    else if(inflictor==source&&l<clawReach&&!Game_ShootsHitscan(source))kind=IMPACT_PLAYER_CLAW;
    I_MadeSoundLevel(kind,x,y,z,std::clamp(0.5f+damage/40.0f,0.5f,1.3f));
}
}
