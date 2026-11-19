/* Ragdoll falls: a rigged monster's body thrown down by the killing blow.
   The doll is eleven points on the drawing's own skeleton (head, neck,
   shoulders, the spine at the chest joint, hips, knees and ankles),
   Verlet-integrated in game tics and held together by bone lengths (the
   torso two stiff blocks hinged at the spine), joint limits that keep the
   bones' lengths and add no speed, the floors under it (the sectors round
   the kill on a grid: steps, ledges, the lower floor a corpse at a stair edge
   lies on) that stop bodies dead, and the walls and steps around it. Muscles, extra lengths across the joints, hold
   the standing pose at first and let go on a schedule the hit sets. It keeps
   the way it faced and falls in the world from where it died, with the
   corpse's slide; the hit tips it over the way it was struck, and while it
   falls it is twisted so it lands with a drawn side (front or back) up or
   toward the player; nothing turns it once it lies. Its relief
   (ragdoll_body.cpp) is skinned to it, and a virtual camera draws it back
   into a sprite (ragdollCard): poses quickly through the hit, then at the
   pace of the monster's own death frames, and at the top of a throw and
   the moments it touches down.
   Sometimes a chest blast plays the shot death on strings instead (guided
   falls, below). Drawing only. From the lab: tests/death_ragdoll.py. */
#include "ragdoll_doll.h"

