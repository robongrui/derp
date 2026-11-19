/* What a falling body does where the world gives it something to do, each tic before the doll moves
   (ragdoll.cpp tickDoll): skidding on ice, rolling down stairs, going over a low wall; and two of
   Hollywood's: the twist of a hit in the dance, the legs pedalling on a long drop. */
#include "ragdoll_doll.h"

namespace doom3d {
static const V3 upWay{0,0,1};

static V3 meanSpeed(const Ragdoll &doll) {   // the body's speed, units a tic
    V3 v{};
    for(int n=0;n<DOLL_POINTS;++n)v=v+(doll.p[n]-doll.prev[n]);
    return v*((float)dollSubsteps/DOLL_POINTS);
}
// Its spin about axis, radians a tic.
static float spinAbout(const Ragdoll &doll,V3 axis) {
    V3 v[DOLL_POINTS],c,vc,spin;
    for(int n=0;n<DOLL_POINTS;++n)v[n]=doll.p[n]-doll.prev[n];
    doll.rigid(v,c,vc,spin);
    return dot(spin,axis)*dollSubsteps;
}
static bool touching(const Ragdoll &doll) {
    for(int n=0;n<DOLL_POINTS;++n)if(!doll.rider(n)&&doll.up(n)<radius[n]+2)return true;
    return false;
}
static float drawOf(uint32_t seed) {return (float)(hash32(seed)>>8)/16777216.0f;}

// Ice: which points lie on it this tic (Ragdoll::rub and damp let them slide). Frozen pools and frozen
// water alike; only the points on the floor, and only moving bodies get here.
void iceTic(Ragdoll &doll) {
    for(int n=0;n<DOLL_POINTS;++n) {
        doll.iced[n]=false;
        if(doll.rider(n)||doll.up(n)>=radius[n]+2)continue;
        const sector_t *sector=doll.sectorUnder(doll.p[n]);
        if(!sector)continue;
        float wx,wy;doll.overGrid(doll.p[n],wx,wy);
        doll.iced[n]=poolFrozen((int)(sector-sectors))||waterIceHolds(wx+doll.floors.x0,wy+doll.floors.y0);
    }
}

// Stairs. A body that comes down on a flight going on over it rolls on down instead of stopping dead on
// the first step: turned about the flat line across its way, up to the spin a wheel of its size has at
// its speed, so it stops being turned once it spins as fast as it goes; loosely curled, the muscles
// half on, so it rolls rather than flops. It stops when the steps run out.
constexpr float rollFrom=1.0f;      // units a tic over the floor it comes down with, at least
constexpr float rollLeast=3.0f;     // and while it rolls, at least (gravity's share on the slope)
constexpr float rollWheel=14;       // units, the body curled up as a wheel
constexpr float rollMost=0.35f;     // radians a tic at most
constexpr float rollTurn=0.05f;     // radians a tic a tic
constexpr float rollTone=0.3f,rollCurl=0.12f,rollGrip=0.5f;
constexpr int rollTics=70,rollOutTics=6;
static const GuideKey rollKey={0,-25,-30,80,100};
float rollGripShare() {return rollGrip;}

// Steps down along way from the body's middle: floors at each 16 units not rising, at least two drops,
// none a ledge (teeterDrop: a body hanging off one has its own way).
static bool stairsAhead(const Ragdoll &doll,V3 way) {
    V3 q=doll.middleOf();
    float was=doll.floorAt(q);
    int drops=0;
    for(int i=1;i<=4;++i) {
        float floor=doll.floorAt(q+way*(16.0f*i));
        if(floor>was+0.5f||was-floor>=teeterDrop)return false;
        drops+=floor<was-0.5f;
        was=floor;
    }
    return drops>=2;
}
// The way down from where the body lies, if any (it came down with nothing to carry it on).
static V3 downhill(const Ragdoll &doll) {
    V3 q=doll.middleOf();
    float dx=doll.floorAt(q+V3{24,0,0})-doll.floorAt(q+V3{-24,0,0}),dy=doll.floorAt(q+V3{0,24,0})-doll.floorAt(q+V3{0,-24,0});
    return std::abs(dx)+std::abs(dy)<4?V3{}:unit(V3{-dx,-dy,0});
}
static void stopRoll(Ragdoll &doll) {doll.rolling=false;doll.rollDone=true;}
void rollTic(Ragdoll &doll,float muscle[MUSCLES],DollPose &shape,float &k) {
    if(doll.halved||doll.lowerHalf||doll.rides||doll.tumbling||doll.slamAt>=0||doll.overing||doll.rollDone) {
        if(doll.rolling)stopRoll(doll);
        return;
    }
    V3 v=meanSpeed(doll);v.z=0;
    float speed=length(v);
    if(!doll.landed) {doll.flew=v;return;}
    if(!doll.rolling) {
        float flew=length(doll.flew);
        V3 way=flew>=rollFrom?doll.flew*(1/flew):downhill(doll);
        doll.rollDone=true;                         // the one chance: as it comes down
        if(length(way)<0.5f||!stairsAhead(doll,way))return;
        doll.rollDone=false;doll.rolling=true;doll.rollAt=doll.tics;doll.rollWay=way;doll.rollOut=0;
        speed=std::max(flew,rollLeast);
        for(int n=0;n<DOLL_POINTS;++n)doll.push(n,way*(speed*0.6f));   // what the landing took
    }
    if(speed>0.5f)doll.rollWay=unit(doll.rollWay*0.8f+v*(0.2f/speed));
    V3 way=doll.rollWay;way.z=0;way=unit(way);
    bool ahead=stairsAhead(doll,way);
    doll.rollOut=ahead?0:doll.rollOut+1;
    if(doll.rollOut>rollOutTics||doll.tics-doll.rollAt>rollTics) {stopRoll(doll);return;}
    if(ahead&&touching(doll)) {
        V3 axis=cross(upWay,way),c=doll.middleOf();
        float want=std::min(rollMost,std::max(speed,rollLeast)/rollWheel);
        float delta=std::clamp(want-spinAbout(doll,axis),-rollTurn,rollTurn);
        for(int n=0;n<DOLL_POINTS;++n)doll.push(n,cross(axis,doll.p[n]-c)*delta);
        if(speed<rollLeast)for(int n=0;n<DOLL_POINTS;++n)doll.push(n,way*((rollLeast-speed)*0.3f));
    }
    for(int g=0;g<MUSCLES;++g)muscle[g]=std::max(muscle[g],rollTone);
    shape=keyShape(doll.body->rest,rollKey);k=rollCurl;
}

// Over a low wall. A body still up, thrown into something whose top is under its chest, turns over the
// top instead of stopping there: the whole body turned about the wall's top, so what is above it goes on
// over and what is below it swings back and up; the legs fold, one late, so they clear it, and it twists
// a little, so it doesn't vault. Only the start is made: it's let go past flat and falls as it falls.
// And the string (the guided falls' jerk vest, ragdoll_doll.h): for the first tics the chest is yanked over
// the edge and up, and once it's over, the heels kick up after it: the railing fall. A real body, a fake
// force, as hard as it came and never past the speed it came with. By the turn alone a shotgun's body
// crawled over in 9-11 tics and stopped 27 past the wall; a rocket's took 5 and went 52. So only for shots:
// a blast has force of its own, and with the string went 107.
constexpr float overFrom=1.5f;      // units a tic its neck must be going over the wall at (as a slam's hit)
constexpr float overTurn=0.06f;     // radians a tic a tic
constexpr float overCarry=2.5f;     // units a tic it keeps going over at (never more than it came with)
constexpr float overRise=1.2f;      // units a tic the turn may lift it at most: it climbs the wall, it isn't thrown off it
constexpr float overPull=1.3f,overPullUp=0.4f;   // the string: units a tic added to the chest a tic, and its share upward
constexpr float overPullRise=2.4f;  // the lift allowed while it pulls
constexpr float overKick=1.4f;      // units a tic up to the heels (half of it along) once the chest is over
constexpr int overPullTics=4;
constexpr float overClear=8;        // units past the wall's face its hips must be (on the line, the floor's 4-unit cells still read the near side)
constexpr float overBeyond=28;      // and are let go at on a top too broad to fall off
constexpr float overTwistMost=0.03f;   // radians a tic about its length, for overTwistTics
constexpr float overTone=0.5f,overCurl=0.15f;
constexpr int overTics=24,overTwistTics=6;
static const GuideKey overKey={0,15,20,55,85};
void overStart(Ragdoll &doll) {
    if(doll.overAt>=0||doll.lyingAt>=0||doll.slamAt>=0||doll.lowerHalf||doll.rides||doll.tumbling||doll.swept)return;
    // Still up, by its feet: the floor under a torso already over the wall is the wall's top (and it
    // has "landed" there). So a body caught on the top is no landing: that's the start of this.
    float feet=std::min(doll.p[P_ANK_L].z,doll.p[P_ANK_R].z);
    if(doll.lowHit<=0)return;
    if(!doll.halved&&(doll.p[P_SPINE].z-feet<=14||unit(doll.p[P_NECK]-doll.hips()).z<=0.4f))return;   // (the top of a cut body has no feet to stand on)
    // How hard by the neck, not the point that hit: thrown high, only its shins catch the wall, slowly, and
    // it trips over at the speed it flew (going by that point, 1 in 3 thrown bodies stopped dead on the top).
    float going=dot(doll.p[P_NECK]-doll.prev[P_NECK],doll.lowWay)*-dollSubsteps;
    if(going<overFrom)return;
    doll.landed=false;
    const Wall &w=doll.lowWall;
    float ex=w.bx-w.ax,ey=w.by-w.ay,len2=std::max(1e-6f,ex*ex+ey*ey);
    V3 spine=doll.p[P_SPINE];
    float t=std::clamp(((spine.x-w.ax)*ex+(spine.y-w.ay)*ey)/len2,0.0f,1.0f);
    doll.overAt=doll.tics;doll.overing=true;
    doll.overHinge=V3{w.ax+ex*t,w.ay+ey*t,w.top};
    doll.overWay=doll.lowWay*-1;
    uint32_t seed=doll.goreSeed^(uint32_t)doll.tics*0x9e3779b9u;
    doll.overSpin=std::clamp(going/30,0.08f,0.18f);doll.overSpeed=going;
    doll.overTwist=(drawOf(seed)*2-1)*overTwistMost;
    doll.overLate=1+(int)(drawOf(seed+1)*5);doll.overLeg=drawOf(seed+2)<0.5f?0:1;
}
void overTic(Ragdoll &doll,float muscle[MUSCLES],DollPose &shape,float &k) {
    if(!doll.overing)return;
    int age=doll.tics-doll.overAt;
    V3 mid=doll.hips(),length_=unit(doll.p[P_NECK]-mid);
    // Let go past the wall: its hips over a floor lower than the top, or, on a broad top, well along it.
    // By the hips, not its middle: let go with the chest over the far edge, it sat down on the wall.
    float beyond=dot(mid-doll.overHinge,doll.overWay);
    bool past=beyond>overClear&&(doll.floorAt(mid)<doll.overHinge.z-2||beyond>overBeyond);
    if(age>=overTics||(past&&length_.z<0.3f)) {doll.overing=false;return;}
    V3 axis=cross(upWay,doll.overWay);
    float delta=std::clamp(doll.overSpin-spinAbout(doll,axis),doll.blasted?-overTurn:-0.02f,overTurn);   // (barely braked on a string: it turns it too)
    for(int n=0;n<DOLL_POINTS;++n)doll.push(n,cross(axis,doll.p[n]-doll.overHinge)*delta);
    if(age<overTwistTics)for(int n=0;n<DOLL_POINTS;++n)doll.push(n,cross(length_,doll.p[n]-mid)*doll.overTwist);
    V3 now=meanSpeed(doll);
    float along=dot(now,doll.overWay),keep=std::min(overCarry,doll.overSpeed);
    if(along<keep)for(int n=0;n<DOLL_POINTS;++n)doll.push(n,doll.overWay*((keep-along)*0.25f));
    float strength=std::clamp(doll.overSpeed/5,0.7f,1.2f);
    bool pulling=!doll.blasted&&age<overPullTics&&along<doll.overSpeed;
    if(pulling) {
        V3 yank=(doll.overWay+V3{0,0,overPullUp})*(overPull*strength*(1-0.5f*age/overPullTics));
        for(int n:{P_SPINE,P_NECK,P_SH_L,P_SH_R})doll.push(n,yank);
        if(!doll.headless)doll.push(P_HEAD,yank*0.6f);
    }
    float rise=pulling?overPullRise:overRise;
    if(now.z>rise)for(int n=0;n<DOLL_POINTS;++n)doll.push(n,V3{0,0,rise-now.z});
    // The heels, one a little after the other, when the neck has gone over.
    if(!doll.halved&&!doll.blasted&&dot(doll.p[P_NECK]-doll.overHinge,doll.overWay)>0)for(int s=0;s<2;++s) {
        if(doll.overKicked[s]||(s==doll.overLeg&&age<doll.overLate))continue;
        doll.overKicked[s]=true;
        V3 kick=(V3{0,0,1}+doll.overWay*0.5f)*(overKick*strength);
        doll.pushInside({{s?P_ANK_R:P_ANK_L,kick},{s?P_KNEE_R:P_KNEE_L,kick*0.6f}});
    }
    muscle[M_LEGS]=0;muscle[M_TRUNK]=overTone;muscle[M_NECK]=std::min(muscle[M_NECK],0.2f);
    if(doll.halved)return;
    shape=keyShape(doll.body->rest,overKey);k=overCurl;
    if(age<doll.overLate) {   // the late leg still straight
        int knee=doll.overLeg?P_KNEE_R:P_KNEE_L,ankle=doll.overLeg?P_ANK_R:P_ANK_L;
        shape[knee]=doll.body->rest[knee];shape[ankle]=doll.body->rest[ankle];
    }
}

// Hollywood's dance: a hit off the middle twists the body about its spine, the shoulders most, fading
// over a few tics; a new hit starts one only when the last has faded, so steady fire rocks it instead of
// winding it up.
constexpr float shockLeast=0.03f,shockMost=0.07f;   // radians a tic a tic at its start
constexpr float shockFade=0.8f,shockGone=0.05f;
void shockHit(Ragdoll &doll,int point,V3 way) {
    if(!doll.hollywood||doll.shock>0)return;
    V3 off=doll.p[point<DOLL_POINTS?point:P_SPINE]-doll.middleOf();
    float across=cross(off,way).z;
    uint32_t seed=doll.goreSeed^(uint32_t)doll.tics*0x85ebca6bu;
    float size=shockLeast+(shockMost-shockLeast)*drawOf(seed);
    doll.shockSpin=std::abs(across)>1.5f?std::copysign(size,across):drawOf(seed+1)<0.5f?-size:size;
    doll.shock=1;
}
void shockTic(Ragdoll &doll) {
    if(doll.shock<=0)return;
    if(doll.lyingAt>=0) {doll.shock=0;return;}
    static const float share[DOLL_POINTS]={1,1,1,1,0.7f,0.3f,0.3f,0.1f,0.1f,0,0};
    V3 mid=doll.hips(),axis=unit(doll.p[P_NECK]-mid);
    for(int n=0;n<DOLL_POINTS;++n)if(share[n]>0)doll.push(n,cross(axis,doll.p[n]-mid)*(doll.shockSpin*doll.shock*share[n]));
    doll.shock*=shockFade;
    if(doll.shock<shockGone)doll.shock=0;
}

// Hollywood's long drop: with time to spare before the floor and a real height under it, the legs go
// round, each at its own pace, until just before it lands. Only the ankles are moved (their speed set,
// over the hips' own); the knees follow as joints do.
constexpr float pedalHeight=48;     // units under the spine, at least
constexpr float pedalFrom=16,pedalTo=3;   // tics to the floor to start from, and to stop at
constexpr float pedalRate=0.35f,pedalRateSpread=0.12f;   // radians a tic
constexpr float pedalCircle=0.28f,pedalDepth=0.68f;      // shares of the leg
constexpr float pedalPull=0.3f;
void pedalTic(Ragdoll &doll) {
    if(!doll.hollywood||doll.landed||doll.halved||doll.lowerHalf||doll.rides||doll.tumbling||doll.overing||
       doll.swept||doll.slamAt>=0||doll.legOff[0]||doll.legOff[1]) {doll.pedalling=false;return;}
    float vz=meanSpeed(doll).z,h=std::max(0.0f,doll.up(P_SPINE)-6);
    float left=(vz+std::sqrt(vz*vz+2*doll.gravity*h))/std::max(0.01f,doll.gravity);
    if(!doll.pedalling) {
        if(doll.pedalDone||doll.tics<4||left<pedalFrom||h<pedalHeight)return;
        uint32_t seed=doll.goreSeed^0x27d4eb2fu;
        doll.pedalling=true;doll.pedalDone=true;
        for(int s=0;s<2;++s) {
            doll.pedalAt[s]=drawOf(seed+s)*6.28f;
            doll.pedalRate[s]=pedalRate+(drawOf(seed+2+s)-0.5f)*pedalRateSpread;
        }
    }
    if(left<pedalTo) {doll.pedalling=false;return;}
    V3 hips=doll.hips(),down=unit(hips-doll.p[P_NECK]),front=doll.pelvisFront(),given{};
    for(int s=0;s<2;++s) {
        int hip=s?P_HIP_R:P_HIP_L,ankle=s?P_ANK_R:P_ANK_L,knee=s?P_KNEE_R:P_KNEE_L;
        float leg=doll.rest(hip,knee)+doll.rest(knee,ankle),a=doll.pedalAt[s]+=doll.pedalRate[s];
        V3 want=doll.p[hip]+down*(pedalDepth*leg)+(front*std::cos(a)+down*std::sin(a))*(pedalCircle*leg);
        V3 along=doll.p[hip]-doll.prev[hip];
        V3 change=along+(want-doll.p[ankle])*(pedalPull/dollSubsteps)-(doll.p[ankle]-doll.prev[ankle]);
        doll.prev[ankle]=doll.prev[ankle]-change;
        given=given+change;
    }
    // What the legs gave taken out of the whole body, up and down too: left in, it swam up through the air.
    given=given*(1.0f/DOLL_POINTS);
    for(int n=0;n<DOLL_POINTS;++n)doll.prev[n]=doll.prev[n]+given;
}
}
