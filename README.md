# Duct-Tape DOOM

A for-fun experimental port of id Software's 1997 DOOM source release to
modern macOS, Linux and Windows. It keeps the original game simulation and
adds an optional GPU renderer that builds a 3D scene from the loaded WAD at
runtime.

The renderer works from the original WAD artwork, map structure and game
state. No replacement textures, models, pre-authored lighting or modified
maps are required. The aim is to make DOOM feel more physically coherent
without turning it into a different game.

## Why this exists

This started as a visual experiment: what would DOOM look like if its
presentation were pushed further while still feeling unmistakably like DOOM?

I treat the renderer as a playable sketch of that idea. The goal is not to
show off individual rendering techniques, but to find cheap, convincing ways
to make the original maps, textures and sprites feel more alive. Ordinary
vanilla and limit-removing WADs should benefit automatically rather than
requiring their authors to prepare special assets or lighting data.

Most of the effects are here because I liked similar tricks in older games.
Few of them are actually modern. They are fake-modern, deliberately cheap
illusions that look the part.

If an idea in here is useful to somebody else or inspires another experiment,
the project has already done something worthwhile.

Claude is used extensively as the implementation tool that makes this
experiment practical in my spare time. I decide the visual direction,
rendering constraints and features, test the resulting behaviour in-game,
and keep iterating until it matches what I had in mind. Much of the code
itself is written or modified by Claude. Without that workflow, this project
probably would not exist.

The default settings are how I prefer to play DOOM; almost everything can be
changed in the graphics panel with **F4**.

<img width="2560" height="1600" alt="DOOM-195357624916" src="https://github.com/user-attachments/assets/34ea0d19-4032-4cd5-b2a4-29a1defd3687" />

## How the graphics cheat

### Rooms are lit by things that look like lights

DOOM already tells a lot about lighting indirectly: sector brightness,
textures, glowing flats, decorations, windows and the shapes of rooms. The
renderer tries to turn those hints into actual light.

Lamp strips, computer panels, lava and other emissive artwork are identified
from texture names and palette colors. Their bright pixels produce glow masks
and approximate light sources without authored light definitions. Wall panels
shine mostly forward instead of lighting through their own wall.

Windows, open sky-facing gaps and similar openings become broad sky-colored
light sources. With sun lighting enabled, windows looking toward sunlit
outdoor areas contribute more than windows facing shade. Several windows are
capped together so a room cannot become implausibly brighter than outdoors.

When lamps, screens and windows explain a room's brightness well, the renderer
can remove some of DOOM's uniform sector light and let those sources put it
back. The result is that light appears to come from somewhere and far corners
can fall darker instead of every point in the sector being equally bright.

See
[`platform/scene3d/bake_lights.cpp`](platform/scene3d/bake_lights.cpp),
[`platform/emissive.h`](platform/emissive.h) and
[`platform/surface_lighting.h`](platform/surface_lighting.h).

### Static lighting is baked when the level loads

The level-load bake walks DOOM's simple 2D map rather than building a modern
offline lightmap.

It gathers light from lamps, emissive textures, glowing liquids, decorations,
windows and the sky. Walls and closed portals block it. It also adds one
bounce, sky fill, sun shadows, ambient occlusion around corners and ledges,
and shadows from decorations such as columns, trees and hanging bodies.

The baked result can be aligned to the original texture grid so lighting
changes along DOOM texels instead of looking like a smooth modern lightmap.

Doors, lifts and lowering walls can trigger local re-lighting as they move, so
light gradually appears underneath an opening instead of remaining frozen in
the level's starting state.

See
[`platform/baked_lighting.h`](platform/baked_lighting.h) and
[`platform/scene3d/relight.cpp`](platform/scene3d/relight.cpp).

### Bump and gloss are guessed from the original pixels

World textures do not need normal maps or material definitions.

The renderer derives surface relief from the WAD artwork itself. Broad
brightness structure is used to find panel seams, mortar and grooves while
small noisy changes are treated more gently, so a riveted ceiling can catch
light along its seams without every painted speck becoming a crater.

