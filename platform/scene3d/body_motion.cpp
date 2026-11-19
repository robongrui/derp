/* Body motion: monsters jolt from the blow that hits or kills them. A
   drawing-only layer over the game's own frames; the game never sees it. */
#include "internal.h"
extern "C" {
void P_UnsetThingPosition(mobj_t *thing);
void P_SetThingPosition(mobj_t *thing);
}

namespace doom3d {
// Every tic each monster is watched for lost health. The blow's direction
// and strength are the push the game itself gives (P_DamageMobj thrusts the
// victim away, scaled by its mass; the chainsaw doesn't push), its height
// comes from where hitscan shots land (bodyHit) or from the missile or
// blast that did the damage (bodyDamaged, every damage event).
// Each blow starts an impulse that leans the body with the push, the upper
// body lagging behind, and buckles the legs; high hits throw the body back,
// low hits knock the legs out. Deaths jolt hardest and fade out before the
// corpse frame, so the final body stays the game's. Gibbing hops the body,
// fliers sway as they fall. Variation comes from hashes, never P_Random.
struct Impulse { int start;float dx,dy,lean,whip,squash,peak,hop;int duration;bool death,flier;float sway=0; };
// The game walks a monster in jumps (a step every few tics, no momentum), so its walking speed is read off
// where it went: the last walkTics tics' moves past what its momentum did, jumps of over walkJump left out
// (a teleport).
constexpr int walkTics=8;constexpr float walkJump=32;
struct Body { int health;float momx,momy;bool alive,seen;float x=0,y=0;float walkX[walkTics]={},walkY[walkTics]={};int walkAt=0;bool knelt=false; };
// This tic's hits on one body: hitscan points (exact) and the attack that
// did the damage (its inflictor: the shooter for hitscan, else the missile
// or blast), which groups bodies struck by one blast.
// And each hitscan hit on its own (gore.cpp reads where every pellet of the killing blow landed).
struct Hit { int tic;float z,dx,dy,side;int count;const mobj_t *attack;float missileZ,missileSide;bool missile;std::vector<GorePellet> pellets;bool direct=false,mouth=false; };
static std::unordered_map<const mobj_t*,Body> watched;
static std::unordered_map<const mobj_t*,std::vector<Impulse>> impulses;
static std::unordered_map<const mobj_t*,Hit> hits;
// A living body's earlier hits (bodyHit's pellets), kept for gore at its death.
static std::unordered_map<const mobj_t*,std::vector<GorePellet>> scars;
static unsigned bodyLevelSerial;
// A flinch's sideways share: a hit off the body's middle knocks it aside, away from the struck side, by the
// damage as much as the push (a baron's push is next to nothing, and head on a lean along the shot only
// shortens the drawing: bodyQuads). A dead-centre hit still goes one way or the other (never symmetric).
constexpr float swayLeast=0.04f,swayMost=0.2f,swayBase=0.03f,swayPerDamage=0.004f,swayPerPush=1/24.0f,swayCentre=0.3f,swayTop=0.4f;
// Vanilla friction on the ground, so the push of this tic is what the
// momentum gained beyond the last tic's slide.
constexpr float slide=(float)0xe800/FRACUNIT;

static bool monster(const mobj_t &thing) {return thing.info&&(thing.info->flags&MF_COUNTKILL);}
// Tics from a death state to its resting frame (or removal).
static int deathTics(int state) {
    int total=0;
    for(int n=0;n<64&&state>0;++n) {
        const state_t &s=states[state];
        if(s.tics<0)break;
        total+=s.tics;state=s.nextstate;
    }
    return std::max(total,4);
}
void bodyWalk(const mobj_t *thing,float &vx,float &vy) {
    vx=vy=0;
    auto body=watched.find(thing);
    if(body==watched.end())return;
    for(int i=0;i<walkTics;++i) {vx+=body->second.walkX[i];vy+=body->second.walkY[i];}
    vx/=walkTics;vy/=walkTics;
}
// Down on one knee (DERP's soldiers), the picture the shots strike is the kneeling one: its hip some 18% up
// (kneel_view.cpp's frames and the squash alike stand 73% of standing, the upper body whole). Heights struck
// on it, back on the standing drawing the dolls and gore know: under the hip stretched, over it raised.
static bool kneelingNow(const mobj_t &thing) {
    return Game_Kneeling(&thing)&&thing.health>0&&!(thing.flags&MF_CORPSE)&&thing.info&&thing.height<thing.info->height;
}
static float standingHeight(const mobj_t &thing,float z) {
    float tall=units(thing.info->height),hip=0.45f*tall,knelt=hip-0.27f*tall;
    return z<knelt?z*hip/knelt:z+(hip-knelt);
}
void bodyHit(mobj_t *target,fixed_t fx,fixed_t fy,fixed_t fz,fixed_t fdx,fixed_t fdy,int damage) {
    if(!target||!monster(*target))return;
    float dx=units(fdx),dy=units(fdy),length=std::hypot(dx,dy);
    if(length<1e-4f)return;
    dx/=length;dy/=length;
    Hit &hit=hits[target];
    if(hit.tic!=leveltime)hit={leveltime,0,0,0,0,0,nullptr,0,0,false};
    // A blast's pellets average into one hit. Side: how far off the body's
    // middle it struck, across the shot, in radii (positive: the shooter's left).
    float across=(units(fx)-units(target->x))*-dy+(units(fy)-units(target->y))*dx;
    hit.z+=units(fz)-units(target->z);hit.dx+=dx;hit.dy+=dy;hit.side+=across/std::max(1.0f,units(target->radius));++hit.count;
    // Where across the body it struck, for gore (PLAN-gore.md A): the shot's way across the body's x (as
    // ragdoll.cpp's axes), as the body faced when it was hit.
    float a=(float)(target->angle*(2*doomPi/4294967296.0));
    float struck=units(fz)-units(target->z);
    if(kneelingNow(*target))struck=standingHeight(*target,struck);   // (gore's pellets on the standing drawing)
    hit.pellets.push_back({struck,-across,damage,dy*-std::sin(a)-dx*std::cos(a)});
}
// Gore: a pellet of the killing volley that went on through the body once it was dead (Doom's dead aren't
// shootable: a super shotgun's 20 pellets killed a zombieman with two or three and the rest passed
// through). Struck at (x,y,z) going (dx,dy) while it died this tic (the watch hasn't seen it die yet),
// it counts with the kill's own pellets; false: not dying now.
bool bodyKnelt(const mobj_t *thing) {
    auto body=watched.find(thing);
    return body!=watched.end()&&body->second.knelt;
}
bool bodyDyingNow(const mobj_t *thing) {
    auto body=watched.find(thing);
    return body!=watched.end()&&body->second.alive&&thing->health<=0;
}
bool bodyKillingPellet(const mobj_t *thing,float x,float y,float z,float dx,float dy,int damage) {
    auto body=watched.find(thing);
    if(body==watched.end()||!body->second.alive||thing->health>0)return false;
    auto hit=hits.find(thing);
    if(hit==hits.end()||hit->second.tic!=leveltime)return false;
    float across=(x-units(thing->x))*-dy+(y-units(thing->y))*dx;
    float a=(float)(thing->angle*(2*doomPi/4294967296.0));
    hit->second.pellets.push_back({z-units(thing->z),-across,damage,dy*-std::sin(a)-dx*std::cos(a)});
    return true;
}
// A death: the game's xdeath (gib) frames rather than its plain death.
static bool gibbed(const mobj_t &thing) {
    int x=thing.info->xdeathstate;
    return x&&(thing.state==&states[x]||thing.state==&states[states[x].nextstate]);
}
// The push of this tic: its direction and speed (units per tic).
static void pushOf(const mobj_t &thing,const Body &before,float hitDx,float hitDy,float &dx,float &dy,float &speed) {
    float mx=units(thing.momx)-before.momx*slide,my=units(thing.momy)-before.momy*slide;
    speed=std::hypot(mx,my);dx=0;dy=0;
    if(speed>0.3f) {dx=mx/speed;dy=my/speed;}
    else if(hitDx!=0||hitDy!=0) {float l=std::hypot(hitDx,hitDy);dx=hitDx/l;dy=hitDy/l;}
    else if(thing.target) {
        float ax=units(thing.x-thing.target->x),ay=units(thing.y-thing.target->y),l=std::hypot(ax,ay);
        if(l>1e-3f){dx=ax/l;dy=ay/l;}
    }
    if(dx==0&&dy==0) {float a=hashUnit((int)(uintptr_t)&thing,leveltime,0x51u)*2*doomPi;dx=std::cos(a);dy=std::sin(a);}
}
static const mobj_t *blastSpot=nullptr;static int blastTic=-1;
void bodyExplosion(mobj_t *spot,mobj_t *source,int damage) {
    blastSpot=spot;blastTic=leveltime;
    explosionAt(spot,damage);
    goreExplosion(spot,source,damage);
}
void bodyDamaged(mobj_t *target,mobj_t *inflictor,mobj_t *source,int damage) {
    playerHit(target,inflictor,source,damage);
    if(!target||!monster(*target))return;
    Hit &hit=hits[target];
    if(hit.tic!=leveltime)hit={leveltime,0,0,0,0,0,nullptr,0,0,false};
    if(!hit.attack)hit.attack=inflictor?inflictor:source;
    // A missile or blast: its height on the body, and for a direct hit
    // (still flying) how far off the middle, across its flight.
    if(inflictor&&inflictor!=source&&!hit.missile) {
        hit.missile=true;hit.missileZ=units(inflictor->z)-units(target->z);
        fatHit(target,0,hit.missileZ,damage);   // (a blast's height is its own: the fat rings from there)
        float mx=units(inflictor->momx),my=units(inflictor->momy),l=std::hypot(mx,my);
        // Struck directly by a missile in flight. Its blast comes from it still flagged as one (P_ExplodeMissile
        // clears the flag after the blast), so an explosion's spot is told apart by bodyExplosion.
        hit.direct=(inflictor->flags&MF_MISSILE)&&!(inflictor==blastSpot&&leveltime==blastTic);
        // Straight into an open mouth (caco_pop.cpp): seen now, its state still the attack's.
        hit.mouth=hit.direct&&Game_ThingRole(inflictor->type)==GAME_THING_ROCKET&&Game_MouthOpen(target);
        if((inflictor->flags&MF_MISSILE)&&l>1e-3f)
            hit.missileSide=((units(inflictor->x)-units(target->x))*-my+(units(inflictor->y)-units(target->y))*mx)/l/std::max(1.0f,units(target->radius));
        // Gore: fire and plasma burn where they strike (a charred wound, smoke, no blood).
        if(hit.direct&&goreOn()&&Game_ThingRole(inflictor->type)==GAME_THING_BURN_SHOT) {
            woundBurn(target,inflictor,source,damage);
            float hx=units(inflictor->x),hy=units(inflictor->y),hz=units(inflictor->z)+units(inflictor->height)/2;
            sprayScorch(hx,hy,hz,l>1e-3f?mx/l:0,l>1e-3f?my/l:0);
        }
    }
}
static void addImpulse(const mobj_t &thing,const Body &before,float hitZ,bool hitKnown,float hitDx,float hitDy,bool death,bool burnt,float side) {
    float dx,dy,speed;pushOf(thing,before,hitDx,hitDy,dx,dy,speed);
    if(burnt)speed=std::min(speed,0.3f);   // fire and plasma: no blow, it crumples (startDeath)
    uint32_t seed=hash32((uint32_t)(uintptr_t)&thing*2654435761u^(uint32_t)leveltime*40503u);
    auto vary=[&](int k,float spread){return 1+spread*(hashUnit(k,0,seed)*2-1);};
    float height=std::max(16.0f,units(thing.info->height));
    // Hit height: 1 well above the hip (thrown back), -1 at the legs (swept).
    // Level shots land a little above the middle, so most hits count as high.
    float high=hitKnown?std::clamp((hitZ/height-0.45f)/0.3f,-1.0f,1.0f):0.2f;
    // High hits throw the body back; low ones lean it the other way.
    float back=high>=0?0.7f+0.3f*high:0.7f+1.2f*high;
    float strength=settings.bodyMotionStrength;
    Impulse impulse={leveltime,dx,dy,0,0,0,2.0f*vary(1,0.3f),0,14,death,false};
    int hurt=std::max(1,before.health-std::max(0,thing.health));
    float share=std::min(1.0f,hurt/(float)std::max(1,thing.info->spawnhealth));
    if(death) {
        bool gib=gibbed(thing);
        impulse.duration=deathTics(gib?thing.info->xdeathstate:thing.info->deathstate);
        // No push (the chainsaw, a heavy body, a burn): the body crumples where it stands.
        bool crumple=speed<0.6f;
        float lean=crumple?0:std::clamp(0.12f+speed/12,0.12f,0.65f);
        impulse.lean=lean*back*vary(2,0.2f)*strength;
        impulse.whip=lean*(high>=-0.2f?0.7f+0.4f*std::max(high,0.0f):-0.5f)*vary(3,0.3f)*strength;
        impulse.squash=std::min(0.6f,(crumple?0.42f:0.18f+(high<0?0.15f*-high:0))*vary(4,0.2f)*strength);
        if(gib)impulse.hop=std::clamp(speed*0.5f,3.0f,7.0f)*strength;
        else if(speed>6)impulse.hop=std::min(4.0f,(speed-6)*0.4f)*strength;
        if(thing.info->flags&MF_FLOAT) {
            // Fliers drop and sway about their middle instead.
            impulse.flier=true;impulse.lean=(0.25f+lean*0.5f)*vary(2,0.2f)*strength;impulse.whip=0;impulse.squash=0;impulse.hop=0;
        }
    } else {
        float lean=std::clamp(0.08f+speed/10+share*0.4f,0.08f,0.35f);
        impulse.lean=lean*back*strength;impulse.whip=lean*0.5f*std::max(high,-0.5f)*strength;
        impulse.squash=high<0?0.1f*-high*strength:0;
        // Knocked aside. Side: positive on the shooter's left; the body goes the other way.
        float across=std::clamp(side*1.2f,-1.0f,1.0f);
        if(std::abs(across)<swayCentre)across=hashUnit(5,0,seed)<0.5f?-swayCentre:swayCentre;
        float knock=std::clamp(swayBase+hurt*swayPerDamage+speed*swayPerPush,swayLeast,swayMost);
        if(!burnt)impulse.sway=-across*knock*strength;
    }
    auto &list=impulses[&thing];
    if(death)list.clear();
    if(list.size()>=4)list.erase(list.begin());
    list.push_back(impulse);
}
// Struck by a flying body (ragdoll_bumps.cpp): it flinches from the blow as from a shot, the lighter it is
// the more, and its pain frame is drawn for a moment (bodyStagger). Drawing only: no damage, no pain state,
// nothing of the game's random numbers; the game's monster goes on as it was and a demo plays the same.
// (way: the blow's, in the world; speed: units a tic; hitZ: over its feet.)
static std::unordered_map<const mobj_t*,int> staggers;   // the tic each was last struck
constexpr int shoveEvery=12,staggerTics=8;
void bodyShove(const mobj_t &thing,float wayX,float wayY,float speed,float hitZ) {
    if(!settings.hitFlinch||!monster(thing)||thing.health<=0)return;
    auto struck=staggers.find(&thing);
    if(struck!=staggers.end()&&leveltime>=struck->second&&leveltime-struck->second<shoveEvery)return;
    staggers[&thing]=leveltime;
    uint32_t seed=hash32((uint32_t)(uintptr_t)&thing*2654435761u^(uint32_t)leveltime*40503u);
    float height=std::max(16.0f,units(thing.info->height));
    float high=std::clamp((hitZ/height-0.45f)/0.3f,-1.0f,1.0f),back=high>=0?0.7f+0.3f*high:0.7f+1.2f*high;
    float give=std::clamp(std::sqrt(100.0f/std::max(50,thing.info->mass)),0.3f,1.0f)*settings.bodyMotionStrength;
    float lean=std::clamp(0.1f+speed/24,0.1f,0.35f)*give;
    Impulse impulse={leveltime,wayX,wayY,lean*back,lean*0.5f*std::max(high,-0.5f),high<0?0.1f*-high*give:0,
                     2.0f*(1+0.3f*(hashUnit(1,0,seed)*2-1)),0,14,false,false};
    impulse.sway=(hashUnit(5,0,seed)<0.5f?-1.0f:1.0f)*2*swayLeast*give;   // never dead straight
    auto &list=impulses[&thing];
    if(list.size()>=4)list.erase(list.begin());
    list.push_back(impulse);
}
// Its pain frame in place of the one the game shows, for staggerTics after a body struck it. Not over a
// firing frame (the flash would go out mid-shot) and not once it is dead.
bool bodyStagger(const mobj_t &thing,int &sprite,int &frame) {
    auto struck=staggers.find(&thing);
    if(struck==staggers.end())return false;
    int since=leveltime-struck->second;
    if(since<0||since>=shoveEvery) {staggers.erase(struck);return false;}
    if(since>=staggerTics||thing.health<=0||!thing.info||thing.info->painstate<=0||(frame&FF_FULLBRIGHT))return false;
    const state_t &pain=states[thing.info->painstate];
    sprite=(int)pain.sprite;frame=(int)pain.frame;
    return true;
}
constexpr int heftyLegsStand=18;
// A dead ball of flesh with gore. Death frames ending in nothing (the Pain Elemental's) would take its doll or burst
// with it: the game's thing stays on the last one, and never raised, as it never was. What comes out of it comes out
// as a swollen one pops, not halfway through: the frames wait. And out of the body: the corpse goes with its doll.
static void keepBall(mobj_t &thing) {
    if(thing.tics<=0)return;
    if(thing.state->nextstate==0) {thing.tics=INT_MAX;return;}
    int lead=Game_DeathSpawns(thing.type);
    if(!lead)return;
    if(balloonSwelling(thing,lead))thing.tics=thing.state->tics;
    float x,y,z;
    if(!ragdollPlace(thing,x,y,z))return;
    P_UnsetThingPosition(&thing);
    thing.x=(fixed_t)(x*FRACUNIT);thing.y=(fixed_t)(y*FRACUNIT);
    P_SetThingPosition(&thing);
}
void watchBodies() {
    bool spray=settings.blood&&settings.bloodSpray;
    if((!settings.deathMotion&&!settings.hitFlinch&&!settings.deathDirector&&!settings.wounds&&!spray)||levelSerial!=r_levelserial) {bodyMotionShutdown();return;}
    if(bodyLevelSerial!=r_levelserial) {bodyMotionShutdown();bodyLevelSerial=r_levelserial;}
    for(auto &entry:watched)entry.second.seen=false;
    static std::vector<mobj_t*> bodies;   // kept from tic to tic: no allocation each
    bodies.clear();
    for(int i=0;i<numsectors;++i) for(mobj_t *thing=sectors[i].thinglist;thing;thing=thing->snext)
        if(monster(*thing))bodies.push_back(thing);
    for(mobj_t *thing:bodies) {
        bool alive=thing->health>0&&!(thing->flags&MF_CORPSE);
        auto found=watched.find(thing);
        if(found==watched.end()) {Body &first=watched[thing]={thing->health,units(thing->momx),units(thing->momy),alive,true,units(thing->x),units(thing->y)};first.knelt=alive&&kneelingNow(*thing);continue;}
        Body &body=found->second;body.seen=true;
        // Its walking, but for a tic it took a blow: the push may have moved it already.
        if(alive&&thing->health>=body.health) {
            float sx=units(thing->x)-body.x-body.momx,sy=units(thing->y)-body.y-body.momy;
            if(std::hypot(sx,sy)>walkJump)sx=sy=0;
            body.walkX[body.walkAt]=sx;body.walkY[body.walkAt]=sy;body.walkAt=(body.walkAt+1)%walkTics;
        }
        bool died=body.alive&&!alive,hurt=alive&&thing->health<body.health;
        if(alive&&!body.alive) {impulses.erase(thing);forgetDeath(thing);forgetWounds(thing);scars.erase(thing);} // Raised again.
        if(died||(hurt&&(settings.hitFlinch||spray))) {
            float z=0,hdx=0,hdy=0,side=0;bool known=false;const mobj_t *attack=nullptr;
            auto hit=hits.find(thing);
            bool missile=false;
            if(hit!=hits.end()&&leveltime-hit->second.tic<=1) {
                const Hit &h=hit->second;attack=h.attack;
                if(h.count>0) {z=h.z/h.count;hdx=h.dx;hdy=h.dy;side=h.side/h.count;known=true;}
                else if(h.missile) {z=h.missileZ;side=h.missileSide;known=true;missile=true;}
            }
            // Blood thrown by the blow (hitscan hits already sprayed their own; a burn throws none, bodyDamaged
            // made its smoke).
            bool burnt=attack&&Game_ThingRole(attack->type)==GAME_THING_BURN_SHOT;
            if(spray&&(died||missile)&&!burnt) {
                float dx,dy,speed;pushOf(*thing,body,hdx,hdy,dx,dy,speed);
                if(died) {
                    // Shot out of the air (Authored's kick, p_swept.c): as much blood as a kill can throw.
                    float sw[2],up;bool air=Game_SweptDead(thing,1,&sw[0],&sw[1],&up)>0.05f&&thing->z>thing->floorz;
                    sprayKill(*thing,dx,dy,speed,known?z:-1,gibbed(*thing),std::max(0,-thing->health)+(air?80:0));
                    if(air)sprayAirKill(*thing);
                }
                else sprayHurt(*thing,dx,dy,z,body.health-thing->health);
            }
            if(died?settings.deathMotion:settings.hitFlinch)addImpulse(*thing,body,z,known,hdx,hdy,died,burnt,side);
            if(died&&settings.deathDirector) {
                float dx,dy,speed;pushOf(*thing,body,hdx,hdy,dx,dy,speed);
                // Hit height: 1 well above the hip, -1 at the legs (as addImpulse); on a kneeler's picture
                // taken back to the standing drawing (the blood above flies from where it was struck).
                float tall=std::max(16.0f,units(thing->info->height));
                if(known&&body.knelt)z=standingHeight(*thing,z);
                float high=known?std::clamp((z/tall-0.45f)/0.3f,-1.0f,1.0f):0.2f;
                // Where the killing blow struck, for a shot through the head: hitscan only.
                bool shot=hit!=hits.end()&&leveltime-hit->second.tic<=1&&hit->second.count>0;
                float hitU=-side*units(thing->radius);    // side: in radii, the shooter's left
                bool fresh=hit!=hits.end()&&leveltime-hit->second.tic<=1;
                // Gore: the killing blow's pellets and the scars of the hits before it (PLAN-gore.md A).
                std::vector<GorePellet> gore=shot?hit->second.pellets:std::vector<GorePellet>{};
                auto scarred=scars.find(thing);
                if(scarred!=scars.end()) {gore.insert(gore.end(),scarred->second.begin(),scarred->second.end());scars.erase(scarred);}
                startDeath(*thing,dx,dy,speed,side,high,gibbed(*thing),attack,shot?z:-1,hitU,gore.empty()?nullptr:&gore,
                           body.health-thing->health,fresh&&hit->second.direct,fresh&&hit->second.mouth);
            }
        }
        // Hurt and still alive: the tic's hitscan hits kept as scars for its death (PLAN-gore.md A), the
        // last 64 at most.
        if(hurt) {
            auto hit=hits.find(thing);
            if(hit!=hits.end()&&leveltime-hit->second.tic<=1&&!hit->second.pellets.empty()) {
                auto &kept=scars[thing];
                for(GorePellet pellet:hit->second.pellets) {pellet.earlier=true;kept.push_back(pellet);}
                if(kept.size()>64)kept.erase(kept.begin(),kept.end()-64);
            }
        }
        if(alive)body.knelt=kneelingNow(*thing);   // (kept past the death: the game stands the body up as it dies)
        body.health=thing->health;body.momx=units(thing->momx);body.momy=units(thing->momy);body.alive=alive;
        body.x=units(thing->x);body.y=units(thing->y);
        if(!alive&&goreOn()&&Game_BodyBall(thing->type))keepBall(*thing);
        // A big monster's legs, cut from under the top, stand a moment before its death frames take them down.
        if(!alive&&thing->tics>0&&goreOn()&&goreHefty(*thing)&&ragdollLegsStand(*thing,heftyLegsStand))thing->tics=thing->state->tics;
    }
    for(auto it=watched.begin();it!=watched.end();) {
        if(it->second.seen) {++it;continue;}
        forgetDeath(it->first);forgetWounds(it->first);scars.erase(it->first);it=watched.erase(it);
    }
    for(auto it=impulses.begin();it!=impulses.end();) {
        auto &list=it->second;
        list.erase(std::remove_if(list.begin(),list.end(),[](const Impulse &p){return leveltime-p.start>p.duration+2;}),list.end());
        it=list.empty()||!watched.count(it->first)?impulses.erase(it):std::next(it);
    }
    for(auto it=hits.begin();it!=hits.end();) it=leveltime-it->second.tic>2?hits.erase(it):std::next(it);
}
void bodyMotionShutdown() {watched.clear();impulses.clear();hits.clear();staggers.clear();deathsShutdown();woundsShutdown();}

// A rise to its peak at tic peak, then a slow fall.
static float pulse(float t,float peak) {return t<=0?0:t/peak*std::exp(1-t/peak);}
// A damped spring struck at rest: out to 1 at its peak, back past rest, settling
// (rate per tic, damping < 1).
static float kick(float t,float rate,float damping) {
    if(t<=0)return 0;
    float root=std::sqrt(1-damping*damping),ringing=rate*root,peak=std::atan2(root,damping)/ringing;
    auto at=[&](float s){return std::exp(-damping*rate*s)*std::sin(ringing*s);};
    return at(t)/at(peak);
}
bool bodyPose(const mobj_t &thing,float fraction,BodyPose &pose) {
    pose={};
    if(thing.tics==-1&&(thing.flags&MF_CORPSE))return false;
    // Down on one knee for a friend's shot (DERP's soldiers, p_tactics.h):
    // the game lowered the body itself. The zombieman and the shotgun guy
    // are drawn in kneeling frames (kneel_view.cpp); any other, or replaced
    // art, has its legs buckle instead, 0.6 squash below a hip 45% up. (The
    // player's shots strike the picture, some 73% of standing either way:
    // P_DerpShown counts on it.)
    float kneel=0;
    int kneelSoldier,kneelAs;
    if(Game_Kneeling(&thing)&&thing.health>0&&!(thing.flags&MF_CORPSE)&&thing.info&&thing.height<thing.info->height&&!kneelFrame(thing,kneelSoldier,kneelAs))
        kneel=std::min(0.6f,(1-(float)thing.height/thing.info->height)*1.6f);
    // Gathering for a jump, or taking the landing (DERP's imps).
    kneel=std::max(kneel,Game_Crouch(&thing));
    // Leaning out of cover to shoot (DERP's soldiers): the body tips towards
    // the open side, some 20 degrees at the lean.
    float leanx=0,leany=0;
    bool leaning=thing.health>0&&!(thing.flags&MF_CORPSE)&&Game_Lean(&thing,&leanx,&leany);
    leanx*=0.35f;leany*=0.35f;
    // Hanging from the ceiling, or turning over as it lets go (DERP's imps).
    float roll=thing.health>0?Game_Hang(&thing,fraction):0;
    pose.roll=roll;
    // Its feet swept away (Authored's sidesweep, p_swept.c): tipped over, and folded a little at the
    // waist after its feet, most on the way down.
    float sweptWay[2]={},swept=thing.health>0?Game_Swept(&thing,fraction,&sweptWay[0],&sweptWay[1]):0;
    float fold=swept>0?0.27f*std::sin(doomPi*std::min(swept,1.0f))+0.12f*swept:0;
    pose.sweep=swept;pose.sweepWay[0]=sweptWay[0];pose.sweepWay[1]=sweptWay[1];
    auto found=impulses.find(&thing);
    if(found==impulses.end()) {
        if(kneel<=0&&!leaning&&roll<=0&&swept<=0)return false;
        pose.squash=kneel;
        pose.lean[0]=leanx;pose.lean[1]=leany;
        pose.whip[0]=sweptWay[0]*fold;pose.whip[1]=sweptWay[1]*fold;
        return true;
    }
    bool any=false;
    for(const Impulse &p:found->second) {
        float t=(float)(leveltime-1-p.start)+fraction;
        if(t<0||t>p.duration)continue;
        float fade=1;
        if(p.death) {float a=p.duration*0.6f,s=std::clamp((t-a)/std::max(1.0f,p.duration-a),0.0f,1.0f);fade=1-s*s*(3-2*s);}
        else fade=std::clamp((p.duration-t)/6.0f,0.0f,1.0f);
        float lean,whip=0,squash=0,sway=0,swayTip=0;
        if(p.flier) {lean=p.lean*std::sin(t*2*doomPi/14)*std::exp(-t/24);pose.flier=true;}
        else if(p.death) {
            // A whiplash over the game's own fall: the body snaps back from
            // the blow and rebounds, the upper body a beat behind, the legs
            // giving way. The frames do the falling; holding a lean would
            // only tip the card over.
            lean=p.lean*kick(t,0.5f,0.35f);
            whip=p.whip*kick(t-1.5f,0.4f,0.3f);
            float s=std::clamp(t/(p.duration*0.4f),0.0f,1.0f);squash=p.squash*s*s*(3-2*s);
        } else {
            lean=p.lean*pulse(t,p.peak);
            whip=p.whip*(pulse(t,p.peak*1.8f)-0.3f*pulse(t,p.peak*4));
            squash=p.squash*pulse(t,p.peak*2.5f);
            sway=p.sway*pulse(t,p.peak);swayTip=p.sway*swayTop*(pulse(t,p.peak*1.8f)-0.3f*pulse(t,p.peak*4));
        }
        pose.lean[0]+=(p.dx*lean-p.dy*sway)*fade;pose.lean[1]+=(p.dy*lean+p.dx*sway)*fade;
        pose.whip[0]+=(p.dx*whip-p.dy*swayTip)*fade;pose.whip[1]+=(p.dy*whip+p.dx*swayTip)*fade;
        pose.squash=std::max(pose.squash,squash*fade);
        if(p.hop>0) {float up=p.hop*t-0.5f*t*t;if(up>0)pose.lift=std::max(pose.lift,up);}
        any=true;
    }
    pose.lean[0]+=leanx;pose.lean[1]+=leany;
    pose.whip[0]+=sweptWay[0]*fold;pose.whip[1]+=sweptWay[1]*fold;
    for(float &v:pose.lean)v=std::clamp(v,-0.9f,0.9f);
    for(float &v:pose.whip)v=std::clamp(v,-0.6f,0.6f);
    pose.squash=std::min(std::max(pose.squash,kneel),0.6f);
    return swept>0||(any||kneel>0||leaning)&&(std::abs(pose.lean[0])+std::abs(pose.lean[1])+std::abs(pose.whip[0])+std::abs(pose.whip[1])+pose.squash+pose.lift>1e-3f);
}

// The sprite in strips of whole texel rows, so the artwork's pixels stay
// square and unblurred. The lean across the view bends the body: each row
// shifts sideways by whole texels, the feet stay planted and the bend grows
// up the legs into the waist, the upper body whipping further. The lean
// toward or away from the eye only shortens the body by dropping rows, as a
// body bent that way looks from here; tipping the card itself would show it
// is flat, so it tips only a sliver. Buckling legs drop rows too.
void bodyQuads(std::vector<Vertex> &out,const BodyPose &pose,const Vertex &base,float x,float y,float feet,float rx,float ry,
               float left,float right,int imageTop,int rows,float u0,float u1,const std::vector<byte> *holes,int tint) {
    float fx=-ry,fy=rx;
    float side1=pose.lean[0]*rx+pose.lean[1]*ry,depth1=pose.lean[0]*fx+pose.lean[1]*fy;
    float side2=side1+pose.whip[0]*rx+pose.whip[1]*ry,depth2=depth1+pose.whip[0]*fx+pose.whip[1]*fy;
    // The upper body bends at most about 70 degrees from upright.
    side1=std::clamp(side1,-1.0f,1.0f);depth1=std::clamp(depth1,-1.0f,1.0f);
    side2=std::clamp(side2,-1.2f,1.2f);depth2=std::clamp(depth2,-1.2f,1.2f);
    constexpr float maxTilt=0.15f;
    float tilt1=std::clamp(depth1,-maxTilt,maxTilt),tilt2=std::clamp(depth2,-maxTilt,maxTilt);
    int pivot=pose.flier?imageTop/2:0;
    float hip=pose.flier?1e6f:std::max(4.0f,std::round(imageTop*0.45f));
    // Row scales below and above the hip: the share of the bend the sliver of tilt doesn't show.
    float lower=(1-pose.flatten)*(1-pose.squash)*std::cos(depth1)/std::cos(tilt1),upper=(1-pose.flatten)*std::cos(depth2)/std::cos(tilt2);
    float hipDest=std::floor(hip*lower);
    // Destination height (above the pivot) of a source row edge that far above it.
    auto dest=[&](int u)->float {
        if(u<=0)return pose.flier?-std::floor(-u*lower):(float)u;
        return u<=hip?std::floor(u*lower):hipDest+std::floor((u-hip)*upper);
    };
    // A destination height's place on the slightly tipped card: forward and up from the pivot.
    auto bend=[&](float d,float &ahead,float &up) {
        if(d<=hipDest) {ahead=d*std::sin(tilt1);up=d*std::cos(tilt1);}
        else {ahead=hipDest*std::sin(tilt1)+(d-hipDest)*std::sin(tilt2);up=hipDest*std::cos(tilt1)+(d-hipDest)*std::cos(tilt2);}
    };
    // Sideways offset at each whole destination height: the lean angle grows
    // from nothing at the feet to side1 at the hip, then on to side2 at the
    // top. Fliers turn whole about their middle.
    int topDest=(int)std::max(0.0f,dest(imageTop-pivot));
    std::vector<float> offset((size_t)topDest+2,0.0f);
    float topSpan=std::max(1.0f,topDest-hipDest);
    for(int d=0;d<=topDest;++d) {
        float mid=d+0.5f,angle=pose.flier?side1:mid<=hipDest?side1*mid/std::max(1.0f,hipDest):side1+(side2-side1)*(mid-hipDest)/topSpan;
        offset[(size_t)d+1]=offset[(size_t)d]+std::tan(angle);
    }
    auto shiftAt=[&](float d) {
        if(d<0)return pose.flier?std::round(d*std::tan(side1)):0.0f;
        float at=std::min(d,(float)topDest+1);int whole=(int)at;
        return std::round(offset[(size_t)whole]+(offset[std::min((size_t)whole+1,offset.size()-1)]-offset[(size_t)whole])*(at-whole));
    };
    // Turned over about its middle, head first through the picture's own plane and depth.
    float rollAngle=pose.roll*doomPi,rollCos=std::cos(rollAngle),rollSin=std::sin(rollAngle),middle=imageTop*0.5f;
    // Swept off its feet: the whole picture turned in its own plane about the hip, feet leading the way
    // they went as the view has it, and let down so its lowest edge stays on what it lies on.
    float sweepAngle=pose.sweep*1.45f*(pose.sweepWay[0]*rx+pose.sweepWay[1]*ry>=0?1.0f:-1.0f);
    float sweepCos=std::cos(sweepAngle),sweepSin=std::sin(sweepAngle),sweepHip=std::round(imageTop*0.45f);
    float sweepDrop=pose.sweep>0?std::max(0.0f,sweepHip-(sweepHip*std::abs(sweepCos)+0.3f*(right-left)*std::abs(sweepSin))):0;
    auto corner=[&](float d,float across,float shift,float u,float v) {
        float ahead,up;bend(d,ahead,up);
        if(pose.roll>0) {
            float h=pivot+up-middle,turned=ahead*rollCos+h*rollSin;
            up=middle-pivot+h*rollCos-ahead*rollSin;ahead=turned;
        }
        float side=across+shift,height=pivot+up;
        if(pose.sweep>0) {
            float h=height-sweepHip,a=side;
            side=a*sweepCos-h*sweepSin;height=sweepHip+a*sweepSin+h*sweepCos-sweepDrop;
        }
        Vertex vertex=base;
        vertex.x=x+rx*side+fx*ahead;vertex.y=y+ry*side+fy*ahead;vertex.z=feet+height;
        vertex.u=u;vertex.v=v;
        return vertex;
    };
    // Runs of source rows that land on consecutive whole rows with one shift.
    int runStart=-1;float runTop=0,runShift=0;bool runUpper=false;
    // Rows with wound holes (wounds.cpp) go out one by one, cut into runs.
    int width=(int)std::lround(right-left);bool flip=u0>u1;
    auto wounded=[&](int r){if(!holes)return false;for(int c=0;c<width;++c)if((*holes)[(size_t)r*width+c])return true;return false;};
    auto flush=[&](int end,float bottom) {
        if(runStart<0)return;
        bool any=false;
        for(int r=runStart;r<end&&!any;++r)any=wounded(r);
        if(!any)
            quad(out,corner(runTop,left,runShift,u0,(float)runStart),corner(runTop,right,runShift,u1,(float)runStart),
                 corner(bottom,right,runShift,u1,(float)end),corner(bottom,left,runShift,u0,(float)end));
        else for(int r=runStart;r<end;++r) {
            float rowTop=runTop-(r-runStart);
            maskedRow(out,*holes,width,r,flip,[&](int edge,bool low) {
                return corner(low?rowTop-1:rowTop,flip?right-edge:left+edge,runShift,(float)edge,(float)(r+(low?1:0)));
            },tint);
        }
        runStart=-1;
    };
    float lastBottom=0;
    for(int r=0;r<rows;++r) {
        int uTop=imageTop-r-pivot;
        float top=dest(uTop),bottom=dest(uTop-1);
        if(top<=bottom) {flush(r,lastBottom);continue;} // Dropped: buckled or bent toward or away.
        float shift=shiftAt((top+bottom)*0.5f);bool upperRow=bottom>=hipDest;
        if(runStart>=0&&(shift!=runShift||upperRow!=runUpper||top!=lastBottom))flush(r,lastBottom);
        if(runStart<0) {runStart=r;runTop=top;runShift=shift;runUpper=upperRow;}
        lastBottom=bottom;
    }
    flush(rows,lastBottom);
}
} // namespace doom3d
