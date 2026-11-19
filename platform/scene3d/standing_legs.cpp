/* Gore's halves (PLAN-gore.md 11, I): the legs left standing. Cut through at
   the waist, the upper half falls as the ragdoll; the legs stay where the
   monster stood, as a picture of their own, on a small rig of the doll's own
   joints (a pelvis piece; per leg a thigh and a shin, each texel carried by its
   bone, as the procedural falls bend the drawings): they stand a few tics,
   swaying, the cut pumping up; then the knees give, one leg first, the hips
   drop unevenly, the knees going forward (the bones shortening in the picture)
   and a little out, the far foot sliding (two-bone IK on planted feet); then
   the heap slumps over toward its low side and lies there.
   Stepped in game tics, nearest texel (texel-sharp), every number from the
   kill's own seed. Drawing only. (A rigid picture tipped over about a foot fell
   like a brick.) */
#include "internal.h"

namespace doom3d {
constexpr int legsKeyBase=-0x68000000;     // image keys clear of the head bursts' and flying heads'
constexpr int canvas=64,floorRow=52;       // the picture, a pixel a unit; the floor that many rows down
constexpr float slumpGravity=0.03f;        // radians a tic a tic at a quarter turn: the heap going over
static int legsSerial=0;
enum { SEG_PELVIS,SEG_THIGH_A,SEG_SHIN_A,SEG_THIGH_B,SEG_SHIN_B,SEGS };
enum { J_HIP,J_KNEE,J_ANKLE };

struct StandingLegs {
    std::vector<byte> colour;std::vector<bool> torn;
    std::map<std::pair<int,int>,size_t> segment[SEGS];   // each bone's texels (a row past its joints: no cracks)
    float rest[2][3][2]={};               // [leg a, b][hip, knee, ankle][x, z], a the leftmost as seen
    float x=0,y=0,floor=0,top=0;
    int hold=10,crumble=10,tics=0;
    float delay[2]={},drop[2]={},shift=0,slide=0,splay=0.3f,side=1,swayPhase=0,azimuth=0;
    float lean=0,leanSpeed=0,leanMost=0.7f;bool slumping=false,down=false,splatted=false;int bounces=0;
    uint32_t seed=0;int tint=BLOOD_RED;bool bleeds=true;
    int key=0;uint32_t drawn=~0u;
};
// A bone: its rest anchor, way and length, and the same now (shorter: a knee come forward, seen end on).
struct Bone { float ax,az,ux,uz,length,bx,bz,vx,vz,now; };

// Where the knee goes for a hip and a planted ankle, thigh and shin lengths kept, bent out toward out.
static void kneeFor(const float hip[2],const float ankle[2],float thigh,float shin,float out,float knee[2]) {
    float dx=ankle[0]-hip[0],dz=ankle[1]-hip[1],d=std::clamp(std::hypot(dx,dz),std::abs(thigh-shin)+0.01f,thigh+shin-0.01f);
    float l=std::max(1e-3f,std::hypot(dx,dz));dx/=l;dz/=l;
    float along=(thigh*thigh-shin*shin+d*d)/(2*d),h=std::sqrt(std::max(0.0f,thigh*thigh-along*along));
    float px=-dz,pz=dx;
    if(px*out<0) {px=-px;pz=-pz;}
    knee[0]=hip[0]+dx*along+px*h;knee[1]=hip[1]+dz*along+pz*h;
}
// The pose this tic: the joints now, per leg, and the heap's lean.
static void pose(const StandingLegs &l,float joint[2][3][2]) {
    float t=(float)l.tics;
    float sway=t<l.hold?std::round(std::sin(t*0.9f+l.swayPhase)*std::max(0.0f,1-t/l.hold)):0;
    float give[2];
    for(int g=0;g<2;++g) {float u=std::clamp((t-l.hold-l.delay[g])/l.crumble,0.0f,1.0f);give[g]=u*u;}   // a buckle gives suddenly, faster and faster
    float both=0.5f*(give[0]+give[1]);
    for(int g=0;g<2;++g) {
        const float (*r)[2]=l.rest[g];
        float hip[2]={r[J_HIP][0]+sway+l.side*l.shift*both,r[J_HIP][1]*(1-l.drop[g]*give[g])};
        bool far=(g==0)!=(l.side<0);   // the leg away from the side it goes down on slides out
        float ankle[2]={r[J_ANKLE][0]+(far?-l.side*l.slide*give[g]:0),r[J_ANKLE][1]};
        float thigh=std::hypot(r[J_KNEE][0]-r[J_HIP][0],r[J_KNEE][1]-r[J_HIP][1]);
        float shin=std::hypot(r[J_ANKLE][0]-r[J_KNEE][0],r[J_ANKLE][1]-r[J_KNEE][1]);
        // The knee mostly forward, out of the picture (the bones shorten), a little out to the side: all
        // the way out the legs bowed into a ring.
        float out[2];kneeFor(hip,ankle,thigh,shin,g==0?-1.0f:1.0f,out);
        float t2=thigh/(thigh+shin),knee[2];
        for(int k=0;k<2;++k) {float straight=hip[k]+(ankle[k]-hip[k])*t2;knee[k]=straight+(out[k]-straight)*l.splay;}
        for(int k=0;k<2;++k) {joint[g][J_HIP][k]=hip[k];joint[g][J_KNEE][k]=knee[k];joint[g][J_ANKLE][k]=ankle[k];}
    }
}
static Bone boneOf(const float a0[2],const float b0[2],const float a1[2],const float b1[2]) {
    float l0=std::max(1e-3f,std::hypot(b0[0]-a0[0],b0[1]-a0[1])),l1=std::max(1e-3f,std::hypot(b1[0]-a1[0],b1[1]-a1[1]));
    return {a0[0],a0[1],(b0[0]-a0[0])/l0,(b0[1]-a0[1])/l0,l0,a1[0],a1[1],(b1[0]-a1[0])/l1,(b1[1]-a1[1])/l1,l1};
}
// A point now back to where it was at rest on the bone: along it stretched back, across it as it is.
static void unbone(const Bone &b,float x,float z,float &rx,float &rz) {
    float dx=x-b.bx,dz=z-b.bz,along=(dx*b.vx+dz*b.vz)*b.length/b.now,across=-dx*b.vz+dz*b.vx;
    rx=b.ax+b.ux*along-b.uz*across;rz=b.az+b.uz*along+b.ux*across;
}

std::shared_ptr<StandingLegs> startStandingLegs(const std::vector<HeadTexel> &legs,const StandingLegsStart &start) {
    if(legs.size()<8)return nullptr;
    auto standing=std::make_shared<StandingLegs>();
    StandingLegs &l=*standing;
    // Leg a the leftmost as seen.
    int a=start.joints[0][0]<=start.joints[1][0]?0:1;
    for(int g=0;g<2;++g)for(int j=0;j<3;++j)for(int k=0;k<2;++k)l.rest[g][j][k]=start.joints[2*j+(g==0?a:1-a)][k];
    float split=0.5f*(l.rest[0][J_HIP][0]+l.rest[1][J_HIP][0]);
    float hipZ=0.5f*(l.rest[0][J_HIP][1]+l.rest[1][J_HIP][1]);
    std::set<std::pair<int,int>> all;
    for(const HeadTexel &t:legs)all.insert({t.x,t.z});
    for(size_t i=0;i<legs.size();++i) {
        const HeadTexel &t=legs[i];
        l.colour.push_back(t.colour);l.top=std::max(l.top,(float)t.z+1);
        float z=t.z+0.5f;int g=t.x+0.5f<split?0:1;
        float knee=l.rest[g][J_KNEE][1];
        if(z>=hipZ-1)l.segment[SEG_PELVIS][{t.x,t.z}]=i;
        if(z<=hipZ+1.5f&&z>=knee-1)l.segment[g?SEG_THIGH_B:SEG_THIGH_A][{t.x,t.z}]=i;
        if(z<=knee+1)l.segment[g?SEG_SHIN_B:SEG_SHIN_A][{t.x,t.z}]=i;
    }
    for(const HeadTexel &t:legs)l.torn.push_back(!all.count({t.x,t.z+1})&&t.z>l.top*0.6f);   // the cut, over it
    l.x=start.x;l.y=start.y;l.floor=start.floor;l.seed=start.seed;l.tint=start.tint;l.bleeds=start.bleeds;
    uint32_t s=start.seed;
    auto rnd=[&] {s=hash32(s+0x9e3779b9u);return (float)(s>>8)/16777216.0f;};
    l.side=rnd()<0.5f?-1.0f:1.0f;
    l.hold=8+(int)(rnd()*9);l.crumble=8+(int)(rnd()*7);
    // The low side's leg gives first and furthest.
    int low=l.side<0?0:1;
    l.delay[low]=0;l.delay[1-low]=1+rnd()*4;
    l.drop[low]=0.68f+0.12f*rnd();l.drop[1-low]=0.45f+0.15f*rnd();
    l.shift=3+2*rnd();l.slide=rnd()*3;l.splay=0.2f+0.2f*rnd();l.leanMost=0.4f+0.5f*rnd();
    l.swayPhase=rnd()*2*doomPi;l.azimuth=rnd()*2*doomPi;
    return standing;
}

void standingLegsTic(StandingLegs &l,int tic) {
    while(l.tics<tic&&!(l.down&&l.tics>400)) {
        ++l.tics;
        float done=l.hold+std::max(l.delay[0],l.delay[1])+l.crumble;
        if(!l.splatted&&l.tics>=done) {
            l.splatted=true;   // the hips hit the floor
            if(l.bleeds)sprayLanding(l.x,l.y,l.floor,2,l.tint);
        }
        if(!l.slumping&&l.tics>=done-2) {l.slumping=true;l.lean=0.05f;l.leanSpeed=0.02f;}
        else if(l.slumping&&!l.down) {
            // The heap goes over toward its low side, slow and then fast, short of lying flat.
            l.leanSpeed+=slumpGravity*std::sin(l.lean);
            l.lean+=l.leanSpeed;
            if(l.lean>=l.leanMost) {
                l.lean=l.leanMost;
                if(l.leanSpeed<0.03f||l.bounces>=1) {l.down=true;l.leanSpeed=0;}
                else {l.leanSpeed=-l.leanSpeed*0.3f;++l.bounces;}
            }
        }
        // The cut pumps on the stump's beat, up while they stand, aslant as they go over.
        float strength=l.bleeds?stumpPulse(l.tics,l.seed):0,stream=l.bleeds?stumpStream(l.tics,l.seed):0;
        if(strength>0||stream>0) {
            float joint[2][3][2];pose(l,joint);
            float h=0.5f*(joint[0][J_HIP][1]+joint[1][J_HIP][1])+l.top-0.5f*(l.rest[0][J_HIP][1]+l.rest[1][J_HIP][1]);
            float tilt=std::sin(l.lean),ox=std::cos(l.azimuth)*tilt*h,oy=std::sin(l.azimuth)*tilt*h;
            sprayStump(l.x+ox,l.y+oy,l.floor+std::cos(l.lean)*h,std::cos(l.azimuth)*tilt,std::sin(l.azimuth)*tilt,std::cos(l.lean),strength,l.tint,stream);
        }
    }
}

void dropStandingLegs(StandingLegs &l) {
    if(!l.key)return;
    images.erase(l.key);
    for(auto *group:{&batches,&mirrorBatches,&shadowBatches,&decalBatches})group->erase(l.key);
    shadowBatches.erase(-1-l.key);decalBatches.erase(-1-l.key);   // its shadows, under the inverse key (a stale one crashed the frame)
    l.key=0;
}

bool standingLegsCard(StandingLegs &l,RagdollCard &card) {
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
    float joint[2][3][2];pose(l,joint);
    int leanStep=(int)std::lround(l.lean*180/doomPi);
    uint32_t signature=(uint32_t)leanStep;
    for(int g=0;g<2;++g)for(int j=0;j<3;++j)for(int k=0;k<2;++k)signature=signature*31+(uint32_t)std::lround(joint[g][j][k]*2+1000);
    if(!l.key||signature!=l.drawn) {
        // Each bone's turn from rest: the pelvis by its hip line from the hips' middle, the thighs and
        // shins by their own lines.
        Bone bones[SEGS];
        float m0[2]={0.5f*(l.rest[0][J_HIP][0]+l.rest[1][J_HIP][0]),0.5f*(l.rest[0][J_HIP][1]+l.rest[1][J_HIP][1])};
        float m1[2]={0.5f*(joint[0][J_HIP][0]+joint[1][J_HIP][0]),0.5f*(joint[0][J_HIP][1]+joint[1][J_HIP][1])};
        // (Rigid: the hip line's turn about the hips' middle, its length kept.)
        Bone hipLine=boneOf(l.rest[0][J_HIP],l.rest[1][J_HIP],joint[0][J_HIP],joint[1][J_HIP]);
        bones[SEG_PELVIS]={m0[0],m0[1],hipLine.ux,hipLine.uz,1,m1[0],m1[1],hipLine.vx,hipLine.vz,1};
        for(int g=0;g<2;++g) {
            bones[g?SEG_THIGH_B:SEG_THIGH_A]=boneOf(l.rest[g][J_HIP],l.rest[g][J_KNEE],joint[g][J_HIP],joint[g][J_KNEE]);
            bones[g?SEG_SHIN_B:SEG_SHIN_A]=boneOf(l.rest[g][J_KNEE],l.rest[g][J_ANKLE],joint[g][J_KNEE],joint[g][J_ANKLE]);
        }
        // The lean: about the low side's foot, its outer edge.
        int low=l.side<0?0:1;
        float pivotX=joint[low][J_ANKLE][0]+l.side*2,phi=-l.side*leanStep*doomPi/180,lc=std::cos(phi),ls=std::sin(phi);
        std::vector<byte> pixels((size_t)canvas*canvas*2,0);
        static const int order[SEGS]={SEG_PELVIS,SEG_THIGH_A,SEG_THIGH_B,SEG_SHIN_A,SEG_SHIN_B};
        for(int j=0;j<canvas;++j)for(int i=0;i<canvas;++i) {
            float px=i+0.5f-canvas/2.0f,pz=floorRow-(j+0.5f);
            // The lean undone, then each bone's move, the first bone with a texel there wins.
            float dx=px-pivotX,qx=lc*dx+ls*pz+pivotX,qz=-ls*dx+lc*pz;
            if(qz<-1)continue;
            const size_t *found=nullptr;float fx=0,fz=0;
            for(int seg:order) {
                float ux,uz;unbone(bones[seg],qx,qz,ux,uz);
                auto hit=l.segment[seg].find({(int)std::floor(ux),(int)std::floor(uz)});
                if(hit!=l.segment[seg].end()) {found=&hit->second;fx=ux;fz=uz;break;}
            }
            if(!found)continue;
            byte colour=l.colour[*found];
            if(l.bleeds&&l.torn[*found])colour=bloodRemap(reds[hashUnit((int)fx,(int)fz,l.seed)<0.6f?0:1],l.tint);
            size_t p=(size_t)j*canvas+i;
            pixels[2*p]=colour;pixels[2*p+1]=255;
        }
        std::vector<byte> normals=spriteNormals(canvas,canvas,pixels.data());
        if(!l.key)l.key=legsKeyBase+(++legsSerial&0x03ffffff);
        auto image=images.find(l.key);
        if(image==images.end())images.emplace(l.key,upload(canvas,canvas,pixels.data(),0,0,nullptr,normals.data()));
        else {
            std::vector<byte> packed((size_t)canvas*canvas*4);
            for(size_t i=0;i<(size_t)canvas*canvas;++i) {packed[4*i]=pixels[2*i];packed[4*i+1]=pixels[2*i+1];packed[4*i+2]=normals[2*i];packed[4*i+3]=normals[2*i+1];}
            gpuUpdateTexture(image->second.texture,GpuFormat::RGBA8,{{0,0,canvas,canvas}},packed.data(),canvas*4);
            image->second.pixels=std::move(pixels);
        }
        l.drawn=signature;
    }
    card.key=l.key;card.size=canvas;card.half=canvas/2.0f;card.x=l.x;card.y=l.y;card.top=l.floor+floorRow;
    card.u0=0;card.v0=0;card.u1=canvas;card.v1=canvas;
    return true;
}
}
