/* The scene's public state, its lifecycle and the per-frame entry points
   (prepareFrame), plus the backend-independent I_Render3D calls. */
#include "internal.h"
#include "../rooms.h"
#include "../shaders/feature_flags.h"
extern "C" {
#include "../audio/sound_options.h"
#include "../audio/gore_floor.h"
}
#include "../i_gamepad.h"
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
std::vector<Vertex> mistVertices,particleVertices,glowVertices,crestVertices,heatVertices,skyVertices,shaftVertices,lampSheetVertices;
GpuTextureRef seamTexture,contactTexture,cloudTexture,wallBakeTexture,flatLightTexture,causticTexture,causticPattern,shoreTexture;
GpuTextureRef wallDirectionTexture,flatDirectionTexture;
float reflectionPlane=0;
bool reflectionActive=false;
bool worldPending,screenshotPending,flashlightOn=false;
static boolean flashlightLit() {return flashlightOn?(boolean)1:(boolean)0;}
float pitch;
unsigned levelSerial;
static float oldEye[3], oldYaw;
// How far the game had carried things through geometry built alike
// (Game_WorldShift) when the last tic began: what it grew by since moves
// where a carried thing was with it, so the step doesn't show.
static float oldShift[3];
static angle_t oldAngle;
// The game's clock at the frame's steady moment (pacing.cpp), before its
// tics ran: the frame shows the world then, blended between the last two tics.
static double frameClock;
// Phobos grav boots: the view's roll over onto a ceiling (0 to pi), how fast it turns, and
// whether it is past half way (then the mouse's pitch goes the other way).
static float bootsRoll, bootsRollSpeed;
static bool bootsOver;

