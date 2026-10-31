/* Dry ice's way in (dry_ice.cpp): the wind presses the outside's knee-high
   layer into the buildings at the openings it blows on. The wind is steady,
   so where it comes in and how much is steady too.
   - Each opening onto the outside (the sky or a porch: the drafts' links,
     drafts.cpp) feels the wind's push on it: full facing straight into the
     wind, none along or away from it, eased by half where the air outside
     is trapped. Under a window (only: under a doorway up a step there is
     no wall to climb) a thin curtain of it climbs the wall, up to
     96 units over the layer at a full push, to the sill where it reaches
     it: an updraft (drawn as a ribbon of the bank outside along the wall).
     A doorway at the layer's level just takes it in. Where it reaches the sill
     it comes in, as much as the push and the headroom left let it; a
     doorway at the layer's own level lets a little in by its weight, wind
     or none. One opening is enough: the air it pushes aside leaves over it.
   - In it goes as a stream from the opening (dry_ice.cpp lays its ribbon,
     dry_ice_basins.cpp walks its course, floor_film.cpp lets it fall
     as the film): straight in, or along the drafts' own trail where the air
     runs on through the building. Its front creeps in as fast as it is
     pushed; when the push stops (a door shuts) it lingers and fades.
   - It thins out as it goes, the faster the warmer the room's air (the
     drafts' temperatures): in a heated room it is gone a few steps in, in
     a room the cold has got into it runs on far. The film it leaves lasts
     the same way (dryIceCold).
   Render-only. */
#include "internal.h"
#include "../rooms.h"

