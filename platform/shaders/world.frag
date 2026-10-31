#version 450
#extension GL_GOOGLE_include_directive : require
// World and sprite shading; OPAQUE enables early depth for full-coverage
// textures. Masked walls (bars, fences) are drawn twice: COVERAGE writes
// depth where they cover, without light, then CUTOUT shades with early depth
// against it, so only the frontmost layer pays for lighting.
#if defined(OPAQUE)||defined(CUTOUT)
layout(early_fragment_tests) in;
#endif
#include "common.glsl"
layout(set=2,binding=0) uniform sampler2D image;
layout(set=2,binding=1) uniform sampler2D palette;
layout(set=2,binding=2) uniform sampler2D nextImage;
// RGBA16 cells as UNORM: SDL_gpu cannot sample integer formats.
layout(set=2,binding=3) uniform sampler2D seams;
layout(set=2,binding=4) uniform sampler2D contact;
layout(set=2,binding=5) uniform sampler2D reflection;
layout(set=2,binding=6) uniform sampler3D paletteLUT;
// Baked lighting: walls in an atlas (rgb light at half scale, a sun; bounce in
// the right half), and four map-space layers stacked in rows: floor light,
// ceiling light, floor bounce, ceiling bounce (rgb at half scale).
layout(set=2,binding=7) uniform sampler2D wallBake;
layout(set=2,binding=8) uniform sampler2D flatLight;
// Caustics: map cells near liquids hold the liquid's tint and a proximity;
// the pattern (R8, 64x64) comes from the level's main liquid flat.
layout(set=2,binding=9) uniform sampler2D causticMap;
layout(set=2,binding=10) uniform sampler2D causticPattern;
// Bake-only lights: where static light comes from (see encodeDirection in
// scene3d/bake_lights.cpp), walls at the wall bake's light coordinates, floors then
// ceilings in map space.
layout(set=2,binding=11) uniform sampler2D wallDirection;
layout(set=2,binding=12) uniform sampler2D flatDirection;
// Monitor glass (platform/screen_glass.h): texture alpha below 255 marks
// glass; this holds, per glass texel, its x and y from its screen's top-left
// texel, then the screen's width and height.
layout(set=2,binding=13) uniform sampler2D glassFrame;
// Shorelines (buildShore in scene3d/caustics.cpp): signed distance to the nearest
// liquid edge in r (sampled linearly), that edge's liquid sector and tint.
layout(set=2,binding=14) uniform sampler2D shore;
// Bump relief of walls and flats (withRelief in scene3d/images.cpp): slope along u
// and v in rg (0.5 level), grain in b; level elsewhere. Tiled flats are 16-bit
// with each texel's tile in a (tiles.glsl); 1.0 there means none.
layout(set=2,binding=15) uniform sampler2D relief;
// Storage buffers follow the samplers in SPIR-V but the four uniform blocks in
// SDL_gpu's Metal layout; METAL selects the second numbering.
#ifdef METAL
#define BLOCKER_BINDING 4
#define SECTOR_BINDING 5
#else
#define BLOCKER_BINDING 16
#define SECTOR_BINDING 17
#endif
// Each light's blockers and per-direction slices (buildLightWords in scene3d/lights.cpp).
layout(std430,set=2,binding=BLOCKER_BINDING) readonly buffer Blockers { uvec4 lightWords[]; };
layout(std430,set=2,binding=SECTOR_BINDING) readonly buffer Sectors { vec4 sectorInfo[]; };
layout(std140,set=3,binding=0) uniform Camera CAMERA_BLOCK c;
layout(std140,set=3,binding=1) uniform Lights FLASH_BLOCK;
layout(std140,set=3,binding=2) uniform Fog FOG_BLOCK;
// blend: the crossfade toward the animation's next frame. weaponProbe, for
// the relit weapon only: what it sees ahead, behind, right, left, up and
// down in the view's frame (rgb; weaponProbe in scene3d/weapon.cpp).
// frameMean, cycleMean: a liquid flat's average color at this crossfade and
// over its whole animation; cycleMean.a is the contrast it keeps far away,
// 0 for everything that is not a liquid (surfaceBinding in scene3d/scene.cpp).
// frameMean.a is 1 on ice floors (iceFlat): deep grooves, glassy shine;
// worn plates add 16 to it (wornPlate in scene3d/scene.cpp), metal 32
// (metalSurface).
layout(std140,set=3,binding=3) uniform Blend { float blend; vec4 weaponProbe[6]; vec4 frameMean; vec4 cycleMean; };
layout(location=0) out vec4 outColor;
#define BLOCKERS
#include "indexed.glsl"
#include "lighting.glsl"
#include "wind.glsl"
#include "whiteout.glsl"
#include "beam_air.glsl"
#include "fog_bank.glsl"

