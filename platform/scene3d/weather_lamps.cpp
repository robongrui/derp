/* The static lamps near the eye and what they add to a speck of weather at a
   point. weatherColor (weather.cpp) gives a drop its sector's light and the
   baked light of the floor under it; a drop right in front of a light strip
   high on a wall took nearly nothing from the strip (DOOM 2 MAP03's yard).
   Here it takes the lamps round it, at its own place in the air.

   The lamps are those the bake lights the level with: the glowing wall panels
   and flats (keptMergedLights, shining only ahead of their wall, dimmed or out
   with a flickering or shot-out lamp) and the decoration lights (torches,
   candles, lamp posts: decorationSource, flickering with their flame). They
   are gathered once a tic, only those whose light reaches into the weather
   round the eye, into a coarse grid of world-fixed cells; nothing runs while
   nothing falls or with the F4 switch off (settings.weatherLamps). A speck
   reads its cell's few lamps: the lights' own falloff (lighting.glsl's
   flashAtOpen), no wall tests. In their place a lamp lights only specks in
   its own space: a yard's lamps the weather in that yard, a room's the
   flakes the drafts carry into that room, so a torch behind a wall never
   lights the rain on the other side. Render-only. */
#include "internal.h"
#include "../rooms.h"

namespace doom3d {
namespace {
// Out to the widest weather's reach (rain's 640 units) round the eye.
constexpr float gatherReach=640,cellSize=128;
// Matte specks (snow, sand, ash) take a lamp's light on their albedo over
// its whole reach, as a wall would: a soft ball of coloured flakes round a
// torch. Rain is thin water: it catches a lamp as a glint in the lamp's own
// colour, not dimmed by its mid-grey albedo and brighter than a wall would
// be, but only in a bubble by the lamp (rainLampBubble), not its whole reach: a
// streak against the dark shows with little light.
constexpr float matteGain=1.0f,glintGain=1.2f;
constexpr float mostPerLamp=1.5f;   // a merged lamp's strength can stack; one lamp lights a speck at most this much
// A drop lying toward a lamp as the eye sees it (within some 25 degrees)
// sends its light on toward the eye: up to forwardGain more than one lit
// from the side, the most straight in line.
constexpr float forwardGain=1.0f,forwardCos=0.9f;
using Lamp=WeatherLamp;
std::vector<Lamp> lamps;
std::vector<std::array<float,3>> towardLamp;   // per lamp, the unit way to it from the eye this frame
float eyeAt[3]={0,0,0};
std::vector<int> cellStart,cellLamps;
int gridX=0,gridY=0,gridSide=0;
int gatheredTic=-1;unsigned gatheredLevel=~0u,gatheredSettings=~0u;
// The space a sector lies in for the lamps: its yard outside, its room
// inside; -1 where the room map has none (lights there match any).
int spaceOf(int sector,bool &outside) {
    outside=Rooms_Outside(sector)!=0;
    return outside?Rooms_Yard(sector):Rooms_Of(sector);
}
int sectorAtPoint(float x,float y) {
    return int(R_PointInSubsector((fixed_t)(x*FRACUNIT),(fixed_t)(y*FRACUNIT))->sector-sectors);
}
void addLamp(float x,float y,float z,float radius,float strength,const std::array<float,3> &color,float extent,float nx,float ny,float post,
             int sector,const Uniforms &camera,bool flame=false) {
    if(strength<=0||radius<=0)return;
    if(std::hypot(x-camera.eye[0],y-camera.eye[1])>gatherReach+radius)return;
    Lamp lamp={x,y,z,radius,std::min(strength,mostPerLamp),extent,nx,ny,post,color,false,-1,sector};
    lamp.space=spaceOf(sector,lamp.outside);lamp.flame=flame;
    lamps.push_back(lamp);
}
// Each lamp into every cell its sphere's square touches.
void fillGrid() {
    cellStart.assign((size_t)gridSide*gridSide+1,0);
    auto cells=[&](const Lamp &l,int &x0,int &y0,int &x1,int &y1) {
        x0=std::max(0,(int)std::floor((l.x-l.radius)/cellSize)-gridX);x1=std::min(gridSide-1,(int)std::floor((l.x+l.radius)/cellSize)-gridX);
        y0=std::max(0,(int)std::floor((l.y-l.radius)/cellSize)-gridY);y1=std::min(gridSide-1,(int)std::floor((l.y+l.radius)/cellSize)-gridY);
    };
    int x0,y0,x1,y1;
    for(const Lamp &l:lamps) {
        cells(l,x0,y0,x1,y1);
        for(int y=y0;y<=y1;++y)for(int x=x0;x<=x1;++x)++cellStart[(size_t)y*gridSide+x+1];
    }
    for(size_t i=1;i<cellStart.size();++i)cellStart[i]+=cellStart[i-1];
    cellLamps.assign(cellStart.back(),0);
    std::vector<int> next(cellStart.begin(),cellStart.end()-1);
    for(int n=0;n<(int)lamps.size();++n) {
        cells(lamps[n],x0,y0,x1,y1);
        for(int y=y0;y<=y1;++y)for(int x=x0;x<=x1;++x)cellLamps[next[(size_t)y*gridSide+x]++]=n;
    }
}
}

const std::vector<WeatherLamp> &weatherLamps() {return lamps;}
// The way from the eye to each lamp, each frame: the eye moves within a tic.
static void aimFromEye(const Uniforms &camera) {
    for(int c=0;c<3;++c)eyeAt[c]=camera.eye[c];
    towardLamp.resize(lamps.size());
    for(size_t i=0;i<lamps.size();++i) {
        float d[3]={lamps[i].x-eyeAt[0],lamps[i].y-eyeAt[1],lamps[i].z-eyeAt[2]},n=std::sqrt(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]);
        towardLamp[i]=n>1?std::array<float,3>{d[0]/n,d[1]/n,d[2]/n}:std::array<float,3>{0,0,0};
    }
}

