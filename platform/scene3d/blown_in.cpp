/* What blows in (every falling kind with FilmRules; dry ice has its own way
   in, dry_ice_intake.cpp): the weather the wind presses in by the openings
   that open during play. Snow and rain lie as at a broken window, a patch
   in front of the doorway spreading as it soaks (blownInMarks, wear.cpp's
   kind 3); sand and ash lie as the floor film (floor_film.cpp), creeping in
   from the opening the way dry ice's streams do, instead of all at once. What the openings open when the level started let in lies in the
   ground's bake (weather.cpp, as if it had been falling a while), and stays
   as it lay when a door shuts on it.
   - The openings: the drafts' links onto the outside (the sky or a porch)
     that weren't open at the level's start: a door, a lift, a lowered wall;
     for sand and ash (filmPanes) a broken pane too, rain and snow have
     theirs (the sill's patch, the drafts' trails, wear.cpp).
   - How much: the press on it (weatherPressOn: facing the wind, eased where
     the air outside is trapped, eddies for the light kinds) as hard as it
     falls; sand, blowing low, less the higher the sill stands over the
     ground outside and none past its lift.
   - Where: along the drafts' trail from the opening where the air runs on
     through the building, else straight in, half of it fallen out every
     straight units (the kind's); its front creeping in as fast as the wind
     presses it (as the drafts' trails creep), laying the kind's feed a
     second across the stream's width.
   - What flies in: at every doorway the wind presses on (those open since
     the level started too) flakes, drops and grains ride the stream in at
     the wind's speed (buildBlownIn): rushing in a gale, drifting in a light
     air, none in still air; through a draft they ride the draft's trail
     (drafts.cpp), at a window the kind's own.
   - Ash is in the air itself: wherever the outside air gets into a room
     (the drafts' outside share, by the openings' slow exchange too, one
     broken pane enough) it settles over the floor, most where the air is
     still (the kind's indoors rule).
   - The film then lies, moves and goes by the kind's rules: snow sticks
     where it lands and melts by the room's warmth, rain runs off and dries,
     sand keeps its shape and spills off ledges, ash stays.
   Render-only. */
#include "internal.h"
#include "../rooms.h"

