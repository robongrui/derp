/* The launcher's window (launcher_ui.h): events in, a frame out.
 *   keyboard  arrows move, Enter / Space press, left / right change a choice,
 *             Delete removes, Tab moves on, Ctrl+Tab switches the page,
 *             Page Up / Down and Home / End move through a list,
 *             F5 plays from anywhere, Esc closes
 *   pad       d-pad or left stick moves, A presses, B goes back, X removes,
 *             Y adds files, the shoulders switch the page, the triggers
 *             move a list a page, Start plays
 *   mouse     clicks and the wheel; files dropped anywhere go to their slots */
#include <stdio.h>
#include <string.h>
#include <SDL3/SDL.h>
#include "launcher_ui.h"
#include "launcher_font.h"
#include "settings_page.h"
#include "sort_files.h"
#include "find_wads.h"
#include "games.h"

#define REPEAT_FIRST 350
#define REPEAT_NEXT 90

static Uint32 dialog_event;
const char *LauncherUI_Screenshot;
int LauncherUI_ScreenshotPage;
int LauncherUI_AutoPlay;
const char *LauncherUI_ScreenshotInput;

typedef struct { int purpose, count; char **paths; } dialog_answer;
typedef struct { launcher_window *window; int purpose; } dialog_request;

static void SDLCALL answered(void *data, const char * const *files, int filter)
{
    dialog_request *request = data;
    dialog_answer *answer = SDL_calloc(1, sizeof(*answer));
    (void)filter;
    if (answer)
    {
        answer->purpose = request->purpose;
        for (int i = 0; files && files[i]; ++i) ++answer->count;
        answer->paths = answer->count ? SDL_calloc((size_t)answer->count, sizeof(char *)) : NULL;
        for (int i = 0; i < answer->count && answer->paths; ++i) answer->paths[i] = SDL_strdup(files[i]);
        SDL_Event event = { 0 };
        event.type = dialog_event;
        event.user.data1 = answer;
        if (!SDL_PushEvent(&event)) SDL_free(answer);
    }
    SDL_free(request);
}

void LauncherUI_Dialog(launcher_window *window, int purpose)
{
    static const SDL_DialogFileFilter games[] = { { "Game WADs", "wad;iwad" }, { "All files", "*" } };
    static const SDL_DialogFileFilter files[] = { { "WADs and DEHACKED patches", "wad;deh;bex" }, { "SoundFonts", "sf2" },
                                                  { "All files", "*" } };
    static const SDL_DialogFileFilter fonts[] = { { "SoundFonts", "sf2" } };
    dialog_request *request = SDL_calloc(1, sizeof(*request));
    if (!request) return;
    request->window = window;
    request->purpose = purpose;
    SDL_Window *parent = window->ui.window;
    switch (purpose)
    {
    case DIALOG_ADD_FILES: SDL_ShowOpenFileDialog(answered, request, parent, files, 3, NULL, true); break;
    case DIALOG_ADD_GAME: case DIALOG_CHANGE_GAME: SDL_ShowOpenFileDialog(answered, request, parent, games, 2, NULL, false); break;
    case DIALOG_SOUNDFONT: SDL_ShowOpenFileDialog(answered, request, parent, fonts, 1, NULL, false); break;
    case DIALOG_GAME_FOLDER: SDL_ShowOpenFolderDialog(answered, request, parent, NULL, false); break;
    default: SDL_free(request);
    }
}

