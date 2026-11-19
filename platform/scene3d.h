/* Platform-neutral 3D scene: map geometry, lights, effects and settings built
   at runtime from the loaded WAD. A GPU backend uploads and draws the result. */
#ifndef DOOM_SCENE3D_H
#define DOOM_SCENE3D_H
#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <unordered_map>
#include <vector>
#include "shaders/vertex_modes.h"
extern "C" {
#include "doomtype.h"
}
#include "climate_rules.h"
#include "flash_lighting.h"
#include "i_render3d.h"

struct SDL_Window;
// Defined by the backend; the scene only holds and passes references.
struct GpuTexture;
using GpuTextureRef=std::shared_ptr<GpuTexture>;
enum class GpuFormat { R8, RG8, RGBA8, RGBA16Uint };

// Backend hooks used while the scene builds its resources.
// levels>1: pixels holds that many mip levels packed tightly one after another.
GpuTextureRef gpuCreateTexture(GpuFormat format,int width,int height,const void *pixels,int bytesPerRow,int levels=1);
// A 3D texture, its slices one after another.
GpuTextureRef gpuCreateVolume(GpuFormat format,int width,int height,int depth,const void *pixels);
// Replaces rects (x, y, width, height) of a texture with the same rects of
// pixels, an image of the texture's size; the rest keeps its contents.
void gpuUpdateTexture(const GpuTextureRef &texture,GpuFormat format,const std::vector<std::array<int,4>> &rects,const void *pixels,int bytesPerRow);
// The light of the 3D view as it was last drawn: the geometric mean of its luminance, 0-1, from
// a frame or two ago; below 0 before the first. For F4 Eye adaptation (scene3d/exposure.cpp).
float gpuFrameLight();
// Its bright end: the brightest channel that nine tenths of the view lie under, 0-1.
float gpuFrameBright();
// Reads an emissive mask image as value*alpha in [0,1]; false if unreadable or the wrong size.
bool platformReadMask(const char *path,int width,int height,std::vector<float> &values);
// The most MSAA samples the GPU offers for the world targets (1: none).
int gpuMaxSamples();
// For the FPS readout: the GPU's name and its driver's version (empty where
// unknown), the API it is driven through, and how frames reach the display
// now (the present mode in use, which may be a fallback for the chosen one).
const char *gpuDeviceName();
const char *gpuDriverVersion();
const char *gpuApiName();
const char *gpuPresentationName();
// F4 "HDR" (gpu3d/hdr_output.cpp): whether the display takes HDR output, and for the readout the
// output in use with its paper white and headroom (empty in SDR with HDR off).
bool gpuHdrOffered();
const char *gpuHdrName();

