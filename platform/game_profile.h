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
/* Whether the game reads typed letters right now (its menus, yes/no
   prompts, save names, chat): those keys follow the keyboard layout, while
   playing keys stay where they sit on the keyboard. */
int Game_TakesText(void);

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

/* The colour a monster bleeds with F4's enemy-appropriate blood, by thing
   type: the blood sprite's own red, turned green or blue, or none at all. */
typedef enum { GAME_BLOOD_RED, GAME_BLOOD_GREEN, GAME_BLOOD_BLUE, GAME_BLOOD_NONE } game_blood_tint;
game_blood_tint Game_BloodTint(int type);

/* An exploding barrel's place in its blast: the frame 0..GAME_BLAST_FRAMES-1,
   or -1 for anything else. From GAME_BLAST_BURST on, the explosion has hit. */
#define GAME_BLAST_FRAMES 5
#define GAME_BLAST_BURST 3
int Game_BlastFrame(const struct mobj_s *thing);

/* A shot's muzzle flash: how far and strong its light reaches (the
   defaults suit a pistol), and the state whose frame colours it (-1: the
   shooter's own frame). */
void Game_MuzzleFlash(int weapon, float *radius, float *strength, int *flashstate);

/* How much a weapon rattles on the player's back as they walk: 0 a light
   one (fist, pistol), 1 a long gun on a sling, 2 a heavy one. */
int Game_WeaponHeft(int weapon);

/* The game's own look up and down, if it has one (Heretic's lookdir, which
   its aiming follows): the view pitch in radians, positive up, and mouselook
   turning it. Game_LookPitch returns 0 when the game has none and the
   renderer keeps its own pitch (DOOM). */
int Game_LookPitch(float *pitch);
void Game_Look(float delta);

/* Sprites the effects borrow artwork from: a sprite number, or -1. */
typedef enum { GAME_SPRITE_BLOOD, GAME_SPRITE_PUFF, GAME_SPRITE_EXPLOSION, GAME_SPRITE_TORCH } game_sprite_role;

/* A torch the player carries (Heretic's Torch item): 1 while the game's
   fixed colormap is its light, with *flame how brightly it burns (0-1, the
   game's own flicker). The renderer lights the view with a carried flame
   and a faint lift instead of that colormap. */
int Game_Torch(float *flame);
int Game_Sprite(game_sprite_role role);

/* Effects made for DOOM's world that a game can do without: their F4
   options stay off and greyed out where Game_HasEffect says no. */
typedef enum {
    GAME_EFFECT_WINDOW_GLASS,     /* panes in windows, shattering */
    GAME_EFFECT_SHOOTABLE_LIGHTS, /* lamps and screens shot out, machinery sparks */
    GAME_EFFECT_FLASHLIGHT,       /* the F key's flashlight */
    GAME_EFFECT_WIND,             /* made-up wind under the sky and through openings */
    GAME_EFFECT_NATURAL_FALLOFF,  /* sounds fading with distance as in air, in place of the game's own curve */
    GAME_EFFECT_FREE_AIM,         /* mouselook bullets going where the view points (else the game's own aiming) */
    GAME_EFFECT_AUTHORED_EPISODES, /* hand-picked looks for the game's own episodes (Game_Mood) */
    GAME_EFFECT_PHOBOS,           /* physically accurate Phobos (Game_Phobos) */
    GAME_EFFECT_TUNED_MOVEMENT,   /* DERP's tighter player movement (else the game's own) */
    GAME_EFFECT_MONSTER_BLOOD     /* blood coloured by the monster (Game_BloodTint) */
} game_effect;
int Game_HasEffect(game_effect effect);

/* The outdoor light. fromSky: 1 reads the sun from the sky texture: its
   height where the sky paints its bright patch (low patches give long
   shadows), and from how bright the sky is, how strong it shines and in
   what colour (a dark sky gives soft moonlight in its own hue); 0 keeps it
   at 40 degrees in the bright columns' tint. shade: the share of light
   shaded outdoor surfaces lose; gain: the share sunlit ones win (walls by
   how squarely they face the sun). skyCast: how much of the outdoor light
   takes the sky's colour. haze: how far outdoor surfaces fade into the
   sky's horizon colour with distance (0 none, 1 strong). */