static void take_answer(launcher_window *window, dialog_answer *answer)
{
    launcher_state *state = window->state;
    sort_result result;
    if (!answer->count) { }
    else if (answer->purpose == DIALOG_GAME_FOLDER)
    {
        int before = state->wad_count;
        Launcher_AddFolder(state, answer->paths[0]);
        FindWads_In(state, answer->paths[0], 2);
        int found = state->wad_count - before;
        Launcher_Status(state, !found, found ? "Found %d game WAD%s there; the folder is searched at each start."
                        : "No game WADs in that folder, but it is searched at each start from now on.", found, found == 1 ? "" : "s");
    }
    else if (answer->purpose == DIALOG_SOUNDFONT)
    {
        SDL_strlcpy(Launcher_Current(state)->soundfont, answer->paths[0], LAUNCHER_PATH);
        Launcher_Status(state, 0, "SoundFont: %s", Launcher_FileName(answer->paths[0]));
    }
    else if (answer->purpose == DIALOG_CHANGE_GAME && state->selected >= 0)
    {
        int game = Games_Identify(answer->paths[0]);
        int existing = Launcher_FindWad(state, answer->paths[0]);
        if (game < 0) Launcher_Status(state, 1, "%s: not a game WAD the launcher knows.", Launcher_FileName(answer->paths[0]));
        else if (existing >= 0) LoadList_MoveTo(state, existing);
        else
        {
            /* The same record, found at its new place: its list stays. */
            launcher_wad *wad = &state->wads[state->selected];
            launcher_wad kept = *wad;
            int index = Launcher_AddWad(state, answer->paths[0], game);
            if (index >= 0)
            {
                launcher_wad fresh = state->wads[index];
                state->wads[index] = kept;
                state->wads[index].game = fresh.game;
                state->wads[index].missing = 0;
                state->wads[index].shareware = fresh.shareware;
                state->wads[index].numbered_maps = fresh.numbered_maps;
                state->wads[index].episode_maps = fresh.episode_maps;
                state->wads[index].found = 1;
                SDL_strlcpy(state->wads[index].path, fresh.path, LAUNCHER_PATH);
                /* The old record keeps no list of its own now. */
                launcher_wad *old = &state->wads[state->selected];
                old->addons = NULL;
                old->addon_count = old->addon_capacity = 0;
                state->selected = index;
            }
        }
    }
    else
    {
        SortFiles(state, (const char * const *)answer->paths, answer->count, 0, &result);
        SortFiles_Report(state, &result);
    }
    for (int i = 0; i < answer->count; ++i) SDL_free(answer->paths[i]);
    SDL_free(answer->paths);
    SDL_free(answer);
}

/* Files dropped together arrive between a begin and a complete; files opened
   on the Mac Dock icon arrive one by one with no window, so those are
   gathered until none has come for a moment. */
typedef struct { char **paths; int count; int icon; Uint64 last; int open; } drop_batch;

static void drop_add(drop_batch *batch, const char *path)
{
    char **paths = SDL_realloc(batch->paths, (size_t)(batch->count + 1) * sizeof(char *));
    if (!paths) return;
    batch->paths = paths;
    batch->paths[batch->count++] = SDL_strdup(path);
    batch->last = SDL_GetTicks();
}

static void drop_finish(launcher_window *window, drop_batch *batch)
{
    if (batch->count)
    {
        sort_result result;
        SortFiles(window->state, (const char * const *)batch->paths, batch->count, batch->icon, &result);
        SortFiles_Report(window->state, &result);
        if (batch->icon) { window->page = PAGE_PLAY; window->focus_play = 1; }
        for (int i = 0; i < batch->count; ++i) SDL_free(batch->paths[i]);
    }
    SDL_free(batch->paths);
    memset(batch, 0, sizeof(*batch));
}

static void move(launcher_window *window, int nav)
{
    ui_state *ui = &window->ui;
    if ((nav == NAV_LEFT || nav == NAV_RIGHT) && ui->focus_kind == KIND_VALUE) ui->step = nav == NAV_LEFT ? -1 : 1;
    else ui->nav = nav;
}

static void switch_page(launcher_window *window)
{
    if (window->page == PAGE_SETTINGS) SettingsPage_Save();
    window->page = window->page == PAGE_PLAY ? PAGE_SETTINGS : PAGE_PLAY;
    window->ui.focus = -1;
}

/* Back: ends editing, cancels a notice, leaves Settings; on Play it closes. */
static void back(launcher_window *window)
{
    ui_state *ui = &window->ui;
    if (ui->editing) ui->editing = 0;
    else if (window->notice) { window->notice = 0; window->focus_play = 1; }
    else if (window->page == PAGE_SETTINGS) switch_page(window);
    else window->quit = 1;
}

