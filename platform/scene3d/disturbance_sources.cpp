/* Where disturbances come from (disturbances.cpp keeps them and does what
   they do): each found as it happens, near the eye, and turned into a
   record as wide, hard and hot as it was. Render-only.
   - Blasts: a barrel's blast frame (Game_BlastFrame), a projectile that
     stops being one (as lights.cpp and shot_lights.cpp see it burst):
     rockets and BFG balls big, fireballs and plasma small and hot.
   - Projectiles in flight: a small wake every few tics along the way, which
     closes behind them in the fog (no ground).
   - Bodies: a corpse coming to rest on the floor, as wide as it is.
   - Steps: the player and the monsters walking, a print every 28 units on
     the ground outside (sand keeps them, snow fills them in as it falls);
     indoors the film is too coarse for a print, so there only the flakes
     part and the fog round the legs.
   - Doors and lifts: a sector whose floor or ceiling starts or stops
     moving, a puff along the floor out of each of its open lines, pushing
     what lies there out (a slam hits hardest).
   - Shots: a bullet hitting the floor (disturbanceShot, from liquids.cpp's
     planeImpact), a small kick and a tiny dent; a gun's blast
     (disturbanceMuzzle, from lights.cpp's muzzleFlash), a small shove and
     swirl in front of it, nothing on the ground.
   - Hot things: torches and other fire-coloured decorations, and what
     flies glowing, looked at every eight tics (disturbanceHeat). */
#include "internal.h"
#include "../rooms.h"