std::vector<std::array<float,2>> oldHeights;
float renderFraction=1;
bool ticSaved;
static uint64_t titleTime;
static int frameCount;
static float fps;
// Each thing's place as the last tic began, by its address: a flat table
// (open addressing, at most half full) filled anew each tic. A map of nodes
// took an allocation per thing per tic. Empty after a level's end.
struct OldPlace { const mobj_t *thing; std::array<float,3> at; };
static std::vector<OldPlace> oldThings;
static size_t oldSlot(const mobj_t *thing) {return (size_t)(((uint64_t)(uintptr_t)thing*0x9E3779B97F4A7C15ull)>>32)&(oldThings.size()-1);}
static void fillOldThings(const std::vector<mobj_t*> &things) {
    size_t want=16;
    while(want<things.size()*2)want<<=1;
    if(oldThings.size()<want)oldThings.assign(want,OldPlace{});
    else for(OldPlace &p:oldThings)p.thing=nullptr;
    for(const mobj_t *thing:things)
        for(size_t i=oldSlot(thing);;i=(i+1)&(oldThings.size()-1)) {
            if(oldThings[i].thing==thing)break;   // listed twice: the first place stays
            if(oldThings[i].thing)continue;
            oldThings[i]={thing,{units(thing->x),units(thing->y),units(thing->z)}};
            break;
        }
}
static const std::array<float,3> *oldPlace(const mobj_t *thing) {
    if(oldThings.empty())return nullptr;
    for(size_t i=oldSlot(thing);;i=(i+1)&(oldThings.size()-1)) {
        if(oldThings[i].thing==thing)return &oldThings[i].at;
        if(!oldThings[i].thing)return nullptr;
    }
}
// The tic's things in the sectors' lists, in their order: walked once for
// the watchers that look at them all (watchSpray, watchGlass), after the
// bodies' watch, which may move a body to another sector's list.
const std::vector<mobj_t*> *ticThings=nullptr;
static void listThings(std::vector<mobj_t*> &things) {
    things.clear();
    for(int i=0;i<numsectors;++i) for(mobj_t *thing=sectors[i].thinglist;thing;thing=thing->snext)things.push_back(thing);
}
// What the game carried things by since the last tic began.
static void worldShifted(float &dx,float &dy,float &dz) {
    Game_WorldShift(&dx,&dy,&dz);
    dx=Game_ShiftGrew(dx,oldShift[0]);dy=Game_ShiftGrew(dy,oldShift[1]);dz=Game_ShiftGrew(dz,oldShift[2]);
}
void interpolatedPosition(const mobj_t &thing,float fraction,float &x,float &y,float &z) {
    x=units(thing.x);y=units(thing.y);z=units(thing.z);
    const std::array<float,3> *old=oldPlace(&thing);
    if(!old)return;
    float oldX=(*old)[0],oldY=(*old)[1],oldZ=(*old)[2];
    if(Game_Carried(&thing)) {float dx,dy,dz;worldShifted(dx,dy,dz);oldX+=dx;oldY+=dy;oldZ+=dz;}
    if(std::hypot(x-oldX,y-oldY)<128) {
        x=oldX+(x-oldX)*fraction;
        y=oldY+(y-oldY)*fraction;
        z=oldZ+(z-oldZ)*fraction;
    }
}
// -renderview x y angle [pitch] (map units, degrees; pitch up, needs free
// look) puts the player there once per level, to aim -rendercheck frames.
extern "C" boolean P_TeleportMove(mobj_t *thing,fixed_t x,fixed_t y);
extern "C" int SlowMotion_Option;   // F4 Slow motion (slow_motion.c)
static void renderViewPlace(player_t &player) {
    static unsigned placed=~0u;
    int view=M_CheckParm((char*)"-renderview");
    if(!view||view+3>=myargc||placed==r_levelserial)return;
    placed=r_levelserial;
    mobj_t *mo=player.mo;
    P_TeleportMove(mo,(fixed_t)(atof(myargv[view+1])*FRACUNIT),(fixed_t)(atof(myargv[view+2])*FRACUNIT));
    mo->z=mo->floorz;mo->angle=(angle_t)(int64_t)(atof(myargv[view+3])/360.0*4294967296.0);
    player.viewz=mo->z+player.viewheight;
    // The pitch may be negative (down): a number, not the next option.
    char *end=nullptr;
    float look=view+4<myargc?std::strtof(myargv[view+4],&end):0;
    if(view+4<myargc&&end!=myargv[view+4]&&*end==0) {
        pitch=std::clamp(look*(float)doomPi/180,-1.2f,1.2f);
        // A game that keeps its own look (Heretic's lookdir) takes it there,
        // as far as its look keys reach.
        float gameLook;
        if(Game_LookPitch(&gameLook))Game_Look(pitch-gameLook);
    }
}
static void beforeTic() {
    if(gamestate!=GS_LEVEL||!players[displayplayer].mo||paused) {ticSaved=false;return;}
    uint64_t ticStart=(statsTiming||statsTracing)?SDL_GetTicksNS():0;   // Visual effects in the tic.
    followCarry();
    renderViewPlace(players[displayplayer]);
    mobj_t *mo=players[displayplayer].mo;
    oldEye[0]=units(mo->x);oldEye[1]=units(mo->y);oldEye[2]=units(players[displayplayer].viewz);
    oldYaw=(float)(mo->angle*(2.0*doomPi/4294967296.0));oldAngle=mo->angle;ticSaved=true;
    Game_WorldShift(&oldShift[0],&oldShift[1],&oldShift[2]);
    saveWeaponPlace();
    oldHeights.resize(numsectors);
    for(int i=0;i<numsectors;++i) oldHeights[i]={units(sectors[i].floorheight),units(sectors[i].ceilingheight)};
    static std::vector<mobj_t*> things;
    listThings(things);
    fillOldThings(things);
    timed(Stat::Liquids,watchSplashes);
    timed(Stat::Bodies,[]{ragdollTestTic();watchBodies();watchStrides();});
    listThings(things);ticThings=&things;
    timed(Stat::Blood,watchSpray);
    timed(Stat::Gore,[]{watchFlyingPieces();watchTears();});
    timed(Stat::Glass,watchGlass);
    ticThings=nullptr;
    timed(Stat::Weather,watchWeather);
    timed(Stat::Drafts,watchDrafts);
    timed(Stat::Weather,watchWaterIce);   // water freezing in the cold (water_ice.cpp)
    timed(Stat::Film,watchBlownIn);   // after the drafts: what the openings let in lies on the floor
    timed(Stat::Drafts,hearIndoorWind);
    timed(Stat::Fog,watchFog);
    timed(Stat::Wear,watchWear);
    timed(Stat::Glass,watchBreaches);
    timed(Stat::ShotLights,watchShotLights);
    // What happens in the weather: after the film is stepped.
    timed(Stat::Disturbances,[]{watchDisturbances();watchDisturbanceSources();watchGroundDents();});
    statsTicWork(ticStart);
}
static void acceleratedView(player_t *player) {
    (void)player; worldPending=true; v_overlayactive=true;
    frameClock=steadyClock(I_GetTimeExact());
    memset(v_overlaymask,255,sizeof(v_overlaymask));
    for(int y=viewwindowy;y<viewwindowy+viewheight;++y) {
        memset(screens[0]+y*SCREENWIDTH+viewwindowx,0,scaledviewwidth);
        memset(v_overlaymask+y*SCREENWIDTH+viewwindowx,0,scaledviewwidth);
    }
}
// The game rules alone, as applySettings sets them (I_Render3DEarlySettings).
// The Authored kick landing (R_KickLanded): what lies in its reach is kicked too (flying_piece.cpp); a sweep
// reaches lower and a little less far.
static void kickLanded(mobj_t *kicker,int sweep) {
    if(!kicker)return;
    float angle=(float)(kicker->angle*(2.0*doomPi/4294967296.0));
    flyingPiecesKicked(units(kicker->x),units(kicker->y),units(kicker->z),angle,sweep?56.0f:64.0f,kicker->player?(int)(kicker->player-players):-1);
}
static void applyGameRules(const Settings &s) {
    Game_SetPhobos(s.phobos);
    Game_SetAuthored(s.authored);
    Game_SetWeather(s.weather);
    Game_SetPhobosRules(s.phobosPlayer,s.phobosEnemies,s.phobosAir,s.phobosRecoil);
}
void applySettings() {
    fitGame(settings);
    rebrickSwitched();
    R_AcceleratedView=settings.accelerated?acceleratedView:nullptr;
    R_BeforeTic=settings.accelerated?beforeTic:nullptr;
    R_MuzzleFlash=settings.accelerated?muzzleFlash:nullptr;
    R_WallImpact=settings.accelerated?wallImpact:nullptr;
    R_BloodHit=settings.accelerated?bloodHit:nullptr;R_BloodSpilled=settings.accelerated?bloodSpilled:nullptr;
    R_DamageHit=settings.accelerated?bodyDamaged:nullptr;
    R_PlaneImpact=settings.accelerated?planeImpact:nullptr;
    R_ShotPassed=settings.accelerated?glassShot:nullptr;
    R_GlassStruck=settings.accelerated?glassStruck:nullptr;
    R_Flashlight=settings.accelerated?flashlightLit:nullptr;
    R_Explosion=settings.accelerated?bodyExplosion:nullptr;
    Gore_Underfoot=settings.accelerated?goreUnderfoot:nullptr;
    Game_TraceShotsAlso(settings.accelerated?[](const mobj_t *shooter,const mobj_t *hit,float x1,float y1,float z1,float x2,float y2,float z2)
        {goreShotTraced(shooter,hit,x1,y1,z1,x2,y2,z2);flyingPiecesShot(x1,y1,z1,x2,y2,z2);}:(game_shot_traced)nullptr);
    R_KickLanded=settings.accelerated?kickLanded:nullptr;
    R_RainedIn=settings.accelerated?rainedIn:nullptr;
    freeaim=settings.accelerated&&settings.look;
    classicaim=settings.classicAim!=0;
    weaponbob=settings.weaponBob;
    classicmovement=settings.movement==1||M_CheckParm((char*)"-classicmovement");
    strafeboost=settings.movement==2;
    applyGameRules(settings);
    quickconfirm=settings.quickConfirm!=0;
    gamepad_options pad={settings.padLookX,settings.padLookY,settings.padDeadzone,settings.gyroSpeed,
                         settings.padBoost,settings.padSouthpaw,settings.gyro,
                         settings.rumble,settings.triggerRumble,settings.worldRumble};
    I_GamepadOptions(&pad);
    if(!settings.accelerated) clearFlashPulses();
    r_uncapped=settings.accelerated;
    if(!settings.look) pitch=0;
    R_SetViewSize(10,0);
    applyWindowSize();
    I_SetSoundOptions(settings.roomAcoustics,settings.reverbAmount,settings.muffleWalls,settings.muffleDistance,settings.naturalFalloff);
    // The baked grid is only heard as the listener's room (room acoustics): without them it isn't baked.
    I_SetLevelSound(settings.authored,settings.farRooms,settings.roomGrid&&settings.roomAcoustics);
    // Traced tails ring in the listener's room and the space beyond (room acoustics) and in far rooms;
    // with neither on nothing plays them, so they aren't traced.
    I_SetRoomTails(settings.roomTails&&(settings.roomAcoustics||settings.farRooms>0));
    I_SetSoundOutput(settings.soundOutput);
    I_SetSfxPlayback(settings.sfxResampling,settings.sfxTreble);
    I_SetMadeSounds(settings.madeImpacts,settings.glassSound,settings.ambience,settings.ambienceAmount,settings.footsteps);
    I_SetAlternativeComputerSounds(settings.alternativeComputerSounds);
    Game_SetRandomPitch(settings.randomPitch);
    Game_SetMonsterVoices(settings.monsterVoices);
    I_AmbienceShowGlass(settings.accelerated&&settings.windowGlass);
    I_SetMusicDevice(settings.musicDevice);
    I_SetSoundtrack(settings.soundtrack);
    // The authored episode's mood is baked into the sun, lamps, bounce and
    // sky light (levelMood): switching it re-bakes them all.
    // So is Phobos' (levelMood, Game_Sun, skyColor), and the player's hour
    // and heat (climate.cpp), which place the sun anew too. And the weather
    // and its wind: its overcast and what it leaves on the ground change the bounce
    // (under Clear the wind leaves nothing: it is only heard).
    static int bakedAuthored=-1,bakedPhobos=-1,bakedClimate=-1,bakedWeather=-1,bakedFast=-1;
    // Sky depth swings the sun round with the hour (sun.cpp's placeSun): part of the climate here.
    // The settings as picked, not climateNow: Random's roll changes only with the map, which bakes anew anyway.
    int phobos=Game_Phobos(),climate=settings.timeOfDay*1000+settings.temperature+100+settings.skyLayers*100000,weather=settings.weather*64+(settings.weather?settings.wind:0);
    bool climateChanged=bakedClimate>=0&&bakedClimate!=climate;
    if((bakedAuthored>=0&&bakedAuthored!=settings.authored)||(bakedPhobos>=0&&bakedPhobos!=phobos)||climateChanged||
       (bakedWeather>=0&&bakedWeather!=weather)) {sunBaked=false;lightBakeKey=-1;bounceKey=-1;ambientKey=-1;flowKey=-1;}
    // DERP mode brings its authored look (mapInfo): its sun and hour too.
    if(climateChanged||(bakedAuthored>=0&&bakedAuthored!=settings.authored))weatherSunAgain();
    bakedAuthored=settings.authored;bakedPhobos=phobos;bakedClimate=climate;bakedWeather=weather;
    // Quick bakes (Settings::fastBakes) re-bake the sky light and bounce.
    if(bakedFast>=0&&bakedFast!=settings.fastBakes) {bounceKey=-1;ambientKey=-1;}
    bakedFast=settings.fastBakes;
    // Light by logic (internal.h's lightByLogic) changes the lamps' strength and the daylight: everything re-bakes.
    static int bakedLogic=-1;
    if(bakedLogic>=0&&bakedLogic!=settings.lightByLogic) {sunBaked=false;lightBakeKey=-1;bounceKey=-1;ambientKey=-1;}
    bakedLogic=settings.lightByLogic;
    // Bumps in all baked light (Settings::bakedBumps): the lamps and bounce bake the HL2 basis.
    static int bakedBumps=-1;
    if(bakedBumps>=0&&bakedBumps!=settings.bakedBumps) {lightBakeKey=-1;bounceKey=-1;}
    bakedBumps=settings.bakedBumps;
}
static void pickGraphicsConfig() {
    int config=M_CheckParm((char*)"-graphics");
    if(config&&config+1<myargc) {graphicsConfig=myargv[config+1];graphicsConfigNamed=true;}
}
void sceneInit(SDL_Window *window) {
    gameWindow=window;
    pickGraphicsConfig();
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
// Read on a copy: sceneInit reads them again for the rest, once the game is known.
void earlySettings() {
    Settings saved=settings;
    pickGraphicsConfig();
    loadSettings();
    Settings early=settings;
    settings=saved;
    fitGame(early);
    applyGameRules(early);
}
void sceneShutdown() {
    stopRelight();
    R_Flashlight=nullptr;R_AcceleratedView=nullptr;R_BeforeTic=nullptr;R_MuzzleFlash=nullptr;R_WallImpact=nullptr;R_BloodHit=nullptr;R_BloodSpilled=nullptr;R_DamageHit=nullptr;R_PlaneImpact=nullptr;R_ShotPassed=nullptr;R_GlassStruck=nullptr;R_Explosion=nullptr;Gore_Underfoot=nullptr;R_RainedIn=nullptr;r_uncapped=0;v_overlayactive=0;
    lightBlockers.clear();flashes={};oldThings.clear();oldHeights.clear();
    batches.clear();shadowBatches.clear();decalBatches.clear();mirrorBatches.clear();particleVertices.clear();glowVertices.clear();
    mistVertices.clear();skyVertices.clear();shaftVertices.clear();heatVertices.clear();lampSheetVertices.clear();
    imagesShutdown();skeletonsShutdown();mapShutdown();causticsShutdown();clearSurfaceLights();lightsShutdown();glassLightShutdown();exposureShutdown();spaceShutdown();skyLayersShutdown();skyLightShutdown();
    bakeAtlasShutdown();sunShutdown();lightBakeShutdown();clearStaticGrid();indirectShutdown();gpuTraceShutdown();
    impactsShutdown();explosionsShutdown();bloodShutdown();puddlesShutdown();splashesShutdown();bodyMotionShutdown();glassShutdown();shotLightsShutdown();
    weatherShutdown();wearShutdown();draftsShutdown();blownInShutdown();disturbancesShutdown();groundDentsShutdown();fogShutdown();waterIceShutdown();
}
// A new level, or the first after shutdown: each part forgets the last
// level, then the map and what is built from it at level load.
static void startLevel() {
    loadingStage(LoadStage::Map);
    stopRelight();
    batches.clear();shadowBatches.clear();mirrorBatches.clear();clearSurfaceLights();shotLightsShutdown();
    bakeAtlasNewLevel();sunNewLevel();lightBakeNewLevel();clearStaticGrid();indirectNewLevel();
    oldThings.clear();oldHeights.clear();ticSaved=false;
    Rooms_Build();   // what the level's spaces are (rooms.c), before anything reads them
    buildMap();
    weatherNewLevel(); // the sun's place and the wind, before anything bakes by them
    buildCaustics();
    buildShore();
    layoutBake();
    findDoors();
    prepareFalls();
    prepareRagdolls();
    prepareSkeletons();
    levelSerial=r_levelserial;
}
static bool levelPending() {return levelSerial!=r_levelserial||floors.empty();}
bool bakesPending() {
    return (settings.bakedLights&&lightBakeKey!=lightBakeInputs())||(settings.bounce&&bounceKey!=bounceInputs())||
        ((settings.skyLight||settings.bakedAO)&&ambientKey!=bakeThingsKey)||(flowWanted()&&flowKey!=flowInputs())||(settings.sun&&!sunBaked);
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
    planLoading(LoadStage::Flow,flowWanted()&&(level||flowKey!=flowInputs()));
}
// What frameMean's alpha tells surfaceLight about a texture's material:
// 0 DOOM's palette ramps (3, stone, for Game_MatteTexture), 1 ice
// (iceFlat); with Game_SurfaceGloss, 2 a sprite (judged by colour alone),
// else 3 + its game_surface; fabric (9) takes materialGloss's matte
// default. Worn plates (wornPlate) add 16, metal (metalSurface) 32; metal
// that is mostly blank plate (Image::blank) is brushed, 64, along v
// (Image::brushedDown) another 128.
static_assert(GAME_SURFACE_STONE==0&&GAME_SURFACE_METAL==1&&GAME_SURFACE_WOOD==2&&GAME_SURFACE_FLESH==3&&GAME_SURFACE_SOIL==4&&GAME_SURFACE_LIQUID==5&&GAME_SURFACE_FABRIC==6,
              "materialGloss in world/surface.glsl numbers the surfaces");
static float surfaceMaterial(int key,int pic,bool flat) {
    if((flat&&iceFlat(pic))||isFrozenFlatKey(key))return 1;
    if(isStillFlatKey(key)) {int lump=-1-key-stillFlatOffset;return surfaceMaterial(-1-lump,lump-firstflat,true);}
    if(isLitPaintKey(key)) {int lump=-1-key-stillFlatOffset-(1<<27);return surfaceMaterial(-1-lump,lump-firstflat,true);}
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
// Walls and flats the game's surface tables call metal, in every game, unless marked
// matte (world/surface.glsl metalness picks the bare parts).
static bool metalSurface(int key,int pic,bool flat) {
    if(key<0&&!flat)return false;
    static std::unordered_map<int,bool> known;
    auto found=known.find(key);
    if(found!=known.end())return found->second;
    char name[9]={};
    if(key>=0)R_TextureName(key,name);
    else memcpy(name,lumpinfo[firstflat+pic].name,8);
    return known[key]=name[0]&&!Game_MatteTexture(name,flat)&&
        (key>=0?Game_WallSurface(name):Game_FlatSurface(name))==GAME_SURFACE_METAL;
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
    binding.frameMean[3]=surfaceMaterial(key,pic,flat)+(settings.wornPlates&&wornPlate(key,pic,flat)?16:0)+
        (settings.metalShading&&settings.detail&&metalSurface(key,pic,flat)?32+(image.blank>=0.55f?64+(image.brushedDown?128:0):0):0);
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
    // Shore ripples in the liquid's own shader (F4 "Shore ripples: In the liquid";
    // world/shore.glsl's shoreRipple): the numbers liquids.cpp's drawn ripples go
    // by, per look, and the same clock in the same steps of four tics. Drawn as
    // blocks they were some 3,000 quads a frame where a view holds much shore.
    if(flat&&settings.shoreRipples==2&&shoreTexture) {
        float wavelength=0,period=1,reach=0,width=0,strength=0;
        switch(liquidLook(pic)) {
        case LIQUID_LOOK_WATER: wavelength=4.5f;period=0.9f;reach=21;width=0.8f;strength=0.75f;break;
        case LIQUID_LOOK_THICK: wavelength=7;period=2.4f;reach=23;width=1.8f;strength=0.6f;break;
        case LIQUID_LOOK_HOT: wavelength=8;period=3.2f;reach=23;width=2.0f;strength=0.85f;break;
        case LIQUID_LOOK_BLOOD: wavelength=-1;width=0.8f;break;   // only a still dark line
        default: break;
        }
        float now=std::floor(gameSeconds()*TICRATE/4)*4/TICRATE;
        binding.ripple={wavelength,std::fmod(now/period,1024.0f),reach,strength};
        binding.rippleShape={width,0,0,0};
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
void muzzleFireFrame(float uiX,float uiY,float uiW,float uiH,float worldX,float worldY,float worldW,float worldH);   // muzzle_fire.cpp
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
    SlowMotion_Option=settings.slowMotion;
    camera.effects[3]=settings.spriteFilter;camera.materials[0]=settings.emissive;
    unsigned features=(settings.detail?FEATURE_SURFACE_DETAIL:0)|(settings.retroReflections?FEATURE_RETRO_REFLECTIONS:0)|
        (settings.caustics&&causticTexture?FEATURE_CAUSTICS:0)|(settings.texelLight?FEATURE_TEXEL_BAKE:0)|
        (settings.caustics&&settings.ceilingCaustics&&causticTexture?FEATURE_CEILING_CAUSTICS:0)|
        (settings.lightFlow&&settings.lightModel==0&&flowKey>=0&&flowKey==flowInputs()?FEATURE_LIGHT_FLOW:0)|
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
    // F4 "Quick baked light reads" (bake.glsl's fast path); the feature bits are all taken, so it rides in farFogRange.z.
    static const bool deck=[]{const char *d=getenv("SteamDeck");return d&&!strcmp(d,"1");}();
    camera.farFogRange[2]=settings.texelLight&&(settings.fastBakeReads==1||(settings.fastBakeReads==0&&deck))?1.0f:0.0f;
    // F4 "Hero light": the floor it lifts dark surfaces toward (world.frag); 0 off. Also in farFogRange, for want of a bit.
    camera.farFogRange[3]=settings.heroLight==2?0.5f:settings.heroLight==1?0.3f:0.0f;
    camera.map[0]=mapOrigin[0];camera.map[1]=mapOrigin[1];camera.map[2]=1/mapCell;
    camera.map[3]=settings.softLight&&seamTexture&&worldPending;
    camera.materials[2]=settings.fog&&worldPending;
    camera.materials[3]=settings.palette&&worldPending;
    // No bloom while the in-game settings panel covers the view: it would glow
    // through the dimmed backdrop and soften the panel's text.
    camera.materials[1]=settings.emissive&&settings.bloom&&worldPending&&camera.effects[2]==0&&!I_Render3DSettingsOpen();
    camera.fx[0]=1;
    camera.eye[3]=Game_WaterClarity();
    // The episode's mood: a dark outdoors dims its sky, a little less than the ground.
    camera.mood[0]=worldPending?1+(levelMood().outdoor-1)*0.85f:1;
    for(int c=0;c<3;++c)camera.mood[1+c]=worldPending?levelMood().skyTint[c]:1; // Its sky's tint.
    // The 320x200 frame and the status bar (status_bar.cpp): the view's place in the frame.
    const ScreenLayout layout=screenLayout(w,h);
    float uiX=layout.uiX,uiY=layout.uiY,uiWidth=layout.uiWidth,uiHeight=layout.uiHeight;
    float worldX=uiX+uiWidth*viewwindowx/320,worldY=uiY+uiHeight*viewwindowy/200.0f;
    float worldW=uiWidth*scaledviewwidth/320,worldH=uiHeight*viewheight/200.0f;
    if(settings.widescreen&&scaledviewwidth==320)
    { worldX=0;worldY=0;worldW=w;worldH=layout.bar.apart?uiY+worldH:h-uiHeight*(1-viewheight/200.0f); }
    float referenceAspect=(4.0f/3)*(scaledviewwidth/320.0f)/(viewheight/200.0f);
    camera.projection[1]=referenceAspect/tan(settings.fov*doomPi/360);
    camera.projection[0]=camera.projection[1]/(worldW/std::max(1.0f,worldH));
    // Doom drew 320x200 for a 4:3 screen, so its pixels stood 1.2 times taller than wide and the
    // art was made for that. The HUD gets this from its 4:3 box; the world gets it here, in height
    // alone, so the view keeps its width and a texel covers the same pixel the status bar does.
    camera.projection[1]*=1.2f;
    camera.fx2[0]=worldH/200;
    float fraction=1;
    if(worldPending) {
        bool level=levelPending();
        if(level) {planStages(true);startLevel();}
        syncBakeThings();
        if(!level)planStages(false);
        timed(Stat::Bakes,relightMovingSectors);
        if(settings.sun&&!sunBaked)bakeSun();
        bool sun=settings.sun&&sunLevel>0;
        for(int c=0;c<3;++c)camera.sun[c]=sun?sunTint[c]:0;
        // Under falling weather (weather.cpp) the sky is overcast: the sunlit patches
        // sink most of the way back to the sector light and the sun's disc is gone.
        float clouded=overcast();
        camera.sun[3]=sun?sunLevel*(1-0.85f*clouded):0;
        camera.fx2[2]=sunYaw;camera.fx2[3]=sun&&settings.sunDisc?sunGlow*(1-clouded):0;
        // outdoor: shade, sun gain, sky color share, the sun disc's height (Game_Sun).
        game_sun game;Game_Sun(&game);
        camera.outdoor[0]=sunShade;camera.outdoor[1]=sunGain;camera.outdoor[2]=game.skyCast;camera.outdoor[3]=sunDiscElevation;
        bakedLightsActive=settings.bakedLights&&lightBakeKey==lightBakeInputs();
        bakeOnlyActive=bakedLightsActive&&(lightBakeKey&2);
        // bake.z: 1 bake-only lights (directions and flicker groups), -1
        // flicker groups only, for lamps that switch with their sector.
        bool directions=bakeDirected&&wallDirectionTexture&&flatDirectionTexture;
        // bake.x: 2 with whole-level lighting.
        camera.bake[0]=bakedLightsActive?(settings.lightModel==1?2.0f:1.0f):0.0f;
        camera.bake[2]=directions?(bakeOnlyActive?1.0f:lampGroupCount>0?-1.0f:0.0f):0.0f;
        if(const mobj_t *viewer=players[displayplayer].mo) {
            float at[3]={units(viewer->x),units(viewer->y),0};
            updateLampGroups(at);
        }
        for(int g=0;g<flickerGroups;++g)camera.flicker[g/4][g%4]=groupFlicker(g);
        screenTextUpdate();   // text running down dim monitors (screen_text.cpp)
        player_t *player=&players[displayplayer];mobj_t *mo=player->mo;
        camera.eye[0]=units(mo->x);camera.eye[1]=units(mo->y);camera.eye[2]=units(player->viewz);
        // Phobos: the player's body turned by his guns' kicks takes the view with it (F4, off
        // by default), on over the top: past straight up or down he faces the other way
        // upside down (Game_FlipView turns his facing about; the camera rolls over, below).
        {
            float kick=Game_LookKick(),edge=(float)doomPi/2;
            if(settings.phobosSpin&&settings.look&&!demoplayback&&Game_Phobos()) {
                float before=pitch;
                pitch+=Game_ViewFlipped()!=bootsOver?-kick:kick;
                // On over the top once it would pass the view's limit, a hair short of it.
                if((pitch>=edge-1e-3f&&pitch>before)||(pitch<=-edge+1e-3f&&pitch<before)) {
                    if(pitch>edge-1e-3f&&pitch<=edge)pitch=edge+1e-3f;
                    if(pitch<-edge+1e-3f&&pitch>=-edge)pitch=-edge-1e-3f;
                    pitch=(pitch>0?1:-1)*(float)doomPi-pitch;
                    Game_FlipView();oldYaw+=(float)doomPi;
                }
                pitch=std::clamp(pitch,-edge+1e-3f,edge-1e-3f);
            } else if(Game_ViewFlipped()) {Game_FlipView();oldYaw+=(float)doomPi;}  // upright again
        }
        followCarry();
        float yaw=(float)(mo->angle*(2.0*doomPi/4294967296.0));
        if(ticSaved&&!paused&&!menuactive&&!demoplayback) {
            // How far the game's clock had got past the newest tic's own time
            // (the clock tic its ticcmd was built at, d_net.c's gametime less
            // the ticcmds built but not run yet), not past the moment the tic
            // ran: tics run only once a frame, late by a varying share of one,
            // and blending from that moment stepped the view unevenly.
            fraction=std::clamp(frameClock-(gametime-(maketic-gametic)),0.0,1.0);
            // The mouse turns the view every frame: its part of the newest
            // tic's turn shows whole at once (when that tic turned the player
            // by its whole ticcmd), and what is queued for the next goes ahead.
            int lastTurn=0,pendingTurn=0;
            player_t &me=players[displayplayer];
            if(!netgame&&displayplayer==consoleplayer)G_MouseTurn(&lastTurn,&pendingTurn);
            const float angleturn=(float)(2*doomPi/65536);
            float eye[3]={oldEye[0],oldEye[1],oldEye[2]},dx,dy,dz;
            worldShifted(dx,dy,dz);eye[0]+=dx;eye[1]+=dy;eye[2]+=dz;
            if(std::hypot(camera.eye[0]-eye[0],camera.eye[1]-eye[1])<128) {
                for(int i=0;i<3;++i)camera.eye[i]=eye[i]+(camera.eye[i]-eye[i])*fraction;
                yaw=oldYaw+std::remainder(yaw-oldYaw,2*doomPi)*fraction;
                if(mo->angle-oldAngle==(angle_t)(unsigned short)me.cmd.angleturn<<16)yaw+=lastTurn*angleturn*(1-fraction);
            }
            yaw+=pendingTurn*angleturn;
        }
        // A game with its own look (Heretic) sets the pitch, demos included;
        // mouselook turns that look (I_Render3DLook) so its aiming agrees.
        float gameLook;
        float look=Game_LookPitch(&gameLook)?gameLook:demoplayback?0:pitch;
        tourpitch=look;freeaim=settings.look&&!demoplayback;
        camera.right[0]=sin(yaw);camera.right[1]=-cos(yaw);
        camera.forward[0]=cos(yaw)*cos(look);camera.forward[1]=sin(yaw)*cos(look);camera.forward[2]=sin(look);
        camera.up[0]=-cos(yaw)*sin(look);camera.up[1]=-sin(yaw)*sin(look);camera.up[2]=cos(look);
        if(Game_ViewFlipped())for(int c=0;c<3;++c) {camera.right[c]=-camera.right[c];camera.up[c]=-camera.up[c];}
        // Authored's slide (p_moves.c): the view tips over a few degrees, his weight on one hip, and
        // widens, as speed does to a view.
        if(float slide=demoplayback?0:Game_SlideView(displayplayer,fraction)) {
            constexpr float slideRoll=0.07f,slideWiden=12;
            float c=cos(slideRoll*slide),s=sin(slideRoll*slide);
            for(int k=0;k<3;++k) {
                float r=camera.right[k],u=camera.up[k];
                camera.right[k]=r*c+u*s;camera.up[k]=u*c-r*s;
            }
            float wider=tan(settings.fov*doomPi/360)/tan((settings.fov+slideWiden*slide)*doomPi/360);
            camera.projection[0]*=wider;camera.projection[1]*=wider;
        }
        // Phobos grav boots: upside down on a ceiling the view rolls over about where he
        // looks, eased (a spring, by the clock: smooth at any frame rate).
        {
            static uint64_t rollTime;uint64_t now=SDL_GetTicksNS();
            float dt=rollTime?std::min(0.05f,(now-rollTime)/1e9f):0;rollTime=now;
            float target=!demoplayback&&Game_Phobos()&&Game_BootsUpside()?(float)doomPi:0,w=7;
            if(!paused&&!menuactive) {bootsRollSpeed+=(w*w*(target-bootsRoll)-2*w*bootsRollSpeed)*dt;bootsRoll+=bootsRollSpeed*dt;}
            if(std::abs(target-bootsRoll)<1e-3f&&std::abs(bootsRollSpeed)<1e-2f) {bootsRoll=target;bootsRollSpeed=0;}
            bootsOver=bootsRoll>(float)doomPi/2;Game_BootsRolled(bootsOver);
            if(bootsRoll!=0) {
                float c=cos(bootsRoll),s=sin(bootsRoll);
                for(int k=0;k<3;++k) {
                    float r=camera.right[k],u=camera.up[k];
                    camera.right[k]=r*c+u*s;camera.up[k]=u*c-r*s;
                }
            }
        }
        // The air's fog thins under the open sky (to 0.35). By how much of the sky the ground
        // under the player sees (the sky bake's share), eased over half a second: by whether
        // his sector had a sky ceiling, one step under a porch's roof tripled it at once, and
        // the yard he had just stood in went dull and flat before his eyes.
        {
            static float openAir=-1;static unsigned airSerial=~0u;static uint64_t airTime=0;
            const sector_t *own=player->mo->subsector->sector;
            float open=own->ceilingpic==skyflatnum?1.0f:0.0f;
            if(settings.skyLight&&ambientKey==bakeThingsKey&&!flatLightCells.empty()&&mapWidth>0) {
                int cx=std::clamp((int)std::floor((units(player->mo->x)-mapOrigin[0])/mapCell),0,mapWidth-1);
                int cy=std::clamp((int)std::floor((units(player->mo->y)-mapOrigin[1])/mapCell),0,mapHeight-1);
                // Half the sky or more counts as open air.
                open=std::clamp((flatCell(floorBounce,(size_t)cy*mapWidth+cx)[3]>>4)/15.0f*2,0.0f,1.0f);
            }
            uint64_t now=SDL_GetTicksNS();
            if(airSerial!=r_levelserial||openAir<0) {airSerial=r_levelserial;openAir=open;}
            else openAir+=(open-openAir)*(1-std::exp(-std::min((now-airTime)/1e9f,0.25f)/0.5f));
            airTime=now;
            camera.fog[0]=0.00045f*(1-0.65f*openAir);
        }
#ifdef DOOM_DEVTOOLS
        // DERP_FOGOUT=0|1: the fog as if the player stood under a roof (0) or under the sky (1), to see what that switch does.
        if(const char *held=getenv("DERP_FOGOUT"))camera.fog[0]=0.00045f*(atoi(held)?0.35f:1.0f);
#endif
        camera.fog[1]=0.012f;camera.fog[2]=fogReferenceHeight;
        camera.fog[3]=levelSeconds(fraction);
        // DERPINFO farfog: Turok's distance fog (world.frag), and the backdrop in its colour (sky.frag).
        const derp_map &info=mapInfo();
        farFogActive=info.farFogFar>info.farFogNear&&info.farFogNear>=0;
        if(farFogActive) {
            int rgb=info.farFogColor>=0?info.farFogColor:0x060608;
            camera.farFog[0]=((rgb>>16)&255)/255.0f;camera.farFog[1]=((rgb>>8)&255)/255.0f;camera.farFog[2]=(rgb&255)/255.0f;camera.farFog[3]=1;
            camera.farFogRange[0]=info.farFogNear;camera.farFogRange[1]=info.farFogFar;
        }
        // Falling through the library's chasm: the rush (rush.cpp).
        float worldView[4]={worldX/w,worldY/h,worldW/w,worldH/h};
        fallRush(camera,worldView);
        closedEyes(camera,worldView);
        // Weather and wear (weather.cpp, wear.cpp): where wear.glsl finds its
        // records, how wet the windows and the ground under the sky are.
        camera.right[3]=wearBase();
        camera.forward[3]=climateNow().weather?glassWetNow:0;
        camera.up[3]=climateNow().weather?wetNow:0;
        // The far rain or snow sky.frag draws over the sky (weather.cpp's
        // stops at the map's sky); which kind falls (Weather), for the
        // ground and glass too.
        camera.weather[0]=climateNow().weather&&camera.effects[2]==0?fallNow:0;
        // The wind only for what falls: with nothing falling no shader works it.
        camera.weather[3]=climateNow().weather?(float)falling():0;
        camera.weather[1]=camera.weather[3]!=0?windNow[0]:0;camera.weather[2]=camera.weather[3]!=0?windNow[1]:0;
        {
            const WeatherKind &kind=weatherKind(climateNow().weather?falling():WEATHER_NONE);
            std::array<float,3> tint=kind.tint?kind.tint():std::array<float,3>{0,0,0};
            for(int c=0;c<3;++c)camera.weatherTint[c]=tint[c];
            camera.weatherTint[3]=kind.tint?1:0;
            // The far sheets in the colour of the farthest near ones (farFall),
            // what settles in its own (snow's white unless the kind says).
            Weather now=climateNow().weather?falling():WEATHER_NONE;
            bool far=now==WEATHER_RAIN||now==WEATHER_NUKAGE||now==WEATHER_SAND||now==WEATHER_ASH||now==WEATHER_FIRESTORM;
            std::array<float,3> lies=kind.lies?kind.lies():std::array<float,3>{0.8f,0.82f,0.87f};
            for(int c=0;c<3;++c) {camera.fallColor[c]=far?farFall.color[c]:0;camera.settleColor[c]=lies[c];}
            camera.fallColor[3]=camera.settleColor[3]=0;
        }
        fogBankUniforms(camera);   // the fog bank where the eye stands (fog.cpp)
        disturbanceUniforms(camera);   // and the holes blasts blew in it (disturbances.cpp)
        // Phobos: no air outside, the sky is space (sky.frag), how plainly its stars show, and
        // the frost on the helmet's visor (present.frag).
        camera.phobos[0]=Game_Phobos()?1:0;camera.phobos[1]=Game_Phobos()?starVisibility():0;
        camera.phobos[2]=visorFrost();
        camera.phobos[3]=daylightScale();
        renderFraction=fraction;
        // Caustics follow the liquid's frame and crossfade like its floor (surfaceBinding).
        if(!causticFrames.empty()&&causticSpeed>0&&leveltime>0) {
            auto at=std::find(causticFrames.begin(),causticFrames.end(),flattranslation[causticFrames[0]]);
            camera.texFilter[2]=at!=causticFrames.end()?float(at-causticFrames.begin()):0;
            camera.texFilter[3]=((leveltime-1)%causticSpeed+fraction)/causticSpeed;
        }
        uint64_t geometryStart=profileStart();
        if(level) {loadingStage(LoadStage::Textures);precacheSprites();precacheWalls();}
        geometry(camera,fraction);
        profileAdd(profile.frame.geometryMs,geometryStart);
        if(profile.enabled)skipProfiledLights();
        // Surface lights come from this frame's geometry; the bake applies from the next frame.
        timed(Stat::Bakes,[&]{
            if(settings.bakedLights&&(poolRebakeWanted()||lightBakeKey!=lightBakeInputs()))bakeLights();
            if(settings.bakedLights&&settings.gridSpriteLight&&lightBakeKey==lightBakeInputs()&&staticSerial!=lightBakeSerial)bakeStaticGrid();
            // The sky share first: the bounce reads it (whole-level lighting, bake_bounce.cpp).
            if((settings.skyLight||settings.bakedAO)&&ambientKey!=bakeThingsKey)bakeAmbient();
            if(settings.bounce&&bounceKey!=bounceInputs())bakeBounce();
            if(flowWanted()&&flowKey!=flowInputs())bakeFlow();
        });
        gpuTraceCheck();     // DERP_GPUTRACECHECK only (gpu_trace_check.cpp)
        bakeCacheFinish();   // what this frame baked to disk, before play changes any of it (bake_cache.cpp)
        camera.bake[1]=settings.bounce&&bounceKey>=0?bounceStrength:0;
        camera.bake[3]=camera.bake[1]>0?settings.bounceTint:0;
        bool ambient=ambientKey==bakeThingsKey;
        camera.fx2[1]=settings.skyLight&&ambient?skyLevel*(Game_Phobos()?marsShine:1):0;
        // The fog haze takes the sky's color too, so it follows the sky without the sky-light bake.
        static int tintedSky=-1;
        static std::array<float,3> horizon;
        if(skytexture!=tintedSky){skyTint=skyColor();horizon=skyHorizon();tintedSky=skytexture;}
        for(int c=0;c<3;++c)camera.fx[1+c]=skyTint[c];
        // Outdoor haze in the sky's horizon color (Game_Sun), with the fog.
        for(int c=0;c<3;++c)camera.haze[c]=horizon[c]*camera.mood[0]*camera.mood[1+c];
        camera.haze[3]=settings.fog?game.haze:0;
        skyLayersUniforms(camera);   // the sky's ridges and mist by distance (sky_layers.cpp)
        climateUniforms(camera);   // the heat's glare and warm haze, the cold's frost (climate.cpp)
        ashHazeUniforms(camera);   // the ash's pall over the far open ground (ash.cpp)
        sandHazeUniforms(camera);  // a sandstorm's dust and whiteout (sand.cpp)
        firestormHazeUniforms(camera);   // a firestorm's smoke, lit orange (firestorm.cpp)
        snowHazeUniforms(camera);  // a blizzard's whiteout (snow.cpp)
        dryIceHazeUniforms(camera);   // dry ice's haze in the bank's colour (fog.cpp)
        // The whiteout close by (whiteout.cpp; world.frag): over what lies out
        // in it always, over the rest as much as is round the eye (c.climate.w,
        // beam_air.cpp's beamUniforms), in the colour the hazes above leave.
        camera.climate[2]=whiteoutNow();
        beamUniforms(camera);      // the flashlight's beam in the weather (beam_air.cpp)
        timed(Stat::Mist,[&]{buildMist(camera);buildShafts(camera);});
        timed(Stat::Liquids,[&]{buildRings(camera);});
        updateExposure(camera);
        updateFlashlightTint(camera);
        // The Torch item's faint lift over the whole view, flickering with
        // the flame; most of its light is the carried flame (lights.cpp).
        if(torchFlame>0)for(int c=0;c<3;++c)camera.flashlightTint[c]+=0.1f*torchFlame*torchColor()[c];
        selectFogLights(camera);
        if(profile.skip&I_RENDER3D_SKIP_MIST) {mistVertices.clear();shaftVertices.clear();}
        if(profile.skip&I_RENDER3D_SKIP_FOG) {fogLights={};camera.materials[2]=0;}
        if(profile.skip&I_RENDER3D_SKIP_BLOOM) camera.materials[1]=0;
        applyProfileKnob(camera);   // the perf test's feature knob, last (profile_knobs.cpp)
    }
    if(!worldPending) {mistVertices.clear();shaftVertices.clear();heatVertices.clear();skyVertices.clear();}
    uint64_t masksStart=profileStart();
    timed(Stat::Lights,assignLightMasks);
    profileAdd(profile.frame.masksMs,masksStart);
    uint64_t wordsStart=profileStart();
    timed(Stat::Lights,buildLightWords);
    profileAdd(profile.frame.lightsMs,wordsStart);
    if(profile.enabled) {recordProfile();profileAdd(profile.frame.prepareMs,prepareStart);}
    view.uiX=uiX;view.uiY=uiY;view.uiWidth=uiWidth;view.uiHeight=uiHeight;view.bar=layout.bar;
    muzzleFireFrame(uiX,uiY,uiWidth,uiHeight,worldX,worldY,worldW,worldH);   // where the weapon layer and the view lie, for the fire on its barrel (muzzle_fire.cpp)
    view.worldX=worldX;view.worldY=worldY;view.worldW=worldW;view.worldH=worldH;
    frameAfterLoadDone();
    return view;
}
// Built over last frame's: most frames the status bar and the mask stand
// still, and then the upload (gpu3d/draw.cpp) is left out.
const std::vector<unsigned> &hudPixels(const unsigned *pixels,bool &changed) {
    static std::vector<unsigned> overlay;
    changed=overlay.size()!=(size_t)(SCREENWIDTH*SCREENHEIGHT);
    if(changed)overlay.assign(SCREENWIDTH*SCREENHEIGHT,0);
    unsigned differ=0;
    if(worldPending)for(int i=0;i<SCREENWIDTH*SCREENHEIGHT;++i) {
        unsigned now=(pixels[i]&0xffffff)|(unsigned)v_overlaymask[i]<<24;
        differ|=now^overlay[i];overlay[i]=now;
    }
    else for(int i=0;i<SCREENWIDTH*SCREENHEIGHT;++i) {differ|=pixels[i]^overlay[i];overlay[i]=pixels[i];}
    changed=changed||differ!=0;
    return overlay;
}
// The sky sector a stretch of open air under a roof (a porch, a doorless
// recess off a yard; rooms.c) opens onto, the nearest through the open air
// itself; -1 for any other sector. Found once a level.
int openAirYard(int sector) {
    static std::vector<int> yard;
    static unsigned serial=~0u;
    if(serial!=r_levelserial||yard.size()!=(size_t)numsectors) {
        serial=r_levelserial;
        yard.assign(numsectors,-1);
        std::vector<int> queue;
        for(int s=0;s<numsectors;++s)if(sectors[s].ceilingpic==skyflatnum) {yard[s]=s;queue.push_back(s);}
        for(size_t k=0;k<queue.size();++k) {
            const sector_t &at=sectors[queue[k]];
            for(int n=0;n<at.linecount;++n) {
                const line_t *line=at.lines[n];
                if(!line->frontsector||!line->backsector)continue;
                int o=int((line->frontsector==&at?line->backsector:line->frontsector)-sectors);
                if(yard[o]>=0||!Rooms_Outside(o))continue;
                yard[o]=yard[queue[k]];queue.push_back(o);
            }
        }
        for(int s=0;s<numsectors;++s)if(sectors[s].ceilingpic==skyflatnum)yard[s]=-1;
    }
    return sector>=0&&sector<(int)yard.size()?yard[sector]:-1;
}
// Current sector light and heights for seam blending and contact shading;
// w is 0 indoors, and under a sky packOutdoor; the open air under a roof
// (openAirYard) carries its yard's, negated: lit by the same rule and the
// same enclosure and floor as the yard, so nothing shows on the roof's line.
// Then the fog bank's entries while it lies (fog.cpp) and the wear records (wear.cpp).
const std::vector<std::array<float,4>> &sectorInfo() {
    static std::vector<std::array<float,4>> info;   // kept from frame to frame: no allocation each
    info.assign(std::max(1,numsectors),{});
    if(worldPending)for(int i=0;i<numsectors;++i) {
        int yard=openAirYard(i);
        info[i]={roomPulse(&sectors[i],lighting(&sectors[i])),floorZ(&sectors[i]),ceilZ(&sectors[i]),
            sectors[i].ceilingpic==skyflatnum?packOutdoor(&sectors[i]):yard>=0?-packOutdoor(&sectors[yard]):0.0f};
    }
    if(worldPending)fogBankEntries(info);
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
    if(due&&M_CheckParm((char*)"-rendersettings")&&!I_Render3DSettingsOpen()) {
        // -rendersettingsgroup G -rendersettingstab T: that page of it (both from 0), -rendersettingsrow R: R rows down.
        I_Render3DSettingsMenu();
        for(int i=renderArg("-rendersettingsgroup",0);i>0;--i)I_Render3DSettingsKey(SDL_SCANCODE_PAGEDOWN,0);
        I_Render3DSettingsKey(SDL_SCANCODE_1+renderArg("-rendersettingstab",0),0);
        for(int i=renderArg("-rendersettingsrow",0);i>0;--i)I_Render3DSettingsKey(SDL_SCANCODE_DOWN,0);
        return false;
    }
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
        char rate[32]="";if(settings.fps==1)snprintf(rate,sizeof(rate)," · %.0f fps",fps);
        char title[256];snprintf(title,sizeof(title),"%s · %s · %dx%d%s%s · F4: settings · F7: screenshot · Esc: menu",
            Game_Title(),settings.accelerated?renderer:"Classic",width,height,rate,
            Game_HasEffect(GAME_EFFECT_FLASHLIGHT)?" · F: flashlight":"");
        SDL_SetWindowTitle(gameWindow,title);
    }
}
} // namespace doom3d


// Backend-independent parts of the renderer interface.
using namespace doom3d;
void I_Render3DStartFrame(void) {worldPending=false;v_overlayactive=false;}
void I_Render3DEarlySettings(void) {earlySettings();}
int I_Render3DIsAccelerated(void) {return settings.accelerated;}
#ifdef DOOM_DEVTOOLS
extern "C" boolean devInputHeld(event_t *event);   // local dev tools: the player held still (null: the mouse's look)
#endif
void I_Render3DLook(float delta) {
    if(!settings.look||!settings.accelerated||demoplayback)return;
#ifdef DOOM_DEVTOOLS
    if(devInputHeld(nullptr))return;
#endif
    if(settings.invertLook)delta=-delta;
    float gameLook;
    if(Game_LookPitch(&gameLook))Game_Look(-delta*0.002f);
    else {
        // Spinning free (Phobos, Guns turn you) the mouse looks up to straight up or down,
        // and upside down its up is the world's down; only the spin carries him over.
        float most=settings.phobosSpin&&Game_Phobos()?(float)doomPi/2-1e-3f:1.2f;
        pitch=std::clamp(pitch-(Game_ViewFlipped()!=bootsOver?-delta:delta)*0.002f,-most,most);
    }
}
void I_Render3DToggleFlashlight(void) {if(settings.accelerated&&Game_HasEffect(GAME_EFFECT_FLASHLIGHT))flashlightOn=!flashlightOn;}
void I_Render3DCenterView(void) {
    if(demoplayback)return;
    float gameLook;
    if(Game_LookPitch(&gameLook))Game_Look(-gameLook);
    else pitch=0;
}
void I_Render3DScreenshot(void) {screenshotPending=true;}
void I_Render3DCycleStats(void) {if(settings.accelerated){settings.fps=(settings.fps+1)%4;saveSettings();}}
const I_Render3DFrameProfile *I_Render3DLastFrame(void) {return &profile.frame;}
void I_Render3DProfileTeleported(void) {surfaceSelection.clear();surfaceLightTime=0;}
void I_Render3DScreenshotTo(const char *path) {profile.screenshotPath=path;screenshotPending=true;}
