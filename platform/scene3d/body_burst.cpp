/* Gore's body burst (PLAN-gore.md G, H): a whole monster blown apart. The
   monster as the player sees it at the kill is cut into uneven chunks (the head
   burst's cut, head_burst.cpp). The big ones are thrown off as flying pieces
   (flying_piece.cpp: they bounce, bleed, and lie where they land, torn all
   round); the rest, and a mist of blood the body's size, burst as a picture
   that eases out and dissolves, as a head does. Nothing of the corpse is drawn
   after it, only the chunks and a pool where it stood. For monsters with no
   rig on a heavy enough blow, any monster on a rocket's direct hit or a
   telefrag (gore.cpp, goreBurst), and corpses an explosion catches lying on
   the floor (R_Explosion). Drawing only; every number from the kill's own seed. */
#include "internal.h"

namespace doom3d {
struct BodyBurst {
    std::shared_ptr<HeadBurst> picture;                  // the small pieces and the mist
    std::vector<std::shared_ptr<FlyingPiece>> chunks;    // the big pieces, thrown
    float x=0,y=0,z=0;                                   // the picture's middle in the world
    int start=0;                                         // the level tic it burst
    std::shared_ptr<Balloon> balloon;int state=0;        // swelling first (caco_pop.cpp), the drawing it pops as,
    float feet[3]={};                                    // and where its feet were (the game's corpse drops on behind the picture)
};
static std::unordered_map<const mobj_t*,BodyBurst> bursts;

// The rotation of a sprite seen from (dx,dy) away, for a thing facing angle (as geometry.cpp draws it).
static unsigned seenRotation(float dx,float dy,angle_t facing) {
    angle_t angle=(angle_t)(int64_t)(atan2(dy,dx)*(4294967296.0/(2*doomPi)));
    return (angle-facing+(unsigned)(ANG45/2)*9)>>29;
}

// The monster's drawing for state as the player sees it: its texels in units right of its middle as seen
// (x) and over its feet (z), and the way to its right as seen over the floor.
static bool seenTexels(const mobj_t &thing,int state,std::vector<HeadTexel> &texels,float &rx,float &ry) {
    if(state<=0)return false;
    const state_t &s=states[state];
    if(s.sprite<0||s.sprite>=numsprites)return false;
    const auto &sprite=sprites[s.sprite];int frame=s.frame&FF_FRAMEMASK;
    if(frame>=sprite.numframes)return false;
    const auto &sf=sprite.spriteframes[frame];
    const mobj_t *viewer=players[displayplayer].mo;
    float ex=viewer?units(viewer->x):units(thing.x)-1,ey=viewer?units(viewer->y):units(thing.y);
    float dx=units(thing.x)-ex,dy=units(thing.y)-ey,l=std::max(1e-3f,std::hypot(dx,dy));
    rx=dy/l;ry=-dx/l;
    unsigned rotation=sf.rotate?seenRotation(dx,dy,thing.angle):0;
    bool flip=sf.flip[rotation];
    const Image &image=lumpImage(firstspritelump+sf.lump[rotation],false);
    for(int r=0;r<image.height;++r)for(int c=0;c<image.width;++c) {
        size_t p=(size_t)r*image.width+c;
        if(!image.pixels[2*p+1])continue;
        int x=flip?image.width-1-c-image.left:c-image.left;
        byte colour=image.pixels[2*p];
        texels.push_back({x,image.top-r-1,colour,colour});
    }
    // Frames hanging a few rows under their foot line stand on the floor (geometry.cpp's floorOverhang),
    // so the burst starts where the drawing was.
    if(image.top<image.height) {int lift=std::min(16,image.height-image.top);for(HeadTexel &t:texels)t.z+=lift;}
    return texels.size()>=16;
}

// Burst: the drawing of state (the pain drawing at a kill, the corpse's own when an explosion catches
// it), blown along (dx,dy) (none: from the inside, a telefrag), as hard as blow (1 a rocket's direct
// hit on a zombieman, 2 the most), struck hitZ units over its feet (below 0: its middle), its feet at
// (tx,ty,tz): where the thing is, or where its ragdoll lies.
static bool burst(const mobj_t &thing,int state,float dx,float dy,float blow,float hitZ,float tx,float ty,float tz) {
    if(bursts.count(&thing))return true;
    std::vector<HeadTexel> texels;float rx,ry;
    if(!seenTexels(thing,state,texels,rx,ry))return false;
    uint32_t seed=hash32((uint32_t)(uintptr_t)&thing*2654435761u^(uint32_t)leveltime*40503u^0x4b1d7a3fu);
    if(M_CheckParm((char*)"-ragdolltest"))seed=hash32((uint32_t)thing.spawnpoint.x*73856093u^(uint32_t)thing.spawnpoint.y*19349663u^(uint32_t)thing.type^(uint32_t)leveltime*40503u);
    auto rnd=[&] {seed=hash32(seed+0x9e3779b9u);return (float)(seed>>8)/16777216.0f;};
    int tint=bloodTint(&thing);bool bleeds=bleedsBlood(thing);
    const GutsPatch *guts=&gutsOf(thing);
    float mx=0,mz=0;
    for(const HeadTexel &t:texels) {mx+=t.x+0.5f;mz+=t.z+0.5f;}
    mx/=texels.size();mz/=texels.size();
    int cx=(int)std::lround(mx),cz=(int)std::lround(mz);
    float reach=0;
    for(const HeadTexel &t:texels)reach=std::max(reach,std::hypot(t.x+0.5f-mx,t.z+0.5f-mz));
    // Struck where the blow came in: its height, on the near side's middle.
    float hz=hitZ>=0?std::clamp(hitZ-mz,-reach*0.6f,reach*0.6f):0;
    float shotLength=std::hypot(dx,dy),sx=shotLength>1e-3f?dx/shotLength:0,sy=shotLength>1e-3f?dy/shotLength:0;
    float across=sx*rx+sy*ry;   // the shot's way across the picture
    // The cut: more pieces the bigger the drawing, 6 to 28.
    int count=std::clamp(6+(int)(texels.size()/150),6,28);
    std::vector<bool> torn;
    std::vector<int> pieceOf=burstCut(texels,count,seed,torn,false);
    std::vector<int> size(count,0);
    for(int k:pieceOf)++size[k];
    // The big ones fly: two to six, the biggest, none under 24 texels.
    std::vector<int> order(count);
    for(int k=0;k<count;++k)order[k]=k;
    std::sort(order.begin(),order.end(),[&](int a,int b){return size[a]>size[b];});
    int flying=std::clamp((int)texels.size()/500+2,2,6);
    std::vector<bool> flies(count,false);
    for(int n=0;n<flying&&n<count;++n)if(size[order[n]]>=24)flies[order[n]]=true;
    BodyBurst b;
    b.start=leveltime;
    for(int k=0;k<count;++k) {
        if(!flies[k])continue;
        std::vector<HeadTexel> chunk;std::vector<bool> chunkTorn;float px=0,pz=0;
        for(size_t i=0;i<texels.size();++i)if(pieceOf[i]==k) {px+=texels[i].x+0.5f;pz+=texels[i].z+0.5f;}
        px/=size[k];pz/=size[k];
        int ix=(int)std::lround(px),iz=(int)std::lround(pz);
        for(size_t i=0;i<texels.size();++i)
            if(pieceOf[i]==k) {chunk.push_back({texels[i].x-ix,texels[i].z-iz,texels[i].colour,texels[i].back});chunkTorn.push_back(torn[i]);}
        // Out from where it was struck, across the picture as it lay in it, nearer or further at random,
        // and on along the shot; the upper chunks thrown a little higher. (Thrown up hard, a caco's top
        // chunks hung in the air for most of a second.)
        float ox=(px-mx)/std::max(1.0f,reach),oz=(pz-mz-hz)/std::max(1.0f,reach),depth=rnd()*2-1;
        float wx=sx*(0.6f+0.6f*depth)+rx*ox*2.2f,wy=sy*(0.6f+0.6f*depth)+ry*ox*2.2f,wl=std::hypot(wx,wy);
        if(wl<0.2f) {float a=rnd()*2*doomPi;wx=std::cos(a);wy=std::sin(a);wl=1;}
        FlyingPieceStart start{tx+rx*px,ty+ry*px,tz+pz,wx/wl,wy/wl,units(thing.momx),units(thing.momy),
                               blow*(0.7f+0.5f*rnd())*std::clamp(8/std::sqrt((float)size[k]),0.6f,1.4f),
                               seed^(uint32_t)k*0x27d4eb2fu,tint,bleeds,guts,false,std::clamp(std::sqrt((float)size[k])*1.6f,10.0f,32.0f),
                               &chunkTorn,std::max(0.8f,1.5f+2*oz)*std::min(blow,1.5f)};
        if(auto piece=startFlyingPiece(chunk,start))b.chunks.push_back(piece);
    }
    // The rest: a picture over its middle, bursting and dissolving.
    std::vector<HeadTexel> rest;std::vector<int> restPiece;std::vector<bool> restTorn;std::vector<int> compact(count,-1);int pieces=0;
    for(size_t i=0;i<texels.size();++i) {
        int k=pieceOf[i];
        if(flies[k])continue;
        if(compact[k]<0)compact[k]=pieces++;
        rest.push_back({texels[i].x-cx,texels[i].z-cz,texels[i].colour,texels[i].back});restPiece.push_back(compact[k]);restTorn.push_back(torn[i]);
    }
    float reachMost=std::clamp(reach*0.8f,16.0f,64.0f);
    HeadBurstStart start{0,hz,across,blow,seed^0x1f83d9abu,tint,bleeds,guts};
    start.canvas=std::clamp(((int)std::ceil(2*(reach+reachMost))+11)/4*4,64,384);
    start.reachScale=4;start.reachMost=reachMost;start.hitMost=reach;
    start.pieceOf=&restPiece;start.torn=&restTorn;
    if(!rest.empty())b.picture=startHeadBurst(rest,start);
    b.x=tx+rx*cx;b.y=ty+ry*cx;b.z=tz+cz;
    if(bleeds) {
        // The blood: a pop at its middle and higher, and a pool where it stood, spreading over six seconds.
        float pdx=shotLength>1e-3f?sx:rx,pdy=shotLength>1e-3f?sy:ry;
        sprayHeadPop(b.x,b.y,b.z,pdx,pdy,tint,false);
        sprayHeadPop(b.x,b.y,b.z+reach*0.4f,pdx,pdy,tint,false);
        sprayGush(b.x,b.y,b.z,pdx,pdy,1.8f,tint);
        bloodPoolAt(tx,ty,std::clamp(units(thing.radius)*2.5f,24.0f,96.0f),6,tint);
    }
    goreSound(bleeds?GORE_SOUND_BLOWUP:GORE_SOUND_CRACK,b.x,b.y,b.z);
    if(M_CheckParm((char*)"-ragdolllog"))fprintf(stderr,"goreburst texels %zu pieces %d flying %zu canvas %d blow %.2f\n",texels.size(),count,b.chunks.size(),start.canvas,blow);
    bursts[&thing]=std::move(b);
    return true;
}

// A rocket in his mouth: his drawing with the mouth open as the player sees it, swelling where he is.
bool startBalloonBurst(const mobj_t &thing) {
    if(!thing.info||bursts.count(&thing))return false;
    int state=thing.info->missilestate;
    for(int n=0;n<2&&state>0;++n)if(states[state].nextstate>0)state=states[state].nextstate;   // its last attack frame: wide open
    std::vector<HeadTexel> texels;float rx,ry;
    if(!seenTexels(thing,state,texels,rx,ry))return false;
    float mx=0,mz=0;
    for(const HeadTexel &t:texels) {mx+=t.x+0.5f;mz+=t.z+0.5f;}
    mx/=texels.size();mz/=texels.size();
    BodyBurst b;b.start=leveltime;b.state=state;b.feet[0]=units(thing.x);b.feet[1]=units(thing.y);b.feet[2]=units(thing.z);
    b.x=units(thing.x)+rx*mx;b.y=units(thing.y)+ry*mx;b.z=units(thing.z)+mz;
    b.balloon=startBalloon(texels,b.x,b.y,b.z,hash32((uint32_t)(uintptr_t)&thing^(uint32_t)leveltime*40503u));
    if(!b.balloon)return false;
    bursts[&thing]=std::move(b);
    return true;
}
bool balloonHides(const mobj_t &thing) {
    float x=units(thing.x),y=units(thing.y);
    for(const auto &entry:bursts)if(entry.second.balloon&&std::hypot(x-entry.second.x,y-entry.second.y)<96)return true;
    return false;
}
bool balloonSwelling(const mobj_t &thing,int lead) {
    auto found=bursts.find(&thing);
    return found!=bursts.end()&&found->second.balloon&&leveltime-found->second.start<balloonLasts()-lead;
}
bool startBodyBurst(const mobj_t &thing,float dx,float dy,int damage,float hitZ) {
    if(!thing.info)return false;
    int state=thing.info->painstate>0?thing.info->painstate:thing.info->spawnstate;
    float blow=damage>=10000?2.0f:std::clamp(0.8f+damage/(4.0f*std::max(1,thing.info->spawnhealth)),1.0f,2.0f);
    if(damage>=10000)dx=dy=0;   // a telefrag: from the inside
    return burst(thing,state,dx,dy,blow,hitZ,units(thing.x),units(thing.y),units(thing.z));
}

// An explosion (P_RadiusAttack, before it hurts anything): the corpses it catches on the floor burst, as
// its blast reaches them past their radius, the monster's health (at most 60) at least. Everything on the
// floor goes up. A ragdoll counts where it lies (the game's corpse slid on, often a hundred units and more).
void goreExplosion(mobj_t *spot,mobj_t *source,int damage) {
    (void)source;
    if(!spot||!goreOn())return;
    flyingPiecesBlast(units(spot->x),units(spot->y),units(spot->z),(float)damage);   // the pieces lying round it go flying
    struct Caught { const mobj_t *thing;float x,y,z,hurt; };
    std::vector<Caught> caught;
    float sx=units(spot->x),sy=units(spot->y),sz=units(spot->z);
    for(int i=0;i<numsectors;++i)for(const mobj_t *thing=sectors[i].thinglist;thing;thing=thing->snext) {
        if(!(thing->flags&MF_CORPSE)||!(thing->flags&MF_COUNTKILL)||thing->health>0||!thing->info||!thing->state||bursts.count(thing))continue;
        if(Game_BodyBall(thing->type)&&bodyDyingNow(thing))continue;   // this rocket's own kill: startDeath decides (the mouth's pop)
        float x=units(thing->x),y=units(thing->y),z=units(thing->z);
        ragdollPlace(*thing,x,y,z);
        float dist=std::max(0.0f,std::max(std::abs(x-sx),std::abs(y-sy))-units(thing->radius));
        if(dist>=damage||std::abs(z-sz)>64||damage-dist<std::min(thing->info->spawnhealth,60))continue;
        caught.push_back({thing,x,y,z,damage-dist});
    }
    for(const Caught &c:caught) {
        if(ragdollBlown(*c.thing,sx,sy,c.hurt))continue;   // a soft ball still moving goes flying instead
        float dx=c.x-sx,dy=c.y-sy,l=std::hypot(dx,dy);
        float blow=std::clamp(0.8f+c.hurt/128,1.0f,1.8f);
        if(burst(*c.thing,(int)(c.thing->state-states),l>1e-3f?dx/l:0,l>1e-3f?dy/l:0,blow,-1,c.x,c.y,c.z))forgetRagdoll(c.thing);
    }
}

// Crushed: P_ChangeSector turned the corpse to the game's gibs (its height and radius 0). With gore its
// body bursts where it lies instead, squeezed out sideways, the drawing its last death frame (the gibs
// aren't drawn: the chunks and the pool are).
bool goreCrush(const mobj_t &thing) {
    if(bursts.count(&thing))return true;
    if(!thing.info||thing.info->deathstate<=0)return false;
    int state=thing.info->deathstate;
    for(int n=0;n<64&&states[state].tics>=0&&states[state].nextstate>0;++n)state=states[state].nextstate;
    float x=units(thing.x),y=units(thing.y),z=units(thing.z);
    ragdollPlace(thing,x,y,z);
    if(!burst(thing,state,0,0,1.3f,-1,x,y,z))return false;
    forgetRagdoll(&thing);
    if(M_CheckParm((char*)"-ragdolllog"))fprintf(stderr,"gorecrush\n");
    return true;
}

// Shots through corpses (PLAN-gore.md D): Doom's bullets pass through the dead, and still do; with gore
// each one through where a body lies (its ragdoll's place, or the corpse's) throws blood there, and once
// they have done its health (30 to 80) it bursts. A shot is counted as 10, a pellet's middle.
static std::unordered_map<const mobj_t*,int> corpseDamage;
void goreShotTraced(const mobj_t *shooter,const mobj_t *hit,float x1,float y1,float z1,float x2,float y2,float z2) {
    (void)hit;
    if(!goreOn()||Game_MeleeWeapon(shooter)==1)return;   // a punch takes nothing out of a corpse
    float dx=x2-x1,dy=y2-y1,dz=z2-z1,len2=dx*dx+dy*dy;
    if(len2<1)return;
    float len=std::sqrt(len2),ux=dx/len,uy=dy/len;
    struct Struck { const mobj_t *thing;float x,y,z;bool done=false; };
    std::vector<Struck> struck;
    for(int i=0;i<numsectors;++i)for(const mobj_t *thing=sectors[i].thinglist;thing;thing=thing->snext) {
        if(!(thing->flags&MF_CORPSE)||!(thing->flags&MF_COUNTKILL)||thing->health>0||!thing->info||bursts.count(thing))continue;
        // Dying this tic (the killing volley's pellets go on through it): still standing, it counts with the
        // kill (body_motion.cpp) instead, so a super shotgun's twenty all tear at the body.
        // Its near misses count for nothing (the leg shots' burst a corpse that hadn't fallen yet).
        if(bodyDyingNow(thing)) {
            float sx=units(thing->x),sy=units(thing->y),sz=units(thing->z);
            float t=std::clamp(((sx-x1)*dx+(sy-y1)*dy)/len2,0.0f,1.0f);
            float px=x1+dx*t,py=y1+dy*t,pz=z1+dz*t;
            if(std::hypot(px-sx,py-sy)<=units(thing->radius)+2&&pz>=sz&&pz<=sz+units(thing->info->height)) {
                bodyKillingPellet(thing,px,py,pz,ux,uy,10);
                // And it tears at the body as the pellets that killed it did, joining their tear: the super
                // shotgun's rip (PLAN-skin-suit.md 7; before, only the two or three that killed it made holes).
                woundHit(thing,(fixed_t)(px*FRACUNIT),(fixed_t)(py*FRACUNIT),(fixed_t)(pz*FRACUNIT),(fixed_t)(ux*FRACUNIT),
                         (fixed_t)(uy*FRACUNIT),(fixed_t)(dz/len*FRACUNIT),10,shooter);
            }
            continue;
        }
        // A ragdoll: struck by part where its body lies (ragdollCorpseShot: chunks torn out first).
        float hx,hy,hz;
        int doll=ragdollCorpseShot(*thing,x1,y1,z1,x2,y2,z2,hx,hy,hz);
        if(doll>=0) {
            sprayCorpseHit(hx,hy,hz,ux,uy,bloodTint(thing));
            if(doll==2)struck.push_back({thing,hx,hy,hz,true});
            continue;
        }
        if(ragdollPlace(*thing,hx,hy,hz))continue;   // the shot missed its body
        float x=units(thing->x),y=units(thing->y),z=units(thing->z);
        // Nearest the shot's line over the floor, before it stopped, low enough to be in the body.
        float t=std::clamp(((x-x1)*dx+(y-y1)*dy)/len2,0.0f,1.0f);
        float px=x1+dx*t,py=y1+dy*t,pz=z1+dz*t;
        float reach=std::max(16.0f,units(thing->info->radius)*1.2f);
        if(std::hypot(px-x,py-y)>reach||pz<z-2||pz>z+16)continue;
        struck.push_back({thing,px,py,pz,false});
    }
    for(auto &s:struck) {
        const mobj_t &thing=*s.thing;
        if(!s.done) {
            sprayCorpseHit(s.x,s.y,s.z,ux,uy,bloodTint(&thing));
            int &taken=corpseDamage[&thing];taken+=10;
            if(taken<std::clamp(thing.info->spawnhealth,30,80))continue;
        }
        int state=thing.state?(int)(thing.state-states):thing.info->deathstate;
        float x=units(thing.x),y=units(thing.y),z=units(thing.z);
        ragdollPlace(thing,x,y,z);
        if(burst(thing,state,ux,uy,1.1f,-1,x,y,z)) {
            forgetRagdoll(&thing);corpseDamage.erase(&thing);
            if(M_CheckParm((char*)"-ragdolllog"))fprintf(stderr,"gorecorpse burst\n");
        }
    }
}

bool bodyBurstView(const mobj_t &thing,DeathView &view) {
    auto found=bursts.find(&thing);
    if(found==bursts.end())return false;
    BodyBurst &b=found->second;
    int tics=leveltime-b.start;
    // Swollen as far as it goes: he pops, from the inside, and it rains.
    if(b.balloon&&tics>=balloonLasts()) {
        int state=b.state;float x=b.x,y=b.y,z=b.z,fx=b.feet[0],fy=b.feet[1],fz=b.feet[2];
        dropBalloon(*b.balloon);bursts.erase(found);
        if(!burst(thing,state,0,0,2,-1,fx,fy,fz))return false;
        sprayRain(thing,x,y,z,std::clamp(units(thing.radius)*4.5f,96.0f,180.0f));
        found=bursts.find(&thing);
        if(found==bursts.end())return false;
        view.burst=&found->second;
        return true;
    }
    timed(Stat::Gore,[&]{for(auto &chunk:b.chunks)flyingPieceTic(*chunk,tics);});
    view.burst=&b;
    return true;
}

bool bodyBurstInView(const BodyBurst &b) {
    if(thingInView(b.x,b.y))return true;
    for(const auto &chunk:b.chunks)if(flyingPieceInView(*chunk))return true;
    return false;
}
void bodyBurstCards(BodyBurst &b,float fraction,std::vector<RagdollCard> &cards) {
    timed(Stat::Gore,[&] {
        RagdollCard card;
        if(b.balloon) {if(balloonCard(*b.balloon,leveltime-b.start,b.x,b.y,b.z,card))cards.push_back(card);return;}
        if(b.picture&&headBurstCard(*b.picture,leveltime-b.start,b.x,b.y,b.z,card))cards.push_back(card);
        else b.picture=nullptr;
        for(auto &chunk:b.chunks)if(flyingPieceCard(*chunk,fraction,card))cards.push_back(card);
    });
}

static void drop(BodyBurst &b) {
    if(b.balloon)dropBalloon(*b.balloon);
    if(b.picture)dropHeadBurst(*b.picture);
    for(auto &chunk:b.chunks)dropFlyingPiece(*chunk);
}
void forgetBodyBurst(const mobj_t *thing) {
    corpseDamage.erase(thing);
    auto found=bursts.find(thing);
    if(found==bursts.end())return;
    drop(found->second);bursts.erase(found);
}
void bodyBurstsShutdown() {for(auto &entry:bursts)drop(entry.second);bursts.clear();corpseDamage.clear();}
}
