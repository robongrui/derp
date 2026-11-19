/* Tear fibres (PLAN-tear-fibres.md 1, 3, 4): a part torn off a body (a head, an
   arm, a leg, what a blast took, the halves) stays joined to the stump for a few
   tics by crude fibres that stretch, thin and snap one by one, the thin ones
   first; the last to go lets the stump gush and the tear be heard (the bone's
   crack is heard at the cut, before them). Each snapped fibre whips back to a
   tag left hanging from both ends. Made for slow motion (the user: most of it is
   only seen there): a fibre is a formula of its two ends and its age, worked out
   every frame at the tic and its fraction, so nothing steps on tics at a quarter
   speed; only the snaps and the release happen on tics. Thicker in slow motion,
   as the spray is: more fibres, beads of blood sliding down the long ones. Drawn
   as chains of whole Doom pixels, as the spray's fast drops are. Drawing only. */
#include "ragdoll_doll.h"
extern "C" {
#include "../audio/impact_synth.h"
#include "../audio/sound_options.h"
}

extern "C" double I_TimeScale;

namespace doom3d {
namespace {
constexpr size_t tearsMost=24;         // tears kept (their tags hanging), the oldest let go first
constexpr int releaseLeast=2;          // the stump's gush and the tear's sound no sooner than this many tics
constexpr int fibresLast=11;           // and by this tic every fibre has let go, stretched or not

struct Fibre {
    float across[2],up[2];             // where it holds at each end (the stump's, the part's), off its middle
    uint32_t texel=0;                  // on a flying piece: which of its torn texels it holds
    float snapAt=10,breakAt=0.5f;      // the length it lets go at, and where along it
    int lastTic=fibresLast;            // the tic it lets go at whatever its length
    float thick=1;                     // texels across at its ends
    float slack=0;                     // the gap between its ends as it was cut (it sags across that)
    std::array<float,3> albedo;
    bool beads=false;
    int snapped=-1;float snapLength=0; // the tic it let go (since the tear) and how long it was
    float tag[2]={3,3};                // the tags it leaves, units
    float whip=1;                      // the whip's side
};
struct End { const Ragdoll *doll=nullptr;int point=-1;std::weak_ptr<FlyingPiece> piece; };
struct Tear {
    const Ragdoll *owner=nullptr;End stump,part;
    int born=0,released=-1;bool deferred=true;
    float radius=2.5f;                 // the stump's across, for where its fibres hold
    float way[2]={1,0},gush=0.9f;int tint=BLOOD_RED;bool bleeds=true;
    uint32_t seed=0;
    std::vector<Fibre> fibres;
};
std::vector<Tear> tears;

float slowShare() {return std::clamp((float)(1-I_TimeScale)/0.75f,0.0f,1.0f);}
float unitHash(uint32_t &s) {s=hash32(s+0x9e3779b9u);return (float)(s>>8)/16777216.0f;}

// An end's middle in the world now (fraction: of the tic since the last). False: it is gone.
bool endAt(const End &e,float fraction,float &x,float &y,float &z) {
    if(e.doll) {ragdollDrawnPoint(*e.doll,e.point,fraction,x,y,z);return true;}
    std::shared_ptr<FlyingPiece> piece=e.piece.lock();
    return piece&&flyingPieceMiddle(*piece,fraction,x,y,z);
}
// Where one fibre holds on an end, as drawn: on a piece its own torn texel, on a body round the stump.
bool holdAt(const Tear &t,const Fibre &f,int side,float fraction,const float right[2],float out[3]) {
    const End &e=side?t.part:t.stump;
    if(!e.doll) {
        std::shared_ptr<FlyingPiece> piece=e.piece.lock();
        if(!piece)return false;
        if(flyingPieceTornAt(*piece,f.texel,fraction,right,out))return true;
        if(!flyingPieceMiddle(*piece,fraction,out[0],out[1],out[2]))return false;
    } else ragdollDrawnPoint(*e.doll,e.point,fraction,out[0],out[1],out[2]);
    out[0]+=right[0]*f.across[side];out[1]+=right[1]*f.across[side];out[2]+=f.up[side];
    return true;
}
// The stump's blood colours (ragdoll_card.cpp's rims: dark, bright, brightest, deep) in a tint, as the palette has them.
std::array<float,3> stumpColour(int which,int tint) {
    static byte rims[4];static bool found=false;
    const byte *pal=playpal();
    if(!found) {
        auto nearest=[&](int r,int g,int b) {
            int best=16,d0=INT_MAX;
            for(int i=16;i<256;++i) {int dr=pal[i*3]-r,dg=pal[i*3+1]-g,db=pal[i*3+2]-b,d=dr*dr+dg*dg+db*db;if(d<d0) {d0=d;best=i;}}
            return (byte)best;
        };
        rims[0]=nearest(30,4,4);rims[1]=nearest(140,8,8);rims[2]=nearest(196,56,48);rims[3]=nearest(84,10,8);found=true;
    }
    byte index=bloodRemap(rims[std::clamp(which,0,3)],tint);
    return {pal[index*3]/255.0f,pal[index*3+1]/255.0f,pal[index*3+2]/255.0f};
}
// A fibre's colour at (x,y,z), lit as the body it hangs from: the light there (as a chip off a wall is, no floor
// at the sector's level), and the flashlight as world.frag lights a sprite, not as a speck in the air: the
// classic beam in its eighths, lifting the light toward a little past full and never beyond, at a sprite's
// average facing; the plain beam added. Specks take the beam onto their colour up to white, and in it the
// fibres stood out from the bodies they hung off (the user).
std::array<float,3> fibreLit(const std::array<float,3> &albedo,float x,float y,float z,const Uniforms &camera) {
    const sector_t &sector=*R_PointInSubsector((fixed_t)(x*FRACUNIT),(fixed_t)(y*FRACUNIT))->sector;
    std::array<float,3> light=gridLightActive()?bakedLightAt(x,y,z,&sector):weatherColor(sector,x,y,z,{1,1,1},false);
    if(flashlightOn&&flashes.count&&flashes.lights[0].direction[3]>0&&camera.effects[2]==0) {
        const Flash &beam=flashes.lights[0];
        float amount=flashAt(x,y,z,beam);
        if(amount>0) {
            constexpr float facing=0.4f+0.6f*0.5f,ceiling=1.2f;
            float steps=std::floor(std::clamp(amount,0.0f,1.0f)*8+0.5f)/8;
            for(int c=0;c<3;++c)
                light[c]+=settings.classicFlashlight?beam.color[c]*steps*facing*std::max(0.0f,ceiling-light[c]):beam.color[c]*amount*facing;
        }
    }
    // A sprite's texel times its light, nothing more (a chip's spill into the channels it lacks greyed the red).
    std::array<float,3> lit;
    for(int c=0;c<3;++c)lit[c]=albedo[c]*light[c];
    return lit;
}
float gap(const Tear &t,float fraction) {
    float a[3],b[3];
    if(!endAt(t.stump,fraction,a[0],a[1],a[2])||!endAt(t.part,fraction,b[0],b[1],b[2]))return 1e9f;
    return std::sqrt((a[0]-b[0])*(a[0]-b[0])+(a[1]-b[1])*(a[1]-b[1])+(a[2]-b[2])*(a[2]-b[2]));
}
}

void tearStart(const TearStart &start) {
    if(!goreOn())return;
    while(tears.size()>=tearsMost)tears.erase(tears.begin());
    Tear t;
    t.owner=start.owner;t.stump.doll=start.stumpDoll;t.stump.point=start.stumpPoint;
    if(start.partDoll) {t.part.doll=start.partDoll;t.part.point=start.partPoint;}
    else t.part.piece=start.piece;
    t.born=leveltime;t.deferred=start.deferred;t.radius=start.radius;
    t.way[0]=start.wayX;t.way[1]=start.wayY;t.gush=start.gush;t.tint=start.tint;t.bleeds=start.bleeds;t.seed=start.seed;
    // How many, and how long the last holds (the user: gruesome sounds good), by what tore; thicker in slow motion.
    struct Kind { int least,most,slowMore;float holdLeast,holdMost; };
    // Lengths are the whole fibre as seen, the gap the part starts at included (the user: a bit too stretchy
    // sometimes; counted past that gap, a head's last stood near 40 units).
    static const Kind kinds[]={{5,7,4,12,20},{4,6,3,9,15},{4,6,3,9,15},{6,9,5,16,24},{1,3,1,5,9},{0,1,1,3,6}};
    const Kind &k=kinds[std::clamp(start.kind,0,TEAR_KINDS-1)];
    uint32_t s=start.seed^0x7f4a7c15u;
    int count=0;
    if(settings.tearFibres&&(start.piece||start.partDoll)) {
        float slow=slowShare();
        count=k.least+(int)((k.most-k.least+1)*unitHash(s)*0.999f)+(int)std::lround(k.slowMore*slow*(0.6f+0.4f*unitHash(s)));
    }
    float hold=k.holdLeast+(k.holdMost-k.holdLeast)*unitHash(s);
    float tornGap=gap(t,1);if(tornGap>1e8f)tornGap=0;
    for(int n=0;n<count;++n) {
        Fibre f;
        // Thin ones first: the n-th of them lets go at its share of the hold, a little either way, the last at it.
        float share=count>1?(n+0.4f*unitHash(s))/(count-1):1;
        f.snapAt=n==count-1?hold:4+(hold-4)*std::pow(std::min(1.0f,share),1.3f)*(0.85f+0.15f*unitHash(s));
        f.lastTic=std::min(fibresLast,3+(int)((fibresLast-3)*(n+1)/(float)count)+(int)(2*unitHash(s)));
        f.breakAt=0.3f+0.4f*unitHash(s);
        f.slack=tornGap;
        f.snapAt=std::max(f.snapAt,tornGap+2+2*unitHash(s));   // never gone before the part has left at all
        f.thick=start.kind==TEAR_HALVES&&n>=count-2?3:n>=count*0.6f?2:1;
        for(int side=0;side<2;++side) {
            float a=unitHash(s)*2*doomPi,r=start.radius*std::sqrt(unitHash(s));
            f.across[side]=r*std::cos(a);f.up[side]=r*std::sin(a)*0.8f;
            f.tag[side]=2+4*unitHash(s);
        }
        f.texel=hash32(s+=0x632be5abu);
        f.whip=unitHash(s)<0.5f?-1.0f:1.0f;
        // The stump's own colours (ragdoll_card.cpp's rims, in the monster's blood): mostly its bright red, some
        // deep and dark, one in five its brightest (the sinew). bloodColor's greys came out a bright grey once lit
        // like the body (the user).
        float pick=unitHash(s);
        f.albedo=stumpColour(pick<0.2f?2:pick<0.6f?1:pick<0.85f?3:0,start.tint);
        f.beads=start.bleeds&&f.snapAt>=10&&unitHash(s)<0.3f+0.5f*slowShare();
        t.fibres.push_back(f);
    }
    tears.push_back(std::move(t));
}

void tearsForget(const Ragdoll &doll) {
    tears.erase(std::remove_if(tears.begin(),tears.end(),[&](const Tear &t){return t.owner==&doll;}),tears.end());
}
void tearsMoved(const Ragdoll &from,const Ragdoll &to) {
    for(Tear &t:tears) {
        if(t.owner==&from)t.owner=&to;
        if(t.stump.doll==&from)t.stump.doll=&to;
        if(t.part.doll==&from)t.part.doll=&to;
    }
}
void tearsShutdown() {tears.clear();}

// Whether the part torn off is down (or gone): then its fibres and their tags go with it (the user: once it has
// landed they can go, easier on the frame).
static bool partDown(const Tear &t) {
    if(t.part.doll)return t.part.doll->lying();   // (legs standing on the floor at the cut are landed already)
    std::shared_ptr<FlyingPiece> piece=t.part.piece.lock();
    return !piece||flyingPieceDown(*piece);
}
// The stump's gush and the tear heard, once: the last fibre gone, or the part down before it.
static void release(Tear &t,int age) {
    t.released=age;
    float sx,sy,sz;
    if(t.deferred&&endAt(t.stump,1,sx,sy,sz)) {
        goreSound(GORE_SOUND_TEAR,sx,sy,sz);
        if(t.bleeds)sprayGush(sx,sy,sz,t.way[0],t.way[1],t.gush,t.tint);
    }
}

// Each game tic: the fibres stretched past their length let go (heard, a speck or two thrown where they broke),
// the taut ones creak, and the last to go lets the stump gush. A tear whose part is down goes.
void watchTears() {
    if(tears.empty())return;
    for(Tear &t:tears) {
        if(t.released>=0)continue;
        int age=leveltime-t.born;
        float length=gap(t,1);
        bool snaps=false,taut=false,holding=false;
        float sx,sy,sz;bool stump=endAt(t.stump,1,sx,sy,sz);
        for(Fibre &f:t.fibres) {
            if(f.snapped>=0)continue;
            float stretch=length;
            if(length>1e8f||stretch>=f.snapAt||age>=f.lastTic) {
                f.snapped=age;f.snapLength=std::min(length>1e8f?f.snapAt:length,f.snapAt*1.2f);snaps=true;
                float bx,by,bz,px,py,pz;
                if(stump&&t.bleeds&&endAt(t.part,1,px,py,pz)) {
                    bx=sx+(px-sx)*f.breakAt;by=sy+(py-sy)*f.breakAt;bz=sz+(pz-sz)*f.breakAt;
                    sprayTrail(bx,by,bz,t.tint);
                }
            } else {holding=true;taut=taut||stretch>0.5f*f.snapAt;}
        }
        if(stump&&snaps)I_MadeSound(IMPACT_GORE_SNAP,sx,sy,sz);
        else if(stump&&taut)I_MadeSound(IMPACT_GORE_STRETCH,sx,sy,sz);
        if(!holding&&age>=releaseLeast)release(t,age);
    }
    tears.erase(std::remove_if(tears.begin(),tears.end(),[](Tear &t) {
        if(!partDown(t))return false;
        if(t.released<0)release(t,leveltime-t.born);
        return true;
    }),tears.end());
}

// The fibres as they are this frame: whole Doom pixels along each, its sag and its thinning, or, let go, its two
// halves whipping back to the tags they leave.
void buildTearFibres(const Uniforms &camera) {
    if(tears.empty())return;
    float rl=std::max(1e-3f,std::hypot(camera.right[0],camera.right[1])),right[2]={camera.right[0]/rl,camera.right[1]/rl};
    for(const Tear &t:tears) {
        if(t.fibres.empty())continue;
        float sx,sy,sz;
        if(!endAt(t.stump,renderFraction,sx,sy,sz)||!thingInView(sx,sy))continue;
        float age=(float)(leveltime-t.born)+renderFraction;
        for(const Fibre &f:t.fibres) {
            float a[3],b[3];
            bool hasA=holdAt(t,f,0,renderFraction,right,a),hasB=holdAt(t,f,1,renderFraction,right,b);
            if(!hasA)continue;
            // Lit by the light where it hangs, as the body is (the dry rule): the wet rule's floor at the sector's
            // level is for drops gone in a moment; held there, fibres and tags glowed in the dark (the user).
            // Each end where it is (a head flown into a dark corner goes dark), blended along between them.
            std::array<float,3> litA=fibreLit(f.albedo,a[0],a[1],a[2],camera),litB=hasB?fibreLit(f.albedo,b[0],b[1],b[2],camera):litA;
            auto dot=[&](float x,float y,float z,float size,float stipple,float toB) {
                std::array<float,3> lit;
                for(int c=0;c<3;++c)lit[c]=litA[c]+(litB[c]-litA[c])*toB;
                pixelBlock(particleVertices,camera,x,y,z,size,1,lit,false,nullptr,true,nullptr,1,stipple,farLeast);
            };
            // A run of pixels from p along a curve to q, sagging by sag in its middle, size from s0 to s1.
            auto run=[&](const float *p,const float *q,float sag,float bend,float s0,float s1,float neckAt,float neck,float b0,float b1) {
                float dx=q[0]-p[0],dy=q[1]-p[1],dz=q[2]-p[2],l=std::sqrt(dx*dx+dy*dy+dz*dz);
                int steps=std::clamp((int)std::ceil(l/0.8f),1,48);
                for(int k=0;k<=steps;++k) {
                    float u=k/(float)steps,droop=4*sag*u*(1-u),side=4*bend*u*(1-u);
                    float size=s0+(s1-s0)*u;
                    // Necking: thinner toward where it will break, see-through at the thinnest.
                    float near=1-std::min(1.0f,std::abs(u-neckAt)*4);
                    size=std::max(1.0f,size*(1-neck*near));
                    float stipple=1-0.45f*neck*near;
                    dot(p[0]+dx*u+right[0]*side,p[1]+dy*u+right[1]*side,p[2]+dz*u-droop,size,stipple,b0+(b1-b0)*u);
                }
            };
            if(f.snapped<0||age<f.snapped) {
                if(!hasB)continue;
                float dx=b[0]-a[0],dy=b[1]-a[1],dz=b[2]-a[2],l=std::sqrt(dx*dx+dy*dy+dz*dz);
                float tension=std::clamp((l-f.slack)/std::max(1.0f,f.snapAt-f.slack),0.0f,1.0f);
                float sag=0.25f*std::max(l,f.snapAt*0.4f)*std::pow(1-tension,1.5f);
                float neck=tension>0.6f?(tension-0.6f)/0.4f:0;
                run(a,b,sag,0,f.thick,f.thick,f.breakAt,neck*(f.thick>1?0.7f:0.4f),0,1);
                // Beads: a drop or two sliding down toward the lowest of it.
                if(f.beads&&l>=10) {
                    for(int n=0;n<2;++n) {
                        float from=n?0.15f:0.85f,u=from+(0.5f-from)*(1-std::exp(-age/(6.0f+3*n)));
                        dot(a[0]+dx*u,a[1]+dy*u,a[2]+dz*u-4*sag*u*(1-u),1.6f,1,u);
                    }
                }
                continue;
            }
            // Let go: each half springs back from where it broke (a damped swing, past and back), turning
            // down to hang, its tip swinging aside, and is a tag, still, a few tics on.
            float since=age-f.snapped;
            float settle=1-std::exp(-since/2.0f),swing=std::exp(-since/1.5f)*std::sin(since*2.6f);
            for(int side=0;side<2;++side) {
                const float *from=side?b:a;
                if(side&&!hasB)continue;
                float l0=(side?1-f.breakAt:f.breakAt)*f.snapLength,tag=f.tag[side];
                float len=std::max(1.0f,tag+(l0-tag)*std::exp(-since/1.2f)*std::cos(since*1.9f));
                float way[3]={0,0,-1};
                if(hasB) {
                    float dx=(side?a[0]-b[0]:b[0]-a[0]),dy=(side?a[1]-b[1]:b[1]-a[1]),dz=(side?a[2]-b[2]:b[2]-a[2]);
                    float dl=std::max(1e-3f,std::sqrt(dx*dx+dy*dy+dz*dz));
                    way[0]=dx/dl*(1-settle);way[1]=dy/dl*(1-settle);way[2]=dz/dl*(1-settle)-settle;
                    float wl=std::max(1e-3f,std::sqrt(way[0]*way[0]+way[1]*way[1]+way[2]*way[2]));
                    for(float &w:way)w/=wl;
                }
                float tip[3]={from[0]+way[0]*len,from[1]+way[1]*len,from[2]+way[2]*len};
                run(from,tip,0,f.whip*swing*len*0.35f,f.thick,1,1,0,(float)side,(float)side);
            }
        }
    }
}
}
