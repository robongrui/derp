/* The ragdoll's card (ragdoll.cpp, internal.h): the posed body drawn back into
   a sprite by a virtual camera, and Gore's pictures over it (the head bursting,
   a severed head flying off). From the lab: tests/death_impostor.py. */
#include "ragdoll_doll.h"
#include "ragdoll_pixels.h"
#include "stump_rim.h"
#include <chrono>

namespace doom3d {
// The card (see internal.h). The posed relief is drawn into a square picture by an orthographic
// camera, one pixel per unit, the nearest face winning (sampled as F4 Ragdoll pixels says, ragdoll_pixels.cpp) (turned-away ones too: see below): each
// pixel a texel of the drawing, darkened through COLORMAP where the relief shows an edge (so it stays
// in the palette); shot-through texels left out, wound rims painted in blood colours. Its normals are
// the sprite normals of the picture itself, so it lights like any sprite. It is drawn again only when
// the pose, the 45-degree step, the pitch or the wounds change.
constexpr int cardKeyBase=-0x50000000;     // image keys clear of walls, lumps and blood
static int cardSerial=0;
void dropCard(const Ragdoll &doll) {
    if(doll.burst)dropHeadBurst(*doll.burst);
    goreBlowDrop(doll);stumpRimDrop(doll);
    if(doll.flying)dropFlyingPiece(*doll.flying);
    for(const auto &arm:doll.armPieces)if(arm)dropFlyingPiece(*arm);
    for(const auto &leg:doll.legPieces)if(leg)dropFlyingPiece(*leg);
    for(const auto &chunk:doll.chunks)dropFlyingPiece(*chunk);
    if(doll.legsDoll)dropCard(*doll.legsDoll);
    if(doll.legs)dropStandingLegs(*doll.legs);
    if(!doll.cardKey)return;
    images.erase(doll.cardKey);
    for(auto *group:{&batches,&mirrorBatches,&shadowBatches,&decalBatches})group->erase(doll.cardKey);
    // Shadow batches use the inverse image key, just as they do for WAD sprites.
    shadowBatches.erase(-1-doll.cardKey);decalBatches.erase(-1-doll.cardKey);
}
// The shown pose with every bone as long as the model's (Relief::tall along the body's length, the rest's
// z): rebuilt from the hips outward along the bones as they lie, then dropped or raised so its lowest
// point is where the shown pose's is (the card stands on that). Only the picture is lengthened: the fall,
// its floors and walls and gore's heights stay in the drawing's units.
static const int boneFrom[POSE_POINTS]={P_NECK,P_SPINE,P_NECK,P_NECK,-1,-1,-1,P_HIP_L,P_HIP_R,P_KNEE_L,P_KNEE_R,P_SH_L,P_SH_R,P_ELB_L,P_ELB_R};
static V3 tallRest(const Relief &body,V3 p) {return {p.x,p.y,p.z*body.tall};}
static DollPose tallPose(const Ragdoll &doll) {
    const Relief &body=*doll.body;
    if(body.tall==1)return doll.shown;
    const DollPose &rest=body.rest,&shown=doll.shown;
    DollPose out=shown;
    V3 root=middle(shown[P_HIP_L],shown[P_HIP_R]),rootRest=middle(rest[P_HIP_L],rest[P_HIP_R]);
    auto grow=[&](V3 from,V3 to,V3 fromRest,V3 toRest) {
        float was=length(toRest-fromRest),now=length(tallRest(body,toRest)-tallRest(body,fromRest));
        return (to-from)*(was>1e-4f?now/was:1.0f);
    };
    for(int n:{P_HIP_L,P_HIP_R,P_SPINE})out[n]=root+grow(root,shown[n],rootRest,rest[n]);
    for(int n:{P_NECK,P_HEAD,P_SH_L,P_SH_R,P_KNEE_L,P_KNEE_R,P_ANK_L,P_ANK_R,P_ELB_L,P_ELB_R,P_HAND_L,P_HAND_R}) {   // parents first
        int from=boneFrom[n];
        out[n]=out[from]+grow(shown[from],shown[n],rest[from],rest[n]);
    }
    float lowWas=1e9f,lowNow=1e9f;
    for(int n=0;n<DOLL_POINTS;++n)if(!doll.rider(n)) {lowWas=std::min(lowWas,shown[n].z-radius[n]);lowNow=std::min(lowNow,out[n].z-radius[n]);}
    if(lowWas<1e8f)for(V3 &q:out)q.z+=lowWas-lowNow;
    return out;
}
const std::vector<V3> &posedCorners(const Ragdoll &doll) {
    const Relief &body=*doll.body;
    if(doll.posedAt!=doll.shownAt) {
        DollPose restTall=body.rest;
        for(V3 &q:restTall)q=tallRest(body,q);
        std::array<PartMotion,DOLL_PARTS> motion;partMotions(restTall,tallPose(doll),motion);
        doll.posed.resize(body.corners.size());
        bool soft=goreBlowSoft(doll);
        // A ball's corners go with their shell points (body_ball.cpp), what stands off the shell (its horns) turned
        // with it: only moved with them, a tumbling ball's horns stayed pointing as they stood.
        if(body.ball) {
            for(size_t i=0;i<body.corners.size();++i) {
                const ReliefCorner &c=body.corners[i];V3 q{},on{};
                for(int k=0;k<4;++k) {
                    V3 rest=body.shell[c.part[k]];
                    q=q+(rest+doll.shellShown[c.part[k]])*c.w[k];on=on+rest*c.w[k];
                }
                V3 off=c.p-on;
                doll.posed[i]=q+doll.shellTurnShown[0]*off.x+doll.shellTurnShown[1]*off.y+doll.shellTurnShown[2]*off.z;
            }
            doll.posedAt=doll.shownAt;
            return doll.posed;
        }
        for(size_t i=0;i<body.corners.size();++i) {
            const ReliefCorner &c=body.corners[i];V3 q{};
            V3 at=soft?c.p+goreBlowOffset(doll,c.p):c.p;   // gore: the flesh ringing round the wound
            at=tallRest(body,at);
            for(int k=0;k<4;++k)if(c.w[k]>0)q=q+carry(motion[c.part[k]],at)*c.w[k];
            doll.posed[i]=q;
        }
        doll.posedAt=doll.shownAt;
    }
    return doll.posed;
}
static byte nearestColour(const byte *palette,int r,int g,int b) {
    int best=0,bestDistance=INT_MAX;
    for(int i=0;i<256;++i) {
        int dr=palette[i*3]-r,dg=palette[i*3+1]-g,db=palette[i*3+2]-b,d=dr*dr+dg*dg+db*db;
        if(d<bestDistance) {bestDistance=d;best=i;}
    }
    return (byte)best;
}
// Where it is drawn: where it died, moved on by how far its middle has gone since the pose shown,
// between the last two tics: the pose steps on Doom's tics, its flight runs smooth, as a walking
// monster's does. Resting, the floor it lies on carries it (a lift). feet: the floor it died on.
void dollPlace(const Ragdoll &doll,float fraction,float &x,float &y,float &feet) {
    float side=doll.mirror?-1.0f:1.0f;
    float mx=doll.centreWas[0]+(doll.centre[0]-doll.centreWas[0])*fraction-doll.centreShown[0];
    float my=doll.centreWas[1]+(doll.centre[1]-doll.centreWas[1])*fraction-doll.centreShown[1];
    if(smoothDolls()&&!doll.resting)mx=my=0;   // the pose drawn is blended to the same moment: it has moved with it
    if(doll.rides) {         // with its corpse, between the last two tics
        x=doll.oxWas+(doll.ox-doll.oxWas)*fraction+doll.ax[0]*side*mx+doll.ay[0]*my;
        y=doll.oyWas+(doll.oy-doll.oyWas)*fraction+doll.ax[1]*side*mx+doll.ay[1]*my;
        feet=doll.gzWas+(doll.gz-doll.gzWas)*fraction;
        return;
    }
    x=doll.ox+doll.ax[0]*side*mx+doll.ay[0]*my;y=doll.oy+doll.ax[1]*side*mx+doll.ay[1]*my;
    feet=doll.gz+(doll.restSector?units(doll.restSector->floorheight)-doll.restFloor:0);
}
bool ragdollWhere(const Ragdoll &doll,float fraction,float &x,float &y,float &z) {
    dollPlace(doll,fraction,x,y,z);
    x+=doll.ax[0]*doll.cardMiddle[0]+doll.ay[0]*doll.cardMiddle[1];y+=doll.ax[1]*doll.cardMiddle[0]+doll.ay[1]*doll.cardMiddle[1];
    return doll.landed;
}
// Riding the neck as the body is drawn (the shown pose where the card puts it): thrown with the body,
// the burst left hanging where the head died read as torn off the body, not burst on it.
const Ragdoll *ragdollLower(const Ragdoll &doll) {return doll.legsDoll.get();}
bool ragdollLegsCut(const Ragdoll &doll,float &cutZ,uint32_t &seed) {
    if(!doll.halved||doll.legs||doll.legsFly)return false;
    cutZ=doll.cutZ;seed=doll.goreSeed^0x2f5e1b7du;
    return true;
}
bool ragdollLegsCard(const Ragdoll &doll,float fraction,RagdollCard &card) {
    (void)fraction;
    return doll.legs&&standingLegsCard(*doll.legs,card);
}
bool ragdollHeadCard(const Ragdoll &doll,float fraction,RagdollCard &card) {
    bool drawn=false;
    if(doll.flying)timed(Stat::Gore,[&]{drawn=flyingPieceCard(*doll.flying,fraction,card);});
    return drawn;
}
int ragdollLooseCount(const Ragdoll &doll) {return 5+(int)doll.chunks.size();}
bool ragdollLooseCard(const Ragdoll &doll,int n,float fraction,RagdollCard &card) {
    if(n==4+(int)doll.chunks.size()) {bool shown=false;timed(Stat::Gore,[&]{shown=goreBlowCard(doll,card);});return shown;}
    const std::shared_ptr<FlyingPiece> &piece=n<2?doll.armPieces[n]:n<4?doll.legPieces[n-2]:doll.chunks[n-4];
    bool drawn=false;
    if(piece)timed(Stat::Gore,[&]{drawn=flyingPieceCard(*piece,fraction,card);});
    return drawn;
}
bool ragdollBurstCard(const Ragdoll &doll,float fraction,RagdollCard &card) {
    if(!doll.burst)return false;
    float x,y,feet,side=doll.mirror?-1.0f:1.0f;dollPlace(doll,fraction,x,y,feet);
    const V3 neck=tallPose(doll)[P_NECK];   // where the picture draws it
    x+=doll.ax[0]*side*neck.x+doll.ay[0]*neck.y;y+=doll.ax[1]*side*neck.x+doll.ay[1]*neck.y;
    bool drawn=false;
    timed(Stat::Gore,[&]{drawn=headBurstCard(*doll.burst,leveltime-doll.start-doll.burstFrom,x+doll.burstAt[0],y+doll.burstAt[1],feet+neck.z+doll.burstAt[2],card);});
    return drawn;
}
// A body point where the picture draws it (the shown pose where the card puts it, as the burst rides the neck),
// for the fibres torn off it (tear_fibres.cpp); -2: the hips' middle.
void ragdollDrawnPoint(const Ragdoll &doll,int point,float fraction,float &x,float &y,float &z) {
    float feet,side=doll.mirror?-1.0f:1.0f;dollPlace(doll,fraction,x,y,feet);
    const DollPose pose=tallPose(doll);
    V3 q=point==-2?middle(pose[P_HIP_L],pose[P_HIP_R]):pose[std::clamp(point,0,POSE_POINTS-1)];
    x+=doll.ax[0]*side*q.x+doll.ay[0]*q.y;y+=doll.ax[1]*side*q.x+doll.ay[1]*q.y;z=feet+q.z;
}
bool ragdollCardKept(const Ragdoll &doll) {
    return doll.baked&&doll.cardKey&&doll.cutSerial==doll.cardCuts&&images.count(doll.cardKey);
}
// Pieces land far from the body, and legs thrown off are bodies of their own.
bool ragdollPiecesInView(const Ragdoll &doll) {
    auto seen=[](const std::shared_ptr<FlyingPiece> &piece){return piece&&flyingPieceInView(*piece);};
    if(seen(doll.flying))return true;
    for(int k=0;k<2;++k)if(seen(doll.armPieces[k])||seen(doll.legPieces[k]))return true;
    for(const auto &chunk:doll.chunks)if(seen(chunk))return true;
    float x,y,z;
    return doll.legsDoll&&(ragdollWhere(*doll.legsDoll,1,x,y,z),thingInView(x,y));
}
// A body that can't be drawn asks again in a second, so it doesn't hold up the others.
bool ragdollWantsPicture(const Ragdoll &doll) {
    if(!doll.resting||leveltime-doll.pictureAsked<35)return false;
    if(ragdollCardKept(doll)&&(!doll.legsDoll||ragdollCardKept(*doll.legsDoll)))return false;
    doll.pictureAsked=leveltime;
    return true;
}
bool ragdollCard(const Ragdoll &doll,float fraction,float sink,const float *eye,const std::vector<byte> *holes,RagdollCard &card,int tint) {
    const Relief &body=*doll.body;
    if(body.cardSize<=0)return false;
    doll.cardDrawnAt=leveltime;
    int size=body.cardSize;
    // Drawn space: the body's own, mirrored when it is drawn mirrored (the world is x + ax*m.x + ay*m.y).
    float side=doll.mirror?-1.0f:1.0f;
    if(smoothDolls()&&!doll.baked&&!doll.resting) {   // between the last two tics' poses, a new picture each frame
        for(int n=0;n<POSE_POINTS;++n)doll.shown[n]=doll.shownWas[n]+(doll.shownTic[n]-doll.shownWas[n])*fraction;
        ++doll.shownAt;
    }
    float x,y,feet;dollPlace(doll,fraction,x,y,feet);feet-=sink;
    V3 upright=unit(doll.shown[P_NECK]-middle(doll.shown[P_HIP_L],doll.shown[P_HIP_R]));
    float lying=std::clamp(1-upright.z,0.0f,1.0f);
    auto found=images.find(doll.cardKey);
    auto place=[&] {
        card.key=doll.cardKey;card.size=size;card.half=size/2.0f;
        card.u0=doll.cardLeft;card.v0=doll.cardFirst;card.u1=doll.cardRight;card.v1=doll.cardBottom+1;
        card.picture=doll.cardSignature;card.lying=body.ball?0:lying;   // (a ball's picture is as tall lying as flying)
        card.x=x+doll.ax[0]*doll.cardMiddle[0]+doll.ay[0]*doll.cardMiddle[1];card.y=y+doll.ax[1]*doll.cardMiddle[0]+doll.ay[1]*doll.cardMiddle[1];
        // Its lowest solid row on the body's lowest surface (the floor it lies on, which may be below
        // the thing's: Doom holds a corpse up at the highest floor under its box), not the thing's feet.
        float lowest=1e9f;
        for(int n=0;n<DOLL_POINTS;++n)if(!doll.rider(n))lowest=std::min(lowest,doll.shown[n].z-radius[n]);   // (halved: the riders aren't there)
        // Lying, pressed cardSink into the floor: the picture's lowest rows are the body's near
        // edge seen from above, and a row or two of floor-coloured fringe read as a gap under it.
        card.top=feet+lowest+doll.cardBottom+1-cardSink*lying;
        // A ball by its middle, the picture's: its shell lies on the floor by itself, whatever its horns do.
        if(body.ball)card.top=feet+doll.cardMiddle[2]+size/2.0f;
        return true;
    };
    // At rest, its picture already has its holes (geometry.cpp asks no mask then): kept as it is.
    if(!holes&&ragdollCardKept(doll))return place();
    uint32_t wounds=0;
    if(holes)for(size_t i=0;i<holes->size();++i)wounds=wounds*31+(*holes)[i];
    wounds=wounds*31+(uint32_t)doll.cutSerial;   // gore's cuts on the corpse (goreCorpseHit) draw it again too
    // A corpse sprite now: the same from every angle, drawn again only when its holes change
    // (shot, or eaten away by nukage rain, craters.cpp).
    if(doll.baked&&doll.cardKey&&found!=images.end()&&wounds==doll.cardWounds)return place();
    const std::vector<V3> &posed=posedCorners(doll);
    if(posed.empty())return false;
    // The camera aims at the body's middle, in height too: aimed at its feet, the top of a body
    // still standing reached past the picture (the canvas only spans the body's own size).
    V3 lo{1e9f,1e9f,1e9f},hi{-1e9f,-1e9f,-1e9f};
    for(const V3 &q:posed) {V3 m{side*q.x,q.y,q.z};lo={std::min(lo.x,m.x),std::min(lo.y,m.y),std::min(lo.z,m.z)};hi={std::max(hi.x,m.x),std::max(hi.y,m.y),std::max(hi.z,m.z)};}
    V3 o=middle(lo,hi);
    // The virtual camera: from the eye's side of the body, stepped to 45 degrees from its front, as
    // the eye sees every sprite, all through the fall: what the shot did is what is drawn. (It used to
    // turn side-on as the body went flat, which hid the far half of a lying body hanging over the
    // floor, but read as the body rolling onto its side after it landed.)
    float ex=eye[0]-x,ey=eye[1]-y;
    V3 seen{ex*doll.ax[0]+ey*doll.ax[1],ex*doll.ay[0]+ey*doll.ay[1],0};
    float azimuth=std::atan2(o.x-seen.x,o.y-seen.y);
    int step=((int)std::lround(azimuth/(doomPi/4))%8+8)%8;
    // A step is kept until the view is well past the next one's middle: flipping between two on the
    // line between them looked like the body turning.
    if(doll.lastStep>=0&&std::abs(std::remainder(azimuth-doll.lastStep*doomPi/4,2*doomPi))<doomPi/8+doomPi/18)step=doll.lastStep;
    doll.lastStep=step;
    int angle=doll.baked?doll.bakedAngle:smoothDolls()?(int)std::lround(azimuth*180/doomPi):step*45;   // whole degrees
    float a=angle*doomPi/180;
    V3 along=doll.shown[P_HEAD]-middle(doll.shown[P_ANK_L],doll.shown[P_ANK_R]);
    float lx=side*along.x,ly=along.y;
    // Tipped down as the body goes over (straight on standing, as the sprite it was), lying as far as
    // the eye looks down at it, at most tiltMost. The card stands upright, so whatever of the body runs
    // away from the eye is drawn as height; tipped further than the eye looks (30 degrees, where the
    // eye from 128 units looks down 18 and from 256 units 9) the far half of a lying body hung over
    // the floor.
    float cx=x+doll.ax[0]*o.x+doll.ay[0]*o.y,cy=y+doll.ax[1]*o.x+doll.ay[1]*o.y;
    float looks=std::atan2(eye[2]-feet,std::max(std::hypot(eye[0]-cx,eye[1]-cy),1.0f))*180/doomPi;
    int tiltStep=smoothDolls()?1:5;
    int tilt=doll.baked?doll.bakedTilt:(int)std::lround(std::clamp(looks,0.0f,tiltMost)*lying/tiltStep);   // pitch in 5-degree steps
    float pitch=tilt*tiltStep*doomPi/180;
#ifdef DOOM_DEVTOOLS
    doll.cardAzimuth=azimuth;doll.cardAngle=angle;doll.cardTilt=tilt*tiltStep;
#endif
    V3 fh{std::sin(a),std::cos(a),0},f{fh.x*std::cos(pitch),fh.y*std::cos(pitch),-std::sin(pitch)},r{fh.y,-fh.x,0},u=cross(r,f);
    // F4 Ragdoll pixels past Nearest (ragdoll_pixels.cpp): the picture anchored, the hips on a pixel corner,
    // so what holds still between two pictures is drawn the same. The bounding box's middle moved by a part
    // of a pixel whenever an arm swung out, and redrew the whole body shifted. The card stands where that
    // moves the picture (cardMiddle), so the body doesn't move in the world.
    int pixelMode=settings.ragdollPixels,hipX=0,hipY=0;
    if(cardAnchored(pixelMode)) {
        DollPose tall=tallPose(doll);V3 hip=middle(tall[P_HIP_L],tall[P_HIP_R]);V3 hq{side*hip.x-o.x,hip.y-o.y,hip.z-o.z};
        float hx=dot(hq,r)+size/2.0f,hy=size/2.0f-dot(hq,u)/body.tall;
        hipX=(int)std::lround(hx);hipY=(int)std::lround(hy);
        o=o+r*(hx-hipX)-u*((hy-hipY)*body.tall);
    }
    uint64_t signature=((uint64_t)(uint32_t)doll.shownAt<<32)^((uint64_t)angle<<20)^((uint64_t)tilt<<14)^((uint64_t)doll.mirror<<8)^((uint64_t)wounds*0x9e3779b97f4a7c15ull);
    if((!doll.cardKey||found==images.end()||signature!=doll.cardSignature)&&M_CheckParm((char*)"-ragdolllog")) {
        // Test runs: each new picture's camera angle and the body's own heading in it (degrees).
        float heading=std::atan2(ly,lx)*(180/doomPi);
        fprintf(stderr,"ragdollcard %u pose %d angle %d heading %.1f tilt %d looks %.1f\n",(unsigned)((uintptr_t)&doll%100000),
            doll.shownAt,angle,heading,tilt,looks);
    }
    if(!doll.cardKey||found==images.end()||signature!=doll.cardSignature) {
        static const byte *palette=nullptr,*colormap=nullptr;
        static byte rims[4];
        if(!palette) {
            palette=(const byte*)W_CacheLumpName((char*)"PLAYPAL",PU_STATIC);colormap=(const byte*)W_CacheLumpName((char*)"COLORMAP",PU_STATIC);
            rims[0]=nearestColour(palette,30,4,4);rims[1]=nearestColour(palette,140,8,8);rims[2]=nearestColour(palette,196,56,48);rims[3]=nearestColour(palette,84,10,8);
        }
        static std::vector<CardQuad> quads;
        std::vector<byte> pixels;
        float half=size/2.0f;
        // Each corner placed in the picture once: the faces share them.
        struct Placed { V3 q;float x,y,d; };
        static std::vector<Placed> placed;placed.resize(posed.size());
        for(size_t n=0;n<posed.size();++n) {
            const V3 &b=posed[n];V3 q{side*b.x-o.x,b.y-o.y,b.z-o.z};
            placed[n]={q,dot(q,r)+half,half-dot(q,u)/body.tall,dot(q,f)};   // squashed back: the world stretches it again
        }
        // Gore: a body that came apart is meat inside, as a caco's is (wounds.cpp, solid): a shot through it shows
        // that, no hole. The dismembering blast's pellets riddled the half left with holes onto the floor (the user).
        bool meaty=doll.halved||doll.lowerHalf||doll.headless||doll.blow||!doll.bites.empty()||
                   doll.armOff[0]||doll.armOff[1]||doll.legOff[0]||doll.legOff[1];
        const StumpRim &rim=stumpRim(doll,tint,rims);
        // A quad of relief corners for the picture (cardPixels: the nearest wins).
        auto drawQuad=[&](const uint32_t corner[4],byte colour,float dim) {
            V3 q[4];
            for(int k=0;k<4;++k)q[k]=placed[corner[k]].q;
            // Faces turned away from the camera are drawn too, darker and a unit behind (a face toward it
            // at the same depth wins): where the body bends, faces fold over or twist half away, and with
            // them left out 1-5% of a falling body's picture was holes onto the floor behind it (arena,
            // three monsters, five shots, front and back), at the chest and hips most.
            bool away=dot(cross(q[3]-q[1],q[2]-q[0])*side,f)>=0;
            int level=std::clamp((int)std::lround((1-dim)*24)+(away?6:0),0,31);
            colour=colormap[level*256+colour];
            float px[4],py[4],pd[4];
            for(int k=0;k<4;++k) {const Placed &c=placed[corner[k]];px[k]=c.x;py[k]=c.y;pd[k]=c.d+(away?1.0f:0.0f);}
            CardQuad cq;cq.colour=colour;
            for(int k=0;k<4;++k) {cq.x[k]=px[k];cq.y[k]=py[k];cq.d[k]=pd[k];}
            quads.push_back(cq);
        };
        quads.clear();
        static std::vector<byte> drawn;drawn.assign(body.faces.size(),0);
        for(size_t fi=0;fi<body.faces.size();++fi) {
            const ReliefFace &face=body.faces[fi];
            size_t texel=(size_t)face.r*body.width+face.c;
            byte wound=holes&&texel<holes->size()?(*holes)[texel]:0;
            if(wound==1&&!meaty)continue;            // shot through
            byte colour=face.colour;
            if(wound==1) {   // (meaty) the hole's meat, dark in its depths here and there
                uint32_t s=doll.goreSeed^0x2du;
                bool dark=hashUnit(face.c,face.r,s^0x41u)<0.25f;
                byte meat=doll.guts&&!dark?gutsColour(*doll.guts,face.c,face.r,s):0;
                colour=meat>=16?meat:bloodRemap(rims[dark?0:3],tint);
            }
            else if(wound>=16&&face.kind==FACE_FRONT)colour=wound;   // gore's flesh and bone under the suit (wounds.cpp)
            else if(wound>=2&&wound<6&&face.kind==FACE_FRONT)colour=bloodRemap(rims[std::min(3,wound-2)],tint);
            if(rim.on) {
                // Gore: what the cuts took and their torn ends (stump_rim.cpp): a severed neck, the halves,
                // an arm or a leg off, the kill's blow, chunks shot out, a bite out of the skull.
                byte paint=rim.paint[texel];
                if(paint==STUMP_GONE)continue;
                if(paint==STUMP_COLOUR)colour=rim.colour[texel];
                else if(paint==STUMP_FLAP) {   // the stump's own skin over the lost part's texels
                    size_t from=(size_t)rim.from[texel];
                    colour=face.kind==FACE_BACK&&from<body.backColour.size()?body.backColour[from]:body.colour[from];
                }
            }
            drawn[fi]=1;
            drawQuad(face.corner,colour,face.dim);
        }
        // Gore: the body is a shell, front and back, closed by walls only round the drawing's own outline. Where a
        // cut took the texels beside one that stays (a leg off at the hip, the top cut from the legs, an arm), the
        // shell stood open and the picture showed through the empty inside (the user: "the place where a leg is
        // missing has no texture"). Each such edge closed with a wall of meat, as the outline's walls are.
        if(meaty&&!body.frontFace.empty()) {
            static const int edges[4][4]={{0,-1,0,1},{1,0,1,2},{0,1,2,3},{-1,0,3,0}};   // as reliefFor: neighbour, the edge's two corners
            uint32_t s=doll.goreSeed^0x6bu;
            for(size_t fi=0;fi<body.faces.size();++fi) {
                const ReliefFace &face=body.faces[fi];
                if(face.kind!=FACE_FRONT||!drawn[fi]||fi+1>=body.faces.size())continue;
                const ReliefFace &back=body.faces[fi+1];   // its back face: the same corners reversed, raised the other way
                for(const auto &e:edges) {
                    int nc=face.c+e[0],nr=face.r+e[1];
                    if(nc<0||nr<0||nc>=body.width||nr>=body.height)continue;
                    int32_t other=body.frontFace[(size_t)nr*body.width+nc];
                    if(other<0||drawn[other])continue;   // the outline (a wall there already), or still there
                    uint32_t cap[4]={face.corner[e[2]],back.corner[3-e[2]],back.corner[3-e[3]],face.corner[e[3]]};
                    float pick=hashUnit(face.c*4+e[0]+1,face.r*4+e[1]+1,s);
                    byte meat=doll.guts&&pick<0.7f?gutsColour(*doll.guts,face.c,face.r,s):0;
                    drawQuad(cap,meat>=16?meat:bloodRemap(rims[pick<0.85f?1:3],tint),0.8f);
                }
            }
        }
        // Steady: the last picture of this camera step, the hips a whole number of pixels from here.
        const std::vector<byte> *last=nullptr;
        if(cardSteady(pixelMode)&&found!=images.end()&&found->second.width==size&&doll.cardHipAngle==angle)last=&found->second.pixels;
        auto started=std::chrono::steady_clock::now();
        cardPixels(quads,size,pixelMode,last,hipX-doll.cardHip[0],hipY-doll.cardHip[1],pixels);
        if(M_CheckParm((char*)"-ragdolllog"))   // test runs: what a picture costs
            fprintf(stderr,"ragdollpixels mode %d size %d faces %zu %.3f ms\n",pixelMode,size,quads.size(),
                std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count());
        doll.cardHip[0]=hipX;doll.cardHip[1]=hipY;doll.cardHipAngle=angle;
        int left=size,right=0,first=size,bottom=-1;
        for(int j=0;j<size;++j)for(int i=0;i<size;++i)if(pixels[((size_t)j*size+i)*2+1]) {
            left=std::min(left,i);right=std::max(right,i+1);first=std::min(first,j);bottom=j;
        }
        doll.cardBottom=bottom<0?size-1:bottom;
        doll.cardLeft=left;doll.cardRight=right;doll.cardFirst=first;
        std::vector<byte> normals=spriteNormals(size,size,pixels.data());
        if(!doll.cardKey)doll.cardKey=cardKeyBase+(++cardSerial&0x0fffffff);
        found=images.find(doll.cardKey);
        if(found==images.end())found=images.emplace(doll.cardKey,upload(size,size,pixels.data(),0,0,nullptr,normals.data())).first;
        else {
            std::vector<byte> packed((size_t)size*size*4);
            for(size_t i=0;i<(size_t)size*size;++i) {packed[4*i]=pixels[2*i];packed[4*i+1]=pixels[2*i+1];packed[4*i+2]=normals[2*i];packed[4*i+3]=normals[2*i+1];}
            gpuUpdateTexture(found->second.texture,GpuFormat::RGBA8,{{0,0,size,size}},packed.data(),size*4);
            found->second.pixels=std::move(pixels);
        }
        doll.cardSignature=signature;doll.cardWounds=wounds;doll.cardCuts=doll.cutSerial;
        // A baked corpse drawn again for its holes: its posed body freed again.
        if(doll.baked) {doll.posed.clear();doll.posed.shrink_to_fit();doll.posedAt=-1;}
    }
    doll.cardMiddle[0]=o.x;doll.cardMiddle[1]=o.y;doll.cardMiddle[2]=o.z;
    if(doll.resting&&!doll.baked&&doll.cardSignature==signature) {
        // At rest and drawn in its last pose: from now on a sprite, the doll's posed body freed.
        doll.baked=true;doll.bakedAngle=angle;doll.bakedTilt=tilt;
        doll.posed.clear();doll.posed.shrink_to_fit();doll.posedAt=-1;
    }
    return place();
}
}
