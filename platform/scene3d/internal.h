/* Internal to the 3D scene in platform/scene3d/: the state and helpers its
   files share. The public interface is scene3d.h. */
#ifndef DOOM_SCENE3D_INTERNAL_H
#define DOOM_SCENE3D_INTERNAL_H
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <climits>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <set>
#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <ctime>
#include <functional>
#include <random>
#include <string>
#include <thread>
extern "C" {
#include "doomdef.h"
#include "doomstat.h"
#include "r_state.h"
#include "r_sky.h"
#include "doomdata.h"
#include "v_video.h"
#include "w_wad.h"
#include "z_zone.h"
#include "m_swap.h"
#include "m_argv.h"
#include "i_system.h"
#include "r_main.h"
#include "m_bbox.h"
extern int viewwindowx, viewwindowy;
extern boolean menuactive, automapactive, paused;
extern int numflats, numtextures;
extern float tourpitch;
extern boolean freeaim, classicaim, classicmovement, strafeboost;
extern int weaponbob; // The game's weapon bob (F4 "Weapon bob").
extern boolean quickconfirm; // The game's menu (F4 "Confirm quicksave and quickload").
extern fixed_t aimslope;
int P_PicAnimationNext(boolean istexture,int pic,int *next);
double I_GetTimeExact(void);
extern int gametime; // d_net.c: the clock tic the newest ticcmd was built at
void G_MouseTurn(int *last,int *pending);
void I_LatchMouse(void); // i_sdl_video.c: the mouse's newest motion, read late
}
#include <SDL3/SDL.h>
#include "../i_render3d.h"
#include "../game_profile.h"
#include "../derp_info.h"
#include "../game_materials.h"
#include "../scene3d.h"
#include "../emissive.h"
#include "../screen_glass.h"
#include "../painted_tiles.h"
#include "../surface_lighting.h"
#include "../baked_lighting.h"
#include "../weapon_lighting.h"
#include "../sprite_eyes.h"
#include "../sprite_eyes_stock.h"


