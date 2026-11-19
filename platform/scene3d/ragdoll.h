/* Ragdoll falls: what ragdoll.cpp (the skeleton and its fall) and
   ragdoll_body.cpp (the relief it moves) share. Body space: x right as the
   drawing is seen, y away from its viewer, z up, units from the thing's
   origin on the floor. Worked out in the lab: tests/death_ragdoll.py. */
#pragma once
#include "internal.h"

namespace doom3d {
struct V3 { float x=0,y=0,z=0; };
inline V3 operator+(V3 a,V3 b) {return {a.x+b.x,a.y+b.y,a.z+b.z};}
inline V3 operator-(V3 a,V3 b) {return {a.x-b.x,a.y-b.y,a.z-b.z};}
inline V3 operator*(V3 a,float s) {return {a.x*s,a.y*s,a.z*s};}
inline float dot(V3 a,V3 b) {return a.x*b.x+a.y*b.y+a.z*b.z;}
inline V3 cross(V3 a,V3 b) {return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
inline float length(V3 a) {return std::sqrt(dot(a,a));}
inline V3 unit(V3 a) {float n=length(a);return n>1e-9f?a*(1/n):V3{};}
inline V3 middle(V3 a,V3 b) {return (a+b)*0.5f;}

// The skeleton's points, and the parts of the body that follow them. The gun is a bone of its own:
// every texel the rig carries follows it alone, never blended, so it stays hard however the body
// bends. It is held in the chest's frame (let go, it would be the weapon dropping).
// The body falls on its first DOLL_POINTS; the arms (elbow and hand each side, gore branch) come after
// them, carried by the shoulders and never pushing back (Ragdoll::swingArms), so the fall is the same
// with them as without.
enum DollPoint { P_HEAD,P_NECK,P_SH_L,P_SH_R,P_SPINE,P_HIP_L,P_HIP_R,P_KNEE_L,P_KNEE_R,P_ANK_L,P_ANK_R,DOLL_POINTS,
                 P_ELB_L=DOLL_POINTS,P_ELB_R,P_HAND_L,P_HAND_R,POSE_POINTS };
enum DollPart { PART_PELVIS,PART_CHEST,PART_HEAD,PART_THIGH_L,PART_SHIN_L,PART_THIGH_R,PART_SHIN_R,PART_GUN,
                PART_UPPER_L,PART_FORE_L,PART_UPPER_R,PART_FORE_R,DOLL_PARTS };
using DollPose=std::array<V3,POSE_POINTS>;

// The relief: a texel grid's corners in body space (front raised toward the
// viewer, back away from it), each following up to four parts, and its faces:
// per opaque texel a front, a back, and walls where the outline is open.
struct ReliefCorner { V3 p;uint8_t part[4]={};float w[4]={}; };
enum { FACE_FRONT,FACE_BACK,FACE_WALL };
// colour: what the face shows, from the drawing seen most nearly head-on to it (front, back or a side)
struct ReliefFace { uint32_t corner[4];uint16_t c,r;uint8_t kind;float dim;byte colour=0; };
struct Relief {
    int width=0,height=0;
    std::vector<byte> colour;           // per texel: its palette index
    std::vector<byte> backColour;       // per texel: the back's, from the drawing seen from behind (the front's where none)
    int cardSize=0;                     // the virtual camera's square picture: wide enough for the body any way round
    // Doom's pixels are 1.2 tall (the world draws them so, scene.cpp): the drawing's rows show a model
    // that much taller than its texels. The card draws the body that tall along its own length and
    // squashes the picture back by as much, so it stands as its sprite and lies its full length.
    // A stopgap, approved "good for now" (2026-11-08): only the picture is lengthened (ragdoll_card.cpp
    // tallPose). The fall, its floors and walls, the shots' and gore's heights still use the drawing's
    // units, so a lying body is drawn ~20% longer than the doll that collides (the head pokes past it).
    // May need redoing properly: the sim in tall space (rest z and world heights x1.2, divided back at the card).
    float tall=1;
    std::vector<ReliefCorner> corners;
    std::vector<ReliefFace> faces;
    std::vector<int32_t> frontFace;     // per texel: its front face's index (its back face the next), -1 where none
    DollPose rest{};                    // the skeleton as the drawing stands
    bool hasHead=false;float neck=0,headMiddle=0,headWidth=0;
    bool guided=false;                  // its hand rig lets a chest blast play the shot death on strings
    bool arms[2]={};                    // an arm found beside the torso each side (else its points draw nothing)
    bool ball=false;                    // a ball of flesh (body_ball.cpp): each corner follows its nearest shell points
    V3 ballMiddle;float ballRadius=0;   // and its middle, its round body's radius,
    std::vector<V3> shell;              // and those points at rest (a corner's part[] says which)
};
// The relief of a rigged drawing (null without a rig), its depth from the side
// drawing (sideLump, its left side, mirrored when sideFlip; -1: none, a round tube), its
// back's colours from the drawing seen from behind (backLump, likewise), and its flanks'
// from the side drawings (sideLump for the left, rightLump for the right). ball: raised the same way with no rig,
// as a ball of flesh, on a shell of points, its skeleton a cross through its middle.
const Relief *reliefFor(int lump,int sideLump,bool sideFlip,int rightLump,bool rightFlip,int backLump,bool backFlip,bool ball=false);
void reliefsShutdown();

#ifdef DOOM_DEVTOOLS
// For local dev tools (devtools/): one body as its card camera last drew it. All in body space (as
// the skeleton: x right as drawn, unmirrored; mirror says whether the card draws it mirrored): live
// the pose simulated this tic, shown the one the card holds, shift how far that is moved where it is
// drawn (its flight runs smooth), posed the relief in the shown pose, walls the walls round it (x0, y0,
// x1, y1, top) and floor its floors on a grid of floorStep units from floorX, floorY. The camera: the
// eye's way to the body (azimuth, radians in drawn space) and the angle and tilt it drew at (degrees).
struct DollPeek {
    const Relief *body=nullptr;const std::vector<V3> *posed=nullptr;
    DollPose live{},shown{};V3 shift{};
    bool mirror=false,landed=false,resting=false,baked=false,smooth=false;
    int tics=0,shownAt=0,lastPose=0,every=1;   // its tics, the poses drawn, the tic of the last, a pose every so many tics
    int cardKey=0;float azimuth=0;int angle=0,tilt=0;
    std::vector<std::array<float,5>> walls;
    static constexpr int floorSide=9;float floorStep=16,floorX=0,floorY=0,floor[floorSide][floorSide]={};
};
struct DollBody { const mobj_t *thing;int start; };
std::vector<DollBody> ragdollBodies();   // every ragdoll, the oldest kill first
bool ragdollPeek(const mobj_t *thing,float fraction,DollPeek &out);
#endif

// Each part's motion from rest to now: world = m * rest + t.
struct PartMotion { float m[3][3];V3 t; };
void partMotions(const DollPose &rest,const DollPose &now,std::array<PartMotion,DOLL_PARTS> &out);
inline V3 carry(const PartMotion &k,V3 p) {
    return {k.m[0][0]*p.x+k.m[0][1]*p.y+k.m[0][2]*p.z+k.t.x,k.m[1][0]*p.x+k.m[1][1]*p.y+k.m[1][2]*p.z+k.t.y,
            k.m[2][0]*p.x+k.m[2][1]*p.y+k.m[2][2]*p.z+k.t.z};
}
}
