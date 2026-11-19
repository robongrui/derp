/* Shots on walls by what the wall is made of (F4 "Bullet impacts"): concrete throws
   a jet of dust that stops short and hangs, wood splinters, metal sends sparks off
   at the mirror angle with a hot dot and a small light, electronics spit a rain of
   sparks and a wisp of smoke, flesh walls bleed, soil drops clods. All of it real
   specks at Doom's pixel size (a soft cloud read as a curtain), stepped on tics;
   nothing falls off a wall. An enemy's shot near the player is heard as well: the
   player's own is the gun's moment. Drawing only. */
#include "internal.h"
extern "C" {
#include "../audio/impact_synth.h"
#include "../audio/sound_options.h"
}

namespace doom3d {
enum WallKind { WALL_CONCRETE,WALL_WOOD,WALL_METAL,WALL_WIRED,WALL_FLESH,WALL_SOIL,WALL_KINDS };
// A speck in the air: thrown at (vx,vy,vz) units a second, the air stopping it within tau seconds, sinking (or
// rising, below 0) at sink. glow: its own light, cooling to nothing (a hot dot).
struct Speck { float x,y,z,vx,vy,vz,tau,sink,born,life,size;std::array<float,3> albedo,lit;bool glow;float fall=0; };   // fall: its pull, units a second a second
static std::vector<Speck> specks;
constexpr size_t maxSpecks=900;
struct Flicker { float x,y,z;int tic; };
static std::vector<Flicker> flickers;
struct Struck { float x,y;int tic; };
static std::vector<Struck> struck;                 // where the game's puffs are not to be drawn
static const float puffReach=10;constexpr int puffTics=40;
constexpr float dustBright=1.35f,dustSinkLeast=12,dustSinkMost=30,dustFall=110;   // how fast it starts to settle, units a second, and how it gathers speed (a steady sink read as Mars)
constexpr float heardReach=250;                    // an enemy's miss heard within this of the player
static int hitTic=-1,hitsThisTic=0,flashedAt=-100,heardAt=-100;
static float hitX=0,hitY=0;
static unsigned wallHitsSerial=0;

static void checkLevel() {
    if(wallHitsSerial==r_levelserial)return;
    specks.clear();flickers.clear();struck.clear();wallHitsSerial=r_levelserial;hitTic=-1;
}
// A shotgun's pellets on one spot share a splash: two whole, the rest a third (as blood's).
static float shareOf(float x,float y) {
    if(hitTic==leveltime&&std::hypot(x-hitX,y-hitY)<96)++hitsThisTic;
    else {hitTic=leveltime;hitsThisTic=1;hitX=x;hitY=y;}
    return hitsThisTic<=2?1.0f:0.3f;
}
static void addSpeck(const Speck &s) {
    if(specks.size()>=maxSpecks)specks.erase(specks.begin(),specks.begin()+(specks.size()-maxSpecks+1));
    specks.push_back(s);
}
// A way near (ax,ay,az), up to spread off it.
static void scatter(float ax,float ay,float az,float spread,float &x,float &y,float &z) {
    x=ax+randomRange(-spread,spread);y=ay+randomRange(-spread,spread);z=az+randomRange(-spread,spread);
    float l=std::max(1e-3f,std::sqrt(x*x+y*y+z*z));x/=l;y/=l;z/=l;
}
static std::array<float,3> paler(std::array<float,3> c,float share) {
    for(float &v:c)v=std::min(1.0f,v+(0.75f-v)*share);
    return c;
}
// A dust jet along (ax,ay,az): fast out, stopped short by the air, hanging and sinking.
static void dust(float x,float y,float z,float ax,float ay,float az,int count,const std::array<float,3> &colour,float speed,float hang) {
    float now=gameSeconds();
    auto lit=particleColour(colour,x,y,z,false);   // once for the plume: it hangs in one light
    // Dust catches the light: darker than the wall it hid in it. Only where there's light to catch.
    float seen=std::max({lit[0],lit[1],lit[2]})/std::max({colour[0],colour[1],colour[2],1e-3f});
    float lift=1+(dustBright-1)*std::clamp((seen-0.3f)/0.4f,0.0f,1.0f);
    for(float &v:lit)v*=lift;   // unclamped: capping each channel at 1 turned lit dust white
    for(int i=0;i<count;++i) {
        float wx,wy,wz;scatter(ax,ay,az,0.35f,wx,wy,wz);
        float v=speed*randomRange(0.5f,1.2f);
        addSpeck({x,y,z,wx*v,wy*v,wz*v,randomRange(0.05f,0.1f),randomRange(dustSinkLeast,dustSinkMost),now,hang*randomRange(0.5f,1.2f),randomRange(1.5f,3.0f),colour,lit,false,dustFall*randomRange(0.7f,1.2f)});
    }
}
static void chip(float x,float y,float z,float vx,float vy,float vz,float life,float size,const std::array<float,3> &colour,float stretch=0) {
    if(debris.size()>512)debris.erase(debris.begin(),debris.begin()+(debris.size()-512));
    Debris d{x,y,z,vx,vy,vz,gameSeconds(),life,size,colour,1,true};
    d.stretch=stretch;debris.push_back(d);
}
static void spark(float x,float y,float z,float vx,float vy,float vz) {
    if(debris.size()>512)debris.erase(debris.begin(),debris.begin()+(debris.size()-512));
    float heat=randomRange(0,1);
    debris.push_back({x,y,z,vx,vy,vz,gameSeconds(),randomRange(0.1f,0.3f),randomRange(0.7f,1.1f),{1.0f,0.75f+0.25f*heat,0.35f+0.5f*heat*heat}});
}

static WallKind kindOf(game_surface surface,bool wired) {
    if(wired)return WALL_WIRED;
    switch(surface) {
    case GAME_SURFACE_WOOD:return WALL_WOOD;
    case GAME_SURFACE_METAL:return WALL_METAL;
    case GAME_SURFACE_FLESH:return WALL_FLESH;
    case GAME_SURFACE_SOIL:return WALL_SOIL;
    default:return WALL_CONCRETE;
    }
}
// What a kind throws at (x,y,z): out along (ax,ay,az), and the shot's own way off the surface for sparks (rx,ry,rz).
static void throwOff(WallKind kind,float x,float y,float z,float ax,float ay,float az,float rx,float ry,float rz,float share,Image *image,const std::array<float,3> &flat) {
    auto colour=[&]{return image?texelColor(*image):flat;};
    auto count=[&](float n){return std::max(1,(int)std::lround(n*share));};
    switch(kind) {
    case WALL_CONCRETE: {
        dust(x,y,z,ax,ay,az,count(26),paler(colour(),0.6f),380,2.2f);
        for(int i=0,n=count(7);i<n;++i) {float wx,wy,wz;scatter(ax,ay,az,0.5f,wx,wy,wz);float v=randomRange(80,200);chip(x,y,z,wx*v,wy*v,wz*v+40,randomRange(0.3f,0.6f),randomRange(1.0f,1.4f),colour());}
        for(int i=0,n=count(2);i<n;++i) {float v=randomRange(30,80);chip(x,y,z,ax*v,ay*v,az*v+30,randomRange(0.6f,0.9f),randomRange(2.0f,2.8f),colour());}
        break;
    }
    case WALL_WOOD:
        dust(x,y,z,ax,ay,az,count(8),paler(colour(),0.45f),200,1.1f);
        for(int i=0,n=count(8);i<n;++i) {float wx,wy,wz;scatter(ax,ay,az,0.8f,wx,wy,wz);float v=randomRange(70,170);chip(x,y,z,wx*v,wy*v,wz*v+50,randomRange(0.5f,0.9f),randomRange(1.0f,1.3f),colour(),3);}
        break;
    case WALL_METAL: {
        for(int i=0,n=count(8);i<n;++i) {float wx,wy,wz;scatter(rx,ry,rz,0.3f,wx,wy,wz);float v=randomRange(150,320);spark(x,y,z,wx*v,wy*v,wz*v+30);}
        addSpeck({x,y,z,0,0,0,0.1f,0,gameSeconds(),0.45f,1.6f,{1.0f,0.6f,0.2f},{1.0f,0.6f,0.2f},true});
        if(leveltime-flashedAt>=2) {flashedAt=leveltime;sparkFlash(x+ax*4,y+ay*4,z+az*4,60,0.5f,{1.0f,0.8f,0.5f});}
        break;
    }
    case WALL_WIRED: {
        for(int i=0,n=count(6);i<n;++i) {float wx,wy,wz;scatter(ax,ay,az,0.6f,wx,wy,wz);float v=randomRange(40,110);spark(x,y,z,wx*v,wy*v,wz*v-30);}
        std::array<float,3> smoke{0.16f,0.15f,0.15f};auto lit=particleColour(smoke,x,y,z,false);
        for(int i=0,n=count(6);i<n;++i) {float wx,wy,wz;scatter(ax,ay,az,0.4f,wx,wy,wz);addSpeck({x,y,z,wx*50,wy*50,wz*50,0.1f,-randomRange(10,18),gameSeconds(),randomRange(0.8f,1.6f),randomRange(1.0f,1.6f),smoke,lit,false});}
        if(leveltime-flashedAt>=2) {
            flashedAt=leveltime;
            int at=leveltime+2+(randomRange(0,1)<0.5f);
            flickers.push_back({x+ax*6,y+ay*6,z+az*6,at});
            if(randomRange(0,1)<0.6f)flickers.push_back({x+ax*6,y+ay*6,z+az*6,at+2+(randomRange(0,1)<0.5f)});
        }
        break;
    }
    case WALL_FLESH:
        sprayCorpseHit(x,y,z,ax,ay,BLOOD_RED);
        break;
    case WALL_SOIL:
        dust(x,y,z,ax,ay,az,count(10),paler(colour(),0.3f),220,1.2f);
        for(int i=0,n=count(6);i<n;++i) {float wx,wy,wz;scatter(ax,ay,az,0.6f,wx,wy,wz);float v=randomRange(40,110);chip(x,y,z,wx*v,wy*v,wz*v+20,randomRange(0.4f,0.7f),randomRange(1.6f,2.6f),colour());}
        break;
    default:break;
    }
}

// The dark a hit leaves (the decal pass darkens by coverage): a kind's own shape, in three cuts.
static int markOf(WallKind kind) {
    constexpr int size=16,base=-0x6e000000;
    int variant=(int)randomRange(0,2.99f),key=base-(kind*3+variant);
    if(!images.count(key)) {
        std::vector<byte> pixels((size_t)size*size*2,0);
        uint32_t seed=hash32((uint32_t)(kind*3+variant+1)*0x9e3779b9u);
        for(int j=0;j<size;++j)for(int i=0;i<size;++i) {
            float x=i+0.5f-size/2.0f,y=j+0.5f-size/2.0f,edge=valueNoise(i+32.0f,j+32.0f,3,seed)*2-1;
            float d=kind==WALL_WOOD?std::hypot(x*2.0f,y*0.8f):std::hypot(x,y);
            float reach=(kind==WALL_METAL?3.2f:kind==WALL_WOOD?6:kind==WALL_CONCRETE?6.5f:5.5f)+1.6f*edge;
            bool ring=kind==WALL_METAL&&d<1.2f;   // a dent: a rim, bright metal in the middle
            if(d<reach&&!ring)pixels[((size_t)j*size+i)*2+1]=255;
        }
        images.emplace(key,upload(size,size,pixels.data()));
    }
    return -1-key;
}

// A shot stopping on a wall: (wx,wy,z) just off it, (nx,ny) its face's way out, (dx,dy,slope) the shot's way.
int wallHit(int texture,Image &image,float wx,float wy,float z,float nx,float ny,const mobj_t *shooter,float dx,float dy,float slope) {
    checkLevel();
    char name[9];R_TextureName(texture,name);
    WallKind kind=kindOf(Game_WallSurface(name),Game_SparkTexture(name,0)||Game_ScreenTexture(name,0));
    float l=std::max(1e-3f,std::sqrt(dx*dx+dy*dy+slope*slope)),ux=dx/l,uy=dy/l,uz=slope/l;
    float into=ux*nx+uy*ny,rx=ux-2*into*nx,ry=uy-2*into*ny,rz=uz;   // off the wall at the mirror angle
    float ax=0.6f*nx+0.4f*rx,ay=0.6f*ny+0.4f*ry,az=0.4f*rz,al=std::max(1e-3f,std::sqrt(ax*ax+ay*ay+az*az));
    throwOff(kind,wx,wy,z,ax/al,ay/al,az/al,rx,ry,rz,shareOf(wx,wy),&image,{});
    struck.push_back({wx,wy,leveltime});
    // An enemy's shot close by, heard (a spark texture has its own crackle).
    const mobj_t *player=players[consoleplayer].mo;
    if(player&&shooter&&shooter!=player&&kind!=WALL_WIRED&&leveltime-heardAt>=2) {
        float d=std::hypot(wx-units(player->x),wy-units(player->y));
        if(d<heardReach) {
            heardAt=leveltime;
            static const int kinds[WALL_KINDS]={IMPACT_HIT_STONE,IMPACT_HIT_WOOD,IMPACT_HIT_METAL,IMPACT_HIT_STONE,IMPACT_HIT_SOFT,IMPACT_HIT_SOFT};
            I_MadeSoundLevel(kinds[kind],wx,wy,z,1-0.5f*d/heardReach);
        }
    }
    return markOf(kind);
}
// A free-aimed shot stopping on a floor or ceiling: off a ceiling the dust falls, off a floor it kicks up.
void flatHit(const sector_t &sector,bool ceiling,float x,float y,float z,const std::array<float,3> &colour) {
    checkLevel();
    char name[9];memcpy(name,lumpinfo[firstflat+(ceiling?sector.ceilingpic:sector.floorpic)].name,8);name[8]=0;
    for(char *c=name;*c;++c)*c=(char)toupper((unsigned char)*c);
    WallKind kind=kindOf(Game_FlatSurface(name),false);
    float up=ceiling?-1.0f:1.0f;
    throwOff(kind,x,y,z+up,0,0,up,0,0,up,shareOf(x,y),nullptr,colour);
    struck.push_back({x,y,leveltime});
}
// The game's puff at a wall hit isn't drawn: the wall has its own.
bool wallHitPuff(const mobj_t &thing) {
    float x=units(thing.x),y=units(thing.y);
    for(const Struck &s:struck)if(std::hypot(x-s.x,y-s.y)<puffReach)return true;
    return false;
}
void buildMuzzleFire(const Uniforms &camera);   // muzzle_fire.cpp
void buildWallHits(const Uniforms &camera) {
    checkLevel();
    buildTracers(camera);
    buildMuzzleFire(camera);
    float now=gameSeconds();
    struck.erase(std::remove_if(struck.begin(),struck.end(),[](const Struck &s){return leveltime-s.tic>puffTics;}),struck.end());
    for(auto it=flickers.begin();it!=flickers.end();) {
        if(leveltime>=it->tic) {sparkFlash(it->x,it->y,it->z,96,0.5f,{0.45f,0.65f,1.0f});it=flickers.erase(it);}
        else ++it;
    }
    specks.erase(std::remove_if(specks.begin(),specks.end(),[&](const Speck &s){return now-s.born>s.life||now<s.born;}),specks.end());
    for(const Speck &s:specks) {
        float t=std::floor((now-s.born)*TICRATE)/TICRATE,reach=s.tau*(1-std::exp(-t/s.tau));
        float x=s.x+s.vx*reach,y=s.y+s.vy*reach,z=s.z+s.vz*reach-s.sink*t-0.5f*s.fall*t*t;
        const sector_t *sector=R_PointInSubsector((fixed_t)(x*FRACUNIT),(fixed_t)(y*FRACUNIT))->sector;
        if(s.fall>0&&z<=floorZ(sector))continue;   // down: settled
        z=std::clamp(z,floorZ(sector)+0.2f,ceilZ(sector)-0.2f);
        float left=1-t/s.life,size=left<0.25f?s.size*0.5f:s.size;
        std::array<float,3> colour=s.lit;
        if(s.glow)for(float &v:colour)v*=left;
        else {
            float glow[3];
            if(sparkGlow(x,y,z,glow))for(int c=0;c<3;++c)colour[c]=std::min(std::max(1.0f,colour[c]),colour[c]+s.albedo[c]*glow[c]);
        }
        // Lit dust and smoke skip the palette snap, as chips do (impacts.cpp); sparks keep it.
        pixelBlock(particleVertices,camera,x,y,z,size,1,colour,false,s.glow?nullptr:&s.albedo,s.glow,nullptr,1,1,farLeast);
    }
}
void wallHitsShutdown() {specks.clear();flickers.clear();struck.clear();}
}
