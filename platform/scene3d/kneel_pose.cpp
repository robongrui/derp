// A soldier down on one knee (p_lanes.c's P_TacKneel): Doom has no frame for that. The frame the game
// shows, its legs re-posed by their bones: the drawing's skeleton (skeletons.cpp: the hand table, else the
// fit) says which texels are thigh and shin, and kneel_poses_stock.h how each view turns and shortens them
// (one kneel in 3D seen from all eight sides, tests/kneel_pose.py). Each leg texel follows its bone, the
// body above stays as drawn, and the picture drops until its lowest texel stands where the frame's did.
// Before, the legs were only squashed (body_motion.cpp), which read as a short man standing. One image per
// drawing and view, made the first time it is drawn, so everything done to a sprite (bends, wounds, light)
// does not know the difference. tests/kneel_pose_lib.py does the same in Python: change both together.
#include "internal.h"
#include "../kneel_poses_stock.h"
#include <algorithm>
#include <cmath>
#include <unordered_set>
#include <vector>

namespace doom3d {
namespace {
constexpr int kneelOffset=1<<30;
constexpr int margin=16;          // room each side and below for legs posed out of the drawing's box
constexpr float reachOut=0.12f;   // a bone's ends drawn a little past its texels, so a bend leaves no gap
constexpr float heldReach=6;      // what a hand holds reaches this far past it (a gun held low over a thigh)
constexpr int gap=2;              // the widest pinhole closed in the legs, in texels
std::unordered_set<int> none;     // keys whose drawing has no skeleton to pose

struct P { float u,h; };
P operator+(P a,P b) {return {a.u+b.u,a.h+b.h};}
P operator-(P a,P b) {return {a.u-b.u,a.h-b.h};}
P operator*(P a,float s) {return {a.u*s,a.h*s};}
float dot(P a,P b) {return a.u*b.u+a.h*b.h;}
float length(P a) {return std::hypot(a.u,a.h);}
// From straight down, degrees, positive toward the picture's right.
P way(float degrees) {float r=degrees*doomPi/180;return {std::sin(r),-std::cos(r)};}
// How far p is from the segment ab, and how far along it (0 at a, 1 at b) for alongOf.
float alongOf(P p,P a,P b) {P d=b-a;return dot(p-a,d)/std::max(1e-6f,dot(d,d));}
float distance(P p,P a,P b) {float t=std::clamp(alongOf(p,a,b),0.0f,1.0f);return length(p-(a+(b-a)*t));}

const int hips[2]={SJ_HIP0,SJ_HIP1},knees[2]={SJ_KNEE0,SJ_KNEE1},ankles[2]={SJ_ANK0,SJ_ANK1};
const int pairs[6][2]={{SJ_SH0,SJ_SH1},{SJ_ELB0,SJ_ELB1},{SJ_HAND0,SJ_HAND1},{SJ_HIP0,SJ_HIP1},{SJ_KNEE0,SJ_KNEE1},{SJ_ANK0,SJ_ANK1}};
// Bones a texel under the hips may belong to instead of a leg: the trunk and the arms (a hand, a gun).
const int trunk[6][2]={{SJ_PELVIS,SJ_NECK},{SJ_NECK,SJ_HEAD},{SJ_SH0,SJ_ELB0},{SJ_SH1,SJ_ELB1},{SJ_ELB0,SJ_HAND0},{SJ_ELB1,SJ_HAND1}};
}

int kneelKey(int lump,unsigned seen) {return -1-(kneelOffset+lump*8+(int)(seen&7));}

Image *kneelImage(int lump,unsigned seen,bool flip) {
    int key=kneelKey(lump,seen);
    auto found=images.find(key);
    if(found!=images.end())return &found->second;
    if(none.count(key))return nullptr;
    const SpriteSkeleton *skeleton=lumpSkeleton(lump);
    if(!skeleton) {none.insert(key);return nullptr;}
    const Image &source=lumpImage(lump,false);
    const doom_kneel_view &view=doom_kneel_views[seen&7];
    const int w=source.width,h=source.height,top=source.top;
    // As the eye sees it: a mirrored drawing turned over, its joints mirrored and its right and left
    // swapped, so the pose names the body's own legs (as the tool shows it).
    const float left=flip?(float)(w-source.left):(float)source.left;
    P joint[SKELETON_JOINTS];
    for(int j=0;j<SKELETON_JOINTS;++j)joint[j]={flip?-skeleton->joint[j][0]:skeleton->joint[j][0],skeleton->joint[j][1]};
    if(flip)for(const auto &pair:pairs)std::swap(joint[pair[0]],joint[pair[1]]);
    auto index=[&](int c,int r) {return (size_t)r*w+(size_t)(flip?w-1-c:c);};
    auto opaque=[&](size_t i) {return source.pixels[2*i+1]!=0;};
    auto at=[&](int c,int r) {return P{c-left+0.5f,top-r-0.5f};};
    // Which leg texel goes with which bone (leg*2 + 0 thigh / 1 shin), how far along it they reach and
    // how far out from it. Under the higher hip, nearer a leg bone than the trunk or an arm; the foot,
    // past the ankle, goes with the shin.
    auto ends=[&](const P *j,int bone,P &a,P &b) {
        int s=bone/2;
        if(bone&1) {a=j[knees[s]];b=j[ankles[s]];} else {a=j[hips[s]];b=j[knees[s]];}
    };
    std::vector<int8_t> bone((size_t)w*h,-1);
    float lo[4],hi[4],thick[4]={};bool has[4]={};
    const float hipTop=std::max(joint[SJ_HIP0].h,joint[SJ_HIP1].h)+0.5f;
    for(int r=0;r<h;++r)for(int c=0;c<w;++c) {
        size_t i=index(c,r);
        if(!opaque(i))continue;
        P p=at(c,r);
        if(p.h>hipTop)continue;
        float best=1e9f;int which=-1;
        for(int b=0;b<4;++b) {P a,e;ends(joint,b,a,e);float d=distance(p,a,e);if(d<best) {best=d;which=b;}}
        for(const auto &t:trunk) {
            P a=joint[t[0]],e=joint[t[1]];
            if(t[1]==SJ_HAND0||t[1]==SJ_HAND1)e=e+(e-a)*(heldReach/std::max(1e-3f,length(e-a)));
            if(distance(p,a,e)<best-0.5f) {which=-1;break;}
        }
        if(which<0)continue;
        bone[(size_t)r*w+c]=(int8_t)which;
        P a,e;ends(joint,which,a,e);float t=alongOf(p,a,e);
        if(!has[which]) {lo[which]=hi[which]=t;has[which]=true;}
        lo[which]=std::min(lo[which],t);hi[which]=std::max(hi[which],t);thick[which]=std::max(thick[which],best);
    }
    // The legs posed: hips where they were drawn, each bone turned and shortened by the view's pose.
    P posed[SKELETON_JOINTS];std::copy(joint,joint+SKELETON_JOINTS,posed);
    for(int s=0;s<2;++s) {
        const doom_kneel_leg &leg=view.leg[s];
        float thigh=length(joint[knees[s]]-joint[hips[s]]),shin=length(joint[ankles[s]]-joint[knees[s]]);
        posed[knees[s]]=posed[hips[s]]+way(leg.thigh)*(thigh*leg.thighShare);
        posed[ankles[s]]=posed[knees[s]]+way(leg.shin)*(shin*leg.shinShare);
    }
    // Each texel of the new picture asks each bone, back leg first, where on the drawing it came from:
    // along the bone by its share, across it as drawn.
    const int W=w+2*margin,H=h+margin;const float dleft=left+margin;
    std::vector<int> from((size_t)W*H,-1);
    auto dat=[&](int c,int r) {return P{c-dleft+0.5f,top-r-0.5f};};
    for(int s:{1-view.front,view.front}) {
        int order[2]={0,1};
        if(view.leg[s].shinBehind) {order[0]=1;order[1]=0;}
        for(int part:order) {
            int b=s*2+part;
            if(!has[b])continue;
            P p0,q0,p1,q1;ends(joint,b,p0,q0);ends(posed,b,p1,q1);
            float l0=std::max(1e-3f,length(q0-p0)),real=length(q1-p1),l1=std::max(0.3f,real);
            P w0=(q0-p0)*(1/l0);
            P w1=real>1e-3f?(q1-p1)*(1/real):way(part?view.leg[s].shin:view.leg[s].thigh);
            float span=std::max(1e-3f,hi[b]-lo[b]),from0=lo[b]-reachOut*span,to=hi[b]+reachOut*span;
            P e0=p1+w1*(l1*from0),e1=p1+w1*(l1*to);float t=thick[b]+1;
            int c0=std::max(0,(int)(std::min(e0.u,e1.u)-t+dleft)),c1=std::min(W,(int)(std::max(e0.u,e1.u)+t+dleft)+2);
            int r0=std::max(0,(int)(top-std::max(e0.h,e1.h)-t)),r1=std::min(H,(int)(top-std::min(e0.h,e1.h)+t)+2);
            for(int r=r0;r<r1;++r)for(int c=c0;c<c1;++c) {
                P d=dat(c,r)-p1;
                float along=dot(d,w1)/l1;
                if(along<from0||along>to)continue;
                float across=d.u*-w1.h+d.h*w1.u,kept=std::clamp(along,lo[b],hi[b]);
                P src=p0+w0*(kept*l0)+P{-w0.h,w0.u}*across;
                int sc=(int)std::floor(src.u+left),sr=(int)std::floor(top-src.h);
                if(sc<0||sr<0||sc>=w||sr>=h||bone[(size_t)sr*w+sc]!=b)continue;
                from[(size_t)r*W+c]=(int)index(sc,sr);
            }
        }
    }
    // The body over them, as drawn.
    for(int r=0;r<h;++r)for(int c=0;c<w;++c) {
        size_t i=index(c,r);
        if(opaque(i)&&bone[(size_t)r*w+c]<0)from[(size_t)r*W+c+margin]=(int)i;
    }
    // Pinholes the posing tears in the legs (a bend's outer side, a bone's end): from just over the hips
    // down, an empty texel with drawn ones within a gap of two either side across, then straight above and
    // below, takes the nearer (the left, the upper on a tie). Wider is the space between the legs and stays.
    int first=std::max(0,(int)std::ceil(top-std::max(joint[SJ_HIP0].h,joint[SJ_HIP1].h)-3));
    for(int pass=0;pass<2;++pass) {
        std::vector<std::pair<size_t,int>> fills;
        for(int r=first;r<H;++r)for(int c=0;c<W;++c) {
            if(from[(size_t)r*W+c]>=0)continue;
            int nearK[2]={0,0},nearI[2]={-1,-1};
            for(int side=0;side<2;++side)for(int k=1;k<=gap;++k) {
                int rr=pass?r+(side?k:-k):r,cc=pass?c:c+(side?k:-k);
                if(rr<0||cc<0||rr>=H||cc>=W)continue;
                int i=from[(size_t)rr*W+cc];
                if(i>=0) {nearK[side]=k;nearI[side]=i;break;}
            }
            if(nearI[0]>=0&&nearI[1]>=0&&nearK[0]+nearK[1]<=gap+1)fills.push_back({(size_t)r*W+c,nearK[1]<nearK[0]?nearI[1]:nearI[0]});
        }
        for(const auto &f:fills)from[f.first]=f.second;
    }
    // Dropped until its lowest texel is where the drawing's lowest was; cropped to what is drawn.
    int low0=h,last=-1,r0=H,c0=W,c1=-1;
    for(int r=0;r<h;++r)for(int c=0;c<w;++c)if(opaque(index(c,r))) {low0=std::min(low0,top-r-1);break;}
    for(int r=0;r<H;++r)for(int c=0;c<W;++c)if(from[(size_t)r*W+c]>=0) {last=r;r0=std::min(r0,r);c0=std::min(c0,c);c1=std::max(c1,c);}
    if(last<0) {none.insert(key);return nullptr;}
    int drop=std::max(0,(top-last-1)-low0);
    int ow=c1-c0+1,oh=last-r0+1,otop=top-drop-r0,oleft=(int)dleft-c0;
    bool glows=source.emission.size()*2==source.pixels.size();
    std::vector<byte> pixels((size_t)ow*oh*2,0),glow((size_t)ow*oh,0);
    for(int r=0;r<oh;++r)for(int c=0;c<ow;++c) {
        int i=from[(size_t)(r+r0)*W+c+c0];
        if(i<0)continue;
        // Stored as the drawing is: a mirrored one turned back, drawn mirrored as its frame is.
        size_t o=(size_t)r*ow+(size_t)(flip?ow-1-c:c);
        pixels[2*o]=source.pixels[2*(size_t)i];pixels[2*o+1]=source.pixels[2*(size_t)i+1];
        glow[o]=glows?source.emission[(size_t)i]:0;
    }
    if(flip)oleft=ow-oleft;
    auto normals=spriteNormals(ow,oh,pixels.data());
    return &images.emplace(key,upload(ow,oh,pixels.data(),oleft,otop,glow.data(),normals.data())).first->second;
}

// Whether a kneeling thing is drawn on its knee here (not squashed): its frame has a skeleton to pose.
bool kneelPictured(const mobj_t &thing) {
    if(!Game_Kneeling(&thing)||thing.health<=0||(thing.flags&MF_CORPSE)||!thing.info||thing.height>=thing.info->height)return false;
    if(thing.sprite<0||thing.sprite>=numsprites)return false;
    const spritedef_t &sprite=sprites[thing.sprite];
    int frame=thing.frame&FF_FRAMEMASK;
    if(frame>=sprite.numframes||!sprite.spriteframes[frame].rotate)return false;
    return lumpSkeleton(firstspritelump+sprite.spriteframes[frame].lump[0])!=nullptr;
}
} // namespace doom3d
