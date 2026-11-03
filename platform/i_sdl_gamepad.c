/* Gamepads and joysticks; see i_gamepad.h. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "doomdef.h"
#include "doomstat.h"
#include "d_event.h"
#include "game_profile.h"
#include "bindings.h"
#include "gamepad_curves.h"
#include "gamepad_rumble.h"
#include "i_gamepad.h"
#include "i_video.h"
#include <SDL3/SDL.h>
#ifdef DOOM_RENDER3D
#include "i_render3d.h"
#endif

extern int mouseSensitivity;

#define MAX_PADS 8
#define MAX_STICKS 4
/* The triggers count as pressed past half way, let go below a third. */
#define TRIGGER_PRESS 0.5f
#define TRIGGER_RELEASE 0.35f
/* A stick in the menus moves like the d-pad past these. */
#define MENU_STICK_PRESS 0.6f
#define MENU_STICK_RELEASE 0.4f
/* A held menu direction repeats after a pause, then this often (seconds). */
#define MENU_REPEAT_DELAY 0.35
#define MENU_REPEAT 0.085
/* Full tilt turns and looks this fast (degrees a second) at speed 1. */
#define YAW_SPEED 220.0f
#define PITCH_SPEED 140.0f

/* The buttons: SDL's, then the triggers, then the left stick pushed up,
   down, left and right (only for working menus). */
#define LT KEY_PADBUTTONS
#define RT (KEY_PADBUTTONS + 1)
#define STICK_UP (KEY_PADBUTTONS + 2)
#define BUTTONS (KEY_PADBUTTONS + 6)

static SDL_Gamepad *pads[MAX_PADS];
static SDL_Joystick *sticks[MAX_STICKS];
static int stick_state[MAX_STICKS][3];   /* buttons, x, y as last posted */
/* The pad last used: its sticks move and its motors rumble. */
static SDL_Gamepad *active;
static int used_last;
static gamepad_options options = { 1, 1, 0.15f, 1, 1, 0, 0, 2, 1, 1 };
/* What each button posted when it went down (a key, 0 for none), so its
   release lets go of the same key though the mode changed in between. */
static int posted[BUTTONS];
static int is_down[BUTTONS];
static int back_held, back_chord, gyro_held;
/* The direction held in a menu (a button, or -1) and when it repeats. */
static int repeating = -1;
static double repeat_at;
static int menu_stick = -1;
static Uint64 last_frame, last_look;
static float boost_held, turn_left;
static float gyro_yaw, gyro_pitch;
static Uint64 gyro_time;

static double now_seconds(void) { return SDL_GetTicksNS() / 1e9; }

void I_GamepadOptions(const gamepad_options *o)
{
    int gyro_was = options.gyro;
    options = *o;
    if (gyro_was != options.gyro)
        for (int i = 0; i < MAX_PADS; ++i)
            if (pads[i] && SDL_GamepadHasSensor(pads[i], SDL_SENSOR_GYRO))
                SDL_SetGamepadSensorEnabled(pads[i], SDL_SENSOR_GYRO, options.gyro != 0);
}

int I_GamepadSteamDeck(void)
{
    const char *deck = SDL_getenv("SteamDeck");
    return deck && !strcmp(deck, "1");
}

void I_GamepadInit(void)
{
#ifndef DOOM_TEST_DRIVER
    if (!SDL_InitSubSystem(SDL_INIT_GAMEPAD))
        fprintf(stderr, "Gamepads unavailable: %s\n", SDL_GetError());
#endif
}

void I_GamepadShutdown(void)
{
    for (int i = 0; i < MAX_PADS; ++i) if (pads[i]) { Rumble_Stop(pads[i]); SDL_CloseGamepad(pads[i]); pads[i] = NULL; }
    for (int i = 0; i < MAX_STICKS; ++i) if (sticks[i]) { SDL_CloseJoystick(sticks[i]); sticks[i] = NULL; }
    active = NULL;
}

void I_GamepadOtherInput(void) { used_last = 0; }

static SDL_GamepadType active_type(void)
{
    SDL_Gamepad *pad = active;
    for (int i = 0; !pad && i < MAX_PADS; ++i) pad = pads[i];
    return pad ? SDL_GetGamepadType(pad) : SDL_GAMEPAD_TYPE_STANDARD;
}