typedef struct { int fromSky; float shade, gain, skyCast, haze; } game_sun;
void Game_Sun(game_sun *sun);

/* Authored episodes (games/<game>/authored.c): looks hand-picked for the
   game's own maps, beyond what the WAD data gives, so episodes can differ in
   mood. Not DERP's derived look: F4 "Authored episodes" switches the whole
   layer off, and maps a custom WAD brings never get it. Game_Mood fills in
   mood for one of the game's own maps and leaves it alone for one it has
   nothing for (it starts as { 1, 1, 0, 0, -1, 1, { 1, 1, 1 }, 0 }).
   outdoor: what the light under the sky keeps (1 as mapped); sun: 0 hides
   the sun (no sunlight, disc or sunbeams); shineOut: window light turns
   round, the lit rooms shining out through their openings into the dark
   outdoors instead of the outdoors lighting the rooms; rain: it pours under
   the sky (F4 Rain); rainAngle: the way the wind blows the rain, in degrees
   as map angles go (0 east, 90 north), whatever the sun does; below 0 the
   level picks its own; rainAmount: how hard (1 a downpour, 0.5 half as
   many drops); skyTint: what the sky and the haze under it are drawn
   in (1, 1, 1 as painted); snow: it snows instead of raining (rainAngle and
   rainAmount then say how the snow blows and how thick it falls). */
typedef struct { float outdoor; int sun, shineOut, rain; float rainAngle, rainAmount, skyTint[3]; int snow; } game_mood;
void Game_Mood(int episode, int map, game_mood *mood);

/* Also authored: windows one of the game's own maps has broken before the
   player comes, each the window nearest a spot (x, y). Fills at most max
   spots, returns how many. */
typedef struct { float x, y; } game_spot;
int Game_BrokenWindows(int episode, int map, game_spot *spots, int max);

/* Duct tape, not authored (always on): sky sectors of one of the game's own
   maps that are courtyards ringed by porches where the room heuristics
   (rooms.c) read a hall round a hole in its ceiling. Fills at most max
   sector numbers, returns how many. */
int Game_Courtyards(int episode, int map, int *sectors, int max);

/* How clearly water mirrors the room, 0 to 1: 0 is murky (DOOM: stained in
   the water's hue, only bright things punch through), 1 a clear pool that
   mirrors walls too. */
float Game_WaterClarity(void);

/* Where surface gloss comes from: 0 DOOM's palette ramps (greys and blues
   read as metal, greens as slime); 1 each texture's material
   (Game_WallSurface/Game_FlatSurface) and its texels' own colour, for
   palettes laid out differently from DOOM's. */
int Game_SurfaceGloss(void);

/* Wind a sector special blows (Heretic's Wind_East to Wind_West, which push
   things in the game), in map units per second toward east and north;
   0 where none. */
int Game_SectorWind(int special, float *east, float *north);

/* Decorations that light the room although their artwork isn't drawn
   fullbright (Heretic's chandeliers and serpent torches), by sprite number.
   Fullbright decorations light the room anyway. */
int Game_DecorationLight(int sprite);

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
/* How much farther than the game's usual range an effect carries: 1 for
   most, more for the loud ones (gunfire, explosions), heard far off under
   the natural falloff. */
float Game_SoundCarry(int id);
/* Hitscan shots: traced is told of each, from the shooter's muzzle to where
   it stopped, in map units, and what it hit (NULL: a wall, floor or the end
   of its range), for sounds that follow the bullet. NULL stops telling. A
   game without hitscan tracing ignores it. */
typedef void (*game_shot_traced)(const struct mobj_s *shooter, const struct mobj_s *hit,
                                 float x1, float y1, float z1, float x2, float y2, float z2);
void Game_TraceShots(game_shot_traced traced);

/* Physically accurate Phobos (DOOM: p_phobos.h): on asks for it (F4);
   Game_Phobos says whether it applies now (DOOM's first episode, not in
   demos or netgames; a map's DERPINFO can switch it either way). Then things
   off the floor fall at Phobos' gravity, 1.49e-4 units a tic a tic, or the
   map's own (Game_PhobosGravity), and slide on with the real
   momentum of what hit them. A game without it: never. */
