/* Disturbances: what happens in the weather that lies and flies, each a
   short-lived record (where, how wide, how hard, how hot, when), on top of
   the weather's static base: a hit in a few frames, then the weather's own
   rules bring it back over seconds. disturbance_sources.cpp finds them
   (blasts, doors, bodies, shots, steps, muzzles, projectiles' wakes).
   - Push: what flies near it is shoved out and up (disturbanceShove, read
     by air_push.cpp), dying away so it goes back to its path.
   - Ground: indoors a crater in what the shader lays (a window's patch, a
     draft's trail: disturbanceMarks, wear.glsl) that fills back in, and
     the floor film round it blown away into a ragged
     rim, once, when it happens (floor_film.cpp's filmDisturb), as hard as
     the record's ground says (0: none, a muzzle's blast): dry ice creeps
     back in by its own rules, snow and sand stay a crater until more blows
     in. Out under the sky the ground's bake is dented (ground_dents.cpp).
   - Fog bank: a hole blown in it (c.clearing, the eight strongest near the
     eye; fog_bank.glsl), open in a few frames, filling back in from its
     edges over seconds.
   - What lay there is thrown up as it goes: a puff of powder, sand or ash
     that falls and lies a moment, or low sheets of mist rolling outward
     (buildDisturbances).
   - Hot things (torches, fireballs: disturbanceHeat, every eight tics) are
     no records but standing spots: what flies rises over them, the bank
     boils away round them, and indoors snow melts and dry ice warms away
     from the film under them.
   A ring of 32 big records (blasts) and 64 small ones (shots, steps and the
   like); with none alive nothing here costs anything. Render-only, with
   its own random numbers. */
#include "internal.h"
#include "../rooms.h"