void I_GamepadButtonName(int button, char *name, int size)
{
    static const char *const faces[] = { "A", "B", "X", "Y" };
    SDL_GamepadType type = active_type();
    int ps = type == SDL_GAMEPAD_TYPE_PS3 || type == SDL_GAMEPAD_TYPE_PS4 || type == SDL_GAMEPAD_TYPE_PS5;
    int nintendo = type >= SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO && type <= SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR;
    const char *known = NULL;
    if (button >= 0 && button <= SDL_GAMEPAD_BUTTON_NORTH)
    {
        switch (SDL_GetGamepadButtonLabelForType(type, (SDL_GamepadButton)button))
        {
        case SDL_GAMEPAD_BUTTON_LABEL_A: known = "A"; break;
        case SDL_GAMEPAD_BUTTON_LABEL_B: known = "B"; break;
        case SDL_GAMEPAD_BUTTON_LABEL_X: known = "X"; break;
        case SDL_GAMEPAD_BUTTON_LABEL_Y: known = "Y"; break;
        case SDL_GAMEPAD_BUTTON_LABEL_CROSS: known = "Cross"; break;
        case SDL_GAMEPAD_BUTTON_LABEL_CIRCLE: known = "Circle"; break;
        case SDL_GAMEPAD_BUTTON_LABEL_SQUARE: known = "Square"; break;
        case SDL_GAMEPAD_BUTTON_LABEL_TRIANGLE: known = "Triangle"; break;
        default: known = faces[button]; break;
        }
    }
    else switch (button)
    {
    case SDL_GAMEPAD_BUTTON_BACK: known = ps ? "Share" : nintendo ? "-" : "Back"; break;
    case SDL_GAMEPAD_BUTTON_GUIDE: known = "Guide"; break;
    case SDL_GAMEPAD_BUTTON_START: known = ps ? "Options" : nintendo ? "+" : "Start"; break;
    case SDL_GAMEPAD_BUTTON_LEFT_STICK: known = "L3"; break;
    case SDL_GAMEPAD_BUTTON_RIGHT_STICK: known = "R3"; break;
    case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: known = ps ? "L1" : nintendo ? "L" : "LB"; break;
    case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: known = ps ? "R1" : nintendo ? "R" : "RB"; break;
    case SDL_GAMEPAD_BUTTON_DPAD_UP: known = "D-pad up"; break;
    case SDL_GAMEPAD_BUTTON_DPAD_DOWN: known = "D-pad down"; break;
    case SDL_GAMEPAD_BUTTON_DPAD_LEFT: known = "D-pad left"; break;
    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: known = "D-pad right"; break;
    case SDL_GAMEPAD_BUTTON_MISC1: known = ps ? "Mic" : "Share"; break;
    /* The Steam Deck's names for its back grips. */
    case SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1: known = "R4"; break;
    case SDL_GAMEPAD_BUTTON_LEFT_PADDLE1: known = "L4"; break;
    case SDL_GAMEPAD_BUTTON_RIGHT_PADDLE2: known = "R5"; break;
    case SDL_GAMEPAD_BUTTON_LEFT_PADDLE2: known = "L5"; break;
    case SDL_GAMEPAD_BUTTON_TOUCHPAD: known = "Touchpad"; break;
    case LT: known = ps ? "L2" : nintendo ? "ZL" : "LT"; break;
    case RT: known = ps ? "R2" : nintendo ? "ZR" : "RT"; break;
    }
    if (known) snprintf(name, (size_t)size, "%s", known);
    else snprintf(name, (size_t)size, "Pad %d", button + 1);
}

static void post_key(int key, int pressed) { I_PostInput(pressed ? ev_keydown : ev_keyup, key, 0, 0); }

/* The left stick's menu directions answer as the d-pad's. */
static int as_dpad(int button)
{
    return button >= STICK_UP && button < STICK_UP + 4 ? SDL_GAMEPAD_BUTTON_DPAD_UP + button - STICK_UP : button;
}
static int is_direction(int button)
{
    button = as_dpad(button);
    return button >= SDL_GAMEPAD_BUTTON_DPAD_UP && button <= SDL_GAMEPAD_BUTTON_DPAD_RIGHT;
}