static void key(launcher_window *window, const SDL_KeyboardEvent *event)
{
    ui_state *ui = &window->ui;
    SDL_Keycode code = event->key;
    if (ui->editing)
    {
        if (code == SDLK_RETURN || code == SDLK_KP_ENTER) ui->activate = 1;
        else if (code == SDLK_ESCAPE) ui->editing = 0;
        else if (code == SDLK_BACKSPACE) ++ui->backspace;
        return;
    }
    switch (code)
    {
    case SDLK_UP: move(window, NAV_UP); break;
    case SDLK_DOWN: move(window, NAV_DOWN); break;
    case SDLK_LEFT: move(window, NAV_LEFT); break;
    case SDLK_RIGHT: move(window, NAV_RIGHT); break;
    case SDLK_TAB:
        if (event->mod & SDL_KMOD_CTRL) switch_page(window);
        else ui->nav = event->mod & SDL_KMOD_SHIFT ? NAV_PREVIOUS : NAV_NEXT;
        break;
    case SDLK_RETURN: case SDLK_KP_ENTER: case SDLK_SPACE: ui->activate = 1; break;
    case SDLK_DELETE: ui->remove = 1; break;
    case SDLK_INSERT: LauncherUI_Dialog(window, DIALOG_ADD_FILES); break;
    case SDLK_F5: if (!window->notice) { window->page = PAGE_PLAY; PlayPage_Play(window); } break;
    case SDLK_ESCAPE: back(window); break;
    case SDLK_PAGEUP: ui->page_nav = -1; break;
    case SDLK_PAGEDOWN: ui->page_nav = 1; break;
    case SDLK_HOME: ui->page_nav = -2; break;
    case SDLK_END: ui->page_nav = 2; break;
    }
}

/* The pad: buttons once, directions held repeat. */
typedef struct { int direction; Uint64 next; int stick; } pad_repeat;

static int pad_direction(SDL_GamepadButton button)
{
    switch (button)
    {
    case SDL_GAMEPAD_BUTTON_DPAD_UP: return NAV_UP;
    case SDL_GAMEPAD_BUTTON_DPAD_DOWN: return NAV_DOWN;
    case SDL_GAMEPAD_BUTTON_DPAD_LEFT: return NAV_LEFT;
    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: return NAV_RIGHT;
    default: return 0;
    }
}

static void pad_button(launcher_window *window, pad_repeat *repeat, SDL_GamepadButton button)
{
    int direction = pad_direction(button);
    if (direction)
    {
        move(window, direction);
        repeat->direction = direction;
        repeat->stick = 0;
        repeat->next = SDL_GetTicks() + REPEAT_FIRST;
        return;
    }
    switch (button)
    {
    case SDL_GAMEPAD_BUTTON_SOUTH: window->ui.activate = 1; break;
    case SDL_GAMEPAD_BUTTON_EAST: back(window); break;
    case SDL_GAMEPAD_BUTTON_WEST: window->ui.remove = 1; break;
    case SDL_GAMEPAD_BUTTON_NORTH: LauncherUI_Dialog(window, DIALOG_ADD_FILES); break;
    case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: switch_page(window); break;
    case SDL_GAMEPAD_BUTTON_START: if (!window->notice) { window->page = PAGE_PLAY; PlayPage_Play(window); } break;
    default: break;
    }
}

static void pad_stick(launcher_window *window, pad_repeat *repeat, SDL_Gamepad *pad)
{
    float x = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTX) / 32767.0f;
    float y = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTY) / 32767.0f;
    int direction = 0;
    if (SDL_fabsf(x) > 0.6f || SDL_fabsf(y) > 0.6f)
        direction = SDL_fabsf(x) > SDL_fabsf(y) ? (x < 0 ? NAV_LEFT : NAV_RIGHT) : (y < 0 ? NAV_UP : NAV_DOWN);
    if (!direction) { if (repeat->stick) repeat->direction = 0; return; }
    if (repeat->stick && repeat->direction == direction) return;
    move(window, direction);
    repeat->direction = direction;
    repeat->stick = 1;
    repeat->next = SDL_GetTicks() + REPEAT_FIRST;
}