Palette ramps also provide a crude material guess. Greys and blues tend toward
metal, greens toward wet or polished surfaces, while browns, flesh and many
stone colors remain much more matte. Optional varied highlights make smooth
artwork produce tighter reflections than busy artwork.

It is intentionally approximate. The aim is to make the existing art react
to light rather than reinterpret it as a physically based material set.

See
[`platform/scene3d/images.cpp`](platform/scene3d/images.cpp) and
[`platform/shaders/world.frag`](platform/shaders/world.frag).

### The weapon sprite is de-lit and lit again

The player's weapon is still the original 2D WAD sprite.

Before drawing it, the renderer tries to remove part of the broad illumination
and painted sheen already present in the artwork while keeping outlines,
texture detail and the palette's own color ramps. It derives rounded normals
from the silhouette and painted shading, and a gloss map from the highlights
and metal-like colors it finds.

The shader then lights that sprite from lamps, muzzle flashes and the room
around the player. Six directional probes give it a cheap approximation of
its surroundings, so bright walls and windows can creep across glossy metal
and the top of a barrel can catch a lamp ahead.

There is no weapon model.

See
[`platform/weapon_lighting.h`](platform/weapon_lighting.h),
[`platform/scene3d/weapon.cpp`](platform/scene3d/weapon.cpp) and
[`platform/shaders/world.frag`](platform/shaders/world.frag).

### Monitor textures pretend to contain curved CRT glass

Computer textures are examined for dark rectangular screen areas surrounded
by lighter housing. Vent grilles and similar shapes are rejected.

Detected screens receive no additional geometry. The world shader creates a
fake curved normal over the flat texture, bends the picture slightly like a
weak lens, darkens the rim and lets dynamic lights slide across it as a glass
highlight.

See
[`platform/screen_glass.h`](platform/screen_glass.h).

### Muzzle flashes, projectiles and explosions carry their own colors

The renderer samples the original sprite artwork to decide what color a light
should be. Bright saturated pixels are used while transparent outlines and
white cores are ignored.

A plasma projectile therefore casts blue light because the plasma sprite is
blue. The same system handles weapon flashes, enemy attacks and glowing
projectiles. Walls, doors, raised floors and other map geometry block their
light.

Enemies also receive a second flattened shadow pointing away from the
strongest nearby dynamic light.

See
[`platform/flash_lighting.h`](platform/flash_lighting.h) and
[`platform/scene3d/shadows.cpp`](platform/scene3d/shadows.cpp).

### Sprite shadows are just squashed sprites

Enemy shadows are not shadow maps.

The current animated sprite silhouette is flattened onto the floor as a dark
translucent decal, clipped to the sector's floor polygons. It spreads and
fades when a monster rises into the air.

The flashlight can also project hard-edged sprite silhouettes onto walls, and
the player can cast a shadow from the sun or strongest suitable light.

Sprites themselves get rounded lighting from normals derived from their
silhouettes, while the original painted shading remains visible.

### Water reflections render the world again, cheaply

Only water mirrors the scene.

When visible water is near the player, the nearest visible water level becomes
a mirror plane and the world is rendered again from a vertically mirrored
camera at half resolution. The water shader distorts that image with both
simple waves and the luminance changes in the original animated water flat,
so DOOM's own texels break up the reflection.

The optional retro reflection mode renders the mirror at one-eighth resolution,
keeps nearest-neighbour pixels and pushes the result back through the WAD
palette.

Nukage, slime, blood and lava deliberately do not mirror the room.

See
[`platform/gpu3d/frame.cpp`](platform/gpu3d/frame.cpp),
[`platform/scene3d/geometry.cpp`](platform/scene3d/geometry.cpp) and
[`platform/shaders/world.frag`](platform/shaders/world.frag).

### Liquids affect the world around them

Liquid flats can generate moving caustics on nearby walls and, optionally,
ceilings. The pattern comes from the level's own liquid artwork rather than a
downloaded texture.

