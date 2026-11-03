/* The bindings as the platform sees them; see bindings.h. */
#include <stdio.h>
#include <string.h>
#include "doomdef.h"
#include "bindings.h"
#include "i_gamepad.h"

void M_SaveDefaults(void);

static const game_action *all(int *count) { return Game_Actions(count); }

game_action_role Bind_Role(int code, int *pressed)
{
    int count;
    const game_action *actions = all(&count);
    if (pressed) *pressed = KEY_NONE;
    if (code == KEY_NONE) return GAME_ACTION_GAME;
    for (int i = 0; i < count; ++i)
        for (int s = 0; s < 3; ++s)
            if (actions[i].slot[s] && *actions[i].slot[s] == code && actions[i].role != GAME_ACTION_GAME)
            {
                if (pressed && actions[i].role == GAME_ACTION_PRESS) *pressed = actions[i].fixed;
                return actions[i].role;
            }
    return GAME_ACTION_GAME;
}

void Bind_Name(int code, char *name, int size)
{
    static const char *const mouse[] = { "Left mouse", "Right mouse", "Middle mouse", "Mouse 4", "Mouse 5" };
    const char *known = NULL;
    switch (code)
    {
    case KEY_NONE: known = "-"; break;
    case KEY_RIGHTARROW: known = "Right"; break;
    case KEY_LEFTARROW: known = "Left"; break;
    case KEY_UPARROW: known = "Up"; break;
    case KEY_DOWNARROW: known = "Down"; break;
    case KEY_ESCAPE: known = "Esc"; break;
    case KEY_ENTER: known = "Enter"; break;
    case KEY_TAB: known = "Tab"; break;
    case KEY_BACKSPACE: known = "Backspace"; break;
    case KEY_PAUSE: known = "Pause"; break;
    case KEY_RSHIFT: known = "Shift"; break;
    case KEY_RCTRL: known = "Ctrl"; break;
    case KEY_RALT: known = "Alt"; break;
    case ' ': known = "Space"; break;
    case KEY_F11: known = "F11"; break;
    case KEY_F12: known = "F12"; break;
    case KEY_WHEELUP: known = "Wheel up"; break;
    case KEY_WHEELDOWN: known = "Wheel down"; break;
    }
    if (known) snprintf(name, (size_t)size, "%s", known);
    else if (code >= KEY_F1 && code <= KEY_F10) snprintf(name, (size_t)size, "F%d", code - KEY_F1 + 1);
    else if (code >= KEY_MOUSE1 && code <= KEY_MOUSE5) snprintf(name, (size_t)size, "%s", mouse[code - KEY_MOUSE1]);
    else if (code >= KEY_PAD && code <= KEY_PADRT) I_GamepadButtonName(code - KEY_PAD, name, size);
    else if (code > ' ' && code < 127) snprintf(name, (size_t)size, "%c", code >= 'a' && code <= 'z' ? code - 'a' + 'A' : code);
    else snprintf(name, (size_t)size, "Key %d", code);
}

/* Keys the menus answer before the game sees them: bound to a game action
   they would open a menu or end the game instead. The platform's own
   (screenshot, fullscreen) take them first, so those may have F-keys. */
static int kept(int code, game_action_role role)
{
    if (code == KEY_ESCAPE || code == KEY_PAUSE || code == KEY_PAD_START || code == KEY_PAD + 5) return 1;
    if (role != GAME_ACTION_GAME) return 0;
    return (code >= KEY_F1 && code <= KEY_F10) || code == KEY_F11 || code == KEY_F12 ||
           code == KEY_TAB || code == KEY_MINUS || code == KEY_EQUALS;
}

int Bind_Set(int action, int slot, int code, int *taken)
{
    int count;
    const game_action *actions = all(&count);
    *taken = -1;
    if (action < 0 || action >= count || slot < 0 || slot > 2 || !actions[action].slot[slot]) return 0;
    if ((slot == 2) != (code >= KEY_PAD && code <= KEY_PADRT)) return 0;
    if (kept(code, actions[action].role)) return 0;
    for (int i = 0; i < count; ++i)
        for (int s = 0; s < 3; ++s)
            if (actions[i].slot[s] && *actions[i].slot[s] == code && (i != action || s != slot))
            {
                *actions[i].slot[s] = KEY_NONE;
                if (i != action) *taken = i;
            }
    *actions[action].slot[slot] = code;
    return 1;
}

void Bind_Reset(int action, int slot)
{
    int count, taken;
    const game_action *actions = all(&count);
    if (action < 0 || action >= count || slot < 0 || slot > 2 || !actions[action].slot[slot]) return;
    if (actions[action].defaults[slot] == KEY_NONE) *actions[action].slot[slot] = KEY_NONE;
    else Bind_Set(action, slot, actions[action].defaults[slot], &taken);
}

void Bind_ResetAll(void)
{
    int count;
    const game_action *actions = all(&count);
    for (int i = 0; i < count; ++i)
        for (int s = 0; s < 3; ++s)
            if (actions[i].slot[s]) *actions[i].slot[s] = actions[i].defaults[s];
}

int Bind_IsDefault(int action, int slot)
{
    int count;
    const game_action *actions = all(&count);
    if (action < 0 || action >= count || slot < 0 || slot > 2 || !actions[action].slot[slot]) return 1;
    return *actions[action].slot[slot] == actions[action].defaults[slot];
}

void Bind_Save(void) { M_SaveDefaults(); }
