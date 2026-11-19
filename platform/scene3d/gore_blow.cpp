/* Gore's blow-off (PLAN-gunplay.md): what comes off a body at the kill as a shape,
   not a menu of joints. The rules still say how much goes (goreArm, goreLeg,
   goreBlastLimbs, in gore.cpp) and the throw is untouched; this decides which
   texels leave. Each limb the rules take gets a ragged bite round where the blow
   landed on it, some small, now and then huge. Thin flesh gives first, deep flesh
   later, bone last: bone inside the bite's edge stays, stripped. What the bites
   part from the body's core flies by itself (connected through the skeleton's
   parts, not through texels that merely touch), what they eat bursts: the bigger
   pieces fly, the rest and a mist burst as a picture where it tore. The core,
   head, spine and hips, never goes: cuts through it are the halves' and the
   head's own tricks. For its first tics the flesh round the wound also rings,
   a wobble on a body already flying. Drawing only; numbers from the kill's seed. */
#include "ragdoll_doll.h"
#include "stump_rim.h"

namespace doom3d {
constexpr float blowExaggerate=1;                 // every bite's reach: the knob for "more"
static float blowMore=1;                          // and for one kill (goreBlowMore): a body shot out of the air
void goreBlowMore(float more) {blowMore=more;}
constexpr float blowBlastReach=1.25f;             // a blast's bites against a shot's
constexpr float blowGrow=1.25f;constexpr int blowTries=6;   // a bite that severs nothing is tried wider
constexpr float blowKept=0.75f;                   // a limb's end with less of it left than this has come off
constexpr float blowBoneFrom=0.6f;                // bone inside this share of the reach goes, past it stays stripped
constexpr float blowRagged=0.28f,blowFleshHolds=0.35f,blowJointGives=0.85f;
constexpr float blowHuge=13;                      // a bite of this reach (at a man's size) goes through the other limb too
constexpr float blowLegReach=0.75f;               // legs are thin: a bite this share of an arm's, so the knee often stays
constexpr float blowSlashBlast=0.2f,blowSlashGib=0.35f;   // how often the heaviest blows cut shoulder to hip
constexpr int blowSmallest=6;                     // a part cut free under this many texels is eaten with the rest
constexpr float blowSoft=0.45f,blowSoftMost=0.12f,blowWave=9;constexpr int blowSoftTics=12;   // the wobble: share of the reach, most (of the height), units a tic the shock crosses, how long

enum { BLOW_KEEP,BLOW_GONE,BLOW_RIM,BLOW_BONE };
struct BlowBite { float x,z,reach;uint32_t seed;int side=-1; };   // side: the limb it is for (0 left, 1 right; -1 none): only a huge one takes the other side's
struct BlowStump { int from,toward;uint32_t seed; };
struct GoreBlow {
    std::vector<byte> state;                       // per texel of the drawing
    std::vector<float> x,z;                        // each texel's place, as the body has it standing
    std::vector<std::vector<size_t>> loose;        // parts cut free (until they fly)
    std::vector<size_t> eaten;                     // what the bites took (until it bursts)
    std::vector<BlowStump> stumps;
    float impactX=0,impactZ=0,reach=10,tall=56;V3 way{};
    std::shared_ptr<HeadBurst> picture;float at[3]={};
    int pumped=0;
};

// Which parts hold on to which: the skeleton's joints.
static bool joined(int a,int b) {
    if(a==b||a==PART_GUN||b==PART_GUN)return true;
    if(a>b)std::swap(a,b);
    static const int pairs[][2]={{PART_PELVIS,PART_CHEST},{PART_CHEST,PART_HEAD},{PART_CHEST,PART_UPPER_L},{PART_CHEST,PART_UPPER_R},
                                 {PART_UPPER_L,PART_FORE_L},{PART_UPPER_R,PART_FORE_R},{PART_PELVIS,PART_THIGH_L},{PART_PELVIS,PART_THIGH_R},
                                 {PART_THIGH_L,PART_SHIN_L},{PART_THIGH_R,PART_SHIN_R}};
    for(const auto &p:pairs)if(p[0]==a&&p[1]==b)return true;
    return false;
}
static float toSegment(float x,float z,V3 a,V3 b) {
    float ex=b.x-a.x,ez=b.z-a.z,t=std::clamp(((x-a.x)*ex+(z-a.z)*ez)/std::max(1e-4f,ex*ex+ez*ez),0.0f,1.0f);
    return std::hypot(x-(a.x+ex*t),z-(a.z+ez*t));
}

// The body as a grid of texels: where each is, which part has it, what is under it.
struct BlowField {
    int w=0,h=0;
    std::vector<byte> present,part,core,bone;
    std::vector<float> x,z,inside;
    std::vector<byte> state;
    std::vector<std::vector<size_t>> loose;
    std::vector<size_t> eaten;
    BlowField(const Relief &body,int lump) {
        w=body.width;h=body.height;size_t n=(size_t)w*h;
        present.assign(n,0);part.assign(n,PART_CHEST);core.assign(n,0);bone.assign(n,0);x.assign(n,0);z.assign(n,0);inside.assign(n,0);
        const DollPose &rest=body.rest;
        V3 hips=middle(rest[P_HIP_L],rest[P_HIP_R]);
        float half=std::abs(rest[P_SH_R].x-rest[P_SH_L].x)/2,hold=std::max(3.0f,1.5f+0.3f*half);
        for(const ReliefFace &face:body.faces) {
            if(face.kind!=FACE_FRONT)continue;
            size_t i=(size_t)face.r*w+face.c;
            V3 c{};float share[DOLL_PARTS]={};
            for(int k=0;k<4;++k) {
                const ReliefCorner &corner=body.corners[face.corner[k]];
                c=c+corner.p*0.25f;
                for(int j=0;j<4;++j)share[corner.part[j]]+=corner.w[j]*0.25f;
            }
            present[i]=1;x[i]=c.x;z[i]=c.z;
            part[i]=(byte)(std::max_element(share,share+DOLL_PARTS)-share);
            core[i]=part[i]==PART_HEAD||part[i]==PART_GUN||
                    std::min(toSegment(c.x,c.z,rest[P_NECK],rest[P_SPINE]),toSegment(c.x,c.z,rest[P_SPINE],hips))<hold;
        }
        // How deep each texel lies inside the outline (as bone_map.cpp's).
        for(size_t i=0;i<n;++i)inside[i]=present[i]?1e4f:0;
        auto relax=[&](int c,int r,int dc,int dr,float step) {
            int c2=c+dc,r2=r+dr;float other=c2<0||r2<0||c2>=w||r2>=h?0:inside[(size_t)r2*w+c2];
            float &here=inside[(size_t)r*w+c];here=std::min(here,other+step);
        };
        for(int r=0;r<h;++r)for(int c=0;c<w;++c) {relax(c,r,-1,0,1);relax(c,r,0,-1,1);}
        for(int r=h-1;r>=0;--r)for(int c=w-1;c>=0;--c) {relax(c,r,1,0,1);relax(c,r,0,1,1);}
        // What the rig says is bone there (none for a monster without one: the flesh goes as it is).
        if(lump>=0)if(const SpriteSkeleton *s=spriteSkeleton(lumpImage(lump,false)))
            if(s->boneMap.size()==n)for(size_t i=0;i<n;++i)bone[i]=s->boneMap[i]==BONE_SOLID||s->boneMap[i]==BONE_EDGE;
    }
    // Every bite laid on the body afresh, then what still hangs on the core: the rest is cut free.
    void cut(const std::vector<BlowBite> &bites,const DollPose &rest,float scale) {
        size_t n=present.size();
        state.assign(n,BLOW_KEEP);loose.clear();eaten.clear();
        static const int joints[]={P_KNEE_L,P_KNEE_R,P_ELB_L,P_ELB_R};
        for(size_t i=0;i<n;++i) {
            if(!present[i]||core[i])continue;
            float give=1-blowFleshHolds*std::min(1.0f,inside[i]/5);   // thin flesh goes further from the blow than deep flesh
            for(int j:joints)if(std::hypot(x[i]-rest[j].x,z[i]-rest[j].z)<2.5f)give/=blowJointGives;
            bool left=part[i]==PART_THIGH_L||part[i]==PART_SHIN_L||part[i]==PART_UPPER_L||part[i]==PART_FORE_L;
            bool right=part[i]==PART_THIGH_R||part[i]==PART_SHIN_R||part[i]==PART_UPPER_R||part[i]==PART_FORE_R;
            for(const BlowBite &b:bites) {
                if(b.side>=0&&b.reach<blowHuge*scale&&(b.side?left:right))continue;
                float edge=(valueNoise(x[i]+64,z[i]+64,3,b.seed)*2-1)*blowRagged*b.reach;
                float d=(std::hypot(x[i]-b.x,z[i]-b.z)+edge)/give;
                if(d>=b.reach)continue;
                if(bone[i]&&d>blowBoneFrom*b.reach) {if(state[i]==BLOW_KEEP)state[i]=BLOW_BONE;}
                else {state[i]=BLOW_GONE;break;}
            }
            if(state[i]==BLOW_GONE)eaten.push_back(i);
        }
        std::vector<byte> held(n,0);std::vector<size_t> open;
        auto spread=[&](std::vector<byte> &seen,std::vector<size_t> &list,std::vector<size_t> *out) {
            while(!list.empty()) {
                size_t i=list.back();list.pop_back();
                if(out)out->push_back(i);
                int c=(int)(i%w),r=(int)(i/w);
                static const int ways[4][2]={{1,0},{-1,0},{0,1},{0,-1}};
                for(const auto &way:ways) {
                    int c2=c+way[0],r2=r+way[1];if(c2<0||r2<0||c2>=w||r2>=h)continue;
                    size_t j=(size_t)r2*w+c2;
                    if(!present[j]||seen[j]||state[j]==BLOW_GONE||!joined(part[i],part[j]))continue;
                    seen[j]=1;list.push_back(j);
                }
            }
        };
        for(size_t i=0;i<n;++i)if(present[i]&&core[i]&&part[i]!=PART_GUN) {held[i]=1;open.push_back(i);}
        spread(held,open,nullptr);
        for(size_t i=0;i<n;++i) {
            if(!present[i]||held[i]||state[i]==BLOW_GONE)continue;
            std::vector<size_t> piece;open.assign(1,i);held[i]=1;
            spread(held,open,&piece);
            for(size_t k:piece)state[k]=BLOW_GONE;
            if((int)piece.size()<blowSmallest)eaten.insert(eaten.end(),piece.begin(),piece.end());
            else loose.push_back(std::move(piece));
        }
    }
    float left(int tip) const {   // the share of a part still on the body
        int all=0,kept=0;
        for(size_t i=0;i<present.size();++i)if(present[i]&&part[i]==tip) {++all;kept+=state[i]!=BLOW_GONE;}
        return all?(float)kept/all:1;
    }
    bool jointGone(V3 at) const {
        int gone=0,kept=0;
        for(size_t i=0;i<present.size();++i)if(present[i]&&std::hypot(x[i]-at.x,z[i]-at.z)<2.2f)(state[i]==BLOW_GONE?gone:kept)++;
        return gone>kept;
    }
};

// A point on the limb a share of its length along.
static V3 alongLimb(V3 a,V3 b,V3 c,float t) {
    float first=length(b-a),second=length(c-b),at=t*(first+second);
    return at<first?a+(b-a)*(at/std::max(1e-3f,first)):b+(c-b)*((at-first)/std::max(1e-3f,second));
}

// The heaviest blows cut the body in two from one shoulder to the far hip: the halves' own trick (two dolls, the
// top and the legs), the cut slanted. The arm under the cut is cut off with it. A blast that takes two limbs does it
// now and then, its throw kept; a point-blank volley's halves too. -gorecut slash: every kill the rules take anything from.
static bool goreSlash(Ragdoll &doll,const mobj_t &thing,bool blast,float tx) {
    const Relief &body=*doll.body;
    if(body.ball||doll.lowerHalf||doll.headless)return false;
    static int forced=-1;
    if(forced<0) {int arg=M_CheckParm((char*)"-gorecut");forced=arg&&arg+1<myargc&&!strcmp(myargv[arg+1],"slash");}
    uint32_t s=doll.goreSeed^0x35a7c1e9u;
    auto rnd=[&] {s=hash32(s+0x9e3779b9u);return (float)(s>>8)/16777216.0f;};
    int limbs=(doll.armOff[0]||doll.armOff[1]?1:0)+(doll.armOff[0]&&doll.armOff[1]?1:0)+(doll.legOff[0]?1:0)+(doll.legOff[1]?1:0);
    bool want=forced?(limbs>0||doll.halved):doll.halved?goreGibKilling()&&rnd()<blowSlashGib:blast&&limbs>=2&&rnd()<blowSlashBlast;
    if(!want)return false;
    const DollPose &rest=body.rest;
    int high=blast&&std::abs(tx)>0.3f?(tx>0?0:1):rnd()<0.5f?0:1;   // the shoulder it starts from: the side it came from
    V3 from=rest[high?P_SH_R:P_SH_L],to=rest[high?P_HIP_L:P_HIP_R];
    float dropBy=1+2*rnd(),rise=to.z+1+2*rnd();
    float span=to.x-from.x;
    if(std::abs(span)<4)return false;
    doll.halved=true;doll.legsFly=true;
    doll.cutSlant=(rise-(from.z-dropBy))/span;doll.cutZ=(from.z-dropBy)-doll.cutSlant*from.x;
    doll.legOff[0]=doll.legOff[1]=0;doll.armOff[0]=doll.armOff[1]=false;
    if(body.arms[high])doll.armOff[high]=true;
    if(!doll.guts)doll.guts=&gutsOf(thing);
    if(M_CheckParm((char*)"-ragdolllog"))fprintf(stderr,"goreslash high %d slant %.2f cut %.1f\n",high,doll.cutSlant,doll.cutZ);
    return true;
}

// At the kill, after the rules: their limbs taken as shapes. tx, ty: the blow's way in the body; high: where it
// went off (1 well above the hip, -1 at the legs). A blast that takes nothing still rings.
void goreBlowOf(Ragdoll &doll,const mobj_t &thing,const std::vector<GorePellet> *gore,float acrossShot,float tx,float ty,float high,bool blast) {
    const Relief &body=*doll.body;
    static const int off=M_CheckParm((char*)"-gorenoblow");
    if(off||body.ball)return;
    if(goreSlash(doll,thing,blast,tx)||doll.halved||doll.lowerHalf)return;
    bool any=doll.armOff[0]||doll.armOff[1]||doll.legOff[0]||doll.legOff[1];
    if(!any&&!blast)return;
    uint32_t s=doll.goreSeed^0x4cf5ad43u;
    auto rnd=[&] {s=hash32(s+0x9e3779b9u);return (float)(s>>8)/16777216.0f;};
    const DollPose &rest=body.rest;
    V3 hips=middle(rest[P_HIP_L],rest[P_HIP_R]);
    float tall=std::max(16.0f,rest[P_HEAD].z),scale=std::clamp(tall/50,0.7f,2.0f),half=std::abs(rest[P_SH_R].x-rest[P_SH_L].x)/2;
    // Where a blast went off, on the body: the side it came from, the height it struck.
    float impactX=-(tx>0?1.0f:-1.0f)*half*std::min(1.0f,std::abs(tx)+0.3f),impactZ=0.5f*(hips.z+body.neck)+0.7f*high*0.5f*(body.neck-hips.z);
    auto reachOf=[&] {   // mostly modest, now and then huge
        float u=rnd(),r=4.5f+5*u+(u>0.8f?7*(u-0.8f)/0.2f:0);
        return r*scale*blowExaggerate*blowMore*(blast?blowBlastReach:1);
    };
    auto blow=std::make_shared<GoreBlow>();
    blow->impactX=impactX;blow->impactZ=impactZ;blow->reach=(blast?10.0f:7.0f)*scale;blow->tall=tall;blow->way=V3{tx,ty,0};
    if(!any) {doll.blow=blow;return;}
    BlowField field(body,ragdollLump(thing));
    std::vector<BlowBite> bites;
    if(blast&&(blowMore>1||rnd()<0.5f))bites.push_back({impactX,impactZ,0.7f*reachOf(),hash32(s^0x1u)});
    // Each limb: the middle of the pellets on it, or a blast's somewhere along it, drawn toward where it went off.
    struct Limb { bool arm;int side;float bitten=0; };
    std::vector<Limb> limbs;
    for(int k=0;k<2;++k)if(doll.armOff[k]&&body.arms[k])limbs.push_back({true,k});
    for(int k=0;k<2;++k)if(doll.legOff[k])limbs.push_back({false,k});
    for(const Limb &limb:limbs) {
        int k=limb.side;
        V3 a=limb.arm?rest[k?P_SH_R:P_SH_L]:rest[k?P_HIP_R:P_HIP_L],b=limb.arm?rest[k?P_ELB_R:P_ELB_L]:rest[k?P_KNEE_R:P_KNEE_L],c=limb.arm?rest[k?P_HAND_R:P_HAND_L]:rest[k?P_ANK_R:P_ANK_L];
        float sx=0,sz=0;int pellets=0;
        if(gore)for(const GorePellet &pellet:*gore) {
            float u=pellet.u*acrossShot;
            if(std::min(toSegment(u,pellet.z,a,b),toSegment(u,pellet.z,b,c))>3)continue;
            sx+=u;sz+=pellet.z;++pellets;
        }
        V3 at;
        if(pellets)at=V3{sx/pellets,0,sz/pellets};
        else {
            float t=limb.arm?0.1f+0.75f*rnd():doll.legOff[k]==2?0.25f+0.4f*rnd():0.55f+0.35f*rnd();
            at=alongLimb(a,b,c,t);
        }
        // (A leg hardly: pulled up toward a blast at the waist, every cut came out at the knee or over it.)
        if(blast) {float pull=limb.arm?0.25f:0.08f;at.x+=pull*(impactX-at.x);at.z+=pull*(impactZ-at.z);}
        bites.push_back({at.x,at.z,reachOf()*(limb.arm?1:blowLegReach),hash32(s^(uint32_t)(k+1)*0x9e37u^(limb.arm?0x51u:0x77u)),k});
        // Tried wider until the limb's end has come away.
        int tip=limb.arm?(k?PART_FORE_R:PART_FORE_L):(k?PART_SHIN_R:PART_SHIN_L);
        for(int tries=0;tries<blowTries;++tries) {
            field.cut(bites,rest,scale);
            if(field.left(tip)<blowKept)break;
            bites.back().reach*=blowGrow;
        }
    }
    field.cut(bites,rest,scale);
    // What came off, whether the rules asked for it or the bite took it too. A limb that wouldn't part goes the
    // old way, whole at its joint.
    for(int k=0;k<2;++k) {
        if(body.arms[k]&&field.left(k?PART_FORE_R:PART_FORE_L)<blowKept) {
            doll.armOff[k]=false;doll.armBlown[k]=true;
            bool whole=field.jointGone(rest[k?P_ELB_R:P_ELB_L]);
            blow->stumps.push_back({whole?(k?P_SH_R:P_SH_L):(k?P_ELB_R:P_ELB_L),whole?(k?P_SH_L:P_SH_R):(k?P_SH_R:P_SH_L),doll.goreSeed^(k?0x1d2c3b4au:0x5e6f7a8bu)});
        }
        if(field.left(k?PART_SHIN_R:PART_SHIN_L)<blowKept) {
            bool whole=field.left(k?PART_THIGH_R:PART_THIGH_L)<0.5f;   // the thigh gone: the leg's gone from the hip
            doll.legOff[k]=whole?2:1;doll.legBlown[k]=true;
            blow->stumps.push_back({whole?(k?P_HIP_R:P_HIP_L):(k?P_KNEE_R:P_KNEE_L),whole?P_SPINE:(k?P_HIP_R:P_HIP_L),doll.goreSeed^(k?0x2b8e4f1du:0x63d1a7e5u)});
        }
    }
    // The edge: what is left beside a gap is torn.
    for(size_t i=0;i<field.state.size();++i) {
        if(!field.present[i]||field.state[i]!=BLOW_KEEP)continue;
        int c=(int)(i%field.w),r=(int)(i/field.w);bool edge=false;
        for(int dr=-1;dr<=1&&!edge;++dr)for(int dc=-1;dc<=1;++dc) {
            int c2=c+dc,r2=r+dr;if(c2<0||r2<0||c2>=field.w||r2>=field.h)continue;
            size_t j=(size_t)r2*field.w+c2;
            if(field.present[j]&&field.state[j]==BLOW_GONE) {edge=true;break;}
        }
        if(edge)field.state[i]=BLOW_RIM;
    }
    if(!bites.empty()) {blow->impactX=bites.back().x;blow->impactZ=bites.back().z;blow->reach=bites.back().reach;}
    blow->state=std::move(field.state);blow->x=std::move(field.x);blow->z=std::move(field.z);
    blow->loose=std::move(field.loose);blow->eaten=std::move(field.eaten);
    doll.blow=blow;
    if(!doll.guts)doll.guts=&gutsOf(thing);
    if(M_CheckParm((char*)"-ragdolllog"))fprintf(stderr,"goreblow bites %zu loose %zu eaten %zu stumps %zu reach %.1f arms %d %d legs %d %d\n",
        bites.size(),blow->loose.size(),blow->eaten.size(),blow->stumps.size(),blow->reach,(int)doll.armBlown[0],(int)doll.armBlown[1],doll.legOff[0],doll.legOff[1]);
}

// Once the doll knows its floors: what came off leaves, at once (the body's throw is already going).
void goreBlowStart(Ragdoll &doll,const mobj_t &thing,float dx,float dy,float blowSpeed) {
    if(!doll.blow||doll.blow->state.empty())return;
    GoreBlow &b=*doll.blow;
    const Relief &body=*doll.body;
    uint32_t s=doll.goreSeed^0x7a3d1c55u;
    auto rnd=[&] {s=hash32(s+0x9e3779b9u);return (float)(s>>8)/16777216.0f;};
    float side=doll.mirror?-1.0f:1.0f;
    int tint=bloodTint(&thing);bool bleeds=bleedsBlood(thing);
    float carryX=units(thing.momx)+doll.walk[0],carryY=units(thing.momy)+doll.walk[1];
    // A piece away from the wound and on along the shot.
    auto fly=[&](const std::vector<HeadTexel> &texels,const std::vector<bool> &torn,float cx,float cz,float size,float strength) {
        float ux=cx-b.impactX,l=std::max(1.0f,std::abs(ux)),ox=side*ux/l;
        float wxWay=dx+0.8f*doll.ax[0]*ox,wyWay=dy+0.8f*doll.ax[1]*ox,wl=std::max(1e-3f,std::hypot(wxWay,wyWay));
        float wx,wy;doll.overGrid(V3{cx,0,0},wx,wy);
        FlyingPieceStart start{wx+doll.floors.x0,wy+doll.floors.y0,doll.gz+cz,wxWay/wl,wyWay/wl,carryX,carryY,strength,hash32(s+=0x9e3779b9u),tint,bleeds,doll.guts,false,
                               std::clamp(std::sqrt(size)*1.6f,10.0f,24.0f),&torn,1.2f+1.4f*rnd()};
        if(auto piece=startFlyingPiece(texels,start)) {doll.chunks.push_back(piece);doll.chunkFrom.push_back(0);}
    };
    auto heftOf=[](size_t n){return std::clamp(8/std::sqrt((float)n),0.6f,1.4f);};
    // The parts cut free, as they are, raw where they left (where, for their stumps' fibres below).
    struct Loose { size_t chunk;float x,z; };
    std::vector<Loose> looseAt;
    for(const std::vector<size_t> &piece:b.loose) {
        float cx=0,cz=0;std::set<size_t> mine(piece.begin(),piece.end());
        for(size_t i:piece) {cx+=b.x[i];cz+=b.z[i];}
        cx/=piece.size();cz/=piece.size();
        std::vector<HeadTexel> texels;std::vector<bool> torn;
        for(size_t i:piece) {
            texels.push_back({(int)std::floor(side*(b.x[i]-cx)),(int)std::floor(b.z[i]-cz),body.colour[i],body.backColour[i],stumpPieceBone(doll,i)});
            bool edge=false;int w=body.width;
            for(long j:{(long)i-1,(long)i+1,(long)i-w,(long)i+w})edge=edge||(j>=0&&j<(long)b.state.size()&&!mine.count((size_t)j)&&b.state[(size_t)j]==BLOW_GONE);
            torn.push_back(edge);
        }
        size_t before=doll.chunks.size();
        fly(texels,torn,cx,cz,(float)piece.size(),0.8f*blowSpeed*(0.8f+0.5f*rnd())*heftOf(piece.size()));
        if(doll.chunks.size()>before)looseAt.push_back({before,cx,cz});
    }
    // What the bites ate: cut up, the biggest thrown, the rest and a mist burst where it tore.
    if(b.eaten.size()>=8) {
        bool behind=doll.toEye.y>0;float sx=side*(behind?-1.0f:1.0f);
        std::vector<HeadTexel> all;float mx=0,mz=0;
        for(size_t i:b.eaten) {mx+=b.x[i];mz+=b.z[i];}
        mx/=b.eaten.size();mz/=b.eaten.size();
        for(size_t i:b.eaten) {
            byte seen=behind?body.backColour[i]:body.colour[i];
            all.push_back({(int)std::floor(sx*(b.x[i]-mx)),(int)std::floor(b.z[i]-mz),seen,body.backColour[i]});
        }
        int count=std::clamp(3+(int)all.size()/40,3,10);
        std::vector<bool> torn;uint32_t cutSeed=hash32(doll.goreSeed^0x2fa1c3d7u);
        std::vector<int> pieceOf=burstCut(all,count,cutSeed,torn,false);
        std::vector<int> size(count,0);
        for(int k:pieceOf)++size[k];
        std::vector<int> order(count);
        for(int k=0;k<count;++k)order[k]=k;
        std::sort(order.begin(),order.end(),[&](int p,int q){return size[p]>size[q];});
        std::vector<bool> flies(count,false);
        for(int n=0,most=std::clamp((int)all.size()/120+1,1,3);n<most&&n<count;++n)if(size[order[n]]>=16)flies[order[n]]=true;
        std::vector<HeadTexel> rest;std::vector<int> restPiece;std::vector<bool> restTorn;std::vector<int> compact(count,-1);int pieces=0;
        for(int k=0;k<count;++k) {
            if(!flies[k])continue;
            std::vector<HeadTexel> chunk;std::vector<bool> chunkTorn;float cx=0,cz=0;int n=0;
            for(size_t i=0;i<all.size();++i)if(pieceOf[i]==k) {cx+=b.x[b.eaten[i]];cz+=b.z[b.eaten[i]];++n;}
            cx/=n;cz/=n;
            for(size_t i=0;i<all.size();++i)if(pieceOf[i]==k) {
                size_t t=b.eaten[i];
                chunk.push_back({(int)std::floor(side*(b.x[t]-cx)),(int)std::floor(b.z[t]-cz),body.colour[t],body.backColour[t]});
                chunkTorn.push_back(true);
            }
            fly(chunk,chunkTorn,cx,cz,(float)n,blowSpeed*(0.7f+0.5f*rnd())*heftOf((size_t)n));
        }
        float reach=0;
        for(size_t i=0;i<all.size();++i) {
            int k=pieceOf[i];
            reach=std::max(reach,std::hypot((float)all[i].x,(float)all[i].z));
            if(flies[k])continue;
            if(compact[k]<0)compact[k]=pieces++;
            rest.push_back(all[i]);restPiece.push_back(compact[k]);restTorn.push_back(torn[i]);
        }
        float wx,wy;doll.overGrid(V3{mx,0,0},wx,wy);
        b.at[0]=wx+doll.floors.x0;b.at[1]=wy+doll.floors.y0;b.at[2]=doll.gz+mz;
        if(rest.size()>=4) {
            float reachMost=std::clamp(reach*0.8f,12.0f,40.0f);
            HeadBurstStart start{0,0,sx*b.way.x,blowSpeed,doll.goreSeed^0x3b7e11d9u,tint,bleeds,doll.guts};
            start.canvas=std::clamp(((int)std::ceil(2*(reach+reachMost))+11)/4*4,48,256);
            start.reachScale=3;start.reachMost=reachMost;start.hitMost=reach;
            start.pieceOf=&restPiece;start.torn=&restTorn;
            b.picture=startHeadBurst(rest,start);
        }
        if(bleeds)sprayGush(b.at[0],b.at[1],b.at[2],dx,dy,std::clamp(0.5f+all.size()/250.0f,0.5f,1.4f),tint);
    }
    // Heard, each where it tore: the bone now; the tear and the stump's first gush when the fibres to the part
    // nearest it let go (tear_fibres.cpp), or a couple of tics on when it kept none.
    for(const BlowStump &stump:b.stumps) {
        float wx,wy;doll.overGrid(doll.p[stump.from],wx,wy);
        float z=doll.gz+doll.p[stump.from].z;
        goreSound(GORE_SOUND_CRACK,wx+doll.floors.x0,wy+doll.floors.y0,z);
        std::shared_ptr<FlyingPiece> part;float best=20;
        for(const Loose &l:looseAt) {
            float d=std::hypot(l.x-body.rest[stump.from].x,l.z-body.rest[stump.from].z);
            if(d<best) {best=d;part=doll.chunks[l.chunk];}
        }
        if(part)flyingPieceHold(*part,2);
        tearStart({&doll,&doll,stump.from,part,nullptr,-1,TEAR_BLOWN,stump.seed^0x3c6ef372u,tint,bleeds,true,2.5f,dx,dy,0.8f});
    }
    b.loose.clear();b.loose.shrink_to_fit();b.eaten.clear();b.eaten.shrink_to_fit();
}

// Each tic the stumps pump on a beat of their own, out along the limb's way.
void goreBlowTic(Ragdoll &doll,const mobj_t &thing,int target) {
    if(!doll.blow||!bleedsBlood(thing))return;
    GoreBlow &b=*doll.blow;
    for(int t=std::max(b.pumped+1,target-1);t<=target;++t)
        for(const BlowStump &stump:b.stumps) {
            float strength=0.7f*stumpPulse(t,stump.seed),stream=0.7f*stumpStream(t,stump.seed);
            if(strength<=0&&stream<=0)continue;
            float sideSign=doll.mirror?-1.0f:1.0f,wx,wy;
            doll.overGrid(doll.p[stump.from],wx,wy);
            V3 way=unit(doll.p[stump.from]-doll.p[stump.toward]);
            float vx=doll.ax[0]*sideSign*way.x+doll.ay[0]*way.y,vy=doll.ax[1]*sideSign*way.x+doll.ay[1]*way.y;
            float carry[3];doll.worldSpeed(stump.from,carry);
            sprayStump(wx+doll.floors.x0,wy+doll.floors.y0,doll.gz+doll.p[stump.from].z,vx,vy,way.z,strength,bloodTint(&thing),stream,carry);
        }
    b.pumped=std::max(b.pumped,target);
}

// What the card does with a texel: 0 draws it, 1 leaves it out, 2 its torn edge, 3 stripped bone.
int goreBlowState(const Ragdoll &doll,size_t texel) {
    const GoreBlow &b=*doll.blow;
    return texel<b.state.size()?b.state[texel]:BLOW_KEEP;
}
// The flesh ringing round the wound, at a corner of the body as it stands: thrown out from the blow and on along
// it, the shock reaching further parts later, dying within blowSoftTics. Added before the pose carries it.
bool goreBlowSoft(const Ragdoll &doll) {return doll.blow&&doll.lastPose<blowSoftTics;}
V3 goreBlowOffset(const Ragdoll &doll,V3 rest) {
    const GoreBlow &b=*doll.blow;
    float ox=rest.x-b.impactX,oz=rest.z-b.impactZ,d=std::hypot(ox,oz),t=doll.lastPose-d/blowWave;
    if(t<=0)return {};
    float ring=std::sin(0.9f*t)*std::exp(-0.45f*t);
    float amount=std::min(blowSoft*b.reach*blowExaggerate*std::exp(-d/(1.6f*b.reach)),blowSoftMost*b.tall)*ring;
    float l=std::max(1.0f,d);
    return V3{ox/l,0,oz/l}*amount+b.way*(0.6f*amount);
}
// The burst where it tore, a picture left hanging there as the body flies on.
bool goreBlowCard(const Ragdoll &doll,RagdollCard &card) {
    if(!doll.blow||!doll.blow->picture)return false;
    GoreBlow &b=*doll.blow;
    return headBurstCard(*b.picture,leveltime-doll.start,b.at[0],b.at[1],b.at[2],card);
}
void goreBlowDrop(const Ragdoll &doll) {
    if(doll.blow&&doll.blow->picture)dropHeadBurst(*doll.blow->picture);
}
}
