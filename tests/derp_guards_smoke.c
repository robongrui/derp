/* DERP's key guards (p_tactics.c) on a real level: monsters near a key hold
   it. A far noise readies a guard (alert) and it stays near the key, at a
   post if it takes one; led too far and out of his sight it goes back; the
   key taken, the duty ends.
   doom-derp-guards-smoke <iwad> */
#include <stdio.h>
#include <stdlib.h>
#include "doomdef.h"
#include "doomstat.h"
#include "d_main.h"
#include "i_system.h"
#include "m_argv.h"
#include "r_main.h"
#include "r_state.h"
#include "p_local.h"
#include "p_derp.h"
#include "p_nav.h"
#include "p_senses.h"
#include "p_tactics.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

static int stage, stagetic, guards;
static mobj_t *key, *guard;
static fixed_t startx, starty;

static void fail(const char *what) { I_Error("DERP guards smoke test: %s", what); }
static mobj_t *player(void) { return players[0].mo; }

static void move_to(mobj_t *mo, fixed_t x, fixed_t y)
{
    P_UnsetThingPosition(mo);
    mo->x = x; mo->y = y;
    P_SetThingPosition(mo);
    mo->z = mo->floorz = mo->subsector->sector->floorheight;
    mo->momx = mo->momy = mo->momz = 0;
}

/* A key with a guard asleep, out of his sight. */
static boolean find_guard(void)
{
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next)
    {
        mobj_t *mo = (mobj_t *)th;
        if (th->function.acp1 != (actionf_p1)P_MobjThinker || !mo->guard) continue;
        if (mo->guard >= 0x20000000) continue;	/* A door or a switch: this is about keys. */
        guards++;
        if (guard || mo->mind != MIND_NONE || P_CheckSight(mo, player())) continue;
        guard = mo;
    }
    if (!guard) return false;
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next)
    {
        mobj_t *mo = (mobj_t *)th;
        if (th->function.acp1 == (actionf_p1)P_MobjThinker && mo->serial == guard->guard) key = mo;
    }
    return key != NULL;
}

/* A standable spot from lo units of the key on, out of the guard's sight. */
static boolean far_spot(int lo, fixed_t *x, fixed_t *y, int skip)
{
    int nav = P_NavClassFor(20, 56), cells = P_NavCells();
    for (int c = 0; c < cells; c += 13)
    {
        fixed_t px, py, pf, d;
        if (!P_NavStandable(nav, c) || P_NavDoor(nav, c)) continue;
        P_NavPoint(nav, c, &px, &py, &pf);
        d = P_AproxDistance(px - guard->guardx, py - guard->guardy);
        if (d < lo*FRACUNIT || d > (lo + 500)*FRACUNIT) continue;
        if (skip-- > 0) continue;
        *x = px; *y = py;
        return true;
    }
    return false;
}

void I_TestFrame(void)
{
    fixed_t x, y;
    if (gamestate != GS_LEVEL || leveltime < 2) return;
    player()->momx = player()->momy = 0;
    switch (stage)
    {
      case 0:
        if (!P_DerpAI() || !P_NavReady()) fail("DERP's monsters or their navigation are not on");
        players[0].cheats |= CF_GODMODE;
        if (!find_guard())
        {
            printf("No guard asleep out of his sight here (%d guards).\n", guards);
            fail("no key with a guard on this map");
        }
        printf("Guards: %d on this map; #%d holds a key %d units off.\n", guards, guard->serial,
               P_AproxDistance(guard->x - key->x, guard->y - key->y) >> FRACBITS);
        startx = guard->x; starty = guard->y;
        /* A loud noise far from the key, near enough to be heard. */
        for (int k = 0; k < 400 && !guard->alert; k += 20)
        {
            if (!far_spot(850, &x, &y, k)) break;
            P_DerpNoise(player(), NOISE_BFG, x, y);
        }
        if (!guard->alert) fail("the guard heard no far noise");
        if (guard->mind != MIND_NONE) fail("a far noise took a guard from its key");
        stage = 1; stagetic = leveltime;
        break;
      case 1:
        if (leveltime - stagetic < 3*TICRATE) break;
        printf("Far noise: alert %d, %d units from its post after 3 s, mind %d.\n", guard->alert,
               P_AproxDistance(guard->x - startx, guard->y - starty) >> FRACBITS, guard->mind);
        /* Where it stood, or at a post near its key (a soldier takes one). */
        if (guard->mind != MIND_NONE && guard->mind != MIND_AMBUSH)
            fail("a far noise sent a guard off to look");
        if (P_AproxDistance(guard->x - guard->guardx, guard->y - guard->guardy) > 800*FRACUNIT)
            fail("a guard left its key for a far noise");
        /* Led far off, he out of sight: back. */
        if (!far_spot(1100, &x, &y, 0)) { puts("No spot far from the key: leash not tried."); stage = 3; break; }
        move_to(guard, x, y);
        guard->target = player();
        guard->mind = MIND_HUNT;
        guard->knownx = x; guard->knowny = y; guard->knowntic = leveltime;
        P_SetMobjState(guard, guard->info->seestate);
        stage = 2; stagetic = leveltime;
        break;
      case 2:
        if (guard->health <= 0) { puts("The guard died: leash not judged."); stage = 3; break; }
        if (guard->mind == MIND_RETURN) { printf("Leash: back after %d tics.\n", leveltime - stagetic); stage = 3; break; }
        if (leveltime - stagetic > 3*TICRATE)
        {
            if (P_CheckSight(guard, player())) { puts("It sees him: leash not judged."); stage = 3; break; }
            fail("a guard led far from its key did not go back");
        }
        break;
      case 3:
        P_DerpKeyTaken(key);
        if (guard->guard) fail("the key taken, a guard still guards it");
        puts("DERP guards smoke test passed.");
        I_Quit();
        break;
    }
}

int main(int argc, char **argv)
{
    if (argc != 2) return 1;
    if (!SDL_Init(SDL_INIT_VIDEO)) return 1;
    char *args[16] = {"derp-guards-smoke", "-iwad", argv[1], "-warp", "1", "2", "-skill", "3",
                      "-nomusic", "-nosound", "-derpai"};
    myargc = 11; myargv = args;
    D_DoomMain();
    return 0;
}
