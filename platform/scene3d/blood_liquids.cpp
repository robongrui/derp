/* Blood landing in liquids (PLAN-skin-suit.md 10.4): it never lies on them as
   a decal. In water a drop makes a cloud of colour in it, chunky specks on
   the surface spreading and thinning over a quarter of a minute, a cloud
   already there grown by what lands in it (a body bleeding into water clouds
   round it); in nukage and slime a dark swirl swallowed in a second or two;
   on lava a hiss and a puff of steam, nothing left; in a pool of blood
   nothing more. Liquid effects stay simple 2D, chunky pixels stepped at the
   tic rate. Drawing only. */
#include "internal.h"
extern "C" {
#include "../audio/impact_synth.h"
#include "../audio/sound_options.h"
}

namespace doom3d {
struct Cloud { float x,y,born,size,last;int sector,tint;bool water;uint32_t seed; };
struct Steam { float x,y,z,born;uint32_t seed; };
static std::vector<Cloud> clouds;
static std::vector<Steam> steams;
static unsigned cloudSerial;

static void checkLevel() {
    if(cloudSerial==r_levelserial)return;
    clouds.clear();steams.clear();cloudSerial=r_levelserial;
}

void bloodLiquidLand(float x,float y,int sector,float splat,float born,int tint) {
    if(sector<0||sector>=numsectors||tint==BLOOD_NONE)return;
    checkLevel();
    int look=liquidLook(sectors[sector].floorpic);
    if(look==LIQUID_LOOK_BLOOD||look==LIQUID_LOOK_NONE)return;   // already blood (or no liquid at all)
    if(look==LIQUID_LOOK_HOT) {
        // A hiss and a little steam where it lands, nothing left.
        if(steams.size()>=48)steams.erase(steams.begin());
        steams.push_back({x,y,floorZ(&sectors[sector]),born,(uint32_t)(randomRange(0,1)*4294967000.0f)});
        if(splat>=8&&randomRange(0,1)<0.4f)I_MadeSound(IMPACT_SPARK_FIZZ,x,y,floorZ(&sectors[sector]));
        return;
    }
    bool water=look==LIQUID_LOOK_WATER;
    float amount=std::max(2.0f,splat);
    // Into a cloud already there, if it lands in it: that one grows.
    for(Cloud &c:clouds)
        if(c.sector==sector&&c.tint==tint&&std::hypot(x-c.x,y-c.y)<c.size*0.6f+3) {
            c.size=std::min(water?48.0f:20.0f,c.size+amount*0.18f);c.last=std::max(c.last,born);
            return;
        }
    if(clouds.size()>=96)clouds.erase(clouds.begin());
    clouds.push_back({x,y,born,std::clamp(amount*0.7f,4.0f,water?40.0f:14.0f),born,sector,tint,water,(uint32_t)(randomRange(0,1)*4294967000.0f)});
}

void buildBloodLiquids(const Uniforms &camera) {
    checkLevel();
    float now=gameSeconds();
    // Water: spreads to its size over three seconds then on by half again, thinning out over 18 seconds from
    // the last blood it took; thick liquids swallow it in 1.5 seconds.
    clouds.erase(std::remove_if(clouds.begin(),clouds.end(),[&](const Cloud &c) {
        return now-c.last>(c.water?18.0f:1.5f)||now<c.born-0.1f;
    }),clouds.end());
    for(const Cloud &c:clouds) {
        if(now<c.born)continue;
        float age=std::floor((now-c.born)*TICRATE)/TICRATE,fresh=std::floor((now-c.last)*TICRATE)/TICRATE;
        float life=c.water?18.0f:1.5f,left=1-fresh/life;
        float radius=c.size*(c.water?std::min(1.5f,0.4f+0.6f*age/3+0.5f*std::max(0.0f,age-3)/15):0.5f+0.5f*std::min(1.0f,age/1.2f));
        const sector_t &sector=sectors[c.sector];
        float z=floorZ(&sector)+decalLift(c.x,c.y,camera)+0.15f,light=0.06f+1.1f*lighting(&sector);
        auto colour=bloodColor(c.water?0.6f:0.12f,c.tint);
        std::array<float,3> lit;for(int k=0;k<3;++k)lit[k]=std::min(1.0f,colour[k]*light);
        int specks=std::clamp((int)(radius*radius*(c.water?0.9f:0.7f)),8,400);
        float keep=c.water?0.25f+0.6f*left:left;   // the share still shown: thinning, never a hard-edged patch
        for(int n=0;n<specks;++n) {
            if(hashUnit(n,3,c.seed)>keep)continue;
            float a=hashUnit(n,0,c.seed)*2*doomPi,d=radius*std::sqrt(hashUnit(n,1,c.seed));
            // Not round: a swirl, turning slowly with age (thick liquids faster, as they swallow it).
            a+=d/std::max(1.0f,radius)*(c.water?0.8f:2.0f)+age*(c.water?0.05f:1.2f);
            float px=c.x+std::cos(a)*d,py=c.y+std::sin(a)*d;
            pixelBlock(particleVertices,camera,px,py,z,1.6f,0.0001f,lit,true,nullptr,true,nullptr,1,c.water?0.55f+0.3f*left:0.8f);
        }
    }
    // Steam off lava, smoke off a burn: a few pale specks rising and thinning over a second, in the light
    // where they are (lava lights its own; a burn's in a dark room stood out at a fixed grey, the user).
    steams.erase(std::remove_if(steams.begin(),steams.end(),[&](const Steam &s){return now-s.born>1.0f||now<s.born-0.1f;}),steams.end());
    for(const Steam &s:steams) {
        if(now<s.born)continue;
        float age=std::floor((now-s.born)*TICRATE)/TICRATE;
        std::array<float,3> pale={0.55f*(1-age),0.52f*(1-age),0.5f*(1-age)};
        auto lit=particleColour(pale,s.x,s.y,s.z,false);
        for(int n=0;n<6;++n) {
            if(hashUnit(n,2,s.seed)<age)continue;
            float a=hashUnit(n,0,s.seed)*2*doomPi,d=2+4*hashUnit(n,1,s.seed)*age;
            pixelBlock(particleVertices,camera,s.x+std::cos(a)*d,s.y+std::sin(a)*d,s.z+2+18*age*(0.6f+0.4f*hashUnit(n,3,s.seed)),
                       1.2f,1,lit,false,&pale,false,nullptr,1,0.5f);   // lit as a wall: no palette snap (impacts.cpp)
        }
    }
}
// A puff of steam or smoke at (x, y, z) (a burn's, lava's).
void bloodSteamAt(float x,float y,float z) {
    checkLevel();
    if(steams.size()>=48)steams.erase(steams.begin());
    steams.push_back({x,y,z-2,gameSeconds(),(uint32_t)(randomRange(0,1)*4294967000.0f)});
}
void bloodLiquidsShutdown() {clouds.clear();steams.clear();}
} // namespace doom3d
