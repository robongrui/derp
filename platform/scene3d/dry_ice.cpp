/* Dry ice (a WeatherKind; weather.cpp says when it lies, fog.cpp carries it
   to the shaders): fog as heavy as it would be on a colder world than ours,
   lying low in a white layer with a crisp top and clear air over it, in
   Doom's sixteen hard light steps (fog_bank.glsl). The sky stays clear.
   - Outside: each sector under the sky or a porch holds a depth of it,
     boiling off slowly, faster where it lies deep. It gathers to a layer
     under the knees only on the low ground (lowness): as heavy as it is it
     lies where the ground is lowest round about, and walkways, ledges and
     rooftops a step or more above stay clear. Across every open line
     between outside sectors it pours from the higher surface to the lower,
     over a step's lip, down stairs. A pit fills to its neighbours' level.
     When the level starts it has been lying a while.
   - Indoors it only comes in where the wind presses it in
     (dry_ice_intake.cpp): at each opening the wind blows on it climbs the
     wall outside in a thin curtain and, where that reaches the sill, comes in as a
     stream creeping in from the opening: a ribbon of the stepped fog along
     the floor (fog.cpp hands it to the shaders), thickest at the opening,
     thinning and fanning out along the way, ragged at its edges, gone the
     sooner the warmer the room. Its front waits at the low spots it meets
     and piles up behind a step up before it flows on over
     (dry_ice_basins.cpp); a strong push skims past them. When the push
     stops it lingers where it lies and fades. What comes in by openings
     already open when the level starts has been coming in a while.
   - What the streams let fall indoors lies as a film on the floor, cell by
     cell (floor_film.cpp): it gathers slowly where it falls, creeps out
     over the level with a front and pours off steps and down stairs, filling
     the basins to their rims; no flat level per sector. It stays only on
     the room's low ground (findRoomLowness): a step and more above it, it
     warms away as it lands, the streams running over it all the same.
   Render-only. */
#include "internal.h"
#include "../rooms.h"
extern "C" {
#include "../audio/sound_options.h"
}