#ifdef DOOM_RENDER3D
/* F4's panel, worked like its keys. */
static int settings_key(int button)
{
    switch (as_dpad(button))
    {
    case SDL_GAMEPAD_BUTTON_DPAD_UP: return SDL_SCANCODE_UP;
    case SDL_GAMEPAD_BUTTON_DPAD_DOWN: return SDL_SCANCODE_DOWN;
    case SDL_GAMEPAD_BUTTON_DPAD_LEFT: return SDL_SCANCODE_LEFT;
    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: return SDL_SCANCODE_RIGHT;
    case SDL_GAMEPAD_BUTTON_SOUTH: return SDL_SCANCODE_RETURN;
    case SDL_GAMEPAD_BUTTON_EAST: case SDL_GAMEPAD_BUTTON_START: return SDL_SCANCODE_ESCAPE;
    case SDL_GAMEPAD_BUTTON_WEST: return SDL_SCANCODE_DELETE;
    case SDL_GAMEPAD_BUTTON_NORTH: return SDL_SCANCODE_BACKSPACE;
    case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: return SDL_SCANCODE_PAGEUP;
    case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: return SDL_SCANCODE_PAGEDOWN;
    case LT: case RT: return SDL_SCANCODE_TAB;
    }
    return SDL_SCANCODE_UNKNOWN;
}
#endif

/* The key the game's menus know for a button, by what the pad does now. */
static int menu_key(int button, game_pad_mode mode)
{
    switch (as_dpad(button))
    {
    case SDL_GAMEPAD_BUTTON_DPAD_UP: return mode == GAME_PAD_ASK ? 0 : KEY_UPARROW;
    case SDL_GAMEPAD_BUTTON_DPAD_DOWN: return mode == GAME_PAD_ASK ? 0 : KEY_DOWNARROW;
    case SDL_GAMEPAD_BUTTON_DPAD_LEFT: return mode == GAME_PAD_ASK ? 0 : KEY_LEFTARROW;
    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: return mode == GAME_PAD_ASK ? 0 : KEY_RIGHTARROW;
    case SDL_GAMEPAD_BUTTON_SOUTH: return mode == GAME_PAD_ASK ? 'y' : KEY_ENTER;
    case SDL_GAMEPAD_BUTTON_EAST: case SDL_GAMEPAD_BUTTON_BACK:
        return mode == GAME_PAD_ASK ? 'n' : mode == GAME_PAD_TYPING ? KEY_ESCAPE : KEY_BACKSPACE;
    case SDL_GAMEPAD_BUTTON_WEST: return mode == GAME_PAD_TYPING ? KEY_BACKSPACE : 0;
    }
    return 0;
}

/* What a press does, by what the pad does now; what it posted waits in
   posted[] for its release. */
static void act_press(int button)
{
#ifdef DOOM_RENDER3D
    if (I_Render3DSettingsOpen())
    {
        if (I_Render3DSettingsCapturing())
        {
            if (button == SDL_GAMEPAD_BUTTON_START) I_Render3DSettingsKey(SDL_SCANCODE_ESCAPE, 0);
            else if (button < STICK_UP) I_Render3DSettingsBind(KEY_PAD + button);
            return;
        }
        int scancode = settings_key(button);
        if (scancode != SDL_SCANCODE_UNKNOWN) I_Render3DSettingsKey(scancode, button == LT);
        return;
    }
#endif
    /* Start opens and closes the game's menu. */
    if (button == SDL_GAMEPAD_BUTTON_START) { post_key(KEY_ESCAPE, 1); post_key(KEY_ESCAPE, 0); return; }
    game_pad_mode mode = Game_PadMode();
    if (mode == GAME_PAD_MENU || mode == GAME_PAD_ASK || mode == GAME_PAD_TYPING)
    {
        int key = menu_key(button, mode);
        if (key) { post_key(key, 1); posted[button] = key; }
        return;
    }
    if (button >= STICK_UP) return;
    /* In the automap the shoulders zoom. */
    if (mode == GAME_PAD_AUTOMAP && (button == SDL_GAMEPAD_BUTTON_LEFT_SHOULDER || button == SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER))
    {
        posted[button] = button == SDL_GAMEPAD_BUTTON_LEFT_SHOULDER ? KEY_MINUS : KEY_EQUALS;
        post_key(posted[button], 1);
        return;
    }
    int pressed_key, code = KEY_PAD + button;
    game_action_role role = Bind_Role(code, &pressed_key);
    switch (role)
    {
    case GAME_ACTION_GAME: post_key(code, 1); posted[button] = code; break;
    case GAME_ACTION_PRESS: post_key(pressed_key, 1); posted[button] = pressed_key; break;
    case GAME_ACTION_GYRO: gyro_held = 1; break;
    default: I_PlatformAction(role); break;
    }
}