namespace doom3d {

static std::unordered_map<const mobj_t*,Ragdoll> dolls;
bool hasRagdoll(const mobj_t *thing) {return thing->health<=0&&dolls.count(thing);}
bool ragdollSettled(const mobj_t *thing) {auto found=dolls.find(thing);return found==dolls.end()||found->second.resting;}
int ragdollsMoving() {int n=0;for(const auto &doll:dolls)n+=!doll.second.resting;return n;}
// A shot from (x1,y1,z1) to (x2,y2,z2) into a corpse's ragdoll (body_burst.cpp's goreShotTraced): the part
// whose point is nearest the shot, within its reach (radius and 3), handed to gore (goreCorpseHit). -1: it
// missed the body (or there is no doll); 0: struck at (hx,hy,hz); 2: struck, and the torso is done.
int ragdollCorpseShot(const mobj_t &thing,float x1,float y1,float z1,float x2,float y2,float z2,float &hx,float &hy,float &hz) {
    auto found=dolls.find(&thing);
    if(found==dolls.end())return -1;
    Ragdoll &doll=found->second;
    V3 a{x1,y1,z1},d{x2-x1,y2-y1,z2-z1};float dd=std::max(1e-6f,dot(d,d));
    int best=-1;float bestGap=1e9f;V3 bestAt{};
    for(int n=0;n<POSE_POINTS;++n) {
        if(n<DOLL_POINTS&&doll.rider(n))continue;
        if(doll.headless&&n==P_HEAD)continue;
        if((n==P_ELB_L||n==P_HAND_L)&&(!doll.body->arms[0]||doll.armOff[0]||doll.armBlown[0]))continue;
        if((n==P_ELB_R||n==P_HAND_R)&&(!doll.body->arms[1]||doll.armOff[1]||doll.armBlown[1]))continue;
        float wx,wy;doll.overGrid(doll.p[n],wx,wy);
        V3 w{wx+doll.floors.x0,wy+doll.floors.y0,doll.gz+doll.p[n].z};
        float t=std::clamp(dot(w-a,d)/dd,0.0f,1.0f);V3 at=a+d*t;
        float gap=length(w-at)-(doll.body->ball?doll.body->ballRadius:n<DOLL_POINTS?radius[n]:armRadius)-3;
        if(gap<bestGap) {bestGap=gap;best=n;bestAt=at;}
    }
    if(best<0||bestGap>0)return -1;
    hx=bestAt.x;hy=bestAt.y;hz=bestAt.z;
    int part=best==P_HEAD||best==P_NECK?CORPSE_HEAD:best==P_ELB_L||best==P_HAND_L?CORPSE_ARM_L:best==P_ELB_R||best==P_HAND_R?CORPSE_ARM_R:CORPSE_TORSO;
    float l=std::max(1e-3f,std::hypot(d.x,d.y));
    if(M_CheckParm((char*)"-ragdolllog"))fprintf(stderr,"gorecorpse part %d point %d\n",part,best);
    doll.dance(best,d.x/l,d.y/l);   // still falling: the hit jerks it and holds it up a little longer
    return goreCorpseHit(doll,thing,part,best,d.x/l,d.y/l,leveltime-doll.start);
}
bool ragdollPoint(const mobj_t &thing,int point,float &x,float &y,float &z) {
    auto found=dolls.find(&thing);
    if(found==dolls.end()||point<0||point>=POSE_POINTS)return false;
    const Ragdoll &doll=found->second;float wx,wy;doll.overGrid(doll.p[point],wx,wy);
    x=wx+doll.floors.x0;y=wy+doll.floors.y0;z=doll.gz+doll.p[point].z;
    return true;
}
bool ragdollPlace(const mobj_t &thing,float &x,float &y,float &z) {
    auto found=dolls.find(&thing);
    if(found==dolls.end())return false;
    ragdollWhere(found->second,1,x,y,z);   // (its answer is whether the body is down, not whether there is one)
    return true;
}
bool ragdollLegsStand(const mobj_t &thing,int tics) {
    auto found=dolls.find(&thing);
    return found!=dolls.end()&&found->second.halved&&!found->second.legsFly&&leveltime-found->second.start<tics;
}
bool ragdollBlown(const mobj_t &thing,float sx,float sy,float hurt) {
    auto found=dolls.find(&thing);
    if(found==dolls.end()||!found->second.ball)return false;
    ballBlast(found->second,sx,sy,hurt);
    return true;
}
void forgetRagdoll(const mobj_t *thing) {
    auto found=dolls.find(thing);
    if(found==dolls.end())return;
    // scp_7179_b is real
    dropCard(found->second);tearsForget(found->second);dolls.erase(found);
}
// Stepped with the game's wrap (carried.cpp): a doll's place in the world
// moves with its corpse; its floors and walls are the ones built alike. Up
// or down a storey, one still falling loses the floors it knows, and lies
// as the game's own drawing.
void ragdollsCarried(float dx,float dy,float dz) {
    for(auto it=dolls.begin();it!=dolls.end();) {
        Ragdoll &doll=it->second;
        if(dz!=0&&!doll.resting) {dropCard(doll);tearsForget(doll);it=dolls.erase(it);continue;}
        doll.ox+=dx;doll.oy+=dy;doll.oxWas+=dx;doll.oyWas+=dy;doll.gatherX+=dx;doll.gatherY+=dy;
        doll.floors.x0+=dx;doll.floors.y0+=dy;doll.spineAt[0]+=dx;doll.spineAt[1]+=dy;
        doll.gz+=dz;doll.gzWas+=dz;doll.gz0+=dz;doll.restFloor+=dz;
        for(Wall &w:doll.walls) {w.ax+=dx;w.ay+=dy;w.bx+=dx;w.by+=dy;w.top+=dz;}
        ++it;
    }
}
void ragdollsShutdown() {
    for(auto &entry:dolls)dropCard(entry.second);
    tearsShutdown();dolls.clear();reliefsShutdown();
}

static int frameLump(int state,int rotation,bool *flip=nullptr) {
    if(state<=0)return -1;
    const state_t &s=states[state];
    if(s.sprite<0||s.sprite>=numsprites)return -1;
    int frame=s.frame&FF_FRAMEMASK;
    if(frame>=sprites[s.sprite].numframes)return -1;
    const spriteframe_t &sf=sprites[s.sprite].spriteframes[frame];
    if(!sf.rotate&&rotation)return -1;
    if(flip)*flip=sf.flip[rotation];
    return firstspritelump+sf.lump[rotation];
}
static const Relief *reliefOfState(int state,bool ball=false) {
    bool flip=false,rightFlip=false,backFlip=false;
    int side=frameLump(state,2,&flip),right=frameLump(state,6,&rightFlip),back=frameLump(state,4,&backFlip);
    return reliefFor(frameLump(state,0),side,flip,right,rightFlip,back,backFlip,ball);
}
bool ragdollRigged(const mobj_t &thing) {return thing.info&&reliefOfState(thing.info->painstate)!=nullptr;}
int ragdollLump(const mobj_t &thing) {return thing.info?frameLump(thing.info->painstate,0):-1;}
bool ragdollRestJoints(int type,float joints[15][3],bool arms[2]) {
    if(type<0||type>=NUMMOBJTYPES)return false;
    const Relief *body=reliefOfState(mobjinfo[type].painstate);
    if(!body)return false;
    for(int n=0;n<POSE_POINTS&&n<15;++n) {joints[n][0]=body->rest[n].x;joints[n][1]=body->rest[n].y;joints[n][2]=body->rest[n].z;}
    arms[0]=body->arms[0];arms[1]=body->arms[1];
    return true;
}
void prepareRagdolls() {
    if(!settings.deathRagdoll)return;
    for(int type=0;type<NUMMOBJTYPES;++type)
        if(mobjinfo[type].flags&MF_COUNTKILL) {
            if(ballStiffness(type)&&!Game_BodyBall(type))reliefOfState(mobjinfo[type].painstate);   // (the fat trial: his rig's relief kept too)
            reliefOfState(mobjinfo[type].painstate,ballStiffness(type)!=0);
        }
}

// The top of a fence: a blocking two-sided line with a mid texture standing on the floor, placed as
// geometry.cpp draws it; below lo there is none.
static float fenceTop(const line_t &line,float lo,float hi) {
    float top=lo-1;
    if(!(line.flags&ML_BLOCKING))return top;
    for(int side=0;side<2;++side) {
        if(line.sidenum[side]<0)continue;
        const side_t &s=sides[line.sidenum[side]];
        if(!s.midtexture)continue;
        float height=units(textureheight[texturetranslation[s.midtexture]]);
        float anchor=((line.flags&ML_DONTPEGBOTTOM)?lo+height:hi)+units(s.rowoffset);
        if(std::max(lo,anchor-height)<=lo+24)top=std::max(top,std::min(hi,anchor));
    }
    return top;
}
// Walls and steps within reach of the body: one-sided lines, and two-sided
// ones whose far floor stands over 16 units above the body's (a step it
// can't lie across), whose opening is too low to lie through, or that a fence stands in.
static std::vector<Wall> wallsNear(float x,float y,float floor,float reach) {
    std::vector<Wall> out;
    for(int i=0;i<numlines;++i) {
        const line_t &line=lines[i];
        if(units(line.bbox[BOXLEFT])>x+reach||units(line.bbox[BOXRIGHT])<x-reach||
           units(line.bbox[BOXBOTTOM])>y+reach||units(line.bbox[BOXTOP])<y-reach)continue;
        float top=1e9f;
        if(line.backsector) {
            float lo=std::max(units(line.frontsector->floorheight),units(line.backsector->floorheight));
            float hi=std::min(units(line.frontsector->ceilingheight),units(line.backsector->ceilingheight));
            float fence=fenceTop(line,lo,hi);
            if(hi-lo>=16&&lo-floor<=16&&fence<=lo)continue;
            top=hi-lo<16?1e9f:std::max(lo,fence)-floor;
        }
        out.push_back({units(line.v1->x),units(line.v1->y),units(line.v2->x),units(line.v2->y),top});
    }
    return out;
}
// Whether a to b crosses a wall's line.
static bool crosses(float ax,float ay,float bx,float by,const Wall &w) {
    auto turn=[](float px,float py,float qx,float qy,float rx,float ry){return (qx-px)*(ry-py)-(qy-py)*(rx-px);};
    float d1=turn(w.ax,w.ay,w.bx,w.by,ax,ay),d2=turn(w.ax,w.ay,w.bx,w.by,bx,by);
    float d3=turn(ax,ay,bx,by,w.ax,w.ay),d4=turn(ax,ay,bx,by,w.bx,w.by);
    return ((d1>0)!=(d2>0))&&((d3>0)!=(d4>0));
}
// The floors round the kill: each cell's sector, sampled once (FloorGrid). A cell whose middle lies
// behind a solid wall (one-sided, or an opening too low to lie through) from the body samples whatever
// room is on the far side, another floor: it takes the body's own sector instead, or a ledge or pit
// up to a cell wide hugged the wall and snagged or swallowed limbs falling against it.
static FloorGrid floorsNear(float tx,float ty,const std::vector<Wall> &walls,float reach) {
    FloorGrid g;
    g.size=(int)(2*reach/floorCell);g.x0=tx-reach;g.y0=ty-reach;
    g.cells.resize((size_t)g.size*g.size);
    const sector_t *own=R_PointInSubsector((fixed_t)(tx*FRACUNIT),(fixed_t)(ty*FRACUNIT))->sector;
    std::vector<const Wall*> solid;
    for(const Wall &w:walls)if(w.top>=1e9f)solid.push_back(&w);
    for(int j=0;j<g.size;++j)for(int i=0;i<g.size;++i) {
        float x=g.x0+(i+0.5f)*floorCell,y=g.y0+(j+0.5f)*floorCell;
        const sector_t *sector=R_PointInSubsector((fixed_t)(x*FRACUNIT),(fixed_t)(y*FRACUNIT))->sector;
        if(sector!=own)
            for(const Wall *w:solid)if(crosses(tx,ty,x,y,*w)) {sector=own;break;}
        g.cells[(size_t)j*g.size+i]=sector;
    }
    return g;
}
static std::vector<int> deathStepsOf(int first) {
    std::vector<int> steps;
    for(int state=first,n=0;n<64&&state>0;++n) {
        if(std::find(steps.begin(),steps.end(),state)!=steps.end())break;
        steps.push_back(state);
        if(states[state].tics<0)break;
        state=states[state].nextstate;
    }
    return steps;
}

// The walls in body space round where it died (mirrored with the body: the doll falls unmirrored
// and is drawn mirrored).
static std::vector<Wall> wallsHere(const Ragdoll &doll) {
    std::vector<Wall> out;
    float side=doll.mirror?-1.0f:1.0f;
    for(const Wall &w:doll.walls) {
        float a0=w.ax-doll.ox,a1=w.ay-doll.oy,b0=w.bx-doll.ox,b1=w.by-doll.oy;
        out.push_back({side*(a0*doll.ax[0]+a1*doll.ax[1]),a0*doll.ay[0]+a1*doll.ay[1],
                       side*(b0*doll.ax[0]+b1*doll.ax[1]),b0*doll.ay[0]+b1*doll.ay[1],w.top});
    }
    return out;
}

// Its walls and floors gathered again round (wx, wy), where it has flown, so it flies on. The walls' tops
// stay measured from the floor it died on, as all its heights are.
static void regather(Ragdoll &doll,float wx,float wy) {
    doll.walls=wallsNear(wx,wy,doll.gz,doll.reach);
    doll.floors=floorsNear(wx,wy,doll.walls,doll.reach);
    doll.gx=doll.ox-doll.floors.x0;doll.gy=doll.oy-doll.floors.y0;
    doll.nearWalls=wallsHere(doll);doll.walls.clear();doll.walls.shrink_to_fit();
    ++doll.regathers;
    if(M_CheckParm((char*)"-ragdolllog"))fprintf(stderr,"ragdollregather %u tic %d\n",(unsigned)((uintptr_t)&doll%100000),doll.tics);
}

constexpr float fatSpine=0.6f;   // how far a fat body's spine closes (Ragdoll::spineLeast)
constexpr float heftyThrow=0.8f;   // a big monster's blast throw, of everyone's
// How heavy a body sounds coming down (contact_sounds.cpp): a man 1, a baron at most.
static float heftOf(const mobj_t &thing) {return std::clamp(std::sqrt(thing.info->mass/100.0f),0.8f,1.4f);}
// The fall the hit makes (tests/death_ragdoll.py FALLS): which points it strikes, the push
// (units per tic: across the body, away from the shooter, up) and each muscle group's hold
// and let-go (tics).
enum { FALL_CHEST,FALL_LEGS,FALL_SIDE,FALL_HEAD,FALL_SWEEP };
bool startRagdoll(const mobj_t &thing,float dx,float dy,float speed,float side,float high,float hitZ,float hitU,bool blast,
                  const std::vector<GorePellet> *gore,bool onlyCut,bool burnt) {
    const bool punch=settings.deathBlast&&!blast&&!burnt;    // F4 Flying enemies: shots punch harder, blasts fly
    const bool rocket=blast;                                 // (the throw of any blast is left as it is: no walking in it)
    blast=blast&&settings.deathBlast;
    if(punch)speed*=hitPunch;
    const bool rides=Game_Phobos()!=0;
    if(rides)speed*=phobosShove;
    const mobjinfo_t &info=*thing.info;
    const Relief *body=reliefOfState(info.painstate);
    if(!body)return false;
    Ragdoll doll;
    doll.body=body;doll.state=info.painstate;doll.start=leveltime;doll.steps=deathStepsOf(info.deathstate);
    doll.hollywood=settings.deathBlast;
    const bool knelt=bodyKnelt(&thing);   // killed down on one knee: it falls from there (ragdoll_kneel.cpp)
    // Gibbed by a blast (Flying enemies only, see startDeath): drawn whole and thrown while the game plays
    // its gib frames, so the doll lasts as long as those do.
    if(std::find(doll.steps.begin(),doll.steps.end(),(int)(thing.state-states))==doll.steps.end()&&info.xdeathstate>0)
        doll.steps=deathStepsOf(info.xdeathstate);
    doll.poseTics=std::clamp((int)states[info.deathstate].tics,2,10);   // the pace of its own death frames
    if(doll.steps.size()<2)return false;
    // Its draw: the thing and the tic. Test runs (ragdoll_test.cpp) key it by where the thing was placed
    // instead of where it sits in memory, so a run replays the same falls.
    uint32_t who=(uint32_t)(uintptr_t)&thing;
    if(M_CheckParm((char*)"-ragdolltest"))who=(uint32_t)thing.spawnpoint.x*73856093u^(uint32_t)thing.spawnpoint.y*19349663u^(uint32_t)thing.type;
    uint32_t seed=hash32(who*2654435761u^(uint32_t)leveltime*40503u^0x7a9du);
    float chance=(float)(hash32(seed^0x6a09e667u)>>8)/16777216.0f;   // its own draw: the falls' stay as they were
    auto rnd=[&] {seed=hash32(seed+0x9e3779b9u);return (float)(seed>>8)/16777216.0f;};
    // The body's axes: the way it faced when it died (a monster shot from behind falls from behind,
    // its back painted from the drawing seen from behind). Turned to face the player, every death
    // swung round at the kill, and a shot in the back read as a blast to the chest. Kicked off its feet,
    // the way it was drawn rolled over (p_swept.c): shot with its back shown, it falls on from its back.
    float facing=(float)(Game_SweptFacing(&thing)*(2*doomPi/4294967296.0)),fx=-std::cos(facing),fy=-std::sin(facing);
    doll.ax[0]=fy;doll.ax[1]=-fx;doll.ay[0]=fx;doll.ay[1]=fy;
    doll.ox=units(thing.x);doll.oy=units(thing.y);
    doll.rides=rides;if(rides) {doll.gravity=Game_PhobosGravity();doll.reach=phobosReach;}
    // Its body drawn one way or the other, its own draw: the one drawing it is raised from shows both hands.
    // (Two of a kind dropped in the same tic were once forced the opposite ways, from the drawn deaths, where
    // that was all that told them apart; a doll's fall is its own, and two thrown apart by one rocket flew as
    // each other's reflection: the user.)
    doll.mirror=rnd()<0.5f;
    // World to body space: x mirrored with the drawing (the doll falls unmirrored, like the walls).
    float sgn=doll.mirror?-1.0f:1.0f;
    auto bodyWay=[&](float wx,float wy){return V3{sgn*(wx*doll.ax[0]+wy*doll.ax[1]),wx*doll.ay[0]+wy*doll.ay[1],0};};
    V3 struckWay=bodyWay(dx,dy);
    float tx=struckWay.x,ty=struckWay.y;
    if(std::abs(tx)+std::abs(ty)<1e-3f)ty=1;
    if(const mobj_t *eye=players[displayplayer].mo) {
        V3 e=bodyWay(units(eye->x-thing.x),units(eye->y-thing.y));
        if(length(e)>1e-3f)doll.toEye=unit(e);
    }
    // Side and hitU come across the shot (positive: the shooter's left); in body space that is the
    // body's -x only when it faces the shooter.
    float acrossShot=sgn*(dy*doll.ax[0]-dx*doll.ax[1]);
    side*=acrossShot;hitU*=acrossShot;
    // Gore's scars (hits before the killing blow, PLAN-gore.md A): where on the body each struck (its own
    // shot's way across the body as it faced then, mirrored as drawn), put into the killing blow's frame
    // (u across it), at half their damage. Killed side-on (nothing across the body), they're left out.
    std::vector<GorePellet> scarred;
    if(gore) {
        for(GorePellet pellet:*gore) {
            if(pellet.earlier) {
                if(std::abs(acrossShot)<0.3f)continue;
                pellet.u=pellet.u*pellet.across*sgn/acrossShot;pellet.damage=(pellet.damage+1)/2;
            }
            scarred.push_back(pellet);
        }
        gore=scarred.empty()?nullptr:&scarred;
    }
    bool headshot=body->hasHead&&hitZ>=body->neck&&std::abs(hitU-body->headMiddle)<=body->headWidth/2+1;
    // Weak hits buckle the legs. A bullet pushes a 100-mass monster at most 1.9 units a tic: under
    // the old cut at 2 every pistol and chaingun kill buckled, never thrown.
    // A heavy blast low on the legs sweeps them out from under it instead: the feet kicked away along the
    // shot, and the body, its support gone, falls by its own weight (buckled, it sank in place and
    // toppled over 10 tics, a collapse).
    // Gore: where each pellet of the killing blow landed decides the head (ragdoll_gore.cpp).
    // Cut in half first: then the head goes with the upper half as it is.
    bool halves=goreHalvesOf(doll,thing,gore,acrossShot,seed);
    GoreHead outcome=halves?GORE_HEAD_STAYS:goreHeadOf(doll,thing,gore,acrossShot,seed);
    goreArmsOf(doll,thing,gore,acrossShot);
    if(!halves)goreLegsOf(doll,thing,gore,acrossShot);
    // Shot out of the air, kicked off its feet (Authored's kick, p_swept.c): the kick's reward, so more of it
    // goes than the rules would take. Blown off in bites (goreBlowOf), not cut in half: the halves are drawn
    // upright, and this body is on its side (the user, 2026-11-14). An arm always, often the other, often a leg.
    float sweptWay[2]={},sweptUp=0;
    const float sweptOver=Game_SweptDead(&thing,1,&sweptWay[0],&sweptWay[1],&sweptUp);
    const bool airKill=sweptOver>0.05f&&thing.z>thing.floorz;
    doll.airKill=airKill;
    if(airKill&&goreOn()&&!halves) {
        int first=rnd()<0.5f?0:1;
        if(body->arms[first])doll.armOff[first]=true;
        if(rnd()<0.45f&&body->arms[first^1])doll.armOff[first^1]=true;
        if(rnd()<0.6f) {int leg=rnd()<0.5f?0:1;if(!doll.legOff[leg])doll.legOff[leg]=1;}
    }
    goreBlastLimbsOf(doll,thing,tx,high,seed^0x51ed270bu);
    bool legless=doll.legOff[0]||doll.legOff[1];
    // A gib kill (goreGibKill) that tore nothing off by where its pellets struck (all on the chest), eight
    // pellets or more on it (a point-blank volley): the upper body goes, cut at the waist (PLAN-gore.md H,
    // the super shotgun point blank).
    if(gore&&goreGibKilling()&&gore->size()>=8&&!halves&&outcome==GORE_HEAD_STAYS&&!doll.armOff[0]&&!doll.armOff[1]&&!legless)
        halves=goreHalvesOf(doll,thing,gore,acrossShot,seed,true);
    // Blown apart or severed: the body's head is gone either way (burst, or flying off on its own).
    doll.headless=outcome==GORE_HEAD_BLOWN||outcome==GORE_HEAD_SEVERED;
    if(doll.headless||doll.torn)headshot=true;
    if(onlyCut&&!doll.headless&&!doll.torn&&!doll.halved&&!doll.armOff[0]&&!doll.armOff[1]&&!legless)return false;
    if(doll.headless||doll.torn||doll.halved)doll.guts=&gutsOf(thing);
    doll.goreSeed=seed^0x5bd1e995u;
    if(goreOn()) {goreBlowMore(airKill?1.7f:1);goreBlowOf(doll,thing,gore,acrossShot,tx,ty,high,rocket||airKill);goreBlowMore(1);}
    // Burnt (fire, plasma): no blow to speak of; the legs give first and it folds down where it stood.
    // A leg shot off: down on that side, the legs' fall.
    // (Kneeling, its legs are down already: nothing to sweep.)
    int kind=(burnt||legless)&&!headshot?FALL_LEGS:headshot?FALL_HEAD:high<-0.2f&&speed>=sweepFrom&&!knelt?FALL_SWEEP:high<-0.2f||speed<1.0f?FALL_LEGS:std::abs(side)>0.5f?FALL_SIDE:FALL_CHEST;
    // Shot out of the air: the body is lying in it, so a hit "on the legs" is only where its low side was.
    // Swept again, it took a second stiff flip and a held trunk on top of the first: a plank (the user,
    // 2026-11-16). It is struck in the middle instead, and goes limp (below).
    if(airKill&&(kind==FALL_SWEEP||(kind==FALL_LEGS&&!burnt&&!legless)))kind=FALL_CHEST;
    float force=std::clamp(speed/6,0.6f,1.4f),blow=std::clamp(speed/3.5f,0.8f,1.6f);   // blow: 1 at a shotgun kill
    if(M_CheckParm((char*)"-ragdolllog"))fprintf(stderr,"ragdollstart tic %d speed %.2f kind %d punch %d blast %d slide %.2f hitZ %.1f hitU %.1f neck %.1f head %.1f/%.1f headless %d gore %d onlyCut %d set %d\n",
        leveltime,speed,kind,punch,blast,std::hypot(units(thing.momx),units(thing.momy)),hitZ,hitU,body->neck,body->headMiddle,body->headWidth,(int)doll.headless,(int)(gore!=nullptr),(int)onlyCut,settings.gore);
    bool leftHit=side>0;                         // side: positive on the shooter's left, the body's -x
    int struck=leftHit?0:1;float across=leftHit?1.0f:-1.0f;
    std::vector<int> hit;V3 push;
    // Swept: legs and trunk stiff a few tics, so the whole body turns as one (folded at the hips, the kick
    // only sat it down), the neck loose so the head lags and comes down last and hardest.
    static const float holds[5][MUSCLES][2]={{{9,6},{5,10},{2,6}},{{0,5},{10,10},{6,10}},{{4,8},{5,10},{0,4}},{{0,2},{0,2},{0,2}},{{4,4},{8,6},{0,3}}};
    switch(kind) {
    case FALL_CHEST:hit={P_SPINE,P_SH_L,P_SH_R,P_NECK};push=V3{tx,ty,0}*(2.2f*force)+V3{0,0,0.4f};break;
    case FALL_LEGS:hit={struck?P_HIP_R:P_HIP_L,struck?P_KNEE_R:P_KNEE_L,P_SPINE};push=V3{1.1f*across,0.5f*ty,0}*blow;break;
    case FALL_SIDE:hit={struck?P_SH_R:P_SH_L,P_NECK,P_SPINE};push=V3{1.6f*across,0.7f*ty,0}*blow;break;
    case FALL_SWEEP:break;
    default:hit={P_HEAD};push=V3{tx,ty,0}*(1.2f*blow);break;
    }
    for(int g=0;g<MUSCLES;++g) {doll.hold[g]=holds[kind][g][0]+(rnd()*4-2);doll.over[g]=holds[kind][g][1];}
    // Burnt: the strength leaves all of it, the legs first, the trunk and neck a moment behind (held as a
    // legs hit's 10 tics, the stiff trunk went over backward like a plank instead of folding down).
    if(burnt) {
        static const float limp[MUSCLES][2]={{0,3},{3,7},{2,6}};
        for(int g=0;g<MUSCLES;++g) {doll.hold[g]=limp[g][0]+rnd()*2;doll.over[g]=limp[g][1];}
        doll.burnt=true;
    }
    // A fat body folds at the belly: the spine closes much further than a thin one's, and a shot leaves its trunk
    // limp. Stiff, all of it tipped over as one piece, like a cookie (the user).
    if(Game_BodyFat(thing.type)) {
        doll.spineLeast=fatSpine;
        if(!blast) {
            static const float slump[MUSCLES][2]={{1,4},{0,3},{2,6}};
            for(int g=0;g<MUSCLES;++g) {doll.hold[g]=slump[g][0]+rnd()*2;doll.over[g]=slump[g][1];}
        }
    }
    doll.late=rnd()*6;                           // one leg's muscles let go later than the other's
    doll.p=doll.prev=body->rest;
    if(knelt)kneelStart(doll);
    // Killed with its feet swept away (Authored's kick, p_swept.c): the doll starts as the picture was, turned
    // about the hips feet first the way they went and let down as far, and keeps what it had: the turn at the
    // rate the picture was turning, the rise or fall it was in, and a throw along the kick. Upright, the body
    // stood up again for its own death; at rest, the sweep's momentum was gone with the kill (the user,
    // 2026-11-14, both).
    {
        float ago[2]={},up=sweptUp,over=sweptOver,before=Game_SweptDead(&thing,0,&ago[0],&ago[1],&up);
        const float *way=sweptWay;
        if(over>0.05f) {
            V3 d=unit(bodyWay(way[0],way[1]));   // mirrored with the drawing, as every way in the body is
            V3 c=(doll.p[P_HIP_L]+doll.p[P_HIP_R])*0.5f;
            float angle=over*1.45f,co=std::cos(angle),si=std::sin(angle),low=1e9f,was=1e9f;
            // Folded at the waist after its feet, as the picture was (bodyPose's fold): without it the trunk
            // snapped straight at the kill, plain to see in slow motion.
            float fold=0.27f*std::sin((float)doomPi*std::min(over,1.0f))+0.12f*over,fc=std::cos(fold),fs=std::sin(fold);
            for(int n=0;n<DOLL_POINTS;++n) {
                V3 r=doll.p[n]-c;float a=dot(r,d),h=r.z;
                if(h>0)doll.p[n]=c+(r-d*a-V3{0,0,h})+d*(a*fc+h*fs)+V3{0,0,h*fc-a*fs};
            }
            // Still going over: no slower than a brisk fall, whatever the eased picture was doing at that tic.
            float turning=over<0.98f?std::max((over-before)*1.45f,0.16f):0;
            V3 carried=over<0.98f?d*1.5f+V3{0,0,std::max(up,0.0f)+1.0f}:V3{0,0,0};
            // Shot out of the air: the blast kicks it up and over harder, and it hangs a beat (a lighter pull).
            // Its rise or fall kept to the end of the turn too (fully over, it lost it and dropped dead).
            if(airKill) {turning*=1.4f;carried=V3{carried.x,carried.y,up+2.5f};doll.gravity*=0.65f;}
            for(int n=0;n<DOLL_POINTS;++n) {
                V3 r=doll.p[n]-c;float a=dot(r,d),h=r.z;
                was=std::min(was,doll.p[n].z);
                doll.p[n]=c+(r-d*a-V3{0,0,h})+d*(a*co-h*si)+V3{0,0,a*si+h*co};
                low=std::min(low,doll.p[n].z);
            }
            for(int n=0;n<DOLL_POINTS;++n) {
                doll.p[n].z-=low-was;doll.prev[n]=doll.p[n];
                V3 r=doll.p[n]-c;
                doll.push(n,d*(-r.z*turning)+V3{0,0,dot(r,d)*turning}+carried);
            }
        }
    }
    // Two chest blasts in three from the front (pushed away from the eye) play the shot death on
    // strings, where the hand rig fits the keys. The harder the blast, the harder the wire: a
    // shotgun's kill pushes 3-4 units a tic; point blank it lands flat on id's own timing.
    // Every chest blast launches the body, as hard as the game pushed it: a far shot hops it back, point
    // blank throws it (2 up, 1 more along the shot and a 0.05 somersault at a shotgun kill's 3.5 units
    // a tic, on top of the corpse's own slide, which the doll now flies with; heavy monsters, pushed
    // less, barely leave the floor; a bullet only hops it).
    float hard=kind==FALL_CHEST?std::clamp((speed-0.8f)/2.7f,0.0f,1.5f):0.0f;
    float sideways=rnd()*0.5f-0.25f;
    if(punch&&(kind==FALL_CHEST||kind==FALL_SIDE)) {
        // Graded by the (doubled) push: up 2 at a chaingun's kill, 2.4 a shotgun's, 3.4 a super shotgun's
        // point blank. And it spins as struck: a hit off the middle turns it about the vertical (a pirouette,
        // the struck side going back), a high one tips it over backward (a somersault); the harder, the
        // faster. How much it turns is drawn for each body: about half barely turn, a third as struck, one in
        // seven really spins (the user: the same medium turn on every body was a bit much, and with the
        // sideways throw, twice what it is now, it mostly carried them off to the side).
        float lift=std::clamp(1.2f*std::log(std::max(speed,1.0f)),0.0f,punchLift),along=std::min(0.12f*speed,2.0f);
        doll.launchV=V3{tx+sideways,ty,0}*along+V3{0,0,lift};
        doll.launchSpin=std::min(0.012f*speed*(0.5f+std::max(high,0.0f)),0.16f)*(0.7f+0.6f*rnd());
        float pick=rnd(),turn=pick<0.55f?0.1f+0.2f*rnd():pick<0.85f?0.6f+0.4f*rnd():1.6f+0.6f*rnd();
        float twist=std::clamp(-side*ty*0.035f*speed,-0.35f,0.35f);
        if(turn>1.5f&&std::abs(twist)<0.12f)twist=std::copysign(0.12f,twist!=0?twist:rnd()-0.5f);   // a real spin even struck square
        doll.launchTwist=std::clamp(twist*turn,-0.5f,0.5f);
        doll.launchAt=0;
    }
    else if(hard>0) {
        float most=std::min(hard,1.2f);              // a super shotgun's 9 a tic threw it past the walls it knows
        doll.launchV=V3{tx+2*sideways,ty,0}*std::min(hard,1.0f)+V3{0,0,2.0f*most};doll.launchSpin=0.05f*std::min(hard,1.0f);doll.launchAt=0;
    }
    doll.blasted=blast;
    if(blast&&!rides)doll.reach=blastReach;   // (Phobos keeps its own, wider)
    if(blast) {
        // Each body its own throw: as hard and as high give or take a quarter, its somersault 0.4 to 1.6 of it,
        // half of them turning a little as they go (the same throw for all, two bodies either side of a rocket
        // flew as each other's mirror image; the user).
        float far=0.75f+0.5f*rnd(),high2=0.7f+0.6f*rnd();
        doll.launchV=V3{tx+2*sideways,ty,0}*(blastThrow*far)+V3{0,0,blastLift*high2};
        doll.launchSpin=blastSpin*(0.4f+1.2f*rnd());
        if(rnd()<0.5f)doll.launchTwist=(0.04f+0.06f*rnd())*(rnd()<0.5f?-1.0f:1.0f);
        doll.launchAt=0;
        // With gore a big monster's the same throw, with some weight in it: lower, shorter. (Its turn stays: slower, it came down on its feet and stood.)
        if(goreOn()&&goreHefty(thing))doll.launchV=doll.launchV*heftyThrow;
    }
    // Shot out of the air (airKill): the sweep's own turn alone cartwheeled every body in one plane, the
    // kick's, whatever the volley did, a board on rails (the user, 2026-11-16). So the hit turns it too:
    // about the vertical toward the struck side, drawn per body as the chest launch's (about half barely, a
    // third as struck, one in seven really spins), and a little about the shot's line, so the cartwheel
    // tilts off its plane. The twist set also keeps the in-air roll brake off this body (Ragdoll::damp).
    if(airKill&&!rides) {
        float pick=rnd(),turn=pick<0.55f?0.3f+0.3f*rnd():pick<0.85f?0.8f+0.4f*rnd():1.8f+0.6f*rnd();
        float way=std::abs(side)>0.1f?(side>0?-1.0f:1.0f):(rnd()<0.5f?-1.0f:1.0f);
        float twist=std::clamp((0.04f+0.06f*std::abs(side))*turn,0.01f,0.3f)*way;
        float roll=(0.02f+0.05f*rnd())*(rnd()<0.5f?-1.0f:1.0f);
        V3 c{},line=unit(V3{tx,ty,0});
        for(int n=0;n<DOLL_POINTS;++n)c=c+doll.p[n];
        c=c*(1.0f/DOLL_POINTS);
        if(doll.launchAt<0)for(int n=0;n<DOLL_POINTS;++n) {V3 r=doll.p[n]-c;doll.push(n,V3{-r.y,r.x,0}*twist);}
        for(int n=0;n<DOLL_POINTS;++n)doll.push(n,cross(line,doll.p[n]-c)*roll);
        // And the roll its picture was in (p_swept.c, through its views) goes on about its own length, as
        // fast: it stopped dead at the kill. Hips to neck is the way the views turn about; mirrored, the
        // body's space turns the other way round.
        V3 lengthWay=unit(doll.p[P_NECK]-(doll.p[P_HIP_L]+doll.p[P_HIP_R])*0.5f);
        V3 spin=lengthWay*(Game_SweptRoll(&thing)*sgn);
        for(int n=0;n<DOLL_POINTS;++n)doll.push(n,cross(spin,doll.p[n]-c));
        doll.launchTwist=twist;   // a launch at tic 0 (punch) turns it by this instead of its own
        doll.launchV.z*=0.5f;     // (the air kill's own pop is already in it)
        // Limp at once, the neck loosest: every body point turned at one rate and held by its muscles, it
        // flew as one stiff piece. The volley takes its middle along the shot; the head and legs are left
        // behind, each its own way, so it folds round the hit and the legs trail apart.
        for(int g=0;g<MUSCLES;++g) {doll.hold[g]=g==M_NECK?0:rnd()*1.5f;doll.over[g]=3+rnd()*3;}
        doll.late*=0.3f;
        doll.pushInside({{P_SPINE,line*1.4f},{P_HIP_L,line*0.9f},{P_HIP_R,line*0.9f}});
        for(int n:{P_ANK_L,P_ANK_R,P_KNEE_L,P_KNEE_R,P_HEAD}) {
            V3 loose{rnd()*2-1,rnd()*2-1,rnd()*2-1};
            doll.pushInside({{n,loose*(0.4f+0.6f*rnd())}});
        }
        doll.reach=std::max(doll.reach,blastReach);   // it flies as far as a blast's body
    }
    doll.tipWay=unit(V3{tx,ty,0});
    if(kind==FALL_SWEEP) {
        // Hollywood, like the chest launch: the blast to the shins throws the legs up and out along the
        // shot and turns the body over about its middle, the trunk going back toward the shooter (on its
        // face, from the front), and lifts it a little; the knees are gone (no muscle holds the legs).
        // From there only its weight: the trunk, still held, comes down hard, the loose head last. Its own
        // weight alone was too slow: the feet slid out and the trunk toppled like a felled tree, 19 tics
        // to the floor (a collapse), or, the feet lifted, the whole body dropped upright (nothing turns a
        // falling body). The lower the hit and the harder the blast, the harder the throw; one leg
        // kicked a little harder: no two alike. And it turns 30-45 degrees about the vertical toward the
        // struck side on the way down: straight along the shot the body fell along the line of sight, its
        // flying legs hidden behind a foreshortened heap (and turned over about a slanted axis instead, it
        // went down diagonally onto a shoulder and rolled back up to sitting).
        float low=std::clamp((-high-0.2f)/0.6f,0.0f,1.0f),strength=std::clamp(speed/3.5f,0.7f,1.4f)*(0.6f+0.4f*low);
        float slant=(0.52f+0.27f*rnd())*(std::abs(side)>0.05f?across:rnd()<0.5f?1.0f:-1.0f);
        V3 d=doll.tipWay,c{};
        doll.tipWay=d*-1;doll.swept=true;
        for(int n=0;n<DOLL_POINTS;++n)c=c+doll.p[n];
        c=c*(1.0f/DOLL_POINTS);
        for(int n=0;n<DOLL_POINTS;++n) {V3 r=doll.p[n]-c;doll.push(n,V3{d.x*r.z,d.y*r.z,-dot(r,d)}*(-sweepSpin*strength)+V3{0,0,sweepLift*strength});}
        int lead=rnd()<0.5f?P_ANK_L:P_ANK_R;
        doll.push(lead,d*(0.8f*strength));
        float yaw=slant/sweepTurnTics;
        for(int n=0;n<DOLL_POINTS;++n) {V3 r=doll.p[n]-c;doll.push(n,V3{-r.y,r.x,0}*yaw);}
        doll.noDrift();
    } else if(settings.deathGuided&&!rides&&!knelt&&body->guided&&kind==FALL_CHEST&&ty>0.7f&&chance<0.67f&&!blast&&!airKill) {
        doll.guided=true;
        if(doll.guide.flinch&&doll.launchAt>=0)doll.launchAt=flinchTics;
        doll.yank=V3{tx+sideways,ty,0.15f}*(doll.guide.ratchet*std::clamp(speed/4,0.7f,1.8f));
        doll.yaw=rnd()*0.12f-0.06f;
        for(float &kick:doll.kicks)kick=0.2f+0.6f*rnd();
        doll.legLate=rnd()*2;
    } else {
        // The push, varied a little in strength and across; a little turn about the vertical;
        // a small, uneven kick forward and out to each knee. Then the push's net drift is taken
        // out: the game's own momentum carries the body, the doll only turns and folds it.
        float drift=rnd()*0.8f-0.4f;
        if(burnt) {push=push*0.25f;drift*=0.25f;}
        for(int n:hit)doll.push(n,push*(0.8f+0.4f*rnd())+V3{drift,0,0});
        float yaw=rnd()*0.2f-0.1f;
        for(int n=0;n<DOLL_POINTS;++n) {V3 r=doll.p[n]-doll.p[P_SPINE];doll.push(n,V3{-r.y,r.x,0}*yaw);}
        V3 f=doll.front();
        for(int s=0;s<2;++s)doll.push(s?P_KNEE_R:P_KNEE_L,f*(0.2f+0.9f*rnd())+V3{(s?1.0f:-1.0f)*0.4f*rnd(),0,0});
        if(burnt) {   // the knees buckle forward and the hips drop: down onto them, not over
            for(int s=0;s<2;++s)doll.push(s?P_KNEE_R:P_KNEE_L,f*(0.8f+0.4f*rnd()));
            for(int n:{P_HIP_L,P_HIP_R,P_SPINE})doll.push(n,V3{0,0,-1.0f});
        }
        doll.noDrift();
        // And the hit tips it over its feet the way it was struck, at once (steered on through the
        // fall instead, the turn came late and eased, a dive rather than a blow).
        doll.tip(burnt?0.05f:kind==FALL_CHEST?0.15f*blow:0.3f*blow);
        // And burnt, a twitch or two on the way down: a hip, a shoulder or a knee jerks (the nerves firing
        // once more), 4-8 tics in and again 6-12 later, the second weaker.
        if(burnt) {
            doll.twitchAt[0]=4+(int)(rnd()*5);doll.twitchAt[1]=rnd()<0.6f?doll.twitchAt[0]+6+(int)(rnd()*7):-1;
            doll.twitchSeed=hash32(seed^0x2c1b3c6du);
        }
    }
    // Gore: and each pellet of the killing volley jerks the part it struck, on top of the fall the volley as a whole
    // makes (taken out of the whole body, so the fall goes where it went): a blast across one shoulder snaps that
    // shoulder back, and two kills of a kind don't start alike.
    if(gore&&!blast&&!burnt) {
        float gave[DOLL_POINTS]={};
        for(const GorePellet &pellet:*gore) {
            if(pellet.earlier)continue;
            float u=pellet.u*acrossShot;int near=-1;float best=1e9f;
            for(int n=0;n<DOLL_POINTS;++n) {
                if(doll.rider(n))continue;
                float d=std::hypot(body->rest[n].x-u,body->rest[n].z-pellet.z);
                if(d<best) {best=d;near=n;}
            }
            if(near<0||gave[near]>=pelletJerkMost)continue;
            float kick=std::min(pelletJerk*pellet.damage/10.0f,pelletJerkMost-gave[near]);
            gave[near]+=kick;
            doll.pushInside({{near,V3{tx,ty,0}*kick}});
        }
    }
    // On Phobos the blow's throw is the game's (its real momentum on the corpse) and a launch keeps only
    // some of its turn: what is left of the pushes turns and folds the body, carrying it nowhere.
    if(rides) {
        doll.launchWay=unit(V3{doll.launchV.x,doll.launchV.y,0});
        doll.launchV={};doll.launchSpin*=phobosSpin;doll.launchTwist*=phobosSpin;
        V3 mean{};
        for(int n=0;n<DOLL_POINTS;++n)mean=mean+(doll.p[n]-doll.prev[n]);
        mean=mean*(1.0f/DOLL_POINTS);
        for(int n=0;n<DOLL_POINTS;++n)doll.prev[n]=doll.prev[n]+mean;
    }
    // The corpse's own slide at the kill is the doll's: it flies on in the world from where it died.
    // Carried by the thing it was braked by the floor's friction in mid-air and slid on after it landed.
    if(!rides) {
        float mx=units(thing.momx),my=units(thing.momy),ms=std::hypot(mx,my);
        if(punch&&!airKill) {mx*=hitPunch;my*=hitPunch;ms*=hitPunch;}
        if(burnt) {mx*=0.2f;my*=0.2f;ms*=0.2f;}   // (the push the game gave it is the drawing's to ignore)
        float most=airKill?airCarry:blast?blastCarry:punch?punchCarry:carryMost;
        if(ms>most) {mx*=most/ms;my*=most/ms;}
        // And its own walking: the game walks monsters in jumps, with no momentum to carry (bodyWalk). A body
        // running at you goes on coming as it falls; what it throws off goes with it (ragdoll_gore.cpp).
        if(!rocket) {
            bodyWalk(&thing,doll.walk[0],doll.walk[1]);mx+=doll.walk[0];my+=doll.walk[1];
            if(M_CheckParm((char*)"-ragdolllog"))fprintf(stderr,"ragdollwalk %.2f %.2f slide %.2f %.2f\n",doll.walk[0],doll.walk[1],mx,my);
        }
        // Turned a few degrees off the shot's line, one way or the other: straight along it, every body
        // of a kind flew the same line and landed alike. Drawn last, so the rest of the fall is unchanged.
        float turn=(slideTurnLeast+(slideTurnMost-slideTurnLeast)*rnd())*(rnd()<0.5f?-1.0f:1.0f)*doomPi/180;
        float tc=std::cos(turn),ts=std::sin(turn),turned=mx*tc-my*ts;my=mx*ts+my*tc;mx=turned;
        V3 carried=bodyWay(mx,my);
        for(int n=0;n<DOLL_POINTS;++n)doll.push(n,carried);
    }
    doll.armsAlong();
    doll.shown=doll.shownWas=doll.shownTic=doll.p;
    doll.walls=wallsNear(units(thing.x),units(thing.y),units(thing.floorz),doll.reach);
    doll.floors=floorsNear(units(thing.x),units(thing.y),doll.walls,doll.reach);
    doll.gx=doll.ox-doll.floors.x0;doll.gy=doll.oy-doll.floors.y0;doll.gz=units(thing.z);
    doll.nearWalls=wallsHere(doll);
    doll.oxWas=doll.ox;doll.oyWas=doll.oy;doll.gz0=doll.gzWas=doll.gz;doll.gatherX=doll.ox;doll.gatherY=doll.oy;
    if(!rides) {doll.walls.clear();doll.walls.shrink_to_fit();}
    goreHeadStart(doll,thing,outcome,dx,dy,hitU,hitZ,tx,blow);
    goreLegsStart(doll,thing,dx,dy,blow);
    goreArmsStart(doll,thing,dx,dy,blow);
    goreLegsOffStart(doll,thing,dx,dy,blow);
    goreBlowStart(doll,thing,dx,dy,blow);
    for(int n=0;n<DOLL_POINTS;++n)doll.centre[0]+=doll.p[n].x/DOLL_POINTS,doll.centre[1]+=doll.p[n].y/DOLL_POINTS;
    std::copy(doll.centre,doll.centre+2,doll.centreWas);std::copy(doll.centre,doll.centre+2,doll.centreShown);
    forgetRagdoll(&thing);
    Ragdoll &kept=dolls[&thing]=std::move(doll);
    tearsMoved(doll,kept);   // the cuts' fibres were tied to it before it was kept
    return true;
}

// A ball of flesh's doll (body_ball.cpp): its relief, its walls and floors, its way of facing and mirroring as
// any doll's, standing as it died; the ball moves it from there. Null without a pain drawing to raise.
Ragdoll *addBallDoll(const mobj_t &thing) {
    const mobjinfo_t &info=*thing.info;
    const Relief *body=reliefOfState(info.painstate,true);
    if(!body)return nullptr;
    Ragdoll doll;
    doll.body=body;doll.state=info.painstate;doll.start=leveltime;doll.steps=deathStepsOf(info.deathstate);
    doll.poseTics=std::clamp((int)states[info.deathstate].tics,2,10);
    if(doll.steps.size()<2)return nullptr;
    uint32_t who=(uint32_t)(uintptr_t)&thing;
    if(M_CheckParm((char*)"-ragdolltest"))who=(uint32_t)thing.spawnpoint.x*73856093u^(uint32_t)thing.spawnpoint.y*19349663u^(uint32_t)thing.type;
    doll.goreSeed=hash32(who*2654435761u^(uint32_t)leveltime*40503u^0x6cac0u);
    float facing=(float)(thing.angle*(2*doomPi/4294967296.0)),fx=-std::cos(facing),fy=-std::sin(facing);
    doll.ax[0]=fy;doll.ax[1]=-fx;doll.ay[0]=fx;doll.ay[1]=fy;
    doll.ox=units(thing.x);doll.oy=units(thing.y);
    doll.mirror=(doll.goreSeed>>8&1)!=0;
    doll.p=doll.prev=doll.shown=doll.shownWas=doll.shownTic=body->rest;
    doll.walls=wallsNear(units(thing.x),units(thing.y),units(thing.floorz),doll.reach);
    doll.floors=floorsNear(units(thing.x),units(thing.y),doll.walls,doll.reach);
    doll.gx=doll.ox-doll.floors.x0;doll.gy=doll.oy-doll.floors.y0;doll.gz=units(thing.floorz);
    doll.oxWas=doll.ox;doll.oyWas=doll.oy;doll.gz0=doll.gzWas=doll.gz;doll.gatherX=doll.ox;doll.gatherY=doll.oy;
    forgetRagdoll(&thing);
    return &(dolls[&thing]=std::move(doll));
}

// Phobos: the body goes where its corpse slides, rises and falls (the game's momentum carries it; its
// own drift is taken out each tic), its walls and floors gathered again once it has slid off them.
// Aloft, corpse and body fall together and the body feels no weight; on the floor, Phobos' pull.
static void followCorpse(Ragdoll &doll,const mobj_t &thing) {
    if(doll.followedAt==leveltime)return;
    doll.followedAt=leveltime;
    float x=units(thing.x),y=units(thing.y);
    doll.oxWas=doll.ox;doll.oyWas=doll.oy;doll.gzWas=doll.gz;
    doll.ox=x;doll.oy=y;doll.gz=units(thing.z);
    doll.gravity=thing.z<=thing.floorz?Game_PhobosGravity():0;
    if(doll.resting)return;
    if(std::hypot(x-doll.gatherX,y-doll.gatherY)>phobosRegather) {
        doll.walls=wallsNear(x,y,units(thing.floorz),doll.reach);doll.floors=floorsNear(x,y,doll.walls,doll.reach);
        doll.gatherX=x;doll.gatherY=y;
    }
    doll.gx=x-doll.floors.x0;doll.gy=y-doll.floors.y0;
    if(x!=doll.oxWas||y!=doll.oyWas)doll.nearWalls=wallsHere(doll);
    if(doll.pinned)return;   // its feet hold it: the lean is all its own
    doll.noDrift();
    // Nor does it rise on its own: the corpse carries any lift. At Phobos' pull the
    // solver's small nudges up off the floor never came back down, and bodies left
    // lying drifted up to the ceiling.
    V3 mean{};
    for(int n=0;n<DOLL_POINTS;++n)mean=mean+(doll.p[n]-doll.prev[n]);
    if(mean.z>0)for(int n=0;n<DOLL_POINTS;++n)doll.prev[n].z+=mean.z/DOLL_POINTS;
}
// Phobos: a living monster a draft tore off its feet goes limp and tumbles instead of flying
// upright in its walking frames: a doll riding the thing (followCorpse) from its pain drawing,
// turning over the way it was taken. Once its boots find a floor again the game stands it up
// (its pain frame) and the doll is dropped (tumbleView).
static bool startTumble(const mobj_t &thing,bool pinned,const float draft[3]) {
    const mobjinfo_t &info=*thing.info;
    const Relief *body=reliefOfState(info.painstate);
    if(!body)return false;
    Ragdoll doll;
    doll.body=body;doll.state=info.painstate;doll.start=leveltime;doll.tumbling=true;
    doll.rides=true;doll.gravity=0;doll.poseTics=3;doll.reach=phobosReach;
    uint32_t seed=hash32((uint32_t)(uintptr_t)&thing*2654435761u^(uint32_t)leveltime*40503u);
    auto rnd=[&] {seed=hash32(seed+0x9e3779b9u);return (float)(seed>>8)/16777216.0f;};
    float facing=(float)(thing.angle*(2*doomPi/4294967296.0)),fx=-std::cos(facing),fy=-std::sin(facing);
    doll.ax[0]=fy;doll.ax[1]=-fx;doll.ay[0]=fx;doll.ay[1]=fy;
    doll.ox=units(thing.x);doll.oy=units(thing.y);
    doll.mirror=rnd()<0.5f;
    doll.p=doll.prev=body->rest;
    // Still in its boots: its feet stay where they stand and the draft bends it (ragdollView).
    // Torn off them at once: over the way the draft takes it, a somersault and a little turn.
    doll.pinned=pinned;doll.ankleAt[0]=doll.p[P_ANK_L];doll.ankleAt[1]=doll.p[P_ANK_R];
    if(!pinned) {
        doll.launchSpin=(tumbleSpin[0]+(tumbleSpin[1]-tumbleSpin[0])*rnd())*(rnd()<0.5f?-1.0f:1.0f);
        doll.launchTwist=(rnd()-0.5f)*0.04f;doll.launchAt=0;
        doll.launchWay={0,1,0};
        doll.pitchInto(draft);
    }
    doll.armsAlong();
    doll.shown=doll.p;
    doll.walls=wallsNear(units(thing.x),units(thing.y),units(thing.floorz),doll.reach);
    doll.floors=floorsNear(units(thing.x),units(thing.y),doll.walls,doll.reach);
    doll.gx=doll.ox-doll.floors.x0;doll.gy=doll.oy-doll.floors.y0;doll.gz=units(thing.z);
    doll.nearWalls=wallsHere(doll);
    doll.oxWas=doll.ox;doll.oyWas=doll.oy;doll.gz0=doll.gzWas=doll.gz;doll.gatherX=doll.ox;doll.gatherY=doll.oy;
    for(int n=0;n<DOLL_POINTS;++n)doll.centre[0]+=doll.p[n].x/DOLL_POINTS,doll.centre[1]+=doll.p[n].y/DOLL_POINTS;
    std::copy(doll.centre,doll.centre+2,doll.centreWas);std::copy(doll.centre,doll.centre+2,doll.centreShown);
    forgetRagdoll(&thing);
    dolls[&thing]=std::move(doll);
    return true;
}
bool tumbleView(const mobj_t &thing,DeathView &view) {
    auto found=dolls.find(&thing);
    bool have=found!=dolls.end()&&found->second.tumbling;
    float way[3]={},pull=thing.health>0?Game_Draft(&thing,way):0;
    bool adrift=thing.health>0&&Game_Adrift(&thing),leaning=pull>=leanStart;
    if(!adrift&&!leaning) {
        // The draft gone with its boots still holding: it stands up again, a moment later.
        if(have&&(!found->second.pinned||++found->second.calm>leanLinger||thing.health<=0)) {forgetRagdoll(&thing);return false;}
        if(!have)return false;
    }
    float draft[3]={way[0]*pull,way[1]*pull,way[2]*pull};
    if(!have) {
        if(!startTumble(thing,!adrift,draft))return false;
        found=dolls.find(&thing);
    }
    Ragdoll &doll=found->second;
    if(adrift&&doll.pinned) {doll.pinned=false;doll.pitchInto(draft);}   // the boots gave: over it goes, head first
    if(adrift||leaning)doll.calm=0;
    std::copy(draft,draft+3,doll.draft);
    return ragdollView(thing,view);
}
static float fade(float t,float hold,float over) {return t>hold?std::clamp(1-(t-hold)/over,0.0f,1.0f):1.0f;}
// A doll's tics up to target (tics since the kill): ragdollView's fall, for the body and (Gore) its legs
// thrown off as a doll of their own.
static void tickDoll(Ragdoll &doll,const mobj_t &thing,int target) {
    if(doll.ball) {ballTic(doll,thing,target);return;}
    while(doll.tics<target&&!doll.resting) {
        float t=(float)doll.tics,muscle[MUSCLES],k=0;
        DollPose shape;
        if(doll.tics==doll.launchAt)doll.launch();
        if(doll.guided)doll.guideBefore(doll.tics,muscle,shape,k);
        else if(doll.swept) {
            // The knees gone, the trunk and hips held until the chest is low, the neck loose. With the hips
            // loose too, a body going over onto its back folded its legs up to its chest, landed on its
            // butt and the legs levered the trunk back up: it sat 15 tics before going over.
            muscle[M_LEGS]=0;muscle[M_NECK]=0;
            muscle[M_TRUNK]=doll.hipHold=doll.lowAt<0?1.0f:std::clamp(1-(t-doll.lowAt)/3,0.0f,1.0f);
        }
        else if(doll.tumbling) {
            for(float &m:muscle)m=doll.pinned?leanTone:tumbleTone;
            // The draft on the top of it, in body space, the feet holding.
            float dx=doll.draft[0],dy=doll.draft[1],dz=doll.draft[2],pull=std::hypot(std::hypot(dx,dy),dz);
            if(doll.pinned&&pull>0) {
                float side=doll.mirror?-1.0f:1.0f,a=std::min(pull*leanGain,leanMost)/pull;
                V3 way{side*(dx*doll.ax[0]+dy*doll.ax[1])*a,(dx*doll.ay[0]+dy*doll.ay[1])*a,dz*a};
                for(int n=0;n<DOLL_POINTS;++n)if(leanShare[n]>0)doll.push(n,way*leanShare[n]);
            }
        }
        else for(int g=0;g<MUSCLES;++g)muscle[g]=fade(t,doll.hold[g]+(g==M_LEGS?doll.late:0),doll.over[g]);
        // The dance (Ragdoll::dance): held up by steady fire, its legs and trunk tensed and gravity on its
        // upper body taken out, as hard as the last hit held it; fading between hits.
        if(doll.danceHold>0.02f) {
            if(doll.lyingAt>=0||doll.tics>=danceTics)doll.danceHold=0;
            else {
                muscle[M_LEGS]=std::max(muscle[M_LEGS],doll.danceHold);muscle[M_TRUNK]=std::max(muscle[M_TRUNK],doll.danceHold);
                k*=1-doll.danceHold;   // the death poses' strings slack while it's held
                V3 hips=doll.hips();
                // Gravity off the upper body, most of it off the hips and knees: off all of it the tensed legs pushed
                // it up off the floor 10 units; off the upper body alone its knees folded and it sank.
                for(int n:{P_HEAD,P_NECK,P_SH_L,P_SH_R,P_SPINE})doll.push(n,V3{0,0,doll.gravity*doll.danceHold});
                for(int n:{P_HIP_L,P_HIP_R,P_KNEE_L,P_KNEE_R})doll.push(n,V3{0,0,0.8f*doll.gravity*doll.danceHold});
                for(int n:{P_HEAD,P_NECK,P_SH_L,P_SH_R}) {
                    V3 back=hips-doll.p[n];back.z=0;
                    doll.push(n,back*(danceUpright*doll.danceHold));
                }
                doll.danceHold*=danceFade;
            }
        }
        // Burnt: the twitches (startRagdoll), a jerk of one part, not held; down already, it jerks where it lies.
        for(int i=0;i<2;++i)if(doll.tics==doll.twitchAt[i]) {
            static const int parts[]={P_HIP_L,P_HIP_R,P_SH_L,P_SH_R,P_KNEE_L,P_KNEE_R};
            uint32_t h=hash32(doll.twitchSeed+(uint32_t)i*0x9e3779b9u);
            float a=(h>>8)/16777216.0f*2*doomPi,strength=(i?0.5f:0.9f)*twitchKick;
            doll.pushInside({{parts[h%6],V3{std::cos(a),std::sin(a),0.6f}*strength}});
        }
        // Held until its hips reach the floor (let go by the clock, half slumped, the legs folded under
        // the pinned torso sprang it off the wall at 6 units a tic), slamTics at most.
        doll.slamAge=doll.slamAt>=0&&doll.tics-doll.slamAt<slamTics&&doll.up(P_SPINE)>10&&!doll.seated()?(float)(doll.tics-doll.slamAt):-1;
        if(doll.slamAge>=0) {             // slammed: the legs give at once, the trunk half held
            muscle[M_LEGS]=0;muscle[M_TRUNK]=std::min(muscle[M_TRUNK],0.5f);
            k=0;
        }
        // What the world gives it to do (ragdoll_moves.cpp).
        kneelTic(doll,muscle);iceTic(doll);rollTic(doll,muscle,shape,k);overTic(doll,muscle,shape,k);shockTic(doll);pedalTic(doll);
        if(doll.rides)followCorpse(doll,thing);
        // Down across a drop it isn't damped as lying: the weight over the edge pulls it off (overDrop).
        {float hang=doll.overDrop();doll.teeter=hang>=teeterFrom?hang:0;}
        doll.heavy=doll.lyingAt>=0&&doll.tics-doll.lyingAt>=lyingGrace&&doll.teeter==0&&!doll.rolling;
        doll.hitWall=0;doll.hitPoint=-1;doll.wallTouches=0;doll.lowHit=0;
        float was=0,spineWas=doll.p[P_SPINE].z;
        for(int n=0;n<DOLL_POINTS;++n)was+=doll.p[n].z;
        // Where its points are in the world before the tic, for the windows it goes through (below).
        float through[DOLL_POINTS][3];
        for(int n=0;n<DOLL_POINTS;++n) {
            doll.overGrid(doll.p[n],through[n][0],through[n][1]);
            through[n][0]+=doll.floors.x0;through[n][1]+=doll.floors.y0;through[n][2]=doll.gz+doll.p[n].z;
        }
        doll.tic(muscle,doll.nearWalls,doll.guided||k>0?&shape:nullptr,k);
        dollBumps(doll,thing);
        overStart(doll);
        if(doll.pinned)for(int s2=0;s2<2;++s2) {int n=s2?P_ANK_R:P_ANK_L;doll.p[n]=doll.prev[n]=doll.ankleAt[s2];}
        // (A doll riding its corpse on Phobos gathers them again as the corpse goes, followCorpse.)
        {float wx,wy;if(!doll.rides&&doll.regathers<regathersMost&&doll.outFromKnown(wx,wy)>=doll.reach-48)regather(doll,wx,wy);}
        doll.keepNear();
        // A body thrown into a window goes through it: the glass shatters where a point of it crossed, fast
        // enough (windows.cpp). The glass is drawing only, as the doll is.
        for(int n=0;n<DOLL_POINTS;++n) {
            float wx,wy;doll.overGrid(doll.p[n],wx,wy);
            if(glassBodyThrough(through[n][0],through[n][1],through[n][2],wx+doll.floors.x0,wy+doll.floors.y0,doll.gz+doll.p[n].z,3))break;
        }
        bool before=doll.landed;
        if(doll.tics>=2)doll.steer();
        if(doll.guided||doll.swept)doll.guideAfter(doll.tics);
        ++doll.tics;
        float z=0,cx=0,cy=0,energy=0;
        for(int n=0;n<DOLL_POINTS;++n) {
            z+=doll.p[n].z;cx+=doll.p[n].x;cy+=doll.p[n].y;
            V3 v=doll.p[n]-doll.prev[n];energy+=dot(v,v);
        }
        // The arms count toward lying still too (they don't toward its middle): baked mid-swing, an arm
        // froze in the air.
        for(int n=DOLL_POINTS;n<POSE_POINTS;++n) {V3 v=doll.p[n]-doll.prev[n];energy+=dot(v,v);}
        float rise=(z-was)/DOLL_POINTS;
        bool top=!doll.landed&&doll.rise>0.3f&&rise<=0;
        doll.rise=rise;
        std::copy(doll.centre,doll.centre+2,doll.centreWas);
        doll.centre[0]=cx/DOLL_POINTS;doll.centre[1]=cy/DOLL_POINTS;
        doll.still=doll.landed&&energy<(doll.rides?phobosQuiet:0.002f)?doll.still+1:0;
        bool landing=doll.landed&&!before,torso=false,slam=false;
        // A slam when the body hits a wall with its torso, hard, still up: not when it slumps into one.
        if(doll.slamAt<0&&doll.overAt<0&&!doll.landed&&!doll.blasted&&doll.hitWall*dollSubsteps>slamSpeed&&doll.up(P_SPINE)>14&&
           unit(doll.p[P_NECK]-doll.hips()).z>0.4f) {
            doll.slamAt=doll.tics;doll.slamWay=doll.hitWay;slam=true;
            // The knees buckle out from the wall (on straight legs a slammed body stood pinned upright).
            for(int n:{P_KNEE_L,P_KNEE_R})doll.push(n,doll.slamWay*slamBuckle+V3{0,0,-0.3f});
            doll.push(P_HIP_L,doll.slamWay*(0.3f*slamBuckle));doll.push(P_HIP_R,doll.slamWay*(0.3f*slamBuckle));
        }
        // In the world: a body point, and a way in body space.
        auto world=[&](V3 q,float &wx,float &wy){doll.overGrid(q,wx,wy);wx+=doll.floors.x0;wy+=doll.floors.y0;};
        float sideSign=doll.mirror?-1.0f:1.0f;
        auto worldWay=[&](V3 v,float &wx,float &wy){wx=doll.ax[0]*sideSign*v.x+doll.ay[0]*v.y;wy=doll.ax[1]*sideSign*v.x+doll.ay[1]*v.y;};
        float drop=std::max(0.0f,spineWas-doll.p[P_SPINE].z);
        float slid=std::hypot(doll.centre[0]-doll.centreWas[0],doll.centre[1]-doll.centreWas[1]);
        // The spine's way over the floor in the world, the faster of this tic's and the last (before
        // the floor took any of it): which way a splash leans.
        float glideX,glideY;
        {
            float sx,sy;world(doll.p[P_SPINE],sx,sy);
            float vx=doll.spineKnown?sx-doll.spineAt[0]:0,vy=doll.spineKnown?sy-doll.spineAt[1]:0;
            bool last=std::hypot(doll.glide[0],doll.glide[1])>std::hypot(vx,vy);
            glideX=last?doll.glide[0]:vx;glideY=last?doll.glide[1]:vy;
            doll.spineAt[0]=sx;doll.spineAt[1]=sy;doll.glide[0]=vx;doll.glide[1]=vy;doll.spineKnown=true;
        }
        // Slid off what it lay on (a pedestal, a ledge), touching nothing, it's falling again: no longer
        // down, so it isn't damped as lying and the deadlines wait for it to land again (counted from its
        // first landing on top, they froze bodies in the air halfway down a pedestal's face).
        if(doll.lyingAt>=0) {
            bool touching=false;
            for(int n=0;n<DOLL_POINTS&&!touching;++n)touching=!doll.rider(n)&&doll.up(n)<radius[n]+2;
            if(!touching)doll.lyingAt=-1;
        }
        if(doll.lyingAt<0&&doll.down()&&(doll.skipAt<0||doll.tics-doll.skipAt>=skipTics)) {
            // The torso coming down splashes the floor under it (the head's touch is not the landing),
            // as hard as it came down: its fall (this tic's or the last whole one) and some of its slide.
            // In a liquid it plunges in instead, by its weight too, leaning the way it went (liquids.cpp);
            // sprayLanding leaves liquids be.
            float wx,wy,fall=std::max(drop,doll.drop),hit=std::hypot(fall,0.5f*slid),glide=std::hypot(glideX,glideY);
            world(doll.p[P_SPINE],wx,wy);
            if(doll.skipAt<0&&glide>=skipSpeed&&fall<glide*skipSlope&&overLiquid(wx,wy)) {
                doll.skipAt=doll.tics;
                bodySplash(wx,wy,hit*0.7f,(float)thing.info->mass,units(thing.radius),glideX,glideY);
                float hop=std::clamp(0.3f*glide,1.5f,3.5f);
                for(int n=0;n<DOLL_POINTS;++n) {
                    V3 v=doll.p[n]-doll.prev[n];
                    v.x*=0.7f;v.y*=0.7f;v.z=std::max(v.z,0.0f)+hop/dollSubsteps;
                    doll.prev[n]=doll.p[n]-v;
                }
            } else {
                doll.lyingAt=doll.tics;torso=true;
                bodySplash(wx,wy,hit,(float)thing.info->mass,units(thing.radius),glideX,glideY);
                // Heard whatever it bleeds (a burnt body came down in silence), as heavy as it is.
                if(hit>1.5f)contactFloor(wx,wy,doll.gz+doll.floorAt(doll.p[P_SPINE]),hit,heftOf(thing));
                if(bleedsBlood(thing)&&!doll.burnt)sprayLanding(wx,wy,doll.gz+doll.floorAt(doll.p[P_SPINE]),hit,bloodTint(&thing),false);
            }
        }
        doll.drop=drop;
        // A torso slamming into a wall splashes it where it hit, once (the corpse's own slam watch in
        // spray.cpp followed the thing, not the body).
        if(!doll.splashed&&doll.hitPoint>=0&&doll.hitWall*dollSubsteps>slamSpeed&&bleedsBlood(thing)&&!doll.burnt) {
            doll.splashed=true;
            float wx,wy,dx,dy;world(doll.p[doll.hitPoint],wx,wy);worldWay(doll.hitWay*-1,dx,dy);
            sprayWallHit(wx,wy,doll.gz+doll.p[doll.hitPoint].z,dx,dy,doll.hitWall*dollSubsteps,bloodTint(&thing),heftOf(thing));
        }
        // Down and sliding: a smear every so far behind its middle, thick end forward.
        if(doll.lyingAt>=0&&slid>1.2f&&bleedsBlood(thing)&&!doll.burnt) {
            float size=std::clamp(units(thing.radius)*0.6f,9.0f,15.0f);
            if((doll.smearing+=slid)>=size*1.4f) {
                doll.smearing=0;
                float wx,wy,dx,dy;world(doll.p[P_SPINE],wx,wy);
                worldWay(V3{doll.centre[0]-doll.centreWas[0],doll.centre[1]-doll.centreWas[1],0}*(1/slid),dx,dy);
                spraySmear(wx-dx*size*0.6f,wy-dy*size*0.6f,std::atan2(dy,dx)+doomPi,size,bloodTint(&thing));
            }
        } else doll.smearing=0;
        // Still for 8 tics, or 40 after the torso came down if it no longer slides, 60 at the latest:
        // from then on a corpse sprite. (At 40 flat, 5 bodies in a review's 48 froze still sliding,
        // up to half a unit a tic.)
        int down=doll.lyingAt>=0?doll.tics-doll.lyingAt:-1;
        const char *stop=doll.tumbling?nullptr:doll.rides?(doll.still>=8?"quiet":down>=phobosDeadline&&slid<0.01f?"deadline":down>=phobosLate?"late":doll.tics>=phobosCap?"cap":nullptr)
            :doll.still>=8?"quiet":doll.teeter==0&&!doll.rolling&&down>=40&&slid<0.2f?"deadline":doll.teeter==0&&!doll.rolling&&down>=60?"late":doll.tics>=175?"cap":nullptr;
        if(stop) {
            if(M_CheckParm((char*)"-ragdolllog")) {
                fprintf(stderr,"ragdollrest %u tic %d down %d slide %.2f %s\n",(unsigned)((uintptr_t)&thing%100000),doll.tics,down,slid,stop);
                // How it lies (surveys): the deepest a point is into the floor under it and the highest one
                // floats (both past its radius), how many lie over floor a step (over 8) under the highest it
                // lies on, and how many touch a wall.
                float sunk=0,floats=0,top=-1e9f;int over=0,walled=0;
                for(int n=0;n<DOLL_POINTS;++n)if(!doll.rider(n))top=std::max(top,doll.floorAt(doll.p[n]));
                for(int n=0;n<DOLL_POINTS;++n) {
                    if(doll.rider(n)||(doll.headless&&n==P_HEAD))continue;   // (a lost head's point rides the neck)
                    float gap=doll.up(n)-radius[n];
                    sunk=std::min(sunk,gap);floats=std::max(floats,gap);
                    over+=doll.floorAt(doll.p[n])<top-8;walled+=doll.walled[n].x!=0||doll.walled[n].y!=0;
                }
                fprintf(stderr,"ragdolllies sunk %.1f floats %.1f overstep %d walled %d teeter %.2f\n",sunk,floats,over,walled,doll.teeter);
            }
            doll.resting=true;
            if((doll.restSector=doll.sectorUnder(doll.p[P_SPINE])))doll.restFloor=units(doll.restSector->floorheight);
            std::copy(doll.centre,doll.centre+2,doll.centreWas);
            doll.nearWalls.clear();doll.nearWalls.shrink_to_fit();doll.floors.cells.clear();doll.floors.cells.shrink_to_fit();
            doll.walls.clear();doll.walls.shrink_to_fit();
        }
        if(M_CheckParm((char*)"-ragdolllog")) {
            // Test runs (ragdoll_test.cpp): the turn per tic about the vertical (yaw), along the body
            // (roll) and all but the roll (tumble), radians; its middle's height over the floor it died on
            // and its speed over the floor.
            V3 v[DOLL_POINTS],c,vc,spin;
            for(int n=0;n<DOLL_POINTS;++n)v[n]=doll.p[n]-doll.prev[n];
            doll.rigid(v,c,vc,spin);spin=spin*(float)dollSubsteps;vc=vc*(float)dollSubsteps;
            V3 axis=unit(doll.p[P_NECK]-doll.hips());
            float yaw=spin.z,roll=dot(spin,axis),tumble=std::sqrt(std::max(0.0f,dot(spin,spin)-roll*roll));
            // hip: the angle (degrees) between the trunk and the legs, 180 standing straight
            V3 legs=middle(doll.p[P_ANK_L],doll.p[P_ANK_R])-doll.hips();
            float hip=std::acos(std::clamp(dot(axis,unit(legs)),-1.0f,1.0f))*180/doomPi;
            // twist: the shoulder line's turn about the trunk this tic, degrees (the rigid fit's roll
            // counts a leg folding against a wall as the body turning)
            V3 sh=doll.p[P_SH_R]-doll.p[P_SH_L];sh=sh-axis*dot(sh,axis);
            V3 sw=doll.shouldersWas-axis*dot(doll.shouldersWas,axis);
            float twist=doll.tics>1&&length(sw)>1e-3f&&length(sh)>1e-3f?std::atan2(dot(axis,cross(sw,sh)),dot(sw,sh))*180/doomPi:0;
            doll.shouldersWas=doll.p[P_SH_R]-doll.p[P_SH_L];
            // pelvis: the hip line's turn about the trunk this tic; torsion: the shoulder line's angle
            // to the hip line about the trunk (the trunk wrung), both degrees
            V3 hl=doll.p[P_HIP_R]-doll.p[P_HIP_L];hl=hl-axis*dot(hl,axis);
            V3 hw=doll.hipsWas-axis*dot(doll.hipsWas,axis);
            float pelvis=doll.tics>1&&length(hw)>1e-3f&&length(hl)>1e-3f?std::atan2(dot(axis,cross(hw,hl)),dot(hw,hl))*180/doomPi:0;
            float torsion=length(hl)>1e-3f&&length(sh)>1e-3f?std::atan2(dot(axis,cross(hl,sh)),dot(hl,sh))*180/doomPi:0;
            doll.hipsWas=doll.p[P_HIP_R]-doll.p[P_HIP_L];
            int iced=0;for(bool on:doll.iced)iced+=on;
            float atX,atY;doll.overGrid(c,atX,atY);
            fprintf(stderr,"ragdoll %u tic %d landed %d lying %d heavy %d slam %d wall %d mid %.1f speed %.2f yaw %.3f roll %.3f tumble %.3f up %.2f hips %.1f neck %.1f head %.1f hip %.0f twist %.1f pelvis %.1f torsion %.1f mirrors %d spin %.3f rolling %d over %d iced %d shock %.2f pedal %d at %.0f %.0f\n",
                (unsigned)((uintptr_t)&thing%100000),doll.tics,doll.landed,doll.lyingAt>=0?doll.tics-doll.lyingAt:-1,doll.heavy,doll.slamAt,doll.wallTouches,
                c.z,std::hypot(vc.x,vc.y),yaw,roll,tumble,axis.z,doll.up(P_HIP_L),doll.up(P_NECK),doll.up(P_HEAD),hip,twist,pelvis,torsion,doll.mirrors,doll.spin,
                (int)doll.rolling,(int)doll.overing,iced,doll.shock,(int)doll.pedalling,atX+doll.floors.x0,atY+doll.floors.y0);
            doll.mirrors=0;
        }
        // In the air at most the zombieman's pace: at the imp's 8 tics a throw was one held pose sliding
        // through the air, a statue, then lying.
        doll.showPose(top||landing||torso||slam);
    }
}
// Shot out of the air: how far its first tics' flash is still on, 1 to 0 (geometry.cpp reddens its card).
float ragdollFlash(const Ragdoll &doll,float fraction) {
    if(!doll.airKill)return 0;
    return std::clamp(1-((float)doll.tics-1+fraction)/3,0.0f,1.0f);
}
bool ragdollView(const mobj_t &thing,DeathView &view) {
    auto found=dolls.find(&thing);
    if(found==dolls.end())return false;
    Ragdoll &doll=found->second;
    // Crushed to gibs, raised, or otherwise moved on: the game's own frames again.
    if(!doll.tumbling&&(thing.health>0||std::find(doll.steps.begin(),doll.steps.end(),(int)(thing.state-states))==doll.steps.end()))return false;
    // The fall runs in game tics, in the world from where it died. A pose is drawn every earlyPose
    // tics through the hit (at the death frames' pace the recoil fell between the first two poses),
    // then as often as the monster's own death frames change (the zombieman's every 5 tics, the
    // imp's every 8), and always at the top of a throw, the tic it first touches down and the tic
    // its torso does: sampled evenly that slowly, the impact often fell between two poses and the
    // body jumped from mid-air to lying.
    int target=leveltime-doll.start;
    tickDoll(doll,thing,target);
    if(doll.legsDoll)tickDoll(*doll.legsDoll,thing,target);   // Gore: legs thrown off, a doll of their own
    if(doll.rides&&doll.resting)followCorpse(doll,thing);
    timed(Stat::Gore,[&]{goreTic(doll,thing,target);});
    view.sprite=states[doll.state].sprite;view.frame=states[doll.state].frame;view.mirror=doll.mirror;
    view.facing=thing.angle;view.bend=0;view.settle=0;view.fall=FallPose{};view.fall.active=true;
    view.ragdoll=&doll;
    return true;
}

#ifdef DOOM_DEVTOOLS
std::vector<DollBody> ragdollBodies() {
    std::vector<DollBody> out;
    for(const auto &entry:dolls)out.push_back({entry.first,entry.second.start});
    std::sort(out.begin(),out.end(),[](const DollBody &a,const DollBody &b){return a.start!=b.start?a.start<b.start:a.thing<b.thing;});
    return out;
}
bool ragdollPeek(const mobj_t *thing,float fraction,DollPeek &out) {
    auto found=dolls.find(thing);
    if(found==dolls.end()||!found->second.body)return false;
    const Ragdoll &doll=found->second;
    out.body=doll.body;out.posed=&posedCorners(doll);
    out.live=doll.p;out.shown=doll.shown;
    out.shift={doll.centreWas[0]+(doll.centre[0]-doll.centreWas[0])*fraction-doll.centreShown[0],
               doll.centreWas[1]+(doll.centre[1]-doll.centreWas[1])*fraction-doll.centreShown[1],0};
    if(smoothDolls()&&!doll.resting)out.shift={};   // as dollPlace
    out.mirror=doll.mirror;out.landed=doll.landed;out.resting=doll.resting;out.baked=doll.baked;out.smooth=smoothDolls();
    out.tics=doll.tics;out.shownAt=doll.shownAt;out.lastPose=doll.lastPose;
    out.every=doll.poseEvery();
    out.cardKey=doll.cardKey;out.azimuth=doll.cardAzimuth;out.angle=doll.cardAngle;out.tilt=doll.cardTilt;
    out.walls.clear();
    for(const Wall &w:doll.nearWalls)out.walls.push_back({w.ax,w.ay,w.bx,w.by,w.top});
    V3 mid=doll.middleOf();
    int half=DollPeek::floorSide/2;
    out.floorX=(std::floor(mid.x/out.floorStep)-half)*out.floorStep;out.floorY=(std::floor(mid.y/out.floorStep)-half)*out.floorStep;
    for(int j=0;j<DollPeek::floorSide;++j)for(int i=0;i<DollPeek::floorSide;++i)
        out.floor[j][i]=doll.floorAt(V3{out.floorX+i*out.floorStep,out.floorY+j*out.floorStep,0});
    return true;
}
#endif
}
