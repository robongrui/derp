/* Muzzle fire (PLAN-muzzle-fire.md): the Hollywood muzzle flash at DOOM's resolution, never the same
   twice. Other games' recipe: much bigger than a real gun's, a random shape per shot turned any
   way, a tic or two at white heat then a hard cut to a dark red, sparks thrown forward, smoke after.
   - Enemies: from where the firing frame has its flash painted (sprite_flash.h), turned with the
     frame as the camera sees it. Toward the camera a shape across the view (a star of uneven
     prongs, a flower of broad petals, or a ragged fireball) round a white core; from the side a
     fireball along his aim with lobes off it.
   - The player's bullet guns: their flash frame is not drawn (weapon.cpp); its own pixels are
     stamped where it would be, turned, mirrored and sized anew each shot, with a few prongs of its
     colours on top.
   Which guns: Game_MuzzleFire (DOOM's bullet guns; none in Heretic). Called from the muzzle
   light (lights.cpp muzzleFlash); aged on game time, so slow motion stretches it. Drawing only
   (F4 Muzzle flash): own random numbers, nothing the game sees. */
#include "internal.h"
#include "../sprite_flash.h"

namespace doom3d {
namespace {
constexpr float pi2=6.2831853f;
// What a firing frame's painted flash gives: its middle in the lump's pixels and its colours.
// whole: a weapon's flash frame, which is all flash: its middle and colours from every pixel of it
// (its yellow alone sat along the top of the shotgun's, and read gold where it is red and white),
// and every pixel of it about that middle, to stamp (stamp).
// mean: its colours' average, brightest channel at 1.
struct StampPixel { float dx,dy;std::array<float,3> colour; };
struct Muzzle {
    bool found=false;float u=0,v=0;int w=0,left=0,top=0;
    std::vector<std::array<float,3>> colours;std::array<float,3> mean{1,1,1};
    std::vector<StampPixel> stamp;
};
std::unordered_map<int,Muzzle> muzzles;
const Muzzle &muzzleOf(int lump,bool whole=false) {
    int key=lump*2+(whole?1:0);
    auto it=muzzles.find(key);
    if(it!=muzzles.end())return it->second;
    Muzzle m;SpriteArt art;
    const byte *data=(const byte*)W_CacheLumpNum(lump,PU_CACHE);
    const byte *palette=(const byte*)W_CacheLumpName((char*)"PLAYPAL",PU_CACHE);
    if(!decodeSprite(data,W_LumpLength(lump),art)) {
        std::vector<unsigned char> mask=whole?std::vector<unsigned char>():doom_sprite_flash(art.w,art.h,art.pixels.data(),palette);
        if(whole)mask.assign((size_t)art.w*art.h,0);
        // A weapon's flash frame is all flash, and not always in the ramps a monster's is painted in:
        // then all of it.
        bool any=std::find(mask.begin(),mask.end(),(unsigned char)255)!=mask.end();
        double su=0,sv=0;int n=0;
        std::vector<std::array<float,3>> all;std::vector<size_t> where;
        std::array<float,3> sum{0,0,0};
        for(size_t i=0;i<mask.size();++i) if(any?mask[i]!=0:art.pixels[2*i+1]!=0) {
            su+=(double)(i%art.w)+0.5;sv+=(double)(i/art.w)+0.5;++n;
            const byte *c=palette+art.pixels[2*i]*3;
            all.push_back({c[0]/255.0f,c[1]/255.0f,c[2]/255.0f});where.push_back(i);
            for(int k=0;k<3;++k)sum[k]+=all.back()[k];
        }
        if(n) {
            m.found=true;m.u=(float)(su/n);m.v=(float)(sv/n);
            // Its colours spread over the whole of it (the first few were all its top edge), and their average.
            for(size_t i=0;i<all.size()&&m.colours.size()<24;i+=std::max<size_t>(1,all.size()/24))m.colours.push_back(all[i]);
            float top=std::max({sum[0],sum[1],sum[2],1e-4f});
            for(int k=0;k<3;++k)m.mean[k]=sum[k]/top;
            if(whole)for(size_t k=0;k<where.size();++k)
                m.stamp.push_back({(float)(where[k]%art.w)+0.5f-m.u,(float)(where[k]/art.w)+0.5f-m.v,all[k]});
        }
        m.w=art.w;m.left=art.left;m.top=art.top;
    }
    return muzzles.emplace(key,std::move(m)).first->second;
}

enum Kind { PISTOL, SHOTGUN, CHAINGUN };
enum Shape { STAR, FLOWER, FIREBALL };
struct Shot {
    float x,y,z,aim;          // where he stood (feet), his aim in radians
    int sprite,frame;angle_t facing;
    Kind kind;bool player,smoked=false;
    int tic;uint32_t seed;
    std::array<float,3> glow;
    std::vector<std::array<float,3>> colours;   // the player's flash frame's
    int flashLump=-1;                           // the player's flash frame, where the fire sits in its stead
    // This shot's own: its shape, turn and size, how long it burns and how much of that at white heat.
    Shape shape;float turn,size;bool mirror;int life,hot;
};
struct Puff { float x,y,z,born,life,size;uint32_t seed; };
std::vector<Shot> shots;
std::vector<Puff> puffs;
unsigned seenLevel=0;
uint32_t nextSeed=0x9e3779b9u;

constexpr size_t mostShots=32,mostPuffs=96;
constexpr int sparkTics=6;   // sparks outlive the flash

// White, the flash's own colour, orange, a dark red: t from 0 to 1.
std::array<float,3> fireColour(float t,const std::array<float,3> &glow) {
    static const std::array<float,3> white{1.0f,0.96f,0.82f},orange{1.0f,0.5f,0.14f},red{0.5f,0.07f,0.02f};
    auto mix=[](const std::array<float,3> &a,const std::array<float,3> &b,float f) {
        return std::array<float,3>{a[0]+(b[0]-a[0])*f,a[1]+(b[1]-a[1])*f,a[2]+(b[2]-a[2])*f};
    };
    t=std::clamp(t,0.0f,1.0f);
    if(t<0.25f)return mix(white,glow,t/0.25f);
    if(t<0.6f)return mix(glow,orange,(t-0.25f)/0.35f);
    return mix(orange,red,(t-0.6f)/0.4f);
}

// How the flash burns at this age (tics, with the frame's fraction): its strength and where on the
// colour ramp it is. Full and white for its hot tics, then cut hard to a dimmer orange that dies red.
struct Heat { float strength,colour; };
Heat heatAt(const Shot &s,float tics) {
    if(tics<s.hot)return {1.0f,0.1f*tics/s.hot};
    float u=std::clamp((tics-s.hot)/std::max(1,s.life-s.hot),0.0f,1.0f);
    return {0.6f*std::pow(1-u,1.5f),0.45f+0.55f*u};
}

// The barrel as the camera sees him: the frame's rotation toward the eye, its flash's middle on the card.
bool enemyMuzzle(const Shot &s,const Uniforms &camera,float out[3]) {
    if(s.sprite<0||s.sprite>=numsprites)return false;
    int frame=s.frame&FF_FRAMEMASK;
    if(frame>=sprites[s.sprite].numframes)return false;
    const auto &sf=sprites[s.sprite].spriteframes[frame];
    double seen=std::atan2(s.y-camera.eye[1],s.x-camera.eye[0]);
    angle_t view=(angle_t)(int64_t)std::llround(seen/(2*3.14159265358979)*4294967296.0);
    int rot=sf.rotate?(int)((view-s.facing+(unsigned)(ANG45/2)*9)>>29):0;
    const Muzzle &m=muzzleOf(firstspritelump+sf.lump[rot]);
    if(!m.found)return false;
    float across=(sf.flip[rot]?m.w-m.u:m.u)-m.left,height=m.top-m.v;
    float rx=camera.right[0],ry=camera.right[1],rl=std::hypot(rx,ry);
    if(rl<1e-4f)return false;
    rx/=rl;ry/=rl;
    // A little toward the eye: in front of his own card.
    float tx=camera.eye[0]-s.x,ty=camera.eye[1]-s.y,tl=std::max(std::hypot(tx,ty),1.0f);
    out[0]=s.x+rx*across+tx/tl*3;out[1]=s.y+ry*across+ty/tl*3;out[2]=s.z+height;
    return true;
}

// Where the 320x200 frame the weapon is laid in lies, and the 3D view, in the render's pixels
// (prepareFrame, last frame's: they hold still). The weapon layer is drawn over the frame, the
// world over the view (gpu3d/draw.cpp), and the two differ whenever the status bar is up.
float uiRect[4]={0,0,0,0},worldRect[4]={0,0,0,0};
constexpr float playerDepth=30;   // how far ahead of the eye the player's fire is laid

// The player's barrel: the middle of his weapon's flash frame where weaponDraws would lay it on the
// screen (weapon.cpp, which no longer draws it), taken back into the view playerDepth ahead of the
// eye. Without one, ahead of and below the eye (as the tracers start, tracers.cpp).
bool layerKnown(const Uniforms &camera) {return camera.projection[0]>0&&camera.projection[1]>0&&uiRect[2]>0&&worldRect[2]>0&&worldRect[3]>0;}
void playerMuzzle(const Shot &s,const Uniforms &camera,float out[3]) {
    const float depth=playerDepth;
    float right=0,up=-6*30/28.0f;
    const pspdef_t &weapon=players[displayplayer].psprites[ps_weapon];
    if(s.flashLump>=0&&weapon.state&&layerKnown(camera)) {
        const Muzzle &m=muzzleOf(s.flashLump,true);
        if(m.found) {
            float base=viewwindowy+viewheight/2.0f-100+weaponDrop();
            float x=units(weapon.sx)-m.left+m.u,y=base+units(weapon.sy)-m.top+m.v;   // in the frame's 320x200
            float px=uiRect[0]+x/320*uiRect[2],py=uiRect[1]+y/200*uiRect[3];         // on the render
            float nx=(px-worldRect[0])/worldRect[2]*2-1,ny=1-(py-worldRect[1])/worldRect[3]*2;   // in the view
            right=nx*depth/camera.projection[0];
            up=ny*depth/camera.projection[1];
        }
    }
    for(int c=0;c<3;++c)out[c]=camera.eye[c]+camera.forward[c]*depth+camera.right[c]*right+camera.up[c]*up;
}

void addSmoke(const Shot &s,const float at[3],float now) {
    int count=s.kind==SHOTGUN?4:s.kind==CHAINGUN?1:2;
    for(int i=0;i<count;++i) {
        if(puffs.size()>=mostPuffs)puffs.erase(puffs.begin());
        uint32_t seed=hash32(s.seed+(uint32_t)i*7919u);
        float life=0.9f+0.5f*hashUnit(i,1,seed),size=(s.kind==SHOTGUN?2.2f:1.6f)*(0.8f+0.4f*hashUnit(i,2,seed));
        float ahead=(s.player?10:2)+3*hashUnit(i,3,seed);   // his own a little ahead of the barrel, clear of the eye
        puffs.push_back({at[0]+std::cos(s.aim)*ahead,at[1]+std::sin(s.aim)*ahead,at[2],now+0.02f*i,life,size,seed});
    }
}

// A solid tongue of fire from o along dir, width either side along side at its root and narrowing to
// its tip, filled with DOOM pixels at that distance so it reads as a shape rather than dots.
// heat: where its root is on the colour ramp (its tip a little further on). paint: colours to fill it
// with (a weapon's flash frame's own), cooling toward the ramp as it fades.
void flameSpike(const Uniforms &camera,const float o[3],const float dir[3],const float side[3],float length,float width,
                float heat,const std::array<float,3> &glow,float strength,const std::vector<std::array<float,3>> *paint=nullptr,uint32_t seed=0) {
    float depth=(o[0]-camera.eye[0])*camera.forward[0]+(o[1]-camera.eye[1])*camera.forward[1]+(o[2]-camera.eye[2])*camera.forward[2];
    if(depth<6||length<=0||strength<=0)return;
    float unit=depth/(100*camera.projection[1]);   // map units a DOOM pixel spans there
    float step=std::max(unit*1.1f,length/40);
    int block=0;
    for(float d=0;d<=length;d+=step) {
        float f=d/length,w=width*std::pow(1-f,0.7f);
        auto ramp=fireColour(heat+f*0.3f,glow);
        for(float a=-w;a<=w+1e-3f;a+=std::max(step,w/6)) {
            auto colour=ramp;
            if(paint&&!paint->empty()) {
                colour=(*paint)[hash32(seed+(uint32_t)block*2654435761u)%paint->size()];
                float cool=std::clamp(heat*1.2f+f*0.5f-0.2f,0.0f,1.0f);
                for(int c=0;c<3;++c)colour[c]+=(ramp[c]-colour[c])*cool;
            }
            ++block;
            float x=o[0]+dir[0]*d+side[0]*a,y=o[1]+dir[1]*d+side[1]*a,z=o[2]+dir[2]*d+side[2]*a;
            // Overlapping, so snapping each to the DOOM-pixel grid leaves no holes (1.05 showed a checkerboard).
            pixelBlock(glowVertices,camera,x,y,z,step*1.6f,1,colour,false,nullptr,false,nullptr,strength*(1-0.45f*f));
        }
    }
}
// A spike across the view at angle a (radians, from the camera's right toward its up).
void viewSpike(const Uniforms &camera,const float o[3],float a,float length,float width,float heat,
               const std::array<float,3> &glow,float strength,const std::vector<std::array<float,3>> *paint,uint32_t seed) {
    float dir[3],side[3];
    for(int c=0;c<3;++c) {
        dir[c]=std::cos(a)*camera.right[c]+std::sin(a)*camera.up[c];
        side[c]=-std::sin(a)*camera.right[c]+std::cos(a)*camera.up[c];
    }
    flameSpike(camera,o,dir,side,length,width,heat,glow,strength,paint,seed);
}

// The shape this shot shows down the barrel, reach across, turned by its turn: a star of 3 to 6 uneven
// prongs (some stubs), a flower of 4 or 5 broad petals, or a ragged fireball; a white core in all.
void frontShape(const Shot &s,const Uniforms &camera,const float at[3],float reach,const Heat &h,
                const std::vector<std::array<float,3>> *paint) {
    uint32_t seed=s.seed;
    int count;float least,most,width;
    switch(s.shape) {
      case STAR:     count=3+(int)(hashUnit(1,20,seed)*4);least=0.3f;most=1.0f;width=0.11f;break;
      case FLOWER:   count=4+(int)(hashUnit(1,20,seed)*2);least=0.45f;most=0.6f;width=0.24f;break;
      default:       count=11+(int)(hashUnit(1,20,seed)*4);least=0.3f;most=0.5f;width=0.17f;break;
    }
    for(int k=0;k<count;++k) {
        float a=s.turn+pi2*(k+0.35f*(hashUnit(k,21,seed)*2-1))/count;
        // A star's prongs: about one in three a stub, the rest long.
        float share=s.shape==STAR&&hashUnit(k,22,seed)<0.33f?least*0.6f:least+(most-least)*hashUnit(k,23,seed);
        viewSpike(camera,at,a,reach*share,reach*width*(0.7f+0.6f*hashUnit(k,24,seed)),h.colour,s.glow,h.strength,paint,seed+(uint32_t)k*7919u);
    }
    // The white core.
    for(int k=0;k<8;++k)
        viewSpike(camera,at,s.turn+pi2*k/8,reach*0.17f,reach*0.09f,h.colour*0.5f,s.glow,std::min(1.0f,h.strength*1.2f),nullptr,0);
}

// From the side: a fireball out along his aim, as long as reach, with lobes off it to either side and
// one up, all its own each shot.
void sideShape(const Shot &s,const Uniforms &camera,const float at[3],float reach,const Heat &h) {
    float aim[3]={std::cos(s.aim),std::sin(s.aim),0};
    float side[3]={-aim[1],aim[0],0};
    flameSpike(camera,at,aim,side,reach,reach*0.22f,h.colour,s.glow,h.strength);
    float from[3];
    for(int c=0;c<3;++c)from[c]=at[c]+aim[c]*reach*(0.15f+0.15f*hashUnit(2,30,s.seed));
    int lobes=s.kind==SHOTGUN?3:2;
    for(int k=0;k<lobes;++k) {
        float spread=(0.5f+0.4f*hashUnit(k,31,s.seed))*(k==0?1:k==1?-1:0);
        float lift=k==2?0.8f:0.15f*(hashUnit(k,32,s.seed)*2-1);
        float dir[3]={aim[0]*std::cos(spread)-aim[1]*std::sin(spread),aim[0]*std::sin(spread)+aim[1]*std::cos(spread),lift};
        float dl=std::sqrt(dir[0]*dir[0]+dir[1]*dir[1]+dir[2]*dir[2]);
        for(float &d:dir)d/=dl;
        float across[3]={-dir[1],dir[0],0};
        flameSpike(camera,from,dir,across,reach*(0.35f+0.2f*hashUnit(k,33,s.seed)),reach*0.13f,h.colour+0.1f,s.glow,h.strength*0.9f);
    }
}

// Sparks thrown forward from at along dir in a cone: short streaks of white-hot DOOM pixels that
// cool and die in sparkTics.
void drawSparks(const Shot &s,const Uniforms &camera,const float at[3],const float dir[3],float tics,float size) {
    if(tics>=sparkTics)return;
    float u=tics/sparkTics;
    int count=(s.kind==SHOTGUN?7:4)+(int)(hashUnit(0,40,s.seed)*4);
    float up[3]={0,0,1};
    float side[3]={dir[1]*up[2]-dir[2]*up[1],dir[2]*up[0]-dir[0]*up[2],dir[0]*up[1]-dir[1]*up[0]};
    float sl=std::max(std::sqrt(side[0]*side[0]+side[1]*side[1]+side[2]*side[2]),1e-4f);
    for(float &c:side)c/=sl;
    float lift[3]={side[1]*dir[2]-side[2]*dir[1],side[2]*dir[0]-side[0]*dir[2],side[0]*dir[1]-side[1]*dir[0]};
    for(int i=0;i<count;++i) {
        float a=(hashUnit(i,41,s.seed)*2-1)*0.45f,b=(hashUnit(i,42,s.seed)*2-1)*0.3f;
        float speed=(s.player?9.0f:14.0f)*(0.6f+0.8f*hashUnit(i,43,s.seed));   // units a tic
        float d[3];
        for(int c=0;c<3;++c)d[c]=dir[c]+side[c]*a+lift[c]*b;
        float dl=std::sqrt(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]);
        for(float &c:d)c/=dl;
        float travel=speed*tics,drop=0.15f*tics*tics;
        auto colour=fireColour(0.05f+0.75f*u,s.glow);
        for(int k=0;k<3;++k) {   // the streak: its head and two behind
            float back=k*speed*0.35f;
            if(travel-back<0)break;
            float x=at[0]+d[0]*(travel-back),y=at[1]+d[1]*(travel-back),z=at[2]+d[2]*(travel-back)-drop;
            pixelBlock(glowVertices,camera,x,y,z,size,1,colour,false,nullptr,false,nullptr,(1-u)*(1-0.3f*k));
        }
    }
}

void drawEnemyFire(const Shot &s,const Uniforms &camera,float tics,const float at[3]) {
    float aim[3]={std::cos(s.aim),std::sin(s.aim),0};
    if(tics<s.life) {
        Heat h=heatAt(s,tics);
        // Hollywood: a pistol's as big as his head, a shotgun's about half of him.
        float reach=(s.kind==SHOTGUN?34.0f:s.kind==CHAINGUN?26.0f:22.0f)*s.size;
        // How far his aim points at the camera: the shape across the view grows with it, the fireball
        // along his aim (end-on, a dot) shrinks.
        float tx=camera.eye[0]-at[0],ty=camera.eye[1]-at[1],tz=camera.eye[2]-at[2],tl=std::max(std::sqrt(tx*tx+ty*ty+tz*tz),1.0f);
        float facing=std::clamp((aim[0]*tx+aim[1]*ty+aim[2]*tz)/tl,0.0f,1.0f);
        frontShape(s,camera,at,reach*(0.35f+0.65f*facing),h,nullptr);
        if(facing<0.85f)sideShape(s,camera,at,reach*1.3f*(1-0.5f*facing),h);
    }
    drawSparks(s,camera,at,aim,tics,0.7f);
}

// The player's: his flash frame's own pixels stamped where it would be drawn, turned, mirrored and
// sized anew, with a few prongs of its colours on top, and sparks.
void drawPlayerFire(const Shot &s,const Uniforms &camera,float tics) {
    float at[3];playerMuzzle(s,camera,at);
    if(tics<s.life) {
        Heat h=heatAt(s,tics);
        const Muzzle *m=s.flashLump>=0?&muzzleOf(s.flashLump,true):nullptr;
        if(m&&!m->stamp.empty()&&layerKnown(camera)) {
            // One of its pixels on the render (the weapon layer's 1.2:1), and a render pixel in map units there.
            float colPx=uiRect[2]/320*s.size,rowPx=uiRect[3]/200*s.size;
            float rightUnit=2*playerDepth/(worldRect[2]*camera.projection[0]),upUnit=2*playerDepth/(worldRect[3]*camera.projection[1]);
            float c=std::cos(s.turn),sn=std::sin(s.turn);
            float block=std::max(colPx*rightUnit,rowPx*upUnit)*1.8f;   // overlapping when turned (1.3 left a checkerboard)
            for(size_t i=0;i<m->stamp.size();++i) {
                const StampPixel &p=m->stamp[i];
                float px=(s.mirror?-p.dx:p.dx)*colPx,py=p.dy*rowPx;      // render pixels, down positive
                float rx=px*c-py*sn,ry=px*sn+py*c;
                float r=rx*rightUnit,u=-ry*upUnit;
                float x=at[0]+camera.right[0]*r+camera.up[0]*u,y=at[1]+camera.right[1]*r+camera.up[1]*u,z=at[2]+camera.right[2]*r+camera.up[2]*u;
                auto colour=p.colour,ramp=fireColour(h.colour,s.glow);
                float cool=std::clamp(h.colour*1.3f-0.15f,0.0f,1.0f);
                for(int k=0;k<3;++k)colour[k]+=(ramp[k]-colour[k])*cool;
                pixelBlock(glowVertices,camera,x,y,z,block,1,colour,false,nullptr,false,nullptr,h.strength);
            }
        }
        float reach=(s.kind==SHOTGUN?9.0f:s.kind==CHAINGUN?7.0f:6.0f)*s.size;
        Shot prongs=s;prongs.shape=STAR;
        frontShape(prongs,camera,at,reach,h,&s.colours);
    }
    drawSparks(s,camera,at,camera.forward,tics,0.25f);
}

void drawPuffs(const Uniforms &camera,float now) {
    static const std::array<float,3> grey{0.5f,0.5f,0.5f};
    for(const Puff &p:puffs) {
        float t=(now-p.born)/p.life;
        if(t<0||t>=1)continue;
        float age=now-p.born;
        float x=p.x+(hashUnit(1,5,p.seed)*2-1)*4*age,y=p.y+(hashUnit(2,5,p.seed)*2-1)*4*age,z=p.z+11*age;
        float near=std::hypot(x-camera.eye[0],y-camera.eye[1]);
        float fade=std::clamp((near-18)/18,0.0f,1.0f);
        if(fade<=0)continue;
        auto lit=particleColour(grey,x,y,z,false);
        // A loose cluster of small specks spreading as it rises: one big block read as a grey square.
        float spread=p.size*(0.6f+1.8f*t),alpha=0.45f*std::pow(1-t,1.2f)*fade;
        for(int k=0;k<6;++k) {
            float ox=(hashUnit(k,6,p.seed)*2-1)*spread,oy=(hashUnit(k,7,p.seed)*2-1)*spread,oz=(hashUnit(k,8,p.seed)*2-1)*spread*0.7f;
            pixelBlock(particleVertices,camera,x+ox,y+oy,z+oz,p.size*0.35f,1,lit,false,&grey,false,nullptr,alpha);
        }
    }
}
}

