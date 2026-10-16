/* What the shared code (renderer, audio, F4 panel) needs to know about the
   game it runs in, in neutral terms, so scene3d/ and audio/ name no game's
   MT_, SPR_, S_ or sfx_ constants. Each game program links its own answers:
   platform/games/<game>/profile.c. */
#ifndef DOOM_GAME_PROFILE_H
#define DOOM_GAME_PROFILE_H
#ifdef __cplusplus
extern "C" {
#endif
struct mobj_s;
struct thinker_s;

/* The program: its name (window title, save folder, message boxes), its
   config file, and what a new config file starts with. */
const char *Game_Title(void);
const char *Game_ConfigFile(void);
const char *Game_ConfigDefaults(void);
/* Which WADs this program plays: a DOOM_WAD_* kind (wad_probe.h). */
int Game_WadKind(void);

/* Whether a thinker is a map object (an mobj_t). */
int Game_ThinkerIsMobj(const struct thinker_s *thinker);

/* The sectors whose light flickers, strobes or glows, and between what
   levels. smooth: glowing and fire-flickering sectors, which fade rather
   than switch. */
typedef void (*game_swing_visit)(int sector, int low, int high, int smooth, void *data);
void Game_LightSwings(game_swing_visit visit, void *data);

/* What a thing type is, as far as lights, shadows, blood and glass care. */
typedef enum {
    GAME_THING_OTHER,
    GAME_THING_ROCKET,            /* explosive missile: blasts lights and glass */
    GAME_THING_BIG_SHOT,          /* the strongest energy missile (BFG ball) */
    GAME_THING_BLOOD,             /* airborne blood */
    GAME_THING_TELEPORT_FOG,      /* teleport and item respawn flashes */
    GAME_THING_BARREL,            /* exploding barrel */
    GAME_THING_UNCOUNTED_MONSTER  /* a monster outside the kill count that still casts a shadow */
} game_thing_role;
game_thing_role Game_ThingRole(int type);

/* An exploding barrel's place in its blast: the frame 0..GAME_BLAST_FRAMES-1,
   or -1 for anything else. From GAME_BLAST_BURST on, the explosion has hit. */
#define GAME_BLAST_FRAMES 5
#define GAME_BLAST_BURST 3
int Game_BlastFrame(const struct mobj_s *thing);

/* A shot's muzzle flash: how far and strong its light reaches (the
   defaults suit a pistol), and the state whose frame colours it (-1: the
   shooter's own frame). */
void Game_MuzzleFlash(int weapon, float *radius, float *strength, int *flashstate);

/* The game's own look up and down, if it has one (Heretic's lookdir, which
   its aiming follows): the view pitch in radians, positive up, and mouselook
   turning it. Game_LookPitch returns 0 when the game has none and the
   renderer keeps its own pitch (DOOM). */
int Game_LookPitch(float *pitch);
void Game_Look(float delta);

/* Sprites the effects borrow artwork from: a sprite number, or -1. */
typedef enum { GAME_SPRITE_BLOOD, GAME_SPRITE_PUFF, GAME_SPRITE_EXPLOSION } game_sprite_role;
int Game_Sprite(game_sprite_role role);

/* Decorations the ambience gives a sound, by sprite number. */
typedef enum {
    GAME_DECORATION_NONE,
    GAME_DECORATION_LAMP,         /* standing lamp or lit column */
    GAME_DECORATION_TORCH,        /* tall torch or burning barrel */
    GAME_DECORATION_SHORT_TORCH,
    GAME_DECORATION_CANDLES
} game_decoration;
game_decoration Game_Decoration(int sprite);

/* Sound effects: I_GetSfxLumpNum (i_sound.h) is the game's, since lump
   names differ; Game_SfxLoaded tells the game where an effect's data sits. */
void Game_SfxLoaded(int id, void *data);

/* The F4 panel's font: the WAD lump of character c ('!' to '_'), or -1,
   and how many pixels to add after each glyph (the art's own spacing). */
int Game_FontLump(int c);
int Game_FontSpacing(void);

/* The F4 panel's clicks: a sound number for S_StartSound. */
typedef enum {
    GAME_MENU_MOVE,    /* selection or tab moved */
    GAME_MENU_DENY,    /* option not available */
    GAME_MENU_CHANGE,  /* option changed */
    GAME_MENU_OPEN,
    GAME_MENU_CLOSE
} game_menu_sound;
int Game_MenuSound(game_menu_sound sound);

#ifdef __cplusplus
}
#endif
#endif
