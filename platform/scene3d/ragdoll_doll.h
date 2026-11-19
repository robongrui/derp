/* The ragdoll's doll (ragdoll.cpp): the skeleton and its constants, the walls
   and floors it knows, and the doll itself, simulated in game tics. Shared by
   ragdoll.cpp (the falls), ragdoll_card.cpp (its picture) and ragdoll_gore.cpp. */
#pragma once
#include "ragdoll.h"

namespace doom3d {
constexpr float dollGravity=0.45f;  // units per tic per tic: 1.5x earth's at a body's scale, so throws read snappy (at earth's 0.3 a hard throw flew flat at chest height for half a second); Doom's 1 drops a body in 8 tics
// Physically accurate Phobos (p_phobos.h): the body rides its corpse, which the game slides on with
// the blow's real momentum, Doom's shove over the real one (phobosShove) picking the fall as on Earth;
// it falls at Phobos' pull only while the corpse is on the floor (aloft the two fall together), keeps
// phobosSpin of a Hollywood launch's turn and none of its throw, flails on almost undamped (phobosFlail
// a substep) and lies still only once it barely moves (phobosQuiet) - minutes later (phobosCap tics).
constexpr float phobosShove=45.6f,phobosSpin=0.15f,phobosFlail=0.9995f,phobosQuiet=1e-6f;
constexpr int phobosDeadline=600,phobosLate=1200,phobosCap=35*240;
// A living monster adrift (startTumble): its muscles half braced, turning over as the draft took it.
constexpr float tumbleTone=0.35f,tumbleSpin[2]={0.015f,0.045f};
// Still in its boots in a draft: braced (leanTone), from leanStart units a tic a tic on the draft
// takes the top of the body (each point by leanShare: head most, hips least), leanGain times the
// pull, at most leanMost a tic; back on its feet leanLinger tics after the draft has gone.
constexpr float leanTone=0.6f,leanStart=0.05f,leanGain=2.0f,leanMost=1.5f;constexpr int leanLinger=20;
static const float leanShare[DOLL_POINTS]={1,0.9f,0.8f,0.8f,0.6f,0.3f,0.3f,0.1f,0.1f,0,0};
// Phobos' bodies fly across whole rooms: the walls and floors they know reach phobosReach round
// them (four times a blast's: a big floor grid, gathered again each phobosRegather units).
constexpr float phobosReach=640,phobosRegather=160;
constexpr int dollSubsteps=4,dollPasses=6;
constexpr float kneeTwist=0.35f;    // of a shin's length, how far it may stray sideways from the knee's hinge plane
constexpr float limitZone=0.3f;     // radians (about 17 degrees) before a joint's stop over which the speed into it is taken out (Ragdoll::soften)
constexpr float foldZone=0.07f,spineZone=0.05f;   // the same for the stops kept as lengths: a leg's fold, the spine's bend (shares of the length)
constexpr int lyingGrace=3;         // tics the torso is down before the whole body is damped (Ragdoll::damp)
constexpr float pelletJerk=0.35f,pelletJerkMost=1.2f;   // gore: a pellet's jerk of the part it struck, a 10-damage pellet's and all of them on one part
constexpr int posesPerTic=3;inline int posesTic=-1,posesThisTic=0;   // the poses in view changed this tic (showPose)
constexpr int earlyTics=8,earlyPose=2,airPose=5,rollPose=3;   // the hit's first tics drawn every earlyPose, in the air at least every airPose, rolling down stairs every rollPose (ragdollView)
constexpr float cardSink=3;         // units a lying body's card is pressed into the floor (ragdollCard)
constexpr float overKeep=0.7f;      // of a tumble past flat in the air, the share kept a substep (Ragdoll::damp)
constexpr float blastFreeAbove=20;  // units over the floor a blast-thrown body's middle tumbles on freely, past flat too
constexpr float rollKeep=0.93f;     // of the body's spin about its own length, the share kept a substep (Ragdoll::damp)
constexpr float tiltMost=30;        // degrees the virtual camera is tipped down at most, lying (ragdollCard)
// The arms (Ragdoll::swingArms): held to the drawing's pose by armTone of the trunk's muscle a substep,
// their swing kept armKeep a substep in the air (armFloorKeep on the floor, where they grip), the hand
// never nearer the shoulder than armFold of the arm's length (the elbow folds, it doesn't fold through),
// the elbow at most armBack of the upper arm behind the shoulders (about 37 degrees: at 17 a body on its
// back couldn't lay an arm on the floor beside it).
constexpr float armTone=0.35f,armKeep=0.99f,armFloorKeep=0.5f,armFold=0.4f,armBack=0.6f,armRadius=1.5f;
// And each stop has a soft zone, as the body's joints (soften): the speed into it taken out over the last
// stretch, relative to the shoulder, so it eases into the stop instead of bouncing off it. The fold over
// armFoldZone of the arm's length, straightening over armStraightZone, the upper arm going behind the
// back over armBackZone of its length.
constexpr float armFoldZone=0.12f,armStraightZone=0.08f,armBackZone=0.35f;
// The clawing upper half (Ragdoll::clawTic): lying, it reaches ahead (clawReachTics) and drags itself
// after its hands (the rest of clawTics, clawDrag units a tic at first, less each time), once or twice.
constexpr int clawTics=16,clawReachTics=6;constexpr float clawDrag=0.45f;
// The dance (Ragdoll::dance, the user's: steady fire holding a dying body up a while, Hollywood's): each hit
// on a body still falling jerks the part struck (danceKick units a tic along the shot, danceDrift of it
// carrying the whole body) and holds it up: its legs and trunk tensed, gravity taken out, its upper body
// drawn back over its hips (danceUpright a tic), danceHold of it, less by danceTire each hit, the hold
// fading danceFade a tic between hits; only in its first danceTics tics and until it lies. (Holding only
// the upper body against gravity, fading 0.8 a tic, it fell 4 tics later than without.)
constexpr float twitchKick=1.4f;   // a burnt body's twitch (startRagdoll): units a tic to one part, the second at half
constexpr float danceKick=0.9f,danceDrift=0.15f,danceHold=0.95f,danceTire=0.04f,danceFade=0.93f,danceUpright=0.08f;constexpr int danceTics=50;
constexpr float carryMost=4.5f;
// With F4 "Flying enemies", an explosion's kill (rocket, barrel, BFG) sends the body flying: its slide kept up to blastCarry
// units a tic, and on top a throw along the blast (blastThrow), up (blastLift) and a back somersault
// (blastSpin radians a tic). At the shotgun's caps a rocket's 15 a tic only hopped it 6 units up.
constexpr float blastCarry=8,blastThrow=2.5f,blastLift=4,blastSpin=0.1f;
// And until it lands it comes back off a wall with blastRebound of the speed it hit it at, so it can
// glance off one column into the next (walls are dead otherwise: see the walls in Ragdoll::tic). No
// slam for it: pinned to the first wall it hit, it never reached a second.
constexpr float blastRebound=0.5f;
// The same switch gives shots more punch: a hitscan kill pushes hitPunch times as hard, its slide kept
// up to punchCarry, and a chest or side kill launches graded by the push (Ragdoll::launch from
// startRagdoll): a chaingun's last bullet hops it, a shotgun throws it, a super shotgun harder, never
// past punchLift up (a blast's own); at one cap for all, the shotgun flew as high as a rocket.
constexpr float hitPunch=2,punchCarry=6.5f,punchLift=3.5f;
// Shot out of the air after a kick (airKill): the flight it had is kept up to this, units a tic, not
// doubled (the game's blast already pushed it on): held to punchCarry the body stopped dead in the air.
constexpr float airCarry=16;
constexpr float slideTurnLeast=2,slideTurnMost=7;   // degrees the corpse's slide is turned off the shot's line, either way (startRagdoll)
constexpr float slamSpeed=1.5f;     // units a tic into a wall a torso point must hit at for a slam (Ragdoll::pin)
constexpr float slamBuckle=1.2f;    // units a tic a slam kicks the knees out from the wall
constexpr float sweepFrom=2.0f;     // units a tic a leg kill must push for the legs to be swept (a single bullet, 1.25, buckles them)
constexpr float sweepTurnTics=10;   // tics a sweep's turn about the vertical (its slant off the shot) is spread over
constexpr float sweepSpin=0.2f,sweepLift=1.0f;    // a sweep's turn over (radians a tic) and lift (units a tic) at a shotgun's kill; then only its weight
constexpr int slamTics=40,slamStick=3;   // tics a slammed body is held to the wall at most while it sinks to sitting; of them, hanging     // units per tic: the most of the corpse's own slide the doll flies with
static const float radius[DOLL_POINTS]={4,2.5f,2.5f,2.5f,4,3,3,2,2,1.5f,1.5f};
struct Link { int a,b; };
static const Link bones[]={{P_HEAD,P_NECK},{P_NECK,P_SH_L},{P_NECK,P_SH_R},{P_SH_L,P_SH_R},{P_SH_L,P_SPINE},{P_SH_R,P_SPINE},
    {P_NECK,P_SPINE},{P_SPINE,P_HIP_L},{P_SPINE,P_HIP_R},{P_HIP_L,P_HIP_R},
    {P_HIP_L,P_KNEE_L},{P_KNEE_L,P_ANK_L},{P_HIP_R,P_KNEE_R},{P_KNEE_R,P_ANK_R}};
static const Link soft[]={{P_HEAD,P_SH_L},{P_HEAD,P_SH_R},{P_SH_L,P_HIP_L},{P_SH_R,P_HIP_R},{P_SH_L,P_HIP_R},{P_SH_R,P_HIP_L}};
enum { M_LEGS,M_TRUNK,M_NECK,MUSCLES };
static const std::vector<Link> muscles[MUSCLES]={
    {{P_HIP_L,P_ANK_L},{P_HIP_R,P_ANK_R},{P_SPINE,P_KNEE_L},{P_SPINE,P_KNEE_R},{P_KNEE_L,P_KNEE_R},{P_ANK_L,P_ANK_R}},
    {{P_NECK,P_HIP_L},{P_NECK,P_HIP_R},{P_SH_L,P_HIP_R},{P_SH_R,P_HIP_L}},
    {{P_HEAD,P_SPINE}}};

// A wall or step near the body, in the world: a point below top (units above the body's floor) can't pass it.
struct Wall { float ax,ay,bx,by,top; };

// The floors round the kill (tests/death_floors.py): a grid of floorCell-wide cells floorReach each
// way, each holding its sector, whose floor is read live (a lift under a body carries it). Doom holds
// a thing up at the highest floor anywhere under its box, so a corpse killed at a stair edge floated
// over the lower step its body lay on, sank into a step behind it and lay in the air over a drop.
// A body coming down flat and fast on a liquid skips off it once, as a stone does: below about
// 18 degrees for a body about as dense as the liquid (the ricochet angle of Johnson and Reid, 18
// over the root of the density ratio), going at least skipSpeed units a tic over the floor. It
// splashes and hops on, slowed, before it comes down again and goes in.
constexpr float skipSpeed=6,skipSlope=0.32f;   // tan 18 degrees
constexpr int skipTics=4;                       // after a skip it doesn't count as down
constexpr float floorCell=4,floorReach=160;   // the doll flies in the world: room for a throw
constexpr float blastReach=224;
constexpr int regathersMost=4;                 // times a body's walls and floors are gathered again round it (regather)                // and for a blast's (twice the cells to sample: why it's a switch)
constexpr float dropEdge=2;                    // how round a step's edge over a drop is (Ragdoll::ground)
constexpr float wallSlide=0.5f;                // of a point's skid along a wall it touches, the share lost a substep (Ragdoll::rub)
constexpr float faceDepth=6;                   // units under a floor cell's top past which a point is against its face (Ragdoll::ground)
constexpr float floorStick=0.6f,floorSlide=0.5f;   // contact friction, static and sliding (Ragdoll::rub)
constexpr float iceGrip=0.12f,iceKeep=0.98f;       // on ice (iceTic): the share of that grip, and a lying point's speed kept a substep
constexpr float overGrip=0.2f;                     // going over a low wall (overTic): the share of it, so it slides off the top
constexpr float overHeadroom=6;                    // units the neck must be over a wall's top for it to go over (a half-height wall, the body low)
// A body across a drop (a pedestal's edge, a ledge), something of it on the top and teeterFrom of its
// weight or more over the lower floor (Ragdoll::overDrop), slides and tips off instead of hanging there
// (the user, MAP01's pedestals: "they keep hanging instead of sliding down"; one hung head down from an
// ankle on the lip): not damped as a lying body, its grip on the top eased teeterGrip times the share
// hanging (to teeterLeast of it at most), no deadline to rest by.
// A drop: at least teeterDrop under the highest floor touched, a point teeterOff over the floor under it.
// Lower (8, and 6 off), a body lying down stairs counted, its legs bridging the steps: never damped as
// lying, it slid down them for over a second, speeding up (the arena's stairs, 16 a step).
constexpr float teeterFrom=0.2f,teeterGrip=1.5f,teeterLeast=0.15f,teeterDrop=24,teeterOff=16;
struct FloorGrid { float x0=0,y0=0;int size=0;std::vector<const sector_t*> cells; };
// -ragdollsmooth (the showcase's --smooth): the body posed every tic and blended between tics at the
// frame rate, its camera turning in whole degrees, to show it is a simulation; the game steps both, on
// purpose, so a ragdoll reads as one of Doom's own animations.
extern "C" double I_TimeScale;
static bool smoothDolls() {if(I_TimeScale<0.9)return true;static int on=-1;if(on<0)on=M_CheckParm((char*)"-ragdollsmooth")!=0;return on!=0;}
// -ragdollhardlimits: the joints' stops without their soft zones (Ragdoll::soften), to compare.
static bool softLimits() {static int on=-1;if(on<0)on=M_CheckParm((char*)"-ragdollhardlimits")==0;return on!=0;}
// A body's weight per point once it has touched a wall (Ragdoll::shareOf): head, neck, shoulders with
// the arms, spine, hips, knees with the thighs, ankles with the shins and feet, shares of the whole.
static const float weight[DOLL_POINTS]={0.07f,0.08f,0.10f,0.10f,0.15f,0.10f,0.10f,0.10f,0.10f,0.05f,0.05f};

// Gore: how much a relief face follows the head, and whether a headless body has lost it: cut a row
// higher or lower per column (a tear, never a straight line). The card leaves those out and the head
// burst takes exactly those, so the two never overlap or leave a gap.
static float headShare(const Relief &body,const ReliefFace &face) {
    float head=0;
    for(int k=0;k<4;++k) {
        const ReliefCorner &corner=body.corners[face.corner[k]];
        for(int j=0;j<4;++j)if(corner.part[j]==PART_HEAD)head+=corner.w[j]*0.25f;
    }
    return head;
}
// Only within the head's own width (and margin units either side): the parts follow the drawing by
// height alone, so the shoulders, a hand or a gun at the neck's height went with the head, a strip
// of torn texels hanging off the flying head like a thread of tissue.
static bool overNeck(const Relief &body,const ReliefFace &face,float margin) {
    float x=0;
    for(int k=0;k<4;++k)x+=body.corners[face.corner[k]].p.x*0.25f;
    return std::abs(x-body.headMiddle)<=body.headWidth/2+margin;
}
static bool headLost(const Relief &body,const ReliefFace &face,float head,uint32_t seed) {
    return overNeck(body,face,0.5f)&&head>0.5f+0.6f*(hashUnit(face.c,1,seed)-0.5f);
}
static V3 turn(V3 v,V3 axis,float angle) {
    float c=std::cos(angle),s=std::sin(angle);
    return v*c+cross(axis,v)*s+axis*(dot(axis,v)*(1-c));
}

// Guided falls: the shot death on strings, aimed at the zombieman's own death drawings (tests/
// death_ragdoll.py --guided, beside POSSH-L). Film's recipe: a real body, a fake force. A ratchet
// (the stunt wire on a jerk vest) yanks the chest along the shot for a few tics, the one force
// from outside, its drift taken out like the hit's. All else acts inside the body: the legs give
// way, the knees kicked forward, and key strings pull the body's shape (never its place or
// heading) toward the drawings' poses by shape matching (Mueller et al. 2005): each substep the
// key is fitted rigidly onto the body and every point pulled part of the way to its fitted spot.
// The strings are slack while the ratchet pulls, take hold after it (Zordan and Hodgins 2002: go
// loose at the impact, then track again) and let go as the chest nears the floor, so the landing
// is the doll's own; the muscles let go in the order the landing asks.
// Key angles (degrees) at tics after the hit: the spine arched back at the chest joint, the head
// thrown back on the neck, the thighs swung forward of the hips, the knees bent.
struct GuideKey { float tic,arch,head,hip,knee; };
static const GuideKey guideKeys[]={{0,0,0,0,0},
    {2,15,30,0,5},          // POSSH: the hit, head snapped back
    {7,35,55,15,25},        // POSSI: arched, the head behind the shoulders
    {12,25,50,60,80},       // POSSJ: the knees buckle, it sits back
    {17,10,40,75,95}};      // POSSK: on its back, knees up
static const GuideKey guideFlinch={0,-12,-25,10,20};   // the startle: trunk and head forward, knees soft
// The landing order. Butt: the hips land, then the back, the head last (the trunk held until the
// hips are down, the neck until the back is). Flat: all lets go when the legs give. Topple: the
// legs stay locked until the chest is low (a plank falling).
enum { LAND_BUTT,LAND_FLAT,LAND_TOPPLE };
// The knobs, as the lab names them: ratchet (units per tic added to the chest each ratchet tic, the
// head 60% of it; scaled by the blast), its tics, the tic the legs give, how far a tic the key
// strings pull, a startle before the hit (it held the body up 4 tics longer and barely showed at
// the death frames' pace: off), and the landing order.
struct Guide { float ratchet=0.9f;int ratchetTics=3,give=4;float key=0.35f;bool flinch=false;int landing=LAND_BUTT; };
constexpr int flinchTics=2;
constexpr float keySpring=0.05f;   // of each key pull, the share kept as speed (0.3 still rocked the chest 10 degrees)
constexpr float keyFade=6;         // tics the strings take to let go after the last key (hung over a ledge they held its jackknife)

static V3 rotX(V3 v,V3 pivot,float a) {   // about the side axis through pivot; positive turns up toward the front (-y)
    float y=v.y-pivot.y,z=v.z-pivot.z,c=std::cos(a),s=std::sin(a);
    return {v.x,pivot.y+y*c-z*s,pivot.z+y*s+z*c};
}
// The rest skeleton bent by a key: arch and head back, the thighs forward, the shins back (the hinge's way).
static DollPose keyShape(const DollPose &rest,const GuideKey &k) {
    const float rad=doomPi/180;
    DollPose p=rest;
    for(int n:{P_NECK,P_SH_L,P_SH_R,P_HEAD})p[n]=rotX(p[n],rest[P_SPINE],-k.arch*rad);
    p[P_HEAD]=rotX(p[P_HEAD],p[P_NECK],-k.head*rad);
    for(int s=0;s<2;++s) {
        int hip=s?P_HIP_R:P_HIP_L,knee=s?P_KNEE_R:P_KNEE_L,ankle=s?P_ANK_R:P_ANK_L;
        p[knee]=rotX(p[knee],rest[hip],-k.hip*rad);p[ankle]=rotX(p[ankle],rest[hip],-k.hip*rad);
        p[ankle]=rotX(p[ankle],p[knee],k.knee*rad);
    }
    return p;
}
// The key u tics after the hit: eased from rest through the keys, held at the last.
static GuideKey keyAt(float u) {
    const int count=(int)(sizeof guideKeys/sizeof guideKeys[0]);
    for(int i=1;i<count;++i) {
        const GuideKey &a=guideKeys[i-1],&b=guideKeys[i];
        if(u>=b.tic)continue;
        float f=std::clamp((u-a.tic)/(b.tic-a.tic),0.0f,1.0f);f=f*f*(3-2*f);
        auto mix=[&](float x,float y){return x+(y-x)*f;};
        return {u,mix(a.arch,b.arch),mix(a.head,b.head),mix(a.hip,b.hip),mix(a.knee,b.knee)};
    }
    return guideKeys[count-1];
}
// The rotation nearest m (its polar part: averaged with its inverse transpose until it settles);
// false when m is degenerate or a reflection.
static bool rotationOf(const float m[3][3],float r[3][3]) {
    float a[3][3];memcpy(a,m,sizeof a);
    for(int pass=0;pass<40;++pass) {
        V3 r0{a[0][0],a[0][1],a[0][2]},r1{a[1][0],a[1][1],a[1][2]},r2{a[2][0],a[2][1],a[2][2]};
        V3 cof[3]={cross(r1,r2),cross(r2,r0),cross(r0,r1)};
        float det=dot(r0,cof[0]);
        if(det<=1e-9f)return false;
        float change=0;
        for(int i=0;i<3;++i) {
            float c[3]={cof[i].x,cof[i].y,cof[i].z};
            for(int j=0;j<3;++j) {float v=0.5f*(a[i][j]+c[j]/det);change=std::max(change,std::abs(v-a[i][j]));a[i][j]=v;}
        }
        if(change<1e-6f)break;
    }
    memcpy(r,a,sizeof a);
    return true;
}

struct BallMotion;
struct GoreBlow;
struct Ragdoll;
// ragdoll_moves.cpp: what a falling body does on ice, stairs and low walls, and Hollywood's dance and long drop.
float rollGripShare();
void shockHit(Ragdoll &doll,int point,V3 way);
struct Ragdoll {
    const Relief *body=nullptr;
    int state=0,start=0,tics=0,still=0;
    std::vector<int> steps;
    float ax[2]={1,0},ay[2]={0,1};     // the body's x and y in the world, fixed at the kill
    float ox=0,oy=0;                   // where it died: the doll falls in the world from there
    bool mirror=false,landed=false,resting=false,heavy=false;
    DollPose p{},prev{};mutable DollPose shown{};
    mutable int pictureAsked=-1000;   // the tic it last asked for a picture out of sight (ragdollWantsPicture)
    mutable int cardDrawnAt=-1000;    // the tic its card was last asked for: in view (showPose)
    mutable int shownAt=0;int lastPose=0,poseTics=5;   // a pose drawn at tic lastPose, held poseTics
    std::shared_ptr<BallMotion> ball;      // a ball of flesh: how it moves (body_ball.cpp), in place of the fall
    std::vector<V3> shell,shellShown;      // and how far each shell point is from rest, now and as shown,
    std::array<V3,3> shellTurn{V3{1,0,0},V3{0,1,0},V3{0,0,1}},shellTurnShown=shellTurn;   // and its axes as it's turned
    DollPose shownWas{},shownTic{};    // -ragdollsmooth: the last two tics' poses, the drawn one between them
    int lyingAt=-1;                        // the tic the torso came down
    float spineLeast=0.9f;                 // how far the neck may close on the hips, of its straight distance
    V3 tipWay{0,1,0},toEye{0,-1,0};        // the hit's way over the floor and the eye's, in body space
    float centre[2]={},centreWas[2]={},centreShown[2]={};   // the body's middle over the floor: this tic, the last, the pose shown
    float rise=0;                          // its middle's climb the last tic (the top of a throw is a pose)
    const sector_t *restSector=nullptr;float restFloor=0;   // the floor it came to rest on (a lift carries the corpse)
    std::vector<Wall> nearWalls;           // the walls round it, in body space
    int slamAt=-1;float slamAge=-1;V3 slamWay{};   // a wall slam (Ragdoll::pin): its tic, age, the wall's way out
    float hitWall=0;V3 hitWay{};int hitPoint=-1;    // the hardest a torso point went into a wall this tic, and which
    bool splashed=false;float drop=0,smearing=0;    // the wall splash made; the spine's fall last tic; slid since a smear
    float spineAt[2]={},glide[2]={};bool spineKnown=false;   // the spine in the world last tic, and its way over the floor then
    int skipAt=-1;                                  // the tic it skipped off a liquid (skipSpeed)
    float hold[MUSCLES]={},over[MUSCLES]={},late=0;
    float spin=0;int way=0;
    V3 launchV{};float launchSpin=0;int launchAt=-1;   // a chest blast's launch (launch), at tic launchAt
    V3 launchWay{};                    // which way it goes over when launchV has no way of its own (Phobos)
    float launchTwist=0;               // and its turn about the vertical (a pirouette), radians a tic
    bool airKill=false;int trailed=0;  // shot out of the air, kicked off its feet (p_swept.c): a flash, and blood off it as it flies, up to that tic
    bool swept=false;                  // legs swept by a heavy blast: the weight brings it down, no steering hold
    bool knelt=false;                  // killed down on one knee: started folded, unfolding (ragdoll_kneel.cpp):
    float kneelLen[6]={},unfold=1;     //   the legs' muscle lengths as folded, and how far they have grown back (1: standing)
    bool headless=false;               // Gore (gore.cpp): the head shot off, its point riding on along the neck (unhead)
    bool torn=false;float biteX=0,biteZ=0,biteR=0;   //   or a chunk torn out of it: a bite at its edge (rest, body units)
    bool halved=false;float cutZ=0;    // Gore: cut in half at the waist (rest height), the legs' points riding the chest (unleg)
    float cutSlant=0;                  //   a slash: the cut's rise per unit across, shoulder to hip (gore_blow.cpp), cutZ its height at x 0
    std::shared_ptr<StandingLegs> legs;//   and left standing (standing_legs.cpp, -gorelegsrig), or in its own frames:
    int legsPumped=0;                  //   their cut's last tic pumped
    const GutsPatch *guts=nullptr;     //   what its cuts show (gore_guts.cpp)
    bool pooled=false;                 //   its stump's pool laid, once it lay still
    bool legsFly=false;                //   halved low: the top dropping in place, the legs thrown off whole
    std::shared_ptr<Ragdoll> legsDoll; //   as a doll of their own (lowerHalf)
    bool lowerHalf=false;              // this doll is such legs: the upper points ride the pelvis (unbody)
    int pumped=0;uint32_t goreSeed=0;  //   and the stump's last tic pumped, and its own beat
    std::shared_ptr<HeadBurst> burst;  //   and the head bursting where it was (head_burst.cpp),
    float burstAt[3]={};               //   its middle from the neck in the world, as at the kill;
    std::shared_ptr<FlyingPiece> flying;//   or, severed, the head flying off (flying_piece.cpp)
    float walk[2]={};                  // Its walking speed at the kill, in the world (bodyWalk): goes with what it throws off
    std::shared_ptr<GoreBlow> blow;    // Gore: what the kill took off as a shape (gore_blow.cpp),
    bool armBlown[2]={},legBlown[2]={};//   and the limbs it took (a leg's legOff stays, for the fall)
    bool armOff[2]={};                 //   an arm shot off at the shoulder (left, right as drawn),
    std::shared_ptr<FlyingPiece> armPieces[2];//   flying off, its shoulder pumping
    int armPumped=0;
    int legOff[2]={};                  //   a leg shot off (left, right as drawn): 1 under the knee, 2 at the hip
    std::shared_ptr<FlyingPiece> legPieces[2];//   flying off, its stump pumping (gore_legs.cpp)
    int legPumped=0;
    int corpseHits[4]={};              //   shots into the corpse by part (head, left arm, right arm, torso), 10 each
    int burstFrom=0,armFrom[2]={};     //   the tic (since the kill) the head burst and each arm's flight began
    int cutSerial=0;                   //   cuts made on the corpse: its picture drawn again for each
    float danceHold=0;int danceHits=0; //   the dance: steady fire holding it up (dance), how hard now, hits so far
    bool burnt=false;int twitchAt[2]={-1,-1};uint32_t twitchSeed=0;   //   killed by a burn (startRagdoll): no blood
                                       //   when it lands or slides, the tics it twitches at and their draw
    struct Bite { float x,z,r; };      //   chunks shot out of the corpse (goreCorpseHit), as drawn,
    std::vector<Bite> bites;
    std::vector<std::shared_ptr<FlyingPiece>> chunks;std::vector<int> chunkFrom;   //   thrown off, each from its tic
    int clawCycles=0,clawStart=-1;     //   cut in half: the top's claws ahead (clawTic), how many, from when
    bool clawReach=false;float clawPull=0;V3 clawWay{};   //   this tic: reaching, or dragging itself after its hands
    bool blasted=false;                // thrown by an explosion: flies, comes back off walls in the air, no slam
    bool hollywood=false;              // F4 Deaths "Ragdolls, Hollywood": the dance's twist and the long drop's legs
    bool iced[DOLL_POINTS]={};         // points lying on ice this tic (iceTic)
    bool rolling=false,rollDone=false;int rollAt=0,rollOut=0;V3 rollWay{},flew{};   // down stairs (rollTic); its way over the floor in the air
    float lowHit=0;V3 lowWay{};Wall lowWall{};      // the hardest it went into a wall with its top under the chest this tic
    bool overing=false;int overAt=-1,overLate=0,overLeg=0;   // over that wall (overTic): the tic it began, the late leg
    V3 overHinge{},overWay{};float overSpin=0,overTwist=0,overSpeed=0;bool overKicked[2]={};
    float shock=0,shockSpin=0;         // the dance's twist (shockTic)
    bool pedalling=false,pedalDone=false;float pedalAt[2]={},pedalRate[2]={};   // the long drop's legs (pedalTic)
    float reach=floorReach;            // how far round the kill its walls and floors are known
    bool rides=false;                  // Phobos: rides its corpse (follow), the walls kept in the world
    float gravity=dollGravity;         // the pull this tic
    float oxWas=0,oyWas=0,gz0=0,gzWas=0;   // Phobos: where the corpse was last tic; the feet's height at the kill
    float gatherX=0,gatherY=0;         // Phobos: where its walls and floors were gathered round
    int followedAt=-1;                 // Phobos: the tic it last followed its corpse
    bool tumbling=false;               // Phobos: a living monster a draft tore off its feet (startTumble)
    bool pinned=false;                 // and still in its boots: its ankles held where they stand,
    V3 ankleAt[2]{};                   //   here, while the draft (draft, units a tic a tic, world) bends the
    float draft[3]={};int calm=0;      //   top of it over; calm: tics the draft has been gone
    int regathers=0;                   // times they were gathered again round where it flew (regather)
    float hipHold=0;                   // a swept body's hips held (spine to knee) while its knees are gone (tic)
    bool guided=false;Guide guide;     // a guided fall (above): its knobs, the ratchet's line,
    V3 yank{};float yaw=0,kicks[2]={},legLate=0;   // its turn, the knees' kicks and the late leg
    int hipsDownAt=-1,chestDownAt=-1,lowAt=-1;     // tics the hips, then the chest touched down, and the chest got low
    std::vector<Wall> walls;
    FloorGrid floors;float gx=0,gy=0,gz=0;          // the floors, and where the thing is over them now (its feet gz)
    float pressed[DOLL_POINTS]={};                  // how deep the floor pushed each point back up this substep
    int wallTouches=0;                              // point-wall contacts this tic (the test log)
    V3 walled[DOLL_POINTS]={};                      // the way out of the wall each point touched this substep (rub)
    int wallAt=-1;                                  // the tic it first touched a wall (shareOf)
    V3 shouldersWas{},hipsWas{};                    // the shoulder and hip lines last tic (the test log's twist and pelvis roll)
    int mirrors=0;                                  // knee hinge mirrors this tic (the test log)
    float teeter=0;                                 // down across a drop: its weight's share over it (0: not)
    mutable std::vector<V3> posed;mutable int posedAt=-1;
    mutable int cardKey=0,cardBottom=0,cardLeft=0,cardRight=0,cardFirst=0;mutable uint64_t cardSignature=~0ull;   // the card's picture (ragdollCard)
    mutable float cardMiddle[3]={};                     // where the card stands, in the body's drawn space
    mutable int lastStep=-1;                            // the camera's 45-degree step last drawn
    mutable int cardHip[2]={},cardHipAngle=-1000;       // where the last picture's hips were, at which camera angle (ragdoll_pixels.cpp)
#ifdef DOOM_DEVTOOLS
    mutable float cardAzimuth=0;mutable int cardAngle=0,cardTilt=0;   // the eye's way and the camera's angle and tilt then (ragdollPeek)
#endif
    // Once it lies still the body is baked into a plain corpse sprite: the last picture, the camera
    // angle and tilt it was seen from, shown the same from every angle (rotation 0), its 3D data freed.
    mutable bool baked=false;mutable int bakedAngle=0,bakedTilt=0;
    mutable int cardCuts=0;                             // the cuts it was drawn with (cutSerial)
    mutable uint32_t cardWounds=0;                      // the holes the picture was drawn with (redrawn when they change)

