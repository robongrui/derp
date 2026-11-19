/* Bounce light: baked for the whole level at load, and solved again in the
   background round a door, a lift or a shot-out lamp once it has settled
   (whole-level lighting). */
#include "internal.h"
#include "open_doors.h"
#include "gpu_trace.h"
#include "bounce_carry.h"
#include "../rooms.h"
#include <memory>
#include <unordered_set>

namespace doom3d {
int bounceKey=-1;
std::vector<uint16_t> levelCarry;
std::mutex levelCarryLock;
size_t levelCarrySize() {
    size_t halves=3*((size_t)2*mapWidth*mapHeight+(size_t)atlasWidth*atlasHeight);
    return halves+(halves&1);
}
bool bounceEightBits() {
#ifdef DOOM_DEVTOOLS
    static const bool eight=getenv("DERP_BOUNCE8")!=nullptr;
    return eight;
#else
    return false;
#endif
}
int bounceWholePasses() {
#ifdef DOOM_DEVTOOLS
    static const int passes=getenv("DERP_BOUNCEPASSES")?std::clamp(atoi(getenv("DERP_BOUNCEPASSES")),1,64):5;
    return passes;
#else
    return 5;
#endif
}
bool bounceNoTail() {
#ifdef DOOM_DEVTOOLS
    static const bool none=getenv("DERP_BOUNCENOTAIL")!=nullptr;
    return none;
#else
    return false;
#endif
}
// Which inputs bounce light gathers: 1 always, 2 the sun, 4 baked lights,
// 8 bake-only lights (pools as areas).
int bounceInputs() {
    bool lights=lightBakeKey==lightBakeInputs();
    return 1|(settings.sun&&sunLevel>0?2:0)|(lights?4:0)|(lights&&(lightBakeKey&2)?8:0)|(settings.lightModel==1?16|((int)std::lround(settings.surfaceReturn*4)<<5)|(settings.skyLight&&ambientKey==bakeThingsKey?4096:0)|(settings.eyeAdaptation?8192:0):0);
}
namespace {
using Runner=std::function<void(int,const std::function<void(int)>&)>;
// What a sample's rays met, kept from the first pass of a solve for the passes
// after it. Whole-level lighting gathers five times, a bounce each, and every
// pass shot the same rays from the same places through the same map to the
// same surfaces: only the light on those surfaces had changed. So the rays
// are traced once and each later pass reads the light where they landed. The
// flicker share's solve (bounceFlicker) shoots them too and takes the level's.
// A ray is one 32-bit word: its kind and the texel of the atlas (with the wall's part) or the
// cell of the map (with floor or ceiling); the surface and the sector are read back off those
// (stripAtlas, the cell's own sector). The few rays that does not hold (a cell hit in a sector
// not its own, an atlas past 32,768 rows) keep a wide word after the sample's others: kind, the
// surface (a wall: (line*2+side)*3+part, a flat: sector*2+ceiling) and the texel or cell.
// A sample is a kilobyte so; at eight bytes a ray Doom 2's MAP13 ran past the budget and the
// samples past it were traced each pass as before, which they still are on a map larger yet.
enum : int { rayNothing, rayEscaped, raySky, rayBare, rayWall, rayFlat, rayWide=7 };   // a decoration; left the map; reached the sky; a wall with no strip
struct RaySeen { int kind=rayNothing,sector=-1; uint64_t surface=0,at=0; };
// Per texel of the atlas' left half, the strip it lies in (line*2+side), -1 for none: made with
// the level's bounce (bakeBounce), read by every solve after it.
std::vector<int32_t> stripAtlas;
void buildStripAtlas() {
    stripAtlas.assign((size_t)atlasWidth*atlasHeight,-1);
    for(size_t i=0;i<bakeStrips.size();++i) {
        const BakeStrip &strip=bakeStrips[i];
        if(strip.x<0||strip.y<0||strip.x+strip.width>atlasWidth||strip.y+strip.height>atlasHeight)continue;
        for(int y=0;y<strip.height;++y)std::fill_n(&stripAtlas[(size_t)(strip.y+y)*atlasWidth+strip.x],strip.width,(int32_t)i);
    }
}
// Where a sample's rays point on its hemisphere (cosineDirection's a, b and c), for each of the
// 1,024 turns its pattern can take: a square root, a sine and a cosine a ray, the same few
// thousand values over and over, were a tenth of a big map's bounce. The same expressions, so
// the same to the bit; the sample's frame turns them into directions.
struct RayTable {
    int side=0;
    std::vector<std::array<float,3>> at;   // turn*rays + ray
    explicit RayTable(int side):side(side),at((size_t)1024*side*side) {
        for(int turn=0;turn<1024;++turn)for(int n=0;n<side*side;++n) {
            float rotation=turn/1024.0f;
            float u=((n%side)+0.5f)/side,v=std::fmod(((n/side)+0.5f)/side+rotation,1.0f);
            float radius=std::sqrt(u),angle=6.28318531f*v;
            at[(size_t)turn*side*side+n]={radius*std::cos(angle),radius*std::sin(angle),std::sqrt(std::max(0.0f,1-u))};
        }
    }
};
const RayTable &rayTable(int side) {
    static const RayTable fine(16),coarse(4);
    return side==16?fine:coarse;
}
struct HitStore {
    static constexpr size_t budget=(size_t)768<<20;
    int rays=0,step=0; float reach=0; bool sky=false;   // what the rays were: a solve of another shape takes none of them
    std::vector<uint32_t*> flats,walls;   // per cell and ceiling; per strip sample (wallStart)
    std::vector<size_t> wallStart;
    // Which of the samples round a block (smoothing: nine, by floor and ceiling) and round a cell
    // (the blend: four) lie on its own surface: traced along the surface once, the same every pass.
    std::vector<uint32_t> blockSame;
    std::vector<uint8_t> cellSame;
    bool blocksKnown=false,cellsKnown=false;   // set between the loops, by the thread that runs them
    std::atomic<size_t> used{0};
    std::atomic<bool> full{false};
    static uint32_t *untraced() {static uint32_t none=0;return &none;}   // a sample that is not kept
    // The samples on the graphics card (gpu_bakes.cpp), their rays kept there; per slot the
    // card's sample, -1 for one gathered here.
    std::shared_ptr<GpuBounce> card;
    std::vector<int32_t> cardFlat,cardWall;
    HitStore(int rays,int step,float reach,bool sky):rays(rays),step(step),reach(reach),sky(sky) {
        flats.assign((size_t)mapWidth*mapHeight*2,nullptr);
        wallStart.resize(bakeStrips.size()+1,0);
        for(size_t i=0;i<bakeStrips.size();++i) {
            const BakeStrip &strip=bakeStrips[i];
            wallStart[i+1]=wallStart[i]+(size_t)((strip.width+step-1)/step)*((strip.height+step-1)/step);
        }
        walls.assign(wallStart.back(),nullptr);
        blockSame.assign((size_t)((mapWidth+step-1)/step)*((mapHeight+step-1)/step),0);
        cellSame.assign((size_t)mapWidth*mapHeight,0);
    }
    ~HitStore() {for(auto *slots:{&flats,&walls})for(uint32_t *record:*slots)if(record!=untraced())delete[] record;}
    HitStore(const HitStore&)=delete;
    HitStore &operator=(const HitStore&)=delete;
    // A sample's rays into its slot (each slot is one thread's at a time).
    void keep(uint32_t *&slot,const uint32_t *words,const uint64_t *wide,int wideCount) {
        size_t bytes=4*(size_t)rays+8*(size_t)wideCount;
        if(used.fetch_add(bytes)+bytes>budget) {used-=bytes;full=true;slot=untraced();return;}
        slot=new uint32_t[rays+2*wideCount];
        std::memcpy(slot,words,4*(size_t)rays);
        if(wideCount)std::memcpy(slot+rays,wide,8*(size_t)wideCount);
    }
    // The short word, where the surface and the sector can be read back off the texel or cell.
    static bool pack(const RaySeen &seen,const BakeMap &map,uint32_t &word) {
        switch(seen.kind) {
        case rayNothing:case rayEscaped:case raySky:word=(uint32_t)seen.kind<<29;return true;
        case rayBare: {
            if(seen.surface>=(1u<<29))return false;
            const BakeLine &line=map.lines[seen.surface/6];
            if(((seen.surface/3)&1?line.back:line.front)!=seen.sector)return false;
            word=(uint32_t)rayBare<<29|(uint32_t)seen.surface;
            return true;
        }
        case rayWall: {
            uint64_t row=seen.at/atlasStride,column=seen.at%atlasStride,texel=row*atlasWidth+column;
            if(column>=(uint64_t)atlasWidth||texel>=(1u<<27)||texel>=stripAtlas.size())return false;
            int strip=stripAtlas[texel];
            if(strip<0||(uint64_t)strip*3+seen.surface%3!=seen.surface||(size_t)strip/2>=map.lines.size())return false;
            const BakeLine &line=map.lines[strip/2];
            if((strip&1?line.back:line.front)!=seen.sector)return false;
            word=(uint32_t)rayWall<<29|(uint32_t)(seen.surface%3)<<27|(uint32_t)texel;
            return true;
        }
        case rayFlat:
            if(seen.at>=(1u<<28)||seen.at>=seamCells.size()||(int)seamCells[seen.at].own!=seen.sector||
               (uint64_t)seen.sector*2+(seen.surface&1)!=seen.surface)return false;
            word=(uint32_t)rayFlat<<29|(uint32_t)(seen.surface&1)<<28|(uint32_t)seen.at;
            return true;
        default:return false;
        }
    }
    // The nth ray of a kept sample.
    RaySeen unpack(const uint32_t *record,int n,const BakeMap &map) const {
        uint32_t word=record[n];
        RaySeen seen;seen.kind=(int)(word>>29);
        if(seen.kind==rayWide) {
            uint64_t wide;std::memcpy(&wide,record+rays+2*(size_t)(word&0x1FFFFFFF),8);
            return unpackWide(wide,map);
        }
        if(seen.kind==rayBare) {
            seen.surface=word&0x1FFFFFFF;
            const BakeLine &line=map.lines[seen.surface/6];seen.sector=(seen.surface/3)&1?line.back:line.front;
        } else if(seen.kind==rayWall) {
            uint32_t texel=word&0x7FFFFFF;int strip=stripAtlas[texel];
            seen.surface=(uint64_t)strip*3+((word>>27)&3);
            seen.at=(uint64_t)(texel/atlasWidth)*atlasStride+texel%atlasWidth;
            const BakeLine &line=map.lines[strip/2];seen.sector=strip&1?line.back:line.front;
        } else if(seen.kind==rayFlat) {
            seen.at=word&0xFFFFFFF;seen.sector=seamCells[seen.at].own;
            seen.surface=(uint64_t)seen.sector*2+((word>>28)&1);
        }
        return seen;
    }
    static bool packWide(const RaySeen &seen,const BakeMap &map,uint64_t &word) {
        if(seen.surface>=(1u<<29)||seen.at>>32)return false;
        // The sector is read back off the surface: it must be the one the ray was in.
        if(seen.kind==rayBare||seen.kind==rayWall) {
            const BakeLine &line=map.lines[seen.surface/6];
            if(((seen.surface/3)&1?line.back:line.front)!=seen.sector)return false;
        }
        word=(uint64_t)seen.kind<<61|seen.surface<<32|seen.at;
        return true;
    }
    static RaySeen unpackWide(uint64_t word,const BakeMap &map) {
        RaySeen seen;
        seen.kind=(int)(word>>61);seen.surface=(word>>32)&0x1FFFFFFF;seen.at=word&0xFFFFFFFFu;
        if(seen.kind==rayBare||seen.kind==rayWall) {const BakeLine &line=map.lines[seen.surface/6];seen.sector=(seen.surface/3)&1?line.back:line.front;}
        else if(seen.kind==rayFlat)seen.sector=(int)(seen.surface/2);
        return seen;
    }
};
// One solve of the bounce: each sample averages what 256 cosine-weighted
// rays hit, albedo (the surface texture's average color) times its sun and
// baked light. Samples are taken every 16 units and spread bilinearly over
// the cells between them within a sector or wall strip.
// prepare() reads the game and the textures, on the game's thread; run() only
// traces and may be on any thread. With rects it is a region's solve: it works
// on its own copies of the map and the light, so the game goes on drawing and
// lighting things from what it has, and commit() puts the result in.
struct BounceSolve {
    bool sun=false,lights=false,whole=false,lobes=false,fast=false;
    bool adapts=false;   // F4 Eye adaptation: the sky's light stays plain (see the gather)
    float day=1;         // and is drawn at this many times the sky's level (daylightScale)
    std::array<float,3> sunEnergy={},lies={};
    Weather kind=WEATHER_NONE;
    std::vector<std::array<float,3>> flatAlbedo,sideAlbedo;
    std::vector<float> level;   // lighting() per sector
    float skyGlow=0;
    // Whole-level lighting: the sky is one emitter for every surface (PLAN-light-model.md,
    // step 6). A ray that reaches the sky brings its light: on open ground, under a porch, in
    // a room, by the one gather, and each pass after carries it on off what it lit. So the
    // sky's light is soft, has no edge where a roof begins, and nothing else lays it down:
    // outside, sector_light.glsl's rule adds only the sun.
    // One reckoning for every place, whatever it is (open ground, a porch, a room): daylight
    // is far brighter than the picture can show, so inside the solve the sky and the sun
    // count skyInGain times what is drawn, and their light is carried from bounce to bounce
    // at that true strength: a sunlit floor lights the walls and the ceiling of its room as
    // brightly as it really is, not as the screen shows it. Only what the last pass leaves
    // is brought down to what can be drawn: no more than the sky's level (the level's one
    // sky: the median of the sky sectors' levels), over a soft shoulder. So ground that
    // sees a fair part of the sky stands at that level, a walled court at noon too, and
    // light falls off only where little is left, with no line where the kind of place
    // changes. (Bounced at its drawn strength, a carpet in the sun stood bright in a black
    // room: it seemed to glow.) A wall sees half the sky at most; that half counts as all.
    // Through the map as it stands, so not through a shut door. This takes the place of the
    // lamp panels at openings (addWindowLights).
    bool skyDome=false;
    std::array<float,3> skyRadiance={};
    std::vector<char> outside;
    float skyShade=0;
    std::array<float,3> skyEnergy={};
    BakeMap map;                // a region's: bakeMap as it stood
    std::unique_ptr<OpenDoors> doors;
    std::vector<std::array<float,4>> rects;   // x0, y0, x1, y1; none: the whole level
    std::vector<uint8_t> flat,wall,flatDirection,wallDirection;   // a region's copies
    std::vector<int> strips;    // a region's wall strips
    std::atomic<bool> cancel{false};
    std::atomic<bool> done{false};   // run has returned (set by the light worker); then the frame thread may read it
    // The rays of this solve (HitStore). Given before run, they are used if they fit; with keepHits
    // they outlive run, for a second solve of the same map (bounceFlicker).
    std::shared_ptr<HitStore> hits;
    bool keepHits=false;
    BounceCarry carry;   // the light between its passes (bounce_carry.h); a region's from levelCarry
    // The groups the series past the last pass is reckoned in (BounceCarry::extendTail): per sector its
    // room, or outside one of its own; made in prepare, on the game's thread (rooms.h).
    std::vector<int> sectorGroup;
    int tailGroups=0;
    void prepare();
    // A prepared shot's solve (relight.cpp) takes its light on the light worker, from the shot's
    // copies, not here: copyLight, after prepare, with deferCopy set.
    bool deferCopy=false;
    int copyBox[4]={};
    std::vector<int> reached;
    void copyLight();
    void run(const Runner &runner);
    void commit();
    bool inside(float x,float y,float grow) const {
        if(rects.empty())return true;
        for(const auto &r:rects)if(x>=r[0]-grow&&x<=r[2]+grow&&y>=r[1]-grow&&y<=r[3]+grow)return true;
        return false;
    }
};
// A region's copies of the light, kept from one solve to the next (the
// frame thread takes them back once the worker is done with a solve), so a
// solve neither allocates nor faults in an atlas-sized copy each time.
struct SolveBuffers { std::vector<uint8_t> flat,wall,flatDirection,wallDirection; std::vector<uint16_t> carry[2]; };
SolveBuffers spare;
void keepBuffers(BounceSolve &solve,SolveBuffers &into=spare) {
    auto keep=[](auto &from,auto &to) {if(to.empty())to.swap(from);};
    keep(solve.flat,into.flat);keep(solve.wall,into.wall);
    keep(solve.flatDirection,into.flatDirection);keep(solve.wallDirection,into.wallDirection);
    for(int k=0;k<2;++k)keep(solve.carry.buffers[k],into.carry[k]);
}
// to, of from's size, takes from's cells within (x0..x1, y0..y1) of each
// layer of the map (mapWidth x mapHeight, 4 bytes a cell).
void copyFlatBox(std::vector<uint8_t> &to,const std::vector<uint8_t> &from,int x0,int y0,int x1,int y1) {
    if(to.size()!=from.size())to.assign(from.size(),0);
    const size_t layer=(size_t)mapWidth*mapHeight,layers=from.size()/(4*layer);
    for(size_t of=0;of<layers;++of)for(int y=y0;y<=y1;++y) {
        size_t at=4*(of*layer+(size_t)y*mapWidth+x0);
        std::memcpy(&to[at],&from[at],4*(size_t)(x1-x0+1));
    }
}
// to, of from's size, takes from's texels of the strips, both halves (lamp light and bounce).
void copyStrips(std::vector<uint8_t> &to,const std::vector<uint8_t> &from,const std::vector<int> &strips) {
    if(to.size()!=from.size())to.assign(from.size(),0);
    for(int index:strips) {
        const BakeStrip &strip=bakeStrips[index];
        for(int row=0;row<strip.height;++row)for(int half=0;half<2;++half) {
            size_t at=4*((size_t)(strip.y+row)*atlasStride+half*atlasWidth+strip.x);
            std::memcpy(&to[at],&from[at],4*(size_t)strip.width);
        }
    }
}
void BounceSolve::prepare() {
    sun=bounceKey&2;lights=bounceKey&4;whole=settings.lightModel==1;fast=settings.fastBakes;
    adapts=settings.eyeAdaptation;day=daylightScale();
    // Under falling weather the sun sinks as the direct sun does (scene.cpp).
    if(sun)for(int c=0;c<3;++c)sunEnergy[c]=sunTint[c]*sunLevel*(1-0.85f*overcast());
    // What the weather leaves on the ground (weather.cpp's bake, as having
    // fallen a while) bounces as wear.glsl draws it: what settles in its own
    // colour (snow white, ash dark grey, sand), wet ground darker.
    kind=climateNow().weather?falling():WEATHER_NONE;
    const WeatherKind &weather=weatherKind(kind);
    lies=weather.lies?weather.lies():std::array<float,3>{0.8f,0.82f,0.87f};
    if(kind!=WEATHER_NONE)bakeWetness();
    flatAlbedo.assign((size_t)numsectors*2,{0.5f,0.5f,0.5f});
    for(int i=0;i<numsectors;++i)for(int ceiling=0;ceiling<2;++ceiling) {
        int pic=ceiling?sectors[i].ceilingpic:sectors[i].floorpic;
        if(pic!=skyflatnum)flatAlbedo[(size_t)i*2+ceiling]=averageColor(lumpImage(firstflat+flattranslation[pic],true));
    }
    // Per side: middle, upper and lower texture.
    sideAlbedo.assign((size_t)numlines*6,{0.5f,0.5f,0.5f});
    for(int i=0;i<numlines;++i)for(int side=0;side<2;++side) {
        if(lines[i].sidenum[side]<0)continue;
        const side_t &s=sides[lines[i].sidenum[side]];
        short textures[3]={s.midtexture,s.toptexture,s.bottomtexture};
        for(int part=0;part<3;++part)if(textures[part]>0)
            sideAlbedo[((size_t)i*2+side)*3+part]=averageColor(wallImage(texturetranslation[textures[part]]));
    }
    // Bounced light takes the surfaces' own color whole. It used to keep a
    // quarter of it: with 64 rays a sample, whether a few of them landed on
    // a strongly colored surface changed from one sample to the next, and
    // the color lay on the floor in patches, so it was greyed until they
    // no longer showed. The patches were the sampling, not the color: the
    // rays and the smoothing below take them out (gather, smoothPasses).
    // How light the surfaces count (settings.surfaceReturn): the artwork's own colour times this,
    // its hue kept, no channel past 0.85 (nothing returns all it gets).
    if(whole&&settings.surfaceReturn>1) {
        auto lighter=[&](std::array<float,3> &a) {
            float peak=std::max({a[0],a[1],a[2],1e-4f}),scale=std::min(settings.surfaceReturn,0.85f/peak);
            if(scale>1)for(float &c:a)c*=scale;
        };
        for(auto *albedos:{&flatAlbedo,&sideAlbedo})for(auto &a:*albedos)lighter(a);
        lighter(lies);
    }
    level.resize(numsectors);
    for(int i=0;i<numsectors;++i)level[i]=lighting(&sectors[i]);
    skyDome=whole&&settings.skyLight;
    if(whole) {
        Rooms_Build();
        const int rooms=Rooms_Count();
        sectorGroup.resize(numsectors);
        for(int s=0;s<numsectors;++s) {int r=Rooms_Of(s);sectorGroup[s]=r>=0?r:rooms+s;}
        tailGroups=rooms+numsectors;
    }
    // Directional baked light (bake_basis.cpp): the rays also split what they
    // see over the HL2 basis: the bounce, the sky they reach (at the level's
    // sky light), and the surfaces they hit lit by their own sector's light
    // (at a typical albedo).
    lobes=settings.bakedBumps&&!wallLampLobes.empty()&&!wallDirectionCells.empty();
    std::vector<float> skyLights;
    for(int i=0;i<numsectors;++i)if(sectors[i].ceilingpic==skyflatnum)skyLights.push_back(level[i]);
    if(!skyLights.empty()) {std::nth_element(skyLights.begin(),skyLights.begin()+skyLights.size()/2,skyLights.end());skyGlow=daylightLevel(skyLights[skyLights.size()/2]);}
    if(skyDome) {
        Rooms_Build();
        outside.resize(numsectors);
        for(int i=0;i<numsectors;++i)outside[i]=sectors[i].ceilingpic==skyflatnum||Rooms_Outside(i);
        skyShade=sun?sunShade:0;   // open shade lies below the level by this much (lightRule)
        skyEnergy=skyLightColor();
        for(int c=0;c<3;++c)skyRadiance[c]=skyEnergy[c]*skyGlow*(1-skyShade);
    }
    if(rects.empty()) {doors=std::make_unique<OpenDoors>();return;}
    map=bakeMap;doors=std::make_unique<OpenDoors>(map);
    stripsNear(rects,strips);
    // What the solve can read of the light: round its rects (and the rim its
    // samples blend over) and its strips, as far as a ray reaches. The copies
    // take that much, into kept buffers; the rest of them is never read, and
    // commit puts back only the rects and the strips.
    std::array<float,4> box=rects[0];
    auto take=[&](float x0,float y0,float x1,float y1) {
        box[0]=std::min(box[0],x0);box[1]=std::min(box[1],y0);box[2]=std::max(box[2],x1);box[3]=std::max(box[3],y1);
    };
    for(const auto &r:rects)take(r[0],r[1],r[2],r[3]);
    for(int index:strips) {
        const BakeStrip &strip=bakeStrips[index];
        float bx=strip.ax+strip.dx*strip.length,by=strip.ay+strip.dy*strip.length;
        take(std::min(strip.ax,bx),std::min(strip.ay,by),std::max(strip.ax,bx),std::max(strip.ay,by));
    }
    const int step=std::max(1,(int)std::lround(16/mapCell));
    const float reach=(whole?1024.0f:512.0f)+3.5f*step*mapCell+2*mapCell;   // run's bounceReach, rim and a cell's slack each way
    box={box[0]-reach,box[1]-reach,box[2]+reach,box[3]+reach};
    int x0=std::clamp((int)std::floor((box[0]-mapOrigin[0])/mapCell),0,mapWidth-1),x1=std::clamp((int)std::floor((box[2]-mapOrigin[0])/mapCell),0,mapWidth-1);
    int y0=std::clamp((int)std::floor((box[1]-mapOrigin[1])/mapCell),0,mapHeight-1),y1=std::clamp((int)std::floor((box[3]-mapOrigin[1])/mapCell),0,mapHeight-1);
    reached.clear();
    stripsNear({box},reached);
    copyBox[0]=x0;copyBox[1]=y0;copyBox[2]=x1;copyBox[3]=y1;
    if(!deferCopy)copyLight();
}
// A prepared shot's solves keep buffers of their own, on the light worker (keepBuffers' spare is the frame thread's).
SolveBuffers preparedSpare;
void BounceSolve::copyLight() {
    SolveBuffers &from=deferCopy?preparedSpare:spare;
    const int x0=copyBox[0],y0=copyBox[1],x1=copyBox[2],y1=copyBox[3];
    flat.swap(from.flat);wall.swap(from.wall);
    copyFlatBox(flat,flatLightCells,x0,y0,x1,y1);
    copyStrips(wall,wallBakeCells,reached);
    if(lobes) {
        flatDirection.swap(from.flatDirection);wallDirection.swap(from.wallDirection);
        copyFlatBox(flatDirection,flatDirectionCells,x0,y0,x1,y1);
        copyStrips(wallDirection,wallDirectionCells,reached);
    }
    // The pass before the last as the level's bounce left it, as far as the rays reach.
    std::lock_guard<std::mutex> hold(levelCarryLock);
    if(!levelCarry.empty()&&levelCarry.size()==levelCarrySize()) {
        for(int k=0;k<2;++k)carry.buffers[k].swap(from.carry[k]);
        carry.startFrom(levelCarry,(size_t)mapWidth*mapHeight,atlasWidth,x0,y0,x1,y1,mapWidth,reached,bakeStrips);
    }
}
void BounceSolve::run(const Runner &runner) {
    const bool region=!rects.empty();
    const BakeMap &base=region?map:bakeMap;
    uint8_t *flatCells=region?flat.data():flatLightCells.data(),*wallCells=region?wall.data():wallBakeCells.data();
    uint8_t *flatBasis=lobes?(region?flatDirection.data():flatDirectionCells.data()):nullptr;
    uint8_t *wallBasis=lobes?(region?wallDirection.data():wallDirectionCells.data()):nullptr;
    const WeatherKind &weather=weatherKind(kind);
    const size_t layer=(size_t)mapWidth*mapHeight;
    auto flatAt=[&](FlatLayer of,size_t cell) {return flatCells+4*((size_t)of*layer+cell);};
    // Whole-level lighting (settings.lightModel 1, PLAN-light-model.md): light goes on bouncing.
    // The gather runs bouncePasses times, each seeing the surfaces lit by the lamps, the sun and
    // the bounce the pass before left on them (kept in before...), so the last holds light that
    // has bounced that many times: what gets round a corner and under a lintel. Each bounce
    // returns only the surfaces' share, so three are most of what there is. Rays run twice as far.
    // Past the fifth the passes are a series carried on (BounceCarry::extendTail, PLAN-bounce-energy.md
    // step 3). Dev tools: DERP_BOUNCEPASSES=n passes, DERP_BOUNCENOTAIL=1 without the series' rest
    // (20 passes and no tail: the reference the tail is held against).
    const int bouncePasses=whole&&!fast?bounceWholePasses():1;
    const bool tail=bouncePasses>=4&&!bounceNoTail();
    const float bounceReach=whole?1024.0f:512.0f;
    int bouncePass=0;
    std::vector<uint8_t> beforeFlat,beforeWall;   // DERP_BOUNCE8: the pass before's 8 bits, as it was
    // The light between the passes, in half floats (bounce_carry.h): a region's from the level's
    // (copyLight), else from the 8 bits the passes start on. DERP_BOUNCE8=1 (dev tools) keeps it in
    // the level's 8 bits as before, to compare.
    // The lamps' light as baked, not as drawn (radiance); DERP_BOUNCEHEADROOM=1 (dev tools) as drawn, to compare.
#ifdef DOOM_DEVTOOLS
    static const bool asDrawn=getenv("DERP_BOUNCEHEADROOM")!=nullptr;
#else
    constexpr bool asDrawn=false;
#endif
    const bool trueLamps=whole&&!asDrawn;
    if(bouncePasses<=1||bounceEightBits())carry=BounceCarry{};
    else if(!region||!carry.on())carry.start(flatCells,layer,floorBounce,wallCells,atlasWidth,atlasStride,atlasHeight);
    constexpr float skyInGain=8;
    // What a surface in the sun gives back counts less than the sky's light does: a sun patch
    // is small and hard, and at the sky's strength the wall under a window glowed in the
    // colour of the sunlit carpet before it.
    constexpr float sunInGain=3;
    // out: what the surface the ray hit gives back of its lamps and of the bounce the pass before left on it
    // (true strength: see skyDome); day: of the sun on it, as drawn.
    // Where a ray stopped, as the passes read it: the surface and its texel or cell.
    auto landed=[&](const BakeHit &hit) {
        RaySeen seen;
        if(hit.kind==BakeHit::thing)return seen; // Decorations stay dark.
        seen.sector=hit.sector;
        if(hit.kind==BakeHit::wall) {
            const BakeLine &line=base.lines[hit.line];
            int side=line.front==hit.sector?0:1;
            const BakeStrip &strip=bakeStrips[(size_t)hit.line*2+side];
            seen.surface=((uint64_t)hit.line*2+side)*3+hit.part;
            seen.kind=rayBare;
            if(strip.x<0)return seen;
            float along=(hit.x-strip.ax)*strip.dx+(hit.y-strip.ay)*strip.dy;
            int column=std::clamp((int)std::floor(along*strip.scale),0,strip.width-1);
            int row=std::clamp((int)std::floor((hit.z-strip.low)*strip.scale),0,strip.height-1);
            seen.kind=rayWall;seen.at=(uint64_t)(strip.y+row)*atlasStride+strip.x+column;
        } else {
            int cx=std::clamp((int)std::floor((hit.x-mapOrigin[0])/mapCell),0,mapWidth-1);
            int cy=std::clamp((int)std::floor((hit.y-mapOrigin[1])/mapCell),0,mapHeight-1);
            seen.kind=rayFlat;seen.surface=(uint64_t)hit.sector*2+(hit.kind==BakeHit::ceiling);seen.at=(uint64_t)cy*mapWidth+cx;
        }
        return seen;
    };
    auto radiance=[&](const RaySeen &hit,float out[3],float day[3]) {
        if(hit.kind!=rayWall&&hit.kind!=rayFlat)return;
        const uint8_t *light=nullptr;float visible=0;const std::array<float,3> *albedo=nullptr;
        float again[3]={};   // the bounce the pass before left on it
        std::array<float,3> covered;
        if(hit.kind==rayWall) {
            albedo=&sideAlbedo[hit.surface];
            light=&wallCells[4*hit.at];
            if(bouncePass>0) {
                if(carry.on())carry.read(carry.wallEntry(hit.at,atlasStride),again);
                else for(int c=0;c<3;++c)again[c]=beforeWall[4*(hit.at+atlasWidth)+c]/255.0f*2;
            }
            visible=light[3]/255.0f;
        } else {
            size_t cell=hit.at;bool ceiling=hit.surface&1;
            albedo=&flatAlbedo[hit.surface];
            light=flatAt(ceiling?ceilingLight:floorLight,cell);
            if(bouncePass>0) {
                if(carry.on())carry.read(carry.flatEntry(ceiling,cell),again);
                else for(int c=0;c<3;++c)again[c]=beforeFlat[4*((size_t)(ceiling?ceilingBounce:floorBounce)*layer+cell)+c]/255.0f*2;
            }
            visible=ceiling?0:contactCells[4*cell+2]/255.0f;
            // snowAt's share of covered texels at that depth, roughly.
            if(uint8_t ground=!ceiling&&kind!=WEATHER_NONE?groundBakeAt(cell):0) {
                float deep=(ground-1)/254.0f;covered=*albedo;
                if(weather.settles) {
                    float share=std::clamp((deep*1.1f-0.35f)/0.45f,0.0f,1.0f);
                    for(int c=0;c<3;++c)covered[c]+=(lies[c]-covered[c])*share;
                } else if(weather.wets)for(float &c:covered)c*=1-0.36f*deep;
                albedo=&covered;
            }
        }
        // The surface shows its static light within the headroom its sector
        // leaves (fillHeadroom), as world.frag draws it.
        float shown[3]={};
        // Whole-level lighting takes the lamps' light as baked, at its true strength as it does the sky's
        // (PLAN-bounce-energy.md step 2): what the picture leaves of it is the drawing's business, and the
        // wall beside a lamp, where most of the first bounce comes from, was flattened first.
        if(lights) {for(int c=0;c<3;++c)shown[c]=light[c]/255.0f*2;if(!trueLamps)fillHeadroom(shown,level[hit.sector]);}
        for(int c=0;c<3;++c) {
            out[c]=(*albedo)[c]*(shown[c]+again[c]);
            day[c]=(*albedo)[c]*sunEnergy[c]*visible;
        }
    };
    // 256 rays a sample (16 with Settings::fastBakes), see the gather.
    const int side=fast?4:16,rays=side*side;
    HitStore *store=nullptr;   // set before the passes
    const RayTable &pattern=rayTable(side);
    // slot: where the sample's rays are kept (store), or null.
    // The card's values this pass (gpuBounceGather), by sample.
    std::vector<std::array<float,6>> cardValues;
    bool cardPass=false;
    auto gather=[&](int sector,float x,float y,float z,const float normal[3],uint32_t seed,float out[6],uint32_t **slot=nullptr) {
        if(cardPass&&slot) {
            int32_t at=-1;
            if(slot>=store->flats.data()&&slot<store->flats.data()+store->flats.size())at=store->cardFlat[slot-store->flats.data()];
            else if(slot>=store->walls.data()&&slot<store->walls.data()+store->walls.size())at=store->cardWall[slot-store->walls.data()];
            if(at>=0) {for(int c=0;c<6;++c)out[c]=cardValues[at][c];return;}
        }
        // A per-sample rotation of the stratified pattern trades banding for fine noise
        // (one shared pattern, as the sky share has, drew its rays' spokes across open
        // yards), and the smoothing of neighbouring samples then takes the noise out.
        // 256 rays (16 with Settings::fastBakes): with 64, whether a few rays landed on
        // a sunlit, a shaded or a strongly colored surface swung a sample enough to
        // blotch floors and whole facades. Load time is free: E1M1 0.2 s -> 0.8 s.
        seed=seed*2654435761u;seed^=seed>>15;
        const std::array<float,3> *turned=&pattern.at[(size_t)(seed&1023)*rays];
        // The sample's frame, as cosineDirection makes it.
        float helper[3]={std::fabs(normal[2])<0.9f?0.0f:1.0f,0,std::fabs(normal[2])<0.9f?1.0f:0.0f};
        float tangent[3]={helper[1]*normal[2]-helper[2]*normal[1],helper[2]*normal[0]-helper[0]*normal[2],helper[0]*normal[1]-helper[1]*normal[0]};
        float tangentLength=std::sqrt(tangent[0]*tangent[0]+tangent[1]*tangent[1]+tangent[2]*tangent[2]);
        for(float &t:tangent)t/=tangentLength;
        float bitangent[3]={normal[1]*tangent[2]-normal[2]*tangent[1],normal[2]*tangent[0]-normal[0]*tangent[2],normal[0]*tangent[1]-normal[1]*tangent[0]};
        for(int c=0;c<6;++c)out[c]=0;
        const BakeMap &traced=doors->of(sector);
        // The sky's light per ray that reaches it, and the most daylight the sample may hold: the same for every place (see skyDome).
        const float gain=skyDome?skyInGain:1,most=skyDome?luminance(skyRadiance.data())*day:0;
        float daylight[3]={};
        int seen=0;   // rays that reached the sky
        // The sample's rays as an earlier pass (or solve) kept them, or traced now and kept.
        // The split over the basis is kept from the last pass only (each pass wrote it over the one
        // before): the passes before it leave it out, and with it each kept ray's direction.
        const bool split=lobes&&bouncePass==bouncePasses-1;
        const uint32_t *kept=slot&&*slot&&*slot!=HitStore::untraced()?*slot:nullptr;
        bool keeping=slot&&!*slot;
        uint32_t words[256];uint64_t wide[256];int wideCount=0;
        for(int n=0;n<rays;++n) {
            float direction[3],hitLight[3]={};RaySeen hit;
            if(!kept||split)for(int i=0;i<3;++i)direction[i]=tangent[i]*turned[n][0]+bitangent[i]*turned[n][1]+normal[i]*turned[n][2];
            if(kept)hit=store->unpack(kept,n,base);
            else {
                BakeHit stopped;bool skyward=false;
                if(traceRay(traced,sector,x,y,z,direction,bounceReach,SkyCeiling::escape,&stopped,nullptr,&skyward)||stopped.kind==BakeHit::none) {
                    // Only a ray that left through the sky brings its light. One that hit nothing
                    // (it slipped out at a corner or under a ledge and ran on through the void) was
                    // taken for sky too, and drew a bright line up every such corner and edge.
                    // The sky, seen from under a roof: through the map as it stands, so not through
                    // a door that is shut (the bounce otherwise counts doors open).
                    BakeHit real;
                    bool reached=skyward&&skyDome&&(&traced==&base||(traceRay(base,sector,x,y,z,direction,bounceReach,SkyCeiling::escape,&real,nullptr,&skyward)&&skyward));
                    hit.kind=reached?raySky:rayEscaped;
                } else hit=landed(stopped);
                if(keeping&&!HitStore::pack(hit,base,words[n])) {
                    if(HitStore::packWide(hit,base,wide[wideCount]))words[n]=(uint32_t)rayWide<<29|(uint32_t)wideCount++;
                    else keeping=false;
                }
            }
            if(hit.kind==rayEscaped||hit.kind==raySky) {
                if(split)addLobes(normal,direction,skyGlow/rays,out+3);
                if(hit.kind==raySky)++seen;
                continue;
            }
            float hitDay[3]={};
            radiance(hit,hitLight,hitDay);
            for(int c=0;c<3;++c) {out[c]+=hitLight[c]/rays;daylight[c]+=hitDay[c]*(skyDome?sunInGain:1)/rays;hitLight[c]+=hitDay[c];}
            if(split&&hit.kind!=rayNothing)addLobes(normal,direction,(luminance(hitLight)+0.3f*level[hit.sector])/rays,out+3);
        }
        if(slot&&!*slot) {
            if(keeping)store->keep(*slot,words,wide,wideCount);
            else *slot=HitStore::untraced();
        }
        // The sky's own light on the place, by how much of the sky it sees, as the eye takes it:
        // not in proportion. A place that sees a quarter of the dome or more (open ground; a
        // wall's half counts as all) stands at the sky's level; one that sees a sliver, the floor
        // of a room by its window, gets the square root's share of it, so a sixteenth of that
        // view still gives a quarter of the light. In proportion a window was a hundredth of
        // the sky and lit nothing, while a stronger sky to make up for it left no shade
        // outside. One curve for every place, so nothing steps where a roof begins.
        // The passes before the last hold it at its true strength, for what it lights in turn.
        if(skyDome&&seen>0) {
            constexpr float fullView=0.25f;
            float view=seen/(float)rays*(std::fabs(normal[2])<=0.5f?2:1);
            // With F4 Eye adaptation the eye is the picture's own (exposure.cpp), so here the light is
            // plain: in proportion to the sky seen, at its true strength, and at the end down to
            // what is stored, day times the sky's level, which the exposure then brings down.
            bool last=bouncePass==bouncePasses-1;
            float share=adapts?(last?std::min(day,view*gain):view*gain):std::min(1.0f,std::sqrt(view/fullView))*(last?1:gain);
            for(int c=0;c<3;++c)daylight[c]+=skyRadiance[c]*share;
        }
        for(int c=0;c<3;++c)out[c]+=daylight[c];
        // The last pass: down to what can be drawn. Plain up to 0.6 of the sky's level, then a shoulder up to it.
        if(skyDome&&bouncePass==bouncePasses-1) {
            float is=luminance(out),knee=most*0.6f;
            if(is>knee&&most>knee) {
                float fit=knee+(most-knee)*(1-std::exp(-(is-knee)/(most-knee)));
                for(int c=0;c<3;++c)out[c]*=fit/is;
            }
        }
    };
    // The basis texel from a sample's split, the lamps' and the sector's own light.
    auto basis=[&](const float sampled[6],const std::array<uint16_t,3> &lamps,int sector,uint8_t *out) {
        float even=level[sector]*ambientLobe,split[3]={sampled[3]+even,sampled[4]+even,sampled[5]+even};
        lampLobes(lamps,split);writeBasis(out,split);
    };
    // Unrounded (8 bits at half scale): bounce also colors the sector light
    // (lampTint), where 1/32 steps would band.
    auto encode=[](const float value[3],uint8_t *out) {
        for(int c=0;c<3;++c)out[c]=(uint8_t)std::min(255L,std::lround(value[c]*0.5f*255));
    };
    int step=std::max(1,(int)std::lround(16/mapCell));
    // Floors and ceilings blend their samples by the surface, not by the sector: two places
    // share their light when they lie on one floor (or ceiling) at one height with nothing
    // standing between them, whatever sectors the map cut that floor into. Kept each to its
    // own sector (which stopped light leaking through walls) every sector showed as a patch
    // of its own with a hard outline, Doom's floor shapes drawn in light, and plainly so once
    // the sky's light came this way. The wall between two rooms stops the blend as before:
    // the way from one place to the other is traced along the surface.
    auto sameSurface=[&](int a,float ax,float ay,int b,float bx,float by,int ceiling) {
        if(a==noSector||b==noSector)return false;
        if(a==b)return true;
        const BakeSector &sa=base.sectors[a],&sb=base.sectors[b];
        if(ceiling?(sa.ceiling!=sb.ceiling||sa.sky||sb.sky):sa.floor!=sb.floor)return false;
        float d[3]={bx-ax,by-ay,0},length=std::hypot(d[0],d[1]);
        if(length<0.01f)return true;
        d[0]/=length;d[1]/=length;
        BakeHit hit;
        return traceRay(base,a,ax,ay,ceiling?sa.ceiling-1:sa.floor+1,d,length,SkyCeiling::open,&hit);
    };
    // A region samples a little past its edge, so the samples its cells blend
    // were smoothed among neighbours as in the level's bake.
    const float rim=3.5f*step*mapCell;
    // The rays are kept where they are shot more than once: over the passes, or for a solve to come.
    if(hits&&(hits->rays!=rays||hits->step!=step||hits->reach!=bounceReach||(skyDome&&!hits->sky)||
              hits->flats.size()!=layer*2||hits->cellSame.size()!=layer||hits->wallStart.size()!=bakeStrips.size()+1))hits.reset();
    if(!hits&&(bouncePasses>1||keepHits))hits=std::make_shared<HitStore>(rays,step,bounceReach,skyDome);
    store=hits.get();
    // The level's bounce on the graphics card (gpu_bakes.cpp): the samples the passes gather
    // (the floors' and ceilings' block centres and the walls' coarse samples, with their seeds
    // and slots) go up once and their rays are traced and kept there; each pass the card
    // gathers them all and the passes below take its values where they would have gathered.
    // A cell too small for a block's sample of its own (see the cells' loop), and everything
    // when the card cannot, is gathered here as before. The flicker share's solve
    // (bounceFlicker) shares the store, and so the card's rays. A region's solve in play (round a
    // door or a lamp shot out) takes the samples within its rects and rim and its own strips, on
    // its copy of the map, when re-bakes run on the card (gpuRelightOn).
    if(store&&!store->card&&(region?gpuRelightOn():gpuBakesOn())) {
        uint64_t cardStart=SDL_GetTicksNS();
        std::vector<BakeSample> asks;
        store->cardFlat.assign(store->flats.size(),-1);store->cardWall.assign(store->walls.size(),-1);
        auto turn=[](uint32_t seed) {seed=seed*2654435761u;seed^=seed>>15;return seed&1023;};
        int blocksX=(mapWidth+step-1)/step,blocksY=(mapHeight+step-1)/step;
        for(int by=0;by<blocksY;++by)for(int bx=0;bx<blocksX;++bx) {
            int x=std::min(bx*step+step/2,mapWidth-1),y=std::min(by*step+step/2,mapHeight-1);
            if(!inside(mapOrigin[0]+(x+0.5f)*mapCell,mapOrigin[1]+(y+0.5f)*mapCell,rim))continue;
            int sector=seamCells[(size_t)y*mapWidth+x].own;
            if(sector==noSector)continue;
            for(int ceiling=0;ceiling<2;++ceiling) {
                if((ceiling?sectors[sector].ceilingpic:sectors[sector].floorpic)==skyflatnum)continue;
                const BakeSector &bs=doors->of(sector).sectors[sector];
                store->cardFlat[((size_t)y*mapWidth+x)*2+ceiling]=(int32_t)asks.size();
                asks.push_back({mapOrigin[0]+(x+0.5f)*mapCell,mapOrigin[1]+(y+0.5f)*mapCell,ceiling?bs.ceiling-1:bs.floor+1,sector,
                                {0,0,ceiling?-1.0f:1.0f},turn((uint32_t)(y*mapWidth+x)*2+ceiling),false});
            }
        }
        for(int index:region?strips:bakeOrder) {
            const BakeStrip &strip=bakeStrips[index];
            int columns=(strip.width+step-1)/step,rows=(strip.height+step-1)/step;
            for(int cy=0;cy<rows;++cy)for(int cx=0;cx<columns;++cx) {
                int column=std::min(cx*step+step/2,strip.width-1),row=std::min(cy*step+step/2,strip.height-1);
                float along=std::clamp((column+0.5f)/strip.scale,0.5f,strip.length-0.5f);
                float z=std::min(strip.low+(row+0.5f)/strip.scale,strip.high);
                store->cardWall[store->wallStart[index]+(size_t)cy*columns+cx]=(int32_t)asks.size();
                asks.push_back({strip.ax+strip.dx*along+strip.dy*0.5f,strip.ay+strip.dy*along-strip.dx*0.5f,z,strip.sector,
                                {strip.dy,-strip.dx,0},turn((uint32_t)index*7919u+cy*131u+cx),true});
            }
        }
        store->card=asks.empty()?nullptr:gpuBounceStart(base,asks,rays,pattern.at,bounceReach,skyDome,*doors);
        if(!store->card) {store->cardFlat.clear();store->cardWall.clear();}
        else if(!region)fprintf(stderr,"3D bounce: %zu samples' rays traced on the graphics card in %.0f ms.\n",asks.size(),(SDL_GetTicksNS()-cardStart)/1e6);
    }
    std::vector<uint8_t> ground;   // the weather's ground a cell, for the card
    if(store&&store->card&&kind!=WEATHER_NONE) {
        ground.resize(layer);
        for(size_t i=0;i<layer;++i)ground[i]=groundBakeAt(i);
    }
    double cardMs=0,passesMs=0;   // for the log: the card's gathers, and the passes in all
    const uint64_t passesStart=SDL_GetTicksNS();
    for(bouncePass=0;bouncePass<bouncePasses&&!cancel;++bouncePass) {
    if(bouncePass>0&&!carry.on()) {beforeFlat.assign(flatCells,flatCells+layer*16);beforeWall.assign(wallCells,wallCells+(size_t)atlasStride*atlasHeight*4);}
    // The passes before the last leave their light in the carry; the last in the level's 8 bits.
    const bool carried=carry.on()&&bouncePass<bouncePasses-1;
    cardPass=false;
    if(store&&store->card&&!cancel) {
        GpuGatherPass p;
        p.pass=bouncePass;p.passes=bouncePasses;
        p.lights=lights;p.skyDome=skyDome;p.adapts=adapts;p.split=lobes&&bouncePass==bouncePasses-1;
        p.weather=kind!=WEATHER_NONE;p.settles=weather.settles;p.wets=weather.wets;p.ground=&ground;p.trueLamps=trueLamps;
        for(int c=0;c<3;++c) {p.sunEnergy[c]=sunEnergy[c];p.lies[c]=lies[c];p.skyRadiance[c]=skyRadiance[c];}
        p.skyGlow=skyGlow;p.day=day;p.skyGain=skyDome?skyInGain:1;p.most=skyDome?luminance(skyRadiance.data())*day:0;p.sunGain=sunInGain;
        p.sideAlbedo=&sideAlbedo;p.flatAlbedo=&flatAlbedo;p.level=&level;
        p.flatCells=flatCells;p.wallCells=wallCells;p.contact=contactCells.data();
        if(carry.on()) {p.carry=&carry.readable();p.carryWalls=carry.wallStart;}
        const uint64_t cardStart=SDL_GetTicksNS();
        cardPass=gpuBounceGather(*store->card,p,cardValues);
        cardMs+=(SDL_GetTicksNS()-cardStart)/1e6;
        // The card could not: this pass and the rest here, the rays traced as before.
        if(!cardPass) {store->card.reset();store->cardFlat.clear();store->cardWall.clear();}
    }
    // Floors and ceilings: samples at block centers, then each cell blends the
    // samples around it that lie in its own sector; a cell with none nearby
    // (a sector smaller than a block) takes its own sample.
    int blocksX=(mapWidth+step-1)/step,blocksY=(mapHeight+step-1)/step;
    std::vector<std::array<float,6>> samples((size_t)blocksX*blocksY*2);
    std::vector<int> sampleSector((size_t)blocksX*blocksY,noSector);
    static const float up[3]={0,0,1},down[3]={0,0,-1};
    auto flatSample=[&](int sector,int x,int y,bool ceiling,float out[6]) {
        for(int c=0;c<6;++c)out[c]=0;
        int pic=ceiling?sectors[sector].ceilingpic:sectors[sector].floorpic;
        if(pic==skyflatnum)return;
        const BakeSector &s=doors->of(sector).sectors[sector];
        gather(sector,mapOrigin[0]+(x+0.5f)*mapCell,mapOrigin[1]+(y+0.5f)*mapCell,ceiling?s.ceiling-1:s.floor+1,
               ceiling?down:up,(uint32_t)(y*mapWidth+x)*2+ceiling,out,store?&store->flats[((size_t)y*mapWidth+x)*2+ceiling]:nullptr);
    };
    runner(blocksY,[&](int by) {
        for(int bx=0;bx<blocksX&&!cancel;++bx) {
            int x=std::min(bx*step+step/2,mapWidth-1),y=std::min(by*step+step/2,mapHeight-1);
            if(!inside(mapOrigin[0]+(x+0.5f)*mapCell,mapOrigin[1]+(y+0.5f)*mapCell,rim))continue;
            size_t b=(size_t)by*blocksX+bx;int sector=seamCells[(size_t)y*mapWidth+x].own;
            sampleSector[b]=sector;if(sector==noSector)continue;
            for(int ceiling=0;ceiling<2;++ceiling)flatSample(sector,x,y,ceiling,samples[b*2+ceiling].data());
        }
    });
    // The floors' and ceilings' samples smoothed 1-2-1 each way, twice, among
    // the samples of their own sector (the walls' are smoothed along their
    // strip): bounced light is scattered light and changes slowly over a
    // surface, so what differs between neighbours 16 units apart is the
    // rays' noise.
    const int smoothPasses=2;
    for(int pass=0;pass<smoothPasses;++pass) {
        std::vector<std::array<float,6>> before=samples;
        const bool known=store&&store->blocksKnown;
        runner(blocksY,[&](int by) {
            for(int bx=0;bx<blocksX;++bx) {
                size_t b=(size_t)by*blocksX+bx;int sector=sampleSector[b];
                if(sector==noSector)continue;
                float sum[2][6]={},total[2]={};
                auto place=[&](int px,int py,float &wx,float &wy) {
                    wx=mapOrigin[0]+(std::min(px*step+step/2,mapWidth-1)+0.5f)*mapCell;wy=mapOrigin[1]+(std::min(py*step+step/2,mapHeight-1)+0.5f)*mapCell;
                };
                float ox,oy;place(bx,by,ox,oy);
                uint32_t same=known?store->blockSame[b]:0;
                for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx) {
                    int nx=bx+dx,ny=by+dy;
                    if(nx<0||ny<0||nx>=blocksX||ny>=blocksY)continue;
                    size_t n=(size_t)ny*blocksX+nx;
                    float w=(dx?1.0f:2.0f)*(dy?1.0f:2.0f),sx,sy;place(nx,ny,sx,sy);
                    for(int ceiling=0;ceiling<2;++ceiling) {
                        const uint32_t bit=1u<<(((dy+1)*3+dx+1)*2+ceiling);
                        if(!known&&sameSurface(sector,ox,oy,sampleSector[n],sx,sy,ceiling))same|=bit;
                        if(!(same&bit))continue;
                        for(int c=0;c<6;++c)sum[ceiling][c]+=before[n*2+ceiling][c]*w;
                        total[ceiling]+=w;
                    }
                }
                for(int ceiling=0;ceiling<2;++ceiling)for(int c=0;c<6;++c)samples[b*2+ceiling][c]=sum[ceiling][c]/total[ceiling];
                if(store&&!known)store->blockSame[b]=same;
            }
        });
        if(store)store->blocksKnown=true;
    }
    const bool cellsKnown=store&&store->cellsKnown;
    runner(mapHeight,[&](int y) {
        for(int x=0;x<mapWidth&&!cancel;++x) {
            size_t i=(size_t)y*mapWidth+x;int sector=seamCells[i].own;
            if(sector==noSector||!inside(mapOrigin[0]+(x+0.5f)*mapCell,mapOrigin[1]+(y+0.5f)*mapCell,0))continue;
            float gx=(x-step/2)/(float)step,gy=(y-step/2)/(float)step;
            int x0=(int)std::floor(gx),y0=(int)std::floor(gy);float fx=gx-x0,fy=gy-y0;
            float sum[2][6]={},total[2]={};
            float ox=mapOrigin[0]+(x+0.5f)*mapCell,oy=mapOrigin[1]+(y+0.5f)*mapCell;
            uint8_t same=cellsKnown?store->cellSame[i]:0;
            for(int k=0;k<4;++k) {
                int bx=x0+(k&1),by=y0+(k>>1);
                if(bx<0||by<0||bx>=blocksX||by>=blocksY)continue;
                size_t b=(size_t)by*blocksX+bx;
                float w=((k&1)?fx:1-fx)*((k>>1)?fy:1-fy)+1e-4f;
                float sx=mapOrigin[0]+(std::min(bx*step+step/2,mapWidth-1)+0.5f)*mapCell,sy=mapOrigin[1]+(std::min(by*step+step/2,mapHeight-1)+0.5f)*mapCell;
                for(int ceiling=0;ceiling<2;++ceiling) {
                    const uint8_t bit=(uint8_t)(1u<<(k*2+ceiling));
                    if(!cellsKnown&&sameSurface(sector,ox,oy,sampleSector[b],sx,sy,ceiling))same|=bit;
                    if(!(same&bit))continue;
                    for(int c=0;c<6;++c)sum[ceiling][c]+=samples[b*2+ceiling][c]*w;
                    total[ceiling]+=w;
                }
            }
            if(store&&!cellsKnown)store->cellSame[i]=same;
            for(int ceiling=0;ceiling<2;++ceiling) {
                float value[6];
                if(total[ceiling]>0)for(int c=0;c<6;++c)value[c]=sum[ceiling][c]/total[ceiling];
                else flatSample(sector,x,y,ceiling,value);
                if(carried)carry.write(carry.flatEntry(ceiling,i),value);
                else encode(value,flatAt(ceiling?ceilingBounce:floorBounce,i));
                if(lobes&&bouncePass==bouncePasses-1)basis(value,flatLampLobes[ceiling*layer+i],sector,&flatBasis[4*((2+ceiling)*layer+i)]);
            }
        }
    });
    if(store)store->cellsKnown=true;
    // Walls: samples every step texels inside each strip, blended bilinearly.
    const std::vector<int> &order=region?strips:bakeOrder;
    runner((int)order.size(),[&](int n) {
        if(cancel)return;
        const BakeStrip &strip=bakeStrips[order[n]];
        const float normal[3]={strip.dy,-strip.dx,0};
        int columns=(strip.width+step-1)/step,rows=(strip.height+step-1)/step;
        std::vector<std::array<float,6>> coarse((size_t)columns*rows);
        for(int cy=0;cy<rows;++cy)for(int cx=0;cx<columns;++cx) {
            int column=std::min(cx*step+step/2,strip.width-1),row=std::min(cy*step+step/2,strip.height-1);
            float along=std::clamp((column+0.5f)/strip.scale,0.5f,strip.length-0.5f);
            float z=std::min(strip.low+(row+0.5f)/strip.scale,strip.high);
            gather(strip.sector,strip.ax+strip.dx*along+strip.dy*0.5f,strip.ay+strip.dy*along-strip.dx*0.5f,z,normal,
                   (uint32_t)order[n]*7919u+cy*131u+cx,coarse[(size_t)cy*columns+cx].data(),
                   store?&store->walls[store->wallStart[order[n]]+(size_t)cy*columns+cx]:nullptr);
        }
        smoothCoarse(coarse,columns,rows);
        for(int row=0;row<strip.height;++row)for(int column=0;column<strip.width;++column) {
            float gx=std::clamp((column-step/2)/(float)step,0.0f,columns-1.0f),gy=std::clamp((row-step/2)/(float)step,0.0f,rows-1.0f);
            int x0=std::min((int)gx,columns-1),y0=std::min((int)gy,rows-1),x1=std::min(x0+1,columns-1),y1=std::min(y0+1,rows-1);
            float fx=gx-x0,fy=gy-y0,value[6];
            for(int c=0;c<6;++c)
                value[c]=(coarse[(size_t)y0*columns+x0][c]*(1-fx)+coarse[(size_t)y0*columns+x1][c]*fx)*(1-fy)+
                         (coarse[(size_t)y1*columns+x0][c]*(1-fx)+coarse[(size_t)y1*columns+x1][c]*fx)*fy;
            size_t at=4*((size_t)(strip.y+row)*atlasStride+atlasWidth+strip.x+column);
            if(carried)carry.write(carry.wallStart+(size_t)(strip.y+row)*atlasWidth+strip.x+column,value);
            else encode(value,&wallCells[at]);
            if(lobes&&bouncePass==bouncePasses-1)basis(value,wallLampLobes[(size_t)(strip.y+row)*atlasWidth+strip.x+column],strip.sector,&wallBasis[at]);
        }
    });
    if(carried) {
        if(tail&&(bouncePass==bouncePasses-3||bouncePass==bouncePasses-2)&&(int)sectorGroup.size()==numsectors) {
            const bool strips=stripAtlas.size()==(size_t)atlasWidth*atlasHeight;
            auto groupOf=[&](size_t entry) {
                int sector;
                if(entry<carry.wallStart)sector=seamCells[entry%layer].own;
                else {
                    if(!strips)return -1;
                    int strip=stripAtlas[entry-carry.wallStart];
                    if(strip<0)return -1;
                    sector=bakeStrips[strip].sector;
                }
                return sector>=0&&sector<numsectors?sectorGroup[sector]:-1;
            };
            if(bouncePass==bouncePasses-3)carry.noteChange(tailGroups,groupOf,runner);
            else carry.extendTail(tailGroups,groupOf,runner);
        }
        carry.flip();
    }
    }
    passesMs=(SDL_GetTicksNS()-passesStart)/1e6;
    if(cardMs>0&&!region)fprintf(stderr,"3D bounce: %d passes in %.0f ms, %.0f of them gathering on the graphics card.\n",bouncePasses,passesMs,cardMs);
    if(!keepHits)hits.reset();
}
// A region's result into the level's light, and to the GPU.
void BounceSolve::commit() {
    const size_t layer=(size_t)mapWidth*mapHeight;
    if(flat.size()!=flatLightCells.size()||wall.size()!=wallBakeCells.size()||!flatLightTexture||!wallBakeTexture)return;
    bool basis=lobes&&flatDirection.size()==flatDirectionCells.size()&&wallDirection.size()==wallDirectionCells.size()&&
               flatDirectionTexture&&wallDirectionTexture;
    std::vector<std::array<int,4>> flats,flatBasis,walls;
    for(const auto &r:rects) {
        int x0=std::clamp((int)std::floor((r[0]-mapOrigin[0])/mapCell),0,mapWidth-1),x1=std::clamp((int)std::floor((r[2]-mapOrigin[0])/mapCell),0,mapWidth-1);
        int y0=std::clamp((int)std::floor((r[1]-mapOrigin[1])/mapCell),0,mapHeight-1),y1=std::clamp((int)std::floor((r[3]-mapOrigin[1])/mapCell),0,mapHeight-1);
        for(int y=y0;y<=y1;++y)for(int x=x0;x<=x1;++x) {
            size_t i=(size_t)y*mapWidth+x;
            for(FlatLayer of:{floorBounce,ceilingBounce}) {
                size_t at=4*((size_t)of*layer+i);
                for(int c=0;c<3;++c)flatLightCells[at+c]=flat[at+c];   // the alphas are other bakes'
                if(basis)for(int c=0;c<3;++c)flatDirectionCells[at+c]=flatDirection[at+c];   // the alpha is bounceFlicker's
            }
        }
        for(int of:{(int)floorBounce,(int)ceilingBounce})flats.push_back({x0,y0+of*mapHeight,x1-x0+1,y1-y0+1});
    }
    for(int index:strips) {
        const BakeStrip &strip=bakeStrips[index];
        for(int row=0;row<strip.height;++row)for(int column=0;column<strip.width;++column) {
            size_t at=4*((size_t)(strip.y+row)*atlasStride+atlasWidth+strip.x+column);
            for(int c=0;c<3;++c)wallBakeCells[at+c]=wall[at+c];
            if(basis)for(int c=0;c<3;++c)wallDirectionCells[at+c]=wallDirection[at+c];
        }
        walls.push_back({strip.x+atlasWidth,strip.y,strip.width,strip.height});
    }
    if(!lightTarget)++lightCellsSerial;   // (a prepared shot's goes into its copies: relight.cpp)
    // Its carry into the level's, for the solves after it. (A prepared shot's stays out: the level's
    // carry keeps the light from before that shot round it until a solve there puts it in.)
    if(!lightTarget&&carry.on()) {
        std::lock_guard<std::mutex> hold(levelCarryLock);
        const std::vector<uint16_t> &from=carry.readable();
        if(levelCarry.size()==from.size()) {
            auto put=[&](size_t entry,size_t count) {std::memcpy(&levelCarry[3*entry],&from[3*entry],6*count);};
            for(const auto &r:rects) {
                int x0=std::clamp((int)std::floor((r[0]-mapOrigin[0])/mapCell),0,mapWidth-1),x1=std::clamp((int)std::floor((r[2]-mapOrigin[0])/mapCell),0,mapWidth-1);
                int y0=std::clamp((int)std::floor((r[1]-mapOrigin[1])/mapCell),0,mapHeight-1),y1=std::clamp((int)std::floor((r[3]-mapOrigin[1])/mapCell),0,mapHeight-1);
                for(int ceiling=0;ceiling<2;++ceiling)for(int y=y0;y<=y1;++y)put(carry.flatEntry(ceiling,(size_t)y*mapWidth+x0),(size_t)(x1-x0+1));
            }
            for(int index:strips) {
                const BakeStrip &strip=bakeStrips[index];
                if(strip.x<0)continue;
                for(int row=0;row<strip.height;++row)put(carry.wallStart+(size_t)(strip.y+row)*atlasWidth+strip.x,(size_t)strip.width);
            }
        }
    }
    // To the GPU a texture a frame (queueLightUpload, relight.cpp).
    queueLightUpload(flatLightTexture,flats,flatLightCells,mapWidth*4);
    queueLightUpload(wallBakeTexture,walls,wallBakeCells,atlasStride*4);
    if(basis) {
        queueLightUpload(flatDirectionTexture,flats,flatDirectionCells,mapWidth*4);
        queueLightUpload(wallDirectionTexture,walls,wallDirectionCells,atlasStride*4);
    }
}
// The bounce round what changed for good, solved in the background.
// Whole-level lighting only: there the bounce carries much of a room's light,
// and a room whose lamp was shot out stood lit by light that no longer
// reached it. Doors, lifts and lowering walls leave the bounce as the level
// baked it, as Classic does. Places wait in dirty until nothing near has
// changed for reBounceWait tics and no re-light runs; a re-light that starts
// meanwhile stops the solve, and it starts again when that has settled.
// The solve runs on the light worker (lightWorkerPost), which holds it too
// until run returns. A solve stopped for a re-light is not waited for: it
// stops at its next row or strip on the worker, the re-light queued behind
// it, and its result is dropped; the frame thread takes its buffers back
// once its done is set (reapStopped).
struct Rebounce {
    bool running=false;
    std::shared_ptr<BounceSolve> solve;
    std::vector<std::shared_ptr<BounceSolve>> stopped;
    uint64_t start=0;
} rebounce;
std::vector<std::array<float,4>> dirty;
int dirtyTic=0;
// The sectors' light as the level's bake read it, and the sky's: a place solved
// again must count the surfaces as that bake did, or it would stand out from the
// rest (and a room caught mid-flicker would keep that moment's bounce).
std::vector<float> bakedLevel;
float bakedSkyGlow=0;
constexpr int reBounceWait=4;
void stopSolve(bool keep) {
    if(!rebounce.running)return;
    rebounce.solve->cancel=true;rebounce.running=false;
    if(keep) {dirty.insert(dirty.end(),rebounce.solve->rects.begin(),rebounce.solve->rects.end());dirtyTic=leveltime;}
    rebounce.stopped.push_back(std::move(rebounce.solve));
}
void reapStopped() {
    for(size_t n=0;n<rebounce.stopped.size();)
        if(rebounce.stopped[n]->done) {keepBuffers(*rebounce.stopped[n]);rebounce.stopped.erase(rebounce.stopped.begin()+n);}
        else ++n;
}
}

