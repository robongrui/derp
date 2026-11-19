/* The games list and the Play page (launcher_ui.h):
 *   left   every game WAD found, grouped by game; unplayable ones greyed with the reason
 *   right  the selected game WAD, its custom WADs in load order, its music,
 *          SoundFont and extra options, and Play */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL3/SDL.h>
#include "launcher_ui.h"
#include "launcher_font.h"
#include "games.h"

#define ROW 52
#define ADDON_ROW 36
#define ADDON_ROWS 6

static const launcher_state *sorting;

/* Playable first, then by the table's order, then by path. */
static int compare_rows(const void *a, const void *b)
{
    const launcher_wad *x = &sorting->wads[*(const int *)a], *y = &sorting->wads[*(const int *)b];
    const launcher_game *gx = Games_Get(x->game), *gy = Games_Get(y->game);
    int px = !gx || gx->reason, py = !gy || gy->reason;
    if (px != py) return px - py;
    if (x->game != y->game) return x->game - y->game;
    return SDL_strcasecmp(x->path, y->path);
}

static const char *wad_title(const launcher_wad *wad)
{
    const launcher_game *game = Games_Get(wad->game);
    if (!game) return Launcher_FileName(wad->path);
    /* The rows for games not in the table say which file they are. */
    if (!strncmp(game->id, "other", 5)) return Launcher_FileName(wad->path);
    return game->title;
}

void PlayPage_DrawGames(launcher_window *window, SDL_FRect area)
{
    ui_state *ui = &window->ui;
    launcher_state *state = window->state;
    UI_Heading(ui, area.x, area.y, "GAMES");
    int count = state->wad_count;
    int *order = count ? SDL_malloc((size_t)count * sizeof(int)) : NULL;
    for (int i = 0; i < count; ++i) order[i] = i;
    sorting = state;
    if (count) qsort(order, (size_t)count, sizeof(int), compare_rows);

    SDL_FRect list = { area.x, area.y + 28, area.w, area.h - 28 - 100 };
    if (!count) Font_DrawWrapped(ui->renderer, list.x, list.y + 8, "No game WAD found yet. Drop one on this window, or press Add game WAD.", 2, UI_DIM, list.w, 6);
    /* A wheel notch goes two games; the Play page's own list for Page Up / Down. */
    UI_ListBegin(ui, &window->games_list, list, 2 * (ROW + 4), window->page == PAGE_PLAY);
    SDL_Rect clip = { (int)list.x - 4, (int)list.y, (int)list.w + 8, (int)list.h };
    SDL_SetRenderClipRect(ui->renderer, &clip);
    ui->clipped = 1;
    ui->clip = list;
    char detail[LAUNCHER_PATH + 32];
    for (int k = 0; k < count; ++k)
    {
        launcher_wad *wad = &state->wads[order[k]];
        const launcher_game *game = Games_Get(wad->game);
        SDL_FRect row = { list.x, list.y + k * (ROW + 4) - window->games_list.scroll, list.w, ROW };
        int flags = 0;
        if (order[k] == state->selected) flags |= UI_SELECTED;
        if (!game || game->reason) flags |= UI_GREY;
        /* Notes at the end: a long path loses its start. */
        if (wad->missing) { flags |= UI_WARN; snprintf(detail, sizeof(detail), "%s (missing)", wad->path); }
        else if (game && game->reason) snprintf(detail, sizeof(detail), "%s (not playable yet)", Launcher_FileName(wad->path));
        else if (wad->shareware) snprintf(detail, sizeof(detail), "%s (shareware)", wad->path);
        else snprintf(detail, sizeof(detail), "%s", wad->path);
        int pressed_by_key = ui->activate;
        int result = UI_Row(ui, row, wad_title(wad), detail, flags);
        if (result)
        {
            if (state->selected != order[k])
            {
                int had = state->selected >= 0;
                state->selected = order[k];
                if (!had) LoadList_TakeLoose(state);
                window->addons_list.scroll = window->addons_list.drawn = 0;
            }
            if (result == 1 && pressed_by_key) PlayPage_Play(window);
        }
    }
    ui->clipped = 0;
    SDL_SetRenderClipRect(ui->renderer, NULL);
    UI_ListEnd(ui, &window->games_list, list, count * (ROW + 4.0f));
    SDL_free(order);

    SDL_FRect add = { area.x, area.y + area.h - 92, area.w, 40 };
    if (UI_Button(ui, add, "+ Add game WAD", 0)) LauncherUI_Dialog(window, DIALOG_ADD_GAME);
    SDL_FRect folder = { area.x, area.y + area.h - 46, area.w, 40 };
    if (UI_Button(ui, folder, "+ Add game folder", 0)) LauncherUI_Dialog(window, DIALOG_GAME_FOLDER);
}