    float rest(int a,int b) const {return length(body->rest[a]-body->rest[b]);}
    // A muscle's length to hold: the rest's, but for a body unfolding from a kneel (ragdoll_kneel.cpp) its legs'
    // grow from the folded ones (held at the rest's from the start, they sprang it upright and up off the floor).
    float muscleRest(int g,size_t i) const {
        const Link &l=muscles[g][i];float r=rest(l.a,l.b);
        return g!=M_LEGS||unfold>=1||i>=6?r:kneelLen[i]+(r-kneelLen[i])*unfold;
    }
    // How many tics a pose is held now, and the end of a tic: a pose drawn when that's up, or at a moment
    // that must be seen (ragdollView says which).
    int poseEvery() const {return smoothDolls()?1:tics<=earlyTics?earlyPose:rolling?std::min(poseTics,rollPose):landed?poseTics:std::min(poseTics,airPose);}
    void showPose(bool moment) {
        if(smoothDolls()) {shownWas=shownTic;shownTic=p;}
        if(tics==1||tics-lastPose>=poseEvery()||moment||resting) {
            // A pose in view is a picture to draw, about a millisecond. Bodies felled by one blast pose in
            // step: past posesPerTic in a tic the rest hold theirs a tic longer. Unseen ones draw nothing.
            bool seen=leveltime-cardDrawnAt<=2;
            if(posesTic!=leveltime) {posesTic=leveltime;posesThisTic=0;}
            if(seen&&posesThisTic>=posesPerTic&&!moment&&!resting&&tics!=1&&!smoothDolls())return;
            posesThisTic+=seen;
            shown=p;shellShown=shell;shellTurnShown=shellTurn;++shownAt;lastPose=tics;
            std::copy(centre,centre+2,centreShown);
        }
    }
    void push(int n,V3 v) {prev[n]=prev[n]-v*(1.0f/dollSubsteps);}   // add a velocity (units per tic)
    void armsAlong() {         // the arms going as their shoulders go (at the kill, after its pushes)
        for(int n=DOLL_POINTS;n<POSE_POINTS;++n) {int sh=n==P_ELB_L||n==P_HAND_L?P_SH_L:P_SH_R;prev[n]=p[n]-(p[sh]-prev[sh]);}
    }
    void noDrift() {          // the points' mean sideways speed taken out: the game's own momentum carries the thing
        V3 mean{};
        for(int n=0;n<DOLL_POINTS;++n)mean=mean+(p[n]-prev[n]);
        mean=mean*(1.0f/DOLL_POINTS);mean.z=0;
        for(int n=0;n<DOLL_POINTS;++n)prev[n]=prev[n]+mean;
    }
    // A cell's floor in the body's heights (the thing's feet 0); off the grid, the nearest edge cell's.
    float cellFloor(int i,int j) const {
        if(floors.cells.empty())return 0;
        i=std::clamp(i,0,floors.size-1);j=std::clamp(j,0,floors.size-1);
        return units(floors.cells[(size_t)j*floors.size+i]->floorheight)-gz;
    }
    // A body point over the grid (its world-aligned units); the doll falls unmirrored, like the walls.
    void overGrid(V3 q,float &wx,float &wy) const {
        float side=mirror?-1.0f:1.0f;
        wx=gx+ax[0]*side*q.x+ay[0]*q.y;wy=gy+ax[1]*side*q.x+ay[1]*q.y;
    }
    // A point's speed in the world, units a second (p and prev are a substep apart).
    void worldSpeed(V3 now,V3 before,float v[3]) const {
        float side=mirror?-1.0f:1.0f;V3 d=(now-before)*(float)(dollSubsteps*TICRATE);
        v[0]=ax[0]*side*d.x+ay[0]*d.y;v[1]=ax[1]*side*d.x+ay[1]*d.y;v[2]=d.z;
    }
    void worldSpeed(int n,float v[3]) const {worldSpeed(p[n],prev[n],v);}
    float floorAt(V3 q) const {
        float wx,wy;overGrid(q,wx,wy);
        return cellFloor((int)std::floor(wx/floorCell),(int)std::floor(wy/floorCell));
    }
    float up(int n) const {return p[n].z-floorAt(p[n]);}   // how high a point stands over the floor under it
    // Its walls and floors are known reach round a spot (where it died, then where it flew: see
    // regather). Near that edge, gathered again round it regathersMost times: after that it goes no
    // further out (a throw that far would pass walls it doesn't know). Stopped dead there in mid-air,
    // a body thrown far by a blast dropped straight down (4 falls in 27).
    V3 middleOf() const {V3 c{};for(int n=0;n<DOLL_POINTS;++n)c=c+p[n];return c*(1.0f/DOLL_POINTS);}
    // How far its middle is from the middle of what it knows (units over the floor), and where that is (world).
    float outFromKnown(float &wx,float &wy) const {
        overGrid(middleOf(),wx,wy);
        float out=std::hypot(wx-reach,wy-reach);
        wx+=floors.x0;wy+=floors.y0;
        return out;
    }
    void keepNear() {
        float wx,wy,out=outFromKnown(wx,wy);
        if(out<reach-24)return;
        // The way out from the known middle, in body space (as wallsHere turns the walls).
        float dx=wx-(floors.x0+reach),dy=wy-(floors.y0+reach),side=mirror?-1.0f:1.0f;
        V3 way=unit(V3{side*(dx*ax[0]+dy*ax[1]),dx*ay[0]+dy*ay[1],0}),v{};
        for(int n=0;n<DOLL_POINTS;++n)v=v+(p[n]-prev[n]);
        float going=dot(v,way)/DOLL_POINTS;
        if(going>0)for(int n=0;n<DOLL_POINTS;++n)prev[n]=prev[n]+way*going;
        if(going>0&&M_CheckParm((char*)"-ragdolllog"))fprintf(stderr,"ragdolledge %u tic %d out %.0f stopped %.2f up %.1f\n",(unsigned)((uintptr_t)this%100000),tics,out,going*dollSubsteps,middleOf().z);
    }
    // The sector of the floor cell under a body point (off the grid, the nearest edge cell's).
    const sector_t *sectorUnder(V3 q) const {
        if(floors.cells.empty())return nullptr;
        float wx,wy;overGrid(q,wx,wy);
        int i=std::clamp((int)std::floor(wx/floorCell),0,floors.size-1),j=std::clamp((int)std::floor(wy/floorCell),0,floors.size-1);
        return floors.cells[(size_t)j*floors.size+i];
    }
    // Down with its torso, not just touched down: the damping that stops a body soon and the legs
    // slackening out wait for this (keyed to touching down, the head too, an upside-down body froze
    // standing on its head against a wall).
    // How high a point stands over the highest floor within its reach: a body across a step's edge lies on
    // the step. (Measured over the floor right under it, a body lying head down the arena's stairs, its
    // spine over the lower step, never counted as lying and lay still to the 175-tic cap.)
    float upHeld(int n) const {
        float wx,wy;overGrid(p[n],wx,wy);float r=radius[n<DOLL_POINTS?n:0],top=-1e9f;
        for(int j=(int)std::floor((wy-r)/floorCell);j<=(int)std::floor((wy+r)/floorCell);++j)
            for(int i=(int)std::floor((wx-r)/floorCell);i<=(int)std::floor((wx+r)/floorCell);++i)top=std::max(top,cellFloor(i,j));
        return p[n].z-top;
    }
    bool lying() const {return lowerHalf?landed&&std::max(up(P_HIP_L),up(P_HIP_R))<6:landed&&(upHeld(P_SPINE)<8||upHeld(P_NECK)<7);}
    // Sitting: both hips on the floor. Slumped against a wall after a slam the spine stays up, and a
    // body counted as down only by its spine was never landed, never damped down, and steered on.
    // (Sitting in the open is only on the way over: a legs-swept fall sits, then goes onto its back.)
    bool seated() const {return std::max(up(P_HIP_L)-radius[P_HIP_L],up(P_HIP_R)-radius[P_HIP_R])<1.5f;}
    bool down() const {return lying()||(slamAt>=0&&landed&&seated());}
    // Pushes that turn and fold the body without carrying it: their net sideways drift taken out of
    // every point (taking out all of it, as noDrift does, cancelled the launch).
    void pushInside(std::initializer_list<std::pair<int,V3>> pushes) {
        V3 mean{};
        for(const auto &q:pushes) {push(q.first,q.second);mean=mean+q.second;}
        mean=mean*(1.0f/DOLL_POINTS);mean.z=0;
        for(int n=0;n<DOLL_POINTS;++n)push(n,mean*-1);
    }
    // The launch (tests/death_ragdoll.py GUIDED launch, lift, spin): the whole body thrown back and up,
    // kept (the game's slide comes on top), and a back somersault about its middle, so the chest leads
    // and the feet sweep up and it flies laid back. Without it a chest blast only turned the body about
    // its hips and it sat down: lights out, never sent flying.
    void launch() {
        V3 c{};
        for(int n=0;n<DOLL_POINTS;++n)c=c+p[n];
        c=c*(1.0f/DOLL_POINTS);
        V3 d=unit(V3{launchV.x,launchV.y,0});
        if(length(d)<0.5f)d=launchWay;   // a throw the game carries (Phobos): only its way is left here
        for(int n=0;n<DOLL_POINTS;++n) {V3 r=p[n]-c;push(n,launchV+V3{d.x*r.z,d.y*r.z,-dot(r,d)}*launchSpin+V3{-r.y,r.x,0}*launchTwist);}
        armsAlong();
    }
    // Torn off its feet by a draft (draft: units a tic a tic, in the world): the pull on the top
    // of it pitches it over head first the way the air goes, the harder the faster.
    void pitchInto(const float draft[3]) {
        float pull=std::hypot(std::hypot(draft[0],draft[1]),draft[2]),side=mirror?-1.0f:1.0f;
        V3 way{side*(draft[0]*ax[0]+draft[1]*ax[1]),draft[0]*ay[0]+draft[1]*ay[1],0};
        if(pull<=0||length(way)<1e-6f)return;
        launchWay=unit(way);launchV={};
        launchSpin=std::clamp(0.03f+0.25f*pull,0.03f,0.15f);
        launchAt=tics;
    }
    V3 hips() const {return middle(p[P_HIP_L],p[P_HIP_R]);}
    V3 front() const {return unit(cross(p[P_SH_R]-p[P_SH_L],p[P_NECK]-hips()));}
    V3 pelvisFront() const {return unit(cross(p[P_HIP_R]-p[P_HIP_L],p[P_SPINE]-hips()));}
    // Point a's share of a link's correction. Even until the body touches a wall, then by weight: with
    // every point as heavy as the spine, a foot stopped against a wall dragged the trunk round it as hard
    // as the trunk dragged the foot, and bodies twisted and rolled there (arena: shotgunner leg sweeps
    // into a wall 22 behind, shoulders turned 279 degrees in all, 91 weighted; open floor 164). Weighted
    // from the start the open-floor falls would change; after the touch the play test read more real.
    float shareOf(int a,int b) const {
        if(wallAt<0)return 0.5f;
        float wa=1/weight[a],wb=1/weight[b];return wa/(wa+wb);
    }
    void link(int a,int b,float length_,float k) {
        V3 d=p[b]-p[a];float dist=std::max(1e-6f,length(d));
        V3 corr=d*(k*(dist-length_)/dist);float sa=shareOf(a,b);
        p[a]=p[a]+corr*sa;p[b]=p[b]-corr*(1-sa);
    }
    // As link, but never lifting either point: the lying body's own easing (legs slackening out,
    // hips opening) assumes a body flat on the floor; slumped against a wall, the only way left for
    // them was up it, and bodies climbed walls (up to 27 units in the arena) and pillars (48).
    void linkDown(int a,int b,float length_,float k) {
        V3 d=p[b]-p[a];float dist=std::max(1e-6f,length(d));
        float sa=shareOf(a,b);V3 corr=d*(k*(dist-length_)/dist),ca=corr*sa,cb=corr*-(1-sa);
        ca.z=std::min(ca.z,0.0f);cb.z=std::min(cb.z,0.0f);
        p[a]=p[a]+ca;p[b]=p[b]+cb;
    }
    // A whole leg turned about its hip part of the way from d to want, where it was too: a limit
    // that keeps the bones' lengths and adds no speed.
    void swing(int s,V3 d,V3 want) {
        V3 axis=cross(d,want);float size=length(axis);
        if(size<1e-6f)return;
        float angle=0.5f*std::atan2(size,dot(d,want));axis=axis*(1/size);
        V3 hip=p[s?P_HIP_R:P_HIP_L];
        for(int n:{s?P_KNEE_R:P_KNEE_L,s?P_ANK_R:P_ANK_L}) {p[n]=hip+turn(p[n]-hip,axis,angle);prev[n]=hip+turn(prev[n]-hip,axis,angle);}
    }
    // The spine bends at most about 45 degrees at the chest joint, a thigh swings no further back
    // than 30 degrees behind the hips nor flexes past 110 toward the chest, and a knee is a hinge:
    // the shin only swings back from the thigh's line (a knee bent the wrong way by over ~9 degrees
    // is mirrored across the hip-ankle line, which keeps both lengths, where it was too; less
    // doesn't show, and a straight leg on the floor flipped every substep and twitched), never
    // folding the leg tighter than 55% of its length (tighter curled bodies into balls). Lying,
    // a leg slackens out toward 85% of its length, and the hips ease open, the thigh out toward the
    // trunk's line (neck to knee 95% of standing): sat down by legs swept or a side hit and rolled
    // drawn side up, a body lay curled with its knees to its chest (7 and 6 in 12 of those falls).
    void limits() {
        for(int s=0;s<2&&!lowerHalf;++s) {   // (the legs alone have no spine)
            int hip=s?P_HIP_R:P_HIP_L;float least=spineLeast*rest(P_NECK,hip);
            if(length(p[P_NECK]-p[hip])<least)link(P_NECK,hip,least,0.5f);
        }
        V3 n=pelvisFront(),up=unit(p[P_SPINE]-hips());
        for(int s=0;s<2;++s) {
            if(legOff[s]==2)continue;   // (gone from the hip: no thigh to hold)
            V3 hip=p[s?P_HIP_R:P_HIP_L],d=unit(p[s?P_KNEE_R:P_KNEE_L]-hip),want=d;
            if(dot(want,n)<-0.5f)want=unit(want+n*(-0.5f-dot(want,n)));
            if(dot(want,up)>0.34f)want=unit(want-up*(dot(want,up)-0.34f));
            if(length(want-d)>1e-6f)swing(s,d,want);
        }
        V3 side=unit(p[P_HIP_R]-p[P_HIP_L]);
        for(int s=0;s<2;++s) {
            if(legOff[s])continue;   // (no shin)
            int h=s?P_HIP_R:P_HIP_L,k=s?P_KNEE_R:P_KNEE_L,a=s?P_ANK_R:P_ANK_L;
            V3 hip=p[h];
            // The hinge's plane: the knee turns about the hips' side axis (square to the thigh), give
            // or take kneeTwist of the shin sideways for the thigh's own turn. The sign test alone let
            // a knee fold sideways (101 of 104 bends past 45 degrees in the settling lab, mostly knee
            // on the floor); the ankle is turned back toward the plane, its speed kept.
            {
                V3 thigh=unit(p[k]-hip),hinge=unit(side-thigh*dot(side,thigh)),shin=p[a]-p[k];
                float len=length(shin),off=dot(shin,hinge);
                if(len>1e-3f&&std::abs(off)>kneeTwist*len) {
                    V3 want=unit(shin-hinge*(off-std::copysign(kneeTwist*len,off)))*len;
                    V3 move=(p[k]+want-p[a])*0.5f;
                    p[a]=p[a]+move;prev[a]=prev[a]+move;
                }
            }
            if(dot(cross(unit(p[k]-hip),unit(p[a]-p[k])),side)<-0.15f) {
                V3 axis=unit(p[a]-hip);
                auto mirrored=[&](V3 q){V3 r=q-hip;return hip+axis*(2*dot(r,axis))-r;};
                p[k]=mirrored(p[k]);prev[k]=mirrored(prev[k]);++mirrors;
            }
            float full=rest(h,k)+rest(k,a),reach=length(p[a]-p[h]);
            if(reach<0.55f*full)link(h,a,0.55f*full,0.5f);
            else if(lying()&&reach<0.85f*full)linkDown(h,a,0.85f*full,0.03f);
            // Only a knee down on the floor: one in the air pushed off the neck held the legs
            // straight up off a body on its back (ragdoll_arena.py, chest blasts); gravity takes those.
            float open=0.95f*rest(P_NECK,k);
            if(lying()&&p[k].z-floorAt(p[k])<radius[k]+4&&length(p[k]-p[P_NECK])<open)linkDown(P_NECK,k,open,0.03f);
        }
    }
    // Soft joint limits, once a substep on the speeds: over the last stretch before each stop the
    // speed heading into it is taken out, none at the zone's start, all at the stop (eased). The hard
    // limits above stay as the backstop, but they turn a limb back with its speed (p and prev moved
    // together), so a leg swinging into its stop was turned back every substep while still driving
    // in, and handed that on to the body: the twisting against walls. Speed away from a stop is free.
    static float zoneShare(float left,float zone) {
        float s=std::clamp(1-left/zone,0.0f,1.0f);
        return s*s*(3-2*s);
    }
    // The pelvis' spin (radians a substep) from the hips' line and the spine above them, as a rigid
    // frame: half the sum of each axis crossed with its own turn.
    V3 pelvisSpin(const V3 v[DOLL_POINTS]) const {
        V3 across=p[P_HIP_R]-p[P_HIP_L],rise=p[P_SPINE]-hips();
        float la=length(across),lr=length(rise);
        if(la<1e-3f||lr<1e-3f)return {};
        V3 e1=across*(1/la),d1=(v[P_HIP_R]-v[P_HIP_L])*(1/la);
        V3 e2=rise*(1/lr),d2=(v[P_SPINE]-middle(v[P_HIP_L],v[P_HIP_R]))*(1/lr);
        V3 e3=cross(e1,e2),d3=cross(d1,e2)+cross(e1,d2);
        return (cross(e1,d1)+cross(e2,d2)+cross(e3,d3))*0.5f;
    }
    // share of a leg's turn against the pelvis about axis (through its hip, positive into the stop) out
    // of knee and ankle. Against the pelvis, not the world: a body turning over as one (a leg sweep)
    // swings its legs with it, and braking that as a joint's speed spun bodies up to 500 degrees.
    void slowLeg(V3 v[DOLL_POINTS],int s,V3 axis,float share,V3 pelvis) const {
        int h=s?P_HIP_R:P_HIP_L,k=s?P_KNEE_R:P_KNEE_L,a=s?P_ANK_R:P_ANK_L;
        V3 r=p[k]-p[h];float rr=dot(r,r);
        if(share<=0||rr<1e-6f)return;
        float w=dot(cross(r,v[k]-v[h]),axis)/rr-dot(pelvis,axis);
        if(w<=0)return;
        for(int n:{k,a})v[n]=v[n]-cross(axis*(w*share),p[n]-p[h]);
    }
    // share of how fast b closes on a out of b's speed (a length's stop).
    void slowClosing(V3 v[DOLL_POINTS],int a,int b,float share,std::initializer_list<int> also={}) const {
        V3 u=unit(p[b]-p[a]);float c=dot(v[b]-v[a],u);
        if(share<=0||c>=0)return;
        v[b]=v[b]-u*(c*share);
        for(int n:also)v[n]=v[n]-u*(c*share);
    }
    // The zones only brake a joint, one side of it: whatever push or turn of the whole body that took
    // out (or put in) is handed back to every point as one rigid motion, so a stop never turns the body
    // (braked on one side alone, legs swept side-on into a wall twisted up to 530 degrees).
    void soften(V3 v[DOLL_POINTS]) const {
        V3 was[DOLL_POINTS],change[DOLL_POINTS],c,vc,spin;
        std::copy(v,v+DOLL_POINTS,was);
        softenJoints(v);
        for(int n=0;n<DOLL_POINTS;++n)change[n]=v[n]-was[n];
        rigid(change,c,vc,spin);
        for(int n=0;n<DOLL_POINTS;++n)v[n]=v[n]-vc-cross(spin,p[n]-c);
    }
    void softenJoints(V3 v[DOLL_POINTS]) const {
        for(int s=0;s<2;++s) {   // the spine's bend: the neck kept spineLeast (90%) of its straight distance from each hip
            int hip=s?P_HIP_R:P_HIP_L;float rel=length(p[P_NECK]-p[hip])/rest(P_NECK,hip);
            slowClosing(v,hip,P_NECK,0.5f*zoneShare(rel-spineLeast,spineZone),{P_HEAD,P_SH_L,P_SH_R});
        }
        V3 n=pelvisFront(),up=unit(p[P_SPINE]-hips()),side=unit(p[P_HIP_R]-p[P_HIP_L]),pelvis=pelvisSpin(v);
        for(int s=0;s<2;++s) {
            if(legOff[s])continue;   // (a leg shot off has nothing to hold)
            int h=s?P_HIP_R:P_HIP_L,k=s?P_KNEE_R:P_KNEE_L,a=s?P_ANK_R:P_ANK_L;
            V3 d=unit(p[k]-p[h]);
            // Back: no further than 30 degrees behind the hips.
            V3 ahead=n-d*dot(n,d);
            if(length(ahead)>1e-3f)
                slowLeg(v,s,unit(cross(unit(ahead),d)),zoneShare(std::asin(std::clamp(dot(d,n),-1.0f,1.0f))+0.5236f,limitZone),pelvis);
            // Forward: flexed no further than 110 degrees toward the chest.
            V3 toChest=up-d*dot(up,d);
            if(length(toChest)>1e-3f)
                slowLeg(v,s,unit(cross(d,unit(toChest))),zoneShare(std::acos(std::clamp(dot(d,up),-1.0f,1.0f))-1.2238f,limitZone),pelvis);
            // The knee straightening: its stop is the straight leg (the hinge's mirror stays the backstop).
            V3 thigh=p[k]-p[h],shin=p[a]-p[k];
            float tt=dot(thigh,thigh),ss=dot(shin,shin);
            if(tt>1e-6f&&ss>1e-6f) {
                float bend=std::asin(std::clamp(dot(cross(unit(thigh),unit(shin)),side),-1.0f,1.0f));
                float opening=dot(cross(shin,v[a]-v[k]),side)/ss-dot(cross(thigh,v[k]-v[h]),side)/tt;
                if(opening<0)v[a]=v[a]-cross(side*(opening*zoneShare(bend,limitZone)),shin);
            }
            // The fold: a leg never tighter than 55% of its length.
            float full=rest(h,k)+rest(k,a);
            slowClosing(v,h,a,zoneShare(length(p[a]-p[h])/full-0.55f,foldZone));
        }
    }
    // Pull the body k of the way toward a key shape fitted rigidly onto it: no net push, no net
    // turn. Only keySpring of each pull is kept as speed (kept whole, the strings rang: the chest
    // thrown back, sprung forward, the gun rocking); a point on the floor takes only the pull's
    // height (moved outright, a planted foot skated and the body inched along).
    void shapeMatch(const DollPose &shape,float k) {
        V3 c{},q0{};
        for(int n=0;n<DOLL_POINTS;++n) {c=c+p[n];q0=q0+shape[n];}
        c=c*(1.0f/DOLL_POINTS);q0=q0*(1.0f/DOLL_POINTS);
        float a[3][3]={},r[3][3];
        for(int n=0;n<DOLL_POINTS;++n) {
            V3 d=p[n]-c,q=shape[n]-q0;
            float dv[3]={d.x,d.y,d.z},qv[3]={q.x,q.y,q.z};
            for(int i=0;i<3;++i)for(int j=0;j<3;++j)a[i][j]+=dv[i]*qv[j];
        }
        if(!rotationOf(a,r))return;
        for(int n=0;n<DOLL_POINTS;++n) {
            V3 q=shape[n]-q0;
            V3 goal=c+V3{r[0][0]*q.x+r[0][1]*q.y+r[0][2]*q.z,r[1][0]*q.x+r[1][1]*q.y+r[1][2]*q.z,r[2][0]*q.x+r[2][1]*q.y+r[2][2]*q.z};
            V3 pull=(goal-p[n])*k;
            if(up(n)<=radius[n]+0.05f)pull={0,0,pull.z};
            p[n]=p[n]+pull;prev[n]=prev[n]+pull*(1-keySpring);
        }
    }
    // The floor: bodies land dead, no bounce. Each point is a ball against the grid's cells (solid
    // below their floor): a top pushes it up, a step's face sideways, an edge out from the edge, and
    // edges over a drop are rounded (dropEdge) as a thigh resting on the corner would be (only points
    // collide), so a body hanging over a drop hangs clear of the face and its weight drags the rest
    // over (by centres alone it lay in the face and hung by its knees). A point that came at a step's
    // face from the side, below the top, stops there first: a ball's push would lift it onto the step.
    // How deep a floor's top pushed back is kept for the friction (rub); an edge is slick.
    void ground(int n) {
        float r=radius[n],side=mirror?-1.0f:1.0f,depth=0;V3 sideways{};
        V3 q=p[n];const V3 was=prev[n];
        float g=floorAt(q);
        if(q.z<g&&g-floorAt(was)>r&&was.z<g+r) {q.x=was.x;q.y=was.y;}
        for(int round=0;round<3;++round) {       // the deepest cell each round
            float wx,wy;overGrid(q,wx,wy);
            float best=0;V3 way{};
            int i0=(int)std::floor((wx-r)/floorCell),i1=(int)std::floor((wx+r)/floorCell);
            int j0=(int)std::floor((wy-r)/floorCell),j1=(int)std::floor((wy+r)/floorCell);
            for(int j=j0;j<=j1;++j)for(int i=i0;i<=i1;++i) {
                float top=cellFloor(i,j);
                if(q.z-r>=top)continue;
                auto lower=[&](int di,int dj){return cellFloor(i+di,j+dj)<top-0.5f?dropEdge:0.0f;};
                float cut[4]={lower(-1,0),lower(1,0),lower(0,-1),lower(0,1)};
                float e=cut[0]+cut[1]+cut[2]+cut[3]>0?dropEdge:0;
                float cx=std::clamp(wx,i*floorCell+cut[0],(i+1)*floorCell-cut[1]),cy=std::clamp(wy,j*floorCell+cut[2],(j+1)*floorCell-cut[3]);
                V3 d{wx-cx,wy-cy,q.z-std::min(q.z,top-e)};
                float dist=length(d),amount;V3 dir;
                if(dist<1e-6f) {                                       // inside the cell: up onto it,
                    dir={0,0,1};amount=top+r-q.z;
                    // or, deep under its top, out through its nearest edge over lower floor: it is
                    // against a step's face (lifted onto the step, a body pushed into one climbed it,
                    // 25 units up a pillar's side in the arena, and flew)
                    // Or out through an edge over lower floor whenever that is the shorter way: a foot
                    // pulled down at a pedestal's lip by the body hanging off it was lifted back onto the
                    // top every substep, a hook the body hung from head down (the user, MAP01).
                    float exits[4]={wx-i*floorCell,(i+1)*floorCell-wx,wy-j*floorCell,(j+1)*floorCell-wy};
                    static const float ways[4][2]={{-1,0},{1,0},{0,-1},{0,1}};
                    int pick=-1;
                    for(int k=0;k<4;++k)if(cut[k]>0&&(pick<0||exits[k]<exits[pick]))pick=k;
                    if(pick>=0&&(amount>r+faceDepth||exits[pick]+r<amount)) {dir={ways[pick][0],ways[pick][1],0};amount=exits[pick]+r;}
                }
                else if(dist<r+e) {dir=d*(1/dist);amount=r+e-dist;}
                else continue;
                if(amount>best) {best=amount;way=dir;}
            }
            if(best<=0)break;
            V3 push=V3{side*(way.x*ax[0]+way.y*ax[1]),way.x*ay[0]+way.y*ay[1],way.z}*best;   // back into the body's frame
            q=q+push;sideways=sideways+V3{push.x,push.y,0};
            if(way.z>0.999f)depth+=way.z*best;
        }
        p[n]=q;
        if(depth>0) {pressed[n]+=depth;prev[n].z=std::max(prev[n].z,q.z);}
        // Out of a step's face or off its edge: no speed from the push (moved alone, a point pushed a
        // sphere's width out of a face in a substep left at up to 16 units a tic: bodies flung off
        // steps, 1.8 a tic gained in a tic against the arena's), and none left going into it.
        if(length(sideways)>1e-6f) {
            V3 out=unit(sideways);
            prev[n]=prev[n]+sideways;
            float into=dot(p[n]-prev[n],out);
            if(into<0)prev[n]=prev[n]+out*into;
        }
    }
    // Contact friction, once a substep (Mueller et al. 2007): a point the floor pushed back up by
    // depth in all keeps none of its sideways motion this substep if that is under floorStick*depth,
    // else loses floorSlide*depth of it. A hard landing stops it dead; a leg resting under its own
    // weight slides when the body hanging off it pulls (a fixed share of speed lost per substep,
    // however lightly a point rested, held bodies hanging off ledges).
    // Walls rub too: a point against one loses wallSlide of its skid along it a substep (frictionless,
    // a body hitting a wall at a slant skidded along it and barrel-rolled, 91 in 144 over half a turn).
    void rub() {
        for(int n=0;n<DOLL_POINTS;++n) {
            V3 out=walled[n];
            if(out.x!=0||out.y!=0) {
                V3 v=p[n]-prev[n],along=v-out*dot(v,out);along.z=0;
                p[n]=p[n]-along*wallSlide;
            }
        }
        float body=(teeter>0?std::max(teeterLeast,1-teeterGrip*teeter):1.0f)*(rolling?rollGripShare():overing?overGrip:1.0f);
        for(int n=0;n<DOLL_POINTS;++n) {
            float depth=pressed[n];
            if(depth<=0)continue;
            float grip=iced[n]?body*iceGrip:body;
            float dx=p[n].x-prev[n].x,dy=p[n].y-prev[n].y,slide=std::hypot(dx,dy);
            if(slide<=grip*floorStick*depth) {p[n].x=prev[n].x;p[n].y=prev[n].y;}
            else if(slide>0) {float keep=std::max(0.0f,1-grip*floorSlide*depth/slide);p[n].x=prev[n].x+dx*keep;p[n].y=prev[n].y+dy*keep;}
        }
    }
    // Damping (Mueller et al. 2006, 3.5): until the torso has been down lyingGrace tics only the
    // body's motion inside itself, its flail, is damped; its flight and its turn as one thing are
    // kept, so a throw carries until something stops it (damped whole it lost two thirds of its speed
    // in half a second of flight) and the slap of a landing reaches the head. Down a while: every
    // point on the floor damped hard, so it stops soon; a limb still in the air falls (damped too,
    // legs left up sank for most of a second).
    // The body moving as one rigid thing: its middle c, the middle's speed vc and its spin (radians
    // a substep about c), from the points' speeds v. Without the points that only ride it: what is taken
    // from them is lost when they're put back, so taking a spin out of the whole pushed what was left of
    // the body along (the top of a body cut in half sped up in the air, 8 to 14 units a tic, and rose
    // 20 units off the floor it had landed on).
    void rigid(const V3 v[DOLL_POINTS],V3 &c,V3 &vc,V3 &spin) const {
        c=vc=spin=V3{};
        int own=0;
        for(int n=0;n<DOLL_POINTS;++n)if(!rider(n)) {c=c+p[n];vc=vc+v[n];++own;}
        if(!own)return;
        c=c*(1.0f/own);vc=vc*(1.0f/own);
        V3 turning{},inertia[3]={};             // its angular momentum, and its inertia's columns
        for(int n=0;n<DOLL_POINTS;++n) {
            if(rider(n))continue;
            V3 r=p[n]-c;float rr=dot(r,r);
            turning=turning+cross(r,v[n]);
            inertia[0]=inertia[0]+V3{rr-r.x*r.x,-r.y*r.x,-r.z*r.x};
            inertia[1]=inertia[1]+V3{-r.x*r.y,rr-r.y*r.y,-r.z*r.y};
            inertia[2]=inertia[2]+V3{-r.x*r.z,-r.y*r.z,rr-r.z*r.z};
        }
        auto det=[](V3 a,V3 b,V3 c){return dot(a,cross(b,c));};
        float d=det(inertia[0],inertia[1],inertia[2]);
        if(d>1e-3f)spin=V3{det(turning,inertia[1],inertia[2]),det(inertia[0],turning,inertia[2]),det(inertia[0],inertia[1],turning)}*(1/d);
    }
    // Rolling down stairs or going over a low wall (ragdoll_moves.cpp): the turn past flat is the point.
    bool turningOver() const {return rolling||overing;}
    void damp(V3 v[DOLL_POINTS]) const {
        V3 c,vc,spin;rigid(v,c,vc,spin);
        // Past flat and still going over: that tumble eased out fast. Its spin kept whole, a body
        // tipped forward (shot from behind) dived on past flat and came down head first, upside down
        // (an imp: tipped -0.86 at the landing); and sliding in head first, its shoulders caught on
        // the floor and its legs flipped up over its head (a barrel roll, hips 29 up). Turning toward
        // flat is left alone.
        // Thrown by an explosion it somersaults on while well off the floor (blastFreeAbove): eased out, it
        // stopped turning the tic it passed flat and fell the rest of the way flat as a board (rocket,
        // tumble 0.17 -> 0.05 a tic, then 13 tics flat in the air). Near the floor the easing tidies its landing.
        V3 axis=unit(p[P_NECK]-hips());
        bool freeFlight=blasted&&!landed&&up(P_SPINE)>blastFreeAbove;
        if(axis.z<0.1f&&cross(spin,axis).z<0&&!turningOver()&&!freeFlight) {
            V3 over=V3{spin.x,spin.y,0}*(1-overKeep);
            spin=spin-over;
            for(int n=0;n<DOLL_POINTS;++n)v[n]=v[n]-cross(over,p[n]-c);
        }
        // Spin about its own length eased out (rollKeep a substep). Straightening in the air, the doll
        // spun up like a skater pulling the arms in (3 to 17 degrees a tic off a wall, nothing touching
        // it): it has no arms to fling out yet, nor flesh to drag. A Hollywood pirouette is left to spin.
        // (Nor a body shot out of the air: it keeps the roll it was in, ragdoll.cpp.)
        if(!rides&&launchTwist==0&&!airKill&&!turningOver()&&shock<=0) {
            V3 roll=axis*(dot(spin,axis)*(1-rollKeep));
            spin=spin-roll;
            for(int n=0;n<DOLL_POINTS;++n)v[n]=v[n]-cross(roll,p[n]-c);
        }
        if(heavy) {
            for(int n=0;n<DOLL_POINTS;++n)v[n]=v[n]*(p[n].z-floorAt(p[n])<radius[n]+1.5f?iced[n]?iceKeep:0.9f:0.985f);
            return;
        }
        float flail=rides?phobosFlail:0.985f;
        for(int n=0;n<DOLL_POINTS;++n) {V3 whole=vc+cross(spin,p[n]-c);v[n]=whole+(v[n]-whole)*flail;}
    }
    void tic(const float muscle[MUSCLES],const std::vector<Wall> &near,const DollPose *shape=nullptr,float k=0) {
        const float dt=1.0f/dollSubsteps;
        clawTic();
        float kSub=1-std::pow(1-std::min(k,0.99f),1.0f/dollSubsteps);
        for(int sub=0;sub<dollSubsteps;++sub) {
            V3 v[DOLL_POINTS];
            for(int n=0;n<DOLL_POINTS;++n)v[n]=p[n]-prev[n];
            damp(v);
            if(softLimits()&&!halved)soften(v);
            if(slamAge>=0)pin(v);
            const DollPose begin=p;
            for(int n=0;n<DOLL_POINTS;++n) {prev[n]=p[n];p[n]=p[n]+v[n]+V3{0,0,-gravity*dt*dt};}
            if(shape&&kSub>0)shapeMatch(*shape,kSub);
            std::fill(std::begin(pressed),std::end(pressed),0.0f);
            std::fill(std::begin(walled),std::end(walled),V3{});
            for(int pass=0;pass<dollPasses;++pass) {
                auto kept=[&](const Link &l){return (!headless||(l.a!=P_HEAD&&l.b!=P_HEAD))&&!rider(l.a)&&!rider(l.b);};
                for(const Link &l:bones)if(kept(l))link(l.a,l.b,rest(l.a,l.b),1);
                for(const Link &l:soft)if(kept(l))link(l.a,l.b,rest(l.a,l.b),0.15f);
                for(int g=0;g<MUSCLES;++g)if(muscle[g]>0)for(size_t i=0;i<muscles[g].size();++i) {
                    const Link &l=muscles[g][i];
                    if(kept(l))link(l.a,l.b,muscleRest(g,i),muscle[g]);
                }
                if(hipHold>0&&!halved)for(int knee:{P_KNEE_L,P_KNEE_R})if(!rider(knee))link(P_SPINE,knee,rest(P_SPINE,knee),hipHold);
                if(!halved||lowerHalf)limits();
                for(int n=0;n<DOLL_POINTS;++n) {
                    if(rider(n))continue;
                    ground(n);
                    for(const Wall &w:near) {
                        if(p[n].z>=w.top)continue;
                        float ex=w.bx-w.ax,ey=w.by-w.ay,len2=std::max(1e-6f,ex*ex+ey*ey);
                        float along=((p[n].x-w.ax)*ex+(p[n].y-w.ay)*ey)/len2,t=std::clamp(along,0.0f,1.0f);
                        // Back to the side it was on when the substep began: pushed out to the nearer
                        // side, a point carried through the line came out beyond it, the body lay
                        // across the wall and its bones flung it. The side from where the point was
                        // (begin), not prev: the limits turn a leg's prev with it, and a knee or ankle
                        // they carried through the line passed for one that had always been beyond,
                        // was held there and tore the body (12% of 189 arena wall falls).
                        float was=ex*(begin[n].y-w.ay)-ey*(begin[n].x-w.ax),now=ex*(p[n].y-w.ay)-ey*(p[n].x-w.ax);
                        if(begin[n].z<w.top&&along>0&&along<1&&std::abs(was)>1e-4f&&(was>0)!=(now>0)) {
                            float l=std::sqrt(len2);
                            V3 normal=V3{-ey/l,ex/l,0}*(was>0?1.0f:-1.0f);
                            V3 push=normal*(radius[n]-((p[n].x-w.ax)*normal.x+(p[n].y-w.ay)*normal.y));
                            p[n]=p[n]+push;prev[n]=prev[n]+push;walled[n]=normal;if(wallAt<0)wallAt=tics;
                            float into=dot(p[n]-prev[n],normal);++wallTouches;
                            if(into<0) {prev[n]=prev[n]+normal*(into*(1+rebound()));struck(n,-into,normal,w);}
                            if(!overing)prev[n].z=std::max(prev[n].z,p[n].z);   // rough: nothing slides up it (below)
                            continue;
                        }
                        float qx=p[n].x-(w.ax+ex*t),qy=p[n].y-(w.ay+ey*t),dist=std::sqrt(qx*qx+qy*qy);
                        if(dist>=radius[n]||dist<1e-4f)continue;
                        V3 out={qx/dist,qy/dist,0};
                        // Dead, like the floor: no speed from the push (it threw a body back off a wall it
                        // died against as fast as it came, 83 in 144 arena falls gaining over a unit a tic).
                        p[n]=p[n]+out*(radius[n]-dist);prev[n]=prev[n]+out*(radius[n]-dist);
                        walled[n]=out;if(wallAt<0)wallAt=tics;
                        float into=dot(p[n]-prev[n],out);++wallTouches;
                        if(into<0) {prev[n]=prev[n]+out*(into*(1+rebound()));struck(n,-into,out,w);}   // and stops going in
                        // Walls are rough: a point pressed to one does not slide up it. A body slamming
                        // in upright, still stiff, ran up the wall like a ladder whose foot slides in,
                        // and was thrown 40 units up (the arena's wall and pillar).
                        if(!overing)prev[n].z=std::max(prev[n].z,p[n].z);
                    }
                }
            }
            rub();
            if(headless)unhead();
            if(halved) {if(lowerHalf)unbody();else unleg();}
            if(legOff[0]||legOff[1])ridLegs();
            swingArms(muscle[M_TRUNK]);
        }
    }
    // The arms, after the body each substep: carried by the shoulders, they push nothing back (the fall is
    // the body's). While the trunk is held they keep to the drawing's pose in the chest's frame, a share
    // of the way a substep; let go they swing and flop: gravity, the floor under them (gripping it), the
    // elbow folding but never through, the upper arm only a little behind the back. No walls: an arm
    // through one is a sprite through one. The legs' doll carries them as riders (unbody).
    // A hit on the body while it still falls (gore's corpse shots): the dance, see danceKick.
    void dance(int point,float dx,float dy) {
        if(lowerHalf||resting||lyingAt>=0||tics>=danceTics)return;
        float side=mirror?-1.0f:1.0f;
        V3 way{side*(dx*ax[0]+dy*ax[1]),dx*ay[0]+dy*ay[1],0};
        if(point<DOLL_POINTS)pushInside({{point,way*danceKick}});
        else pushInside({{point==P_ELB_L||point==P_HAND_L?P_SH_L:P_SH_R,way*danceKick}});
        for(int n=0;n<DOLL_POINTS;++n)push(n,way*(danceKick*danceDrift));
        danceHold=std::max(danceHold,std::max(0.0f,::doom3d::danceHold-danceTire*danceHits));
        ++danceHits;
        shockHit(*this,point,way);
    }
    // Gore, the top half of a body cut in half lying on the floor (Comic beats, PLAN-gore.md I): a few
    // tics after it comes down it reaches ahead, the way its head points, grips the floor and drags
    // itself after its hands, once or twice, weaker each time, then stops.
    void clawTic() {
        clawReach=false;clawPull=0;
        if(!halved||lowerHalf||clawCycles<=0||lyingAt<0)return;
        if(clawStart<0)clawStart=tics+3+(int)(5*hashUnit(7,1,goreSeed));
        int t=tics-clawStart,cycle=t/clawTics,in=t%clawTics;
        if(t<0||cycle>=clawCycles)return;
        V3 ahead=p[P_NECK]-p[P_SPINE];ahead.z=0;
        if(length(ahead)<0.5f)return;
        clawWay=unit(ahead);
        if(in<clawReachTics) {clawReach=true;return;}
        clawPull=clawDrag*(1-0.35f*cycle)*(1-(float)(in-clawReachTics)/(clawTics-clawReachTics));
        for(int n=0;n<DOLL_POINTS;++n)if(!rider(n))push(n,clawWay*clawPull+V3{0,0,n==P_NECK||n==P_HEAD?0.15f:0});
    }
    void swingArms(float trunk) {
        if(lowerHalf)return;
        const float dt=1.0f/dollSubsteps;
        auto frame=[](const V3 *q,V3 e[3]) {
            e[0]=unit(q[P_SH_R]-q[P_SH_L]);V3 up=q[P_NECK]-q[P_SPINE];
            e[1]=unit(up-e[0]*dot(up,e[0]));e[2]=cross(e[0],e[1]);
        };
        V3 r[3],now[3];frame(body->rest.data(),r);frame(p.data(),now);
        float tone=armTone*std::clamp(trunk,0.0f,1.0f);
        for(int s=0;s<2;++s) {
            int sh=s?P_SH_R:P_SH_L,el=s?P_ELB_R:P_ELB_L,hd=s?P_HAND_R:P_HAND_L;
            float upper=rest(sh,el),fore=rest(el,hd);
            if(clawPull>0) {   // gripping: the hand stays where it holds, the elbow up between it and the shoulder
                prev[hd]=p[hd];
                V3 mid=middle(p[sh],p[hd])+V3{0,0,0.4f*upper};
                prev[el]=p[el];p[el]=p[el]+(mid-p[el])*0.5f;
                continue;
            }
            // The soft zones: each arm point's speed against the shoulder's, braked toward its stops.
            V3 vs=p[sh]-prev[sh],rv[2]={p[el]-prev[el]-vs,p[hd]-prev[hd]-vs};
            if(softLimits()) {
                float whole=upper+fore,reach=length(p[hd]-p[sh]);
                V3 u=unit(p[hd]-p[sh]);float c=dot(rv[1],u);
                if(c<0)rv[1]=rv[1]-u*(c*zoneShare(reach/whole-armFold,armFoldZone));        // folding in
                else rv[1]=rv[1]-u*(c*zoneShare(1-reach/whole,armStraightZone));            // straightening
                float back=-dot(p[el]-p[sh],now[2]),going=-dot(rv[0],now[2]);
                if(going>0) {
                    float share=zoneShare(armBack*upper-back,armBackZone*upper);
                    for(V3 &w:rv)w=w+now[2]*(going*share);
                }
            }
            for(int i=0;i<2;++i) {
                int n=i?hd:el;
                // Lying (heavy), damped as the body is, harder against the floor.
                float keep=up(n)<armRadius+0.5f?armFloorKeep:armKeep;
                if(heavy)keep*=up(n)<armRadius+1.5f?0.9f:0.985f;
                V3 v=(vs+rv[i])*keep;
                prev[n]=p[n];p[n]=p[n]+v+V3{0,0,-gravity*dt*dt};
                if(clawReach) {   // reaching ahead along the floor, the elbow raised
                    V3 hand=p[sh]+clawWay*(0.9f*(upper+fore));hand.z=floorAt(hand)+armRadius;
                    V3 target=n==hd?hand:middle(p[sh],hand)+V3{0,0,0.5f*upper};
                    p[n]=p[n]+(target-p[n])*0.35f;
                } else if(tone>0) {
                    V3 d=body->rest[n]-body->rest[sh];
                    V3 held=p[sh]+now[0]*dot(d,r[0])+now[1]*dot(d,r[1])+now[2]*dot(d,r[2]);
                    p[n]=p[n]+(held-p[n])*tone;
                }
            }
            for(int pass=0;pass<2;++pass) {
                p[el]=p[sh]+unit(p[el]-p[sh])*upper;
                V3 fw=p[hd]-p[el];p[hd]=p[el]+unit(fw)*fore;
                float reach=length(p[hd]-p[sh]),least=armFold*(upper+fore);
                // The stops are dead, as the walls: moved out, never sped up by it (a lying body's hand
                // held against one hovered and crept for most of a second).
                if(reach<least&&reach>1e-4f) {V3 to=p[sh]+(p[hd]-p[sh])*(least/reach);prev[hd]=prev[hd]+(to-p[hd]);p[hd]=to;}
                float back=-dot(p[el]-p[sh],now[2]);   // now[2] faces front
                if(back>armBack*upper) {V3 out=now[2]*(back-armBack*upper);p[el]=p[el]+out;prev[el]=prev[el]+out;}
                for(int n:{el,hd}) {
                    float floor=floorAt(p[n])+armRadius;
                    if(p[n].z<floor) {p[n].z=floor;prev[n].z=std::max(prev[n].z,floor);}
                }
            }
        }
    }
    float rebound() const {return blasted&&!landed?blastRebound:0;}
    // Across a drop: the share of its weight at points over floor at least teeterDrop under the highest
    // floor it touches, and well off it (a knee raised over flat floor doesn't count); 0 if it touches
    // nothing (flying, or fallen off and falling).
    float overDrop() const {
        float floor[DOLL_POINTS],top=-1e9f,share=0;
        for(int n=0;n<DOLL_POINTS;++n) {
            floor[n]=floorAt(p[n]);
            if(!rider(n)&&p[n].z-floor[n]<radius[n]+2)top=std::max(top,floor[n]);
        }
        if(top<-1e8f)return 0;
        for(int n=0;n<DOLL_POINTS;++n)if(!rider(n)&&floor[n]<top-teeterDrop&&p[n].z-floor[n]>radius[n]+teeterOff)share+=weight[n];
        return share;
    }
    // Headless: the head's point carried on along the neck where the head would be, weightless (nothing
    // links it), so the head's part still has a frame for the torn neck's corners that half follow it.
    void unhead() {
        float l=rest(P_HEAD,P_NECK);
        p[P_HEAD]=p[P_NECK]+unit(p[P_NECK]-p[P_SPINE])*l;prev[P_HEAD]=prev[P_NECK]+unit(prev[P_NECK]-prev[P_SPINE])*l;
    }
    // Halved: the points below the waist (hips, knees, ankles) carried as they stood in the chest's own
    // frame (shoulder line, neck over the spine), weightless: nothing links them, nothing they touch
    // stops them. The pelvis and legs' parts then move with the chest, so the texels over the cut that
    // half follow the pelvis go with the torso. (A rigid fit can't do it: the rest pose is flat.)
    static bool lower(int n) {return n>=P_HIP_L&&n<DOLL_POINTS;}
    // Cut in half, the points that only ride along: the legs' on the upper half, the rest on the legs.
    bool rider(int n) const {return (halved&&(lowerHalf?!lower(n):lower(n)))||legRider(n);}
    // A leg shot off (legOff): its points under the cut ride the hips (ridLegs), out of the fall: nothing
    // holds that side up.
    bool legRider(int n) const {
        for(int s=0;s<2;++s)
            if(legOff[s]&&(n==(s?P_ANK_R:P_ANK_L)||(legOff[s]==2&&n==(s?P_KNEE_R:P_KNEE_L))))return true;
        return false;
    }
    void ridLegs() {
        auto frame=[](const V3 *q,V3 e[3]) {
            e[0]=unit(q[P_HIP_R]-q[P_HIP_L]);V3 up=q[P_SPINE]-middle(q[P_HIP_L],q[P_HIP_R]);
            e[1]=unit(up-e[0]*dot(up,e[0]));e[2]=cross(e[0],e[1]);
        };
        V3 r[3],now[3],was[3];frame(body->rest.data(),r);frame(p.data(),now);frame(prev.data(),was);
        for(int s=0;s<2;++s) {
            if(!legOff[s])continue;
            int from=legOff[s]==2?(s?P_HIP_R:P_HIP_L):(s?P_KNEE_R:P_KNEE_L);
            for(int n:{s?P_KNEE_R:P_KNEE_L,s?P_ANK_R:P_ANK_L}) {
                if(!legRider(n))continue;
                V3 d=body->rest[n]-body->rest[from];float a=dot(d,r[0]),b=dot(d,r[1]),c=dot(d,r[2]);
                p[n]=p[from]+now[0]*a+now[1]*b+now[2]*c;prev[n]=prev[from]+was[0]*a+was[1]*b+was[2]*c;
            }
        }
    }
    // A leg off: whether a face went with it (0), is the stump's torn rim (1), or neither (2); only: that leg.
    int legCut(const ReliefFace &face,int only=-1) const {
        float share[2]={};
        for(int k=0;k<4;++k) {
            const ReliefCorner &corner=body->corners[face.corner[k]];
            for(int j=0;j<4;++j) {
                int part=corner.part[j];
                if(part==PART_SHIN_L||(legOff[0]==2&&part==PART_THIGH_L))share[0]+=corner.w[j]*0.25f;
                if(part==PART_SHIN_R||(legOff[1]==2&&part==PART_THIGH_R))share[1]+=corner.w[j]*0.25f;
            }
        }
        int out=2;
        for(int s=0;s<2;++s)if(legOff[s]&&!legBlown[s]&&(only<0||only==s)) {if(share[s]>0.5f)return 0;if(share[s]>0.05f)out=1;}
        return out;
    }
    // The legs cut off: the upper points carried as they stood in the pelvis' own frame (the hip line,
    // the thighs' way down), weightless, so the parts over the hips have a frame for the texels under the
    // cut that half follow them.
    void unbody() {
        auto frame=[](const V3 *q,V3 &m,V3 e[3]) {
            m=middle(q[P_HIP_L],q[P_HIP_R]);V3 knees=middle(q[P_KNEE_L],q[P_KNEE_R]);
            e[0]=unit(q[P_HIP_R]-q[P_HIP_L]);V3 up=m-knees;
            e[1]=unit(up-e[0]*dot(up,e[0]));e[2]=cross(e[0],e[1]);
        };
        V3 m0,mp,mw,r[3],now[3],was[3];frame(body->rest.data(),m0,r);frame(p.data(),mp,now);frame(prev.data(),mw,was);
        for(int n=P_HEAD;n<POSE_POINTS;++n) {
            if(lower(n))continue;
            V3 d=body->rest[n]-m0;float a=dot(d,r[0]),b=dot(d,r[1]),c=dot(d,r[2]);
            p[n]=mp+now[0]*a+now[1]*b+now[2]*c;prev[n]=mw+was[0]*a+was[1]*b+was[2]*c;
        }
    }
    // The legs' doll: whether a face is over the cut (0, the upper half's), on the cut's face just under it
    // (1), or the legs' (2). The same jagged line as cut(); the gun goes with the arms.
    // An arm off: whether a face went with it (0), is the shoulder's torn rim (1), or neither (2); only:
    // that arm alone (-1 either).
    int armCut(const ReliefFace &face,int only=-1) const {
        float share[2]={};
        for(int k=0;k<4;++k) {
            const ReliefCorner &corner=body->corners[face.corner[k]];
            for(int j=0;j<4;++j) {
                int part=corner.part[j];
                if(part==PART_UPPER_L||part==PART_FORE_L)share[0]+=corner.w[j]*0.25f;
                if(part==PART_UPPER_R||part==PART_FORE_R)share[1]+=corner.w[j]*0.25f;
            }
        }
        int out=2;
        for(int s=0;s<2;++s)if(armOff[s]&&(only<0||only==s)) {if(share[s]>0.5f)return 0;if(share[s]>0.05f)out=1;}
        return out;
    }
    // An arm's share of a face: the arms go with the upper half however low their hands hang.
    float armShare(const ReliefFace &face) const {
        float arm=0;
        for(int k=0;k<4;++k) {
            const ReliefCorner &corner=body->corners[face.corner[k]];
            for(int j=0;j<4;++j)if(corner.part[j]>=PART_UPPER_L)arm+=corner.w[j]*0.25f;
        }
        return arm;
    }
    int legsCut(const ReliefFace &face) const {
        float z=0,gun=0,x=0;
        for(int k=0;k<4;++k) {
            const ReliefCorner &corner=body->corners[face.corner[k]];z+=corner.p.z*0.25f;x+=corner.p.x*0.25f;
            for(int j=0;j<4;++j)if(corner.part[j]==PART_GUN)gun+=corner.w[j]*0.25f;
        }
        gun+=armShare(face);
        if(gun>0.5f)return 0;
        float edge=cutZ+cutSlant*x+1.4f*(hashUnit(face.c,3,goreSeed)-0.5f);
        return z>=edge?0:z>=edge-1.5f?1:2;
    }
    void unleg() {
        auto frame=[](const V3 *q,V3 e[3]) {
            e[0]=unit(q[P_SH_R]-q[P_SH_L]);V3 up=q[P_NECK]-q[P_SPINE];
            e[1]=unit(up-e[0]*dot(up,e[0]));e[2]=cross(e[0],e[1]);
        };
        V3 r[3],now[3],was[3];frame(body->rest.data(),r);frame(p.data(),now);frame(prev.data(),was);
        for(int n=P_HIP_L;n<DOLL_POINTS;++n) {
            V3 d=body->rest[n]-body->rest[P_SPINE];float a=dot(d,r[0]),b=dot(d,r[1]),c=dot(d,r[2]);
            p[n]=p[P_SPINE]+now[0]*a+now[1]*b+now[2]*c;prev[n]=prev[P_SPINE]+was[0]*a+was[1]*b+was[2]*c;
        }
    }
    // Halved: whether a face is below the cut (0, it went with the legs), on its torn rim (1), or
    // neither (2). A row higher or lower per column; the gun stays with the arms that hold it.
    int cut(const ReliefFace &face) const {
        if(!halved)return 2;
        float z=0,gun=0,x=0;
        for(int k=0;k<4;++k) {
            const ReliefCorner &corner=body->corners[face.corner[k]];z+=corner.p.z*0.25f;x+=corner.p.x*0.25f;
            for(int j=0;j<4;++j)if(corner.part[j]==PART_GUN)gun+=corner.w[j]*0.25f;
        }
        gun+=armShare(face);
        if(gun>0.5f)return 2;
        float edge=cutZ+cutSlant*x+1.4f*(hashUnit(face.c,3,goreSeed)-0.5f);
        return z<edge?0:z<edge+1.5f?1:2;
    }
    // Torn: whether a face of the head is bitten out (0), on the bite's torn rim (1), or neither (2).
    // A ragged bite: each face's own reach into it varies.
    // Shot out of the corpse: whether a face went with a chunk (0), is a chunk's torn rim (1), or neither
    // (2). Ragged as the skull's bite: each face's own reach into it varies. only: that bite alone.
    int bitten(const ReliefFace &face,int only=-1) const {
        if(bites.empty())return 2;
        V3 c{};for(int k=0;k<4;++k)c=c+body->corners[face.corner[k]].p*0.25f;
        int out=2;
        for(int i=0;i<(int)bites.size();++i) {
            if(only>=0&&i!=only)continue;
            const Bite &b=bites[i];
            float d=std::hypot(c.x-b.x,c.z-b.z);
            if(d<b.r*(0.75f+0.5f*hashUnit(face.c,face.r+i*31,goreSeed)))return 0;
            if(d<b.r+1.5f)out=1;
        }
        return out;
    }
    int bite(const ReliefFace &face) const {
        if(!torn||!overNeck(*body,face,0.5f)||headShare(*body,face)<0.5f)return 2;
        V3 c{};for(int k=0;k<4;++k)c=c+body->corners[face.corner[k]].p*0.25f;
        float d=std::hypot(c.x-biteX,c.z-biteZ);
        if(d<biteR*(0.75f+0.5f*hashUnit(face.c,face.r,goreSeed)))return 0;
        return d<biteR+1.5f?1:2;
    }
    // A point stopped going into a wall at speed (units a substep), along out: a torso point's for a
    // slam, and any point's against a wall with its top under the neck for going over it (overStart).
    void struck(int n,float speed,V3 out,const Wall &w) {
        if(w.top<p[P_NECK].z-overHeadroom&&speed>lowHit) {lowHit=speed;lowWay=out;lowWall=w;}
        if(headless&&n==P_HEAD)return;
        if((n==P_HEAD||n==P_NECK||n==P_SH_L||n==P_SH_R||n==P_SPINE)&&speed>hitWall) {hitWall=speed;hitWay=out;hitPoint=n;}
    }
    // The wall slam, film's way: a body thrown into a wall back or chest first, still up, sticks
    // and sinks down it into a slump instead of bouncing off. While pinned (slamAge tics in, up to
    // slamTics) its torso keeps no speed away from the wall, little along it, none up; it hangs
    // slamStick tics, then slides down ever faster (a slow slide held it up most of a second and
    // it read as stuck); its legs go limp the moment it hits (ragdollView).
    void pin(V3 v[DOLL_POINTS]) const {
        float slide=(slamAge<slamStick?0.1f:std::min(3.0f,0.1f+0.4f*(slamAge-slamStick)))/dollSubsteps;
        for(int n:{P_HEAD,P_NECK,P_SH_L,P_SH_R,P_SPINE}) {
            V3 &w=v[n];
            float away=dot(w,slamWay);
            if(away>0)w=w-slamWay*away;
            V3 flat{w.x,w.y,0};flat=flat-slamWay*dot(flat,slamWay);
            w=w-flat*0.5f;
            w.z=std::clamp(w.z,-slide,0.0f);
        }
        // The hips too, against the wall but free to sink: with only the chest pinned the limp legs
        // swung the pelvis round under it (its turn 211 degrees a fall at a wall, 142 pinned).
        for(int n:{P_HIP_L,P_HIP_R}) {
            V3 &w=v[n];
            float away=dot(w,slamWay);
            if(away>0)w=w-slamWay*away;
            V3 flat{w.x,w.y,0};flat=flat-slamWay*dot(flat,slamWay);
            w=w-flat*0.5f;
        }
    }
    // The hit tips the whole body over about its feet the way it was struck (tip, at the hit); while
    // it falls a light hold keeps it going that way (harder the more it leans back against it), so
    // a body shot from behind goes down on its face. Once it leans past 25 degrees it is twisted
    // about its length toward a drawn side (front or back) up or toward the player, whichever is
    // the shorter turn, spread over the time left before the chest lands. Down: nothing more. A swept
    // body gets neither: twisted while it turned over, it landed on a shoulder and rolled up to sitting.
    void tip(float amount) {
        float feet=std::min(p[P_ANK_L].z,p[P_ANK_R].z);
        for(int n=0;n<DOLL_POINTS;++n)push(n,tipWay*(amount*std::max(0.0f,p[n].z-feet)/40));
    }
    void steer() {
        // The legs alone: down when the hips or knees are; nothing steers them.
        if(lowerHalf) {
            if(std::min(up(P_HIP_L),up(P_HIP_R))<4.5f||std::min(up(P_KNEE_L),up(P_KNEE_R))<3)landed=true;
            return;
        }
        if(overing)return;   // the floor under it is the wall's top: it isn't down yet (overTic)
        if(up(P_SPINE)<6||up(P_NECK)<5||(!headless&&up(P_HEAD)<radius[P_HEAD]+0.5f)||(slamAt>=0&&seated()))landed=true;
        // Slammed into a wall: no more steering, ever (once the pin let go, the hold leaned the slumped
        // body back into the wall every tic until it sprang off it rolling, up to 4 units a tic).
        // On Phobos nothing steers a body: it goes as struck. Nor a top whose legs were knocked out from
        // under it (Gore, legsFly): it drops where it stood.
        if(landed||slamAt>=0||overAt>=0||rides||legsFly)return;
        V3 mid=hips(),axis=unit(p[P_NECK]-mid);
        if(up(P_SPINE)>=7&&axis.z>0.5f&&!swept) {
            V3 lean=p[P_NECK]-mid,flat=unit(V3{lean.x,lean.y,0});
            float against=std::abs(lean.x)+std::abs(lean.y)>1e-3f?std::max(0.0f,-dot(flat,tipWay)):0.0f;
            tip(0.03f+0.2f*against);
        }
        if(up(P_SPINE)<14||axis.z>0.906f||swept||launchTwist!=0)return;   // a pirouette is left to spin
        auto flat=[&](V3 v){return unit(v-axis*dot(v,axis));};
        V3 n=flat(front());
        float best=0;bool any=false;
        for(V3 want:{V3{0,0,1},V3{0,0,-1},toEye,toEye*-1}) {
            if(length(want-axis*dot(want,axis))<0.3f)continue;   // along the body: no side to turn toward
            V3 w=flat(want);
            float angle=std::atan2(dot(axis,cross(n,w)),dot(n,w));
            if(!any||std::abs(angle)<std::abs(best)) {best=angle;any=true;}
        }
        if(std::abs(best)>2.6f&&way)best=std::abs(best)*way;     // nearly opposite: keep the way chosen
        way=best>=0?1:-1;
        float left=std::sqrt(2*std::max(1.0f,up(P_SPINE)-4)/dollGravity);
        float want=std::clamp(best/std::max(3.0f,left),-0.15f,0.15f),delta=(want-spin)*0.5f;
        spin+=delta;
        for(int k=0;k<DOLL_POINTS;++k)push(k,cross(axis,p[k]-mid)*delta);
    }
    // A guided fall's tic t, before the doll moves: the ratchet and the knees' kick, the muscles
    // in the landing's order, and the key the strings pull toward and how hard.
    void guideBefore(int t,float muscle[MUSCLES],DollPose &shape,float &k) {
        const Guide &g=guide;
        int r0=g.flinch?flinchTics:0,give=r0+g.give;
        if(t>=r0&&t<r0+g.ratchetTics) {
            pushInside({{P_SPINE,yank},{P_NECK,yank},{P_SH_L,yank},{P_SH_R,yank},{P_HEAD,yank*0.6f}});
            if(t==r0)for(int n=0;n<DOLL_POINTS;++n) {V3 r=p[n]-p[P_SPINE];push(n,V3{-r.y,r.x,0}*yaw);}
        }
        if(t==give&&g.landing!=LAND_TOPPLE) {
            V3 f=pelvisFront();
            pushInside({{P_KNEE_L,V3{f.x,f.y,0}*kicks[0]},{P_KNEE_R,V3{f.x,f.y,0}*kicks[1]}});
        }
        auto after=[&](int at,float over){return at<0?1.0f:std::clamp(1-(t-at)/over,0.0f,1.0f);};
        auto fadeAt=[&](float from,float over){return t>from?std::clamp(1-(t-from)/over,0.0f,1.0f):1.0f;};
        if(g.landing==LAND_TOPPLE) {muscle[M_LEGS]=muscle[M_TRUNK]=after(lowAt,3);muscle[M_NECK]=0.5f*after(chestDownAt,1);}
        else if(g.landing==LAND_FLAT) {muscle[M_LEGS]=muscle[M_TRUNK]=fadeAt((float)give,2);muscle[M_NECK]=0.5f*muscle[M_LEGS];}
        else {muscle[M_LEGS]=fadeAt(give+legLate,2);muscle[M_TRUNK]=0.8f*after(hipsDownAt,3);muscle[M_NECK]=0.5f*after(chestDownAt,1);}
        if(t<r0) {shape=keyShape(body->rest,guideFlinch);k=0.5f;return;}
        int u=t-r0;
        float ramp=u>=g.ratchetTics+2?1.0f:u<g.ratchetTics?0.0f:(u-g.ratchetTics+1)/3.0f;
        const float lastKey=guideKeys[sizeof guideKeys/sizeof guideKeys[0]-1].tic;
        float height=std::clamp((up(P_SPINE)-10)/8,0.0f,1.0f)*std::clamp(1-(u-lastKey)/keyFade,0.0f,1.0f);   // and after the last key, wherever it is
        shape=keyShape(body->rest,keyAt((float)u));k=landed?0.0f:g.key*ramp*height;
    }
    void guideAfter(int t) {
        if(hipsDownAt<0&&std::min(up(P_HIP_L),up(P_HIP_R))<radius[P_HIP_L]+1)hipsDownAt=t;
        if(chestDownAt<0&&up(P_SPINE)<radius[P_SPINE]+1.5f)chestDownAt=t;
        if(lowAt<0&&up(P_SPINE)<20)lowAt=t;
    }
};

void iceTic(Ragdoll &doll);
// ragdoll_kneel.cpp: killed down on one knee, started folded as drawn; its legs' muscles eased while it unfolds.
void kneelStart(Ragdoll &doll);
void kneelTic(Ragdoll &doll,float muscle[MUSCLES]);
void rollTic(Ragdoll &doll,float muscle[MUSCLES],DollPose &shape,float &k);
void overStart(Ragdoll &doll);
void overTic(Ragdoll &doll,float muscle[MUSCLES],DollPose &shape,float &k);
void shockTic(Ragdoll &doll);
void pedalTic(Ragdoll &doll);

// ragdoll_card.cpp: its picture freed; the relief posed as the card last showed it; and where the
// body is drawn this frame (its card's place, and the floor under it).
void dropCard(const Ragdoll &doll);
const std::vector<V3> &posedCorners(const Ragdoll &doll);
void dollPlace(const Ragdoll &doll,float fraction,float &x,float &y,float &feet);

// ragdoll_gore.cpp: the head's outcome from the killing blow's pellets, its burst or flight at the
// kill, and each tic the severed head and the stump.
GoreHead goreHeadOf(Ragdoll &doll,const mobj_t &thing,const std::vector<GorePellet> *gore,float acrossShot,uint32_t seed);
bool goreHalvesOf(Ragdoll &doll,const mobj_t &thing,const std::vector<GorePellet> *gore,float acrossShot,uint32_t seed,bool force=false);
void goreLegsStart(Ragdoll &doll,const mobj_t &thing,float dx,float dy,float blow);
void goreArmsOf(Ragdoll &doll,const mobj_t &thing,const std::vector<GorePellet> *gore,float acrossShot);
void goreArmsStart(Ragdoll &doll,const mobj_t &thing,float dx,float dy,float blow,bool posed=false,int only=-1);
void goreBlastLimbsOf(Ragdoll &doll,const mobj_t &thing,float tx,float high,uint32_t seed);
// gore_legs.cpp: a leg shot off by the killing blow, thrown off, its stump pumping.
void goreLegsOf(Ragdoll &doll,const mobj_t &thing,const std::vector<GorePellet> *gore,float acrossShot);
void goreLegsOffStart(Ragdoll &doll,const mobj_t &thing,float dx,float dy,float blow);
void goreLegsOffTic(Ragdoll &doll,const mobj_t &thing,int target);
// A shot into the corpse (ragdoll.cpp finds the part): 0 nothing more, 2 the torso is done, burst it whole.
enum { CORPSE_HEAD,CORPSE_ARM_L,CORPSE_ARM_R,CORPSE_TORSO };
int goreCorpseHit(Ragdoll &doll,const mobj_t &thing,int part,int point,float dx,float dy,int tic);
void goreHeadStart(Ragdoll &doll,const mobj_t &thing,GoreHead outcome,float dx,float dy,float hitU,float hitZ,float tx,float blow,bool posed=false);
void goreTic(Ragdoll &doll,const mobj_t &thing,int target);
// gore_blow.cpp: at the kill, after the rules, then once the doll has its floors; each tic; for the card.
void goreBlowOf(Ragdoll &doll,const mobj_t &thing,const std::vector<GorePellet> *gore,float acrossShot,float tx,float ty,float high,bool blast);
void goreBlowMore(float more);   // the next goreBlowOf's bites reach this much further (1: as the rules have them)
void goreBlowStart(Ragdoll &doll,const mobj_t &thing,float dx,float dy,float blowSpeed);
void goreBlowTic(Ragdoll &doll,const mobj_t &thing,int target);
// A body point where its picture draws it now (the shown pose where the card puts it), in the world; point -2:
// the hips' middle (ragdoll_card.cpp).
void ragdollDrawnPoint(const Ragdoll &doll,int point,float fraction,float &x,float &y,float &z);
// tear_fibres.cpp (PLAN-tear-fibres.md): a part torn off held to its stump by fibres a few tics. The stump: a
// point of a doll; the part: a flying piece, or a point of another doll (the legs thrown off). owner: the doll
// whose going takes the tear with it. deferred: the stump's first gush and the tear's sound wait for the last
// fibre (at way, gush its size); else they were had at the cut. radius: the stump's across, where fibres hold.
enum TearKind { TEAR_HEAD,TEAR_ARM,TEAR_LEG,TEAR_HALVES,TEAR_BLOWN,TEAR_CHUNK,TEAR_KINDS };
struct TearStart {
    const Ragdoll *owner;const Ragdoll *stumpDoll;int stumpPoint;std::shared_ptr<FlyingPiece> piece;
    const Ragdoll *partDoll;int partPoint;int kind;uint32_t seed;int tint;bool bleeds;bool deferred;float radius;
    float wayX,wayY,gush;
};
void tearStart(const TearStart &start);
void tearsForget(const Ragdoll &doll);   // its tears go with it
void tearsMoved(const Ragdoll &from,const Ragdoll &to);   // made on one doll, kept as another
int goreBlowState(const Ragdoll &doll,size_t texel);
bool goreBlowSoft(const Ragdoll &doll);
V3 goreBlowOffset(const Ragdoll &doll,V3 rest);
bool goreBlowCard(const Ragdoll &doll,RagdollCard &card);
void goreBlowDrop(const Ragdoll &doll);
// body_ball.cpp: a ball of flesh's tics, in place of the fall; and ragdoll.cpp: its doll, made at the kill.
void ballTic(Ragdoll &doll,const mobj_t &thing,int target);
void dollBumps(Ragdoll &doll,const mobj_t &own);   // ragdoll_bumps.cpp: out of the living in its way, who flinch
void ballBlast(Ragdoll &doll,float sx,float sy,float hurt);
Ragdoll *addBallDoll(const mobj_t &thing);
}
