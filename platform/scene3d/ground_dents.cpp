/* Dents in the ground's bake out under the sky: what disturbances
   (disturbances.cpp: blasts, steps, bodies, shots) do to the snow, sand,
   ash or wet lying outside, as signed offsets from the bake (weather.cpp's
   bakeWetness), which stays the resting state. Only on sky and sheltered
   cells (Rooms_Outside); indoors the floor film takes them.
   - A dent takes what lies within its radius away, ragged a cell at a
     time, and throws half of it onto a ragged rim (snow, sand, ash: a
     raised edge, an offset the other way). Rain has nothing to throw: a
     blast dries a circle instead, darkening again as the rain wets it.
   - The weather fills them back in, each cell as fast as its kind falls on
     it times how deep the bake lays it there: a crater in the open yard
     fills quickly, one under a porch's edge slowly, the bare back of a porch
     never (there was nothing). Snow and ash as fast as they fall, sand as
     hard as the wind blows, rain as hard as it rains; when it stops,
     craters and prints stay. What was thrown up onto the ground goes again
     slowly.
   - Stored sparsely (the dented cells only), stepped every eight tics,
     written into the floor light's spare alpha, the changed tiles uploaded.
     No dents, no cost: it grows with the dents, never with the outside.
     The offsets outlive a re-bake round a door or lift (dentedAlpha).
   Render-only, with its own random numbers. */
#include "internal.h"
#include "../rooms.h"