/* The music choices offered: the game's own, the packs its row takes that were found (games.c),
   a music WAD if one was given. */
static int music_offered(const launcher_state *state, const launcher_wad *wad, int music)
{
    switch (music)
    {
    case MUSIC_OWN: return 1;
    case MUSIC_PSX: return Launcher_PackFor(state, PACK_PSX, wad) != NULL;
    case MUSIC_3DO: return Launcher_PackFor(state, PACK_3DO, wad) != NULL;
    case MUSIC_IDKFA: case MUSIC_ORIGINAL: return Launcher_PackFor(state, PACK_EXTRAS, wad) != NULL;
    case MUSIC_WAD: return wad->music_wad[0] != 0;
    }
    return 0;
}

static const char *music_title(int music)
{
    static const char *titles[MUSIC_CHOICES] = { "The game's own", "PlayStation", "3DO", "IDKFA remix (re-release)",
                                                 "Original, recorded (re-release)", "Music WAD" };
    return titles[music];
}

static const char *music_detail(const launcher_state *state, const launcher_wad *wad)
{
    static char text[LAUNCHER_PATH + 64];
    const char *path = NULL;
    switch (wad->music)
    {
    case MUSIC_PSX: path = Launcher_PackFor(state, PACK_PSX, wad); break;
    case MUSIC_3DO: path = Launcher_PackFor(state, PACK_3DO, wad); break;
    case MUSIC_IDKFA: case MUSIC_ORIGINAL: path = Launcher_PackFor(state, PACK_EXTRAS, wad); break;
    case MUSIC_WAD: path = wad->music_wad; break;
    }
    if (path) return path;
    const launcher_game *game = state->selected >= 0 ? Games_Get(wad->game) : NULL;
    if (!game || game->reason) return "";
    int packs = 0;
    for (int m = MUSIC_PSX; m <= MUSIC_ORIGINAL; ++m) packs += music_offered(state, wad, m);
    if (packs) return "Recorded packs found: pick one with left and right.";
    /* The packs this game takes, by their file names, else only a music WAD of its own songs. */
    char names[256] = "";
    if (game->packs & GAME_PACK(PACK_PSX)) SDL_strlcat(names, "HQPSXMUS.WAD, ", sizeof(names));
    if ((game->packs & GAME_PACK(PACK_3DO)) && game->pack_3do)
    {
        SDL_strlcat(names, game->pack_3do, sizeof(names));
        SDL_strlcat(names, ", ", sizeof(names));
    }
    if (game->packs & GAME_PACK(PACK_EXTRAS)) SDL_strlcat(names, "extras.wad, ", sizeof(names));
    if (names[0]) snprintf(text, sizeof(text), "Drop %sor a music WAD here to play it.", names);
    else snprintf(text, sizeof(text), "Drop a music WAD of %s's songs here to play it.", game->title);
    return text;
}

