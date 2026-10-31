/* The F4 settings; see settings_panel.h. */
extern "C" {
#include "doomtype.h"
}
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include "settings_panel.h"
#include "game_profile.h"

namespace doom3d {
namespace {
Option check(const char *key,const char *title,int *flag,const char *tip,OptionCondition enabled=nullptr) {
    Option option{};option.kind=OptionKind::Check;option.key=key;option.title=title;option.tip=tip;option.flag=flag;option.enabled=enabled;return option;
}
Option choice(const char *key,const char *title,int *flag,std::vector<const char*> items,std::vector<int> values,const char *tip,
              OptionCondition enabled=nullptr) {
    Option option{};option.kind=OptionKind::Choice;option.key=key;option.title=title;option.tip=tip;
    option.flag=flag;option.items=items;option.values=values;option.enabled=enabled;return option;
}
Option slider(const char *key,const char *title,float *number,double low,double high,double step,const char *tip,
              OptionCondition enabled=nullptr,std::vector<const char*> labels={}) {
    Option option{};option.kind=OptionKind::Slider;option.key=key;option.title=title;option.tip=tip;
    option.number=number;option.low=low;option.high=high;option.step=step;option.enabled=enabled;option.items=labels;
    return option;
}
bool hasGlass(const Settings&) {return Game_HasEffect(GAME_EFFECT_WINDOW_GLASS)!=0;}
bool hasMonsterBlood(const Settings&) {return Game_HasEffect(GAME_EFFECT_MONSTER_BLOOD)!=0;}
bool hasFlashlight(const Settings&) {return Game_HasEffect(GAME_EFFECT_FLASHLIGHT)!=0;}
bool hasFalloff(const Settings&) {return Game_HasEffect(GAME_EFFECT_NATURAL_FALLOFF)!=0;}
bool hasFreeAim(const Settings &s) {return s.look&&Game_HasEffect(GAME_EFFECT_FREE_AIM)!=0;}
bool hasAuthored(const Settings&) {return Game_HasEffect(GAME_EFFECT_AUTHORED_EPISODES)!=0;}
bool hasPhobos(const Settings&) {return Game_HasEffect(GAME_EFFECT_PHOBOS)!=0;}
bool hasTunedMovement(const Settings&) {return Game_HasEffect(GAME_EFFECT_TUNED_MOVEMENT)!=0;}
bool hasWind(const Settings&) {return Game_HasEffect(GAME_EFFECT_WIND)!=0;}
// The climate's choices go together (climate_rules.h), in their order: the
// weather and the time of day are always all there; the temperature offers
// only what goes with them, the wind only what goes with all three. Picking
// one never hides it behind a choice below it (fitGame moves those).
Option fits(Option option,bool (*allows)(const Settings&,int)) {option.allows=allows;return option;}
bool temperatureFits(const Settings &s,int v) {return climateFits(weatherOfSetting(s.weather),s.timeOfDay,v,WIND_OWN);}
bool windFits(const Settings &s,int v) {return climateFits(weatherOfSetting(s.weather),s.timeOfDay,s.temperature,v);}
}

void fitGame(Settings &s) {
    // The combined Combat choices set the switches the effects read.
    s.deathDirector=s.deaths>=1;s.deathFalls=s.deaths>=2;s.deathRagdoll=s.deaths>=3;s.deathGuided=s.deaths>=3;
    s.deathBlast=s.deaths>=4;s.deathFaceViewer=0;
    s.deathMotion=s.hitFlinch=s.hitReactions!=0;
    s.blood=s.bloodLevel>=1;s.bloodSpray=s.bloodShine=s.bloodLevel>=2;
    // One way to play at a time: the authored looks or Phobos, never both.
    s.authored=s.mode==1;s.phobos=s.mode==2;
    if(!Game_HasEffect(GAME_EFFECT_WINDOW_GLASS))s.windowGlass=0;
    if(!Game_HasEffect(GAME_EFFECT_MONSTER_BLOOD))s.monsterBlood=0;
    if(!Game_HasEffect(GAME_EFFECT_SHOOTABLE_LIGHTS))s.shootableLights=0;
    if(!Game_HasEffect(GAME_EFFECT_NATURAL_FALLOFF))s.naturalFalloff=0;
    if(!Game_HasEffect(GAME_EFFECT_WIND))s.wind=s.windFrom=WIND_OWN;
    // The climate in its order: a weather or time picked that the
    // temperature or the wind below it doesn't go with moves them to the
    // nearest that does (fog in a gale: a breeze), or else the map's own.
    int kind=weatherOfSetting(s.weather);
    if(!climateFits(kind,s.timeOfDay,s.temperature,WIND_OWN)) {
        int best=climateOwn;
        for(int t=-30;t<=50;t+=10)if(climateFits(kind,s.timeOfDay,t,WIND_OWN)&&(best==climateOwn||std::abs(t-s.temperature)<std::abs(best-s.temperature)))best=t;
        s.temperature=best;
    }
    if(!climateFits(kind,s.timeOfDay,s.temperature,s.wind)) {
        int best=WIND_OWN;
        for(int w=WIND_STILL;w<=WIND_GALE;++w)if(climateFits(kind,s.timeOfDay,s.temperature,w)&&(best==WIND_OWN||std::abs(w-s.wind)<std::abs(best-s.wind)))best=w;
        s.wind=best;
    }
}

std::vector<OptionGroup> optionGroups(Settings &s) {
    std::vector<OptionGroup> groups={
        {"Game",{
            {"Mode",{
                {"Gameplay",{
                    choice("mode","Gameplay",&s.mode,{"Normal","Authored","Phobos"},{0,1,2},
                        "Normal: every map looks the way DERP derives it from the WAD, sun and all. Authored: the game's own episodes "
                        "get looks picked by hand; Knee-Deep in the Dead lies at night in a downpour. Phobos: Knee-Deep in the Dead "
                        "taken literally (see the Phobos page). Switching restarts a first-episode map; you keep what you carry.",
                        [](const Settings &d){return hasAuthored(d)||hasPhobos(d);})}},
                {"Weather",{
                    choice("weather","Weather",&s.weather,{"Map's own","Clear","Rain","Snow","Nukage rain","Sand","Ash","Fog","Dry ice"},{1,0,2,3,4,5,6,7,8},
                        "Rain falls under the sky in streaks of whole pixels and splashes where it lands; drops gather and run down "
                        "the outside of window glass, the ground darkens as it soaks, and you hear it. Snow drifts down in flakes "
                        "of a pixel, lies on the ground in ragged drifts, frosts the windows from their edges in, and hushes the echoes "
                        "outdoors. Sand blows through as a storm, grains streaking nearly flat on the wind and skipping along the "
                        "ground, dust rolling over the sky; it drifts against the walls in ripples and dusts the windows. Ash falls "
                        "as from an eruption: dark grey flakes tumbling down under a darkened sky, a few still glowing embers that "
                        "cool as they fall; it lies grey over everything. Fog lies instead of falling: it pools in the low ground outside, "
                        "over your head in the yards and round your knees on the ledges above, drifts in sheets on the wind, hides the sky, "
                        "and fills the rooms as far as the drafts carry the cold; lamps, fireballs and eyes glow through it, the "
                        "windows mist over and the open air goes quiet. Dry ice lies as on a colder world: a white layer round your knees "
                        "under a clear sky, fading in Doom's own hard light steps; it pours off ledges and fills the pits outside. Indoors it "
                        "never fills a room: it only streams in at the windows and doors the wind blows in by and along the drafts "
                        "through the rooms, showing where the air goes. Map's own: where the map's own DERPINFO or the Authored gameplay's look has rain or snow (rain in DOOM's first episode). "
                        "Clear: nothing falls anywhere and no weather is worked out (no drafts, nothing blown in), the lightest choice; the wind is only heard. Rain, Snow, Sand, Ash, Fog, Dry ice: on every map; picking one moves the temperature and wind below to the nearest that go with it. Nukage rain: rain in the nukage's colour that hurts everything alive under "
                        "the open sky, or where it blows in at a broken window, a little every second (a radiation suit keeps it off you); "
                        "it slowly eats the monsters away where its drops land and melts their corpses away; "
                        "in demos and netgames it is plain rain. Through a broken window the wind blows it in: rain soaks the floor "
                        "beneath, snow, sand and ash settle there, and the drafts carry them on through the rooms. Never on Phobos."),
                    choice("fall_amount","Amount",&s.fallAmount,{"Map's own","Light","Moderate","Heavy","Extreme"},{0,1,2,3,4},
                        "How hard it falls, or how thick it lies: a light shower to a downpour, a few flakes to a blizzard, a "
                        "haze of sand to a storm, a thin mist to a fog you can't see through. The heavier, the more the clouds hide "
                        "the sun. Map's own: as the map's DERPINFO or the Authored gameplay has it (a downpour, unless it says less).",
                        [](const Settings &d){return d.weather!=0;}),
                    choice("time_of_day","Time of day",&s.timeOfDay,{"Map's own","Dawn","Morning","Noon","Afternoon","Dusk","Night"},{0,1,2,3,4,5,6},
                        "Where the sun stands and how it shines, from where the sky paints it. Dawn and Dusk: low and orange, soft "
                        "shadows, a tinted sky. Noon: high and white, the brightest light and the hardest shadows. Night: no sun, the "
                        "outdoors dark and the lit rooms shining out through their windows. Map's own: as DERP derives it from the sky, "
                        "or as the map or the Authored gameplay has it. Re-bakes the light. Never on Phobos."),
                    fits(choice("temperature","Temperature",&s.temperature,{"Map's own","-30 C","-20 C","-10 C","0 C","10 C","20 C","30 C","40 C","50 C"},
                        {climateOwn,-30,-20,-10,0,10,20,30,40,50},
                        "How hot or cold it is outside. From 30 C the heat of the day shows: a stronger, warmer sun, glare round it, a "
                        "bleached sky, a warm haze in the distance and the far ground shimmering (less in a wind). Below 0 the windows "
                        "frost over even under a clear sky. Rain falls from 0 to 40 C, snow at 0 and below, sand from 10 C, fog from "
                        "-10 to 20 C, and it is no hotter than 30 C at dawn or night, 40 at dusk. Map's own: the weather's own."),temperatureFits),
                    fits(choice("wind","Wind",&s.wind,{"Map's own","Still","Breeze","Strong","Gale"},{0,1,2,3,4},
                        "How hard the wind blows: how flat rain and snow slant, how far weather blows in at broken windows and doors and "
                        "on through the rooms, where it drifts against the walls, how fast fog drifts. Still: everything falls straight "
                        "down and nothing blows in. A sandstorm needs wind; fog and dry ice lie only in still air or a breeze.",
                        hasWind),windFits),
                    choice("wind_from","Wind from",&s.windFrom,{"Map's own","North","North-east","East","South-east","South","South-west","West","North-west"},
                        {0,1,2,3,4,5,6,7,8},
                        "Where the wind comes from, by the automap's compass (north is up there). The weather blows in at the windows "
                        "facing it.",hasWind)}}}},
            {"Phobos",{
                {"Player",{
                    choice("phobos_player","Gear",&s.phobosPlayer,{"Legs and guns","Thrusters","Thrusters, grav boots"},{0,1,2},
                        "Legs and guns: you hop, kick off walls (Use) and jump (E); your guns' kick is your only other push. "
                        "Thrusters: run plus movement fires the suit's thrusters. Grav boots: walk on floors and ceilings, "
                        "E jumps the way you look onto the next one. G switches them off and on.",
                        [](const Settings &d){return hasPhobos(d)&&d.mode==2;}),
                    check("phobos_recoil","Gun recoil",&s.phobosRecoil,
                        "Every shot pushes you back with its real momentum: a pistol round about 3 newton-seconds, a shotgun "
                        "blast 16, the super shotgun 32. In the air your guns are thrusters. Off: they don't move you.",
                        [](const Settings &d){return hasPhobos(d)&&d.mode==2;}),
                    check("phobos_spin","Guns turn you",&s.phobosSpin,
                        "Each shot's kick also spins you back while nothing holds you, about 15 degrees a second per shotgun "
                        "blast. You twist against up to 30 degrees a second of it (10 while firing); long bursts flip you "
                        "over the top, upside down. A floor or ceiling rights you. Needs gun recoil.",
                        [](const Settings &d){return hasPhobos(d)&&d.mode==2&&d.phobosRecoil!=0;})}},
                {"Monsters",{
                    choice("phobos_enemies","Boots",&s.phobosEnemies,{"All wear mag boots","Zombies have none","Nobody has boots"},{0,1,2},
                        "All wear mag boots: every monster walks as in DOOM until a strong draft tears it loose. Zombies "
                        "have none: zombiemen, shotgun guys and chaingunners can't walk on Phobos; every step tumbles them "
                        "and their own shots push them. Nobody has boots: every walking monster tumbles.",
                        [](const Settings &d){return hasPhobos(d)&&d.mode==2;})}},
                {"Air",{
                    choice("phobos_air","Decompression",&s.phobosAir,{"Off","Gentle","Real"},{0,1,2},
                        "Real: a broken window or a door to the outside empties the room at the speed of sound and carries "
                        "everything loose with it, you included. Gentle: a quarter of the push; things drift toward the "
                        "opening. Off: the air still rushes out, but pushes nothing.",
                        [](const Settings &d){return hasPhobos(d)&&d.mode==2;})}}}},
            {"Controls",{
                {"Movement",{
                    // The key from when it was a switch: old configs' 0 and 1 still read.
                    choice("classic_movement","Movement",&s.movement,{"Normal","Normal plus","Classic"},{0,2,1},
                        "Normal: you start and stop sooner, and diagonals go no faster than straight ahead. Normal plus: the same, "
                        "but hold a diagonal while running and over about half a second you build up to DOOM's faster "
                        "strafe-running. Classic: DOOM's own movement; you speed up and slow down gradually, slide a little "
                        "when you let go, and move faster diagonally at once. Demos and netgames always move as in DOOM.",
                        hasTunedMovement)}},
                {"View",{
                    check("look","Mouse look",&s.look,"Mouse look tilts the camera, and your bullets go where you look."),
                    check("invert_look","Invert mouse look",&s.invertLook,"Moving the mouse forward tilts the view down, like a flight stick.",
                        [](const Settings &d){return d.look!=0;}),
                    check("classic_aim","Classic aim",&s.classicAim,
                        "Bullets aim themselves at a monster in front of you, as in DOOM, and hit it in the middle; with none in front "
                        "they follow the view. Off: they go exactly where you look, so you can miss, and where you hit is where the "
                        "wound and the fall come from. Rockets, plasma and the BFG aim as in DOOM either way.",hasFreeAim),
                    check("crosshair","Crosshair",&s.crosshair,"A small crosshair in the middle of the view.")}},
                {"Saving",{
                    check("quick_confirm","Confirm quicksave and quickload",&s.quickConfirm,
                        "Ask before a quicksave overwrites its slot or a quickload replaces the game in progress. Off: the keys act at once.")}}}}}},
        {"Graphics",{
            {"Display",{
                {"Renderer",{
                    choice("accelerated","Renderer",&s.accelerated,{"Accelerated 3D","Classic software"},{1,0},
                        "The 3D renderer draws the world at your window's pixel resolution; classic is the original 320x200 view."),
                    choice("window_size","Window size",&s.windowSize,{"Free","1280x720","1600x900","1920x1080","2560x1440","3840x2160"},{0,720,900,1080,1440,2160},
                        "The window itself made exactly this many pixels, for recordings and screenshots at a standard size; "
                        "the picture inside it still draws at the Resolution below, so this moves the window's edges and nothing "
                        "else. Picking a size leaves fullscreen (F11 goes back) and the window keeps that size until Free is "
                        "picked, when it can be dragged to any size again. -width and -height on the command line win for a session."),
                    choice("scale","Resolution",&s.scale,{"Native (100%)","75%","50%","1080p","720p","480p","360p","240p"},
                        {100,75,50,1080,720,480,360,240},
                        "Lower resolutions lighten the GPU workload. Percentages follow the window; the fixed heights "
                        "keep the same number of rows at any window size (never more than the window's own), the width "
                        "following its shape. Halves and thirds of the window's height upscale most evenly."),
                    choice("upscale","Upscaling",&s.upscale,{"Smooth (bilinear)","Crisp pixels","Sharp bilinear"},{0,1,2},
                        "How a lower resolution is stretched to the window. Smooth blurs it; crisp keeps square pixels, "
                        "a little uneven in width unless the window is a whole multiple; sharp bilinear keeps them square "
                        "and blends only the one screen pixel between them.",
                        [](const Settings &d){return d.scale!=100;}),
                    slider("msaa","Anti-aliasing (MSAA)",&s.msaa,0,std::max(1.0,std::log2((double)gpuMaxSamples())),1,
                        "Smooths the stair-steps along the edges of walls, floors and ledges. Texels, sprites and bars stay crisp. "
                        "Each step costs more GPU time.",
                        [](const Settings &){return gpuMaxSamples()>1;},{"Off","2x","4x","8x"}),
                    slider("fov","Field of view",&s.fov,60,120,5,"Measured against the classic 4:3 view."),
                    check("widescreen","Widescreen",&s.widescreen,"Show more world at the sides rather than stretching it."),
                    choice("fps","Show FPS",&s.fps,{"Off","In the window title","FPS","Full"},{0,1,2,3},
                        "Window title: frames per second and the GPU driver up there. FPS: the frame rate and frame times in the "
                        "top left corner. Full: also a graph of the last frames, where each frame's time goes, the CPU load, how "
                        "frames reach the screen, the GPU, the version and the map, and the systems at work with what they cost, "
                        "so a screenshot (F7) says what a report needs. The key left of 1 steps through these while you play."),
                    choice("presentation","Frame presentation",&s.presentation,{"VSync","VSync, 3 frames","Fast","Adaptive","Off"},{0,1,2,3,4},
                        "How frames reach the screen. VSync: in step with the display, but a frame that runs over waits for "
                        "the next refresh, so 60 drops to 30. VSync, 3 frames: one more frame queued, so running over costs "
                        "one refresh instead of half the rate; a frame more of delay. Fast: drawn as fast as the GPU goes, the "
                        "newest finished frame shown at each refresh; no tearing, no halving (VSync, 3 frames where the driver "
                        "can't). Adaptive: VSync while the game keeps up, uncapped while it keeps running over, so it tears "
                        "instead of halving. Off: uncapped, lowest delay, tearing; with G-Sync or FreeSync and a frame limit "
                        "just under the display's rate, neither."),
                    choice("frame_limit","Frame limit",&s.frameLimit,{"None","Display rate","30","60","120","144","240"},{0,1,2,3,4,5,6},
                        "Holds Fast, Off, and Adaptive while it runs uncapped, to a steady rate: less heat and fan noise, and "
                        "an even pace. VSync holds the rate itself.",
                        [](const Settings &d){return d.presentation>=2;})}},
                {"Retro look",{
                    check("retro","Retro scanlines",&s.retro,"Dark lines between the rows of the picture, like an old CRT monitor."),
                    check("palette","Doom palette colors",&s.palette,"Quantize the final lighting and fog to the current WAD palette.")}}}},
            {"Textures",{
                {"Filtering",{
                    choice("filter","Walls and floors",&s.filter,{"Crisp pixels","Smooth (bilinear)","Sharp bilinear"},{0,1,2},
                        "Sharp bilinear keeps every texel flat and crisp and only softens the step between texels, "
                        "by about a screen pixel plus the edge softness below."),
                    slider("sharp_softness","Edge softness",&s.sharpSoftness,0,1,0.05,
                        "With sharp bilinear walls and floors: how far texel edges blend, in texels. Left keeps only a one-pixel edge; right approaches bilinear.",
                        [](const Settings &d){return d.filter==2;}),
                    check("palette_mipmaps","Palette mipmaps",&s.paletteMips,
                        "With smooth or sharp walls and floors: far and grazing surfaces read smaller versions snapped to the WAD palette, "
                        "as software Quake did, instead of shimmering. Crisp pixels stay vanilla.",
                        [](const Settings &d){return d.filter!=0;}),
                    choice("sprite_filter","Sprites",&s.spriteFilter,{"Crisp pixels","Soft (bilinear)"},{0,1},
                        "Crisp keeps the artwork's pixels square; soft blends them smoothly.")}},
                {"Surface detail",{
                    check("surface_detail","Bump detail and metal gloss",&s.detail,
                        "Bump detail and metal gloss derived from the WAD artwork; affects dynamic lights, and static ones with bake-only lights."),
                    check("varied_highlights","Varied highlights",&s.variedHighlights,
                        "Gloss highlights are tight and bright where the artwork runs smooth and broad and dim where it is busy, "
                        "with slime and blue panels sharper than worn grey metal.",
                        [](const Settings &d){return d.detail!=0;}),
                    check("metal_shading","Metal shading",&s.metalShading,
                        "Highlights on bare steel and brass take the metal's own colour: gold on brass, blue-grey on steel. "
                        "Paint, rust and grime keep white ones.",
                        [](const Settings &d){return d.detail!=0;}),
                    check("uneven_tiles","Uneven floor tiles",&s.unevenTiles,
                        "Tiles, plates and stones painted into floors each lie a little crooked, as if laid by hand: "
                        "lights catch some and miss others, and a thin lip lights up or falls into shadow where neighbors meet. Found from the artwork's seams at load.",
                        [](const Settings &d){return d.detail!=0;}),
                    check("uneven_walls","Uneven wall and ceiling tiles",&s.unevenWalls,
                        "The same for the plates, panels and bricks painted into walls and ceilings; walls split only into straight rows and joints.",
                        [](const Settings &d){return d.detail!=0;}),
                    check("weapon_lighting","Relit weapon",&s.weaponLighting,
                        "The sheen and light painted into the weapon artwork are taken out, and the lights around you relight it: "
                        "their color, darker rounded edges, glowing rims against lights behind it, and highlights that slide over "
                        "the metal as you look around and the weapon bobs."),
                    check("window_glass","Window glass",&s.windowGlass,
                        "Windows found in the map's geometry get a pane of glass: grimy at the edges, streaked with highlights that "
                        "slide as you move, reflecting the sky from outside. The first shot, projectile or blast through it shatters "
                        "it into large, reflective shards that tumble and bounce; shots and sight pass exactly as in vanilla.",hasGlass),
                    check("glass_reflections","Glass reflections",&s.glassReflections,
                        "Experimental: the window filling most of the view mirrors the room in front of it, monsters and shots "
                        "included, drawn a second time like the liquid reflections and at their resolution. Costs about as much as they do.",
                        [](const Settings &d){return d.windowGlass!=0;})}},
                {"Glow",{
                    check("emissive","Emissive textures",&s.emissive,
                        "Lamp strips, computer displays, lava and nukage glow with soft bloom and colored light on nearby surfaces."),
                    check("bloom","Bloom",&s.bloom,
                        "With emissive textures: glowing texels, monster eyes and pickups bleed a soft halo. Off keeps their "
                        "glow and the light they cast, without the halo, and skips the half-resolution glow pass.",
                        [](const Settings &d){return d.emissive!=0;}),
                    check("glossy_screens","Glossy monitor screens",&s.glossyScreens,
                        "Screens found in computer textures get curved CRT glass: the picture bends and shifts behind it as you move, "
                        "and it catches sharp highlights from lamps, shots and the flashlight."),
                    check("live_readouts","Live readouts",&s.liveReadouts,
                        "The blocks of glowing cells painted into computer panels (the green grids beside DOOM's consoles) "
                        "run a simple blink loop like old console lamps: whole lamps snap on and off together, "
                        "all out, back in stages, all on, thinning out again."),
                    check("glowing_eyes","Glowing eyes",&s.glowingEyes,
                        "The eyes painted into monster artwork glow in the dark: the zombies' and imp's red, the demon's yellow, "
                        "the cacodemon's and mancubus's green, the arachnotron's blue. Picked by hand for the stock monsters, searched for in other artwork; "
                        "their halo needs bloom.")}}}},
            {"Lighting",{
                {"Baked at level load",{
                    check("baked_lights","Baked static lights",&s.bakedLights,
                        "Torches, lamps and glowing textures light the whole level, baked at level load; nearby ones still flicker."),
                    check("window_light","Window light",&s.windowLight,
                        "Windows, open doorways and holes in the ceiling let the outdoor light in: they light the rooms beside them "
                        "like big soft lamps in the sky's color, fading deeper into the room. Baked at level load.",
                        [](const Settings &d){return d.bakedLights!=0;}),
                    slider("window_strength","Window light strength",&s.windowStrength,0.5,2,0.25,
                        "How brightly the outdoors shines in through windows and openings. Changing it re-bakes the lights.",
                        [](const Settings &d){return d.bakedLights!=0&&d.windowLight!=0;}),
                    check("dim_lit_rooms","Darker rooms around lamps and windows",&s.dimLitRooms,
                        "Rooms that lamps, screens and windows light well lose part of their even light level, so the light "
                        "visibly comes from them and the far corners fall darker. Baked at level load.",
                        [](const Settings &d){return d.bakedLights!=0;}),
                    slider("unlit_darkening","Darker rooms without lamps",&s.unlitDarkening,0,0.5,0.05,
                        "Indoor rooms nothing visibly lights lose this much of their even light level too, so they don't look "
                        "brighter and flatter than the lamp-lit rooms around them.",
                        [](const Settings &d){return d.bakedLights!=0&&d.dimLitRooms!=0;},
                        {"Off","5%","10%","15%","20%","25%","30%","35%","40%","45%","50%"}),
                    check("sun_shadows","Sun shadows",&s.sun,"Shadows and sunlit patches baked at level load from the sky texture and map geometry."),
                    check("bounce_light","Bounce light",&s.bounce,
                        "Sunlit and lamp-lit surfaces tint and lift what faces them, including ceilings; baked at level load."),
                    slider("bounce_tint","Room color from bounce",&s.bounceTint,0,4,0.5,
                        "Light from colored lamps, bounced around the room, tints its even light level too, so corners "
                        "the lamps never reach directly still take their color.",
                        [](const Settings &d){return d.bakedLights!=0&&d.bounce!=0;}),
                    check("sky_light","Sky light",&s.skyLight,
                        "A cool fill from the visible sky, tinted by the sky texture: windows glow faintly, overhangs and alcoves outdoors stay darker."),
                    check("baked_occlusion","Ambient occlusion",&s.bakedAO,"Corners, ledges, stair risers and alcoves darken in light-level steps."),
                    check("decoration_shadows","Decoration shadows",&s.thingShadows,
                        "Columns, trees and hanging bodies cast baked sun and lamp shadows shaped like their sprites. Changing it re-bakes the level."),
                    check("texel_lighting","Texel-aligned baked light",&s.texelLight,
                        "Baked lights, sun patches and bounce blend smoothly but step per texture pixel, instead of in hard 4-5 unit map cells.")}},
                {"Live light",{
                    check("soft_light","Light seams and contact shading",&s.softLight,"Blend sector light across openings and darken floors along walls."),
                    check("light_flow","Sector light flow",&s.lightFlow,
                        "Brighter rooms light the floors and walls beyond their openings; flickering rooms flicker faintly next door."),
                    check("door_light","Door light spill",&s.doorLight,"Light from a brighter room spills through a door as it opens."),
                    check("moving_relight","Moving doors re-light",&s.movingRelight,
                        "Baked lamp light and sunlight follow doors, lifts and lowering walls: as a door rises, the light beyond it "
                        "steps in under the edge, re-baked around it in the background.",
                        [](const Settings &d){return d.bakedLights!=0||d.sun!=0;}),
                    check("eye_adaptation","Eye adaptation",&s.eyeAdaptation,"Brief stepped over- or underexposure when you move between bright and dark areas.")}},
                {"Flashlight and shadows",{
                    slider("flashlight_tint","Flashlight tint",&s.flashlightTintGain,0,2,0.5,"Subtle target-color tint: left is off, middle is the default, right is stronger.",hasFlashlight),
                    check("classic_flashlight","Classic flashlight",&s.classicFlashlight,
                        "A shorter, warmer beam that works like Doom's own light levels: it brightens what it hits in "
                        "hard steps to a little past full brightness and no further, so monsters keep their palette colors.",hasFlashlight),
                    check("stippled_shadows","Stippled shadow edges",&s.stippledShadows,
                        "The shadows walls, ledges and monsters throw in the flashlight stay hard but break into a coarse dither at the edge: "
                        "a pixel where the wall meets its shadow, wider the farther the shadow falls behind it.",hasFlashlight),
                    check("soft_shot_shadows","Soft shot shadows",&s.softShotShadows,
                        "Muzzle flashes, projectiles and exploding barrels shine from a small body instead of a point: the shadows they "
                        "throw step with the artwork's pixels and soften at the edge into a coarse dither, like the baked light's. "
                        "Off: a sharp edge at screen resolution."),
                    check("flashlight_shadows","Flashlight silhouettes",&s.flashlightShadows,"Things caught in the flashlight throw their sprite outline onto the wall behind them.",hasFlashlight),
                    check("player_shadow","Player shadow",&s.playerShadow,"Your own sprite's shadow from the sun or the strongest light above you.")}}}},
            {"Atmosphere",{
                {"Air",{
                    check("fog","Atmospheric fog",&s.fog,"Height fog and soft cloud layers derived from pits, liquids and damaging floors."),
                    check("sun_shafts","Sunbeams",&s.sunShafts,
                        "Soft shafts of sunlight slant down through ceiling holes and windows, where the dust motes glint. Needs sun shadows.",
                        [](const Settings &d){return d.sun!=0;}),
                    check("sun_scatter","Sunbeam scattering",&s.sunScatter,
                        "Sunbeams glow brighter when you look toward the sun through them and fainter from behind, as dust scatters light onward.",
                        [](const Settings &d){return d.sun!=0&&d.sunShafts!=0;}),
                    check("sun_disc","Sun in the sky",&s.sunDisc,"A faint sun disc and soft glow where the baked sunlight comes from, over the sky texture's brightest part."),
                    check("dust_motes","Dust motes",&s.dust,"Specks that glint in indoor sunbeams and the flashlight beam."),
                    check("embers","Embers",&s.embers,"A few sparks rise from torches and fires and cool as they climb; where the game's wind blows, it carries them off."),
                    check("heat_haze","Heat haze",&s.heatHaze,"Rows of pixels shimmer above lava, hot damaging floors and flames.")}},
                {"Wear",{
                    check("worn_plates","Worn plates",&s.wornPlates,
                        "Plain metal plates show years of use: grime packed into their seams, dull blotches, boot scuffs low down, "
                        "and a duller sheen. Only on plates that are otherwise blank, such as the fronts of DOOM's computer desks."),
                    check("window_streaks","Window streaks",&s.windowStreaks,
                        "Rain has run down the outside walls below windows for years: dark ragged streaks from the sill, rusty on "
                        "metal. Inside they start once rain blows in through a broken pane. Needs window glass.",hasGlass)}},
                {"Liquids",{
                    check("reflections","Reflections",&s.reflections,
                        "Mirror the scene in nearby water; renders the view a second time at half resolution."),
                    check("retro_reflections","Retro reflections (PS1 style)",&s.retroReflections,
                        "Eighth resolution, hard pixels, Doom palette colors and a stepped wobble.",
                        [](const Settings &d){return d.reflections!=0;}),
                    check("caustics","Caustics",&s.caustics,
                        "Rippling light from water, nukage, slime, blood and lava on the walls around them; pattern taken from the level's liquid flat."),
                    check("ceiling_caustics","Caustics on ceilings",&s.ceilingCaustics,"The ripples also play on the ceiling above water, nukage, slime and lava.",
                        [](const Settings &d){return d.caustics!=0;}),
                    check("caustics_computed","Computed caustics",&s.causticsComputed,
                        "The light the liquid's waves actually gather, worked out from its flat at level load: soft near the water, "
                        "sharp lines about 40 units up, broader above. Off shows the flat's own bright streaks.",
                        [](const Settings &d){return d.caustics!=0;}),
                    check("caustics_grow","Caustics grow with distance",&s.causticsGrow,
                        "The shapes widen from 2 to 4 units per pixel the farther the light travels from the water; off keeps one unit per pixel like the floor.",
                        [](const Settings &d){return d.caustics!=0;}),
                    check("caustics_angle","Flashlight reflection angle",&s.causticsAngle,
                        "Water reflects more of the beam the flatter it strikes: aiming across a pool lights the walls, aiming straight down barely does.",
                        [](const Settings &d){return d.caustics!=0;}),
                    check("caustics_sway","Caustic sway",&s.causticsSway,
                        "The pattern sways with the wave slope, more the higher up the wall, like light dancing under a bridge.",
                        [](const Settings &d){return d.caustics!=0;}),
                    check("caustics_sprites","Caustics on monsters and things",&s.causticsSprites,
                        "Monsters, items and decorations near water, nukage, slime and lava catch the ripples too, in their own colors.",
                        [](const Settings &d){return d.caustics!=0;}),
                    check("caustics_shots","Caustics from shots",&s.causticsShots,
                        "Muzzle flashes and projectiles over liquids throw brief ripples on the walls around them, like the flashlight. Subtle next to their direct light.",
                        [](const Settings &d){return d.caustics!=0;}),
                    check("damp_shores","Damp shores",&s.dampShores,
                        "Walls and banks just above water, nukage, slime and blood turn darker and damp up to a ragged line, "
                        "a little stained by the liquid. Lava stays dry."),
                    check("glow_edges","Glowing liquid edges",&s.glowEdges,
                        "Walls running straight into nukage, slime or lava take the glowing strip the game paints on its own edge textures, "
                        "like the NUKE24 steps, instead of only the walls a mapper gave that texture."),
                    check("shore_ripples","Shore ripples",&s.shoreRipples,
                        "Liquids ripple where they meet walls, like the splash rings running along the shore: quick thin lines on water, "
                        "slow broad ones on nukage and slime, slow glowing ones on lava, a still dark line on blood."),
                    check("splashes","Splashes",&s.splashes,"Ripple rings where things land, wade or are shot in liquids, and spouts where shots and projectiles hit them. "
                        "Things standing in liquids sink in; nukage, slime, blood and lava splash thick and bubble.")}}}},
            {"Combat",{
                {"Blood",{
                    choice("blood_level","Blood",&s.bloodLevel,{"Off","Splats","Full"},{0,1,2},
                        "Splats: hitscan hits leave splats on floors and nearby walls, and corpses grow a pool, in the WAD's blood colors. "
                        "Full adds the spray: hits throw blood out the far side of the body and kills burst, the drops flying as pixels "
                        "and landing as splats on floors, walls and low ceilings; fresh wounds drip, sliding bodies smear it, bodies "
                        "thrown into a wall splash it; and fresh blood glints under lights, drying matte."),
                    check("monster_blood","Enemy-appropriate blood",&s.monsterBlood,
                        "Each monster bleeds its own colour: Cacodemons blue, Barons of Hell and Hell Knights green, the rest red. "
                        "Lost Souls are bone and fire and leave no blood at all. Splats, spray, pools, wound rims and the game's own "
                        "blood all follow. Drawing only.",hasMonsterBlood),
                    check("wounds","Wound holes",&s.wounds,
                        "Bullets and pellets leave holes with ragged bloody edges, dark torn rims and short blood smears. "
                        "The marks follow the monster's animation. Close hits widen a hole; seen from the side it closes. "
                        "Drawing only: the monster is hit as always.")}},
                {"Deaths",{
                    choice("deaths","Deaths",&s.deaths,{"Classic","Varied","Procedural falls","Ragdolls","Ragdolls, Hollywood"},{0,1,2,3,4},
                        "Classic: the game's own death frames. Varied: no two monsters die alike, the frames retimed (a snap, a hesitation, "
                        "a slump), turned off the middle or stepped back by the push. Procedural falls: monsters on two legs fall a way "
                        "their drawings never show, on joints placed by hand or found in the art, knocked down by the hit. Ragdolls: "
                        "their drawing gets a body as deep as its side view on a small skeleton the killing blow throws (back, legs "
                        "swept, spun, strings cut by a head shot), the zombieman's chest blasts sometimes yanked as on a stunt wire. "
                        "Hollywood: shots punch harder and spin the body as struck, and explosions send it flying off walls and "
                        "columns, gibs included; each thrown body keeps track of twice the floor around it, so big blasts cost more "
                        "CPU, and the Arch-vile raises the game's corpse where the monster died, not where its body landed. Drawing only."),
                    check("hit_reactions","Hit reactions",&s.hitReactions,
                        "Monsters lean with the push of every hit and spring back, and dying ones jolt with the killing blow over the "
                        "game's own death frames: thrown back by high hits, legs knocked out by low ones, crumpling under the chainsaw. "
                        "Drawing only: the game sees no difference."),
                    slider("body_motion_strength","Reaction strength",&s.bodyMotionStrength,0.5,3,0.25,
                        "How far hit reactions throw, bend and buckle the body.",
                        [](const Settings &d){return d.hitReactions!=0;})}},
                {"Shots",{
                    check("soft_effects","Soft effect sprites",&s.softSprites,"Explosions, puffs and teleport fog stop slicing into walls and dissolve on a dither at floors."),
                    check("shootable_lights","Shootable lights",&s.shootableLights,
                        "Computer panels and tech walls throw blue sparks when shot. A shot or fireball into the glowing part "
                        "of a lamp or screen breaks its whole fixture, and rockets and barrels break those around them: they sputter out "
                        "and go dark, and the room darkens with them a moment later. Render-only: monsters see as before, and the lights "
                        "are back after a reload.",
                        [](const Settings &d){return d.emissive!=0&&Game_HasEffect(GAME_EFFECT_SHOOTABLE_LIGHTS)!=0;}),
                    check("kill_shots_carry","Kill shots carry on",&s.killShotsCarry,
                        "A bullet or pellet that kills what it hits carries on past the body, so a fight can shoot out the lamps and "
                        "screens behind the monsters. Only for sparks and lights, never damage: the game sees the shot stop as always.",
                        [](const Settings &d){return d.emissive!=0&&d.shootableLights!=0;})}}}},
            {"Performance",{
                {"Cheaper light work",{
                    check("bake_only_lights","Bake-only static lights",&s.bakeOnlyLights,
                        "Torches, lamps and glowing textures stop costing per-frame light work: their flicker, bump detail and gloss come from the bake, "
                        "and lava and nukage pools glow as one light each. Shots, explosions and the flashlight stay dynamic. Changing it re-bakes the level.",
                        [](const Settings &d){return d.bakedLights!=0;}),
                    check("grid_sprite_light","Grid lighting for monsters",&s.gridSpriteLight,
                        "Monsters and items take static light, and the light their shadow falls from, from a grid baked at level load "
                        "instead of tracing rays every frame.",
                        [](const Settings &d){return d.bakedLights!=0;}),
                    check("unoccluded_surface_lights","Unblocked glowing-texture light",&s.unoccludedSurfaceLights,
                        "Light from lamp strips and glowing flats skips the wall tests per pixel. Cheaper near light panels, "
                        "but their highlights can show on shiny surfaces through walls.",
                        [](const Settings &d){return d.emissive!=0&&d.bakeOnlyLights==0;}),
                    check("fewer_surface_lights","Fewer glowing-texture lights",&s.fewerSurfaceLights,
                        "Only the 8 nearest lamp strips and glowing flats light their surroundings per pixel, not 24. Cheaper in rooms "
                        "full of light panels; farther ones keep their baked glow but lose their moving highlights.",
                        [](const Settings &d){return d.emissive!=0&&d.bakeOnlyLights==0;})}},
                {"Less geometry",{
                    check("skip_hidden_geometry","Skip hidden walls and floors",&s.skipHiddenGeometry,
                        "Walls, floors and ceilings the view can't reach are left out each frame, found the way DOOM found them: "
                        "front to back through the map, solid walls and closed doors hiding what lies behind. Fewer triangles for "
                        "older graphics chips; a wall can pop in at the screen's edge where the test misses it. Monsters, items "
                        "and the weather are drawn as before.")}}}}}},
        {"Sound",{
            {"Music",{
                {"Music",{
                    choice("music_device","Music",&s.musicDevice,
    #ifdef __APPLE__
                        {"SoundFont","OPL (FM)","Mac synthesizer"},{0,1,2},
    #else
                        {"SoundFont","OPL (FM)"},{0,1},
    #endif
                        "SoundFont plays sampled General MIDI instruments, close to the Roland Sound Canvas the music was written on. "
                        "OPL is the FM synthesis of DOS sound cards, with DOOM's own instrument set. "
                        "A .sf2 bank put in the soundfonts folder beside doom.cfg replaces the bundled one.")}},
                {"Effects",{
                    choice("sfx_resampling","Sample playback",&s.sfxResampling,{"Original","Smooth","Clean"},{0,1,2},
                        "DOOM's effects are 11 kHz samples. Original holds each sample, raw and gritty, with a bright sizzle on top. "
                        "Smooth blends between samples and keeps some edge. Clean filters the sizzle out entirely."),
                    slider("sfx_treble","Made-up treble",&s.sfxTreble,0,2,0.25,
                        "With Smooth or Clean, treble the samples never had is made up from their own top end as they play: "
                        "overtones for tones, finer hiss for shots and blasts, without the metallic ring of held samples. "
                        "At 100% about as bright as Original.",
                        [](const Settings &d){return d.sfxResampling!=0;},
                        {"Off","25%","50%","75%","100%","125%","150%","175%","200%"})}}}},
            {"Made-up sounds",{
                {"Made-up sounds",{
                    check("made_impacts","Sparks, dying lights and glass",&s.madeImpacts,
                        "Sounds DOOM never had, made at startup in its own 8-bit style: machinery crackles where it is shot, "
                        "a lamp sputters out or a screen dies in static, and windows break."),
                    choice("glass_sound","Breaking glass",&s.glassSound,{"Shatter","Fracture"},{0,1},
                        "Shatter: a hard crack, pieces rattling as the pane comes apart, a rush of splinters and the shards falling. "
                        "Fracture: a crack front branching through the pane, hundreds of tiny cracks ringing the pieces they leave.",
                        [](const Settings &d){return d.madeImpacts!=0;}),
                    check("footsteps","Footsteps and splashes",&s.footsteps,
                        "Your steps sound by what the floor is: stone, metal, wood, grass and carpet. In water you splash, "
                        "in nukage, slime, blood and lava it gloops, and monsters wading through liquids splash too. "
                        "Monsters on dry floors stay as silent as ever."),
                    check("ambience","Ambience",&s.ambience,
                        "Lamps, door lights and computers hum, nukage and blood bubble, lava rumbles, fire crackles, water drips and wind blows "
                        "under the sky, heard dull through window glass until it is shot, where the map shows them. Only near ones are heard; "
                        "steady lamps barely, flickering ones buzz with their light, and shot-out fixtures go quiet. "
                        "Loud effects push it down for a moment."),
                    check("alternative_computer_sounds","Alternative computer sounds",&s.alternativeComputerSounds,
                        "Computers use a narrow machine hum, hard-drive seeks and relay chatter as an alternative to their original bleeps.",
                        [](const Settings &d){return d.ambience!=0;}),
                    slider("ambience_amount","Ambience volume",&s.ambienceAmount,0.25,2,0.25,
                        "How loud the ambience is next to the effects.",
                        [](const Settings &d){return d.ambience!=0;},
                        {"25%","50%","75%","100%","125%","150%","175%","200%"})}}}},
            {"Hearing",{
                {"Rooms",{
                    check("room_acoustics","Room acoustics",&s.roomAcoustics,
                        "Sounds echo off the walls around you and ring on in a tail measured from the room: corridors slap back, "
                        "big metal halls boom, wooden rooms stay dull and the outdoors almost dry."),
                    slider("reverb_amount","Echo and reverb",&s.reverbAmount,0.25,2,0.25,
                        "How loud the echoes and the tail are next to the sound itself.",
                        [](const Settings &d){return d.roomAcoustics!=0;},
                        {"25%","50%","75%","100%","125%","150%","175%","200%"})}},
                {"Hearing",{
                    check("muffle_walls","Muffled behind walls",&s.muffleWalls,
                        "Sounds you can't see come through muffled, the more the longer their way round through the map's openings, "
                        "more through closed doors and window glass, most through solid walls. Walls take their treble, not their loudness."),
                    check("muffle_distance","Duller with distance",&s.muffleDistance,
                        "Far sounds lose their treble, the way they do through air."),
                    check("natural_falloff","Natural distance falloff",&s.naturalFalloff,
                        "Far sounds fade the way they do in air, losing half their strength each time the distance doubles, "
                        "so a monster across the room sounds across the room. Gunfire and blasts carry farther, faint and dull. "
                        "Off: DOOM's straight fade, still loud at mid range.",hasFalloff)}}}}}}};
    // A game without Phobos (Heretic) shows no Phobos page.
    if(!Game_HasEffect(GAME_EFFECT_PHOBOS))groups[0].tabs.erase(groups[0].tabs.begin()+1);
    return groups;
}

namespace {
template<class Visit> void eachOption(Settings &s,Visit visit) {
    for(OptionGroup &group:optionGroups(s))for(OptionTab &tab:group.tabs)
        for(OptionSection &section:tab.sections)for(Option &option:section.options)visit(option);
}
}
// graphics.cfg from before the combined Combat and Gameplay choices: the old switches' values, until
// finishReading turns them into the choices a file without those keys still lacks.
static double oldDeath[4]={1,1,0,0},oldHit[2]={1,1},oldBlood[3]={1,1,1},oldMode[2]={1,0};   // varied, falls, ragdoll, blast; motion, flinch; blood, spray, shine; authored, phobos
static bool readOld[4],readNew[4];
void readOption(Settings &s,const char *key,double value) {
    static const char *const olds[][4]={{"varied_deaths","death_falls","death_ragdoll","death_blast"},{"death_motion","hit_flinch"},{"blood","blood_spray","blood_shine"},
                                        {"authored_episodes","phobos"}};
    double *into[]={oldDeath,oldHit,oldBlood,oldMode};
    static const char *const news[]={"deaths","hit_reactions","blood_level","mode"};
    for(int g=0;g<4;++g) {
        for(int k=0;k<4&&olds[g][k];++k)if(!strcmp(key,olds[g][k])) {into[g][k]=value;readOld[g]=true;}
        if(!strcmp(key,news[g]))readNew[g]=true;
    }
    eachOption(s,[&](Option &option) {
        if(strcmp(option.key,key))return;
        if(option.kind==OptionKind::Check)*option.flag=value!=0;
        else if(option.kind==OptionKind::Choice) {for(int v:option.values)if(v==value)*option.flag=v;}
        else *option.number=option.snap(value);
    });
}
void finishReading(Settings &s) {
    if(readOld[0]&&!readNew[0])s.deaths=!oldDeath[0]?0:oldDeath[2]?(oldDeath[3]?4:3):oldDeath[1]?2:1;
    if(readOld[1]&&!readNew[1])s.hitReactions=oldHit[0]||oldHit[1];
    if(readOld[2]&&!readNew[2])s.bloodLevel=!oldBlood[0]?0:oldBlood[1]||oldBlood[2]?2:1;
    if(readOld[3]&&!readNew[3])s.mode=oldMode[1]?2:oldMode[0]?1:0;
    for(int g=0;g<4;++g)readOld[g]=readNew[g]=false;
}
void writeOptions(FILE *file,Settings &s) {
    eachOption(s,[&](Option &option) {
        if(option.kind==OptionKind::Slider)fprintf(file,"%s %g\n",option.key,*option.number);
        else fprintf(file,"%s %d\n",option.key,*option.flag);
    });
}
}