namespace doom3d {
// hot: lava, or a damaging floor whose flat is mostly red or orange (heat haze).
struct SectorMist { bool pit=false,liquid=false,toxic=false,outdoor=false,reflective=false,hot=false,thick=false; std::array<float,3> color={0.24f,0.29f,0.32f}; };
// Map-space surface data, built once per level from the linedefs. Each cell
// stores its sector, the nearest open neighbor sector with blend weights
// (low byte for the floor, high byte for the ceiling, 255 = half the
// neighbor's light), the sector its light flows from (bakeFlow), and floor/ceiling distances to
// walls for contact shading.
constexpr uint16_t noSector=0xFFFF;
struct SeamCell { uint16_t own=noSector,neighbor=noSector,weight=0,flow=noSector; };
// Sunbeams (bakeShafts): from a sunlit indoor point (x,y,z), on a floor or a wall, back toward
// the sun for length units, until the ray leaves the building. floor: its sector's floor then.
struct SunShaft { float x,y,z,length,strength; int sector; float floor; };
// Walls: one strip per linedef side (index line*2+side) in an RGBA8 atlas
// (rgb baked light at half scale, a sun), columns along the side from its
// first drawn vertex at a, rows up from low to the highest ceiling its
// sectors and their neighbors reach. The atlas's right half holds bounce
// light at the same coordinates.
struct BakeStrip { int x=-1,y=0,width=0,height=0,sector=0; float low=0,high=0,scale=0,ax=0,ay=0,dx=0,dy=0,length=0; };
constexpr int atlasWidth=4096,atlasStride=2*atlasWidth;
// Four map-sized layers stacked in rows: floor light, ceiling light, floor
// bounce, ceiling bounce; rgb at half scale.
enum FlatLayer { floorLight, ceilingLight, floorBounce, ceilingBounce };
constexpr int flickerGroups=16;
// Static light at a point as things take it, without facing: the color
// (unflickered), where it comes from (unit directions weighted by each
// light's luminance, and their total), the strongest flicker group's share,
// and the strongest point light not far below that can cast a thing's shadow.
struct StaticSample {
    std::array<float,3> color={},towards={};float amount=0;
    std::array<float,3> bounce={}; // Filled by bakedLightAt: the bounce light there.
    int group=0;float groupShare=0;
    int shadow=-1;float shadowAmount=0;
    // Whole-level lighting's shadows (shadows.cpp): the light that comes from no lamp, each
    // as the way it comes from on balance: sky, the sky the
    // place sees (its length the share of a surface's view that is); glow, what the lit
    // surfaces round it send (a glowing pool's bank, a bright wall; in light's units). Light
    // from all sides alike cancels to nothing, and throws no shadow.
    std::array<float,3> sky={},glow={};
    // The way the lamps' and pools' light comes on balance, for the shadow it throws: as
    // towards, but a glowing pool counts as a light standing over it (its way lifted to half
    // as steep as it is long), so its shadow lies away from the pool over the floor and not
    // up the walls from underfoot.
    std::array<float,3> cast={};
    float pool=0;   // how much of amount is glowing pools' light
    std::array<float,3> lampWay={};   // the lamps' part of towards alone (no pools)
};
// Grid lighting cells (bakeStaticGrid).
constexpr float staticCell=32;
struct StaticCell { bool used=false; float low=0,high=0; StaticSample at[2]; };
constexpr float bounceStrength=1.0f;
// A light that casts things' floor shadows (addEnemyShadow).
struct ShadowLight { float x,y,z,strength; };
// Bake-only static lights near the eye, for the fog glow (collectSurfaceLights).
// spot: a glowing wall panel's half-height and normal, as its dynamic copy passes them to appendLight
// (zeros for a flat's light or a decoration's): its glow fills only the air in front of its wall.
struct StaticFog { float x,y,z,radius,strength;std::array<float,3> color;std::array<float,4> spot={0,0,0,0}; };
struct Debris { float x,y,z,vx,vy,vz,born,life,size; std::array<float,3> color; float light=1;bool lit=false;float stretch=0; }; // color unlit; lit: in the light round it (particleColour), else times light
std::array<float,3> particleColour(const std::array<float,3> &albedo,float x,float y,float z,bool wet);   // impacts.cpp: a small thing in the air, lit where it is
struct FlatDecal { float x,y,born,size;int sector;bool ceiling,flip; };
struct WallSpan { Point a,b;float bottom,top,anchor;int texture; };
// Where a ray meets a wall (firstSolidWall).
struct WallHit { int line=-1,side=0,section=0; float t=0; };

// scene.cpp
extern unsigned levelSerial;
// Sector heights at the previous tic; doors and lifts blend like moving things.
extern std::vector<std::array<float,2>> oldHeights;
extern float renderFraction;
extern bool ticSaved;   // the last tic's places are saved to blend from (beforeTic)
void saveWeaponPlace(); // weapon.cpp: the weapon's place at the last tic
// pacing.cpp: the game's clock when the frame's wait for the display last ended (now if none lately).
double steadyClock(double now);
#ifdef DOOM_DEVTOOLS
extern "C" double devTimeScale;   // local dev tools (devtools/): the game's clock slowed, game seconds per real second
extern bool devBankRed;           // and the fog bank drawn red, to see where it lies and where it breaks off
extern float devWeatherPace;      // and weather's build-up run that many times as fast (weatherTicSeconds)
#endif
// The seconds a tic of weather build-up stands for (windows letting it in, the drafts' trails, the
// films blown in, the fog and dry ice banks; and their drying and fading): a tic's, or a local
// dev tool's faster pace. What falls keeps the game's clock.
#ifdef DOOM_DEVTOOLS
inline float weatherTicSeconds() {return devWeatherPace/TICRATE;}
#else
constexpr float weatherTicSeconds() {return 1.0f/TICRATE;}
#endif
void interpolatedPosition(const mobj_t &thing,float fraction,float &x,float &y,float &z);
// While beforeTic runs the watchers that look at every thing (watchSpray,
// watchGlass): the sectors' things listed once, in their lists' order; else null.
extern const std::vector<mobj_t*> *ticThings;

// config.cpp
extern const char *graphicsConfig;
extern bool graphicsConfigNamed; // -graphics named it: read whatever its configVersion
void loadSettings();
void saveSettings();   // graphics.cfg as the settings are now, nothing applied

// window_size.cpp: the window made settings.windowSize, when that changed.
void applyWindowSize();

// images.cpp
void imagesShutdown();
Image upload(int w,int h,const byte *pixels,int left=0,int top=0,const byte *mask=nullptr,const byte *normals=nullptr,const byte *glass=nullptr,bool mipmaps=false);
// Phobos: glowing slime frozen solid, frosted (grey 0) to dead grey ice (16), its glow mask at
// glow eighths (0-8), batched under keys of their own (images.cpp).
constexpr int frozenFlatOffset=1<<24,frozenWallOffset=1<<26,stillFlatOffset=1<<28,sleepingOffset=1<<29;
inline int frozenFlatKey(int lump,int grey,int glow) {return -1-(frozenFlatOffset+lump*256+grey*9+glow);}
inline int frozenWallKey(int texture,int grey,int glow) {return -1-(frozenWallOffset+texture*256+grey*9+glow);}
inline bool isFrozenFlatKey(int key) {return key<0&&-1-key>=frozenFlatOffset&&-1-key<stillFlatOffset;}
inline int stillFlatKey(int lump) {return -1-(stillFlatOffset+lump);}
inline bool isStillFlatKey(int key) {return key<0&&-1-key>=stillFlatOffset&&-1-key<stillFlatOffset+(1<<27);}
// Paint lit as a lamp (lit_paint.cpp): the flat glowing all over, in the upper half of the
// still flats' range.
inline int litPaintKey(int lump) {return -1-(stillFlatOffset+(1<<27)+lump);}
inline bool isLitPaintKey(int key) {return key<0&&-1-key>=stillFlatOffset+(1<<27)&&-1-key<sleepingOffset;}
Image &litPaintFlat(int lump);
// lit_paint.cpp: this sector's floor or ceiling is paint (Game_PaintLamp) the mapper lit
// well above the rooms beside it, so it glows. Judged once per level, at its first levels.
bool litPaint(int sector,bool ceiling);
// The image a sector's floor or ceiling shows when nothing else (ice) replaces it.
Image &sectorFlatImage(int sector,bool ceiling);
int sectorFlatKey(int sector,bool ceiling);
// The library: a sleeper's sprite frame with its blood left out (Game_ThingAsleep).
inline int sleepingKey(int lump) {return -1-(sleepingOffset+lump);}
// Enemy-appropriate blood: the game's blood sprite frame recoloured to a tint (bloodRemap).
inline int bloodSpriteKey(int lump,int tint) {return -1-(sleepingOffset+(1<<27)+lump*4+tint);}
Image &bloodSpriteImage(int lump,int tint);
Image &stillFlat(int lump);   // a liquid stilled under forming ice
Image &sleepingImage(int lump);
// waist.cpp: a soldier firing as it walks, a torso's frame over a run's.
int waistKey(int torso,bool torsoFlip,int legs,bool legsFlip);
Image &waistImage(int key);
Image &frozenFlat(int lump,int grey,int glow);
Image &frozenWall(int texture,int grey,int glow);
const std::array<float,3> &averageColor(Image &image);
const byte *playpal();   // the game's palette (768 bytes), copied once
// images.cpp: the relief a wall (or flat) texture gets, RGBA8 (slope u, v, grain), for dev tools;
// seamsOnly false: as before seams were told from tile interiors.
std::vector<byte> reliefPreview(Image &image,bool flat,bool seamsOnly=true);
// relaid_walls.cpp: an oversized brick texture (w x h index, coverage pairs)
// re-laid with bricks 1/split the size from its own artwork; empty when it is
// no plain brick texture; why, when given, says what it found.
std::vector<byte> rebrickedWall(int w,int h,const std::vector<byte> &pixels,int split,std::string *why=nullptr);
// images.cpp: re-bricked wall textures load again once the switch changes (applySettings).
void rebrickSwitched();
std::array<float,3> paletteColor(const std::array<float,3> &color);
const std::array<float,3> &brightestColor(Image &image);
std::array<float,3> texelColor(Image &image);
// A sprite's image as lumpImage makes it, in steps: decodeSprite (nullptr, or
// what is wrong with the patch) and finishSpriteArt (normals, eyes, flash,
// glow) touch nothing shared, so the precache runs them on worker threads;
// keepSprite uploads the result under the lump's key. spriteEyes and
// spriteFlash say which of the two a lump gets (main thread only).
struct SpriteArt { int w=0,h=0,left=0,top=0; std::vector<byte> pixels,normals; std::array<float,3> glow={1,1,1}; float glowWeight=0,glowHeight=0.85f; };
const char *decodeSprite(const byte *data,int size,SpriteArt &art);
void finishSpriteArt(SpriteArt &art,const char *name,const byte *palette,bool eyes,bool flash,bool slime=false);
Image &keepSprite(int lump,SpriteArt &art);
// kick_view.cpp: the kicking leg's frame (kick_frames_stock.h), as painted or relit as the guns are.
Image &kickImage(int frame,bool relit);
bool spriteEyes(int lump);
bool spriteFlash(int lump);
bool spriteSlime(int lump);   // a barrel standing: its slime top glows (main thread only, as the two above)
// Every PLAYPAL palette as I_SetPalette hands it on (gamma applied), for the
// palette lookups built ahead (gpu3d/device.cpp).
std::vector<std::array<unsigned,256>> gamePalettes();

// sprite_precache.cpp: every sprite frame the level's things can show, loaded
// under the loading screen instead of at first sight.
void precacheSprites();
void precacheWalls();   // images.cpp: every wall texture the map's sides name, and its switches' other faces
// screen_text.cpp: text running down a dim-screen texture's monitors (SPACEW3). pixels: index and
// coverage pairs with the painted text brightened; mask: 255 on that text; glass, frame: as
// doom_screen_glass gives them. Update: once a frame, writes the screens that stepped.
void screenTextRegister(int index,int w,int h,const byte *pixels,const byte *mask,const byte *glass,const byte *frame,const byte *palette);
void screenTextUpdate();
void screenTextShutdown();
void precacheWeaponImage(int lump);   // weapon.cpp: a weapon frame's relit image, ahead of its first showing
Image *weaponPackImage(int lump);     // weapon_pack.cpp: the frame rendered from a model, or null

// sprite_normals.cpp
std::vector<float> spriteTubeHeights(int w,int h,const byte *pixels);   // texels' true tube depth, 0 where empty
std::vector<byte> spriteNormals(int w,int h,const byte *pixels);

// map.cpp
void mapShutdown();
// How a liquid flat looks at its shores (shore ripples) and how much of its
// contrast remains far away (0: not a liquid).
enum { LIQUID_LOOK_NONE,LIQUID_LOOK_WATER,LIQUID_LOOK_THICK,LIQUID_LOOK_BLOOD,LIQUID_LOOK_HOT };
int liquidLook(int pic);
// Ice floors (Game_IceFlat): deep grooves, glassy highlights and a sheen.
bool iceFlat(int pic);
float liquidDetailFloor(int pic);
extern std::vector<std::vector<Point>> floors;
extern std::vector<std::vector<int>> sectorFloors;
extern std::vector<std::array<float,4>> floorBounds;
extern std::vector<SectorMist> sectorMist;
extern float fogReferenceHeight;
extern std::vector<SeamCell> seamCells;
// RGBA8 per cell: floor and ceiling contact distance, baked sun visibility,
// light flow weight.
extern std::vector<uint8_t> contactCells;
extern float mapOrigin[2],mapCell;
extern int mapWidth,mapHeight;
std::vector<Point> clip(const std::vector<Point> &poly,Point origin,Point direction,bool right);
void buildMap();
float sectorHeight(const sector_t *sector,bool ceiling);
// The level's mood: the authored episode's (Game_Mood, with settings.authored) or the plain derived one:
// outdoor light, sun, window light turned round, rain and its angle.
const game_mood &levelMood();
float lighting(const sector_t *sector,bool bright=false);

// book_view.cpp: the library's book aimed at, outlined (the page: I_Render3DPageDraw)
void buildBookAim(const Uniforms &camera);
// The fall's rush (rush.cpp): smear, widening and shake as he falls; view: the world view's place in uv.
void fallRush(Uniforms &camera,const float view[4]);
// The player's eyes (eyes.cpp): lids and darkness from the game; view as for fallRush.
void closedEyes(Uniforms &camera,const float view[4]);
// What the library's people say, over their heads (speech.cpp).
void buildSpeech(const Uniforms &camera,float fraction);

// carried.cpp: the renderer's world stepped with the game's wrap (Game_WorldShift)
void followCarry();
int lineCarried(int line,float dx,float dy);
int sectorCarried(float x,float y);

// caustics.cpp
void causticsShutdown();
extern std::vector<int> causticFrames;
extern int causticSpeed;
void buildCaustics();

// liquid_edges.cpp
// A glowing liquid's edge strip, from its edge texture (height 0: none):
// per row up from the foot, 64 texels of palette index, paint and glow.
struct EdgeStrip { int kind=0,height=0,texture=-1; std::vector<byte> index,paint,glow; };
const EdgeStrip &edgeStrip(int kind);
int liquidGlowKind(int sector);
bool paintsOwnEdge(int texture,int kind);
const EdgeStrip *levelEdgeStrip();
void buildShore();
// Liquid edges from buildShore: a to b, the liquid sector, and the unit
// normal pointing into the liquid. shoreDistance: signed distance per map
// cell (into the liquid positive), as the shore texture holds it.
struct ShoreEdge { float ax,ay,bx,by,nx,ny; int sector; };
extern std::vector<ShoreEdge> shoreEdges;
float shoreDistanceAt(float x,float y);

// bake_atlas.cpp
void bakeAtlasNewLevel();
void bakeAtlasShutdown();
extern std::vector<BakeStrip> bakeStrips;
extern std::vector<int> bakeOrder;
extern int atlasHeight;
// The level's baked light (wallBakeCells, flatLightCells and the direction maps below, through
// lightTarget's macros): defined by these names, read and written by the others.
extern std::vector<uint8_t> liveWallBakeCells;
extern std::vector<uint8_t> liveFlatLightCells;
extern BakeMap bakeMap;
void layoutBake();
void parallelFor(int count,const std::function<void(int)> &work,unsigned threads=16);
// gpu_trace_check.cpp: DERP_GPUTRACECHECK's report after a level's bakes.
void gpuTraceCheck();
void gpuTraceShutdown();   // gpu_trace.cpp
// The bake grid (bake_lights.cpp): its size in cells and a cell's size in units.
void bakeGridSize(int &width,int &height);
float bakeGridCellSize();
void uploadWallBake();
void uploadFlatLight();
void bakeWallCoordinates(const line_t &line,int side,float along,Vertex &vertex,bool strip=false);

// bake_sun.cpp: the sun's light baked where sun.cpp placed it.
void sunNewLevel();
void sunShutdown();
extern bool sunBaked;
extern float sunLevel;
// sun.cpp: where the sun stands and how it shines, placed as the level
// starts (weather.cpp) from the sky texture.
void placeSun();
// Where the sky's painting has its sun (radians, map angles), placeSun's last.
extern float sunPaintedYaw;
// sun_aim.cpp: the sun placed and baked anew, as a change of hour does it.
void sunPlaceAgain();
// map_info.cpp: the map's DERPINFO as the renderer goes by it, DERP mode's authored look
// (Game_AuthoredInfo) under the WADs' and the player's (DerpInfo_Map). mapInfoVersion: one more
// each time it changes; mapInfoAgain: read the authored look anew.
const derp_map &mapInfo();
unsigned mapInfoVersion();
void mapInfoAgain();
#ifdef DOOM_DEVTOOLS
// Dev tools (devtools/authoring.cpp): the authored file in the source tree as it stands, and
// the sun aims held for this map, not yet saved.
bool devAuthoredText(const char *file,std::string &text);
void devAuthoredHeld(derp_map &info);
// The Aim the sun and Sun back keys (sun_aim.cpp) while authoring: true when they were the
// authored look's (an aim held or let go), false for the player's derpinfo.txt.
bool devSunHold(int hour,bool back,float angle,float height);
#endif
extern std::array<float,3> sunTint;
extern float sunAzimuth[2];
extern float sunDirection[3];
// How far the sun's rays fan out in its bake, 0 under a clear sky to 1 under
// full cloud (placeSun, from overcast).
extern float sunSpread;
// The sun drawn in the sky: yaw of the baked direction, and its strength
// from how bright the sky texture is around it (a dark sky shows only a hint).
extern float sunYaw,sunGlow;
extern float sunElevation,sunDiscElevation,sunShade,sunGain;
// (Tried 2026-11-13 and taken out again: whole-level yards centred on their mapper's level,
// open shade at 0.75 of it and sunlit ground at 1.3. It read as too bright in play; a yard
// that is too dark is the lesser fault.)
// F4 "Light by logic" (a test, whole-level lighting only): the mapper's light levels set no
// brightness at all. Lamps shine in full by their artwork (surface_lights.cpp's lampLevel), rooms
// keep no fill (room_fill.cpp), and daylight is the hour's, the same on every map
// (daylightLevel): the yards' levels, the sun's and the sky's. What a level does to its light
// while it runs stays: switches, flickers, traps.
inline bool lightByLogic() {return settings.lightByLogic&&settings.lightModel==1;}
// The level the sun and the sky shine at, from the sky sectors' typical level (mapped).
float daylightLevel(float mapped);
std::array<float,3> skyHorizon();
std::array<float,3> torchColor();
// Light through stained glass, baked with the static lights (glass_light.cpp).
void findGlassWindows();
void addGlassLight(const BakeMap &map,int sector,float x,float y,float z,const float normal[3],float out[3]);
void glassLightShutdown();
extern std::vector<SunShaft> sunShafts;
// Sectors whose air sunbeams and sunlit dust show in (bakeShafts): under a
// ceiling, or in a sky opening that is enclosed and at most 400x400 units in
// area, a ceiling hole or a walled pocket the light drops into. A courtyard
// or open ground, however walled in, is plain daylight with no beams in it.
extern std::vector<char> beamAir;
// spread: sunSpread (sun.cpp), how far the sun's rays fan out under cloud.
int sunCell(const BakeMap &map,size_t i,const float direction[3],float spread);
void sunStrip(const BakeMap &map,const BakeStrip &strip,const float direction[3],float spread);
void bakeSun();
float packOutdoor(const sector_t *sector,float *enclosed=nullptr,float *room=nullptr);
// The one light rule (bake_sun.cpp; sector_light.glsl's lightRule): what it
// reads of the frame, and its result: the level, and the parts of it that
// take the sun's and the sky's tint.
// even: how evenly a whiteout or thick fog lights open ground (flatAir in the shader).
struct LightRuleInputs { float sun=0,shade=0,gain=0,skyLight=0,skyCast=0,even=0; bool windowLight=false,whole=false; };
struct LightRule { float level=0,sun=0,sky=0; };
LightRuleInputs lightRuleInputs();
LightRule lightRule(const LightRuleInputs &in,bool sky,float room,float light,float visible,float open);
float roomLight(const sector_t *sector);
float sunLightAt(float x,float y,const sector_t *sector,float light);
// scene.cpp: the sky sector the open air under a roof opens onto (-1: none, or a sky sector itself).
int openAirYard(int sector);
float seamLightAt(float x,float y,const sector_t *sector,float fallback);

// bake_lights.cpp
void lightBakeNewLevel();
void lightBakeShutdown();
// Static lights were baked from these inputs (see lightBakeInputs); -1
// before the level's first light bake. The serial counts light bakes.
extern int lightBakeKey;
extern unsigned lightBakeSerial;
// Counts changes to the bake's lights and map between bakes (relight.cpp): a door moving, a lamp shot
// out. What traced them (tracedStatic) is stale after either.
extern unsigned liveLightSerial;
extern bool bakedLightsActive;
// Per sector, how far its light level drops to the room around it while baked
// lights are on (classifyLampSectors): the brightness its mapper gave it for
// the lamps over it, which the bake now draws itself.
extern std::vector<float> lampDrop;
extern bool bakeOnlyActive;
// The baked lights, kept for sprites, and a coarse grid listing the lights
// that can reach each of its cells.
extern std::vector<BakeLight> liveBakeSources;   // bakeSources, through lightTarget's macro below
// Per bakeSources entry: the merge cell of the glowing tiles it stands for
// (shot_lights.cpp breaks them by it), all zeros for other lights.
extern std::vector<SurfaceLightKey> bakeSourceKeys;
// Re-merges bakeSources[n], a merged surface light, from the tiles of its
// merge cell not shot out, as bakeLights merged it (strength 0 with none
// left), and lists it in bakeGrid wherever it now reaches.
void remergeBakeSource(int n);
extern int firstWindowSource;
// Whether bakeSources[n] is the window lamp of a door shut in map (no
// height): no ray gets in to it, so the bake passes it by.
bool shutWindowLamp(const BakeMap &map,int n);
float capWindowLight(float rgb[3]);
// bake_doorways.cpp: an opening from a brighter room lights the darker one beside it. Appends its lamps
// to sources and, per lamp, the shut door it sits in to doors (-1: none); lamps: how many of sources are
// the level's own lamps (the rooms' colors come from them).
void addDoorwayLights(std::vector<BakeLight> &sources,std::vector<int> &doors,int lamps);
extern std::vector<std::vector<int>> bakeGrid;
// Bake-only lights: liquid pools as area lights, listed in bakeGrid after the
// point lights; each point light's flicker group (0: steady); and per wall
// atlas texel and floor/ceiling cell where the static light comes from.
extern std::vector<BakeArea> bakeAreas;
extern std::vector<uint8_t> sourceGroups;
extern std::vector<uint8_t> liveWallDirectionCells,liveFlatDirectionCells;
// Prepared shots (relight.cpp, PLAN-destructible-lights.md step 2a): the light worker bakes a shot
// that hasn't happened yet, on copies of the level's light. On its threads (and on the frame
// thread while it hands a prepared bounce solve its light) lightTarget points at the copies, and
// these names mean them; everywhere else they mean the level's own, which the frame draws, lights
// things and the weapon from, and uploads. relightFor's helpers take the caller's target.
struct LightTarget {
    std::vector<uint8_t> flat,wall,flatDirection,wallDirection;
    std::vector<BakeLight> sources;
};
extern thread_local LightTarget *lightTarget;
// Work nobody waits for (a prepared shot's, on the light worker): on the processor whatever the
// re-bakes' setting (gpuRelightOn), so the frames keep the card. (On the worker thread alone, the
// game's cores left free, a fixture took 3 to over 6 s on the PC: too slow to be ready in time.)
extern thread_local bool backgroundLight;
inline std::vector<uint8_t> &lightLayer(std::vector<uint8_t> &live,std::vector<uint8_t> LightTarget::*own) {return lightTarget?lightTarget->*own:live;}
inline std::vector<BakeLight> &bakeSourcesNow() {return lightTarget?lightTarget->sources:liveBakeSources;}
#define flatLightCells (lightLayer(liveFlatLightCells,&LightTarget::flat))
#define wallBakeCells (lightLayer(liveWallBakeCells,&LightTarget::wall))
#define flatDirectionCells (lightLayer(liveFlatDirectionCells,&LightTarget::flatDirection))
#define wallDirectionCells (lightLayer(liveWallDirectionCells,&LightTarget::wallDirection))
#define bakeSources (bakeSourcesNow())
// Changes whenever the level's light texels change for real (relight.cpp: a re-bake applied, a
// prepared shot put in; bake_bounce.cpp: a bounce solve committed): prepared shots hold while it doesn't.
extern unsigned lightCellsSerial;
// bakeSources[n] as remergeBakeSource would leave it if the tiles in also were shot out too
// (relight.cpp's prepared shots); nothing else changes. And the bake grid listing n wherever that
// light reaches, as remergeBakeSource does for the light it makes.
BakeLight remergedBakeSource(int n,const std::set<SurfaceLightKey> &also);
void listBakeSource(int n,const BakeLight &light);
// A prepared shot's bounce (bake_bounce.cpp): a region's solve beside the re-bounce's. Started on the
// frame thread with lightTarget at the shot's copies; it takes them and runs on the light worker. Once
// done, the committer (called on the light worker, lightTarget at the copies) puts it into them.
bool preparedBounceStart(const std::vector<std::array<float,4>> &rects);
bool preparedBounceDone();
const std::vector<int> &preparedBounceStrips();
std::function<void()> preparedBounceCommitter();
void preparedBounceStop();
// Whether the direction maps' first halves hold the lamps' directions and
// flicker groups (bake-only lights, or lamps that switch).
extern bool bakeDirected;
// bake_basis.cpp: directional baked light (Settings::bakedBumps), the HL2
// basis. Lamp light per wall texel (row*atlasWidth+column) and floor, then
// ceiling cell, split over the three directions; the sector's own light
// counts this much into each, coming from everywhere.
extern std::vector<std::array<uint16_t,3>> wallLampLobes,flatLampLobes;
constexpr float ambientLobe=0.15f;
void addLobes(const float normal[3],const float towards[3],float weight,float lobes[3]);
void keepLampLobes(std::array<uint16_t,3> &kept,const float lobes[3]);
void lampLobes(const std::array<uint16_t,3> &kept,float lobes[3]);
void writeBasis(uint8_t *out,const float lobes[3]);
void writeLampBasis();
void uploadDirections();
const std::vector<int> &bakeGridLights(float x,float y);
int lightBakeInputs();
// blockers: per light and pool (bakeSources, then bakeAreas), the wall that last stopped its rays, or null.
void lightCell(const BakeMap &map,size_t i,bool only,int *blockers=nullptr);
void lightStrip(const BakeMap &map,const BakeStrip &strip,bool only,int *blockers=nullptr);
// The same round moved sectors (boxes; moved: per sector), from the map before to the map now,
// tracing only the lights whose way to the texel crosses a box (bake_moved.cpp).
// Lamps that went out or were switched, pair by pair: as each shone, as it shines now, and its flicker group.
struct ChangedLamps { std::vector<BakeLight> was,now; std::vector<uint8_t> group; };
void lightCellChanged(const BakeMap &map,const ChangedLamps &lamps,size_t i,bool only);
void lightStripChanged(const BakeMap &map,const ChangedLamps &lamps,const BakeStrip &strip,bool only);
void lightCellMoved(const BakeMap &before,const BakeMap &now,const std::vector<std::array<float,4>> &boxes,const std::vector<char> &moved,size_t i);
void lightStripMoved(const BakeMap &before,const BakeMap &now,const std::vector<std::array<float,4>> &boxes,const std::vector<char> &moved,const BakeStrip &strip);
std::array<float,3> skyColor();
std::array<float,3> skyLightColor();   // skyColor under the hour and the cloud (bake_lights.cpp)
void bakeLights();

// bake_indirect.cpp
void indirectNewLevel();
void indirectShutdown();
// Bounce light was baked from these inputs (see bounceInputs); -1: not yet.
extern int bounceKey;
// Decorations in the bakes (settings.thingShadows) as last synced; -1 before
// the level's first sync. Sky/occlusion and flow were baked with this key.
extern int bakeThingsKey,ambientKey,flowKey;
extern float skyLevel;
extern std::array<float,3> skyTint;
int bounceInputs();
void bakeBounce();
// The bounce round a place whose light changed, solved again in the background
// (bake_bounce.cpp; whole-level lighting). relight.cpp marks the places, ticks
// it each frame (busy: a re-light runs or waits), and stops it before a re-light.
void reBounceMark(float x0,float y0,float x1,float y1);
void reBounceTick(bool busy);
void cancelRebounce();
std::vector<uint16_t> litCells();   // per map cell, the lamp light on its floor and ceiling
void dimBounce(const std::vector<uint16_t> &before,const int bounds[4]);   // the bounce goes down where lamp light went
bool reBounceBusy();   // a solve runs or places wait
void dropRebounce();
// A wall strip's coarse samples (columns x rows, Channels each) smoothed 1-2-1
// along and up the strip before they are spread over its texels, so one sample
// the rays happened to read high or low no longer spreads into a blotch the
// size of the sample spacing (MAP13's facades under the firestorm's orange sky).
template<class Value>
void smoothCoarse(std::vector<Value> &coarse,int columns,int rows) {
    if(columns*rows<2)return;
    std::vector<Value> pass=coarse;
    auto blur=[&](std::vector<Value> &to,const std::vector<Value> &from,bool along) {
        for(int y=0;y<rows;++y)for(int x=0;x<columns;++x) {
            int n=along?columns:rows,i=along?x:y;
            auto at=[&](int k){k=std::clamp(k,0,n-1);return from[along?(size_t)y*columns+k:(size_t)k*columns+x];};
            Value &out=to[(size_t)y*columns+x];
            for(size_t c=0;c<out.size();++c)out[c]=(at(i-1)[c]+2*at(i)[c]+at(i+1)[c])*0.25f;
        }
    };
    blur(pass,coarse,true);blur(coarse,pass,false);
}
void syncBakeThings();
void bakeAmbient();
void bakeFlow();
float flowLightAt(float x,float y,const sector_t *sector,float light);

// relight.cpp
void stopRelight();
// A merged light (a bakeSourceKeys entry) lost shot-out tiles: its source
// keeps the tiles left (remergeBakeSource) or goes dark, and the next
// background re-bake follows.
void relightBrokenLight(const SurfaceLightKey &cell);
// Waits for the background re-bake around moving sectors (relightMovingSectors)
// and, with apply, uploads what it baked. Every full bake calls it first.
void finishRelight(bool apply=true);
void relightMovingSectors();
// The light bake is fresh: it holds the sectors at bakeMap's heights (bakeLights calls it).
void relightBaked();
// work(0..count-1) on the calling thread and the re-bakes' few helper threads. One caller at a time.
void relightFor(int count,const std::function<void(int)> &work);
// Whether a re-bake is in flight (it writes the floor light's rgb meanwhile).
bool relightRunning();
// The light worker: one long-lived thread running the re-bakes and the bounce's solves in turn, in the
// order posted (the only caller of relightFor). Wait blocks until all posted has run: level changes and
// full bakes only, never a played frame.
void lightWorkerPost(std::function<void()> task);
void lightWorkerWait();
// work(0..count-1) on the calling thread and a few helper threads kept up, for slices of whole-map work in
// play on the game's thread. One caller at a time.
void playFor(int count,const std::function<void(int)> &work);
// Light texture uploads after a re-bake or a solve, one texture a frame (relightMovingSectors); each reads
// cells as they are when it goes. Frame thread only.
void queueLightUpload(const GpuTextureRef &texture,const std::vector<std::array<int,4>> &rects,const std::vector<uint8_t> &cells,int bytesPerRow);
bool lightUploadsPending();
// The wall strips (of bakeOrder) whose ends' box overlaps any of boxes (x0, y0, x1, y1), each once, in
// index order. Frame thread only.
void stripsNear(const std::vector<std::array<float,4>> &boxes,std::vector<int> &out);
// litCells over only the window dimBounce reads round bounds (map cells); lit keeps the level's size.
void litCellsAround(const int bounds[4],std::vector<uint16_t> &lit);

// thing_light.cpp
void clearStaticGrid();
extern int staticWidth,staticHeight;
extern unsigned staticSerial;
extern std::vector<StaticCell> staticGrid;
bool gridLightActive();
StaticCell staticCellAt(const BakeMap &map,int gx,int gy);
void bakeStaticGrid();
std::array<float,3> bakedLightAt(float x,float y,float z,const sector_t *sector,StaticSample *sample=nullptr);
unsigned packedLampTint(const StaticSample &statics,float light);
bool staticShadow(const StaticSample &sample,float z,ShadowLight &light);

// lights.cpp
void clearFlashPulses();
bool ownFlashLit();
void lightsShutdown();
extern std::array<const mobj_t*,maxLights> lightSources;
// Lights kept only for the fog glow (bake-only static lights): no surface,
// sprite or shadow uses them.
extern std::array<bool,maxLights> fogOnly;
// For profiling: each light's I_RENDER3D_LIGHT_* kind, set by its collector.
extern std::array<uint8_t,maxLights> lightKinds;
extern uint8_t lightKind;
void muzzleFlash(mobj_t *source,int weapon,int projectile);
// room_pulse.cpp: a flash lifts the light of its room while it shines. Begin
// starts a frame's list, Add gives one flash (the collector, lights.cpp), and
// roomPulse is a sector's light as drawn with it (level: its lighting()).
void roomPulseBegin();
void roomPulseAdd(float x,float y,float radius,float strength);
float roomPulse(const sector_t *sector,float level);
void roomPulseShutdown();
void sparkFlash(float x,float y,float z,float radius,float strength,const std::array<float,3> &color);
bool sparkGlow(float x,float y,float z,float glow[3]);   // what the sparks' flashes add at (x,y,z), for specks in the air
void appendLight(float x,float y,float z,float radius,float strength,const std::array<float,3> &color,const mobj_t *source,float directionality,bool occluded=true,float baked=0,
                 const std::array<float,4> &spot={});
bool decorationLight(const mobj_t &thing);
int flickerGroup(const mobj_t &thing);
float groupFlicker(int group);
// Flicker groups 1..lampGroupCount belong to switching lamps (bake_lights.cpp).
extern int lampGroupCount;
extern float lampGroupFlicker[flickerGroups];
void updateLampGroups(const float eye[3]);
BakeLight decorationSource(const mobj_t &thing);
float decorationFlicker(const mobj_t &thing);   // its flame's level this tic, 1 at its brightest frame
extern std::vector<StaticFog> staticFog;
void collectFlashes(float fraction,const Uniforms &camera);
float flashAt(float x,float y,float z,const Flash &f);
void findDoors();
bool doorLine(int line);                   // a face of a door (findDoors: a sector shut at level start)
void selectFogLights(const Uniforms &camera);
void assignLightMasks();
void skipProfiledLights();
// The perf test's feature knob (I_Render3DProfileKnob) on the frame's camera; nothing unless one is set.
void applyProfileKnob(Uniforms &camera);   // profile_knobs.cpp
extern std::array<float,maxLights> lightSliceCost;
void buildLightWords();

// surface_lights.cpp
void clearSurfaceLights();
extern std::vector<SurfaceLight> surfaceLights;
extern float surfaceEye[3];
extern SurfaceLightSelection surfaceSelection;
extern uint64_t surfaceLightTime;
std::array<float,2> panelNormal(const SurfaceLight &light);
std::vector<SurfaceLight> mergedSurfaceLights(std::vector<SurfaceLight> tiles);
void collectFlatLights();
void collectSurfaceLights();
void wallLights(const line_t &line,int side,Point a,Point b,float bottom,float top,int tex,float anchor,int span);
// The glowing tiles of a wall piece (u: the texture offset at its start, len its length): the
// texture repeats whose glow's centre lies on it. A piece that shows a repeat's glow without
// its centre (a strip lower than the texture is tall, the end of a light grid) has that one
// repeat, lit by the share of its glow the piece shows; none (first past last) if it shows
// under a twentieth of it. missU, missV: the centre lies off the piece that way.
struct GlowTiles {int firstU,lastU,firstV,lastV;float share;bool missU,missV;};
GlowTiles glowTiles(const Image &image,float u,float len,float anchor,float bottom,float top);
// The merge cell (mergeSurfaceLights key) a tile's light joins.
SurfaceLightKey surfaceCellOf(const SurfaceLight &tile);
unsigned surfaceGroupSerial();   // changes whenever a merge cell is grouped anew
unsigned lampLevelSerial();      // changes whenever a lamp's lit level does
// surface_light_keep.cpp: whether last frame's glowing tiles still stand
// (storeys: the library's, which follow the eye), their shine brought up
// to this frame; false: collect them anew this frame. Their merged lights,
// merged again only when the tiles or their groups changed.
bool keepSurfaceLights(bool storeys);
// geometry.cpp: the texture a side's missing upper or lower section borrows (0: none).
int wallFillTexture(const line_t &line,int side,bool lower);
const std::vector<SurfaceLight> &keptMergedLights();
void forgetKeptSurfaceLights();
// Lamps of a sector whose light flickers follow it: how far on this tic
// (0-1), and whether switched off (blinking and strobing sectors only).
// With an image: whether that texture follows (screens stay on beside lamps).
bool lampFlickers(int sector);
bool lampFlickers(int sector,const Image &image);
float lampShine(int sector);
bool lampOut(int sector,const Image &image);
// A sector's light level as drawn: one whose lamps flicker by group keeps
// its low, the lamps' light carrying the swing.
int lampLightLevel(const sector_t *sector);
int lampPeakLevel(int sector);
bool lampLevelsFollow();   // lamps follow a switched sector's level; true when any changed
// room_fill.cpp: whole-level lighting's one light with no source, per room. Solved when the bakes are done.
void solveRoomFill();
float roomFillNow(const sector_t *sector);
float yardLevelNow(const sector_t *sector);   // a sky sector's level from its yard; -1: none
void roomFillShutdown();
bool lampGroupSector(int sector); // Its lamps hold a bake flicker group.
// Vertices from first on carry a fading sector's lamp glow for their
// glowing texels (Vertex::lampTint alpha on walls and flats).
void lampGlow(std::vector<Vertex> &out,size_t first,const sector_t *sector,const Image &image);

// geometry.cpp
void quad(std::vector<Vertex> &out,Vertex a,Vertex b,Vertex c,Vertex d);
void geometry(const Uniforms &camera,float fraction);

// shadows.cpp
struct RagdollCard;
void addEnemyShadow(const mobj_t &thing,const Image &image,int lump,bool flip,
                    float x,float y,float z,const Uniforms &camera,const ShadowLight *light=nullptr,bool anyThing=false,const RagdollCard *card=nullptr);
// Whole-level lighting: the light a thing's shadow is seen against where it stands (lamps,
// bounce, sun, fill), how dark a light's shadow is against it (the light's share of it), and
// the thing's outline thrown by a light onto the walls behind it.
// A thing's shadow volume from a dynamic light (shadows.cpp); false if that light casts none this frame.
bool shadowVolume(const mobj_t &thing,const Image &image,bool flip,float x,float y,float z,const ShadowLight &light,int kind,float squash=1,const RagdollCard *card=nullptr);
bool leafInView(int leaf);   // geometry.cpp: among the leaves the eye can see this frame
bool shadowLeafSeen(int leaf);   // thing_view.cpp: leafInView while the things are culled by sight, else always
void shadowVolumeFrame();   // shadows.cpp: once a frame, before any thing casts
float shadowLevel(float x,float y,const sector_t *sector);
float castOpacity(const ShadowLight &light,float level);
void wallShadow(const mobj_t &thing,const Image &image,int lump,bool flip,float x,float y,float z,const Uniforms &camera,const ShadowLight &light,float opacity);
void flashlightSilhouette(const mobj_t &thing,const Image &image,int lump,bool flip,float x,float y,float z,const Uniforms &camera,const Flash &beam,const RagdollCard *card=nullptr);

// atmosphere.cpp
void buildShafts(const Uniforms &camera);
void buildMist(const Uniforms &camera);
// A mist sheet's triangle and how far along the view it lies (buildMist sorts them far to near).
struct MistTriangle { float depth; Vertex v[3]; };
void buildHeat(const Uniforms &camera);
// ground_heat.cpp: the heat of the day shimmering over the far open ground (into heatVertices).
void buildGroundHeat(const Uniforms &camera);
void buildDust(const Uniforms &camera);
// embers.cpp
void buildEmbers(const Uniforms &camera);

// decompression.cpp (Phobos)
void buildDecompression(const Uniforms &camera);
unsigned bodyFrost(const mobj_t &thing);   // 0-255, for MODE_FROST_SHIFT
float bodySwell(const mobj_t &thing);      // 0-1: how far a body in vacuum has swollen
struct BreachFrost { float ax,ay,bx,by,frost;int sectors[2]; };
void watchBreaches();                      // each tic (and the pools: poolFrozen, poolBoiling)
bool poolFrozen(int sector);               // a liquid floor frozen solid (Phobos)
struct PoolIce { float ice=0,glow=1; };    // how far its ice sheet has formed (0-1), the slime's glow left
PoolIce poolIce(int sector);
bool frozenLook(int sector,int &grey,int &glow);   // frozen over: grey 0-16, glow 0-8
bool frozenLookBeside(const line_t &line,int &grey,float &glow);   // a glowing wall by a pool frozen over
bool poolRebakeWanted();                  // pools dimmed: bake the lights again (once per ask)
float poolIceCode(int sector);             // for the shaders: ice (0-1) + 2 * glow left in fifteenths
struct IceCrack { int sector;float x,y,reach;uint32_t seed; };
bool crackIce(int sector,float x,float y);  // a shot into a pool's ice: false when it has none to crack
const std::vector<IceCrack> &iceCracksNow();
float poolBoiling(int sector);             // 0-1: how hard a liquid floor boils off now
void breachFrost(std::vector<BreachFrost> &out);   // the frosted openings, for wear.cpp
float visorFrost();

// impacts.cpp
void dropOldLevelImpacts();
void impactsShutdown();
void impactsCarried(float dx,float dy,float dz);
extern std::vector<FlatDecal> flatDecals;
extern std::vector<Debris> debris;
extern std::minstd_rand impactRandom;
// Doom-pixel cells of the view, each taken at most once a frame: a flat grid
// over the screen, sized by the first block that asks, since thousands of
// crest blocks a frame made a hash set's node per cell the cost of ripples.
struct BlockCells {
    std::vector<uint8_t> taken;int width=0,height=0,left=0,bottom=0;
    void clear() {taken.clear();}
    // False if the cell at (x,y), in Doom pixels from the view's middle, was taken already.
    bool take(int x,int y,float across) {
        if(taken.empty()) {
            int half=(int)std::ceil(across)+4;
            width=2*half;height=2*104;left=-half;bottom=-104;
            taken.assign((size_t)width*height,0);
        }
        x-=left;y-=bottom;
        if(x<0||y<0||x>=width||y>=height)return true;
        uint8_t &cell=taken[(size_t)y*width+x];
        if(cell)return false;
        cell=1;return true;
    }
};
void pixelBlock(std::vector<Vertex> &out,const Uniforms &camera,float x,float y,float z,float size,float tall,const std::array<float,3> &color,bool resting=false,
                const std::array<float,3> *albedo=nullptr,bool palette=true,BlockCells *taken=nullptr,float alpha=1,float stipple=1,float least=1,
                unsigned modeBits=0);
// modeBits: added to the vertices' mode (MODE_PARTICLE_DAYLIT for the weather).
// least: the smallest it's drawn, in Doom pixels. Below 1 a far speck shrinks with distance: held at a whole pixel
// and faded, far blood turned to a thin haze of big squares (the user). farLeast: what things thrown by a hit use.
constexpr float farLeast=0.3f;
// A speck thrown by a hit, far off: smaller than a Doom pixel but drawn as one, so it would look the same size at any
// distance. The alpha to draw that one pixel at: the share of it the speck really covers (1 when it fills it).
float distantShare(const Uniforms &camera,float x,float y,float z,float size);
// stipple: below 1, the share of the block's Doom pixels drawn, in an ordered pattern fixed to the block (it
// moves with it, no crawl): thin liquid the background shows through, Doom's way of translucency.
// Doom-pixel cells on screen already holding a liquid crest this frame:
// crests brighten what is under them, so overlapping ones would compound.
extern BlockCells crestCells;
bool wallSpan(const line_t &line,int side,int section,WallSpan &span);
void wallImpact(line_t *line,int side,fixed_t fx,fixed_t fy,fixed_t fz,mobj_t *shooter,fixed_t dx,fixed_t dy,fixed_t slope);
// wall_hits.cpp: a shot on a wall by what it is made of: throws its plume and chips, is heard if an enemy's near the
// player, and answers the decal key of its mark; on a floor or ceiling; whether a puff is one of its hits'.
int wallHit(int texture,Image &image,float wx,float wy,float z,float nx,float ny,const mobj_t *shooter,float dx,float dy,float slope);
void flatHit(const sector_t &sector,bool ceiling,float x,float y,float z,const std::array<float,3> &colour);
bool wallHitPuff(const mobj_t &thing);
void buildWallHits(const Uniforms &camera);
void buildTracers(const Uniforms &camera);   // tracers.cpp: travelling bullets in flight
void buildShotWakes(const Uniforms &camera);   // shot_wakes.cpp: slow motion's shimmering air behind them (into heatVertices)
void wallHitsShutdown();
// player_hits.cpp: a blow on the player, heard. body_stains.cpp: blood on bystanders. gun_blood.cpp: blood on the weapon.
void playerHit(mobj_t *target,mobj_t *inflictor,mobj_t *source,int damage);
void bodyStain(const mobj_t *thing,float x,float y,float z,float splat,int tint,float from);
bool stainMask(const mobj_t &thing,const Image &image,bool flipped,float rx,float ry,std::vector<byte> &mask);
void forgetStains(const mobj_t *thing);
void stainsShutdown();
// body_jolt.cpp: the struck part jolted on the living.
void bodyJolt(const mobj_t *thing,int bone,int damage);
int bodyJoltKey(const mobj_t &thing,int lump,const Image &image);
// fat_wobble.cpp: a fat monster's flesh rings where it's shot.
void fatHit(const mobj_t *thing,float across,float height,int damage);
int fatWobbleKey(const mobj_t &thing,int lump,const Image &image);
void fatWobbleMask(const mobj_t &thing,const Image &image,std::vector<byte> &mask);   // the wounds moved with it
void forgetWobble(const mobj_t *thing);
void wobblesShutdown();
void forgetJolt(const mobj_t *thing);
void joltsShutdown();
void gunBloodDrop(int tint,float size);
void gunBloodMelee(int tint,bool blade);
Image *gunBloodImage(int lump);
void barrelBlast(const mobj_t &thing,float x,float y,float z);
void buildImpacts(const Uniforms &camera);
bool firstSolidWall(float x,float y,float z,float dx,float dy,float dz,float length,WallHit &hit,const std::vector<int> *only=nullptr);
float valueNoise(float x,float y,float cell,uint32_t seed);

// shot_lights.cpp
void shotLightsShutdown();
// Per sector, what its light level keeps after shot-out lamps (1: all).
extern std::vector<float> shotDim;
// A sector lost share of its baked floor light to shot-out lamps.
void dimShotSector(int sector,float share);
// Whether a glowing tile is out now (sputtering out for a moment after the shot).
bool tileDark(const SurfaceLightKey &key);
unsigned brokenTileSerial();   // changes whenever a tile is shot out
bool brokenTilesSteady();      // no tile shot out still sputters: tileDark holds from frame to frame
// Whether a glowing tile has been shot out, sputtering or not.
bool tileShotOut(const SurfaceLightKey &key);
// The fixture a tile belongs to, as a shot breaks it (shot_lights.cpp): its tiles' keys, their merge
// cells (the bake sources they are part of), and its middle.
void fixtureOf(const SurfaceLight &tile,std::vector<SurfaceLightKey> &tiles,std::vector<SurfaceLightKey> &cells,float middle[3]);
void shotWall(const line_t &line,int side,int section,float along,float x,float y,float z,int reach=1);
void shotFlat(int sector,bool ceiling,float x,float y,float z,int reach=1);
// Rockets and barrels bursting break the lights around them.
void blastLights(float x,float y,float z,float radius);
// Each tic: projectiles bursting against lights break them.
void watchShotLights();
void shotThrough(const mobj_t *target,fixed_t fx,fixed_t fy,fixed_t fz,fixed_t fdx,fixed_t fdy,int damage);
// The world quads of a wall piece or floor leaf with shot-out tiles, split so
// those tiles carry MODE_DEAD_GLOW; false (nothing added) when none are out.
bool addBrokenWall(std::vector<Vertex> &out,const line_t &line,int side,int span,const Image &image,Point a,Point b,
                   float bottom,float top,float u,float anchor,float light,unsigned mode,unsigned seed);
bool addBrokenFlat(std::vector<Vertex> &out,int leaf,int sector,bool ceiling,const Image &image,float z,float light,unsigned mode);

// blood.cpp
void bloodShutdown();
void bloodCarried(float dx,float dy);
// Blood tints (enemy-appropriate blood, Game_BloodTint): the blood sprite's red, turned green
// or blue; BLOOD_NONE: the monster doesn't bleed.
enum { BLOOD_RED,BLOOD_GREEN,BLOOD_BLUE,BLOOD_TINTS,BLOOD_NONE=-1 };
int bloodTint(const mobj_t *thing);    // BLOOD_RED with the switch off
bool bleedsBlood(const mobj_t &thing); // not MF_NOBLOOD, not BLOOD_NONE
byte bloodRemap(byte index,int tint);  // a red palette index in that tint
// Opaque blood-sprite palette indices, ordered from dark to bright.
const std::vector<byte> &bloodPalette(int tint=BLOOD_RED);
// Blood shown at all: switched on, in a level, the WAD's blood sprite found.
bool bloodOn();
// A blood shade (0 dark - 1 bright) of the blood sprite, unlit.
std::array<float,3> bloodColor(float shade,int tint=BLOOD_RED);
// The game's own airborne blood (GAME_THING_BLOOD): its tint, learned when it was spilled, and its
// sprite frame recoloured to it under a key of its own.
int bloodSpriteTint(const mobj_t *blood);
void bloodSpilled(mobj_t *blood,mobj_t *bleeder);   // R_BloodSpilled
void bloodHit(mobj_t *target,mobj_t *shooter,fixed_t fx,fixed_t fy,fixed_t fz,fixed_t fdx,fixed_t fdy,fixed_t slope,int damage);
void buildBlood(const Uniforms &camera);
// Splats shown from born on: a splat, a drop's speck, or a body's smear
// (thick end forward along angle). size across, stretched along angle.
enum { BLOOD_SPLAT,BLOOD_SPECK,BLOOD_SMEAR };
void bloodFloorSplat(float x,float y,int sector,float size,float angle,float stretch,bool ceiling,float born,int kind,int tint=BLOOD_RED);
// On the wall hit at (x,y,z), stretched along it; drips: one or two run down from it.
// velocity (units a second, x y z): what hit it; a drop coming in steeper than across streaks up or down the wall, leaning its way.
void bloodWallSplat(const WallHit &hit,float x,float y,float z,float size,float stretch,float born,int kind,bool drips,int tint=BLOOD_RED,
                    const float *velocity=nullptr);
// A pool of its own (gore: under a stump that came to rest, under a lying head), size units across at
// most, spreading over that many seconds; corpses get theirs here by themselves.
void bloodPoolAt(float x,float y,float size,float seconds,int tint=BLOOD_RED);
// A stump's drop landing at (x,y) at born, its splat that big: drops landing close together gather into a pool, and grow it.
void bloodPoolFeed(float x,float y,float splat,float born,int tint=BLOOD_RED);
bool bloodPoolUnder(float x,float y);   // a pool spread under (x,y)
// A pool laid from born on (a liquid takes it instead); volume: the blood it has to give to an edge.
void bloodPoolLaid(float x,float y,float born,float size,float seconds,int tint,float volume);
// Splat decals, shared by blood and liquid puddles (puddles.cpp). The artwork:
// one pool shape, four splats, three specks, a drip run and a smear.
constexpr int splatPool=0,splatSplats=4,splatSpeckFirst=1+splatSplats,splatSpecks=3,splatDrip=splatSpeckFirst+splatSpecks,splatSmear=splatDrip+1;
int splatVariant(int kind);   // a variant of a BLOOD_ kind, picked at random
// A variant's artwork under the image key: paint gives each texel's palette index from its
// shade (0 dark - 1 bright) and how far out it is (0 the middle - 1 the edge; thrown droplets
// and drip runs 1), and with glows its emission mask.
Image &splatArt(int key,int variant,const std::function<void(float,float,byte&,byte&)> &paint,bool glows,int look=0);
// A wall run's pictures (splatArt's splatDrip, in dripLooks shapes: its head's side, where it steps aside).
// Each run takes its own (dripLook), so two runs side by side never match. Empty: no runs.
constexpr int dripLooks=4;
struct DripLooks { int key[dripLooks]={};const Image *image[dripLooks]={}; };
// A splat on the floor (or ceiling) clipped to its sector, size across, stretched along angle;
// wet, dark, wash as blood's (world.frag MODE_BLOOD), glow: how much of the art's glow shows.
void flatSplat(int key,const Image &image,int sector,float cx,float cy,float size,float angle,float stretch,bool flip,bool ceiling,
               float wet,float dark,float wash,float glow,const Uniforms &camera);
// Blood's (blood_art.cpp): dir, its way (16ths of a turn, along the wall and up), stretchStep its stretch
// (0 round to 3), orient one of the grid's eight turns (a round one's), seed its shape's (0-3).
struct WallSplat { int line,side,section;float along,dz,born,size,stretch;int variant;bool flip;int drips;float dripU[2],dripLength[2];int tint=BLOOD_RED;
                   int dir=0,stretchStep=0,orient=0,seed=0;
                   bool merged=false,layer=false; };   // merged: painted into the wall's layer, only its runs left; layer: a layer tile
// Placed where a ray hit a wall (false when (x,y,z) is off that wall section); drips: one or two runs.
bool placeWallSplat(const WallHit &hit,float x,float y,float z,float size,float stretch,float born,int variant,bool drips,WallSplat &out);
void bloodWallAdd(WallSplat &wall,int tint);   // a placed splat among the rest, given a tint and one of the grid's turns
// blood_spill.cpp: a pool being laid finds an edge close by: true, with size made broad enough to reach it and
// the runs down the riser and the pool at its foot laid to follow.
bool bloodSpill(float x,float y,int sector,float born,float seconds,float &size,float volume,int tint);
void drawWallSplat(const WallSplat &splat,int key,const Image &image,const DripLooks &drips,float dark,float wash,float glow,
                   float now,const Uniforms &camera);
int dripLook(const WallSplat &splat,int n);   // which of the run pictures run n of a splat takes
// blood_art.cpp: blood at a texel a unit, on the world's grid. A picture of a kind (splat variant), shape
// seed (0-3), size (units across), way dir (16ths of a turn: bloodDirection) and stretch step (0-3:
// bloodStretchStep), its key in images; its left and top: its middle's texel.
int bloodDirection(float angle);
int bloodStretchStep(float stretch);
const Image &bloodArt(int variant,int seed,float size,int dir,int stretch,int tint,int &key);
// Drawn with its middle at (x,y) on a floor or ceiling, or as a wall splat (its dir etc. above), turned
// orient: mirrored across (1), down (2), a quarter turn (4).
// raise: its lift over the floor in decal lifts (the merged layer over the pools, so they never fight)
void gridFloorSplat(int key,const Image &image,int sector,float x,float y,int orient,bool ceiling,float wet,float dark,float wash,
                    const Uniforms &camera,float raise=1,bool glowing=false);
void gridWallSplat(const WallSplat &splat,int key,const Image &image,const DripLooks &drips,float dark,float wash,float now,
                   const Uniforms &camera);
// blood_layer.cpp: landed splats painted into a layer per surface (tiles of how much blood lies where).
void bloodLayerFloor(int sector,bool ceiling,int tint,float x,float y,const Image &art,int orient,int amount,float born);
void bloodLayerWall(const WallSplat &splat,const Image &art,int amount);
void bloodLayerWallTexel(int line,int side,int section,int tint,int gu,int gh,int amount,float born);   // gh: up from the anchor
// void_patterns.cpp: a big burst at (x,y,z) fans fine spatter onto the nearest wall, heading (dx,dy) within spread
// (pi: all round), strength 0-1; monsters in the way leave their outline clean and take a stain. source: the bleeder.
// windows.cpp: drawing-only bodies and pieces through the glass, and where flying blood meets it.
struct GlassFace { float ax,ay,bx,by,bottom,top;int line,frame; };
bool glassBodyThrough(float x0,float y0,float z0,float x1,float y1,float z1,float least);   // broke a window
int glassPaneCrossed(float x0,float y0,float x1,float y1,float &s);   // the unbroken pane met first (-1 none)
bool glassPaneAt(int pane,GlassFace &face);                            // false once broken
// glass_blood.cpp: blood on window glass (a drop from (fromX,fromY) landing at (x,y,z)), drawn each frame.
void glassBloodLand(int pane,float x,float y,float z,float fromX,float fromY,float splat,float born,int kind,int tint);
void buildGlassBlood(const Uniforms &camera,float now);
DripLooks bloodDrips(int tint);   // blood.cpp: a tint's run pictures
void voidBurst(const mobj_t *source,float x,float y,float z,float dx,float dy,float spread,float strength,int tint);
void buildBloodLayer(const Uniforms &camera,float now);
void bloodLayerShutdown();
// blood_liquids.cpp: blood landing in a liquid at (x, y): a cloud in water, a swirl swallowed by nukage, steam
// off lava (the splat's size, the time it lands, the bleeder's tint).
void bloodLiquidLand(float x,float y,int sector,float splat,float born,int tint);
void buildBloodLiquids(const Uniforms &camera);
void bloodLiquidsShutdown();
byte wallEmissionAt(int line,int side,int section,float along,float z);   // shot_lights.cpp: a wall's glow there, 0-255
byte flatEmissionAt(int sector,bool ceiling,float x,float y);              // a flat's
bool wallShotOutAt(int line,int side,int section,float along,float z);    // there on a lamp shot out (no glow through blood)
bool flatShotOutAt(int sector,bool ceiling,float x,float y);
size_t lampsShotCount();                                                   // changes whenever a lamp is shot out
// blood.cpp, for the layer: a stain's wet and dark by its age, its rain wash (below 0: gone), rain on a floor.
void bloodLayerAge(float age,float &wet,float &dark);
float bloodLayerWash(bool rained,float age,float &dark);
bool bloodLayerRained(int sector,float x,float y);
#ifdef DOOM_DEVTOOLS
void devBlood(const Uniforms &camera);   // local dev tools (devtools/): their own blood, drawn with the rest
#endif

// spray.cpp
// What a hitscan hit throws: an exit spray past the body along the shot, a speck or two back.
void sprayHit(const mobj_t *target,float x,float y,float z,float dx,float dy,float slope,int damage);
void sprayBone(const mobj_t *target,float x,float y,float z,float dx,float dy,float slope,int damage);   // a hit on bare bone: chips, a drop
void metalSparks(float x,float y,float z,float nx,float ny,int damage);   // impacts.cpp: a bullet off armour (along nx, ny back out)
// A kill's burst: along the push (dx,dy, speed units a tic) from where it struck (hitZ above
// the feet, -1 unknown), more for overkill, all around for a gib.
void sprayKill(const mobj_t &thing,float dx,float dy,float speed,float hitZ,bool gib,int overkill);
// A body shot out of the air (Authored's kick): a cloud of blood all over it, thicker the slower time runs,
// and (sprayTrail, a tic's worth) blood off it as it flies on.
void sprayAirKill(const mobj_t &thing);
void sprayTrail(float x,float y,float z,int tint);
// A hit that didn't kill, by a missile or blast, at hitZ above the feet.
void sprayHurt(const mobj_t &thing,float dx,float dy,float hitZ,int damage);
// A ragdoll's torso coming down at (x,y) on the floor z, at speed (units a tic): the harder, the more.
void sprayLanding(float x,float y,float z,float speed,int tint=BLOOD_RED,bool slap=true);   // slap: its sound
// A ragdoll's torso slamming into the wall along (dx,dy) from (x,y,z) at speed (units a tic).
void sprayWallHit(float x,float y,float z,float dx,float dy,float speed,int tint=BLOOD_RED,float heft=1);
// contact_sounds.cpp: a body or a piece striking the floor at (x,y,z) or the wall it hit, at speed (units a tic).
// heft: 1 a man, more a big monster, under 0.7 a small piece.
void contactFloor(float x,float y,float z,float speed,float heft);
void contactWall(const WallHit &hit,float x,float y,float z,float speed,float heft);
// Gore (gore.cpp): a head blown off at (x,y,z) by a shot along (dx,dy); a stump's beat, thrown out
// along the unit way (wx,wy,wz), strength 1 the first.
void sprayHeadPop(float x,float y,float z,float dx,float dy,int tint=BLOOD_RED,bool sound=true);
void sprayCorpseHit(float x,float y,float z,float dx,float dy,int tint=BLOOD_RED);
void sprayGush(float x,float y,float z,float dx,float dy,float size,int tint=BLOOD_RED);   // gore: a part torn off, sized by it
// strength: the beat's (stumpPulse), stream: the opening stream's (stumpStream); carry: the stump's own speed
// (units a second, x y z), which its drops leave with.
void sprayCutSpill(const mobj_t &thing,float x,float y,float z);
// A body popped overhead: its blood thrown up and raining down round (x,y,z) for a second or two.
void sprayRain(const mobj_t &thing,float x,float y,float z,float reach);
void sprayScorch(float x,float y,float z,float dx,float dy);   // a burn: smoke and char flakes, no blood
void bloodSteamAt(float x,float y,float z);                    // blood_liquids.cpp: a puff of steam or smoke   // cut in half: everything spills at once
void sprayStump(float x,float y,float z,float wx,float wy,float wz,float strength,int tint=BLOOD_RED,float stream=0,const float *carry=nullptr);
// A body sliding on the floor: a smear at (x,y), thick end forward along angle.
void spraySmear(float x,float y,float angle,float size,int tint=BLOOD_RED);
// Each tic: wounds drip, sliding bodies smear, bodies thrown into walls splash them.
void watchSpray();
void buildSpray(const Uniforms &camera);
void sprayShutdown();

// body_motion.cpp
// A monster's drawn pose from the blows it took: lean and upper-body whip
// as world-space vectors (radians, toward the push), legs squashed by that
// share, lifted this many units; fliers pivot about their middle.
// flatten: the whole body shortened by that share (a landing's settle, from the death director).
// roll: turned over about its middle, 0 upright to 1 upside down (an imp on the ceiling).
// sweep: tipped over in the picture's own plane about the hip, 0 to 1, feet leading sweepWay (world x/y).
struct BodyPose { float lean[2],whip[2],squash,lift,flatten,roll;bool flier;float sweep,sweepWay[2]; };
void bodyHit(mobj_t *target,fixed_t fx,fixed_t fy,fixed_t fz,fixed_t fdx,fixed_t fdy,int damage);
// A flying body struck it (ragdoll_bumps.cpp): a flinch and, for a moment, its pain frame. Drawing only.
void bodyShove(const mobj_t &thing,float wayX,float wayY,float speed,float hitZ);
bool bodyStagger(const mobj_t &thing,int &sprite,int &frame);
void bodyDamaged(mobj_t *target,mobj_t *inflictor,mobj_t *source,int damage);
bool bodyKillingPellet(const mobj_t *thing,float x,float y,float z,float dx,float dy,int damage);   // gore: the killing volley's rest
bool bodyDyingNow(const mobj_t *thing);   // died this tic, its death not yet seen
bool bodyKnelt(const mobj_t *thing);   // down on one knee at its last tic alive (the game stands the dead up)
void bodyWalk(const mobj_t *thing,float &vx,float &vy);   // how fast it was walking, units a tic (its momentum is none of it)
// An explosion starting (R_Explosion): its spot's damage from here on is the blast, not a direct hit; gore
// bursts the corpses it catches (goreExplosion).
void bodyExplosion(mobj_t *spot,mobj_t *source,int damage);
void watchBodies();
void bodyMotionShutdown();
bool bodyPose(const mobj_t &thing,float fraction,BodyPose &pose);
void bodyQuads(std::vector<Vertex> &out,const BodyPose &pose,const Vertex &base,float x,float y,float feet,float rx,float ry,
               float left,float right,int imageTop,int rows,float u0,float u1,const std::vector<byte> *holes=nullptr,int tint=BLOOD_RED);

// death_director.cpp
// How a dying monster is drawn this frame: the sprite and frame (with its
// full-bright bit), the facing its rotations are picked by, and whether
// the frames are mirrored.
// A procedural fall's drawing this frame (death_falls.cpp): rigged (rig set:
// legs' hip drops, sideways shift, torso turn, legs' swing about the pelvis,
// sink, all in units and radians, + turning left) or a drawing set down by
// ground units, lifted, pressed flat by a share. Front view (rotation 1).
struct FallRig;
struct FallPose { const FallRig *rig=nullptr;float drops[2]={},shift=0,torso=0,legs=0,sink=0,head=0,ground=0,lift=0,flatten=0;bool active=false; };
// bend: how much of the death jolt (body_motion.cpp) the pictured body
// still takes, 1 before the collapse, 0 once it lies; settle: a landing's
// brief flattening; fall: a procedural fall, which draws instead.
// ragdoll: a ragdoll fall (ragdoll.cpp), which draws instead of all of the above.
struct Ragdoll;
struct BodyBurst;
// burst: blown apart (body_burst.cpp), drawn as its pieces instead of the thing.
struct DeathView { int sprite,frame;angle_t facing;bool mirror;float bend,settle;FallPose fall;const Ragdoll *ragdoll=nullptr;BodyBurst *burst=nullptr; };
// One hitscan hit of a killing blow: its height over the feet, how far off the middle across the shot
// (as hitU: positive on the shooter's right), and its damage. Gore reads where each landed.
// across: the shot's way across the body's x as it faced then (the body's own x is u times it, mirrored as
// drawn); earlier: a scar from a hit while it lived (PLAN-gore.md A), counted at half its damage.
struct GorePellet { float z,u;int damage;float across=0;bool earlier=false; };
// pellets: the killing tic's hitscan hits, one by one (null for none); damage: the killing tic's, all
// of it; rocketDirect: a rocket (or the like) struck it in flight.
void startDeath(const mobj_t &thing,float dx,float dy,float speed,float side,float high,bool gib,const mobj_t *attack,float hitZ=-1,float hitU=0,
                const std::vector<GorePellet> *pellets=nullptr,int damage=0,bool rocketDirect=false,bool mouthShot=false);
void forgetDeath(const mobj_t *thing);
// body_ball.cpp: a ball of flesh (Game_BodyBall) as a soft body when killed (softBalls, gore.cpp; a direct rocket
// or -goreballgib bursts it): a doll like the rest (ragdoll.cpp), its shell moving its relief.
bool softBalls();
bool fatBalls();                 // trial: the Mancubus a soft body too (-gorefatball)
bool fatFrames();                // trial: the Mancubus dies on his own death frames, landing heavily (-gorefatframes)
int ballStiffness(int type);     // a soft body's stiffness, 100 the cacodemon's; 0: not one
bool startBall(const mobj_t &thing,float dx,float dy,float speed,bool blast);
void deathsShutdown();
bool deathView(const mobj_t &thing,float fraction,DeathView &view);

// strides.cpp
// A walker's run frames by the ground it covers (DERP's monsters,
// Game_StrideLegs): watched each tic; the frame drawn, if it is running.
void watchStrides();
bool strideView(const mobj_t &thing,DeathView &view);
// The run frame its legs are on, whatever state it is in; -1 if it has no run.
int strideLegs(const mobj_t &thing,bool backwards=false);

// death_falls.cpp
// hitZ, hitU: where the killing blow struck, units above the feet and right of the middle as the
// shooter sees it (hitZ < 0: not known, as for a missile).
bool startFall(const mobj_t &thing,float speed,float high,float side,float hitZ,float hitU);
void forgetFall(const mobj_t *thing);
void fallsShutdown();
void prepareFalls();      // at level load: rigs for every killable monster's pain and first death drawing
bool fallView(const mobj_t &thing,float fraction,DeathView &view);
void fallQuads(std::vector<Vertex> &out,const Vertex &base,const FallPose &pose,float x,float y,float feet,float rx,float ry,bool flip,
               const std::vector<byte> *holes=nullptr,int tint=BLOOD_RED);
// The rig a drawing falls on, for the ragdoll's body: hips, knees and ankles ([left, right][u, h], units
// right of the origin and above the feet), where the legs part, the head (if found), whether its hand
// rig plays the guided shot death (ragdoll.cpp), and per texel whether it moves with the torso whatever
// its height (a gun).
struct RigJoints { float hip[2][2],knee[2][2],ankle[2][2],split;bool hasHead,guided;float neck,headMiddle,headWidth,depth;
                   const std::vector<byte> *carried;int width,height,left,top; };
bool rigJoints(int lump,RigJoints &out);

// gore.cpp
bool goreOn();                                  // F4 Gore, and not in a demo or netgame (a rule of the game)
// What a killing blow does to the head, from its pellets that landed on it and at the neck (their
// damage and how many): it stays on (whole, or a chunk torn out of it), is blown apart, or (a lucky
// shot) severed.
enum GoreHead { GORE_HEAD_STAYS,GORE_HEAD_BLOWN,GORE_HEAD_SEVERED,GORE_HEAD_TORN };
GoreHead goreHead(const mobj_t &thing,int headDamage,int headPellets,int neckDamage,int neckPellets);
// Cut in half at the waist, from the killing blow's pellets in the band there (their damage, how many).
bool goreHefty(const mobj_t &thing);   // too big for its legs to be knocked away: they stay standing
bool goreHalves(const mobj_t &thing,int waistDamage,int waistPellets);
bool goreArm(const mobj_t &thing,int damage,int pellets);   // an arm off, from the pellets on it
bool goreLeg(const mobj_t &thing,int damage,int pellets);   // a leg off, from the pellets on it (gore_legs.cpp)
void goreGibKill(bool on);   // the kill being decided is past the gib line: the rules above lenient
bool goreGibKilling();
void goreBlastKill(int damage);   // the kill being decided is an explosion's, this much damage (0: not)
int goreBlastLimbs(const mobj_t &thing);   // how many limbs that blast tears off (0-2)
// The whole body bursts, from the killing tic's damage (10000 or more: a telefrag), whether a rocket
// struck it directly, and whether it has a rig (ragdollRigged).
bool goreBurst(const mobj_t &thing,int damage,bool rocketDirect,bool rigged);
enum GoreSound { GORE_SOUND_TEAR,GORE_SOUND_CRACK,GORE_SOUND_BLOWUP,GORE_SOUND_THUD,GORE_SOUND_SLAP };
void goreSound(GoreSound sound,float x,float y,float z);   // audio/gore_synth.c's, at (x,y,z)
int goreUnderfoot(float x,float y);             // gore lying at (x,y): a step squelches (audio/gore_floor.h)
float stumpPulse(int tics,uint32_t seed);       // a stump's beat that many tics after the cut (0: none)
float stumpStream(int tics,uint32_t seed);      // the steady stream under the beats in the first second (0: none)
// gore_guts.cpp: what a cut shows. A monster's own gib drawing as a texture of its meat (colour per
// texel, -1 empty), borrowed in its blood's tint when it has none; bone and a dark edge.
struct GutsPatch { int w=0,h=0;std::vector<int> colour;byte bone=0,dark=0;int tint=BLOOD_RED; };
const GutsPatch &gutsOf(const mobj_t &thing);
byte gutsColour(const GutsPatch &guts,int x,int y,uint32_t seed);   // meat for a texel of a cut

// ragdoll.cpp
// A rigged monster's body thrown down as a ragdoll: its pain drawing raised into a relief
// (ragdoll_body.cpp) on a small 3D skeleton the hit knocks over. hitZ, hitU as startFall; blast: an
// explosion killed it (thrown flying).
// gore: with Gore on (gore.cpp), the killing blow's pellets, which may take the head off; onlyCut: no
// ragdoll unless they do (the game's own frames otherwise). burnt: killed by fire or plasma (it crumples
// where it stands, the legs first, and twitches once or twice).
bool startRagdoll(const mobj_t &thing,float dx,float dy,float speed,float side,float high,float hitZ,float hitU,bool blast,
                  const std::vector<GorePellet> *gore=nullptr,bool onlyCut=false,bool burnt=false);
bool ragdollView(const mobj_t &thing,DeathView &view);
// Phobos: a living monster adrift (Game_Adrift) tumbles as a doll; false when it isn't.
bool tumbleView(const mobj_t &thing,DeathView &view);
void forgetRagdoll(const mobj_t *thing);
bool ragdollLegsStand(const mobj_t &thing,int tics);   // cut in half under this many tics ago, its legs left standing
bool ragdollRigged(const mobj_t &thing);   // its pain drawing makes a body (the joint finder took it)
bool ragdollPlace(const mobj_t &thing,float &x,float &y,float &z);   // where its ragdoll lies (feet), if it has one
bool ragdollBlown(const mobj_t &thing,float sx,float sy,float hurt);   // an explosion throwing a soft ball still moving (body_ball.cpp), if it is one
bool ragdollPoint(const mobj_t &thing,int point,float &x,float &y,float &z);   // a point of its ragdoll in the world (ragdoll.h's DollPoint)
// A shot into a corpse's ragdoll: -1 missed it (or no doll), 0 struck at (hx,hy,hz), 2 struck and its torso is done.
int ragdollCorpseShot(const mobj_t &thing,float x1,float y1,float z1,float x2,float y2,float z2,float &hx,float &hy,float &hz);
void ragdollsShutdown();
void ragdollsCarried(float dx,float dy,float dz);
void prepareRagdolls();   // at level load: bodies for every killable monster's pain drawing
// The skeleton of a monster type's ragdoll at rest (its pain drawing, front): joints as ragdoll.h's
// DollPoint, x units right of the origin as drawn and z above the feet (y unused); arms: one found each
// side (screen left first). False without a body.
bool ragdollRestJoints(int type,float joints[15][3],bool arms[2]);

// skeletons.cpp, skeleton_fit.cpp (PLAN-skin-suit.md 2-4): gore's implied skeleton in each drawing of a
// monster, so a wound stays on the bone it struck whatever drawing shows it. Side 0 is the body's right
// (the screen's left seen from the front), side 1 its left.
enum SkeletonJoint { SJ_PELVIS,SJ_NECK,SJ_HEAD,SJ_SH0,SJ_SH1,SJ_ELB0,SJ_ELB1,SJ_HAND0,SJ_HAND1,SJ_HIP0,SJ_HIP1,
                     SJ_KNEE0,SJ_KNEE1,SJ_ANK0,SJ_ANK1,SKELETON_JOINTS };
enum SkeletonBone { SB_PELVIS,SB_SPINE,SB_HEAD,SB_UPPER0,SB_UPPER1,SB_FORE0,SB_FORE1,SB_THIGH0,SB_THIGH1,SB_SHIN0,SB_SHIN1,
                    SKELETON_BONES };
extern const int skeletonBoneEnds[SKELETON_BONES][2];
// One drawing's (a sprite lump as stored, unmirrored): joints in units right of its origin and above the
// feet; each bone's flesh round it (radius), whether the monster has it; the view it shows (yaw: 0 the
// front, a quarter turn its left side); which bone each opaque texel belongs to (-1 none); how well it
// fits (fit: the share of the drawing's texels within reach of a bone); hand: from the stock table.
// depth: each joint's nearness to the eye (units, larger nearer; deep: known, from a 3D rig).
struct SpriteSkeleton {
    float joint[SKELETON_JOINTS][2]={};float radius[SKELETON_BONES]={};bool has[SKELETON_BONES]={};
    float depth[SKELETON_JOINTS]={};bool deep=false;
    float yaw=0,fit=0,cost=0;bool hand=false;   // cost: the fit's own measure of the drawing's match (lower better)
    int width=0,height=0,left=0,top=0;std::vector<int8_t> owner;
    std::vector<byte> boneMap;   // per texel, what lies under the skin (bone_map.cpp): BONE_NONE (flesh) ...
};
// bone_map.cpp: the skeleton filling the body (skull, rib cage, pelvis, two-bone forearms and shins) per
// texel of a drawing, from its rig; and the palette colour a code draws in.
enum { BONE_NONE,BONE_SOLID,BONE_EDGE,BONE_HOLE };
void buildBoneMap(SpriteSkeleton &s,const Image &image);
byte boneMapColour(int code);
// stump_rim.cpp: a cut bone's long splinter stays on the stump (true) or goes with the piece; bone is
// stumpSplinterBone's (a SkeletonBone, plus SKELETON_BONES for a forearm's or shin's right shaft: never the same).
bool stumpKeepsSplinter(uint32_t goreSeed,int bone);
int stumpSplinterBone(const SpriteSkeleton &s,int c,int r);   // for a drawing texel; -1 none
const SpriteSkeleton *spriteSkeleton(const Image &image);   // null: none (no skeleton kind, or it didn't fit)
// kneel_view.cpp: a soldier down on one knee in its kneeling frames (kneel_frames_stock.h): which soldier
// and frame (0 aim, 1 fire, 2 pain), false where it isn't kneeling or has no such frames (then bodyPose
// squashes its legs); the frame's picture for a view (seen: rotation 0-7, never flipped), lit as the game's
// own frame lump is, under a key of its own.
bool kneelFrame(const mobj_t &thing,int &soldier,int &frame);
int kneelKey(int soldier,int frame,unsigned seen);
Image &kneelImage(int soldier,int frame,unsigned seen,int lump);
// A point's place on a bone: along it (0 at its first joint, 1 its second, clamped) and across it (units,
// toward the drawing's right); and back: the point at that place.
void skeletonPlace(const SpriteSkeleton &s,int bone,float u,float h,float &along,float &across);
void skeletonPoint(const SpriteSkeleton &s,int bone,float along,float across,float &u,float &h);
bool skeletonBonesJoin(int a,int b);   // the same bone, or two sharing a joint
void prepareSkeletons();   // at level load: every drawing of every monster with a skeleton kind (cached by lump)
void skeletonsShutdown();
// skeleton_fit.cpp: the reference (the rest joints, lateral: how much of the body's width this view shows,
// cos of its yaw) fitted to a drawing, starting from start (joints of a neighbouring drawing, or null: the
// reference turned to the view). Fills joints, radius, has, owner, fit.
struct SkeletonReference { float joint[SKELETON_JOINTS][2];float radius[SKELETON_BONES];bool has[SKELETON_BONES]; };
bool referenceSkeleton(int type,SkeletonReference &out);
void measureSkeletonRadii(SkeletonReference &ref,const Image &image);   // each bone's flesh in the reference drawing
void skeletonOwners(SpriteSkeleton &s,const Image &image);   // which bone each texel belongs to (and the fit)
// move false: start's joints as they are (a hand-placed skeleton), only the texels given to its bones.
void fitSkeleton(const SkeletonReference &ref,const Image &image,float lateral,const SpriteSkeleton *start,SpriteSkeleton &out,bool move=true);
void ragdollTestTic();    // ragdoll_test.cpp: -ragdolltest's scripted kills, each tic
// The body as a sprite card: drawn by a virtual camera of its own (looking at it from the eye's side,
// stepped to 45 degrees, straight on while it stands and 30 degrees down once it lies, one pixel
// per unit), and that picture stood upright at the body facing the eye like any sprite, its lowest
// solid row on the floor. Once the body lies still the card is baked: the last picture shown the same
// from every angle (rotation 0, like id's corpse frames), the doll's posed body freed.
// key: the picture's image; centred at (x, y), top at top, half wide, size square.
// The doll flies in the world from where it died, not with the thing: fraction is the frame's
// share of the tic, sink how far the thing sinks into a liquid.
// opaque texel bounds, upper limits exclusive; shadow: a loose gore piece casting its own (geometry.cpp)
// picture: which drawing its image holds (it is drawn afresh with each pose); lying: 0 upright to 1 flat (the body's, for its shadow volume).
struct RagdollCard { int key;float x,y,top,half;int size;int u0,v0,u1,v1;bool shadow=false;uint64_t picture=0;float lying=0; };
bool ragdollCard(const Ragdoll &doll,float fraction,float sink,const float *eye,const std::vector<byte> *holes,RagdollCard &card,int tint=BLOOD_RED);
// A doll come to rest whose picture holds its wounds already: drawn as it is, its holes not asked again.
bool ragdollCardKept(const Ragdoll &doll);
// A piece of this body in view, wherever the body is (thing_view.cpp).
bool ragdollPiecesInView(const Ragdoll &doll);
// A body come to rest out of sight with no sprite yet: it may have one now.
bool ragdollWantsPicture(const Ragdoll &doll);
// Gore's head burst over a ragdoll whose head was blown off (head_burst.cpp): false once it has faded.
bool ragdollBurstCard(const Ragdoll &doll,float fraction,RagdollCard &card);
// And a severed head flying off it or lying where it came to rest (flying_piece.cpp).
bool ragdollHeadCard(const Ragdoll &doll,float fraction,RagdollCard &card);
// Pieces off the body, drawn on their own: the arms shot off (0 left, 1 right as drawn), then the chunks shot
// out of the corpse.
int ragdollLooseCount(const Ragdoll &doll);
bool ragdollLooseCard(const Ragdoll &doll,int n,float fraction,RagdollCard &card);
// And its legs when it was cut in half: flying off (flying_piece.cpp, cut low) or left standing as a
// picture of their own (standing_legs.cpp, -gorelegsrig).
bool ragdollLegsCard(const Ragdoll &doll,float fraction,RagdollCard &card);
// Or its legs thrown off as a doll of their own, drawn with ragdollCard (null: none).
const Ragdoll *ragdollLower(const Ragdoll &doll);
// Or, the default, its own death frames with the top cut away (cutMask): where and how to cut them.
bool ragdollLegsCut(const Ragdoll &doll,float &cutZ,uint32_t &seed);

// head_burst.cpp: a head blown apart as a picture (PLAN-gore.md N). The head's texels in picture
// units from its middle (x right as seen, z up); where the shot struck it (picture units from its
// middle), the shot's way across the picture (-1 left to 1 right), the blow (1 at a shotgun kill) and
// the kill's own seed. Its card is drawn with its middle at (x,y,z), wherever the body has taken it.
struct HeadTexel { int x,z;byte colour,back;byte bone=0; };   // back: the colour seen from behind; bone: at a torn end (stumpPieceBone)
// A whole body bursting (body_burst.cpp) uses it too, bigger: its picture's size, how far the pieces
// fly (scale on the head's, at most), how far off the middle the shot may strike, and the texels
// already cut (burstCut: each texel's piece, and whether it is on a crack) when some pieces left as
// flying pieces of their own.
struct HeadBurstStart { float hitX,hitZ,shotAcross,blow;uint32_t seed;int tint;bool bleeds;const GutsPatch *guts;
                        int canvas=64;float reachScale=1,reachMost=24,hitMost=5;
                        const std::vector<int> *pieceOf=nullptr;const std::vector<bool> *torn=nullptr; };
// The cut: count seeds on the texels, each texel to its nearest (weighted, the borders wobbled);
// torn: on a crack, or (cutBelow) with nothing under it, the neck's cut.
std::vector<int> burstCut(const std::vector<HeadTexel> &texels,int count,uint32_t seed,std::vector<bool> &torn,bool cutBelow);
// caco_pop.cpp: the balloon, his drawing swelling before he pops (body_burst.cpp starts it).
struct Balloon;
std::shared_ptr<Balloon> startBalloon(const std::vector<HeadTexel> &texels,float x,float y,float z,uint32_t seed);
int balloonLasts();
bool balloonCard(Balloon &b,int tic,float x,float y,float z,RagdollCard &card);
void dropBalloon(Balloon &b);
bool startBalloonBurst(const mobj_t &thing);   // false: no such drawing
bool balloonHides(const mobj_t &thing);        // the rocket's explosion inside him isn't drawn
bool balloonSwelling(const mobj_t &thing,int lead);   // this many tics or more before he pops
struct HeadBurst;
std::shared_ptr<HeadBurst> startHeadBurst(const std::vector<HeadTexel> &head,const HeadBurstStart &start);
bool headBurstCard(HeadBurst &burst,int tic,float x,float y,float z,RagdollCard &card);   // its picture that many tics on
void dropHeadBurst(HeadBurst &burst);
int headBurstsLive();                 // bursting now (the budget: at most maxHeadBursts at once)

// flying_piece.cpp: a part cut off whole and thrown off the body, a head or legs (PLAN-gore.md M, B). Its texels as the head
// burst's (front as drawn, back seen from behind), where its middle starts in the world, the shot's
// way over the floor, the corpse's slide at the kill (units a tic), the blow and the kill's seed.
// cutOnTop: the cut is over the piece (legs) rather than under it (a head); pool: the pool it lies in, across.
// A chunk of a burst body: torn, its texels on a crack (meat all round, no bones); up: its throw
// upward (units a tic) when not the usual.
struct FlyingPieceStart { float x,y,z,shotX,shotY,carryX,carryY,blow;uint32_t seed;int tint;bool bleeds;const GutsPatch *guts;bool cutOnTop;float pool;
                          const std::vector<bool> *torn=nullptr;float up=-1; };
struct FlyingPiece;
std::shared_ptr<FlyingPiece> startFlyingPiece(const std::vector<HeadTexel> &head,const FlyingPieceStart &start);
void flyingPieceTic(FlyingPiece &head,int tic);   // on to that many tics after the kill
bool flyingPieceInView(const FlyingPiece &head);
bool flyingPieceCard(FlyingPiece &head,float fraction,RagdollCard &card);
void dropFlyingPiece(FlyingPiece &head);
int flyingPiecesLive();                // flying or lying, at most maxFlyingPieces (the oldest go)
bool flyingPieceUnder(float x,float y);   // one down on the floor at (x,y)
void flyingPiecesBlast(float x,float y,float z,float damage);   // an explosion throws the pieces in reach
// For tear_fibres.cpp: its middle now, a torn texel of it (pick: any number, the same one each time) where it is
// drawn now (right: the view's right over the floor), and its first tics' flight held back (tics).
bool flyingPieceMiddle(const FlyingPiece &h,float fraction,float &x,float &y,float &z);
bool flyingPieceTornAt(const FlyingPiece &h,uint32_t pick,float fraction,const float right[2],float out[3]);
void flyingPieceHold(FlyingPiece &h,int tics);
bool flyingPieceDown(const FlyingPiece &h);   // landed, stuck to a wall, sunk or gone
// Kicked, scuffed, shot (PLAN-tear-fibres.md 6): a kick landing (where, the way it faces in radians, its reach,
// the player kicking); each tic the players walking into what lies about, the kicked ones on their own clock;
// a shot passing.
void flyingPiecesKicked(float x,float y,float z,float angle,float reach,int player);
void watchFlyingPieces();
void flyingPiecesShot(float x1,float y1,float z1,float x2,float y2,float z2);
// tear_fibres.cpp: each game tic its fibres let go; each frame drawn; at the level's end gone.
void watchTears();
void buildTearFibres(const Uniforms &camera);
void tearsShutdown();
// explosions.cpp (F4 "Explosions"): a rocket's or a barrel's blast drawn in place of the game's sprite: a
// fireball and its smoke (explosion_fire.cpp), sparks, dust, a scorch, a barrel's own pieces thrown, and a
// light that outlasts the fire, so what the blast throws casts its shadow.
bool explosionsOn();
bool explosionFront(float x,float y,float z,float radius,bool glass,bool lamps);   // the physical one breaks them in turn (true: taken on)
void explosionAt(mobj_t *spot,int damage);                      // the game's blast (R_Explosion), before it hurts anything
void explosionBarrel(const mobj_t &thing,float x,float y,float z);   // a barrel seen in its blast frames (lights.cpp)
void explosionCancel(const mobj_t &thing);                      // its blast isn't drawn after all (swallowed: caco_pop.cpp)
bool explosionHides(const mobj_t &thing);
bool explosionRim(float x,float y,float z);                      // a Flashbulb blast burns near there: edges are picked out                       // the game's sprite isn't drawn, nor lit from
void explosionLights(float fraction);                           // into this frame's lights (collectFlashes)
void addExplosionCards(const Uniforms &camera,float fraction);  // the fire, the smoke and the wreckage, as sprites
void buildExplosionSpecks(const Uniforms &camera);              // sparks, burning bits and dust (after buildImpacts)
void buildExplosionShimmer(const Uniforms &camera);             // slow motion: the shock in the air (into heatVertices)
void explosionsShutdown();
// explosion_fire.cpp: the fireball's two pictures (fire, smoke), redrawn on tics.
struct FireLobe { float dx,dy,reach,radius,birth,heat,tau,rise,life,depth;int ox,oy;
                  float way[3]={0,0,0};bool core=false,wisp=false; };   // way: where it goes in the world (Flashbulb's burst: seen from the eye's side each tic)
struct BlastPicture { int size=0,fireKey=0,smokeKey=0,drawnStep=-1,hot=12;float reach=44,middle=0.62f;bool fire=false,smoke=false;
                      bool burst=false;   // Flashbulb's opening burst: a few broad lobes that tear apart from the middle, no smoke
                     std::vector<FireLobe> lobes; };
// out: the way out of the surface it went off on, a unit vector, or all 0 in the open (the burst's lobes go by it).
void startBlastPicture(BlastPicture &picture,float scale,uint32_t seed,int hot,bool onFloor,float headroom,bool burst=false,const float *out=nullptr);
bool drawBlastPicture(BlastPicture &picture,float age,int step,float acrossX=1,float acrossY=0);   // across: the card's way over the ground
void dropBlastPicture(BlastPicture &picture);
// explosion_grid.cpp, explosion_view.cpp (F4 Explosions: Physical): the blast's gas simulated on a coarse grid with the
// map in it, and the eye's picture of it. fuel: a barrel (fire leads); else a charge (dust leads).
struct BlastGrid;
struct BlastGridCard { int fireKey,smokeKey;bool fire,smoke;float x,y,ax,ay,bottom,wide,tall;int w=272,h=128;float pull=0; };   // pull: how far in front of the card its nearest gas is
BlastGrid *startBlastGrid(float x,float y,float z,bool fuel,float scale,uint32_t seed);
bool blastGridTic(BlastGrid &grid,int age);
bool blastGridCard(BlastGrid &grid,const Uniforms &camera,int step,BlastGridCard &card);
bool blastGridHeat(const BlastGrid &grid,float at[3]);   // where it burns (kept once it is out); whether it still does
void dropBlastGrid(BlastGrid *grid);
// impacts.cpp: what a blast burns onto the floor, the ceiling and the wall beside it.
void blastScorch(float x,float y,float z,float size);

// body_burst.cpp: a whole monster blown apart (PLAN-gore.md G, H): its big chunks thrown as flying
// pieces, the rest and a mist of blood a bursting picture. A kill (gore.cpp's goreBurst decided): the
// killing blow's way, its damage, where it struck (units over the feet, below 0 unknown). An explosion
// (R_Explosion) bursts the corpses lying in its reach.
bool startBodyBurst(const mobj_t &thing,float dx,float dy,int damage,float hitZ);
void goreExplosion(mobj_t *spot,mobj_t *source,int damage);
bool goreCrush(const mobj_t &thing);   // a corpse a crusher turned to gibs: burst where its body lies
// A hitscan shot from (x1,y1,z1) to where it stopped (Game_TraceShotsAlso): the corpses it went through.
void goreShotTraced(const mobj_t *shooter,const mobj_t *hit,float x1,float y1,float z1,float x2,float y2,float z2);
bool bodyBurstView(const mobj_t &thing,DeathView &view);
bool bodyBurstInView(const BodyBurst &burst);
void bodyBurstCards(BodyBurst &burst,float fraction,std::vector<RagdollCard> &cards);
void forgetBodyBurst(const mobj_t *thing);
void bodyBurstsShutdown();

// standing_legs.cpp: the legs left standing by a cut through the waist (PLAN-gore.md 11). Their
// texels (as HeadTexel: x across from their middle, z up from the floor, the colour seen), where they
// stand in the world, their joints (hip, knee, ankle, each left then right, as the texels) and the
// kill's seed.
struct StandingLegsStart { float x,y,floor,joints[6][2];uint32_t seed;int tint;bool bleeds; };
struct StandingLegs;
std::shared_ptr<StandingLegs> startStandingLegs(const std::vector<HeadTexel> &legs,const StandingLegsStart &start);
void standingLegsTic(StandingLegs &legs,int tic);   // on to that many tics after the kill
bool standingLegsCard(StandingLegs &legs,RagdollCard &card);
void dropStandingLegs(StandingLegs &legs);
// Where the body is drawn (its card's middle) and the floor it died on: its light and liquid are found
// there. True once it is down (a body in the air doesn't sink into the liquid under it).
bool ragdollWhere(const Ragdoll &doll,float fraction,float &x,float &y,float &z);
float ragdollFlash(const Ragdoll &doll,float fraction);   // ragdoll.cpp: an air kill's first tics, 1 to 0
// thing_view.cpp: what the eye might see this frame, from cullHidden's leaves (empty: everything).
void planThingView(const std::vector<char> &shown);
bool thingInView(float x,float y);
bool hasRagdoll(const mobj_t *thing);   // a ragdoll draws it: its own contacts make its blood and slaps
bool ragdollSettled(const mobj_t *thing);   // no ragdoll, or one come to rest (ragdoll_test.cpp's -ragdollloop)
int ragdollsMoving();   // the ragdolls not yet at rest (the FPS readout)

// wounds.cpp
// Holes shot through monsters' sprites. woundMask: per texel of a drawn frame,
// 0 body, 1 hole (a shot's, or a nukage rain bite's), 2 dark rim, 3 red rim, 4 bright
// lip, 5 stain (false when none show).
void woundHit(const mobj_t *target,fixed_t fx,fixed_t fy,fixed_t fz,fixed_t fdx,fixed_t fdy,fixed_t slope,int damage,const mobj_t *shooter=nullptr);
// Remember the main view so impacts can intersect the artwork after flinch,
// lifting, interpolation and liquid clipping, before damage changes its frame.
void woundView(const mobj_t *thing,const mobj_t *viewer,const Image &image,bool flipped,float x,float y,float feet,
               float top,float bottom,float rx,float ry,const BodyPose *pose=nullptr);
void forgetWounds(const mobj_t *thing);
// A fire or plasma missile striking target: a charred wound where it hit (gore).
void woundBurn(mobj_t *target,const mobj_t *missile,const mobj_t *source,int damage);
int ragdollLump(const mobj_t &thing);   // the drawing a monster's ragdoll body is raised from (-1: none)
// What the last hit (woundHit) cut, for its spray's bits: none (no wound), flesh, bone, skull, brain, guts.
enum { CUT_NONE,CUT_FLESH,CUT_BONE,CUT_SKULL,CUT_BRAIN,CUT_GUTS };
int woundLastCut();
enum { WOUND_FLESH,WOUND_BONE,WOUND_METAL };
int woundLastMaterial();   // what the last hit (woundHit) struck as painted there (Game_BodyMaterials): flesh, bone, metal
void woundsShutdown();
// Where a living monster's sprite was last drawn (woundView, the last two
// tics): its drawing, standing at (x, y) from top to bottom, its texel
// columns running along (rx, ry) from the drawing's left (right, flipped).
struct SpriteView { const Image *image; float x,y,top,bottom,rx,ry; bool flipped; };
bool woundSpriteView(const mobj_t &thing,SpriteView &out);
// A monster's wounds as spray.cpp drips from them: height above the feet,
// across the drawing it was seen in, that view's turn from the monster's
// facing (radians), the hole's size and the tic it was last shot.
struct WoundSpot { float height,across,yaw,radius;int tic; };
int woundSpots(const mobj_t *thing,WoundSpot *out,int most,bool burns=false);
bool woundsOnlyBurnt(const mobj_t *thing);   // it has wounds and every one is a burn (sealed: a corpse makes no pool)   // its open wounds (burns: its burnt ones instead)
bool woundMask(const mobj_t &thing,const Image &image,bool flipped,float rx,float ry,std::vector<byte> &mask);
void woundVertex(Vertex &vertex,byte state,int tint=BLOOD_RED);   // tint: the bleeder's (bloodTint)
// Gore's halves: a mask of the drawing with all above the cut cut away (as woundMask's states; 16 and
// up: that palette colour, the cut's face), the shot holes over it. cutZ: the cut's height standing.
bool cutMask(const mobj_t &thing,const Image &image,bool flipped,float rx,float ry,float cutZ,uint32_t seed,std::vector<byte> &mask);
float standingTop(const mobj_t &thing);   // wounds.cpp: the top of a monster's first drawing, how tall it stands
void maskedRow(std::vector<Vertex> &out,const std::vector<byte> &mask,int width,int r,bool flip,
               const std::function<Vertex(int,bool)> &corner,int tint=BLOOD_RED);
void woundedQuads(std::vector<Vertex> &out,const Vertex &base,const Image &image,const std::vector<byte> &mask,
                  float x,float y,float top,float rx,float ry,bool flip,float rowHeight=1,int tint=BLOOD_RED);

// liquids.cpp
void splashesShutdown();
boolean planeImpact(sector_t *sector,boolean ceiling,fixed_t fx,fixed_t fy,fixed_t fz);
// A body come down at (x,y) in a liquid: rings, and a plunge as hard as its fall (units a tic) and mass.
// False when not over a liquid.
// (vx,vy): its way over the floor as it went in (units a tic): the splash leans that way.
bool bodySplash(float x,float y,float fall,float mass,float radius,float vx=0,float vy=0);
bool overLiquid(float x,float y);   // a liquid floor under (x,y) that splashes
void watchSplashes();
void buildRings(Uniforms &camera);
float liquidLight(int sector,Image &flat,float x,float y,float z,const Uniforms &camera);
std::array<float,3> gooColor(int sector,uint32_t seed,float x,float y,float z,const Uniforms &camera);
void buildBubbles(const Uniforms &camera);

// puddles.cpp
// A glob a plunge throws (liquids.cpp): from (x,y) on the liquid's surface z, along (dx,dy) out
// units over its flight of air seconds, thrown up at up units a second under gravity, after delay;
// size in Doom pixels. landGlobs fills in where it comes down: when (landT, seconds into its
// flight), where, and on what.
enum { GLOB_LIQUID,GLOB_FLOOR,GLOB_WALL,GLOB_GONE };
struct GlobFlight {
    float x,y,z,dx,dy,out,up,gravity,air,delay,size;int shade;bool column;
    int land=GLOB_GONE,landSector=-1;float landT=0,landX=0,landY=0,landZ=0;WallHit wall;
};
// Where each glob of a plunge round (x,y) in sector liquid comes down, and the liquid's splats it
// leaves on the floors and walls round it, from born (seconds) on.
void landGlobs(std::vector<GlobFlight> &globs,float x,float y,int liquid,float born);
void buildPuddles(const Uniforms &camera);
void puddlesShutdown();

// windows.cpp
void glassShutdown();
void glassShot(line_t *line,fixed_t x,fixed_t y,fixed_t z,fixed_t dx,fixed_t dy);
void glassBlast(float x,float y,float z,float radius);
void glassStruck(line_t *line,fixed_t x,fixed_t y,fixed_t z,fixed_t dx,fixed_t dy);
void watchGlass();
void buildGlass(const Uniforms &camera);
bool planGlassCaptures(const Uniforms &camera);
// A pane's opening, for the weather and wear: its map line (frame to outer),
// the sectors, the pane's ends (halfway through the frame), sill and lintel,
// the unit normal pointing in from the outer side, and whether it is broken
// and looks out under the sky. The serial changes when the panes are found anew.
struct PaneView { int line,frame,outer,inner; float ax,ay,bx,by,bottom,top,inX,inY; bool broken,sky; uint32_t seed; };
unsigned windowPaneSerial();
unsigned windowPaneBreaks();   // counts up each time a pane breaks during play (the serial stays the level's)
int windowPaneCount();
bool windowPane(int index,PaneView &out);
// wall_mirrors.cpp: the level's wall mirrors (DERPINFO mirror), half a unit
// out from their wall, facing (nx, ny) into the room; bottom to top.
struct WallMirror { float ax,ay,bx,by,bottom,top,nx,ny; };
const std::vector<WallMirror> &wallMirrors();

// weather.cpp: the sky over the level. weatherNewLevel places the sun
// (sun.cpp) and raises the wind (wind.cpp); watchWeather steps the wind
// and what falls from the sky (climateNow().weather), one kind at a time
// (Weather, in climate_rules.h with the climate it goes with).
// A cell of open ground as the wind finds it (bakeWetness): where it is, the
// level's weather seed, whether a wall stands just downwind (the foot of a
// wall facing the wind) and whether a wall, roof or ledge shelters it
// upwind, leeAt units away; roofed: what shelters it hangs overhead (a
// roof's edge or a ledge), no wall rising from the ground for a drift.
// amount: how hard it falls (levelMood's rainAmount: 0.25 light .. 1 extreme);
// wind: how hard it blows (windAmount: 0 still .. 1 a gale), along hx, hy.
// trapped: how caught out of the wind the air there lies (trapped_air.cpp), 0-1.
struct WeatherGround { float x,y; uint32_t seed; bool windward,lee; float leeAt; bool roofed=false; float amount=1,wind=0.5f,hx=1,hy=0; float trapped=0; };
// How what lies on an indoor floor as a film (floor_film.cpp) moves and
// goes: over a level floor a share of the surfaces' difference a second
// (creep), only from a film at least yield deep and only where the surfaces
// differ by slope or more (sand's angle of rest: it keeps its shape); off
// an edge as fast as pour (0: it sticks where it lies); past limit deep it
// goes in eight seconds. It lasts lasts seconds in a heated room, coldLasts
// times as long as cold as outside (melting snow, warming dry ice). full:
// units deep that lie as the ground's deepest (settles: drawn as what the
// ground's bake lays down, the deeper of the two; else, dry ice, as depth).
// loose: how readily the wind moves what lies (sand, ash 1; snow per cell,
// by how cold it settled: powder.cpp).
struct FilmRules { float creep,pour,yield,slope,limit,lasts,coldLasts,full; bool settles; float loose=0; };
// One kind of weather: what weather.cpp asks of it.
struct WeatherKind {
    const char *name;
    float slant;        // units it falls for one across on the wind (the lee walls cast)
    float glassSeconds; // for the windows to wet or frost over
    float paneSeconds;  // for a broken window facing the wind to let it in
    bool settles;       // it lies on what it falls on (white) instead of soaking it (wet, streaks)
    float (*ground)(const WeatherGround &cell);  // how wet or deep it lies there, 0-1
    void (*build)(const Uniforms &camera);       // what falls round the eye and in at broken windows
    void (*ambience)(float amount);              // its sound, by how hard it falls (0: silent)
    std::array<float,3> (*tint)(); // the colour the ground, glass and far sky take (nukage), or none
    float outside=6;    // the air's temperature out in it, C (drafts.cpp)
    bool melts=false;   // what settles melts in warm air: slush, then wet (drafts.cpp's trails)
    // One of it riding a draft through the rooms (drafts.cpp), or none:
    // drops, or snowflakes where the trail lies white.
    void (*carried)(const Uniforms &camera,float x,float y,float z,const sector_t &sector,uint32_t seed,float alpha)=nullptr;
    std::array<float,3> (*lies)()=nullptr; // the albedo of what settles (c.settleColor), or snow's white
    bool trails=true;   // what it carries in lies along the drafts' trails, drawn by them (fog's doesn't)
    bool wets=true;     // it leaves the ground, walls and sills it reaches wet or damp (dry ice doesn't)
    // What blows in by an opening that opens during play (a door, a lift)
    // lies as a floor film by these rules (blown_in.cpp), or nothing does;
    // filmPanes: a broken pane's lies there too, instead of along the
    // drafts' trails (sand, ash).
    const FilmRules *film=nullptr;
    bool filmPanes=false;
    float carry=2.5f;   // how far a draft carries what settles without melting (drafts.cpp's trails)
    float lee=0;        // how much the eddies bring round to an opening out of the wind (weatherPress), 0-1
    float lift=INFINITY;// how high over the ground outside it blows in at an opening (sand stays low)
    float feed=0.1f;    // units deep a second it lays at a full opening's mouth (blown_in.cpp)
    float straight=48;  // units in a stream straight in from an opening goes before half of it has fallen out
    // What is in the air itself settles indoors toward this depth (units)
    // wherever the outside air gets in (ash), or nothing does.
    float (*indoors)(int sector,float x,float y)=nullptr;
};
extern const WeatherKind rainWeather,snowWeather,nukageWeather,sandWeather,ashWeather,fogWeather,dryIceWeather,firestormWeather;
// How fast what a kind lays creeps in from an opening, as a share of the
// air's pace (drafts.cpp's trails, blown_in.cpp's streams): what rolls
// along the ground (sand: a finite lift) drops out as soon as the wind
// slows inside, piles at the opening first and fingers in slowly; what
// rides the air comes in with it.
inline float creepShare(const WeatherKind &kind) {return kind.lift<INFINITY?0.15f:1.0f;}
extern const FilmRules rainFilm;   // rain's, which nukage rain shares
// The colour of what falls at the edge of the field round the eye (rain's
// drops, sand's grains, ash's flakes), which the far sheets past the map's
// sky take on (sky.frag, c.fallColor): a kind's build shows it the ones
// beyond half its reach (see), then settles it toward their mean, the more
// of them the faster, or on a level where none have shown yet to fallback.
struct FarColor {
    std::array<float,3> color={0.5f,0.5f,0.5f},sum={0,0,0};
    int count=0;
    unsigned level=~0u;
    void see(const std::array<float,3> &c) {for(int k=0;k<3;++k)sum[k]+=c[k];++count;}
    void settle(const std::array<float,3> &fallback);
};
extern FarColor farFall;
// Flakes (snow, ash) are placed by where they land but drawn where they are
// on the way down: in a strong wind one landing near the eye is still far
// upwind and low, and looking into the wind those piled up in a band above
// the horizon that ended hard where the sky's far sheets begin. So the area
// they are placed in lies downwind of the eye by half the way the wind
// carries one in a fall (fallSeconds), grown by as much (flyArea), only
// flakes now within reach are drawn, fading out past fade (flyFade), where
// the far sheets take over; and a flake is left out behind the eye only
// when both where it lands and where it set out are (flyBehind).
struct FlyArea { float x,y,grow; };
FlyArea flyArea(const Uniforms &camera,float windX,float windY,float fallSeconds);
float flyFade(float distance,float fade,float reach);
bool flyBehind(const Uniforms &camera,float x,float y,float windX,float windY,float fallSeconds);
// Whether a flake's whole way (from where it sets out, a long fall upwind,
// to where it lands at (x, y)) stays further than reach from the eye: none
// of it is drawn, so it is left before its sector is looked up.
bool flyOutOfReach(const Uniforms &camera,float x,float y,float windX,float windY,float fallSeconds,float reach);
// What falls on this level now, and its kind's description.
Weather falling();
const WeatherKind &weatherKind(Weather kind);
// How hard it falls this tic (0-1, 0 when dry), and how hard a given kind
// falls (0 unless it is the one falling); whether the ground and walls
// outside are wet or white (1 while it falls), how wet or frosted the
// windows (following the weather, drying slower than they soak). Per pane,
// how much a broken window has let in.
extern float fallNow,wetNow,glassWetNow;
float weatherNow(Weather kind);
// How overcast the sky is while something falls: 0 clear, 1 under a full
// downpour (as much as the mood's or DERPINFO's amount); the sun dims with it.
float overcast();
bool sunshower();
float paneSoak(int pane);
// The level's weather seed. How hard the wind presses the weather onto an
// opening whose unit normal points in along (nx, ny), at (x, y) in it: how
// squarely it faces into the wind, eased by half where the air just outside
// is trapped (trapped_air.cpp); eddy receives what the eddies bring round to
// it out of the wind (a little anywhere, more where the air outside is
// trapped), for the kinds light enough to follow them (WeatherKind lee).
// weatherPressOn: the falling kind's whole press, the deeper of the two. A
// broken pane's share of it (paneInward), and whether it lets weather in.
uint32_t weatherSeed();
float windPress(float nx,float ny,float x,float y,float *eddy=nullptr);
float weatherPressOn(float nx,float ny,float x,float y);
float paneInward(const PaneView &p);
// How much of a draft runs in through a broken pane (0 none, 1 a strong
// one; drafts.cpp's flow through its line): with nowhere for the air to go
// the weather only puffs in at the sill, with a draft through the room it
// streams in and on. The windows' particles (each kind's build) go by it.
float paneDraft(const PaneView &p);
// Of what blows low (WeatherKind lift: sand), the share that reaches climb
// units up from the ground outside (1 for the rest).
float liftShare(const WeatherKind &kind,float climb);
bool letsWeatherIn(const PaneView &p);
// Whether the weather reaches a point blown in at a broken window, or a
// thing at all (under the sky or so); rainedIn answers the game (R_RainedIn).
bool blownIn(float x,float y,float z);
bool weatherReaches(const mobj_t &thing);
int rainedIn(mobj_t *mo);
// weather_lamps.cpp: the static lamps near the eye (glowing wall panels and
// flats, torches and lamp posts), gathered once a tic while weather falls and
// settings.weatherLamps is on, and the light they give a speck in the air.
// How a speck takes it: not at all; matte (snow, sand, ash: the lamp's light
// on its albedo); a glint (rain: brighter, in the lamp's colour, further, and
// brightest lying toward the lamp from the eye).
enum class LampCatch {None,Matte,Glint};
void gatherWeatherLamps(const Uniforms &camera);
// Added into light: what the lamps give at (x, y, z) in sector, to be drawn
// at plain scale (a daylit speck's share comes divided by daylightScale).
// caught, when given, receives how lit it is by them (0 none, 1 in full),
// before colour and gain: a lit raindrop goes opaque and longer by it.
void weatherLampLight(const sector_t &sector,float x,float y,float z,LampCatch kind,const std::array<float,3> &albedo,bool daylit,float light[3],
                      float *caught=nullptr);
// A lamp as gathered this tic: where, how far it reaches and how strong
// (capped), a glowing panel's half height (extent) and wall normal (0 for
// the rest), a post lamp's share (bakePostShare), its colour, the sector it
// shines into and the space that is (out in a yard or a room, spaceOf).
struct WeatherLamp {
    float x,y,z,radius,strength,extent,nx,ny,post;
    std::array<float,3> color;
    bool outside;int space,sector;
    bool flame=false;   // a torch or a candle, no lamp on a post and no glowing panel
};
const std::vector<WeatherLamp> &weatherLamps();
// How far rain catches a lamp, in its radii: thin streaks against the dark
// show with little light, further than a wall would.
constexpr float rainLampReach=0.3f;
// The bubble of lit rain round a lamp: a share of how far the lamp lights the walls, and within
// bounds, so it stays a ball by the lamp whatever the lamp's reach. (Over the whole reach three
// torches filled a yard with coloured streaks.)
// A flame's is half that and its rain at flameRain of the brightness: a torch is a small fire,
// not a street lamp, and a row of them still drew the eye from everything else.
constexpr float flameRain=0.4f;
inline float rainLampBubble(float radius,bool flame=false) {
    return flame?std::clamp(radius*rainLampReach*0.5f,32.0f,56.0f):std::clamp(radius*rainLampReach,48.0f,112.0f);
}
// lamp_sheets.cpp: round the nearest lamps out in the falling rain, cards
// of its streaks in the lamp's colour (lampSheetVertices, drawn added over
// the scene); none while nothing falls or with settings.weatherLamps off.
void buildLampSheets(const Uniforms &camera);
// What rain or snow of an albedo looks like at a point in a sector (its
// light and the lamp pools under it); the sector at a map point, or none.
// lamps: how it catches the lamps near it (weather_lamps.cpp), daylit: drawn
// with MODE_PARTICLE_DAYLIT; unlit, when given: the colour without them, for
// the far sheets (farFall), which stay as they were; caught: how lit by the
// lamps (weatherLampLight).
std::array<float,3> weatherColor(const sector_t &sector,float x,float y,float z,const std::array<float,3> &albedo,bool inBeam=true,
                                 LampCatch lamps=LampCatch::None,bool daylit=true,std::array<float,3> *unlit=nullptr,float *caught=nullptr);
// How much of the open air is round the eye (c.climate.w): 1 out in it, a
// little by an open door indoors, lingering a while after coming in.
float openAirRound(const Uniforms &camera);
// open_air.cpp: how much of the open air's weather the wind carries in
// under a roof, per map cell (0-1, 1 under the sky), through open space
// only, as far as the wind (openAirReach, units) takes the kind; baked for
// the cells x0..x1, y0..y1 (all of them the first time a level asks).
float openAirReach(Weather kind);
void bakeOpenAir(Weather kind,float hx,float hy,int x0,int y0,int x1,int y1);
float openAirAt(size_t cell);
bool openAirBaked();
void openAirShutdown();
// beam_air.cpp: the flashlight's beam in the weather. beamAt: how strongly
// it lights (x, y, z), as it lights surfaces (0 outside it or with the
// flashlight off), color its lamp's colour; beamGlint: a flake or grain in
// it catches it this tic, at rate of them (whitening color); beamUniforms:
// c.beam, c.beamAim, c.beamLight for the shaders' glowing air, and c.climate.w.
float beamAt(float x,float y,float z,std::array<float,3> *color=nullptr);
void beamGlint(float x,float y,float z,uint32_t seed,float rate,std::array<float,3> &color);
void beamUniforms(Uniforms &camera);
const sector_t *weatherSectorAt(float x,float y);
// Where rain or snow starts falling over a sky sector (world-fixed, at
// least above units over its floor).
float skyFallTop(const sector_t &sector,float above);
// How far over a roof a falling flake or drop must be to be drawn there: it
// is a block a screen pixel or more tall, and just over a roof its lower half
// showed through the ceiling under it.
constexpr float roofClear=8;
// The ground's wetness under the sky, and where it blows in under a roof's
// edge, baked into the floor light's alpha (1 + wetness * 254, 0: none).
void bakeWetness();
// trapped_air.cpp: how caught out of the wind's heading (hx, hy) the air at
// each map cell lies (bakeTrappedAir, cached per level and heading), 0 open
// to the wind - 1 eddying in a hole, pit or a tall wall's wind shadow.
void bakeTrappedAir(float hx,float hy);
float trappedAir(size_t cell);
bool trappedAirBaked();   // for this level (it bakes only while something falls)
// rooms.cpp: the room map (rooms.c) per map cell, three separate answers.
// Roof: 1 under a roof, 0 under the sky. Enclosure: 0 open air - 1 closed
// in, for heat and sound (a room 1, a porch growing toward 1 deeper in, a
// sky cell its opening's enclosure, a hole in a ceiling high). Wind: how
// open to the wind the cell lies, 0 - 1, NAN while there is no wind data
// (nothing falls: the trapped air isn't baked); indoors the drafts' air,
// fading from the openings it moves through (a dead end calm). Void
// cells: NAN for all three.
float roomRoof(size_t cell);
float roomEnclosure(size_t cell);
float roomWind(size_t cell);
// Each tic: the drafts' wind where the listener stands indoors, to the
// ambience (I_AmbienceDraft); none outside, where the open air's wind is heard.
void hearIndoorWind();
// Whether a sector lies under the open sky, as the room map has it (built on
// first use): what the weather asks instead of the sector's ceiling, so the
// room map stays the one account of outside.
bool underSky(int sector);
void weatherNewLevel();
void weatherSunAgain();
void watchWeather();
// What falls round the eye (the falling kind's build).
void buildWeather(const Uniforms &camera);
void weatherShutdown();
// rain.cpp: rain's streaks, splashes and drops through broken windows in a
// colour of the falling kind's (albedo; glow: how bright they stay in the
// dark, 0 lit only by their sector), and how wet the ground lies under it.
void buildRainDrops(const Uniforms &camera,const std::array<float,3> &albedo,float glow);
float rainGround(const WeatherGround &cell);
// ash.cpp: the eruption's pall over the far open ground (c.haze) while ash falls.
void ashHazeUniforms(Uniforms &camera);
void snowHazeUniforms(Uniforms &camera);   // snow.cpp: a blizzard's whiteout over the far open ground
float whiteoutNow();   // whiteout.cpp: the whiteout close by for the weather and wind, 0-1 (c.climate.z)
void dryIceHazeUniforms(Uniforms &camera);   // fog.cpp: dry ice's haze in the bank's colour, for its faint whiteout
void sandHazeUniforms(Uniforms &camera);   // sand.cpp: the storm's dust over the open ground
void firestormHazeUniforms(Uniforms &camera);   // firestorm.cpp: the smoke, lit orange, over the open ground
// nukage_rain.cpp: the colour the nukage rain takes from the game's nukage
// art (Game_RainLiquidFlat), and the bites it takes out of monsters
// (craters.cpp) as it falls on them.
std::array<float,3> nukageColor();
void watchCraters(const Uniforms &camera);
// craters.cpp: nukage rain's bites in a drawing, as holes in woundMask's mask
// (sized: it already is the drawing's size); forgetting a thing's
// (forgetWounds) or all of them.
bool craterMask(const mobj_t &thing,const Image &image,std::vector<byte> &mask,bool sized);
bool cratered(const mobj_t &thing);   // the rain has bitten it
// A corpse the rain has eaten away entirely: not drawn at all.
bool dissolvedAway(const mobj_t &thing);
// How soaked in nukage rain a monster is, 0-255 (drawn through MODE_FROST_SHIFT).
unsigned bodySoak(const mobj_t &thing);
void forgetCraters(const mobj_t *thing);
void cratersShutdown();
// wind.cpp: the wind carrying the weather this tic (units a second), and
// where it blows (unit vector); how much of its press on the openings it
// has (0 still air, 1 a breeze and up), and a kind's slant in it.
extern float windNow[2];
void windHeading(float &x,float &y);
float windStrength();
float windAmount();  // 0 still air, 0.5 a breeze, 1 a gale, as windamount goes
float windCarry();   // how far what it carries gets, against a breeze (0.25-2)
float windSlant(float slant);
void windNewLevel();
void windTick(float t);
float windSpeedNow();   // units a second
float windGust(float x,float y,float t);   // the gusts' shove along the wind at (x, y), t seconds in (units)
float windGustRate(float x,float y,float t);   // how much faster than the steady wind the gusts blow there now (-1..1 of it)

// air_push.cpp: the air's push on what flies, added by every weather
// particle builder to its own path. airShift: the gusts (out in the open,
// as much as the sector is open: gustShare) taken as the difference from
// where they leave it at until (when it lands, or now for what never
// does), plus the shoves of what happens (disturbances.cpp), into out
// (units). airStir: the stir of a draft's jet on what rides it, along
// units in of length, width wide, heading (dx, dy), turn radians where it
// turns there, side -1..1 across it. airKeep: the most of a push (all,
// half, none) that keeps it where ok(x, y) says it may fly, not in a wall.
float gustShare(int sector);
// Whether what falls in home's open sky may be at (x, y, z): over open sky,
// or over a roof clear of it, never inside a room (the falling kinds' rule),
// and with room to spare: so are the points six units to each side (the
// map's cells straddle walls, and a flake right at a wall drew inside).
bool fliesOutside(const sector_t &home,float x,float y,float z);
void airShift(int sector,float x,float y,float z,float t,float until,float out[3]);
void airStir(float along,float length,float width,float dx,float dy,float turn,float side,float t,uint32_t seed,float out[3]);
// What blows in at broken window p (the kinds' own builds): any blast's
// shove at t, never out of the window's frame, the room or the outside.
void paneShove(const PaneView &p,float &x,float &y,float &z,float t);
template<class Ok> void airKeep(float x,float y,float push[3],Ok ok) {
    for(float k:{1.0f,0.5f})if(ok(x+push[0]*k,y+push[1]*k)) {for(int i=0;i<3;++i)push[i]*=k;return;}
    push[0]=push[1]=push[2]=0;
}

// disturbances.cpp: what happens in the weather: short-lived records, each
// shoving what flies near it (disturbanceShove, adding to out), blowing a
// hole in the floor film or the ground (as hard as ground says: 0 none) and
// in the fog bank (its clearings into c.clearing, the strongest eight), and
// throwing up a puff of what lay there that settles again
// (buildDisturbances). disturb adds one now: radius units, strength 0-1.5,
// heat 0-1, big or small; watchDisturbances ages them each tic. Hot things
// are standing spots instead (disturbanceHeat, every eight tics).
struct HotSpot { float x,y,z,radius,heat; };
void disturb(float x,float y,float z,float radius,float strength,float heat,bool big,float ground=1);
void disturbanceHeat(const std::vector<HotSpot> &spots,float dt);
void watchDisturbances();
void disturbanceShove(float x,float y,float z,float t,float out[3]);
unsigned disturbanceSerial();   // changes whenever what disturbanceShove reads may have, but for t
void disturbanceUniforms(Uniforms &camera);
void buildDisturbances(const Uniforms &camera);
int disturbanceCount();   // the records alive now (the FPS readout)
void disturbancesShutdown();
// disturbance_sources.cpp: where they come from, found each tic (blasts,
// projectiles' wakes, bodies, steps, doors and lifts, hot things), and
// from the game's hooks: a bullet on the floor, a gun's blast.
void watchDisturbanceSources();
void disturbanceShot(float x,float y,float z);
void disturbanceMuzzle(const mobj_t &source);
void disturbanceSourcesShutdown();
// ground_dents.cpp: dents in the ground's bake out under the sky (dentGround,
// once, as one happens), filled back in by the weather (watchGroundDents);
// dentedAlpha: a cell's floor light alpha with its dent over the bake;
// dentsPaint: the dents back over a bake just written.
void dentGround(float x,float y,float radius,float strength,uint32_t seed);
void watchGroundDents();
uint8_t dentedAlpha(size_t cell,uint8_t baked);
void dentsPaint();
void groundDentsShutdown();

// climate.cpp: the player's climate (climate_rules.h) on this level. The
// hour the mood was worked out for (TIME_OWN: as derived); the mood and the
// sun under the hour and the heat (levelMood, placeSun); the temperature
// outside (C), the heat of the day (0-1 from 30 to 50 C) and the cold
// frosting clear windows (0-1 from 0 to -20 C); the map's own weather and
// wind giving way to the player's picks, the player's wind heading (map
// degrees it blows to; false: the map's own); the shaders' share.
int climateHour();
void climateMood(game_mood &mood);
void climateSun(float &elevation,float &discElevation,std::array<float,3> &tint,float &shade,float &gain,float &glow);
float climateTemperature(Weather kind);
float climateHeat();
float climateCold();
Weather climateWeather(Weather own);
float climateWindSpeed(float own);
bool climateWindHeading(float &degrees);
void climateUniforms(Uniforms &camera);

// wear.cpp
// Wear drawn by world/wear.glsl: rain streaks under windows, wet patches
// where rain came in, leaking ceilings. Records follow the sector entries in the sector
// buffer from wearBase() on (0: none this frame).
float wearBase();
void appendWear(std::vector<std::array<float,4>> &info);
// Cracks in a few ceilings beside the sky, dripping while it rains (none
// otherwise): the room and the crack's center.
struct CeilingLeak { int sector; float x,y; uint32_t seed; };
const std::vector<CeilingLeak> &ceilingLeaks();
// Whether rain blown in through a broken window has soaked the floor at (x, y) in a sector.
bool rainSoaked(int sector,float x,float y);
void watchWear();
void wearShutdown();
// Water thrown out of a pool (puddles.cpp): the floor or wall round (x,y,z) in sector lies wet, radius units out.
void wetSplash(int sector,float x,float y,float z,float radius);
// A record for wear.glsl from another file: the sector it lies in, its two entries.
struct WearMark { int sector; std::array<float,4> a,b; };

// drafts.cpp: the wind through the buildings (rooms linked by their
// openings, the pressure, the flow and the rooms' temperatures, solved
// every few tics while something falls), the trails of snow or wet it lays
// in from the openings where the outside air comes in, as far as the cold
// reaches, and rime round cold openings (draftMarks, for appendWear); the
// flakes and drops riding them (buildDrafts).
void watchDrafts();
// The air's temperature at a map point (C; NAN while nothing falls), and the sector there.
float airTemperature(float x,float y,int *room=nullptr);
// The openings as last solved (a, b: room-map spaces, b -1 the outside; the
// opening's ends, its normal into a and the flow into a; the wind's pressure
// on it from outside, its sill and lintel, the sector on a's side, whether a
// pane broke there), and a space's temperature (NAN before a solve), for the
// dev tools' room map and dry ice's intake.
struct DraftLinkView { int a,b; float ax,ay,bx,by,nx,ny,flow,speed,outside,bottom,top; int sector; bool pane; };   // speed: the air through it, units a second
int draftLinkCount();
bool draftLink(int index,DraftLinkView &out);
float draftTemperature(int space);
// How much of the fog outside lies in a sector's air (fog.cpp): all of it
// outside, in a room as much as the drafts make it cold there. NAN before a solve.
float draftFogShare(int sector);
// How much of a sector's air is the outside's (0-1, 1 outside), for what the
// outside air carries in that isn't fog (ash); NAN before a solve.
float draftOutsideShare(int sector);
// How fast the outside air runs in through a line (units a second, 0 out
// or none; NAN before a solve).
float draftInflow(int line);
// The trails the outside air flows or flowed in along, for dry_ice.cpp:
// from the opening's sill and its ends; how far in what it carries has
// crept, how long it has soaked in (0-1), how much of it lingers once it
// stops (1 while it flows), how fast the air comes in and how fast the front
// creeps (units a second); then step by step: a to b, half width, how much
// it carries, the jet's height, the sector it ends in and starts in.
struct DraftStep { float ax,ay,bx,by,width,amount,z; int sector,from; float jet=NAN; int state=0; };   // jet C, state 2 snow 1 slush 0 wet (draft trails only)
struct DraftPath { int line; float sill,ax,ay,bx,by,reach,soak,linger,speed,creep; std::vector<DraftStep> steps; bool flowing=false; };
void draftPaths(std::vector<DraftPath> &out);
void draftMarks(std::vector<WearMark> &out);
// disturbances.cpp: the craters recent ones left in what lies indoors as
// wear records (kind 10, for the snow wear.glsl draws: a window's patch, a
// draft's trail).
void disturbanceMarks(std::vector<WearMark> &out);
// blown_in.cpp: the patches snow and rain lay in front of doorways opened
// during play, as wear records (kind 3, as a broken window's).
void blownInMarks(std::vector<WearMark> &out);
void buildDrafts(const Uniforms &camera);
// One flake or drop the air carries in, as the kind draws it (drop: where it lies wet).
void drawCarried(const Uniforms &camera,const WeatherKind &weather,float x,float y,float z,const sector_t &sector,uint32_t seed,float alpha,bool drop);
void draftsShutdown();
// powder.cpp: the drafts' trails lay powder in the film where they lie as
// snow, loose by how cold the jet was, and the gusts drift loose film
// (powder, sand, ash) along the trails and the doorways' streams
// (powderDrift, also from blown_in.cpp). watchPowder each tic between the
// film's fallout cleared and its step.
void watchPowder(const WeatherKind &kind,float dt);
void powderDrift(const std::vector<DraftStep> &steps,float reach,float speed,float ox,float oy,float dt);

// fog.cpp: the fog bank (WEATHER_FOG), pooling out under the sky and
// carried into the rooms by the drafts; dry ice's (dry_ice.cpp) too, on its way to the shaders. watchFog eases each
// sector's share every tic; fogBankCount is how many entries it puts in the
// sector buffer right after the sector entries (numsectors while it lies, 0
// otherwise), fogBankEntries appends them, fogBankUniforms fills c.bank and
// c.bankColor, fogSheets adds its drifting mist sheets for buildMist.
void watchFog();
int fogBankCount();
void fogBankEntries(std::vector<std::array<float,4>> &info);
void fogBankUniforms(Uniforms &camera);
void fogSheets(const Uniforms &camera,std::vector<MistTriangle> &out);
void fogShutdown();
std::array<float,3> bankAlbedo();   // the bank's colour (c.settleColor), fog's or dry ice's
// A stretch of fog lying along a draft's stream (dry_ice.cpp), for the
// shaders: the sector it lies in, from (a.xy) to (a.zw), its half width and
// depth at each end (b: width a, width b, depth a, depth b).
struct BankRibbon { int sector; std::array<float,4> a,b; };
// dry_ice.cpp: dry ice's layer outside, pooling and pouring by its weight
// sector to sector, and its streams creeping in along the drafts.
void watchDryIce();
bool dryIceReady();
float dryIceTop(int sector);
float dryIceShare(int sector);
const std::vector<BankRibbon> &dryIceRibbons();
// The top of the dry ice lying in the outside sector nearest (x, y) (within 1536 units) whose floor is not above z, NAN if none.
float dryIceOpenTop(float x,float y,float z);
void dryIceShutdown();
// water_ice.cpp: water freezing in the cold, per map cell (0 open - 1 a white sheet), smoothed
// along the water; the shaders read it from the ceiling light's alpha.
void watchWaterIce();                        // each tic: built anew on a level, a temperature, the drafts' first solve
void ensureWaterIce();                       // built for this level now (main thread; the reads below don't build)
void writeWaterIce();                        // into flatLightCells as the bakes are made (uploadFlatLight)
float waterIceAt(float x,float y);           // 0-1 between the cells, 0 off the water
float waterIceCell(size_t cell);
float waterIceMax(int sector);               // the most of it in a sector, for drawing its ice at all
bool waterIceHolds(float x,float y);         // thin ice or the sheet there: no splash, no ring
bool airFreezing(float x,float y);           // the air there below freezing, as the ice reads it (no drips)
void waterIceShutdown();
// dry_ice_basins.cpp: the low spots where a stream's front waits before it
// goes on. findDryIceBasins once a level (area: each sector's floor area);
// dryIceCourse walks a stream along its draft: how far its front has got,
// how much thinner it runs at each step and where it piles up behind a step
// (how far along, its top over the lower floor at the step);
// dryIceBasinRim: how deep a sector lies under its basin's rim (0: in none);
// dryIceIsland: a sector the fog parts round (higher than all round it),
// never lying on top.
struct DryIceCourse { float front; std::vector<float> thin; std::vector<std::array<float,2>> piles; };
void findDryIceBasins(const std::vector<float> &area);
void dryIceCourse(const DraftPath &path,float stream,DryIceCourse &out);
float dryIceBasinRim(int sector);
bool dryIceIsland(int sector);
float dryIcePileLength();
int dryIceBasinCount();
void dryIceBasinsShutdown();
// floor_film.cpp: what the weather lays on indoor floors as it comes in, as
// a film per floor cell (about 20 units) that gathers, creeps over the level
// and pours off edges by the lying kind's FilmRules, drawn from the floor
// light's alpha (filmLevel once a level or kind, with how deep it may lie
// per sector, or the kind's limit; each tic filmClearFallout, then
// dryIceFilmFallout per dry ice stream or filmFeed / filmGather for what
// blows in, filmStep (cold: how cold a sector's air is, 0-1), filmUpload;
// filmPaint puts it back after the ground's wetness was baked over it;
// filmAt: its depth at a point). groundBakeAt (weather.cpp): the ground
// bake's alpha at a map cell, which what settles lies over.
void filmLevel(const std::vector<char> &outside,const FilmRules &rules,float (*limitOf)(int sector)=nullptr);
bool filmReady(const FilmRules &rules);   // built this level, for these rules
void filmClearFallout();
void dryIceFilmFallout(const DraftPath &path,const DryIceCourse &course,float stream);
void filmFeed(float x,float y,float depth,float loose=-1);   // loose: how loose what it lays is (-1: the kind's)
// The wind moving loose film along (dx, dy) at (x, y): a share rate a second
// of what lies there, by how loose it is, into the next cell downwind.
void filmDrift(float x,float y,float dx,float dy,float rate,float dt);
void filmGather(float (*target)(int sector,float x,float y),float dt,float seconds,bool now);
void filmStep(float dt,const std::vector<char> &sealed,float (*cold)(int sector));
void filmUpload();
void filmPaint();
// A blast (disturbances.cpp) blows the film round (x, y) away within radius
// (in the room of sector), a share of it onto a ragged rim; what it took (units deep at the middle).
float filmDisturb(float x,float y,float radius,float strength,int sector,uint32_t seed);
float filmCell();   // a film cell's width, units
// Heat (a torch) melts or warms away share of the film within radius of (x, y), most in the middle.
void filmMelt(float x,float y,float radius,float share);
float filmAt(float x,float y);
float filmDeepest(int sector);
int filmAwake();   // the cells stepped next tic (the FPS readout)
int filmLying();   // the cells lying or still showing
void filmShutdown();
uint8_t groundBakeAt(size_t cell);
// blown_in.cpp: what the weather blows in by the openings that open during
// play (doors, lifts; for sand and ash broken panes too) as the floor film,
// creeping in from the opening; ash also settling wherever the outside air
// gets in (draftOutsideShare). watchBlownIn each tic after the drafts.
void watchBlownIn();
// What flies in on those streams near the eye, straight in at every doorway
// the wind presses on (those open since the level started too), as fast as
// it presses.
void buildBlownIn(const Uniforms &camera);
void blownInShutdown();
// dry_ice.cpp: dryIcePour pours the outside's layer across every open line
// between two outside sectors, from the higher surface to the lower.
void dryIcePour(float dt,const std::vector<float> &area,const std::vector<char> &sealed,const std::vector<char> &outside,std::vector<float> &depth);
// dry_ice_intake.cpp: where the wind presses it in. dryIceIntake each tic:
// the streams from the openings it blows on (as DraftPaths: straight in, or
// along the drafts' trail) and the updrafts up the walls under them (ribbons on
// the outside sectors). dryIceCold: how cold a sector's air is for it, 0 a
// heated room to 1 as cold as outside (outside always 1).
void dryIceIntake(std::vector<DraftPath> &streams,std::vector<BankRibbon> &updrafts);
float dryIceCold(int sector);
void dryIceIntakeShutdown();

// space.cpp
void spaceShutdown();
// sky_layers.cpp: the sky's layers by distance, baked as the sky texture
// changes; skyLayersUniforms each frame (camera.skyLayers).
void skyLayersUniforms(Uniforms &camera);
void skyLayersShutdown();
// The sky's towers as sky/depth.glsl builds them, in near-ring radii: at
// skyShrink of the painted height, on a floor skyFloorDrop under the
// reference eye, their feet reaching out skyFootReach of their height, the
// far ring skyFarRing out.
constexpr float skyFloorDrop=0.12f,skyFootReach=0.6f,skyFarRing=2.6f;
// The layers texture's rows under the painting: eight of settings, then the
// rock's limits (sky_rock_limit.cpp). sky/clouds.glsl's layerRows is the same.
constexpr int skyRockLevels=12,skyLayerRows=8+skyRockLevels;
extern float skyShrink;   // 0.9, less where a painting's cliffs would loom (sky_profiles.cpp)
// Where the sky's painting stands: its row fraction v shows at rise
// (skyRowHorizon-v)/skyRowScale. The top edge stays put, so the scale follows the
// horizon; a tall painting with its ground along the bottom (2025 Heretic)
// takes a lower horizon to get that ground above it (sky_profiles.cpp).
extern float skyRowHorizon,skyRowScale;
// How far down the painting is sky (0 all of it), where ground fills its foot.
extern int skyPaintRows;
// What sky_light.cpp lights: the layers texture's bytes as uploaded, the
// normals read from the painted shading (RG near, BA far), whether there is
// a far ring, the rings' tallest tops (each in its own radii); serial
// changes with every bake.
struct SkyPaint { int w=0,h=0; std::vector<byte> texels,normals; bool far=false; float maxTop[2]={}; unsigned serial=0; };
const SkyPaint &skyPaint();
// sky_rock_limit.cpp: how high the rock can reach, into paint's limit rows.
// city: its boxes, not towers; far: there is a far ring (paint.far is what
// sky_light.cpp lights, which leaves the city's out).
void skyRockLimits(SkyPaint &paint,bool city,bool far);
// sky_sun_block.cpp: how much of the sun's disc the mountains leave the eye,
// 0 behind them to 1 clear, for what the sun casts through the air (god rays,
// sunbeams, the dust in them). Kept by skySunUpdate each frame.
extern float skySunClear;
void skySunUpdate(const Uniforms &camera,const SkyPaint &paint,bool towers);
// sky_light.cpp: the towers' light baked per texel for the sun as it stands
// (skyLayerLight in scene3d.h); skyLightUpdate re-bakes when either changed.
void skyLightUpdate();
void skyLightShutdown();
// sky_city.cpp: whether the level's sky is a painted city (Doom 2's
// burning skyline), whose own weather is a firestorm (weather.cpp's falling).
bool skyIsCity();
constexpr float marsShine=0.75f;  // Phobos: Mars-shine, of the sky light it stands in for

// exposure.cpp
// Phobos: how plainly the stars show over the black sky, 0 to 1.
float starVisibility();
// How many times the level's light daylight is drawn at (the sky's light in the bounce, the
// sun, the sky itself): 2 with F4 Eye adaptation in whole-level lighting, which exposes for
// it; 1 otherwise (scene3d/exposure.cpp).
float daylightScale();
void exposureShutdown();
void updateExposure(Uniforms &camera);
void updateFlashlightTint(Uniforms &camera);
bool flashlightBounce(float position[3],float normal[2],std::array<float,3> &color,float &strength);

// frame_stats.cpp: the numbers of F4's "Show FPS" readout (stats_overlay.cpp).
// While the full readout shows (statsTiming), timed adds the CPU time of a
// system's work to its slot, tic work and frame work alike; otherwise it only
// runs it. statsCount: how many things a system's work drew or holds now (-1
// none to show). beforeTic's whole work, timed from start (0: not timed), is
// DERP's share of the game's tics (statsTicWork). The view: the last quarter
// second's averages per frame (ms), the worst frame, the frame times oldest
// first for the graph, the CPU load (%, of all cores; the system's -1 where it
// isn't known); serial counts the views.
enum class Stat { World, Sprites, Lights, Mist, Bakes, Weather, Drafts, Film, Fog, Disturbances, Wear, Glass, Blood, Bodies, Liquids, ShotLights, Gore, Count };
constexpr size_t statsHistory=180;
struct StatsView {
    unsigned serial=0;
    float fps=0,frameMs=0,worstMs=0,gameMs=0,effectsMs=0,sceneMs=0,submitMs=0,waitMs=0;
    float load=-1,systemLoad=-1;int cores=1;
    std::array<float,(size_t)Stat::Count> systemMs{},systemWorst{};
    std::array<int,(size_t)Stat::Count> items{};
    std::array<float,statsHistory> recent{};
    // F4's GPU times (settings.fps 4, the frame timed piece by piece: gpu3d/gpu_pieces.cpp):
    // each piece's GPU time per frame and its worst frame (I_RENDER3D_PIECE_*), and their sum;
    // gpuPieces is false while the frames are not timed so.
    bool gpuPieces=false;
    std::array<float,I_RENDER3D_PIECES> pieceMs{},pieceWorst{};
    float piecesMs=0,piecesWorst=0;
};
extern bool statsTiming;
extern bool statsTracing;
void statsAdd(Stat stat,uint64_t ns);
void statsCount(Stat stat,int items);
void statsTicWork(uint64_t start);
const StatsView &statsView();
template<class Work> void timed(Stat stat,Work &&work) {
    if(!statsTiming) {work();return;}
    uint64_t start=SDL_GetTicksNS();
    work();
    statsAdd(stat,SDL_GetTicksNS()-start);
}

// Small helpers and templates shared across files.
// Adds the time since start to total while profiling.
inline uint64_t profileStart() {return profile.enabled?SDL_GetTicksNS():0;}
inline void profileAdd(double &total,uint64_t start) {if(profile.enabled)total+=(SDL_GetTicksNS()-start)/1e6;}
// loading.cpp: the stages of the loading pass (runLoading), in the order they
// run. Each marks its start; planLoading says which this pass will run (what
// the bar counts); work on the loading thread calls loadingTick to keep the
// screen drawn, with how much of its stage is done when it knows.
enum class LoadStage { Map, Textures, Sun, Lights, Grid, Bounce, Ambient, Flow, Count };
void planLoading(LoadStage stage,bool planned);
void loadingStage(LoadStage stage);
void loadingRestored(LoadStage stage);   // the stage came from the bake cache: its time teaches the bar nothing
void loadingTick(float part=-1);
bool loadingOnThisThread();
// The first frame after a level's load is still under the loading screen (the
// same present draws it), so it captures every glass probe and builds the map
// whole. True while that frame is prepared; prepareFrame ends it with
// frameAfterLoadDone.
bool firstFrameAfterLoad();
void frameAfterLoadDone();
// Whether the next world frame builds a level or bakes (scene.cpp).
bool bakesPending();
struct ProfileTimer { double &total; uint64_t start=profileStart(); ~ProfileTimer() {profileAdd(total,start);} };
inline uint8_t *flatCell(FlatLayer layer,size_t cell) {return &flatLightCells[4*((size_t)layer*mapWidth*mapHeight+cell)];}
inline float floorZ(const sector_t *sector) {return sectorHeight(sector,false);}
// Which way a sprite's card runs across: the view's right, level. With the view rolled
// (Phobos grav boots, standing on a wall) square to where it looks, level, instead.
inline void cardAcross(const Uniforms &camera,float &x,float &y) {
    x=camera.right[0];y=camera.right[1];
    if(std::abs(camera.right[2])<0.01f)return;
    float fx=camera.forward[1],fy=-camera.forward[0],l=std::hypot(fx,fy);
    if(l<0.05f){fx=camera.up[1];fy=-camera.up[0];l=std::hypot(fx,fy);}
    if(l>1e-4f){x=fx/l;y=fy/l;}
}
inline float ceilZ(const sector_t *sector) {return sectorHeight(sector,true);}
// Calls texel(cell index, x, y, z) for each texel of a wall strip, at a
// point just in front of the side, in its own sector.
template<class Texel> void forStripTexels(const BakeStrip &strip,Texel texel) {
    for(int column=0;column<strip.width;++column)for(int row=0;row<strip.height;++row) {
        float along=std::clamp((column+0.5f)/strip.scale,0.5f,strip.length-0.5f);
        float z=std::min(strip.low+(row+0.5f)/strip.scale,strip.high);
        texel((size_t)(strip.y+row)*atlasStride+strip.x+column,
              strip.ax+strip.dx*along+strip.dy*0.5f,strip.ay+strip.dy*along-strip.dx*0.5f,z);
    }
}
inline float luminance(const float color[3]) {return 0.299f*color[0]+0.587f*color[1]+0.114f*color[2];}
// The vertex mode bits that carry sector+1 for map-space lighting (vertex_modes.h).
inline unsigned sectorMode(const sector_t *sector) {return unsigned(sector-sectors+1)<<MODE_SECTOR_SHIFT;}
// The game's clock for drawing, in seconds into the level, wrapped round clockWrapTics (about
// 18 hours): a float keeps it finer than a tic in any session, where it stuttered after weeks of
// play; patterns may jump once a wrap. Ages are counted in tics first (leveltime - start), never
// taken from it.
constexpr int clockWrapTics=TICRATE*65536;
inline float levelSeconds(float fraction=0) {return (leveltime%clockWrapTics+fraction)/(float)TICRATE;}
inline float gameSeconds() {return levelSeconds(renderFraction);}
inline float randomRange(float low,float high) {return std::uniform_real_distribution<float>(low,high)(impactRandom);}
// Chris Wellons' lowbias32 integer hash (hash-prospector, public domain).
inline uint32_t hash32(uint32_t x) {x^=x>>16;x*=0x7feb352du;x^=x>>15;x*=0x846ca68bu;x^=x>>16;return x;}
inline float hashUnit(int x,int y,uint32_t seed) {return (hash32((uint32_t)x*73856093u^(uint32_t)y*19349663u^seed)&0xffffff)/16777216.0f;}
// Decal depth offset: grows with distance so depth precision never lets the
// surface below show through.
inline float decalLift(float x,float y,const Uniforms &camera) {return 0.15f+std::hypot(x-camera.eye[0],y-camera.eye[1])*0.0004f;}
// What flow was baked from: decoration shadows, and 2 with window light,
// which brings the outdoor light in itself, so flow then skips sky sectors.
// Whether the light flow is baked at all: only the Classic model shows it (scene.cpp's
// FEATURE_LIGHT_FLOW, flowLightAt). Under Whole level it was still traced at every level
// load, about a second, for nothing.
inline bool flowWanted() {return settings.lightFlow&&settings.lightModel==0;}
inline int flowInputs() {return bakeThingsKey+(settings.windowLight&&settings.bakedLights?2:0)+(settings.doorwayLight&&settings.bakedLights?4:0);}
// bake_cache.cpp: the level's light bakes kept on disk (bake-cache/ in the
// save folder), so a second visit to a map with the same settings loads what
// each bake's expensive loops made instead of tracing it again. A bake calls
// bakeCacheRestore just before its loops with BAKE_BUILT (its own file's
// build time, so a rebuilt bake never takes what an older one made), what it
// reads that the cache cannot see (inputs) and, in flags, the shape of what it
// makes (the bounce: 1 with the bump basis). True: the loops' output is in
// place and flags holds what was kept with it. Otherwise it runs the loops and
// hands the result to bakeCacheKeep with its flags. bakeCacheFinish, once a
// frame after its bakes, writes the file and reports. DERP_NOBAKECACHE=1 turns
// it off; DERP_BAKECACHECHECK=1 bakes afresh and compares with the cache.
#define BAKE_BUILT __DATE__ " " __TIME__
uint64_t glassLightInputs();   // glass_light.cpp: what the stained glass adds to the lights' inputs
uint64_t bakeHash(const void *data,size_t bytes,uint64_t seed=0);
bool bakeCacheRestore(LoadStage stage,const char *built,uint64_t inputs=0,int *flags=nullptr);
void bakeCacheKeep(LoadStage stage,int flags=0);
void bakeCacheFinish();
} // namespace doom3d
#endif