namespace doom3d {
constexpr float doomPi=3.14159265358979323846f;
inline float units(int value) { return value/65536.0f; }
struct Point { float x,y; };
// sunU/sunV: texel in the wall bake atlas, or -1 for surfaces without one.
// statics: on sprites with bake-only lights, the static light's direction
// (x, y, z as unorm bytes) and how much of it comes from that direction (w);
// on walls, a seed per wall section so their uneven tiles differ (tiles.glsl).
// lampTint: the sector light's color from the lamps around a sprite (lampTint
// in baked_lighting.h), unorm bytes; white for surfaces, which compute their own.
struct Vertex { float x,y,z,u,v,light; unsigned mode; float red=0,green=0,blue=0; unsigned lightMask[2]={}; float sunU=-1,sunV=0; unsigned statics=0; unsigned lampTint=0xFFFFFFFFu; float lift=0; };
// right.w: where the wear records start in the sector buffer (0: none);
// forward.w, up.w: how wet the windows and the ground under the sky are.
// mood: x how bright the sky is drawn (the episode's mood, Game_Mood).
// fx: exposure, sky light tint; fx2: render-target pixels per Doom pixel,
// sky light level (0: off); texFilter: sharp bilinear edge softness in texels, palette mipmaps (0: off);
// flicker: the current light of the baked flicker groups, group 0 steady.
// bank, bankColor: the fog bank (scene3d/fog.cpp, shaders/fog_bank.glsl).
// climate: the heat of the day and the cold on clear windows (scene3d/climate.cpp).
// beam, beamAim, beamLight: the flashlight's beam in the weather (scene3d/beam_air.cpp).
// farFog, farFogRange: a distance fog as in Turok (DERPINFO farfog): colour (rgb), on (w); clear to range.x, solid from range.y.
// rush: the fall's smear (rush.cpp, rush.glsl): the point it falls toward in screen uv, homogeneous (xyz), how hard (w).
// eyes: the player's (eyes.cpp): lids closed (x, 0-1), dark (y, 0-1).
// skyLayers: where the sky's painting is seen from (xyz), 1 when its layers are on (w; sky_layers.cpp);
// skyShape: the near ridge's, the mist's, the far ridge's and the clouds' ring radius;
// skyExtent: the near and far rings' tallest tower top over the reference eye (in each ring's radii), 1 when there is a far ring, the floor's drop (near radii).
// skyWind: where the level's steady wind blows (xy, unit), how strong (z: 0 still air - 1 a gale), for the sky's clouds.
// skyCloud: the map's own place in the cloud field (xy), and its cover and backdrop (game_mood).
// skyState: how much weather, steady (x), how far into night (y); sky/cloud_state.glsl.
struct Uniforms { float eye[4],right[4],forward[4],up[4],projection[4],effects[4],materials[4],flashlightTint[4],fog[4],map[4],water[4],sun[4],bake[4],fx[4],fx2[4],texFilter[4],outdoor[4],haze[4],mood[4],weather[4],phobos[4],weatherTint[4],fallColor[4],settleColor[4],bank[4],bankColor[4],climate[4],beam[4],beamAim[4],beamLight[4],farFog[4],farFogRange[4],flicker[4][4],clearing[8][4],rush[4],eyes[4],skyLayers[4],skyShape[4],skyExtent[4],skyWind[4],skyCloud[4],skyState[4],shadowSkip[4]; };
static_assert(sizeof(Vertex)==68&&sizeof(Uniforms)==848,"Shader buffer layout");
using Flash=doom_flash_t;
// 63 lights keep the FlashSet uniform block under 4 KB and fit the 64-bit light masks.
constexpr unsigned maxLights=63;
struct FlashSet { unsigned count=0,reserved[3]={}; Flash lights[maxLights]={}; };
using LightBlocker=doom_light_blocker_t;
static_assert(sizeof(Flash)==64&&sizeof(FlashSet)==4048&&sizeof(LightBlocker)==32,"Flash shader layout");
struct FogLights { unsigned count=0,indices[4]={},padding[3]={}; };
static_assert(sizeof(FogLights)==32,"Fog shader layout");
struct Settings {
    int accelerated=1, widescreen=1, crosshair=1, look=1, retro=0, fps=1;
    // How frames reach the display (gpu3d/presentation.cpp): 0 VSync, 1 VSync
    // with 3 frames in flight, 2 Fast (mailbox), 3 Adaptive, 4 Off (uncapped);
    // the limit for the uncapped ones: 0 none, 1 the display's rate, 2 30,
    // 3 60, 4 120, 5 144, 6 240 a second.
    int presentation=2, frameLimit=0;
    // HDR output (gpu3d/hdr_output.cpp): on where the display takes it; the view drawn in float
    // so lit surfaces, gloss and burned-out light rise past white too (costs bandwidth); paper
    // white over the system's SDR white; how much of the light over white rises into the headroom.
    int hdr=0, hdrScene=1;
    int hdrColors=0; // 0 Auto (vivid on the Steam Deck OLED's own screen, which shows SDR stretched to its wide gamut), 1 accurate, 2 vivid.
    float hdrBrightness=1, hdrHighlights=1;
    int windowSize=0; // The window's exact size (scene3d/window_size.cpp): 0 free, else its height in pixels at 16:9 (720, 1080, 1440, 2160).
    int scale=100; // Render size: up to 100 a percentage of the window, above that a fixed height in pixels (never above the window's).
    int upscale=0; // The render stretched to the window: 0 smooth (bilinear), 1 crisp (nearest), 2 sharp bilinear.
    // The status bar (scene3d/status_bar.cpp): its size, 100 the classic 4:3 frame's, less that share of it at whole
    // pixels per DOOM pixel; across the whole width (the wide STBAR's sides, then the border flat).
    int hudSize=100, hudWide=0;
    int invertLook=0; // Mouse look: moving the mouse forward tilts the view down.
    int weaponBob=0; // The weapon bob: 0 DOOM's own clock, 1 in time with the footsteps, 2 none.
    int classicAim=0; // With mouse look: bullets autoaim at a monster in front as in DOOM, following the view only with none.
    int movement=0; // 0 DERP's tighter tuning, 1 DOOM's own (acceleration, friction, faster diagonals), 2 tuned plus held running diagonals easing into DOOM's strafe-running.
    int quickConfirm=1; // Quicksave and quickload ask before overwriting or loading.
    // Gamepads (i_sdl_gamepad.c): look speed across and up and down, the sticks' dead zone,
    // the turn boost at full tilt, the sticks swapped (southpaw), gyro aim (0 off, 1 on,
    // 2 while the gyro button is held) and its speed.
    float padLookX=1, padLookY=1, padDeadzone=0.15f, gyroSpeed=1;
    int padBoost=1, padSouthpaw=0, gyro=0;
    // Rumble (gamepad_rumble.c): 0 off, 1 low, 2 full; the trigger motors; rumble from what is heard around.
    int rumble=2, triggerRumble=1, worldRumble=1;
    float msaa=0; // Geometry edge anti-aliasing: 0 off, then 2x, 4x and 8x MSAA (capped by gpuMaxSamples).
    // Walls and floors: 0 crisp pixels, 1 smooth (bilinear), 2 sharp bilinear
    // (flat texels, edges blended over sharpSoftness texels plus a pixel).
    int filter=0;
    float sharpSoftness=0;
    int paletteMips=0; // Palette-snapped mip levels for distant walls and floors when smooth or sharp.
    float fov=90;
    float flashlightTintGain=1;
    int emissive=1, fog=1, palette=0;
    int bloom=1; // With emissive: glowing texels, eyes and fullbright pickups bleed a soft halo.
    int detail=1; // Derived normal maps and palette gloss.
    int variedHighlights=1; // Gloss highlights tighten on smooth artwork and spread on busy artwork, per palette ramp.
    int unevenTiles=1; // Tiles painted into floors each lie a little crooked, with a lit or shadowed lip where they meet.
    int bakedBumps=1; // Bump detail takes all baked light the way it comes (the HL2 basis, scene3d/bake_basis.cpp).
    int unevenWalls=1; // The same for the tiles, plates and bricks painted into walls and ceilings.
    int rebrick=1; // Oversized painted bricks (DOOM's 64 x 32) laid anew at half the size from their own artwork (scene3d/relaid_walls.cpp).
    int softLight=1; // Light seam blending and edge contact shading.
    int reflections=1; // Mirrored scene on water.
    int retroReflections=1; // Eighth resolution, palette colors, stepped wobble.
    int spriteFilter=0; // 0: crisp, 1: bilinear.
    int sun=1; // Sun shadows baked at level load from the sky texture.
    int bakedLights=1; // Static lights baked at level load; nearby dynamic copies add flicker and detail.
    int lightModel=1; // 1 Whole level, the standard: the level lit as one model by its own lights (PLAN-light-model.md). 0 Classic: Doom's sector light with the bakes on top.
    float surfaceReturn=2; // With lightModel 1: how much lighter than their artwork the surfaces count when light bounces off them (1-3). 2 since the bounce keeps its energy (PLAN-bounce-energy.md): 3 made up for what it lost.
    int lightByLogic=0; // With lightModel 1, a test: the mapper's light levels set no brightness; lamps in full, daylight by the hour, no fill.
    float fillLight=0; // With lightModel 1: how much of the mapper's light level a room keeps as light with no source (0-1).
    float lampTail=0; // Baked lamps light on past their radius at this share, falling with the square of the distance (BakeLight::tail, 0-0.5): a bright room spills into the dark one beside it.
    int doorwayLight=0; // An opening from a brighter room lights the darker room beside it like a soft lamp, in the bake (bake_doorways.cpp).
    int windowLight=1; // Openings to the sky light the rooms beside them like big soft lamps, in the bake.
    float windowStrength=1; // How brightly window light shines in (0.5-2).
    int dimLitRooms=1; // Rooms the baked lamps and windows light well lose part of their flat sector light.
    float unlitDarkening=0.25f; // With dimLitRooms: the least share of flat light any indoor room loses (0-0.5).
    int bounce=1; // Light bounced from sunlit and lit surfaces, baked at level load.
    float bounceTint=3; // With bounce: how strongly bounced lamp light colors the sector light around it (0-4).
    int caustics=1; // Rippling light from liquids on nearby walls.
    // Experimental effects, each separately switchable.
    // F4's combined choices; fitGame sets the switches below them from these.
    int deaths=4; // Deaths: 0 the game's frames, 1 varied, 2 procedural falls, 3 ragdolls, 4 ragdolls Hollywood (flying, punch, spins).
    int hitReactions=1; // Hit reactions: death reactions and hit flinches.
    int mode=1; // Gameplay (F4 Game, Mode): 0 normal (the derived look), 1 authored episodes, 2 physically accurate Phobos; sets authored and phobos.
    int bloodLevel=2; // Blood: 0 off, 1 splats and pools, 2 full (spray, shine).
    int gore=1; // Gore (F4 Game, a rule of the game): kills past the gib line tear bodies apart; off in demos and netgames.
    int tearFibres=1; // With Gore: a part torn off stays joined by fibres that stretch and snap before it flies off (tear_fibres.cpp).
    int blood=1; // Splats on floors and walls from hitscan hits; pools under corpses.
    int bloodSpray=1; // With blood: hits and kills throw drops that land as splats; wounds drip, sliding bodies smear, bodies thrown into walls splash them.
    int bloodShine=1; // Fresh blood glints under dynamic lights, then dries matte.
    int deathMotion=1; // Deaths jolt with the killing blow's push and height before the game's corpse frame.
    int slowMotion=0; // 0 off, 1 its key (X, F12), 2 also when shooting: the world at a quarter, the view at full speed (slow_motion.c).
    int wallHits=1; // Shots on walls throw what the wall is made of: dust, splinters, sparks; drawing only.
    int wounds=1; // Hitscan hits shoot holes through monsters' sprites, rimmed with blood; drawing only.
    int monsterBlood=1; // Enemy-appropriate blood: Cacodemons bleed blue, Barons and Hell Knights green, Lost Souls nothing (Game_BloodTint).
    int pickupFlash=1; // Pickup flash: 0 the WAD's gold for everything, 1 by item (p_pickup_flash.c): health green, armor blue, keys their colour, strength by how much.
    int deathFalls=1; // Rigged monsters (hand rigs, else joints found in the art) fall procedurally: 0 off, 1 by the hit, 2 always legs swept, 3 always chest blast, 4 always head shot.
    int deathRagdoll=0; // Rigged monsters fall as ragdolls instead: the pain drawing raised into a relief on a small skeleton the hit knocks over.
    int ragdollPixels=0; // Ragdoll pixels: how the card redraws the body (ragdoll_pixels.h CardPixels): 0 nearest, 1 anchored, 2 coverage, 3 coverage steady, 4 blended, 5 blended steady.
    int deathGuided=1; // With deathRagdoll: chest blasts from the front sometimes play the shot death on strings (hand rigs that fit it: the zombieman).
    int deathBlast=0; // With deathRagdoll: explosions (rockets, barrels, the BFG) send bodies flying, glancing off walls until they land.
    int deathFaceViewer=0; // With deathDirector: a monster killed facing away turns toward the eye before its death frames.
    int deathDirector=1; // Each death drawn its own way: staggers and turns from the monster's own frames, retimed, mirrored to fall away from the blow.
    int hitFlinch=1; // Living monsters flinch from hits, leaning with the push.
    float bodyMotionStrength=1; // How far death reactions and flinches throw the body (0.5-3).
    int flashlightShadows=1; // Things in the flashlight beam cast their silhouette on the wall behind.
    int classicFlashlight=1; // A shorter, warmer beam that raises light in steps to a little past full brightness, never further.
    int heroLight=0; // 0 off, 1 faint, 2 strong: near-black surfaces and things close by lift toward a low floor (world.frag); a new config takes Game_HeroLight().
    int shadowVolumes=1; // Whole-level lighting: monsters block light (shadow volumes, gpu3d/frame.cpp's drawShadowVolumes); off: a silhouette laid over floor and wall.
    int stippledShadows=0; // Shadow edges from walls and ledges (the flashlight's, shots') break into an ordered dither; off: a hard edge in the artwork's fat pixels.
    int flashRooms=1; // A shot, a spark or a bursting barrel lifts the light of the room it goes off in while it shines (room_pulse.cpp).
    int softShotShadows=1; // Shots, projectiles and barrels light from a small body: shadow edges step per texel and soften into a dither.
    int softSprites=1; // Explosions and puffs stop slicing into walls; dithered toward floors.
    int explosions=1; // Rockets and barrels: a fireball drawn per blast, sparks, smoke, a lingering light, the barrel in pieces (explosions.cpp).
    int heatHaze=0; // Stepped raster shimmer above lava, hot floors and flames.
    int eyeAdaptation=1; // Brief stepped over/underexposure when the light changes.
    int splashes=1; // Ripple rings where things land or walk in liquids, spouts where shots hit them; bubbles and sinking in thick ones.
    int dust=1; // Specks glinting in sunbeams and the flashlight beam.
    int embers=1; // A few sparks rising from flames, blown along by the game's wind.
    int phobos=0; // (From mode.) Physically accurate Phobos (p_phobos.h): DOOM's first episode with Phobos' gravity, friction and real knockback.
    int phobosSpin=0; // Phobos: the guns' kicks turn the player's view (Game_LookKick).
    int phobosPlayer=1,phobosEnemies=1,phobosAir=2,phobosRecoil=1; // Phobos: how much help (Game_SetPhobosRules).
    int authored=1; // (From mode.) Authored episodes (Game_Mood): hand-picked looks for the game's own maps (DOOM's E1 night in the rain); off: the derived look everywhere.
    int weather=1; // What falls under the sky: 0 nothing (clear), 1 where the map's DERPINFO or an authored episode has it, else always: 2 rain, 3 snow, 4 nukage rain, 5 sand, 6 ash, 7 fog, 8 dry ice.
    // The rest of the climate (climate_rules.h), each the map's own or one choice everywhere:
    int fallAmount=0; // How hard it falls (or how thick it lies): 0 the map's own, 1 light, 2 moderate, 3 heavy, 4 extreme.
    int timeOfDay=TIME_OWN; // The hour: the sun's height, colour and strength, the sky's tint; night as the authored E1's.
    int temperature=climateOwn; // Degrees C outside, -30 to 50 in tens: heat shimmer and glare from 30, frosted windows below 0.
    int wind=WIND_OWN; // How hard the wind blows: still, a breeze, strong, a gale.
    int windFrom=0; // Where it blows from: 0 the map's own, 1 north, on round the compass to 8 north-west.
    int weatherLamps=1; // Rain, snow, sand and ash near a torch or a glowing wall catch its light (scene3d/weather_lamps.cpp).
    int wornPlates=1; // Plain metal plates (Game_WornPlate) show years of use: grime in the seams, dull blotches, scuffs near the floor.
    int metalShading=1; // Bare metal (Game_WallSurface/Game_FlatSurface METAL) takes highlights in its own colour (world/surface.glsl metalness).
    int windowStreaks=1; // Dark rain runs down the outside walls below windows, rusty on metal; inside too once rain blows through a broken one.
    int playerShadow=1; // The player's own sprite shadow from the sun or a strong light.
    int doorLight=1; // Light from a bright room spills through a door as it opens.
    int instantShots=1; // Whole-level lighting: the shots of the lamps near the eye are baked before they happen, on the processor, so a shot lamp's light leaves on the frame it breaks (relight.cpp's prepared shots; 50-150 MB). 0 off, 1 quick (three helper threads), 2 gentle (two).
    int gpuRelight=0; // Where re-bakes in play run (relight.cpp, PLAN-gpu-bakes.md stage 5): 0 auto (the graphics card, the processor on a Steam Deck and a Mac), 1 the graphics card, 2 the processor.
    int movingRelight=1; // Baked lamp light and sun re-bake round a door, floor or wall that moved and stays (relight.cpp); doors that close again keep the spill only.
    int texelLight=1; // Baked light, sun and bounce blend per texture pixel instead of hard map cells.
    int fastBakeReads=0; // Texel-aligned baked light read with the card's own blending where four cells agree (bake.glsl's fast path): 0 auto (on a Steam Deck only), 1 on, 2 off.
    int skyLight=1; // Cool fill from the visible sky, baked at level load; dark under overhangs.
    int bakedAO=1; // Corners, ledges and alcoves darken, baked at level load.
    int thingShadows=1; // Decorations (columns, trees, hanging bodies) cast baked shadows.
    int lightFlow=1; // Brighter sectors light their neighbors through openings, flicker included.
    int ceilingCaustics=1; // Liquid caustics also ripple on the ceilings above.
    int causticsComputed=1; // Caustics are the light the liquid flat's waves gather, sharpening with distance from the water.
    int causticsGrow=1; // Caustic shapes grow from 2 to 4 units per texel with distance from the water.
    int causticsAngle=1; // Flashlight caustics follow how much the water reflects at the beam's angle.
    int causticsSway=1; // Caustics sway with the wave slope, more the farther the light travels.
    int causticsSprites=1; // Monsters and things near liquids catch the caustics too.
    int causticsShots=0; // Muzzle flashes and projectiles over liquids throw caustics, not just the flashlight.
    int dampShores=1; // Walls and banks just above liquids turn darker and damp up to a ragged line.
    int glowEdges=1; // Walls meeting nukage, slime or lava take the strip the game paints on its edge textures.
    int shoreRipples=1; // Liquids ripple a little where they meet walls, each in its own way: 1 drawn as blocks (liquids.cpp), 2 by the liquid's own shader (world/shore.glsl).
    int sunDisc=1; // A faint sun in the sky where the baked sunlight comes from.
    int skyLayers=1; // The sky's ridges split from it at their own distances, shifting as you move, valley mist between (sky_layers.cpp).
    int sunShafts=1; // Soft sunbeams slanting down through ceiling holes and windows, with the dust motes in them.
    int sunScatter=1; // Sunbeams glow brighter seen toward the sun and fainter from behind it.
    int glossyScreens=1; // Monitor glass found in computer textures bulges, refracts the screen behind it and catches light.
    int liveReadouts=1; // The LED readouts painted into computer textures blink their lamps on a simple console loop (doom_screen_readouts).
    int glowingEyes=1; // The eyes painted into monster artwork glow in the dark, with bloom (sprite_eyes.h).
    int windowGlass=1; // Windows found in the map get glass that reflects and shatters when shot through.
    int shootableLights=1; // Experiment: tech walls spark when shot; shots into a lamp or screen break its fixture and re-bake its light.
    int muzzleFire=1; // Fire blocks and smoke at a bullet gun's barrel, the enemies' and the player's (scene3d/muzzle_fire.cpp).
    int killShotsCarry=1; // With shootableLights: a hitscan shot that kills its target carries on to spark and break lights behind it.
    int glassReflections=1; // Experiment: the nearest window mirrors the world, drawn again like the liquid reflection.
    int weaponLighting=1; // The weapon's painted sheen and light come out at load; lights around the player relight it.
    int weaponArt=0; // With weaponLighting: 1 relights frames rendered from 3D models (weapon pack) where there are some.
    // Sound, though graphics.cfg keeps it: the mixer is told in applySettings.
    int musicDevice=0; // What plays the music: 0 SoundFont, 1 OPL FM, 2 the Mac's own synthesizer.
    int soundtrack=1; // 0 the PC scores; recordings where their pack is beside the game WADs: 1 PlayStation, 2 3DO, 3 IDKFA remix, 4 originals.
    int soundOutput=0; // 0 speakers, 1 headphones through a KEMAR head, 2 through a KU100.
    int sfxResampling=0; // Effects between their samples: 0 held (original sizzle), 1 cubic, 2 windowed sinc.
    float sfxTreble=1; // Made-up treble above the samples' own band, past Original playback (0-2).
    // The dearest sounds are off by default (mixer thread, demo1 in DERP mode, 2026-11-19: about 70 ms of
    // mixing a second with all four on, 8 with none): room acoustics ~56, traced tails ~16 plus a bake of
    // ~20 s of worker time after level start, ambience ~15, far rooms ~13.
    int roomAcoustics=0; // Echoes and a reverb tail measured from the room around the player.
    float reverbAmount=1; // How loud the echoes and tail are (0-2).
    int farRooms=0; // F4 Sound: on authored maps, how many far rooms ring at once with their own reverb (3, 1 or 0).
    int roomTails=0; // F4 Sound: on authored maps each room's tail traced from the map (1) or reverb.c's everywhere (0).
    int roomGrid=1; // F4 Sound: on authored maps the room around the player from a grid baked at level start (1) or live rays (0).
    int muffleWalls=1; // Sounds out of sight are muffled, more behind closed doors.
    int muffleDistance=1; // Far sounds lose their treble.
    int naturalFalloff=1; // Far sounds fade as in air (half as strong per doubled distance), not in DOOM's straight line.
    int madeImpacts=1; // Made-up sounds for sparks, lamps and screens dying, and glass breaking (audio/made_bank.h).
    int glassSound=0; // Which breaking glass: 0 the shatter, 1 the fracture (a branching crack front).
    int ambience=0; // Lamps, machines, liquids, fire and wind sound where the map's textures show them.
    int randomPitch=0; // Effects start a little higher or lower each time, as in the earliest DOOM.
    int alternativeComputerSounds=0; // Machine hum, hard-drive seeks and relay chatter in place of the computer bleeps.
    float ambienceAmount=1; // How loud the ambience is next to the effects (0-2).
    int monsterVoices=1; // DERP mode's monsters bark and cry in their own voices (p_barks.c); off: id's sounds.
    int footsteps=1; // The player's footsteps by floor, splashes in liquids, monsters splashing as they wade.
    int soundStyle=2; // F4 Sound's Sounds row (settings_presets.cpp): 1 DOOM's own, 2 the defaults, 0 Custom (as the switches come to).
    // Performance: cheaper stand-ins for per-frame light work.
    int bakeOnlyLights=1; // Static lights only in the bake, with its light direction and flicker groups; pools as area lights.
    int gridSpriteLight=1; // Things take static light and their shadow light from a grid baked at level load.
    int unoccludedSurfaceLights=0; // Glowing textures' dynamic light skips wall tests; its gloss can show through walls.
    int fewerSurfaceLights=0; // Only the 8 nearest glowing-texture lights stay dynamic, not 24.
    // The level's bakes run on the graphics card where it has compute shaders (PLAN-gpu-bakes.md, scene3d/gpu_trace.h); off: on the processor.
    // Off on a Mac: there the processor bakes faster (M2, fresh loads: E1M1 3.0 s against 4.3, E1M7 4.7 against 8.1; the lamps 3-4x slower on the card).
#ifdef __APPLE__
    int gpuBakes=0;
#else
    int gpuBakes=1;
#endif
    int fastBakes=0; // Sky light and bounce bake with 16 rays a sample instead of 64 (sky) and 256 (bounce): faster level loads, a little patchier (bake_indirect.cpp).
    int poolPoints=64; // Whole-level lighting: points a glowing pool's light is traced from, 64, 32 or 16 (bake_lights.cpp's buildPoolAreas); fewer bake and re-bake faster near nukage.
    int skipHiddenGeometry=1; // Walls, flats and things the BSP walk finds hidden from the eye are left out each frame.
    int preset=3; // F4 Graphics, Display: the preset the options that cost frame time are set as (settings_presets.cpp): 0 none (custom), 1 lowest, 2 handheld, 3 balanced (these defaults), 4 high, 5 everything.
    int skyCache=0; // The sky painted a little at a time into a small cube instead of live: 0 real time, 1 fine, 2 chunky (gpu3d/sky_cache.cpp).
};
// glassFrame: on textures with monitor screens, where each glass pixel sits
// on its screen (see screen_glass.h); null otherwise. relief: on walls and
// flats, the bump slope and grain the shader shades them with (withRelief).
// brushedDown: the artwork's streaks run along v, so bare metal is brushed
// that way (withRelief); otherwise along u. blank: the share of smooth
// texels; only mostly blank metal is brushed.
struct Image { bool opaque=true; std::vector<byte> pixels; GpuTextureRef texture,glassFrame,relief; int width=0,height=0,left=0,top=0; std::array<float,3> glow={1,1,1}; float glowWeight=0,glowHeight=0.85f; std::array<float,3> emissionColor={}; float emissionWeight=0,emissionCoverage=0,emissionU=0,emissionV=0; std::vector<byte> emission,screens; std::array<float,3> average={},brightest={}; bool averaged=false,brightened=false; bool leaded=false; bool indicator=false; /* a switch's lamp: it glows and lights next to nothing, and is no lamp a sector's flicker is for */ bool brushedDown=false; float blank=0; };

// Frame profiling for the performance smoke test (I_Render3DProfile).
struct Profile { bool enabled=false; unsigned skip=0; const char *screenshotPath=nullptr; I_Render3DFrameProfile frame={}; };
extern Profile profile;

extern Settings settings;
extern SDL_Window *gameWindow;
extern FlashSet flashes;
// What the GPU gets: flashes with first/count into lightWords, the blockers
// and their per-direction slices (buildLightWords in scene3d/lights.cpp).
extern FlashSet gpuFlashes;
extern std::vector<std::array<uint32_t,4>> lightWords;
extern FogLights fogLights;
// The relit weapon's surroundings: ahead, behind, right, left, up, down (weaponDraws).
extern float weaponProbe[24];
extern std::vector<LightBlocker> lightBlockers;
/* Texture keys: wall index >=0, WAD lump = -1-lump. */
extern std::unordered_map<int,Image> images;
extern std::map<int,std::vector<Vertex>> batches,shadowBatches,decalBatches;
// The sprites of the glass mirror pass, seen from the mirrored eye, the
// player's own included (addThing in scene3d/geometry.cpp).
extern std::map<int,std::vector<Vertex>> mirrorBatches;
extern std::vector<Vertex> mistVertices,particleVertices,heatVertices;
// Glowing particles (ash's embers): drawn as particles, and into the bloom
// pass as well, so they glow like a monster's eyes without lighting anything.
extern std::vector<Vertex> glowVertices;
// Liquid crests (splash rings, shore ripples): brighten what is under them by
// one plus their color (crestPipeline).
extern std::vector<Vertex> crestVertices;
// The rain round the nearest lamps (scene3d/lamp_sheets.cpp): cards of
// streaks added over the scene, hidden by what stands in front (lampSheetPipeline).
extern std::vector<Vertex> lampSheetVertices;
// Shadow volumes (scene3d/shadows.cpp's shadowVolume; gpu3d/frame.cpp's drawShadowVolumes): per
// light that casts this frame, the closed shapes behind the things it is cast on, for the
// stencil. kind 0: none; 1 a dynamic light (light[0] its index among the frame's lights), 5 the same, the flashlight;
// 2 the baked light, all of it as one (each thing's volume from the way its light comes);
// 3 the sun; 4 the baked light where most of it is a glowing pool's: a wide light, which a
// body hides only part of, so its shadow takes a third of what a lamp's does.
struct ShadowVolumeSet { int kind=0; float light[4]={}; std::vector<Vertex> vertices; };
constexpr int shadowVolumeSets=6;   // a dynamic light, the sun, the lamps, the pools, a second dynamic light, the flashlight
extern ShadowVolumeSet shadowVolumes[shadowVolumeSets];
// Sunbeam ribbons, drawn additively in the mist pass.
extern std::vector<Vertex> shaftVertices;
// Sky ceilings and sky walls, drawn into depth with the sky shader.
extern std::vector<Vertex> skyVertices;
// Whether the level has any sky (a sky flat). Without one (the library),
// nothing is drawn behind the world: past all it draws is darkness.
extern bool levelSky;
// Whether a far fog (DERPINFO farfog) is on: the sky's backdrop is drawn in
// its colour then, sky or not (sky.frag).
extern bool farFogActive;
// Window panes (scene3d/windows.cpp), blended over the finished world by glass.frag.
extern std::vector<Vertex> glassVertices;
// With glass reflections: whether the world is mirrored in a window this
// frame, that pane's plane (unit normal toward the eye, offset), and how far
// it has faded in from its probe since it became the mirror (0-1).
extern bool glassMirrorActive;
extern bool glassMirrorSilvered;   // the chosen mirror is a wall mirror (wall_mirrors.cpp), not a window
extern float glassMirrorPlane[4],glassMirrorFade,glassMirrorSpan[2];
// Glass reflection probes: cube maps captured from in front of each window
// on each side, for panes without the live mirror. at: where; low/high: the
// room's box, which reflected rays are projected onto for parallax. The
// backend renders glassCaptures this frame into its probe atlas, which it
// reallocates when glassProbeSerial changes.
constexpr size_t maxGlassProbes=64;
constexpr int glassProbeSize=64;
struct GlassProbe { float at[3],low[3],high[3]; };
extern std::vector<GlassProbe> glassProbes;
extern std::vector<int> glassCaptures;
extern unsigned glassProbeSerial;
// Counts settings changes, so what is built from them (probes) can redo it.
extern unsigned settingsSerial;
extern GpuTextureRef seamTexture,contactTexture,cloudTexture,wallBakeTexture,flatLightTexture,causticTexture,causticPattern,shoreTexture;
// Bake-only lights: where static light comes from, per wall atlas texel and
// floor/ceiling cell (see bakeLights).
extern GpuTextureRef wallDirectionTexture,flatDirectionTexture;
extern float reflectionPlane;
extern bool reflectionActive;
extern bool worldPending,screenshotPending,flashlightOn;
extern float pitch;

// Screen layout of the world view and the 4:3 HUD in render-target pixels.
// Where the 2D layer lies (scene3d/status_bar.cpp): the 320x200 frame (ui) in render pixels, and
// the status bar when it is drawn apart from it, in the screen's pixels (screenW x screenH; apart:
// its rows from top down; x, y, w, h its 320 columns; left and right what it covers with the wide
// sides; scaleX, scaleY the screen pixels per DOOM pixel).
struct StatusBarPlace { bool apart=false; int top=200; float x=0,y=0,w=0,h=0,left=0,right=0,scaleX=1,scaleY=1,screenW=1,screenH=1; };
struct ScreenLayout { float uiX,uiY,uiWidth,uiHeight; StatusBarPlace bar; };
struct FrameView {
    Uniforms camera;
    float uiX,uiY,uiWidth,uiHeight,worldX,worldY,worldW,worldH;
    StatusBarPlace bar;
};
// The status bar for the present pass (present.frag's bar, barSpan, barSide): its rect in screen uv,
// the span its wide sides cover, whether it is apart and where its rows begin in the 2D layer, then
// the wide STBAR's left and width in uv (no width: none) and the border flat's repeats per uv (none: 0),
// then the hourglass's place and size in DOOM pixels from the bar's top left (no size: none; hourglass.cpp).
struct StatusBarDraw { float uniform[16]={}; GpuTextureRef flatTexture,wideTexture,glassTexture; };
// frameMean and cycleMean (liquid flats only): the shown frames' average color
// at the crossfade, and the whole animation's; cycleMean's alpha is how much
// of the artwork's contrast stays far away (0: not a liquid, no fade);
// frameMean's alpha is the material (surfaceMaterial in scene3d/scene.cpp).
// ripple, rippleShape: a liquid's shore ripples drawn by its own shader (scene.cpp's surfaceBinding); zero for none.
struct SurfaceBinding { const Image *image,*next; float blend; std::array<float,4> frameMean{},cycleMean{},ripple{},rippleShape{}; };
struct SpriteDraw { const Image *image; std::vector<Vertex> vertices; };

// Lifecycle: init reads options and graphics.cfg, the backend may override
// them, then applySettings installs the engine hooks.
void sceneInit(SDL_Window *window);
void sceneShutdown();
void applySettings();
void settingsChanged();
void buildCloudTexture();
Image &wallImage(int index);
// The sky to draw: on Phobos its copy with the painted air cleared (space.cpp).
const GpuTextureRef &skyImage();
// The sky split into layers by distance (sky_layers.cpp), a 1x1 texture when
// off, and its towers' baked light (sky_light.cpp).
const GpuTextureRef &skyLayersImage();
const GpuTextureRef &skyCloudField();
const GpuTextureRef &skyCloudNoise();
const GpuTextureRef &skyLayerLight();
Image &lumpImage(int lump,bool flat);
// Builds the camera, layout and (when the world is visible) all geometry and lights.
FrameView prepareFrame(int width,int height);
// Frame pacing (pacing.cpp): the backend times the two calls that may wait for
// the display, taking the swapchain image and submitting the frame, from paceStart.
double paceStart();
void paceAcquired(double start);
void paceSubmitted(double start);
// Level loading (loading.cpp): when loadPending, the backend calls
// runLoading before drawing the frame. It does the frame's level building
// and bakes while show draws the loading screen (level or other title, the
// current stage, 0-1 done), from the calling thread, a few times a second.
using LoadingShow=void(*)(const char *title,const char *stage,float fraction);
bool loadPending();
void runLoading(int width,int height,LoadingShow show);
// The HUD's pixels with the world's mask in alpha, in a buffer kept from frame
// to frame; changed: whether they differ from the last frame's.
const std::vector<unsigned> &hudPixels(const unsigned *pixels,bool &changed);
const std::vector<std::array<float,4>> &sectorInfo();
SurfaceBinding surfaceBinding(int key);
std::vector<SpriteDraw> weaponDraws(const Uniforms &camera);
std::vector<Vertex> crosshairVertices(float worldW,float worldH);
// status_bar.cpp: the screen's own size (the drawable; the render may be smaller), the layout for a
// render of w x h (prepareFrame), and what the present pass needs to draw the bar.
void setScreenSize(int width,int height);
ScreenLayout screenLayout(int w,int h);
StatusBarDraw statusBarDraw(const FrameView &view,const unsigned *palette);
// How many DOOM rows the weapon sits lower to reach the screen's bottom beside a narrow bar.
float weaponDrop();
void screenQuad(std::vector<Vertex> &out,float x,float y,float w,float h,float u0,float v0,float u1,float v1,float light=1,unsigned mode=MODE_SCREEN);
// 32³ nearest-palette lookup, BGRA, indexed (b*32+g)*32+r.
std::vector<unsigned> paletteLUTColors(const unsigned *palette);
bool renderCheckDue();
void saveScreenshot(const void *bgra,int width,int height,int stride);
void updateTitle(int width,int height,const char *renderer);
// F4's "Show FPS" readout (scene3d/frame_stats.cpp, stats_overlay.cpp): the
// backend marks each frame's moments (SDL_GetTicksNS): when it started waiting
// for the display, got its image, had the scene built (prepareFrame) and
// submitted it. statsOverlay: the readout for the top left corner,
// straight-alpha RGBA, null while it is off; render and display: the sizes the
// frame is drawn at and shown at.
void statsFrame(uint64_t waitStart,uint64_t acquired,uint64_t prepared,uint64_t submitted);
// Opt-in live frame capture. All timestamps are unscaled SDL nanoseconds.
struct FrameTraceTimes {
    uint64_t renderStart,waitStart,paced,acquired,prepared,submitStart,submitted;
};
bool startFrameTrace();
void skipFrameTrace();
void traceFrame(const FrameTraceTimes &times,bool loading,bool capture,bool visible,bool focused);
GpuTextureRef statsOverlay(int &width,int &height,int renderWidth,int renderHeight,int displayWidth,int displayHeight);
#ifdef DOOM_DEVTOOLS
// Local dev tools (devtools/): an overlay image for the top right corner, straight-alpha RGBA,
// drawn after prepareFrame; null when none.
GpuTextureRef devOverlay(int &width,int &height);
// And a signal glitch for the present pass (present.frag's upscale.w): 0 off.
float devGlitch();
#endif
} // namespace doom3d
#endif
