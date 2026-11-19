/* Gore's flying pieces (PLAN-gore.md M, B): a part cut off whole, the head by
   the lucky shot through the neck, the legs by a blast low across the waist
   (the halves' mirror). It leaves the body as a small picture of its own:
   thrown off along the shot, spinning end over end, its front and back taking
   turns every half turn, bouncing off the floors and walls the map really has
   (the body's own floor grid is freed once the body lies still, the piece may
   still be flying), trailing blood from the cut, splatting where it lands,
   rolling out and lying still for good in a small pool. Stepped in game tics like the body,
   its turn drawn in 22.5-degree steps (texel-sharp, no smooth rotation), its
   place blended between tics like any thing. Drawing only. */
#include "internal.h"

namespace doom3d {
constexpr int pieceKeyBase=-0x64000000;   // image keys clear of the head bursts' (-0x60000000) and the rest
constexpr float pieceGravity=0.45f;        // units a tic a tic: the ragdoll's own (dollGravity)
constexpr float pieceFloorBounce=0.35f,pieceFloorGrip=0.6f,pieceWallBounce=0.45f,pieceRollGrip=0.85f;
constexpr int turnSteps=16;               // the turn drawn in 22.5-degree steps
// Doom's pixels are 1.2 tall (scene.cpp): the piece's rows are that much longer than its texels, as a ragdoll's
// are (Relief::tall). Turned, it keeps its length; its picture is squashed back, the world stretching it again.
constexpr float pieceTall=1.2f;
// Meat doesn't bounce (a head does, it has a skull in it): it slaps down dead, and small enough it sticks where it
// hits, slides down a wall leaving a run, lets go of a ceiling a moment later. Past meatStickMost texels it is too
// heavy to hold.
constexpr float meatFloorBounce=0.12f,meatFloorGrip=0.4f,meatWallBounce=0.15f,meatStickFrom=1.0f,meatSmearFrom=1.0f;
constexpr size_t meatStickMost=260;constexpr int meatHoldLeast=10,meatHoldMost=50;
constexpr float meatSlide=0.25f,meatSmearEvery=7,pieceSink=0.25f;
static int pieceSerial=0;
// The budget (PLAN-gore.md L): at most this many pieces, flying or lying, per level; a new one retires
// the oldest (its picture freed, drawn no more). Lying ones cost nothing but their small picture.
constexpr size_t maxFlyingPieces=64;   // a burst caco leaves five or six
static std::deque<FlyingPiece*> pieces;
static bool piecesDriven=false;            // watchFlyingPieces stepping the kicked ones (their own clock)

struct FlyingPiece {
    std::vector<int> x,z;std::vector<byte> front,back,bone;std::vector<bool> torn;bool boned=false;   // bone: its torn end's (stumpPieceBone)
    std::map<std::pair<int,int>,size_t> at;
    float px=0,py=0,pz=0,vx=0,vy=0,vz=0,wasX=0,wasY=0,wasZ=0;   // its middle in the world, units a tic
    float angle=0,spin=0,radius=4,pool=16;
    float wide=1,tall=1;                  // its texels' span across and up, as drawn
    int canvas=32;                        // its square picture, a pixel a unit, the piece's middle in the middle
    bool cutOnTop=false;float bones[2]={0,1e9f};   // the cut over it (legs) or under it (a head); its bones' dots
    int tics=0,still=0;bool resting=false,landed=false;
    uint32_t seed=0;int tint=BLOOD_RED;bool bleeds=true;const GutsPatch *guts=nullptr;
    int key=0,drawnStep=-1;bool drawnBack=false;
    int drawnBox[4]={0,0,0,0};            // the drawn picture's opaque bounds (its shadow's size)
    bool retired=false;                   // gone for the budget's sake
    bool pooled=false;                    // its pool laid (once, however often it's kicked on)
    bool woke=false;                      // thrown again (a blast, a kick): its clock picks up where it is now
    bool head=false;                      // a severed head: its neck streams as it spins, painting what it passes
    int stuck=0,stuckFor=0;               // meat: 1 on a wall, 2 on a ceiling, and for how many tics more
    WallHit wall;float wallX=0,wallY=0;   // the wall it's on, its way out from it
    float smeared=0;                      // slid since its last smear or run
    bool sinking=false;float sunk=0;      // gone into a liquid: how far under
    const sector_t *lies=nullptr;float liesAt=0;   // the floor it came to rest on (a lift carries it) and that floor's height then
    int held=0;                           // its first tics' flight held back by its tear's fibres (tear_fibres.cpp)
    bool ownClock=false;                  // kicked, scuffed or shot: stepped each game tic (watchFlyingPieces), not by its body
    int kicks=0,kicker=-1;                // kicked how often; the player whose kick it flies from, till it hits someone
    int scuffed=-100;                     // the tic a boot last moved it
    int wokeAt=0;                         // its tics when last thrown again: the longest roll counts from there
    FlyingPiece() {pieces.push_back(this);}
    ~FlyingPiece() {pieces.erase(std::remove(pieces.begin(),pieces.end(),this),pieces.end());}
};
// How heavy it sounds (contact_sounds.cpp): 0.7 at 200 texels, where a piece stops thudding and slaps.
static float pieceHeft(const FlyingPiece &h) {return std::clamp(std::sqrt((float)h.x.size())/20,0.25f,1.2f);}
int flyingPiecesLive() {int n=0;for(const FlyingPiece *h:pieces)n+=!h->retired;return n;}

std::shared_ptr<FlyingPiece> startFlyingPiece(const std::vector<HeadTexel> &head,const FlyingPieceStart &start) {
    if(head.size()<4)return nullptr;
    while(flyingPiecesLive()>=(int)maxFlyingPieces) {
        for(FlyingPiece *old:pieces)if(!old->retired) {dropFlyingPiece(*old);old->retired=true;break;}
    }
    auto flying=std::make_shared<FlyingPiece>();
    FlyingPiece &h=*flying;
    float reach=0;
    for(size_t i=0;i<head.size();++i) {
        h.x.push_back(head[i].x);h.z.push_back(head[i].z);h.front.push_back(head[i].colour);h.back.push_back(head[i].back);
        h.bone.push_back(head[i].bone);h.boned=h.boned||head[i].bone;
        h.at[{head[i].x,head[i].z}]=i;reach=std::max(reach,std::hypot(head[i].x+0.5f,head[i].z+0.5f));
    }
    // The cut: the texels with nothing under them (a head) or over them in its upper half (legs); in it the
    // spine's dot in the middle, or a thigh bone's in each leg.
    h.cutOnTop=start.cutOnTop;h.head=!start.cutOnTop&&!start.torn;
    int lo=INT_MAX,hi=INT_MIN;
    if(start.torn)h.torn=*start.torn;   // a burst's chunk: torn where it cracked off, no bones
    else for(size_t i=0;i<head.size();++i) {
        bool torn=h.cutOnTop?!h.at.count({h.x[i],h.z[i]+1})&&h.z[i]>0:!h.at.count({h.x[i],h.z[i]-1});
        h.torn.push_back(torn);
        if(torn) {lo=std::min(lo,h.x[i]);hi=std::max(hi,h.x[i]);}
    }
    if(h.cutOnTop&&lo<=hi) {h.bones[0]=lo+0.3f*(hi-lo);h.bones[1]=lo+0.7f*(hi-lo);}
    else if(start.torn) {h.bones[0]=h.bones[1]=1e9f;}
    else {h.bones[0]=-0.5f;h.bones[1]=1e9f;}
    h.radius=std::max(3.0f,0.6f*reach);h.pool=start.pool;
    {
        int x0=INT_MAX,x1=INT_MIN,z0=INT_MAX,z1=INT_MIN;
        for(size_t i=0;i<head.size();++i) {x0=std::min(x0,head[i].x);x1=std::max(x1,head[i].x);z0=std::min(z0,head[i].z);z1=std::max(z1,head[i].z);}
        h.wide=(float)(x1-x0+1);h.tall=(float)(z1-z0+1);
    }
    h.canvas=std::clamp(((int)std::ceil(2*pieceTall*reach)+7)/4*4,16,128);   // (lying, its length runs across)
    h.px=h.wasX=start.x;h.py=h.wasY=start.y;h.pz=h.wasZ=start.z;
    h.seed=start.seed;h.tint=start.tint;h.bleeds=start.bleeds;h.guts=start.guts;
    uint32_t s=start.seed;
    auto rnd=[&] {s=hash32(s+0x9e3779b9u);return (float)(s>>8)/16777216.0f;};
    // Off the body along the shot, kicked a little aside and up, spinning one way or the other.
    float along=(2+1.5f*rnd())*start.blow,aside=(rnd()*2-1)*0.8f;
    h.vx=start.carryX+start.shotX*along-start.shotY*aside;h.vy=start.carryY+start.shotY*along+start.shotX*aside;
    h.vz=start.up>=0?start.up*(0.8f+0.4f*rnd()):3.5f+2*rnd();
    h.spin=(rnd()<0.5f?-1.0f:1.0f)*(0.25f+0.4f*rnd());
    return flying;
}

void flyingPieceTic(FlyingPiece &h,int tic) {
    if(h.retired)return;
    if(h.ownClock&&!piecesDriven)return;   // kicked about: watchFlyingPieces steps it, its body's clock no more
    if(h.woke) {h.woke=false;h.tics=std::max(h.tics,tic-1);h.wokeAt=h.tics;}
    static const auto sectorAt=[](float x,float y){return R_PointInSubsector((fixed_t)(x*FRACUNIT),(fixed_t)(y*FRACUNIT))->sector;};
    while(h.tics<tic&&!h.resting) {
        ++h.tics;
        h.wasX=h.px;h.wasY=h.py;h.wasZ=h.pz;
        // Gone into a liquid: it goes on down and is gone.
        if(h.sinking) {
            h.pz-=pieceSink;h.sunk+=pieceSink;
            if(h.sunk>=2*h.radius) {dropFlyingPiece(h);h.retired=true;return;}
            continue;
        }
        // Stuck: on a wall it creeps down leaving a run; both let go when its hold runs out.
        if(h.stuck) {
            if(h.stuck==1) {
                float creep=meatSlide*(1+0.5f*hashUnit(h.tics,3,h.seed));
                h.pz-=creep;h.smeared+=creep;
                if(h.bleeds&&h.smeared>=3) {h.smeared=0;bloodWallSplat(h.wall,h.px,h.py,h.pz+h.radius*0.5f,randomRange(7,11),1.4f,gameSeconds(),BLOOD_SPLAT,false,h.tint);}
                if(h.pz<=floorZ(sectorAt(h.px,h.py))+h.radius)h.stuckFor=0;
            }
            if(--h.stuckFor<=0) {h.stuck=0;h.vx=h.wallX*0.3f;h.vy=h.wallY*0.3f;h.vz=0;}
            continue;
        }
        h.vz-=Game_Phobos()?Game_PhobosGravity():pieceGravity;
        // Held back by its fibres its first tics (tear_fibres.cpp): it goes at a share of its speed, then free.
        float pace=h.tics<=h.held?0.4f:1.0f;
        h.vx*=pace;h.vy*=pace;h.vz*=pace;
        float fromX=h.px,fromY=h.py,fromZ=h.pz;   // a window it goes through breaks (below)
        // Walls and step faces in its way, at its middle's height: it comes back off them, slowed.
        float speed=std::hypot(h.vx,h.vy);
        if(speed>1e-3f) {
            float dx=h.vx/speed,dy=h.vy/speed;WallHit hit;
            if(firstSolidWall(h.px,h.py,h.pz,dx,dy,0,speed+h.radius,hit)) {
                const line_t &line=lines[hit.line];
                float ex=units(line.v2->x-line.v1->x),ey=units(line.v2->y-line.v1->y),el=std::max(1e-3f,std::hypot(ex,ey));
                float nx=-ey/el,ny=ex/el,into=h.vx*nx+h.vy*ny;
                float back=std::max(0.0f,hit.t-h.radius);
                h.px+=dx*back;h.py+=dy*back;
                float hitSpeed=std::abs(into),out=into>0?-1.0f:1.0f;
                bool meat=!h.head&&h.bleeds;
                h.vx=(h.vx-2*into*nx)*(meat?meatWallBounce:pieceWallBounce);h.vy=(h.vy-2*into*ny)*(meat?meatWallBounce:pieceWallBounce);h.spin*=-0.6f;
                if(meat&&hitSpeed>meatStickFrom&&h.x.size()<=meatStickMost) {
                    h.stuck=1;h.wall=hit;h.wallX=nx*out;h.wallY=ny*out;h.vx=h.vy=h.vz=0;h.spin=0;h.smeared=0;
                    h.stuckFor=meatHoldLeast+(int)((meatHoldMost-meatHoldLeast)*hashUnit(h.tics,5,h.seed));
                }
                if(h.bleeds&&std::abs(into)>1.5f)sprayWallHit(h.px,h.py,h.pz,-nx*(into>0?1:-1),-ny*(into>0?1:-1),std::abs(into)*0.5f,h.tint,pieceHeft(h));
            } else {h.px+=h.vx;h.py+=h.vy;}
        }
        h.pz+=h.vz;
        h.vx/=pace;h.vy/=pace;h.vz/=pace;
        glassBodyThrough(fromX,fromY,fromZ,h.px,h.py,h.pz,2);
        const sector_t *sector=sectorAt(h.px,h.py);
        float floor=floorZ(sector)+h.radius,ceiling=ceilZ(sector)-h.radius;
        if(h.pz>ceiling&&sector->ceilingpic!=skyflatnum) {
            if(!h.head&&h.bleeds&&h.vz>meatStickFrom&&h.x.size()<=meatStickMost) {
                h.stuck=2;h.vx=h.vy=h.vz=0;h.spin=0;h.wallX=h.wallY=0;
                h.stuckFor=meatHoldLeast+(int)((meatHoldMost-meatHoldLeast)*hashUnit(h.tics,6,h.seed));
            }
            h.pz=ceiling;h.vz=-std::abs(h.vz)*0.3f;
        }
        size_t here=sector-sectors;
        if(h.pz<=floor&&here<sectorMist.size()&&sectorMist[here].liquid&&!waterIceHolds(h.px,h.py)) {
            // Into a liquid: its splash, a cloud of what it bled, and down it goes.
            h.pz=floor;h.sinking=true;h.landed=true;
            bodySplash(h.px,h.py,-h.vz,20+0.2f*h.x.size(),h.radius,h.vx,h.vy);
            if(h.bleeds)bloodLiquidLand(h.px,h.py,(int)here,14,gameSeconds(),h.tint);
            h.vx=h.vy=h.vz=h.spin=0;
            continue;
        }
        if(h.pz<=floor) {
            h.pz=floor;
            bool meat=!h.head&&h.bleeds;
            // Sliding, it smears (bodies already do): a body's trail, in small. A kicked head too, its first
            // few kicks, while its neck still has blood in it.
            float slid=std::hypot(h.vx,h.vy);
            if((meat||(h.head&&h.bleeds&&h.kicks>0&&h.kicks<=3))&&slid>meatSmearFrom) {
                h.smeared+=slid;
                if(h.smeared>=meatSmearEvery) {h.smeared=0;spraySmear(h.px,h.py,std::atan2(h.vy,h.vx)+doomPi,std::clamp(h.radius*1.4f,6.0f,12.0f),h.tint);}
            }
            if(h.vz<-1) {
                // A bounce: a splat where it hit, the more the harder.
                // Heard by its size and how hard it came down: a small chunk thuds, a big one (the legs) slaps.
                if(-h.vz>1.5f)contactFloor(h.px,h.py,floorZ(sector),-h.vz,pieceHeft(h));
                if(h.bleeds)sprayLanding(h.px,h.py,floorZ(sector),-h.vz,h.tint,false);
                h.vz=-h.vz*(meat?meatFloorBounce:pieceFloorBounce);h.vx*=meat?meatFloorGrip:pieceFloorGrip;h.vy*=meat?meatFloorGrip:pieceFloorGrip;h.spin*=0.6f;h.landed=true;
            } else {
                // Rolling out: the turn follows the roll until it stops.
                h.vz=0;h.vx*=pieceRollGrip;h.vy*=pieceRollGrip;h.landed=true;
                float roll=std::hypot(h.vx,h.vy)/h.radius;
                h.spin=(h.spin<0?-1.0f:1.0f)*std::min(std::abs(h.spin),roll);
            }
        }
        h.angle+=h.spin;
        // A head's neck: the stump's stream, thrown out of the cut as it turns, so the jet sweeps arcs across
        // the walls while it flies (the user: a trail makes it better).
        float streaming=h.head&&h.bleeds&&!h.landed?stumpStream(h.tics,h.seed):0,beat=h.head&&h.bleeds&&!h.landed?0.7f*stumpPulse(h.tics,h.seed):0;
        if(streaming>0||beat>0) {
            float along=std::max(1e-3f,std::hypot(h.vx,h.vy)),fx=h.vx/along,fy=h.vy/along,sa=std::sin(h.angle),ca=std::cos(h.angle);
            float carry[3]={0.8f*h.vx*TICRATE,0.8f*h.vy*TICRATE,0.8f*h.vz*TICRATE};
            sprayStump(h.px,h.py,h.pz,fx*sa,fy*sa,-ca,beat,h.tint,streaming,carry);
        }
        // Blood off the cut while it flies, every other tic, thinning out: dropping and trailing behind.
        else if(h.bleeds&&!h.landed&&(h.tics&1)) {
            float tx=-0.3f*h.vx,ty=-0.3f*h.vy,tl=std::max(1e-3f,std::sqrt(tx*tx+ty*ty+1));
            // Most of the piece's own speed goes with the drops (all of it, they'd ride along with it: no trail).
            float carry[3]={0.6f*h.vx*TICRATE,0.6f*h.vy*TICRATE,0.6f*h.vz*TICRATE};
            sprayStump(h.px,h.py,h.pz,tx/tl,ty/tl,-1/tl,std::max(0.1f,0.4f-h.tics*0.02f),h.tint,0,carry);
        }
        bool still=h.landed&&h.pz<=floor+0.01f&&std::hypot(h.vx,h.vy)<0.15f;
        h.still=still?h.still+1:0;
        // (From its last throw: from the kill, a head kicked a few times lay down dead the moment it moved.)
        if(h.still>=6||h.tics-h.wokeAt>350) {
            // Down for good on its long side (the user: pieces left standing on end, long side up): a
            // piece taller than wide, as drawn, lies a quarter turn round, the nearer way; its middle
            // half its thickness lying so over the floor, not the ball it rolled as.
            h.resting=true;h.vx=h.vy=h.vz=0;h.spin=0;
            if(h.bleeds&&!h.pooled)bloodPoolAt(h.px,h.py,h.pool,3,h.tint);   // a small pool where it lies
            h.pooled=true;
            bool turned=h.tall>h.wide;
            float base=turned?doomPi/2:0;
            h.angle=base+std::round((h.angle-base)/doomPi)*doomPi;
            h.pz=floorZ(sector)+std::max(1.0f,(turned?h.wide/pieceTall:h.tall)/2);   // (its picture's rows: squashed when turned)
            h.lies=sector;h.liesAt=floorZ(sector);
            h.wasX=h.px;h.wasY=h.py;h.wasZ=h.pz;
        }
    }
}

// Thrown again (PLAN-gore.md C, drawing only: the pieces aren't the game's things): woken where it lies
// and given a speed, spinning.
static void wake(FlyingPiece &h,float vx,float vy,float vz) {
    h.resting=false;h.still=0;h.woke=true;h.stuck=0;h.lies=nullptr;h.wokeAt=h.tics;
    h.vx+=vx;h.vy+=vy;h.vz=std::max(h.vz,vz);
    h.spin+=(hashUnit(h.tics,7,h.seed)<0.5f?-1.0f:1.0f)*std::min(0.5f,std::hypot(vx,vy)/std::max(2.0f,h.radius));
}
// An explosion (R_Explosion, gore): the pieces in its reach thrown away from it, the harder the nearer.
void flyingPiecesBlast(float x,float y,float z,float damage) {
    for(FlyingPiece *h:pieces) {
        if(h->retired)continue;
        float dx=h->px-x,dy=h->py-y,d=std::hypot(dx,dy);
        if(d>=damage||std::abs(h->pz-z)>64)continue;
        float k=(damage-d)/damage,ux=d>0.5f?dx/d:1,uy=d>0.5f?dy/d:0;
        wake(*h,ux*7*k,uy*7*k,2+3*k);
    }
}
bool flyingPieceUnder(float x,float y) {
    for(const FlyingPiece *h:pieces)if(!h->retired&&h->landed&&std::hypot(x-h->px,y-h->py)<h->radius+6)return true;
    return false;
}

void dropFlyingPiece(FlyingPiece &h) {
    if(!h.key)return;
    images.erase(h.key);
    for(auto *group:{&batches,&mirrorBatches,&shadowBatches,&decalBatches})group->erase(h.key);
    shadowBatches.erase(-1-h.key);decalBatches.erase(-1-h.key);   // its shadows, under the inverse key (a stale one crashed the frame)
    h.key=0;
}

bool flyingPieceInView(const FlyingPiece &h) {return !h.retired&&thingInView(h.px,h.py);}
bool flyingPieceCard(FlyingPiece &h,float fraction,RagdollCard &card) {
    if(h.retired)return false;
    static const byte *palette=nullptr;
    static byte reds[2];
    if(!palette) {
        palette=(const byte*)W_CacheLumpName((char*)"PLAYPAL",PU_STATIC);
        auto nearest=[&](int r,int g,int b) {
            int best=0,bestDistance=INT_MAX;
            for(int i=0;i<256;++i) {int dr=palette[3*i]-r,dg=palette[3*i+1]-g,db=palette[3*i+2]-b,d=dr*dr+dg*dg+db*db;if(d<bestDistance){bestDistance=d;best=i;}}
            return (byte)best;
        };
        reds[0]=nearest(140,8,8);reds[1]=nearest(84,10,8);
    }
    // Its turn in steps, and which side shows: the front, then the back every other half turn.
    float turns=h.angle/(2*doomPi);
    int step=((int)std::lround(h.angle/(2*doomPi/turnSteps))%turnSteps+turnSteps)%turnSteps;
    bool back=((int)std::floor(turns*2+0.5f)&1)!=0;
    const int canvas=h.canvas;
    if(!h.key||step!=h.drawnStep||back!=h.drawnBack) {
        std::vector<byte> pixels((size_t)canvas*canvas*2,0);
        int box[4]={canvas,canvas,0,0};
        float a=step*2*doomPi/turnSteps,c=std::cos(a),s=std::sin(a),half=canvas/2.0f;
        for(int j=0;j<canvas;++j)for(int i=0;i<canvas;++i) {
            // Each pixel turned back into the piece's own texels (nearest, no blending: texel-sharp), its rows
            // pieceTall long: the picture's height unsquashed, turned back, the rows counted again.
            float px=i+0.5f-half,pz=(half-(j+0.5f))*pieceTall;
            float hx=c*px-s*pz,hz=(s*px+c*pz)/pieceTall;
            int tx=(int)std::floor(back?-hx:hx),tz=(int)std::floor(hz);
            auto found=h.at.find({tx,tz});
            if(found==h.at.end())continue;
            size_t k=found->second;
            byte colour=back?h.back[k]:h.front[k];
            if(h.bleeds&&h.torn[k]) {   // the cut: meat, and the bone the drawing has there (or, with no rig, pale dots)
                if(h.boned) {
                    colour=h.guts?gutsColour(*h.guts,tx,tz,h.seed):bloodRemap(reds[hashUnit(tx,tz,h.seed)<0.6f?0:1],h.tint);
                    if(h.bone[k]==1)colour=boneMapColour(BONE_SOLID);
                    else if(h.bone[k]==2)colour=bloodRemap(reds[1],h.tint);
                } else {
                    bool bone=std::abs(tx+0.5f-h.bones[0])<0.8f||std::abs(tx+0.5f-h.bones[1])<0.8f;
                    colour=bone?boneMapColour(BONE_SOLID):h.guts?gutsColour(*h.guts,tx,tz,h.seed):bloodRemap(reds[hashUnit(tx,tz,h.seed)<0.6f?0:1],h.tint);
                }
            }
            size_t p=(size_t)j*canvas+i;
            pixels[2*p]=colour;pixels[2*p+1]=255;
            box[0]=std::min(box[0],i);box[1]=std::min(box[1],j);box[2]=std::max(box[2],i+1);box[3]=std::max(box[3],j+1);
        }
        std::vector<byte> normals=spriteNormals(canvas,canvas,pixels.data());
        if(!h.key)h.key=pieceKeyBase+(++pieceSerial&0x03ffffff);
        auto found=images.find(h.key);
        if(found==images.end())images.emplace(h.key,upload(canvas,canvas,pixels.data(),0,0,nullptr,normals.data()));
        else {
            std::vector<byte> packed((size_t)canvas*canvas*4);
            for(size_t i=0;i<(size_t)canvas*canvas;++i) {packed[4*i]=pixels[2*i];packed[4*i+1]=pixels[2*i+1];packed[4*i+2]=normals[2*i];packed[4*i+3]=normals[2*i+1];}
            gpuUpdateTexture(found->second.texture,GpuFormat::RGBA8,{{0,0,canvas,canvas}},packed.data(),canvas*4);
            found->second.pixels=std::move(pixels);
        }
        h.drawnStep=step;h.drawnBack=back;
        if(box[2]>box[0])std::copy(box,box+4,h.drawnBox);
    }
    float t=h.resting?1:fraction;
    card.key=h.key;card.size=canvas;card.half=canvas/2.0f;
    card.x=h.wasX+(h.px-h.wasX)*t;card.y=h.wasY+(h.py-h.wasY)*t;card.top=h.wasZ+(h.pz-h.wasZ)*t+canvas/2.0f;
    if(h.lies)card.top+=floorZ(h.lies)-h.liesAt;   // on a lift: it goes with the floor
    card.u0=h.drawnBox[0];card.v0=h.drawnBox[1];card.u1=h.drawnBox[2];card.v1=h.drawnBox[3];card.shadow=true;
    return true;
}

// For tear_fibres.cpp: where the piece is drawn now, and where one of its torn texels is in its picture (its turn's
// step and side as flyingPieceCard draws them), the picture standing across the view.
bool flyingPieceMiddle(const FlyingPiece &h,float fraction,float &x,float &y,float &z) {
    if(h.retired)return false;
    float t=h.resting?1:fraction;
    x=h.wasX+(h.px-h.wasX)*t;y=h.wasY+(h.py-h.wasY)*t;z=h.wasZ+(h.pz-h.wasZ)*t;
    if(h.lies)z+=floorZ(h.lies)-h.liesAt;
    return true;
}
bool flyingPieceTornAt(const FlyingPiece &h,uint32_t pick,float fraction,const float right[2],float out[3]) {
    size_t count=0;
    for(bool torn:h.torn)count+=torn;
    if(!count||!flyingPieceMiddle(h,fraction,out[0],out[1],out[2]))return false;
    size_t want=pick%count,k=0;
    for(;k<h.torn.size();++k)if(h.torn[k]&&want--==0)break;
    float turns=h.angle/(2*doomPi);
    int step=((int)std::lround(h.angle/(2*doomPi/turnSteps))%turnSteps+turnSteps)%turnSteps;
    bool back=((int)std::floor(turns*2+0.5f)&1)!=0;
    float a=step*2*doomPi/turnSteps,c=std::cos(a),s=std::sin(a);
    float hx=back?-(h.x[k]+0.5f):h.x[k]+0.5f,hz=(h.z[k]+0.5f)*pieceTall;
    // flyingPieceCard turns each pixel back into the piece's texels; turned forward again here, its rows unsquashed.
    float px=c*hx+s*hz,pz=(-s*hx+c*hz)/pieceTall;
    out[0]+=right[0]*px;out[1]+=right[1]*px;out[2]+=pz;
    return true;
}
void flyingPieceHold(FlyingPiece &h,int tics) {h.held=std::max(h.held,h.tics+tics);}
bool flyingPieceDown(const FlyingPiece &h) {return h.retired||h.landed||h.stuck||h.sinking;}

// Kicked about (PLAN-tear-fibres.md 6, the user: people will want to kick it): still drawing only, the game told
// nothing but a kicked head striking a monster (P_HeadStrikes, p_kick.c: Authored, a rule of the game).
static void keepLatest(FlyingPiece *h) {
    auto it=std::find(pieces.begin(),pieces.end(),h);
    if(it!=pieces.end()) {pieces.erase(it);pieces.push_back(h);}   // played with: the last the budget retires
}
static bool movable(const FlyingPiece &h) {return !h.retired&&!h.sinking&&!h.stuck;}
// Only a head answers the boot (the user, 2026-11-19: every limb and chunk kickable was a bit much). Shots and
// blasts still move the rest.
static bool kickable(const FlyingPiece &h) {return h.head&&movable(h);}
void flyingPiecesKicked(float x,float y,float z,float angle,float reach,int player) {
    float fx=std::cos(angle),fy=std::sin(angle);
    std::vector<FlyingPiece*> hit;
    for(FlyingPiece *h:pieces) {
        if(!kickable(*h)||h->pz<z-8||h->pz>z+40)continue;
        float dx=h->px-x,dy=h->py-y,d=std::hypot(dx,dy);
        if(d>reach+h->radius||(d>2&&(dx*fx+dy*fy)/d<0.5f))continue;   // within sixty degrees of his way
        hit.push_back(h);
    }
    for(FlyingPiece *h:hit) {
        uint32_t s=hash32(h->seed^(uint32_t)leveltime*2654435761u);
        float along=7+2*hashUnit(1,0,s),aside=(hashUnit(2,0,s)*2-1)*1.2f,up=4+1.5f*hashUnit(3,0,s);
        h->vx=h->vy=h->vz=0;
        wake(*h,fx*along-fy*aside,fy*along+fx*aside,up);
        h->ownClock=true;h->kicker=player;h->scuffed=leveltime;++h->kicks;
        goreSound(GORE_SOUND_THUD,h->px,h->py,h->pz);
        if(h->bleeds&&h->kicks<=2)sprayGush(h->px,h->py,h->pz,fx,fy,0.35f,h->tint);   // what is left in the neck
        keepLatest(h);
    }
}
void flyingPiecesShot(float x1,float y1,float z1,float x2,float y2,float z2) {
    float dx=x2-x1,dy=y2-y1,dz=z2-z1,dd=dx*dx+dy*dy+dz*dz,across=std::hypot(dx,dy);
    if(dd<1||across<1e-3f||pieces.empty())return;
    float ux=dx/across,uy=dy/across;
    std::vector<FlyingPiece*> hit;
    for(FlyingPiece *h:pieces) {
        if(!movable(*h))continue;
        float t=std::clamp(((h->px-x1)*dx+(h->py-y1)*dy+(h->pz-z1)*dz)/dd,0.0f,1.0f);
        float ox=x1+dx*t-h->px,oy=y1+dy*t-h->py,oz=z1+dz*t-h->pz;
        if(ox*ox+oy*oy+oz*oz<=(h->radius*0.8f+0.5f)*(h->radius*0.8f+0.5f))hit.push_back(h);
    }
    for(FlyingPiece *h:hit) {
        // Each pellet a knock along the shot, up to a skip across the floor (a shotgun's seven don't make a rocket).
        float add=std::hypot(h->vx,h->vy)>10?0.5f:2.5f;
        wake(*h,ux*add,uy*add,1.5f+hashUnit(leveltime,4,h->seed));
        h->ownClock=true;h->scuffed=leveltime;
        if(h->bleeds)sprayCorpseHit(h->px,h->py,h->pz,ux,uy,h->tint);
        keepLatest(h);
    }
}
// Each game tic: the players' boots move what they walk into, and the kicked pieces fly on their own clock, a
// kicked one striking the first monster it meets.
void watchFlyingPieces() {
    if(pieces.empty())return;
    std::vector<FlyingPiece*> moved;
    for(int p=0;p<MAXPLAYERS;++p) {
        if(!playeringame[p]||!players[p].mo)continue;
        const mobj_t *mo=players[p].mo;
        float x=units(mo->x),y=units(mo->y),feet=units(mo->z),vx=units(mo->momx),vy=units(mo->momy),speed=std::hypot(vx,vy);
        if(speed<0.6f)continue;
        float reach=units(mo->radius);
        for(FlyingPiece *h:pieces) {
            if(!kickable(*h)||!h->landed||leveltime-h->scuffed<6||h->pz>feet+h->radius+12)continue;
            float dx=h->px-x,dy=h->py-y,d=std::hypot(dx,dy);
            if(d>reach+h->radius*0.8f||(d>0.5f&&dx*vx+dy*vy<=0))continue;   // only what he walks into
            float ax=d>0.5f?dx/d:vx/speed,ay=d>0.5f?dy/d:vy/speed;
            float tx=0.33f*vx+0.5f*ax,ty=0.33f*vy+0.5f*ay;
            wake(*h,tx-h->vx,ty-h->vy,0.6f+0.08f*speed);
            h->ownClock=true;h->scuffed=leveltime;
            if(speed>8)goreSound(GORE_SOUND_THUD,h->px,h->py,h->pz);   // run into, not nudged
            moved.push_back(h);
        }
    }
    for(FlyingPiece *h:moved)keepLatest(h);
    std::vector<FlyingPiece*> stepping;
    for(FlyingPiece *h:pieces)if(h->ownClock&&!h->retired&&!h->resting)stepping.push_back(h);
    piecesDriven=true;
    for(FlyingPiece *h:stepping) {
        flyingPieceTic(*h,h->tics+1);
        if(h->kicker<0)continue;
        float speed=std::hypot(h->vx,h->vy);
        if(h->retired||h->resting||speed<2.5f) {h->kicker=-1;continue;}
        if(speed>=4&&Game_HeadStrikes(h->px,h->py,h->pz,h->radius,h->kicker)) {
            // Off him, most of its speed gone into him; once a kick.
            h->vx*=-0.3f;h->vy*=-0.3f;h->vz=std::max(h->vz,1.0f);h->spin*=-0.5f;h->kicker=-1;
            goreSound(GORE_SOUND_THUD,h->px,h->py,h->pz);
            if(h->bleeds)sprayTrail(h->px,h->py,h->pz,h->tint);
        }
    }
    piecesDriven=false;
}
}
