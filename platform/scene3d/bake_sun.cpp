/* Sunlight baked from the sky texture: sun visibility, sky openings and
   the sunbeams through them. */
#include "internal.h"
#include "../rooms.h"

namespace doom3d {
bool sunBaked=false;
float sunLevel=0; // Typical sky-sector light; indoor sun patches rise toward it.
// Sky sectors joined by sky-to-sky lines form one opening (a hole may be
// several sectors, nested or side by side). Its outer edge decides how
// enclosed it is: indoor lines lead into non-sky sectors.
struct SkyOpening { float edge=0; std::vector<std::pair<const sector_t*,float>> indoor; };
static std::vector<int> skyOpeningOf;static std::vector<SkyOpening> skyOpenings;
std::vector<SunShaft> sunShafts;

std::vector<char> beamAir;
// Sun shadows, baked once per level into the contact map's blue channel and
// the wall atlas alpha, from where the sun stands (sun.cpp, placed by
// weather.cpp as the level starts). Rays see the sector heights in bakeMap.
static float enclosure(const sector_t *sector,float *light=nullptr);
static void bakeShafts();
static void findSkyOpenings();
// A floor cell's sun visibility (0-255), or -1 for void and sky floors.
int sunCell(const BakeMap &map,size_t i,const float direction[3]) {
    int sector=seamCells[i].own;
    if(sector==noSector||sectors[sector].floorpic==skyflatnum)return -1;
    float px=mapOrigin[0]+(i%mapWidth+0.5f)*mapCell,py=mapOrigin[1]+(i/mapWidth+0.5f)*mapCell;
    return (int)std::lround(sunVisibility(map,sector,px,py,map.sectors[sector].floor+1,direction)*255);
}
// A wall strip's sun visibility into the atlas alpha. Walls fade with the
// angle to the sun so grazing walls fall into shade.
void sunStrip(const BakeMap &map,const BakeStrip &strip,const float direction[3]) {
    float length=std::hypot(direction[0],direction[1]),sunX=direction[0]/length,sunY=direction[1]/length;
    float facing=std::clamp((strip.dy*sunX-strip.dx*sunY)*1.5f,0.0f,1.0f);
    forStripTexels(strip,[&](size_t i,float x,float y,float z) {
        float visible=facing>0?facing*sunVisibility(map,strip.sector,x,y,z,direction):0;
        wallBakeCells[4*i+3]=(uint8_t)std::lround(visible*255);
    });
}
void bakeSun() {
    loadingStage(LoadStage::Sun);
    finishRelight();
    sunBaked=true;sunLevel=0;
    if(contactCells.empty())return;
    std::vector<float> skyLights;
    for(int i=0;i<numsectors;++i)if(sectors[i].ceilingpic==skyflatnum)skyLights.push_back(lighting(&sectors[i]));
    if(skyLights.empty()) {fprintf(stderr,"3D sun: no sky sectors.\n");return;}
    std::nth_element(skyLights.begin(),skyLights.begin()+skyLights.size()/2,skyLights.end());
    float level=skyLights[skyLights.size()/2];
    uint64_t start=SDL_GetTicksNS();
    const float *direction=sunDirection;
    std::atomic<size_t> lit{0};
    parallelFor(mapHeight,[&](int y) {
        for(int x=0;x<mapWidth;++x) {
            size_t i=(size_t)y*mapWidth+x;
            int visible=sunCell(bakeMap,i,direction);
            if(visible<0)continue;
            contactCells[4*i+2]=(uint8_t)visible;
            if(visible>0)++lit;
        }
    });
    parallelFor((int)bakeOrder.size(),[&](int n) {sunStrip(bakeMap,bakeStrips[bakeOrder[n]],direction);});
    contactTexture=gpuCreateTexture(GpuFormat::RGBA8,mapWidth,mapHeight,contactCells.data(),mapWidth*4);
    if(!contactTexture)I_Error((char*)"Could not allocate surface maps");
    uploadWallBake();
    // Hidden by the episode's mood (sun.cpp): no sunlight, no beams.
    if(levelMood().sun)bakeShafts();else sunShafts.clear();
    sunLevel=levelMood().sun?level:0;
    fprintf(stderr,"3D sun: yaw %.0f degrees, %.0f up, %zu sunlit cells, %zu sunbeams, %zu wall strips in %dx%d, baked in %.0f ms.\n",
        sunYaw*180/doomPi,sunElevation*180/doomPi,lit.load(),sunShafts.size(),bakeOrder.size(),atlasWidth,atlasHeight,(SDL_GetTicksNS()-start)/1e6);
}
// Sunbeams: about one per 24 units of sunlit floor in beam air (beamAir),
// jittered within its block so the beams never line up in rows. Each runs
// from the floor back toward the sun, sampled on the map grid, while it stays
// in beam air; it ends a little past the point where it leaves through a sky
// ceiling or a window into open sky.
// Beams belong to dusty air lit through a narrow gap, so a beam fades where
// the light floods in instead: where the opening onto open ground it leaves
// through (a window, or the edge of a cave mouth or overhang) is wider than
// 128 units, gone at 320.
static void bakeShafts() {
    Rooms_Build();
    sunShafts.clear();
    if(skyOpeningOf.size()!=(size_t)numsectors)findSkyOpenings();
    std::vector<double> openingArea(skyOpenings.size());
    for(size_t i=0;i<seamCells.size();++i) {
        int at=seamCells[i].own;
        if(at!=noSector&&skyOpeningOf[at]>=0)openingArea[skyOpeningOf[at]]+=mapCell*mapCell;
    }
    // Beam air: everything under a roof (the room map's rooms, openings and
    // porches), and a hole in a ceiling (an enclosed sky opening no bigger
    // than 400 square). Porches count, so the sun falling through a hole
    // onto the porch floor round it has its beams like the hole's own floor;
    // a porch lit wide off a yard loses them to the opening's width below.
    std::vector<char> &indoor=beamAir;
    indoor.assign(numsectors,0);
    for(int i=0;i<numsectors;++i) {
        room_kind kind=Rooms_Kind(i);
        indoor[i]=kind==ROOM_INDOOR||kind==ROOM_OPENING||kind==ROOM_SHELTERED||(underSky(i)&&enclosure(&sectors[i])>0.5f&&openingArea[skyOpeningOf[i]]<=400*400);
    }
    // Openings onto open ground: lines with beam air on one side and open sky
    // on the other, through a gap at least 24 high, joined into chains where
    // they share a vertex. A chain's length is how wide the opening is.
    auto opensOut=[&](const line_t &line) {
        if(!line.frontsector||!line.backsector)return false;
        int f=(int)(line.frontsector-sectors),k=(int)(line.backsector-sectors);
        if(indoor[f]==indoor[k]||!bakeMap.sectors[indoor[f]?k:f].sky)return false;
        return std::min(bakeMap.sectors[f].ceiling,bakeMap.sectors[k].ceiling)-std::max(bakeMap.sectors[f].floor,bakeMap.sectors[k].floor)>=24;
    };
    std::vector<int> root(numvertexes);
    for(int v=0;v<numvertexes;++v)root[v]=v;
    auto chainOf=[&](int v) {while(root[v]!=v)v=root[v]=root[root[v]];return v;};
    for(int i=0;i<numlines;++i)if(opensOut(lines[i]))root[chainOf((int)(lines[i].v1-vertexes))]=chainOf((int)(lines[i].v2-vertexes));
    std::vector<float> chainLength(numvertexes);
    for(int i=0;i<numlines;++i)if(opensOut(lines[i]))chainLength[chainOf((int)(lines[i].v1-vertexes))]+=std::hypot(units(lines[i].dx),units(lines[i].dy));
    // The width of the opening a beam leaves through from sector last into
    // open sector out near (x,y): the chain of the nearest line between them.
    auto openingWidth=[&](int last,int out,float x,float y) {
        float best=INFINITY,width=0;
        const sector_t &from=sectors[last];
        for(int i=0;i<from.linecount;++i) {
            const line_t &line=*from.lines[i];
            if(!opensOut(line)||(line.frontsector-sectors!=out&&line.backsector-sectors!=out))continue;
            float ax=units(line.v1->x),ay=units(line.v1->y),dx=units(line.dx),dy=units(line.dy);
            float u=std::clamp(((x-ax)*dx+(y-ay)*dy)/std::max(dx*dx+dy*dy,1e-6f),0.0f,1.0f);
            float distance=std::hypot(ax+dx*u-x,ay+dy*u-y);
            if(distance<best) {best=distance;width=chainLength[chainOf((int)(line.v1-vertexes))];}
        }
        return width;
    };
    int block=std::max(1,(int)std::lround(24/mapCell));
    for(int by=0;by<mapHeight;by+=block)for(int bx=0;bx<mapWidth;bx+=block) {
        int x=std::min(mapWidth-1,bx+(int)(hashUnit(bx,by,0x5a17u)*block)),y=std::min(mapHeight-1,by+(int)(hashUnit(bx,by,0x5a18u)*block));
        size_t i=(size_t)y*mapWidth+x;int sector=seamCells[i].own;
        if(sector==noSector||!indoor[sector]||sectors[sector].floorpic==skyflatnum)continue;
        float visible=contactCells[4*i+2]/255.0f;
        if(visible<0.5f)continue;
        float px=mapOrigin[0]+(x+0.5f)*mapCell,py=mapOrigin[1]+(y+0.5f)*mapCell,pz=bakeMap.sectors[sector].floor;
        float length=0,width=0;bool left=false;int last=sector;
        for(float t=4;t<1024;t+=4) {
            float qx=px+sunDirection[0]*t,qy=py+sunDirection[1]*t,qz=pz+sunDirection[2]*t;
            int cx=(int)std::floor((qx-mapOrigin[0])/mapCell),cy=(int)std::floor((qy-mapOrigin[1])/mapCell);
            if(cx<0||cy<0||cx>=mapWidth||cy>=mapHeight)break;
            int at=seamCells[(size_t)cy*mapWidth+cx].own;
            if(at==noSector||qz<bakeMap.sectors[at].floor)break;
            if(!indoor[at]||qz>bakeMap.sectors[at].ceiling) {
                left=bakeMap.sectors[at].sky;
                if(!indoor[at])width=openingWidth(last,at,qx,qy);
                break;
            }
            length=t;last=at;
        }
        if(!left||length<24)continue;
        float wide=std::clamp((width-128)/192,0.0f,1.0f),contrast=1-wide*wide*(3-2*wide);
        if(contrast<0.05f)continue;
        sunShafts.push_back({px,py,pz,length+24,visible*contrast,sector});
    }
}
// The sky's openings, from the room map (rooms.c).
static void findSkyOpenings() {
    Rooms_Build();
    skyOpeningOf.assign(numsectors,-1);skyOpenings.assign(Rooms_SkyOpeningCount(),{});
    for(int i=0;i<numsectors;++i)skyOpeningOf[i]=Rooms_SkyOpening(i);
    for(int id=0;id<(int)skyOpenings.size();++id) {
        skyOpenings[id].edge=Rooms_SkyEdge(id);
        for(int k=0;k<Rooms_SkyIndoorCount(id);++k) {const rooms_sky_indoor &in=*Rooms_SkyIndoor(id,k);skyOpenings[id].indoor.push_back({&sectors[in.sector],in.length});}
    }
}
// How enclosed a sky sector's opening is: 0 for open air up to 1 where most
// of its edge opens into indoor sectors, as a hole in a ceiling does. light
// receives those sectors' current light, weighted by edge length. Lighting
// no longer reads it (lightRule); the weather's open share, sunbeams and the
// window lamps do.
static float enclosure(const sector_t *sector,float *light) {
    if(skyOpeningOf.size()!=(size_t)numsectors)findSkyOpenings();
    if(light)*light=0;
    int id=skyOpeningOf[sector-sectors];if(id<0)return 0;
    const SkyOpening &opening=skyOpenings[id];
    float open=0,sum=0;
    for(const auto &[other,length]:opening.indoor) {open+=length;sum+=length*lighting(other);}
    if(open<=0||opening.edge<=0)return 0;
    if(light)*light=sum/open;
    float t=std::clamp((open/opening.edge-0.3f)/0.3f,0.0f,1.0f);
    return t*t*(3-2*t);
}
// The one light rule, for every surface, from what the bakes traced there:
// sun visibility (visible, -1 for none, as on ceilings) and the share of
// sky it sees (open). Three anchors from the sector's light L: D where it
// sees neither, K where it sees all the sky, U in full sun, joined as
//     D + (K - D) * open + (U - K) * visible.
// Under a roof L is the room's own level (D), and the sky and the sun rise
// from there toward the outdoor levels. Under the sky L is the level of open
// sunlit ground (U), open shade (K) falls a share below it, and where the
// ground sees no sky (D) it lies at the light of the roofed sectors round
// its opening (room, -1 for none: then a quarter below open shade), never
// above L, and open shade no lower than that. So the mapper's extra light
// on a hole in a roof shows only where the traced sun lands, the hole
// shades no darker than the room under the
// roof round it, an alcove off a yard falls toward the rooms beside it, and
// open ground keeps its sun and shade. Nothing reads porches or enclosure,
// and sector_light.glsl blends the result across seams like the sector
// light, so nothing steps where a roof ends. The result is the level and
// the parts of it that take the sun's and the sky's tint. Keep lightRule in
// sector_light.glsl equivalent.
LightRule lightRule(const LightRuleInputs &in,bool sky,float room,float light,float visible,float open) {
    bool sun=in.sun>0&&visible>=0;
    float v=sun?visible:0;
    LightRule r;
    if(sky) {
        float shade=sun?in.shade*(1-in.even):0,gain=sun?in.gain*(1-in.even):0;   // a whiteout takes the shadows too
        float k=light*(1-shade),u=light*(1+gain);
        float d=in.skyLight<=0?k:room>0?std::min(room,light):k*0.75f;
        if(room>0)k=std::max(k,std::min(room,light));
        float h=std::clamp(open,0.0f,1.0f);h+=(1-h)*in.even;
        r.level=d+(k-d)*h+(u-k)*v;
        r.sun=light*gain*v;r.sky=in.skyCast*h*r.level;
        return r;
    }
    float lift=in.skyLight>0&&!in.windowLight?std::max(0.0f,in.skyLight-light)*0.7f:0;
    float d=light,k=light+lift,u=std::max(in.sun,light);
    float h=std::clamp(open*1.2f,0.0f,1.0f);
    r.level=d+(k-d)*h+(u-light)*v;
    r.sun=(u-light)*v;r.sky=(k-d)*h;
    return r;
}
LightRuleInputs lightRuleInputs() {
    LightRuleInputs in;
    in.sun=settings.sun&&sunLevel>0?sunLevel*(1-0.85f*overcast()):0;
    in.shade=sunShade;in.gain=sunGain;
    in.skyLight=settings.skyLight&&ambientKey==bakeThingsKey?skyLevel*(Game_Phobos()?marsShine:1):0;
    in.windowLight=settings.windowLight&&bakedLightsActive;
    game_sun game;Game_Sun(&game);in.skyCast=game.skyCast;
    // A whiteout or a thick fog lights the open ground evenly (sector_light.glsl's flatAir).
    float fog=fogBankCount()?0.75f*std::clamp(weatherNow(WEATHER_FOG)*1.25f,0.0f,1.0f):0.0f;
    in.even=std::max(whiteoutNow(),settings.weather?fog:0.0f);
    return in;
}
// The light of the roofed sectors round a sky sector's opening, by the
// length of edge they share with it (their current light); -1 for none.
float roomLight(const sector_t *sector) {
    float light;enclosure(sector,&light);
    return light>0?light:-1;
}
// Sky sectors pass their enclosure (for the weather) and the light of the
// rooms round their opening (lightRule's D) to the shader packed in
// sectorInfo w: 1 + round(enclosure*255) + round(room*255)/256, room 0 for
// none (0 indoors).
float packOutdoor(const sector_t *sector,float *enclosed,float *room) {
    float light,e=enclosure(sector,&light);
    if(enclosed)*enclosed=e;
    if(room)*room=light>0?light:-1;
    return 1+std::round(e*255)+std::round(std::clamp(light,0.0f,1.0f)*255)/256;
}
// CPU twin of the shader's light rule on floors, for sprites and the weapon:
// lightRule at the nearest cell, for the sector alone (no seam blend).
float sunLightAt(float x,float y,const sector_t *sector,float light) {
    if(contactCells.empty())return light;
    LightRuleInputs in=lightRuleInputs();
    if(in.sun<=0&&in.skyLight<=0)return light;
    int cx=std::clamp((int)std::floor((x-mapOrigin[0])/mapCell),0,mapWidth-1);
    int cy=std::clamp((int)std::floor((y-mapOrigin[1])/mapCell),0,mapHeight-1);
    size_t i=(size_t)cy*mapWidth+cx;
    float visible=contactCells[4*i+2]/255.0f;
    float sky=in.skyLight>0&&!flatLightCells.empty()?(flatCell(floorBounce,i)[3]>>4)/15.0f:1.0f;
    bool outside=sector->ceilingpic==skyflatnum;
    float level=lightRule(in,outside,outside?roomLight(sector):-1,light,visible,sky).level;
    return light+std::round((level-light)*16)/16;
}
// CPU twin of seamLight at the nearest cell, for sprites and the weapon.
float seamLightAt(float x,float y,const sector_t *sector,float fallback) {
    if(!settings.softLight||seamCells.empty())return fallback;
    int cx=(int)std::floor((x-mapOrigin[0])/mapCell),cy=(int)std::floor((y-mapOrigin[1])/mapCell);
    if(cx<0||cy<0||cx>=mapWidth||cy>=mapHeight)return fallback;
    const auto &cell=seamCells[(size_t)cy*mapWidth+cx];
    uint16_t own=(uint16_t)(sector-sectors);
    if(cell.own!=own||cell.neighbor==noSector)return fallback;
    return fallback+(lighting(&sectors[cell.neighbor])-fallback)*(cell.weight&255)/510.0f;
}
void sunNewLevel() {sunBaked=false;skyOpeningOf.clear();skyOpenings.clear();sunShafts.clear();beamAir.clear();}
void sunShutdown() {sunNewLevel();sunLevel=0;}
} // namespace doom3d
