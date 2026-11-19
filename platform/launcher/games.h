/* The games the launcher knows, one row each (games.c), and the game
 * programs that play them. Everything the window shows about a game comes
 * from here: its name, program, music packs, the folders it is searched
 * in. Adding a game is a row here plus its program. */
#ifndef DERP_LAUNCHER_GAMES_H
#define DERP_LAUNCHER_GAMES_H
#ifdef __cplusplus
extern "C" {
#endif

/* A game program: the save folder it keeps graphics.cfg in (its Game_Title),
   its file beside the launcher, and the GAME_EFFECT_ bits its Game_HasEffect
   answers yes to (game_profile.h; the launcher links no game). */
typedef struct {
    const char *title;
    const char *program;
    unsigned effects;
} launcher_program;

/* Bits of launcher_game.packs: the recorded soundtracks (PACK_* of launcher.h)
   its program plays in place of the game's own scores (music_pack.c). */
#define GAME_PACK(kind) (1u << (kind))

typedef struct {
    const char *id;         /* launcher.cfg's name for it */
    const char *title;
    const char *family;     /* the name its kind goes by when several rows share one ("Final DOOM"), else NULL */
    int program;            /* Games_Program index, -1: none yet */
    int wad_kind;           /* DOOM_WAD_* from wad_probe.h; -1 for DOOM 64 (no palette) */
    const char *files[6];   /* its game WAD's usual file names */
    const char *reason;     /* why it cannot be played, NULL when it can */
    unsigned packs;         /* GAME_PACK bits: the recorded soundtracks it takes */
    const char *pack_3do;   /* its 3DO pack's file name, when it takes one */
    const char *music;      /* how its songs' lumps are named (a music WAD holds only these) */
    const char *folder;     /* a word in its install folders' names (Steam, GOG, /usr/share/games) */
} launcher_game;

int Games_Count(void);
const launcher_game *Games_Get(int game);
int Games_ProgramCount(void);
const launcher_program *Games_Program(int program);
/* The program's file beside the launcher (from SDL_malloc), or NULL when missing. */
char *Games_ProgramPath(int program);
/* Which row a game WAD belongs to, from its file name and else its
   contents (wad_probe.c); -1 when it is no game WAD. */
int Games_Identify(const char *path);
/* The recorded pack a file is, by its name (PACK_*), or -1. */
int Games_PackKind(const char *path);
/* Whether a row takes that pack file: the kind, and for 3DO the game's own. */
int Games_TakesPack(int game, const char *path);
/* A WAD of songs alone: the DOOM_WAD_* kind whose songs they are, or -1. */
int Games_MusicKind(const char *path);
/* A soundtrack DERP cannot play yet (the Heretic and Hexen re-releases'): why, else NULL. */
const char *Games_UnplayedMusic(const char *path);
/* The PACK bits of every row the program plays. */
unsigned Games_ProgramPacks(int program);
/* The titles of the games a program plays, for a line of text ("A, B and C"). */
void Games_ProgramTitles(int program, char *text, int size);
/* The title of a row's kind of game (DOOM_WAD_*), for messages. */
const char *Games_KindTitle(int wad_kind);

#ifdef __cplusplus
}
#endif
#endif