static void draw(launcher_window *window, int present)
{
    ui_state *ui = &window->ui;
    SDL_Renderer *renderer = ui->renderer;
    SDL_SetRenderDrawColor(renderer, 18, 17, 16, 255);
    SDL_RenderClear(renderer);
    UI_BeginFrame(ui);

    Font_Draw(renderer, 24, 20, "DERP", 4, UI_ACCENT_COLOR);
    SDL_FRect tab = { 340, 16, 160, 44 };
    if (UI_Button(ui, tab, "Play", window->page == PAGE_PLAY ? UI_SELECTED : 0) && window->page != PAGE_PLAY) switch_page(window);
    tab.x += 172;
    if (UI_Button(ui, tab, "Settings", window->page == PAGE_SETTINGS ? UI_SELECTED : 0) && window->page != PAGE_SETTINGS) switch_page(window);
    SDL_FRect underline = { window->page == PAGE_PLAY ? 340.0f : 512.0f, 60, 160, 4 };
    UI_Fill(ui, underline, UI_ACCENT_COLOR);
    Font_Draw(renderer, 860, 34, "Ctrl+Tab / L R: page   F5 / Start: play", 1, UI_FAINT);
    ui->redraw = 0;

    SDL_FRect games = { 16, 84, 300, UI_HEIGHT - 84 - 44 };
    PlayPage_DrawGames(window, games);
    SDL_FRect right = { 340, 84, UI_WIDTH - 340 - 24, UI_HEIGHT - 84 - 44 };
    if (window->page == PAGE_PLAY) PlayPage_Draw(window, right);
    else SettingsPage_Draw(ui, window->state, right);

    SDL_FRect bar = { 0, UI_HEIGHT - 34, UI_WIDTH, 34 };
    UI_Fill(ui, bar, (SDL_Color){ 26, 25, 24, 255 });
    if (window->state->status[0])
        Font_DrawFitted(renderer, 16, UI_HEIGHT - 26, window->state->status, 2,
                        window->state->status_error ? UI_WARN_COLOR : UI_DIM, UI_WIDTH - 32, 0);
    UI_EndFrame(ui);
    if (present) SDL_RenderPresent(renderer);
}

/* Input played into a hidden window before its picture is taken, one step a
   frame or more, apart by spaces (-screenshotinput):
     wheel:N@X,Y   the wheel turned N notches (up positive) with the pointer at X, Y
     key:NAME      up down left right tab enter pageup pagedown home end
     click:X,Y     the left button pressed and let go at X, Y
     drag:X,Y,X2,Y2  pressed at X, Y, moved to X2, Y2, let go
     all           the Settings page shows all settings */
static void play_script(launcher_window *window, const char *script)
{
    static const struct { const char *name; SDL_Keycode key; } keys[] = {
        { "up", SDLK_UP }, { "down", SDLK_DOWN }, { "left", SDLK_LEFT }, { "right", SDLK_RIGHT }, { "tab", SDLK_TAB },
        { "enter", SDLK_RETURN }, { "pageup", SDLK_PAGEUP }, { "pagedown", SDLK_PAGEDOWN }, { "home", SDLK_HOME },
        { "end", SDLK_END },
    };
    ui_state *ui = &window->ui;
    char step[64];
    for (const char *p = script; *p; )
    {
        while (*p == ' ') ++p;
        size_t length = strcspn(p, " ");
        if (!length) break;
        if (length >= sizeof(step)) length = sizeof(step) - 1;
        memcpy(step, p, length);
        step[length] = 0;
        p += length;
        float n, x, y, x2, y2;
        if (sscanf(step, "wheel:%f@%f,%f", &n, &x, &y) == 3)
        {
            ui->mouse_x = x; ui->mouse_y = y; ui->wheel = n;
        }
        else if (sscanf(step, "drag:%f,%f,%f,%f", &x, &y, &x2, &y2) == 4)
        {
            ui->mouse_x = x; ui->mouse_y = y; ui->clicked = 1; ui->mouse_held = 1;
            draw(window, 1);
            ui->mouse_x = x2; ui->mouse_y = y2;
            draw(window, 1);
            ui->mouse_held = 0;
        }
        else if (sscanf(step, "click:%f,%f", &x, &y) == 2)
        {
            ui->mouse_x = x; ui->mouse_y = y; ui->clicked = 1; ui->mouse_held = 1;
            draw(window, 1);
            ui->mouse_held = 0;
        }
        else if (!strcmp(step, "all")) SettingsPage_ShowAll(1);
        else if (!strncmp(step, "key:", 4))
        {
            SDL_KeyboardEvent event = { 0 };
            for (size_t i = 0; i < SDL_arraysize(keys); ++i)
                if (!strcmp(step + 4, keys[i].name)) event.key = keys[i].key;
            if (event.key) key(window, &event);
        }
        draw(window, 1);
        /* A list that scrolled after it was drawn is drawn again, as the window would. */
        for (int i = 0; i < 4 && ui->redraw; ++i) draw(window, 1);
    }
}

