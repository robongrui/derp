/* Drawing helpers: the per-frame upload of vertices and light and sector
   data, resource binding, and the world and overlay draws the passes share. */
#include "internal.h"
#include "../scene3d/sky_kind.h"
#ifdef DOOM_DEVTOOLS
extern "C" void R_TextureName(int texture,char name[9]);   // (DERP_OPAQUECLASS)
#endif

namespace gpu3d {
struct Arena { SDL_GPUBuffer *buffer=nullptr; Uint32 capacity=0; };
static Arena vertexArena,blockerArena,sectorArena;
static SDL_GPUTransferBuffer *frameTransfer;
static Uint32 frameTransferCapacity;
static std::unordered_map<const std::vector<Vertex>*,Uint32> vertexOffsets;
// The next upload follows a load (presizeFrameUploads). A level's first frame
// is built whole (geometry.cpp), so its size bounds what play asks for.
static bool presizing=false;
void presizeFrameUploads() {presizing=true;}
// One copy pass per frame uploads every vertex batch and the light/sector data.
// An arena grows to half again what it needs, or to room when that is more.
static void reserve(Arena &arena,Uint32 bytes,SDL_GPUBufferUsageFlags usage,Uint32 room=0) {
    if(arena.buffer&&arena.capacity>=std::max(bytes,room))return;
    if(arena.buffer)SDL_ReleaseGPUBuffer(device,arena.buffer);
    arena.capacity=std::max<Uint32>({65536,bytes+bytes/2,room});
    SDL_GPUBufferCreateInfo info={usage,arena.capacity,0};
    arena.buffer=SDL_CreateGPUBuffer(device,&info);
    if(!arena.buffer)I_Error((char*)"Could not allocate GPU buffer: %s",SDL_GetError());
}
bool hudCurrent=false;
static void sortQuiet();   // (below, with drawOpaque)
void uploadFrame(SDL_GPUCommandBuffer *command,const std::vector<const std::vector<Vertex>*> &transient,
                 const std::vector<std::array<float,4>> &sectors,const unsigned *palette,const std::vector<unsigned> &hud,
                 bool hudChanged,const std::vector<unsigned> &panel) {
    // The HUD (256 KB) only when it changed: most frames it stands still.
    bool sendHud=hudChanged||!hudCurrent;
    sortQuiet();
    vertexOffsets.clear();Uint32 bytes=0;
    auto place=[&](const std::vector<Vertex> &vertices) {
        if(vertices.empty())return;
        vertexOffsets[&vertices]=bytes;
        bytes=(bytes+(Uint32)(vertices.size()*sizeof(Vertex))+255)&~Uint32(255);
    };
    for(const auto *groups:{&batches,&shadowBatches,&decalBatches,&mirrorBatches})for(const auto &batch:*groups)place(batch.second);
    place(mistVertices);place(shaftVertices);place(particleVertices);place(glowVertices);place(crestVertices);place(lampSheetVertices);place(heatVertices);place(skyVertices);place(glassVertices);for(const auto &set:shadowVolumes)place(set.vertices);
    for(const auto *vertices:transient)place(*vertices);
    Uint32 blockerBytes=(Uint32)(lightWords.size()*sizeof(lightWords[0]));
    Uint32 sectorBytes=(Uint32)(sectors.size()*sizeof(sectors[0]));
    Uint32 vertexBytes=std::max<Uint32>(bytes,256);
    // After a load: the whole map's vertices and half again, and 4 MB more
    // for what comes and goes (weather, blood, sprites), so play never grows it.
    Uint32 vertexRoom=presizing?(Uint32)std::min<uint64_t>(vertexBytes+vertexBytes/2+(4ull<<20),1ull<<30):0;
    presizing=false;
    reserve(vertexArena,vertexBytes,SDL_GPU_BUFFERUSAGE_VERTEX,vertexRoom);
    reserve(blockerArena,blockerBytes,SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ);
    reserve(sectorArena,sectorBytes,SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ);
    Uint32 imageOffset=vertexBytes+blockerBytes+sectorBytes,hudBytes=sendHud?(Uint32)(hud.size()*4):0;
    Uint32 panelBytes=(Uint32)(panel.size()*4),total=imageOffset+1024+hudBytes+panelBytes;
    // The frame's upload buffer the same, room for the arena beside the rest.
    Uint32 transferRoom=vertexRoom?total-vertexBytes+vertexRoom:0;
    if(!frameTransfer||frameTransferCapacity<std::max(total,transferRoom)) {
        if(frameTransfer)SDL_ReleaseGPUTransferBuffer(device,frameTransfer);
        frameTransferCapacity=std::max<Uint32>({262144,total+total/2,transferRoom});
        SDL_GPUTransferBufferCreateInfo info={SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,frameTransferCapacity,0};
        frameTransfer=SDL_CreateGPUTransferBuffer(device,&info);
        if(!frameTransfer)I_Error((char*)"Could not allocate GPU frame upload: %s",SDL_GetError());
    }
    byte *mapped=(byte*)SDL_MapGPUTransferBuffer(device,frameTransfer,true);
    for(const auto &entry:vertexOffsets)
        memcpy(mapped+entry.second,entry.first->data(),entry.first->size()*sizeof(Vertex));
    memcpy(mapped+vertexBytes,lightWords.data(),blockerBytes);
    memcpy(mapped+vertexBytes+blockerBytes,sectors.data(),sectorBytes);
    memcpy(mapped+imageOffset,palette,1024);
    if(hudBytes)memcpy(mapped+imageOffset+1024,hud.data(),hudBytes);
    if(panelBytes)memcpy(mapped+imageOffset+1024+hudBytes,panel.data(),panelBytes);
    SDL_UnmapGPUTransferBuffer(device,frameTransfer);
    SDL_GPUCopyPass *copy=SDL_BeginGPUCopyPass(command);
    auto send=[&](Uint32 offset,SDL_GPUBuffer *buffer,Uint32 size) {
        SDL_GPUTransferBufferLocation source={frameTransfer,offset};
        SDL_GPUBufferRegion target={buffer,0,size};
        SDL_UploadToGPUBuffer(copy,&source,&target,true);
    };
    if(bytes)send(0,vertexArena.buffer,bytes);
    send(vertexBytes,blockerArena.buffer,blockerBytes);
    send(vertexBytes+blockerBytes,sectorArena.buffer,sectorBytes);
    auto image=[&](Uint32 offset,SDL_GPUTexture *texture,Uint32 w,Uint32 h) {
        SDL_GPUTextureTransferInfo source={frameTransfer,offset,w,h};
        SDL_GPUTextureRegion target={};target.texture=texture;target.w=w;target.h=h;target.d=1;
        SDL_UploadToGPUTexture(copy,&source,&target,true);
    };
    image(imageOffset,paletteTexture,256,1);
    if(hudBytes) {image(imageOffset+1024,hudTexture,SCREENWIDTH,SCREENHEIGHT);hudCurrent=true;}
    if(panelBytes)image(imageOffset+1024+hudBytes,settingsTexture,DOOM_SETTINGS_WIDTH,DOOM_SETTINGS_HEIGHT);
    SDL_EndGPUCopyPass(copy);
}
void drawVertices(SDL_GPURenderPass *pass,const std::vector<Vertex> &vertices) {
    if(vertices.empty())return;
    auto uploaded=vertexOffsets.find(&vertices);
    if(uploaded==vertexOffsets.end())return;
    SDL_GPUBufferBinding binding={vertexArena.buffer,uploaded->second};
    SDL_BindGPUVertexBuffers(pass,0,&binding,1);
    SDL_DrawGPUPrimitives(pass,(Uint32)vertices.size(),1,0,0);
}
// count of the vertices from first on (drawOpaque's quiet and other runs of a batch).
static void drawVertexRange(SDL_GPURenderPass *pass,const std::vector<Vertex> &vertices,size_t first,size_t count) {
    if(!count)return;
    auto uploaded=vertexOffsets.find(&vertices);
    if(uploaded==vertexOffsets.end())return;
    SDL_GPUBufferBinding binding={vertexArena.buffer,uploaded->second};
    SDL_BindGPUVertexBuffers(pass,0,&binding,1);
    SDL_DrawGPUPrimitives(pass,(Uint32)count,1,(Uint32)first,0);
}
void setView(SDL_GPURenderPass *pass,float x,float y,float w,float h,const SDL_Rect *scissor) {
    SDL_GPUViewport viewport={x,y,w,h,0,1};SDL_SetGPUViewport(pass,&viewport);
    SDL_Rect clip=scissor?*scissor:SDL_Rect{(int)x,(int)y,(int)w,(int)h};
    SDL_SetGPUScissor(pass,&clip);
}
// World fragment resources: image, palette, next frame, seams, contact,
// reflection, palette lookup, wall bake, flat light, caustic map and pattern,
// wall and flat light directions, the surface's screen glass frame, detail
// texture layers, the shore map; blockers and sector data;
// four uniform blocks.
// Bindings reset with every render pass, so each pass binds them again.
void bindWorld(SDL_GPURenderPass *pass,SDL_GPUTexture *reflection) {
    SDL_GPUTextureSamplerBinding samplers[]={
        {paletteTexture,nearestClamp},{seamTexture?gpu(seamTexture):dummySeam,nearestClamp},
        {contactTexture?gpu(contactTexture):dummyContact,linearClamp},{reflection?reflection:paletteTexture,linearClamp},
        {paletteLUT,nearestClamp},{wallBakeTexture?gpu(wallBakeTexture):dummyBake,linearClamp},
        {flatLightTexture?gpu(flatLightTexture):dummyBake,linearClamp},
        {causticTexture?gpu(causticTexture):dummyContact,linearClamp},{causticPattern?gpu(causticPattern):dummyContact,nearestClamp},
        // (The bake maps are read cell by cell with texelFetch, which no sampler touches; linear for
        // bake.glsl's fast path, F4 "Quick baked light reads".)
        {wallDirectionTexture?gpu(wallDirectionTexture):dummyBake,linearClamp},{flatDirectionTexture?gpu(flatDirectionTexture):dummyBake,linearClamp},
        {dummyBake,nearestClamp},{shoreTexture?gpu(shoreTexture):dummyContact,linearClamp},{dummyRelief,nearestClamp}};
    SDL_BindGPUFragmentSamplers(pass,1,&samplers[0],1);
    SDL_BindGPUFragmentSamplers(pass,3,&samplers[1],13);
    SDL_GPUBuffer *storage[]={blockerArena.buffer,sectorArena.buffer};
    SDL_BindGPUFragmentStorageBuffers(pass,0,storage,2);
}
// The bloom pass's glowing liquid edges: shore map, caustic pattern (holding
// the edge strip), sector heights and the camera.
void bindEmissionEdges(SDL_GPUCommandBuffer *command,SDL_GPURenderPass *pass,const Uniforms &camera) {
    SDL_GPUTextureSamplerBinding samplers[]={{shoreTexture?gpu(shoreTexture):dummyContact,linearClamp},
        {causticPattern?gpu(causticPattern):dummyContact,nearestClamp}};
    SDL_BindGPUFragmentSamplers(pass,3,samplers,2);
    SDL_BindGPUFragmentStorageBuffers(pass,0,&sectorArena.buffer,1);
    SDL_PushGPUFragmentUniformData(command,1,&camera,sizeof(camera));
}
void bindLightBlockers(SDL_GPURenderPass *pass) {SDL_BindGPUFragmentStorageBuffers(pass,0,&blockerArena.buffer,1);}
// Binds a batch texture with its next animation frame and crossfade amount;
// world passes also take its screen glass frame and bump relief.
void bindSurface(SDL_GPUCommandBuffer *command,SDL_GPURenderPass *pass,int key,Uint32 blendSlot,bool world) {
    bindSurface(command,pass,surfaceBinding(key),blendSlot,world);
}
void bindSurface(SDL_GPUCommandBuffer *command,SDL_GPURenderPass *pass,const SurfaceBinding &binding,Uint32 blendSlot,bool world) {
    SDL_GPUTextureSamplerBinding image={gpu(binding.image->texture),linearRepeat},next={gpu(binding.next->texture),linearRepeat};
    SDL_BindGPUFragmentSamplers(pass,0,&image,1);
    SDL_BindGPUFragmentSamplers(pass,2,&next,1);
    if(world) {
        SDL_GPUTextureSamplerBinding frame={binding.image->glassFrame?gpu(binding.image->glassFrame):dummyBake,nearestClamp};
        SDL_BindGPUFragmentSamplers(pass,13,&frame,1);
        SDL_GPUTextureSamplerBinding relief={binding.image->relief?gpu(binding.image->relief):dummyRelief,nearestClamp};
        SDL_BindGPUFragmentSamplers(pass,15,&relief,1);
    }
    // The crossfade, the weapon's surroundings (unused here), then the
    // liquid's frame and cycle averages.
    float blend[44]={binding.blend};
    std::copy(binding.frameMean.begin(),binding.frameMean.end(),blend+28);
    std::copy(binding.cycleMean.begin(),binding.cycleMean.end(),blend+32);
    std::copy(binding.ripple.begin(),binding.ripple.end(),blend+36);
    std::copy(binding.rippleShape.begin(),binding.rippleShape.end(),blend+40);
    SDL_PushGPUFragmentUniformData(command,blendSlot,blend,world?sizeof(blend):4*sizeof(float));
}
void bindImage(SDL_GPURenderPass *pass,const GpuTextureRef &texture,SDL_GPUSampler *filter) {
    SDL_GPUTextureSamplerBinding binding={gpu(texture),filter};
    SDL_BindGPUFragmentSamplers(pass,0,&binding,1);
}
// The view last pushed has no weather air, haze, beam in the air or fog bank (world.frag's
// HAS_AIR tests): drawOpaque may then take the CLEAR_AIR variants.
static bool clearAirView=false;
// Nor weather, heat, a sun shower (its rainbow), beam in the air or fog bank for the sky
// (sky.frag's HAS_SKY_AIR tests): drawWorld may then take the live sky's SKY_CLEAR twin.
static bool clearSkyView=false;
void pushWorldUniforms(SDL_GPUCommandBuffer *command,const Uniforms &view) {
    clearAirView=view.climate[2]<=0&&view.haze[3]<=0&&view.beamLight[3]<=0&&view.bank[3]<=0;
    clearSkyView=view.weather[0]<=0&&view.climate[0]<=0&&view.climate[2]<=0&&view.beamLight[3]<=0&&view.bank[3]<=0&&view.skyState[3]<=0;
    float noBlend[44]={};
    SDL_PushGPUVertexUniformData(command,0,&view,sizeof(view));
    SDL_PushGPUFragmentUniformData(command,0,&view,sizeof(view));
    SDL_PushGPUFragmentUniformData(command,1,&gpuFlashes,sizeof(gpuFlashes));
    SDL_PushGPUFragmentUniformData(command,2,&fogLights,sizeof(fogLights));
    SDL_PushGPUFragmentUniformData(command,3,noBlend,sizeof(noBlend));
}
bool opaqueBatch(const std::pair<const int,std::vector<Vertex>> &batch) {
    return !batch.second.empty()&&!(batch.second.front().mode&MODE_SPRITE)&&images.at(batch.first).opaque;
}
// world.frag's opaque variants (PLAN-metal-shader-split.md) each leave out code, and with it the
// registers that code held: PLAIN what only some surfaces need, and PLAIN's WALLS and FLATS the
// other shape's half as well (the opaque world from 60 to about 20 ms on the Mac). A batch takes
// PLAIN when none of that code would do anything on it: no screen glass or LED readouts
// (glassFrame, screens), no stained glass, not a pool (cycleMean.a) or shore ripples, and no
// vertex of blood, a mirroring floor or an ice sheet (vTint.r on a flat); and PLAIN's walls or
// flats when every triangle of it stands upright or lies level. When unsure, the bigger shader:
// that only costs time. The vertices are looked at once a frame (batchGeneration).
enum FullReason { plainOk, fullScreens, fullPool, fullBlood, fullMirror, fullStained, fullIce };
enum BatchShape { shapeMixed, shapeWalls, shapeFlats };
// Upright (seen from above, a triangle's corners in a line: a wall) or level (one height: a floor
// or ceiling), triangle by triangle; anything else, or both kinds, mixed.
static int batchShape(const std::vector<Vertex> &v) {
    if(v.size()%3)return shapeMixed;
    bool walls=true,flats=true;
    for(size_t n=0;n+2<v.size()&&(walls||flats);n+=3) {
        const Vertex &a=v[n],&b=v[n+1],&c=v[n+2];
        if(a.z!=b.z||b.z!=c.z)flats=false;
        float ux=b.x-a.x,uy=b.y-a.y,wx=c.x-a.x,wy=c.y-a.y,size=std::abs(ux)+std::abs(uy)+std::abs(wx)+std::abs(wy);
        if(std::abs(ux*wy-uy*wx)>1e-4f*size*size)walls=false;
    }
    return flats?shapeFlats:walls?shapeWalls:shapeMixed;
}
struct BatchClass { int reason,shape; };
static BatchClass batchClass(int key,const std::vector<Vertex> &vertices,const SurfaceBinding &binding) {
    struct Seen { unsigned generation; size_t count; int reason,shape; };
    static std::unordered_map<int,Seen> seen;
    Seen &known=seen[key];
    if(!known.generation||known.generation!=batchGeneration||known.count!=vertices.size()) {
        int reason=plainOk;
        for(const Vertex &v:vertices) {
            if(v.mode&MODE_BLOOD) {reason=fullBlood;break;}
            if(v.mode&MODE_MIRROR_FLOOR) {reason=fullMirror;break;}
            if(v.mode&MODE_STAINED_GLASS) {reason=fullStained;break;}
            // An ice sheet (world.frag's iceSheet: vTint.r on a floor) lies in a flat's batch (negative
            // key, geometry.cpp); walls keep their light atlas strip in red (bake_atlas.cpp), and no
            // wall faces up.
            if(v.red>0&&key<0) {reason=fullIce;break;}
        }
        known={batchGeneration,vertices.size(),reason,batchShape(vertices)};
    }
    const Image &image=*binding.image;
    int reason=image.glassFrame||!image.screens.empty()?fullScreens:image.leaded?fullStained:
        binding.cycleMean[3]>0||binding.ripple[0]!=0?fullPool:known.reason;
#ifdef DOOM_DEVTOOLS
    // DERP_POOLPLAIN=1: pools through PLAIN (a wrong picture on them: what the liquid surface costs).
    static const bool poolPlain=getenv("DERP_POOLPLAIN")!=nullptr;
    if(poolPlain&&reason==fullPool)reason=known.reason;
#endif
    return {reason,known.shape};
}
// Each opaque batch that takes a walls or flats variant, its quiet triangles first: no dynamic
// light on any corner (vLightMask) and nowhere near a liquid (nearLiquid, caustics.cpp), where
// world.frag's QUIET variant draws them alike. Each side keeps its own order. drawOpaque then
// draws the two runs with two variants. Once a frame, before the upload.
static std::unordered_map<const std::vector<Vertex>*,size_t> quietRun;
static void sortQuiet() {
    quietRun.clear();
#ifdef DOOM_DEVTOOLS
    static const bool loudOnly=getenv("DERP_OPAQUELOUD")!=nullptr;   // no quiet runs, no clear air
    if(loudOnly)return;
    // DERP_NEARLIQUIDOFF=1: nothing counts as near a liquid (a wrong picture by shores: the most a
    // finer nearLiquid could win).
    static const bool shoreBlind=getenv("DERP_NEARLIQUIDOFF")!=nullptr;
#else
    const bool shoreBlind=false;
#endif
    static std::vector<Vertex> loud;
    static std::vector<char> quietOf;
    for(auto &batch:batches) if(opaqueBatch(batch)) {
        BatchClass is=batchClass(batch.first,batch.second,surfaceBinding(batch.first));
        if(is.reason!=plainOk||is.shape==shapeMixed)continue;
        std::vector<Vertex> &v=batch.second;
        // Each triangle's class first: a batch all quiet, all loud or already in order stays put.
        quietOf.resize(v.size()/3);
        size_t quietCount=0;bool ordered=true;
        for(size_t n=0;n+2<v.size();n+=3) {
            const Vertex *t=&v[n];
            bool lit=(t[0].lightMask[0]|t[0].lightMask[1]|t[1].lightMask[0]|t[1].lightMask[1]|t[2].lightMask[0]|t[2].lightMask[1])!=0;
            bool quiet=!lit&&(shoreBlind||!nearLiquid(std::min({t[0].x,t[1].x,t[2].x}),std::min({t[0].y,t[1].y,t[2].y}),
                                         std::max({t[0].x,t[1].x,t[2].x}),std::max({t[0].y,t[1].y,t[2].y})));
            quietOf[n/3]=quiet;
            if(quiet) {ordered=ordered&&quietCount==n/3;++quietCount;}
        }
        quietRun[&v]=quietCount*3;
        if(ordered)continue;
        loud.clear();size_t kept=0;
        for(size_t n=0;n+2<v.size();n+=3) {
            const Vertex *t=&v[n];
            if(!quietOf[n/3]) {loud.insert(loud.end(),t,t+3);continue;}
            if(kept!=n)std::copy(t,t+3,v.begin()+kept);
            kept+=3;
        }
        std::copy(loud.begin(),loud.end(),v.begin()+kept);
    }
}
// Which of world.frag's opaque variants pipelines full's kind (main view, reflection, shadowed,
// shadow factor), -1 for none.
static int variantKind(SDL_GPUGraphicsPipeline *full) {
    if(full==opaquePipeline)return 0;
    if(full==reflectOpaquePipeline)return 1;
    if(full==shadowedPipeline)return 2;
    if(full==shadowFactorPipeline)return 3;
    return -1;
}
void drawOpaque(SDL_GPUCommandBuffer *command,SDL_GPURenderPass *pass,SDL_GPUGraphicsPipeline *full,SDL_GPUTexture *reflection) {
    int kind=variantKind(full);
    SDL_GPUGraphicsPipeline *bound=nullptr;
    bool clearAir=clearAirView;
#ifdef DOOM_DEVTOOLS
    // DERP_OPAQUEFULL=1: every batch through the full shader, to hold the picture against the
    // variants'; DERP_OPAQUENOSHAPE=1: PLAIN without its walls and flats; DERP_OPAQUELOUD=1:
    // walls and flats without the quiet and clear-air variants.
    static const bool fullOnly=getenv("DERP_OPAQUEFULL")!=nullptr,noShape=getenv("DERP_OPAQUENOSHAPE")!=nullptr,
        loudOnly=getenv("DERP_OPAQUELOUD")!=nullptr;
    if(fullOnly)kind=-1;
    if(loudOnly)clearAir=false;
    // DERP_OPAQUECLASS=1: once, on the main view's 300th frame, each opaque batch's class and why.
    static const bool report=getenv("DERP_OPAQUECLASS")!=nullptr;
    if(report&&full==opaquePipeline&&batchGeneration==300) {
        static const char *why[]={"plain","screens","pool","blood","mirror","stained","ice"},*shapes[]={"mixed","walls","flats"};
        for(const auto &batch:batches) if(opaqueBatch(batch)) {
            BatchClass is=batchClass(batch.first,batch.second,surfaceBinding(batch.first));
            auto quiet=quietRun.find(&batch.second);
            char name[9]={};if(batch.first>=0)R_TextureName(batch.first,name);
            fprintf(stderr,"opaque class: key %d %-8s %6zu vertices %s %s, %zu quiet, clear air %d\n",batch.first,name,batch.second.size(),
                    why[is.reason],shapes[is.shape],quiet==quietRun.end()?(size_t)0:quiet->second,(int)clearAir);
        }
    }
#else
    const bool noShape=false;
#endif
    auto use=[&](SDL_GPUGraphicsPipeline *want) {
        if(want!=bound) {SDL_BindGPUGraphicsPipeline(pass,want);bindWorld(pass,reflection);bound=want;}
    };
    // A variant still being built (buildVariants, pipelines.cpp) is PLAIN for now: the same picture.
    auto variantOr=[&](int variant) {
        SDL_GPUGraphicsPipeline *ready=opaqueVariants[kind][variant].load();
        if(!ready)ready=opaqueVariants[kind][opaquePlain].load();
        return ready?ready:full;
    };
    // In the batches' own order, switching only where the variant changes, so coplanar surfaces
    // settle their depth ties as before.
    for(const auto &batch:batches) if(opaqueBatch(batch)) {
        SurfaceBinding binding=surfaceBinding(batch.first);
        BatchClass is=kind>=0?batchClass(batch.first,batch.second,binding):BatchClass{fullScreens,shapeMixed};
        if(is.reason!=plainOk) {use(full);bindSurface(command,pass,binding,3);drawVertices(pass,batch.second);continue;}
        if(noShape||is.shape==shapeMixed) {
            use(variantOr(opaquePlain));bindSurface(command,pass,binding,3);drawVertices(pass,batch.second);continue;
        }
        // Walls or flats: the quiet run (sortQuiet) with QUIET, the rest without; CLEAR_AIR for both
        // when the view has no air to draw.
        int variant=(is.shape==shapeWalls?opaquePlainWalls:opaquePlainFlats)+(clearAir?2:0);
        auto run=quietRun.find(&batch.second);
        size_t quiet=run==quietRun.end()?0:run->second,count=batch.second.size();
        if(quiet) {use(variantOr(variant+1));bindSurface(command,pass,binding,3);drawVertexRange(pass,batch.second,0,quiet);}
        if(quiet<count) {use(variantOr(variant));bindSurface(command,pass,binding,3);drawVertexRange(pass,batch.second,quiet,count-quiet);}
    }
}
// The live sky's pipeline, or its twin built for this level's kind of sky under clear air: on
// an M2 the far weather and the kinds a level doesn't have held the live sky at 44 ms where the
// twin draws E1's in 25. Neither alone gets there (43 and 33-39): they fill the same registers.
// The kinds DOOM's skies come in (DERP_SKYKIND): a cloud field alone (E1-E3), the field with a
// cloud deck (E4, MAP01-11), a deck alone (TNT, Plutonia); a city, hell, spire or trees keep the
// whole shader. Built in the background; the whole shader until ready.
static SDL_GPUGraphicsPipeline *skyTwin(SDL_GPUGraphicsPipeline *live) {
#ifdef DOOM_DEVTOOLS
    static const bool full=getenv("DERP_SKYFULL")!=nullptr;   // the whole sky shader, to compare against
    if(full)return live;
#endif
    if(!clearSkyView)return live;
    int twin=live==skyPipeline?skyTwinMain:live==skySurfacePipeline?skyTwinSurface:
             live==reflectSkyPipeline?skyTwinReflect:live==reflectSkySurfacePipeline?skyTwinReflectSurface:-1;
    if(twin<0)return live;
    SkyKind kind=skyKind();
    if(!kind.valid||kind.city||kind.hell==1||kind.hell==3||kind.spire||kind.tree)return live;
    int variant=kind.field?(kind.cloud?skyFieldCloud:skyField):kind.cloud?skyCloud:-1;
    SDL_GPUGraphicsPipeline *ready=variant>=0?skyClearPipelines[variant][twin].load():nullptr;
    return ready?ready:live;
}
// The world drawn opaque-first, then sky surfaces into depth and the sky into
// uncovered pixels, then cutouts and sprites. Shared by the main view and the
// liquid reflection. With coverage and cutout pipelines, masked walls first
// write their depth, after the sky so their soft edges still blend over it,
// and then shade only where they are frontmost. Without sprites (glass
// probes), only the map itself.
void skyImages(SDL_GPUTextureSamplerBinding images[6]) {
    const SDL_GPUTextureSamplerBinding all[6]={{gpu(doom3d::skyImage()),linearRepeat},{paletteTexture,nearestClamp},
                                               {gpu(doom3d::skyLayersImage()),nearestClamp},{gpu(doom3d::skyLayerLight()),nearestClamp},
                                               {gpu(doom3d::skyCloudField()),linearRepeat},{gpu(doom3d::skyCloudNoise()),linearRepeat}};
    std::copy(all,all+6,images);
}
bool skyOnlyView() {
#ifdef DOOM_DEVTOOLS
    static const bool on=getenv("DERP_SKYONLY")!=nullptr;
    return on;
#else
    return false;
#endif
}
void drawWorld(SDL_GPUCommandBuffer *command,SDL_GPURenderPass *pass,SDL_GPUGraphicsPipeline *opaque,SDL_GPUGraphicsPipeline *skySurface,
               SDL_GPUGraphicsPipeline *sky,SDL_GPUGraphicsPipeline *world,SDL_GPUTexture *reflection,
               SDL_GPUGraphicsPipeline *coverage,SDL_GPUGraphicsPipeline *cutout,bool sprites,const PieceCut *cut) {
    auto cutouts=[&](SDL_GPUGraphicsPipeline *pipeline) {
        SDL_BindGPUGraphicsPipeline(pass,pipeline);bindWorld(pass,reflection);
        for(const auto &batch:batches) {
            if(batch.second.empty()||(batch.second.front().mode&MODE_SPRITE)||images.at(batch.first).opaque)continue;
            bindSurface(command,pass,batch.first,3);drawVertices(pass,batch.second);
        }
    };
    // A dev view of the sky alone (DERP_SKYONLY): no world, the whole screen sky.
    if(skyOnlyView()) {
        SDL_GPUTextureSamplerBinding skyImage[7];skyImages(skyImage);
        if(sky==skyCachedPipeline)skyImage[6]=skyCacheImage();
        SDL_BindGPUGraphicsPipeline(pass,sky);SDL_BindGPUFragmentSamplers(pass,0,skyImage,sky==skyCachedPipeline?7:6);
        SDL_DrawGPUPrimitives(pass,3,1,0,0);
        return;
    }
    // Where a masked wall covers wholly, its depth first: the opaque world behind a
    // grate's bars is then left unshaded (world.frag's CORE).
    if(coverage&&cutout&&corePipeline)cutouts(corePipeline);
    drawOpaque(command,pass,opaque,reflection);
    if(cut)(*cut)(I_RENDER3D_PIECE_OPAQUE,command,pass);
    SDL_GPUTextureSamplerBinding skyImage[7];skyImages(skyImage);
    Uint32 skySamplers=6;
    if(sky==skyCachedPipeline) {skyImage[6]=skyCacheImage();skySamplers=7;}
    if(!skyVertices.empty()) {
        SDL_BindGPUGraphicsPipeline(pass,skyTwin(skySurface));SDL_BindGPUFragmentSamplers(pass,0,skyImage,skySamplers);
        drawVertices(pass,skyVertices);
    }
    // Uncovered pixels are the void behind the map, not the sky: the sky is the
    // sky surfaces above (ceilings, floors, the strips over walls), and of the
    // view only seams and the gaps under walls stay uncovered (measured on E1M1's
    // courtyard and MAP02's yard: none of the sky came from here). Drawn as sky,
    // every seam in a dark room shone with the daylight, burnt white by the open
    // eye, and the water mirrored it at its rims (MAP02's sewer). So the void
    // keeps the clear color, as on maps without a sky; only a level's far fog
    // (DERPINFO farfog) fills it, being all there is behind the world.
    if(farFogActive) {
        SDL_BindGPUGraphicsPipeline(pass,skyTwin(sky));
        SDL_BindGPUFragmentSamplers(pass,0,skyImage,skySamplers);
        SDL_DrawGPUPrimitives(pass,3,1,0,0);
    }
    // (Without any sky the piece is empty, and not cut.)
    if(cut&&(!skyVertices.empty()||farFogActive))(*cut)(I_RENDER3D_PIECE_SKY,command,pass);
    // (Timed apart: a grate's depth, its shading, and the sprites after them.)
    if(coverage&&cutout) {
        cutouts(coverage);
        if(cut)(*cut)(I_RENDER3D_PIECE_MASKED_DEPTH,command,pass);
        cutouts(cutout);
        if(cut)(*cut)(I_RENDER3D_PIECE_MASKED,command,pass);
    }
    else cutouts(world);
    if(sprites) {SDL_BindGPUGraphicsPipeline(pass,world);bindWorld(pass,reflection);}
    for(const auto &batch:batches) {
        if(!sprites||batch.second.empty()||!(batch.second.front().mode&MODE_SPRITE))continue;
        bindSurface(command,pass,batch.first,3);drawVertices(pass,batch.second);
    }
    float noBlend[44]={};SDL_PushGPUFragmentUniformData(command,3,noBlend,sizeof(noBlend));
}
void drawOverlays(SDL_GPUCommandBuffer *command,SDL_GPURenderPass *pass,const FrameView &view,
                  const std::vector<SpriteDraw> &weapons,const std::vector<Vertex> &cross,bool depth) {
    SDL_Rect full={0,0,width,height};
    setView(pass,view.uiX,view.uiY,view.uiWidth,view.uiHeight,&full);
    if(worldPending&&!weapons.empty()) {
        SDL_BindGPUGraphicsPipeline(pass,depth?weaponPipeline:overlayWeaponPipeline);bindWorld(pass,nullptr);
        // No crossfade, then what the weapon sees around it.
        float blend[44]={};std::copy(weaponProbe,weaponProbe+24,blend+4);
        SDL_PushGPUFragmentUniformData(command,3,blend,sizeof(blend));
        for(const auto &draw:weapons) {
            SDL_GPUTextureSamplerBinding image[]={{gpu(draw.image->texture),linearRepeat},{paletteTexture,nearestClamp},{gpu(draw.image->texture),linearRepeat}};
            SDL_BindGPUFragmentSamplers(pass,0,image,3);drawVertices(pass,draw.vertices);
        }
    }
    if(!cross.empty()) {
        setView(pass,view.worldX,view.worldY,view.worldW,view.worldH,&full);
        SDL_BindGPUGraphicsPipeline(pass,depth?solidPipeline:overlaySolidPipeline);drawVertices(pass,cross);
    }
}
void releaseFrameUploads() {
    for(Arena *a:{&vertexArena,&blockerArena,&sectorArena}) {release(a->buffer,SDL_ReleaseGPUBuffer);a->capacity=0;}
    release(frameTransfer,SDL_ReleaseGPUTransferBuffer);frameTransferCapacity=0;vertexOffsets.clear();
}
} // namespace gpu3d
