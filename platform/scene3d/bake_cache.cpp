/* The level's light bakes kept on disk (PLAN-performance.md, stage 6: load
   time). The bakes are most of a level's load (the bounce alone 8 to 14 s on
   the dev PC), and what they make depends only on the map, its artwork and the
   settings, so a second visit loads it from bake-cache/ in the save folder
   instead of tracing it again.

   Only what each bake's expensive loops make is kept. The rest of a bake (its
   lists of lamps, pools and windows, the grid of which lamps reach where, the
   flicker groups, the rooms' drop and fill, keys, serials, strengths and the
   uploads to the GPU) is cheap and runs as it always did around the restored
   loops: it lands in the state the traced loops would have left and goes on
   from there by the same code. Keeping that bookkeeping too would mean a second
   copy of every bake's state to keep in step by hand, which is how light goes
   subtly wrong.

   What the loops make, per stage (the cells and texels each loop writes, in
   one fixed order), and what the bake around them recomputes:
   - Sun (bakeSun): contactCells blue on floor cells (not sky floors) after
     the median pass; wallBakeCells alpha, left half. Around it: sunBaked,
     sunLevel, sunShafts, beamAir, the contact texture, the atlas upload.
   - Lights (bakeLights: lightCell, lightStrip): floorLight rgb on floor
     cells, ceilingLight rgb on ceiling cells, wallBakeCells rgb on the left
     half; with direction maps (bake-only lights, switching lamps or bumps)
     the first two layers of flatDirectionCells and the left half of
     wallDirectionCells, all four bytes; with bumps flatLampLobes and
     wallLampLobes. Around it: lightBakeKey, lightBakeSerial, bakeTailShare,
     bakeSources with their keys, tiles and groups, bakeAreas, bakeGrid,
     firstWindowSource and the window doors, the lamp groups, bakeDirected,
     the lamp basis (writeLampBasis), lampDrop, the room fill, the uploads.
   - Grid (bakeStaticGrid): every staticGrid cell. Around it: staticSerial,
     staticWidth, staticHeight.
   - Ambient (bakeAmbient): floorBounce and ceilingBounce alpha on every cell
     with a sector, wallBakeCells alpha on the right half. Around it:
     ambientKey, skyLevel, skyTint, the uploads.
   - Bounce (bakeBounce: BounceSolve::run, bounceFlicker): floorBounce and
     ceilingBounce rgb, wallBakeCells rgb on the right half; with bumps the
     basis layers of flatDirectionCells and the right half of
     wallDirectionCells, rgb; where lamps flicker, their alpha (the flicker
     share) and the flag that bounceFlicker found any. Around it: bounceKey,
     bakedLevel, bakedSkyGlow, the room fill, the spare solve buffers, the
     uploads (uploadDirections by the flag). bounceStrength is a constant.
   - Flow (bakeFlow): seamCells' flow on every cell, contactCells alpha on
     every cell. Around it: flowKey, the seam and contact textures.

   The file a level loads is named by a key over everything the bakes depend
   on: this file's build and the format version, the game, the map as it
   stands (vertexes, lines, sides with their textures as animated now, sectors
   with heights, light, flats, specials and tags, the BSP, the things that
   light or occlude, bakeMap and the bake layout), the artwork (the WADs' lump
   directory, their files' sizes and times, PLAYPAL, COLORMAP, the texture
   definitions and the map's flats), the settings (all of Settings but input,
   display, sound, gore and the drawing-only effects) and what is worked out from them before the bakes
   (the mood, the sun's place, the weather and wind, DERPINFO). On top of the
   key each stage keeps a hash of what its loops actually read when it was
   baked, the buffers earlier stages left included, and of its calling file's
   build time: a stage whose inputs differ is baked afresh and replaced, so a
   gap in the key costs a re-bake, not wrong light. The cache is used only
   while the level's light is as it loaded (no door has moved, no lamp has
   been shot: liveLightSerial), and written once a frame's bakes are done,
   before play changes anything.

   DERP_NOBAKECACHE=1: neither read nor written. DERP_BAKECACHECHECK=1: every
   stage bakes afresh as well, compares what it made with the cache item by
   item, and goes on with the fresh bake; a stage the cache lacks (or has from
   other inputs) is written, one it has is kept, to check again. Where the
   inputs differ it lists each named part of them (stageInputs) with both
   sides' hashes, so a pair of runs shows which part moved.

   Everything a bake records must come out the same on every run of the same
   map: a flicker group taken from a thing's address (it was, lights.cpp's
   flickerGroup) changed the direction cells' group byte from run to run. */
#include "internal.h"
#include "gpu_trace.h"
#include "bounce_carry.h"
#include "../rooms.h"
#include <cstddef>
#include <cstdlib>
#include <type_traits>
#include <sys/types.h>
#include <sys/stat.h>

