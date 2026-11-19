/* The launcher's sorting of files into slots, the checks before Play, the
 * game's command line and launcher.cfg (platform/launcher). Works in a
 * folder of its own under the system's temporary folder. */
#include <stdio.h>
#include <string.h>
#include <SDL3/SDL.h>
#include "../platform/launcher/launcher.h"
#include "../platform/launcher/sort_files.h"
#include "../platform/launcher/load_list.h"
#include "../platform/launcher/game_args.h"
#include "../platform/launcher/launcher_cfg.h"
#include "../platform/launcher/games.h"
#include "../platform/launcher/scroll_list.h"
#include "../platform/launcher/settings_page.h"

int LauncherTests_ConfigVersion(void); /* launcher_settings.cpp */

#define CHECK(test) do { if (!(test)) { fprintf(stderr, "Failed at line %d: %s\n", __LINE__, #test); return 1; } } while (0)

/* A scrolled list: 200 rows of 38 under a view of 400. */
static int scroll_checks(void)
{
    const float row = 38, view = 400, content = 200 * row;
    CHECK(Scroll_Clamp(-50, content, view) == 0);
    CHECK(Scroll_Clamp(1e6f, content, view) == content - view); /* the last row's bottom at the view's bottom */
    CHECK(Scroll_Clamp(30, 300, view) == 0);                    /* shorter than the view: no scrolling */
    /* A row below the view comes up to its bottom, above it down to its top, inside stays put. */
    CHECK(Scroll_Reveal(0, 20 * row, row, view) == 21 * row - view);
    CHECK(Scroll_Reveal(1000, 10 * row, row, view) == 10 * row);
    CHECK(Scroll_Reveal(1000, 1000 + row, row, view) == 1000);
    /* The knob runs the whole track: at the top at 0, its end at the track's end at the bottom. */
    float offset, size;
    Scroll_Knob(0, content, view, &offset, &size);
    CHECK(offset == 0 && size >= SCROLL_KNOB_LEAST && size < view);
    Scroll_Knob(content - view, content, view, &offset, &size);
    CHECK(SDL_fabsf(offset + size - view) < 0.01f);
    Scroll_Knob(1e6f, content, view, &offset, &size);
    CHECK(offset + size <= view + 0.01f);
    /* Dragging the knob back gives the scroll it showed; past the ends it stops there. */
    Scroll_Knob(1234, content, view, &offset, &size);
    CHECK(SDL_fabsf(Scroll_FromKnob(offset, content, view) - 1234) < 0.5f);
    CHECK(Scroll_FromKnob(-40, content, view) == 0);
    CHECK(Scroll_FromKnob(view, content, view) == content - view);
    /* Page Down / Up go about a view on, at least one row; Home and End to the ends. */
    float tops[200];
    for (int i = 0; i < 200; ++i) tops[i] = i * row;
    float page = view - row;
    int down = Scroll_PageTarget(tops, 200, 0, page, 1);
    CHECK(down > 1 && tops[down] <= page && tops[down + 1] > page);
    CHECK(Scroll_PageTarget(tops, 200, down, page, -1) == 0);
    CHECK(Scroll_PageTarget(tops, 200, 199, page, 1) == 199);
    CHECK(Scroll_PageTarget(tops, 200, 0, page, -1) == 0);
    CHECK(Scroll_PageTarget(tops, 200, 57, page, 2) == 199 && Scroll_PageTarget(tops, 200, 57, page, -2) == 0);
    CHECK(Scroll_PageTarget(tops, 200, -1, page, 1) == 199); /* from outside the list */
    float tall[3] = { 0, 1000, 2000 };                        /* rows taller than a page: still one on */
    CHECK(Scroll_PageTarget(tall, 3, 0, page, 1) == 1);
    return 0;
}

static char folder[1024];

static const char *in(const char *name)
{
    static char paths[16][1024];
    static int next;
    char *path = paths[next++ % 16];
    snprintf(path, 1024, "%s%s", folder, name);
    return path;
}