Banks immediately above non-hot liquids can darken as if damp.

Bullets striking liquid throw palette-colored drops and expanding rings.
Things landing or wading in liquid disturb it too. Nukage, slime, blood and
lava behave as heavier liquids, with slower rings, larger drops, bubbles and
a wobbling collar around objects standing in them. Water stays lighter and
quicker.

Particles remain chunky DOOM-sized blocks rather than smooth modern particle
textures.

See [`platform/scene3d/liquids.cpp`](platform/scene3d/liquids.cpp) and
[`platform/shaders/world.frag`](platform/shaders/world.frag).

### The atmosphere is mostly fake geometry and cheap math

Distance fog uses an analytic exponential approximation rather than
volumetric raymarching.

Pits, liquids and damaging floors can carry low cloud-like mist layers.
Sunlit openings can generate simple light shafts back toward the inferred sun,
with dust motes appearing only where the sun or flashlight catches them.

Hot floors and flames create small heat volumes. A later screen pass turns
those into a tic-stepped sideways shimmer rather than simulating refracting
air.

The sun itself is inferred from the level's sky lighting and can appear in the
sky with optional forward scattering.

See
[`platform/volumetric_fog.h`](platform/volumetric_fog.h),
[`platform/scene3d/atmosphere.cpp`](platform/scene3d/atmosphere.cpp),
[`platform/shaders/mist.frag`](platform/shaders/mist.frag) and
[`platform/shaders/present.frag`](platform/shaders/present.frag).

### Blood is generated from the WAD too

Hitscan attacks can leave floor and wall splats, and corpses gradually grow
pools beneath them.

The decal artwork is generated at runtime using colors sampled from the WAD's
own blood sprites. Fresh floor blood can catch dynamic highlights before
drying darker and matte. Pools grow in visible steps rather than continuously.

Bullet impacts also leave marks and throw small chunks colored from the
surface that was hit.

See [`platform/scene3d/blood.cpp`](platform/scene3d/blood.cpp) and
[`platform/scene3d/impacts.cpp`](platform/scene3d/impacts.cpp).

### Pixel art stays pixel art

The renderer can draw at native display resolution without forcing the
textures themselves to become smooth.

World textures can use nearest filtering, ordinary bilinear filtering, or a
sharp-bilinear mode that keeps the center of each texel flat and only softens
its edge. Palette-aware mipmaps reduce distant shimmer.

Sprites can remain nearest-neighbour or use an optional xBR shader that
reconstructs diagonal pixel-art edges from the original palette colors.

A final optional palette pass quantizes the complete rendered image — including
lighting, fog and bloom — back to the current WAD palette.

Animated walls and flats crossfade between their original frames, while doors,
lifts, sprites and the camera interpolate between DOOM's original 35 Hz game
ticks.

## How the sound cheats

The sound gets the same treatment as the graphics: everything is worked out
from the map while you play, and the original sound effects are still mixed
as crunchy 8-bit samples. The goal is closer to a late-90s A3D or EAX sound
card than to a modern lush reverb. Music stays dry. The effects are in the
**Sound** tab of the F4 panel.

### Rooms are measured with rays

Every other tic, 32 rays are cast across the map from the player in a flat
circle. A ray stops at a one-sided wall, or at an opening too low to let
sound through, such as a closed door or a raised lift. Rays that leave
through the open sky do not come back.

The rays give the walls their distance, and they also sketch the floor plan.
From that plan, the ceiling heights the rays crossed and how much of it is
open sky, the room's volume goes into Sabine's reverberation formula. The
result is squeezed into a game-like range, so corridors slap back, big halls
boom and the outdoors stays almost dry.

There is no material data in a WAD, so materials are guessed from texture
names. `STARTAN`, `COMP` and `METAL` ring like metal, `WOOD` and the panel
textures are dull, and flesh, liquids and dirt each soak up sound their own
way. Names that match none of these count as stone.

### Echoes and a tail, kept a little lo-fi

