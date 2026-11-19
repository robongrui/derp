/* Gore on a ragdoll (gore.cpp, PLAN-gore.md M, N): what the killing blow's
   pellets do to the head, the head bursting or flying off at the kill, and
   each tic the severed head's flight and the stump's pump. The body's card
   leaves the lost texels out (ragdoll_card.cpp). */
#include "ragdoll_doll.h"
#include "stump_rim.h"

namespace doom3d {
// A big monster's cut (goreHefty): the top's slide off its legs along the blow, and what its height adds, a tic; its gushes.
constexpr float heftySlide=1.2f,heftyOver=1.0f,heftyGush=2.6f;
// Gore: where each pellet of the killing blow landed decides the head (gore.cpp): on it (above the
// neck's band) or in the band at the neck, across as wide as the head. The head where the pain drawing
// has it (the zombieman's flinch leans it 8 units aside) or over the middle, where the drawings the
// player shoots at (walking, firing) carry it: aimed at the middle, a shot to the face missed the pain
// drawing's head. Blown apart: the head's gone (strings cut, the head's fall).
// acrossShot: the body's x across the shot (as startRagdoll's); seed: the kill's own.
GoreHead goreHeadOf(Ragdoll &doll,const mobj_t &thing,const std::vector<GorePellet> *gore,float acrossShot,uint32_t seed) {
    GoreHead outcome=GORE_HEAD_STAYS;
    const Relief *body=doll.body;
    if(!gore||!body->hasHead)return outcome;
    constexpr float band=1.2f;   // the neck's band: half its height, units
    int headDamage=0,headPellets=0,neckDamage=0,neckPellets=0;float headX=0,headZ=0;
    for(const GorePellet &pellet:*gore) {
        float u=pellet.u*acrossShot;
        if(std::abs(u-body->headMiddle)>body->headWidth/2+1&&std::abs(u)>body->headWidth/2+1)continue;
        if(std::abs(pellet.z-body->neck)<band) {neckDamage+=pellet.damage;++neckPellets;}
        else if(pellet.z>body->neck) {
            headDamage+=pellet.damage;++headPellets;headZ+=pellet.z;
            headX+=std::abs(u-body->headMiddle)<=body->headWidth/2+1?u:body->headMiddle+u;   // on the head as drawn, or as seen
        }
    }
    outcome=goreHead(thing,headDamage,headPellets,neckDamage,neckPellets);
    // The pistol's head kill: the rule kills, whatever the bullet did, so its damage can't decide. It always shows.
    if(outcome==GORE_HEAD_STAYS&&Game_HeadKill(&thing))outcome=GORE_HEAD_TORN;
    if(outcome==GORE_HEAD_TORN) {
        // The bite: on the head's edge the way the pellets struck from its middle (straight through the
        // middle: the top of the skull, tipped a little), as wide as the blow.
        uint32_t s=seed^0x3c6ef372u;
        auto rnd=[&] {s=hash32(s+0x9e3779b9u);return (float)(s>>8)/16777216.0f;};
        std::set<std::pair<int,int>> cells;float mx=0,mz=0;
        for(const ReliefFace &face:body->faces) {
            if(face.kind!=FACE_FRONT||!overNeck(*body,face,0.5f)||headShare(*body,face)<0.5f)continue;
            V3 c{};for(int k=0;k<4;++k)c=c+body->corners[face.corner[k]].p*0.25f;
            cells.insert({(int)std::floor(c.x),(int)std::floor(c.z)});mx+=c.x;mz+=c.z;
        }
        if(!cells.empty()) {
            mx/=cells.size();mz/=cells.size();
            float wx=headPellets?headX/headPellets-mx:0,wz=headPellets?headZ/headPellets-mz:0;
            float a=std::hypot(wx,wz)>1?std::atan2(wz,wx):doomPi/2+(rnd()*2-1)*0.7f;
            float ex=mx,ez=mz;
            for(float t=0;t<16;t+=0.5f) {
                float x=mx+std::cos(a)*t,z=mz+std::sin(a)*t;
                if(!cells.count({(int)std::floor(x),(int)std::floor(z)}))break;
                ex=x;ez=z;
            }
            doll.torn=true;doll.biteX=ex;doll.biteZ=ez;
            doll.biteR=std::clamp(2.5f+1.5f*rnd()+headDamage/40.0f,2.5f,5.5f);
        }
    }
    if(M_CheckParm((char*)"-ragdolllog"))fprintf(stderr,"gorehead pellets %zu head %d/%d neck %d/%d outcome %d\n",
        gore->size(),headPellets,headDamage,neckPellets,neckDamage,(int)outcome);
    return outcome;
}

// Cut in half: four pellets or more in the band from the thighs up through the waist, across as wide as
// the shoulders (gore.cpp's rule). The cut halfway between the hips and the spine joint, a little higher
// or lower each time. Struck mostly under it (the pellets' middle under the cut): the halves' mirror, the
// legs knocked out from under it and thrown, the top dropping where it stood.
bool goreHalvesOf(Ragdoll &doll,const mobj_t &thing,const std::vector<GorePellet> *gore,float acrossShot,uint32_t seed,bool force) {
    if(!gore)return false;
    const Relief *body=doll.body;
    const DollPose &rest=body->rest;
    float waist=0.5f*(0.5f*(rest[P_HIP_L].z+rest[P_HIP_R].z)+rest[P_SPINE].z),half=std::abs(rest[P_SH_R].x-rest[P_SH_L].x)/2+3;
    constexpr float band=3;   // over the waist, units; under it halfway down the thighs (lower: a leg, gore_legs.cpp)
    float knees=0.5f*(0.5f*(rest[P_KNEE_L].z+rest[P_KNEE_R].z)+0.5f*(rest[P_HIP_L].z+rest[P_HIP_R].z));
    int damage=0,pellets=0;float middle=0;
    for(const GorePellet &pellet:*gore)
        if(pellet.z<waist+band&&pellet.z>knees&&std::abs(pellet.u*acrossShot-rest[P_SPINE].x)<=half) {damage+=pellet.damage;++pellets;middle+=pellet.z;}
    bool cut=force||goreHalves(thing,damage,pellets);
    if(M_CheckParm((char*)"-ragdolllog"))fprintf(stderr,"gorewaist pellets %d damage %d halves %d middle %.1f\n",pellets,damage,(int)cut,pellets?middle/pellets:0.0f);
    if(!cut)return false;
    doll.halved=true;doll.cutZ=waist+2*((float)(hash32(seed^0x7f4a7c15u)>>8)/16777216.0f-0.5f);
    static int low=-1;
    if(low<0) {int arg=M_CheckParm((char*)"-gorecut");low=arg&&arg+1<myargc&&!strcmp(myargv[arg+1],"halveslow");}
    doll.legsFly=low||(!goreHefty(thing)&&pellets&&middle/pellets<doll.cutZ-1);
    return true;
}

// Cut in half: the legs, the texels the card now leaves out below the cut, left standing where it died
// (as the eye saw them: their back's colours from behind, mirrored as drawn), the upper half falling.
void goreLegsStart(Ragdoll &doll,const mobj_t &thing,float dx,float dy,float blow) {
    if(!doll.halved)return;
    // The top claws ahead once or twice when it comes down (clawTic), if it has an arm to do it with.
    if(doll.body->arms[0]||doll.body->arms[1])doll.clawCycles=1+(hash32(doll.goreSeed^0x2a7f13c1u)&1);
    // Heard: torn through at the waist, the spine snapping.
    goreSound(GORE_SOUND_TEAR,units(thing.x),units(thing.y),doll.gz+doll.cutZ);
    // Two gushes, the top's and the legs': cut through at the middle, the most a body loses at once short of
    // bursting (the user: "the cut in half part needs still more blood").
    if(bleedsBlood(thing)) {
        float gush=goreHefty(thing)?heftyGush:1.6f;
        sprayGush(units(thing.x),units(thing.y),doll.gz+doll.cutZ+2,dx,dy,gush,bloodTint(&thing));
        sprayGush(units(thing.x),units(thing.y),doll.gz+doll.cutZ-2,-dx,-dy,gush,bloodTint(&thing));
        sprayCutSpill(thing,units(thing.x),units(thing.y),doll.gz+doll.cutZ);
    }
    goreSound(GORE_SOUND_CRACK,units(thing.x),units(thing.y),doll.gz+doll.cutZ);
    doll.unleg();
    if(doll.legsFly) {
        // The mirror: the top drops where it stood (nothing carries it); the legs, a doll of their own
        // (lowerHalf: the same body, the upper points riding the pelvis), are knocked out along the shot,
        // up and over, limp. A blast's slash keeps the blast's throw (gore_blow.cpp): both halves go.
        bool thrown=doll.cutSlant!=0&&doll.blasted;
        if(!thrown) {
        for(int n=0;n<POSE_POINTS;++n)doll.prev[n]=doll.p[n];
        doll.launchAt=-1;doll.launchV={};doll.launchSpin=doll.launchTwist=0;
        }
        // Not guided either: its ratchet pushed the chest along the shot, the riding legs taking the push
        // back, and the top walked off after the legs.
        doll.guided=doll.swept=false;
        // Left alone it sank onto its stump and stayed propped there like a bust. The legs leaving drag the
        // stump a little after them; the chest goes over the other way, toward the shooter.
        if(!thrown) {
            uint32_t s=doll.goreSeed^0x1b873593u;
            float over=(0.9f+0.6f*(float)(hash32(s)>>8)/16777216.0f)*blow;
            for(int n=0;n<DOLL_POINTS;++n)
                if(!doll.rider(n))doll.push(n,doll.tipWay*(0.5f-over*std::max(0.0f,doll.p[n].z-doll.cutZ)/30));
        }
        auto legs=std::make_shared<Ragdoll>(doll);
        Ragdoll &l=*legs;
        l.legsDoll=nullptr;l.burst=nullptr;l.flying=nullptr;l.legs=nullptr;
        l.lowerHalf=true;l.headless=l.torn=l.guided=l.swept=false;l.cardKey=0;l.pooled=false;
        l.hold[M_LEGS]=0;l.over[M_LEGS]=1;l.late=0;
        l.unbody();
        float side=doll.mirror?-1.0f:1.0f;
        V3 way=unit(V3{side*(dx*doll.ax[0]+dy*doll.ax[1]),dx*doll.ay[0]+dy*doll.ay[1],0});
        uint32_t s=doll.goreSeed^0x6c8e9cf5u;
        auto rnd=[&] {s=hash32(s+0x9e3779b9u);return (float)(s>>8)/16777216.0f;};
        l.launchV=way*((1.6f+0.8f*rnd())*blow)+V3{0,0,1.8f+1.2f*rnd()};
        l.launchWay=way;l.launchSpin=-(0.10f+0.08f*rnd());l.launchTwist=(rnd()*2-1)*0.04f;l.launchAt=0;
        if(thrown)l.launchV=doll.launchV*0.8f+way*(0.6f*blow);   // the blast's throw, a little apart from the top
        doll.legsDoll=legs;
        // Fibres across the cut, the spine's end to the legs' (tear_fibres.cpp); its gushes were had already.
        tearStart({&doll,&doll,P_SPINE,nullptr,legs.get(),-2,TEAR_HALVES,doll.goreSeed^0x1f83d9abu,bloodTint(&thing),bleedsBlood(thing),false,6.0f,dx,dy,0});
        return;
    }
    // Too big for anything to knock its legs away: the top slides off them along the blow and goes over.
    if(goreHefty(thing))
        for(int n=0;n<DOLL_POINTS;++n)
            if(!doll.rider(n))doll.push(n,doll.tipWay*(heftySlide+heftyOver*std::max(0.0f,doll.p[n].z-doll.cutZ)/30));
    // By default the legs are the monster's own death frames with the top cut away (geometry.cpp,
    // cutMask): pushed by the blast, they go over backward as Doom drew it. -gorelegsrig: the legs as a
    // picture of their own on a small rig, crumbling over sideways (standing_legs.cpp).
    static int rig=-1;
    if(rig<0)rig=M_CheckParm((char*)"-gorelegsrig")!=0;
    if(!rig)return;
    const Relief *body=doll.body;
    bool behind=doll.toEye.y>0;
    float sx=(doll.mirror?-1.0f:1.0f)*(behind?-1.0f:1.0f);
    struct Kept { float x,z;byte colour; };
    std::vector<Kept> kept;float mx=0;
    for(const ReliefFace &face:body->faces) {
        if(face.kind!=FACE_FRONT||doll.cut(face)!=0)continue;
        V3 c{};for(int k=0;k<4;++k)c=c+body->corners[face.corner[k]].p*0.25f;
        size_t texel=(size_t)face.r*body->width+face.c;
        kept.push_back({sx*c.x,c.z,behind?body->backColour[texel]:body->colour[texel]});mx+=sx*c.x;
    }
    if(kept.empty())return;
    mx/=kept.size();
    std::vector<HeadTexel> legs;
    for(const Kept &t:kept)legs.push_back({(int)std::floor(t.x-mx),(int)std::floor(t.z),t.colour,t.colour});
    float wx,wy;doll.overGrid(V3{sx*mx,0,0},wx,wy);
    StandingLegsStart start{wx+doll.floors.x0,wy+doll.floors.y0,doll.gz,{},doll.goreSeed^0x0b4611a5u,bloodTint(&thing),bleedsBlood(thing)};
    static const int joints[6]={P_HIP_L,P_HIP_R,P_KNEE_L,P_KNEE_R,P_ANK_L,P_ANK_R};
    for(int j=0;j<6;++j) {start.joints[j][0]=sx*body->rest[joints[j]].x-mx;start.joints[j][1]=body->rest[joints[j]].z;}
    doll.legs=startStandingLegs(legs,start);
}

// At the kill, once the doll knows its floors: the head (or the bite out of it) bursts or flies off.
// dx, dy: the shot's way in the world; hitU, hitZ: where it struck (body x, height); tx: the shot's
// way across the body; blow: 1 at a shotgun kill.
// posed: on a corpse (goreCorpseHit), the head where the body has it now, not where it stood at the kill.
void goreHeadStart(Ragdoll &doll,const mobj_t &thing,GoreHead outcome,float dx,float dy,float hitU,float hitZ,float tx,float blow,bool posed) {
    if(!doll.headless&&!doll.torn)return;
    const Relief *body=doll.body;
    if(doll.headless)doll.unhead();
    // The head (or the bite out of it) as the eye saw it (its back's colours from behind, mirrored as drawn) bursts where it
    // was, or flies off whole: the texels the card now leaves out.
    bool behind=doll.toEye.y>0,severed=outcome==GORE_HEAD_SEVERED;
    float side=doll.mirror?-1.0f:1.0f,sx=side*(behind?-1.0f:1.0f);
    struct Lost { float x,z;byte front,back,bone; };
    std::vector<Lost> lost;float mx=0,mz=0;
    for(const ReliefFace &face:body->faces) {
        if(face.kind!=FACE_FRONT||(doll.torn?doll.bite(face)!=0:!headLost(*body,face,headShare(*body,face),doll.goreSeed)))continue;
        V3 c{};for(int k=0;k<4;++k)c=c+body->corners[face.corner[k]].p*0.25f;
        size_t texel=(size_t)face.r*body->width+face.c;
        lost.push_back({c.x,c.z,body->colour[texel],body->backColour[texel],stumpPieceBone(doll,texel)});
        mx+=sx*c.x;mz+=c.z;
    }
    if(!lost.empty()) {
        mx/=lost.size();mz/=lost.size();
        // Picture x: the burst as the eye sees it, the flying head as drawn (it shows both sides).
        float px=severed?side:sx,pm=px*sx*mx;
        std::vector<HeadTexel> head;
        for(const Lost &t:lost) {
            byte seen=!severed&&behind?t.back:t.front;
            head.push_back({(int)std::floor(px*t.x-pm),(int)std::floor(t.z-mz),seen,t.back,severed?t.bone:(byte)0});
        }
        // Hanging where the head's own texels were (the head point rides on out along the neck), struck
        // where the shot met it: on the pain drawing's head, or, a shot at the middle, the head's middle
        // as the eye saw it then.
        float hx,hy;doll.overGrid(V3{sx*mx,doll.p[P_NECK].y,0},hx,hy);
        if(posed) {doll.overGrid(doll.p[P_HEAD],hx,hy);mz=doll.p[P_HEAD].z;}
        bool onPainHead=std::abs(hitU-body->headMiddle)<=body->headWidth/2+1;
        float nx,ny;doll.overGrid(doll.p[P_NECK],nx,ny);
        doll.burstAt[0]=hx-nx;doll.burstAt[1]=hy-ny;doll.burstAt[2]=mz-doll.p[P_NECK].z;
        // Heard: the skull going (blown apart, its wet burst is the spray's; a chunk torn out), or the neck
        // torn through and its bone snapping.
        float wx=hx+doll.floors.x0,wy=hy+doll.floors.y0,wz=doll.gz+mz;
        goreSound(GORE_SOUND_CRACK,wx,wy,wz);
        if(severed) {
            FlyingPieceStart start{hx+doll.floors.x0,hy+doll.floors.y0,doll.gz+mz,dx,dy,units(thing.momx)+(posed?0:doll.walk[0]),units(thing.momy)+(posed?0:doll.walk[1]),blow,
                                  doll.goreSeed^0x2545f491u,bloodTint(&thing),bleedsBlood(thing),doll.guts,false,16};
            doll.flying=startFlyingPiece(head,start);
            // Held to the neck a few tics by its fibres (tear_fibres.cpp): the bone cracked now, the tear heard and
            // the neck gushing when the last of them lets go.
            if(doll.flying) {
                flyingPieceHold(*doll.flying,2);
                tearStart({&doll,&doll,P_NECK,doll.flying,nullptr,-1,TEAR_HEAD,doll.goreSeed^0x51ed270bu,bloodTint(&thing),bleedsBlood(thing),true,2.5f,dx,dy,0.9f});
            } else goreSound(GORE_SOUND_TEAR,wx,wy,wz);
        } else {
            HeadBurstStart start{onPainHead?sx*hitU-mx:sx*hitU,hitZ-mz,sx*tx,doll.torn?0.6f*blow:blow,
                                 doll.goreSeed^0x1b873593u,bloodTint(&thing),bleedsBlood(thing),doll.guts};
            doll.burst=startHeadBurst(head,start);
        }
        // The blood thrown from the same spot (from the head point, out along the neck, the drops flew
        // in a clump beside the burst).
        if(bleedsBlood(thing)&&!severed&&!doll.torn)sprayHeadPop(hx+doll.floors.x0,hy+doll.floors.y0,doll.gz+mz,dx,dy,bloodTint(&thing));
        // And gushing where it tore (a torn skull, the neck of one severed or blown apart).
        if(bleedsBlood(thing)&&!(severed&&doll.flying))sprayGush(hx+doll.floors.x0,hy+doll.floors.y0,doll.gz+mz,dx,dy,severed?0.9f:doll.torn?0.5f:1.0f,bloodTint(&thing));
    }
}

// Arms off (gore.cpp's goreArm): the killing blow's pellets on an arm, within 2.5 units of its bones
// (shoulder to elbow to hand) and not over the torso (outward of the shoulder). Only arms the drawing
// shows beside the body come off (the zombieman's other arm is on his rifle).
void goreArmsOf(Ragdoll &doll,const mobj_t &thing,const std::vector<GorePellet> *gore,float acrossShot) {
    if(!gore)return;
    const DollPose &rest=doll.body->rest;
    auto near=[](V3 q,V3 a,V3 b) {V3 d=b-a;float t=std::clamp(dot(q-a,d)/std::max(1e-4f,dot(d,d)),0.0f,1.0f);return length(q-(a+d*t));};
    for(int s=0;s<2;++s) {
        if(!doll.body->arms[s])continue;
        V3 sh=rest[s?P_SH_R:P_SH_L],el=rest[s?P_ELB_R:P_ELB_L],hd=rest[s?P_HAND_R:P_HAND_L];
        float out=s?1.0f:-1.0f;
        int damage=0,pellets=0;
        for(const GorePellet &pellet:*gore) {
            V3 q{pellet.u*acrossShot,0,pellet.z};
            // (A little inside the shoulder too: an arm hanging in front of the body, the imp's, took none.)
            if((q.x-sh.x)*out<-1.5f||std::min(near(q,sh,el),near(q,el,hd))>3.0f)continue;
            damage+=pellet.damage;++pellets;
        }
        doll.armOff[s]=goreArm(thing,damage,pellets);
        if(M_CheckParm((char*)"-ragdolllog"))fprintf(stderr,"gorearm side %d pellets %d damage %d off %d\n",s,pellets,damage,(int)doll.armOff[s]);
    }
}
// An explosion that killed, close (goreBlastLimbs): a limb or two torn off on the side facing it, the legs
// when it went off low, the arms when high, else either; thrown with the blast as the body is (the user).
// tx: the blast's push across the body (body x), so the near side is the other way; high: where it went
// off on the body (1 well above the hip, -1 at the legs).
void goreBlastLimbsOf(Ragdoll &doll,const mobj_t &thing,float tx,float high,uint32_t seed) {
    int limbs=goreBlastLimbs(thing);
    if(limbs<=0||doll.halved)return;
    auto rnd=[&] {seed=hash32(seed+0x9e3779b9u);return (float)(seed>>8)/16777216.0f;};
    if(limbs==2&&rnd()<0.3f)limbs=1;   // (two bodies by one blast: not the same cut on both)
    int near=std::abs(tx)>0.3f?(tx>0?0:1):(rnd()<0.5f?0:1);
    struct Limb { bool arm;int s; };
    std::vector<Limb> order;
    auto add=[&](bool arm,int s) {if(arm?doll.body->arms[s]&&!doll.armOff[s]:!doll.legOff[s])order.push_back({arm,s});};
    bool low=high<-0.2f||(high<=0.2f&&rnd()<0.5f);
    if(rnd()<0.25f)low=!low;
    if(low) {add(false,near);add(false,1-near);add(true,near);}
    else {add(true,near);add(true,1-near);add(false,near);}
    if(order.size()>=3&&rnd()<0.5f)std::swap(order[1],order[2]);   // the second: the other of the pair, or the near one of the others
    for(int i=0;i<limbs&&i<(int)order.size();++i) {
        if(order[i].arm)doll.armOff[order[i].s]=true;
        else doll.legOff[order[i].s]=rnd()<0.35f?2:1;
    }
    if(M_CheckParm((char*)"-ragdolllog"))fprintf(stderr,"goreblast limbs %d arms %d %d legs %d %d\n",limbs,(int)doll.armOff[0],(int)doll.armOff[1],doll.legOff[0],doll.legOff[1]);
}

// An arm off: its texels (front as drawn, back from behind) thrown off as a flying piece along the
// shot and outward, torn where it left the shoulder; the shoulder's rim left on the body (the card).
// posed, only: on a corpse (goreCorpseHit), that arm alone, from where the body has its elbow now.
void goreArmsStart(Ragdoll &doll,const mobj_t &thing,float dx,float dy,float blow,bool posed,int only) {
    const Relief *body=doll.body;
    float side=doll.mirror?-1.0f:1.0f;
    for(int s=0;s<2;++s) {
        if(!doll.armOff[s]||(only>=0&&s!=only))continue;
        struct Kept { float x,z;byte front,back;bool torn;byte bone; };
        std::vector<Kept> kept;float mx=0,mz=0;
        V3 sh=body->rest[s?P_SH_R:P_SH_L];
        for(const ReliefFace &face:body->faces) {
            if(face.kind!=FACE_FRONT)continue;
            if(doll.armCut(face,s)!=0)continue;
            V3 c{};for(int k=0;k<4;++k)c=c+body->corners[face.corner[k]].p*0.25f;
            size_t texel=(size_t)face.r*body->width+face.c;
            kept.push_back({side*c.x,c.z,body->colour[texel],body->backColour[texel],false,stumpPieceBone(doll,texel)});
            mx+=side*c.x;mz+=c.z;
        }
        if(kept.size()<4)continue;
        mx/=kept.size();mz/=kept.size();
        // Torn where it left the body: the texels nearest the shoulder (a fifth of them).
        std::vector<std::pair<float,size_t>> byNear;
        for(size_t i=0;i<kept.size();++i)byNear.push_back({std::hypot(kept[i].x-side*sh.x,kept[i].z-sh.z),i});
        std::sort(byNear.begin(),byNear.end());
        for(size_t i=0;i<std::max<size_t>(1,kept.size()/5);++i)kept[byNear[i].second].torn=true;
        std::vector<HeadTexel> arm;std::vector<bool> torn;
        for(const Kept &t:kept) {arm.push_back({(int)std::floor(t.x-mx),(int)std::floor(t.z-mz),t.front,t.back,t.bone});torn.push_back(t.torn);}
        float wx,wy;doll.overGrid(V3{side*mx,0,0},wx,wy);
        if(posed) {int el=s?P_ELB_R:P_ELB_L;doll.overGrid(doll.p[el],wx,wy);mz=doll.p[el].z;sh=doll.p[s?P_SH_R:P_SH_L];}
        // Along the shot and out to its side.
        float ox=side*(s?1.0f:-1.0f),ax=doll.ax[0]*ox,ay=doll.ax[1]*ox;
        float wl=std::max(1e-3f,std::hypot(dx+0.6f*ax,dy+0.6f*ay));
        FlyingPieceStart start{wx+doll.floors.x0,wy+doll.floors.y0,doll.gz+mz,(dx+0.6f*ax)/wl,(dy+0.6f*ay)/wl,units(thing.momx)+(posed?0:doll.walk[0]),units(thing.momy)+(posed?0:doll.walk[1]),
                               0.8f*blow,doll.goreSeed^(s?0x3c6ef372u:0xa54ff53au),bloodTint(&thing),bleedsBlood(thing),doll.guts,false,12,&torn,2.5f};
        doll.armPieces[s]=startFlyingPiece(arm,start);
        float sx,sy;doll.overGrid(sh,sx,sy);
        // The bone first; the tear and the shoulder's gush when its fibres have let go (tear_fibres.cpp).
        goreSound(GORE_SOUND_CRACK,sx+doll.floors.x0,sy+doll.floors.y0,doll.gz+sh.z);
        if(doll.armPieces[s]) {
            flyingPieceHold(*doll.armPieces[s],2);
            tearStart({&doll,&doll,s?P_SH_R:P_SH_L,doll.armPieces[s],nullptr,-1,TEAR_ARM,doll.goreSeed^(s?0x2c1b3c6du:0x297a2d39u),bloodTint(&thing),bleedsBlood(thing),true,2.0f,
                       (dx+0.6f*ax)/wl,(dy+0.6f*ay)/wl,0.9f});
        } else {
            goreSound(GORE_SOUND_TEAR,sx+doll.floors.x0,sy+doll.floors.y0,doll.gz+sh.z);
            if(bleedsBlood(thing))sprayGush(sx+doll.floors.x0,sy+doll.floors.y0,doll.gz+sh.z,(dx+0.6f*ax)/wl,(dy+0.6f*ay)/wl,0.9f,bloodTint(&thing));
        }
    }
}

// Shots into a corpse (PLAN-gore.md D; the user: "while the body is dead it could tear out chunks"), 10 a
// shot, by the part struck. The head: a chunk torn out of the skull at half the monster's health (20 to 80),
// blown apart at all of it. An arm the drawing shows: off at the shoulder at half. The torso: the whole body
// bursts at four times it (60 to 200; 2: the caller bursts it). Each cut is made where the body lies now, its burst or
// flight on a clock of its own from now, and the body's picture drawn again without what it lost.
// A chunk shot out of the corpse at a doll point (the torso, the legs): a ragged bite round where the
// drawing has that point (jittered 2.5 units, 2.5-4.5 across), its texels thrown off as a small chunk raw
// at its edge from where the body has the point now; at most 8. (Hits there tore nothing out before the
// whole body burst: the user, shooting corpses, "they dont lose anything".)
static void corpseBite(Ragdoll &doll,const mobj_t &thing,int point,float dx,float dy,int tic) {
    if(doll.bites.size()>=8)return;
    const Relief *body=doll.body;
    uint32_t s=hash32(doll.goreSeed^(uint32_t)tic*2654435761u^(uint32_t)point*40503u);
    auto rnd=[&] {s=hash32(s+0x9e3779b9u);return (float)(s>>8)/16777216.0f;};
    V3 at=body->rest[point];
    doll.bites.push_back({at.x+(rnd()*2-1)*2.5f,at.z+(rnd()*2-1)*2.5f,2.5f+2*rnd()});
    int bite=(int)doll.bites.size()-1;
    const Ragdoll::Bite &b=doll.bites.back();
    float side=doll.mirror?-1.0f:1.0f;
    std::vector<HeadTexel> chunk;std::vector<bool> torn;
    for(const ReliefFace &face:body->faces) {
        if(face.kind!=FACE_FRONT||doll.bitten(face,bite)!=0)continue;
        V3 c{};for(int k=0;k<4;++k)c=c+body->corners[face.corner[k]].p*0.25f;
        size_t texel=(size_t)face.r*body->width+face.c;
        chunk.push_back({(int)std::floor(side*(c.x-b.x)),(int)std::floor(c.z-b.z),body->colour[texel],body->backColour[texel]});
        torn.push_back(std::hypot(c.x-b.x,c.z-b.z)>0.55f*b.r);
    }
    ++doll.cutSerial;
    float wx,wy;doll.overGrid(doll.p[point],wx,wy);
    if(bleedsBlood(thing))sprayGush(wx+doll.floors.x0,wy+doll.floors.y0,doll.gz+doll.p[point].z+1,dx,dy,0.3f+0.08f*b.r,bloodTint(&thing));
    if(chunk.size()<4)return;
    float a=rnd()*2*doomPi,ox=0.5f*std::cos(a),oy=0.5f*std::sin(a),wl=std::max(1e-3f,std::hypot(dx+ox,dy+oy));
    FlyingPieceStart start{wx+doll.floors.x0,wy+doll.floors.y0,doll.gz+doll.p[point].z+1,(dx+ox)/wl,(dy+oy)/wl,0,0,
                           0.35f,s,bloodTint(&thing),bleedsBlood(thing),doll.guts,false,8,&torn,2.0f};
    if(auto piece=startFlyingPiece(chunk,start)) {
        doll.chunks.push_back(piece);doll.chunkFrom.push_back(tic);
        tearStart({&doll,&doll,point,piece,nullptr,-1,TEAR_CHUNK,s,bloodTint(&thing),bleedsBlood(thing),false,1.5f,dx,dy,0});
    }
}
int goreCorpseHit(Ragdoll &doll,const mobj_t &thing,int part,int point,float dx,float dy,int tic) {
    if(doll.lowerHalf)return 0;
    const Relief *body=doll.body;
    int health=std::clamp(thing.info?thing.info->spawnhealth:20,20,80);
    if(part==CORPSE_HEAD&&(doll.headless||!body->hasHead))part=CORPSE_TORSO;
    if((part==CORPSE_ARM_L||part==CORPSE_ARM_R)&&(!body->arms[part-CORPSE_ARM_L]||doll.armOff[part-CORPSE_ARM_L]||doll.armBlown[part-CORPSE_ARM_L]))part=CORPSE_TORSO;
    int &taken=doll.corpseHits[part];taken+=10;
    float wx,wy;
    switch(part) {
    case CORPSE_HEAD:
        if(taken>=health) {
            doll.headless=true;doll.torn=false;
            goreHeadStart(doll,thing,GORE_HEAD_BLOWN,dx,dy,body->headMiddle,body->rest[P_HEAD].z,0,0.8f,true);
            doll.burstFrom=tic;++doll.cutSerial;
        } else if(taken>=health/2&&!doll.torn) {
            doll.torn=true;doll.biteX=body->headMiddle;doll.biteZ=body->rest[P_HEAD].z+1;
            doll.biteR=2.5f+2*(float)(hash32(doll.goreSeed^(uint32_t)tic)>>8)/16777216.0f;
            goreHeadStart(doll,thing,GORE_HEAD_TORN,dx,dy,body->headMiddle,body->rest[P_HEAD].z,0,0.6f,true);
            doll.burstFrom=tic;++doll.cutSerial;
        }
        break;
    case CORPSE_ARM_L:case CORPSE_ARM_R: {
        int s=part-CORPSE_ARM_L;
        if(taken>=health/2) {
            doll.armOff[s]=true;
            goreArmsStart(doll,thing,dx,dy,0.3f,true,s);   // a few bullets: a short throw (at 0.6 it flew 300 units)
            doll.armFrom[s]=tic;++doll.cutSerial;
        }
        break;
    }
    default:
        // The whole body only at four times the monster's health, 60 to 200 (at its health a zombieman burst at
        // the second bullet: no dance, and too easy); before that every hit but the first tears a chunk out.
        if(taken>=std::clamp(4*(thing.info?thing.info->spawnhealth:20),60,200))return 2;
        if(taken>=20&&point<DOLL_POINTS)corpseBite(doll,thing,point,dx,dy,tic);
    }
    (void)wx;(void)wy;
    return 0;
}

// Each tic (target: tics since the kill) the severed head flies on and the stump pumps.
void goreTic(Ragdoll &doll,const mobj_t &thing,int target) {
    // Shot out of the air (Authored's kick): blood off it for its first tics' flight, a streak the eye follows.
    if(doll.airKill&&target>doll.trailed&&target<=22&&bleedsBlood(thing)) {
        float x,y,z;
        doll.trailed=target;
        ragdollWhere(doll,1,x,y,z);
        sprayTrail(x,y,z+10,bloodTint(&thing));
    }
    // Gore: a severed head flies on (flying_piece.cpp), as far as the body's tics have gone.
    if(doll.flying)flyingPieceTic(*doll.flying,target);
    // The legs left standing stand, buckle and go over (standing_legs.cpp).
    if(doll.legs)standingLegsTic(*doll.legs,target);
    // Or, the legs in the game's own death frames: their cut pumps from where the corpse is, at the cut's
    // height in the frame it shows (the share of its height the cut is at), up and a little back.
    else if(doll.halved&&!doll.legsFly&&bleedsBlood(thing)) {
        for(int t=std::max(doll.legsPumped+1,target-1);t<=target;++t) {
            float strength=stumpPulse(t,doll.goreSeed^0x2f5e1b7du),stream=stumpStream(t,doll.goreSeed^0x2f5e1b7du);
            if((strength<=0&&stream<=0)||thing.sprite<0||thing.sprite>=numsprites)continue;
            const auto &sprite=sprites[thing.sprite];int frame=thing.frame&FF_FRAMEMASK;
            if(frame>=sprite.numframes)continue;
            const Image &image=lumpImage(firstspritelump+sprite.spriteframes[frame].lump[0],false);
            float tall=std::max(1.0f,doll.body->rest[P_HEAD].z),h=std::max(2.0f,image.top*doll.cutZ/tall);
            float back=(float)(hash32(doll.goreSeed^(uint32_t)t)>>8)/16777216.0f;
            float carry[3]={units(thing.momx)*TICRATE,units(thing.momy)*TICRATE,0};
            sprayStump(units(thing.x),units(thing.y),units(thing.z)+h,0.3f*back,0.3f*(1-back),0.9f,strength,bloodTint(&thing),stream,carry);
        }
        doll.legsPumped=std::max(doll.legsPumped,target);
    }
    // Gore: the stump pumps on its beat (gore.cpp), falling or lying, out along the neck. Only the last
    // two tics' beats: a body out of sight doesn't catch up on them all at once when it comes back.
    // The legs thrown off: their cut pumps up out of the hips, and leaves its own pool where they lie.
    if(doll.legsDoll&&bleedsBlood(thing)) {
        Ragdoll &l=*doll.legsDoll;
        for(int t=std::max(l.pumped+1,target-1);t<=target;++t) {
            float strength=stumpPulse(t,doll.goreSeed^0x3a0d5c11u),stream=stumpStream(t,doll.goreSeed^0x3a0d5c11u);
            if(strength<=0&&stream<=0)continue;
            float sideSign=l.mirror?-1.0f:1.0f,wx,wy;
            V3 hips=l.hips();l.overGrid(hips,wx,wy);
            V3 way=unit(l.p[P_SPINE]-hips);
            float vx=l.ax[0]*sideSign*way.x+l.ay[0]*way.y,vy=l.ax[1]*sideSign*way.x+l.ay[1]*way.y;
            float carry[3];l.worldSpeed(hips,middle(l.prev[P_HIP_L],l.prev[P_HIP_R]),carry);
            sprayStump(wx+l.floors.x0,wy+l.floors.y0,l.gz+hips.z+1,vx,vy,way.z,strength,bloodTint(&thing),stream,carry);
        }
        l.pumped=std::max(l.pumped,target);
        if(!l.pooled&&l.resting) {
            l.pooled=true;
            float wx,wy;l.overGrid(l.hips(),wx,wy);
            bloodPoolAt(wx+l.floors.x0,wy+l.floors.y0,std::clamp(units(thing.radius)*2.4f,28.0f,64.0f),8,bloodTint(&thing));
        }
    }
    goreLegsOffTic(doll,thing,target);
    goreBlowTic(doll,thing,target);
    // Arms off: they fly on, and the shoulders pump out sideways, a beat of their own each.
    for(int k=0;k<2;++k)if(doll.armPieces[k])flyingPieceTic(*doll.armPieces[k],target-doll.armFrom[k]);
    for(size_t k=0;k<doll.chunks.size();++k)flyingPieceTic(*doll.chunks[k],target-doll.chunkFrom[k]);
    if((doll.armOff[0]||doll.armOff[1])&&bleedsBlood(thing)) {
        for(int t=std::max(doll.armPumped+1,target-1);t<=target;++t)
            for(int k=0;k<2;++k) {
                if(!doll.armOff[k])continue;
                uint32_t seed=doll.goreSeed^(k?0x1d2c3b4au:0x5e6f7a8bu);
                float strength=0.7f*stumpPulse(t,seed),stream=0.7f*stumpStream(t,seed);
                if(strength<=0&&stream<=0)continue;
                int sh=k?P_SH_R:P_SH_L;
                float sideSign=doll.mirror?-1.0f:1.0f,wx,wy;
                doll.overGrid(doll.p[sh],wx,wy);
                V3 way=unit(doll.p[sh]-doll.p[k?P_SH_L:P_SH_R]);
                float vx=doll.ax[0]*sideSign*way.x+doll.ay[0]*way.y,vy=doll.ax[1]*sideSign*way.x+doll.ay[1]*way.y;
                float carry[3];doll.worldSpeed(sh,carry);
                sprayStump(wx+doll.floors.x0,wy+doll.floors.y0,doll.gz+doll.p[sh].z,vx,vy,way.z,strength,bloodTint(&thing),stream,carry);
            }
        doll.armPumped=std::max(doll.armPumped,target);
    }
    // Come to rest, the stump bleeds a pool where it lies, bigger than a corpse's and spreading longer
    // (it pumps on): the game's own corpse is the legs when halved, its pool where they are.
    if(!doll.pooled&&doll.resting&&(doll.headless||doll.halved)&&bleedsBlood(thing)) {
        doll.pooled=true;
        float wx,wy;doll.overGrid(doll.p[doll.halved?P_SPINE:P_NECK],wx,wy);
        bloodPoolAt(wx+doll.floors.x0,wy+doll.floors.y0,std::clamp(units(thing.radius)*2.4f,28.0f,64.0f)*(doll.halved?1.3f:1.0f),8,bloodTint(&thing));
    }
    // Cut in half, the stump is the torso's cut at the spine, pumping out down the trunk.
    if((doll.headless||doll.halved)&&bleedsBlood(thing)) {
        int from=doll.halved?P_SPINE:P_NECK,toward=doll.halved?P_NECK:P_SPINE;
        for(int t=std::max(doll.pumped+1,target-1);t<=target;++t) {
            float strength=stumpPulse(t,doll.goreSeed),stream=stumpStream(t,doll.goreSeed);
            if(strength<=0&&stream<=0)continue;
            float sideSign=doll.mirror?-1.0f:1.0f,wx,wy;
            doll.overGrid(doll.p[from],wx,wy);
            V3 way=unit(doll.p[from]-doll.p[toward]);
            float vx=doll.ax[0]*sideSign*way.x+doll.ay[0]*way.y,vy=doll.ax[1]*sideSign*way.x+doll.ay[1]*way.y;
            float carry[3];doll.worldSpeed(from,carry);
            sprayStump(wx+doll.floors.x0,wy+doll.floors.y0,doll.gz+doll.p[from].z+1,vx,vy,way.z,strength,bloodTint(&thing),stream,carry);
            if(M_CheckParm((char*)"-ragdolllog"))fprintf(stderr,"ragdollpump %u tic %d strength %.2f up %.2f\n",(unsigned)((uintptr_t)&thing%100000),t,strength,way.z);
        }
        doll.pumped=std::max(doll.pumped,target);
    }
}
}
