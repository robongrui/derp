/* Water freezing in the cold (not on Phobos, which ices its own pools,
   decompression.cpp): how far each map cell of water has frozen, 0 open water
   to 1 a white sheet, from the air's temperature there. Out under the sky the
   outside's; in a room the drafts' (the rooms' temperatures and the cold jets
   in from the openings, drafts.cpp), or, before they are solved or while
   nothing falls, the outside's in the open halls, tunnels and porches and a
   room's warmth elsewhere. Smoothed along the water only, so the ice thins out
   over some hundred units into floes and open water instead of stopping at a
   sector's edge: a sewer open to the cold at one end freezes there and runs
   free further in. Only water: nukage, slime, blood and lava keep flowing.
   Worked out on events only (a level, a change of temperature, the drafts'
   first solve), then it stays. The shaders read it from the ceiling light's
   alpha (bake_atlas.cpp's flatLight, a channel nothing else uses there);
   world.frag draws the bands. */
#include "internal.h"
#include "../rooms.h"
extern "C" {
#include "../audio/frozen.h"
#include "../audio/ambience_spots.h"
}

namespace doom3d {
namespace {
constexpr float roomWarm=10;    // C: a room the drafts haven't solved, heated
constexpr float frozenAt=-10;   // C: the whole way to a white sheet; 0 C none
constexpr int smoothing=12;     // passes along the water: some hundred units of fade
constexpr int closing=6;        // cells: open patches up to twice this across freeze over
std::vector<float> field,sectorMax;
unsigned builtLevel=~0u;int builtOutside=1<<30;bool builtSolved=false;int lastCheck=-1;
bool waterSector(int s) {return liquidLook(sectors[s].floorpic)==LIQUID_LOOK_WATER&&sectors[s].floorpic!=skyflatnum;}
float cellTemperature(int s,float x,float y,float outside) {
    if(Rooms_Outside(s))return outside;
    float t=airTemperature(x,y);   // the rooms and the jets from the openings (NAN while nothing falls)
    if(!std::isnan(t))return t;
    room_kind kind=Rooms_Kind(s);
    return kind==ROOM_INDOOR&&!Rooms_Mouth(s)?roomWarm:outside;
}
bool draftsSolved() {for(int r=0;r<Rooms_Count();++r)if(!std::isnan(draftTemperature(r)))return true;return false;}
// Into the ceiling light's alpha, and up to the texture when it is there.
void writeField(bool upload) {
    if(flatLightCells.size()<(size_t)4*4*mapWidth*mapHeight)return;
    for(size_t i=0;i<(size_t)mapWidth*mapHeight;++i)
        flatCell(ceilingLight,i)[3]=i<field.size()?(uint8_t)std::lround(std::clamp(field[i],0.0f,1.0f)*255):0;
    if(upload&&flatLightTexture)
        gpuUpdateTexture(flatLightTexture,GpuFormat::RGBA8,{{0,ceilingLight*mapHeight,mapWidth,mapHeight}},flatLightCells.data(),mapWidth*4);
}
void build(float outside,bool solved) {
    builtLevel=r_levelserial;builtOutside=(int)std::lround(outside);builtSolved=solved;
    size_t count=(size_t)mapWidth*mapHeight;
    field.assign(count,0);sectorMax.assign(numsectors,0);
    if(Game_Phobos()||outside>=0&&!solved)return;   // nothing cold enough anywhere
    std::vector<char> water(count,0);
    for(size_t i=0;i<count;++i) {
        int own=seamCells[i].own;
        if(own==noSector||!waterSector(own))continue;
        water[i]=1;
        float x=mapOrigin[0]+(i%mapWidth+0.5f)*mapCell,y=mapOrigin[1]+(i/mapWidth+0.5f)*mapCell;
        field[i]=std::clamp(cellTemperature(own,x,y,outside)/frozenAt,0.0f,1.0f);
    }
    // Closed first: a stretch of water no wider than closing cells both ways
    // in the middle of the ice (a sector read warmer than its neighbours: a
    // doorway, a step) freezes with what lies round it, so no open patch is
    // left in a frozen channel. Dilated, then eroded, along the water.
    std::vector<float> next(count);
    auto filter=[&](bool grow) {
        for(int axis=0;axis<2;++axis) {
            for(size_t i=0;i<count;++i) {
                next[i]=field[i];
                if(!water[i])continue;
                int x=int(i%mapWidth),y=int(i/mapWidth);
                for(int d=-closing;d<=closing;++d) {
                    int nx=axis?x:x+d,ny=axis?y+d:y;
                    if(nx<0||ny<0||nx>=mapWidth||ny>=mapHeight)continue;
                    size_t n=(size_t)ny*mapWidth+nx;
                    if(water[n])next[i]=grow?std::max(next[i],field[n]):std::min(next[i],field[n]);
                }
            }
            field.swap(next);
        }
    };
    filter(true);filter(false);
    for(int pass=0;pass<smoothing;++pass) {
        for(size_t i=0;i<count;++i) {
            if(!water[i]) {next[i]=0;continue;}
            int x=int(i%mapWidth),y=int(i/mapWidth);float sum=field[i];int n=1;
            if(x>0&&water[i-1]) {sum+=field[i-1];++n;}
            if(x+1<mapWidth&&water[i+1]) {sum+=field[i+1];++n;}
            if(y>0&&water[i-mapWidth]) {sum+=field[i-mapWidth];++n;}
            if(y+1<mapHeight&&water[i+mapWidth]) {sum+=field[i+mapWidth];++n;}
            next[i]=sum/n;
        }
        field.swap(next);
    }
    int cells=0,iced=0;
    for(size_t i=0;i<count;++i)if(water[i]) {
        sectorMax[seamCells[i].own]=std::max(sectorMax[seamCells[i].own],field[i]);
        ++cells;iced+=field[i]>=0.35f;
    }
    if(cells)fprintf(stderr,"3D water ice: %d of %d water cells iced, outside %.0f C, rooms %s.\n",iced,cells,outside,solved?"solved":"not solved");
}
bool ready() {return builtLevel==r_levelserial&&field.size()==(size_t)mapWidth*mapHeight&&sectorMax.size()==(size_t)numsectors;}
}
// Built for this level if it isn't yet (on the main thread: the reads below don't build it).
void ensureWaterIce() {
    if(ready()||seamCells.size()<(size_t)mapWidth*mapHeight||mapWidth<=0)return;
    build(climateTemperature(falling()),draftsSolved());
}

// A second a check: a new level, the temperature moved, or the drafts' first solve.
void watchWaterIce() {
    // The made-up sounds (audio/frozen.h): steps on the ice, no lapping water, no drips in freezing air.
    Frozen_Water=[](float x,float y)->int {return waterIceHolds(x,y);};
    Frozen_Air=[](float x,float y)->int {return airFreezing(x,y);};
    if(gamestate!=GS_LEVEL||seamCells.size()<(size_t)mapWidth*mapHeight||mapWidth<=0)return;
    bool fresh=!ready();
    if(!fresh&&lastCheck>=0&&leveltime-lastCheck<TICRATE&&leveltime>=lastCheck)return;
    lastCheck=leveltime;
    float outside=climateTemperature(falling());bool solved=draftsSolved();
    if(!fresh&&builtOutside==(int)std::lround(outside)&&(builtSolved||!solved))return;
    build(outside,solved);
    writeField(true);
    ambience_built=~0u;   // the water's sounds read anew (audio/ambience_map.c)
    if(!fresh)buildCaustics();   // and its light, off the open water only (caustics.cpp)
}
// For uploadFlatLight: the field into the ceiling light's alpha as the bakes are made anew.
void writeWaterIce() {ensureWaterIce();if(ready())writeField(false);}
// How far the water at (x, y) has frozen (0-1, between the cells), 0 off the water.
float waterIceAt(float x,float y) {
    if(!ready())return 0;
    float gx=(x-mapOrigin[0])/mapCell-0.5f,gy=(y-mapOrigin[1])/mapCell-0.5f;
    int x0=(int)std::floor(gx),y0=(int)std::floor(gy);float fx=gx-x0,fy=gy-y0,sum=0;
    for(int n=0;n<4;++n) {
        int cx=std::clamp(x0+(n&1),0,mapWidth-1),cy=std::clamp(y0+(n>>1),0,mapHeight-1);
        sum+=field[(size_t)cy*mapWidth+cx]*((n&1)?fx:1-fx)*((n>>1)?fy:1-fy);
    }
    return sum;
}
float waterIceCell(size_t cell) {return ready()&&cell<field.size()?field[cell]:0;}
float waterIceMax(int sector) {return ready()&&sector>=0&&sector<numsectors?sectorMax[sector]:0;}
// Where it is ice that holds (thin ice and the sheet, world.frag's bands): no splash, no ring.
bool waterIceHolds(float x,float y) {return waterIceAt(x,y)>=0.55f;}
// Whether the air at (x, y) is below freezing, as the ice reads it.
bool airFreezing(float x,float y) {
    if(Game_Phobos()||seamCells.empty()||mapWidth<=0)return false;
    int cx=std::clamp((int)std::floor((x-mapOrigin[0])/mapCell),0,mapWidth-1),cy=std::clamp((int)std::floor((y-mapOrigin[1])/mapCell),0,mapHeight-1);
    int own=seamCells[(size_t)cy*mapWidth+cx].own;
    return own!=noSector&&cellTemperature(own,x,y,climateTemperature(falling()))<0;
}
void waterIceShutdown() {field.clear();sectorMax.clear();builtLevel=~0u;lastCheck=-1;}
} // namespace doom3d
