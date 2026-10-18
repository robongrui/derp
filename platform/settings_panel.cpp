/* The F4 graphics options; see settings_panel.h. */
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
bool hasFlashlight(const Settings&) {return Game_HasEffect(GAME_EFFECT_FLASHLIGHT)!=0;}
bool hasFalloff(const Settings&) {return Game_HasEffect(GAME_EFFECT_NATURAL_FALLOFF)!=0;}
bool hasFreeAim(const Settings &s) {return s.look&&Game_HasEffect(GAME_EFFECT_FREE_AIM)!=0;}
}

void fitGame(Settings &s) {
    if(!Game_HasEffect(GAME_EFFECT_WINDOW_GLASS))s.windowGlass=0;
    if(!Game_HasEffect(GAME_EFFECT_SHOOTABLE_LIGHTS))s.shootableLights=0;
    if(!Game_HasEffect(GAME_EFFECT_NATURAL_FALLOFF))s.naturalFalloff=0;
}

std::vector<OptionTab> optionTabs(Settings &s) {
    return {
        {"Display",{
            {"Renderer",{
                choice("accelerated","Renderer",&s.accelerated,{"Accelerated 3D","Classic software"},{1,0},
                    "The 3D renderer draws the world at your window's pixel resolution; classic is the original 320x200 view."),
                choice("scale","Resolution",&s.scale,{"Native (100%)","75%","50%"},{100,75,50},
                    "Lower resolutions lighten the GPU workload."),
                slider("msaa","Anti-aliasing (MSAA)",&s.msaa,0,std::max(1.0,std::log2((double)gpuMaxSamples())),1,
                    "Smooths the stair-steps along the edges of walls, floors and ledges. Texels, sprites and bars stay crisp. "
                    "Each step costs more GPU time.",
                    [](const Settings &){return gpuMaxSamples()>1;},{"Off","2x","4x","8x"}),
                slider("fov","Field of view",&s.fov,60,120,5,"Measured against the classic 4:3 view."),
                check("widescreen","Widescreen",&s.widescreen,"Show more world at the sides rather than stretching it.")}},
            {"View",{
                check("look","Mouse look",&s.look,"Mouse look tilts the camera, and your bullets go where you look."),
                check("classic_aim","Classic aim",&s.classicAim,
                    "Bullets aim themselves at a monster in front of you, as in DOOM, and hit it in the middle; with none in front "
                    "they follow the view. Off: they go exactly where you look, so you can miss, and where you hit is where the "
                    "wound and the fall come from. Rockets, plasma and the BFG aim as in DOOM either way.",hasFreeAim),
                check("crosshair","Crosshair",&s.crosshair,"A small crosshair in the middle of the view."),
                check("fps","Show FPS in window title",&s.fps,"Frames per second and the GPU driver in the window title.")}},
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
                check("glossy_screens","Glossy monitor screens",&s.glossyScreens,
                    "Screens found in computer textures get curved CRT glass: the picture bends and shifts behind it as you move, "
                    "and it catches sharp highlights from lamps, shots and the flashlight."),
                check("glowing_eyes","Glowing eyes",&s.glowingEyes,
                    "The eyes painted into monster artwork glow in the dark: the zombies' and imp's red, the demon's yellow, "
                    "the cacodemon's and mancubus's green, the arachnotron's blue. Picked by hand for the stock monsters, searched for in other artwork; "
                    "bloom needs emissive textures.")}}}},
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
            {"Combat",{
                check("blood","Blood splats and pools",&s.blood,
                    "Hitscan hits leave splats on floors and nearby walls; corpses grow a pool. Colors come from the WAD's blood sprite."),
                check("blood_shine","Wet blood shine",&s.bloodShine,"Fresh floor blood and wound edges glint under lights. Floor blood dries matte over time.",
                    [](const Settings &d){return d.blood!=0||d.wounds!=0;}),
                check("wounds","Wound holes",&s.wounds,
                    "Bullets and pellets leave holes with ragged bloody edges, dark torn rims and short blood smears. "
                    "The marks follow the monster's animation. Close hits widen a hole; seen from the side it closes. "
                    "Drawing only: the monster is hit as always."),
                check("varied_deaths","Varied deaths",&s.deathDirector,
                    "No two monsters die alike: the death frames keep their order but take new timing each kill (a snap, a "
                    "hesitation then a drop, a slump), monsters dropped by one blast differ on purpose, and the body settles "
                    "as it lands. Now and then a hit off the middle turns it, or a hard push steps it back. Drawing only."),
                choice("death_falls","Procedural falls",&s.deathFalls,{"Off","By the hit","Legs swept","Chest blast","Head shot"},{0,1,2,3,4},
                    "Monsters standing on two legs fall a way their drawings never show, on joints placed by hand or found in their "
                    "own art. A weak or low hit buckles the knee on its side and the body goes over, toward its weapon or away; a "
                    "hard hit snaps the shoulders back and knocks the body down, sooner and farther the harder the hit; a killing "
                    "shot through the head cuts the strings and the body crumples. Drawing only: damage is the game's own. The "
                    "fixed choices are for judging each one.",
                    [](const Settings &d){return d.deathDirector!=0;}),
                check("death_face_viewer","Dying turn to face you",&s.deathFaceViewer,
                    "A monster killed facing away turns toward you before its death frames, which are drawn facing the viewer, "
                    "instead of snapping round.",
                    [](const Settings &d){return d.deathDirector!=0;}),
                check("death_motion","Death reactions",&s.deathMotion,
                    "Dying monsters jolt with the killing blow: thrown back by high hits, legs knocked out by low ones, crumpling "
                    "under the chainsaw, hopping when gibbed, swaying as fliers fall. Drawn in whole-pixel rows over the game's own "
                    "death frames, and gone by the corpse frame."),
                check("hit_flinch","Hit flinches",&s.hitFlinch,"Monsters lean with the push of every hit and spring back. Drawing only: the game sees no difference."),
                slider("body_motion_strength","Reaction strength",&s.bodyMotionStrength,0.5,3,0.25,
                    "How far death reactions and hit flinches throw, bend and buckle the body.",
                    [](const Settings &d){return d.deathMotion!=0||d.hitFlinch!=0;}),
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
                    [](const Settings &d){return d.emissive!=0&&d.shootableLights!=0;})}},
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
        {"Sound",{
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
                    {"Off","25%","50%","75%","100%","125%","150%","175%","200%"})}},
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
                slider("ambience_amount","Ambience volume",&s.ambienceAmount,0.25,2,0.25,
                    "How loud the ambience is next to the effects.",
                    [](const Settings &d){return d.ambience!=0;},
                    {"25%","50%","75%","100%","125%","150%","175%","200%"})}},
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
                    "Off: DOOM's straight fade, still loud at mid range.",hasFalloff)}}}},
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
                    [](const Settings &d){return d.emissive!=0&&d.bakeOnlyLights==0;})}}}}};
}

namespace {
template<class Visit> void eachOption(Settings &s,Visit visit) {
    for(OptionTab &tab:optionTabs(s))for(OptionSection &section:tab.sections)for(Option &option:section.options)visit(option);
}
}
void readOption(Settings &s,const char *key,double value) {
    eachOption(s,[&](Option &option) {
        if(strcmp(option.key,key))return;
        if(option.kind==OptionKind::Check)*option.flag=value!=0;
        else if(option.kind==OptionKind::Choice) {for(int v:option.values)if(v==value)*option.flag=v;}
        else *option.number=option.snap(value);
    });
}
void writeOptions(FILE *file,Settings &s) {
    eachOption(s,[&](Option &option) {
        if(option.kind==OptionKind::Slider)fprintf(file,"%s %g\n",option.key,*option.number);
        else fprintf(file,"%s %d\n",option.key,*option.flag);
    });
}
}
