/* Authored's slow-motion meter (p_focus.c, PLAN-shootslide.md): full at the start, by hand a
   tic a tic, a shootslide a charge at once, a kill a charge back up to the most, nothing
   outside Authored, nothing with the dev tools' free filming; and the gun's own clock in slow
   motion, switching at full speed and firing at the root of the slowing.
   doom-derp-focus-smoke <iwad> */
#include <stdio.h>
#include "doomdef.h"
#include "doomstat.h"
#include "d_main.h"
#include "i_system.h"
#include "m_argv.h"
#include "p_local.h"
#include "p_derp.h"
#include "p_focus.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

static int stage;
#define WEAPONTOP	(32*FRACUNIT)	/* p_pspr.c's */

static void fail(const char *what) { I_Error("DERP focus smoke test: %s", what); }
static player_t *me(void) { return &players[0]; }

/* head: the shot found his head this tic (p_heads.c's mark). */
static void kill_one(mobj_t *source, boolean head)
{
    angle_t an = me()->mo->angle >> ANGLETOFINESHIFT;
    mobj_t *z = P_SpawnMobj(me()->mo->x + 96*finecosine[an], me()->mo->y + 96*finesine[an], ONFLOORZ, MT_POSSESSED);
    if (head) z->headtic = leveltime;
    P_DamageMobj(z, source, source, 10000);
}

static void check_meter(void)
{
    int i;
    if (!P_FocusRules()) fail("Authored has no meter");
    if (me()->focus != FOCUS_MOST) fail("the meter is not full at the start");
    P_FocusHand(true);
    for (i = 0; i < 10; i++) P_FocusTic(me());
    if (me()->focus != FOCUS_MOST - 10 || !P_FocusFlowing()) fail("by hand it did not run down a tic a tic");
    P_FocusHand(false);
    P_FocusTic(me());
    if (me()->focus != FOCUS_MOST - 10 || P_FocusFlowing()) fail("it ran down with nobody slowing it");
    printf("By hand: %d of %d left after 10 slowed tics.\n", me()->focus, FOCUS_MOST);

    me()->focus = FOCUS_CHARGE + 5;
    P_FocusShootslide(me());
    if (me()->focus != 5 || me()->focusasked != FOCUS_SLIDE || !P_FocusAsked()) fail("a shootslide did not pay a charge for its slow motion");
    me()->focusasked = 0;
    P_FocusHand(true);
    P_FocusShootslide(me());
    P_FocusHand(false);
    if (me()->focus != 5 || me()->focusasked) fail("a shootslide paid while slowed by hand");
    me()->focus = 0;
    P_FocusShootslide(me());
    if (me()->focusasked) fail("an empty meter still slowed a shootslide");
    printf("Shootslide: one charge, none by hand, a plain slide when empty.\n");

    kill_one(me()->mo, false);
    if (me()->focus != FOCUS_CHARGE/2) fail("a plain kill did not put half a charge back");
    kill_one(me()->mo, true);
    if (me()->focus != FOCUS_CHARGE/2 + FOCUS_CHARGE) fail("a headshot kill did not put a whole charge back");
    me()->focus = 0;
    me()->focusasked = FOCUS_SLIDE;
    kill_one(me()->mo, false);
    me()->focusasked = 0;
    if (me()->focus != FOCUS_CHARGE) fail("a kill in a shootslide did not put a whole charge back");
    kill_one(NULL, false);
    if (me()->focus != FOCUS_CHARGE) fail("a kill nobody made paid him");
    me()->focus = FOCUS_MOST - 10;
    kill_one(me()->mo, true);
    if (me()->focus != FOCUS_MOST) fail("a kill went past the most");
    printf("Kills: half a charge, a whole one for a headshot or in a shootslide, up to %d.\n", FOCUS_MOST);

    P_FocusFree = 1;
    me()->focus = 0;
    P_FocusShootslide(me());
    P_FocusHand(true);
    P_FocusTic(me());
    P_FocusHand(false);
    if (!me()->focusasked || me()->focus != 0 || P_FocusCosts()) fail("free filming still cost");
    P_FocusFree = 0;
    me()->focusasked = 0;

    derpauthored = false;
    me()->focus = 3;
    P_FocusHand(true);
    P_FocusTic(me());
    P_FocusHand(false);
    kill_one(me()->mo, true);
    derpauthored = true;
    if (me()->focus != 3) fail("the meter moved outside Authored");
    printf("Free filming and vanilla: no price.\n");
    me()->focus = FOCUS_MOST;
}

/* The gun in slow motion: a switch at the world's quarter steps four times a tic, other frames twice. */
static void check_gun(void)
{
    pspdef_t *psp = &me()->psprites[ps_weapon];
    fixed_t was;

    me()->pendingweapon = wp_nochange;
    P_FocusClock(0.25);
    psp->state = &states[S_PISTOLDOWN];psp->tics = 1;psp->sy = WEAPONTOP;me()->focusstep = 0;
    was = psp->sy;
    P_FocusPsprites(me());
    printf("Switching: down %d units in one slowed tic.\n", (psp->sy - was) >> FRACBITS);
    if (psp->sy - was != 4*6*FRACUNIT) fail("a switch in slow motion did not keep full speed");
    psp->state = &states[S_PISTOL1];psp->tics = 4;me()->focusstep = 0;
    P_FocusPsprites(me());
    printf("Firing: %d tics of a 4-tic frame gone in one slowed tic.\n", 4 - psp->tics);
    if (psp->tics != 2) fail("firing in slow motion did not run at half speed");
    P_FocusClock(1);
    psp->state = &states[S_PISTOL1];psp->tics = 4;
    P_FocusPsprites(me());
    if (psp->tics != 3) fail("at full speed the gun did not step once");
    P_SetPsprite(me(), ps_weapon, S_PISTOL);
    psp->sy = WEAPONTOP;
}

void I_TestFrame(void)
{
    if (gamestate != GS_LEVEL || leveltime < 2) return;
    switch (stage)
    {
      case 0:
        if (leveltime < TICRATE) break;
        derpauthored = true;
        players[0].cheats |= CF_GODMODE;
        check_meter(); check_gun();
        puts("DERP focus smoke test passed.");
        I_Quit();
        break;
    }
}

int main(int argc, char **argv)
{
    if (argc != 2) return 1;
    if (!SDL_Init(SDL_INIT_VIDEO)) return 1;
    char *args[16] = {"derp-focus-smoke", "-iwad", argv[1], "-warp", "1", "1", "-skill", "3",
                      "-nomonsters", "-nomusic", "-nosound", "-derpai"};
    myargc = 12; myargv = args;
    D_DoomMain();
    return 0;
}