static void act_release(int button)
{
    if (posted[button]) post_key(posted[button], 0);
    posted[button] = 0;
    if (button < STICK_UP && Bind_Role(KEY_PAD + button, NULL) == GAME_ACTION_GYRO) gyro_held = 0;
}

/* A button goes down or up. Back acts when let go, unless it was held for
   Back+Start (F4); Start acts when pressed. */
static void button(int button, int pressed)
{
    if (button < 0 || button >= BUTTONS || is_down[button] == pressed) return;
    is_down[button] = pressed;
    if (pressed)
    {
        used_last = 1;
        if (SDL_CursorVisible()) SDL_HideCursor();
    }
    if (button == SDL_GAMEPAD_BUTTON_BACK)
    {
        if (pressed) { back_held = 1; back_chord = 0; return; }
        back_held = 0;
        if (!back_chord) { act_press(button); act_release(button); }
        back_chord = 0;
        return;
    }
    if (button == SDL_GAMEPAD_BUTTON_START && pressed && back_held)
    {
        back_chord = 1;
        I_OpenSettingsPanel();
        return;
    }
    if (!pressed)
    {
        act_release(button);
        if (button == repeating) repeating = -1;
        return;
    }
    act_press(button);
    if (is_direction(button)) { repeating = button; repeat_at = now_seconds() + MENU_REPEAT_DELAY; }
}

void I_GamepadReleaseAll(void)
{
    for (int i = 0; i < BUTTONS; ++i)
    {
        if (posted[i]) post_key(posted[i], 0);
        posted[i] = 0;
        is_down[i] = 0;
    }
    back_held = back_chord = gyro_held = 0;
    repeating = menu_stick = -1;
}

static int slot_of(SDL_JoystickID id)
{
    for (int i = 0; i < MAX_PADS; ++i)
        if (pads[i] && SDL_GetGamepadID(pads[i]) == id) return i;
    return -1;
}

static void add_pad(SDL_JoystickID id)
{
    if (slot_of(id) >= 0) return;
    for (int i = 0; i < MAX_PADS; ++i)
        if (!pads[i])
        {
            if (!(pads[i] = SDL_OpenGamepad(id))) return;
            if (options.gyro && SDL_GamepadHasSensor(pads[i], SDL_SENSOR_GYRO))
                SDL_SetGamepadSensorEnabled(pads[i], SDL_SENSOR_GYRO, true);
            const char *name = SDL_GetGamepadName(pads[i]);
            fprintf(stderr, "Gamepad: %s\n", name ? name : "unnamed");
            if (!active) active = pads[i];
            return;
        }
}

static void remove_pad(SDL_JoystickID id)
{
    int slot = slot_of(id);
    if (slot < 0) return;
    if (pads[slot] == active)
    {
        I_GamepadReleaseAll();
        active = NULL;
        for (int i = 0; i < MAX_PADS; ++i) if (i != slot && pads[i]) active = pads[i];
    }
    SDL_CloseGamepad(pads[slot]);
    pads[slot] = NULL;
}

/* A joystick SDL knows no pad layout for: DOOM's digital joystick events,
   the first four buttons and the first two axes. */
static int stick_slot(SDL_JoystickID id)
{
    for (int i = 0; i < MAX_STICKS; ++i)
        if (sticks[i] && SDL_GetJoystickID(sticks[i]) == id) return i;
    return -1;
}

static void stick_changed(int slot)
{
    SDL_Joystick *stick = sticks[slot];
    int buttons = 0, axis[2] = { 0, 0 };
    for (int b = 0; b < 4 && b < SDL_GetNumJoystickButtons(stick); ++b)
        if (SDL_GetJoystickButton(stick, b)) buttons |= 1 << b;
    for (int a = 0; a < 2 && a < SDL_GetNumJoystickAxes(stick); ++a)
    {
        int value = SDL_GetJoystickAxis(stick, a);
        axis[a] = value > 16384 ? 1 : value < -16384 ? -1 : 0;
    }
    int *last = stick_state[slot];
    if (last[0] == buttons && last[1] == axis[0] && last[2] == axis[1]) return;
    last[0] = buttons; last[1] = axis[0]; last[2] = axis[1];
    used_last = 1;
    I_PostInput(ev_joystick, buttons, axis[0], axis[1]);
}

