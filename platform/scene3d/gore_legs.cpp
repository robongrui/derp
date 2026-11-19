/* Gore: a leg shot off by the killing blow (the user: "do we even have single legs, if not, we need
   them"). The pellets on a leg decide it as an arm's do (gore.cpp's goreLeg): under the knee the shin
   goes, on the thigh the whole leg at the hip. The leg's texels are thrown off as a flying piece, the
   body's card leaves them out with the stump's rim torn (ragdoll_card.cpp), the leg's points under the
   cut ride the hips out of the fall (Ragdoll::ridLegs), so that side has nothing to stand on, and the
   stump pumps a while. */
#include "ragdoll_doll.h"
#include "stump_rim.h"

namespace doom3d {
// The killing blow's pellets on each leg: within 2.5 units of its bones (hip to knee to ankle) and on its
// side of the hips' middle. Where they struck: any on the thigh, the leg goes at the hip.
void goreLegsOf(Ragdoll &doll,const mobj_t &thing,const std::vector<GorePellet> *gore,float acrossShot) {
    if(!gore||doll.halved)return;
    const DollPose &rest=doll.body->rest;
    auto near=[](V3 q,V3 a,V3 b) {V3 d=b-a;float t=std::clamp(dot(q-a,d)/std::max(1e-4f,dot(d,d)),0.0f,1.0f);return length(q-(a+d*t));};
    float mid=0.5f*(rest[P_HIP_L].x+rest[P_HIP_R].x);
    for(int s=0;s<2;++s) {
        V3 hip=rest[s?P_HIP_R:P_HIP_L],knee=rest[s?P_KNEE_R:P_KNEE_L],ankle=rest[s?P_ANK_R:P_ANK_L];
        float out=s?1.0f:-1.0f;
        int damage=0,pellets=0;bool thigh=false;
        for(const GorePellet &pellet:*gore) {
            V3 q{pellet.u*acrossShot,0,pellet.z};
            if((q.x-mid)*out<0)continue;
            float onThigh=near(q,hip,knee),onShin=near(q,knee,ankle);
            if(std::min(onThigh,onShin)>2.5f)continue;
            damage+=pellet.damage;++pellets;thigh=thigh||onThigh<onShin;
        }
        doll.legOff[s]=goreLeg(thing,damage,pellets)?(thigh?2:1):0;
        if(M_CheckParm((char*)"-ragdolllog"))fprintf(stderr,"goreleg side %d pellets %d damage %d off %d\n",s,pellets,damage,doll.legOff[s]);
    }
}

// The leg off: its texels (front as drawn, back from behind) thrown along the shot and out to its side,
// low, torn where it left the body; the stump gushes once.
void goreLegsOffStart(Ragdoll &doll,const mobj_t &thing,float dx,float dy,float blow) {
    const Relief *body=doll.body;
    float side=doll.mirror?-1.0f:1.0f;
    for(int s=0;s<2;++s) {
        if(!doll.legOff[s]||doll.legBlown[s])continue;   // (a blown-off leg is gore_blow.cpp's)
        int from=doll.legOff[s]==2?(s?P_HIP_R:P_HIP_L):(s?P_KNEE_R:P_KNEE_L);
        V3 cut=body->rest[from];
        struct Kept { float x,z;byte front,back,bone; };
        std::vector<Kept> kept;float mx=0,mz=0;
        for(const ReliefFace &face:body->faces) {
            if(face.kind!=FACE_FRONT||doll.legCut(face,s)!=0)continue;
            V3 c{};for(int k=0;k<4;++k)c=c+body->corners[face.corner[k]].p*0.25f;
            size_t texel=(size_t)face.r*body->width+face.c;
            kept.push_back({side*c.x,c.z,body->colour[texel],body->backColour[texel],stumpPieceBone(doll,texel)});
            mx+=side*c.x;mz+=c.z;
        }
        if(kept.size()<4)continue;
        mx/=kept.size();mz/=kept.size();
        // Torn where it left the body: the fifth of its texels nearest the cut.
        std::vector<std::pair<float,size_t>> byNear;
        for(size_t i=0;i<kept.size();++i)byNear.push_back({std::hypot(kept[i].x-side*cut.x,kept[i].z-cut.z),i});
        std::sort(byNear.begin(),byNear.end());
        std::vector<bool> torn(kept.size(),false);
        for(size_t i=0;i<std::max<size_t>(1,kept.size()/5);++i)torn[byNear[i].second]=true;
        std::vector<HeadTexel> leg;
        for(const Kept &t:kept)leg.push_back({(int)std::floor(t.x-mx),(int)std::floor(t.z-mz),t.front,t.back,t.bone});
        float wx,wy;doll.overGrid(V3{side*mx,0,0},wx,wy);
        float ox=side*(s?1.0f:-1.0f),ax=doll.ax[0]*ox,ay=doll.ax[1]*ox;
        float wl=std::max(1e-3f,std::hypot(dx+0.5f*ax,dy+0.5f*ay));
        FlyingPieceStart start{wx+doll.floors.x0,wy+doll.floors.y0,doll.gz+mz,(dx+0.5f*ax)/wl,(dy+0.5f*ay)/wl,units(thing.momx)+doll.walk[0],units(thing.momy)+doll.walk[1],
                               0.6f*blow,doll.goreSeed^(s?0x6c8e9cf5u:0x7a3b1d29u),bloodTint(&thing),bleedsBlood(thing),doll.guts,false,12,&torn,1.5f};
        doll.legPieces[s]=startFlyingPiece(leg,start);
        float cx,cy;doll.overGrid(cut,cx,cy);
        // The bone first; the tear and the stump's gush when its fibres have let go (tear_fibres.cpp).
        goreSound(GORE_SOUND_CRACK,cx+doll.floors.x0,cy+doll.floors.y0,doll.gz+cut.z);
        if(doll.legPieces[s]) {
            flyingPieceHold(*doll.legPieces[s],2);
            tearStart({&doll,&doll,from,doll.legPieces[s],nullptr,-1,TEAR_LEG,doll.goreSeed^(s?0x5bd1e995u:0x4cf5ad43u),bloodTint(&thing),bleedsBlood(thing),true,
                       doll.legOff[s]==2?3.0f:2.5f,(dx+0.5f*ax)/wl,(dy+0.5f*ay)/wl,0.8f});
        } else {
            goreSound(GORE_SOUND_TEAR,cx+doll.floors.x0,cy+doll.floors.y0,doll.gz+cut.z);
            if(bleedsBlood(thing))sprayGush(cx+doll.floors.x0,cy+doll.floors.y0,doll.gz+cut.z,(dx+0.5f*ax)/wl,(dy+0.5f*ay)/wl,0.8f,bloodTint(&thing));
        }
    }
}

// Each tic: the legs fly on, and each stump pumps out along the leg's way, a beat of its own.
void goreLegsOffTic(Ragdoll &doll,const mobj_t &thing,int target) {
    if(!doll.legOff[0]&&!doll.legOff[1])return;
    for(int k=0;k<2;++k)if(doll.legPieces[k])flyingPieceTic(*doll.legPieces[k],target);
    if(!bleedsBlood(thing))return;
    for(int t=std::max(doll.legPumped+1,target-1);t<=target;++t)
        for(int k=0;k<2;++k) {
            if(!doll.legOff[k]||doll.legBlown[k])continue;
            uint32_t seed=doll.goreSeed^(k?0x2b8e4f1du:0x63d1a7e5u);
            float strength=0.7f*stumpPulse(t,seed),stream=0.7f*stumpStream(t,seed);
            if(strength<=0&&stream<=0)continue;
            int from=doll.legOff[k]==2?(k?P_HIP_R:P_HIP_L):(k?P_KNEE_R:P_KNEE_L),toward=doll.legOff[k]==2?P_SPINE:(k?P_HIP_R:P_HIP_L);
            float sideSign=doll.mirror?-1.0f:1.0f,wx,wy;
            doll.overGrid(doll.p[from],wx,wy);
            V3 way=unit(doll.p[from]-doll.p[toward]);
            float vx=doll.ax[0]*sideSign*way.x+doll.ay[0]*way.y,vy=doll.ax[1]*sideSign*way.x+doll.ay[1]*way.y;
            float carry[3];doll.worldSpeed(from,carry);
            sprayStump(wx+doll.floors.x0,wy+doll.floors.y0,doll.gz+doll.p[from].z,vx,vy,way.z,strength,bloodTint(&thing),stream,carry);
        }
    doll.legPumped=std::max(doll.legPumped,target);
}
} // namespace doom3d
