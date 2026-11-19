/* Explosions (F4 "Explosions"): a rocket's or a barrel's blast, drawn in
   place of the game's sprite.
   - The fireball and its smoke: a picture of its own per blast, redrawn on
     tics (explosion_fire.cpp), pushed clear of the wall it went off at.
   - The light, in three parts: a near-white flash for two tics, the
     fireball's orange while it burns, and an afterglow that rises with the
     smoke and takes two and a half seconds to go out. It is a dynamic light
     like a shot's, so what the blast throws (ragdolls, the living) casts its
     shadow away from it for the whole flight.
   - Sparks in arcs with a trail, bouncing once; a few burning bits that land
     and glow on; a ring of dust kicked out along the floor and dust shaken
     off the ceiling over it; a scorch (impacts.cpp).
   - A barrel bursts as the gibbed do: its own drawing cut into pieces and
     thrown (flying_piece.cpp), charred, lying where they land. It bursts
     with its sound (the second blast frame); the game's damage follows ten
     tics on, into a fire still burning, with a second flash and more dust.
   - In slow motion the shock is seen in the air: a ring of the lava's
     shimmer running out from the blast, and the heat over the fire.
   Drawing only: the game's blast, its damage and its things are as ever. */
#include "internal.h"

extern "C" double SlowMotion_Share(void);
// (Test runs only, DERP_EXPLODE's kinds 3 and 4: the game's own blasts, set off from here.)
extern "C" void P_DamageMobj(mobj_t *target,mobj_t *inflictor,mobj_t *source,int damage);
extern "C" mobj_t *P_SpawnMobj(fixed_t x,fixed_t y,fixed_t z,mobjtype_t type);
extern "C" boolean P_SetMobjState(mobj_t *mobj,statenum_t state);

