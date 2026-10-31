/* Sand (a WeatherKind; weather.cpp says when it blows and how hard): a
   sandstorm, blown rather than falling. Grains of one Doom pixel ride the
   whole wind, most of them low over the ground, sinking slowly as they go
   and streaking as far as they travel in a tic, so the storm reads
   unbroken; the ones that reach the ground skip along it in short hops.
   They blow across the open sky sectors, stopped where a wall or a step
   stands in their way. Through a broken window facing the wind they blow
   in and settle on the floor inside (wear.glsl), and the drafts carry them
   on through the rooms (drafts.cpp). On the ground it lies in drifts
   against the walls (sandGround), rippled across the wind
   (world/settled.glsl); the glass dusts over (glass.frag); past the map's
   sky the storm blows on under its dust (sky/sand.glsl). Its colour comes
   from the map's own open ground. Render-only, with its own random
   numbers. */
#include "internal.h"

namespace doom3d {
namespace {
constexpr float sink=40;   // units a second it sinks as it blows (sky/sand.glsl's too)
const std::array<float,3> sandTone={0.66f,0.54f,0.38f};
std::array<float,3> albedo=sandTone;
unsigned albedoLevel=~0u;
uint32_t sandSeed() {return hash32((uint32_t)gameepisode*7919u+(uint32_t)gamemap*104729u+0x7a3d2b19u);}

// Sand's colour, unlit: the flat most of the map's open ground lies on (by
// map cells, liquids aside), brought to sand's brightness and taken a third
// of the way there from sand, so a grey map's storm blows grey-brown and a
// brown map's brown.
std::array<float,3> sandAlbedo() {
    if(albedoLevel==r_levelserial||seamCells.empty()) return albedo;
    albedoLevel=r_levelserial;
    std::unordered_map<int,int> use;
    for(const SeamCell &cell:seamCells) {
        if(cell.own==noSector) continue;
        const sector_t &sector=sectors[cell.own];
        if(!underSky(int(&sector-sectors))||(cell.own<sectorMist.size()&&sectorMist[cell.own].liquid)) continue;
        ++use[sector.floorpic];
    }
    albedo=sandTone;
    if(use.empty()) return albedo;
    int pic=std::max_element(use.begin(),use.end(),[](const auto &a,const auto &b){return a.second<b.second;})->first;
    const auto &ground=averageColor(lumpImage(firstflat+flattranslation[pic],true));
    auto luma=[](const std::array<float,3> &c){return 0.3f*c[0]+0.59f*c[1]+0.11f*c[2];};
    float scale=luma(sandTone)/std::max(luma(ground),0.02f);
    for(int c=0;c<3;++c)albedo[c]=sandTone[c]+(std::min(1.0f,ground[c]*scale)-sandTone[c])/3;
    return albedo;
}
// A grain whose head is at (x, y, z), blowing along (hx, hy): a Doom pixel
// streaking back along its way length units, in pieces of a Doom pixel at
// its distance.
void grain(const Uniforms &camera,float x,float y,float z,float hx,float hy,float length,float distance,const std::array<float,3> &color,float alpha) {
    float unit=std::max(distance,4.0f)/(100*camera.projection[1]);
    int pieces=std::clamp((int)std::lround(length/unit),1,6);
    for(int k=0;k<pieces;++k) {
        float back=length*k/pieces;
        pixelBlock(particleVertices,camera,x-hx*back,y-hy*back,z,0.45f,1,color,false,&albedo,true,nullptr,alpha);
    }
}
}

// The storm round the eye: three grains per 16-unit cell in a share of the
// cells as hard as it blows (the same cells first as it thickens), out to
// 384 units, in sky sectors only. Each blows through its cell on the whole
// wind, 128 units of it, sinking as it goes: two at any height up to where
// the sky starts (skyFallTop), more of them low, so the storm fills the air
// up to the far sheet in the sky (sky/sand.glsl); the third low over the
// ground. One that reaches the ground skips along it in hops a few units
// high. Airborne sand catches the light: a shade brighter than it lies. Faded in and out at the ends of
// its run, so where it starts and stops never shows. Then the broken
// windows the wind blows it in by.
static void buildSand(const Uniforms &camera) {
    constexpr float cell=16,reach=384,run=128;
    float now=std::floor(gameSeconds()*TICRATE)/TICRATE;
    float density=0.7f*fallNow;
    uint32_t base=sandSeed();
    const std::array<float,3> lies=sandAlbedo();
    std::array<float,3> tone;
    for(int c=0;c<3;++c)tone[c]=std::min(1.0f,lies[c]*1.2f);
    float wind=std::hypot(windNow[0],windNow[1]),hx=1,hy=0;
    if(wind>1) {hx=windNow[0]/wind;hy=windNow[1]/wind;}
    float blow=std::max(wind,30.0f);
    int gx0=(int)std::floor((camera.eye[0]-reach)/cell),gx1=(int)std::floor((camera.eye[0]+reach)/cell);
    int gy0=(int)std::floor((camera.eye[1]-reach)/cell),gy1=(int)std::floor((camera.eye[1]+reach)/cell);
    float flat=std::hypot(camera.forward[0],camera.forward[1]);
    float fx=flat>1e-3f?camera.forward[0]/flat:0,fy=flat>1e-3f?camera.forward[1]/flat:0;
    for(int gy=gy0;gy<=gy1;++gy)for(int gx=gx0;gx<=gx1;++gx)for(int k=0;k<3;++k) {
        uint32_t seed=base^(uint32_t)k*0x9e3779b9u;
        if(hashUnit(gx,gy,seed)>=density) continue;
        float x0=(gx+hashUnit(gx,gy,seed^1u))*cell,y0=(gy+hashUnit(gx,gy,seed^2u))*cell;
        float dx=x0-camera.eye[0],dy=y0-camera.eye[1],distance=std::hypot(dx,dy);
        // Behind the eye only near it, where a glance down still shows them.
        if(distance>reach||(distance>96&&(dx*fx+dy*fy)<-0.3f*distance&&flat>0.5f)) continue;
        const sector_t *sector=weatherSectorAt(x0,y0);
        if(!sector||!underSky(int(sector-sectors))) continue;
        float floor=floorZ(sector);
        uint32_t own0=hash32((uint32_t)gx*73856093u^(uint32_t)gy*19349663u^seed);
        float speed=blow*(0.8f+0.4f*hashUnit(gx,gy,seed^3u)),period=run/speed+0.3f*hashUnit(gx,gy,seed^5u);
        float clock=now+hashUnit(gx,gy,seed^4u)*period*7;
        int cycle=(int)std::floor(clock/period);
        float age=clock-cycle*period,travel=age*speed;
        if(travel>run) continue;   // between its runs
        uint32_t own=own0^(uint32_t)cycle*2654435761u;
        // Its height halfway along its run.
        float h=hashUnit(cycle,1,own),top=skyFallTop(*sector,128);
        float middle=k<2?floor+2+std::max(0.0f,top-floor-2)*std::pow(h,1.5f):floor+3+40*h*h;
        float z=middle+sink*(run/speed*0.5f-age);
        float x=x0+hx*(travel-run/2),y=y0+hy*(travel-run/2);
        // Down on the ground it skips: hops 16 to 28 units long, a few high.
        if(z<floor+1) {
            float hop=16+12*hashUnit(cycle,2,own),past=(floor+1-z)/sink*speed,f=std::fmod(past,hop)/hop;
            z=floor+1+(2+3*hashUnit(cycle,6,own))*4*f*(1-f);
        }
        auto flies=[&](float px,float py,float pz) {
            const sector_t *at=weatherSectorAt(px,py);
            return at&&floorZ(at)<=pz&&(underSky(int(at-sectors))||pz<ceilZ(at));
        };
        if(!flies(x,y,z)) continue;
        // The air's push (air_push.cpp): the gusts since its run began (it
        // fades in from nothing there), and any blast's shove.
        float push[3];
        airShift(int(sector-sectors),x,y,z,now,now-age,push);
        airKeep(x,y,push,[&](float px,float py){return flies(px,py,z+std::max(push[2],0.0f));});
        x+=push[0];y+=push[1];z+=std::max(push[2],0.0f);
        const sector_t *at=weatherSectorAt(x,y);
        float fade=std::min({1.0f,travel/(run*0.15f),(run-travel)/(run*0.15f)});
        float alpha=(0.75f-0.35f*distance/reach)*fade*std::min(1.0f,fallNow*2);
        auto color=weatherColor(*at,x,y,z,tone);
        if(distance>reach*0.5f) farFall.see(color);
        if(distance>96)beamGlint(x,y,z,own,0.03f,color);   // a grain of quartz catching the flashlight (beam_air.cpp)
        grain(camera,x,y,z,hx,hy,speed/TICRATE,distance,color,alpha);
    }
    // Until any show on a level, the far sheet takes sand at the outdoors'
    // usual light.
    float light=std::clamp(0.6f*levelMood().outdoor,0.12f,1.0f);
    std::array<float,3> usual;
    for(int c=0;c<3;++c)usual[c]=tone[c]*light*1.15f+0.04f;
    farFall.settle(usual);
    // Through broken windows: grains blown in across the opening as the
    // storm outside drives them (along the wind at its pace, at least a
    // little inward, sinking as fast), losing the wind inside over half a
    // second; one that lands skips on along the floor in shrinking hops with
    // what it has left, then lies a moment, so it is seen to roll in rather
    // than the sand on the floor just appearing. As many as the window is
    // wide, the storm hard and the wind blowing (a gale twice a breeze's),
    // some even at a window the wind only brushes.
    for(int i=0;i<windowPaneCount();++i) {
        PaneView p;
        if(!windowPane(i,p)||!letsWeatherIn(p)||p.top-p.bottom<4) continue;
        float in=paneInward(p);
        float ex=p.bx-p.ax,ey=p.by-p.ay,len=std::hypot(ex,ey);
        float mx=(p.ax+p.bx)/2,my=(p.ay+p.by)/2,distance=std::hypot(mx-camera.eye[0],my-camera.eye[1]);
        if(len<4||distance>reach) continue;
        float draft=paneDraft(p),reachIn=0.5f+0.7f*draft;   // a draft through the room carries it in, else it only puffs in
        int count=std::min(96,(int)std::lround(len/2.5f*fallNow*std::max(in,0.25f)*windCarry()*(0.3f+0.7f*draft)));
        float into=hx*p.inX+hy*p.inY;
        float wx=hx*blow,wy=hy*blow;
        if(into*blow<20) {wx+=p.inX*(20-into*blow);wy+=p.inY*(20-into*blow);}
        for(int n=0;n<count;++n) {
            uint32_t seed=p.seed^(uint32_t)n*0x9e3779b9u^0x5d1f3a77u;
            float period=1.6f+0.8f*hashUnit(n,0,seed),clock=now+hashUnit(n,1,seed)*period*5;
            int cycle=(int)std::floor(clock/period);
            float age=clock-cycle*period;
            uint32_t own=seed^(uint32_t)cycle*2654435761u;
            float u=hashUnit(cycle,1,own)*len,z0=p.bottom+(p.top-p.bottom)*(0.1f+0.7f*hashUnit(cycle,2,own));
            float pace=0.8f+0.4f*hashUnit(cycle,3,own),fall=sink*(0.8f+0.4f*hashUnit(cycle,4,own));
            float vx=wx*pace,vy=wy*pace,push=std::hypot(vx,vy);
            auto inside=[&](float t,float &x,float &y) {
                float carried=0.5f*(1-std::exp(-t/0.5f))*reachIn;
                x=p.ax+ex/len*u+vx*carried;y=p.ay+ey/len*u+vy*carried;
            };
            float x,y,z=z0-fall*age;
            inside(age,x,y);
            paneShove(p,x,y,z,now);   // a blast's shove (air_push.cpp)
            const sector_t *at=weatherSectorAt(x,y);
            if(!at||(at!=&sectors[p.frame]&&at!=&sectors[p.inner>=0?p.inner:p.frame]&&at!=&sectors[p.outer])) continue;
            float floor=floorZ(at);
            if(z>floor+1) {
                float speed=push*std::exp(-age/0.5f);
                grain(camera,x,y,z,vx/std::max(push,1.0f),vy/std::max(push,1.0f),speed/TICRATE,distance,weatherColor(*at,x,y,z,tone),0.7f);
                continue;
            }
            // Landed: skipping on with what it has left of the wind, in hops
            // that shrink as it slows, then lying a moment where it stops.
            float landed=std::max(0.0f,z0-floor)/fall,lx,ly;
            inside(landed,lx,ly);
            float since=age-landed,left=std::exp(-landed/0.5f),slide=0.35f*(1-std::exp(-since/0.35f))*left*reachIn;
            float hop=(2+3*hashUnit(cycle,5,own))*std::exp(-since/0.4f)*left,f=std::fmod(since*(3+2*hashUnit(cycle,6,own)),1.0f);
            float sx=lx+vx*slide,sy=ly+vy*slide;
            const sector_t *under=weatherSectorAt(sx,sy);
            if(!under||!(under==at||under==&sectors[p.frame]||(p.inner>=0&&under==&sectors[p.inner])))continue;   // not through a wall
            if(since<1.6f&&!(under-sectors<(ptrdiff_t)sectorMist.size()&&sectorMist[under-sectors].liquid))
                pixelBlock(particleVertices,camera,sx,sy,floorZ(under)+hop*4*f*(1-f),0.45f,1,weatherColor(*under,sx,sy,floorZ(under),tone),true,&albedo,true,nullptr,0.7f*std::min(1.0f,(1.6f-since)/0.6f));
        }
    }
}

// The storm's dust over the open ground (the outdoor haze, c.haze,
// world.frag) in the colour the sky's dust takes (sky/sand.glsl's sandHaze:
// the sand as bright as the horizon), the colour its whiteout (whiteout.cpp)
// fades to: it blew all round the eye and the far streets stood crisp
// through it.
void sandHazeUniforms(Uniforms &camera) {
    float amount=weatherNow(WEATHER_SAND);
    if(amount<=0)return;
    float luma=0.299f*camera.haze[0]+0.587f*camera.haze[1]+0.114f*camera.haze[2];
    std::array<float,3> dust=sandAlbedo();
    for(int c=0;c<3;++c)camera.haze[c]+=(dust[c]*std::max(luma*1.4f,0.1f)-camera.haze[c])*std::min(1.0f,amount*1.25f);
    if(settings.fog)camera.haze[3]=std::max(camera.haze[3],1.5f*amount);
}

namespace {
// A grain riding a draft through the rooms (drafts.cpp): too heavy for the
// room's air to hold up, it skips along the floor in hops a few units
// high, at the tic rate, as outside.
void sandCarried(const Uniforms &camera,float x,float y,float z,const sector_t &sector,uint32_t seed,float alpha) {
    (void)z;
    std::array<float,3> tone=sandAlbedo();
    for(float &v:tone)v=std::min(1.0f,v*1.2f);   // airborne, as buildSand's
    float now=std::floor(gameSeconds()*TICRATE)/TICRATE,f=std::fmod(now*(2+hashUnit(14,15,seed))+hashUnit(16,15,seed),1.0f);
    float floor=floorZ(&sector),hop=floor+1+(2+3*hashUnit(17,15,seed))*4*f*(1-f);
    pixelBlock(particleVertices,camera,x,y,hop,0.45f,1,weatherColor(sector,x,y,hop,tone),false,&albedo,true,nullptr,alpha);
}
// How deep sand lies: blown nearly flat, it piles against the foot of a
// wall facing the storm and drifts deep in a wall's lee near it, where the
// wind drops it, a strip of bare ground right under the wall; further into
// the long shelter only a little, unless the air there is caught out of the
// wind (trapped_air.cpp: a courtyard, a pit): there the storm slows and drops
// what it carries all over, so an enclosed yard lies deep, not bare. Over the
// open ground a layer with deeper patches. Sheltered by a roof's edge or a
// ledge overhead it barely lies.
float sandGround(const WeatherGround &cell) {
    float n=valueNoise(cell.x,cell.y,80,cell.seed),deep=n<0.35f?0.4f:n<0.7f?0.6f:0.85f;
    if(cell.windward)deep=1;
    if(cell.lee) {
        float shelter=0.3f+(deep+0.1f-0.3f)*std::clamp((cell.trapped-0.3f)/0.5f,0.0f,1.0f);
        deep=cell.leeAt<=8||cell.roofed?0.15f:cell.leeAt<64?std::max(deep,0.95f):shelter;
    }
    return deep;
}
void quiet(float) {}
}
// What sand blows in lies as the floor film and keeps its shape: a thin
// layer stays where it lands (under a unit deep nothing moves it), a pile
// slides only where its surface drops twelve units from one cell to the
// next (sand's angle of rest), slowly; it pours slowly off a ledge, never
// goes; four units deep lies full. The gusts drift it a little (loose 0.4).
// It flowed out over whole floors like a liquid before.
const FilmRules sandFilm={0.3f,2,1,12,INFINITY,INFINITY,1,4,true,0.4f};
// Sand blows nearly flat: it sinks a unit for two and a half across, dusts
// the glass in about 15 seconds and lies under a broken window in about 20.
// It is warm out in it, and it never melts. Heavy, it blows thickest low
// (96 units up from the ground in a breeze, twice that in a gale; a third
// as much over that, the storm's dust), and comes in only as the wind
// presses it (no eddies); but a storm is thick, so what gets in lays fast
// (half a unit a second at a full opening) and the wind drives it on
// along the floor: halving every 64 units straight in (further in a gale,
// windCarry), along a draft nearly as far as snow rides one.
const WeatherKind sandWeather={"sand",0.4f,15,20,true,sandGround,buildSand,quiet,nullptr,28,false,sandCarried,sandAlbedo,true,true,
                               &sandFilm,true,0.8f,0,96,0.25f,48};
} // namespace doom3d
