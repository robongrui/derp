/* When the glowing tiles (surface_lights.cpp) stand as last collected.
   Collecting them walked every sector's floor and ceiling and every wall
   piece, hidden ones too, and merging them sorted them through maps, every
   frame, though most frames nothing they are made of changes. They stand
   while all of it holds: the level and settings, each sector's flats, each
   side's textures (as animated, and those that fill a missing one) and
   offsets, the lamps a switch set (lampLevelSerial) and those shot out once
   the newest has stopped sputtering (brokenTilesSteady); and the heights as
   drawn of the sectors that hold a glowing flat or border a side that may
   show a glowing texture (a door elsewhere moving changes no tile).
   Phobos' freezing pools and the library's storeys (cut by where the eye
   stands) change them by other means: there they are collected every
   frame, as before. A flickering sector's lamps keep their tiles; only
   their shine follows the sector, each frame, as surfaceLight sets it. */
#include "internal.h"

namespace doom3d {
namespace {
// What the tiles were collected from, as numbers, but for the heights; kept:
// last collection's. Then the sectors whose heights count, and those heights.
std::vector<uint32_t> keptState,nowState;
std::vector<int> heightSectors;
std::vector<uint32_t> keptHeights,nowHeights;
bool keptValid=false;
unsigned listSerial=0;   // changes whenever the tiles are collected anew
// The kept tiles' merged lights, and the merged light each tile joins.
std::vector<SurfaceLight> merged;
std::vector<int> joins;
std::vector<float> shineSums,weightSums;
unsigned mergedList=~0u,mergedGroups=~0u;
bool mergedKept=false,anyFlicker=false;
void put(uint32_t v) {nowState.push_back(v);}
// A texture as a side shows it now (its animation frame); 0 for none.
int shown(int texture) {return texture>0&&texture<numtextures?texturetranslation[texture]:0;}
bool glows(int texture) {return texture>0&&wallImage(texture).emissionWeight>0;}
// The heights as drawn (blended between tics while a sector moves) of the
// sectors that count.
void heightsNow(std::vector<uint32_t> &out) {
    out.clear();
    for(int i:heightSectors)for(float z:{floorZ(&sectors[i]),ceilZ(&sectors[i])}) {
        uint32_t v;std::memcpy(&v,&z,sizeof v);out.push_back(v);
    }
}
// The sectors whose heights shape the tiles: those with a glowing flat (by
// its own frame, as collectFlatLights takes it) and both sides of every
// line with a side that may show a glowing texture, filled in or not.
void findHeightSectors() {
    std::vector<char> counts(numsectors,0);
    for(int i=0;i<numsectors;++i)for(int ceiling=0;ceiling<2;++ceiling) {
        int flat=ceiling?sectors[i].ceilingpic:sectors[i].floorpic;
        if(flat!=skyflatnum&&lumpImage(firstflat+flat,true).emissionWeight!=0)counts[i]=1;
    }
    for(int i=0;i<numlines;++i) {
        const line_t &line=lines[i];
        bool any=false;
        for(int side=0;side<2&&!any;++side) {
            if(line.sidenum[side]<0)continue;
            const side_t &s=sides[line.sidenum[side]];
            for(int texture:{(int)s.midtexture,(int)s.toptexture,(int)s.bottomtexture,wallFillTexture(line,side,false),wallFillTexture(line,side,true)})
                any=any||glows(shown(texture));
        }
        if(!any)continue;
        for(const sector_t *s:{line.frontsector,line.backsector})if(s)counts[s-sectors]=1;
    }
    heightSectors.clear();
    for(int i=0;i<numsectors;++i)if(counts[i])heightSectors.push_back(i);
}
}

bool keepSurfaceLights(bool storeys) {
    // (With glowing textures off there are none to collect: nothing to keep either.)
    if(!settings.emissive||storeys||Game_Phobos()||!brokenTilesSteady()) {keptValid=false;++listSerial;return false;}
    nowState.clear();
    put(r_levelserial);put(settingsSerial);put(lampLevelSerial());put(brokenTileSerial());
    put((uint32_t)numsectors);put((uint32_t)numlines);put((uint32_t)numtextures);
    for(int i=0;i<numsectors;++i) {put((uint32_t)sectors[i].floorpic);put((uint32_t)sectors[i].ceilingpic);}
    for(int i=0;i<numlines;++i)for(int side=0;side<2;++side) {
        const line_t &line=lines[i];
        if(line.sidenum[side]<0) {put(~0u);continue;}
        const side_t &s=sides[line.sidenum[side]];
        put((uint32_t)s.textureoffset);put((uint32_t)s.rowoffset);
        put((uint32_t)s.toptexture);put((uint32_t)s.bottomtexture);put((uint32_t)s.midtexture);
        put((uint32_t)shown(s.toptexture));put((uint32_t)shown(s.bottomtexture));put((uint32_t)shown(s.midtexture));
        put((uint32_t)shown(wallFillTexture(line,side,false)));put((uint32_t)shown(wallFillTexture(line,side,true)));
    }
    if(!keptValid||nowState!=keptState) {
        keptState.swap(nowState);keptValid=true;++listSerial;
        findHeightSectors();heightsNow(keptHeights);
        return false;
    }
    heightsNow(nowHeights);
    if(nowHeights!=keptHeights) {keptHeights.swap(nowHeights);++listSerial;return false;}
    for(SurfaceLight &tile:surfaceLights)if(tile.flickerSector>=0)tile.shine=lampShine(tile.flickerSector);
    return true;
}

// The merge (mergedSurfaceLights) of tiles kept and grouped as before gives
// the same lights but for their shine, the members' shine weighted by
// strength: summed again here in the merge's own order, so it comes out
// to the bit. Merged anew whenever the tiles were collected anew or any cell
// was grouped anew (the bake groups its own tiles too), and every frame
// before the bake map is laid out, when the tiles go ungrouped.
const std::vector<SurfaceLight> &keptMergedLights() {
    bool ready=bakeMap.sectors.size()==(size_t)numsectors;
    if(mergedKept&&ready&&mergedList==listSerial&&mergedGroups==surfaceGroupSerial()&&joins.size()==surfaceLights.size()) {
        if(anyFlicker) {
            shineSums.assign(merged.size(),0.0f);weightSums.assign(merged.size(),0.0f);
            for(size_t t=0;t<surfaceLights.size();++t) {
                const SurfaceLight &light=surfaceLights[t];
                float w=std::max(light.strength,0.001f);
                shineSums[joins[t]]+=light.shine*w;weightSums[joins[t]]+=w;
            }
            for(size_t m=0;m<merged.size();++m)merged[m].shine=shineSums[m]/weightSums[m];
        }
        return merged;
    }
    merged=mergedSurfaceLights(surfaceLights);
    mergedKept=false;anyFlicker=false;
    if(!ready)return merged;
    mergedList=listSerial;mergedGroups=surfaceGroupSerial();
    joins.resize(surfaceLights.size());
    for(size_t t=0;t<surfaceLights.size();++t) {
        SurfaceLightKey cell=surfaceCellOf(surfaceLights[t]);
        auto at=std::lower_bound(merged.begin(),merged.end(),cell,[](const SurfaceLight &m,const SurfaceLightKey &k){return m.key<k;});
        if(at==merged.end()||at->key!=cell)return merged;   // not where the merge put it: merged anew each frame
        joins[t]=int(at-merged.begin());
        anyFlicker|=surfaceLights[t].flickerSector>=0;
    }
    mergedKept=true;
    return merged;
}

void forgetKeptSurfaceLights() {
    keptValid=false;mergedKept=false;++listSerial;keptState.clear();nowState.clear();merged.clear();joins.clear();
}
} // namespace doom3d