namespace doom3d {
namespace {
constexpr int lingerTics=36;          // the afterglow, after the fire: a second (two and a half looked weird: a lamp hanging in the air; the user)
constexpr size_t mostBlasts=12,mostSparks=420,mostMotes=520,mostWreck=28;
constexpr float sparkGravity=0.45f;   // units a tic a tic, as the flying pieces'

struct Blast {
    float x,y,z;int tic;float scale;int hot;bool barrel;
    const mobj_t *source;int hitTic=-1;uint32_t seed=0;
    float floor=0,ceiling=0;bool sky=false;
    BlastPicture picture;bool pictureLeft=true;
    int mode=1;                // F4 Explosions as it stood when it went off: 1 Doom-like, 2 Physical, 3 Flashbulb
    float out[3]={0,0,0};      // the way out of the surface it went off on (a wall, the floor), or nothing in the open
    BlastGrid *grid=nullptr;   // the physical one: simulated (explosion_grid.cpp), no picture of the above kind
};
// A spark or a burning bit, stepped on tics: units a tic.
struct Spark { float x,y,z,wasX,wasY,wasZ,vx,vy,vz;int born,life;float size,heat;bool bit,bounced=false,resting=false;
               bool streak=false;float trail[5][3]={};int kept=0; };   // streak (Flashbulb): a head with a tail along where it has just been
// Dust: thrown, stopped short by the air, hanging and sinking (as wall_hits.cpp's specks).
struct Mote { float x,y,z,vx,vy,vz,tau,sink,fall,born,life,size;std::array<float,3> albedo,lit; };
struct Wreck { std::shared_ptr<FlyingPiece> piece;int start; };

std::deque<Blast> blasts;
std::vector<Spark> sparks;
std::vector<Mote> motes;
std::deque<Wreck> wreckage;
// The blast's front on its way out (the physical one): what it breaks, it breaks as it arrives.
struct Front { float x,y,z,radius;int tic;bool glass,lamps;float reached=0; };
std::vector<Front> fronts;
constexpr float frontUnits=28;   // units a tic: a 128-unit blast takes five tics to cross its reach
unsigned seenLevel=~0u;
int steppedTic=-1;

void dropBlast(Blast &b) {dropBlastPicture(b.picture);dropBlastGrid(b.grid);b.grid=nullptr;}
void dropAll() {
    for(Blast &b:blasts)dropBlast(b);
    for(Wreck &w:wreckage)dropFlyingPiece(*w.piece);
    fronts.clear();blasts.clear();sparks.clear();motes.clear();wreckage.clear();steppedTic=-1;
}
void checkLevel() {
    if(seenLevel==r_levelserial)return;
    dropAll();seenLevel=r_levelserial;
}
const sector_t *sectorAt(float x,float y) {return R_PointInSubsector((fixed_t)(x*FRACUNIT),(fixed_t)(y*FRACUNIT))->sector;}
float ageOf(const Blast &b,float fraction) {return leveltime-b.tic+fraction;}

// Flashbulb's sparks (PLAN-explosions-flashbulb.md): few, fast, each a hot fragment of its own with a thin tail,
// thrown clear of the blast: raked away from the surface it went off on, fanned out in the open, a barrel's low
// enough to skip over the floor. A chain of barrels shares them: with many in the air already, half as many.
void throwStreaks(const Blast &b,int count) {
    uint32_t seed=b.seed^(uint32_t)leveltime*0x85ebca6bu;
    auto rnd=[&] {seed=hash32(seed+0x9e3779b9u);return (float)(seed>>8)/16777216.0f;};
    if(sparks.size()>120)count=(count+1)/2;
    for(int n=0;n<count;++n) {
        if(sparks.size()>=mostSparks)sparks.erase(sparks.begin());
        float zc=rnd()*2-1,a=rnd()*2*doomPi,flat=std::sqrt(std::max(0.0f,1-zc*zc));
        float way[3]={flat*std::cos(a)+b.out[0]*0.9f,flat*std::sin(a)+b.out[1]*0.9f,zc+b.out[2]*0.9f};
        if(b.barrel)way[2]=std::abs(way[2])*0.3f;
        float along=way[0]*b.out[0]+way[1]*b.out[1]+way[2]*b.out[2];
        if(along<0.1f)for(int c=0;c<3;++c)way[c]+=b.out[c]*(0.2f-along);   // never into what it went off on
        float l=std::max(1e-3f,std::sqrt(way[0]*way[0]+way[1]*way[1]+way[2]*way[2]));
        float speed=n%3==0?16+8*rnd():8+9*rnd();   // a third of them fast enough to leave the blast behind at once
        Spark s{b.x,b.y,b.z,b.x,b.y,b.z,way[0]/l*speed,way[1]/l*speed,way[2]/l*speed+1,leveltime,8+(int)(9*rnd()),0.9f,0.75f+0.25f*rnd(),false};
        s.streak=true;
        sparks.push_back(s);
    }
}
void throwSparks(const Blast &b,int count,int bits,float push) {
    uint32_t seed=b.seed^(uint32_t)leveltime*0x9e3779b9u;
    auto rnd=[&] {seed=hash32(seed+0x9e3779b9u);return (float)(seed>>8)/16777216.0f;};
    bool low=b.z-b.floor<32;
    for(int n=0;n<count+bits;++n) {
        if(sparks.size()>=mostSparks)sparks.erase(sparks.begin());
        bool bit=n>=count;
        float a=rnd()*2*doomPi,out=(bit?1.5f+2.5f*rnd():3+7*rnd())*push,up=bit?3+3*rnd():(low?1:-3)+8*rnd();
        Spark s{b.x,b.y,b.z,b.x,b.y,b.z,std::cos(a)*out,std::sin(a)*out,up,leveltime,
                bit?110+(int)(110*rnd()):14+(int)(26*rnd()),bit?1.3f:0.7f+0.5f*rnd(),0.6f+0.4f*rnd(),bit};
        sparks.push_back(s);
    }
}
// The floor's own dust, paler, in the light there.
std::array<float,3> dustColour(const sector_t &sector,bool ceiling) {
    std::array<float,3> colour={0.45f,0.42f,0.38f};
    int pic=ceiling?sector.ceilingpic:sector.floorpic;
    if(pic!=skyflatnum) {
        const auto &average=averageColor(lumpImage(firstflat+flattranslation[pic],true));
        for(int c=0;c<3;++c)colour[c]=average[c]+(0.7f-average[c])*0.45f;
    }
    return colour;
}
void addMote(const Mote &m) {
    if(motes.size()>=mostMotes)motes.erase(motes.begin());
    motes.push_back(m);
}
// A ring of dust out along the floor, and what the ceiling over it lets go.
void throwDust(const Blast &b,float share) {
    float now=gameSeconds();
    const sector_t &sector=*sectorAt(b.x,b.y);
    if(b.z-b.floor<48&&sector.floorpic!=skyflatnum) {
        auto colour=dustColour(sector,false);auto lit=particleColour(colour,b.x,b.y,b.floor+4,false);
        for(int n=0,count=(int)(40*share*b.scale);n<count;++n) {
            float a=randomRange(0,2*doomPi),v=randomRange(240,460)*b.scale;
            addMote({b.x,b.y,b.floor+randomRange(1,5),std::cos(a)*v,std::sin(a)*v,randomRange(0,60),randomRange(0.22f,0.4f),
                     -randomRange(2,9),randomRange(10,26),now,randomRange(1.1f,2.4f),randomRange(1.0f,2.2f),colour,lit});
        }
    }
    if(!b.sky&&b.ceiling-b.z<200) {
        auto colour=dustColour(sector,true);auto lit=particleColour(colour,b.x,b.y,b.ceiling-4,false);
        for(int n=0,count=(int)(26*share);n<count;++n) {
            float a=randomRange(0,2*doomPi),d=randomRange(0,110)*b.scale,x=b.x+std::cos(a)*d,y=b.y+std::sin(a)*d;
            const sector_t *over=sectorAt(x,y);
            if(over->ceilingpic==skyflatnum)continue;
            addMote({x,y,ceilZ(over)-0.5f,0,0,0,0.1f,randomRange(10,28),randomRange(70,130),now+randomRange(0.03f,0.3f),
                     randomRange(1.2f,2.2f),randomRange(1.0f,2.2f),colour,lit});
        }
    }
}
// A barrel's own drawing, cut up and thrown: charred, bouncing, lying where it lands.
void throwBarrel(const Blast &b,const mobj_t &thing) {
    if(!thing.info)return;
    const state_t &state=states[thing.info->spawnstate];
    if(state.sprite<0||state.sprite>=numsprites)return;
    const auto &sprite=sprites[state.sprite];int frame=state.frame&FF_FRAMEMASK;
    if(frame>=sprite.numframes)return;
    const Image &image=lumpImage(firstspritelump+sprite.spriteframes[frame].lump[0],false);
    std::vector<HeadTexel> texels;
    for(int r=0;r<image.height;++r)for(int c=0;c<image.width;++c) {
        size_t p=(size_t)r*image.width+c;
        if(!image.pixels[2*p+1])continue;
        // Burnt: the game's own darker shades of its colours, the upper rows more.
        int burn=8+(int)(10*hashUnit(c,r,b.seed))+(r<image.height/2?4:0);
        byte colour=colormaps?colormaps[std::min(burn,28)*256+image.pixels[2*p]]:image.pixels[2*p];
        texels.push_back({c-image.left,image.top-r-1,colour,colour});
    }
    if(texels.size()<16)return;
    const mobj_t *viewer=players[displayplayer].mo;
    float tx=units(thing.x),ty=units(thing.y),tz=units(thing.z);
    float dx=tx-(viewer?units(viewer->x):tx-1),dy=ty-(viewer?units(viewer->y):ty),l=std::max(1e-3f,std::hypot(dx,dy)),rx=dy/l,ry=-dx/l;
    uint32_t seed=b.seed^0x3c6ef372u;
    auto rnd=[&] {seed=hash32(seed+0x9e3779b9u);return (float)(seed>>8)/16777216.0f;};
    int count=std::clamp(5+(int)(texels.size()/160),5,9);
    std::vector<bool> torn;
    std::vector<int> pieceOf=burstCut(texels,count,seed,torn,false);
    for(int k=0;k<count;++k) {
        std::vector<HeadTexel> chunk;float px=0,pz=0;
        for(size_t i=0;i<texels.size();++i)if(pieceOf[i]==k) {px+=texels[i].x+0.5f;pz+=texels[i].z+0.5f;chunk.push_back(texels[i]);}
        if(chunk.size()<6)continue;
        px/=chunk.size();pz/=chunk.size();
        int ix=(int)std::lround(px),iz=(int)std::lround(pz);
        for(HeadTexel &t:chunk) {t.x-=ix;t.z-=iz;}
        std::vector<bool> whole(chunk.size(),false);
        // Out all round, the side it was on a little more, the upper ones higher.
        float a=rnd()*2*doomPi,wx=std::cos(a)+rx*px*0.08f,wy=std::sin(a)+ry*px*0.08f,wl=std::max(1e-3f,std::hypot(wx,wy));
        FlyingPieceStart start{tx+rx*px,ty+ry*px,tz+pz,wx/wl,wy/wl,0,0,1.3f+1.5f*rnd(),seed^(uint32_t)k*0x27d4eb2fu,BLOOD_RED,false,nullptr,false,0,
                               &whole,3.5f+4*rnd()+0.08f*pz};
        auto piece=startFlyingPiece(chunk,start);
        if(!piece)continue;
        while(wreckage.size()>=mostWreck) {dropFlyingPiece(*wreckage.front().piece);wreckage.pop_front();}
        wreckage.push_back({piece,leveltime});
    }
}

Blast &start(float x,float y,float z,float scale,int hot,bool barrel,const mobj_t *source) {
    checkLevel();
    while(blasts.size()>=mostBlasts) {dropBlast(blasts.front());blasts.pop_front();}
    Blast b{x,y,z,leveltime,scale,hot,barrel,source};
    b.mode=settings.explosions;
    b.seed=hash32((uint32_t)(int)(x*8)*73856093u^(uint32_t)(int)(y*8)*19349663u^(uint32_t)leveltime*40503u^0x7f4a7c15u);
    // Clear of the walls beside it, or half the fire is in them.
    for(int k=0;k<8;++k) {
        float a=k*doomPi/4,dx=std::cos(a),dy=std::sin(a);WallHit hit;
        if(firstSolidWall(b.x,b.y,b.z,dx,dy,0,28,hit)) {b.x-=dx*(28-hit.t);b.y-=dy*(28-hit.t);b.out[0]-=dx*(28-hit.t);b.out[1]-=dy*(28-hit.t);}
    }
    const sector_t *sector=sectorAt(b.x,b.y);
    b.floor=floorZ(sector);b.ceiling=ceilZ(sector);b.sky=sector->ceilingpic==skyflatnum;
    b.z=std::max(b.z,b.floor+10*scale);
    if(!b.sky)b.z=std::min(b.z,std::max(b.floor+4,b.ceiling-6));
    // The way out: off the wall it was pushed clear of, up off the floor it is low over, down from a ceiling close over it.
    if(b.z-b.floor<28)b.out[2]+=28-(b.z-b.floor);
    if(!b.sky&&b.ceiling-b.z<28)b.out[2]-=28-(b.ceiling-b.z);
    if(float l=std::sqrt(b.out[0]*b.out[0]+b.out[1]*b.out[1]+b.out[2]*b.out[2]);l>2)for(float &c:b.out)c/=l;
    else b.out[0]=b.out[1]=b.out[2]=0;
    if(b.mode==3) {
        // Flashbulb: a burst of fire that opens up within eight tics (explosion_fire.cpp), no dust, no smoke; a flash,
        // a fill that fades, sparks that fly clear, the scorch.
        startBlastPicture(b.picture,scale,b.seed,barrel?20:18,b.z-b.floor<28*scale,b.sky?1e4f:b.ceiling-b.z,true,b.out);
        // The first beat reaches the room: by a floor, a thin broken skirt of its grit swept out ahead of the fire,
        // low and soon down. (In the open, a faint ring in the air instead: buildExplosionShimmer.)
        if(const sector_t *under=sectorAt(b.x,b.y);b.z-b.floor<48&&under->floorpic!=skyflatnum&&!overLiquid(b.x,b.y)) {
            float now=gameSeconds();
            auto colour=dustColour(*under,false);auto lit=particleColour(colour,b.x,b.y,b.floor+3,false);
            for(int n=0,count=barrel?26:18;n<count;++n) {
                float a=randomRange(0,2*doomPi),v=randomRange(380,600)*(barrel?1.2f:1.0f);
                if(hashUnit(n,(int)(a*3),b.seed)<0.25f)continue;   // broken: gaps in it
                addMote({b.x,b.y,b.floor+randomRange(1,3),std::cos(a)*v,std::sin(a)*v,randomRange(0,25),randomRange(0.16f,0.26f),
                         0,randomRange(40,70),now,randomRange(0.4f,0.7f),randomRange(0.8f,1.5f),colour,lit});
            }
        }
        throwStreaks(b,barrel?14+(int)(hashUnit(1,2,b.seed)*7):8+(int)(hashUnit(1,2,b.seed)*5));
    } else {
        if(b.mode==2)b.grid=startBlastGrid(b.x,b.y,b.z,barrel,scale,b.seed);
        else startBlastPicture(b.picture,scale,b.seed,hot,b.z-b.floor<28*scale,b.sky?1e4f:b.ceiling-b.z);
        if(b.mode==2&&!barrel) {
            // The physical rocket: a charge. Its light burns a few tics, not twelve; its fragments are Flashbulb's
            // streaks, a few sparks with them; its dust is the grid's.
            b.hot=5;
            throwStreaks(b,10+(int)(hashUnit(1,2,b.seed)*4));
            throwSparks(b,12,2,1);
        } else {
            throwSparks(b,(int)(34*scale),(int)(6*scale),1);
            throwDust(b,1);
        }
    }
    blastScorch(b.x,b.y,b.z,66*scale);
    blasts.push_back(std::move(b));
    return blasts.back();
}
Blast *blastOf(const mobj_t *source) {
    for(Blast &b:blasts)if(b.source==source)return &b;
    return nullptr;
}

// The blast's light at age tics: how strong, how far, what colour, how high over its middle.
bool lightOf(const Blast &b,float age,float &strength,float &radius,std::array<float,3> &colour,float &lift) {
    float big=b.barrel?1.15f:std::sqrt(b.scale);
    lift=8+std::min(std::floor(age)*2.2f,44.0f);
    if(b.mode==3) {
        // Flashbulb's light is its fire's (the user: no pure white point light; and no light without a source): ivory
        // for the tic it goes off, then as warm and as strong as the fire still is, amber as the lobes cool, a
        // dim red with the last shreds, out with them at eighteen tics (a barrel's twenty). A barrel's damage, ten
        // tics in, is lit by the same fire: a short warm lift then, when the bodies come apart.
        float last=b.barrel?20.0f:18.0f,t=std::clamp(age/last,0.0f,1.0f);
        if(age>=last)return false;
        float burn=age<1?3.6f:age<9?2.6f-1.6f*(age-1)/8:1.0f*std::pow(1-(age-9)/(last-9),1.5f);
        float cool=std::clamp((age-1)/8,0.0f,1.0f),ember=std::clamp((age-9)/(last-9),0.0f,1.0f);
        colour={1.0f,0.9f-0.16f*cool-0.18f*ember,0.7f-0.26f*cool-0.2f*ember};   // (warm from its first tic: no white)
        strength=burn*big;radius=age<1?420.0f:520-120*t;
        if(b.barrel&&b.hitTic>=0) {
            float since=leveltime-b.hitTic+(age-std::floor(age));
            if(since>=0&&since<8) {strength=std::max(strength,2.2f*big*(1-since/8));colour={1.0f,0.86f,0.62f};}
        }
        return strength>0.02f;
    }
    if(age<2) {strength=4.0f*big;radius=640;colour={1.0f,0.93f,0.72f};}
    else if(age<b.hot) {
        float t=(age-2)/(b.hot-2);
        strength=(2.8f-1.4f*t)*big;radius=600-120*t;colour={1.0f,0.75f-0.2f*t,0.4f-0.2f*t};
    } else {
        float t=(age-b.hot)/lingerTics;
        if(t>=1)return false;
        // Fast hit, slow recovery; flickering on tics as embers do.
        strength=1.4f*big*std::pow(1-t,2.0f)*(0.94f+0.12f*hashUnit((int)age/2,3,b.seed));
        radius=480-120*t;colour={1.0f,0.45f,0.16f};
    }
    // A barrel's damage, ten tics into its fire: a second flash.
    if(b.hitTic>=0&&leveltime-b.hitTic<2) {strength=std::max(strength,3.6f*big);radius=640;colour={1.0f,0.9f,0.65f};}
    return strength>0.02f;
}
std::array<float,3> dynamicAt(float x,float y,float z) {
    std::array<float,3> sum={};
    for(unsigned n=0;n<flashes.count;++n) {
        if(fogOnly[n])continue;
        float amount=flashAt(x,y,z,flashes.lights[n]);
        for(int c=0;c<3;++c)sum[c]+=amount*flashes.lights[n].color[c];
    }
    return sum;
}
// Sparks on tics: falling, off the floor once, stopped by what stands in their way.
void step() {
    if(steppedTic==leveltime)return;
    int tics=steppedTic<0||leveltime<steppedTic?1:std::min(leveltime-steppedTic,4);
    steppedTic=leveltime;
    // The fronts a ring further: glass and lamps go as it reaches them (both leave alone what is broken
    // already), and a liquid it crosses is whipped up along it.
    for(Front &f:fronts) {
        float r=std::min(f.radius,frontUnits*(leveltime-f.tic+1));
        if(r<=f.reached)continue;
        if(f.glass)glassBlast(f.x,f.y,f.z,r);
        if(f.lamps)blastLights(f.x,f.y,f.z,r);
        if(f.reached==0&&f.z-floorZ(sectorAt(f.x,f.y))<40)bodySplash(f.x,f.y,9,60,16);   // right over a liquid: a column out of it
        float middle=(f.reached+r)*0.5f,strength=1-middle/std::max(f.radius,1.0f);
        for(int n=0;n<10;++n) {
            float a=(n+hashUnit(n,leveltime,(uint32_t)f.tic))*2*doomPi/10,x=f.x+std::cos(a)*middle,y=f.y+std::sin(a)*middle;
            if(f.z-floorZ(sectorAt(x,y))<72)bodySplash(x,y,1.5f+3*strength,8+10*strength,5,std::cos(a)*2,std::sin(a)*2);
        }
        // And a flame it passes is blown at: sparks torn off it, away from the blast.
        for(int i=0;i<numsectors;++i)for(const mobj_t *thing=sectors[i].thinglist;thing;thing=thing->snext) {
            float dx=units(thing->x)-f.x,dy=units(thing->y)-f.y,d=std::hypot(dx,dy);
            if(d<=f.reached||d>r||d<1||!decorationLight(*thing))continue;
            BakeLight flame=decorationSource(*thing);
            if(!(flame.color[0]>flame.color[1]*1.15f&&flame.color[0]>flame.color[2]*1.5f))continue;   // fire only, as the embers
            for(int n=0;n<7;++n) {
                if(sparks.size()>=mostSparks)sparks.erase(sparks.begin());
                float out=2+4*hashUnit(n,leveltime,(uint32_t)f.tic),side=(hashUnit(n,7,(uint32_t)f.tic)-0.5f)*2;
                sparks.push_back(Spark{flame.x,flame.y,flame.z,flame.x,flame.y,flame.z,dx/d*out-dy/d*side,dy/d*out+dx/d*side,1+3*hashUnit(n,3,(uint32_t)f.tic),
                                       leveltime,12+(int)(16*hashUnit(n,5,(uint32_t)f.tic)),0.7f,0.7f,false});
            }
        }
        f.reached=r;
    }
    fronts.erase(std::remove_if(fronts.begin(),fronts.end(),[](const Front &f){return f.reached>=f.radius||leveltime<f.tic;}),fronts.end());
    std::vector<Spark> flecks;
    // Where a streak strikes: its tail ends there (the new one follows the bounce), two or three tiny flecks fly off.
    auto strikes=[&](Spark &s) {
        s.kept=0;
        for(int n=0,count=2+(hashUnit(s.born,(int)s.x,7u)<0.4f);n<count;++n) {
            float a=hashUnit(n,(int)(s.x+s.y),(uint32_t)leveltime)*2*doomPi,v=1.5f+2.5f*hashUnit(n,3,(uint32_t)s.born);
            flecks.push_back(Spark{s.x,s.y,s.z,s.x,s.y,s.z,std::cos(a)*v,std::sin(a)*v,1+2*hashUnit(n,5,(uint32_t)s.born),leveltime,5+n,0.5f,0.6f,false});
        }
    };
    for(int n=0;n<tics;++n)for(Spark &s:sparks) {
        s.wasX=s.x;s.wasY=s.y;s.wasZ=s.z;
        if(s.resting)continue;
        if(s.streak) {
            for(int k=4;k>0;--k)std::copy(s.trail[k-1],s.trail[k-1]+3,s.trail[k]);
            s.trail[0][0]=s.x;s.trail[0][1]=s.y;s.trail[0][2]=s.z;s.kept=std::min(s.kept+1,5);
        }
        s.vz-=sparkGravity;
        float nx=s.x+s.vx,ny=s.y+s.vy;
        const sector_t *to=sectorAt(nx,ny);
        if(floorZ(to)>s.z+12||ceilZ(to)<s.z+1) {   // a wall, a step's face
            if(s.streak&&s.bounced) {s.life=std::min(s.life,leveltime-s.born+1);continue;}   // its second: out
            s.vx*=-0.4f;s.vy*=-0.4f;
            if(s.streak) {s.bounced=true;strikes(s);}
        }
        else {s.x=nx;s.y=ny;}
        s.z+=s.vz;
        const sector_t *at=sectorAt(s.x,s.y);
        float floor=floorZ(at)+0.3f,ceiling=ceilZ(at)-0.5f;
        if(s.z>ceiling&&at->ceilingpic!=skyflatnum) {s.z=ceiling;s.vz=-std::abs(s.vz)*0.3f;}
        if(s.z<=floor) {
            s.z=floor;
            if(!s.bounced&&s.vz<-1.5f) {s.bounced=true;s.vz=-s.vz*0.35f;s.vx*=0.6f;s.vy*=0.6f;if(s.streak)strikes(s);}
            else if(s.bit) {s.resting=true;s.vx=s.vy=s.vz=0;}
            else s.life=std::min(s.life,leveltime-s.born+2);   // out where it lands
        }
    }
    sparks.insert(sparks.end(),flecks.begin(),flecks.end());
    sparks.erase(std::remove_if(sparks.begin(),sparks.end(),[](const Spark &s){return leveltime-s.born>=s.life||leveltime<s.born;}),sparks.end());
    for(Blast &b:blasts)if(b.source&&leveltime-b.tic>(b.barrel?32:19))b.source=nullptr;   // its thing is gone by now: the address may be another's
    while(!blasts.empty()&&(leveltime<blasts.front().tic||(!blasts.front().pictureLeft&&leveltime-blasts.front().tic>blasts.front().hot+lingerTics))) {
        dropBlast(blasts.front());blasts.pop_front();
    }
}
} // namespace

bool explosionsOn() {return settings.explosions&&Game_Explosions();}

// A blast's reach on glass or on lamps, asked for where the engine breaks them at once: with the physical
// explosion its front does it in turn (true: taken on; false: the caller breaks them now, as ever).
bool explosionFront(float x,float y,float z,float radius,bool glass,bool lamps) {
    if(!explosionsOn()||settings.explosions!=2||gamestate!=GS_LEVEL)return false;
    checkLevel();
    for(Front &f:fronts)if(f.tic==leveltime&&std::abs(f.x-x)<24&&std::abs(f.y-y)<24) {   // the same blast, asked about its other kind
        f.glass|=glass;f.lamps|=lamps;f.radius=std::max(f.radius,radius);return true;
    }
    fronts.push_back({x,y,z,radius,leveltime,glass,lamps});
    return true;
}

void explosionAt(mobj_t *spot,int damage) {
    if(!spot||!explosionsOn()||gamestate!=GS_LEVEL)return;
    checkLevel();
    game_thing_role role=Game_ThingRole(spot->type);
    float x=units(spot->x),y=units(spot->y),z=units(spot->z);
    if(role==GAME_THING_ROCKET) {
        // A rocket goes off once. A blast already holding this address is an older rocket's: the game hands a freed
        // rocket's memory to the next one fired, and taken for the old one the new rocket flew unseen, got no blast
        // and showed Doom's own explosion once the old one was forgotten (the user: sometimes no rocket flies and
        // the standard explosion appears).
        for(Blast &old:blasts)if(old.source==spot&&old.tic!=leveltime)old.source=nullptr;
        if(!blastOf(spot))start(x,y,z+units(spot->height)*0.5f,std::clamp(damage/128.0f,0.7f,1.4f),12,false,spot);
    } else if(role==GAME_THING_BARREL) {
        for(Blast &old:blasts)if(old.source==spot&&(!old.barrel||leveltime-old.tic>32))old.source=nullptr;   // (another thing's blast at this address)
        Blast *b=blastOf(spot);
        if(!b) {b=&start(x,y,z+20,1.3f,20,true,spot);throwBarrel(*b,*spot);}
        else if(b->mode==3) {b->hitTic=leveltime;throwStreaks(*b,5);}
        else {b->hitTic=leveltime;throwSparks(*b,22,2,1.3f);throwDust(*b,0.6f);}
    } else return;
    if(!goreOn())flyingPiecesBlast(x,y,z,(float)damage);   // (gore throws them itself: body_burst.cpp)
}
void explosionBarrel(const mobj_t &thing,float x,float y,float z) {
    if(!explosionsOn()||Game_BlastFrame(&thing)<1)return;
    checkLevel();
    for(Blast &old:blasts)if(old.source==&thing&&(!old.barrel||leveltime-old.tic>32))old.source=nullptr;   // (as in explosionAt)
    if(blastOf(&thing))return;
    Blast &b=start(x,y,z+20,1.3f,20,true,&thing);
    throwBarrel(b,thing);
}
// The rocket that went off inside the balloon (caco_pop.cpp): no fireball outside him.
void explosionCancel(const mobj_t &thing) {
    for(auto it=blasts.begin();it!=blasts.end();++it)if(it->source==&thing) {dropBlast(*it);blasts.erase(it);return;}
}
bool explosionRim(float x,float y,float z) {
    static const bool off=getenv("DERP_NORIM")!=nullptr;   // test runs: without it, to compare
    if(blasts.empty()||off)return false;
    for(const Blast &b:blasts) {
        if(b.mode!=3||leveltime<b.tic||leveltime-b.tic>=(b.barrel?20:18))continue;
        if(std::abs(x-b.x)<480&&std::abs(y-b.y)<480&&std::abs(z-b.z)<320)return true;
    }
    return false;
}
bool explosionHides(const mobj_t &thing) {
    if(blasts.empty()||!explosionsOn())return false;
    // Only what is going off: a rocket that has struck (no longer a missile), a barrel in its blast frames. By the
    // address alone, a new rocket given a spent one's memory was hidden in flight.
    game_thing_role role=Game_ThingRole(thing.type);
    bool going=role==GAME_THING_ROCKET?!(thing.flags&MF_MISSILE):role==GAME_THING_BARREL&&Game_BlastFrame(&thing)>=1;
    if(!going)return false;
    for(const Blast &b:blasts)if(b.source==&thing&&b.barrel==(role==GAME_THING_BARREL))return true;
    return false;
}

// The newest three shine: more at once only wash the room out.
void explosionLights(float fraction) {
    if(!explosionsOn())return;
    checkLevel();step();
    // Test runs: DERP_EXPLODE=x,y,tic[,n] sets a rocket's blast off there, 32 over the floor (1: a barrel's, at the
    // barrel nearest there, which stays as it is, its pieces thrown all the same; 2: a barrel's mix at the spot itself;
    // 3: the game's own barrel nearest there is shot, so it blows as in play; 4: the game's own rocket is put there
    // 24 over the floor and killed, so it goes off as one that struck something).
    static unsigned tested=~0u;
    if(const char *at=getenv("DERP_EXPLODE");at&&tested!=r_levelserial) {
        float x=0,y=0;int tic=0,barrel=0;
        if(sscanf(at,"%f,%f,%d,%d",&x,&y,&tic,&barrel)>=3&&leveltime>=tic) {
            tested=r_levelserial;
            const mobj_t *nearest=nullptr;
            if(barrel==1||barrel==3)for(int i=0;i<numsectors;++i)for(const mobj_t *thing=sectors[i].thinglist;thing;thing=thing->snext)
                if(Game_ThingRole(thing->type)==GAME_THING_BARREL&&(!nearest||std::hypot(units(thing->x)-x,units(thing->y)-y)<std::hypot(units(nearest->x)-x,units(nearest->y)-y)))nearest=thing;
            if(nearest) {x=units(nearest->x);y=units(nearest->y);}
            if(barrel==3) {
                if(nearest) {fprintf(stderr,"Test: the barrel at %.0f,%.0f shot.\n",x,y);P_DamageMobj(const_cast<mobj_t*>(nearest),nullptr,nullptr,1000);}
            } else if(barrel==4) {
                for(int type=0;type<NUMMOBJTYPES;++type)if(Game_ThingRole(type)==GAME_THING_ROCKET) {
                    mobj_t *rocket=P_SpawnMobj((fixed_t)(x*FRACUNIT),(fixed_t)(y*FRACUNIT),(fixed_t)((floorZ(sectorAt(x,y))+24)*FRACUNIT),(mobjtype_t)type);
                    fprintf(stderr,"Test: a rocket at %.0f,%.0f goes off.\n",x,y);
                    P_DamageMobj(rocket,nullptr,nullptr,1000);   // (not shootable: nothing; so its death state by hand below)
                    rocket->momx=rocket->momy=rocket->momz=0;
                    P_SetMobjState(rocket,(statenum_t)rocket->info->deathstate);rocket->flags&=~MF_MISSILE;
                    break;
                }
            } else {
            Blast &b=start(x,y,floorZ(sectorAt(x,y))+(barrel?20:32),barrel?1.3f:1.0f,barrel?20:12,barrel!=0,nullptr);
            if(!explosionFront(b.x,b.y,b.z,128,true,true)) {glassBlast(b.x,b.y,b.z,128);blastLights(b.x,b.y,b.z,128);}
            if(nearest) {fprintf(stderr,"Test blast at the barrel at %.0f,%.0f.\n",x,y);throwBarrel(b,*nearest);}
            }
        }
    }
    int shining=0;
    for(auto it=blasts.rbegin();it!=blasts.rend()&&shining<3;++it) {
        const Blast &b=*it;
        float strength,radius,lift;std::array<float,3> colour;
        if(leveltime<b.tic||!lightOf(b,ageOf(b,fraction),strength,radius,colour,lift))continue;
        // The physical one shines from where its heat is, and goes where the fire goes (under the ceiling, into
        // the doorway); once it has burnt out, from where it last was.
        float at[3]={b.x,b.y,b.z+lift};
        if(b.grid) {blastGridHeat(*b.grid,at);at[2]+=8;}
        const sector_t *lit=sectorAt(at[0],at[1]);
        float z=at[2],lowest=b.grid?floorZ(lit):b.floor,highest=b.grid?ceilZ(lit):b.ceiling;bool open=b.grid?lit->ceilingpic==skyflatnum:b.sky;
        if(!open)z=std::min(z,highest-10);
        z=std::max(z,std::min(lowest+20,highest-2));
        z=std::floor(z/4)*4;at[0]=std::floor(at[0]/4)*4;at[1]=std::floor(at[1]/4)*4;   // in steps: its shadows' walls are found afresh only when it has moved
        if(!b.sky)z=std::min(z,b.ceiling-10);
        z=std::max(z,std::min(b.floor+20,b.ceiling-2));
        appendLight(at[0],at[1],z,radius,strength,colour,nullptr,0.75f,true,0,{0,0,0,settings.softShotShadows?-6.0f:0.0f});
        // The room's own lift only while it burns: under the afterglow's flicker the room stepped up and down with it.
        if(leveltime-b.tic<b.hot)roomPulseAdd(b.x,b.y,radius,strength);
        ++shining;
    }
}

void addExplosionCards(const Uniforms &camera,float fraction) {
    if(blasts.empty()&&wreckage.empty())return;
    checkLevel();step();
    float ax,ay;cardAcross(camera,ax,ay);
    auto card=[&](int key,float x,float y,float top,float size,unsigned mode,float light,const std::array<float,3> &rgb,unsigned tint,float soft) {
        auto &batch=batches[key];size_t from=batch.size();
        float h=size*0.5f,bottom=top-size;
        quad(batch,{x-ax*h,y-ay*h,top,0,0,light,mode},{x+ax*h,y+ay*h,top,size,0,light,mode},
             {x+ax*h,y+ay*h,bottom,size,size,light,mode},{x-ax*h,y-ay*h,bottom,0,size,light,mode});
        for(size_t n=from;n<batch.size();++n) {
            batch[n].red=rgb[0];batch[n].green=rgb[1];batch[n].blue=rgb[2];batch[n].lampTint=tint;
            if(soft>0)batch[n].sunU=soft;
        }
    };
    // Doom's stepping (the user: redrawn every tic it was too smooth): a drawing is held as the game's own
    // explosion frames are, two tics through the burst, three while it burns, four once it is smoke. In slow
    // motion too: its frames are held longer there, as every sprite's are.
    for(Blast &b:blasts) {
        if(!b.pictureLeft||leveltime<b.tic)continue;
        int age=leveltime-b.tic,hold=b.mode==3?(age<4?1:2):age<4?2:age<b.hot+6?3:age<70?4:6,stepNow=age/hold*hold;   // (Flashbulb's eight tics: a pose a tic)   // (the smoke that lingers: six)
        if(b.grid) {
            // The physical one: its gas a step on, and the eye's picture of it, on a card through the nearest of it.
            b.pictureLeft=blastGridTic(*b.grid,age);
            if(!b.pictureLeft) {dropBlastGrid(b.grid);b.grid=nullptr;continue;}
            BlastGridCard seen;
            if(!blastGridCard(*b.grid,camera,stepNow,seen))continue;
            // (No soft sprite's dissolve at floors and ceilings: its rays stop at the map's walls themselves, and a
            // layer under a ceiling would be all dither.)
            // The card stands across the view through the gas's middle and would be cut where it meets a wall,
            // though the gas it shows is in front of that wall (the user: it reads as a sprite). Its rays stop at
            // the map's own walls already, so it is drawn as near as its nearest gas (a soft sprite's pull toward
            // the eye), without that kind's dissolve at floors and ceilings (sunV 1: world.frag).
            unsigned mode=MODE_SPRITE|MODE_SOFT|sectorMode(sectorAt(seen.x,seen.y));
            auto plane=[&](int key,float nearer,unsigned m,float soft) {
                auto &batch=batches[key];size_t from=batch.size();
                float h=seen.wide*0.5f,px=seen.x+seen.ay*nearer,py=seen.y-seen.ax*nearer,top=seen.bottom+seen.tall;
                quad(batch,{px-seen.ax*h,py-seen.ay*h,top,0,0,1,m},{px+seen.ax*h,py+seen.ay*h,top,(float)seen.w,0,1,m},
                     {px+seen.ax*h,py+seen.ay*h,seen.bottom,(float)seen.w,(float)seen.h,1,m},{px-seen.ax*h,py-seen.ay*h,seen.bottom,0,(float)seen.h,1,m});
                for(size_t n=from;n<batch.size();++n)if(soft>0) {batch[n].sunU=soft;batch[n].sunV=1;}
            };
            // (The smoke holds its light already: each texel lit where it hangs. Drawn as it is.)
            if(seen.smoke)plane(seen.smokeKey,0,mode,seen.pull+8);
            if(seen.fire) {plane(seen.fireKey,0.5f,mode,seen.pull+8);plane(seen.fireKey,1,MODE_SPRITE|MODE_PICKUP|MODE_PICKUP_GLOW,0);}   // (the second for the bloom, as below)
            continue;
        }
        b.pictureLeft=drawBlastPicture(b.picture,(float)stepNow+1,stepNow,ax,ay);
        if(!b.pictureLeft) {dropBlastPicture(b.picture);continue;}
        const sector_t *sector=sectorAt(b.x,b.y);
        float size=(float)b.picture.size,top=b.z+size*b.picture.middle,soft=std::clamp(size*0.5f,8.0f,64.0f);
        unsigned mode=MODE_SPRITE|MODE_SOFT|sectorMode(sector);
        if(b.picture.fire) {
            card(b.picture.fireKey,b.x,b.y,top,size,mode,1,{0,0,0},0xFFFFFFFFu,soft);
            // And once more, a unit nearer, for the bloom alone (a pickup's glow: emission.frag); behind the
            // first in the view itself, which is drawn pulled toward the eye.
            float nx=-ay,ny=ax;
            if(nx*(camera.eye[0]-b.x)+ny*(camera.eye[1]-b.y)<0) {nx=-nx;ny=-ny;}
            card(b.picture.fireKey,b.x+nx,b.y+ny,top,size,MODE_SPRITE|MODE_PICKUP|MODE_PICKUP_GLOW,1,{0,0,0},0xFFFFFFFFu,0);
        }
        if(b.picture.smoke) {
            // The smoke in the room's light, and the blast's own from under it.
            float mz=b.z+20;
            StaticSample here;auto lit=bakedLightAt(b.x,b.y,mz,sector,&here);
            float level=roomPulse(sector,lighting(sector));
            // (A third of it, and no brighter than a lit wall: in the fire's full light it was a brown boulder.)
            auto glow=dynamicAt(b.x,b.y,mz);
            for(int c=0;c<3;++c)lit[c]=std::min(1.0f,lit[c]+glow[c]*0.3f);
            card(b.picture.smokeKey,b.x,b.y,top,size,mode,level,lit,packedLampTint(here,level),soft);
        }
    }
    // The wreckage: lit where each piece is, a shadow under it.
    const mobj_t *anyone=players[displayplayer].mo;
    unsigned relit=settings.detail?MODE_RELIT:0u;
    for(Wreck &w:wreckage) {
        flyingPieceTic(*w.piece,leveltime-w.start);
        RagdollCard piece;
        if(!flyingPieceInView(*w.piece)||!flyingPieceCard(*w.piece,fraction,piece))continue;
        const sector_t *sector=sectorAt(piece.x,piece.y);
        float mz=piece.top-piece.size*0.5f;
        StaticSample here;auto lit=bakedLightAt(piece.x,piece.y,mz,sector,&here);
        float level=roomPulse(sector,lighting(sector));
        if(!relit) {auto glow=dynamicAt(piece.x,piece.y,mz);for(int c=0;c<3;++c)lit[c]+=glow[c];}
        card(piece.key,piece.x,piece.y,piece.top,(float)piece.size,MODE_SPRITE|relit|(relit&&explosionRim(piece.x,piece.y,mz)?MODE_RIM:0u),level,lit,packedLampTint(here,level),0);
        auto found=images.find(piece.key);
        if(anyone&&found!=images.end())addEnemyShadow(*anyone,found->second,-1-piece.key,false,piece.x,piece.y,0,camera,nullptr,true,&piece);
    }
}

void buildExplosionSpecks(const Uniforms &camera) {
    if(sparks.empty()&&motes.empty())return;
    checkLevel();step();
    for(const Spark &s:sparks) {
        float age=(leveltime-s.born)/(float)s.life,left=1-age;
        if(s.streak) {
            // A small ivory head cooling to orange; behind it a thin amber tail along where it has just been, which
            // shortens as it cools. Its tail starts afresh where it struck something.
            std::array<float,3> head={1.0f,0.55f+0.42f*left,0.2f+0.6f*left*left};
            pixelBlock(glowVertices,camera,s.x,s.y,s.z,left<0.3f?1.0f:1.5f,1,head,false,nullptr,false,nullptr,1,1,1);   // (never under a Doom pixel: at a room's length they were lost)
            int use=std::min(s.kept,1+(int)(4*left));
            float from[3]={s.x,s.y,s.z};
            for(int k=0;k<use;++k) {
                const float *to=s.trail[k];
                float d=std::sqrt((to[0]-from[0])*(to[0]-from[0])+(to[1]-from[1])*(to[1]-from[1])+(to[2]-from[2])*(to[2]-from[2]));
                int steps=std::clamp((int)(d/1.6f),1,12);
                for(int m=1;m<=steps;++m) {
                    float t=m/(float)steps,fade=left*(1-(k+t)/(use+0.5f));
                    std::array<float,3> tail={1.0f*fade,0.62f*fade,0.18f*fade};
                    pixelBlock(glowVertices,camera,from[0]+(to[0]-from[0])*t,from[1]+(to[1]-from[1])*t,from[2]+(to[2]-from[2])*t,0.8f,1,tail,false,nullptr,false,nullptr,1,1,0.6f);
                }
                std::copy(to,to+3,from);
            }
            continue;
        }
        // White-hot, then orange, then a dull red going out; a bit on the floor flickers as it dies.
        std::array<float,3> colour={1.0f,0.35f+0.6f*s.heat*left,0.05f+0.6f*s.heat*left*left};
        float shine=s.resting?(0.45f+0.55f*left)*(0.7f+0.3f*hashUnit(leveltime/2,(int)(s.x*4),(uint32_t)s.born)):std::min(1.0f,0.4f+left);
        for(float &c:colour)c*=shine;
        float size=left<0.25f&&!s.bit?s.size*0.6f:s.size;
        pixelBlock(glowVertices,camera,s.x,s.y,s.z,size,1,colour,s.resting,nullptr,false,nullptr,1,1,farLeast);
        if(!s.resting&&(s.x!=s.wasX||s.z!=s.wasZ)) {
            for(float &c:colour)c*=0.55f;
            pixelBlock(glowVertices,camera,(s.x+s.wasX)*0.5f,(s.y+s.wasY)*0.5f,(s.z+s.wasZ)*0.5f,size,1,colour,false,nullptr,false,nullptr,1,1,farLeast);
            for(float &c:colour)c*=0.6f;
            pixelBlock(glowVertices,camera,s.wasX,s.wasY,s.wasZ,size,1,colour,false,nullptr,false,nullptr,1,1,farLeast);
        }
    }
    float now=gameSeconds();
    motes.erase(std::remove_if(motes.begin(),motes.end(),[&](const Mote &m){return now-m.born>m.life||now<m.born-1;}),motes.end());
    for(const Mote &m:motes) {
        if(now<m.born)continue;   // not let go yet
        float t=std::floor((now-m.born)*TICRATE)/TICRATE,reach=m.tau*(1-std::exp(-t/m.tau));
        float x=m.x+m.vx*reach,y=m.y+m.vy*reach,z=m.z+m.vz*reach-m.sink*t-0.5f*m.fall*t*t;
        const sector_t *sector=sectorAt(x,y);
        if(z<=floorZ(sector))continue;   // down: settled
        z=std::min(z,ceilZ(sector)-0.2f);
        float left=1-t/m.life,size=left<0.25f?m.size*0.5f:m.size;
        // In the blast's own light while it lasts, as the wall's dust is in a spark's.
        std::array<float,3> colour=m.lit;
        auto glow=dynamicAt(x,y,z);
        for(int c=0;c<3;++c)colour[c]+=m.albedo[c]*glow[c]*0.6f;
        pixelBlock(particleVertices,camera,x,y,z,size,1,colour,false,&m.albedo,false,nullptr,1,1,farLeast);
    }
}

// Slow motion only: the shock running out through the air as a ring of shimmer, and the heat over the fire.
void buildExplosionShimmer(const Uniforms &camera) {
    float slow=(float)SlowMotion_Share();
    if(blasts.empty())return;
    constexpr int segments=28;constexpr float ringTics=14;
    auto at=[&](const Blast &b,float r,float a,float lift,float heat) {
        float c=std::cos(a)*r,s=std::sin(a)*r;
        return Vertex{b.x+camera.right[0]*c+camera.up[0]*s,b.y+camera.right[1]*c+camera.up[1]*s,b.z+lift+camera.right[2]*c+camera.up[2]*s,0,0,heat,0};
    };
    for(const Blast &b:blasts) {
        float age=ageOf(b,renderFraction);
        if(age<0)continue;
        float near=std::hypot(b.x-camera.eye[0],b.y-camera.eye[1]);
        // Slow motion's; and at any speed, faint and for its first tics, Flashbulb's in open air (its first beat
        // reaching the room where no floor is near to show it).
        float share=slow;
        if(b.mode==3&&age<5&&b.z-b.floor>=48)share=std::max(share,0.3f);
        float seen=share*std::clamp((near-48)/48,0.0f,1.0f);   // not in the eye's own face
        if(seen<=0.01f)continue;
        if(age<ringTics) {
            float r=20+340*b.scale*(1-std::exp(-age/5)),heat=seen*(1-age/ringTics)*(1-age/ringTics);
            for(int n=0;n<segments;++n) {
                float a0=n*2*doomPi/segments,a1=(n+1)*2*doomPi/segments;
                Vertex in0=at(b,r*0.8f,a0,0,0),in1=at(b,r*0.8f,a1,0,0),mid0=at(b,r*0.92f,a0,0,heat),mid1=at(b,r*0.92f,a1,0,heat),out0=at(b,r,a0,0,0),out1=at(b,r,a1,0,0);
                heatVertices.insert(heatVertices.end(),{in0,in1,mid1,in0,mid1,mid0,mid0,mid1,out1,mid0,out1,out0});
            }
        }
        float burning=age<b.hot?1.0f:1-(age-b.hot)/40;
        if(burning>0) {
            float r=b.picture.reach*1.5f,heat=0.8f*seen*burning,lift=b.picture.reach*0.5f;
            Vertex middle=at(b,0,0,lift,heat);
            for(int n=0;n<segments;++n) {
                float a0=n*2*doomPi/segments,a1=(n+1)*2*doomPi/segments;
                heatVertices.insert(heatVertices.end(),{middle,at(b,r,a0,lift,0),at(b,r,a1,lift,0)});
            }
        }
    }
}
void explosionsShutdown() {dropAll();seenLevel=~0u;}
} // namespace doom3d