static void open_stick(SDL_JoystickID id)
{
    if (SDL_IsGamepad(id) || stick_slot(id) >= 0) return;
    for (int i = 0; i < MAX_STICKS; ++i)
        if (!sticks[i])
        {
            if (!(sticks[i] = SDL_OpenJoystick(id))) return;
            memset(stick_state[i], 0, sizeof(stick_state[i]));
            const char *name = SDL_GetJoystickName(sticks[i]);
            fprintf(stderr, "Joystick: %s\n", name ? name : "unnamed");
            return;
        }
}

int I_GamepadEvent(const SDL_Event *event)
{
    if (event->type < SDL_EVENT_JOYSTICK_AXIS_MOTION || event->type >= SDL_EVENT_FINGER_DOWN) return 0;
#ifdef DOOM_TEST_DRIVER
    /* Test input comes from D_PostEvent, independent of the desk's pads. */
    return 1;
#endif
    int slot;
    switch (event->type)
    {
    case SDL_EVENT_GAMEPAD_ADDED: add_pad(event->gdevice.which); break;
    case SDL_EVENT_GAMEPAD_REMOVED: remove_pad(event->gdevice.which); break;
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN: case SDL_EVENT_GAMEPAD_BUTTON_UP:
        if ((slot = slot_of(event->gbutton.which)) < 0 || event->gbutton.button >= KEY_PADBUTTONS) break;
        if (event->gbutton.down) active = pads[slot];
        if (pads[slot] == active) button(event->gbutton.button, event->gbutton.down);
        break;
    case SDL_EVENT_GAMEPAD_AXIS_MOTION:
        if ((slot = slot_of(event->gaxis.which)) < 0) break;
        if (event->gaxis.axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER || event->gaxis.axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER)
        {
            float value = event->gaxis.value / 32767.0f;
            int which = event->gaxis.axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER ? LT : RT;
            if (!is_down[which] && value > TRIGGER_PRESS) { active = pads[slot]; button(which, 1); }
            else if (is_down[which] && value < TRIGGER_RELEASE && pads[slot] == active) button(which, 0);
        }
        else if (abs(event->gaxis.value) > 16384) { active = pads[slot]; used_last = 1; }
        break;
    case SDL_EVENT_GAMEPAD_SENSOR_UPDATE:
        if (event->gsensor.sensor == SDL_SENSOR_GYRO && (slot = slot_of(event->gsensor.which)) >= 0 && pads[slot] == active)
        {
            /* Rates in radians a second, over the time since the last reading. */
            Uint64 at = event->gsensor.sensor_timestamp;
            float seconds = gyro_time && at > gyro_time ? (float)((at - gyro_time) / 1e9) : 0;
            gyro_time = at;
            if (seconds > 0.1f) seconds = 0.1f;
            gyro_pitch += event->gsensor.data[0] * seconds;
            gyro_yaw += event->gsensor.data[1] * seconds;
        }
        break;
    case SDL_EVENT_JOYSTICK_ADDED: open_stick(event->jdevice.which); break;
    case SDL_EVENT_JOYSTICK_REMOVED:
        if ((slot = stick_slot(event->jdevice.which)) < 0) break;
        if (stick_state[slot][0] || stick_state[slot][1] || stick_state[slot][2]) I_PostInput(ev_joystick, 0, 0, 0);
        SDL_CloseJoystick(sticks[slot]);
        sticks[slot] = NULL;
        break;
    case SDL_EVENT_JOYSTICK_AXIS_MOTION:
        if ((slot = stick_slot(event->jaxis.which)) >= 0) stick_changed(slot);
        break;
    case SDL_EVENT_JOYSTICK_BUTTON_DOWN: case SDL_EVENT_JOYSTICK_BUTTON_UP:
        if ((slot = stick_slot(event->jbutton.which)) >= 0) stick_changed(slot);
        break;
    }
    return 1;
}

/* The active pad's stick through the dead zone: 0 moves, 1 looks. */
static void stick(int which, float *x, float *y)
{
    *x = *y = 0;
    if (!active) return;
    if (options.southpaw) which = !which;
    SDL_GamepadAxis ax = which ? SDL_GAMEPAD_AXIS_RIGHTX : SDL_GAMEPAD_AXIS_LEFTX;
    SDL_GamepadAxis ay = which ? SDL_GAMEPAD_AXIS_RIGHTY : SDL_GAMEPAD_AXIS_LEFTY;
    Pad_Deadzone(SDL_GetGamepadAxis(active, ax) / 32767.0f, SDL_GetGamepadAxis(active, ay) / 32767.0f,
                 options.deadzone, x, y);
}