/* A WAD with the given lumps (names only, no data). */
static void wad(const char *name, const char *type, const char **lumps, int count)
{
    unsigned char bytes[12 + 16 * 16] = { 0 };
    memcpy(bytes, type, 4);
    bytes[4] = (unsigned char)count;
    bytes[8] = 12;
    for (int i = 0; i < count; ++i) memcpy(bytes + 12 + 16 * i + 8, lumps[i], strlen(lumps[i]));
    FILE *file = fopen(in(name), "wb");
    fwrite(bytes, 1, 12 + 16 * (size_t)count, file);
    fclose(file);
}

static void text(const char *name, const char *contents)
{
    FILE *file = fopen(in(name), "wb");
    fputs(contents, file);
    fclose(file);
}

/* Everything follows the game selected: music packs and music WADs, the checks' words, the F4 options. */
static int game_checks(void)
{
    const char *doom2[] = { "PLAYPAL", "MAP01" }, *heretic_lumps[] = { "PLAYPAL", "E1M1", "E2M1", "TINTTAB" };
    const char *heretic_demo[] = { "PLAYPAL", "E1M1", "TINTTAB" }, *map[] = { "MAP01", "THINGS" };
    const char *doom_songs[] = { "D_RUNNIN" }, *heretic_songs[] = { "MUS_E1M1", "MUS_TITL" }, *remix[] = { "H_BLOOD" };
    wad("DOOM2.WAD", "IWAD", doom2, 2);
    wad("HERETIC.WAD", "IWAD", heretic_lumps, 4);
    wad("heretic1.wad", "IWAD", heretic_demo, 3);
    wad("mapsforii.wad", "PWAD", map, 2);
    wad("HQPSXMUS.WAD", "PWAD", doom_songs, 1);
    wad("Doom2_3DO.wad", "PWAD", doom_songs, 1);
    wad("doomsongs.wad", "PWAD", doom_songs, 1);
    wad("hereticsongs.wad", "PWAD", heretic_songs, 2);
    wad("heretic_mus_remix.wad", "PWAD", remix, 1);

    launcher_state state;
    memset(&state, 0, sizeof(state));
    state.selected = -1;
    sort_result result;
    const char *games[] = { in("DOOM2.WAD"), in("HERETIC.WAD") };
    SortFiles(&state, games, 2, 0, &result);
    int doom = Launcher_FindWad(&state, in("DOOM2.WAD")), heretic_wad = Launcher_FindWad(&state, in("HERETIC.WAD"));
    CHECK(state.selected == heretic_wad);
    launcher_wad *heretic = &state.wads[heretic_wad];

    /* A DOOM pack dropped on Heretic: kept for the DOOM games, not Heretic's music. */
    const char *packs[] = { in("HQPSXMUS.WAD"), in("Doom2_3DO.wad") };
    SortFiles(&state, packs, 2, 0, &result);
    CHECK(heretic->music == MUSIC_OWN && result.note[0] && !result.refused && state.pack_count == 2);
    CHECK(!Launcher_PackFor(&state, PACK_PSX, heretic) && !Launcher_PackFor(&state, PACK_3DO, heretic));
    CHECK(Launcher_PackFor(&state, PACK_PSX, &state.wads[doom]) && Launcher_PackFor(&state, PACK_3DO, &state.wads[doom]));
    CHECK(Games_ProgramPacks(Games_Get(heretic->game)->program) == 0);
    /* Its own songs are its music; DOOM's songs and the re-release's recordings are refused. */
    const char *songs[] = { in("doomsongs.wad") }, *own[] = { in("hereticsongs.wad") }, *later[] = { in("heretic_mus_remix.wad") };
    SortFiles(&state, songs, 1, 0, &result);
    CHECK(result.refused == 1 && heretic->music == MUSIC_OWN && strstr(result.refusal, "Heretic"));
    SortFiles(&state, later, 1, 0, &result);
    CHECK(result.refused == 1 && !heretic->addon_count);
    SortFiles(&state, own, 1, 0, &result);
    CHECK(!result.refused && heretic->music == MUSIC_WAD && strstr(heretic->music_wad, "hereticsongs.wad"));
    game_args args;
    CHECK(GameArgs_Build(&state, &args));
    CHECK(args.soundtrack == 0 && !strcmp(args.argv[3], "-file") && strstr(args.argv[4], "hereticsongs.wad"));
    for (int i = 0; i < args.argc; ++i) CHECK(!strstr(args.argv[i], "music") || i == 4);
    GameArgs_Free(&args);
    /* DOOM II takes DOOM's songs, and not Heretic's. */
    state.selected = doom;
    SortFiles(&state, own, 1, 0, &result);
    CHECK(result.refused == 1 && strstr(result.refusal, "DOOM II"));
    SortFiles(&state, songs, 1, 0, &result);
    CHECK(!result.refused && state.wads[doom].music == MUSIC_WAD);

    /* The checks speak of the game selected, never of DOOM or Freedoom on Heretic. */
    state.selected = heretic_wad;
    heretic->music = MUSIC_OWN;
    const char *maps[] = { in("mapsforii.wad") };
    SortFiles(&state, maps, 1, 0, &result);
    launcher_check check;
    LoadList_Check(&state, &check);
    CHECK(check.problem == CHECK_WRONG_MAPS && check.other_wad == doom);
    CHECK(strstr(check.text, "Heretic") && !strstr(check.text, "DOOM") && !strstr(check.text, "Freedoom"));
    Launcher_ClearAddons(heretic);
    const char *demo[] = { in("heretic1.wad"), in("hereticsongs.wad") };
    SortFiles(&state, demo, 1, 0, &result);
    Launcher_AddAddon(Launcher_Current(&state), in("mapsforii.wad"), ADDON_WAD);
    Launcher_Current(&state)->addons[0].numbered_maps = 0; /* a Heretic add-on, as far as levels go */
    LoadList_Check(&state, &check);
    CHECK(check.problem == CHECK_SHAREWARE && strstr(check.text, "Heretic") && !strstr(check.text, "DOOM") && !strstr(check.text, "Freedoom"));
    Launcher_Free(&state);

    /* graphics.cfg of an older release is skipped (configVersion, tests/launcher_settings.cpp). */
    CHECK(LauncherTests_ConfigVersion());

    /* F4's options, as each game has them (its Game_HasEffect, its soundtracks). */
    CHECK(SettingsPage_GameHas(0, "window_glass") && !SettingsPage_GameHas(1, "window_glass"));
    CHECK(SettingsPage_GameHas(0, "soundtrack") && !SettingsPage_GameHas(1, "soundtrack"));
    CHECK(SettingsPage_GameHas(0, "classic_aim") && !SettingsPage_GameHas(1, "classic_aim"));
    CHECK(SettingsPage_GameHas(0, "shootable_lights") && !SettingsPage_GameHas(1, "shootable_lights"));
    CHECK(SettingsPage_GameHas(1, "scale") && SettingsPage_GameHas(1, "presentation") && SettingsPage_GameHas(1, "music_device"));
    char titles[160];
    Games_ProgramTitles(1, titles, sizeof(titles));
    CHECK(!strcmp(titles, "Heretic"));
    Games_ProgramTitles(0, titles, sizeof(titles));
    CHECK(!strcmp(titles, "DOOM, DOOM II, Final DOOM and Freedoom"));

    const char *names[] = { "DOOM2.WAD", "HERETIC.WAD", "heretic1.wad", "mapsforii.wad", "HQPSXMUS.WAD", "Doom2_3DO.wad",
                            "doomsongs.wad", "hereticsongs.wad", "heretic_mus_remix.wad" };
    for (size_t i = 0; i < SDL_arraysize(names); ++i) SDL_RemovePath(in(names[i]));
    return 0;
}

