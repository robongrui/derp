/* Gore's bone map (PLAN-skin-suit.md 5): what lies under the skin at each
   texel of a drawing with a skeleton (skeletons.cpp), a skeleton that fills
   the body as a real one does (the user: the frame is good, it needs more
   bones). Built once per drawing from its rig: the skull filling the head with
   its eye and nose holes, the rib cage over the chest with the breastbone and
   the collarbones, the vertebrae down the back, the pelvis's wings, the upper
   arms and thighs as thick shafts with knobbly ends, the forearms and shins as
   two bones side by side, knuckles at the hands. Each texel goes by the bone
   that owns it (the nearer one where parts overlap) and where it lies on it:
   along the bone and across it, and how deep inside the outline (the body's
   flesh there). Fantasy anatomy at Doom's size: ribs two texels, gaps one. */
#include "internal.h"

namespace doom3d {
byte boneMapColour(int code) {
    static byte colours[4]={0,0,0,0};
    if(!colours[1]) {
        const byte *pal=playpal();
        auto nearest=[&](int r,int g,int b) {
            int best=16,d0=INT_MAX;
            for(int i=16;i<256;++i) {int dr=pal[i*3]-r,dg=pal[i*3+1]-g,db=pal[i*3+2]-b,d=dr*dr+dg*dg+db*db;if(d<d0) {d0=d;best=i;}}
            return (byte)best;
        };
        colours[1]=nearest(160,136,104);colours[2]=nearest(112,96,72);colours[3]=nearest(36,14,10);
    }
    return code>0&&code<4?colours[code]:0;
}

void buildBoneMap(SpriteSkeleton &s,const Image &image) {
    int w=image.width,h=image.height;
    auto opaque=[&](int c,int r){return c>=0&&r>=0&&c<w&&r<h&&image.pixels[((size_t)r*w+c)*2+1];};
    // How deep each texel lies inside the outline (chamfer distance to the nearest clear texel).
    std::vector<float> inside((size_t)w*h,0);
    for(int r=0;r<h;++r)for(int c=0;c<w;++c)inside[(size_t)r*w+c]=opaque(c,r)?1e4f:0;
    auto relax=[&](int c,int r,int dc,int dr,float step) {
        int c2=c+dc,r2=r+dr;float other=c2<0||r2<0||c2>=w||r2>=h?0:inside[(size_t)r2*w+c2];
        float &here=inside[(size_t)r*w+c];here=std::min(here,other+step);
    };
    for(int r=0;r<h;++r)for(int c=0;c<w;++c) {relax(c,r,-1,0,1);relax(c,r,0,-1,1);relax(c,r,-1,-1,1.4f);relax(c,r,1,-1,1.4f);}
    for(int r=h-1;r>=0;--r)for(int c=w-1;c>=0;--c) {relax(c,r,1,0,1);relax(c,r,0,1,1);relax(c,r,1,1,1.4f);relax(c,r,-1,1,1.4f);}
    // The view: how much of the front shows (1 front, -1 back), and which way the face looks across the drawing.
    float front=std::cos(s.yaw),faceAcross=-std::sin(s.yaw);
    auto length=[&](int b) {const float *p=s.joint[skeletonBoneEnds[b][0]],*q=s.joint[skeletonBoneEnds[b][1]];return std::hypot(q[0]-p[0],q[1]-p[1]);};
    float hipH=(s.joint[SJ_HIP0][1]+s.joint[SJ_HIP1][1])/2,hipU=(s.joint[SJ_HIP0][0]+s.joint[SJ_HIP1][0])/2;
    s.boneMap.assign((size_t)w*h,BONE_NONE);
    for(int r=0;r<h;++r)for(int c=0;c<w;++c) {
        size_t i=(size_t)r*w+c;
        int b=s.owner.empty()?-1:s.owner[i];
        if(!opaque(c,r)||b<0)continue;
        float u=c-image.left+0.5f,hh=image.top-r-0.5f,d=inside[i],t,a;
        skeletonPlace(s,b,u,hh,t,a);
        float half=std::abs(a)+d;                 // the flesh's half width here, across the bone
        float len=std::max(1.0f,length(b));
        int code=BONE_NONE;
        if(b==SB_HEAD) {
            // The skull fills the head but its rim; eye sockets and the nose hole where the face shows,
            // the jaw below them narrower.
            if(d>=0.9f)code=BONE_SOLID;
            if(t<0.4f&&std::abs(a)>0.7f*half)code=BONE_NONE;
            if(front>-0.3f) {
                float mid=faceAcross*0.45f*half,spread=0.33f*half*std::max(0.0f,front);
                for(float side:{-1.0f,1.0f}) {
                    float du=a-(mid+side*spread),dt=(t-0.78f)*len;
                    if(du*du+dt*dt<1.6f)code=BONE_HOLE;
                }
                if(std::abs(a-mid)<0.6f&&std::abs((t-0.6f)*len)<0.8f&&front>0.3f)code=BONE_HOLE;
            }
        } else if(b==SB_SPINE) {
            float units=t*len;
            if(t>0.45f) {
                // The rib cage: as wide as the chest at the top, narrowing toward its bottom; ribs every three
                // units drooping toward the sides, the breastbone down the front, the vertebrae down the back,
                // the collarbones across the top.
                float reach=half*(0.55f+0.4f*(t-0.45f)/0.55f);
                if(std::abs(a)<=reach&&d>=0.6f) {
                    float rib=units+0.12f*a*a;
                    code=std::fmod(rib,3.0f)<1.8f?BONE_SOLID:BONE_HOLE;   // the gaps between them dark: the cage reads
                    if(front>0.4f&&std::abs(a)<0.9f&&t>0.55f&&t<0.93f)code=BONE_SOLID;
                    if(front<-0.4f&&std::abs(a)<1.1f)code=std::fmod(units,2.5f)<1.8f?BONE_SOLID:BONE_HOLE;
                }
                if(t>0.92f&&t<0.99f&&d>=0.6f)code=BONE_SOLID;
            } else if(front<0.3f&&std::abs(a)<1.2f) {
                code=std::fmod(units,2.5f)<1.8f?BONE_SOLID:BONE_HOLE;   // the lower back's vertebrae
            }
        } else if(b==SB_PELVIS) {
            // The pelvis: wings rising over the hip joints, the hollow between them seen from the front.
            float up=hh-hipH;
            if(up>-1.5f&&up<4.0f&&d>=1.2f)code=BONE_SOLID;
            if(front>0.4f&&std::abs(u-hipU)<0.22f*len&&up<1.5f&&up>-2.5f)code=BONE_HOLE;
        } else {
            bool upper=b==SB_UPPER0||b==SB_UPPER1||b==SB_THIGH0||b==SB_THIGH1;
            bool hand=b==SB_FORE0||b==SB_FORE1;
            if(upper) {
                // One thick shaft, its ends knobbly.
                float shaft=std::max(0.6f,0.28f*half)+(t<0.1f||t>0.9f?0.7f:0.0f);
                if(std::abs(a)<=shaft)code=BONE_SOLID;
            } else {
                // Two bones side by side where the limb is wide enough, one where it isn't.
                // each a texel wide, a third of the flesh out from the middle
                if(half>=2.5f) {float off=std::abs(std::abs(a)-0.33f*half);if(off<0.6f)code=BONE_SOLID;}
                else if(std::abs(a)<0.6f)code=BONE_SOLID;
                if(hand&&t>0.85f)code=hashUnit(c,r,0x6b1du)<0.55f&&d>=0.5f?BONE_SOLID:BONE_NONE;   // knuckles
            }
        }
        s.boneMap[i]=(byte)code;
    }
    // Bone texels at the edge of a bone are drawn darker, so its shape reads at a few texels.
    std::vector<byte> shaded=s.boneMap;
    for(int r=0;r<h;++r)for(int c=0;c<w;++c) {
        size_t i=(size_t)r*w+c;
        if(s.boneMap[i]!=BONE_SOLID)continue;
        for(auto [dc,dr]:{std::pair{1,0},std::pair{-1,0},std::pair{0,1},std::pair{0,-1}}) {
            int c2=c+dc,r2=r+dr;
            if(opaque(c2,r2)&&s.boneMap[(size_t)r2*w+c2]==BONE_NONE) {shaded[i]=BONE_EDGE;break;}   // against flesh only: ribs stay pale between their gaps
        }
    }
    s.boneMap=std::move(shaded);
}
} // namespace doom3d
