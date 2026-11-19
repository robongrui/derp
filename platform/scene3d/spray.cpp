/* Blood spray: what hits and kills throw. Drops fly as Doom-pixel specks
   and land as splats on floors, walls and ceilings; wounds drip; bodies
   sliding across the floor smear it, bodies thrown into walls splash them.
   Drawing only: own random numbers, nothing the game sees. */
#include "internal.h"
#include "ragdoll.h"
extern "C" {
#include "../audio/impact_synth.h"
#include "../audio/sound_options.h"
}

namespace doom3d {
// A drop's flight is worked out once, when it is thrown: where it lands
// (the floor under it, a wall in its way, the ceiling it rises into) and
// when. Its splat is laid then, shown from that moment, and the speck flies
// until it gets there, moving at the tic rate like the debris.
// Doom's: a unit a tic, each tic. On Phobos its own, next to nothing: drops
// fly straight on to whatever wall or ceiling is in their way.
static float sprayGravity() {return Game_Phobos()?Game_PhobosGravity()*1225:1225;}
struct Drop { float x,y,z,vx,vy,vz,born,end,size;std::array<float,3> color;float thrown;bool dry=false; };   // thrown: when (born later: a drip); dry: no sheen (bone)
static std::vector<Drop> drops;
// A hit's fine mist (sprayViscera): a small, thin puff of single-pixel specks behind the drops, spreading and
// sinking over a third of a second (the user wanted a cloud back, but not the curtain the first one was).
struct Mist { float x,y,z,born,radius;uint32_t seed;int tint; };
static std::vector<Mist> mists;
constexpr size_t maxDrops=768;
// Dying and dead bodies watched for sliding (smears) and stopping short (a wall).
struct Slide { float x,y,speed,dx,dy,since;bool slammed,seen; };
static std::unordered_map<const mobj_t*,Slide> slides;
static unsigned sprayLevelSerial;

static void checkSprayLevel() {
    if(sprayLevelSerial==r_levelserial)return;
    drops.clear();slides.clear();mists.clear();sprayLevelSerial=r_levelserial;
}
static const sector_t *sectorAt(float x,float y) {return R_PointInSubsector((fixed_t)(x*FRACUNIT),(fixed_t)(y*FRACUNIT))->sector;}
static bool bleeds(const mobj_t &thing) {
    if(!thing.info||!bleedsBlood(thing))return false;
    return (thing.info->flags&MF_COUNTKILL)||Game_ThingRole(thing.type)==GAME_THING_UNCOUNTED_MONSTER;
}
static bool sprayOn() {return settings.bloodSpray&&bloodOn();}

// Gore: while a body's hit throws its spray, who stands round it (body_stains.cpp) and the player's gun (gun_blood.cpp)
// are in the way of the drops.
struct Bystander { const mobj_t *thing;float x,y,z,radius,height; };
static std::vector<Bystander> bystanders;
static const mobj_t *paintBy=nullptr;
constexpr float bystanderReach=224,gunReach=128,gunNear=22;
struct Painting {
    explicit Painting(const mobj_t &source) {
        if(!goreOn())return;
        static const mobj_t *gathered=nullptr;static int gatheredAt=-1;
        paintBy=&source;
        if(gathered==&source&&gatheredAt==leveltime)return;
        gathered=&source;gatheredAt=leveltime;bystanders.clear();
        float sx=units(source.x),sy=units(source.y);
        for(int i=0;i<numsectors;++i)for(const mobj_t *m=sectors[i].thinglist;m;m=m->snext) {
            if(m==&source||!(m->flags&MF_COUNTKILL)||m->health<=0)continue;
            float x=units(m->x),y=units(m->y);
            if(std::hypot(x-sx,y-sy)<bystanderReach)bystanders.push_back({m,x,y,units(m->z),units(m->radius),units(m->height)});
        }
        if(M_CheckParm((char*)"-ragdolllog"))fprintf(stderr,"gorebystanders %zu\n",bystanders.size());
    }
    ~Painting() {paintBy=nullptr;}
};
// Throws a drop of size (its speck, units) from (x,y,z) at (vx,vy,vz) units
// a second; splat: the size of what it leaves (0 none); tint: the bleeder's.
// gathers: a stump's, landing with others into a pool (bloodPoolFeed); delay: let go that much later (a drip).
static void throwDrop(float x,float y,float z,float vx,float vy,float vz,float size,float splat,int kind,int tint,
                      bool gathers=false,float delay=0) {
    float thrown=gameSeconds(),now=thrown+delay;
    const sector_t *start=sectorAt(x,y);
    z=std::clamp(z,floorZ(start)+0.5f,ceilZ(start)-0.5f);
    auto fallTime=[&](float floor) {
        float d=z-floor;
        return d<0?-1.0f:(vz+std::sqrt(vz*vz+2*sprayGravity()*d))/sprayGravity();
    };
    // The floor: the start's, then the landing point's (a step's face in
    // between is found as a wall below).
    float t=fallTime(floorZ(start));
    const sector_t *landing=start;
    for(int pass=0;pass<2;++pass) {
        const sector_t *s=sectorAt(x+vx*t,y+vy*t);
        if(s==landing)break;
        float again=fallTime(floorZ(s));
        if(again<0)break;
        landing=s;t=again;
    }
    t=std::max(t,0.03f);
    enum { FLOOR,WALL,CEILING,GLASS } where=FLOOR;
    // The ceiling, when it rises that high first.
    if(vz>0&&start->ceilingpic!=skyflatnum) {
        float rise=ceilZ(start)-0.5f-z;
        if(rise>0&&vz*vz>2*sprayGravity()*rise) {
            float up=(vz-std::sqrt(vz*vz-2*sprayGravity()*rise))/sprayGravity();
            const sector_t *above=sectorAt(x+vx*up,y+vy*up);
            if(up<t&&std::abs(ceilZ(above)-ceilZ(start))<1) {t=up;where=CEILING;landing=above;}
        }
    }
    // A wall in the way, along the chord of its arc.
    float h=std::hypot(vx,vy);WallHit hit;
    if(h>1) {
        float reach=h*t,end=z+vz*t-0.5f*sprayGravity()*t*t;
        if(firstSolidWall(x,y,z,vx/h,vy/h,(end-z)/reach,reach,hit)) {t=hit.t/h;where=WALL;}
    }
    // Gore: someone standing in its way takes it, a stain instead of a splat beyond them.
    const mobj_t *painted=nullptr;
    if(paintBy&&h>1) {
        for(const Bystander &b:bystanders) {
            float at=std::clamp(((b.x-x)*vx+(b.y-y)*vy)/(h*h),0.0f,t);
            if(std::hypot(x+vx*at-b.x,y+vy*at-b.y)>b.radius)continue;
            float hz=z+vz*at-0.5f*sprayGravity()*at*at;
            if(hz<b.z||hz>b.z+b.height)continue;
            t=at;painted=b.thing;
        }
        // And a drop coming at the eye lands on the gun.
        if(const mobj_t *me=players[consoleplayer].mo) {
            float ex=units(me->x),ey=units(me->y),ez=units(players[consoleplayer].viewz);
            float at=std::clamp(((ex-x)*vx+(ey-y)*vy)/(h*h),0.0f,t);
            if(std::hypot(x-ex,y-ey)<gunReach&&std::hypot(x+vx*at-ex,y+vy*at-ey)<gunNear&&std::abs(z+vz*at-0.5f*sprayGravity()*at*at-ez)<gunNear+8)
                gunBloodDrop(tint,size);
        }
    }
    // An unbroken window in its way (windows.cpp), between its sill and lintel: it lands on the glass.
    int pane=-1;
    if(!painted&&h>1) {
        float s;int crossed=glassPaneCrossed(x,y,x+vx*t,y+vy*t,s);
        GlassFace face;
        if(crossed>=0&&glassPaneAt(crossed,face)) {
            float at=s*t,hz=z+vz*at-0.5f*sprayGravity()*at*at;
            if(hz>=face.bottom&&hz<=face.top) {t=at;pane=crossed;where=GLASS;}
        }
    }
    if(t>(Game_Phobos()?12.0f:2.5f))splat=0; // Down a pit (on Phobos: still on its way).
    float lx=x+vx*t,ly=y+vy*t,lz=z+vz*t-0.5f*sprayGravity()*t*t,fall=vz-sprayGravity()*t;
    if(painted) {bodyStain(painted,lx,ly,lz,std::max(splat,10.0f),tint,now+t);splat=0;}   // (even a speck: it joins the stain it lands by)
    if(splat>0) {
        if(where==GLASS)glassBloodLand(pane,lx,ly,lz,x,y,splat,now+t,kind,tint);
        else if(where==WALL) {
            // Glancing drops streak along the wall, the way they came in (falling steeply: down it).
            const line_t &line=lines[hit.line];
            float ex=units(line.v2->x-line.v1->x),ey=units(line.v2->y-line.v1->y),el=std::max(1e-3f,std::hypot(ex,ey));
            float facing=std::abs((vx*ey-vy*ex)/el)/std::max(1e-3f,std::hypot(h,fall));
            float velocity[3]={vx,vy,fall};
            bloodWallSplat(hit,lx,ly,lz,splat,std::clamp(1/std::max(facing,0.35f),1.0f,2.4f),now+t,kind,kind==BLOOD_SPLAT&&splat>=(gathers?12:18),tint,velocity);
            // A fast drop bursts where it hits: a few tiny ones jump back off the wall and fall, a moment later.
            if(h>420&&delay<0.5f)
                for(int n=0,bits=2+(randomRange(0,1)<0.5f);n<bits;++n) {
                    float back=randomRange(40,110),side=randomRange(-60,60);
                    throwDrop(lx-vx/h*1.5f,ly-vy/h*1.5f,lz,-vx/h*back-vy/h*side,-vy/h*back+vx/h*side,randomRange(-20,60),
                              randomRange(0.6f,0.8f),randomRange(0,1)<0.5f?randomRange(3.5f,5.0f):0,BLOOD_SPECK,tint,false,t+delay);
                }
        } else {
            const sector_t *s=where==CEILING?landing:sectorAt(lx,ly);
            size_t index=s-sectors;
            bool liquid=index<sectorMist.size()&&sectorMist[index].liquid&&!waterIceHolds(lx,ly);   // frozen water takes blood as a floor
            if(where==FLOOR&&liquid&&splat>0)bloodLiquidLand(lx,ly,(int)index,splat,now+t,tint);   // a cloud in it, a hiss on lava
            if(where==CEILING||!liquid) {
                // Drops coming in low stretch along their way.
                float stretch=where==CEILING?1:1+std::clamp(h/std::max(60.0f,std::abs(fall))*0.6f,0.0f,1.2f);
                bloodFloorSplat(lx,ly,(int)index,splat,std::atan2(vy,vx),stretch,where==CEILING,now+t,kind,tint);
                if(gathers&&where==FLOOR)bloodPoolFeed(lx,ly,splat,now+t,tint);
                // A big one on the ceiling drips a moment later, once or twice (not on Phobos: nothing pulls).
                if(where==CEILING&&splat>=9&&!Game_Phobos())
                    for(int n=0,drips=1+(randomRange(0,1)<0.4f);n<drips;++n)
                        throwDrop(lx+randomRange(-2,2),ly+randomRange(-2,2),lz-0.5f,0,0,0,randomRange(0.7f,1.0f),randomRange(4,6),
                                  BLOOD_SPECK,tint,false,t+delay+randomRange(0.5f,2.5f));
            }
        }
    }
    if(drops.size()>=maxDrops)drops.erase(drops.begin(),drops.begin()+(drops.size()-maxDrops+1));
    // In the air, the palette's brighter half: the darkened palette (for stains) put the drops in the dark at
    // a tenth of full red, black (the user, Doom 2).
    drops.push_back({x,y,z,vx,vy,vz,now,now+t,size,bloodColor(randomRange(0.55f,0.95f),tint),thrown});
}
// A spray of count drops from (x,y,z) around the heading (dx,dy), spread
// that far across it, speeds and rises in those ranges; the first big ones
// carry splats of the big size range, the rest specks now and then.
struct Burst { int count,big;float spread,speed[2],rise[2],bigSize[2],speckChance; };
static void burst(float x,float y,float z,float dx,float dy,const Burst &b,int tint) {
    for(int i=0;i<b.count;++i) {
        float a=std::atan2(dy,dx)+randomRange(-b.spread,b.spread),speed=randomRange(b.speed[0],b.speed[1]);
        bool big=i<b.big;
        float splat=big?randomRange(b.bigSize[0],b.bigSize[1]):randomRange(0,1)<b.speckChance?randomRange(4.5f,7.5f):0;
        throwDrop(x,y,z,std::cos(a)*speed,std::sin(a)*speed,randomRange(b.rise[0],b.rise[1]),big?1.6f:randomRange(0.8f,1.3f),
                  splat,big?BLOOD_SPLAT:BLOOD_SPECK,tint);
    }
}

// Gore (PLAN-skin-suit.md 10.1): in place of the game's cartoon puff (not drawn with gore, geometry.cpp),
// sprays of fast drops out of the exit and the entry (below), and bits of what the shot cut thrown
// out the far side with the spray (woundLastCut): dark meat, pale bone chips, the skull's, the brain's
// pink, the guts' own colours. They fly as the drops do and land as specks.
static void sprayViscera(const mobj_t &target,float nx,float ny,float nz,float ex,float ey,float ez,float dx,float dy,
                         float slope,float force,int tint) {
    // The blood itself as drops (PLAN-skin-suit.md 10.1; the user: the cloud was a curtain, not physical like
    // the rain and snow). As high-speed footage of shots shows it: out of the exit a narrow cone (its edge some
    // 27 degrees off the shot), fast (to 1500 units a second), most of the drops; back out of the entry toward
    // the shooter a wide one (57 degrees), half as fast, fewer. Fast drops draw as streaks along their way
    // (buildSpray) and burst where they hit (throwDrop): the wall behind gets painted by the drops themselves.
    // In the first tic some leave in threads, a few in a row along one way at falling speeds, so the spray
    // reads as strings rather than confetti.
    auto mist=[&](float x,float y,float z,float radius) {
        if(mists.size()>=64)mists.erase(mists.begin());
        mists.push_back({x,y,z,gameSeconds(),radius,(uint32_t)(randomRange(0,1)*4294967000.0f),tint});
    };
    // How heavy the hit (0 a light bullet, 1 the heaviest pellet): a pistol throws a few drops, a heavy blow
    // many (the user: a pistol's spray was too random). Each hit picks one way its spray leans and one speed,
    // and its drops keep near them, so a hit reads as one gesture, not confetti.
    float heft=std::clamp((force-0.6f)/0.9f,0.0f,1.0f);   // a 10-damage bullet about half
    // A shotgun's pellets on one body in one tic share a splash: the first two throw their whole spray, the
    // rest a third of it and no mist (a full one each turned a hit into a red curtain).
    static const mobj_t *lastTarget=nullptr;static int lastTic=-1,hits=0;
    hits=&target==lastTarget&&leveltime==lastTic?hits+1:1;lastTarget=&target;lastTic=leveltime;
    float share=hits<=2?1.0f:0.3f;
    auto counted=[&](float n){return (int)std::lround(n*share);};
    // Drops leave with half the body's own motion: a running imp's blood goes with it, then on its own.
    float walkX,walkY;bodyWalk(&target,walkX,walkY);
    float cx=0.5f*(units(target.momx)+walkX)*TICRATE,cy=0.5f*(units(target.momy)+walkY)*TICRATE;
    float lean=randomRange(0,1)<0.5f?-1.0f:1.0f,pace=randomRange(0.85f,1.15f);
    auto near=[&](float base,float jitter){return base*pace*randomRange(1-jitter,1+jitter);};
    if(hits<=2) {mist(nx,ny,nz,3.0f+2*force);mist(ex,ey,ez,7.0f+5*force);}   // the exit's puff the bigger (the user)
    float rx=-dy,ry=dx;   // the shot's right
    auto throwAt=[&](float x,float y,float z,float turn,float speed,float rise,float size,float splat,int kind) {
        // Fine drops lose their speed sooner in the air: thrown slower, they fall shorter (no drag in the flight).
        float drag=std::clamp(0.55f+0.35f*size,0.75f,1.0f);
        float wx=dx*std::cos(turn)+rx*std::sin(turn),wy=dy*std::cos(turn)+ry*std::sin(turn);
        throwDrop(x,y,z,wx*speed*drag+cx,wy*speed*drag+cy,rise*drag,size,splat,kind,tint,true);
    };
    // The exit's core, narrow and fast: it paints the wall behind.
    for(int i=0,count=counted(4+8*heft);i<count;++i) {
        float turn=randomRange(-0.4f,0.4f),speed=near(900,0.25f)*std::min(force,1.0f);
        throwAt(ex,ey,ez,turn,speed,(slope+randomRange(-0.15f,0.25f))*speed,i<2?1.4f:randomRange(0.8f,1.2f),
                i<2?randomRange(12,20):randomRange(0,1)<0.7f?randomRange(4.5f,7.5f):0,i<2?BLOOD_SPLAT:BLOOD_SPECK);
    }
    // From the shooter that core flies straight away, hidden behind the body (the user: the exit felt weak):
    // so a fan leaves wide past the body's edge on the side the spray leans (a few the other way), a rooster
    // tail rises over the shoulders and comes down in view, and on a heavier hit a big blob arcs out beside it.
    for(int i=0,count=counted(2+5*heft);i<count;++i) {
        float side=(i%3==2?-lean:lean)*randomRange(0.7f,1.1f),speed=near(340,0.2f);
        throwAt(ex,ey,ez,side,speed,near(0.35f,0.3f)*speed+60,randomRange(0.9f,1.3f),randomRange(0,1)<0.6f?randomRange(4.5f,8.0f):0,BLOOD_SPECK);
    }
    for(int i=0,count=counted(1+3*heft);i<count;++i)
        throwAt(ex,ey,ez+4,lean*randomRange(0.0f,0.4f),near(220,0.2f),near(340,0.15f),randomRange(1.0f,1.3f),
                randomRange(0,1)<0.5f?randomRange(5,8):0,BLOOD_SPECK);
    for(int i=0,count=hits<=2?(heft>0.35f)+(heft>0.8f):0;i<count;++i)
        throwAt(ex,ey,ez+2,lean*randomRange(0.5f,0.9f),near(170,0.2f),near(170,0.2f),2.2f,randomRange(14,22),BLOOD_SPLAT);
    // Threads: a few drops in a row along one way at falling speeds, so the spray reads as strings.
    for(int thread=0,threads=hits<=2?1+(heft>0.5f):0;thread<threads;++thread) {
        float turn=lean*randomRange(0.0f,0.3f),speed=near(800,0.15f)*std::min(force,1.0f),up=randomRange(-0.1f,0.25f);
        for(int k=0;k<4;++k)throwAt(ex,ey,ez,turn,speed*(1-0.16f*k),(slope+up)*speed*(1-0.16f*k),0.9f,k==0?randomRange(6,10):0,BLOOD_SPECK);
    }
    // The entry: back toward the shooter, the side the player sees, so stronger than the footage has it, but
    // slow (the user: it flew too far toward the player): it falls within a couple of metres of the body.
    // In two or three uneven lobes (the main one, a smaller to the other side, sometimes one straight back),
    // each drop near its lobe's way: a fan with a shape, not a ring (the research the user found).
    float lobes[3]={doomPi+lean*randomRange(0.35f,0.75f),doomPi-lean*randomRange(0.2f,0.55f),doomPi+randomRange(-0.15f,0.15f)};
    int lobeCount=2+(randomRange(0,1)<0.5f);
    for(int i=0,count=counted(2+4*heft);i<count;++i) {
        float pick=randomRange(0,1);int lobe=pick<0.55f?0:pick<0.85f||lobeCount==2?1:2;
        float turn=lobes[lobe]+randomRange(-0.15f,0.15f),speed=near(190,0.25f);
        throwAt(nx,ny,nz,turn,speed,near(0.4f,0.4f)*speed+40,i==0?1.4f:randomRange(0.8f,1.2f),
                i==0?randomRange(12,18):randomRange(0,1)<0.7f?randomRange(4.5f,7.5f):0,i==0?BLOOD_SPLAT:BLOOD_SPECK);
    }
    if(heft>0.3f&&hits<=2) {
        float turn=lobes[0]+randomRange(-0.1f,0.1f),speed=near(220,0.15f);
        for(int k=0;k<4;++k)throwAt(nx,ny,nz,turn,speed*(1-0.16f*k),0.3f*speed*(1-0.16f*k),0.9f,k==0?randomRange(6,10):0,BLOOD_SPECK);
    }
    int what=woundLastCut();
    if(what==CUT_NONE)return;
    const byte *palette=playpal();
    auto colourOf=[&](byte index) {return std::array<float,3>{palette[index*3]/255.0f,palette[index*3+1]/255.0f,palette[index*3+2]/255.0f};};
    const GutsPatch &guts=gutsOf(target);
    auto bit=[&](std::array<float,3> colour,float size,float splat) {
        float across=randomRange(-0.35f,0.35f),speed=randomRange(120,240)*force;
        throwDrop(ex,ey,ez,(dx-dy*across)*speed,(dy+dx*across)*speed,randomRange(20,140)+slope*speed,size,splat,BLOOD_SPECK,tint);
        drops.back().color=colour;
    };
    auto meat=[&]{return bloodColor(randomRange(0.2f,0.45f),tint);};
    std::array<float,3> bone=colourOf(guts.bone),pink={0.86f,0.55f,0.58f};
    auto chip=[&] {   // the bone, darker, half of them smeared red
        float k=randomRange(0.6f,0.85f);std::array<float,3> red=bloodColor(0.35f,tint);
        float blood=randomRange(0,1)<0.5f?randomRange(0.3f,0.55f):0.1f;
        return std::array<float,3>{(bone[0]*k)*(1-blood)+red[0]*blood,(bone[1]*k)*(1-blood)+red[1]*blood,(bone[2]*k)*(1-blood)+red[2]*blood};
    };
    // Bone chips: chunky, slow (never fast enough to streak) and bloodied, no sheen. Small, pale and fast they
    // flew as streaks and read as sparks (the user).
    auto shard=[&](float size) {
        float across=randomRange(-0.5f,0.5f),speed=randomRange(60,120);
        throwDrop(ex,ey,ez,(dx-dy*across)*speed,(dy+dx*across)*speed,randomRange(30,90),size,0,BLOOD_SPECK,tint);
        drops.back().color=chip();drops.back().dry=true;
    };
    switch(what) {
    case CUT_FLESH: for(int n=0;n<3;++n)bit(meat(),randomRange(1.2f,1.6f),3.5f);break;
    case CUT_BONE: for(int n=0;n<2;++n)shard(randomRange(1.6f,2.0f));bit(meat(),1.4f,3.5f);break;
    case CUT_SKULL: for(int n=0;n<3;++n)shard(randomRange(1.6f,2.0f));bit(meat(),1.4f,3.5f);break;
    case CUT_BRAIN: for(int n=0;n<3;++n)bit(pink,randomRange(1.2f,1.6f),3);shard(1.7f);break;
    case CUT_GUTS: for(int n=0;n<3;++n)bit(colourOf(gutsColour(guts,n,(int)randomRange(0,16),(uint32_t)randomRange(0,65536))),randomRange(1.4f,1.8f),3.5f);break;
    }
}

// Cut in half (PLAN-skin-suit.md 10.2): the moment it comes apart everything spills at once. A flat ring of
// drops thrown out all round at the cut's height (a band on the walls about), a pour straight down from both
// cut faces into a big pool at once, gut bits dropping out of the top half, and a thin mist at the cut.
void sprayCutSpill(const mobj_t &thing,float x,float y,float z) {
    if(!sprayOn()||!bleeds(thing))return;
    checkSprayLevel();
    int tint=bloodTint(&thing);
    uint32_t seed=(uint32_t)(randomRange(0,1)*4294967000.0f);
    for(int i=0,count=28;i<count;++i) {
        float a=(i+0.6f*hashUnit(i,0,seed))*2*doomPi/count,speed=randomRange(260,560);
        throwDrop(x,y,z,std::cos(a)*speed,std::sin(a)*speed,randomRange(-30,60),i%4?randomRange(0.8f,1.2f):1.4f,
                  i%4?(randomRange(0,1)<0.6f?randomRange(4.5f,7.5f):0):randomRange(10,16),i%4?BLOOD_SPECK:BLOOD_SPLAT,tint,true);
    }
    for(int i=0;i<10;++i)
        throwDrop(x+randomRange(-4,4),y+randomRange(-4,4),z+randomRange(-2,2),randomRange(-30,30),randomRange(-30,30),randomRange(-80,0),
                  randomRange(1.2f,1.8f),i<3?randomRange(14,22):0,i<3?BLOOD_SPLAT:BLOOD_SPECK,tint,true);
    bloodPoolAt(x,y,std::clamp(units(thing.radius)*3.2f,40.0f,96.0f),1.2f,tint);   // a big pool at once, not over seconds
    const GutsPatch &guts=gutsOf(thing);
    const byte *palette=playpal();
    for(int i=0;i<5;++i) {
        float a=randomRange(0,2*doomPi),speed=randomRange(40,140);
        throwDrop(x,y,z+2,std::cos(a)*speed,std::sin(a)*speed,randomRange(40,140),randomRange(1.8f,2.4f),randomRange(5,8),BLOOD_SPECK,tint,true);
        byte g=gutsColour(guts,i,(int)randomRange(0,16),seed^(uint32_t)i);
        drops.back().color={palette[g*3]/255.0f,palette[g*3+1]/255.0f,palette[g*3+2]/255.0f};
    }
    if(mists.size()>=64)mists.erase(mists.begin());
    mists.push_back({x,y,z,gameSeconds(),9.0f,seed,tint});
}

// A burn (plasma, fire: PLAN-skin-suit.md 7): no blood flies, a puff of smoke and a few black flakes of char.
void sprayScorch(float x,float y,float z,float dx,float dy) {
    if(!sprayOn())return;
    checkSprayLevel();
    bloodSteamAt(x,y,z);
    for(int i=0;i<4;++i) {
        float a=std::atan2(-dy,-dx)+randomRange(-1.2f,1.2f),speed=randomRange(40,120);
        throwDrop(x,y,z,std::cos(a)*speed,std::sin(a)*speed,randomRange(20,90),randomRange(0.8f,1.1f),0,BLOOD_SPECK,BLOOD_RED);
        float k=randomRange(0.05f,0.12f);drops.back().color={k,k*0.8f,k*0.7f};
    }
}

// A hit on bare bone (the revenant's arms, shins, skull: wounds.cpp's woundMaterial): chips of it thrown back
// toward the shooter and out the far side, chunky and slow as the bone chips of a wound (never streaking), in
// the bone's own tan, and now and then a drop of blood: there's next to none in it.
void sprayBone(const mobj_t *target,float x,float y,float z,float dx,float dy,float slope,int damage) {
    if(!target||!sprayOn())return;
    checkSprayLevel();
    int tint=bloodTint(target);
    for(int n=0,count=2+std::min(damage,30)/8;n<count;++n) {
        bool back=n%2==0;
        float across=randomRange(-0.7f,0.7f),speed=randomRange(50,130)*(back?1.0f:0.8f),way=back?-1.0f:1.0f;
        throwDrop(x,y,z,(dx*way-dy*across)*speed,(dy*way+dx*across)*speed,randomRange(30,100)+slope*speed,
                  randomRange(1.4f,2.0f),0,BLOOD_SPECK,tint);
        float k=randomRange(0.75f,1.05f);
        drops.back().color={0.56f*k,0.40f*k,0.24f*k};drops.back().dry=true;
    }
    if(randomRange(0,1)<0.35f)
        throwDrop(x,y,z,-dx*randomRange(40,90),-dy*randomRange(40,90),randomRange(20,60),1.0f,randomRange(3,5),BLOOD_SPECK,tint);
}

void sprayHit(const mobj_t *target,float x,float y,float z,float dx,float dy,float slope,int damage) {
    if(!target||!bleeds(*target))return;
    checkSprayLevel();
    Painting painting(*target);
    int tint=bloodTint(target);
    // The game puts the hit 10 units short of the body. The exit spray
    // leaves the far side of it, along the shot, the harder the faster.
    float r=units(target->radius),through=(units(target->x)-x)*dx+(units(target->y)-y)*dy+r*0.6f;
    float ex=x+dx*through,ey=y+dy*through,ez=z+slope*through,force=0.7f+std::min(damage,30)/30.0f;
    int count=std::clamp(1+damage/5,2,7),big=1+(randomRange(0,30)<damage);
    for(int i=0;i<count;++i) {
        float across=randomRange(-0.3f,0.3f),speed=randomRange(110,280)*force;
        float vx=(dx-dy*across)*speed,vy=(dy+dx*across)*speed,vz=randomRange(-30,110)+slope*speed;
        float splat=i<big?randomRange(14,24)*(damage>=15?1.25f:1.0f):randomRange(0,1)<0.6f?randomRange(4.5f,7.5f):0;
        throwDrop(ex,ey,ez,vx,vy,vz,i<big?1.6f:randomRange(0.8f,1.3f),splat,i<big?BLOOD_SPLAT:BLOOD_SPECK,tint);
    }
    // Back spatter at the entry, falling short toward the shooter.
    if(randomRange(0,1)<0.5f) {
        float near=std::max(0.0f,through-2*r);
        burst(x+dx*near,y+dy*near,z,-dx,-dy,{1+(damage>=10),0,0.6f,{30,90},{0,80},{0,0},0.7f},tint);
    }
    if(goreOn())sprayViscera(*target,x+dx*std::max(0.0f,through-2*r),y+dy*std::max(0.0f,through-2*r),z,ex,ey,ez,dx,dy,slope,force,tint);
}
void sprayKill(const mobj_t &thing,float dx,float dy,float speed,float hitZ,bool gib,int overkill) {
    if(!sprayOn()||!bleeds(thing))return;
    checkSprayLevel();
    Painting painting(thing);
    float x=units(thing.x),y=units(thing.y),feet=units(thing.z),tall=std::max(16.0f,units(thing.info->height)),r=units(thing.radius);
    float z=feet+(hitZ>=0?std::clamp(hitZ,tall*0.2f,tall*0.9f):tall*0.6f);
    if(gib) {
        // Burst all around and high, up to a low ceiling.
        burst(x,y,z,1,0,{32,6,doomPi,{60,230},{40,330},{18,30},0.7f},bloodTint(&thing));
        voidBurst(&thing,x,y,z,1,0,doomPi,1,bloodTint(&thing));   // the walls round it, outlines of who stood near
        return;
    }
    float force=std::clamp((speed+overkill/8.0f)/4,0.4f,2.0f);
    int count=std::clamp(6+overkill/3+(int)(speed*2),6,26),big=std::min(5,2+overkill/20);
    bool pushed=speed>=0.6f;
    float ox=x+(pushed?dx*r*0.5f:0),oy=y+(pushed?dy*r*0.5f:0);
    burst(ox,oy,z,dx,dy,{count,big,pushed?0.7f:doomPi,{60*force,200*force},{-20,140*std::max(0.6f,force)},{18,30},0.7f},bloodTint(&thing));
    if(count>=18)voidBurst(&thing,ox,oy,z,dx,dy,pushed?0.7f:doomPi,std::min(1.0f,count/26.0f),bloodTint(&thing));   // big ones only
    if(count>=10)I_MadeSound(IMPACT_GORE_BURST,x,y,z);
}
// How far time is slowed, 0 to 1 at slow motion's quarter speed (slow_motion.c): what is thrown for the eye to
// dwell on is thrown thicker then.
extern "C" double I_TimeScale;
static float slowed() {return std::clamp((float)(1-I_TimeScale)/0.75f,0.0f,1.0f);}
void sprayAirKill(const mobj_t &thing) {
    if(!sprayOn()||!bleeds(thing))return;
    checkSprayLevel();
    Painting painting(thing);
    float x=units(thing.x),y=units(thing.y),feet=units(thing.z),tall=std::max(16.0f,units(thing.info->height)),slow=slowed();
    int count=8+(int)(14*slow),big=1+(int)(2*slow);
    // All round and slow, at three heights of it: it hangs about the body, which flies out of it.
    for(float share:{0.25f,0.55f,0.85f})
        burst(x,y,feet+tall*share,1,0,{count,big,(float)doomPi,{10,80},{0,150},{12,22},0.85f},bloodTint(&thing));
}
void sprayTrail(float x,float y,float z,int tint) {
    if(!sprayOn()||tint==BLOOD_NONE)return;
    checkSprayLevel();
    burst(x,y,z,1,0,{2+(int)(2*slowed()),0,(float)doomPi,{5,40},{-10,60},{10,16},1.0f},tint);
}
// A head blown off (gore.cpp): most of it out along the shot and up, the rest all round, and the gore sound.
void sprayHeadPop(float x,float y,float z,float dx,float dy,int tint,bool sound) {
    if(!sprayOn())return;
    checkSprayLevel();
    burst(x,y,z,dx,dy,{26,6,1.1f,{70,250},{60,320},{14,26},0.8f},tint);
    burst(x,y,z,1,0,{12,2,doomPi,{20,100},{90,240},{10,18},0.6f},tint);
    voidBurst(nullptr,x,y,z,dx,dy,1.1f,1,tint);
    if(sound)I_MadeSound(IMPACT_GORE_BURST,x,y,z);
}
// A stump's beat: a jet out along the unit way (wx,wy,wz), its first drop big while the beat is strong.
static bool onLiquid(float x,float y);
// Gore: blood gushing where a part tore off (the user: "people will expect more ... when you tear off a big
// chunk"), size by how much went (0.5 a torn skull or a chunk shot out, 1 a limb or a head, up to 2 a body):
// a wide spray out along (dx,dy) with big splats, a pour straight down, and a splat on the floor under it.
void sprayGush(float x,float y,float z,float dx,float dy,float size,int tint) {
    if(!sprayOn()||size<=0||tint==BLOOD_NONE)return;
    checkSprayLevel();
    float l=std::hypot(dx,dy);if(l<1e-3f) {dx=1;dy=0;} else {dx/=l;dy/=l;}
    burst(x,y,z,dx,dy,{(int)(10+24*size),(int)(2+5*size),0.9f*doomPi,{40,90+110*size},{30,120+140*size},{14,22+12*size},0.7f},tint);
    burst(x,y,z,dx,dy,{(int)(4+8*size),(int)(1+2*size),doomPi,{5,40},{-60,20},{12,20+8*size},0.8f},tint);
    size_t index=sectorAt(x,y)-sectors;
    if(!onLiquid(x,y))bloodFloorSplat(x,y,(int)index,std::clamp(14+12*size,16.0f,40.0f),randomRange(0,2*doomPi),1,false,gameSeconds(),BLOOD_SPLAT,tint);
}
// A body popped overhead (caco_pop.cpp): its blood goes up, then comes down all round it for a second or two,
// each drop let go from under the ceiling, or high in the open, kept to the room by the walls round the middle.
void sprayRain(const mobj_t &thing,float x,float y,float z,float reach) {
    if(!sprayOn()||!bleeds(thing))return;
    checkSprayLevel();
    Painting painting(thing);
    int tint=bloodTint(&thing);
    burst(x,y,z,1,0,{36,8,doomPi,{80,300},{180,620},{16,26},0.6f},tint);
    voidBurst(&thing,x,y,z,1,0,doomPi,1,tint);
    const sector_t *sector=sectorAt(x,y);
    float top=std::min(ceilZ(sector)-2,z+160);
    for(int i=0;i<110;++i) {
        float a=randomRange(0,2*doomPi),d=reach*std::sqrt(randomRange(0,1)),dx=std::cos(a),dy=std::sin(a);
        WallHit hit;
        if(firstSolidWall(x,y,z,dx,dy,0,d,hit))d=std::max(0.0f,hit.t-3);
        bool big=randomRange(0,1)<0.2f;
        throwDrop(x+dx*d,y+dy*d,top,randomRange(-14,14),randomRange(-14,14),-randomRange(20,70),big?1.6f:randomRange(0.8f,1.2f),
                  big?randomRange(12,20):randomRange(0,1)<0.6f?randomRange(4.5f,8.0f):0,big?BLOOD_SPLAT:BLOOD_SPECK,tint,true,0.25f+1.7f*randomRange(0,1));
    }
}
// The stump's drops leave with its own speed (carry, at most 600 a second): a body flying on throws its
// blood on ahead of it, onto what it flies into, the stream trailing and arcing as it turns.
void sprayStump(float x,float y,float z,float wx,float wy,float wz,float strength,int tint,float stream,const float *carry) {
    if(!sprayOn()||(strength<=0&&stream<=0))return;
    checkSprayLevel();
    float cx=0,cy=0,cz=0;
    if(carry) {
        cx=carry[0];cy=carry[1];cz=carry[2];
        float c=std::sqrt(cx*cx+cy*cy+cz*cz);
        if(c>600) {cx*=600/c;cy*=600/c;cz*=600/c;}
    }
    int count=strength>0?std::max(1,(int)std::lround(6*strength)):0;
    for(int i=0;i<count;++i) {
        float speed=randomRange(60,180)*(0.4f+0.6f*strength);
        float vx=(wx+randomRange(-0.25f,0.25f))*speed,vy=(wy+randomRange(-0.25f,0.25f))*speed,vz=(wz+randomRange(-0.15f,0.15f))*speed+randomRange(0,40);
        bool big=i==0&&strength>0.35f;
        float splat=big?6+12*strength:randomRange(0,1)<0.5f?randomRange(4.5f,7.5f):0;
        throwDrop(x,y,z,vx+cx,vy+cy,vz+cz,big?1.6f:randomRange(0.8f,1.3f),splat,big?BLOOD_SPLAT:BLOOD_SPECK,tint,true);
    }
    // The opening stream: a tight jet, faster than the beats, one to three drops a tic, the first laying a splat;
    // turning with the body it sweeps arcs across the walls.
    int streaming=stream>0?1+(int)std::lround(2*stream):0;
    for(int i=0;i<streaming;++i) {
        float speed=randomRange(150,230)*(0.5f+0.5f*stream);
        float vx=(wx+randomRange(-0.08f,0.08f))*speed,vy=(wy+randomRange(-0.08f,0.08f))*speed,vz=(wz+randomRange(-0.06f,0.06f))*speed+randomRange(0,20);
        bool big=i==0;
        float splat=big?8+8*stream:randomRange(0,1)<0.4f?randomRange(4.5f,7.5f):0;
        throwDrop(x,y,z,vx+cx,vy+cy,vz+cz,big?1.4f:randomRange(0.8f,1.2f),splat,big?BLOOD_SPLAT:BLOOD_SPECK,tint,true);
    }
}
void sprayHurt(const mobj_t &thing,float dx,float dy,float hitZ,int damage) {
    if(!sprayOn()||!bleeds(thing))return;
    checkSprayLevel();
    float tall=std::max(16.0f,units(thing.info->height)),z=units(thing.z)+(hitZ>=0?std::clamp(hitZ,tall*0.2f,tall*0.9f):tall*0.6f);
    float r=units(thing.radius),x=units(thing.x)+dx*r*0.5f,y=units(thing.y)+dy*r*0.5f;
    burst(x,y,z,dx,dy,{std::clamp(2+damage/8,3,8),1,0.8f,{60,180},{-10,120},{14,22},0.6f},bloodTint(&thing));
}
// Gore: a shot through a corpse (body_burst.cpp), at (x,y,z) where its body lies, going (dx,dy).
void sprayCorpseHit(float x,float y,float z,float dx,float dy,int tint) {
    if(!sprayOn()||tint==BLOOD_NONE)return;
    checkSprayLevel();
    burst(x,y,z,dx,dy,{4,1,0.9f,{40,140},{20,150},{14,22},0.6f},tint);
}
static bool onLiquid(float x,float y) {
    size_t index=sectorAt(x,y)-sectors;
    return index<sectorMist.size()&&sectorMist[index].liquid&&!waterIceHolds(x,y);
}
// The slap is heard with or without blood spray (it went quiet with the spray switch).
void sprayLanding(float x,float y,float z,float speed,int tint,bool slap) {
    if(onLiquid(x,y))return;
    if(slap&&speed>1.5f)contactFloor(x,y,z,speed,1);
    if(!sprayOn())return;
    checkSprayLevel();
    size_t index=sectorAt(x,y)-sectors;
    bloodFloorSplat(x,y,(int)index,std::clamp(16+speed*4,18.0f,32.0f),randomRange(0,2*doomPi),1,false,gameSeconds(),BLOOD_SPLAT,tint);
    burst(x,y,z+2,1,0,{std::clamp((int)(2+speed*1.2f),3,8),0,doomPi,{30,110},{60,150},{0,0},0.8f},tint);
}
// A splash on the first wall within reach from (x,y,z) along (dx,dy), a burst back off it, the slap.
static bool wallSlam(float x,float y,float z,float dx,float dy,float reach,float size,int specks,int tint,float speed,float heft) {
    WallHit hit;
    if(!firstSolidWall(x,y,z,dx,dy,0,reach,hit))return false;
    contactWall(hit,x,y,z,speed,heft);
    if(!sprayOn())return true;
    float now=gameSeconds();
    bloodWallSplat(hit,x+dx*hit.t,y+dy*hit.t,z,size,1.3f,now,BLOOD_SPLAT,true,tint);
    burst(x,y,z,-dx,-dy,{specks,0,0.9f,{30,100},{0,90},{0,0},0.7f},tint);
    return true;
}
void sprayWallHit(float x,float y,float z,float dx,float dy,float speed,int tint,float heft) {
    if(sprayOn())checkSprayLevel();
    wallSlam(x,y,z,dx,dy,16,std::clamp(18+speed*4,20.0f,34.0f),std::clamp((int)(2+speed*1.5f),3,8),tint,speed,heft);
}
void spraySmear(float x,float y,float angle,float size,int tint) {
    if(!sprayOn()||onLiquid(x,y))return;
    checkSprayLevel();
    bloodFloorSplat(x,y,(int)(sectorAt(x,y)-sectors),size,angle,1,false,gameSeconds(),BLOOD_SMEAR,tint);
}

// Wounds drip while they are fresh: about one drop a second from a big
// hole, thinning out over twenty seconds, falling with the monster's step.
static void drip(const mobj_t &thing) {
    if(Game_Phobos())return;   // Nothing pulls a drop off a wound there.
    WoundSpot spots[10];
    int count=woundSpots(&thing,spots,10);
    if(!count)return;
    float x=units(thing.x),y=units(thing.y),feet=units(thing.z),r=units(thing.radius);
    float facing=(float)(thing.angle*(2*doomPi/4294967296.0));
    for(int n=0;n<count;++n) {
        const WoundSpot &w=spots[n];
        float age=(leveltime-w.tic)/(float)TICRATE,fresh=std::clamp(1-age/20,0.0f,1.0f);
        if(randomRange(0,1)>=0.02f*w.radius*fresh)continue;
        // The wound faces the view it was shot from; across is to that view's right.
        float view=facing+w.yaw,vx=std::cos(view),vy=std::sin(view);
        float wx=x-vx*r*0.7f+vy*w.across,wy=y-vy*r*0.7f-vx*w.across;
        throwDrop(wx,wy,feet+w.height,units(thing.momx)*TICRATE+randomRange(-6,6),units(thing.momy)*TICRATE+randomRange(-6,6),0,
                  randomRange(0.7f,1.0f),randomRange(0,1)<0.5f?randomRange(4,6):0,BLOOD_SPECK,bloodTint(&thing));
    }
}
// Burns smoke a while: wisps off each burnt wound for five seconds, thinning out, off the living and
// the dead (a ragdoll's from the part of it the wound is on, by its height on the standing body).
static void smoke(const mobj_t &thing) {
    WoundSpot spots[10];
    int count=woundSpots(&thing,spots,10,true);
    float x=units(thing.x),y=units(thing.y),feet=units(thing.z),r=units(thing.radius);
    float facing=(float)(thing.angle*(2*doomPi/4294967296.0)),tall=std::max(16.0f,standingTop(thing));
    for(int n=0;n<count;++n) {
        const WoundSpot &w=spots[n];
        float left=1-(leveltime-w.tic)/(5.0f*TICRATE);
        if(left<=0||randomRange(0,1)>=0.4f*left)continue;
        float wx,wy,wz;
        if(hasRagdoll(&thing)) {
            float f=w.height/tall;
            int point=f>0.8f?P_NECK:f>0.55f?P_SPINE:f>0.35f?(w.across<0?P_HIP_L:P_HIP_R):(w.across<0?P_KNEE_L:P_KNEE_R);
            if(!ragdollPoint(thing,point,wx,wy,wz))continue;
        } else {
            float view=facing+w.yaw,vx=std::cos(view),vy=std::sin(view);
            wx=x-vx*r*0.7f+vy*w.across;wy=y-vy*r*0.7f-vx*w.across;
            // A corpse lies in its last drawing: low, whatever height the wound was at.
            wz=feet+(thing.health>0?w.height:std::min(w.height,6.0f));
        }
        bloodSteamAt(wx+randomRange(-1,1),wy+randomRange(-1,1),wz);
    }
}
void watchSpray() {
    if(!sprayOn()||levelSerial!=r_levelserial) {drops.clear();slides.clear();return;}
    checkSprayLevel();
    for(auto &entry:slides)entry.second.seen=false;
    float now=gameSeconds();
    auto watch=[&](mobj_t *thing) {
        if(!bleeds(*thing))return;
        smoke(*thing);
        if(thing->health>0) {drip(*thing);if(!slides.empty())slides.erase(thing);return;}
        float x=units(thing->x),y=units(thing->y),mx=units(thing->momx),my=units(thing->momy),speed=std::hypot(mx,my);
        auto found=slides.find(thing);
        if(found==slides.end()) {slides[thing]={x,y,speed,speed>0?mx/speed:0,speed>0?my/speed:0,0,false,true};return;}
        Slide &s=found->second;s.seen=true;
        // A ragdoll's body is elsewhere: its own contacts splash and smear (ragdoll.cpp).
        bool doll=hasRagdoll(thing);
        // Thrown hard and stopped short (more than friction does): it met
        // a wall. The wall takes a splash at the body's middle, once.
        if(!doll&&!s.slammed&&s.speed>5&&speed<s.speed*0.5f) {
            float tall=std::max(16.0f,units(thing->info->height)),z=units(thing->z)+tall*0.5f;
            if(wallSlam(s.x,s.y,z,s.dx,s.dy,units(thing->radius)+12,randomRange(24,34),6,bloodTint(thing),s.speed,1))s.slammed=true;
        }
        // Down and sliding: a smear every so far, thick end at the body.
        float moved=std::hypot(x-s.x,y-s.y);
        if(!doll&&!(thing->flags&MF_SOLID)&&thing->z<=thing->floorz&&speed>1.2f) {
            s.since+=moved;
            float size=std::clamp(units(thing->radius)*0.6f,9.0f,15.0f);
            if(s.since>=size*1.4f) {
                s.since=0;
                size_t index=thing->subsector->sector-sectors;
                if(index>=sectorMist.size()||!sectorMist[index].liquid||waterIceHolds(x,y))
                    bloodFloorSplat(x-mx/speed*size*0.6f,y-my/speed*size*0.6f,(int)index,size,std::atan2(my,mx)+doomPi,1,false,now,BLOOD_SMEAR,bloodTint(thing));
            }
        } else s.since=0;
        s.x=x;s.y=y;s.speed=speed;
        if(speed>0.01f) {s.dx=mx/speed;s.dy=my/speed;}
    };
    // The tic's things as beforeTic listed them, else the sectors' lists in the same order.
    if(ticThings)for(mobj_t *thing:*ticThings)watch(thing);
    else for(int i=0;i<numsectors;++i)for(mobj_t *thing=sectors[i].thinglist;thing;thing=thing->snext)watch(thing);
    for(auto it=slides.begin();it!=slides.end();) it=it->second.seen?std::next(it):slides.erase(it);
}
// How a drop in the air is lit: by where it is, a little brighter (a lit drop catches the light: in a dim
// level they went black a few metres out, dark red times dim light), but dark where the room is dark (the
// user: with the lights shot out, a floor of half the light glowed).
// (Never below 0.3: in the dark the spray stays a dim red, not gone; the user.)
static float flyingLight(float x,float y) {return std::min(1.0f,std::max(0.3f,0.06f+1.3f*lighting(sectorAt(x,y))));}
// A flying drop's colour in the light round it (impacts.cpp's particleColour), wet.
static std::array<float,3> flyingColour(const std::array<float,3> &albedo,float x,float y,float z) {return particleColour(albedo,x,y,z,true);}
// How much a drop at (x, y, z) catches a strong light: the flashlight's beam, a bright room's lamps; and that
// light's colour. A drop's head glints in it (a pixel toward the light's colour), so a spray sparkles in
// the beam and in front of a lamp.
static float glintAt(float x,float y,float z,std::array<float,3> &colour) {
    float beam=beamAt(x,y,z,&colour);
    float room=std::max(0.0f,lighting(sectorAt(x,y))-0.7f)*2.5f;
    if(room>beam) {colour={1,0.95f,0.9f};return std::min(0.6f,room);}
    return std::min(0.85f,beam*1.4f);
}

void buildSpray(const Uniforms &camera) {
    checkSprayLevel();
    float now=gameSeconds();
    // The mists: single-pixel specks, thin (a third of a speck per square unit of its middle), out to their
    // size in three tics, sinking, gone by the twelfth; each speck's place and when it goes its own hash.
    constexpr float mistFor=12.0f/TICRATE;
    mists.erase(std::remove_if(mists.begin(),mists.end(),[&](const Mist &m){return now-m.born>=mistFor||now<m.born-0.1f;}),mists.end());
    for(const Mist &m:mists) {
        float age=std::floor((now-m.born)*TICRATE)/TICRATE,life=age/mistFor,burst=std::min(1.0f,age*TICRATE/3);
        float spread=m.radius*(0.3f+0.7f*burst*(2-burst))*(1+0.3f*life),sink=0.5f*60.0f*age*age;
        int specks=std::clamp((int)(m.radius*m.radius*1.1f),10,90);
        std::array<float,3> colour=bloodColor(0.5f,m.tint);
        float light=flyingLight(m.x,m.y);
        std::array<float,3> lit;for(int c=0;c<3;++c)lit[c]=std::min(1.0f,colour[c]*light);
        for(int n=0;n<specks;++n) {
            if(hashUnit(n,4,m.seed)<life)continue;   // thinning out
            float a=hashUnit(n,0,m.seed)*2*doomPi,b=(hashUnit(n,1,m.seed)-0.5f)*doomPi*0.8f,d=spread*std::cbrt(hashUnit(n,2,m.seed));
            // A mist is see-through: coarser specks, half their pixels, thinning to a quarter as it spreads.
            pixelBlock(particleVertices,camera,m.x+std::cos(a)*std::cos(b)*d,m.y+std::sin(a)*std::cos(b)*d,
                       m.z+std::sin(b)*d-sink*(0.5f+hashUnit(n,5,m.seed)),1.2f,1,lit,false,&colour,true,nullptr,1,0.5f-0.25f*life,farLeast);
        }
    }
    drops.erase(std::remove_if(drops.begin(),drops.end(),[&](const Drop &d){return now>=d.end||now<d.thrown-0.1f;}),drops.end());
    for(const Drop &d:drops) {
        if(now<d.born)continue;   // a drip not let go yet
        float t=std::clamp(std::floor((now-d.born)*TICRATE)/TICRATE,0.0f,d.end-d.born);
        float x=d.x+d.vx*t,y=d.y+d.vy*t,z=d.z+d.vz*t-0.5f*sprayGravity()*t*t;
        std::array<float,3> lit=flyingColour(d.color,x,y,z);
        // Sheen: in a strong light (the flashlight's beam, a bright room) the whole drop goes a lighter red, steady.
        {
            std::array<float,3> light;float glint=d.dry?0:glintAt(x,y,z,light);
            if(glint>0.05f) {
                std::array<float,3> light2=d.color;
                float top=std::max({light2[0],light2[1],light2[2],1e-3f});
                for(int c=0;c<3;++c)lit[c]=std::min(1.0f,lit[c]+(light2[c]/top*0.75f-lit[c])*glint*0.6f);
            }
        }
        // A fast drop draws as a streak of whole Doom pixels trailing back along its way, as the rain does: as
        // far as it went in the last half tic (to 20 units), its tail a little thinner. Slowing, it shrinks
        // back to a dot.
        float vx=d.vx,vy=d.vy,vz=d.vz-sprayGravity()*t,speed=std::sqrt(vx*vx+vy*vy+vz*vz);
        float trail=std::min(20.0f,speed*0.5f/TICRATE);
        if(trail>d.size*1.5f) {
            int pieces=std::min(16,(int)std::ceil(trail/(d.size*0.7f)));   // overlapping: a solid streak, not dots
            // Its head solid, its tail thinning out to nothing (blood is thin there: see-through, stippled),
            // single pixels of the far tail dropped by the drop's own hash so they don't blink.
            uint32_t own=hash32((uint32_t)std::lround(d.thrown*1000)^(uint32_t)std::lround(d.x*8)*73856093u);
            for(int k=0;k<pieces;++k) {
                float back=trail*k/pieces,thin=1-0.75f*k/std::max(1,pieces-1);
                if(thin<0.5f&&hashUnit(k,0,own)>thin*1.6f)continue;
                pixelBlock(particleVertices,camera,x-vx/speed*back,y-vy/speed*back,z-vz/speed*back,k?d.size*0.8f:d.size,1,lit,false,&d.color,
                           true,nullptr,1,k?thin:1.0f,farLeast);
            }
        } else pixelBlock(particleVertices,camera,x,y,z,d.size,1,lit,false,&d.color,true,nullptr,1,d.size<1.0f?0.6f:1.0f,farLeast);   // fine drops thin
    }
}
void sprayShutdown() {drops.clear();slides.clear();mists.clear();}
} // namespace doom3d
