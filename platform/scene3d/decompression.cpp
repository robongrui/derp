/* Physically accurate Phobos (Game_Phobos), what decompression looks like:
   the room's moisture condensing as its air rushes out, streaming through
   the opening, and frost creeping over the helmet's visor while there is no
   air around you. */
#include "internal.h"

namespace doom3d {
// Condensation: per venting opening (Game_Vents) up to this many specks, as
// many as its flow is near the speed of sound, each on its own short cycle:
// it starts in the room behind the opening, up to mistReach units back,
// and is drawn through the gap and out past it, accelerating, a whitish
// grey that thins as it goes. One Doom pixel, two at the throat.
constexpr int mistMost=40;
constexpr float mistReach=112,mistSonic=110;
static void buildBoiling(const Uniforms &camera);
void buildDecompression(const Uniforms &camera) {
    if(!Game_Phobos()||camera.effects[2]!=0||seamCells.empty())return;
    buildBoiling(camera);
    game_vent vents[64];
    int count=Game_Vents(vents,64);
    float now=gameSeconds();
    for(int v=0;v<count;++v) {
        const game_vent &g=vents[v];
        if(std::hypot(g.x-camera.eye[0],g.y-camera.eye[1])>1200)continue;
        // Only a fast drop in pressure cools the air enough to fog: the first violent
        // moments, not the long trickle after.
        float strength=std::clamp((g.speed/mistSonic-0.35f)/0.65f,0.0f,1.0f);
        if(strength<=0)continue;
        int specks=(int)std::round(mistMost*strength*std::clamp(g.width/48,0.4f,1.5f));
        uint32_t seed=hash32((uint32_t)std::lround(g.x*7+g.y*13));
        float ax=-g.dy,ay=g.dx;   // along the opening
        for(int n=0;n<specks;++n) {
            float life=0.5f+0.4f*hashUnit(n,0,seed),clock=now+life*hashUnit(n,1,seed);
            int cycle=(int)std::floor(clock/life);
            float age=(clock-cycle*life)/life;
            uint32_t own=seed^(uint32_t)cycle*2654435761u;
            float back=mistReach*(0.2f+0.8f*hashUnit(n,2,own)),side=(hashUnit(n,3,own)-0.5f)*g.width;
            float high=(hashUnit(n,4,own)-0.5f)*std::min(g.width,48.0f);
            // Behind the gap at first, through it at about two thirds of its life, out beyond.
            float along=-back+(back+48)*age*age*1.4f;
            float pinch=std::clamp(std::abs(along)/back,0.15f,1.0f);
            float x=g.x+g.dx*along+ax*side*pinch,y=g.y+g.dy*along+ay*side*pinch,z=g.z+high*pinch;
            int cx=(int)std::floor((x-mapOrigin[0])/mapCell),cy=(int)std::floor((y-mapOrigin[1])/mapCell);
            if(cx<0||cy<0||cx>=mapWidth||cy>=mapHeight)continue;
            int at=seamCells[(size_t)cy*mapWidth+cx].own;
            if(at==noSector||z<floorZ(&sectors[at])+1||z>ceilZ(&sectors[at])-1)continue;
            float grey=(0.55f+0.25f*hashUnit(n,5,own))*(1-0.6f*age)*strength;
            std::array<float,3> color={grey,grey*1.02f,grey*1.08f};
            pixelBlock(particleVertices,camera,x,y,z,pinch<0.3f?1.2f:0.6f,1,color);
        }
    }
}
// A monster or its body left in vacuum. Rime: the moisture on it boils off
// and freezes back on, at the edges first; it whitens over about a minute and
// a half and thaws in twenty seconds with air. Nothing freezes through: in
// vacuum a body only loses heat by radiating it, which takes hours. Swelling
// (ebullism): the fluids inside boil too, and with no heartbeat holding them
// back (DOOM's zombies are dead, its demons are something else) the tissue
// puffs up with vapour within twenty seconds; the skin holds, nothing bursts.
// With air again it settles back over half a minute.
struct BodyCold { float frost=0,swell=0;int tic=-1; };
static BodyCold &bodyCold(const mobj_t &thing) {
    static std::unordered_map<const mobj_t*,BodyCold> bodies;static unsigned serial=~0u;
    static BodyCold none;
    if(!Game_Phobos()||!(thing.info->flags&MF_COUNTKILL)||!thing.subsector) {none={};return none;}
    if(serial!=r_levelserial) {serial=r_levelserial;bodies.clear();}
    BodyCold &b=bodies[&thing];
    if(b.tic<0)b.tic=leveltime;
    int steps=std::clamp(leveltime-b.tic,0,90*TICRATE);b.tic=leveltime;
    bool vacuum=Game_Air(int(thing.subsector->sector-sectors))<0.25f;
    b.frost=vacuum?std::min(1.0f,b.frost+steps/(90.0f*TICRATE)):std::max(0.0f,b.frost-steps/(20.0f*TICRATE));
    b.swell=vacuum?std::min(1.0f,b.swell+steps/(20.0f*TICRATE)):std::max(0.0f,b.swell-steps/(30.0f*TICRATE));
    return b;
}
unsigned bodyFrost(const mobj_t &thing) {return (unsigned)std::lround(bodyCold(thing).frost*255);}
float bodySwell(const mobj_t &thing) {float s=bodyCold(thing).swell;return s*s*(3-2*s);}
// Frost round a breach: the escaping air's moisture freezes on the walls and
// floor about the opening it rushed through. It builds while the air goes out
// hard (a couple of seconds of a full blast covers it), stays in vacuum (it
// sublimes far too slowly to see) and melts off over a minute once there is
// air again. Wear records (wear.cpp) for both sides of each frosted opening.
// Not at doors: what blows through one as it cycles is too little to frost the doorway,
// and the vacuum side is no colder for it.
struct Breach { float frost=0;int tic=-1; };
static std::unordered_map<int,Breach> breaches;static unsigned breachSerial=~0u;
static void watchPools();
void watchBreaches() {
    watchPools();
    if(!Game_Phobos()||gamestate!=GS_LEVEL) {breaches.clear();return;}
    if(breachSerial!=r_levelserial) {breachSerial=r_levelserial;breaches.clear();}
    static int last=-1;
    if(leveltime==last)return;
    last=leveltime;
    game_vent vents[64];
    int count=Game_Vents(vents,64);
    for(int i=0;i<count;++i) {
        float hard=std::clamp((vents[i].speed/mistSonic-0.35f)/0.65f,0.0f,1.0f);
        if(hard>0&&!doorLine(vents[i].line))breaches[vents[i].line].frost=std::min(1.0f,breaches[vents[i].line].frost+hard/(2.0f*TICRATE));
    }
    for(auto &entry:breaches) {
        const line_t &l=lines[entry.first];
        float air=std::max(l.frontsector?Game_Air(int(l.frontsector-sectors)):0.0f,l.backsector?Game_Air(int(l.backsector-sectors)):0.0f);
        if(air>0.5f)entry.second.frost=std::max(0.0f,entry.second.frost-1.0f/(60*TICRATE));
    }
}
void breachFrost(std::vector<BreachFrost> &out) {
    for(const auto &entry:breaches) {
        if(entry.second.frost<0.02f)continue;
        const line_t &l=lines[entry.first];
        BreachFrost f;
        f.ax=units(l.v1->x);f.ay=units(l.v1->y);f.bx=units(l.v2->x);f.by=units(l.v2->y);
        f.frost=entry.second.frost;f.sectors[0]=l.frontsector?int(l.frontsector-sectors):-1;f.sectors[1]=l.backsector?int(l.backsector-sectors):-1;
        out.push_back(f);
    }
}
// Pools on Phobos. Under the sky, long frozen solid and dark. Indoors, when their room loses
// its air they boil (the pressure drops below their vapour pressure); the evaporation cools
// what is left, and after poolIceStart seconds an ice sheet forms on it in patches, closing
// over poolIceForm seconds while the liquid churns on under the gaps. Frozen over, the
// slime's glow dies and it greys over poolGlowFade seconds; dark, the lights are baked again
// without it. With air again it thaws poolThawFor seconds later, the sheet melting away over
// poolMelt seconds. From the moment it boils it is no liquid: no splashes, bubbling, caustics
// or glowing shore.
constexpr float poolIceStart=1,poolIceForm=7,poolGlowFade=30,poolThawFor=45,poolMelt=12;
struct Pool { float boilAt=-1,frozenAt=-1,thawAt=-1,meltAt=-1;bool frozen=false,liquid=false,dark=false,thick=false,open=true;int baked=8; };
static std::vector<Pool> pools;static unsigned poolSerial=~0u;
// The room light is baked again as pools dim, at most every poolRebakeEvery seconds (each
// bake is a hitch of some 45 ms); the last, dark step always. Asked for apart from
// lightBakeKey, so the old bake stays on until the new one replaces it: clearing the key
// drew the frame of the bake without baked light, a flash.
constexpr float poolRebakeEvery=3;
static float lastRebake=-1e9f;static bool rebakeDue=false,rebakeWanted=false;
static void rebakePools(bool now) {
    rebakeDue=true;
    if(now||gameSeconds()-lastRebake>=poolRebakeEvery) {rebakeWanted=true;lastRebake=gameSeconds();rebakeDue=false;}
}
bool poolRebakeWanted() {bool wanted=rebakeWanted;rebakeWanted=false;return wanted;}
static bool outdoorPool(int sector) {
    if(sectors[sector].ceilingpic!=skyflatnum)return false;
    int look=liquidLook(sectors[sector].floorpic);
    return look!=LIQUID_LOOK_NONE&&look!=LIQUID_LOOK_HOT;
}
// Under the sky known before the first tic (the bakes at the level's start need it).
PoolIce poolIce(int sector) {
    if(!Game_Phobos()||sector<0||sector>=numsectors)return {};
    if(outdoorPool(sector))return {1,0};
    if((size_t)sector>=pools.size())return {};
    const Pool &p=pools[sector];float now=gameSeconds();
    if(p.meltAt>=0) {float m=std::clamp((now-p.meltAt)/poolMelt,0.0f,1.0f);return {1-m,m};}
    if(p.boilAt>=0) {float t=std::clamp((now-p.boilAt-poolIceStart)/poolIceForm,0.0f,1.0f);return {t*t*(3-2*t),1};}
    if(p.frozen)return {1,1-std::clamp((now-p.frozenAt)/poolGlowFade,0.0f,1.0f)};
    return {};
}
static void watchPools() {
    if(!Game_Phobos()||gamestate!=GS_LEVEL) {pools.clear();return;}
    if(poolSerial!=r_levelserial||pools.size()!=(size_t)numsectors) {
        poolSerial=r_levelserial;pools.assign(numsectors,{});
        for(int i=0;i<numsectors;++i) {
            pools[i].liquid=liquidLook(sectors[i].floorpic)!=LIQUID_LOOK_NONE&&liquidLook(sectors[i].floorpic)!=LIQUID_LOOK_HOT;
            pools[i].thick=(size_t)i<sectorMist.size()&&sectorMist[i].thick;
        }
    }
    bool changed=false;
    float now=gameSeconds();
    // A pool already in vacuum when the level starts (its room open to space all along) froze
    // and went dark long ago. The air is worked out in the level's first few tics.
    static unsigned settledFor=~0u;
    bool starting=settledFor!=r_levelserial;
    if(starting&&leveltime<6)return;
    if(starting) {
        settledFor=r_levelserial;
        bool any=false;
        for(int i=0;i<numsectors;++i) {
            Pool &p=pools[i];
            if(!p.liquid||sectors[i].ceilingpic==skyflatnum||Game_Air(i)>=0.25f)continue;
            p.frozen=true;p.frozenAt=now-poolGlowFade-1;p.dark=true;p.baked=0;any=true;
        }
        if(any)rebakePools(true);
    }
    for(int i=0;i<numsectors;++i) {
        Pool &p=pools[i];
        if(!p.liquid||sectors[i].ceilingpic==skyflatnum)continue;
        if(Game_Air(i)<0.25f) {
            p.thawAt=-1;
            if(p.meltAt>=0) {p.meltAt=-1;p.boilAt=now;}
            if(!p.frozen&&p.boilAt<0)p.boilAt=now;
            if(p.boilAt>=0&&now-p.boilAt>=poolIceStart+poolIceForm) {p.frozen=true;p.frozenAt=now;p.boilAt=-1;}
            // The room's light from it (baked) dims in eighths as its glow dies.
            int grey,glow;
            if(frozenLook(i,grey,glow)&&glow!=p.baked) {p.baked=glow;rebakePools(glow==0);}
            if(p.frozen&&poolIce(i).glow<=0)p.dark=true;
        } else {
            p.boilAt=-1;
            if(p.frozen&&p.thawAt<0)p.thawAt=now;
            if(p.frozen&&now-p.thawAt>=poolThawFor) {p.frozen=false;p.thawAt=-1;p.meltAt=now;if(p.baked!=8)rebakePools(true);p.dark=false;p.baked=8;}
            if(p.meltAt>=0&&now-p.meltAt>=poolMelt)p.meltAt=-1;
        }
        // No bubbling once it boils; no liquid (splashes, plunges) once the ice has closed.
        // The caustics and the glowing shore fade with its ice in the shaders (poolIceCode).
        bool open=!p.frozen&&p.boilAt<0&&p.meltAt<0;
        if(open!=p.open&&(size_t)i<sectorMist.size()) {
            p.open=open;changed=true;
            sectorMist[i].liquid=!p.frozen;sectorMist[i].thick=open&&p.thick;
        }
    }
    (void)changed;
    if(rebakeDue)rebakePools(false);
}
float poolIceCode(int sector) {
    PoolIce ice=poolIce(sector);
    return ice.ice+2*std::round(ice.glow*15);
}
bool poolFrozen(int sector) {return poolIce(sector).ice>=1;}
// What a frozen surface shows: how grey (0-16) and how much glow is left (0-4); false
// while it is not frozen over (a wall: its pool half frozen over).
bool frozenLook(int sector,int &grey,int &glow) {
    PoolIce ice=poolIce(sector);
    if(ice.ice<1)return false;
    grey=(int)std::lround((1-ice.glow)*16);glow=(int)std::lround(ice.glow*8);
    return true;
}
bool frozenLookBeside(const line_t &line,int &grey,float &glow) {
    bool any=false;grey=0;glow=1;
    for(const sector_t *s:{line.frontsector,line.backsector}) {
        if(!s)continue;
        PoolIce ice=poolIce(int(s-sectors));
        if(ice.ice<1)continue;
        any=true;glow=std::min(glow,ice.glow);
    }
    if(any)grey=(int)std::lround((1-glow)*16);
    return any;
}
float poolBoiling(int sector) {
    if(!Game_Phobos()||sector<0||(size_t)sector>=pools.size()||pools[sector].boilAt<0)return 0;
    return 1-poolIce(sector).ice;
}
// A boiling pool: pale vapour and spatters a Doom pixel each popping off its surface all
// over, as hard as it boils.
static void buildBoiling(const Uniforms &camera) {
    float now=gameSeconds();
    for(int s=0;s<numsectors;++s) {
        float boil=poolBoiling(s);
        if(boil<=0||(size_t)s>=sectorFloors.size())continue;
        uint32_t seed=hash32((uint32_t)s*2654435761u+7u);
        float z=floorZ(&sectors[s]);
        const auto &leaves=sectorFloors[s];
        if(leaves.empty())continue;
        int specks=(int)(60*boil);
        for(int n=0;n<specks;++n) {
            float life=0.35f+0.3f*hashUnit(n,0,seed),clock=now+life*hashUnit(n,1,seed);
            int cycle=(int)std::floor(clock/life);
            float t=(clock-cycle*life)/life;
            uint32_t own=seed^(uint32_t)cycle*2654435761u;
            const auto &b=floorBounds[leaves[(size_t)(hashUnit(n,2,own)*leaves.size())%leaves.size()]];
            float x=b[0]+(b[2]-b[0])*hashUnit(n,3,own),y=b[1]+(b[3]-b[1])*hashUnit(n,4,own);
            if(std::hypot(x-camera.eye[0],y-camera.eye[1])>900)continue;
            if(R_PointInSubsector((fixed_t)(x*FRACUNIT),(fixed_t)(y*FRACUNIT))->sector!=&sectors[s])continue;
            float rise=(3+12*hashUnit(n,5,own))*t,pale=(0.5f+0.35f*hashUnit(n,6,own))*(1-t);
            pixelBlock(particleVertices,camera,x,y,z+rise,0.5f,1,{pale*0.85f,pale,pale*0.85f});
        }
    }
}
// Shots into a pool's ice (planeImpact) crack it instead of splashing: a star of white
// fracture lines where each landed, drawn by the wear (wear.glsl, kind 7), kept for the level.
static std::vector<IceCrack> iceCracks;static unsigned crackSerial=~0u;
bool crackIce(int sector,float x,float y) {
    if(crackSerial!=r_levelserial) {crackSerial=r_levelserial;iceCracks.clear();}
    if(poolIce(sector).ice<0.5f&&!waterIceHolds(x,y))return false;   // or water frozen in the cold (water_ice.cpp)
    if(iceCracks.size()>=64)iceCracks.erase(iceCracks.begin());
    iceCracks.push_back({sector,x,y,10+8*(float)(impactRandom()%1000)/1000,(uint32_t)impactRandom()});
    return true;
}
const std::vector<IceCrack> &iceCracksNow() {
    static const std::vector<IceCrack> none;
    return crackSerial==r_levelserial?iceCracks:none;
}
// The visor: frost creeps in over about eight seconds without air round you
// and clears in four once there is air again.
float visorFrost() {
    static float frost=0;static int tic=-1;static unsigned serial=~0u;
    const mobj_t *mo=players[displayplayer].mo;
    if(!Game_Phobos()||!mo||gamestate!=GS_LEVEL) {frost=0;return 0;}
    if(serial!=r_levelserial) {serial=r_levelserial;frost=0;tic=leveltime;}
    int steps=std::clamp(leveltime-tic,0,TICRATE);tic=leveltime;
    float air=Game_Air(int(mo->subsector->sector-sectors));
    for(int i=0;i<steps;++i)frost=air<0.5f?std::min(1.0f,frost+1.0f/(8*TICRATE)):std::max(0.0f,frost-1.0f/(4*TICRATE));
    return frost;
}
} // namespace doom3d
