/* Light from glowing wall and floor textures, grouped into tiles that
   see each other. */
#include "internal.h"
extern "C" {
}

namespace doom3d {
std::vector<SurfaceLight> surfaceLights;
static std::map<SurfaceLightKey,int> surfaceGroups; // Tile key to visibility group (groupSurfaceLights).
float surfaceEye[3];
SurfaceLightSelection surfaceSelection;
uint64_t surfaceLightTime;
struct FlatLightSample { Point point; int u,v; };
static std::map<std::pair<int,int>,std::vector<FlatLightSample>> flatLightSamples;
// A wall light's normal from its merge group (facing 2+ the normal's
// octant), for doom_panel_emission; flat lights have none.
std::array<float,2> panelNormal(const SurfaceLight &light) {
    if(light.facing<2)return {0,0};
    float angle=(light.facing-2)*(float)(M_PI/4);
    return {std::cos(angle),std::sin(angle)};
}
// Visibility groups of emissive tiles (SurfaceLight::group), per level: in
// each merge cell, a tile joins the first group whose first tile it sees
// along a bake ray, else starts its own. Cells are regrouped whenever a tile
// first appears (a moving door reshapes its wall pieces); the bake map's
// heights at that moment decide.
static bool tileSees(const SurfaceLight &a,const SurfaceLight &b) {
    float d[3]={b.x-a.x,b.y-a.y,b.z-a.z},length=std::sqrt(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]);
    if(length<1)return true;
    for(float &c:d)c/=length;
    int sector=(int)(R_PointInSubsector((fixed_t)(a.x*FRACUNIT),(fixed_t)(a.y*FRACUNIT))->sector-sectors);
    return traceRay(bakeMap,sector,a.x,a.y,a.z,d,length-0.5f,SkyCeiling::open);
}
static void groupSurfaceLights(std::vector<SurfaceLight> &tiles) {
    if(bakeMap.sectors.size()!=(size_t)numsectors)return;
    std::set<SurfaceLightKey> stale;
    for(auto &tile:tiles) {
        auto known=surfaceGroups.find(tile.key);
        if(known!=surfaceGroups.end())tile.group=known->second;
        else {tile.group=0;stale.insert(surfaceCell(tile));}
    }
    if(stale.empty())return;
    std::map<SurfaceLightKey,std::vector<SurfaceLight*>> cells;
    for(auto &tile:tiles) {
        SurfaceLight plain=tile;plain.group=0;
        SurfaceLightKey cell=surfaceCell(plain);
        if(stale.count(cell))cells[cell].push_back(&tile);
    }
    for(auto &entry:cells) {
        auto &members=entry.second;
        std::sort(members.begin(),members.end(),[](const SurfaceLight *a,const SurfaceLight *b){return a->key<b->key;});
        std::vector<const SurfaceLight*> seeds;
        std::vector<std::array<float,4>> sums; // Each group's weighted position sum and weight.
        auto centerOf=[](const SurfaceLight &like,const std::array<float,4> &sum) {
            SurfaceLight center=like;center.x=sum[0]/sum[3];center.y=sum[1]/sum[3];center.z=sum[2]/sum[3];
            return center;
        };
        // A tile joins the first group whose first tile it sees; strict, it
        // also needs the group's center with it added in view of both.
        auto seed=[&](const std::vector<SurfaceLight*> &tiles,size_t first,bool strict) {
            for(SurfaceLight *tile:tiles) {
                float w=std::max(tile->strength,0.001f);
                std::array<float,4> own={tile->x*w,tile->y*w,tile->z*w,w};
                int group=-1;
                for(size_t n=first;n<seeds.size()&&group<0;++n) {
                    if(!tileSees(*tile,*seeds[n]))continue;
                    if(strict) {
                        std::array<float,4> with=sums[n];for(int c=0;c<4;++c)with[c]+=own[c];
                        SurfaceLight center=centerOf(*tile,with);
                        if(!tileSees(*tile,center)||!tileSees(*seeds[n],center))continue;
                    }
                    group=(int)n;
                }
                if(group<0) {group=(int)seeds.size();seeds.push_back(tile);sums.push_back({});}
                for(int c=0;c<4;++c)sums[group][c]+=own[c];
                tile->group=group;
            }
        };
        seed(members,0,false);
        // The merged light stands at its group's weighted center (as in
        // mergeSurfaceLights); tiles that cannot see it, around a corner of
        // an odd-shaped room or pool, regroup strictly among themselves, and
        // any still cut off stand alone.
        for(int pass=0;pass<6;++pass) {
            std::vector<SurfaceLight*> cut;
            for(SurfaceLight *tile:members)
                if(!tileSees(*tile,centerOf(*tile,sums[tile->group])))cut.push_back(tile);
            if(cut.empty())break;
            for(SurfaceLight *tile:cut) {
                float w=std::max(tile->strength,0.001f);
                auto &sum=sums[tile->group];sum[0]-=tile->x*w;sum[1]-=tile->y*w;sum[2]-=tile->z*w;sum[3]-=w;
            }
            if(pass<3)seed(cut,seeds.size(),true);
            else for(SurfaceLight *tile:cut) {
                float w=std::max(tile->strength,0.001f);
                tile->group=(int)seeds.size();seeds.push_back(tile);sums.push_back({tile->x*w,tile->y*w,tile->z*w,w});
            }
        }
        for(const SurfaceLight *tile:members)surfaceGroups[tile->key]=tile->group;
    }
}
SurfaceLightKey surfaceCellOf(const SurfaceLight &tile) {
    SurfaceLight grouped=tile;
    auto known=surfaceGroups.find(tile.key);
    grouped.group=known!=surfaceGroups.end()?known->second:0;
    return surfaceCell(grouped);
}
// Merged emissive lights, kept apart where walls divide their tiles.
std::vector<SurfaceLight> mergedSurfaceLights(std::vector<SurfaceLight> tiles) {
    groupSurfaceLights(tiles);
    return mergeSurfaceLights(tiles);
}
// How strongly a sector's glowing textures light, from its light level: the
// mapper's stand-in for its lamps. A light strip in a 144 sector beside a 128
// room (Doom 2 MAP02's computer room) was meant as a dim fixture, not one
// that floods the room. Strength rises with the square of the level from 96
// to 208 (144: 18%, 160: 33%, 192: 73%), so dim lamps barely reach past the
// sector light and lamps from 208 up shine in full. Flickering sectors
// count at their peak (their light thinker's maxlight), read once per level
// with the low they drop to, so a save loaded mid-flicker lights like a
// fresh start.
struct LampRange { int low,peak; bool flickers,smooth,lamps; };
static std::vector<LampRange> lampRanges;
// A glowing texture other than a screen: a lamp the sector's swing is for.
static bool isLamp(const Image &image) {return image.emissionWeight>0&&image.screens.empty();}
// Whether a flickering sector shows a lamp texture: on its floor or
// ceiling, or on the wall sections of its sides as the map starts.
static void findLamps() {
    for(int i=0;i<numsectors;++i)for(int ceiling=0;ceiling<2;++ceiling) {
        int flat=ceiling?sectors[i].ceilingpic:sectors[i].floorpic;
        if(lampRanges[i].flickers&&flat!=skyflatnum&&isLamp(lumpImage(firstflat+flattranslation[flat],true)))lampRanges[i].lamps=true;
    }
    for(int i=0;i<numlines;++i)for(int side=0;side<2;++side) {
        if(lines[i].sidenum[side]<0)continue;
        const side_t &s=sides[lines[i].sidenum[side]];
        int index=int(s.sector-sectors);
        if(!lampRanges[index].flickers||lampRanges[index].lamps)continue;
        const sector_t *back=lines[i].sidenum[side^1]>=0?sides[lines[i].sidenum[side^1]].sector:nullptr;
        int shown[3]={s.midtexture,
                      back&&back->ceilingheight<s.sector->ceilingheight?s.toptexture:0,
                      back&&back->floorheight>s.sector->floorheight?s.bottomtexture:0};
        for(int tex:shown)if(tex>0&&isLamp(wallImage(texturetranslation[tex])))lampRanges[index].lamps=true;
    }
}
static void readLampRanges() {
    if(lampRanges.size()==(size_t)numsectors)return;
    lampRanges.resize(numsectors);
    for(int i=0;i<numsectors;++i)lampRanges[i]={sectors[i].lightlevel,sectors[i].lightlevel,false,false,false};
    Game_LightSwings([](int sector,int low,int high,int smooth,void*) {
        if(sector<0||sector>=numsectors)return;
        LampRange &range=lampRanges[sector];
        range={low,std::max(range.peak,high),true,smooth!=0,false};
    },nullptr);
    findLamps();
}
static float lampLevel(int sector) {
    readLampRanges();
    if(sector<0||sector>=numsectors)return 1;
    float f=std::clamp((lampRanges[sector].peak-96)/112.0f,0.0f,1.0f);
    return f*f;
}
// Lamps in a flickering sector follow it. Blinking and strobing sectors
// switch them, as a shot-out lamp sputters: out while the sector stands in
// the lower half of its swing, their glowing texels dead (addBrokenWall,
// addBrokenFlat). MAP02's strips strobing between 128 and 144 stay out and
// flash on for 5 tics in 40. Glowing and fire-flickering sectors fade them
// with their swing instead, glow and all (lampGlow): E1M2's armor alcove
// (sector 28) pulses. Their baked light follows by flicker group
// (assignLampGroups), and the sector itself keeps its low (lampLightLevel).
// The swing is the lamps' and not the room's power: screens beside them
// stay on. A sector whose only glowing textures are screens was drawn for
// them, so there the screens are what flickers.
bool lampFlickers(int sector) {
    readLampRanges();
    return sector>=0&&sector<numsectors&&lampRanges[sector].flickers;
}
bool lampFlickers(int sector,const Image &image) {
    return lampFlickers(sector)&&(image.screens.empty()||!lampRanges[sector].lamps);
}
float lampShine(int sector) {
    if(!lampFlickers(sector))return 1;
    const LampRange &range=lampRanges[sector];
    int now=sectors[sector].lightlevel;
    if(range.smooth)return std::clamp((now-range.low)/(float)(range.peak-range.low),0.0f,1.0f);
    return now*2<range.low+range.peak?0.0f:1.0f;
}
bool lampOut(int sector,const Image &image) {return lampFlickers(sector,image)&&!lampRanges[sector].smooth&&lampShine(sector)<=0;}
int lampLightLevel(const sector_t *sector) {
    int index=int(sector-sectors);
    if(!lampGroupSector(index))return sector->lightlevel;
    return std::min<int>(sector->lightlevel,lampRanges[index].low);
}
void lampGlow(std::vector<Vertex> &out,size_t first,const sector_t *sector,const Image &image) {
    int index=int(sector-sectors);
    if(!lampFlickers(index,image)||!lampRanges[index].smooth)return;
    float shine=lampShine(index);
    if(shine>=1)return;
    unsigned alpha=(unsigned)std::lround(shine*255);
    for(size_t n=first;n<out.size();++n)out[n].lampTint=(out[n].lampTint&0xFFFFFFu)|alpha<<24;
}
static void surfaceLight(const SurfaceLightKey &key,int sector,float x,float y,float z,const Image &image,int facing,float radius=224,float extent=0) {
    if(!settings.emissive || image.emissionWeight==0 || tileDark(key)) return;
    // Lamps too dim to light stay listed (strength 0), and so do lamps
    // switched off: shots still find them.
    SurfaceLight light={key,x,y,z,radius,1.65f*image.emissionWeight*lampLevel(sector),image.emissionColor,facing,extent};
    if(lampFlickers(sector,image)) {light.flickerSector=sector;light.shine=lampShine(sector);}
    surfaceLights.push_back(light);
}
static const std::vector<FlatLightSample> &flatSamples(int sectorIndex,int lump,const Image &image) {
    auto key=std::make_pair(sectorIndex,lump);
    auto cached=flatLightSamples.find(key);
    if(cached!=flatLightSamples.end()) return cached->second;
    std::map<std::pair<int,int>,Point> samples;
    // Sample the texture's emission center on its world-aligned repeat grid.
    // BSP polygons only test membership; their centers never place lights.
    for(int leaf:sectorFloors[sectorIndex]) {
        const auto &poly=floors[leaf];if(poly.size()<3) continue;
        const auto &bounds=floorBounds[leaf];
        int firstU=(int)std::ceil((bounds[0]-image.emissionU)/image.width);
        int lastU=(int)std::floor((bounds[2]-image.emissionU)/image.width);
        int firstV=(int)std::ceil((-bounds[3]-image.emissionV)/image.height);
        int lastV=(int)std::floor((-bounds[1]-image.emissionV)/image.height);
        for(int u=firstU;u<=lastU;++u) for(int v=firstV;v<=lastV;++v) {
            Point point={image.emissionU+u*image.width,-image.emissionV-v*image.height};
            bool positive=false,negative=false;
            for(size_t n=0;n<poly.size();++n) {
                Point a=poly[n],b=poly[(n+1)%poly.size()];
                float cross=(b.x-a.x)*(point.y-a.y)-(b.y-a.y)*(point.x-a.x);
                positive|=cross>0.01f;negative|=cross<-0.01f;
            }
            if(!(positive&&negative)) samples[{u,v}]=point;
        }
    }
    std::vector<FlatLightSample> result;
    for(const auto &sample:samples) result.push_back({sample.second,sample.first.first,sample.first.second});
    return flatLightSamples.emplace(key,std::move(result)).first->second;
}
void collectFlatLights() {
    if(!settings.emissive) return;
    for(int i=0;i<numsectors;++i) for(int ceiling=0;ceiling<2;++ceiling) {
        const sector_t &sector=sectors[i];int flat=ceiling?sector.ceilingpic:sector.floorpic;
        if(flat==skyflatnum)continue;
        int lump=firstflat+flattranslation[flat];const Image &image=lumpImage(lump,true);
        if(image.emissionWeight==0)continue;
        float z=sectorHeight(&sector,ceiling)+(ceiling?-8:8);
        for(const auto &sample:flatSamples(i,lump,image))
            surfaceLight({0,i,ceiling,sample.u,sample.v},i,sample.point.x,sample.point.y,z,image,ceiling,256);
    }
}
void collectSurfaceLights() {
    uint64_t now=SDL_GetTicksNS();
    float seconds=surfaceLightTime?(now-surfaceLightTime)/1e9f:1.0f/60;
    surfaceLightTime=now;
    lightKind=bakeOnlyActive?I_RENDER3D_LIGHT_FOG:I_RENDER3D_LIGHT_SURFACE;
    if(bakeOnlyActive) {
        // The bake holds these lights; the nearest static lights stay as
        // fog-only lights so the glow around torches and pools remains.
        surfaceSelection.clear();
        if(!settings.fog)return;
        if(settings.emissive)for(const auto &light:mergedSurfaceLights(surfaceLights))
            staticFog.push_back({light.x,light.y,light.z,light.radius,light.strength,light.color});
        std::vector<std::pair<float,size_t>> scored;
        for(size_t n=0;n<staticFog.size();++n) {
            const StaticFog &f=staticFog[n];
            float distance=std::hypot(f.x-surfaceEye[0],f.y-surfaceEye[1]);
            if(distance<=f.radius+512)scored.emplace_back(f.strength/(1+distance/128),n);
        }
        std::sort(scored.begin(),scored.end(),[](const auto &a,const auto &b){return a.first>b.first;});
        for(size_t n=0;n<std::min(size_t(4),scored.size())&&flashes.count<maxLights;++n) {
            const StaticFog &f=staticFog[scored[n].second];
            appendLight(f.x,f.y,f.z,f.radius,f.strength,f.color,nullptr,0.75f,false);
            fogOnly[flashes.count-1]=true;
        }
        return;
    }
    if(!settings.emissive) {surfaceSelection.clear();return;}
    surfaceSelection.capacity=settings.fewerSurfaceLights?8:24;
    std::vector<SurfaceLight> lit=mergedSurfaceLights(surfaceLights);
    lit.erase(std::remove_if(lit.begin(),lit.end(),[](const SurfaceLight &l){return l.strength<=0;}),lit.end());
    surfaceSelection.update(lit,surfaceEye,seconds);
    // Surface lights reach a short way, but walls still stop them: the bake
    // subtracts only their flat share, so their gloss would otherwise show on
    // shiny surfaces through walls. The Performance switch skips the tests.
    // A flickering lamp's copy dims and goes out with its baked light.
    for(const auto &slot:surfaceSelection.slots) {
        const auto &p=slot.light;
        if(p.shine<=0)continue;
        auto n=panelNormal(p);
        appendLight(p.x,p.y,p.z,p.radius,p.strength*slot.gain*p.shine,p.color,nullptr,0.35f,!settings.unoccludedSurfaceLights,
                    bakedLightsActive?1.0f:0,{std::min(p.extent,p.radius),n[0],n[1],0});
    }
}
void wallLights(const line_t &line,int side,Point a,Point b,float bottom,float top,int tex,float anchor,int span) {
    if(!settings.emissive||tex<=0||top<=bottom)return;
    const Image &image=wallImage(texturetranslation[tex]);
    float u=units(sides[line.sidenum[side]].textureoffset),len=std::hypot(b.x-a.x,b.y-a.y);
    if(image.emissionWeight<=0||len<=0)return;
    float nx=(b.y-a.y)/len,ny=-(b.x-a.x)/len;
    int facing=2+((int)std::lround(std::atan2(ny,nx)/(float)(M_PI/4))+8)%8;
    int firstU=(int)std::ceil((u-image.emissionU)/image.width);
    int lastU=(int)std::floor((u+len-image.emissionU)/image.width);
    int firstV=(int)std::ceil((anchor-top-image.emissionV)/image.height);
    int lastV=(int)std::floor((anchor-bottom-image.emissionV)/image.height);
    int stepV=std::max(1,64/image.height);
    for(int tileU=firstU;tileU<=lastU;tileU+=std::max(1,64/image.width)) {
        float along=image.emissionU+tileU*image.width-u,t=along/len;
        for(int tileV=firstV;tileV<=lastV;tileV+=stepV) {
            // Each sample stands for the rows of tiles it skips, within the wall piece.
            float z=anchor-image.emissionV-tileV*image.height;
            float extent=std::max(0.0f,std::min({image.height*stepV*0.5f,top-z,z-bottom}));
            surfaceLight({1,(int)(&line-lines),side,tileU,tileV,span},int(sides[line.sidenum[side]].sector-sectors),
                a.x+(b.x-a.x)*t+nx*DOOM_PANEL_OFFSET,a.y+(b.y-a.y)*t+ny*DOOM_PANEL_OFFSET,z,image,facing,224,extent);
        }
    }
}
void clearSurfaceLights() {
    surfaceLights.clear();surfaceSelection.clear();flatLightSamples.clear();surfaceGroups.clear();lampRanges.clear();surfaceLightTime=0;
}
} // namespace doom3d