static void draw_addons(launcher_window *window, launcher_wad *wad, SDL_FRect area)
{
    ui_state *ui = &window->ui;
    launcher_state *state = window->state;
    UI_Heading(ui, area.x, area.y, "CUSTOM WADS");
    Font_Draw(ui->renderer, area.x + 200, area.y + 4, "(loads top down)", 1, UI_DIM);
    SDL_FRect list = { area.x, area.y + 26, area.w, ADDON_ROWS * ADDON_ROW };
    if (!wad->addon_count)
        UI_Label(ui, list.x + 12, list.y + 10, "None: the game as it comes.", 2, UI_FAINT);
    UI_ListBegin(ui, &window->addons_list, list, ADDON_ROW, 0);
    SDL_Rect clip = { (int)list.x - 4, (int)list.y, (int)list.w + 8, (int)list.h };
    SDL_SetRenderClipRect(ui->renderer, &clip);
    ui->clipped = 1;
    ui->clip = list;
    int remove = -1, up = -1, down = -1;
    char text[LAUNCHER_PATH + 16];
    for (int i = 0; i < wad->addon_count; ++i)
    {
        launcher_addon *addon = &wad->addons[i];
        float y = list.y + i * ADDON_ROW - window->addons_list.scroll;
        const char *note = LoadList_Note(state, wad, i);
        int warn = note[0] && strcmp(note, "patch");
        snprintf(text, sizeof(text), "%d  %s", i + 1, Launcher_FileName(addon->path));
        SDL_FRect row = { list.x, y, list.w - 3 * 44, ADDON_ROW - 4 };
        int id = UI_NextId(ui);
        UI_Row(ui, row, text, NULL, warn ? UI_WARN : 0);
        if (note[0]) Font_Draw(ui->renderer, row.x + row.w - Font_Width(note, 1) - 10, row.y + 12, note, 1, warn ? UI_WARN_COLOR : UI_DIM);
        if (UI_Focused(ui, id))
        {
            if (ui->remove) remove = i;
            ui->focus_tip = addon->path;
        }
        SDL_FRect button = { row.x + row.w + 4, y, 40, ADDON_ROW - 4 };
        if (UI_Button(ui, button, "^", i ? 0 : UI_DISABLED)) up = i;
        button.x += 44;
        if (UI_Button(ui, button, "v", i + 1 < wad->addon_count ? 0 : UI_DISABLED)) down = i;
        button.x += 44;
        if (UI_Button(ui, button, "x", 0) || (UI_Focused(ui, id + 3) && ui->remove)) remove = i;
    }
    ui->clipped = 0;
    SDL_SetRenderClipRect(ui->renderer, NULL);
    UI_ListEnd(ui, &window->addons_list, list, wad->addon_count * (float)ADDON_ROW);
    if (up >= 0) Launcher_MoveAddon(wad, up, -1);
    if (down >= 0) Launcher_MoveAddon(wad, down, 1);
    if (remove >= 0)
    {
        Launcher_Status(state, 0, "Removed %s from the list.", Launcher_FileName(wad->addons[remove].path));
        Launcher_RemoveAddon(wad, remove);
    }

    float y = list.y + list.h + 8;
    SDL_FRect add = { area.x, y, 200, 40 };
    if (UI_Button(ui, add, "+ Add files", 0)) LauncherUI_Dialog(window, DIALOG_ADD_FILES);
    SDL_FRect clear = { area.x + 212, y, 120, 40 };
    if (UI_Button(ui, clear, "Clear", wad->addon_count ? 0 : UI_DISABLED))
    {
        Launcher_ClearAddons(wad);
        Launcher_Status(state, 0, "The list is empty: the game as it comes.");
    }
    Font_Draw(ui->renderer, area.x + 348, y + 12, "or drop files anywhere here", 2, UI_DIM);
}

static void draw_notice(launcher_window *window, SDL_FRect area)
{
    ui_state *ui = &window->ui;
    launcher_state *state = window->state;
    UI_Fill(ui, area, (SDL_Color){ 64, 40, 24, 255 });
    UI_Frame(ui, area, UI_WARN_COLOR);
    Font_DrawWrapped(ui->renderer, area.x + 12, area.y + 10, window->check.text, 2, UI_TEXT, area.w - 24, 2);
    float y = area.y + area.h - 48;
    SDL_FRect yes = { area.x + 12, y, 300, 40 }, no = { area.x + 324, y, 140, 40 };
    if (window->focus_notice) { ui->focus = UI_NextId(ui); window->focus_notice = 0; }
    if (window->check.problem == CHECK_MISSING)
    {
        if (UI_Button(ui, yes, "Play without them", UI_ACCENT)) { window->notice = 0; window->play = 1; }
    }
    else if (window->check.problem == CHECK_WRONG_MAPS && window->check.other_wad >= 0)
    {
        char label[64];
        snprintf(label, sizeof(label), "Switch to %s", wad_title(&state->wads[window->check.other_wad]));
        if (UI_Button(ui, yes, label, UI_ACCENT))
        {
            LoadList_MoveTo(state, window->check.other_wad);
            window->notice = 0;
            window->focus_play = 1;
            Launcher_Status(state, 0, "Switched to %s, with the same files.", wad_title(Launcher_Current(state)));
        }
    }
    if (UI_Button(ui, no, "Cancel", 0)) { window->notice = 0; window->focus_play = 1; }
}

void PlayPage_Play(launcher_window *window)
{
    LoadList_Check(window->state, &window->check);
    switch (window->check.problem)
    {
    case CHECK_OK: window->play = 1; return;
    case CHECK_MISSING: window->notice = 1; window->focus_notice = 1; return;
    case CHECK_WRONG_MAPS:
        if (window->check.other_wad >= 0) { window->notice = 1; window->focus_notice = 1; return; }
        break;
    }
    Launcher_Status(window->state, 1, "%s", window->check.text);
}