void Game_SetPhobos(int on);
/* How much help on Phobos (F4): the player's gear (0 none, 1 thrusters, 2 grav boots
   too), the walking monsters' footing (0 mag boots, 1 the former humans brainless, 2 no
   boots at all), decompression's push (0 off, 1 gentle, 2 real), the guns' recoil. */
void Game_SetPhobosRules(int player, int enemies, int air, int recoil);
int Game_Phobos(void);
/* How much air sector holds, 0 (vacuum) to 1: what carries sound. Without
   Phobos always 1. */
float Game_Air(int sector);
/* A window pane on line, whole or broken, as it is found or breaks: on
   Phobos whole glass holds the air in. */
void Game_Glass(int line, int whole);
/* The openings venting air now (Phobos), at most most of them. */
typedef struct { float x, y, z, dx, dy, speed, width; int sector, line; } game_vent;
int Game_Vents(game_vent *vents, int most);
/* Whether a draft tore thing off its feet and it drifts (Phobos). */
int Game_Adrift(const struct mobj_s *thing);
/* The draft on a monster still in its boots (Phobos): how hard, units a tic a
   tic (0: none), and in way its direction. */
float Game_Draft(const struct mobj_s *thing, float way[3]);
/* A living body tumbling free (Phobos' brainless zombies): its up, between the last
   tic and this one by fraction; 0 for anything else (drawn upright). */
int Game_Tumble(const struct mobj_s *thing, float fraction, float up[3]);
/* How far the player's body has turned his view up since last asked (radians;
   Phobos: his guns' kicks spin him while nothing holds him). */
float Game_LookKick(void);
/* Whether the player's view is over the top, upside down and facing about (Phobos);
   Game_FlipView turns it over (his facing turned about, his turning and strafing
   mirrored to match). */
int Game_ViewFlipped(void);
void Game_FlipView(void);
/* Phobos grav boots: whether the view should be upside down (on a ceiling, or flown
   past the room's middle toward one); the renderer rolls it over about where he looks
   and tells the game once it is past half way (his turning and strafing then reverse). */
int Game_BootsUpside(void);
void Game_BootsRolled(int over);
/* F4's Gameplay changed (Normal, authored episodes, Phobos): the game restarts the
   map in play if the new one plays or looks differently there, keeping what the
   player carries; nonzero when it does. */
int Game_ModeChanged(void);
/* F4's Weather (0 never, 1 authored, 2 rain, 3 snow, 4 nukage rain, 5 sand, 6 ash): what the
   game plays by. Game_NukageRain: whether nukage rain falls on this level as
   a rule (it hurts what lives under the open sky); 0 where the game has no
   such rule, or not now (demos, netgames, Phobos). */
void Game_SetWeather(int choice);
int Game_NukageRain(void);
float Game_PhobosGravity(void);
/* How far the game has carried things in all since the level began by
   stepping them through geometry built alike (DOOM: episode 0.5's library,
   p_library.h), in map units; 0 in a game without it. Whatever smooths
   movement between tics adds what this grew by to where a carried thing was
   (Game_Carried: the player always, and all but the map's fixtures); dz up
   and down (the library's stairs and its chasm, a storey at a time). The
   sum runs on without end (a fall goes on for ever), so it is kept wrapped
   round GAME_SHIFT_WRAP units: take what it grew by with Game_ShiftGrew,
   never the values themselves. */
#define GAME_SHIFT_WRAP 8192.0f
void Game_WorldShift(float *dx, float *dy, float *dz);
static inline float Game_ShiftGrew(float now, float before)
{
    float grew = now - before, turns = grew / GAME_SHIFT_WRAP;
    return grew - GAME_SHIFT_WRAP * (float)(long)(turns + (turns < 0 ? -0.5f : 0.5f));
}
int Game_Carried(const struct mobj_s *thing);
/* Whether the map in play carries things that way (its fixtures, like the
   lamps, standing in for many alike, so they keep no state of their own:
   none are shot out); and a count that grows whenever all round the player
   is made anew there (a new floor, a morning), when what the renderer keeps
   of the world (blood, marks) goes. */
