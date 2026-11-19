/* A ball of flesh dying as a soft body: the cacodemon's doll, the Pain Elemental's a stiffer one (a rocket's direct
   hit bursts them instead, and -goreballgib every kill: goreBurst).
   Rigid, a ball can only turn, and a slow turn shows what was never drawn: a true 3D ball. So this one doesn't, shot
   dead: its relief (ragdoll_body.cpp) hangs on a shell of points, Verlet in game tics, each pulled back to its home
   round the shell's middle, so the blob keeps facing as it died and only its shape moves. A blast is another matter
   (the user: let it fly however it wants, the resolution masks the missing art): it sets it tumbling, fast.
   Thrown with the blow, its near side first (a dent that comes back); landing, the points that reach the floor stop
   and the rest come down on them, a slap, a blue splash and a wobble; the floor under each point is its own, so it
   slumps over steps. The walls stop the body, not its points (points stopped one by one, a skin went either side of
   a column and tore): its middle slides along them as a circle, the whole of it pressed flat a moment. From its death
   it deflates (the user): the air going fast and then trickling, its home flattening and spreading at the bottom,
   the pull going slack, and soft on the floor each part of it lies on the ground under it, over step edges and down
   risers (the user: like Dali's melted watches). No point strays further than ballStretch from its home. Drawn as any
   doll is: the same virtual camera a pose at a time, baked into a corpse sprite at rest (ragdollCard), the same
   wounds, light and shadows. An explosion throws it as a blast throws a ragdoll (blastCarry, blastThrow, blastLift,
   back off walls by blastRebound until it lands) but further, it weighs little, an empty skin least, killing it or
   catching it still moving, weighty in the air (the user: an inflated football, not a balloon: it keeps its speed and
   its shape, and only goes flat where it lands), lying down on whichever side it lands. At rest it's a corpse sprite like any other, its motion freed, and what goes off near it then bursts
   it (goreExplosion), as it does all of them. Its corpse is moved to where it came to rest (unlinked and linked again, so its sector
   and blockmap follow): an arch-vile raises it there, a save keeps it there. */
#include "ragdoll_doll.h"
extern "C" {
void P_UnsetThingPosition(mobj_t *thing);
void P_SetThingPosition(mobj_t *thing);
}