static int pad_plays(void)
{
#ifdef DOOM_RENDER3D
    if (I_Render3DSettingsOpen()) return 0;
#endif
    game_pad_mode mode = Game_PadMode();
    return active && (mode == GAME_PAD_PLAY || mode == GAME_PAD_AUTOMAP);
}

boolean I_GamepadMove(float *forward, float *side)
{
    float x, y;
    *forward = *side = 0;
    if (!pad_plays()) return false;
    stick(0, &x, &y);
    if (x == 0 && y == 0) return false;
    *forward = -y;
    *side = x;
    return true;
}

boolean I_GamepadUsedLast(void) { return used_last != 0; }

/* In menus the left stick pushed far over is a d-pad press. */
static void menu_stick_frame(void)
{
    float x = SDL_GetGamepadAxis(active, SDL_GAMEPAD_AXIS_LEFTX) / 32767.0f;
    float y = SDL_GetGamepadAxis(active, SDL_GAMEPAD_AXIS_LEFTY) / 32767.0f;
    float most = fabsf(x) > fabsf(y) ? fabsf(x) : fabsf(y);
    int direction = -1;
    if (most > (menu_stick >= 0 ? MENU_STICK_RELEASE : MENU_STICK_PRESS))
        direction = STICK_UP + (fabsf(x) > fabsf(y) ? (x > 0 ? 3 : 2) : (y > 0 ? 1 : 0));
    if (direction == menu_stick) return;
    if (menu_stick >= 0) button(menu_stick, 0);
    menu_stick = direction;
    if (direction >= 0) button(direction, 1);
}

void I_GamepadFrame(int view, int *turn, float *look)
{
    Uint64 now = SDL_GetTicksNS();
    float seconds = last_frame && now > last_frame ? (float)((now - last_frame) / 1e9) : 0;
    if (seconds > 0.1f) seconds = 0.1f;
    last_frame = now;
    *turn = 0; *look = 0;

    int plays = pad_plays();
    if (!plays && active) menu_stick_frame();
    else if (menu_stick >= 0) { button(menu_stick, 0); menu_stick = -1; }
    /* Held directions repeat in menus only: in play a held button stays held. */
    if (!plays && repeating >= 0 && is_down[repeating] && now_seconds() >= repeat_at)
    {
        repeat_at = now_seconds() + MENU_REPEAT;
        act_release(repeating);
        act_press(repeating);
    }

    Rumble_Frame(active, seconds, demoplayback ? 0 : options.rumble, options.trigger_rumble, options.world_rumble);

    /* The look keeps its own clock: waiting for the renderer's moment
       spends none of it. */
    if (view == GAMEPAD_VIEW_LATER && plays) return;
    if (view == GAMEPAD_VIEW_OFF || !plays)
    {
        boost_held = 0; gyro_yaw = gyro_pitch = 0; last_look = 0;
        return;
    }
    seconds = last_look && now > last_look ? (float)((now - last_look) / 1e9) : 0;
    if (seconds > 0.1f) seconds = 0.1f;
    last_look = now;
    float x, y;
    stick(1, &x, &y);
    float tilt = sqrtf(x * x + y * y);
    float speed = tilt > 0 ? Pad_Curve(tilt) / tilt : 0;
    if (options.boost) speed *= Pad_Boost(tilt, seconds, &boost_held);
    /* DOOM's angle is 65536 to the full turn; its mouse unit (G_Responder's
       mousex) turns it by 8. */
    float yaw_units = x * speed * options.look_x * YAW_SPEED * seconds * (65536.0f / 360.0f);
    float pitch_radians = y * speed * options.look_y * PITCH_SPEED * seconds * (SDL_PI_F / 180);
    if (options.gyro == 1 || (options.gyro == 2 && gyro_held))
    {
        yaw_units -= gyro_yaw * options.gyro_speed * (65536.0f / (2 * SDL_PI_F));
        pitch_radians -= gyro_pitch * options.gyro_speed;
    }
    gyro_yaw = gyro_pitch = 0;
    turn_left += yaw_units / 8;
    /* G_Responder scales an ev_mouse's motion by the mouse sensitivity:
       post what comes out right after it, keeping what it rounds away. */
    int scale = mouseSensitivity + 5, whole = (int)turn_left;
    if (whole && scale > 0)
    {
        *turn = whole * 10 / scale;
        turn_left -= (float)(*turn * scale / 10);
    }
    /* I_Render3DLook takes the mouse's own units: 0.002 radians each. */
    *look = pitch_radians / 0.002f;
}