namespace doom3d {
namespace {
constexpr float layer=18;      // the layer open ground gathers: under the knees
constexpr float deep=32;       // past this it boils off fast
constexpr float stream=10;     // a stream's ribbon at its opening, units deep
constexpr float pourSpeed=40;  // outside: units a second it pours over a lip, per unit of head
constexpr float kneeHigh=8;    // indoors: past this the film warms away fast (in a basin: past its rim)
// Its film indoors (floor_film.cpp): runs like a heavy liquid, a yield before
// it creeps; twelve seconds in a heated room, sixty times that in the cold;
// 32 units deep for the alpha's full range.
const FilmRules dryIceFilm={1.5f,6,0.5f,0,kneeHigh,12,60,32,false};
float filmLimit(int sector);
// Per sector: how deep it lies outside (indoors none: the film lies per
// cell, floor_film.cpp) and the floor's area. Per line: whether whole
// glass shuts it.
std::vector<float> depth,area;
std::vector<char> sealed,outside;   // outside: under the sky or a porch (the room map)
std::vector<std::array<float,2>> center;   // per sector: the middle of its floor (its map cells')
std::vector<float> lowness;         // per sector: 1 on the low ground round about (indoors: of its room), 0 a step and more above it
std::vector<DraftPath> paths;       // the streams this tic (dry_ice_intake.cpp)
std::vector<BankRibbon> ribbons,updrafts;   // the streams' ribbons and the updrafts outside, for the shaders
DryIceCourse course;                // one stream's course, while its ribbons are made
unsigned level=~0u,sealedSerial=~0u,sealedBreaks=~0u;
int lastTic=-1;
bool lying() {return falling()==WEATHER_DRY_ICE;}
float smooth01(float t) {t=std::clamp(t,0.0f,1.0f);return t*t*(3-2*t);}

// How low each outside sector lies: against the ground round about, the
// floor under which the lowest tenth of the open floor within 640 units of
// its middle lies (a small pit doesn't pull it down, rooftops and walkways
// don't lift it), all of it there or below, none from 24 units (a step)
// above, between them smoothly. On a map whose streets all join, the lowest
// floor of all would have left every street bare for one pit.
void findLowness() {
    lowness.assign(numsectors,0);
    constexpr float reach=640,lowest=0.1f,full=8,none=24;
    std::vector<int> count(numsectors,0),touched;
    int span=(int)std::ceil(reach/mapCell);
    for(int s=0;s<numsectors;++s) {
        if(!outside[s]||area[s]<=0)continue;
        int cx=(int)std::floor((center[s][0]-mapOrigin[0])/mapCell),cy=(int)std::floor((center[s][1]-mapOrigin[1])/mapCell);
        int total=0;touched.clear();
        for(int y=std::max(0,cy-span);y<=std::min(mapHeight-1,cy+span);++y)
            for(int x=std::max(0,cx-span);x<=std::min(mapWidth-1,cx+span);++x) {
                if((x-cx)*(x-cx)+(y-cy)*(y-cy)>span*span)continue;
                int own=seamCells[(size_t)y*mapWidth+x].own;
                if(own==noSector||!outside[own]||sectors[own].floorpic==skyflatnum)continue;
                if(count[own]++==0)touched.push_back(own);
                ++total;
            }
        std::sort(touched.begin(),touched.end(),[](int a,int b){return floorZ(&sectors[a])<floorZ(&sectors[b]);});
        float ground=floorZ(&sectors[s]);
        for(int seen=0,k=0;k<(int)touched.size();++k) {
            seen+=count[touched[k]];
            if(seen>=lowest*total) {ground=floorZ(&sectors[touched[k]]);break;}
        }
        for(int t:touched)count[t]=0;
        lowness[s]=1-smooth01((floorZ(&sectors[s])-ground-full)/(none-full));
    }
}
// The same indoors, room by room: the floor under which the lowest tenth
// of the room's floor lies, its liquids left out (a pool is no floor to lie
// on). The streams from windows on a dais kept its film lying there and
// down the steps, over the floor round a pool below.
void findRoomLowness() {
    constexpr float lowest=0.1f,full=8,none=24;
    std::vector<std::vector<int>> rooms(std::max(0,Rooms_Count()));
    for(int s=0;s<numsectors;++s) {
        int r=Rooms_Of(s);
        if(outside[s]||r<0||r>=(int)rooms.size()||sectors[s].floorpic==skyflatnum)continue;
        rooms[r].push_back(s);
    }
    for(auto &room:rooms) {
        std::vector<int> ground;
        for(int s:room)if(!((size_t)s<sectorMist.size()&&sectorMist[s].liquid))ground.push_back(s);
        if(ground.empty())ground=room;
        std::sort(ground.begin(),ground.end(),[](int a,int b){return floorZ(&sectors[a])<floorZ(&sectors[b]);});
        float total=0,seen=0;
        for(int s:ground)total+=area[s];
        float level=ground.empty()?0:floorZ(&sectors[ground.back()]);
        for(int s:ground) {seen+=area[s];if(seen>=lowest*total) {level=floorZ(&sectors[s]);break;}}
        for(int s:room)lowness[s]=1-smooth01((floorZ(&sectors[s])-level-full)/(none-full));
    }
}
// Past this the film warms away fast (floor_film.cpp): knee high on the
// room's low ground, nothing a step and more above it, a basin's rim in one.
float filmLimit(int sector) {return std::max(kneeHigh*lowness[sector],dryIceBasinRim(sector));}
// The panes: which lines whole glass shuts.
void findPanes() {
    sealed.assign(numlines,0);
    for(int i=0;i<windowPaneCount();++i) {
        PaneView p;
        if(windowPane(i,p)&&!p.broken&&p.line>=0&&p.line<numlines)sealed[p.line]=1;
    }
    sealedSerial=windowPaneSerial();sealedBreaks=windowPaneBreaks();
}
// How deep a stream lies at a point along its trail: at most stream units
// at the opening, as much as the trail carries there, lingering once it
// stops; tapering to nothing over the 32 units behind its front; deeper
// where it piles up behind a step (rising over the pile's length to its top
// at the step); thinner where a strong draft has carried it over one.
float streamDepth(const DraftPath &path,float along,float amount,float thin) {
    float d=stream*std::min(1.0f,amount*2)*path.linger*smooth01((course.front-along)/32)*thin;
    for(const auto &pile:course.piles)
        if(along<=pile[0]+0.5f&&along>pile[0]-dryIcePileLength())d=std::max(d,pile[1]*path.linger*smooth01(1-(pile[0]-along)/dryIcePileLength()));
    return d;
}
// The streams this tic, where the wind presses it in (dry_ice_intake.cpp,
// which also raises the updrafts outside): each one's course (dry_ice_basins.cpp), its
// ribbon as far as the front has got, a record a step in each sector it
// touches (from its start to its end, half widths and depths; a step it
// piles up behind split there, the pile on the lower side) and what it lets
// fall a second on the floor it has reached (the film, floor_film.cpp).
void findStreams() {
    dryIceIntake(paths,updrafts);
    ribbons.clear();
    filmClearFallout();
    for(const DraftPath &path:paths) {
        dryIceCourse(path,stream,course);
        dryIceFilmFallout(path,course,stream);
        float along=0;
        for(size_t k=0;k<path.steps.size();++k) {
            const DraftStep &g=path.steps[k];
            float l=std::hypot(g.bx-g.ax,g.by-g.ay);
            if(along>=course.front||l<=0)break;
            float end=std::min(along+l,course.front),split=NAN,thin=course.thin[k];
            for(const auto &pile:course.piles)if(pile[0]>along&&pile[0]<along+l)split=pile[0];
            auto at=[&](float t,float &x,float &y) {x=g.ax+(g.bx-g.ax)*(t-along)/l;y=g.ay+(g.by-g.ay)*(t-along)/l;};
            // In every sector the ribbon's middle or edges pass over (a frame,
            // a doorway's strip, a step it crosses), or the stream breaks off
            // there; a pile only on its low side, behind its step.
            auto lay=[&](float t0,float t1,int first,int second,bool over) {
                float d0=streamDepth(path,t0,g.amount,thin),d1=streamDepth(path,t1,g.amount,thin);
                if(d0<0.5f&&d1<0.5f)return;
                BankRibbon r{first,{0,0,0,0},{g.width*1.2f,g.width*1.2f,d0,d1}};
                at(t0,r.a[0],r.a[1]);at(t1,r.a[2],r.a[3]);
                int under[16],count=0;
                auto add=[&](int s) {
                    if(s<0||s>=numsectors||dryIceIsland(s))return;
                    for(int k=0;k<count;++k)if(under[k]==s)return;
                    if(count<16)under[count++]=s;
                };
                add(first);add(second);
                if(over) {
                    float dx=r.a[2]-r.a[0],dy=r.a[3]-r.a[1],length=std::hypot(dx,dy),half=g.width*1.2f;
                    float nx=length>0?-dy/length:0,ny=length>0?dx/length:0;
                    int samples=std::max(1,(int)std::ceil(length/(2*mapCell)));
                    for(int k=0;k<=samples;++k)for(float side:{-1.0f,0.0f,1.0f}) {
                        float t=(float)k/samples;
                        const sector_t *at=weatherSectorAt(r.a[0]+dx*t+nx*half*side,r.a[1]+dy*t+ny*half*side);
                        if(at)add(int(at-sectors));
                    }
                }
                for(int k=0;k<count;++k) {r.sector=under[k];ribbons.push_back(r);}
            };
            if(std::isnan(split))lay(along,end,g.from,g.sector,true);
            else {
                lay(along,std::min(split,end),g.from,g.from,false);
                if(end>split)lay(split,end,g.sector,g.sector,true);
            }
            along+=l;
        }
    }
    ribbons.insert(ribbons.end(),updrafts.begin(),updrafts.end());
    ribbons.erase(std::remove_if(ribbons.begin(),ribbons.end(),[](const BankRibbon &r){return r.sector<0||r.sector>=numsectors;}),ribbons.end());
}
// One step of dt seconds. First each outside sector gathers it toward its
// area's level or the layer (three units a second; a porch's plain layer
// four fifths of it) and boils
// it off (over a minute and a half, eight seconds past deep); indoors the
// film gathers what the streams let fall, runs and warms away
// (floor_film.cpp). Then outside it pours across the open lines.
void step(float dt) {
    filmStep(dt,sealed,dryIceCold);
    for(int s=0;s<numsectors;++s) {
        if(!outside[s]) {depth[s]=0;continue;}
        const sector_t &sector=sectors[s];
        float room=ceilZ(&sector)-floorZ(&sector)-2;
        if(sector.floorpic==skyflatnum||room<=0) {depth[s]=0;continue;}
        float want=(underSky(s)?1.0f:0.8f)*layer*lowness[s];   // only on the low ground
        if(depth[s]<want)depth[s]=std::min(want,depth[s]+3*dt);
        depth[s]-=depth[s]*dt/90+std::max(0.0f,depth[s]-std::max(deep,want))*dt/8;
        depth[s]=std::clamp(depth[s],0.0f,room);
    }
    dryIcePour(dt,area,sealed,outside,depth);
}
}

// Each tic while it lies: a new level's sectors and lines, then the streams
// and one step; the level's first tic settles it over a minute first.
void watchDryIce() {
    if(gamestate!=GS_LEVEL||levelSerial!=r_levelserial||seamCells.empty()||!lying())return;
    if(level!=r_levelserial||depth.size()!=(size_t)numsectors||!filmReady(dryIceFilm)) {   // a new level, or the film was another kind's
        level=r_levelserial;lastTic=-1;
        depth.assign(numsectors,0);area.assign(numsectors,0);
        center.assign(numsectors,{0,0});
        for(size_t i=0;i<seamCells.size();++i) {
            int own=seamCells[i].own;
            if(own==noSector)continue;
            area[own]+=mapCell*mapCell;
            center[own][0]+=mapOrigin[0]+(i%mapWidth+0.5f)*mapCell;center[own][1]+=mapOrigin[1]+(i/mapWidth+0.5f)*mapCell;
        }
        for(int s=0;s<numsectors;++s) {float n=area[s]/(mapCell*mapCell);if(n>0) {center[s][0]/=n;center[s][1]/=n;}}
        for(float &a:area)a=std::max(a,64.0f);
        outside.assign(numsectors,0);
        for(int s=0;s<numsectors;++s)outside[s]=(char)Rooms_Outside(s);
        findLowness();findRoomLowness();
        findPanes();
        findDryIceBasins(area);
        filmLevel(outside,dryIceFilm,filmLimit);
    }
    if(leveltime==lastTic)return;
    bool first=lastTic<0;
    lastTic=leveltime;
    if(sealedSerial!=windowPaneSerial()||sealedBreaks!=windowPaneBreaks())findPanes();   // a new level's panes, or one broke
    findStreams();
    if(first)for(int n=0;n<240;++n)step(0.25f);
#ifdef DOOM_DEVTOOLS
    // Test runs: DRYICESPEED=N lets N tics of it pass a tic, to see minutes of it settle in seconds.
    static const int speed=getenv("DRYICESPEED")?std::max(1,atoi(getenv("DRYICESPEED"))):1;
#else
    constexpr int speed=1;
#endif
    for(int n=0;n<speed;++n)step(weatherTicSeconds());
    filmUpload();
}
// Across every open line (whole glass shuts one) between two outside
// sectors: from the higher surface to the lower, between the higher floor
// and the lower ceiling, as fast as the head over the lip allows, never
// more than half way to level (or past the lip) in one step.
void dryIcePour(float dt,const std::vector<float> &area,const std::vector<char> &sealed,const std::vector<char> &outside,std::vector<float> &depth) {
    for(int i=0;i<numlines;++i) {
        const line_t &line=lines[i];
        if(!line.frontsector||!line.backsector||line.frontsector==line.backsector||sealed[i])continue;
        int f=int(line.frontsector-sectors),k=int(line.backsector-sectors);
        if(!outside[f]||!outside[k])continue;
        float ff=floorZ(line.frontsector),fk=floorZ(line.backsector);
        float bottom=std::max(ff,fk),top=std::min(ceilZ(line.frontsector),ceilZ(line.backsector));
        float sf=ff+depth[f],sk=fk+depth[k];
        int a=sf>=sk?f:k,b=a==f?k:f;
        float sa=std::max(sf,sk),sb=std::min(sf,sk);
        float head=std::min(sa,top)-std::max(bottom,sb);
        if(top-bottom<2||head<=0.05f)continue;
        float len=std::hypot(units(line.v2->x)-units(line.v1->x),units(line.v2->y)-units(line.v1->y));
        if(len<1)continue;
        float level=(sa-sb)*area[a]*area[b]/(area[a]+area[b]);
        float most=sb>=bottom?level:std::min((sa-bottom)*area[a],level);
        float v=std::min({pourSpeed*len*std::min(head,24.0f)*dt,0.5f*most,depth[a]*area[a]});
        if(v<=0)continue;
        depth[a]-=v/area[a];depth[b]+=v/area[b];
    }
}
bool dryIceReady() {return lying()&&level==r_levelserial&&lastTic>=0&&depth.size()==(size_t)numsectors;}
float dryIceTop(int s) {return floorZ(&sectors[s])+depth[s];}
float dryIceShare(int s) {return std::clamp(depth[s]/3,0.0f,1.0f);}
float dryIceOpenTop(float x,float y,float z) {
    if(!dryIceReady()||center.size()!=(size_t)numsectors)return NAN;
    float top=NAN,nearest=1536;
    for(int s=0;s<numsectors;++s) {
        float d=std::hypot(center[s][0]-x,center[s][1]-y);
        if(!outside[s]||depth[s]<1||d>=nearest||floorZ(&sectors[s])>z)continue;   // not a rooftop over the eye
        nearest=d;top=floorZ(&sectors[s])+depth[s];
    }
    return top;
}
const std::vector<BankRibbon> &dryIceRibbons() {static const std::vector<BankRibbon> none;return dryIceReady()?ribbons:none;}

void dryIceShutdown() {dryIceBasinsShutdown();filmShutdown();dryIceIntakeShutdown();updrafts.clear();depth.clear();area.clear();sealed.clear();center.clear();lowness.clear();paths.clear();ribbons.clear();level=sealedSerial=~0u;lastTic=-1;}

namespace {
// Dry ice is dry: nothing soaks.
float dryGround(const WeatherGround &) {return 0;}
// It hushes the open air a little.
void dryHush(float amount) {I_AmbienceSnow(amount*0.35f);}
}
// Dry ice settles on nothing and wets nothing; its glass stays clear. It is
// bitter cold out in it (the drafts carry the cold in, rime round the
// openings it comes in by); it lays no trails, it streams along them.
const WeatherKind dryIceWeather={"dry ice",0.5f,40,60,false,dryGround,nullptr,dryHush,nullptr,-20,false,nullptr,bankAlbedo,false,false};
} // namespace doom3d