void muzzleFireFrame(float uiX,float uiY,float uiW,float uiH,float worldX,float worldY,float worldW,float worldH) {
    uiRect[0]=uiX;uiRect[1]=uiY;uiRect[2]=uiW;uiRect[3]=uiH;
    worldRect[0]=worldX;worldRect[1]=worldY;worldRect[2]=worldW;worldRect[3]=worldH;
}

// Whether the player's weapon has its flash frame replaced by the fire here (weapon.cpp leaves it out):
// a bullet gun with Muzzle flash on. Off, Doom's own flash frame is drawn as ever.
bool muzzleFireHidesFlash(int weapon) {return settings.muzzleFire&&Game_MuzzleFire(weapon)>0;}

// A bullet gun fired (lights.cpp muzzleFlash): its fire and smoke, if anyone's to see them.
void muzzleFire(const mobj_t &source,int weapon,int projectile,const std::array<float,3> &glow) {
    if(!settings.muzzleFire||gamestate!=GS_LEVEL||projectile>=0)return;
    int gun=Game_MuzzleFire(weapon);
    if(gun<=0)return;
    Kind kind=gun==2?SHOTGUN:gun==3?CHAINGUN:PISTOL;
    if(seenLevel!=r_levelserial) {shots.clear();puffs.clear();muzzles.clear();seenLevel=r_levelserial;}
    if(shots.size()>=mostShots)shots.erase(shots.begin());
    Shot s;
    s.x=units(source.x);s.y=units(source.y);s.z=units(source.z);
    s.aim=(float)(source.angle*(2*3.14159265358979/4294967296.0));
    s.sprite=source.sprite;s.frame=source.frame;s.facing=source.angle;
    s.kind=kind;s.player=source.player!=nullptr;s.tic=leveltime;
    nextSeed=hash32(nextSeed+0x6d2b79f5u);s.seed=nextSeed;
    s.glow=glow;
    // Its own: a shape (stars most, flowers and fireballs), any turn, 0.7 to 1.4 its size with one in
    // five a big one, and a burn of its own length.
    float pick=hashUnit(0,10,s.seed);
    s.shape=pick<0.5f?STAR:pick<0.75f?FLOWER:FIREBALL;
    s.turn=pi2*hashUnit(0,11,s.seed);
    s.size=hashUnit(0,12,s.seed)<0.2f?1.8f:0.7f+0.7f*hashUnit(0,13,s.seed);
    s.mirror=hashUnit(0,14,s.seed)<0.5f;
    int least=kind==SHOTGUN?3:2;
    s.life=least+(hashUnit(0,15,s.seed)<0.5f?1:0);
    s.hot=kind==SHOTGUN&&hashUnit(0,16,s.seed)<0.5f?2:1;
    if(s.player) {
        float radius=0,strength=0;int flashState=-1;
        Game_MuzzleFlash(weapon,&radius,&strength,&flashState);
        if(flashState>=0) {
            const auto &state=states[flashState];
            int frame=state.frame&FF_FRAMEMASK;
            if(state.sprite>=0&&state.sprite<numsprites&&frame<sprites[state.sprite].numframes) {
                s.flashLump=firstspritelump+sprites[state.sprite].spriteframes[frame].lump[0];
                const Muzzle &m=muzzleOf(s.flashLump,true);
                s.colours=m.colours;
                if(m.found)s.glow=m.mean;   // the fire in the flash frame's own colours (the shotgun's red and white)
            }
        }
        // His own stays nearer the frame's size: a little smaller to a little over half again.
        s.size=hashUnit(0,12,s.seed)<0.2f?1.6f:0.85f+0.45f*hashUnit(0,13,s.seed);
    }
    shots.push_back(std::move(s));
}

void buildMuzzleFire(const Uniforms &camera) {
    if(seenLevel!=r_levelserial) {shots.clear();puffs.clear();muzzles.clear();seenLevel=r_levelserial;}
    if(!settings.muzzleFire) {shots.clear();puffs.clear();return;}
    float now=gameSeconds();
    for(Shot &s:shots) {
        float tics=leveltime-s.tic+renderFraction;
        if(tics<0||tics>=std::max(s.life,sparkTics))continue;
        float at[3];
        bool placed=s.player?(playerMuzzle(s,camera,at),true):enemyMuzzle(s,camera,at);
        if(!placed)continue;
        if(!s.smoked) {s.smoked=true;addSmoke(s,at,now);}
        if(s.player)drawPlayerFire(s,camera,tics);
        else drawEnemyFire(s,camera,tics,at);
    }
    shots.erase(std::remove_if(shots.begin(),shots.end(),[](const Shot &s){
        return leveltime<s.tic||leveltime-s.tic>std::max(s.life,sparkTics)+1;}),shots.end());
    puffs.erase(std::remove_if(puffs.begin(),puffs.end(),[&](const Puff &p){return now<p.born-1||now-p.born>p.life;}),puffs.end());
    drawPuffs(camera,now);
}
} // namespace doom3d
