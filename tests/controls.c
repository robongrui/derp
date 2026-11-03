/* The controls without a pad or a game: the sticks' dead zone, curve and
   turn boost (platform/gamepad_curves.c), and binding rules (platform/
   bindings.c) on a small action table of its own. */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "doomdef.h"
#include "../platform/bindings.h"
#include "../platform/gamepad_curves.h"

static int failures;
#define CHECK(test) do { if (!(test)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #test); ++failures; } } while (0)
static int near(float a, float b) { return fabsf(a - b) < 1e-3f; }

static int fire[3], use[3], shot[3], automap;
static const game_action actions[] = {
    {"Fire", "Act", {&fire[0], &fire[1], &fire[2]}, {KEY_RCTRL, KEY_MOUSE1, KEY_PADRT}, GAME_ACTION_GAME, KEY_NONE},
    {"Use", "Act", {&use[0], &use[1], &use[2]}, {' ', KEY_NONE, KEY_PAD_X}, GAME_ACTION_GAME, KEY_NONE},
    {"Screenshot", "Screens", {&shot[0], &shot[1], &shot[2]}, {KEY_F7, KEY_NONE, KEY_NONE}, GAME_ACTION_SCREENSHOT, KEY_NONE},
    {"Automap", "Screens", {NULL, NULL, &automap}, {KEY_NONE, KEY_NONE, KEY_PAD_BACK}, GAME_ACTION_PRESS, KEY_TAB},
};
const game_action *Game_Actions(int *count) { *count = 4; return actions; }
void I_GamepadButtonName(int button, char *name, int size) { snprintf(name, (size_t)size, "Pad %d", button); }
static int saved;
void M_SaveDefaults(void) { ++saved; }

static void sticks(void)
{
    float x, y, held = 0;
    Pad_Deadzone(0.1f, 0, 0.15f, &x, &y);
    CHECK(x == 0 && y == 0);
    Pad_Deadzone(1, 0, 0.15f, &x, &y);
    CHECK(near(x, 1) && near(y, 0));
    Pad_Deadzone(0.575f, 0, 0.15f, &x, &y);
    CHECK(near(x, 0.5f));
    /* A diagonal keeps its direction; past the rim it stays at full tilt. */
    Pad_Deadzone(0.8f, -0.8f, 0.15f, &x, &y);
    CHECK(near(x, -y) && near(sqrtf(x * x + y * y), 1));
    CHECK(near(Pad_Curve(0.5f), 0.25f) && near(Pad_Curve(1), 1));
    CHECK(Pad_Boost(1, 0.2f, &held) == 1);
    CHECK(near(Pad_Boost(1, 0.35f, &held), PAD_BOOST));
    CHECK(Pad_Boost(0.9f, 0.02f, &held) == 1 && held == 0);
}

static void bindings(void)
{
    int taken, pressed;
    char name[32];
    Bind_ResetAll();
    CHECK(fire[0] == KEY_RCTRL && automap == KEY_PAD_BACK);
    /* Taken from another action: that one lets go. */
    CHECK(Bind_Set(1, 0, KEY_RCTRL, &taken) && taken == 0 && fire[0] == KEY_NONE && use[0] == KEY_RCTRL);
    CHECK(!Bind_IsDefault(0, 0) && !Bind_IsDefault(1, 0));
    /* Pads in the pad column only, keys and mouse buttons in the others. */
    CHECK(!Bind_Set(1, 0, KEY_PAD_A, &taken) && !Bind_Set(1, 2, KEY_RCTRL, &taken) && Bind_Set(1, 2, KEY_PAD_A, &taken));
    /* The menus' keys stay theirs; the platform's own may have F-keys. */
    CHECK(!Bind_Set(0, 0, KEY_ESCAPE, &taken) && !Bind_Set(0, 0, KEY_F2, &taken) && !Bind_Set(0, 0, KEY_TAB, &taken));
    CHECK(!Bind_Set(0, 2, KEY_PAD_START, &taken) && Bind_Set(2, 1, KEY_F12, &taken));
    /* A fixed key can't change; its pad button presses it. */
    CHECK(!Bind_Set(3, 0, 'm', &taken));
    CHECK(Bind_Role(KEY_PAD_BACK, &pressed) == GAME_ACTION_PRESS && pressed == KEY_TAB);
    CHECK(Bind_Role(KEY_F7, NULL) == GAME_ACTION_SCREENSHOT && Bind_Role(' ', NULL) == GAME_ACTION_GAME);
    Bind_Reset(0, 0);
    CHECK(fire[0] == KEY_RCTRL && use[0] == KEY_NONE);
    Bind_ResetAll();
    for (int a = 0; a < 4; ++a) for (int s = 0; s < 3; ++s) CHECK(Bind_IsDefault(a, s));
    Bind_Name(KEY_RCTRL, name, sizeof(name)); CHECK(!strcmp(name, "Ctrl"));
    Bind_Name(KEY_MOUSE1, name, sizeof(name)); CHECK(!strcmp(name, "Left mouse"));
    Bind_Name('a', name, sizeof(name)); CHECK(!strcmp(name, "A"));
    Bind_Name(KEY_F7, name, sizeof(name)); CHECK(!strcmp(name, "F7"));
    Bind_Name(KEY_NONE, name, sizeof(name)); CHECK(!strcmp(name, "-"));
}

int main(void)
{
    sticks();
    bindings();
    if (failures) fprintf(stderr, "%d failed\n", failures);
    else printf("Controls: all passed\n");
    return failures != 0;
}