// The parts of the shading main() runs, by topic.
#include "world/tiles.glsl"
#include "world/surface.glsl"
#include "world/glass.glsl"
#include "world/bake.glsl"
#include "world/sector_light.glsl"
#include "world/caustics.glsl"
#include "world/shore.glsl"
#include "world/wear.glsl"
#include "world/water_ice.glsl"
#include "world/settled.glsl"
#include "world/weapon.glsl"
#include "world/mirror.glsl"
#include "world/fog_march.glsl"
// The classic flashlight's light, gathered apart from pulse (classicBeam).
vec3 beamLight=vec3(0);
// What dynamic lights add to walls and flats on top of the static light, for
// the saturation lift and highlight rolloff.
vec3 addedLight=vec3(0);
// What the baked lamps and their bounce add to walls and flats, for the
// saturation lift alone.
vec3 lampLight=vec3(0);
// A worn plate's grime at this texel (plateGrime), -1 on everything else.
float plateWear=-1.0;
// How much snow lies on this texel (world/wear.glsl): it smooths the bump and takes the gloss.
float snowWear=0.0;
// Water frozen in the cold here (world/water_ice.glsl's band, -1 none).
float coldIce=-1.0;
#include "world/sprite_light.glsl"
#include "world/surface_light.glsl"
void main() {
    bool sprite=(vMode&MODE_SPRITE)!=0u;
    bool wound=sprite&&(vMode&MODE_WOUND)!=0u;
    vec3 dp1=dFdx(vWorld),dp2=dFdy(vWorld);vec2 duv1=dFdx(vUV),duv2=dFdy(vUV);
    float footprint=max(length(duv1),length(duv2));
    // Mip level from the shorter axis, allowing 2:1 stretch along the longer
    // one so floors at grazing angles stay sharper.
    vec2 texelPixel=abs(duv1)+abs(duv2);
    float lod=log2(max(min(length(duv1),length(duv2)),footprint*0.5));
    // Map surfaces are planar, so screen derivatives give the exact face
    // normal; orient it toward the eye because either side may be drawn.
    vec3 normal=normalize(cross(dp1,dp2));
    if(dot(normal,c.eye.xyz-vWorld)<0.0) normal=-normal;
    // MODE_BLOOD: blood decals. vTint carries wetness (shine), how far the
    // color has darkened while drying, and the rain on it (blood.cpp
    // rainWash): its pale step (0-4) times two plus how much is washed away.
    bool blood=(vMode&MODE_BLOOD)!=0u;
    uint bakeFlags=uint(c.flashlightTint.w);
    // FEATURE_GLOSSY_SCREENS: monitor glass in computer textures. Its curved CRT dome
    // shapes light highlights and an edge sheen, and like a weak lens it
    // enlarges the picture's middle slightly (edges still meet the rim).
    bool glassy=!sprite&&!blood&&(bakeFlags&FEATURE_GLOSSY_SCREENS)!=0u&&c.effects.z==0.0&&isGlass(vUV);
    vec3 screenNormal=normal;vec2 screen=vec2(0),uv=vUV;
    bool dead=!sprite&&(vMode&MODE_DEAD_GLOW)!=0u;
    float crack=0.0;
    if(glassy) {
        vec2 origin,extent;screen=glassScreen(vUV,origin,extent);
        vec3 tangent,bitangent;textureFrame(normal,dp1,dp2,duv1,duv2,tangent,bitangent);
        if(dead&&(vMode&MODE_LAMP_OFF)==0u) {
            // MODE_DEAD_GLOW: a shot-out screen's glass lies broken, no
            // longer a lens: cracked into shards that each lean their own
            // way and so catch light apart. One its blinking sector only
            // switched off (MODE_LAMP_OFF) goes dark behind whole glass.
            uint shard,seed=vStatic*0x9e3779b9u^(vMode>>MODE_SECTOR_SHIFT);
            crack=glassCracks(vUV,origin,extent,seed,shard);
            uint h=tileHash(ivec2(int(shard),1),seed);
            vec2 lean=(vec2(float(h&255u),float((h>>8)&255u))/127.5-1.0)*0.2;
            screenNormal=normalize(normal+lean.x*tangent+lean.y*bitangent);
        } else {
            screenNormal=glassDome(screen,extent,normal,tangent,bitangent);
            vec2 lensed=screen*(0.88+0.12*screen*screen);
            uv=clamp(origin+(lensed*0.5+0.5)*extent,origin+0.5,origin+extent-0.5);
        }
    }
    // MODE_STAINED_GLASS: leaded stained glass, shaded like a shot-out
    // screen's broken glass. Its colored panes (the glow mask) are the
    // shards; each leans its own way (the leaded relief, withLeadedRelief)
    // and so takes its own highlights and sheen; the lead stays a matte wall.
    if(!sprite&&!glassy&&(vMode&MODE_STAINED_GLASS)!=0u&&(bakeFlags&FEATURE_GLOSSY_SCREENS)!=0u&&c.effects.z==0.0&&
       maskAt(image,vUV,false)>0.0) {
        vec3 tangent,bitangent;textureFrame(normal,dp1,dp2,duv1,duv2,tangent,bitangent);
        vec2 lean=(texelFetch(relief,wrapTexel(ivec2(floor(vUV)),textureSize(relief,0)),0).rg*255.0-128.0)/127.0;
        screenNormal=normalize(normal+lean.x*tangent+lean.y*bitangent);
        glassy=true;
    }
    // FEATURE_LIVE_READOUTS: LED readouts switch their lamps (readoutCell);
    // a shot-out panel's stay dark and still.
    if(!sprite&&!blood&&!glassy&&!dead&&(bakeFlags&FEATURE_LIVE_READOUTS)!=0u&&c.effects.z==0.0)
        uv=readoutCell(uv,vStatic);
    vec4 color;
    if(sprite) color=indexed(image,palette,uv,c.effects.w==1.0,false);
    else color=surfaceTexture(image,uv,texelPixel,lod);
    // Animated flats and walls crossfade toward their next frame.
    if(blend>0.0) color=mix(color,surfaceTexture(nextImage,uv,texelPixel,lod),blend);
    // Big pools: with distance the liquid's artwork fades into detail on a
    // steady average color, its contrast kept down to cycleMean.a, so far
    // water reads as one body instead of a tiled floor and no longer pulses
    // as the frames change. Grazing views fade it sooner. poolTone varies
    // the whole body a little across calm and rough stretches.
    if(!sprite&&cycleMean.a>0.0&&c.effects.z==0.0) {
        float facing=abs(normalize(c.eye.xyz-vWorld).z);
        float reach=vDistance*mix(2.5,1.0,saturate(facing*2.5));
        float detail=mix(1.0,cycleMean.a,smoothstep(192.0,1024.0,reach));
        color.rgb=max(cycleMean.rgb+(color.rgb-frameMean.rgb)*detail,vec3(0))*poolTone(vWorld.xy);
    }
    if(color.a<0.45) discard;
    // Phobos (scene3d/decompression.cpp poolIce): a pool's ice sheet, vTint how far it has
    // formed, the slime's glow left and how grey it has gone. It freezes in patches, whole
    // texels, the liquid showing through the gaps until it closes.
    bool iceSheet=!sprite&&!blood&&vTint.r>0.0&&normal.z>0.5;
    // Water frozen in the cold (scene3d/water_ice.cpp, vTint.r 2): the same sheet laid over
    // it, cut texel by texel into floes, thin ice and the white sheet (world/water_ice.glsl).
    if(iceSheet&&vTint.r>1.5) {
        coldIce=waterIceBand(floor(vWorld.xy)+0.5);
        if(coldIce<0.0) discard;
    }
    else if(iceSheet&&vTint.r<1.0) {
        vec2 t=floor(vWorld.xy)+0.5;
        // Noise piles up round its middle; stretched out, the sheet closes evenly over its time
        // instead of mostly at once.
        float when=smoothstep(0.22,0.78,0.7*wearNoise(t,14.0,0x7a3c91d5u)+0.3*wearHash(ivec2(t),0x1d2e3f47u));
        if(when>vTint.r) discard;
    }
    // Blood the rain washes away goes texel by texel.
    float bloodStep=blood?floor(vTint.b*0.5+0.001):0.0,bloodPale=bloodStep*0.25,bloodWash=blood?max(vTint.b-bloodStep*2.0,0.0):0.0;
    if(bloodWash>0.0&&float(tileHash(ivec2(floor(vUV)),0x2f6b9d1eu)&255u)/255.0<bloodWash) discard;
#ifdef COVERAGE
    // Depth comes from the rasterizer, per sample like the shading pass.
    outColor=color;return;
#endif
    // Wounds keep the sprite's coverage and silhouette normals, but their
    // blood has its own palette color even over dark clothing. A thin outer
    // stain blends into the artwork; the torn rim replaces it.
    if(wound) {
        uint index=(vMode>>MODE_WOUND_PALETTE_SHIFT)&255u;
        vec3 paint=texelFetch(palette,ivec2(int(index),0),0).rgb;
        color.rgb=(vMode&MODE_WOUND_STAIN)!=0u?mix(color.rgb,paint,0.7):paint;
    }
    // Phobos (MODE_FROST_SHIFT): rime on a monster left in vacuum, the moisture on it boiled
    // off and frozen back on. Whole texels: the silhouette's edges first (normals facing
    // aside), then mottled over the rest; thin, so the paint still shows through it.
    // Under nukage rain (c.weatherTint) the same bits are its soak instead: a translucent
    // darker green running down from the top in mottled texels.
    uint frostBits=sprite&&(vMode&(MODE_SCREEN|MODE_SOFT|MODE_BLOOD))==0u?(vMode>>MODE_FROST_SHIFT)&255u:0u;
    if(frostBits>0u&&c.weatherTint.w>0.0) {
        float soak=float(frostBits)/255.0;
        ivec2 texel=ivec2(floor(uv));
        float speck=float(tileHash(texel,0x2c1b3c6du)&255u)/255.0;
        float down=saturate(uv.y/max(float(textureSize(image,0).y),1.0));
        vec3 wet=c.weatherTint.rgb*0.45*max(dot(color.rgb,vec3(0.3,0.59,0.11))*1.8,0.25);
        float coat=down*0.65+speck*0.35<soak*1.2?0.5:0.0;
        color.rgb=mix(color.rgb,wet,max(coat,0.18*soak));
    }
    else if(frostBits>0u) {
        float frost=float(frostBits)/255.0;
        ivec2 texel=ivec2(floor(uv));
        float speck=float(tileHash(texel,0x6c8e9cf5u)&255u)/255.0;
        float rim=0.5;
        if((vMode&MODE_RELIT)!=0u) rim=saturate(length(texelFetch(image,clamp(texel,ivec2(0),textureSize(image,0)-1),0).ba*2.0-1.0));
        vec3 rime=vec3(0.80,0.86,0.93)*(0.8+0.2*speck)*max(dot(color.rgb,vec3(0.3,0.59,0.11))*1.6,0.45);
        float coat=speck*0.6+(1.0-rim)*0.4<frost?0.75:0.0;
        color.rgb=mix(color.rgb,rime,max(coat,0.15*frost));
    }
    // Rained on, blood pales: its red fades toward a dull grey of its own
    // brightness, never lighter (lighter reds would read pink).
    if(blood) color.rgb=mix(color.rgb,vec3(dot(color.rgb,vec3(0.299,0.587,0.114)))*vec3(1.0,0.92,0.86),bloodPale*0.6)*vTint.g;
    // The sheet turning plain white ice as the glow dies.
    // The cold's ice: thin ice dark and clear over the still water, slush darker, floes and the
    // sheet a flat pale grey keeping only a trace of the water's pattern (by its texels'
    // brightness the sheet still read as rippling water, only paler).
    if(iceSheet&&coldIce>=0.0) color.rgb=mix(color.rgb*0.75,vec3(0.80,0.86,0.92)*(0.72+0.25*(dot(color.rgb,vec3(0.299,0.587,0.114))-0.3)),coldIce*0.9);
    else if(iceSheet) color.rgb=mix(color.rgb,vec3(0.80,0.86,0.92)*(0.55+0.6*dot(color.rgb,vec3(0.299,0.587,0.114))),vTint.b*0.8);
    // The thick rim of the glass shades the picture's edges, in light steps.
    if(glassy) color.rgb*=round((1.0-0.5*smoothstep(0.7,1.0,max(abs(screen.x),abs(screen.y))))*16.0)/16.0;
    // A dead screen shows nothing but the dark tube behind the glass, and its
    // cracks catching the light.
    if(glassy&&dead) color.rgb=mix(color.rgb*0.08,vec3(0.45),crack);
    // Map surfaces carry sector+1 above the mode flags; sprite wounds use
    // the same bits for their palette paint and never decode a sector.
    bool mapped=(vMode>>MODE_SECTOR_SHIFT)>0u&&!sprite&&c.effects.z==0.0;
    uint sector=mapped?(vMode>>MODE_SECTOR_SHIFT)-1u:0u;
    SectorLit lit=sectorLighting(normal,mapped,sector,bakeFlags);
    float light=lit.light;
    // Weather and wear (world/wear.glsl): streaks, wet ground and snow.
    Wear wear=Wear(0.0,0.0,false,0.0,0.0,0.0,0.0,0.0,false,0.0,vec2(0.0));
    if(mapped&&!blood&&!glassy&&cycleMean.a==0.0) {wear=surfaceWear(vWorld-vec3(0.0,0.0,vLift),normal,vUV.x,sector,bakeFlags);snowWear=wear.snow;}
    // Worn plates (frameMean.a +16, wornPlate in scene3d/scene.cpp).
    if(mapped&&!blood&&!glassy&&(uint(round(frameMean.a))&16u)!=0u) {wear.grime=plateGrime(vUV,vWorld-vec3(0.0,0.0,vLift),normal,sector,vStatic);plateWear=wear.grime;}
    // Grounding: things resting on the floor darken over their lowest 24
    // units in colormap-like 1/16 steps; vSun holds strength and height.
    float grounding=1.0;
    if(sprite&&(vMode&MODE_GROUNDED)!=0u&&c.effects.z==0.0)
        grounding=round(mix(1.0,mix(0.72,1.0,smoothstep(0.0,24.0,vSun.y)),vSun.x)*16.0)/16.0;
    light*=grounding;
    // MODE_SCREEN|MODE_RELIT: the relit weapon (weapon.glsl).
    bool weapon=sprite&&(vMode&(MODE_SCREEN|MODE_RELIT))==(MODE_SCREEN|MODE_RELIT)&&c.effects.z==0.0;
    Weapon relit=Weapon(vec3(0),vec3(0),vec3(0),0.0);
    if(weapon) relit=weaponSurface(light);
    // Preserve the sprite's painted shading with one RGB increment for the
    // whole billboard; world surfaces evaluate colored lighting per pixel.
    // A sector's light level stands for the lamps the mapper lit it with, so
    // where static light falls, that light takes the lamps' color by their
    // share: grey under a red lamp turns red, white lamps change nothing, and
    // the tint fades out with the lamps' reach across sector edges. Bounce
    // light counts too (c.bake.w weights it), so corners the lamps reach only
    // by bounce take their color, not the flat level's grey. Sprites and the
    // weapon bring theirs from the CPU (lampTint in baked_lighting.h).
    vec3 lampTint=unpackUnorm4x8(vLampTint).rgb;
    if(mapped&&c.bake.x>0.0&&c.effects.z==0.0) {
        vec3 colored=lit.baked.rgb+lit.bounce*c.bake.y*c.bake.w;float peak=max(colored.r,max(colored.g,colored.b));
        if(peak>0.0) lampTint=mix(vec3(1),colored/peak,peak/(peak+lit.shown));
        lampTint=settledLight(lampTint,snowWear);   // on snow its brightness only (world/wear.glsl)
    }
    vec3 illumination=vec3(light)*lampTint*mix(vec3(1),c.sun.rgb,lit.sunShare)*mix(vec3(1),c.fx.yzw,lit.skyShare),specular=vec3(0);
    if(c.effects.z==0.0) {
        // Sprites carry their baked light (and, without detail, their
        // dynamic light) in the tint.
        vec3 pulse=sprite?vTint*grounding:vec3(0);
        if(sprite&&(vMode&(MODE_SCREEN|MODE_RELIT))==MODE_RELIT) billboardLight(color,pulse,specular);
        if(weapon) weaponLight(relit,light,pulse,specular);
        if(!sprite) surfaceLight(lit,mapped,normal,screenNormal,glassy,blood,color,dp1,dp2,duv1,duv2,pulse,specular);
        illumination=clamp(illumination+pulse+c.flashlightTint.rgb,vec3(0),vec3(2.2));
        // Like extralight, the classic beam brightens toward a little past
        // the palette's own colors and never beyond that.
        vec3 beam=beamLight*max(BEAM_CEILING-illumination,vec3(0));
        illumination+=beam;addedLight+=beam;
        if(glassy) {
            // Where the pane curves away from the eye it catches the room's
            // light as a thin sheen, in brightness only, in 1/16 steps.
            float edge=pow(1.0-saturate(dot(screenNormal,normalize(c.eye.xyz-vWorld))),3.0);
            specular+=vec3(round(edge*saturate(dot(illumination,vec3(0.299,0.587,0.114)))*0.45*16.0)/16.0);
        }
        // Lying snow and sand glitter in the flashlight's beam (beam_air.glsl).
        if(mapped&&snowWear>0.5&&c.beam.w>0.0&&(c.weather.w==WEATHER_SNOW||c.weather.w==WEATHER_SAND)) specular+=beamGlitter(vWorld,normal);
    }
    // FEATURE_GLOW_EDGES: walls at a glowing liquid's edge take its painted
    // strip (edgeStrip in world/shore.glsl), glowing where the strip does.
    float edgeGlow=0.0;
    if(mapped&&(bakeFlags&FEATURE_GLOW_EDGES)!=0u&&abs(normal.z)<=0.5) {
        vec3 paint;float amount=edgeStrip(vWorld-vec3(0.0,0.0,vLift),normal,vUV.x,paint,edgeGlow);
        color.rgb=mix(color.rgb,paint,amount);
    }
    float emission=0.0;
    // MODE_GLOWING_EYES: a monster's painted eyes shine a little past full
    // brightness whatever light falls on it.
    if(sprite&&!wound&&(vMode&MODE_GLOWING_EYES)!=0u&&c.effects.z==0.0) {
        emission=eyeGlow(image,uv);
        illumination=max(illumination,vec3(emission*1.2));
    }
    if(!sprite&&c.materials.x>0.0&&c.effects.z==0.0) {
        // The blue channel carries a mask, independent of palette/coverage.
        vec2 at=c.effects.x==2.0?sharpen(uv,texelPixel+c.texFilter.x):uv;
        emission=maskAt(image,at,c.effects.x>0.0);
        if(blend>0.0) emission=mix(emission,maskAt(nextImage,at,c.effects.x>0.0),blend);
        // MODE_DEAD_GLOW: a shot-out lamp or screen. What glowed is a dead,
        // dark tube or screen; the housing around it keeps its shading. By
        // the whole mask, before the sector's pulse below, so a dead lamp
        // in a glowing sector stays dead instead of pulsing.
        if(dead) {
            if(!glassy) color.rgb=mix(color.rgb,vec3(dot(color.rgb,vec3(0.299,0.587,0.114))*0.15),emission);
            emission=0.0;
        }
        // A glowing or fire-flickering sector's lamps fade with it (vLampTint alpha, lampGlow).
        emission*=unpackUnorm4x8(vLampTint).a;
        if(iceSheet) emission*=vTint.g;   // a frozen pool's glow dying (Phobos)
        emission=max(emission,edgeGlow);
        illumination=max(illumination,vec3(emission));
    }
    // FEATURE_DAMP_SHORES: damp walls and banks above liquids (shoreWet), darker and
    // a little stained by the liquid; glowing texels stay dry. Round a pool freezing
    // over (Phobos) the same rim is frost instead: white, taking the texels as their
    // glow dies.
    if(mapped&&(bakeFlags&FEATURE_DAMP_SHORES)!=0u&&normal.z>-0.5) {
        vec3 stain;vec2 rimIce;
        // The texel's own color, not the filtered or mipped one, so the line keeps to whole texels.
        float texelLuma=dot(surfaceLevel(image,floor(uv)+0.5,texelPixel,0).rgb,vec3(0.299,0.587,0.114));
        vec3 uDir,vDir;textureFrame(normal,dp1,dp2,duv1,duv2,uDir,vDir);
        float damp=shoreWet(vWorld-vec3(0.0,0.0,vLift),normal,vUV.x,normalize(uDir.xy+vec2(1e-6,0)),sector,texelLuma,stain,rimIce)*(1.0-emission);
        if(damp>0.0&&rimIce.x>0.0)
            color.rgb=mix(color.rgb,vec3(0.86,0.91,0.96)*max(dot(color.rgb,vec3(0.299,0.587,0.114))*1.4,0.6),damp*0.85);
        else if(damp>0.0) color.rgb*=mix(vec3(1),vec3(0.55)*mix(vec3(1),stain,0.25),damp);
    }
    color.rgb=applyWear(color.rgb,wear,emission);
    vec3 albedo=color.rgb;
    color.rgb=color.rgb*illumination;
    // Light also spills toward its own color by how saturated the texel is,
    // as the palette's ramps brighten toward white: by multiplying alone pure
    // dark blue carpet, a few percent luminance, swallowed it. Map surfaces
    // only: sprites keep their painted color and the shading of their
    // silhouette normals (billboardLight).
    float chroma=max(albedo.r,max(albedo.g,albedo.b))-dot(albedo,vec3(0.299,0.587,0.114));
    // The baked lamps and their bounce spill too, so lamp pools show on
    // saturated surfaces as on the grey floors beside them; a warm lamp
    // otherwise leaves blue carpet only its weakened blue. Only into the
    // channels the texel lacks: light it already reflects (a blue lamp on
    // blue carpet) the multiply has counted.
    vec3 lacking=1.0-albedo/max(max(albedo.r,max(albedo.g,albedo.b)),0.001);
    // Blood keeps its own pure red: spilled light would pale it to pink.
    vec3 lampSpill=c.effects.z==0.0&&!sprite&&!blood?lampLight*lacking*chroma*0.6*(1.0-emission):vec3(0);
    color.rgb+=lampSpill;
    if(c.effects.z==0.0&&!sprite&&any(greaterThan(addedLight,vec3(0)))) {
        // Dynamic light: the flashlight, shots, fireballs.
        if(!blood) color.rgb+=addedLight*chroma*0.6*(1.0-emission);
        // What it adds rolls off on bright texels, as an eye or camera keeps
        // a hotspot from blowing out: a bright wall in the beam no longer
        // jumps far ahead of the dark floor beside it. Dark texels and the
        // light without it keep their values.
        vec3 unlit=albedo*max(illumination-addedLight,vec3(emission))+lampSpill;
        float lit=dot(color.rgb,vec3(0.299,0.587,0.114)),base=dot(unlit,vec3(0.299,0.587,0.114));
        const float knee=0.45;
        if(lit>knee&&lit>base+0.001) {
            float rolled=max(knee+(1.0-knee)*(1.0-exp(-(lit-knee)/(1.0-knee))),base);
            color.rgb=unlit+(color.rgb-unlit)*((rolled-base)/(lit-base));
        }
    }
    color.rgb+=specular;
    if(mapped&&(bakeFlags&FEATURE_CAUSTICS)!=0u&&(abs(normal.z)<=0.5||(normal.z<-0.5&&(bakeFlags&FEATURE_CEILING_CAUSTICS)!=0u))) color.rgb+=caustics(vWorld,normal,sector,false);
    // FEATURE_CAUSTICS_SPRITES: things in the world (not the weapon or effect sprites)
    // near liquids catch the caustics too, in their own colors so the painted
    // shading survives; the scale matches the walls' additive light.
    if(sprite&&(bakeFlags&(FEATURE_CAUSTICS|FEATURE_CAUSTICS_SPRITES))==(FEATURE_CAUSTICS|FEATURE_CAUSTICS_SPRITES)&&(vMode&(MODE_SCREEN|MODE_SOFT))==0u&&c.effects.z==0.0)
        color.rgb+=albedo*(1.0-emission)*caustics(vWorld,normal,0u,true)*2.5;
    if((vMode&MODE_MIRROR_FLOOR)!=0u&&c.water.z>0.0&&c.effects.z==0.0) liquidMirror(color,albedo);
    // What hangs in the open air (a blizzard's whiteout, the outdoor haze, the
    // pall sand and ash raise) fades a point by how far the view to it runs
    // through the open air, not by where the point lies: eight points along
    // the way look at the map's cells, and those out under the sky count. A
    // window or doorway across a yard belongs to the room behind it and stood
    // sharp in the storm seen from indoors, and sprites carry no sector at all.
    // The same points gather the flashlight's beam in that air (beam_air.glsl),
    // indoors as much as there is of the open air round the eye (c.climate.w).
    float airOutside=0.0;   // the view's share through the open air, for the weather's ground fog (weatherAir)
    if((vMode&MODE_SCREEN)==0u&&(c.climate.z>0.0||c.haze.w>0.0||c.beamLight.w>0.0)) {
        // The open air: under the sky, or under a porch's roof (wear.glsl's sectorOpenAir).
        float way=length(vWorld-c.eye.xyz);float outside=mapped&&(sectorInfo[sector].w>0.0||sectorOpenAir(sector))?1.0:0.0,lit=0.0;
        uint yard=mapped&&sectorInfo[sector].w>0.0?sector:0xFFFFu;   // the sky the way ends in, for its enclosure
        if(c.map.z>0.0) {
            ivec2 size=textureSize(seams,0);
            float open=0.0;
            for(int i=0;i<8;++i) {
                float t=(float(i)+0.5)/8.0;
                vec2 at=mix(c.eye.xy,vWorld.xy,t);
                uint own=uint(round(texelFetch(seams,clamp(ivec2(floor((at-c.map.xy)*c.map.z)),ivec2(0),size-1),0).x*65535.0));
                bool air=own!=0xFFFFu&&(sectorInfo[own].w>0.0||sectorOpenAir(own));
                if(air) {open+=1.0;if(yard==0xFFFFu&&sectorInfo[own].w>0.0)yard=own;}
                if(c.beamLight.w>0.0) lit+=beamAir(mix(c.eye.xyz,vWorld,t))*(air?1.0:c.climate.w);
            }
            outside=open/8.0;
        }
        airOutside=outside;
        // The whiteout (whiteout.glsl): indoors as much as there is round the eye (c.climate.w).
        color.rgb=mix(color.rgb,c.haze.rgb*c.fx.x,whiteoutOver(way*(outside+(1.0-outside)*c.climate.w)));
        // Outdoor haze (c.haze, Game_Sun): far walls meet the sky's horizon colour,
        // less in an enclosed yard.
        if(c.haze.w>0.0&&yard!=0xFFFFu) {
            float open=1.0-outdoorShape(yard).x;
            color.rgb=mix(color.rgb,c.haze.rgb*c.fx.x,open*c.haze.w*0.6*(1.0-exp(-max(vDistance*outside-256.0,0.0)/1400.0)));
        }
        color.rgb+=beamGlow(way,lit/8.0);   // the beam lighting the air in front (beam_air.glsl)
    }
    if((vMode&MODE_SCREEN)==0u) {
        vec4 fog=fogAlong(vWorld);
        color.rgb=color.rgb*fog.a+fog.rgb;
        color.rgb+=(c.haze.rgb*c.fx.x-fogHaze(c.fx.yzw))*(1.0-fog.a)*weatherAir()*airOutside;   // the weather's air (lighting.glsl)
        // The fog bank (fog_bank.glsl): the point's sector by the map's
        // cells, so sprites and particles find theirs too, taken a cell and a
        // half toward the eye so a wall finds the sector in front of it, not
        // the one behind (a window's frame, a door). A sector whose floor lies
        // above the point can't hold it (the wall under a sill): then the
        // eye's own bank. What glows cuts through.
        if(c.bank.w>0.0) {
            vec2 toward=c.eye.xy-vWorld.xy,at=vWorld.xy+toward/max(length(toward),1.0)*1.5/max(c.map.z,1e-4);
            ivec2 cell=clamp(ivec2(floor((at-c.map.xy)*c.map.z)),ivec2(0),textureSize(seams,0)-1);
            uint own=uint(round(texelFetch(seams,cell,0).x*65535.0));
            vec3 there=c.bank.xyz;
            if(own!=0xFFFFu&&c.map.z>0.0&&vWorld.z-vLift>=sectorInfo[own].y-1.0) {
                vec4 entry=sectorInfo[uint(c.bankColor.w)+own];
                float stretches=floor(entry.y/2.0);
                there=vec3(entry.x,entry.y-2.0*stretches,entry.z);
                there=bankFilm(there,cell,sectorInfo[own].y);   // world/fog_march.glsl
                if(sectorInfo[own].w>0.0) there.x+=bankWave(vWorld.xy,sectorOpen(own));   // out under the sky: the wind's swells (fog_bank.glsl)
                // A draft's stream (dry ice) lying across the floor here: each
                // stretch a ribbon, its edge ragged in noise drifting along it a
                // step per tic, raising the bank's top where it is deeper.
                float stream=0.0;
                for(uint i=0u;i<uint(stretches);++i) {
                    vec4 ab=sectorInfo[uint(c.bankColor.w)+uint(entry.w)+2u*i],wd=sectorInfo[uint(c.bankColor.w)+uint(entry.w)+2u*i+1u];
                    vec2 d=ab.zw-ab.xy;float t=clamp(dot(vWorld.xy-ab.xy,d)/max(dot(d,d),1e-4),0.0,1.0);
                    vec2 way=d/max(length(d),1e-3);
                    float ragged=0.7+0.6*wearNoise(vWorld.xy-way*floor(c.fog.w*35.0)*0.4,20.0,0x7f4a7c15u);
                    float off=length(vWorld.xy-ab.xy-d*t)/max(mix(wd.x,wd.y,t)*ragged,1.0);
                    stream=max(stream,mix(wd.z,wd.w,t)*(1.0-smoothstep(0.5,1.0,off)));
                }
                // A stream is thicker than a layer: three times the bank there.
                if(stream>0.0) {there.x=max(there.x,sectorInfo[own].y+stream);there.y=max(there.y,3.0*saturate(stream/3.0));}            }
            vec4 bank=bankMarch(vWorld,there,emission);   // world/fog_march.glsl
            color.rgb=color.rgb*bank.a+bank.rgb;
        }
    }
    // Eye adaptation: a stepped exposure while the view settles to new light.
    if(c.effects.z==0.0) color.rgb*=c.fx.x;
    if(c.effects.z==32.0) color.rgb=1.0-color.rgb;
    // A distance fog as in Turok (DERPINFO farfog): everything fades to one
    // colour, which is also all there is behind it; past its far end nothing
    // is drawn. After the exposure, so the fully fogged matches the clear.
    if(c.farFog.w>0.0&&(vMode&MODE_SCREEN)==0u)
        color.rgb=mix(color.rgb,c.farFog.rgb,saturate((length(vWorld-c.eye.xyz)-c.farFogRange.x)/max(c.farFogRange.y-c.farFogRange.x,1.0)));
    if((vMode&MODE_FUZZ)!=0u) {color.rgb*=0.3;color.a=0.65;}
    if(c.effects.y>0.0) {
        color.rgb*=0.88+0.12*sin(gl_FragCoord.y*3.14159265);
        color.rgb=floor(color.rgb*31.0+0.5)/31.0;
    }
#ifndef OPAQUE
    // MODE_SOFT: effect sprites (explosions, puffs, fogs). They dissolve on
    // an ordered dither in Doom pixels toward their sector's floor and
    // ceiling, and their depth is pulled toward the eye by vSun.x so walls
    // they stand against no longer slice the billboard.
    float depth=gl_FragCoord.z;
    if((vMode&MODE_SOFT)!=0u&&!wound) {
        vec4 info=sectorInfo[(vMode>>MODE_SECTOR_SHIFT)-1u];
        float room=min(vWorld.z-info.y,info.w>0.0?1e4:info.z-vWorld.z);
        if(saturate(room/12.0)<bayer4(ivec2(floor(gl_FragCoord.xy/max(c.fx2.x,1.0))))) discard;
        float n=c.projection.z,f=c.projection.w,z=max(n,vDistance-vSun.x);
        depth=f/(f-n)-n*f/((f-n)*z);
    }
    // OVERLAY draws the weapon in the pass without a depth target, where Metal
    // on Intel GPUs rejects a shader that writes depth.
#if !defined(CUTOUT)&&!defined(COVERAGE)&&!defined(OVERLAY)
    gl_FragDepth=depth;
#endif
#else
    // Mirror pass (water = plane,0,0,1): alpha holds how near the point is
    // to the liquid surface along the mirrored ray, sharp within a step's
    // height, gone a couple of storeys up (clear water, c.eye.w: a whole
    // hall up). Blended geometry keeps it.
    if(c.water.z==0.0&&c.water.w>0.0) {
        float along=max(vWorld.z-c.water.x,0.0)/max(abs(normalize(vWorld-c.eye.xyz).z),0.05);
        color.a=1.0-smoothstep(16.0,mix(256.0,1024.0,c.eye.w),along);
    }
#endif
    outColor=color;
}
