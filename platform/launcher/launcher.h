/* The universal launcher (PLAN-launcher.md): what it knows about the game
 * WADs found, the files loaded with each, and the music. The window
 * (launcher_ui.c) edits this; launcher_cfg.c keeps it in launcher.cfg;
 * start_game.c turns the selected game WAD into a game program's arguments. */
#ifndef DERP_LAUNCHER_H
#define DERP_LAUNCHER_H
#ifdef __cplusplus
extern "C" {
#endif

#define LAUNCHER_PATH 1024
#define LAUNCHER_EXTRA 512

/* A custom file in a load list: a WAD (-file) or a DEHACKED patch (-deh). */
enum { ADDON_WAD, ADDON_PATCH };
typedef struct {
    char path[LAUNCHER_PATH];
    int kind;
    /* From its directory (LoadList_Probe): */
    int missing, numbered_maps, episode_maps, game;
} launcher_addon;

/* What plays the music: the game's own scores, a recorded pack (the F4
   Soundtrack values 1-4, music_pack.c), or a music-only WAD loaded last. */
enum { MUSIC_OWN, MUSIC_PSX, MUSIC_3DO, MUSIC_IDKFA, MUSIC_ORIGINAL, MUSIC_WAD, MUSIC_CHOICES };

typedef struct {
    char path[LAUNCHER_PATH];
    int game;          /* games.c row */
    int missing;       /* not there any more */
    int shareware;     /* DOOM's or Heretic's first episode alone: no custom WADs */
    int numbered_maps; /* MAP01 style (DOOM II) */
    int episode_maps;  /* E1M1 style (DOOM, Heretic) */
    int found;         /* found by this start's search, not only remembered */
    int music_changed; /* the music was picked this session: it goes to F4's Soundtrack at Play */
    /* Remembered for this game WAD: */
    launcher_addon *addons;
    int addon_count, addon_capacity;
    int music;
    char music_wad[LAUNCHER_PATH];
    char soundfont[LAUNCHER_PATH]; /* empty: the game's own choice */
    char extra[LAUNCHER_EXTRA];    /* more options, as typed */
} launcher_wad;

/* A recorded soundtrack pack: PACK_* and where it is. */
enum { PACK_PSX, PACK_3DO, PACK_EXTRAS, PACK_KINDS };
typedef struct {
    int kind;
    char path[LAUNCHER_PATH];
} launcher_pack;

#define LAUNCHER_PROGRAMS 4
typedef struct {
    launcher_wad *wads;
    int wad_count, wad_capacity;
    int selected;               /* index into wads, -1 for none */
    launcher_wad loose;         /* files added while no game WAD is selected */
    launcher_pack *packs;
    int pack_count, pack_capacity;
    char **folders;             /* folders the player added */
    int folder_count;
    int fullscreen[LAUNCHER_PROGRAMS]; /* per game program (games.h) */
    char status[256];           /* the one line of news at the bottom */
    int status_error;
} launcher_state;

/* The selected game WAD's record, or the loose one with none selected. */
launcher_wad *Launcher_Current(launcher_state *state);
/* Adds a game WAD (or returns the one already listed under that path). */
int Launcher_AddWad(launcher_state *state, const char *path, int game);
int Launcher_FindWad(const launcher_state *state, const char *path);
void Launcher_AddAddon(launcher_wad *wad, const char *path, int kind);
void Launcher_RemoveAddon(launcher_wad *wad, int index);
void Launcher_MoveAddon(launcher_wad *wad, int index, int direction);
void Launcher_ClearAddons(launcher_wad *wad);
void Launcher_AddPack(launcher_state *state, int kind, const char *path);
/* The pack of that kind for a game WAD: 3DO packs are one per game. NULL: none. */
const char *Launcher_PackFor(const launcher_state *state, int kind, const launcher_wad *wad);
void Launcher_AddFolder(launcher_state *state, const char *path);
void Launcher_Status(launcher_state *state, int error, const char *format, ...);
void Launcher_Free(launcher_state *state);

/* Paths */
const char *Launcher_FileName(const char *path);
int Launcher_SamePath(const char *a, const char *b);
int Launcher_FileExists(const char *path);
int Launcher_IsFolder(const char *path);
/* The folder this program runs from, with a separator at the end (from SDL_malloc). */
char *Launcher_ProgramFolder(void);
/* A file name's extension compared without case ("wad", no dot). */
int Launcher_HasExtension(const char *path, const char *extension);
/* Windows: one kind of separator, so paths read and compare alike. */
void Launcher_CleanPath(char *path);

#ifdef __cplusplus
}
#endif
#endif
