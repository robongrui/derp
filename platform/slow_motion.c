/* Slow motion (F4 Combat, or -slowmotion; its key, X or F12, or on its own when shooting): the game's clock runs slow, so the world
 * crawls and the same tics run further apart; saves and demos can't tell. The view
 * keeps turning every frame (the 3D renderer turns it by the mouse itself), menus and
 * the F4 panel run at full speed. Not in netgames. A nod to F.E.A.R.'s reflex time, so it
 * shows: tracers fly and shots leave shimmering wakes (tracers.cpp, shot_wakes.cpp), the
 * colours pop (present.frag), a whoosh in and out (slow_whoosh.c), echoes ring
 * longer (Reverb_Stretch). In DOOM's Authored it has a price (p_focus.c through the game profile):
 * by hand the meter runs down while the world is slow, the shootslide pays at once and asks for
 * its own stretch, and slowing by hand with an empty meter does nothing. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "doomdef.h"
#include "doomstat.h"
#include "m_argv.h"
#ifndef BT_ATTACK
#include "d_event.h"   /* Doom's buttons; Heretic's doomdef.h has them */
#endif
#include "slow_motion.h"
#include "game_profile.h"
#include <SDL3/SDL.h>
#ifdef DOOM_RENDER3D
#include "i_render3d.h"
#endif

#define SLOW_SPEED 0.25   /* the world at this share of its speed */
#define EASE_SECONDS 0.1  /* easing in and out: about a third of a second all told */
#define SHOT_HOLD 5.0     /* When shooting: real seconds slow after the trigger lets go */

extern boolean menuactive;
extern boolean paused;
extern double I_TimeScale;

static int on;
static double left;   /* When shooting: real seconds still slow */
int SlowMotion_Option;   /* F4 Slow motion: 0 off, 1 the key, 2 when shooting too (scene.cpp sets it) */
volatile int SlowMotion_Starts, SlowMotion_Ends;

/* By hand only with something in the meter, where slow motion costs. */
static int Affordable(void) { return !Game_SlowCosts() || Game_SlowLeft(); }

double SlowMotion_Share(void)
{
    double share = (1 - I_TimeScale) / (1 - SLOW_SPEED);
    return share < 0 ? 0 : share > 1 ? 1 : share;
}

/* When shooting: the first shot slows the world; it stays slow while the trigger is held
 * and SHOT_HOLD real seconds past the last, so the deaths and the blood play out slow too. */
static int Shooting(double dt)
{
    if (SlowMotion_Option != 2 || gamestate != GS_LEVEL) {left = 0;return 0;}
    /* The trigger itself: attackdown holds until the weapon is ready again, four real
       seconds of a shotgun's pump at a quarter speed, and the right button couldn't end it. */
    if ((players[consoleplayer].cmd.buttons & BT_ATTACK) && Affordable()) left = SHOT_HOLD;
    else if (left > 0 && !menuactive && !paused) left -= dt;
    return left > 0;
}

int SlowMotion_Toggle(void)
{
    static int flag = -1;
    if (flag < 0) flag = M_CheckParm("-slowmotion") != 0;
    if (!flag && !SlowMotion_Option) return 0;
    if (on || left > 0) {on = 0;left = 0;}   /* slowed by a shot or by hand: either way, off */
    else if (Affordable()) on = 1;
    return 1;
}

void SlowMotion_Button(void)
{
    if (SlowMotion_Option != 2) return;   /* the key alone: the key does it, the button stays the game's */
    if (Game_SlowRules()) return;   /* Authored: the right button is the shootslide */
    if (gamestate != GS_LEVEL || menuactive || paused) return;
    if (on || left > 0) {on = 0;left = 0;}   /* a trigger still held slows it again at once */
    else if (Affordable()) on = 1;   /* slow until the next click, shooting or not */
}

void SlowMotion_Frame(void)
{
    static int started;
    if (!started) {started = 1;if (M_CheckParm("-slowmotionon")) on = 1;}   /* test runs have no key */
    static uint64_t before;
    uint64_t now = SDL_GetTicksNS();
    double dt = before ? (now - before) / 1e9 : 0;
    before = now;
    /* The meter ran dry: by hand it is over (the ease takes it out). */
    if (Game_SlowCosts() && !Game_SlowLeft()) {on = 0;left = 0;}
    int hand = on || Shooting(dt);
    int slow = hand || Game_SlowAsked();
    /* The mixer's whoosh in and out (slow_whoosh.c): on the wish, not on a menu covering it. */
    static int was;
    if (slow != was && !netgame && gamestate == GS_LEVEL) { if (slow) ++SlowMotion_Starts; else ++SlowMotion_Ends; }
    was = slow;
    boolean full = !slow || netgame || menuactive || paused || gamestate != GS_LEVEL;
#ifdef DOOM_RENDER3D
    full = full || I_Render3DSettingsOpen();
#endif
    if (full && !slow) { I_TimeScale += (1 - I_TimeScale) * (1 - exp(-dt / EASE_SECONDS)); if (I_TimeScale > 0.995) I_TimeScale = 1; }
    else if (full) I_TimeScale = 1;   /* a menu over it: at once */
    else I_TimeScale += (SLOW_SPEED - I_TimeScale) * (1 - exp(-dt / EASE_SECONDS));
    /* The game's side: what by hand costs, and the gun's own clock (p_focus.c). */
    Game_SlowHand(hand && !full);
    Game_SlowClock(I_TimeScale);
}