namespace doom3d {
namespace {
constexpr float stepLength=24;   // a straight stream's steps
// An opening it comes in by: how far its front has got, how much it lets in.
struct Mouth { float reach=0,rate=0; bool seen=false,laid=false; };
std::unordered_map<int64_t,Mouth> mouths;
std::unordered_set<int64_t> atStart;   // the openings open when the level started: the bake has theirs
// A doorway's stream straight in, for what flies in on it (buildBlownIn):
// its steps, how far its front has got, how fast the wind presses it in
// (units a second), how much it lets in, its sill and lintel, a seed.
struct Stream { std::vector<DraftStep> steps; float reach,speed,rate,sill,lintel; uint32_t seed; };
// How loose what blows in lies (powder.cpp): snow by how cold it is outside
// (-5 C or colder: loose), the rest as its kind lies (-1).
float laidLoose=-1;
// Snow and rain at a doorway opened during play lie as at a broken window:
// a patch in front of it (wear.cpp's kind 3, wear.glsl draws it in ragged
// clumps fanning out past the frame), as far in as the opening is tall,
// spreading as it soaks (the kind's pane time, the harder the wind presses
// in the faster) and going over four minutes once the door shuts or it
// stops. The film laid a creeping sheet instead, the outside's snow
// crawling in. Sand and ash keep the film, as at windows.
struct Patch { float ax=0,ay=0,bx=0,by=0,depth=16,soak=0; int sectors[3]={-1,-1,-1}; bool seen=false; };
std::unordered_map<int64_t,Patch> patches;
std::vector<Stream> streams;
std::vector<char> outside,sealed;
unsigned level=~0u;
Weather lying=WEATHER_NONE;
bool started=false,gathered=false;
int lastTic=-1;
int64_t keyOf(const DraftLinkView &v) {
    return (int64_t)std::lround(v.ax*7+v.ay*13)*1000003+(int64_t)std::lround(v.bx*17+v.by*19);
}
int sectorOf(float x,float y) {const sector_t *s=weatherSectorAt(x,y);return s?int(s-sectors):-1;}
bool indoor(int s) {return !Rooms_Outside(s);}
float creepSpeed(float speed) {return std::clamp(speed*0.8f,4.0f,80.0f);}
uint64_t pairOf(int a,int b) {return a<b?(uint64_t)a<<32|(uint32_t)b:(uint64_t)b<<32|(uint32_t)a;}
// How cold a sector's air is for the lying kind: 0 a heated room, 1 as cold
// as outside (none for what is warmer out than in: sand, ash).
float roomCold(int sector) {
    if(sector<0||sector>=numsectors)return 0;
    if(!indoor(sector))return 1;
    float air=draftTemperature(Rooms_Of(sector)),out=weatherKind(lying).outside;
    if(std::isnan(air)||out>=20)return 0;
    return std::clamp((20-air)/(20-out),0.0f,1.0f);
}
// The stream's steps, across their width, as far as its front has got
// (thinning out over the last 32 units before it): the kind's feed a second
// there, by how much it lets in and how much is left of it. With tongues
// (a seed: what rolls in along the ground, sand) it is laid in a few
// tongues instead of an even band, each its own length (from half to 1.3
// times the front) and the gaps between thin, so it fingers inward from
// the pile at the mouth; seeded by the opening, they hold their place.
void lay(const std::vector<DraftStep> &steps,float front,float feed,uint32_t tongues=0) {
    float along=0,longest=tongues?front*1.3f:front;
    for(const DraftStep &g:steps) {
        float l=std::hypot(g.bx-g.ax,g.by-g.ay);
        if(along>=longest||l<=0)break;
        float nx=-(g.by-g.ay)/l,ny=(g.bx-g.ax)/l;
        int across=std::max(1,(int)std::ceil(2*g.width/16));
        for(float t=8;t<l+0.01f&&along+t<longest;t+=16)for(int k=0;k<=across;++k) {
            float side=g.width*(2.0f*k/across-1),x=g.ax+(g.bx-g.ax)*t/l+nx*side,y=g.ay+(g.by-g.ay)*t/l+ny*side;
            float reach=front,weight=1;
            if(tongues) {float n=valueNoise(side,0,24,tongues);reach=front*(0.5f+0.8f*n);weight=0.3f+1.4f*n*n;}
            if(along+t>reach)continue;
            int s=sectorOf(x,y);
            if(s<0||!indoor(s))continue;
            float f=std::clamp((reach-along-t)/32,0.0f,1.0f),taper=f*f*(3-2*f);
            float edge=1-0.5f*std::fabs(side)/std::max(g.width,1.0f);
            filmFeed(x,y,feed*g.amount*taper*edge*weight,laidLoose);
        }
        along+=l;
    }
}
// Each opening onto the outside this tic, the ones it comes in by fed.
void feedMouths(const WeatherKind &kind) {
    std::vector<DraftPath> trails;draftPaths(trails);
    std::unordered_set<uint64_t> joined;
    std::vector<DraftLinkView> openings;
    for(int i=0;i<draftLinkCount();++i) {
        DraftLinkView v;
        if(!draftLink(i,v))continue;
        if(v.b>=0)joined.insert(pairOf(v.a,v.b));else openings.push_back(v);
    }
    if(!started) {
        // The level's first look: what is open now has been letting it in a while.
        for(const DraftLinkView &v:openings)if(!v.pane)atStart.insert(keyOf(v));
        started=true;
    }
    const float tic=weatherTicSeconds();
    float wind=std::hypot(windNow[0],windNow[1]);
    for(auto &[key,m]:mouths)m.seen=false;
    streams.clear();
    for(const DraftLinkView &v:openings) {
        float mx=(v.ax+v.bx)/2,my=(v.ay+v.by)/2,length=std::hypot(v.bx-v.ax,v.by-v.ay);
        if(length<8||(v.pane&&!kind.filmPanes))continue;
        int64_t key=keyOf(v);
        // Open since the level started: the bake has what the wind lays there,
        // but what lies in the film alone (sand and ash) it lays too, the
        // first time a minute's worth at once (it has been blowing a while).
        bool start=atStart.count(key)>0,lays=kind.filmPanes;
        uint32_t tongues=kind.lift<INFINITY?((uint32_t)(key*2654435761u)|1u):0u;   // sand fingers in
        int out=sectorOf(mx-v.nx*8,my-v.ny*8);
        if(out<0||indoor(out))continue;
        float press=weatherPressOn(v.nx,v.ny,mx,my),climb=v.bottom-floorZ(&sectors[out]);
        float rate=press*fallNow*liftShare(kind,climb);
        if(rate<0.02f)continue;
        Mouth &m=mouths[key];
        m.seen=true;m.rate=rate;
        if(start&&m.reach==0)m.reach=INFINITY;   // it has been coming in a while
        float feed=kind.feed*rate*(start&&!m.laid?60*TICRATE:1.0f);
        m.laid=true;
        m.reach+=creepSpeed(std::max(wind*press*0.5f,8.0f))*creepShare(kind)*tic;   // sand far slower
        if(!start&&!v.pane&&!kind.filmPanes&&(kind.settles||kind.wets)) {
            Patch &pt=patches[key];
            pt.seen=true;pt.ax=v.ax;pt.ay=v.ay;pt.bx=v.bx;pt.by=v.by;
            pt.depth=std::clamp((v.top-v.bottom)*0.45f,16.0f,72.0f);
            pt.soak=std::min(1.0f,pt.soak+fallNow*(0.4f+0.6f*std::min(1.0f,press))*tic/kind.paneSeconds);
            // The door's own sector and the room's just inside it.
            int count=0;
            for(float in:{0.0f,0.5f,1.0f}) {
                int s=in==0?v.sector:sectorOf(mx+v.nx*pt.depth*in,my+v.ny*pt.depth*in);
                if(s<0||!indoor(s)||std::find(pt.sectors,pt.sectors+count,s)!=pt.sectors+count)continue;
                pt.sectors[count++]=s;
            }
        }
        // Along the drafts' trail from it, or straight in.
        bool steered=false;
        for(const DraftPath &t:trails) {
            if(std::fabs(t.ax-v.ax)>0.5f||std::fabs(t.ay-v.ay)>0.5f||std::fabs(t.bx-v.bx)>0.5f||std::fabs(t.by-v.by)>0.5f||t.steps.empty())continue;
            if(lays)lay(t.steps,m.reach,feed,tongues);
            steered=true;   // what flies in rides the draft (buildDrafts)
        }
        if(steered)continue;
        // Straight in it goes the further the harder the wind blows (half again
        // as far for twice the wind), and heavy sand climbs higher steps.
        std::vector<DraftStep> steps;
        float x=mx+v.nx*2,y=my+v.ny*2,travelled=0,amount=1,reach=windCarry(),straight=kind.straight*reach*std::sqrt(reach);
        int from=v.sector;
        for(int n=0;n<24&&amount>=0.05f;++n) {
            float nx=x+v.nx*stepLength,ny=y+v.ny*stepLength;
            int to=sectorOf(nx,ny);
            if(to<0||!indoor(to))break;
            int sa=Rooms_Of(from),sb=Rooms_Of(to);
            if(sa!=sb&&!joined.count(pairOf(sa,sb)))break;   // a wall, a shut door, whole glass
            // Heavy (sand) stops at a step up taller than a quarter of its lift.
            if(floorZ(&sectors[to])-floorZ(&sectors[from])>kind.lift*reach/4)break;
            amount*=std::exp2(-stepLength/straight);
            float half=std::min(64.0f,length*0.4f+travelled*0.2f);
            steps.push_back({x,y,nx,ny,half,amount,floorZ(&sectors[to]),to,from});
            x=nx;y=ny;travelled+=stepLength;from=to;
        }
        static const bool debug=getenv("ROOMSDEBUG")!=nullptr;   // each opening every 10 s
        if(debug&&leveltime%350==0) {
            fprintf(stderr,"BLOWNIN tic %d opening %.0f,%.0f-%.0f,%.0f into %d%s: press %.2f sill %.0f up, lets in %.2f, %zu steps straight in, front %.0f, film",
                    leveltime,v.ax,v.ay,v.bx,v.by,v.sector,v.pane?" (pane)":"",press,climb,rate,steps.size(),m.reach);
            for(const DraftStep &g:steps)fprintf(stderr," %.1f",filmAt(g.bx,g.by));
            fprintf(stderr,"\n");
        }
        if(lays)lay(steps,m.reach,feed,tongues);
        // A doorway's stream carries what flies in (a window has its own, the kind's build).
        if(!v.pane&&!steps.empty())streams.push_back({std::move(steps),m.reach,std::max(wind*press,20.0f),rate,v.bottom,v.top,(uint32_t)(key*2654435761u)});
    }
    // An opening that shut takes its stream with it; what it laid lies on.
    for(auto i=mouths.begin();i!=mouths.end();)i=i->second.seen?std::next(i):mouths.erase(i);
}
// Each tic: a patch whose doorway shut or that it no longer falls on goes
// over four minutes.
void fadePatches(float dt) {
    for(auto i=patches.begin();i!=patches.end();) {
        Patch &pt=i->second;
        if(!pt.seen)pt.soak-=dt/240;
        pt.seen=false;
        i=pt.soak>0?std::next(i):patches.erase(i);
    }
}
}

// Each tic while a kind with a film falls (or lies on after it stops): a
// new level's or kind's film, then what blows in and one step of it.
void watchBlownIn() {
    if(gamestate!=GS_LEVEL||seamCells.size()<(size_t)mapWidth*mapHeight||bakeMap.sectors.size()!=(size_t)numsectors)return;
    Weather kind=falling();
    const WeatherKind &weather=weatherKind(kind);
    if(kind==WEATHER_DRY_ICE||!weather.film)return;   // dry ice's film is its own (dry_ice.cpp)
    if(level!=r_levelserial||lying!=kind||!filmReady(*weather.film)) {
        level=r_levelserial;lying=kind;started=gathered=false;lastTic=-1;
        mouths.clear();atStart.clear();streams.clear();patches.clear();
        Rooms_Build();
        outside.assign(numsectors,0);
        for(int s=0;s<numsectors;++s)outside[s]=(char)Rooms_Outside(s);
        filmLevel(outside,*weather.film);
    }
    if(leveltime==lastTic)return;
    lastTic=leveltime;
    sealed.assign(numlines,0);
    for(int i=0;i<windowPaneCount();++i) {PaneView p;if(windowPane(i,p)&&!p.broken&&p.line>=0&&p.line<numlines)sealed[p.line]=1;}
    const float tic=weatherTicSeconds();
    laidLoose=weather.melts?std::clamp(-climateTemperature(kind)/5,0.0f,1.0f):-1.0f;
    filmClearFallout();
    streams.clear();
    if(fallNow>0.02f&&draftLinkCount()>0) {
        feedMouths(weather);
        // What is in the air settles wherever the outside air gets in: at once
        // the first time (it has been falling a while), then over two minutes,
        // looked at every eight tics.
        if(weather.indoors&&!std::isnan(draftOutsideShare(0))&&(!gathered||leveltime%8==0)) {filmGather(weather.indoors,8*tic,120,!gathered);gathered=true;}
        // The gusts drift what lies loose along each doorway's stream (powder.cpp).
        for(const Stream &s:streams)powderDrift(s.steps,s.reach,s.speed,s.steps.front().ax,s.steps.front().ay,tic);
    }
    fadePatches(tic);
    watchPowder(weather,tic);   // the drafts' powder, and their gusts (powder.cpp)
    filmStep(tic,sealed,roomCold);
    filmUpload();
}
// What flies in on the doorways' streams near the eye: in at the wind's
// speed through the opening, slowing as it goes, sinking from the
// doorway's lower half to the floor, no further in than the stream's front
// has crept, thinning as the stream lets it fall; drawn as the drafts'
// are (drawCarried: the kind's own, or flakes, or drops where it wets).
void buildBlownIn(const Uniforms &camera) {
    if(level!=r_levelserial||fallNow<=0.02f||streams.empty())return;
    const WeatherKind &weather=weatherKind(lying);
    float now=std::floor(gameSeconds()*TICRATE)/TICRATE;
    for(const Stream &s:streams) {
        const DraftStep &first=s.steps.front();
        if(std::hypot(first.ax-camera.eye[0],first.ay-camera.eye[1])>768)continue;
        // Timed on the whole stream, drawn no further in than its front has
        // crept (timed on the crept length they slid back out as it grew).
        float full=s.steps.size()*stepLength,length=std::min(full,s.reach);
        if(length<8)continue;
        // What rolls in along the ground (sand: a finite lift) in a crowd of
        // grains skipping in to where it piles, so it is seen to come in.
        bool rolls=weather.lift<INFINITY;
        int count=std::min(rolls?96:32,(int)std::lround(length/(rolls?5.0f:12.0f)*std::min(1.0f,s.rate*(rolls?4:2))));
        // In at half again the wind's press, slowing to a stop at the far end.
        float cross=std::clamp(2*full/(s.speed*1.5f),0.6f,8.0f);
        for(int n=0;n<count;++n) {
            // Flying for period, then (what settles) lying a moment where it came down.
            float period=cross*(0.8f+0.4f*hashUnit(n,0,s.seed)),rest=weather.settles?1.2f:0.0f,whole=period+rest;
            float clock=now+hashUnit(n,1,s.seed)*whole*5;
            int cycle=(int)std::floor(clock/whole);
            float seconds=clock-cycle*whole,age=std::min(1.0f,seconds/period);
            uint32_t own=s.seed^(uint32_t)cycle*2654435761u;
            float along=full*(1-(1-age)*(1-age))*(0.4f+0.6f*hashUnit(cycle,2,own));
            if(along>length)continue;   // past the front: not that far in yet
            size_t k=std::min(s.steps.size()-1,(size_t)(along/stepLength));
            const DraftStep &g=s.steps[k];
            if(g.amount<0.1f)continue;
            float f=along/stepLength-k,side=(hashUnit(cycle,3,own)-0.5f)*g.width*1.4f;
            float dx=g.bx-g.ax,dy=g.by-g.ay,len=std::max(std::hypot(dx,dy),1e-3f);
            float x=g.ax+dx*f-dy/len*side,y=g.ay+dy*f+dx/len*side;
            int at=sectorOf(x,y);
            if(at<0||!indoor(at))continue;
            float floor=floorZ(&sectors[at]),from=std::min(s.lintel,s.sill+8+56*hashUnit(cycle,4,own));
            float z=floor+(std::min(from,ceilZ(&sectors[at])-2)-floor)*(1-age);
            float alpha=0.75f*std::min(1.0f,g.amount*2);
            if(seconds>=period) {
                if(weather.settles)drawCarried(camera,weather,x,y,floor+0.5f,sectors[at],own,alpha*(1-(seconds-period)/rest),false);
                continue;
            }
            if(z<floor+0.5f)continue;
            // The stream's stir (air_push.cpp) and any blast's shove; never through a wall.
            float turn=0;
            if(k+1<s.steps.size()) {
                const DraftStep &h=s.steps[k+1];
                turn=std::remainder(std::atan2(h.by-h.ay,h.bx-h.ax)-std::atan2(dy,dx),2*(float)doomPi);
            }
            float push[3],stir[3];
            airShift(at,x,y,z,now,now,push);
            airStir(along,full,g.width,dx/len,dy/len,turn,side/std::max(g.width*0.7f,1.0f),now,own,stir);
            for(int i=0;i<3;++i)push[i]+=stir[i]*std::min(1.0f,age*4);
            airKeep(x,y,push,[&](float px,float py){int t=sectorOf(px,py);return t>=0&&indoor(t);});
            x+=push[0];y+=push[1];
            int to=sectorOf(x,y);
            if(to<0)continue;
            z=std::clamp(z+push[2],floorZ(&sectors[to])+0.5f,ceilZ(&sectors[to])-2);
            drawCarried(camera,weather,x,y,z,sectors[to],own,alpha,!weather.settles);
        }
    }
}
// The doorways' patches as wear records (wear.cpp's kind 3, as a broken
// window's): how far in as it has soaked, how white or wet.
void blownInMarks(std::vector<WearMark> &out) {
    if(level!=r_levelserial||lying!=falling())return;
    const WeatherKind &kind=weatherKind(lying);
    for(const auto &[key,pt]:patches) {
        float amount=kind.settles?pt.soak:std::min(1.0f,0.5f+pt.soak);
        for(int s:pt.sectors)if(s>=0)out.push_back({s,{3,pt.depth*(0.25f+0.75f*pt.soak),0,amount},{pt.ax,pt.ay,pt.bx,pt.by}});
    }
}
void blownInShutdown() {filmShutdown();mouths.clear();streams.clear();patches.clear();atStart.clear();outside.clear();sealed.clear();level=~0u;lying=WEATHER_NONE;started=gathered=false;lastTic=-1;}
} // namespace doom3d