void gatherWeatherLamps(const Uniforms &camera) {
    if(gatheredTic==leveltime&&gatheredLevel==r_levelserial&&gatheredSettings==settingsSerial) {aimFromEye(camera);return;}
    gatheredTic=leveltime;gatheredLevel=r_levelserial;gatheredSettings=settingsSerial;
    lamps.clear();cellLamps.clear();cellStart.clear();
    if(!settings.weatherLamps)return;
    // Glowing panels and flats, as their fog glow shines (surface_lights.cpp).
    if(settings.emissive)for(const SurfaceLight &light:keptMergedLights()) {
        if(light.strength<=0||light.shine<=0)continue;
        auto n=panelNormal(light);
        // Its space: a little ahead of its wall, in the place it lights.
        int sector=sectorAtPoint(light.x+n[0]*8,light.y+n[1]*8);
        addLamp(light.x,light.y,light.z,light.radius,light.strength*light.shine,light.color,std::min(light.extent,light.radius),n[0],n[1],0,sector,camera);
    }
    // Torches, candles and lamp posts, each with its flame to the tic.
    for(int i=0;i<numsectors;++i)for(const mobj_t *thing=sectors[i].thinglist;thing;thing=thing->snext) {
        if(std::hypot(units(thing->x)-camera.eye[0],units(thing->y)-camera.eye[1])>gatherReach+320||!decorationLight(*thing))continue;
        BakeLight light=decorationSource(*thing);
        addLamp(light.x,light.y,light.z,light.radius,light.strength*decorationFlicker(*thing),{light.color[0],light.color[1],light.color[2]},
                0,0,0,light.post,i,camera,light.post<=0);
    }
    aimFromEye(camera);
    if(lamps.empty())return;
    float half=gatherReach+cellSize;
    gridX=(int)std::floor((camera.eye[0]-half)/cellSize);gridY=(int)std::floor((camera.eye[1]-half)/cellSize);
    gridSide=(int)std::ceil(2*half/cellSize)+1;
    fillGrid();
}

void weatherLampLight(const sector_t &sector,float x,float y,float z,LampCatch kind,const std::array<float,3> &albedo,bool daylit,float light[3],
                      float *caught) {
    if(kind==LampCatch::None||lamps.empty())return;
    int cx=(int)std::floor(x/cellSize)-gridX,cy=(int)std::floor(y/cellSize)-gridY;
    if(cx<0||cy<0||cx>=gridSide||cy>=gridSide)return;
    size_t cell=(size_t)cy*gridSide+cx;
    if(cellStart[cell]==cellStart[cell+1])return;
    bool outside;
    int space=spaceOf(int(&sector-sectors),outside);
    bool glint=kind==LampCatch::Glint;
    // Drawn at the daylight's scale (flat.frag's MODE_PARTICLE_DAYLIT), the
    // lamp's share is taken out of it again: night snow by a torch burned out.
    float gain=(glint?glintGain:matteGain)/(daylit?daylightScale():1.0f);
    // The way to the speck from the eye, for the forward glint.
    float seen[3]={x-eyeAt[0],y-eyeAt[1],z-eyeAt[2]},far=std::sqrt(seen[0]*seen[0]+seen[1]*seen[1]+seen[2]*seen[2]);
    if(far>1)for(float &v:seen)v/=far;
    float lit=0;
    for(int k=cellStart[cell];k<cellStart[cell+1];++k) {
        const Lamp &l=lamps[cellLamps[k]];
        if(l.outside!=outside||(l.space!=space&&l.space>=0&&space>=0))continue;
        // From the nearest point of a panel's glowing span (extent), else its centre.
        float dx=x-l.x,dy=y-l.y,dz=z-l.z;
        float along=std::max(0.0f,std::abs(dz)-l.extent),d2=dx*dx+dy*dy+along*along;
        float reach=glint?rainLampBubble(l.radius,l.flame):l.radius;
        if(d2>=reach*reach)continue;
        float d=std::sqrt(d2),f=1-d/reach,amount=l.strength*f*f*(3-2*f);
        amount*=doom_panel_emission(dx,dy,dz,l.nx,l.ny,nullptr);   // ahead of its wall only
        if(l.post>0)amount*=bakePostShare(d>0.001f?dz/d:0);   // a post lamp shines out, not up or down
        if(amount<=0)continue;
        if(glint&&l.flame)amount*=flameRain;
        if(glint&&far>1) {
            const auto &to=towardLamp[cellLamps[k]];
            float f=std::clamp((seen[0]*to[0]+seen[1]*to[1]+seen[2]*to[2]-forwardCos)/(1-forwardCos),0.0f,1.0f);
            amount*=1+forwardGain*f*f;
        }
        lit+=amount;
        for(int c=0;c<3;++c)light[c]+=l.color[c]*amount*gain*(glint?1.0f:albedo[c]);
    }
    if(caught)*caught=std::min(1.0f,lit);
}
} // namespace doom3d