namespace doom3d {
namespace {
struct Disturbance { float x,y,z,radius,strength,heat,ground,born,lying; int sector; uint32_t seed; bool alive=false; };
std::array<Disturbance,32> bigOnes;
std::array<Disturbance,64> smallOnes;
int nextBig=0,nextSmall=0,alive=0;
unsigned level=~0u;
constexpr float lifetime=16;   // seconds: the longest any effect of one lasts (the bank filling back)
std::vector<HotSpot> hot;      // the hot things near the eye, as last looked at

float now() {return gameSeconds();}
template<class F> void each(F f) {
    if(!alive)return;
    for(Disturbance &d:bigOnes)if(d.alive)f(d);
    for(Disturbance &d:smallOnes)if(d.alive)f(d);
}
bool bankLies() {Weather kind=falling();return kind==WEATHER_FOG||kind==WEATHER_DRY_ICE;}
// How much of what lies at (x, y) in sector s a blast there finds (0-1),
// before it blows it away: indoors the film, outside what settles on the
// ground, or the fog bank.
float lyingAt(float x,float y,int s) {
    Weather kind=falling();
    if(kind==WEATHER_NONE||s<0)return 0;
    if(kind==WEATHER_FOG)return fallNow>0.02f?1.0f:0.0f;
    if(kind==WEATHER_DRY_ICE&&Rooms_Outside(s))return dryIceReady()?dryIceShare(s):0.0f;
    if(!Rooms_Outside(s)) {const WeatherKind &w=weatherKind(kind);return w.film?std::clamp(filmAt(x,y)/w.film->full,0.0f,1.0f):0.0f;}
    if(!weatherKind(kind).settles||fallNow<=0.02f)return 0;
    int cx=(int)std::floor((x-mapOrigin[0])/mapCell),cy=(int)std::floor((y-mapOrigin[1])/mapCell);
    if(cx<0||cy<0||cx>=mapWidth||cy>=mapHeight)return 0;
    uint8_t baked=groundBakeAt((size_t)cy*mapWidth+cx);
    return baked?(baked-1)/254.0f:0.0f;
}
// The shove's strength at age seconds: up in three tics, gone over a second and a half.
float shoveAt(float age) {return age<0?0:std::min(1.0f,age*TICRATE/3)*std::exp(-age/1.5f);}
// A clearing in the bank at age seconds: open in a few tics, held a moment,
// then filling in from its edge over refill seconds; its radius.
float clearingRadius(const Disturbance &d,float age,float refill) {
    float open=std::min(1.0f,age*TICRATE/4),fill=std::clamp((age-0.5f)/refill,0.0f,1.0f);
    return d.radius*1.1f*open*(1-fill*fill*(3-2*fill));
}
float refillSeconds() {return falling()==WEATHER_DRY_ICE?6.0f:10.0f;}
// A new level: no records, no hot things (the sources keep their own).
void resetRecords() {
    for(Disturbance &d:bigOnes)d.alive=false;
    for(Disturbance &d:smallOnes)d.alive=false;
    alive=0;nextBig=nextSmall=0;hot.clear();level=r_levelserial;
}
}

void disturb(float x,float y,float z,float radius,float strength,float heat,bool big,float ground) {
    if(gamestate!=GS_LEVEL||!settings.weather||falling()==WEATHER_NONE)return;
    if(level!=r_levelserial)resetRecords();
    Disturbance &d=big?bigOnes[nextBig]:smallOnes[nextSmall];
    if(big)nextBig=(nextBig+1)%(int)bigOnes.size();else nextSmall=(nextSmall+1)%(int)smallOnes.size();
    if(!d.alive)++alive;
    const sector_t *at=weatherSectorAt(x,y);
    int s=at?int(at-sectors):-1;
    d={x,y,z,radius,strength,heat,ground,now(),lyingAt(x,y,s),s,hash32((uint32_t)leveltime*2654435761u^(uint32_t)(x*7+y*13)),true};
    static const bool debug=getenv("ROOMSDEBUG")!=nullptr;
    if(debug&&big)fprintf(stderr,"DISTURB tic %d at %.0f,%.0f sector %d: radius %.0f strength %.2f, %.2f lying\n",leveltime,x,y,s,radius,strength,d.lying);
    if(s<0||ground<=0)return;
    // The ground, once, now: indoors the film blown away (it wakes and
    // mends by its rules), outside the bake dented (ground_dents.cpp).
    // Indoors only what is wider than a film cell (about 20 units): a step
    // or a bullet there dug out one whole cell and heaped its neighbours,
    // a line of square blocks.
    if(!Rooms_Outside(s)) {if(radius*0.6f>=filmCell())filmDisturb(x,y,radius*0.6f,strength*ground,s,d.seed);}
    else dentGround(x,y,radius*0.6f,strength*ground,d.seed);
}

// Each tic: old records die.
void watchDisturbances() {
    if(gamestate!=GS_LEVEL||!settings.weather||!levelSky)return;
    if(level!=r_levelserial)resetRecords();
    float t=now();
    each([&](Disturbance &d){if(t-d.born>lifetime||t<d.born) {d.alive=false;--alive;}});
#ifdef DOOM_DEVTOOLS
    // Test runs: DISTURBAT=x,y,tic sets off a rocket's blast there on that tic.
    static float testAt[3]={NAN,NAN,-1};
    if(std::isnan(testAt[0])) {testAt[0]=0;if(const char *at=getenv("DISTURBAT"))sscanf(at,"%f,%f,%f",&testAt[0],&testAt[1],&testAt[2]);}
    if(testAt[2]>=0&&leveltime==(int)testAt[2]) {
        const sector_t *s=weatherSectorAt(testAt[0],testAt[1]);
        if(s)disturb(testAt[0],testAt[1],floorZ(s)+8,128,1.0f,0.7f,true);
    }
#endif
}

// The hot things near the eye, every eight tics (dt seconds since): kept for
// the push and the clearings; indoors what melts melts and dry ice warms
// away from the film round each, as hot as it is.
void disturbanceHeat(const std::vector<HotSpot> &spots,float dt) {
    hot=spots;
    Weather kind=falling();
    if(kind!=WEATHER_SNOW&&kind!=WEATHER_DRY_ICE)return;
    for(const HotSpot &h:hot) {
        const sector_t *at=weatherSectorAt(h.x,h.y);
        if(at&&!Rooms_Outside(int(at-sectors)))filmMelt(h.x,h.y,h.radius,std::min(1.0f,h.heat*dt/(kind==WEATHER_DRY_ICE?1.5f:6.0f)));
    }
}

void disturbanceShove(float x,float y,float z,float t,float out[3]) {
    each([&](const Disturbance &d) {
        float age=t-d.born,dx=x-d.x,dy=y-d.y,dz=z-d.z,dist=std::sqrt(dx*dx+dy*dy+dz*dz);
        if(age>6||dist>d.radius*2)return;
        float push=d.strength*d.radius*0.6f*shoveAt(age)*(1-std::clamp((dist-d.radius*0.3f)/(d.radius*1.7f),0.0f,1.0f));
        if(push<=0.05f)return;
        float inv=1/std::max(dist,1.0f);
        out[0]+=dx*inv*push;out[1]+=dy*inv*push;out[2]+=(std::max(dz,0.0f)*inv*0.5f+0.3f)*push;
    });
    // Over a hot thing what flies rises, spreading a little as it goes.
    for(const HotSpot &h:hot) {
        float dx=x-h.x,dy=y-h.y,d=std::hypot(dx,dy),reach=h.radius*1.5f;
        if(d>=reach||z<h.z-32)continue;
        float near=1-d/reach,rise=std::clamp((z-h.z+32)/96,0.0f,1.0f);
        out[2]+=24*h.heat*near*rise;
        if(d>1) {out[0]+=dx/d*6*h.heat*near*rise;out[1]+=dy/d*6*h.heat*near*rise;}
    }
}

// Each recent one indoors that dug at the ground: a crater record (kind
// 10: middle, radius; seed, how filled back, how hard) on the sectors of
// its room it reaches (its own and those round its edge), filling back in
// over fifteen seconds as the snow keeps blowing in.
void disturbanceMarks(std::vector<WearMark> &out) {
    float t=std::floor(gameSeconds()*TICRATE)/TICRATE;   // filling back a step per tic
    each([&](const Disturbance &d) {
        float radius=d.radius*0.6f,age=t-d.born;
        if(d.ground<=0||radius<6||d.sector<0||Rooms_Outside(d.sector)||age<0)return;
        float fill=std::clamp((age-1)/14,0.0f,1.0f);
        WearMark mark{d.sector,{10,d.x,d.y,radius},{(float)(d.seed&0xffff),fill*fill*(3-2*fill),std::min(1.0f,d.strength),0}};
        int room=Rooms_Of(d.sector),seen[9],count=0;
        for(int k=-1;k<8;++k) {
            float x=k<0?d.x:d.x+std::cos(k*0.785f)*radius*1.3f,y=k<0?d.y:d.y+std::sin(k*0.785f)*radius*1.3f;
            const sector_t *at=weatherSectorAt(x,y);
            if(!at)continue;
            int s=int(at-sectors);
            if(Rooms_Outside(s)||Rooms_Of(s)!=room||std::find(seen,seen+count,s)!=seen+count)continue;
            seen[count++]=s;mark.sector=s;out.push_back(mark);
        }
    });
}

// The eight strongest clearings near the eye into c.clearing (x, y,
// radius, how open), the rest zero; only while a bank lies. The hot
// things' standing holes count among them.
void disturbanceUniforms(Uniforms &camera) {
    for(auto &k:camera.clearing)for(float &v:k)v=0;
    if(!bankLies()||(!alive&&hot.empty()))return;
    struct Hole { float x,y,r,open,weight; };
    std::vector<Hole> holes;
    float t=now(),refill=refillSeconds();
    each([&](const Disturbance &d) {
        float r=clearingRadius(d,t-d.born,refill),far=std::hypot(d.x-camera.eye[0],d.y-camera.eye[1]);
        if(r<4||far>2048||d.lying<=0)return;
        float open=std::min(1.0f,d.strength);
        holes.push_back({d.x,d.y,r,open,r*open/(1+far/512)});
    });
    for(const HotSpot &h:hot) {
        float far=std::hypot(h.x-camera.eye[0],h.y-camera.eye[1]),open=0.8f*std::min(1.0f,h.heat);
        holes.push_back({h.x,h.y,h.radius,open,h.radius*open/(1+far/512)});
    }
    std::sort(holes.begin(),holes.end(),[](const Hole &a,const Hole &b){return a.weight>b.weight;});
    for(size_t i=0;i<holes.size()&&i<8;++i) {camera.clearing[i][0]=holes[i].x;camera.clearing[i][1]=holes[i].y;camera.clearing[i][2]=holes[i].r;camera.clearing[i][3]=holes[i].open;}
}

// What lay there thrown up near the eye: as many bits as there was of it,
// the harder and wider the more, out and up from the middle, slowing in
// the air, coming down and lying a moment where they land (all of it gone
// after two and a half seconds). Fog and dry ice roll outward low instead,
// in pale sheets of mist, thinning as they spread. Drawn as the falling
// kind draws what the drafts carry (drawCarried); none through a wall.
void buildDisturbances(const Uniforms &camera) {
    if(!alive)return;
    Weather kind=falling();
    if(kind==WEATHER_NONE)return;
    const WeatherKind &weather=weatherKind(kind);
    bool bank=bankLies();
    const std::array<float,3> mist=bankAlbedo();
    float t=std::floor(gameSeconds()*TICRATE)/TICRATE;
    each([&](const Disturbance &d) {
        float age=t-d.born;
        if(age<0||age>2.5f||d.lying<=0.05f||d.sector<0||d.ground<=0)return;
        if(std::hypot(d.x-camera.eye[0],d.y-camera.eye[1])>900)return;
        bool outside=Rooms_Outside(d.sector);
        int room=Rooms_Of(d.sector);
        float floor=floorZ(&sectors[d.sector]);
        float size=std::min(1.0f,d.radius/96)*d.ground;
        int count=(int)std::lround(std::min(1.0f,d.lying)*std::min(1.5f,d.strength)*(bank?20:32)*size);
        for(int n=0;n<count;++n) {
            uint32_t own=d.seed^(uint32_t)n*0x9e3779b9u;
            float a=hashUnit(n,1,own)*6.2832f,ca=std::cos(a),sa=std::sin(a);
            float speed=d.radius*(bank?0.9f:1.6f)*(0.4f+0.6f*hashUnit(n,2,own))*d.strength;
            constexpr float drag=2.5f;
            float out=speed/drag*(1-std::exp(-drag*age));
            float x=d.x+ca*out,y=d.y+sa*out,z;
            if(bank) z=floor+2+6*hashUnit(n,3,own)+4*age;
            else {
                // Up and out, then down at the kind's settling pace; lying where it lands.
                float up=(60+120*hashUnit(n,3,own))*d.strength,sink=30+20*hashUnit(n,4,own);
                z=std::max(floor,floor+2+up/drag*(1-std::exp(-drag*age))-sink*age);
            }
            const sector_t *at=weatherSectorAt(x,y);
            if(!at)continue;
            int s=int(at-sectors);
            if(Rooms_Outside(s)!=outside||(!outside&&Rooms_Of(s)!=room))continue;   // never through a wall
            float fade=1-std::clamp((age-1.6f)/0.9f,0.0f,1.0f);
            if(bank) {
                float spread=std::min(1.0f,age*2);
                pixelBlock(particleVertices,camera,x,y,z,2.5f+3*spread,0.6f,weatherColor(*at,x,y,z,mist),false,&mist,true,nullptr,0.45f*fade*(1-0.6f*spread));
            }
            else drawCarried(camera,weather,x,y,std::max(z,floorZ(at)+0.5f),*at,own,0.8f*fade,false);
        }
    });
}

// For the FPS readout: the records alive now.
int disturbanceCount() {
    int n=0;
    for(const Disturbance &d:bigOnes)n+=d.alive;
    for(const Disturbance &d:smallOnes)n+=d.alive;
    return n;
}

void disturbancesShutdown() {
    resetRecords();level=~0u;
    disturbanceSourcesShutdown();
}
} // namespace doom3d
