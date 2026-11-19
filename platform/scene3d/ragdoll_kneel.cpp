// A DERP soldier killed down on one knee (p_lanes.c's P_TacKneel, drawn kneeling by kneel_view.cpp) falls from
// the kneel, not from standing: the game stands its body up as it dies (P_DerpKilled), and a doll raised from
// the standing pain drawing popped up a quarter of its height at the kill. Its doll starts as it was drawn: the
// right knee on the floor with the shin back along it rising to the toes, the left foot planted ahead under its
// knee, the hips where the kneeling picture has them (73% of standing, the upper body whole: kneelDrop), all
// still. Then it unfolds: the legs' muscles (ragdoll_doll.h's M_LEGS) hold the lengths they have folded and
// let them grow back to standing over kneelUnfold tics (Ragdoll::muscleRest), so the legs straighten out from
// under it as it goes over. Holding the standing lengths from the start sprang the folded legs straight in
// three tics and threw the body up out of the arena; weaker muscles didn't help, the solver's passes made
// them stiff again (arena, 2026-11-16). Bones only turn, so every bone keeps the rest's length.
#include "internal.h"
#include "ragdoll_doll.h"

namespace doom3d {
namespace {
constexpr float kneelDrop=0.27f;    // of the standing height: the kneeling picture's top stands 73% up
constexpr float kneelUnfold=20;     // tics until the legs' muscles hold the standing lengths again (it is down by then)
constexpr float shinRise=0.35f;     // the kneeling shin's rise to the toes, per unit back
constexpr float footAhead=0.85f;    // the planted foot ahead of the hip, of the thigh's length

// The knee of a leg from hip h to ankle a with these lengths, bent toward 'bend' (in the plane of the
// leg and that way). Out of reach, the leg straight toward the ankle.
V3 kneeBetween(V3 h,V3 a,float thigh,float shin,V3 bend) {
    V3 d=a-h;float l=length(d);
    if(l<1e-3f)return h+bend*thigh;
    V3 u=d*(1/l);
    if(l>=thigh+shin-1e-3f)return h+u*thigh;
    float along=(thigh*thigh-shin*shin+l*l)/(2*l),out=std::sqrt(std::max(0.0f,thigh*thigh-along*along));
    V3 n=bend-u*dot(bend,u);
    float nl=length(n);
    n=nl>1e-3f?n*(1/nl):V3{0,0,1};
    return h+u*along+n*out;
}
}

void kneelStart(Ragdoll &doll) {
    doll.knelt=true;
    V3 *p=doll.p.data();
    V3 fwd=doll.front();fwd.z=0;
    float fl=length(fwd);
    if(fl<1e-3f)return;
    fwd=fwd*(1/fl);
    float tall=p[P_HEAD].z+radius[P_HEAD],drop=kneelDrop*tall;
    // The upper body and the arms down whole.
    for(int n:{P_HEAD,P_NECK,P_SH_L,P_SH_R,P_SPINE,P_HIP_L,P_HIP_R})p[n].z-=drop;
    for(int n=DOLL_POINTS;n<POSE_POINTS;++n)p[n].z-=drop;
    for(int s=0;s<2;++s) {
        int hip=s?P_HIP_R:P_HIP_L,knee=s?P_KNEE_R:P_KNEE_L,ankle=s?P_ANK_R:P_ANK_L;
        const DollPose &rest=doll.body->rest;
        float thigh=length(rest[knee]-rest[hip]),shin=length(rest[ankle]-rest[knee]);
        V3 h=p[hip];
        if(s) {
            // Down on this knee: the knee on the floor, as far ahead of the hip as the thigh reaches; the
            // shin back from it, rising to the toes.
            float kz=radius[P_KNEE_R],fall=h.z-kz,ahead=std::sqrt(std::max(0.0f,thigh*thigh-fall*fall));
            V3 k=V3{h.x,h.y,kz}+fwd*ahead;
            if(fall>thigh)k=h+V3{0,0,-thigh};
            V3 back=unit(fwd*-1+V3{0,0,shinRise});
            p[knee]=k;p[ankle]=k+back*shin;
            p[ankle].z=std::max(p[ankle].z,radius[P_ANK_R]);
        } else {
            // This foot planted ahead, the knee up between.
            V3 a=V3{h.x,h.y,rest[ankle].z}+fwd*(footAhead*thigh);
            p[ankle]=a;p[knee]=kneeBetween(h,a,thigh,shin,fwd+V3{0,0,1});
        }
    }
    doll.prev=doll.p;
    for(size_t i=0;i<muscles[M_LEGS].size()&&i<6;++i) {const Link &l=muscles[M_LEGS][i];doll.kneelLen[i]=length(p[l.a]-p[l.b]);}
    doll.unfold=0;
}

void kneelTic(Ragdoll &doll,float muscle[MUSCLES]) {
    (void)muscle;
    if(!doll.knelt)return;
    float t=std::clamp((float)doll.tics/kneelUnfold,0.0f,1.0f);
    doll.unfold=t*t*(3-2*t);
}
} // namespace doom3d