The rays are grouped into eight directions around the player. Each group
becomes one discrete echo, delayed by the round trip to its walls and panned
to where they are relative to the way the player faces. A ninth echo comes
off the ceiling.

A small feedback delay network then produces the late tail: eight damped
delay lines, mixed through a Householder matrix. Metal rooms keep their
treble, wooden ones swallow it. When the room changes, the echoes crossfade
and the tail glides over a third of a second, so walking through a door
never clicks.

Far sounds send relatively more to the reverb than to the dry mix, so they
come across more distant as well as quieter.

### Walls muffle, distance dulls

Each positioned sound runs through its own two-stage lowpass filter. The
cutoff is measured when the sound starts and again every other tic while
the sound or the player moves.

- **In sight:** only distance takes away treble, from about 200 units out
  to DOOM's own clipping distance.
- **Round a corner:** if sound has an open way round to the player through
  an open door, over a ledge or through a window, it is muffled a little.
  The search walks short routes through connected sectors.
- **Sealed:** with only closed doors and solid walls in between, it is
  muffled heavily.

Muffling only changes the tone. Volume stays exactly as in vanilla DOOM,
so you can still hear where monsters are as well as you ever could.

The mixer itself now uses DOOM's real 0–15 effects volume scale and DMX-style
panning. A peak limiter ducks the mix briefly when many shots fire together
instead of letting it crackle.

See [`platform/audio/room.c`](platform/audio/room.c),
[`platform/audio/reverb.c`](platform/audio/reverb.c),
[`platform/audio/occlusion.c`](platform/audio/occlusion.c) and
[`platform/i_sdl_sound.c`](platform/i_sdl_sound.c).

## At a glance

- **Accelerated 3D renderer:** SDL_gpu, using Metal on macOS and Vulkan on
  Linux and Windows.
- **Native resolution and arbitrary aspect ratio**, with mouse look,
  interpolated movement and optional MSAA.
- **Classic renderer always available:** the original 320×200 software view
  is one keypress away.
- **No replacement asset pack:** original WAD textures, flats, sprites and
  palettes remain the source material.
- **Compatibility:** vanilla and limit-removing maps, including DEHACKED
  patches.
- **Not supported:** Boom, MBF21 and GZDoom maps, slopes, 3D floors, model
  packs, ray tracing and multiplayer.
- **Room acoustics:** echoes and reverb measured from the map, sounds muffled
  behind walls and dulled with distance.
- **Settings panel:** press **F4** to enable, disable and tune the individual
  graphics and sound effects.
- Headless demo runner, regression tests and a GPU performance smoke test are
  included for development.

The Mac build is the most tested. Linux and Windows builds compile in CI but
have seen less real-world play.

## Download and play

Ready-to-play builds for macOS, Windows and Linux are on the
[Releases page](https://github.com/robongrui/duct-tape-doom/releases).

Each includes Freedoom, so it plays straight away.

On Windows and Linux, drop your own `.wad` files into the `wads` folder next
to the game. On the Mac, choose your WAD folder when the app starts.

See [PLAY.md](PLAY.md) for controls and a description of the graphics options.

## Game data

This repository contains **no commercial game data**.

To build and play from source you need an IWAD:

- **Freedoom** (free): download `freedoom1.wad` or `freedoom2.wad` from
  [freedoom.github.io](https://freedoom.github.io/).
- **Original DOOM / DOOM II** (commercial): use the `.wad` files from a copy
  you own, for example from Steam or GOG.

Please do not commit or upload commercial WAD files to this repository.

## Build

You need CMake 3.21+ and a C11/C++17 compiler (Apple Clang, Clang, GCC or
Visual Studio 2022). SDL3 is downloaded automatically if it is not installed.

```sh
cmake -S . -B build/play -DCMAKE_BUILD_TYPE=Release -DDOOM_BUILD_SDL=ON \
  -DDOOM_GAME_DATA="/absolute/path/to/freedoom1.wad"
cmake --build build/play --parallel
ctest --test-dir build/play --output-on-failure
