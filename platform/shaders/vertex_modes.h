// Vertex mode bits (Vertex::mode in C++, vMode in the shaders), shared by
// both sides like feature_flags.h. scene3d.h and common.glsl include it.
#ifndef DOOM_VERTEX_MODES_H
#define DOOM_VERTEX_MODES_H

#define MODE_SCREEN          1u    // already in clip space: HUD, weapon, crosshair
#define MODE_SPRITE          2u    // a sprite: clamped texels, drawn after the opaque world
#define MODE_SUNBEAM         2u    // mist pass only: a sunbeam ribbon, not a mist layer
#define MODE_MIST_OUTSIDE    4u    // mist pass only: a mist layer out under the sky (a blizzard whites it out from anywhere)
#define MODE_PARTICLE_OUTSIDE 4u   // particle pass only: out under the sky, where the outdoor haze takes it as it takes the walls
#define MODE_PARTICLE_DAYLIT 8u    // particle pass only: weather (rain, snow, sand, ash), lit by daylight: drawn at its scale as the sky is
#define MODE_LAMP_SHEET      16u   // particle pass only: a card of rain round a lamp (scene3d/lamp_sheets.cpp, lamp_sheet.glsl)
#define MODE_LAMP_SHEET_FAR  32u   // with MODE_LAMP_SHEET: the card behind the lamp, its own streaks
#define MODE_LAMP_SHEET_PANEL 64u  // with MODE_LAMP_SHEET: a glowing wall panel's, lighting only ahead of its wall
#define MODE_FUZZ            4u   // spectre (MF_SHADOW): darkened and translucent
#define MODE_GLASS_SHARD     4u    // glass pass only: a tumbling polygon with a full 3D face normal
#define MODE_GLASS_CRACK     2u    // glass pass only: a fracture edge reached by the spreading crack
#define MODE_GLASS_HELD      8u    // glass pass only: a fragment still attached to the pane
#define MODE_GLASS_DARK      16u   // glass pass only: the shadow beside a fracture edge
#define MODE_RELIT           8u    // per-pixel light from the silhouette normals
#define MODE_FLIPPED         16u   // mirrored sprite frame: flip the normals too
#define MODE_LAMP_OFF        16u   // map surfaces only, with MODE_DEAD_GLOW: switched off by its sector, not shot out
#define MODE_MIRROR_FLOOR    32u   // a liquid floor sampling the reflection pass
#define MODE_PICKUP          64u   // pickups: a glint now and then (world/pickup_glint.glsl)
#define MODE_GROUNDED        128u  // darkened feet of things resting on the floor
#define MODE_STAINED_GLASS   128u  // map surfaces only: leaded stained glass (Image::leaded)
#define MODE_BLOOD           256u  // blood decal; vTint carries wetness and drying
#define MODE_HARD_SILHOUETTE 512u  // shadow pass: keep the sprite's pixel outline
#define MODE_SOFT            1024u // effect sprite: dithers out at floors, drawn in front of walls
#define MODE_STIPPLED        1024u // decal pass only: a hard silhouette's edge breaks into a stipple
#define MODE_DECAL_OUTSIDE   2048u // decal pass only: the mark lies out under the sky (in the weather's whiteout)
#define MODE_WET_SPRITE      2048u // airborne blood glints like floor blood (needs MODE_RELIT)
#define MODE_GLOWING_EYES    4096u // a living monster: its eyes glow (eyeGlow in indexed.glsl)
#define MODE_DEAD_GLOW       8192u // map surfaces only: a shot-out lamp's glowing texels go dark (shot_lights.cpp)
#define MODE_WOUND           8192u // sprites only: blood paint from the WAD palette, preserving coverage and normals
// Map surfaces and soft effect sprites carry sector+1 here; 0 means none.
// Wounded monster sprites instead carry a blood palette index in bits 14-21.
#define MODE_SECTOR_SHIFT    14
#define MODE_WOUND_PALETTE_SHIFT 14
#define MODE_WOUND_STAIN     (1u<<22) // blend blood into the source paint instead of replacing it
// Pickups only (never soft or wounded): the glint's seed, 0-255, in bits 14-21, and a
// fullbright pickup frame (a soulsphere) keeping its soft bloom.
#define MODE_PICKUP_SEED_SHIFT 14
#define MODE_PICKUP_GLOW     (1u<<22)
#define MODE_LAMP_GLOW       (1u<<22) // a sprite that is no pickup: a decoration Doom draws fullbright (a lamp, a torch): lit by the room, its bright paint kept bright
// Sunbeam ribbons only: the ribbon's number, 0-255, in bits 14-21, shifting its dither (mist.frag).
#define MODE_SUNBEAM_SEED_SHIFT 14
// And its length in 8-unit steps, 0-255, in bits 22-29, for ends that fade over a set distance.
#define MODE_SUNBEAM_LENGTH_SHIFT 22
// Monster sprites only (not soft ones, whose sector reaches this bit): a firing frame (firingFlash in indexed.glsl).
#define MODE_RIM             (1u<<21) // a lit sprite (not soft, wound paint or pickup: their bits 14 to 21 are taken): the edges that face a light are picked out (explosions.cpp's Flashbulb)
#define MODE_FIRING          (1u<<23)
// Monster sprites only (not soft ones): Phobos' rime on a body left in vacuum, 0-255 (decompression.cpp).
#define MODE_FROST_SHIFT     24

#endif