namespace doom3d {
namespace {
constexpr float lift=96;          // how high a full push sends it up an outside wall (a thin curtain, not a wave)
constexpr float stepLength=24;    // a straight stream's steps
constexpr float trickle=0.15f;    // what a doorway at the layer's level lets in by its weight alone
// An opening it comes in by: how far the stream's front has got, how much
// it lets in (kept while it lingers), and 1 while it does, fading after.
struct Mouth { float reach=0,rate=0,linger=0; bool open=false,seen=false; };
std::unordered_map<int64_t,Mouth> mouths;
unsigned level=~0u;
bool settled=false;   // the level's openings already open have been letting it in a while
int64_t keyOf(const DraftLinkView &v) {
    return (int64_t)std::lround(v.ax*7+v.ay*13)*1000003+(int64_t)std::lround(v.bx*17+v.by*19);
}
int sectorOf(float x,float y) {const sector_t *s=weatherSectorAt(x,y);return s?int(s-sectors):-1;}
bool indoor(int s) {return !Rooms_Outside(s);}
// How fast its front creeps in at a push's speed (as the drafts' trails creep, drafts.cpp).
float creepSpeed(float speed) {return std::clamp(speed*0.8f,4.0f,80.0f);}
// Whether a sector is a window's frame (rooms.c finds them in the geometry).
bool windowFrame(int sector) {
    for(int i=0;i<Rooms_FrameCount();++i)if(Rooms_Frame(i)->sector==sector)return true;
    return false;
}
uint64_t pairOf(int a,int b) {return a<b?(uint64_t)a<<32|(uint32_t)b:(uint64_t)b<<32|(uint32_t)a;}
// How far it gets in a sector's air before half of it is gone: 32 units in a
// heated room, 16 times that as cold as outside.
float halfway(int sector) {return 32*std::pow(16.0f,dryIceCold(sector));}
}

float dryIceCold(int sector) {
    if(sector<0||sector>=numsectors)return 0;
    if(!indoor(sector))return 1;
    float air=draftTemperature(Rooms_Of(sector));
    if(std::isnan(air))return 0;
    float outside=weatherKind(WEATHER_DRY_ICE).outside;
    return std::clamp((20-air)/(20-outside),0.0f,1.0f);
}

void dryIceIntake(std::vector<DraftPath> &streams,std::vector<BankRibbon> &updrafts) {
    streams.clear();updrafts.clear();
    if(level!=r_levelserial) {level=r_levelserial;mouths.clear();settled=false;}
    if(draftLinkCount()==0)return;
    std::vector<DraftPath> trails;draftPaths(trails);
    std::unordered_set<uint64_t> joined;   // spaces open to each other now
    std::vector<DraftLinkView> openings;   // and onto the outside
    for(int i=0;i<draftLinkCount();++i) {
        DraftLinkView v;
        if(!draftLink(i,v))continue;
        if(v.b>=0)joined.insert(pairOf(v.a,v.b));else openings.push_back(v);
    }
    const float tic=weatherTicSeconds();
    float wind=std::hypot(windNow[0],windNow[1]);
    for(auto &[key,m]:mouths)m.open=m.seen=false;
    for(const DraftLinkView &v:openings) {
        float mx=(v.ax+v.bx)/2,my=(v.ay+v.by)/2,length=std::hypot(v.bx-v.ax,v.by-v.ay);
        if(length<8)continue;
        int out=sectorOf(mx-v.nx*8,my-v.ny*8);
        if(out<0||indoor(out))continue;
        // The push: how squarely the opening faces into the wind (as snow
        // blows in, weather.cpp), eased by half where the air outside is
        // trapped (the drafts ease their pressure further: a walled yard's
        // window feels little draft, but the layer is still driven at it).
        // And how far the sill stands over the layer outside.
        float push=windPress(v.nx,v.ny,mx,my);   // the same press every weather's openings read (weather.cpp)
        float top=dryIceTop(out),climb=v.bottom-top,reach=lift*push;
        float rate=climb<=0?std::max(push,trickle):climb<reach?push*(1-0.5f*climb/reach):0.0f;
        static const bool debug=getenv("ROOMSDEBUG")!=nullptr;   // each opening every 10 s
        if(debug&&leveltime%350==0)
            fprintf(stderr,"DRYICE tic %d opening %.0f,%.0f-%.0f,%.0f into %d from %d: push %.2f sill %.0f over the layer, lets in %.2f\n",
                    leveltime,v.ax,v.ay,v.bx,v.by,v.sector,out,push,climb,rate);
        int64_t key=keyOf(v);
        auto found=mouths.find(key);
        if(rate<0.02f&&found==mouths.end())continue;
        Mouth &m=mouths[key];
        m.open=rate>=0.02f;m.seen=true;
        float speed=std::max(wind*push*0.5f,8.0f),cold=dryIceCold(v.sector);
        if(m.open) {
            if(!settled&&!v.pane&&m.reach==0)m.reach=INFINITY;
            m.reach+=creepSpeed(speed)*tic;m.rate=rate;m.linger=1;
        } else m.linger=std::max(0.0f,m.linger-tic/(20+220*cold));
        if(m.linger<=0) {mouths.erase(key);continue;}
        // The updraft outside, as long as the wind presses it in: a thin
        // curtain up the wall to the sill, only under a window (a frame, the
        // room map's, glass or none): under a doorway up a step there is no
        // wall, and a curtain hung in the open reads as a block.
        if(m.open&&climb>=8&&windowFrame(v.sector)) {
            float layer=top-floorZ(&sectors[out]),curtain=layer+(std::min(climb,lift*push)+2)*std::min(1.0f,rate*3);
            updrafts.push_back({out,{v.ax-v.nx*4,v.ay-v.ny*4,v.bx-v.nx*4,v.by-v.ny*4},{6,6,curtain,curtain}});
        }
        // The stream: along the drafts' trails from this opening, or straight in.
        DraftPath base{(int)(key&0x7fffffff),v.bottom,v.ax,v.ay,v.bx,v.by,m.reach,1,m.linger,speed,creepSpeed(speed),{}};
        base.flowing=m.open;
        bool steered=false;
        for(const DraftPath &t:trails) {
            if(std::fabs(t.ax-v.ax)>0.5f||std::fabs(t.ay-v.ay)>0.5f||std::fabs(t.bx-v.bx)>0.5f||std::fabs(t.by-v.by)>0.5f||t.steps.empty())continue;
            DraftPath path=base;path.steps=t.steps;
            float amount=m.rate;
            for(DraftStep &g:path.steps) {
                amount*=std::exp2(-std::hypot(g.bx-g.ax,g.by-g.ay)/halfway(g.sector));
                g.amount=amount;
            }
            streams.push_back(std::move(path));steered=true;
        }
        if(steered)continue;
        DraftPath path=base;
        float x=mx+v.nx*2,y=my+v.ny*2,travelled=0,amount=m.rate;
        int from=v.sector;
        for(int n=0;n<24&&amount>=0.05f;++n) {
            float nx=x+v.nx*stepLength,ny=y+v.ny*stepLength;
            int to=sectorOf(nx,ny);
            if(to<0||!indoor(to))break;
            int sa=Rooms_Of(from),sb=Rooms_Of(to);
            if(sa!=sb&&!joined.count(pairOf(sa,sb)))break;   // a wall, a shut door, whole glass
            amount*=std::exp2(-stepLength/halfway(to));
            float half=std::min(64.0f,length*0.4f+travelled*0.2f);
            path.steps.push_back({x,y,nx,ny,half,amount,floorZ(&sectors[to]),to,from});
            x=nx;y=ny;travelled+=stepLength;from=to;
        }
        if(!path.steps.empty())streams.push_back(std::move(path));
    }
    // An opening that shut (a door, its link gone) takes its stream with it.
    for(auto i=mouths.begin();i!=mouths.end();)i=i->second.seen?std::next(i):mouths.erase(i);
    settled=true;
}
void dryIceIntakeShutdown() {mouths.clear();level=~0u;settled=false;}
} // namespace doom3d