int Game_WorldWraps(void);
int Game_WorldRenewed(void);
/* Whether a sector repeats every storey above and below (the library's
   floors), for the renderer to draw them as copies of the band from bottom
   up a storey; in map units. 0 where it doesn't. */
int Game_Storey(int sector, float *storey, float *bottom);
/* How many floors there are above the player's (the library: none on floor
   1, its top), so no copies are drawn of storeys that don't exist. */
int Game_FloorsAbove(void);
/* And below (the library: none under its last floor). */
int Game_FloorsBelow(void);
/* The library's day (DOOM: p_library.h), for the renderer and the sound:
   how closed the player's eyes are (lids 0-1, tired blinks and falling
   asleep) and how dark it is for him (black 0-1: asleep, passed out, dead),
   nonzero only in the library; the yawns so far (one is due each time it
   grows); and his place at the kiosk last, as a note to copy ("" before). */
int Game_LibraryEyes(float *lids, float *black);
int Game_LibraryYawns(void);
const char *Game_LibraryNote(void);
/* The library's books (DOOM: p_library_books.c): the one the view points at
   (its outline on the shelf, corners clockwise from the top left, in map
   units, and whether it has been read), nonzero when there is one; and the
   open book's page, GAME_PAGE_LINES lines of GAME_PAGE_LINE characters and a
   0 each, with marks laid out alike (1: a letter of a real word, shown in
   red), returning its number from 1 (0: no book open). */
#define GAME_PAGE_LINE 80
#define GAME_PAGE_LINES 40
int Game_LibraryAim(float corners[4][3], int *read);
int Game_LibraryPage(char *lines, char *marks);
/* The library's people on the floors above and below (DOOM:
   p_library_figures.c), to draw on the storeys' copies so every floor has
   someone: each at seconds of game time, x and y as on the player's floor,
   storey floors up from his (negative: down), its thing type's sprite and
   frame. Drawing only: none of them is in the game. Returns how many (at
   most `most`). */
typedef struct { float x, y; unsigned angle; int sprite, frame, type, storey; } game_figure;
/* What one of the library's people says (DOOM: p_library_babel.c), at
   seconds of game time, for the renderer to write over its head; NULL for
   nothing; *shown how much of it shows (0-1: its letters coming and
   going). Drawing only. */
const char *Game_LibrarySays(const struct mobj_s *thing, double seconds, float *shown);
/* The player's ears ringing after hitting an edge in the library's chasm
   (DOOM: p_library_day.c), 0-1, for the sound. */
float Game_LibraryRinging(void);
/* How many times the player has hit an edge in the library's chasm since
   the program started (DOOM: p_library_day.c): a new one is his head's
   crunch. Never saved, so a loaded game doesn't replay it. */
int Game_LibraryKnocks(void);
/* How fast the player falls through the library's chasm (DOOM:
   p_library_shaft.c), map units a tic, 0 when not: the air rushing by.
   Kept by the game each tic, so the sound's thread may read it. */
float Game_LibraryFallSpeed(void);
/* The library's sound (DOOM: p_library_shaft.c, p_library_day.c): whether
   the map is the library, how open the player stands to its chasm (0-1)
   and on which side of him it lies (-1 left .. 1 right as he faces), and
   how tired he is (0-1). Kept by the game each tic: the sound's thread may
   read them. */
int Game_InLibrary(void);
float Game_LibraryChasm(float *side);
float Game_LibraryTired(void);
/* Falling through the library's chasm (DOOM: p_library_shaft.c): nonzero
   while the player falls, his speed in map units a tic. */
int Game_LibraryFall(float speed[3]);
int Game_LibraryFigures(game_figure *out, int most, double seconds);
/* Whether a thing (a figure's body too) lies asleep in one of the
   library's beds (DOOM: p_library_people.c): drawn in its frame with the
   blood left out (images.cpp sleepingImage). */
int Game_ThingAsleep(const struct mobj_s *thing);

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
