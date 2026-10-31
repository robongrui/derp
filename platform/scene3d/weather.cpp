/* Weather: the sky over the level. At its start this places the sun
   (sun.cpp) and raises the wind (wind.cpp); each tic it steps the wind and
   whatever falls under the sky (settings.weather), one kind at a time, each
   in its own file (rain.cpp, snow.cpp, nukage_rain.cpp, sand.cpp, ash.cpp,
   and fog.cpp, which lies instead) described by a WeatherKind. Whether
   a map rains or snows is authored, not derived: the episode's mood says
   so (levelMood, Game_Mood), or the player asks for one kind everywhere. It
   falls from the start to the end of the map, driven slant by the wind. Through a
   broken window facing the wind it blows in, and the floor and wall under
   the window soak or whiten (wear.cpp). The ground under the sky lies wet
   or white in steps baked once (bakeWetness, each kind's ground rule), the
   same all map long. The glass draws its drops or frost itself
   (glass.frag), the ground its wetness or snow (wear.glsl), the far sky its
   own (sky.frag), the ambience its sound. Render-only, with its own random
   numbers. */
#include "internal.h"
#include "../rooms.h"

namespace doom3d {
float fallNow=0,wetNow=0,glassWetNow=0;
namespace {
// Every kind by its Weather number; none for WEATHER_NONE.
const WeatherKind *const weatherKinds[WEATHER_KINDS]={nullptr,&rainWeather,&snowWeather,&nukageWeather,&sandWeather,&ashWeather,&fogWeather,&dryIceWeather};
// Dry: nothing falls, nothing lies, nothing sounds.
float dryGround(const WeatherGround &) {return 0;}
const WeatherKind dryWeather={"dry",4.5f,6,8,false,dryGround,nullptr,nullptr};
}
// What falls on this level: F4 Weather is Clear, the map's own (its
// DERPINFO, else the authored episode's look, as the player's temperature
// and wind let it: climate.cpp), then one choice per kind from Rain on.
Weather falling() {
    if(Game_Phobos()) return WEATHER_NONE;   // no air on Phobos: nothing falls from its sky
    if(!levelSky) return WEATHER_NONE;       // no sky, no outside (the library): no weather
    Weather kind=WEATHER_NONE;
    if(settings.weather>=2&&settings.weather-1<WEATHER_KINDS) kind=(Weather)(settings.weather-1);
    else if(settings.weather==1&&DerpInfo_Map.weather>=0&&DerpInfo_Map.weather<WEATHER_KINDS) kind=(Weather)DerpInfo_Map.weather;
    else if(settings.weather==1) kind=levelMood().snow?WEATHER_SNOW:levelMood().rain?WEATHER_RAIN:WEATHER_NONE;
    if(settings.weather==1) kind=climateWeather(kind);
    // Nukage rain is a rule of the game too: where the game doesn't run it
    // (a demo, a netgame, a game without nukage) it falls as rain.
    if(kind==WEATHER_NUKAGE&&!Game_NukageRain()) kind=WEATHER_RAIN;
    return kind;
}
const WeatherKind &weatherKind(Weather kind) {
    return kind>WEATHER_NONE&&kind<WEATHER_KINDS?*weatherKinds[kind]:dryWeather;
}
float weatherNow(Weather kind) {return kind!=WEATHER_NONE&&falling()==kind?fallNow:0;}
// Dry ice lies under a clear sky.
float overcast() {return settings.weather&&falling()!=WEATHER_NONE&&falling()!=WEATHER_DRY_ICE?std::clamp(levelMood().rainAmount,0.0f,1.0f):0.0f;}
namespace {
unsigned weatherLevelSerial=~0u,soakPaneSerial=~0u;
int weatherTic=-1;
std::vector<float> soak;
// How hard it falls: a downpour (or as thick as snow gets), or less as the
// mood says (rainAmount), the gusts varying it a little.
float downpour(float t) {return (0.8f+0.2f*valueNoise(t,0,6,weatherSeed()^0x5bd1e995u))*levelMood().rainAmount;}
}

FarColor farFall;
void FarColor::settle(const std::array<float,3> &fallback) {
    if(level!=r_levelserial||count>0) {
        float take=level!=r_levelserial?1:count/(count+32.0f);
        for(int c=0;c<3;++c)color[c]+=((count>0?sum[c]/count:fallback[c])-color[c])*take;
        level=r_levelserial;
    }
    sum={0,0,0};count=0;
}

uint32_t weatherSeed() {
    return hash32((uint32_t)gameepisode*7919u+(uint32_t)gamemap*104729u+(uint32_t)skytexture*31u+17u);
}
// A new level (scene.cpp's startLevel, before anything is baked; or the
// first tic of one): the sun's place, the wind, and what falls.
void weatherNewLevel() {
    if(weatherLevelSerial==r_levelserial) return;
    weatherLevelSerial=r_levelserial;weatherTic=-1;
    placeSun();
    windNewLevel();
    Weather kind=falling();
    fallNow=kind?downpour(0):0;
    wetNow=kind&&weatherKind(kind).wets?1:0;glassWetNow=fallNow;
    soak.clear();soakPaneSerial=~0u;
    const char *what=weatherKind(kind).name;
    if(!kind) fprintf(stderr,"3D weather: dry.\n");
    else if(settings.weather==1) fprintf(stderr,"3D weather: authored %s.\n",what);
    else fprintf(stderr,"3D weather: %s everywhere.\n",what);
}
// The player changed the hour or the heat (climate.cpp) during a level: the
// sun placed again for the re-bake.
void weatherSunAgain() {if(weatherLevelSerial==r_levelserial)placeSun();}

float windPress(float nx,float ny,float x,float y,float *eddy) {
    float hx,hy;windHeading(hx,hy);
    float trapped=0;
    int cx=(int)std::floor((x-nx*24-mapOrigin[0])/mapCell),cy=(int)std::floor((y-ny*24-mapOrigin[1])/mapCell);
    if(trappedAirBaked()&&cx>=0&&cy>=0&&cx<mapWidth&&cy<mapHeight)trapped=trappedAir((size_t)cy*mapWidth+cx);
    // Both as strong as the wind is (none in still air).
    float strength=windStrength();
    if(eddy)*eddy=(0.3f+0.7f*trapped)*strength;
    return std::max(0.0f,hx*nx+hy*ny)*(1-0.5f*trapped)*strength;
}
float weatherPressOn(float nx,float ny,float x,float y) {
    float eddy,press=windPress(nx,ny,x,y,&eddy);
    return std::max(press,weatherKind(falling()).lee*eddy);
}
// Every window's weather (its particles, what soaks in under it, its glass)
// reads the same press as the doors and dry ice's intake; what blows low
// (sand) the less the higher the sill stands over the ground outside.
float paneDraft(const PaneView &p) {
    float speed=draftInflow(p.line);
    return std::isnan(speed)?1.0f:std::clamp(speed/40,0.0f,1.0f);   // before a solve: as it was
}
float paneInward(const PaneView &p) {
    float press=weatherPressOn(p.inX,p.inY,(p.ax+p.bx)/2,(p.ay+p.by)/2);
    return p.outer<0||p.outer>=numsectors?press:press*liftShare(weatherKind(falling()),p.bottom-floorZ(&sectors[p.outer]));
}
// Of what blows low (a finite lift), the share that reaches climb units up
// from the ground: all of it at the ground, a third as much (the storm's
// dust) past its lift.
float liftShare(const WeatherKind &kind,float climb) {
    if(std::isinf(kind.lift))return 1;
    return 0.35f+0.65f*std::clamp(1-climb/(kind.lift*windCarry()),0.0f,1.0f);   // higher in a gale
}
FlyArea flyArea(const Uniforms &camera,float windX,float windY,float fallSeconds) {
    float speed=std::hypot(windX,windY),half=std::min(speed*fallSeconds,768.0f)/2;
    if(speed<1)return {camera.eye[0],camera.eye[1],0};
    return {camera.eye[0]+windX/speed*half,camera.eye[1]+windY/speed*half,half};
}
float flyFade(float distance,float fade,float reach) {return std::clamp((reach-distance)/std::max(reach-fade,1.0f),0.0f,1.0f);}
bool flyOutOfReach(const Uniforms &camera,float x,float y,float windX,float windY,float fallSeconds,float reach) {
    float dx=-windX*fallSeconds*1.5f,dy=-windY*fallSeconds*1.5f,ex=camera.eye[0]-x,ey=camera.eye[1]-y;
    float t=std::clamp((ex*dx+ey*dy)/std::max(dx*dx+dy*dy,1e-4f),0.0f,1.0f);
    return std::hypot(ex-dx*t,ey-dy*t)>reach+48;
}
bool flyBehind(const Uniforms &camera,float x,float y,float windX,float windY,float fallSeconds) {
    float flat=std::hypot(camera.forward[0],camera.forward[1]);
    if(flat<=0.5f)return false;   // looking down, everything round shows
    float fx=camera.forward[0]/flat,fy=camera.forward[1]/flat;
    auto behind=[&](float px,float py) {
        float dx=px-camera.eye[0],dy=py-camera.eye[1],d=std::hypot(dx,dy);
        return d>96&&dx*fx+dy*fy<-0.3f*d;
    };
    return behind(x,y)&&behind(x-windX*fallSeconds,y-windY*fallSeconds);
}
bool letsWeatherIn(const PaneView &p) {return p.broken&&p.sky&&fallNow>0.02f&&paneInward(p)>0.05f;}
// In front of a broken pane letting weather in, inside, below its lintel,
// as far in as the drops land (rain.cpp, snow.cpp: further the straighter
// the wind blows in).
bool blownIn(float x,float y,float z) {
    if(fallNow<=0.02f)return false;
    for(int i=0;i<windowPaneCount();++i) {
        PaneView p;
        if(!windowPane(i,p)||!letsWeatherIn(p))continue;
        float ex=p.bx-p.ax,ey=p.by-p.ay,len=std::hypot(ex,ey);
        if(len<4)continue;
        float along=((x-p.ax)*ex+(y-p.ay)*ey)/len,depth=(x-p.ax)*p.inX+(y-p.ay)*p.inY;
        if(along>-16&&along<len+16&&depth>0&&depth<24+72*paneInward(p)&&z<p.top)return true;
    }
    return false;
}
bool weatherReaches(const mobj_t &thing) {
    if(!thing.subsector)return false;
    return underSky(int(thing.subsector->sector-sectors))||blownIn(units(thing.x),units(thing.y),units(thing.z));
}
int rainedIn(mobj_t *mo) {return mo&&blownIn(units(mo->x),units(mo->y),units(mo->z));}
float paneSoak(int pane) {return pane>=0&&(size_t)pane<soak.size()?soak[pane]:0.0f;}

namespace {
// The ground's wetness per map cell (1 + wetness * 254; 0: none, out of
// the weather's reach), or how deep what settles lies there, for the
// level, wind heading and weather it was baked for; wetRaw as the paths
// found it, before it is carried along the ground under roofs.
std::vector<uint8_t> wetCells,wetRaw;
unsigned wetLevelSerial=~0u;
// The window panes the bake stopped its paths at (windowPaneSerial): the
// panes are found on the level's first tic, which may come after its first
// frame's bakes, so the ground is baked again once they are known.
unsigned wetPaneSerial=~0u;
float wetHeading[2]={0,0},wetSpeed=-1,wetAmount=-1;
Weather wetKind=WEATHER_NONE;
// Per sector, the floor and ceiling bakeMap had when the paths were traced.
std::vector<std::array<float,2>> wetHeights;
// A re-trace under way (settleWetness): its cells' bounds and the next row.
struct Retrace { bool active=false;int x0=0,y0=0,x1=-1,y1=-1,row=0; } retrace;
bool skyCell(int own) {return own!=noSector&&underSky(own);}
// Per sector, the open air under a roof (rooms.c: a porch, a hall or
// garage open sideways onto a yard, a doorless recess): the ground bake
// takes its roof away and lays the ground there as on the open ground
// beside it, so nothing changes on the roof's line.
std::vector<char> rooflessSectors() {
    std::vector<char> roofless(numsectors);
    for(int s=0;s<numsectors;++s)roofless[s]=Rooms_Outside(s)&&!underSky(s);
    return roofless;
}
// Per sector, whether it is outside (under the sky or a porch), for the
// re-traces round a door that came to rest: indoors what lay stays.
std::vector<char> outsideSectors() {
    std::vector<char> outside(numsectors);
    for(int s=0;s<numsectors;++s)outside[s]=Rooms_Outside(s)!=0;
    return outside;
}
// The paths for the cells x0..x1, y0..y1 into wetRaw, on the heading and
// weather wetHeading and wetKind say; with only, just the cells of the
// sectors it marks (the rest keep what they had).
void traceWetness(int x0,int y0,int x1,int y1,const std::vector<char> *only=nullptr) {
    const float hx=wetHeading[0],hy=wetHeading[1];
    const WeatherKind &weather=weatherKind(wetKind);
    uint32_t seed=weatherSeed()^0x27d4eb2fu;
    const float fall=windSlant(weather.slant);   // flatter the harder it blows (wind.cpp)
    const float amount=wetAmount,wind=windAmount();   // for the kind's ground rule
    // Where the air is trapped (trapped_air.cpp) what falls eddies: its
    // paths spread round to every side and come down steeper, the light
    // and slanting (snow) all the way, the heavy and steep (rain) less.
    bakeTrappedAir(hx,hy);
    const float swirl=std::min(1.0f,2/fall);
    // (Window glass: under a roof the open air's field stops at a window
    // frame's sector, open_air.cpp; out under the sky no path meets one.)
    // The path a drop came down to the cell, traced back up the wind:
    // turned yaw radians off the wind, slant times as steep.
    auto path=[&](float yaw,float slant,float d[3]) {
        float c=std::cos(yaw),s=std::sin(yaw),length=std::sqrt(1+slant*slant);
        d[0]=-(hx*c-hy*s)/length;d[1]=-(hx*s+hy*c)/length;d[2]=slant/length;
    };
    const std::vector<char> roofless=rooflessSectors();
    ensureWaterIce();   // frozen water lies as ground (water_ice.cpp); built before the workers read it
    parallelFor(y1-y0+1,[&](int row) {
        int cy=y0+row;
        for(int cx=x0;cx<=x1;++cx) {
            size_t i=(size_t)cy*mapWidth+cx;
            int own=seamCells[i].own;
            if(only&&(own==noSector||!(*only)[own]))continue;
            wetRaw[i]=0;
            if(own==noSector||sectors[own].floorpic==skyflatnum)continue;
            bool open=skyCell(own)||roofless[own],liquid=(size_t)own<sectorMist.size()&&sectorMist[own].liquid;
            if(liquid&&waterIceCell(i)>=0.55f)liquid=false;   // frozen over: snow lies on it as on the ground
            if(liquid) {if(open)wetRaw[i]=1;continue;}
            WeatherGround cell{mapOrigin[0]+(cx+0.5f)*mapCell,mapOrigin[1]+(cy+0.5f)*mapCell,seed,false,false,0,false,amount,wind,hx,hy};
            float z=bakeMap.sectors[own].floor+1,d[3];
            float across[3]={hx,hy,0},eddy=trappedAir(i)*swirl;
            cell.trapped=trappedAir(i);
            // Only the wind blowing straight on piles it at a wall's foot.
            cell.windward=eddy<0.5f&&!traceRay(bakeMap,own,cell.x,cell.y,z+23,across,std::max(mapCell,8.0f),SkyCeiling::open,nullptr,&roofless);
            // Under a room's roof (round a skylight, in at a broken window's
            // room): what the wind carries in from the open sky (open_air.cpp),
            // as the kind lies on open ground there, ragged, in steps.
            if(!open) {
                float carried=openAirAt(i);
                if(carried<=0.02f)continue;
                carried*=0.7f+0.6f*valueNoise(cell.x,cell.y,32,seed^0x1b873593u);
                float wet=std::clamp(weather.ground(cell),0.0f,1.0f)*std::min(1.0f,carried);
                if(wet<0.06f)continue;
                static const float steps[]={0.12f,0.35f,0.6f,0.85f,1.0f};
                float best=steps[0];
                for(float step:steps)if(std::fabs(step-wet)<std::fabs(best-wet))best=step;
                wetRaw[i]=(uint8_t)(1+std::lround(best*254));
                continue;
            }
            // The eight paths: how many reach the sky, and how near the
            // nearest blocked one is stopped.
            float turn=(hashUnit(cx,cy,seed^0x5f356495u)-0.5f)*0.12f,nearest=INFINITY;
            int reached=0;bool roofed=false;
            // Fanned 0.6 radians round the heading in open air, all the
            // way round where it is trapped; flatter and steeper by turns.
            float spread=0.085f+((float)doomPi/4-0.085f)*eddy,steep=fall*(1+2*eddy);
            for(int n=0;n<8;++n) {
                BakeHit hit;path(turn+(n-3.5f)*spread,steep*(n%2?1.4f:0.7f),d);
                if(traceRay(bakeMap,own,cell.x,cell.y,z,d,INFINITY,SkyCeiling::escape,&hit,&roofless)) {++reached;continue;}
                float at=std::hypot(hit.x-cell.x,hit.y-cell.y);
                if(at<nearest) {nearest=at;roofed=hit.kind==BakeHit::ceiling||(hit.kind==BakeHit::wall&&hit.part==1);}
            }
            float share=reached/8.0f,wet=std::clamp(weather.ground(cell),0.0f,1.0f);
            if(reached<8) {
                // Partly sheltered: between the lee and the open ground
                // by the share of paths that get through.
                WeatherGround lee=cell;lee.lee=true;lee.leeAt=std::max(mapCell,nearest);lee.roofed=roofed;
                wet+=(std::clamp(weather.ground(lee),0.0f,1.0f)-wet)*(1-share);
            }
            // A porch lies exactly as the open ground; a hall or garage open
            // sideways onto the yard as much less as the wind carries less
            // of it in there (open_air.cpp): the same as the yard at its
            // mouth, thinning further in, the pattern running on unbroken.
            float fade=roofless[own]&&Rooms_Kind(own)!=ROOM_SHELTERED?std::min(1.0f,openAirAt(i)):1.0f;
            if(fade<=0.02f)continue;
            wet*=fade;
            if(share<1||fade<1) {
                // In steps, from damp at the furthest in.
                static const float steps[]={0.12f,0.35f,0.6f,0.85f,1.0f};
                if(wet<0.06f)continue;
                float best=steps[0];
                for(float step:steps)if(std::fabs(step-wet)<std::fabs(best-wet))best=step;
                wet=best;
            }
            wetRaw[i]=(uint8_t)(1+std::lround(wet*254));
        }
    });
}
// Blown in, it arrives along the ground: under a roof a cell keeps no more
// than a neighbour nearer the open air has, stepping only within a sector
// or across an open line in bakeMap (never a wall, a shut door or whole
// glass), so it fades in from the open edge and never lies again behind a
// bare stretch (E1M6: a low passage the paths can't get through, then a
// tall hall behind it that they could). wetRaw into wetCells over the cells
// x0..x1, y0..y1 (nothing comes in from beyond them); with only, through just
// the cells of the sectors it marks.
void carryWetness(int x0,int y0,int x1,int y1,const std::vector<char> *only=nullptr) {
    std::vector<char> whole(numlines,0);
    for(int n=0;n<windowPaneCount();++n) {PaneView p;if(windowPane(n,p)&&!p.broken&&p.line>=0&&p.line<numlines)whole[p.line]=1;}
    std::unordered_set<uint64_t> joined;
    for(int n=0;n<numlines;++n) {
        const line_t &line=lines[n];
        if(!line.frontsector||!line.backsector||whole[n])continue;
        const BakeSector &f=bakeMap.sectors[line.frontsector-sectors],&b=bakeMap.sectors[line.backsector-sectors];
        if(std::min(f.ceiling,b.ceiling)-std::max(f.floor,b.floor)<8)continue;
        uint64_t p=(uint64_t)(line.frontsector-sectors),q=(uint64_t)(line.backsector-sectors);
        joined.insert(p<q?p<<32|q:q<<32|p);
    }
    std::vector<uint8_t> kept(wetRaw.size(),0);
    const std::vector<char> roofless=rooflessSectors();
    auto open=[&](int own){return own!=noSector&&(skyCell(own)||roofless[own]);};
    for(int cy=y0;cy<=y1;++cy)for(int cx=x0;cx<=x1;++cx) {size_t i=(size_t)cy*mapWidth+cx;if(open(seamCells[i].own))kept[i]=wetRaw[i];}
    // What blows along the ground (sand: a finite lift) climbs no step taller
    // than a quarter of how high it blows: it piles at the foot instead.
    const float climb=weatherKind(wetKind).lift*windCarry()/4;
    auto from=[&](size_t i,int x,int y) {
        if(x<x0||y<y0||x>x1||y>y1)return;
        size_t n=(size_t)y*mapWidth+x;
        int a=seamCells[i].own,b=seamCells[n].own;
        if(b==noSector||!kept[n])return;
        if(a!=b) {uint64_t p=(uint64_t)a,q=(uint64_t)b;if(!joined.count(p<q?p<<32|q:q<<32|p))return;}
        if(a!=b&&bakeMap.sectors[a].floor-bakeMap.sectors[b].floor>climb)return;
        kept[i]=std::max(kept[i],std::min(wetRaw[i],kept[n]));
    };
    for(int pass=0;pass<3;++pass) {
        for(int cy=y0;cy<=y1;++cy)for(int cx=x0;cx<=x1;++cx) {
            size_t i=(size_t)cy*mapWidth+cx;int own=seamCells[i].own;
            if(own==noSector||open(own)||!wetRaw[i]||(only&&!(*only)[own]))continue;
            from(i,cx-1,cy);from(i,cx,cy-1);from(i,cx-1,cy-1);from(i,cx+1,cy-1);
        }
        for(int cy=y1;cy>=y0;--cy)for(int cx=x1;cx>=x0;--cx) {
            size_t i=(size_t)cy*mapWidth+cx;int own=seamCells[i].own;
            if(own==noSector||open(own)||!wetRaw[i]||(only&&!(*only)[own]))continue;
            from(i,cx+1,cy);from(i,cx,cy+1);from(i,cx+1,cy+1);from(i,cx-1,cy+1);
        }
    }
    for(int cy=y0;cy<=y1;++cy)for(int cx=x0;cx<=x1;++cx) {
        size_t i=(size_t)cy*mapWidth+cx;int own=seamCells[i].own;
        if(only&&(own==noSector||!(*only)[own]))continue;
        wetCells[i]=own!=noSector?kept[i]:wetRaw[i];
    }
}
}
uint8_t groundBakeAt(size_t cell) {return cell<wetCells.size()&&wetLevelSerial==r_levelserial?wetCells[cell]:0;}
namespace {
void keepWetHeights() {
    wetHeights.resize(numsectors);
    for(int s=0;s<numsectors;++s)wetHeights[s]={bakeMap.sectors[s].floor,bakeMap.sectors[s].ceiling};
}
}
// Bakes the wetness of the ground into the floor light's spare alpha
// (bake_atlas.cpp's floorLight layer; wear.glsl reads it). It comes in
// slanted on the wind (the kind's slant: rain falls about four and a half
// units for one across), traced through the map like the sun's rays along
// eight paths, the gusts' spread of headings and slants, spread round to
// every side and steeper where the air is trapped (trapped_air.cpp). An open cell lies
// between the lee and the open ground by the share of paths a wall, roof
// or ledge stops (leeAt: how far off the nearest stops), is at the foot of
// a wall facing the wind where one stands just downwind (a roof's edge
// with headroom under it is no wall), and the falling kind's ground rule
// says how wet or deep it lies there. A porch's roof is taken away for it:
// the ground under it lies exactly as the open ground beside it does, so
// nothing changes on the roof's line; a hall or garage open sideways onto
// the yard the same, thinning in as far as the wind carries it (open_air.cpp);
// under a room's roof (round a skylight) only as far as the wind carries it
// from the open sky, ragged. Steps, not a gradient, and the same all map long: it has
// been falling a while. Never on liquids; under a roof only as far as it
// reaches along the ground from the open air (no bare stretch on the way).
// Round a door or lift that moved and came to rest it is traced again
// (settleWetness).
void bakeWetness() {
    Rooms_Build();
    if(flatLightCells.size()<(size_t)mapWidth*mapHeight*4||seamCells.size()<(size_t)mapWidth*mapHeight||bakeMap.sectors.size()!=(size_t)numsectors)return;
    float hx,hy;windHeading(hx,hy);
    Weather kind=falling();
    if(wetLevelSerial!=r_levelserial||wetCells.size()!=seamCells.size()||hx!=wetHeading[0]||hy!=wetHeading[1]||windSpeedNow()!=wetSpeed||kind!=wetKind||wetPaneSerial!=windowPaneSerial()||levelMood().rainAmount!=wetAmount) {
        wetLevelSerial=r_levelserial;wetHeading[0]=hx;wetHeading[1]=hy;wetSpeed=windSpeedNow();wetKind=kind;wetPaneSerial=windowPaneSerial();wetAmount=levelMood().rainAmount;
        retrace.active=false;   // all of it traced anew
        wetCells.assign(seamCells.size(),0);wetRaw.assign(seamCells.size(),0);
        if(kind!=WEATHER_NONE) {   // dry: nothing lies, no paths to trace
            bakeOpenAir(kind,hx,hy,0,0,mapWidth-1,mapHeight-1);   // what the wind carries in under roofs (open_air.cpp)
            traceWetness(0,0,mapWidth-1,mapHeight-1);
            carryWetness(0,0,mapWidth-1,mapHeight-1);
        }
        keepWetHeights();
        // ROOMSDEBUG=1: how deep it lies per outside sector (mean of its cells, 0-1), and the share of its cells bare.
        if(getenv("ROOMSDEBUG")) {
            std::vector<double> sum(numsectors,0);std::vector<int> cells(numsectors,0),bare(numsectors,0);
            for(size_t i=0;i<wetCells.size();++i) {int own=seamCells[i].own;if(own==noSector||!Rooms_Outside(own))continue;
                ++cells[own];sum[own]+=wetCells[i]?(wetCells[i]-1)/254.0:0;bare[own]+=wetCells[i]==0;}
            for(int s=0;s<numsectors;++s)if(cells[s])fprintf(stderr,"WET sector %d%s: mean %.2f, bare %.0f%%\n",s,underSky(s)?"":" (roof)",sum[s]/cells[s],100.0*bare[s]/cells[s]);
        }
    }
    for(size_t i=0;i<wetCells.size();++i)flatCell(floorLight,i)[3]=wetCells[i];
    filmPaint();   // what lies as the floor film indoors goes back over it (floor_film.cpp)
    dentsPaint();  // and what disturbances dug out outside (ground_dents.cpp)
}
namespace {
// Per sector, its floor and ceiling as last seen and the tic they last
// changed: a mover is at rest once they have held for a few tics (bakeMap
// catches up with a moving door every few tics, which looked like rest).
std::vector<std::array<float,2>> seenHeights;
std::vector<int> movedTic;
unsigned seenSerial=~0u;
void watchMovers() {
    if(seenSerial!=r_levelserial||seenHeights.size()!=(size_t)numsectors) {
        seenSerial=r_levelserial;seenHeights.assign(numsectors,{NAN,NAN});movedTic.assign(numsectors,INT_MIN/2);
    }
    for(int s=0;s<numsectors;++s) {
        std::array<float,2> h{units(sectors[s].floorheight),units(sectors[s].ceilingheight)};
        if(h!=seenHeights[s]) {if(!std::isnan(seenHeights[s][0]))movedTic[s]=leveltime;seenHeights[s]=h;}
    }
}
// A door, lift or lowering wall that moved and came to rest in bakeMap
// (relight.cpp follows it there): the paths are traced again over the
// outside cells it may stand in the way of, as far off as the flattest path
// takes to climb over its tallest side, and what blows in is carried along
// the ground again over them; only for one outside or beside the outside.
// Indoors it stays as it lay: what a door opened during play lets in creeps
// in as the floor film (blown_in.cpp), and what lay behind one that shuts
// lies on where it is, instead of all appearing or vanishing at once; so
// the cells there are left alone (tracing them too took 30 ms in E1M1's
// gale). Outside (under the sky, a porch) the drifts take the new shape.
// The trace runs about 2 ms a tic, the carry on the tic after it finishes,
// so it never hitches; a drift taking shape a few tics late goes unseen.
// The changed cells' bounds go into changed (map cells, inclusive); false
// if nothing changed (yet). Not while a re-bake writes the floor light.
bool settleWetness(int changed[4]) {
    watchMovers();
    if(wetLevelSerial!=r_levelserial||wetHeights.size()!=(size_t)numsectors||bakeMap.sectors.size()!=(size_t)numsectors||relightRunning())return false;
    int x0=INT_MAX,y0=INT_MAX,x1=-1,y1=-1;
    const float fall=windSlant(weatherKind(wetKind).slant);
    auto cellX=[&](float x){return std::clamp((int)std::floor((x-mapOrigin[0])/mapCell),0,mapWidth-1);};
    auto cellY=[&](float y){return std::clamp((int)std::floor((y-mapOrigin[1])/mapCell),0,mapHeight-1);};
    std::vector<char> outside=outsideSectors();
    for(int s=0;s<numsectors;++s) {
        const BakeSector &b=bakeMap.sectors[s];
        if(b.floor==wetHeights[s][0]&&b.ceiling==wetHeights[s][1])continue;
        if(b.floor!=units(sectors[s].floorheight)||b.ceiling!=units(sectors[s].ceilingheight)||leveltime-movedTic[s]<4)continue;   // still moving
        float bx0=1e9f,by0=1e9f,bx1=-1e9f,by1=-1e9f,low=std::min(b.floor,wetHeights[s][0]),high=std::max(b.ceiling,wetHeights[s][1]);
        bool touches=outside[s];
        for(int index:b.lines) {
            const BakeLine &line=bakeMap.lines[index];
            bx0=std::min({bx0,line.ax,line.bx});by0=std::min({by0,line.ay,line.by});
            bx1=std::max({bx1,line.ax,line.bx});by1=std::max({by1,line.ay,line.by});
            int other=line.front==s?line.back:line.front;
            if(other>=0) {low=std::min(low,bakeMap.sectors[other].floor);high=std::max(high,bakeMap.sectors[other].ceiling);touches|=outside[other];}
        }
        wetHeights[s]={b.floor,b.ceiling};
        // A door between rooms changes nothing outside: no path from the
        // open air down to the ground passes through it.
        if(bx1<bx0||!touches)continue;
        float reach=std::min((high-low+128)/(0.7f*fall),1024.0f)+mapCell;
        x0=std::min(x0,cellX(bx0-reach));y0=std::min(y0,cellY(by0-reach));
        x1=std::max(x1,cellX(bx1+reach));y1=std::max(y1,cellY(by1+reach));
    }
    // Another came to rest while one was under way: both, from the top.
    if(x1>=0) {
        if(retrace.active) {x0=std::min(x0,retrace.x0);y0=std::min(y0,retrace.y0);x1=std::max(x1,retrace.x1);y1=std::max(y1,retrace.y1);}
        retrace={true,x0,y0,x1,y1,y0};
        // Under the roofs round it too (open_air.cpp), on a tic of its own.
        bakeOpenAir(wetKind,wetHeading[0],wetHeading[1],x0,y0,x1,y1);
        return false;
    }
    if(!retrace.active)return false;
    const Retrace &r=retrace;
    static const bool debug=getenv("ROOMSDEBUG")!=nullptr;   // ROOMSDEBUG=1 times it
    uint64_t start=SDL_GetTicksNS();
    if(r.row<=r.y1) {
        while(retrace.row<=r.y1&&SDL_GetTicksNS()-start<2000000) {
            int to=std::min(r.y1,r.row+31);
            traceWetness(r.x0,r.row,r.x1,to,&outside);
            retrace.row=to+1;
        }
        if(debug)fprintf(stderr,"SETTLE tic %d: traced to row %d of %d..%d in %.2f ms\n",leveltime,r.row,r.y0,r.y1,(SDL_GetTicksNS()-start)/1e6);
        return false;
    }
    retrace.active=false;
    std::vector<uint8_t> before(wetCells);
    carryWetness(r.x0,r.y0,r.x1,r.y1,&outside);
    changed[0]=changed[1]=INT_MAX;changed[2]=changed[3]=-1;
    for(int cy=r.y0;cy<=r.y1;++cy)for(int cx=r.x0;cx<=r.x1;++cx) {
        size_t i=(size_t)cy*mapWidth+cx;
        if(wetCells[i]==before[i])continue;
        flatCell(floorLight,i)[3]=dentedAlpha(i,wetCells[i]);   // dents outlive the re-bake (ground_dents.cpp)
        changed[0]=std::min(changed[0],cx);changed[1]=std::min(changed[1],cy);
        changed[2]=std::max(changed[2],cx);changed[3]=std::max(changed[3],cy);
    }
    if(debug)fprintf(stderr,"SETTLE tic %d: cells %dx%d of %dx%d carried in %.2f ms\n",leveltime,r.x1-r.x0+1,r.y1-r.y0+1,mapWidth,mapHeight,
                     (SDL_GetTicksNS()-start)/1e6);
    return changed[2]>=0;
}
}

// Each tic: the wind (wind.cpp), how hard it falls, whether the ground is wet
// or white (it is while it falls), how wet the glass gets (over the kind's
// glassSeconds, drying over one minute), and how much each broken window
// has let in (over the kind's paneSeconds with the wind blowing straight
// in, the wet or white spreading out from the sill as it does; four
// minutes to dry or melt). Only a window facing the wind lets it in. One
// already broken when the level starts has been letting it in all along.
void watchWeather() {
    if(gamestate!=GS_LEVEL||levelSerial!=r_levelserial) return;
    weatherNewLevel();
    if(leveltime==weatherTic) return;
    weatherTic=leveltime;
    float t=levelSeconds();
    Weather kind=falling();
    const WeatherKind &weather=weatherKind(kind);
    fallNow=kind?downpour(t):0;
    windTick(t);
    float wx,wy;windHeading(wx,wy);
    // The heading or the weather turned (the authored layer or the weather
    // switched), or the panes were found after the bake: the ground's
    // wetness or snow anew; or round a door that moved, once it rests. Dry,
    // only when it stops falling: the wind and the panes lay nothing then.
    if(flatLightTexture&&wetLevelSerial==r_levelserial&&(kind!=wetKind||(kind!=WEATHER_NONE&&(wx!=wetHeading[0]||wy!=wetHeading[1]||windSpeedNow()!=wetSpeed||wetPaneSerial!=windowPaneSerial()||levelMood().rainAmount!=wetAmount)))) {
        bakeWetness();
        gpuUpdateTexture(flatLightTexture,GpuFormat::RGBA8,{{0,floorLight*mapHeight,mapWidth,mapHeight}},flatLightCells.data(),mapWidth*4);
    }
    else if(flatLightTexture&&kind!=WEATHER_NONE) {
        int c[4];
        if(settleWetness(c))
            gpuUpdateTexture(flatLightTexture,GpuFormat::RGBA8,{{c[0],c[1]+floorLight*mapHeight,c[2]-c[0]+1,c[3]-c[1]+1}},flatLightCells.data(),mapWidth*4);
    }
    const float tic=weatherTicSeconds();
    wetNow=kind&&weather.wets?1:0;
    glassWetNow=fallNow>glassWetNow?glassWetNow+(fallNow-glassWetNow)*tic/weather.glassSeconds:std::max(fallNow,glassWetNow-tic/60);
    // Dry with every sill dried out (Clear): no pane to look at.
    bool soaking=kind!=WEATHER_NONE||std::any_of(soak.begin(),soak.end(),[](float s){return s>0;});
    if(soaking&&soakPaneSerial!=windowPaneSerial()) {
        soakPaneSerial=windowPaneSerial();soak.assign(windowPaneCount(),0);
        for(size_t i=0;i<soak.size();++i) {PaneView p;soak[i]=windowPane((int)i,p)&&letsWeatherIn(p)?1.0f:0.0f;}
    }
    if(soaking)for(size_t i=0;i<soak.size();++i) {
        PaneView p;
        if(!windowPane((int)i,p)) continue;
        if(letsWeatherIn(p)) soak[i]=std::min(1.0f,soak[i]+fallNow*(0.4f+0.6f*paneInward(p))*tic/weather.paneSeconds);
        else soak[i]=std::max(0.0f,soak[i]-tic/240);
    }
    // Every kind's sound, the rest fading out before the falling one's sets
    // its strength (kinds may share a sound: nukage rain is rain's).
    for(int k=WEATHER_NONE+1;k<WEATHER_KINDS;++k)
        if(k!=kind)weatherKinds[k]->ambience(0);
    if(kind!=WEATHER_NONE)weather.ambience(settings.accelerated?fallNow:0);
}

namespace {
// Per sector, the highest sky ceiling of the open sky it lies under (the
// sky sectors joined to it by two-sided lines), taken each frame.
std::vector<float> openSkyTop;
void findOpenSkyTops() {
    auto open=[](const sector_t *s){return underSky(int(s-sectors))&&ceilZ(s)>floorZ(s);};
    std::vector<int> group(numsectors);
    openSkyTop.assign(numsectors,0);
    for(int i=0;i<numsectors;++i) {group[i]=i;openSkyTop[i]=ceilZ(&sectors[i]);}
    auto root=[&](int i){while(group[i]!=i)i=group[i]=group[group[i]];return i;};
    for(int i=0;i<numlines;++i) {
        const line_t &line=lines[i];
        if(!line.frontsector||!line.backsector||!open(line.frontsector)||!open(line.backsector)) continue;
        int a=root(int(line.frontsector-sectors)),b=root(int(line.backsector-sectors));
        if(a!=b) {group[b]=a;openSkyTop[a]=std::max(openSkyTop[a],openSkyTop[b]);}
    }
    for(int i=0;i<numsectors;++i) openSkyTop[i]=std::max(openSkyTop[i],openSkyTop[root(i)]);
}
}
// Where rain or snow starts falling over a sky sector: world-fixed, never
// from the eye (its bob and steps would shift every drop's cycle and make
// them jump): its sky ceiling, at least above units over its floor, up to
// twice that under the open sky's highest ceiling, so from a ledge the
// fall in the yard below still starts up in the sky.
float skyFallTop(const sector_t &sector,float above) {
    float floor=floorZ(&sector),top=std::max(ceilZ(&sector),floor+above);
    size_t index=&sector-sectors;
    if(index<openSkyTop.size()) top=std::max(top,std::min(openSkyTop[index],floor+2*above));
    return top;
}

void buildWeather(const Uniforms &camera) {
    if(fallNow<=0.01f||camera.effects[2]!=0||seamCells.empty()||levelSerial!=r_levelserial||!settings.weather) return;
    findOpenSkyTops();
    if(const auto build=weatherKind(falling()).build) build(camera);
    buildDrafts(camera);   // and what the drafts carry in through the buildings
    buildBlownIn(camera);  // and what the wind presses in at the doorways
    buildDisturbances(camera);   // and what blasts throw up of what lies
}

// What a streak of rain (or a flake of snow, its albedo) at (x, y, z) looks
// like: in its sector's light, a
// little brighter than the walls behind it so it reads against them, and
// the baked lamp light of the floor under it, which it catches more the
// nearer the ground: a glowing pool or a lamp's pool of light below takes
// the rain falling into it over to its own color, by its share of the
// light there, and brightens it a little in that color. One lookup a drop.
std::array<float,3> weatherColor(const sector_t &sector,float x,float y,float z,const std::array<float,3> &albedo) {
    float light=lighting(&sector),glow[3]={0,0,0};
    if(bakedLightsActive&&!flatLightCells.empty()) {
        int cx=std::clamp((int)std::floor((x-mapOrigin[0])/mapCell),0,mapWidth-1);
        int cy=std::clamp((int)std::floor((y-mapOrigin[1])/mapCell),0,mapHeight-1);
        const uint8_t *baked=flatCell(floorLight,(size_t)cy*mapWidth+cx);
        float near=1-0.85f*std::clamp((z-floorZ(&sector))/128,0.0f,1.0f);
        for(int c=0;c<3;++c)glow[c]=baked[c]/255.0f*2*near;
    }
    float peak=std::max({glow[0],glow[1],glow[2]}),share=peak>0.01f?std::min(1.0f,peak/(peak+light*0.5f)):0;
    // In the flashlight's beam it lights up in the lamp's colour (beam_air.cpp).
    std::array<float,3> lamp;
    float beam=beamAt(x,y,z,&lamp);
    std::array<float,3> color;
    for(int c=0;c<3;++c) {
        float tint=peak>0.01f?glow[c]/peak:1;
        color[c]=std::min(1.0f,albedo[c]*(light*1.15f*(1+(tint-1)*share)+beam*lamp[c]*0.6f)+glow[c]*0.15f+0.04f);
    }
    return color;
}
// How much of the open air is round the eye (c.climate.w, 0-1): all of it
// out in the weather. Indoors, as much as the drafts' air brings in where it
// stands (roomWind: by an open door or a broken window, fading into the
// room), at most a third. Coming in from outside it doesn't drop to that at
// once: what was in the air came in too and thins over two seconds or so;
// going out it closes in within a few tics. Stepped each tic.
float openAirRound(const Uniforms &camera) {
    static float round=0;
    static int stepped=-1;
    static unsigned level=~0u;
    const sector_t *eye=weatherSectorAt(camera.eye[0],camera.eye[1]);
    float target=0;
    int cx=(int)std::floor((camera.eye[0]-mapOrigin[0])/mapCell),cy=(int)std::floor((camera.eye[1]-mapOrigin[1])/mapCell);
    bool inside=cx>=0&&cy>=0&&cx<mapWidth&&cy<mapHeight;
    if(eye&&Rooms_Outside(int(eye-sectors))) {
        // Out in it: all of it under the sky; under a porch's roof or in a
        // doorless corridor as much as the wind carries in there (open_air.cpp).
        target=underSky(int(eye-sectors))||!openAirBaked()||!inside?1.0f:openAirAt((size_t)cy*mapWidth+cx);
    }
    else {
        float wind=inside?roomWind((size_t)cy*mapWidth+cx):NAN;
        if(!std::isnan(wind))target=std::clamp(wind,0.0f,1.0f)/3;
    }
    if(level!=r_levelserial||leveltime<stepped) {level=r_levelserial;round=target;stepped=leveltime;}
    for(;stepped<leveltime;++stepped)round+=(target-round)*(target>round?0.25f:0.035f);
    return round;
}
// The sector whose floor lies at (x, y), by the map's cells; none outside.
const sector_t *weatherSectorAt(float x,float y) {
    int cx=(int)std::floor((x-mapOrigin[0])/mapCell),cy=(int)std::floor((y-mapOrigin[1])/mapCell);
    if(cx<0||cy<0||cx>=mapWidth||cy>=mapHeight) return nullptr;
    int own=seamCells[(size_t)cy*mapWidth+cx].own;
    return own==noSector?nullptr:&sectors[own];
}

void weatherShutdown() {
    openAirShutdown();
    fallNow=wetNow=glassWetNow=0;soak.clear();weatherLevelSerial=~0u;soakPaneSerial=~0u;
    for(int k=WEATHER_NONE+1;k<WEATHER_KINDS;++k)weatherKinds[k]->ambience(0);
}
} // namespace doom3d
