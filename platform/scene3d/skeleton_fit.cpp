/* The skeleton fitted to a drawing (PLAN-skin-suit.md 4.3): the monster's
   reference skeleton (its ragdoll's, from the front pain drawing) is not found
   again in every drawing but fitted to it: its bones keep their lengths (a
   limb may shorten to 0.65, pointing toward or away from the eye), the hips
   and shoulders keep their width as much as the view shows of it, and only
   the angles and the pelvis's place change, until the bones cover the
   drawing and stay inside it. Bone identity is kept by construction: the left
   thigh stays the left thigh through the walk. Coordinate descent from a
   start (a neighbouring drawing's fit, or the reference turned to the view),
   in shrinking steps; a drawing costs a few milliseconds, once, at load. */
#include "internal.h"

namespace doom3d {
const int skeletonBoneEnds[SKELETON_BONES][2]={{SJ_HIP0,SJ_HIP1},{SJ_PELVIS,SJ_NECK},{SJ_NECK,SJ_HEAD},
    {SJ_SH0,SJ_ELB0},{SJ_SH1,SJ_ELB1},{SJ_ELB0,SJ_HAND0},{SJ_ELB1,SJ_HAND1},
    {SJ_HIP0,SJ_KNEE0},{SJ_HIP1,SJ_KNEE1},{SJ_KNEE0,SJ_ANK0},{SJ_KNEE1,SJ_ANK1}};
bool skeletonBonesJoin(int a,int b) {
    if(a==b)return true;
    for(int i=0;i<2;++i)for(int j=0;j<2;++j)if(skeletonBoneEnds[a][i]==skeletonBoneEnds[b][j])return true;
    return false;
}

namespace {
// The limb segments the fit turns, each from a joint the body places: upper arms, forearms, thighs, shins.
constexpr int LIMBS=8;
const int limbFrom[LIMBS]={SJ_SH0,SJ_SH1,SJ_ELB0,SJ_ELB1,SJ_HIP0,SJ_HIP1,SJ_KNEE0,SJ_KNEE1};
const int limbTo[LIMBS]={SJ_ELB0,SJ_ELB1,SJ_HAND0,SJ_HAND1,SJ_KNEE0,SJ_KNEE1,SJ_ANK0,SJ_ANK1};
const int limbBone[LIMBS]={SB_UPPER0,SB_UPPER1,SB_FORE0,SB_FORE1,SB_THIGH0,SB_THIGH1,SB_SHIN0,SB_SHIN1};

struct Body { float spine,head,hipHalf,shoulderHalf,shoulderDrop,len[LIMBS]; };
// The pose the fit moves: the pelvis, the spine's and head's angles, the spine's stretch, each limb
// segment's angle and length (a share of its reference length), and how wide the shoulders and hips stand
// (shares of the reference's) and how far under the neck the shoulders sit (units more).
constexpr int PARAMS=4+1+2*LIMBS+3;
struct Pose { float v[PARAMS]; };
enum { PX,PH,SPINE_A,HEAD_A,SPINE_K,LIMB_A,LIMB_K=LIMB_A+LIMBS,SH_W=LIMB_K+LIMBS,SH_D,HIP_W };

float dist(const float *a,const float *b) {return std::hypot(a[0]-b[0],a[1]-b[1]);}
float angleOf(const float *a,const float *b) {return std::atan2(b[1]-a[1],b[0]-a[0]);}

void place(const Body &b,const Pose &p,float lateral,float j[SKELETON_JOINTS][2]) {
    const float *v=p.v;
    float sx=std::cos(v[SPINE_A]),sy=std::sin(v[SPINE_A]),rx=sy,ry=-sx;   // the spine's way, and its right
    j[SJ_PELVIS][0]=v[PX];j[SJ_PELVIS][1]=v[PH];
    j[SJ_NECK][0]=v[PX]+sx*b.spine*v[SPINE_K];j[SJ_NECK][1]=v[PH]+sy*b.spine*v[SPINE_K];
    j[SJ_HEAD][0]=j[SJ_NECK][0]+std::cos(v[HEAD_A])*b.head;j[SJ_HEAD][1]=j[SJ_NECK][1]+std::sin(v[HEAD_A])*b.head;
    for(int s=0;s<2;++s) {
        float side=(s?1.0f:-1.0f)*lateral;   // side 0, the body's right, on the screen's left seen from the front
        float hip=b.hipHalf*v[HIP_W]*side,shoulder=b.shoulderHalf*v[SH_W]*side,drop=b.shoulderDrop+v[SH_D];
        j[SJ_HIP0+s][0]=v[PX]+rx*hip;j[SJ_HIP0+s][1]=v[PH]+ry*hip;
        j[SJ_SH0+s][0]=j[SJ_NECK][0]-sx*drop+rx*shoulder;
        j[SJ_SH0+s][1]=j[SJ_NECK][1]-sy*drop+ry*shoulder;
    }
    for(int k=0;k<LIMBS;++k) {   // in order: each segment's start is placed before it
        float a=v[LIMB_A+k],l=b.len[k]*v[LIMB_K+k];
        j[limbTo[k]][0]=j[limbFrom[k]][0]+std::cos(a)*l;j[limbTo[k]][1]=j[limbFrom[k]][1]+std::sin(a)*l;
    }
}
Pose poseOf(const float j[SKELETON_JOINTS][2],const Body &b) {
    Pose p{};
    p.v[PX]=j[SJ_PELVIS][0];p.v[PH]=j[SJ_PELVIS][1];
    p.v[SPINE_A]=angleOf(j[SJ_PELVIS],j[SJ_NECK]);p.v[HEAD_A]=angleOf(j[SJ_NECK],j[SJ_HEAD]);
    p.v[SPINE_K]=std::clamp(dist(j[SJ_PELVIS],j[SJ_NECK])/std::max(1.0f,b.spine),0.85f,1.1f);
    for(int k=0;k<LIMBS;++k) {
        p.v[LIMB_A+k]=angleOf(j[limbFrom[k]],j[limbTo[k]]);
        p.v[LIMB_K+k]=std::clamp(dist(j[limbFrom[k]],j[limbTo[k]])/std::max(0.5f,b.len[k]),0.65f,1.05f);
    }
    // The widths a neighbour's fit stood at: its spread over the reference's (as wide as its view shows).
    float hips=dist(j[SJ_HIP0],j[SJ_HIP1])/2,shoulders=dist(j[SJ_SH0],j[SJ_SH1])/2;
    p.v[HIP_W]=b.hipHalf>0.5f&&hips>0.5f?std::clamp(hips/b.hipHalf,0.7f,1.4f):1;
    p.v[SH_W]=b.shoulderHalf>0.5f&&shoulders>0.5f?std::clamp(shoulders/b.shoulderHalf,0.7f,1.8f):1;
    float mx=(j[SJ_SH0][0]+j[SJ_SH1][0])/2,my=(j[SJ_SH0][1]+j[SJ_SH1][1])/2;
    p.v[SH_D]=std::clamp((j[SJ_NECK][0]-mx)*std::cos(p.v[SPINE_A])+(j[SJ_NECK][1]-my)*std::sin(p.v[SPINE_A])-b.shoulderDrop,-3.0f,6.0f);
    return p;
}

float segment(float pu,float ph,const float *a,const float *b,float *along=nullptr) {
    float dx=b[0]-a[0],dy=b[1]-a[1],l2=dx*dx+dy*dy;
    float t=l2>1e-6f?std::clamp(((pu-a[0])*dx+(ph-a[1])*dy)/l2,0.0f,1.0f):0;
    if(along)*along=t;
    return std::hypot(pu-a[0]-dx*t,ph-a[1]-dy*t);
}

// The drawing as the fit sees it: its opaque texels' middles, and how far each texel is from the nearest
// opaque one (0 inside).
struct Drawing {
    int w,h,left,top;std::vector<std::array<float,2>> texels;std::vector<float> away;
    float awayAt(float u,float hh) const {
        int c=(int)std::floor(u+left),r=(int)std::floor(top-hh);
        float over=std::max({0.0f,(float)-c,(float)(c-(w-1)),(float)-r,(float)(r-(h-1))});
        c=std::clamp(c,0,w-1);r=std::clamp(r,0,h-1);
        return away[(size_t)r*w+c]+over;
    }
};
Drawing drawingOf(const Image &image) {
    Drawing d{image.width,image.height,image.left,image.top,{},{}};
    d.away.assign((size_t)d.w*d.h,1e4f);
    for(int r=0;r<d.h;++r)for(int c=0;c<d.w;++c)if(image.pixels[((size_t)r*d.w+c)*2+1]) {
        d.away[(size_t)r*d.w+c]=0;
        d.texels.push_back({c-d.left+0.5f,d.top-r-0.5f});
    }
    // Chamfer distance, two passes (1 straight, 1.4 diagonal).
    auto relax=[&](int c,int r,int dc,int dr,float step) {
        int c2=c+dc,r2=r+dr;
        if(c2<0||r2<0||c2>=d.w||r2>=d.h)return;
        float &here=d.away[(size_t)r*d.w+c];here=std::min(here,d.away[(size_t)r2*d.w+c2]+step);
    };
    for(int r=0;r<d.h;++r)for(int c=0;c<d.w;++c) {relax(c,r,-1,0,1);relax(c,r,0,-1,1);relax(c,r,-1,-1,1.4f);relax(c,r,1,-1,1.4f);}
    for(int r=d.h-1;r>=0;--r)for(int c=d.w-1;c>=0;--c) {relax(c,r,1,0,1);relax(c,r,0,1,1);relax(c,r,1,1,1.4f);relax(c,r,-1,1,1.4f);}
    return d;
}

float wrap(float a) {while(a>doomPi)a-=2*doomPi;while(a<-doomPi)a+=2*doomPi;return a;}

struct Fit {
    const SkeletonReference &ref;const Body &body;const Drawing &d;float lateral;Pose prior;
    float cost(const Pose &p) const {return shape(p)+drift(p);}
    // Near the start: a limb doesn't turn far or shorten without the drawing asking for it.
    float drift(const Pose &p) const {
        float drift=0;
        for(int k=0;k<LIMBS;++k) {
            float da=wrap(p.v[LIMB_A+k]-prior.v[LIMB_A+k]);
            drift+=0.02f*da*da;
        }
        drift+=0.05f*wrap(p.v[SPINE_A]-prior.v[SPINE_A])*wrap(p.v[SPINE_A]-prior.v[SPINE_A]);
        return drift;
    }
    float shape(const Pose &p) const {
        float j[SKELETON_JOINTS][2];place(body,p,lateral,j);
        // The arms hang (the user's hand rigs, the imp's and the zombieman's 75 drawings, every view, walking,
        // attacking and in pain): the elbows a tenth of the drawing's height or more under the shoulders, the
        // hands 6% or more. Left free, a side view's far arm, hidden behind the body, swung up over the head.
        float tall=(float)d.h,anatomy=0;
        for(int s=0;s<2;++s) {
            if(!ref.has[SB_UPPER0+s])continue;
            float sh=j[SJ_SH0+s][1];
            float elbow=std::max(0.0f,j[SJ_ELB0+s][1]-(sh-0.1f*tall)),hand=std::max(0.0f,j[SJ_HAND0+s][1]-(sh-0.06f*tall));
            anatomy+=0.03f*(elbow*elbow+hand*hand);
        }
        // Covered: each texel no further from a bone than its flesh reaches.
        float covered=0;
        for(const auto &t:d.texels) {
            float best=1e9f;
            for(int b=0;b<SKELETON_BONES;++b) {
                if(!ref.has[b])continue;
                float e=segment(t[0],t[1],j[skeletonBoneEnds[b][0]],j[skeletonBoneEnds[b][1]])-ref.radius[b];
                best=std::min(best,e);
            }
            best=std::max(0.0f,best);covered+=std::min(best*best,25.0f);
        }
        // Inside: each bone's length within the drawing.
        float outside=0;int samples=0;
        for(int b=0;b<SKELETON_BONES;++b) {
            if(!ref.has[b])continue;
            const float *a=j[skeletonBoneEnds[b][0]],*e=j[skeletonBoneEnds[b][1]];
            for(int k=0;k<=4;++k) {
                float t=k/4.0f,o=d.awayAt(a[0]+(e[0]-a[0])*t,a[1]+(e[1]-a[1])*t);
                outside+=o*o;++samples;
            }
        }
        // (Limbs at their full length unless the drawing asks: what drift holds them to, whatever the start.)
        float shortened=0;
        for(int k=0;k<LIMBS;++k)shortened+=0.6f*(1-p.v[LIMB_K+k])*(1-p.v[LIMB_K+k]);
        return covered/std::max<size_t>(1,d.texels.size())+0.5f*outside/std::max(1,samples)+anatomy+shortened;
    }
};
} // namespace

bool referenceSkeleton(int type,SkeletonReference &out) {
    float rest[15][3];bool arms[2];
    if(!ragdollRestJoints(type,rest,arms))return false;
    // ragdoll.h's DollPoint: head, neck, shoulders, spine (chest), hips, knees, ankles, elbows, hands; its
    // "L" is the drawing's left, the body's right: side 0.
    enum { HEAD,NECK,SH_L,SH_R,SPINE,HIP_L,HIP_R,KNEE_L,KNEE_R,ANK_L,ANK_R,ELB_L,ELB_R,HAND_L,HAND_R };
    auto set=[&](int j,int p){out.joint[j][0]=rest[p][0];out.joint[j][1]=rest[p][2];};
    set(SJ_NECK,NECK);set(SJ_HEAD,HEAD);
    set(SJ_SH0,SH_L);set(SJ_SH1,SH_R);set(SJ_ELB0,ELB_L);set(SJ_ELB1,ELB_R);set(SJ_HAND0,HAND_L);set(SJ_HAND1,HAND_R);
    // The shoulder is where the arm leaves the torso (the ragdoll keeps the elbow halfway from there to the
    // hand); its own shoulder points sit at the neck's narrow row, and arms hung from them crossed the chest.
    for(int s=0;s<2;++s)if(arms[s]) {
        int elb=s?ELB_R:ELB_L,hand=s?HAND_R:HAND_L;
        out.joint[SJ_SH0+s][0]=2*rest[elb][0]-rest[hand][0];out.joint[SJ_SH0+s][1]=2*rest[elb][2]-rest[hand][2];
        // Its hand is the far third's middle: the fist reaches a quarter on.
        float hx=rest[hand][0]+(rest[hand][0]-out.joint[SJ_SH0+s][0])*0.25f,hz=rest[hand][2]+(rest[hand][2]-out.joint[SJ_SH0+s][1])*0.25f;
        out.joint[SJ_ELB0+s][0]=(out.joint[SJ_SH0+s][0]+hx)/2;out.joint[SJ_ELB0+s][1]=(out.joint[SJ_SH0+s][1]+hz)/2;
        out.joint[SJ_HAND0+s][0]=hx;out.joint[SJ_HAND0+s][1]=hz;
    }
    set(SJ_HIP0,HIP_L);set(SJ_HIP1,HIP_R);set(SJ_KNEE0,KNEE_L);set(SJ_KNEE1,KNEE_R);set(SJ_ANK0,ANK_L);set(SJ_ANK1,ANK_R);
    out.joint[SJ_PELVIS][0]=(rest[HIP_L][0]+rest[HIP_R][0])/2;out.joint[SJ_PELVIS][1]=(rest[HIP_L][2]+rest[HIP_R][2])/2;
    for(int b=0;b<SKELETON_BONES;++b)out.has[b]=true;
    out.has[SB_UPPER0]=out.has[SB_FORE0]=arms[0];out.has[SB_UPPER1]=out.has[SB_FORE1]=arms[1];
    for(float &r:out.radius)r=2;
    return true;
}

// Each bone's flesh in the reference drawing: the reach of three quarters of the texels nearest it.
void measureSkeletonRadii(SkeletonReference &ref,const Image &image) {
    Drawing d=drawingOf(image);
    std::vector<float> reach[SKELETON_BONES];
    for(const auto &t:d.texels) {
        int best=-1;float near=1e9f;
        for(int b=0;b<SKELETON_BONES;++b) {
            if(!ref.has[b])continue;
            float e=segment(t[0],t[1],ref.joint[skeletonBoneEnds[b][0]],ref.joint[skeletonBoneEnds[b][1]]);
            if(e<near) {near=e;best=b;}
        }
        if(best>=0)reach[best].push_back(near);
    }
    for(int b=0;b<SKELETON_BONES;++b) {
        if(reach[b].empty()) {ref.radius[b]=1.5f;continue;}
        std::sort(reach[b].begin(),reach[b].end());
        ref.radius[b]=std::clamp(reach[b][reach[b].size()*3/4],1.0f,9.0f);
    }
}

void fitSkeleton(const SkeletonReference &given,const Image &image,float lateral,const SpriteSkeleton *start,SpriteSkeleton &out,bool move) {
    Drawing d=drawingOf(image);
    out.width=d.w;out.height=d.h;out.left=d.left;out.top=d.top;
    const SkeletonReference &ref=given;
    Body body{};
    const auto &rj=ref.joint;
    body.spine=std::max(1.0f,dist(rj[SJ_PELVIS],rj[SJ_NECK]));body.head=std::max(1.0f,dist(rj[SJ_NECK],rj[SJ_HEAD]));
    body.hipHalf=std::abs(rj[SJ_HIP1][0]-rj[SJ_HIP0][0])/2;body.shoulderHalf=std::abs(rj[SJ_SH1][0]-rj[SJ_SH0][0])/2;
    body.shoulderDrop=rj[SJ_NECK][1]-(rj[SJ_SH0][1]+rj[SJ_SH1][1])/2;
    for(int k=0;k<LIMBS;++k)body.len[k]=std::max(0.5f,dist(rj[limbFrom[k]],rj[limbTo[k]]));
    // The start: a neighbour's fit, or the reference turned to the view (what runs across the body shows
    // lateral of its width; seen from behind, mirrored).
    Pose pose;
    if(start)pose=poseOf(start->joint,body);
    else {
        pose=poseOf(rj,body);
        float l=std::abs(lateral)<0.05f?(lateral<0?-0.05f:0.05f):lateral;
        auto turnAngle=[&](float a){return std::atan2(std::sin(a),std::cos(a)*l);};
        pose.v[SPINE_A]=turnAngle(pose.v[SPINE_A]);pose.v[HEAD_A]=turnAngle(pose.v[HEAD_A]);
        for(int k=0;k<LIMBS;++k)pose.v[LIMB_A+k]=turnAngle(pose.v[LIMB_A+k]);
        float mid=0;for(const auto &t:d.texels)mid+=t[0];
        if(!d.texels.empty())pose.v[PX]=mid/d.texels.size();
        // Leaning forward: the hand rigs' bodies lean toward where they face, in Doom's rotations 2 to 4 the
        // drawing's left (the pelvis behind the neck by 5% to 22% of the height side-on); the front
        // reference turned to the view stood bolt upright, and the zombieman's fits leaned back.
        pose.v[SPINE_A]+=0.25f*std::sqrt(std::max(0.0f,1-lateral*lateral));
    }
    Fit fit{ref,body,d,lateral,pose};
    float best=fit.cost(pose);
    static const float steps[PARAMS]={2,2,0.3f,0.3f,0.08f,0.35f,0.35f,0.35f,0.35f,0.35f,0.35f,0.35f,0.35f,
                                      0.1f,0.1f,0.1f,0.1f,0.1f,0.1f,0.1f,0.1f,0.2f,1.5f,0.1f};
    auto bounded=[&](Pose &p,int i) {
        if(i==SPINE_K)p.v[i]=std::clamp(p.v[i],0.85f,1.1f);
        if(i>=LIMB_K&&i<SH_W)p.v[i]=std::clamp(p.v[i],0.65f,1.05f);
        if(i==SH_W)p.v[i]=std::clamp(p.v[i],0.7f,1.8f);
        if(i==SH_D)p.v[i]=std::clamp(p.v[i],-3.0f,6.0f);
        if(i==HIP_W)p.v[i]=std::clamp(p.v[i],0.7f,1.4f);
    };
    for(float scale:{1.0f,0.5f,0.25f,0.12f,0.06f}) {
        if(!move)break;
        for(int pass=0;pass<3;++pass) {
            bool moved=false;
            for(int i=0;i<PARAMS;++i)
                for(float sign:{1.0f,-1.0f}) {
                    Pose tried=pose;tried.v[i]+=sign*steps[i]*scale;bounded(tried,i);
                    float c=fit.cost(tried);
                    if(c<best) {best=c;pose=tried;moved=true;break;}
                }
            if(!moved)break;
        }
    }
    place(body,pose,lateral,out.joint);
    out.cost=fit.shape(pose);
    if(!move&&start)memcpy(out.joint,start->joint,sizeof out.joint);
    for(int b=0;b<SKELETON_BONES;++b) {out.radius[b]=ref.radius[b];out.has[b]=ref.has[b];}
    if(!move&&start) {memcpy(out.depth,start->depth,sizeof out.depth);out.deep=start->deep;}
    skeletonOwners(out,image);
}

// Each texel to the bone it lies deepest in; with depth known (a 3D rig), among the bones whose flesh
// reaches it the one nearest the eye (an arm across the chest owns the chest's texels it covers).
void skeletonOwners(SpriteSkeleton &out,const Image &image) {
    int w=image.width,h=image.height;
    out.owner.assign((size_t)w*h,-1);
    int near=0,total=0;
    for(int r=0;r<h;++r)for(int c=0;c<w;++c) {
        if(!image.pixels[((size_t)r*w+c)*2+1])continue;
        float u=c-image.left+0.5f,hh=image.top-r-0.5f,deepest=1e9f,nearest=-1e9f;int bone=-1,front=-1;
        for(int b=0;b<SKELETON_BONES;++b) {
            if(!out.has[b])continue;
            int j0=skeletonBoneEnds[b][0],j1=skeletonBoneEnds[b][1];float t;
            float e=segment(u,hh,out.joint[j0],out.joint[j1],&t)-out.radius[b];
            if(e<deepest) {deepest=e;bone=b;}
            float z=out.depth[j0]+(out.depth[j1]-out.depth[j0])*t;
            if(out.deep&&e<=0.5f&&z>nearest) {nearest=z;front=b;}
        }
        out.owner[(size_t)r*w+c]=(int8_t)(front>=0?front:bone);
        ++total;near+=deepest<=1.0f;
    }
    out.fit=total?(float)near/total:0;
}
} // namespace doom3d
