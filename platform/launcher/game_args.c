/* Building a game program's command line (game_args.h). */
#include <stdio.h>
#include <string.h>
#include <SDL3/SDL.h>
#include "game_args.h"
#include "games.h"

static void add(game_args *args, const char *arg)
{
    char **argv = SDL_realloc(args->argv, (size_t)(args->argc + 2) * sizeof(*argv));
    if (!argv) return;
    args->argv = argv;
    args->argv[args->argc++] = SDL_strdup(arg);
    args->argv[args->argc] = NULL;
}

void GameArgs_AddTyped(game_args *args, const char *text)
{
    char word[LAUNCHER_EXTRA];
    const char *p = text;
    while (*p)
    {
        while (*p == ' ' || *p == '\t') ++p;
        if (!*p) break;
        size_t length = 0;
        int quoted = 0;
        for (; *p && (quoted || (*p != ' ' && *p != '\t')); ++p)
        {
            if (*p == '"') { quoted = !quoted; continue; }
            if (length + 1 < sizeof(word)) word[length++] = *p;
        }
        word[length] = 0;
        add(args, word);
    }
}

int GameArgs_Build(const launcher_state *state, game_args *args)
{
    memset(args, 0, sizeof(*args));
    if (state->selected < 0 || state->selected >= state->wad_count) return 0;
    const launcher_wad *wad = &state->wads[state->selected];
    const launcher_game *game = Games_Get(wad->game);
    if (!game || game->program < 0) return 0;
    char *program = Games_ProgramPath(game->program);
    if (!program)
    {
        /* Still build the line (the tests, the log): the program's own name. */
        program = SDL_strdup(Games_Program(game->program)->program);
    }
    args->program = game->program;
    add(args, program);
    SDL_free(program);
    add(args, "-iwad");
    add(args, wad->path);

    int files = 0;
    for (int i = 0; i < wad->addon_count; ++i)
    {
        const launcher_addon *addon = &wad->addons[i];
        if (addon->kind != ADDON_WAD || !Launcher_FileExists(addon->path)) continue;
        if (!files++) add(args, "-file");
        add(args, addon->path);
    }
    /* A music WAD last, so its songs replace the others'. */
    int music = wad->music;
    if (music == MUSIC_WAD && !Launcher_FileExists(wad->music_wad)) music = MUSIC_OWN;
    if (music == MUSIC_WAD)
    {
        if (!files++) add(args, "-file");
        add(args, wad->music_wad);
    }
    int patches = 0;
    for (int i = 0; i < wad->addon_count; ++i)
    {
        const launcher_addon *addon = &wad->addons[i];
        if (addon->kind != ADDON_PATCH || !Launcher_FileExists(addon->path)) continue;
        if (!patches++) add(args, "-deh");
        add(args, addon->path);
    }

    args->soundtrack = 0;
    static const struct { int music, pack; const char *option; } packs[] = {
        { MUSIC_PSX, PACK_PSX, "-psxmusic" }, { MUSIC_3DO, PACK_3DO, "-3domusic" },
        { MUSIC_IDKFA, PACK_EXTRAS, "-extrasmusic" }, { MUSIC_ORIGINAL, PACK_EXTRAS, "-extrasmusic" },
    };
    for (size_t i = 0; i < SDL_arraysize(packs); ++i)
    {
        if (packs[i].music != music) continue;
        const char *pack = Launcher_PackFor(state, packs[i].pack, wad);
        if (!pack) break;
        add(args, packs[i].option);
        add(args, pack);
        args->soundtrack = music; /* MUSIC_PSX..MUSIC_ORIGINAL are the Soundtrack values 1-4 */
    }
    if (wad->soundfont[0] && Launcher_FileExists(wad->soundfont))
    {
        add(args, "-soundfont");
        add(args, wad->soundfont);
    }
    if (game->program < LAUNCHER_PROGRAMS && state->fullscreen[game->program]) add(args, "-fullscreen");
    GameArgs_AddTyped(args, wad->extra);
    return 1;
}

void GameArgs_Free(game_args *args)
{
    for (int i = 0; i < args->argc; ++i) SDL_free(args->argv[i]);
    SDL_free(args->argv);
    memset(args, 0, sizeof(*args));
}

char *GameArgs_Line(const game_args *args)
{
    size_t size = 1;
    for (int i = 0; i < args->argc; ++i) size += strlen(args->argv[i]) + 3;
    char *line = SDL_malloc(size), *out = line;
    if (!line) return NULL;
    for (int i = 0; i < args->argc; ++i)
    {
        int quote = strchr(args->argv[i], ' ') != NULL;
        out += SDL_snprintf(out, size - (size_t)(out - line), "%s%s%s%s", i ? " " : "", quote ? "\"" : "", args->argv[i], quote ? "\"" : "");
    }
    *out = 0;
    return line;
}
