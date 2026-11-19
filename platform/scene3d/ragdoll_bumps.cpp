/* A falling body and the living: a ragdoll's points are pushed out of the monsters standing in its way, and
   each one it strikes flinches from the blow (body_motion.cpp's bodyShove). Drawing only, as the doll is:
   the game's monsters take no damage, enter no pain state and go on with what they were doing. */
#include "ragdoll_doll.h"

namespace doom3d {
// The living monsters, gathered once a tic for all the dolls falling in it.
static std::vector<const mobj_t*> living;
static int livingTic=-1;static unsigned livingLevel=0;
static const std::vector<const mobj_t*> &theLiving() {
    if(livingTic==leveltime&&livingLevel==(unsigned)r_levelserial)return living;
    livingTic=leveltime;livingLevel=(unsigned)r_levelserial;living.clear();
    for(int i=0;i<numsectors;++i)for(const mobj_t *t=sectors[i].thinglist;t;t=t->snext)
        if(t->health>0&&t->info&&(t->info->flags&MF_COUNTKILL)&&(t->flags&MF_SHOOTABLE)&&!(t->flags&MF_CORPSE))living.push_back(t);
    return living;
}
// A monster as the doll meets it: a cylinder bumpGirth of the game's box wide (its drawing is narrower than
// its box) and as tall as the game has it. Struck at shoveSpeed (units a tic) or faster, it flinches.
constexpr float bumpGirth=0.6f,shoveSpeed=2.5f,bumpNear=64;

// After the doll's tic, once: each point inside a living monster is put out of it the short way and stops
// going in (dead, as a wall: no speed from the push). Never trapped inside, whoever walked into whom; its
// bones pull the rest after it over the next substeps. A body at rest is a sprite and is left alone.
void dollBumps(Ragdoll &doll,const mobj_t &own) {
    if(doll.pinned||doll.tumbling||doll.resting)return;
    float side=doll.mirror?-1.0f:1.0f;
    float mx,my;doll.overGrid(doll.middleOf(),mx,my);mx+=doll.floors.x0;my+=doll.floors.y0;
    for(const mobj_t *t:theLiving()) {
        if(t==&own)continue;
        float tx=units(t->x),ty=units(t->y),girth=units(t->radius)*bumpGirth;
        if(std::abs(tx-mx)>girth+bumpNear||std::abs(ty-my)>girth+bumpNear)continue;
        float low=units(t->z)-doll.gz,high=low+units(t->height);
        float hardest=0,wayX=0,wayY=0,at=0;
        for(int n=0;n<DOLL_POINTS;++n) {
            if(doll.rider(n)||doll.p[n].z<low-radius[n]||doll.p[n].z>high)continue;
            float wx,wy;doll.overGrid(doll.p[n],wx,wy);wx+=doll.floors.x0;wy+=doll.floors.y0;
            float qx=wx-tx,qy=wy-ty,dist=std::hypot(qx,qy),reach=girth+radius[n];
            if(dist>=reach)continue;
            float depth=reach-dist;
            if(dist<1e-3f) {qx=mx-tx;qy=my-ty;dist=std::hypot(qx,qy);if(dist<1e-3f) {qx=1;qy=0;dist=1;}}   // dead centre: out toward its own middle
            float ox=qx/dist,oy=qy/dist;
            V3 out{side*(ox*doll.ax[0]+oy*doll.ax[1]),ox*doll.ay[0]+oy*doll.ay[1],0};   // the way out, in body space
            doll.p[n]=doll.p[n]+out*depth;doll.prev[n]=doll.prev[n]+out*depth;
            float into=dot(doll.p[n]-doll.prev[n],out);
            if(into>=0)continue;
            doll.prev[n]=doll.prev[n]+out*into;
            float speed=-into*dollSubsteps;
            if(speed>hardest) {hardest=speed;wayX=-ox;wayY=-oy;at=doll.p[n].z-low;}
        }
        if(hardest>shoveSpeed)bodyShove(*t,wayX,wayY,hardest,at);
    }
}
}