namespace doom3d {
namespace {
std::unordered_map<size_t,float> dents;   // map cell: offset from the bake, in its 0-1 depth
unsigned level=~0u;
Weather dentKind=WEATHER_NONE;
constexpr int tileCells=32;
std::vector<char> dirty;
std::vector<std::array<int,4>> rects;
void mark(size_t cell) {
    int across=(mapWidth+tileCells-1)/tileCells;
    if(dirty.size()!=(size_t)across*((mapHeight+tileCells-1)/tileCells))dirty.assign((size_t)across*((mapHeight+tileCells-1)/tileCells),0);
    int tx=(int)(cell%mapWidth)/tileCells,ty=(int)(cell/mapWidth)/tileCells;
    char &d=dirty[(size_t)ty*across+tx];
    if(d)return;
    d=1;
    int rx=tx*tileCells,ry=ty*tileCells;
    rects.push_back({rx,ry,std::min(tileCells,mapWidth-rx),std::min(tileCells,mapHeight-ry)});
}
// How fast the lying kind fills a dent back in, in its depth a second
// where the bake lays it full; and whether anything lies to dent.
float refill(Weather kind) {
    switch(kind) {
    case WEATHER_SNOW: return 0.02f*fallNow;
    case WEATHER_ASH: return 0.03f*fallNow;
    case WEATHER_SAND: return 0.05f*windAmount()*std::min(1.0f,fallNow*2);
    case WEATHER_RAIN: case WEATHER_NUKAGE: return 0.1f*fallNow;
    default: return 0;
    }
}
bool dentable(Weather kind) {return kind==WEATHER_SNOW||kind==WEATHER_ASH||kind==WEATHER_SAND||kind==WEATHER_RAIN||kind==WEATHER_NUKAGE;}
bool ready() {return level==r_levelserial&&flatLightCells.size()>=(size_t)mapWidth*mapHeight*4&&seamCells.size()>=(size_t)mapWidth*mapHeight;}
// Repaints the marked tiles' outside cells: indoor cells are the floor
// film's to paint (floor_film.cpp), and painting the bake over them made
// fresh film blink.
void paintMarked() {
    for(const auto &r:rects)for(int gy=r[1];gy<r[1]+r[3];++gy)for(int gx=r[0];gx<r[0]+r[2];++gx) {
        size_t i=(size_t)gy*mapWidth+gx;
        int own=seamCells[i].own;
        if(own==noSector||!Rooms_Outside(own))continue;
        flatCell(floorLight,i)[3]=dentedAlpha(i,groundBakeAt(i));
    }
}
uint8_t alphaOf(uint8_t baked,float offset) {
    float depth=(baked?(baked-1)/254.0f:0.0f)+offset;
    if(depth<=0.004f)return baked?1:0;   // dug bare (still in the weather's reach)
    return (uint8_t)(1+std::lround(std::min(depth,1.0f)*254));
}
}

uint8_t dentedAlpha(size_t cell,uint8_t baked) {
    if(dents.empty()||level!=r_levelserial)return baked;
    auto found=dents.find(cell);
    return found==dents.end()?baked:alphaOf(baked,found->second);
}

void dentGround(float x,float y,float radius,float strength,uint32_t seed) {
    Weather kind=falling();
    if(level!=r_levelserial) {dents.clear();level=r_levelserial;}
    if(!dentable(kind)||!ready()||radius<=0)return;
    if(kind!=dentKind) {dents.clear();dentKind=kind;}
    bool settles=weatherKind(kind).settles;
    if(!settles&&strength<0.8f)return;   // rain: only a blast dries a circle
    int cx=(int)std::floor((x-mapOrigin[0])/mapCell),cy=(int)std::floor((y-mapOrigin[1])/mapCell);
    int reach=(int)std::ceil(radius*1.45f/mapCell)+1;
    const sector_t *home=weatherSectorAt(x,y);
    float homeFloor=home?floorZ(home):0;
    float taken=0;
    std::vector<size_t> rim;
    for(int gy=cy-reach;gy<=cy+reach;++gy)for(int gx=cx-reach;gx<=cx+reach;++gx) {
        if(gx<0||gy<0||gx>=mapWidth||gy>=mapHeight)continue;
        size_t i=(size_t)gy*mapWidth+gx;
        int own=seamCells[i].own;
        if(own==noSector||!Rooms_Outside(own)||std::fabs(floorZ(&sectors[own])-homeFloor)>24)continue;   // its own ground, not a ledge
        float px=mapOrigin[0]+(gx+0.5f)*mapCell,py=mapOrigin[1]+(gy+0.5f)*mapCell;
        float d=std::hypot(px-x,py-y)/radius*(0.8f+0.4f*hashUnit(gx,gy,seed));
        uint8_t baked=groundBakeAt(i);
        float &offset=dents[i];
        if(d<1) {
            float lies=(baked?(baked-1)/254.0f:0.0f)+offset,take=std::max(0.0f,lies)*std::min(1.0f,strength*(1.3f-d));
            offset-=take;taken+=take;mark(i);
        }
        else if(d<1.45f&&settles)rim.push_back(i);
        if(offset==0)dents.erase(i);
    }
    if(!rim.empty()&&taken>0) {
        float weight=0;
        for(size_t i:rim)weight+=0.4f+hashUnit((int)i,7,seed);
        for(size_t i:rim) {dents[i]+=0.5f*taken*(0.4f+hashUnit((int)i,7,seed))/weight;mark(i);}
    }
    paintMarked();
    if(flatLightTexture&&!rects.empty())gpuUpdateTexture(flatLightTexture,GpuFormat::RGBA8,rects,flatLightCells.data(),mapWidth*4);
    rects.clear();std::fill(dirty.begin(),dirty.end(),0);
}

// Every eight tics: each dent filled back in (or what was thrown up worn
// away) by its kind's rate, the changed tiles painted and uploaded; the
// filled ones forgotten.
void watchGroundDents() {
    if(gamestate!=GS_LEVEL||dents.empty())return;
    if(level!=r_levelserial||falling()!=dentKind) {dents.clear();return;}
    if(leveltime%8||!ready())return;
    constexpr float dt=8.0f/TICRATE;
    float rate=refill(dentKind);
    for(auto i=dents.begin();i!=dents.end();) {
        float &offset=i->second,was=offset;
        uint8_t baked=groundBakeAt(i->first);
        if(offset<0)offset=std::min(0.0f,offset+rate*(baked?(baked-1)/254.0f:0.0f)*dt);
        else offset=std::max(0.0f,offset-(0.3f*rate+0.002f)*dt);
        if(alphaOf(baked,offset)!=alphaOf(baked,was))mark(i->first);
        if(std::fabs(offset)<0.004f) {offset=0;mark(i->first);}
        ++i;
    }
    paintMarked();
    for(auto i=dents.begin();i!=dents.end();)i=i->second==0?dents.erase(i):std::next(i);
    if(flatLightTexture&&!rects.empty())gpuUpdateTexture(flatLightTexture,GpuFormat::RGBA8,rects,flatLightCells.data(),mapWidth*4);
    rects.clear();std::fill(dirty.begin(),dirty.end(),0);
}

// After the bake was written over the alpha (bakeWetness): the dents back over it.
void dentsPaint() {
    if(dents.empty()||!ready())return;
    for(const auto &[cell,offset]:dents)flatCell(floorLight,cell)[3]=alphaOf(groundBakeAt(cell),offset);
}

void groundDentsShutdown() {dents.clear();dirty.clear();rects.clear();level=~0u;dentKind=WEATHER_NONE;}
} // namespace doom3d