namespace doom3d {
// Per tic: the ragdolls' gravity; the speed a point keeps in the air, and over the floor it touches; the most it steps
// up, past that a riser that stops it; falls harder than this slap and splash; into a wall harder than this splash it.
constexpr float ballGravity=dollGravity,ballKeep=0.97f,ballGrip=0.75f,ballStep=24,ballSlapFall=1.5f,ballWallSpeed=2.5f;
// The share of the way home a point goes a tic, full of air and empty; how much more of the blow its near side takes.
constexpr float ballFirm=0.16f,ballSlack=0.05f,ballDent=0.8f;
// The furthest a point strays from its home, in radii: past it the skin read as stretched like gum.
constexpr float ballStretch=0.3f;
// Into a wall: the speed that presses it as flat as it goes, how much of its depth along the wall's way that takes,
// how far it spreads over the wall, and the share of the press it keeps a tic (fast hit, slow recovery).
// The walls keep its middle this far off, in radii, less by ballWallGive pressed as flat as it goes.
constexpr float ballBodyWide=0.8f,ballWallGive=0.6f;
constexpr float ballWallPress=8,ballWallFlat=0.7f,ballWallSpread=0.45f,ballPressKeep=0.94f;
// Thrown by a blast, a football: the speed a point keeps a tic (less, and it floated like a balloon), its gravity and
// the pull home, and the share of its flatness it shows in the air. And what emptying takes off the speed otherwise:
// a slack skin has no spring in it (without, it lay flat and swelled up again a second later).
constexpr float ballFlyKeep=0.985f,ballFlyGravity=0.65f,ballFlyFirm=0.25f,ballFlyFlat=0.35f,ballEmptyDamp=0.12f;
// Lying soft on the floor: how far below the floor under its middle a part of it hangs, in radii, past that it's
// held up by the rest of it.
constexpr float ballDrape=1.5f;
// Deflating: the tics it takes from its death; the height it loses, and how far its bottom spreads over the floor.
constexpr int ballDeflateTics=40;
constexpr float ballFlat=0.62f,ballSpread=0.3f;
// The share of its points on the floor that carries all of it: the pull home nets to nothing, so with gravity left
// on what the floor holds up, the few points touching bore the whole body and it melted into a line.
constexpr float ballCarried=0.125f;
// Thrown by a blast: the ragdolls' throw times ballLight, more by ballEmptyThrow empty (at the ragdolls' throw it only
// skimmed the floor); the share of its fall it bounces with landing from one; the most its way strays from the
// blast's (radians); its spin, radians a tic, the least and what's added at most.
constexpr float ballLight=1.5f,ballEmptyThrow=0.4f,ballHop=0.55f,ballStray=0.5f,ballSpin=0.1f,ballSpinMore=0.12f;
// Tumbling, on the floor: the spin a tic keeps, the turn laying it flat a tic (empty), and the share of a roll it takes
// from sliding (full).
constexpr float ballScrub=0.8f,ballLayFlat=0.12f,ballRolls=0.3f;
// After a slap its poses come at the hit's pace this long: a wobble this quick falls between the landed ones.
constexpr int ballWobbleTics=12;
// Fat, not air (the Mancubus, -gorefatball): a blast's throw divided by this, its hops off the floor too.
constexpr float fatHeavy=2.5f;

// Its turn as a unit quaternion (w, x, y, z): the shell at rest to the world.
struct Q { float w=1,x=0,y=0,z=0; };
static Q operator*(Q a,Q b) {return {a.w*b.w-a.x*b.x-a.y*b.y-a.z*b.z,a.w*b.x+a.x*b.w+a.y*b.z-a.z*b.y,
                                     a.w*b.y-a.x*b.z+a.y*b.w+a.z*b.x,a.w*b.z+a.x*b.y-a.y*b.x+a.z*b.w};}
static V3 rotate(Q q,V3 v) {V3 u{q.x,q.y,q.z},t=cross(u,v)*2;return v+t*q.w+cross(u,t);}
// Turned by a turn of axis times angle (radians) in the world, before the turn it has.
static Q turned(Q q,V3 by) {
    float angle=length(by);
    if(angle<1e-6f)return q;
    float s=std::sin(angle/2)/angle;
    Q d{std::cos(angle/2),by.x*s,by.y*s,by.z*s},r=d*q;
    float n=std::sqrt(r.w*r.w+r.x*r.x+r.y*r.y+r.z*r.z);
    return {r.w/n,r.x/n,r.y/n,r.z/n};
}

struct BallMotion {
    std::vector<V3> at,was,home;           // each point in the world, a tic ago, and from the middle at rest
    V3 middle;                             // the points' middle
    float radius=16,deflate=0,onFloor=0;   // the round body's; how far its air has gone (0 to 1); its share on the floor
    float stiff=1;                         // of the cacodemon's: pulls home harder, strays and flattens less
    float heavy=1;                         // fat, not air (the Mancubus): a blast throws it this much less far, it bounces less
    int touchedAt=-1,slapAt=-1,offFloor=0; // the tics it first touched the floor and last slapped it; tics since it touched
    int tics=0,still=0;bool resting=false;
    bool flying=false;                     // thrown by a blast, until it lands: comes back off walls
    Q turn;V3 spin;bool tumbled=false;     // its turn, its spin (radians a tic about the world's axes), a blast turned it
    V3 pressWay;float press=0;             // pressed against a wall: the wall's way out, and how hard (0 to 1)
    float worst=0;                         // the furthest a point has strayed from home, in radii (the test log)
    int tint=BLOOD_RED;
};

static const sector_t *sectorOf(float x,float y) {return R_PointInSubsector((fixed_t)(x*FRACUNIT),(fixed_t)(y*FRACUNIT))->sector;}

// World to the doll's own space (x mirrored with its drawing, z over the floor it died on) and back, for ways.
static V3 toDoll(const Ragdoll &doll,V3 w) {
    float side=doll.mirror?-1.0f:1.0f;
    return {side*(w.x*doll.ax[0]+w.y*doll.ax[1]),w.x*doll.ay[0]+w.y*doll.ay[1],w.z};
}
static V3 toWorld(const Ragdoll &doll,V3 d) {
    float side=doll.mirror?-1.0f:1.0f;
    return {doll.ax[0]*side*d.x+doll.ay[0]*d.y,doll.ax[1]*side*d.x+doll.ay[1]*d.y,d.z};
}

// The doll where the ball has it: its skeleton carried along with the middle, its shell's points as they lie.
static void poseDoll(Ragdoll &doll,const BallMotion &b) {
    const Relief &body=*doll.body;
    V3 origin{doll.ox,doll.oy,doll.gz},at=toDoll(doll,b.middle-origin),shift=at-body.ballMiddle;
    for(int n=0;n<POSE_POINTS;++n)doll.p[n]=body.rest[n]+shift;
    doll.centre[0]=at.x;doll.centre[1]=at.y;
    for(size_t i=0;i<b.at.size();++i)doll.shell[i]=toDoll(doll,b.at[i]-origin)-body.shell[i];
    static const V3 axes[3]={{1,0,0},{0,1,0},{0,0,1}};
    for(int k=0;k<3;++k)doll.shellTurn[k]=toDoll(doll,rotate(b.turn,toWorld(doll,axes[k])));
}

// A blast's throw (way: from it, carry: what it carries along, as a ragdoll's slide, strength: 0 to 1 by how near it
// went off), and the tumbling it starts, over the way it goes and any other way besides: each its own.
static V3 blastThrown(BallMotion &b,V3 way,float carry,float strength,uint32_t &seed) {
    auto rnd=[&] {seed=hash32(seed+0x9e3779b9u);return (float)(seed>>8)/16777216.0f;};
    float stray=(rnd()-0.5f)*2*ballStray,c=std::cos(stray),s=std::sin(stray),light=(ballLight+ballEmptyThrow*b.deflate)/b.heavy;
    V3 goes{way.x*c-way.y*s,way.y*c+way.x*s,0};
    V3 over=unit(cross(V3{0,0,1},goes)+V3{rnd()-0.5f,rnd()-0.5f,rnd()-0.5f});
    b.spin=b.spin+over*((ballSpin+ballSpinMore*rnd())*strength);
    b.tumbled=true;b.flying=true;
    return (goes*(carry+blastThrow*(0.75f+0.5f*rnd()))+V3{0,0,blastLift*(0.7f+0.6f*rnd())*strength})*light;
}

bool startBall(const mobj_t &thing,float dx,float dy,float speed,bool blast) {
    if(!thing.info||!ballStiffness(thing.type)||!goreOn()||!softBalls())return false;
    Ragdoll *made=addBallDoll(thing);
    if(!made)return false;
    Ragdoll &doll=*made;
    const Relief &body=*doll.body;
    auto ball=std::make_shared<BallMotion>();BallMotion &b=*ball;
    b.radius=std::max(8.0f,body.ballRadius);
    b.stiff=ballStiffness(thing.type)/100.0f;
    if(Game_BodyFat(thing.type))b.heavy=fatHeavy;
    b.tint=bloodTint(&thing);
    uint32_t seed=doll.goreSeed;
    auto rnd=[&] {seed=hash32(seed+0x9e3779b9u);return (float)(seed>>8)/16777216.0f;};
    // The game shoves its 400 of mass under a unit a tic for a shotgun's kill: the ball takes more of the blow
    // (Hollywood's, as the ragdolls do), a rocket's the most; up a little, the harder the higher.
    float l=std::hypot(dx,dy),ux=l>1e-3f?dx/l:doll.ay[0],uy=l>1e-3f?dy/l:doll.ay[1];
    float push=std::min(10.0f,0.3f+speed*3.2f),turn=(rnd()-0.5f)*0.5f,c=std::cos(turn),s=std::sin(turn);
    V3 way{ux*c-uy*s,uy*c+ux*s,0},thrown=way*push+V3{0,0,1.0f+1.5f*rnd()+std::min(3.0f,speed*0.3f)};
    // A blast's kill as a ragdoll's (startRagdoll): its slide up to blastCarry, and the throw on top, each its own.
    if(blast)thrown=blastThrown(b,way,std::min(push,blastCarry),1,seed);
    // Where it hangs as it died: over the floor by its own height, the near side thrown the harder.
    V3 origin{doll.ox,doll.oy,doll.gz},lift{0,0,units(thing.z)-doll.gz};
    size_t count=body.shell.size();
    b.at.resize(count);b.was.resize(count);b.home.resize(count);
    doll.shell.resize(count);
    for(size_t i=0;i<count;++i) {
        b.at[i]=origin+toWorld(doll,body.shell[i]+lift);
        b.home[i]=toWorld(doll,body.shell[i]-body.ballMiddle);
        float near=std::max(0.0f,-dot(unit(b.home[i]),way));
        b.was[i]=b.at[i]-thrown-way*(push*ballDent*near);
        b.middle=b.middle+b.at[i]*(1.0f/count);
    }
    poseDoll(doll,b);
    doll.shown=doll.shownWas=doll.shownTic=doll.p;doll.shellShown=doll.shell;
    std::copy(doll.centre,doll.centre+2,doll.centreWas);std::copy(doll.centre,doll.centre+2,doll.centreShown);
    doll.ball=std::move(ball);
    if(M_CheckParm((char*)"-ragdolllog"))fprintf(stderr,"ball start speed %.2f push %.2f radius %.1f mirror %d\n",speed,push,b.radius,(int)doll.mirror);
    return true;
}

// Whether a line stops a body this high (bottom to middle): one-sided, a column closed to nothing, a step too high to
// go up, an opening its middle doesn't fit under.
static bool stopsBody(const line_t &line,float bottom,float middle) {
    if(!line.backsector)return true;
    float floor=std::max(floorZ(line.frontsector),floorZ(line.backsector)),ceiling=std::min(ceilZ(line.frontsector),ceilZ(line.backsector));
    bool sky=line.frontsector->ceilingpic==skyflatnum&&line.backsector->ceilingpic==skyflatnum;
    return ceiling<=floor||floor>bottom+ballStep||(ceiling<middle&&!sky);
}
// The middle from where it is to where it's going, a circle this wide against the lines round it, in steps shorter
// than itself (it doesn't go through them at a rocket's speed). out: the way off the wall it was pushed hardest by.
static V3 againstWalls(V3 from,V3 to,float reach,float bottom,V3 &out) {
    V3 way{to.x-from.x,to.y-from.y,0},at=from;
    int steps=std::max(1,(int)std::ceil(length(way)/(reach*0.5f)));
    float deepest=0;
    for(int step=0;step<steps;++step) {
        at=at+way*(1.0f/steps);
        for(int pass=0;pass<2;++pass)for(int n=0;n<numlines;++n) {
            const line_t &line=lines[n];
            if(at.x+reach<units(line.bbox[BOXLEFT])||at.x-reach>units(line.bbox[BOXRIGHT])||
               at.y+reach<units(line.bbox[BOXBOTTOM])||at.y-reach>units(line.bbox[BOXTOP])||!stopsBody(line,bottom,to.z))continue;
            float ax=units(line.v1->x),ay=units(line.v1->y),ex=units(line.v2->x)-ax,ey=units(line.v2->y)-ay;
            float t=std::clamp(((at.x-ax)*ex+(at.y-ay)*ey)/std::max(1e-6f,ex*ex+ey*ey),0.0f,1.0f);
            float dx=at.x-(ax+ex*t),dy=at.y-(ay+ey*t),gap=std::hypot(dx,dy);
            if(gap>=reach||gap<1e-3f)continue;
            V3 off{dx/gap,dy/gap,0};
            at=at+off*(reach-gap);
            if(reach-gap>deepest) {deepest=reach-gap;out=off;}
        }
    }
    at.z=to.z;
    return at;
}

// A tic of it: the points fall and go home, the walls, risers and floors stop them. True on a slap into the floor.
static bool step(BallMotion &b,const mobj_t &thing) {
    float firm=(b.flying?ballFlyFirm:ballFirm+(ballSlack-ballFirm)*b.deflate)*b.stiff,fastest=0,hardest=0,wallSpeed=0,stray=ballStretch*b.radius/b.stiff;
    b.press*=ballPressKeep;
    float flat=b.deflate*(b.flying?ballFlyFlat:1)/b.stiff;
    float wide=ballSpread*flat,tall=1-ballFlat*flat,pull=(b.flying?ballFlyGravity:ballGravity)*(1-std::min(1.0f,b.onFloor/ballCarried));
    float melt=b.flying||b.onFloor<=0?0:b.deflate*b.deflate;
    V3 wallAt,wallWay,wallOut,middle,wasMiddle=b.middle;
    bool touched=false;int touching=0;
    size_t count=b.at.size();
    // The turn this tic, carrying the points round the middle (their speed kept: pulled round by their homes alone,
    // a slack skin lagged behind its own spin and shrank).
    if(b.tumbled) {
        Q by=turned(Q{},b.spin);
        b.turn=turned(b.turn,b.spin);
        for(size_t i=0;i<count;++i) {
            V3 speed=b.at[i]-b.was[i];
            b.at[i]=b.middle+rotate(by,b.at[i]-b.middle);
            b.was[i]=b.at[i]-speed;
        }
    }
    // Where it's all going first: pulled home round the middle as it was, every point was dragged back by the
    // whole body's speed, and a stray limit on that held it to a crawl.
    std::vector<V3> next(count);
    V3 going;
    float keep=b.flying?ballFlyKeep:ballKeep-ballEmptyDamp*b.deflate;
    for(size_t i=0;i<count;++i) {
        next[i]=b.at[i]+(b.at[i]-b.was[i])*keep;next[i].z-=pull;
        going=going+next[i]*(1.0f/count);
    }
    // The walls: on the whole of it. All its points carried off them as one, then its speed into them taken out
    // (and thrown by a blast, some given back).
    V3 speed=going-b.middle,stopped=againstWalls(b.middle,going,b.radius*ballBodyWide*(1-ballWallGive*b.press),going.z-b.radius*tall,wallOut);
    float into=std::max(0.0f,-dot(speed,wallOut));
    V3 shift=stopped-going,bounce=wallOut*(into*(1+(b.flying?blastRebound:0.0f)));
    for(size_t i=0;i<count;++i) {next[i]=next[i]+shift;b.at[i]=b.at[i]+shift-bounce;}
    going=stopped;
    if(into>0) {wallSpeed=into;wallAt=stopped-wallOut*b.radius;wallWay=wallOut*-1;b.spin=b.spin*ballScrub;}
    const sector_t *body=sectorOf(going.x,going.y);
    for(size_t i=0;i<count;++i) {
        V3 was=b.at[i],now=next[i];
        // Home: where it lies from the middle, flatter and wider at the bottom as the air goes.
        V3 h=b.home[i];
        float low=1-std::clamp((h.z+b.radius)/(2*b.radius),0.0f,1.0f);
        V3 home=rotate(b.turn,V3{h.x*(1+wide*low),h.y*(1+wide*low),h.z*tall});
        if(b.press>0.01f) {
            V3 deep=b.pressWay*dot(home,b.pressWay);
            home=home-deep*(ballWallFlat*b.press)+(home-deep)*(ballWallSpread*b.press);
        }
        V3 goal=going+home;
        // Soft on the floor: over its own bit of ground, no longer round its middle.
        if(melt>0) {
            float ground=floorZ(sectorOf(goal.x,goal.y)),level=floorZ(body);
            if(ground>level+ballStep)ground=level;
            ground=std::max(ground,level-ballDrape*b.radius);
            goal.z+=(ground+b.radius*tall+home.z-goal.z)*melt;
        }
        now=now+(goal-now)*firm;
        V3 off=now-goal;
        if(length(off)>stray)now=goal+unit(off)*stray;
        // Its own floor, while that's no higher than the body: a point over a ledge or a column the body keeps off
        // goes by the floor under the body, or the skin snagged on it. On the floor it stops falling and drags.
        const sector_t *under=sectorOf(now.x,now.y);
        float floor=floorZ(under),ceiling=ceilZ(under);
        if(floor>going.z+ballStep||ceiling<going.z) {under=body;floor=floorZ(under);ceiling=ceilZ(under);}
        if(now.z>ceiling&&under->ceilingpic!=skyflatnum)now.z=ceiling;
        V3 before=was;
        if(now.z<=floor) {
            hardest=std::max(hardest,was.z-now.z);
            touched=true;++touching;
            now.z=floor;before.z=floor;
            before.x=now.x-(now.x-before.x)*ballGrip;before.y=now.y-(now.y-before.y)*ballGrip;
        }
        fastest=std::max(fastest,length(now-was));
        b.worst=std::max(b.worst,length(now-goal)/b.radius);
        b.was[i]=before;b.at[i]=now;
        middle=middle+now*(1.0f/count);
    }
    b.middle=middle;b.onFloor=touching/(float)count;
    // On the floor it stops tumbling: a full ball rolls with its slide, an empty skin lies down on whichever side's nearer.
    if(b.tumbled&&touched) {
        V3 top=rotate(b.turn,V3{0,0,1}),flat{0,0,top.z<0?-1.0f:1.0f},slide=middle-wasMiddle;slide.z=0;
        b.spin=b.spin*ballScrub+cross(top,flat)*(ballLayFlat*b.deflate)+cross(V3{0,0,1},slide)*(ballRolls*(1-b.deflate)/b.radius);
    }
    if(wallSpeed>0.5f) {
        float hard=std::min(1.0f,wallSpeed/ballWallPress);
        if(hard>b.press) {b.press=hard;b.pressWay=wallOut;}
    }
    if(wallSpeed>ballWallSpeed) {
        b.slapAt=b.tics;
        sprayWallHit(wallAt.x,wallAt.y,wallAt.z,wallWay.x,wallWay.y,wallSpeed,b.tint);
        goreSound(GORE_SOUND_SLAP,wallAt.x,wallAt.y,wallAt.z);
    }
    bool slapped=false;
    if(touched) {
        bool thrown=b.flying;
        b.flying=false;
        bool first=b.touchedAt<0;
        if(first)b.touchedAt=b.tics;
        if(hardest>ballSlapFall&&(first||b.offFloor>=3)) {
            slapped=true;b.slapAt=b.tics;
            float floor=floorZ(sectorOf(b.middle.x,b.middle.y));
            if(!first||!bodySplash(b.middle.x,b.middle.y,hardest,(float)thing.info->mass,units(thing.radius),0,0)) {
                goreSound(first?GORE_SOUND_SLAP:GORE_SOUND_THUD,b.middle.x,b.middle.y,floor);
                if(bleedsBlood(thing))sprayLanding(b.middle.x,b.middle.y,floor,hardest,b.tint,false);
            }
            // Off the floor again, all of it, until the bounces are too small to slap.
            if(thrown) {
                b.flying=true;b.onFloor=0;
                for(V3 &was:b.was)was.z-=hardest*ballHop/b.heavy;
            }
        }
        b.offFloor=0;
    } else ++b.offFloor;
    {float left=1-std::min(1.0f,b.tics/(float)ballDeflateTics);b.deflate=1-left*left;}
    ++b.tics;
    b.still=touched&&fastest<0.12f?b.still+1:0;
    if((b.still>=4&&b.deflate>=1)||b.tics>=TICRATE*8)b.resting=true;
    return slapped;
}

// Come to rest: its corpse moved to where the ball lies, on the floor there, still (the doll goes on drawing it).
static void handOver(BallMotion &b,const mobj_t &thing) {
    if(M_CheckParm((char*)"-ragdolllog"))fprintf(stderr,"ball rest tic %d at %.1f %.1f, corpse was %.1f %.1f, stray %.2f\n",b.tics,b.middle.x,b.middle.y,units(thing.x),units(thing.y),b.worst);
    mobj_t *corpse=const_cast<mobj_t*>(&thing);
    P_UnsetThingPosition(corpse);
    corpse->x=(fixed_t)(b.middle.x*FRACUNIT);corpse->y=(fixed_t)(b.middle.y*FRACUNIT);
    P_SetThingPosition(corpse);
    corpse->floorz=corpse->subsector->sector->floorheight;corpse->ceilingz=corpse->subsector->sector->ceilingheight;
    corpse->z=corpse->floorz;corpse->momx=corpse->momy=corpse->momz=0;
}

// An explosion at (sx, sy) catching it still moving, its blast this strong there: thrown as a blast throws a ragdoll,
// on top of what it's doing, the points nearest it the hardest.
void ballBlast(Ragdoll &doll,float sx,float sy,float hurt) {
    BallMotion &b=*doll.ball;
    float dx=b.middle.x-sx,dy=b.middle.y-sy,l=std::hypot(dx,dy);
    V3 way=l>1e-3f?V3{dx/l,dy/l,0}:V3{doll.ay[0],doll.ay[1],0};
    uint32_t seed=hash32(doll.goreSeed^(uint32_t)leveltime*40503u);
    float carry=std::min(blastCarry,hurt/16);
    V3 thrown=blastThrown(b,way,carry,std::clamp(hurt/128,0.4f,1.0f),seed);
    for(size_t i=0;i<b.at.size();++i) {
        float near=std::max(0.0f,-dot(unit(b.home[i]),way));
        b.was[i]=b.was[i]-thrown-way*(carry*ballDent*near);
    }
    if(M_CheckParm((char*)"-ragdolllog"))fprintf(stderr,"ball blast hurt %.0f at tic %d\n",hurt,b.tics);
    b.still=0;b.offFloor=0;b.onFloor=0;doll.landed=false;
}

// In place of the fall (tickDoll): the ball's tics, the doll posed from them, drawn as any doll is, at the same
// pace, and always at the slap and through the wobble that follows.
void ballTic(Ragdoll &doll,const mobj_t &thing,int target) {
    BallMotion &b=*doll.ball;
    while(doll.tics<target&&!doll.resting) {
        std::copy(doll.centre,doll.centre+2,doll.centreWas);
        bool slapped=step(b,thing);
        poseDoll(doll,b);
        ++doll.tics;
        doll.landed=b.touchedAt>=0&&!b.flying;
        bool wobbling=b.slapAt>=0&&b.tics-b.slapAt<ballWobbleTics&&doll.tics-doll.lastPose>=earlyPose;
        if(b.resting) {
            doll.resting=true;
            doll.restSector=sectorOf(b.middle.x,b.middle.y);doll.restFloor=units(doll.restSector->floorheight);
            std::copy(doll.centre,doll.centre+2,doll.centreWas);
            doll.nearWalls.clear();doll.nearWalls.shrink_to_fit();doll.floors.cells.clear();doll.floors.cells.shrink_to_fit();
            doll.walls.clear();doll.walls.shrink_to_fit();
        }
        doll.showPose(slapped||wobbling);
    }
    // At rest: its corpse where it lies, and a sprite from here on, its motion freed (what's shown stays, for the
    // picture to be drawn again with new holes in it).
    if(doll.resting) {
        handOver(b,thing);
        doll.ball.reset();doll.shell.clear();doll.shell.shrink_to_fit();
    }
}
} // namespace doom3d