int LauncherUI_Run(launcher_state *state, int play_focused)
{
    launcher_window window;
    memset(&window, 0, sizeof(window));
    window.state = state;
    window.ui.focus = -1;
    window.focus_play = 1;
    (void)play_focused;
    if (!dialog_event) dialog_event = SDL_RegisterEvents(1);

    SDL_Window *sdl_window = NULL;
    SDL_Renderer *renderer = NULL;
    SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY | (LauncherUI_Screenshot || LauncherUI_AutoPlay ? SDL_WINDOW_HIDDEN : 0);
    if (!SDL_CreateWindowAndRenderer("DERP", UI_WIDTH, UI_HEIGHT, flags,
                                     &sdl_window, &renderer))
    {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "DERP", SDL_GetError(), NULL);
        return 0;
    }
    SDL_SetWindowMinimumSize(sdl_window, UI_WIDTH / 2, UI_HEIGHT / 2);
    SDL_SetRenderLogicalPresentation(renderer, UI_WIDTH, UI_HEIGHT, SDL_LOGICAL_PRESENTATION_LETTERBOX);
    SDL_SetRenderVSync(renderer, 1);
    /* The Deck's screen is the window's own size: fill it. */
    const char *deck = SDL_getenv("SteamDeck");
    if (deck && !strcmp(deck, "1") && !LauncherUI_Screenshot) SDL_SetWindowFullscreen(sdl_window, true);
    window.ui.renderer = renderer;
    window.ui.window = sdl_window;
    SDL_StopTextInput(sdl_window);

    drop_batch batch = { 0 };
    pad_repeat repeat = { 0 };
    SDL_Gamepad *pads[8] = { 0 };
    int triggers[2] = { 0 };
    if (LauncherUI_Screenshot)
    {
        /* One page drawn into a picture, nothing shown (launcher_main.c -screenshot). */
        window.page = LauncherUI_ScreenshotPage;
        SettingsPage_ReadOnly = 1;
        for (int i = 0; i < 3; ++i) draw(&window, 1);
        if (LauncherUI_ScreenshotInput) play_script(&window, LauncherUI_ScreenshotInput);
        draw(&window, 0);
        SDL_Surface *shot = SDL_RenderReadPixels(renderer, NULL);
        if (shot) { SDL_SavePNG(shot, LauncherUI_Screenshot); SDL_DestroySurface(shot); }
        window.quit = 1;
    }
    if (LauncherUI_AutoPlay)
    {
        /* Play pressed once the window is drawn; a notice or a refusal ends it. */
        draw(&window, 1);
        PlayPage_Play(&window);
        if (!window.play) window.quit = 1;
    }
    while (!window.play && !window.quit)
    {
        SDL_Event event;
        int input = 0;
        /* Wake for the caret's blink and the pad's repeat. */
        if (SDL_WaitEventTimeout(&event, window.ui.redraw ? 0 : repeat.direction ? 16 : 250))
        {
            do
            {
                switch (event.type)
                {
                case SDL_EVENT_QUIT: case SDL_EVENT_WINDOW_CLOSE_REQUESTED: window.quit = 1; break;
                case SDL_EVENT_KEY_DOWN: key(&window, &event.key); input = 1; break;
                case SDL_EVENT_TEXT_INPUT:
                    SDL_strlcat(window.ui.typed, event.text.text, sizeof(window.ui.typed));
                    break;
                case SDL_EVENT_MOUSE_MOTION:
                    SDL_ConvertEventToRenderCoordinates(renderer, &event);
                    window.ui.mouse_x = event.motion.x; window.ui.mouse_y = event.motion.y;
                    break;
                case SDL_EVENT_MOUSE_BUTTON_DOWN:
                    SDL_ConvertEventToRenderCoordinates(renderer, &event);
                    if (event.button.button == SDL_BUTTON_LEFT)
                    {
                        window.ui.mouse_x = event.button.x; window.ui.mouse_y = event.button.y;
                        window.ui.clicked = 1;
                        window.ui.mouse_held = 1;
                        input = 1;
                    }
                    break;
                case SDL_EVENT_MOUSE_BUTTON_UP:
                    if (event.button.button == SDL_BUTTON_LEFT) window.ui.mouse_held = 0;
                    break;
                case SDL_EVENT_MOUSE_WHEEL:
                    /* Lists scroll under the pointer; "natural" scrolling arrives flipped. */
                    SDL_ConvertEventToRenderCoordinates(renderer, &event);
                    window.ui.mouse_x = event.wheel.mouse_x; window.ui.mouse_y = event.wheel.mouse_y;
                    window.ui.wheel += event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -event.wheel.y : event.wheel.y;
                    input = 1;
                    break;
                case SDL_EVENT_DROP_BEGIN:
                    drop_finish(&window, &batch);
                    batch.open = 1;
                    batch.icon = event.drop.windowID == 0;
                    break;
                case SDL_EVENT_DROP_FILE:
                    if (!batch.open && !batch.count) batch.icon = event.drop.windowID == 0;
                    if (event.drop.data) drop_add(&batch, event.drop.data);
                    break;
                case SDL_EVENT_DROP_COMPLETE:
                    drop_finish(&window, &batch);
                    input = 1;
                    break;
                case SDL_EVENT_GAMEPAD_ADDED:
                    for (int i = 0; i < 8; ++i)
                        if (!pads[i]) { pads[i] = SDL_OpenGamepad(event.gdevice.which); break; }
                    break;
                case SDL_EVENT_GAMEPAD_REMOVED:
                    for (int i = 0; i < 8; ++i)
                        if (pads[i] && SDL_GetGamepadID(pads[i]) == event.gdevice.which) { SDL_CloseGamepad(pads[i]); pads[i] = NULL; }
                    break;
                case SDL_EVENT_GAMEPAD_BUTTON_DOWN: pad_button(&window, &repeat, (SDL_GamepadButton)event.gbutton.button); input = 1; break;
                case SDL_EVENT_GAMEPAD_BUTTON_UP:
                    if (pad_direction((SDL_GamepadButton)event.gbutton.button) == repeat.direction && !repeat.stick) repeat.direction = 0;
                    break;
                case SDL_EVENT_GAMEPAD_AXIS_MOTION:
                {
                    SDL_Gamepad *pad = SDL_GetGamepadFromID(event.gaxis.which);
                    /* The triggers page through a list, once per pull. */
                    if (event.gaxis.axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER || event.gaxis.axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER)
                    {
                        int side = event.gaxis.axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER;
                        int pulled = event.gaxis.value > 16000;
                        if (pulled && !triggers[side]) { window.ui.page_nav = side ? 1 : -1; input = 1; }
                        if (pulled || event.gaxis.value < 8000) triggers[side] = pulled;
                    }
                    if (pad && (event.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTX || event.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTY))
                    {
                        int before = window.ui.nav || window.ui.step;
                        pad_stick(&window, &repeat, pad);
                        if (!before && (window.ui.nav || window.ui.step)) input = 1;
                    }
                    break;
                }
                default:
                    if (event.type == dialog_event) { take_answer(&window, event.user.data1); input = 1; }
                    break;
                }
            } while (!input && !window.quit && SDL_PollEvent(&event));
        }
        if (repeat.direction && SDL_GetTicks() >= repeat.next)
        {
            move(&window, repeat.direction);
            repeat.next = SDL_GetTicks() + REPEAT_NEXT;
        }
        /* Dock icon drops: done once none has come for a moment. */
        if (batch.count && !batch.open && SDL_GetTicks() - batch.last > 300) drop_finish(&window, &batch);
        draw(&window, 1);
    }
    drop_finish(&window, &batch);
    SettingsPage_Save();
    for (int i = 0; i < 8; ++i) if (pads[i]) SDL_CloseGamepad(pads[i]);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(sdl_window);
    return window.play;
}