namespace doom3d {
// A fast 64-bit hash for telling inputs and files apart (not against attack):
// 8 bytes a step, then a finalizer.
uint64_t bakeHash(const void *data,size_t bytes,uint64_t seed) {
    constexpr uint64_t k1=0x9E3779B185EBCA87ull,k2=0xC2B2AE3D27D4EB4Full;
    const uint8_t *p=static_cast<const uint8_t*>(data);
    uint64_t h=seed^(bytes*k1);
    size_t words=bytes/8;
    for(size_t i=0;i<words;++i) {
        uint64_t w;std::memcpy(&w,p+8*i,8);
        h^=w*k2;h=(h<<31|h>>33)*k1;
    }
    if(bytes%8) {
        uint64_t tail=0;std::memcpy(&tail,p+8*words,bytes%8);
        h^=tail*k2;h=(h<<31|h>>33)*k1;
    }
    h^=h>>33;h*=0xFF51AFD7ED558CCDull;h^=h>>33;h*=0xC4CEB9FE1A85EC53ull;h^=h>>33;
    return h;
}

namespace {
constexpr uint32_t formatVersion=3;   // 2: sections keep their inputs' parts; 3: files named by level and settings
const char magic[8]={'D','E','R','P','B','A','K','E'};
// This file's build: a rebuilt exe never reads what an older one wrote. Each
// stage adds its own bake file's build time to its inputs (BAKE_BUILT).
const char buildStamp[]=__DATE__ " " __TIME__;
const char folder[]="bake-cache";
constexpr int maxFiles=64;
constexpr uint64_t maxBytes=1ull<<30;
constexpr int stageCount=(int)LoadStage::Count;
constexpr int maxItems=12;
// Stage flags: what a stage made, so what its items are. Lights' are the
// cache's own reading of the buffers; the bounce's come from bakeBounce.
constexpr int lightDirections=1<<8,lightLobes=1<<9,bounceBasis=1,bounceFlickers=2,bounceCarry=4;   // bounceCarry: levelCarry (bounce_carry.h)

static_assert(sizeof(std::array<uint16_t,3>)==6,"lamp lobes are kept as three 16-bit values");
static_assert(sizeof(SeamCell)==8&&sizeof(BakeStrip)==52,"hashed as plain bytes");
static_assert(sizeof(StaticSample)==27*4&&sizeof(StaticCell)==228,"cellFields lists every field of a grid cell");
static_assert(std::is_trivially_copyable<Settings>::value,"Settings is hashed as bytes");

uint64_t buildHash() {return bakeHash(buildStamp,sizeof(buildStamp)-1,formatVersion);}
bool environment(const char *name) {const char *v=getenv(name);return v&&*v&&strcmp(v,"0")!=0;}
bool cacheOn() {static const bool off=environment("DERP_NOBAKECACHE");return !off;}
bool checkOn() {static const bool check=environment("DERP_BAKECACHECHECK");return check;}

const char *stageName(LoadStage stage) {
    switch(stage) {
    case LoadStage::Sun:return "sun";
    case LoadStage::Lights:return "lights";
    case LoadStage::Grid:return "light grid";
    case LoadStage::Bounce:return "bounce";
    case LoadStage::Ambient:return "sky light";
    case LoadStage::Flow:return "light flow";
    default:return "?";
    }
}

struct Hasher {
    uint64_t h=0x62616B6563616368ull;
    void bytes(const void *p,size_t n) {h=bakeHash(n?p:nullptr,n,h);}
    template<class T> void add(const T &v) {static_assert(std::is_trivially_copyable<T>::value,"plain values only");bytes(&v,sizeof(T));}
    void text(const char *s) {if(!s)s="";bytes(s,strlen(s));}
    template<class T> void all(const std::vector<T> &v) {add(v.size());if(!v.empty())bytes(v.data(),v.size()*sizeof(T));}
};

// ---- What the bakes read ----
// bakeMap as the rays walk it: sectors, lines and their middle-texture masks,
// and the decorations standing in it.
void hashBakeMap(Hasher &h) {
    h.add(bakeMap.sectors.size());
    for(const BakeSector &s:bakeMap.sectors) {h.add(s.floor);h.add(s.ceiling);h.add(s.sky);h.all(s.things);}
    h.add(bakeMap.lines.size());
    for(const BakeLine &l:bakeMap.lines) {
        const BakeMask &m=l.mask;
        float v[]={l.ax,l.ay,l.bx,l.by,(float)l.front,(float)l.back,(float)m.width,(float)m.height,m.u,m.direction,m.anchor,m.bottom,m.top};
        h.add(v);h.add(m.pixels!=nullptr);
        // What the rays read of it: index and coverage, a texel each.
        if(m.pixels&&m.width>0&&m.height>0)h.bytes(m.pixels,(size_t)m.width*m.height*2);
    }
    h.add(bakeMap.things.size());
    for(const BakeThing &t:bakeMap.things) {float v[]={t.x,t.y,t.top,t.left,(float)t.width,(float)t.height};h.add(v);}
}
// The map grid and the wall atlas layout, and each cell's sector.
void hashLayout(Hasher &h) {
    float grid[]={mapOrigin[0],mapOrigin[1],mapCell};
    h.add(grid);h.add(mapWidth);h.add(mapHeight);h.add(atlasHeight);
    h.all(bakeStrips);h.all(bakeOrder);
    std::vector<uint16_t> own(seamCells.size());
    for(size_t i=0;i<own.size();++i)own[i]=seamCells[i].own;
    h.all(own);
    std::vector<uint8_t> sky((size_t)numsectors*2);
    for(int i=0;i<numsectors;++i) {sky[2*i]=sectors[i].floorpic==skyflatnum;sky[2*i+1]=sectors[i].ceilingpic==skyflatnum;}
    h.all(sky);
}
// A stage's inputs come in named parts, each hashed on its own, so the check
// can say which part moved between two runs (compareStage). The parts' names
// are fixed per stage; the file keeps only their hashes, in this order.
using Parts=std::vector<std::pair<const char*,uint64_t>>;
template<class Fill> void part(Parts &parts,const char *name,Fill fill) {Hasher h;fill(h);parts.push_back({name,h.h});}
// The baked lights as bakeLights listed them before its loops.
void hashLights(Parts &parts) {
    part(parts,"light sources",[](Hasher &h) {
        h.add(bakeSources.size());
        for(const BakeLight &l:bakeSources) {
            float v[]={l.x,l.y,l.z,l.radius,l.strength,l.directionality,l.color[0],l.color[1],l.color[2],l.extent,l.nx,l.ny,l.beam,l.tail,(float)l.tileCount,l.post};
            h.add(v);
            if(l.tiles&&l.tileCount>0)h.bytes(l.tiles,sizeof(float)*3*(size_t)l.tileCount);
        }
    });
    part(parts,"source keys",[](Hasher &h) {h.all(bakeSourceKeys);});
    part(parts,"flicker groups",[](Hasher &h) {h.all(sourceGroups);h.add(lampGroupCount);});
    part(parts,"window lamps",[](Hasher &h) {
        h.add(firstWindowSource);
        for(int n=firstWindowSource;n<(int)bakeSources.size();++n)h.add(shutWindowLamp(bakeMap,n));
    });
    part(parts,"pools",[](Hasher &h) {
        h.add(bakeAreas.size());
        for(const BakeArea &a:bakeAreas) {
            h.add(a.pieces.size());
            for(const auto &piece:a.pieces)h.all(piece);
            float v[]={a.z,a.clearance,(float)a.ceiling,a.radius,a.strength,a.directionality,a.color[0],a.color[1],a.color[2],a.low[0],a.low[1],a.high[0],a.high[1]};
            h.add(v);h.all(a.samples);
        }
    });
    part(parts,"lamp grid",[](Hasher &h) {h.add(bakeGrid.size());for(const auto &cell:bakeGrid)h.all(cell);});
    part(parts,"light shape",[](Hasher &h) {
        h.add(bakeTailShare);h.add(bakeDirected);
        h.add(wallDirectionCells.size());h.add(flatDirectionCells.size());h.add(wallLampLobes.size());h.add(flatLampLobes.size());
    });
}
void hashRooms(Parts &parts) {
    part(parts,"rooms",[](Hasher &h) {
        Rooms_Build();
        for(int s=0;s<numsectors;++s) {
            float top=0;int door=Rooms_Door(s,&top);
            int v[]={Rooms_Of(s),Rooms_Outside(s),door};
            h.add(v);h.add(door?top:0.0f);
        }
    });
}
// What a stage's loops read, as it stands just before them.
Parts stageInputs(LoadStage stage) {
    Parts parts;
    part(parts,"stage and model",[&](Hasher &h) {h.add((int)stage);h.add(settings.lightModel);});
    part(parts,"bake map",[](Hasher &h) {hashBakeMap(h);});
    part(parts,"layout",[](Hasher &h) {hashLayout(h);});
    switch(stage) {
    case LoadStage::Sun:part(parts,"sun direction",[](Hasher &h) {h.add(sunDirection);h.add(sunSpread);});break;   // spread: the cloud (sun.cpp)
    case LoadStage::Lights:hashLights(parts);break;
    case LoadStage::Grid:
        hashLights(parts);
        part(parts,"sky light setting",[](Hasher &h) {h.add(settings.skyLight);});
        part(parts,"flat light cells",[](Hasher &h) {h.all(flatLightCells);});
        part(parts,"wall light cells",[](Hasher &h) {h.all(wallBakeCells);});
        part(parts,"grid size",[](Hasher &h) {h.add(staticWidth);h.add(staticHeight);});
        break;
    case LoadStage::Ambient:hashRooms(parts);part(parts,"fast bakes",[](Hasher &h) {h.add(settings.fastBakes);});break;
    case LoadStage::Bounce:
        hashRooms(parts);
        part(parts,"flat light cells",[](Hasher &h) {h.all(flatLightCells);});
        part(parts,"wall light cells",[](Hasher &h) {h.all(wallBakeCells);});
        part(parts,"contact cells",[](Hasher &h) {h.all(contactCells);});
        part(parts,"flat direction cells",[](Hasher &h) {h.all(flatDirectionCells);});
        part(parts,"wall direction cells",[](Hasher &h) {h.all(wallDirectionCells);});
        part(parts,"lamp lobes",[](Hasher &h) {h.all(flatLampLobes);h.all(wallLampLobes);});
        part(parts,"ground",[](Hasher &h) {
            std::vector<uint8_t> ground((size_t)mapWidth*mapHeight);
            for(size_t i=0;i<ground.size();++i)ground[i]=groundBakeAt(i);
            h.all(ground);
        });
        break;
    case LoadStage::Flow:hashRooms(parts);part(parts,"flow inputs",[](Hasher &h) {h.add(flowInputs());});break;
    default:break;
    }
    return parts;
}

// ---- The key ----
// The map as it stands: the level's own data, the things that light or
// occlude, and what the renderer built of it.
uint64_t mapKey() {
    Hasher h;
    auto texture=[](int t)->int64_t {return t>0&&t<numtextures?texturetranslation[t]:t;};
    auto flat=[](int f)->int64_t {return f>=0&&f<numflats?flattranslation[f]:f;};
    h.add(numvertexes);
    for(int i=0;i<numvertexes;++i) {h.add(vertexes[i].x);h.add(vertexes[i].y);}
    h.add(numlines);
    for(int i=0;i<numlines;++i) {
        const line_t &l=lines[i];
        int64_t v[]={(int64_t)(l.v1-vertexes),(int64_t)(l.v2-vertexes),(int64_t)l.flags,(int64_t)l.special,(int64_t)l.tag,(int64_t)l.sidenum[0],(int64_t)l.sidenum[1]};
        h.add(v);
    }
    h.add(numsides);
    for(int i=0;i<numsides;++i) {
        const side_t &s=sides[i];
        int64_t v[]={(int64_t)s.textureoffset,(int64_t)s.rowoffset,(int64_t)s.toptexture,texture(s.toptexture),(int64_t)s.bottomtexture,texture(s.bottomtexture),
                     (int64_t)s.midtexture,texture(s.midtexture),s.sector?(int64_t)(s.sector-sectors):-1};
        h.add(v);
    }
    h.add(numsectors);
    for(int i=0;i<numsectors;++i) {
        const sector_t &s=sectors[i];
        int64_t v[]={(int64_t)s.floorheight,(int64_t)s.ceilingheight,(int64_t)s.floorpic,flat(s.floorpic),(int64_t)s.ceilingpic,flat(s.ceilingpic),
                     (int64_t)s.lightlevel,(int64_t)s.special,(int64_t)s.tag};
        h.add(v);
    }
    h.add(numsegs);
    for(int i=0;i<numsegs;++i) {
        const seg_t &s=segs[i];
        int64_t v[]={(int64_t)(s.v1-vertexes),(int64_t)(s.v2-vertexes),(int64_t)s.offset,s.linedef?(int64_t)(s.linedef-lines):-1,s.sidedef?(int64_t)(s.sidedef-sides):-1};
        h.add(v);
    }
    h.add(numsubsectors);
    for(int i=0;i<numsubsectors;++i) {
        const subsector_t &s=subsectors[i];
        int64_t v[]={(int64_t)s.numlines,(int64_t)s.firstline,s.sector?(int64_t)(s.sector-sectors):-1};
        h.add(v);
    }
    h.add(numnodes);
    for(int i=0;i<numnodes;++i) {
        const node_t &n=nodes[i];
        int64_t v[]={(int64_t)n.x,(int64_t)n.y,(int64_t)n.dx,(int64_t)n.dy,(int64_t)n.children[0],(int64_t)n.children[1]};
        h.add(v);
    }
    // Things the bakes see: lamps (decorationLight) and what syncBakeThings takes as occluders.
    for(int i=0;i<numsectors;++i)for(const mobj_t *t=sectors[i].thinglist;t;t=t->snext) {
        bool occluder=!t->player&&!(t->flags&(MF_COUNTKILL|MF_SPECIAL|MF_MISSILE|MF_NOBLOCKMAP|MF_SHOOTABLE|MF_CORPSE))&&(t->flags&(MF_SOLID|MF_SPAWNCEILING));
        if(!occluder&&!decorationLight(*t))continue;
        int64_t v[]={(int64_t)i,(int64_t)t->type,(int64_t)t->x,(int64_t)t->y,(int64_t)t->z,(int64_t)t->angle,(int64_t)t->flags,(int64_t)t->sprite,(int64_t)t->frame};
        h.add(v);
    }
    hashBakeMap(h);hashLayout(h);
    int sky[]={skytexture,skyflatnum,levelSky?1:0};
    h.add(sky);
    return h.h;
}
// The artwork. The bakes read the textures' and flats' colors and glow, the
// sprites of decorations and the sky: hashing every image would mean loading
// them all first, so this stands in. The lump directory of every WAD (names,
// sizes, places, which file) catches lumps added, dropped, moved or resized
// and a different set or order of WADs; the files' sizes and modification
// times catch an edit that keeps every size; the palette, colormap, texture
// definitions and the map's own flats are hashed whole.
uint64_t artKey() {
    Hasher h;
    h.add(numlumps);
    std::vector<int> files;
    for(int i=0;i<numlumps;++i) {
        const lumpinfo_t &l=lumpinfo[i];
        int file=(int)(std::find(files.begin(),files.end(),l.handle)-files.begin());
        if(file==(int)files.size())files.push_back(l.handle);
        h.bytes(l.name,8);h.add(l.position);h.add(l.size);h.add(file);
    }
    for(int handle:files) {
        if(handle<0)continue;
#ifdef _WIN32
        struct _stat64 st;
        if(_fstat64(handle,&st)!=0)continue;
#else
        struct stat st;
        if(fstat(handle,&st)!=0)continue;
#endif
        h.add((int64_t)st.st_size);h.add((int64_t)st.st_mtime);
    }
    static const char *const shared[]={"PLAYPAL","COLORMAP","TEXTURE1","TEXTURE2","PNAMES","ANIMATED","SWITCHES"};
    for(const char *name:shared) {
        char lumpName[9];snprintf(lumpName,sizeof lumpName,"%s",name);
        int lump=W_CheckNumForName(lumpName);
        if(lump<0)continue;
        h.add(lump);h.bytes(W_CacheLumpNum(lump,PU_CACHE),(size_t)W_LumpLength(lump));
    }
    std::vector<int> flats;
    for(int i=0;i<numsectors;++i)for(int pic:{(int)sectors[i].floorpic,(int)sectors[i].ceilingpic})
        if(pic>=0&&pic<numflats)flats.push_back(firstflat+flattranslation[pic]);
    std::sort(flats.begin(),flats.end());flats.erase(std::unique(flats.begin(),flats.end()),flats.end());
    for(int lump:flats) {h.add(lump);h.bytes(W_CacheLumpNum(lump,PU_CACHE),(size_t)W_LumpLength(lump));}
    return h.h;
}
// The settings, but those no bake reads (input, display, sound, gore, deaths
// and the drawing-only effects): changing those keeps the cache. Then what is worked out from the
// settings, the game and the map before the bakes run.
uint64_t settingsKey() {
    Settings s=settings;const Settings plain{};
#define BAKE_IGNORES(field) s.field=plain.field;
    BAKE_IGNORES(widescreen) BAKE_IGNORES(crosshair) BAKE_IGNORES(look) BAKE_IGNORES(fps) BAKE_IGNORES(presentation)
    BAKE_IGNORES(frameLimit) BAKE_IGNORES(hdr) BAKE_IGNORES(hdrScene) BAKE_IGNORES(hdrColors) BAKE_IGNORES(hdrBrightness) BAKE_IGNORES(hdrHighlights) BAKE_IGNORES(windowSize) BAKE_IGNORES(scale) BAKE_IGNORES(upscale) BAKE_IGNORES(hudSize) BAKE_IGNORES(hudWide) BAKE_IGNORES(invertLook)
    BAKE_IGNORES(weaponBob) BAKE_IGNORES(classicAim) BAKE_IGNORES(movement) BAKE_IGNORES(quickConfirm)
    BAKE_IGNORES(padLookX) BAKE_IGNORES(padLookY) BAKE_IGNORES(padDeadzone) BAKE_IGNORES(gyroSpeed) BAKE_IGNORES(padBoost)
    BAKE_IGNORES(padSouthpaw) BAKE_IGNORES(gyro) BAKE_IGNORES(rumble) BAKE_IGNORES(triggerRumble) BAKE_IGNORES(worldRumble)
    BAKE_IGNORES(msaa) BAKE_IGNORES(sharpSoftness) BAKE_IGNORES(paletteMips) BAKE_IGNORES(fov) BAKE_IGNORES(flashlightTintGain)
    BAKE_IGNORES(deaths) BAKE_IGNORES(hitReactions) BAKE_IGNORES(bloodLevel) BAKE_IGNORES(gore) BAKE_IGNORES(blood)
    BAKE_IGNORES(bloodSpray) BAKE_IGNORES(bloodShine) BAKE_IGNORES(deathMotion) BAKE_IGNORES(slowMotion) BAKE_IGNORES(wallHits)
    BAKE_IGNORES(wounds) BAKE_IGNORES(monsterBlood) BAKE_IGNORES(pickupFlash)BAKE_IGNORES(deathFalls) BAKE_IGNORES(deathRagdoll) BAKE_IGNORES(deathGuided) BAKE_IGNORES(ragdollPixels)
    BAKE_IGNORES(deathBlast) BAKE_IGNORES(deathFaceViewer) BAKE_IGNORES(deathDirector) BAKE_IGNORES(hitFlinch)
    BAKE_IGNORES(bodyMotionStrength) BAKE_IGNORES(flashlightShadows) BAKE_IGNORES(classicFlashlight) BAKE_IGNORES(heroLight) BAKE_IGNORES(phobosSpin)
    BAKE_IGNORES(phobosPlayer) BAKE_IGNORES(phobosEnemies) BAKE_IGNORES(phobosAir) BAKE_IGNORES(phobosRecoil)
    BAKE_IGNORES(musicDevice) BAKE_IGNORES(soundtrack) BAKE_IGNORES(soundOutput) BAKE_IGNORES(sfxResampling) BAKE_IGNORES(sfxTreble)
    BAKE_IGNORES(roomAcoustics) BAKE_IGNORES(reverbAmount) BAKE_IGNORES(farRooms) BAKE_IGNORES(roomTails) BAKE_IGNORES(roomGrid)
    BAKE_IGNORES(muffleWalls) BAKE_IGNORES(muffleDistance) BAKE_IGNORES(naturalFalloff) BAKE_IGNORES(madeImpacts)
    BAKE_IGNORES(glassSound) BAKE_IGNORES(ambience) BAKE_IGNORES(randomPitch) BAKE_IGNORES(alternativeComputerSounds)
    BAKE_IGNORES(ambienceAmount) BAKE_IGNORES(footsteps) BAKE_IGNORES(skyCache)
    // Drawing only: read by the frame's shaders and effects, by no bake and by nothing a bake reads.
    // (Left in on purpose though they look like drawing: palette, rebrick, windowGlass,
    // softLight, eyeAdaptation, bounceTint, gridSpriteLight, movingRelight; bakes or their inputs read them.)
    BAKE_IGNORES(accelerated) BAKE_IGNORES(retro) BAKE_IGNORES(filter) BAKE_IGNORES(spriteFilter) BAKE_IGNORES(bloom)
    BAKE_IGNORES(variedHighlights) BAKE_IGNORES(metalShading) BAKE_IGNORES(unevenTiles) BAKE_IGNORES(unevenWalls)
    BAKE_IGNORES(wornPlates) BAKE_IGNORES(windowStreaks) BAKE_IGNORES(weatherLamps) BAKE_IGNORES(texelLight) BAKE_IGNORES(fastBakeReads) BAKE_IGNORES(weaponLighting) BAKE_IGNORES(weaponArt)
    BAKE_IGNORES(glossyScreens) BAKE_IGNORES(liveReadouts) BAKE_IGNORES(glowingEyes) BAKE_IGNORES(glassReflections)
    BAKE_IGNORES(reflections) BAKE_IGNORES(retroReflections) BAKE_IGNORES(caustics) BAKE_IGNORES(ceilingCaustics)
    BAKE_IGNORES(causticsComputed) BAKE_IGNORES(causticsGrow) BAKE_IGNORES(causticsAngle) BAKE_IGNORES(causticsSway)
    BAKE_IGNORES(causticsSprites) BAKE_IGNORES(causticsShots) BAKE_IGNORES(dampShores) BAKE_IGNORES(glowEdges)
    BAKE_IGNORES(shoreRipples) BAKE_IGNORES(splashes) BAKE_IGNORES(sunDisc) BAKE_IGNORES(sunShafts) BAKE_IGNORES(sunScatter)
    BAKE_IGNORES(dust) BAKE_IGNORES(embers) BAKE_IGNORES(heatHaze) BAKE_IGNORES(playerShadow) BAKE_IGNORES(softSprites) BAKE_IGNORES(explosions)
    BAKE_IGNORES(shadowVolumes) BAKE_IGNORES(stippledShadows) BAKE_IGNORES(softShotShadows) BAKE_IGNORES(flashRooms)
    BAKE_IGNORES(doorLight) BAKE_IGNORES(shootableLights) BAKE_IGNORES(killShotsCarry) BAKE_IGNORES(muzzleFire) BAKE_IGNORES(skipHiddenGeometry)
    BAKE_IGNORES(unoccludedSurfaceLights) BAKE_IGNORES(fewerSurfaceLights) BAKE_IGNORES(preset) BAKE_IGNORES(detail)
    // Which side bakes goes in as it is, not as asked (gpuBakesOn): a stage baked on the
    // graphics card is not restored for the processor, nor one of one backend for another
    // (they agree within a shade, not to the bit). DERP_BAKECACHEANYSIDE=1 leaves it out, so
    // DERP_BAKECACHECHECK can hold one side's bake against the other's.
    BAKE_IGNORES(gpuBakes) BAKE_IGNORES(gpuRelight) BAKE_IGNORES(instantShots)
#undef BAKE_IGNORES
    Hasher h;h.add(s);
    if(!environment("DERP_BAKECACHEANYSIDE"))h.text(gpuBakesOn()?gpucompute::backend():"processor");
    h.add(levelMood());
    game_sun sun;Game_Sun(&sun);h.add(sun);
    h.add(mapInfo());
    // Weather's Random: this map's roll (settings alone say only Random).
    Climate climate=climateNow();h.add(climate);
    float hx=0,hy=0;windHeading(hx,hy);
    h.add((int)falling());
    float sky[]={overcast(),daylightScale(),windSpeedNow(),hx,hy,sunAzimuth[0],sunAzimuth[1],sunYaw,sunGlow,sunElevation,sunDiscElevation,sunShade,sunGain};
    h.add(sky);h.add(sunDirection);h.add(sunTint);
#ifdef DOOM_DEVTOOLS
    // Dev tools that take lamps out of the bake (bake_lights.cpp).
    bool tools[]={getenv("DERP_NOFLICKLAMPS")!=nullptr,getenv("DERP_NOLAMPS")!=nullptr};
    h.add(tools);
#endif
    return h.h;
}
// The game the level belongs to: what no setting changes.
uint64_t gameKey() {
    Hasher h;
    int game[]={Game_Phobos(),Game_WorldWraps(),Game_SurfaceGloss(),Game_NukageRain(),Game_WadKind(),gameepisode,gamemap};
    h.add(game);h.text(Game_Title());
    return h.h;
}
// A cache file is named by two keys. level: the build, the game, the artwork and the map, what
// stays while the player changes settings. variant: the settings and what follows from them
// (settingsKey). A level's files differ only in the variant, and a stage whose own inputs came
// out the same under other settings (the lamps when only the weather changed) is taken from
// one of them instead of being baked again (findSibling).
// Worked out once a frame at most: the inputs hold still while a frame bakes.
struct Keys { uint64_t level=0,variant=0; };
Keys currentKeys() {
    static unsigned level=~0u,serial=~0u,artLevel=~0u;static int tic=INT_MIN;static Keys keys;static uint64_t art=0;
    if(level==levelSerial&&serial==settingsSerial&&tic==leveltime)return keys;
    if(artLevel!=levelSerial) {art=artKey();artLevel=levelSerial;}
    Hasher h;h.text(buildStamp);h.add(formatVersion);h.add(art);h.add(mapKey());h.add(gameKey());
    keys.level=h.h;keys.variant=settingsKey();
    level=levelSerial;serial=settingsSerial;tic=leveltime;
    return keys;
}
uint64_t currentKey() {
    Keys keys=currentKeys();
    Hasher h;h.add(keys.level);h.add(keys.variant);
    return h.h;
}
// Whether the level's light is still as it loaded: the cache holds only that.
bool clean() {
    static unsigned level=~0u,live=0;
    if(level!=levelSerial) {level=levelSerial;live=liveLightSerial;}
    return liveLightSerial==live;
}

// ---- The items: what the loops wrote ----
struct ItemSpec { const char *name; uint32_t element; char type; };   // type: b bytes, h 16-bit values, f floats
const ItemSpec sunItems[]={{"sun on floors",1,'b'},{"sun on walls",1,'b'}};
const ItemSpec lightItems[]={{"lamp light on floors",3,'b'},{"lamp light on ceilings",3,'b'},{"lamp light on walls",3,'b'},
    {"lamp directions on floors",4,'b'},{"lamp directions on ceilings",4,'b'},{"lamp directions on walls",4,'b'},
    {"lamp lobes on floors",6,'h'},{"lamp lobes on ceilings",6,'h'},{"lamp lobes on walls",6,'h'}};
const ItemSpec gridItems[]={{"light grid cells",4,'f'}};
const ItemSpec ambientItems[]={{"sky and openness on floors",1,'b'},{"sky and openness on ceilings",1,'b'},{"sky and openness on walls",1,'b'}};
const ItemSpec bounceItems[]={{"bounce on floors",3,'b'},{"bounce on ceilings",3,'b'},{"bounce on walls",3,'b'},
    {"bump basis on floors",3,'b'},{"bump basis on ceilings",3,'b'},{"bump basis on walls",3,'b'},
    {"bounce flicker on floors",1,'b'},{"bounce flicker on ceilings",1,'b'},{"bounce flicker on walls",1,'b'},
    {"bounce carry on floors",6,'h'},{"bounce carry on ceilings",6,'h'},{"bounce carry on walls",6,'h'}};
const ItemSpec flowItems[]={{"flow neighbour",2,'h'},{"flow weight",1,'b'}};
const ItemSpec *itemSpecs(LoadStage stage,int &count) {
    switch(stage) {
    case LoadStage::Sun:count=2;return sunItems;
    case LoadStage::Lights:count=9;return lightItems;
    case LoadStage::Grid:count=1;return gridItems;
    case LoadStage::Ambient:count=3;return ambientItems;
    case LoadStage::Bounce:count=12;return bounceItems;
    case LoadStage::Flow:count=2;return flowItems;
    default:count=0;return nullptr;
    }
}
// What the cache reads of a stage's shape itself.
int shapeOf(LoadStage stage) {
    if(stage!=LoadStage::Lights)return 0;
    return (flatDirectionCells.empty()?0:lightDirections)|(flatLampLobes.empty()?0:lightLobes);
}
int shapeMask(LoadStage stage) {return stage==LoadStage::Lights?lightDirections|lightLobes:stage==LoadStage::Bounce?bounceBasis|bounceCarry:0;}
// Whether the buffers the stage's items live in are there at their sizes.
bool ready(LoadStage stage,int flags) {
    const size_t layer=(size_t)mapWidth*mapHeight,atlas=(size_t)atlasStride*atlasHeight*4;
    if(!layer||seamCells.size()!=layer||contactCells.size()!=4*layer||flatLightCells.size()!=16*layer||wallBakeCells.size()!=atlas)return false;
    for(int index:bakeOrder) {
        if(index<0||(size_t)index>=bakeStrips.size())return false;
        const BakeStrip &s=bakeStrips[index];
        if(s.x<0||s.y<0||s.width<0||s.height<0||s.x+s.width>atlasWidth||s.y+s.height>atlasHeight)return false;
    }
    bool directions=(stage==LoadStage::Lights&&(flags&lightDirections))||(stage==LoadStage::Bounce&&(flags&(bounceBasis|bounceFlickers)));
    if(directions&&(flatDirectionCells.size()!=16*layer||wallDirectionCells.size()!=atlas))return false;
    if(stage==LoadStage::Lights&&(flags&lightLobes)&&(flatLampLobes.size()!=2*layer||wallLampLobes.size()!=(size_t)atlasWidth*atlasHeight))return false;
    if(stage==LoadStage::Grid&&staticGrid.size()!=(size_t)staticWidth*staticHeight)return false;
    if(stage==LoadStage::Bounce&&(flags&bounceCarry)&&levelCarry.size()!=levelCarrySize())return false;
    return true;
}
// Calls visit(item, element, bytes) for every element a stage's loops write,
// in one fixed order, gathering and restoring alike. The cells and texels are
// exactly those each loop writes (see the top of the file).
template<class Visit> void visitStage(LoadStage stage,int flags,Visit &&visit) {
    const size_t layer=(size_t)mapWidth*mapHeight;
    auto floorCell=[](size_t i) {int s=seamCells[i].own;return s!=noSector&&sectors[s].floorpic!=skyflatnum;};
    auto ceilingCell=[](size_t i) {int s=seamCells[i].own;return s!=noSector&&sectors[s].ceilingpic!=skyflatnum;};
    auto owned=[](size_t i) {return seamCells[i].own!=noSector;};
    auto every=[](size_t) {return true;};
    auto cells=[&](int item,uint8_t *base,size_t stride,size_t offset,size_t bytes,auto pick) {
        for(size_t i=0;i<layer;++i)if(pick(i))visit(item,base+stride*i+offset,bytes);
    };
    // row: elements per atlas row (atlasStride, or atlasWidth for the lamp lobes); half 1: the right half.
    auto texels=[&](int item,uint8_t *base,size_t stride,size_t row,int half,size_t offset,size_t bytes) {
        for(int index:bakeOrder) {
            const BakeStrip &strip=bakeStrips[index];
            for(int y=0;y<strip.height;++y)for(int x=0;x<strip.width;++x)
                visit(item,base+stride*((size_t)(strip.y+y)*row+(size_t)half*atlasWidth+strip.x+x)+offset,bytes);
        }
    };
    uint8_t *flat=flatLightCells.data(),*wall=wallBakeCells.data(),*contact=contactCells.data();
    uint8_t *flatDirection=flatDirectionCells.data(),*wallDirection=wallDirectionCells.data();
    const size_t row=atlasStride;
    switch(stage) {
    case LoadStage::Sun:
        cells(0,contact,4,2,1,floorCell);
        texels(1,wall,4,row,0,3,1);
        break;
    case LoadStage::Lights:
        cells(0,flat+4*(size_t)floorLight*layer,4,0,3,floorCell);
        cells(1,flat+4*(size_t)ceilingLight*layer,4,0,3,ceilingCell);
        texels(2,wall,4,row,0,0,3);
        if(flags&lightDirections) {
            cells(3,flatDirection,4,0,4,floorCell);
            cells(4,flatDirection+4*layer,4,0,4,ceilingCell);
            texels(5,wallDirection,4,row,0,0,4);
        }
        if(flags&lightLobes) {
            uint8_t *lobes=reinterpret_cast<uint8_t*>(flatLampLobes.data());
            cells(6,lobes,6,0,6,floorCell);
            cells(7,lobes+6*layer,6,0,6,ceilingCell);
            texels(8,reinterpret_cast<uint8_t*>(wallLampLobes.data()),6,atlasWidth,0,0,6);
        }
        break;
    case LoadStage::Ambient:
        cells(0,flat+4*(size_t)floorBounce*layer,4,3,1,owned);
        cells(1,flat+4*(size_t)ceilingBounce*layer,4,3,1,owned);
        texels(2,wall,4,row,1,3,1);
        break;
    case LoadStage::Bounce:
        cells(0,flat+4*(size_t)floorBounce*layer,4,0,3,owned);
        cells(1,flat+4*(size_t)ceilingBounce*layer,4,0,3,owned);
        texels(2,wall,4,row,1,0,3);
        if(flags&bounceBasis) {
            cells(3,flatDirection+8*layer,4,0,3,owned);
            cells(4,flatDirection+12*layer,4,0,3,owned);
            texels(5,wallDirection,4,row,1,0,3);
        }
        if(flags&bounceFlickers) {
            cells(6,flatDirection+8*layer,4,3,1,owned);
            cells(7,flatDirection+12*layer,4,3,1,owned);
            texels(8,wallDirection,4,row,1,3,1);
        }
        if(flags&bounceCarry) {   // levelCarry's layout (bounce_carry.h): floors, ceilings, then the atlas by atlasWidth
            uint8_t *carry=reinterpret_cast<uint8_t*>(levelCarry.data());
            cells(9,carry,6,0,6,owned);
            cells(10,carry+6*layer,6,0,6,owned);
            texels(11,carry+12*layer,6,atlasWidth,0,0,6);
        }
        break;
    case LoadStage::Flow:
        cells(0,reinterpret_cast<uint8_t*>(seamCells.data()),sizeof(SeamCell),offsetof(SeamCell,flow),2,every);
        cells(1,contact,4,3,1,every);
        break;
    default:break;
    }
}
// A grid cell as floats, both ways: value reads or writes each in turn.
constexpr size_t gridFloats=57;
template<class Value> void cellFields(StaticCell &cell,Value value) {
    float used=cell.used?1.0f:0.0f;value(used);cell.used=used!=0;
    value(cell.low);value(cell.high);
    for(StaticSample &s:cell.at) {
        for(float &c:s.color)value(c);
        for(float &c:s.towards)value(c);
        value(s.amount);
        for(float &c:s.bounce)value(c);
        float group=(float)s.group;value(group);s.group=(int)group;
        value(s.groupShare);
        float shadow=(float)s.shadow;value(shadow);s.shadow=(int)shadow;
        value(s.shadowAmount);
        for(float &c:s.sky)value(c);
        for(float &c:s.glow)value(c);
        for(float &c:s.cast)value(c);
        value(s.pool);
        for(float &c:s.lampWay)value(c);
    }
}
using Items=std::vector<std::vector<uint8_t>>;
void gatherStage(LoadStage stage,int flags,Items &items,std::vector<uint32_t> &elements) {
    items.assign(maxItems,{});elements.assign(maxItems,0);
    if(stage==LoadStage::Grid) {
        std::vector<uint8_t> &out=items[0];out.reserve(staticGrid.size()*gridFloats*4);
        for(StaticCell &cell:staticGrid)cellFields(cell,[&](float &v) {uint8_t b[4];std::memcpy(b,&v,4);out.insert(out.end(),b,b+4);});
        elements[0]=4;
        return;
    }
    visitStage(stage,flags,[&](int item,uint8_t *at,size_t bytes) {items[item].insert(items[item].end(),at,at+bytes);elements[item]=(uint32_t)bytes;});
}
// Puts the items back, only once every item's size has checked out.
bool scatterStage(LoadStage stage,int flags,const Items &items) {
    if(stage==LoadStage::Grid) {
        for(int k=1;k<maxItems;++k)if(!items[k].empty())return false;
        if(items[0].size()!=staticGrid.size()*gridFloats*4)return false;
        size_t at=0;
        for(StaticCell &cell:staticGrid)cellFields(cell,[&](float &v) {std::memcpy(&v,items[0].data()+at,4);at+=4;});
        return true;
    }
    std::vector<size_t> counts(maxItems,0);
    visitStage(stage,flags,[&](int item,uint8_t*,size_t bytes) {counts[item]+=bytes;});
    for(int k=0;k<maxItems;++k)if(counts[k]!=items[k].size())return false;
    std::vector<size_t> at(maxItems,0);
    visitStage(stage,flags,[&](int item,uint8_t *p,size_t bytes) {std::memcpy(p,items[item].data()+at[item],bytes);at[item]+=bytes;});
    return true;
}

// ---- Packing: each item split into byte planes, run-length coded ----
// Light is smooth and much of it is dark or even, so the planes run long.
// Codes below 128 copy that many plus one bytes; 128 and up repeat the next
// byte code-125 times (3 to 130).
void packPlane(const std::vector<uint8_t> &plane,std::vector<uint8_t> &out) {
    const size_t n=plane.size();
    size_t i=0;
    while(i<n) {
        size_t run=1;
        while(i+run<n&&run<130&&plane[i+run]==plane[i])++run;
        if(run>=3) {out.push_back((uint8_t)(128+run-3));out.push_back(plane[i]);i+=run;continue;}
        size_t start=i,length=0;
        while(i<n&&length<128) {
            if(i+2<n&&plane[i]==plane[i+1]&&plane[i]==plane[i+2])break;
            ++i;++length;
        }
        out.push_back((uint8_t)(length-1));
        out.insert(out.end(),plane.begin()+(ptrdiff_t)start,plane.begin()+(ptrdiff_t)(start+length));
    }
}
bool unpackPlane(const uint8_t *&p,const uint8_t *end,size_t count,std::vector<uint8_t> &plane) {
    plane.resize(count);
    size_t at=0;
    while(at<count) {
        if(p>=end)return false;
        unsigned code=*p++;
        if(code<128) {
            size_t length=code+1;
            if((size_t)(end-p)<length||count-at<length)return false;
            std::memcpy(plane.data()+at,p,length);p+=length;at+=length;
        } else {
            size_t length=code-125;
            if(p>=end||count-at<length)return false;
            std::memset(plane.data()+at,*p++,length);at+=length;
        }
    }
    return true;
}
struct ItemHeader { uint32_t item,element; uint64_t raw,packed; };
// A section: this header, parts hashes of its inputs (stageInputs' order), then its items.
struct SectionHeader { uint32_t stage; int32_t flags; uint64_t inputs,bytes,check; uint32_t parts,unused; };
struct FileHeader { char magic[8]; uint32_t version,sections; uint64_t key,build,check; };
static_assert(sizeof(ItemHeader)==24&&sizeof(SectionHeader)==40&&sizeof(FileHeader)==40,"file layout");
void packItem(uint32_t item,uint32_t element,const std::vector<uint8_t> &raw,std::vector<uint8_t> &out) {
    ItemHeader header={item,element,(uint64_t)raw.size(),0};
    size_t headerAt=out.size();out.resize(out.size()+sizeof header);
    size_t start=out.size(),count=raw.size()/element;
    std::vector<uint8_t> plane(count);
    for(uint32_t k=0;k<element;++k) {
        for(size_t j=0;j<count;++j)plane[j]=raw[j*element+k];
        packPlane(plane,out);
    }
    header.packed=out.size()-start;
    std::memcpy(out.data()+headerAt,&header,sizeof header);
}
bool unpackItems(const std::vector<uint8_t> &data,Items &items,std::vector<uint32_t> &elements) {
    items.assign(maxItems,{});elements.assign(maxItems,0);
    size_t at=0;
    std::vector<uint8_t> plane;
    while(at<data.size()) {
        ItemHeader header;
        if(data.size()-at<sizeof header)return false;
        std::memcpy(&header,data.data()+at,sizeof header);at+=sizeof header;
        if(header.item>=(uint32_t)maxItems||header.element==0||header.element>64||header.raw%header.element||header.raw>(1ull<<31)||
           header.packed>data.size()-at||!items[header.item].empty())return false;
        size_t count=(size_t)(header.raw/header.element);
        std::vector<uint8_t> &raw=items[header.item];raw.resize((size_t)header.raw);elements[header.item]=header.element;
        const uint8_t *p=data.data()+at,*end=p+header.packed;
        for(uint32_t k=0;k<header.element;++k) {
            if(!unpackPlane(p,end,count,plane))return false;
            for(size_t j=0;j<count;++j)raw[j*header.element+k]=plane[j];
        }
        if(p!=end)return false;
        at+=(size_t)header.packed;
    }
    return true;
}

// ---- The session: one file's stages, for one key ----
struct Section { bool present=false,fresh=false; int flags=0; uint64_t inputs=0; size_t raw=0; std::vector<uint64_t> parts; std::vector<uint8_t> data; };
struct Session { bool open=false,hadFile=false; uint64_t key=0,level=0; std::string path; Section stages[stageCount]; } session;
// Between a stage's restore and its keep: what its loops read, and in the
// check, what the cache holds to compare with.
struct Pending { bool armed=false,expect=false; uint64_t inputs=0; Parts parts; Section expected; } pending[stageCount];
// This frame's bakes, for the stderr line. borrowed: stages taken from the level's files under other settings.
struct Report { bool any=false,skipped=false; int borrowed=0; std::vector<const char*> restored,baked; size_t restoredBytes=0; uint64_t restoreNs=0; } report;

bool reject(const Session &s,const char *why) {
    fprintf(stderr,"3D bake cache: %s is not usable (%s); baking afresh.\n",s.path.c_str(),why);
    return false;
}
bool readCache(Session &s) {
    FILE *file=fopen(s.path.c_str(),"rb");
    if(!file)return false;
    std::vector<uint8_t> all;
    bool ok=fseek(file,0,SEEK_END)==0;
    long size=ok?ftell(file):-1;
    ok=ok&&size>=(long)sizeof(FileHeader)&&fseek(file,0,SEEK_SET)==0;
    if(ok) {all.resize((size_t)size);ok=fread(all.data(),1,all.size(),file)==all.size();}
    fclose(file);
    if(!ok)return reject(s,"short read");
    FileHeader header;std::memcpy(&header,all.data(),sizeof header);
    if(std::memcmp(header.magic,magic,8)!=0||header.check!=bakeHash(&header,offsetof(FileHeader,check),0))return reject(s,"damaged header");
    if(header.version!=formatVersion||header.build!=buildHash()||header.key!=s.key)return reject(s,"another build or key");
    if(header.sections>(uint32_t)stageCount)return reject(s,"damaged header");
    size_t at=sizeof header;
    Section found[stageCount];
    for(uint32_t n=0;n<header.sections;++n) {
        SectionHeader section;
        if(all.size()-at<sizeof section)return reject(s,"cut short");
        std::memcpy(&section,all.data()+at,sizeof section);at+=sizeof section;
        size_t partBytes=(size_t)section.parts*8;
        if(section.stage>=(uint32_t)stageCount||found[section.stage].present||section.parts>64||partBytes>all.size()-at||
           section.bytes>all.size()-at-partBytes)return reject(s,"damaged section");
        uint64_t seed=bakeHash(all.data()+at,partBytes,section.inputs);
        if(bakeHash(all.data()+at+partBytes,(size_t)section.bytes,seed)!=section.check)return reject(s,"checksum");
        Section &into=found[section.stage];
        into.present=true;into.flags=section.flags;into.inputs=section.inputs;
        into.parts.resize(section.parts);
        if(partBytes)std::memcpy(into.parts.data(),all.data()+at,partBytes);
        at+=partBytes;
        into.data.assign(all.data()+at,all.data()+at+section.bytes);
        at+=(size_t)section.bytes;
    }
    if(at!=all.size())return reject(s,"trailing bytes");
    for(int k=0;k<stageCount;++k)s.stages[k]=std::move(found[k]);
    s.hadFile=true;
    return true;
}
void openSession() {
    uint64_t key=currentKey();
    if(session.open&&session.key==key)return;
    Keys keys=currentKeys();
    session=Session{};
    session.open=true;session.key=key;session.level=keys.level;
    char path[80];snprintf(path,sizeof path,"%s/%016llx-%016llx.bin",folder,(unsigned long long)keys.level,(unsigned long long)keys.variant);
    session.path=path;
    readCache(session);
}
// A stage with these inputs from another of the level's files (the same level under other
// settings), the newest first. Only the headers are read until one fits.
bool findSibling(LoadStage stage,uint64_t inputs,Section &out) {
    char pattern[32];snprintf(pattern,sizeof pattern,"%016llx-*.bin",(unsigned long long)session.level);
    int count=0;
    char **names=SDL_GlobDirectory(folder,pattern,0,&count);
    if(!names)return false;
    struct Entry { std::string path; SDL_Time time; };
    std::vector<Entry> files;
    for(int n=0;n<count;++n) {
        std::string path=std::string(folder)+"/"+names[n];
        SDL_PathInfo info;
        if(path!=session.path&&SDL_GetPathInfo(path.c_str(),&info)&&info.type==SDL_PATHTYPE_FILE)files.push_back({path,info.modify_time});
    }
    SDL_free(names);
    std::sort(files.begin(),files.end(),[](const Entry &a,const Entry &b) {return a.time>b.time;});
    for(const Entry &entry:files) {
        FILE *file=fopen(entry.path.c_str(),"rb");
        if(!file)continue;
        bool found=false;
        FileHeader header;
        bool ok=fread(&header,sizeof header,1,file)==1&&std::memcmp(header.magic,magic,8)==0&&
                header.check==bakeHash(&header,offsetof(FileHeader,check),0)&&header.version==formatVersion&&
                header.build==buildHash()&&header.sections<=(uint32_t)stageCount;
        for(uint32_t n=0;ok&&!found&&n<header.sections;++n) {
            SectionHeader section;
            ok=fread(&section,sizeof section,1,file)==1&&section.parts<=64&&section.bytes<=maxBytes;
            if(!ok)break;
            long skip=(long)(section.parts*8+section.bytes);
            if(section.stage!=(uint32_t)stage||section.inputs!=inputs) {ok=fseek(file,skip,SEEK_CUR)==0;continue;}
            Section taken;
            taken.parts.resize(section.parts);taken.data.resize((size_t)section.bytes);
            ok=(taken.parts.empty()||fread(taken.parts.data(),8,taken.parts.size(),file)==taken.parts.size())&&
               (taken.data.empty()||fread(taken.data.data(),1,taken.data.size(),file)==taken.data.size());
            if(!ok)break;
            uint64_t seed=bakeHash(taken.parts.data(),taken.parts.size()*8,section.inputs);
            if(bakeHash(taken.data.data(),taken.data.size(),seed)!=section.check)break;
            taken.present=true;taken.fresh=true;taken.flags=section.flags;taken.inputs=section.inputs;
            out=std::move(taken);found=true;
        }
        fclose(file);
        if(found)return true;
    }
    return false;
}
bool endsWith(const char *name,const char *tail) {
    size_t a=strlen(name),b=strlen(tail);
    return a>=b&&strcmp(name+a-b,tail)==0;
}
// The folder kept to maxFiles files and maxBytes, the oldest written going
// first; left-over temporary files from a crash go after an hour.
void prune(const std::string &kept) {
    int count=0;
    char **names=SDL_GlobDirectory(folder,"*",0,&count);
    if(!names)return;
    struct Entry { std::string path; SDL_Time time; uint64_t size; };
    std::vector<Entry> files;
    SDL_Time now=0;SDL_GetCurrentTime(&now);
    for(int n=0;n<count;++n) {
        std::string path=std::string(folder)+"/"+names[n];
        SDL_PathInfo info;
        if(!SDL_GetPathInfo(path.c_str(),&info)||info.type!=SDL_PATHTYPE_FILE)continue;
        if(endsWith(names[n],".tmp")) {if(now-info.modify_time>(SDL_Time)3600*SDL_NS_PER_SECOND)SDL_RemovePath(path.c_str());continue;}
        if(endsWith(names[n],".bin"))files.push_back({path,info.modify_time,info.size});
    }
    SDL_free(names);
    std::sort(files.begin(),files.end(),[](const Entry &a,const Entry &b) {return a.time>b.time;});
    uint64_t total=0;
    for(size_t n=0;n<files.size();++n) {
        total+=files[n].size;
        if(n>0&&files[n].path!=kept&&(n>=(size_t)maxFiles||total>maxBytes))SDL_RemovePath(files[n].path.c_str());
    }
}
// All the session's stages into its file: written under a temporary name and
// renamed over the old one, so a crash part way leaves no half file behind.
bool writeCache(const Session &s,size_t &bytes) {
    SDL_CreateDirectory(folder);
    std::vector<uint8_t> out(sizeof(FileHeader));
    uint32_t sections=0;
    for(int k=0;k<stageCount;++k) {
        const Section &section=s.stages[k];
        if(!section.present)continue;
        const size_t partBytes=section.parts.size()*8;
        uint64_t seed=bakeHash(section.parts.data(),partBytes,section.inputs);
        SectionHeader header={(uint32_t)k,(int32_t)section.flags,section.inputs,(uint64_t)section.data.size(),
                              bakeHash(section.data.data(),section.data.size(),seed),(uint32_t)section.parts.size(),0};
        const uint8_t *p=reinterpret_cast<const uint8_t*>(&header);
        out.insert(out.end(),p,p+sizeof header);
        const uint8_t *q=reinterpret_cast<const uint8_t*>(section.parts.data());
        out.insert(out.end(),q,q+partBytes);
        out.insert(out.end(),section.data.begin(),section.data.end());
        ++sections;
    }
    FileHeader header={};
    std::memcpy(header.magic,magic,8);
    header.version=formatVersion;header.sections=sections;header.key=s.key;header.build=buildHash();
    header.check=bakeHash(&header,offsetof(FileHeader,check),0);
    std::memcpy(out.data(),&header,sizeof header);
    std::string temporary=s.path+"."+std::to_string((unsigned long long)(SDL_GetTicksNS()%1000000007ull))+".tmp";
    FILE *file=fopen(temporary.c_str(),"wb");
    if(!file)return false;
    bool ok=fwrite(out.data(),1,out.size(),file)==out.size();
    ok=fflush(file)==0&&ok;
    ok=fclose(file)==0&&ok;
    if(!ok||!SDL_RenamePath(temporary.c_str(),s.path.c_str())) {SDL_RemovePath(temporary.c_str());return false;}
    bytes=out.size();
    prune(s.path);
    return true;
}
// The check: what the cache holds against what was just baked, item by item.
void compareStage(LoadStage stage,const Pending &p,const Items &fresh,int flags) {
    Items cached;std::vector<uint32_t> elements;
    const char *name=stageName(stage);
    if(!unpackItems(p.expected.data,cached,elements)) {fprintf(stderr,"3D bake cache check, %s: the cached data is damaged.\n",name);return;}
    fprintf(stderr,"3D bake cache check, %s: inputs %s, flags %s.\n",name,
            p.expected.inputs==p.inputs?"match":"differ (the bake code was rebuilt, or the key misses something)",
            p.expected.flags==flags?"match":"differ");
    // Which parts of the inputs moved: both sides' hashes, the differing marked.
    if(p.expected.inputs!=p.inputs) {
        for(size_t k=0;k<p.parts.size();++k) {
            bool kept=k<p.expected.parts.size();
            uint64_t was=kept?p.expected.parts[k]:0,now=p.parts[k].second;
            fprintf(stderr,"  input %-22s cached %016llx fresh %016llx%s\n",p.parts[k].first,(unsigned long long)was,(unsigned long long)now,
                    !kept?"  (not in the file)":was!=now?"  <- differs":"");
        }
    }
    int count=0;const ItemSpec *specs=itemSpecs(stage,count);
    for(int k=0;k<count;++k) {
        const std::vector<uint8_t> &a=cached[k],&b=fresh[k];
        if(a.empty()&&b.empty())continue;
        if(a.size()!=b.size()) {fprintf(stderr,"  %s: sizes differ (cached %zu, fresh %zu bytes)\n",specs[k].name,a.size(),b.size());continue;}
        size_t values=0,differ=0,over1=0,over2=0,worst=0;double most=0;
        if(specs[k].type=='h') {
            for(size_t i=0;i+1<a.size();i+=2) {
                uint16_t x,y;std::memcpy(&x,&a[i],2);std::memcpy(&y,&b[i],2);
                ++values;if(x!=y) {++differ;most=std::max(most,(double)std::abs((int)x-(int)y));}
            }
        } else if(specs[k].type=='f') {
            for(size_t i=0;i+3<a.size();i+=4) {
                float x,y;std::memcpy(&x,&a[i],4);std::memcpy(&y,&b[i],4);
                ++values;
                if(std::memcmp(&x,&y,4)!=0) {++differ;double d=std::fabs((double)x-(double)y);most=std::max(most,std::isnan(d)?INFINITY:d);}
            }
        } else {
            // Bytes: how many are off by more than one and by more than two, and where the worst is
            // (the GPU bakes agree within a shade, not to the bit: PLAN-gpu-bakes.md).
            for(size_t i=0;i<a.size();++i) {
                ++values;if(a[i]==b[i])continue;
                int off=std::abs((int)a[i]-(int)b[i]);
                ++differ;over1+=off>1;over2+=off>2;
                if(off>most) {most=off;worst=i;}
            }
        }
        if(!differ)fprintf(stderr,"  %s: match (%zu values)\n",specs[k].name,values);
        else if(specs[k].type=='h'||specs[k].type=='f')fprintf(stderr,"  %s: %zu of %zu values differ, most by %g\n",specs[k].name,differ,values,most);
        else fprintf(stderr,"  %s: %zu of %zu values differ (%.4f%% by more than 1, %.4f%% by more than 2), most by %g at byte %zu\n",
                     specs[k].name,differ,values,100.0*over1/std::max<size_t>(1,values),100.0*over2/std::max<size_t>(1,values),most,worst);
    }
}
std::string joined(const std::vector<const char*> &names) {
    std::string out;
    for(size_t n=0;n<names.size();++n)out+=(n?(n+1==names.size()?" and ":", "):"")+std::string(names[n]);
    return out;
}
} // namespace

bool bakeCacheRestore(LoadStage stage,const char *built,uint64_t inputs,int *flags) {
    int s=(int)stage;
    if(s<0||s>=stageCount)return false;
    pending[s]=Pending{};
    if(!cacheOn())return false;
    // DERP_BAKEFRESH: bakes afresh, as without the cache, but writes it (and the slice rates, gpu_trace.cpp) as usual.
    if(environment("DERP_BAKEFRESH"))return false;
    if(!clean()) {report.any=report.skipped=true;return false;}
    int shape=(flags?*flags:0)|shapeOf(stage);
    if(!ready(stage,shape))return false;
    uint64_t start=SDL_GetTicksNS();
    Parts parts=stageInputs(stage);
    parts.push_back({"bake's own inputs",bakeHash(&inputs,sizeof inputs,0)});
    parts.push_back({"bake's build",bakeHash(built,strlen(built),0)});
    Hasher h;
    for(const auto &p:parts)h.add(p.second);
    pending[s].armed=true;pending[s].inputs=h.h;pending[s].parts=std::move(parts);
    openSession();
    Section &cached=session.stages[s];
    if(!cached.present||cached.inputs!=pending[s].inputs) {
        Section other;
        if(findSibling(stage,pending[s].inputs,other)) {cached=std::move(other);++report.borrowed;}
    }
    if(!cached.present)return false;
    if(checkOn()) {pending[s].expect=true;pending[s].expected=cached;return false;}
    if(cached.inputs!=pending[s].inputs) {
        fprintf(stderr,"3D bake cache: the %s was cached from other inputs; baking it afresh.\n",stageName(stage));
        return false;
    }
    int mask=shapeMask(stage);
    if((cached.flags&mask)!=(shape&mask)||!ready(stage,cached.flags))return false;
    Items items;std::vector<uint32_t> elements;
    int count=0;const ItemSpec *specs=itemSpecs(stage,count);
    bool ok=unpackItems(cached.data,items,elements);
    for(int k=0;ok&&k<maxItems;++k)ok=items[k].empty()||(k<count&&elements[k]==specs[k].element);
    if(!ok||!scatterStage(stage,cached.flags,items)) {
        fprintf(stderr,"3D bake cache: the cached %s does not fit this level; baking it afresh.\n",stageName(stage));
        cached=Section{};
        return false;
    }
    if(flags)*flags=cached.flags;
    pending[s].armed=false;   // what the file holds is what was baked
    for(const auto &item:items)report.restoredBytes+=item.size();
    report.any=true;report.restored.push_back(stageName(stage));
    report.restoreNs+=SDL_GetTicksNS()-start;
    loadingRestored(stage);
    return true;
}
void bakeCacheKeep(LoadStage stage,int flags) {
    int s=(int)stage;
    if(s<0||s>=stageCount||!pending[s].armed)return;
    Pending &p=pending[s];
    p.armed=false;
    flags|=shapeOf(stage);
    if(!ready(stage,flags))return;
    Items items;std::vector<uint32_t> elements;
    gatherStage(stage,flags,items,elements);
    if(p.expect)compareStage(stage,p,items,flags);
    openSession();
    Section &section=session.stages[s];
    report.any=true;report.baked.push_back(stageName(stage));
    // The check keeps what the cache holds, to check again; a stage cached from other inputs is stale anyway.
    if(p.expect&&section.present&&section.inputs==p.inputs)return;
    Section fresh;
    fresh.present=fresh.fresh=true;fresh.flags=flags;fresh.inputs=p.inputs;
    for(const auto &named:p.parts)fresh.parts.push_back(named.second);
    for(int k=0;k<maxItems;++k)if(elements[k]) {packItem((uint32_t)k,elements[k],items[k],fresh.data);fresh.raw+=items[k].size();}
    section=std::move(fresh);
}
void bakeCacheFinish() {
    clean();   // the level's first frame sets what its light was at load, bakes or none
    if(!report.any)return;
    Report done=std::move(report);
    report=Report{};
    for(Pending &p:pending)p=Pending{};
    std::string line;
    char part[256];
    if(!done.restored.empty()) {
        snprintf(part,sizeof part,"restored %s (%.1f MB) in %.0f ms",joined(done.restored).c_str(),done.restoredBytes/1048576.0,done.restoreNs/1e6);
        line+=part;
        if(done.borrowed) {snprintf(part,sizeof part,", %d of them baked under other settings",done.borrowed);line+=part;}
    }
    bool fresh=false;size_t raw=0;
    for(const Section &section:session.stages)if(section.fresh) {fresh=true;raw+=section.raw;}
    if(!done.baked.empty()) {
        snprintf(part,sizeof part,"%sbaked %s afresh%s",line.empty()?"":"; ",joined(done.baked).c_str(),
                 session.hadFile?"":" (no cache for this map and settings yet)");
        line+=part;
    }
    if(fresh&&session.open&&cacheOn()&&clean()) {
        uint64_t start=SDL_GetTicksNS();size_t bytes=0;
        if(writeCache(session,bytes))
            snprintf(part,sizeof part,"; wrote %.1f MB (%.1f MB unpacked) to %s in %.0f ms",bytes/1048576.0,raw/1048576.0,session.path.c_str(),(SDL_GetTicksNS()-start)/1e6);
        else snprintf(part,sizeof part,"; could not write %s",session.path.c_str());
        line+=part;
    }
    if(done.skipped)line+=line.empty()?"not used: the level's light has changed in play since it loaded":"";
    if(!line.empty())fprintf(stderr,"3D bake cache: %s (tic %d).\n",line.c_str(),leveltime);
    session=Session{};
}
} // namespace doom3d