void PlayPage_Draw(launcher_window *window, SDL_FRect area)
{
    ui_state *ui = &window->ui;
    launcher_state *state = window->state;
    launcher_wad *wad = Launcher_Current(state);
    const launcher_game *game = state->selected >= 0 ? Games_Get(wad->game) : NULL;

    /* The game WAD. */
    Font_DrawFitted(ui->renderer, area.x, area.y, game ? wad_title(wad) : "No game WAD chosen", 3, UI_TEXT, area.w - 160, 0);
    if (game)
        Font_DrawFitted(ui->renderer, area.x, area.y + 34, wad->path, 1, wad->missing ? UI_WARN_COLOR : UI_DIM, area.w - 160, 1);
    if (game && game->reason) Font_Draw(ui->renderer, area.x, area.y + 48, game->reason, 2, UI_WARN_COLOR);
    SDL_FRect change = { area.x + area.w - 140, area.y - 4, 140, 40 };
    if (UI_Button(ui, change, game ? "Change" : "Add", 0))
        LauncherUI_Dialog(window, game ? DIALOG_CHANGE_GAME : DIALOG_ADD_GAME);

    SDL_FRect addons = { area.x, area.y + 76, area.w, 26 + ADDON_ROWS * ADDON_ROW + 56 };
    draw_addons(window, wad, addons);

    /* Music and SoundFont. */
    float y = addons.y + addons.h + 12;
    UI_Heading(ui, area.x, y, "MUSIC");
    y += 26;
    SDL_FRect music = { area.x, y, area.w, 36 };
    if (!music_offered(state, wad, wad->music)) wad->music = MUSIC_OWN;
    int step = UI_Choice(ui, music, "Music", music_title(wad->music), area.x + 200, 0);
    if (step)
    {
        int next = wad->music;
        for (int i = 0; i < MUSIC_CHOICES; ++i)
        {
            next = (next + step + MUSIC_CHOICES) % MUSIC_CHOICES;
            if (music_offered(state, wad, next)) break;
        }
        wad->music = next;
        wad->music_changed = 1;
    }
    Font_DrawFitted(ui->renderer, area.x + 10, y + 40, music_detail(state, wad), 1, UI_DIM, area.w - 20, 1);
    y += 56;
    SDL_FRect font = { area.x, y, area.w - 2 * 132, 36 };
    const char *bank = wad->soundfont[0] ? Launcher_FileName(wad->soundfont) : "The game's own (GeneralUser GS or F4's pick)";
    char line[LAUNCHER_PATH + 16];
    snprintf(line, sizeof(line), "SoundFont: %s", bank);
    int font_id = UI_NextId(ui);
    if (UI_Row(ui, font, line, NULL, wad->soundfont[0] && !Launcher_FileExists(wad->soundfont) ? UI_WARN : 0) == 1)
        LauncherUI_Dialog(window, DIALOG_SOUNDFONT);
    if (UI_Focused(ui, font_id)) ui->focus_tip = wad->soundfont[0] ? wad->soundfont : "Drop an .sf2 here, or press Change.";
    SDL_FRect button = { font.x + font.w + 8, y, 124, 36 };
    if (UI_Button(ui, button, "Change", 0)) LauncherUI_Dialog(window, DIALOG_SOUNDFONT);
    button.x += 132;
    if (UI_Button(ui, button, "Default", wad->soundfont[0] ? 0 : UI_DISABLED)) wad->soundfont[0] = 0;

    /* Extra options and Play. */
    y += 52;
    UI_Label(ui, area.x, y + 10, "Extra options", 2, UI_DIM);
    SDL_FRect extra = { area.x + 230, y, area.w - 230, 38 };
    int extra_id = UI_NextId(ui);
    UI_TextField(ui, extra, wad->extra, sizeof(wad->extra), 0);
    if (UI_Focused(ui, extra_id)) ui->focus_tip = "Passed to the game as typed, such as -warp 1 3 -skill 4. Enter to edit.";

    SDL_FRect play = { area.x + area.w - 260, area.y + area.h - 72, 260, 72 };
    if (window->notice)
    {
        SDL_FRect notice = { area.x, area.y + area.h - 120, area.w - 280, 120 };
        draw_notice(window, notice);
    }
    else if (ui->focus_tip)
        Font_DrawWrapped(ui->renderer, area.x, area.y + area.h - 60, ui->focus_tip, 1, UI_DIM, area.w - 290, 5);
    int play_flags = UI_ACCENT | (game && !game->reason ? 0 : UI_DISABLED);
    if (window->focus_play || ui->focus < 0) { ui->focus = UI_NextId(ui); window->focus_play = 0; }
    int play_id = UI_NextId(ui);
    if (UI_Button(ui, play, "PLAY", play_flags) && !window->notice) PlayPage_Play(window);
    (void)play_id;
}