namespace doom3d {
namespace {
struct Flight { float x,y,z; int type; bool seen; int wake; };
std::unordered_map<const mobj_t*,Flight> flights;
std::unordered_set<const mobj_t*> blown,blasting;   // barrels that blew (still in their blast); in it this tic
std::unordered_set<const mobj_t*> rested;          // corpses that came to rest
struct Print { float x,y; bool seen; };
std::unordered_map<const mobj_t*,Print> prints;     // where each walker last left a print
std::vector<std::array<float,2>> heights;           // each sector's floor and ceiling last tic
std::vector<float> moving;                          // and how fast it moved then (units a tic)
std::vector<HotSpot> spots;
unsigned level=~0u;
constexpr float near=1536;   // only what happens this near the eye

bool close(float x,float y,const mobj_t *eye) {return eye&&std::hypot(x-units(eye->x),y-units(eye->y))<near;}
// A sector that started or stopped moving: out of each of its open lines
// (the longest four), on both sides, a puff along the floor.
void doorPuff(int s,float speed,bool stopped) {
    const sector_t &sector=sectors[s];
    std::vector<std::pair<float,const line_t*>> open;
    for(int n=0;n<sector.linecount;++n) {
        const line_t *line=sector.lines[n];
        if(!line->backsector||line->frontsector==line->backsector)continue;
        float len=std::hypot(units(line->v2->x)-units(line->v1->x),units(line->v2->y)-units(line->v1->y));
        if(len>=16)open.push_back({len,line});
    }
    std::sort(open.begin(),open.end(),[](const auto &a,const auto &b){return a.first>b.first;});
    float strength=std::clamp(speed/8,0.15f,0.8f)*(stopped?1.3f:1.0f);
    for(size_t i=0;i<open.size()&&i<4;++i) {
        const line_t *line=open[i].second;float len=open[i].first;
        float ax=units(line->v1->x),ay=units(line->v1->y),bx=units(line->v2->x),by=units(line->v2->y);
        float mx=(ax+bx)/2,my=(ay+by)/2,nx=(by-ay)/len,ny=-(bx-ax)/len;
        for(float side:{-1.0f,1.0f}) {
            float x=mx+nx*side*20,y=my+ny*side*20;
            const sector_t *at=weatherSectorAt(x,y);
            if(at)disturb(x,y,floorZ(at)+8,std::min(64.0f,len*0.5f+16),strength,0,false);
        }
    }
}
}

void watchDisturbanceSources() {
    if(gamestate!=GS_LEVEL||!settings.weather)return;
    if(level!=r_levelserial) {disturbanceSourcesShutdown();level=r_levelserial;}
    if(falling()==WEATHER_NONE) {flights.clear();blown.clear();rested.clear();prints.clear();heights.clear();return;}
    const mobj_t *eye=players[displayplayer].mo;
    // Doors and lifts.
    if(heights.size()!=(size_t)numsectors) {
        heights.resize(numsectors);moving.assign(numsectors,0);
        for(int s=0;s<numsectors;++s)heights[s]={units(sectors[s].floorheight),units(sectors[s].ceilingheight)};
    }
    for(int s=0;s<numsectors;++s) {
        float floor=units(sectors[s].floorheight),ceiling=units(sectors[s].ceilingheight);
        float speed=std::max(std::fabs(floor-heights[s][0]),std::fabs(ceiling-heights[s][1]));
        heights[s]={floor,ceiling};
        bool started=speed>0.5f&&moving[s]<=0.5f,stopped=speed<=0.5f&&moving[s]>0.5f;
        float was=moving[s];moving[s]=speed;
        if((started||stopped)&&sectors[s].soundorg.x!=0&&close(units(sectors[s].soundorg.x),units(sectors[s].soundorg.y),eye))
            doorPuff(s,std::max(speed,was),stopped);
    }
    // Things: blasts, wakes, bodies, steps.
    for(auto &entry:flights)entry.second.seen=false;
    for(auto &entry:prints)entry.second.seen=false;
    blasting.clear();
    bool looking=leveltime%8==0;   // hot things every eight tics
    if(looking)spots.clear();
    for(int i=0;i<numsectors;++i)for(mobj_t *thing=sectors[i].thinglist;thing;thing=thing->snext) {
        float x=units(thing->x),y=units(thing->y),z=units(thing->z);
        if(Game_BlastFrame(thing)>=GAME_BLAST_BURST) {
            blasting.insert(thing);
            if(blown.insert(thing).second)disturb(x,y,z+8,160,1.2f,0.6f,true);
            continue;
        }
        bool missile=thing->flags&MF_MISSILE,hot=(thing->frame&FF_FULLBRIGHT)!=0;
        auto found=flights.find(thing);
        if(missile) {
            Flight &f=flights[thing];
            if(found==flights.end()||f.type!=thing->type)f.wake=0;
            f={x,y,z,thing->type,true,f.wake+1};
            float h=z+units(thing->height)/2;
            if(f.wake%3==0&&close(x,y,eye))disturb(x,y,h,20,0.2f,hot?1.0f:0.0f,false,0);
            if(looking&&hot&&close(x,y,eye))spots.push_back({x,y,h,32,1});
            continue;
        }
        if(found!=flights.end()) {
            // A projectile that stopped being one where it was: it burst.
            if(found->second.type==thing->type&&std::hypot(x-found->second.x,y-found->second.y)<units(thing->info->speed)*2+16) {
                game_thing_role role=Game_ThingRole(thing->type);
                float h=z+units(thing->height)/2;
                if(role==GAME_THING_BIG_SHOT)disturb(x,y,h,224,1.5f,0.8f,true);
                else if(role==GAME_THING_ROCKET)disturb(x,y,h,128,1.0f,0.7f,true);
                else disturb(x,y,h,40,0.35f,1.0f,false);   // a fireball, plasma: small and hot
            }
            flights.erase(found);
        }
        if(!close(x,y,eye))continue;
        bool grounded=thing->z<=thing->floorz+FRACUNIT;
        // A body come to rest on the floor, once.
        if(thing->flags&MF_CORPSE) {
            if(grounded&&thing->momx==0&&thing->momy==0&&rested.insert(thing).second)
                disturb(x,y,z+4,std::clamp(units(thing->radius)*1.6f,24.0f,64.0f),0.6f,0,false);
            continue;
        }
        rested.erase(thing);
        // Steps: the player and the monsters on their feet.
        if((thing->player||(thing->flags&MF_COUNTKILL))&&grounded&&!(thing->flags&MF_FLOAT)&&thing->health>0) {
            auto p=prints.find(thing);
            if(p==prints.end())prints[thing]={x,y,true};
            else {
                p->second.seen=true;
                if(std::hypot(x-p->second.x,y-p->second.y)>=28) {
                    p->second.x=x;p->second.y=y;
                    disturb(x,y,z+2,std::clamp(units(thing->radius)*0.4f,8.0f,24.0f),0.5f,0,false);
                }
            }
        }
        // Torches and what burns: decorations glowing a fire's colour.
        if(looking&&decorationLight(*thing)) {
            BakeLight source=decorationSource(*thing);
            if(source.color[0]>source.color[2]*1.4f)spots.push_back({x,y,z,std::clamp(source.radius*0.25f,24.0f,72.0f),std::clamp(source.strength,0.5f,1.0f)});
        }
    }
    for(auto i=flights.begin();i!=flights.end();)i=i->second.seen?std::next(i):flights.erase(i);
    for(auto i=prints.begin();i!=prints.end();)i=i->second.seen?std::next(i):prints.erase(i);
    for(auto i=blown.begin();i!=blown.end();)i=blasting.count(*i)?std::next(i):blown.erase(i);   // done blowing: forgotten
    if(looking) {
        // The nearest 24.
        if(eye&&spots.size()>24) {
            float ex=units(eye->x),ey=units(eye->y);
            std::partial_sort(spots.begin(),spots.begin()+24,spots.end(),[&](const HotSpot &a,const HotSpot &b){return std::hypot(a.x-ex,a.y-ey)<std::hypot(b.x-ex,b.y-ey);});
            spots.resize(24);
        }
        disturbanceHeat(spots,8.0f/TICRATE);
    }
}

void disturbanceShot(float x,float y,float z) {disturb(x,y,z+2,12,0.35f,0,false);}

void disturbanceMuzzle(const mobj_t &source) {
    float angle=(float)(source.angle*(2.0*doomPi/4294967296.0));
    float z=source.player?units(source.player->viewz)-12:units(source.z)+units(source.height)*0.6f;
    float x=units(source.x)+std::cos(angle)*24,y=units(source.y)+std::sin(angle)*24;
    disturb(x,y,z,32,0.25f,0.3f,false,0);
}

void disturbanceSourcesShutdown() {flights.clear();blown.clear();blasting.clear();rested.clear();prints.clear();heights.clear();moving.clear();spots.clear();level=~0u;}
} // namespace doom3d