// What a level's solve reads besides the bakes' buffers, which the bake cache
// hashes itself (bake_cache.cpp): its own setup from the settings, the sun,
// the weather and the textures.
static uint64_t solveInputs(const BounceSolve &solve) {
    uint64_t h=bakeHash(nullptr,0,0x626F756E6365ull);
    auto add=[&](const void *data,size_t bytes) {h=bakeHash(data,bytes,h);};
    int flags[]={solve.sun,solve.lights,solve.whole,solve.lobes,solve.fast,solve.adapts,solve.skyDome,(int)solve.kind};
    float values[]={solve.day,solve.skyGlow,solve.skyShade};
    add(flags,sizeof flags);add(values,sizeof values);
    for(const auto *rgb:{&solve.sunEnergy,&solve.lies,&solve.skyRadiance,&solve.skyEnergy})add(rgb->data(),sizeof(float)*3);
    add(solve.flatAlbedo.data(),solve.flatAlbedo.size()*sizeof(solve.flatAlbedo[0]));
    add(solve.sideAlbedo.data(),solve.sideAlbedo.size()*sizeof(solve.sideAlbedo[0]));
    add(solve.level.data(),solve.level.size()*sizeof(float));
    add(solve.outside.data(),solve.outside.size());
    return h;
}
// How much of each texel's bounce flickers with the lamps (whole-level
// lighting). A texel's lamp light steps with the flicker group that gives
// most of it (bake.glsl's bakeFlicker; the direction texel's alpha,
// group*16+share), and its bounce used to take that same share. That is
// wrong both ways: under a flickering lamp the whole bounce went out with
// it, the steady lamps' part too, so the lamp took light away that was never
// its own; and a ceiling the lamp reaches only by bounce stood still.
// Bounce adds up, so the share is worked out, not guessed: the level's
// bounce is solved once more with only the flickering part of the lamp
// light, and each texel keeps that solve's bounce over its whole bounce, in
// the group that gives most of the flickering light in its room. It goes to
// the alpha of the texel's basis texel, which nothing else uses. With the
// lamps off the bounce is then what the steady lights give. At load only
// (it doubles the bounce bake where lamps flicker): a lamp shot out later
// leaves its share, which matters only while others of its sector still
// flicker (updateLampGroups).
// Classic leaves that alpha 0, which the shader reads as "as the lamp light",
// what it always did.
static bool bounceFlicker(const std::shared_ptr<HitStore> &hits) {
    if(settings.lightModel!=1||!bakeDirected||lampGroupCount<=0||flatDirectionCells.empty()||wallDirectionCells.empty()||seamCells.empty())return false;
    const size_t layer=(size_t)mapWidth*mapHeight;
    const uint8_t steady=0x0F;   // no group: the bounce does not flicker
    auto luma=[](const uint8_t *rgb) {return 77*rgb[0]+150*rgb[1]+29*rgb[2];};
    BounceSolve solve;
    solve.rects.push_back({mapOrigin[0]-1,mapOrigin[1]-1,mapOrigin[0]+mapWidth*mapCell+1,mapOrigin[1]+mapHeight*mapCell+1});
    solve.prepare();
    solve.sun=false;solve.sunEnergy={};solve.lobes=false;solve.skyGlow=0;solve.skyDome=false;
    if(bakedLevel.size()==solve.level.size())solve.level=bakedLevel;
    // Its lamp light: only what flickers. Per room, the flickering light by group.
    Rooms_Build();
    int rooms=Rooms_Count();
    std::vector<std::array<double,flickerGroups>> byGroup(rooms);
    for(auto &g:byGroup)g.fill(0);
    bool any=false;
    for(size_t i=0;i<layer;++i)for(int ceiling=0;ceiling<2;++ceiling) {
        uint8_t mark=flatDirectionCells[4*(ceiling*layer+i)+3];
        uint8_t *rgb=&solve.flat[4*((size_t)(ceiling?ceilingLight:floorLight)*layer+i)];
        float share=mark>=16?(mark&15)/15.0f:0;
        for(int c=0;c<3;++c)rgb[c]=(uint8_t)std::lround(rgb[c]*share);
        if(share<=0)continue;
        any=true;
        int s=seamCells[i].own;
        if(s!=noSector)if(int r=Rooms_Of(s);r>=0)byGroup[r][mark>>4]+=luma(rgb);
    }
    for(int index:bakeOrder) {
        const BakeStrip &strip=bakeStrips[index];
        for(int row=0;row<strip.height;++row)for(int column=0;column<strip.width;++column) {
            size_t at=4*((size_t)(strip.y+row)*atlasStride+strip.x+column);
            uint8_t mark=wallDirectionCells[at+3];
            float share=mark>=16?(mark&15)/15.0f:0;
            for(int c=0;c<3;++c)solve.wall[at+c]=(uint8_t)std::lround(solve.wall[at+c]*share);
            any|=share>0;
        }
    }
    if(!any)return false;
    solve.hits=hits;   // the level's solve shot these rays already
    solve.run([](int count,const std::function<void(int)> &work){parallelFor(count,work);});
    std::vector<uint8_t> group(rooms,0);
    for(int r=0;r<rooms;++r)for(int g=1;g<flickerGroups;++g)if(byGroup[r][g]>(group[r]?byGroup[r][group[r]]:0))group[r]=(uint8_t)g;
    auto markOf=[&](const uint8_t *flickers,const uint8_t *all,int sector)->uint8_t {
        int r=sector>=0&&sector<numsectors?Rooms_Of(sector):-1,whole=luma(all);
        if(r<0||!group[r]||whole<=0)return steady;
        long share=std::lround(std::clamp(luma(flickers)/(double)whole,0.0,1.0)*15);
        return share>0?(uint8_t)(group[r]*16+share):steady;
    };
    for(size_t i=0;i<layer;++i) {
        int s=seamCells[i].own;if(s==noSector)continue;
        for(int ceiling=0;ceiling<2;++ceiling) {
            FlatLayer of=ceiling?ceilingBounce:floorBounce;
            flatDirectionCells[4*((2+ceiling)*layer+i)+3]=markOf(&solve.flat[4*((size_t)of*layer+i)],flatCell(of,i),s);
        }
    }
    for(int index:bakeOrder) {
        const BakeStrip &strip=bakeStrips[index];
        for(int row=0;row<strip.height;++row)for(int column=0;column<strip.width;++column) {
            size_t at=4*((size_t)(strip.y+row)*atlasStride+atlasWidth+strip.x+column);
            wallDirectionCells[at+3]=markOf(&solve.wall[at],&wallBakeCells[at],strip.sector);
        }
    }
    return true;
}
void bakeBounce() {
    loadingStage(LoadStage::Bounce);
    finishRelight();
    dropRebounce();
    bounceKey=bounceInputs();
    if(flatLightCells.empty()||wallBakeCells.empty())return;
    uint64_t start=SDL_GetTicksNS();
    BounceSolve solve;
    solve.prepare();
    bakedLevel=solve.level;bakedSkyGlow=solve.skyGlow;
    // The solve and the flicker share, from an earlier visit if the bake cache has them (bake_cache.cpp).
    int cached=solve.lobes?1:0;
    // With what decides whether the flicker share is solved at all (bounceFlicker).
    // And the dev tools' switches that change the solve (DERP_BOUNCE8, DERP_BOUNCEHEADROOM,
    // DERP_BOUNCENOTAIL, DERP_BOUNCEPASSES), so an A/B
    // with the cache on does not restore the other side's bake.
    int devSwitches=0;
#ifdef DOOM_DEVTOOLS
    devSwitches=(bounceEightBits()?1:0)|(getenv("DERP_BOUNCEHEADROOM")?2:0)|(bounceNoTail()?4:0)|(bounceWholePasses()<<8);
#endif
    int flickerGate[]={bakeDirected?1:0,lampGroupCount,(int)settings.lightModel,devSwitches};
    // The level's carry (bounce_carry.h) is kept with it: the cache restores it in place.
    {
        std::lock_guard<std::mutex> hold(levelCarryLock);
        if(solve.whole&&!solve.fast&&!bounceEightBits()) {levelCarry.assign(levelCarrySize(),0);cached|=4;}
        else levelCarry.clear();
    }
    bool restored=bakeCacheRestore(LoadStage::Bounce,BAKE_BUILT,bakeHash(flickerGate,sizeof flickerGate,solveInputs(solve)),&cached);
    solve.keepHits=true;
    buildStripAtlas();
    if(!restored) {
        solve.run([](int count,const std::function<void(int)> &work){parallelFor(count,work);});
        std::lock_guard<std::mutex> hold(levelCarryLock);
        if(solve.carry.on()&&solve.carry.readable().size()==levelCarry.size())levelCarry.swap(solve.carry.buffers[solve.carry.reading]);
        else levelCarry.clear();
    }
    solveRoomFill();
    uploadFlatLight();
    uploadWallBake();
    const uint64_t flickerStart=SDL_GetTicksNS();
    bool flickers=restored?(cached&2)!=0:bounceFlicker(solve.hits);
    if(!restored&&flickers)fprintf(stderr,"3D bounce: the flicker share solved in %.0f ms.\n",(SDL_GetTicksNS()-flickerStart)/1e6);
    if(solve.hits)fprintf(stderr,"3D bounce: %.0f MB of rays kept over the passes%s.\n",(size_t)solve.hits->used/1048576.0,solve.hits->full?" (full: the rest were traced each pass)":"");
    solve.hits.reset();
#ifdef DOOM_DEVTOOLS
    // DERP_BOUNCESAT=1: how much of the lamps' light and of the bounce stands at the 8 bits' top (2.0),
    // and how much of the lamps' light lay past fillHeadroom's knee in an unlit room (0.625): what
    // the bounce lost to each (PLAN-bounce-energy.md step 2).
    if(getenv("DERP_BOUNCESAT")) {
        const size_t layer=(size_t)mapWidth*mapHeight;
        struct Tally { size_t lit=0,top=0,knee=0; } lamps,bounce;
        auto count=[](Tally &t,const uint8_t *rgb) {
            if(!(rgb[0]|rgb[1]|rgb[2]))return;
            ++t.lit;t.top+=std::max({rgb[0],rgb[1],rgb[2]})==255;
            t.knee+=(0.299f*rgb[0]+0.587f*rgb[1]+0.114f*rgb[2])/255.0f*2>0.625f;
        };
        for(size_t i=0;i<layer;++i)if(seamCells[i].own!=noSector)for(int ceiling=0;ceiling<2;++ceiling) {
            count(lamps,&flatLightCells[4*((size_t)(ceiling?ceilingLight:floorLight)*layer+i)]);
            count(bounce,&flatLightCells[4*((size_t)(ceiling?ceilingBounce:floorBounce)*layer+i)]);
        }
        for(int index:bakeOrder) {
            const BakeStrip &strip=bakeStrips[index];
            for(int row=0;row<strip.height;++row)for(int column=0;column<strip.width;++column) {
                size_t at=4*((size_t)(strip.y+row)*atlasStride+strip.x+column);
                count(lamps,&wallBakeCells[at]);count(bounce,&wallBakeCells[at+4*(size_t)atlasWidth]);
            }
        }
        auto pc=[](size_t n,size_t of) {return of?100.0*n/of:0.0;};
        fprintf(stderr,"3D bounce saturation: lamp light lit %zu, at 2.0 %.2f%%, past the knee %.2f%%; bounce lit %zu, at 2.0 %.2f%%, past 0.625 %.2f%%.\n",
                lamps.lit,pc(lamps.top,lamps.lit),pc(lamps.knee,lamps.lit),bounce.lit,pc(bounce.top,bounce.lit),pc(bounce.knee,bounce.lit));
    }
#endif
    if(!restored)bakeCacheKeep(LoadStage::Bounce,(solve.lobes?1:0)|(flickers?2:0)|(levelCarry.empty()?0:4));
    if(solve.lobes||flickers)uploadDirections();
    // The solves in play (reBounceTick) copy the light into these: made now, as
    // load time is free, so the first one after a door faults in no atlas copy.
    if(settings.lightModel==1&&settings.bounce) {
        spare.flat.assign(flatLightCells.size(),0);spare.wall.assign(wallBakeCells.size(),0);
        if(solve.lobes) {spare.flatDirection.assign(flatDirectionCells.size(),0);spare.wallDirection.assign(wallDirectionCells.size(),0);}
        else {spare.flatDirection={};spare.wallDirection={};}
    } else spare={};
    fprintf(stderr,"3D bounce: %s%s, baked in %.0f ms.\n",solve.sun?"sun":"no sun",solve.lights?" and lights":"",(SDL_GetTicksNS()-start)/1e6);
}
// The light round here changed (relight.cpp): its bounce is to be solved again.
void reBounceMark(float x0,float y0,float x1,float y1) {
    dirtyTic=leveltime;
    for(const auto &r:dirty)if(x0>=r[0]&&y0>=r[1]&&x1<=r[2]&&y1<=r[3])return;
    dirty.push_back({x0,y0,x1,y1});
    if(dirty.size()<=16)return;
    std::array<float,4> all=dirty[0];
    for(const auto &r:dirty) {all[0]=std::min(all[0],r[0]);all[1]=std::min(all[1],r[1]);all[2]=std::max(all[2],r[2]);all[3]=std::max(all[3],r[3]);}
    dirty.assign(1,all);
}
// Each frame (relightMovingSectors): puts a finished solve in and starts the
// next once things have settled. busy: a re-light is running or waiting.
// A lamp went out: the bounce it fed goes with it at once, roughly. Bounce is
// the light of what lies round a surface, so each texel's bounce keeps the
// share of lamp light that the floors and ceilings within dimRound of it
// still get (before: litCells() as the lamp shone). The solve that follows
// (reBounceTick) puts the exact bounce in its place round the lamp. Without
// this the room stood lit by a dead lamp's bounce for the second the solve
// takes. Scaling a whole room by one share left its far end too dark for
// good: the solve only reaches round the lamp.
constexpr float dimRound=192;
std::vector<uint16_t> litCells() {
    const size_t layer=(size_t)mapWidth*mapHeight;
    std::vector<uint16_t> lit(flatLightCells.size()>=4*4*layer?layer:0);
    for(size_t i=0;i<lit.size();++i) {
        const uint8_t *f=flatCell(floorLight,i),*c=flatCell(ceilingLight,i);
        lit[i]=(uint16_t)((77*(f[0]+c[0])+150*(f[1]+c[1])+29*(f[2]+c[2]))>>8);
    }
    return lit;
}
// The cells that may dim round bounds (x0..x1, y0..y1: dims) and the cells their windows read (reads).
static void dimWindows(const int bounds[4],int dims[4],int reads[4]) {
    const int reach=std::max(1,(int)std::lround(dimRound/mapCell));
    dims[0]=std::max(bounds[0]-reach,0);dims[1]=std::max(bounds[1]-reach,0);dims[2]=std::min(bounds[2]+reach,mapWidth-1);dims[3]=std::min(bounds[3]+reach,mapHeight-1);
    reads[0]=std::max(dims[0]-reach,0);reads[1]=std::max(dims[1]-reach,0);reads[2]=std::min(dims[2]+reach,mapWidth-1);reads[3]=std::min(dims[3]+reach,mapHeight-1);
}
// Only what dimBounce will read of it; the rest of lit is left as it was.
void litCellsAround(const int bounds[4],std::vector<uint16_t> &lit) {
    const size_t layer=(size_t)mapWidth*mapHeight;
    lit.resize(flatLightCells.size()>=4*4*layer?layer:0);
    if(lit.empty()||bounds[2]<bounds[0])return;
    int dims[4],reads[4];
    dimWindows(bounds,dims,reads);
    for(int y=reads[1];y<=reads[3];++y)for(int x=reads[0];x<=reads[2];++x) {
        size_t i=(size_t)y*mapWidth+x;
        const uint8_t *f=flatCell(floorLight,i),*c=flatCell(ceilingLight,i);
        lit[i]=(uint16_t)((77*(f[0]+c[0])+150*(f[1]+c[1])+29*(f[2]+c[2]))>>8);
    }
}
void dimBounce(const std::vector<uint16_t> &before,const int bounds[4]) {
    const size_t layer=(size_t)mapWidth*mapHeight;
    if(settings.lightModel!=1||before.size()!=layer||wallBakeCells.empty()||!flatLightTexture||!wallBakeTexture||bounds[2]<bounds[0])return;
    static std::vector<uint16_t> after;   // frame thread only
    litCellsAround(bounds,after);
    if(after.size()!=layer)return;
    const int reach=std::max(1,(int)std::lround(dimRound/mapCell));
    // The cells that may dim, and the cells their windows read.
    int dims[4],reads[4];
    dimWindows(bounds,dims,reads);
    int x0=dims[0],y0=dims[1],x1=dims[2],y1=dims[3];
    int gx0=reads[0],gy0=reads[1],gx1=reads[2],gy1=reads[3];
    int gw=gx1-gx0+1,gh=gy1-gy0+1;
    std::vector<uint32_t> was((size_t)(gw+1)*(gh+1),0),is(was.size(),0);   // summed areas
    for(int y=0;y<gh;++y)for(int x=0;x<gw;++x) {
        size_t i=(size_t)(gy0+y)*mapWidth+gx0+x,at=(size_t)(y+1)*(gw+1)+x+1;
        was[at]=before[i]+was[at-1]+was[at-(gw+1)]-was[at-(gw+1)-1];
        is[at]=after[i]+is[at-1]+is[at-(gw+1)]-is[at-(gw+1)-1];
    }
    auto keepAt=[&](int cx,int cy) {
        int ax=std::max(cx-reach,gx0)-gx0,ay=std::max(cy-reach,gy0)-gy0,bx=std::min(cx+reach,gx1)-gx0+1,by=std::min(cy+reach,gy1)-gy0+1;
        auto sum=[&](const std::vector<uint32_t> &t) {
            return (double)t[(size_t)by*(gw+1)+bx]-t[(size_t)ay*(gw+1)+bx]-t[(size_t)by*(gw+1)+ax]+t[(size_t)ay*(gw+1)+ax];
        };
        double b=sum(was);
        return b>0?(float)std::clamp(sum(is)/b,0.0,1.0):1.0f;
    };
    for(int y=y0;y<=y1;++y)for(int x=x0;x<=x1;++x) {
        size_t i=(size_t)y*mapWidth+x;
        int own=seamCells[i].own;
        if(own==noSector||sectors[own].ceilingpic==skyflatnum||Rooms_Outside(own))continue;   // outside the bounce is the sky's
        float keep=keepAt(x,y);
        if(keep>=0.995f)continue;
        for(FlatLayer of:{floorBounce,ceilingBounce}) {
            uint8_t *cell=flatCell(of,i);
            for(int c=0;c<3;++c)cell[c]=(uint8_t)std::lround(cell[c]*keep);
        }
    }
    queueLightUpload(flatLightTexture,{{x0,y0+floorBounce*mapHeight,x1-x0+1,y1-y0+1},{x0,y0+ceilingBounce*mapHeight,x1-x0+1,y1-y0+1}},flatLightCells,mapWidth*4);
    float wx0=mapOrigin[0]+x0*mapCell,wy0=mapOrigin[1]+y0*mapCell,wx1=mapOrigin[0]+(x1+1)*mapCell,wy1=mapOrigin[1]+(y1+1)*mapCell;
    std::vector<std::array<int,4>> walls;
    static std::vector<int> near;
    stripsNear({{wx0,wy0,wx1,wy1}},near);
    for(int index:near) {
        const BakeStrip &strip=bakeStrips[index];
        if(strip.sector<0||strip.sector>=numsectors||sectors[strip.sector].ceilingpic==skyflatnum||Rooms_Outside(strip.sector))continue;
        forStripTexels(strip,[&](size_t i,float x,float y,float) {
            // By the cell in front of the wall.
            int cx=(int)std::floor((x+strip.dy*8-mapOrigin[0])/mapCell),cy=(int)std::floor((y-strip.dx*8-mapOrigin[1])/mapCell);
            if(cx<x0||cy<y0||cx>x1||cy>y1)return;
            float keep=keepAt(cx,cy);
            if(keep>=0.995f)return;
            uint8_t *cell=&wallBakeCells[4*(i+atlasWidth)];
            for(int c=0;c<3;++c)cell[c]=(uint8_t)std::lround(cell[c]*keep);
        });
        walls.push_back({strip.x+atlasWidth,strip.y,strip.width,strip.height});
    }
    queueLightUpload(wallBakeTexture,walls,wallBakeCells,atlasStride*4);
}
void reBounceTick(bool busy) {
    reapStopped();
    if(rebounce.running&&rebounce.solve->done) {
        rebounce.running=false;
        rebounce.solve->commit();
#ifdef DOOM_DEVTOOLS
        // DERP_REBOUNCEDUMP=<file>: the floors' and walls' light as this solve left it, to hold one side's against the other's.
        if(const char *dump=getenv("DERP_REBOUNCEDUMP"))if(FILE *f=fopen(dump,"wb")) {
            fwrite(flatLightCells.data(),1,flatLightCells.size(),f);fwrite(wallBakeCells.data(),1,wallBakeCells.size(),f);fclose(f);
        }
#endif
#ifdef DOOM_DEVTOOLS
        if(getenv("DERP_RELIGHT")||getenv("DERP_REBOUNCE"))
            fprintf(stderr,"3D bounce: %zu places solved again in %.0f ms at tic %d.\n",rebounce.solve->rects.size(),(SDL_GetTicksNS()-rebounce.start)/1e6,leveltime);
#endif
        keepBuffers(*rebounce.solve);
        rebounce.solve.reset();
    }
    if(rebounce.running||dirty.empty()||busy)return;
    if(settings.lightModel!=1||!settings.bounce||bounceKey<0||bounceKey!=bounceInputs()||flatLightCells.empty()||wallBakeCells.empty()) {dirty.clear();return;}
    if(std::abs(leveltime-dirtyTic)<reBounceWait)return;
    rebounce.solve=std::make_shared<BounceSolve>();
    rebounce.solve->rects.swap(dirty);
    rebounce.solve->prepare();
    if(bakedLevel.size()==rebounce.solve->level.size()) {rebounce.solve->level=bakedLevel;rebounce.solve->skyGlow=bakedSkyGlow;}
    rebounce.start=SDL_GetTicksNS();
    rebounce.running=true;
    lightWorkerPost([solve=rebounce.solve] {
        solve->run(relightFor);
        solve->done=true;
    });
}
// A re-light is about to start: a solve in flight would read light that is
// about to change. Its places wait again; it winds down on the worker, ahead
// of the re-light, and the frame does not wait for it.
void cancelRebounce() {stopSolve(true);}
bool reBounceBusy() {return rebounce.running||!dirty.empty();}
// A prepared shot's solve (relight.cpp): the re-bounce's way, on the light the caller's
// lightTarget gives prepare, into it again at commit.
namespace {std::shared_ptr<BounceSolve> preparedSolve;const std::vector<int> noStrips;}
bool preparedBounceStart(const std::vector<std::array<float,4>> &rects) {
    preparedBounceStop();
    if(rects.empty()||settings.lightModel!=1||!settings.bounce||bounceKey<0||bounceKey!=bounceInputs()||flatLightCells.empty()||wallBakeCells.empty())return false;
    preparedSolve=std::make_shared<BounceSolve>();
    preparedSolve->rects=rects;
    preparedSolve->deferCopy=true;
    preparedSolve->prepare();
    if(bakedLevel.size()==preparedSolve->level.size()) {preparedSolve->level=bakedLevel;preparedSolve->skyGlow=bakedSkyGlow;}
    lightWorkerPost([solve=preparedSolve,target=lightTarget] {
        lightTarget=target;
        solve->copyLight();
        lightTarget=nullptr;
        backgroundLight=true;
        solve->run(relightFor);
        backgroundLight=false;
        solve->done=true;
    });
    return true;
}
bool preparedBounceDone() {return !preparedSolve||preparedSolve->done;}
const std::vector<int> &preparedBounceStrips() {return preparedSolve?preparedSolve->strips:noStrips;}
std::function<void()> preparedBounceCommitter() {
    if(!preparedSolve||!preparedSolve->done)return {};
    return [solve=std::move(preparedSolve)] {
        if(!solve->cancel)solve->commit();
        keepBuffers(*solve,preparedSpare);
    };
}
void preparedBounceStop() {
    if(!preparedSolve)return;
    preparedSolve->cancel=true;
    rebounce.stopped.push_back(std::move(preparedSolve));   // reapStopped takes its buffers back once done
}
// Before a full bake, and when the level goes: here the solves are waited
// for, as what they read is about to be rebuilt.
void dropRebounce() {
    stopSolve(false);dirty.clear();
    lightWorkerWait();
    reapStopped();
    rebounce.stopped.clear();
}
} // namespace doom3d
