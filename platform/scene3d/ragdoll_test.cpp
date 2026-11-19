/* Ragdoll test runs (tests/ragdoll_arena.py): -ragdolltest <weapon>[:<aim>]
   kills every monster on the map in turn, a tic apart from tic 3 (before
   any has woken and turned), with the game's own hitscans fired from the
   player: pistol (one 10-damage bullet), shotgun (three 10-damage pellets)
   or ssg (seven), aimed at the chest, head, legs, or the chest's left or
   right edge as the shooter sees it; a third field, :n, keeps only the n-th
   monster of the row (west to east, from 0) and removes the rest, and a fourth,
   :t, kills it at tic t instead of 3 (the fall's own random draw follows the tic). Each victim's health is set so the last
   pellet kills it, so the corpse takes that weapon's push. -ragdollview x,y,
   angle then moves the player (the eye; the shots are already fired) to
   watch from there; -ragdollview side, 220 units to the right of the shot's
   line at the one victim, looking across its flight (close: 100 units); front, 150 back along it. Pair with -rendercheck's series (scene.cpp).
   -ragdollloop runs it for show: once every body has come to rest (or 12 seconds on) the corpses
   are cleared, the row stands up again where it was placed and is shot again, until the game is
   quit; the player can't die and may walk about to watch. Aim mix gives each victim another aim
   each round (chest, legs, left, right, head). Weapon rocket kills each victim in turn, 20 tics
   apart, with a rocket's explosion beside it (the game's own blast, push and sound): with aim mix
   anywhere round it at any height, so bodies fly every way; otherwise chest and legs from the
   shooter's side (thrown away), left and right from that side (across the view), head from behind
   (toward the eye). Only the one in turn can be hurt, so each blast throws one body; it gibs as in
   the game, drawn as a body thrown with Flying enemies on. Weapon chaingun fires a burst of three bullets 4 tics apart (the gun's pace), the last
   one killing. Weapon realsg / realssg fires the game's shotgun / super shotgun as it does (7 or 20
   pellets, its spread, 5-15 each) at a victim left at full health. Weapon wound fires one 10-damage
   bullet (woundN: N, 4 tics apart, the victim holding still, into one spot) and leaves the victim alive, never to attack (gore's wounds on the living, seen walking); aim shoulderl / shoulderr at the
   shoulders. A test switch only: it changes the game. */
#include "internal.h"
#include "ragdoll.h"
extern "C" {
void P_LineAttack(mobj_t *t1,angle_t angle,fixed_t distance,fixed_t slope,int damage);
fixed_t P_AproxDistance(fixed_t dx,fixed_t dy);
boolean P_TeleportMove(mobj_t *thing,fixed_t x,fixed_t y);
void P_RemoveMobj(mobj_t *th);
mobj_t *P_SpawnMobj(fixed_t x,fixed_t y,fixed_t z,mobjtype_t type);
boolean P_SetMobjState(mobj_t *mobj,statenum_t state);
void P_ExplodeMissile(mobj_t *mo);
}