int main(void)
{
    char *temporary = SDL_GetCurrentDirectory();
    snprintf(folder, sizeof(folder), "%slauncher-test/", temporary ? temporary : "./");
    SDL_free(temporary);
    SDL_CreateDirectory(folder);
    if (game_checks()) return 1;

    const char *doom2[] = { "PLAYPAL", "MAP01", "MAP02" }, *doom[] = { "PLAYPAL", "E1M1", "E2M1" }, *shareware[] = { "PLAYPAL", "E1M1" };
    const char *map[] = { "MAP01", "THINGS" }, *episode[] = { "E1M1", "THINGS" }, *songs[] = { "D_RUNNIN", "D_STALKS" };
    const char *heretic[] = { "PLAYPAL", "E1M1", "E2M1", "TINTTAB" }, *hexen_map[] = { "MAP01", "BEHAVIOR" };
    wad("DOOM2.WAD", "IWAD", doom2, 3);
    wad("doom.wad", "IWAD", doom, 3);
    wad("doom1.wad", "IWAD", shareware, 2);
    wad("HERETIC.WAD", "IWAD", heretic, 4);
    wad("hexen.wad", "IWAD", hexen_map, 2);
    wad("eviternity.wad", "PWAD", map, 2);
    wad("sigil.wad", "PWAD", episode, 2);
    wad("hexmaps.wad", "PWAD", hexen_map, 2);
    wad("HQPSXMUS.WAD", "PWAD", songs, 2);
    wad("mymusic.wad", "PWAD", songs, 2);
    text("eviternity.deh", "Patch File for DeHackEd v3.0\n");
    text("bank.sf2", "RIFF");
    text("pack.pk3", "PK");
    text("notes.txt", "hello");

    /* The games table knows them by name and by contents. */
    CHECK(!strcmp(Games_Get(Games_Identify(in("DOOM2.WAD")))->id, "doom2"));
    CHECK(!strcmp(Games_Get(Games_Identify(in("HERETIC.WAD")))->id, "heretic"));
    CHECK(Games_Get(Games_Identify(in("hexen.wad")))->reason != NULL);
    CHECK(Games_Identify(in("eviternity.wad")) == -1);

    /* Dropped on the window: each to its slot. */
    launcher_state state;
    memset(&state, 0, sizeof(state));
    state.selected = -1;
    const char *drop[] = { in("eviternity.wad"), in("DOOM2.WAD"), in("HQPSXMUS.WAD"), in("bank.sf2"), in("pack.pk3"), in("notes.txt") };
    sort_result result;
    SortFiles(&state, drop, 6, 0, &result);
    CHECK(result.game_wads == 1 && state.selected >= 0);
    launcher_wad *current = Launcher_Current(&state);
    CHECK(!strcmp(Games_Get(current->game)->id, "doom2"));
    CHECK(current->addon_count == 2); /* eviternity.wad and its .deh */
    CHECK(current->addons[0].kind == ADDON_WAD && current->addons[1].kind == ADDON_PATCH);
    CHECK(current->music == MUSIC_PSX && state.pack_count == 1);
    CHECK(strstr(current->soundfont, "bank.sf2"));
    CHECK(result.refused == 2);
    launcher_check check;
    LoadList_Check(&state, &check);
    /* Passed, but for the game program where it is not beside the test. */
    CHECK(check.problem == CHECK_OK || check.problem == CHECK_NO_PROGRAM);

    /* The command line. */
    SDL_strlcpy(current->extra, "-warp 1 3 -skill \"4\"", sizeof(current->extra));
    game_args args;
    CHECK(GameArgs_Build(&state, &args));
    char *line = GameArgs_Line(&args);
    printf("%s\n", line);
    CHECK(!strcmp(args.argv[1], "-iwad") && strstr(args.argv[2], "DOOM2.WAD"));
    CHECK(!strcmp(args.argv[3], "-file") && strstr(args.argv[4], "eviternity.wad"));
    CHECK(!strcmp(args.argv[5], "-deh") && strstr(args.argv[6], "eviternity.deh"));
    CHECK(!strcmp(args.argv[7], "-psxmusic") && strstr(args.argv[8], "HQPSXMUS.WAD"));
    CHECK(!strcmp(args.argv[9], "-soundfont"));
    CHECK(!strcmp(args.argv[11], "-warp") && !strcmp(args.argv[13], "3") && !strcmp(args.argv[15], "4") && args.argc == 16);
    CHECK(args.soundtrack == 1);
    SDL_free(line);
    GameArgs_Free(&args);

    /* A music WAD goes last in -file. */
    const char *music[] = { in("mymusic.wad") };
    SortFiles(&state, music, 1, 0, &result);
    CHECK(current->music == MUSIC_WAD);
    CHECK(GameArgs_Build(&state, &args));
    CHECK(!strcmp(args.argv[3], "-file") && strstr(args.argv[5], "mymusic.wad") && !strcmp(args.argv[6], "-deh"));
    CHECK(args.soundtrack == 0);
    GameArgs_Free(&args);

    /* DOOM II maps on DOOM: the right base offered. */
    const char *doom_drop[] = { in("doom.wad") };
    SortFiles(&state, doom_drop, 1, 0, &result);
    current = Launcher_Current(&state);
    CHECK(!strcmp(Games_Get(current->game)->id, "doom") && !current->addon_count);
    const char *maps[] = { in("eviternity.wad") };
    SortFiles(&state, maps, 1, 0, &result);
    LoadList_Check(&state, &check);
    CHECK(check.problem == CHECK_WRONG_MAPS && check.other_wad >= 0);
    CHECK(!strcmp(LoadList_Note(&state, current, 0), "MAP01 levels"));
    LoadList_MoveTo(&state, check.other_wad);
    CHECK(!strcmp(Games_Get(Launcher_Current(&state)->game)->id, "doom2"));

    /* On the icon: the files make up the list, on a base that fits them. */
    state.selected = Launcher_FindWad(&state, in("doom.wad"));
    const char *icon[] = { in("eviternity.wad") };
    SortFiles(&state, icon, 1, 1, &result);
    current = Launcher_Current(&state);
    CHECK(!strcmp(Games_Get(current->game)->id, "doom2") && current->addon_count == 2);
    const char *icon2[] = { in("sigil.wad") };
    SortFiles(&state, icon2, 1, 1, &result);
    current = Launcher_Current(&state);
    CHECK(!strcmp(Games_Get(current->game)->id, "doom") && current->addon_count == 1);

    /* Shareware takes no custom WADs; Hexen maps are flagged under DOOM II. */
    const char *small[] = { in("doom1.wad"), in("sigil.wad") };
    SortFiles(&state, small, 2, 0, &result);
    LoadList_Check(&state, &check);
    CHECK(check.problem == CHECK_SHAREWARE);
    state.selected = Launcher_FindWad(&state, in("DOOM2.WAD"));
    const char *other[] = { in("hexmaps.wad") };
    SortFiles(&state, other, 1, 0, &result);
    LoadList_Check(&state, &check);
    CHECK(check.problem == CHECK_OTHER_GAME);
    Launcher_RemoveAddon(Launcher_Current(&state), Launcher_Current(&state)->addon_count - 1);

    /* A folder: its WADs by the same rules. */
    launcher_state fresh;
    memset(&fresh, 0, sizeof(fresh));
    fresh.selected = -1;
    char bare[1024];
    snprintf(bare, sizeof(bare), "%s", folder);
    bare[strlen(bare) - 1] = 0;
    const char *whole[] = { bare };
    SortFiles(&fresh, whole, 1, 0, &result);
    CHECK(fresh.wad_count == 5 && result.game_wads == 5 && fresh.pack_count == 1);
    Launcher_Free(&fresh);

    /* launcher.cfg keeps it all. */
    current = Launcher_Current(&state);
    Launcher_AddFolder(&state, folder);
    state.fullscreen[0] = 1;
    int wads = state.wad_count, addons = current->addon_count, music_choice = current->music;
    CHECK(LauncherCfg_Write(&state, in("launcher.cfg")));
    CHECK(LauncherCfg_Write(&state, in("launcher.cfg"))); /* over the old one */
    launcher_state read;
    memset(&read, 0, sizeof(read));
    read.selected = -1;
    CHECK(LauncherCfg_Read(&read, in("launcher.cfg")));
    CHECK(read.wad_count == wads && read.selected == state.selected && read.folder_count == 1 && read.fullscreen[0] == 1);
    launcher_wad *again = Launcher_Current(&read);
    CHECK(again->addon_count == addons && again->music == music_choice);
    CHECK(!strcmp(again->extra, current->extra) && !strcmp(again->soundfont, current->soundfont));
    CHECK(read.pack_count == state.pack_count);
    Launcher_Free(&read);
    Launcher_Free(&state);

    const char *names[] = { "DOOM2.WAD", "doom.wad", "doom1.wad", "HERETIC.WAD", "hexen.wad", "eviternity.wad", "sigil.wad",
                            "hexmaps.wad", "HQPSXMUS.WAD", "mymusic.wad", "eviternity.deh", "bank.sf2", "pack.pk3", "notes.txt",
                            "launcher.cfg" };
    for (size_t i = 0; i < SDL_arraysize(names); ++i) SDL_RemovePath(in(names[i]));
    SDL_RemovePath(folder);
    if (scroll_checks()) return 1;
    puts("Launcher checks passed");
    return 0;
}
