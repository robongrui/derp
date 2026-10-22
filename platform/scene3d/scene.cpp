/* The scene's public state, its lifecycle and the per-frame entry points
   (prepareFrame), plus the backend-independent I_Render3D calls. */
#include "internal.h"
#include "../shaders/feature_flags.h"
extern "C" {
#include "../audio/sound_options.h"
}
#include "../settings_panel.h"

namespace doom3d {
Profile profile;
Settings settings;
SDL_Window *gameWindow;
FlashSet flashes;
FogLights fogLights;
std::vector<LightBlocker> lightBlockers;
std::unordered_map<int,Image> images;
std::map<int,std::vector<Vertex>> batches,shadowBatches,decalBatches,mirrorBatches;
std::vector<Vertex> mistVertices,particleVertices,crestVertices,heatVertices,skyVertices,shaftVertices;
GpuTextureRef seamTexture,contactTexture,cloudTexture,wallBakeTexture,flatLightTexture,causticTexture,causticPattern,shoreTexture;
GpuTextureRef wallDirectionTexture,flatDirectionTexture;
float reflectionPlane=0;
bool reflectionActive=false;
bool worldPending,screenshotPending,flashlightOn=false;
float pitch;
unsigned levelSerial;
static float oldEye[3], oldYaw;

std::vector<std::array<float,2>> oldHeights;
float renderFraction=1;
uint64_t tickTime;
static uint64_t titleTime;
static int frameCount;
static float fps;
static std::unordered_map<const mobj_t*, std::array<float,3>> oldThings;
void interpolatedPosition(const mobj_t &thing,float fraction,float &x,float &y,float &z) {
    x=units(thing.x);y=units(thing.y);z=units(thing.z);
    auto old=oldThings.find(&thing);
    if(old!=oldThings.end()&&std::hypot(x-old->second[0],y-old->second[1])<128) {
        x=old->second[0]+(x-old->second[0])*fraction;
        y=old->second[1]+(y-old->second[1])*fraction;
        z=old->second[2]+(z-old->second[2])*fraction;
    }
}
// -renderview x y angle [pitch] (map units, degrees; pitch up, needs free
// look) puts the player there once per level, to aim -rendercheck frames.
extern "C" boolean P_TeleportMove(mobj_t *thing,fixed_t x,fixed_t y);
static void renderViewPlace(player_t &player) {
    static unsigned placed=~0u;
    int view=M_CheckParm((char*)"-renderview");
    if(!view||view+3>=myargc||placed==r_levelserial)return;
    placed=r_levelserial;
    mobj_t *mo=player.mo;
    P_TeleportMove(mo,(fixed_t)(atof(myargv[view+1])*FRACUNIT),(fixed_t)(atof(myargv[view+2])*FRACUNIT));
    mo->z=mo->floorz;mo->angle=(angle_t)(int64_t)(atof(myargv[view+3])/360.0*4294967296.0);
    player.viewz=mo->z+player.viewheight;
    if(view+4<myargc&&myargv[view+4][0]!='-')pitch=std::clamp((float)atof(myargv[view+4])*(float)doomPi/180,-1.2f,1.2f);
}
static void beforeTic() {
    if(gamestate!=GS_LEVEL||!players[displayplayer].mo||paused) {tickTime=0;return;}
    renderViewPlace(players[displayplayer]);
    mobj_t *mo=players[displayplayer].mo;
    oldEye[0]=units(mo->x);oldEye[1]=units(mo->y);oldEye[2]=units(players[displayplayer].viewz);
    oldYaw=(float)(mo->angle*(2.0*doomPi/4294967296.0));tickTime=SDL_GetTicksNS();
    oldThings.clear();
    oldHeights.resize(numsectors);
    for(int i=0;i<numsectors;++i) oldHeights[i]={units(sectors[i].floorheight),units(sectors[i].ceilingheight)};
    for(int i=0;i<numsectors;++i) for(mobj_t *thing=sectors[i].thinglist;thing;thing=thing->snext)
        oldThings.emplace(thing,std::array<float,3>{units(thing->x),units(thing->y),units(thing->z)});
    watchSplashes();
    ragdollTestTic();
    watchBodies();
    watchSpray();
    watchGlass();
    watchWeather();
    watchWear();
    watchShotLights();
}
static void acceleratedView(player_t *player) {
    (void)player; worldPending=true; v_overlayactive=true;
    memset(v_overlaymask,255,sizeof(v_overlaymask));
    for(int y=viewwindowy;y<viewwindowy+viewheight;++y) {
        memset(screens[0]+y*SCREENWIDTH+viewwindowx,0,scaledviewwidth);
        memset(v_overlaymask+y*SCREENWIDTH+viewwindowx,0,scaledviewwidth);
    }
}
void applySettings() {
    fitGame(settings);
    R_AcceleratedView=settings.accelerated?acceleratedView:nullptr;
    R_BeforeTic=settings.accelerated?beforeTic:nullptr;
    R_MuzzleFlash=settings.accelerated?muzzleFlash:nullptr;
    R_WallImpact=settings.accelerated?wallImpact:nullptr;
    R_BloodHit=settings.accelerated?bloodHit:nullptr;
    R_DamageHit=settings.accelerated?bodyDamaged:nullptr;
    R_PlaneImpact=settings.accelerated?planeImpact:nullptr;
    R_ShotPassed=settings.accelerated?glassShot:nullptr;
    freeaim=settings.accelerated&&settings.look;
    classicaim=settings.classicAim!=0;
    if(!settings.accelerated) clearFlashPulses();
    r_uncapped=settings.accelerated;
    if(!settings.look) pitch=0;
    R_SetViewSize(10,0);
    I_SetSoundOptions(settings.roomAcoustics,settings.reverbAmount,settings.muffleWalls,settings.muffleDistance,settings.naturalFalloff);
    I_SetSfxPlayback(settings.sfxResampling,settings.sfxTreble);
    I_SetMadeSounds(settings.madeImpacts,settings.glassSound,settings.ambience,settings.ambienceAmount,settings.footsteps);
    I_SetAlternativeComputerSounds(settings.alternativeComputerSounds);
    I_AmbienceShowGlass(settings.accelerated&&settings.windowGlass);
    I_SetMusicDevice(settings.musicDevice);
    // The authored episode's mood is baked into the sun, lamps, bounce and
    // sky light (levelMood): switching it re-bakes them all.
    static int bakedAuthored=-1;
    if(bakedAuthored>=0&&bakedAuthored!=settings.authored) {sunBaked=false;lightBakeKey=-1;bounceKey=-1;ambientKey=-1;flowKey=-1;}
    bakedAuthored=settings.authored;
}
void sceneInit(SDL_Window *window) {
    gameWindow=window;
    int config=M_CheckParm((char*)"-graphics");
    if(config&&config+1<myargc)graphicsConfig=myargv[config+1];
    loadSettings();if(M_CheckParm((char*)"-classic"))settings.accelerated=0;
    if(M_CheckParm((char*)"-rendercheck"))settings.accelerated=1;
    int fov=M_CheckParm((char*)"-fov");if(fov&&fov+1<myargc)settings.fov=std::clamp((float)atof(myargv[fov+1]),60.0f,120.0f);
    flashlightOn=M_CheckParm((char*)"-flashlight")!=0&&Game_HasEffect(GAME_EFFECT_FLASHLIGHT);
    int tintOption=M_CheckParm((char*)"-flashlighttint");
    if(tintOption&&tintOption+1<myargc)
        settings.flashlightTintGain=std::clamp((float)atof(myargv[tintOption+1]),0.0f,2.0f);
    int fogOption=M_CheckParm((char*)"-fog");
    if(fogOption&&fogOption+1<myargc)settings.fog=atoi(myargv[fogOption+1])!=0;
    int paletteOption=M_CheckParm((char*)"-palette");
    if(paletteOption&&paletteOption+1<myargc)settings.palette=atoi(myargv[paletteOption+1])!=0;
}
void sceneShutdown() {
    stopRelight();
    R_AcceleratedView=nullptr;R_BeforeTic=nullptr;R_MuzzleFlash=nullptr;R_WallImpact=nullptr;R_BloodHit=nullptr;R_DamageHit=nullptr;R_PlaneImpact=nullptr;R_ShotPassed=nullptr;r_uncapped=0;v_overlayactive=0;
    lightBlockers.clear();flashes={};oldThings.clear();oldHeights.clear();
    batches.clear();shadowBatches.clear();decalBatches.clear();mirrorBatches.clear();particleVertices.clear();
    mistVertices.clear();skyVertices.clear();shaftVertices.clear();heatVertices.clear();
    imagesShutdown();mapShutdown();causticsShutdown();clearSurfaceLights();lightsShutdown();glassLightShutdown();exposureShutdown();
    bakeAtlasShutdown();sunShutdown();lightBakeShutdown();clearStaticGrid();indirectShutdown();
    impactsShutdown();bloodShutdown();splashesShutdown();bodyMotionShutdown();glassShutdown();shotLightsShutdown();
    weatherShutdown();wearShutdown();
}
// A new level, or the first after shutdown: each part forgets the last
// level, then the map and what is built from it at level load.
static void startLevel() {
    loadingStage(LoadStage::Map);
    stopRelight();
    batches.clear();shadowBatches.clear();mirrorBatches.clear();clearSurfaceLights();shotLightsShutdown();
    bakeAtlasNewLevel();sunNewLevel();lightBakeNewLevel();clearStaticGrid();indirectNewLevel();
    oldThings.clear();oldHeights.clear();tickTime=0;
    buildMap();
    buildCaustics();
    buildShore();
    layoutBake();
    findDoors();
    prepareFalls();
    prepareRagdolls();
    levelSerial=r_levelserial;
}
static bool levelPending() {return levelSerial!=r_levelserial||floors.empty();}
bool bakesPending() {
    return (settings.bakedLights&&lightBakeKey!=lightBakeInputs())||(settings.bounce&&bounceKey!=bounceInputs())||
        ((settings.skyLight||settings.bakedAO)&&ambientKey!=bakeThingsKey)||(settings.lightFlow&&flowKey!=flowInputs())||(settings.sun&&!sunBaked);
}
bool loadPending() {return worldPending&&(levelPending()||settings.thingShadows!=bakeThingsKey||bakesPending());}
// What this frame builds and bakes, for the loading bar. A new level resets
// every bake, so each one switched on runs.
static void planStages(bool level) {
    bool lights=settings.bakedLights&&(level||lightBakeKey!=lightBakeInputs());
    planLoading(LoadStage::Map,level);planLoading(LoadStage::Textures,level);
    planLoading(LoadStage::Sun,settings.sun&&(level||!sunBaked));
    planLoading(LoadStage::Lights,lights);
    planLoading(LoadStage::Grid,settings.bakedLights&&settings.gridSpriteLight&&(lights||staticSerial!=lightBakeSerial));
    planLoading(LoadStage::Bounce,settings.bounce&&(level||lights||bounceKey!=bounceInputs()));
    planLoading(LoadStage::Ambient,(settings.skyLight||settings.bakedAO)&&(level||ambientKey!=bakeThingsKey));
    planLoading(LoadStage::Flow,settings.lightFlow&&(level||flowKey!=flowInputs()));
}
// What frameMean's alpha tells surfaceLight about a texture's material:
// 0 DOOM's palette ramps (3, stone, for Game_MatteTexture), 1 ice
// (iceFlat); with Game_SurfaceGloss, 2 a sprite (judged by colour alone),
// else 3 + its game_surface; fabric (9) takes materialGloss's matte
// default. Worn plates (wornPlate) add 16.
static_assert(GAME_SURFACE_STONE==0&&GAME_SURFACE_METAL==1&&GAME_SURFACE_WOOD==2&&GAME_SURFACE_FLESH==3&&GAME_SURFACE_SOIL==4&&GAME_SURFACE_LIQUID==5&&GAME_SURFACE_FABRIC==6,
              "materialGloss in world/surface.glsl numbers the surfaces");
static float surfaceMaterial(int key,int pic,bool flat) {
    if(flat&&iceFlat(pic))return 1;
    static std::unordered_map<int,float> known;
    auto found=known.find(key);
    if(found!=known.end())return found->second;
    char name[9]={};
    if(key>=0)R_TextureName(key,name);
    else if(flat)memcpy(name,lumpinfo[firstflat+pic].name,8);
    float material=2;
    if(!Game_SurfaceGloss())material=name[0]&&Game_MatteTexture(name,flat)?3+(float)GAME_SURFACE_STONE:0;
    else if(key>=0)material=3+(float)Game_WallSurface(name);
    else if(flat)material=3+(float)Game_FlatSurface(name);
    return known[key]=material;
}
// Plain metal plates that use has worn (Game_WornPlate; world/wear.glsl plateGrime).
static bool wornPlate(int key,int pic,bool flat) {
    static std::unordered_map<int,bool> known;
    auto found=known.find(key);
    if(found!=known.end())return found->second;
    char name[9]={};
    if(key>=0)R_TextureName(key,name);
    else if(flat)memcpy(name,lumpinfo[firstflat+pic].name,8);
    return known[key]=name[0]&&Game_WornPlate(name,flat);
}
// Binds a batch texture with its next animation frame and crossfade amount.
// The engine switches frames before leveltime++, so the phase uses leveltime-1.
SurfaceBinding surfaceBinding(int key) {
    Image &image=images.at(key);
    const Image *next=&image;float blend=0;
    int pic=key>=0?key:-1-key-firstflat,nextPic=0;
    bool flat=key<0&&pic>=0&&pic<numflats;
    int speed=key>=0||flat?P_PicAnimationNext(key>=0,pic,&nextPic):0;
    if((key>=0||flat)&&leveltime>0) {
        if(speed>0) {
            const Image &candidate=key>=0?wallImage(nextPic):lumpImage(firstflat+nextPic,true);
            if(candidate.width==image.width&&candidate.height==image.height) {
                next=&candidate;blend=((leveltime-1)%speed+renderFraction)/speed;
            }
        }
    }
    SurfaceBinding binding={&image,next,blend};
    binding.frameMean[3]=surfaceMaterial(key,pic,flat)+(settings.wornPlates&&wornPlate(key,pic,flat)?16:0);
    float floor=flat?liquidDetailFloor(pic):0;
    if(floor>0) {
        // The frames' average at the crossfade, and the average over the
        // whole cycle, which stays put as the frames change.
        const auto &a=averageColor(image),&b=averageColor(const_cast<Image&>(*next));
        std::array<float,3> cycle={};int frames=0;
        for(int at=pic,n=0;n<64;++n) {
            const auto &mean=averageColor(lumpImage(firstflat+at,true));
            for(int c=0;c<3;++c)cycle[c]+=mean[c];
            ++frames;int following;
            if(P_PicAnimationNext(false,at,&following)<=0||following==pic)break;
            at=following;
        }
        for(int c=0;c<3;++c) {
            binding.frameMean[c]=a[c]+(b[c]-a[c])*blend;
            binding.cycleMean[c]=cycle[c]/frames;
        }
        binding.cycleMean[3]=floor;
    }
    return binding;
}
void screenQuad(std::vector<Vertex> &out,float x,float y,float w,float h,float u0,float v0,float u1,float v1,float light,unsigned mode) {
    quad(out,{x,y,0,u0,v0,light,mode},{x+w,y,0,u1,v0,light,mode},
        {x+w,y-h,0,u1,v1,light,mode},{x,y-h,0,u0,v1,light,mode});
}
// Profiling: this frame's lights, blockers and how many triangles they reach.
static void recordProfile() {
    I_Render3DFrameProfile &frame=profile.frame;
    frame.world=worldPending;frame.reflection=reflectionActive;frame.lights=(int)flashes.count;
    for(unsigned i=0;i<flashes.count;++i) {
        const Flash &f=flashes.lights[i];
        const mobj_t *source=lightSources[i];
        frame.light[i]={f.position[0],f.position[1],f.position[2],f.position[3],lightKinds[i],source?(int)source->type:-1,(int)f.count,0,lightSliceCost[i]};
        frame.blockers+=(int)f.count;
    }
    for(const auto &batch:batches) for(size_t n=0;n+2<batch.second.size();n+=3) {
        const unsigned *mask=batch.second[n].lightMask;
        ++frame.triangles;
        if(mask[0]|mask[1])++frame.litTriangles;
        for(unsigned i=0;i<flashes.count;++i) if(mask[i/32]&(1u<<(i%32))) {++frame.light[i].triangles;++frame.lightTriangles;}
    }
}
FrameView prepareFrame(int w,int h) {
    FrameView view={};Uniforms &camera=view.camera;
    uint64_t prepareStart=profileStart();
    static unsigned profileSerial;
    if(profile.enabled) {profile.frame=I_Render3DFrameProfile{};profile.frame.serial=++profileSerial;pitch=0;}
    camera.projection[2]=1;camera.projection[3]=32768;
    camera.effects[0]=settings.filter;camera.effects[1]=settings.retro;
    camera.texFilter[0]=settings.sharpSoftness;camera.texFilter[1]=settings.paletteMips&&settings.filter;
    // A carried torch's colormap (Game_Torch) becomes real light (lights.cpp).
    float torchFlame;
    camera.effects[2]=Game_Torch(&torchFlame)?0:players[displayplayer].fixedcolormap;
    camera.effects[3]=settings.spriteFilter;camera.materials[0]=settings.emissive;
    unsigned features=(settings.detail?FEATURE_SURFACE_DETAIL:0)|(settings.retroReflections?FEATURE_RETRO_REFLECTIONS:0)|
        (settings.caustics&&causticTexture?FEATURE_CAUSTICS:0)|(settings.texelLight?FEATURE_TEXEL_BAKE:0)|
        (settings.caustics&&settings.ceilingCaustics&&causticTexture?FEATURE_CEILING_CAUSTICS:0)|
        (settings.lightFlow&&flowKey>=0&&flowKey==flowInputs()?FEATURE_LIGHT_FLOW:0)|
        (settings.bakedAO&&ambientKey>=0&&ambientKey==bakeThingsKey?FEATURE_BAKED_OCCLUSION:0)|
        (settings.glossyScreens?FEATURE_GLOSSY_SCREENS:0)|(settings.causticsComputed?FEATURE_CAUSTICS_COMPUTED:0)|
        (settings.causticsGrow?FEATURE_CAUSTICS_GROW:0)|(settings.causticsAngle?FEATURE_CAUSTICS_ANGLE:0)|
        (settings.causticsSway?FEATURE_CAUSTICS_SWAY:0)|(settings.causticsSprites?FEATURE_CAUSTICS_SPRITES:0)|
        (settings.causticsShots?FEATURE_CAUSTICS_SHOTS:0)|(settings.dampShores&&shoreTexture?FEATURE_DAMP_SHORES:0)|
        (settings.detail&&settings.variedHighlights?FEATURE_VARIED_HIGHLIGHTS:0)|
        (settings.windowLight&&bakedLightsActive?FEATURE_WINDOW_LIGHT:0)|
        (settings.detail&&settings.unevenTiles?FEATURE_UNEVEN_TILES:0)|(settings.detail&&settings.unevenWalls?FEATURE_UNEVEN_WALLS:0)|
        (settings.classicFlashlight?FEATURE_CLASSIC_FLASHLIGHT:0)|(settings.stippledShadows?FEATURE_STIPPLED_SHADOWS:0)|
        (settings.glowEdges&&shoreTexture&&causticPattern?FEATURE_GLOW_EDGES:0)|
        (settings.windowStreaks?FEATURE_WINDOW_STREAKS:0)|(settings.liveReadouts?FEATURE_LIVE_READOUTS:0);
    camera.flashlightTint[3]=(float)features; // see shaders/feature_flags.h
    camera.map[0]=mapOrigin[0];camera.map[1]=mapOrigin[1];camera.map[2]=1/mapCell;
    camera.map[3]=settings.softLight&&seamTexture&&worldPending;
    camera.materials[2]=settings.fog&&worldPending;
    camera.materials[3]=settings.palette&&worldPending;
    // No bloom while the in-game settings panel covers the view: it would glow
    // through the dimmed backdrop and soften the panel's text.
    camera.materials[1]=settings.emissive&&worldPending&&camera.effects[2]==0&&!I_Render3DSettingsOpen();
    camera.fx[0]=1;
    camera.eye[3]=Game_WaterClarity();
    // The episode's mood: a dark outdoors dims its sky, a little less than the ground.
    camera.mood[0]=worldPending?1+(levelMood().outdoor-1)*0.85f:1;
    for(int c=0;c<3;++c)camera.mood[1+c]=worldPending?levelMood().skyTint[c]:1; // Its sky's tint.
    float uiWidth=std::min((float)w,h*4.0f/3),uiHeight=uiWidth*0.75f,uiX=(w-uiWidth)/2;
    float uiY=worldPending&&settings.widescreen?h-uiHeight:(h-uiHeight)/2;
    float worldX=uiX+uiWidth*viewwindowx/320,worldY=uiY+uiHeight*viewwindowy/200.0f;
    float worldW=uiWidth*scaledviewwidth/320,worldH=uiHeight*viewheight/200.0f;
    if(settings.widescreen&&scaledviewwidth==320)
    { worldX=0;worldY=0;worldW=w;worldH=h-uiHeight*(1-viewheight/200.0f); }
    float referenceAspect=(4.0f/3)*(scaledviewwidth/320.0f)/(viewheight/200.0f);
    camera.projection[1]=referenceAspect/tan(settings.fov*doomPi/360);
    camera.projection[0]=camera.projection[1]/(worldW/std::max(1.0f,worldH));
    camera.fx2[0]=worldH/200;
    float fraction=1;
    if(worldPending) {
        bool level=levelPending();
        if(level) {planStages(true);startLevel();}
        syncBakeThings();
        if(!level)planStages(false);
        relightMovingSectors();
        if(settings.sun&&!sunBaked)bakeSun();
        bool sun=settings.sun&&sunLevel>0;
        for(int c=0;c<3;++c)camera.sun[c]=sun?sunTint[c]:0;
        camera.sun[3]=sun?sunLevel:0;
        camera.fx2[2]=sunYaw;camera.fx2[3]=sun&&settings.sunDisc?sunGlow:0;
        // outdoor: shade, sun gain, sky color share, the sun disc's height (Game_Sun).
        game_sun game;Game_Sun(&game);
        camera.outdoor[0]=sunShade;camera.outdoor[1]=sunGain;camera.outdoor[2]=game.skyCast;camera.outdoor[3]=sunDiscElevation;
        bakedLightsActive=settings.bakedLights&&lightBakeKey==lightBakeInputs();
        bakeOnlyActive=bakedLightsActive&&(lightBakeKey&2);
        // bake.z: 1 bake-only lights (directions and flicker groups), -1
        // flicker groups only, for lamps that switch with their sector.
        bool directions=wallDirectionTexture&&flatDirectionTexture;
        camera.bake[0]=bakedLightsActive;camera.bake[2]=directions?(bakeOnlyActive?1.0f:lampGroupCount>0?-1.0f:0.0f):0.0f;
        if(const mobj_t *viewer=players[displayplayer].mo) {
            float at[3]={units(viewer->x),units(viewer->y),0};
            updateLampGroups(at);
        }
        for(int g=0;g<flickerGroups;++g)camera.flicker[g/4][g%4]=groupFlicker(g);
        player_t *player=&players[displayplayer];mobj_t *mo=player->mo;
        camera.eye[0]=units(mo->x);camera.eye[1]=units(mo->y);camera.eye[2]=units(player->viewz);
        float yaw=(float)(mo->angle*(2.0*doomPi/4294967296.0));
        if(tickTime&&!paused&&!menuactive&&!demoplayback) {
            fraction=std::clamp((SDL_GetTicksNS()-tickTime)/(1e9/TICRATE),0.0,1.0);
            if(std::hypot(camera.eye[0]-oldEye[0],camera.eye[1]-oldEye[1])<128) {
                for(int i=0;i<3;++i)camera.eye[i]=oldEye[i]+(camera.eye[i]-oldEye[i])*fraction;
                yaw=oldYaw+std::remainder(yaw-oldYaw,2*doomPi)*fraction;
            }
        }
        // A game with its own look (Heretic) sets the pitch, demos included;
        // mouselook turns that look (I_Render3DLook) so its aiming agrees.
        float gameLook;
        float look=Game_LookPitch(&gameLook)?gameLook:demoplayback?0:pitch;
        tourpitch=look;freeaim=settings.look&&!demoplayback;
        camera.right[0]=sin(yaw);camera.right[1]=-cos(yaw);
        camera.forward[0]=cos(yaw)*cos(look);camera.forward[1]=sin(yaw)*cos(look);camera.forward[2]=sin(look);
        camera.up[0]=-cos(yaw)*sin(look);camera.up[1]=-sin(yaw)*sin(look);camera.up[2]=cos(look);
        camera.fog[0]=0.00045f*(player->mo->subsector->sector->ceilingpic==skyflatnum?0.35f:1.0f);
        camera.fog[1]=0.012f;camera.fog[2]=fogReferenceHeight;
        camera.fog[3]=(leveltime+fraction)/TICRATE;
        // Weather and wear (weather.cpp, wear.cpp): where wear.glsl finds its
        // records, how wet the windows and the ground under the sky are.
        camera.right[3]=wearBase();
        camera.forward[3]=settings.weather?glassWetNow:0;
        camera.up[3]=settings.weather?wetNow:0;
        // The far rain sky.frag draws over the sky (weather.cpp's rain stops at the map's sky).
        camera.weather[0]=settings.weather&&camera.effects[2]==0?rainNow:0;
        camera.weather[1]=rainWind[0];camera.weather[2]=rainWind[1];
        renderFraction=fraction;
        // Caustics follow the liquid's frame and crossfade like its floor (surfaceBinding).
        if(!causticFrames.empty()&&causticSpeed>0&&leveltime>0) {
            auto at=std::find(causticFrames.begin(),causticFrames.end(),flattranslation[causticFrames[0]]);
            camera.texFilter[2]=at!=causticFrames.end()?float(at-causticFrames.begin()):0;
            camera.texFilter[3]=((leveltime-1)%causticSpeed+fraction)/causticSpeed;
        }
        uint64_t geometryStart=profileStart();
        if(level)loadingStage(LoadStage::Textures);
        geometry(camera,fraction);
        profileAdd(profile.frame.geometryMs,geometryStart);
        if(profile.enabled)skipProfiledLights();
        // Surface lights come from this frame's geometry; the bake applies from the next frame.
        if(settings.bakedLights&&lightBakeKey!=lightBakeInputs())bakeLights();
        if(settings.bakedLights&&settings.gridSpriteLight&&lightBakeKey==lightBakeInputs()&&staticSerial!=lightBakeSerial)bakeStaticGrid();
        if(settings.bounce&&bounceKey!=bounceInputs())bakeBounce();
        if((settings.skyLight||settings.bakedAO)&&ambientKey!=bakeThingsKey)bakeAmbient();
        if(settings.lightFlow&&flowKey!=flowInputs())bakeFlow();
        camera.bake[1]=settings.bounce&&bounceKey>=0?bounceStrength:0;
        camera.bake[3]=camera.bake[1]>0?settings.bounceTint:0;
        bool ambient=ambientKey==bakeThingsKey;
        camera.fx2[1]=settings.skyLight&&ambient?skyLevel:0;
        // The fog haze takes the sky's color too, so it follows the sky without the sky-light bake.
        static int tintedSky=-1;
        static std::array<float,3> horizon;
        if(skytexture!=tintedSky){skyTint=skyColor();horizon=skyHorizon();tintedSky=skytexture;}
        for(int c=0;c<3;++c)camera.fx[1+c]=skyTint[c];
        // Outdoor haze in the sky's horizon color (Game_Sun), with the fog.
        for(int c=0;c<3;++c)camera.haze[c]=horizon[c]*camera.mood[0]*camera.mood[1+c];
        camera.haze[3]=settings.fog?game.haze:0;
        buildMist(camera);
        buildShafts(camera);
        buildRings(camera);
        updateExposure(camera);
        updateFlashlightTint(camera);
        // The Torch item's faint lift over the whole view, flickering with
        // the flame; most of its light is the carried flame (lights.cpp).
        if(torchFlame>0)for(int c=0;c<3;++c)camera.flashlightTint[c]+=0.1f*torchFlame*torchColor()[c];
        selectFogLights(camera);
        if(profile.skip&I_RENDER3D_SKIP_MIST) {mistVertices.clear();shaftVertices.clear();}
        if(profile.skip&I_RENDER3D_SKIP_FOG) {fogLights={};camera.materials[2]=0;}
        if(profile.skip&I_RENDER3D_SKIP_BLOOM) camera.materials[1]=0;
    }
    if(!worldPending) {mistVertices.clear();shaftVertices.clear();heatVertices.clear();skyVertices.clear();}
    uint64_t masksStart=profileStart();
    assignLightMasks();
    profileAdd(profile.frame.masksMs,masksStart);
    uint64_t wordsStart=profileStart();
    buildLightWords();
    profileAdd(profile.frame.lightsMs,wordsStart);
    if(profile.enabled) {recordProfile();profileAdd(profile.frame.prepareMs,prepareStart);}
    view.uiX=uiX;view.uiY=uiY;view.uiWidth=uiWidth;view.uiHeight=uiHeight;
    view.worldX=worldX;view.worldY=worldY;view.worldW=worldW;view.worldH=worldH;
    return view;
}
std::vector<unsigned> hudPixels(const unsigned *pixels) {
    std::vector<unsigned> overlay(pixels,pixels+SCREENWIDTH*SCREENHEIGHT);
    if(worldPending) for(int i=0;i<SCREENWIDTH*SCREENHEIGHT;++i)overlay[i]=(overlay[i]&0xffffff)|(unsigned)v_overlaymask[i]<<24;
    return overlay;
}
// Current sector light and heights for seam blending and contact shading;
// w is 0 indoors, and under a sky packOutdoor.
std::vector<std::array<float,4>> sectorInfo() {
    std::vector<std::array<float,4>> info(std::max(1,numsectors));
    if(worldPending)for(int i=0;i<numsectors;++i)
        info[i]={lighting(&sectors[i]),floorZ(&sectors[i]),ceilZ(&sectors[i]),
            sectors[i].ceilingpic==skyflatnum?packOutdoor(&sectors[i]):0.0f};
    if(worldPending)appendWear(info);
    return info;
}
// -rendercheck <png> saves the first 3D frame after tic 10 (or -rendertic N)
// and quits; -rendersettings opens the F4 panel for that frame first.
// -renderframes M -renderevery K: M frames K tics apart, laid out as one contact
// sheet (-rendersheet columns, default 4) of the 3D view cropped to
// -rendercrop x0,y0,x1,y1 (shares of the frame) and scaled by -renderscale.
static int renderArg(const char *name,int fallback) {
    int at=M_CheckParm((char*)name);
    return at&&at+1<myargc?atoi(myargv[at+1]):fallback;
}
static int renderFramesTaken=0;
bool renderCheckDue() {
    int after=renderArg("-rendertic",10)+renderFramesTaken*std::max(1,renderArg("-renderevery",1));
    bool due=M_CheckParm((char*)"-rendercheck")&&worldPending&&gametic>after;
    if(due&&M_CheckParm((char*)"-rendersettings")&&!I_Render3DSettingsOpen()) {I_Render3DSettingsMenu();return false;}
    return due;
}
void saveScreenshot(const void *bgra,int width,int height,int stride) {
    SDL_CreateDirectory("Screenshots");
    char path[256];snprintf(path,sizeof(path),"Screenshots/%s-%llu.png",Game_Title(),(unsigned long long)SDL_GetTicksNS());
    int test=M_CheckParm((char*)"-rendercheck");
    const char *output=test&&test+1<myargc?myargv[test+1]:profile.screenshotPath?profile.screenshotPath:path;
    profile.screenshotPath=nullptr;
    SDL_Surface *surface=bgra?SDL_CreateSurfaceFrom(width,height,SDL_PIXELFORMAT_BGRA32,(void*)bgra,stride):nullptr;
    int frames=test?renderArg("-renderframes",1):1;
    if(frames>1&&surface) {
        static SDL_Surface *sheet=nullptr;
        float crop[4]={0.1f,0.2f,0.9f,0.84f},scale=0.25f;
        int at=M_CheckParm((char*)"-rendercrop");
        if(at&&at+1<myargc)sscanf(myargv[at+1],"%f,%f,%f,%f",&crop[0],&crop[1],&crop[2],&crop[3]);
        at=M_CheckParm((char*)"-renderscale");
        if(at&&at+1<myargc)scale=(float)atof(myargv[at+1]);
        SDL_Rect from={(int)(crop[0]*width),(int)(crop[1]*height),(int)((crop[2]-crop[0])*width),(int)((crop[3]-crop[1])*height)};
        int columns=std::max(1,renderArg("-rendersheet",4)),cw=std::max(1,(int)(from.w*scale)),ch=std::max(1,(int)(from.h*scale)),gap=4;
        if(!sheet) {
            int rows=(frames+columns-1)/columns;
            sheet=SDL_CreateSurface(columns*(cw+gap)+gap,rows*(ch+gap)+gap,SDL_PIXELFORMAT_BGRA32);
            SDL_FillSurfaceRect(sheet,nullptr,SDL_MapSurfaceRGB(sheet,24,24,28));
        }
        int n=renderFramesTaken++;
        SDL_Rect to={gap+(n%columns)*(cw+gap),gap+(n/columns)*(ch+gap),cw,ch};
        SDL_BlitSurfaceScaled(surface,&from,sheet,&to,SDL_SCALEMODE_LINEAR);
        SDL_DestroySurface(surface);
        fprintf(stderr,"Render frame %d of %d at tic %d.\n",n+1,frames,gametic);
        if(renderFramesTaken<frames)return;
        surface=sheet;
    }
    bool saved=surface&&SDL_SavePNG(surface,output);SDL_DestroySurface(surface);
    if(!test) players[consoleplayer].message=(char*)(saved?"SCREENSHOT SAVED":"SCREENSHOT FAILED");
    fprintf(stderr,"Screenshot %s: %s (%dx%d).\n",saved?"saved":"failed",output,width,height);
    if(test) {if(!saved)I_Error((char*)"Render check could not save image");I_Quit();}
}
void updateTitle(int width,int height,const char *renderer) {
    ++frameCount;
    uint64_t now=SDL_GetTicksNS();
    if(now-titleTime>1000000000) {
        fps=frameCount*1e9/(now-titleTime);frameCount=0;titleTime=now;
        char rate[32]="";if(settings.fps)snprintf(rate,sizeof(rate)," · %.0f fps",fps);
        char title[256];snprintf(title,sizeof(title),"%s · %s · %dx%d%s%s · F4: graphics · F7: screenshot · Esc: menu",
            Game_Title(),settings.accelerated?renderer:"Classic",width,height,rate,
            Game_HasEffect(GAME_EFFECT_FLASHLIGHT)?" · F: flashlight":"");
        SDL_SetWindowTitle(gameWindow,title);
    }
}
} // namespace doom3d


// Backend-independent parts of the renderer interface.
using namespace doom3d;
void I_Render3DStartFrame(void) {worldPending=false;v_overlayactive=false;}
int I_Render3DIsAccelerated(void) {return settings.accelerated;}
void I_Render3DLook(float delta) {
    if(!settings.look||!settings.accelerated||demoplayback)return;
    if(settings.invertLook)delta=-delta;
    float gameLook;
    if(Game_LookPitch(&gameLook))Game_Look(-delta*0.002f);
    else pitch=std::clamp(pitch-delta*0.002f,-1.2f,1.2f);
}
void I_Render3DToggleFlashlight(void) {if(settings.accelerated&&Game_HasEffect(GAME_EFFECT_FLASHLIGHT))flashlightOn=!flashlightOn;}
void I_Render3DScreenshot(void) {screenshotPending=true;}
const I_Render3DFrameProfile *I_Render3DLastFrame(void) {return &profile.frame;}
void I_Render3DProfileTeleported(void) {surfaceSelection.clear();surfaceLightTime=0;}
void I_Render3DScreenshotTo(const char *path) {profile.screenshotPath=path;screenshotPending=true;}