namespace doom3d {
// -ragdollloop: the tic the current round began (its kills from first tics on), and its number
static int roundAt=0,round=0,settledFor=0;
// The row stood up again where it was placed, its corpses and their bodies gone.
static void standUp(std::vector<mobj_t*> &victims) {
    std::vector<mobj_t*> drops;         // the clips and guns the last round dropped
    for(int i=0;i<numsectors;++i)for(mobj_t *mo=sectors[i].thinglist;mo;mo=mo->snext)if(mo->flags&MF_DROPPED)drops.push_back(mo);
    for(mobj_t *drop:drops)P_RemoveMobj(drop);
    for(mobj_t *&dead:victims) {
        mapthing_t at=dead->spawnpoint;mobjtype_t type=dead->type;
        forgetRagdoll(dead);P_RemoveMobj(dead);
        dead=P_SpawnMobj(at.x*FRACUNIT,at.y*FRACUNIT,INT_MIN,type);   // INT_MIN: on the floor (ONFLOORZ)
        dead->spawnpoint=at;dead->angle=(angle_t)(ANG45*(at.angle/45));
    }
}
// -gorechamber [seconds [still]]: a test chamber for playing (any map; the arena's imp row makes it the imp chamber):
// the player can't die and holds every weapon with full ammo; once every monster has been dead that long
// (three seconds unless given), the corpses, what they dropped and their pieces are cleared and all the
// level began with stand up again where they started, those burst to nothing too, to be shot again. Still:
// the living stay on their spots, doing nothing (the arena's walls are behind them: left to walk, those facing
// the player died too far out to reach them). A test switch only: it changes the game.
static void goreChamberTic() {
    static const int on=M_CheckParm((char*)"-gorechamber");
    static const int seconds=on&&on+1<myargc&&atoi(myargv[on+1])>0?atoi(myargv[on+1]):3;
    static const bool still=on&&on+2<myargc&&!strcmp(myargv[on+2],"still");
    mobj_t *player=players[consoleplayer].mo;
    if(!on||gamestate!=GS_LEVEL||!player)return;
    player_t &p=players[consoleplayer];
    p.cheats|=CF_GODMODE;
    for(int w=0;w<NUMWEAPONS;++w)p.weaponowned[w]=true;
    for(int a=0;a<NUMAMMO;++a)p.ammo[a]=p.maxammo[a];
    static int allDeadAt=-1,lastTic=-1;
    static std::vector<std::pair<mapthing_t,mobjtype_t>> row;   // who the level began with
    if(leveltime<lastTic) {row.clear();allDeadAt=-1;}
    lastTic=leveltime;
    std::vector<mobj_t*> monsters;
    bool alive=false;
    for(int i=0;i<numsectors;++i)for(mobj_t *mo=sectors[i].thinglist;mo;mo=mo->snext)
        if((mo->flags&MF_COUNTKILL)&&mo!=player) {monsters.push_back(mo);alive=alive||mo->health>0;}
    if(row.empty())for(const mobj_t *mo:monsters)row.push_back({mo->spawnpoint,mo->type});
    if(still)for(mobj_t *mo:monsters)if(mo->health>0) {
        if(mo->state!=&states[mo->info->spawnstate])P_SetMobjState(mo,(statenum_t)mo->info->spawnstate);
        mo->tics=-1;mo->target=nullptr;
    }
    if(row.empty()||alive) {allDeadAt=-1;return;}
    if(allDeadAt<0||allDeadAt>leveltime)allDeadAt=leveltime;
    if(leveltime-allDeadAt<TICRATE*seconds)return;
    for(mobj_t *dead:monsters) {forgetBodyBurst(dead);forgetWounds(dead);}
    standUp(monsters);
    for(const auto &one:row) {
        bool back=false;
        for(const mobj_t *mo:monsters)back=back||(mo->spawnpoint.x==one.first.x&&mo->spawnpoint.y==one.first.y);
        if(back)continue;
        mobj_t *mo=P_SpawnMobj(one.first.x*FRACUNIT,one.first.y*FRACUNIT,INT_MIN,one.second);
        mo->spawnpoint=one.first;mo->angle=(angle_t)(ANG45*(one.first.angle/45));
    }
    allDeadAt=-1;
}

// -walltest t: from tic t on, a bullet straight ahead every 8 tics, for looking at what walls do (wall_hits.cpp).
static void wallTestTic() {
    static const int arg=M_CheckParm((char*)"-walltest");
    mobj_t *player=players[consoleplayer].mo;
    if(!arg||arg+1>=myargc||!player||gamestate!=GS_LEVEL)return;
    int from=atoi(myargv[arg+1]);
    // -stainstest: one of the map's monsters stood in the line of fire and another right behind it, to take its blood.
    if(leveltime==from-1&&M_CheckParm((char*)"-stainstest")) {
        mobjtype_t type=(mobjtype_t)-1;
        for(int i=0;i<numsectors&&type<0;++i)for(mobj_t *m=sectors[i].thinglist;m;m=m->snext)if(m->flags&MF_COUNTKILL) {type=m->type;break;}
        if(type>=0)for(int n=0;n<2;++n) {
            float at=(float)(player->angle*(2*doomPi/4294967296.0)),d=120.0f+50*n;
            mobj_t *m=P_SpawnMobj(player->x+(fixed_t)(std::cos(at)*d*FRACUNIT),player->y+(fixed_t)(std::sin(at)*d*FRACUNIT),INT_MIN,type);
            m->health=100000;m->tics=-1;
        }
    }
    if(leveltime>=from&&(leveltime-from)%8==0) {
        P_LineAttack(player,player->angle,2048*FRACUNIT,0,10);
        if(M_CheckParm((char*)"-gunbloodtest"))gunBloodMelee(BLOOD_RED,true);
    }
}
// -ragdollkneel: every living monster down on one knee for good, as DERP's soldiers kneel (p_lanes.c's P_TacKneel:
// half its height, a kneeling tic far off), for the kneeling frames (kneel_view.cpp) and the falls from a kneel
// (ragdoll_kneel.cpp) without the AI. A test switch only: it changes the game.
static void kneelTestTic() {
    static const bool on=M_CheckParm((char*)"-ragdollkneel")!=0;
    if(!on||gamestate!=GS_LEVEL)return;
    for(int i=0;i<numsectors;++i)for(mobj_t *mo=sectors[i].thinglist;mo;mo=mo->snext)
        if((mo->flags&MF_COUNTKILL)&&mo->health>0&&!mo->player)Game_KneelForTest(mo);
}

void ragdollTestTic() {
    kneelTestTic();
    wallTestTic();
    goreChamberTic();
    int option=M_CheckParm((char*)"-ragdolltest");
    if(!option||option+1>=myargc||gamestate!=GS_LEVEL)return;
    mobj_t *shooter=players[consoleplayer].mo;
    if(!shooter)return;
    std::string spec=myargv[option+1];
    std::vector<std::string> fields;
    for(size_t at=0;;) {size_t end=spec.find(':',at);fields.push_back(spec.substr(at,end-at));if(end==std::string::npos)break;at=end+1;}
    std::string weapon=fields[0],aim=fields.size()>1?fields[1]:"chest";
    int only=fields.size()>2?atoi(fields[2].c_str()):-1,first=only>=0&&fields.size()>3?std::max(3,atoi(fields[3].c_str())):3;
    int pellets=weapon=="ssg"?7:weapon=="shotgun"?3:1,damage=10;
    bool blast=weapon=="rocket",burst=weapon=="chaingun";
    bool real=weapon=="realsg"||weapon=="realssg";   // the game's own shotguns, at full health (gore's survey)
    int wounds=weapon.rfind("wound",0)==0?std::max(1,atoi(weapon.c_str()+5)):0;   // wound, wound3: bullets into one spot
    bool burns=weapon.rfind("burn",0)==0;   // burn, burn3: the same with fire (a burn-shot missile, gore's charred wounds)
    bool plasma=weapon=="plasma";           // plasma: one burn shot that kills (the crumple, the twitch, the smoke)
    if(burns)wounds=std::max(1,atoi(weapon.c_str()+4));
    bool rocketIn=weapon=="rockethit"||weapon=="rocketmouth";   // a real rocket that strikes and kills it; rocketmouth: its mouth open
    if(plasma||rocketIn) {burns=true;wounds=1;}
    static int burnShot=-2;
    if(burns&&burnShot==-2) {burnShot=-1;for(int t=0;t<NUMMOBJTYPES&&burnShot<0;++t)if(Game_ThingRole(t)==GAME_THING_BURN_SHOT)burnShot=t;}
    int spacing=blast?20:burst?12:wounds?4*wounds+4:1;    // tics between kills
    static int rocket=-2;               // the game's rocket, whose explosion the blasts are
    if(rocket==-2) {rocket=-1;for(int t=0;t<NUMMOBJTYPES&&rocket<0;++t)if(Game_ThingRole(t)==GAME_THING_ROCKET)rocket=t;}
    if(blast&&rocket<0)return;
    std::vector<mobj_t*> victims;
    for(int i=0;i<numsectors;++i)for(mobj_t *mo=sectors[i].thinglist;mo;mo=mo->snext)
        if((mo->flags&MF_COUNTKILL)&&mo!=shooter)victims.push_back(mo);
    std::sort(victims.begin(),victims.end(),[](const mobj_t *a,const mobj_t *b){return a->spawnpoint.x<b->spawnpoint.x;});   // in the row, west to east
    if(only>=0&&leveltime==2) {      // from then on the one victim is all the map holds
        for(int i=0;i<(int)victims.size();++i)if(i!=only)P_RemoveMobj(victims[i]);
        return;
    }
    if(M_CheckParm((char*)"-ragdollloop")) {
        players[consoleplayer].cheats|=CF_GODMODE;
        bool done=leveltime>roundAt+first+spacing*(int)victims.size();
        for(mobj_t *v:victims)done=done&&v->health<=0&&ragdollSettled(v);
        settledFor=done?settledFor+1:0;
        if(settledFor>=TICRATE*3/2||leveltime-roundAt>=TICRATE*12) {   // a moment to look at them lying, then again
            standUp(victims);roundAt=leveltime;++round;settledFor=0;
        }
    }
    // -ragdolltestblast t: at tic t a rocket goes off on the floor in the middle of the row, the bodies
    // lying round it (gore: a barrel among corpses, body_burst.cpp).
    if(int arg=M_CheckParm((char*)"-ragdolltestblast");arg&&arg+1<myargc&&leveltime==atoi(myargv[arg+1])&&rocket>=0&&!victims.empty()) {
        fixed_t mx=0,my=0;
        for(const mobj_t *v:victims) {mx+=v->spawnpoint.x/(int)victims.size();my+=v->spawnpoint.y/(int)victims.size();}
        mobj_t *boom=P_SpawnMobj(mx*FRACUNIT,my*FRACUNIT,INT_MIN,(mobjtype_t)rocket);
        boom->target=shooter;
        P_ExplodeMissile(boom);
    }
    // -ragdolltestshoot t[:part[:n]][,...]: at tic t the player fires seven pellets (or n) at each body: low
    // where it lies, or at a part of its ragdoll (head, arml, armr, torso) (gore: shots into corpses,
    // body_burst.cpp; n 1 every 4 tics is the chaingun's fire, the dance).
    if(int arg=M_CheckParm((char*)"-ragdolltestshoot");arg&&arg+1<myargc) {
        std::string list=myargv[arg+1];
        for(size_t from=0;from<list.size();) {
            size_t end=list.find(',',from);if(end==std::string::npos)end=list.size();
            std::string item=list.substr(from,end-from);from=end+1;
            size_t colon=item.find(':');
            if(atoi(item.c_str())!=leveltime)continue;
            std::string part=colon==std::string::npos?"":item.substr(colon+1);
            int count=7;
            size_t second=part.find(':');
            if(second!=std::string::npos) {count=std::max(1,atoi(part.c_str()+second+1));part=part.substr(0,second);}
            int point=part=="head"?P_HEAD:part=="arml"?P_HAND_L:part=="armr"?P_HAND_R:part=="torso"?P_SPINE:-1;
            for(mobj_t *v:victims) {
                if(v->health>0)continue;
                float x=units(v->x),y=units(v->y),z=units(v->z)+6;
                if(point<0||!ragdollPoint(*v,point,x,y,z)) {ragdollPlace(*v,x,y,z);z+=6;}
                // From the eye, as the player shoots with mouse look (tested once with its stop at the floor too: the
                // same parts struck).
                float ex=units(shooter->x),ey=units(shooter->y),ez=units(shooter->player->viewz);
                float d=std::max(1.0f,std::hypot(x-ex,y-ey));
                angle_t angle=(angle_t)(int64_t)(std::atan2(y-ey,x-ex)*(4294967296.0/(2*doomPi)));
                for(int n=0;n<count;++n) P_LineAttack(shooter,angle,32*64*FRACUNIT,(fixed_t)((z-ez)/d*FRACUNIT),5);
            }
        }
    }
    static const char *aims[]={"chest","legs","left","right","head"};
    int at=leveltime-roundAt-first,which=at>=0&&at%spacing==0?at/spacing:-1;
    // A tic before each kill the victim is already down to the health the shot will leave it on, so the blow
    // the game measures is the shot's own (the death code takes the killing tic's damage from the health a
    // tic before: set in the same tic, a 400-health cacodemon took one 409-damage hit and burst).
    if(!wounds&&!real&&!burst&&at+1>=0&&(at+1)%spacing==0) {
        int next=(at+1)/spacing;
        if(next<(int)victims.size()&&victims[next]->health>0)
            victims[next]->health=std::min(victims[next]->health,blast?1:damage*(pellets-1)+1);
    }
    int bullet=0;                       // a burst's bullets: 0, 1, then the killing one
    if(burst&&at>=0&&at%spacing%4==0) {which=at/spacing;bullet=at%spacing/4;}
    if(wounds&&at>=0&&at%spacing%4==0&&at%spacing/4<wounds) {which=at/spacing;bullet=at%spacing/4;}
    bool anywhere=blast&&aim=="mix";   // the blast anywhere round the victim
    if(aim=="mix"&&which>=0)aim=aims[(which+round)%5];
    if(blast) {
        // Only the victim in turn shootable: a blast's reach takes in the neighbours.
        int turn=std::clamp(at/spacing,0,(int)victims.size()-1);
        for(int i=0;i<(int)victims.size();++i) {
            mobj_t *v=victims[i];
            if(v->health>0)v->flags=i==turn?v->flags|MF_SHOOTABLE:v->flags&~MF_SHOOTABLE;
        }
    }
    if(blast&&which>=0&&which<(int)victims.size()&&victims[which]->health>0) {
        mobj_t *target=victims[which];
        float dx=units(shooter->x-target->x),dy=units(shooter->y-target->y),l=std::max(1.0f,std::hypot(dx,dy));
        dx/=l;dy/=l;                    // toward the shooter
        // Where round the victim it goes off, turned from the shooter's side (positive: the shooter's right,
        // throwing the body to its left): by the aim, or anywhere.
        float turn=aim=="left"?-80.0f:aim=="right"?80.0f:aim=="head"?160.0f:0.0f;
        float height=units(target->height),z=aim=="legs"?6:aim=="head"?height-12:24;
        if(anywhere) {
            uint32_t draw=hash32((uint32_t)leveltime*2654435761u^0x5b1au);
            turn=(float)(draw%360);z=6+(float)(draw>>16)/65536.0f*(height-18);
        }
        float a=turn*(float)doomPi/180,c=std::cos(a),s=std::sin(a),bx=dx*c-dy*s,by=dx*s+dy*c;
        float out=units(target->radius)+8;
        target->health=1;
        mobj_t *boom=P_SpawnMobj(target->x+(fixed_t)(bx*out*FRACUNIT),target->y+(fixed_t)(by*out*FRACUNIT),
                                 target->z+(fixed_t)(z*FRACUNIT),(mobjtype_t)rocket);
        boom->target=shooter;
        P_ExplodeMissile(boom);
    }
    else if(which>=0&&which<(int)victims.size()&&victims[which]->health>0) {
        mobj_t *target=victims[which];
        // (Down on one knee, at the picture the player sees: 73% of standing, p_lanes.c's KNEEL_SHOWN.)
        float height=units(Game_Kneeling(target)?target->info->height*73/100:target->height),across=0,z=0.55f*height;
        if(aim=="head")z=height-5;
        else if(aim=="legs")z=0.2f*height;
        else if(aim=="belly")z=0.5f*height;
        else if(aim=="ribs")z=0.66f*height;
        else if(aim=="thighl"||aim=="thighr") {z=0.3f*height;across=(aim=="thighl"?0.3f:-0.3f)*units(target->radius);}
        else if(aim=="bellyl"||aim=="bellyr") {z=0.5f*height;across=(aim=="bellyl"?0.2f:-0.2f)*units(target->radius);}
        else if(aim=="shoulderl"||aim=="shoulderr") {z=0.72f*height;across=(aim=="shoulderl"?0.35f:-0.35f)*units(target->radius);}
        else if(aim=="left")across=0.6f*units(target->radius);
        else if(aim=="right")across=-0.6f*units(target->radius);
        fixed_t distance=P_AproxDistance(target->x-shooter->x,target->y-shooter->y);
        fixed_t shootZ=shooter->z+(shooter->height>>1)+8*FRACUNIT;
        fixed_t slope=FixedDiv(target->z+(fixed_t)(z*FRACUNIT)-shootZ,std::max(distance,FRACUNIT));
        angle_t angle=R_PointToAngle2(shooter->x,shooter->y,target->x,target->y)+
            (angle_t)(int64_t)(std::atan2(across,units(distance))/(2*doomPi)*4294967296.0);
        if(wounds) {   // a bullet, left alive: wounds on the living, seen walking (it never attacks)
            mobjinfo_t *info=const_cast<mobjinfo_t*>(target->info);
            if(weapon=="rocketmouth")P_SetMobjState(target,(statenum_t)info->missilestate);
            if(!rocketIn)info->missilestate=info->meleestate=0;
            target->health=plasma||rocketIn?1:std::max(target->health,1000);
            // More than one: it holds still and doesn't flinch, so they all strike one spot of one drawing.
            if(wounds>1)info->painchance=0;
            if(burns&&burnShot>=0) {
                // From just in front of the victim, so it strikes at once wherever it's aimed.
                float tx=units(target->x)-units(shooter->x),ty=units(target->y)-units(shooter->y),tl=std::max(1.0f,std::hypot(tx,ty));
                float back=units(target->radius)+24;
                mobj_t *shot=P_SpawnMobj(target->x-(fixed_t)(tx/tl*back*FRACUNIT),target->y-(fixed_t)(ty/tl*back*FRACUNIT),
                                         target->z+(fixed_t)(z*FRACUNIT),(mobjtype_t)(rocketIn?rocket:burnShot));
                shot->target=shooter;
                shot->momx=(fixed_t)(tx/tl*shot->info->speed);shot->momy=(fixed_t)(ty/tl*shot->info->speed);shot->momz=0;
                shot->angle=R_PointToAngle2(shooter->x,shooter->y,target->x,target->y);
            }
            else P_LineAttack(shooter,angle,32*64*FRACUNIT,slope,10);
            if(wounds>1)target->tics=-1;   // after the hit, which wakes it into its walk
        }
        else if(real) {
            // As the game fires: full health, the weapon's pellets, its spread and its 5-15 damage each (the
            // shot's own draw from the kill's place, not the game's random numbers).
            uint32_t draw=hash32((uint32_t)leveltime*2654435761u^(uint32_t)which*40503u^0x6d2b79f5u);
            auto rnd=[&]{draw=hash32(draw+0x9e3779b9u);return (int)(draw>>24);};
            int count=weapon=="realssg"?20:7;
            for(int n=0;n<count;++n) {
                angle_t a=angle+(angle_t)((rnd()-rnd())<<(weapon=="realssg"?19:18));
                fixed_t sl=weapon=="realssg"?slope+((rnd()-rnd())<<5):slope;
                P_LineAttack(shooter,a,32*64*FRACUNIT,sl,5*(rnd()%3+1));
            }
        } else if(weapon=="rip") {
            // A super shotgun's blast point blank into one spot, the victim left alive and still to look at the
            // tear: seven 10-damage pellets in one tic, spread 4 units either way across and up.
            mobjinfo_t *info=const_cast<mobjinfo_t*>(target->info);info->missilestate=info->meleestate=0;info->painchance=0;
            target->health=std::max(target->health,1000);
            float far=std::max(1.0f,units(distance));
            for(int n=0;n<7;++n) {
                float off=(hashUnit(n,0,0x51eu)*2-1)*4,up=(hashUnit(n,1,0x51eu)*2-1)*4;
                angle_t a=angle+(angle_t)(int64_t)(std::atan2(off,far)/(2*doomPi)*4294967296.0);
                P_LineAttack(shooter,a,32*64*FRACUNIT,slope+(fixed_t)(up/far*FRACUNIT),10);
            }
            target->tics=-1;
        } else {
            target->health=burst?(bullet==0?damage*2+1:std::min(target->health,damage*(2-bullet)+1)):damage*(pellets-1)+1;
            for(int n=0;n<pellets&&target->health>0;++n)P_LineAttack(shooter,angle,32*64*FRACUNIT,slope,damage);
        }
    }
    int view=M_CheckParm((char*)"-ragdollview");
    if(view&&view+1<myargc&&leveltime==first+(int)victims.size()+1) {
        float x=0,y=0,facing=90;
        bool close=!strcmp(myargv[view+1],"close")&&victims.size()==1;
        bool side=(close||!strcmp(myargv[view+1],"side"))&&victims.size()==1,front=!strcmp(myargv[view+1],"front")&&victims.size()==1;
        if(side||front) {
            float vx=victims[0]->spawnpoint.x,vy=victims[0]->spawnpoint.y,dx=vx-units(shooter->x),dy=vy-units(shooter->y),l=std::max(1.0f,std::hypot(dx,dy));
            dx/=l;dy/=l;                             // the shot's way; its right (dy, -dx)
            if(side) {float out=close?100.0f:220.0f;x=vx+dy*out+dx*48;y=vy-dx*out+dy*48;facing=(float)(std::atan2(dx,-dy)*180/doomPi);}
            else {x=vx-dx*150;y=vy-dy*150;facing=(float)(std::atan2(dy,dx)*180/doomPi);}   // behind the shot, along it
            side=true;
        }
        if(side||sscanf(myargv[view+1],"%f,%f,%f",&x,&y,&facing)>=2) {
            P_TeleportMove(shooter,(fixed_t)(x*FRACUNIT),(fixed_t)(y*FRACUNIT));
            shooter->angle=(angle_t)(int64_t)(facing/360.0*4294967296.0);
            players[consoleplayer].viewz=shooter->z+players[consoleplayer].viewheight;
        }
    }
}
}
