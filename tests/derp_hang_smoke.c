/* An imp on the ceiling (p_hang.c), on E1M1: out of his sight it goes up at a high spot
   and hangs there, upside down; he can walk under it; when he has passed, his back to it, it
   hisses, drops behind him and comes down an ordinary imp again.
   doom-derp-hang-smoke <iwad> */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "doomdef.h"
#include "doomstat.h"
#include "d_main.h"
#include "i_system.h"
#include "m_argv.h"
#include "r_main.h"
#include "r_state.h"
#include "p_local.h"
#include "p_derp.h"
#include "p_hang.h"
#include "p_nav.h"
#include "p_senses.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#define U(n) ((n)*FRACUNIT)
#define HUNT_WAITING 3	/* LURK_WAITING */

static int stage, stagetic;
static fixed_t spotx, spoty;
static mobj_t *imp;
static void fail(const char *what) { I_Error("DERP hang smoke test: %s", what); }
static mobj_t *player(void) { return players[0].mo; }

static void move_to(mobj_t *mo, fixed_t x, fixed_t y)
{
    P_UnsetThingPosition(mo);
    mo->x = x; mo->y = y;
    P_SetThingPosition(mo);
    mo->z = mo->floorz = mo->subsector->sector->floorheight;
    mo->ceilingz = mo->subsector->sector->ceilingheight;
    mo->momx = mo->momy = mo->momz = 0;
}

/* A place to hang, with floor a step to one side to stand him on (the same floor). */
static boolean find_spot(mobj_t *probe, int nav, fixed_t *sx, fixed_t *sy, fixed_t *px, fixed_t *py)
{
    for (int y = -5000; y < 1000; y += 32)
        for (int x = -1000; x < 4000; x += 32)
        {
            int cell = P_NavCellAt(nav, U(x), U(y));
            /* (Clear of anything standing there: a barrel, say.) */
            if (cell < 0 || !P_NavStandable(nav, cell) || !P_HangPlace(U(x), U(y)) || !P_CheckPosition(probe, U(x), U(y))) continue;
            for (int k = 0; k < 4; k++)
            {
                static const int d[4][2] = {{112, 0}, {-112, 0}, {0, 112}, {0, -112}};
                int c2 = P_NavCellAt(nav, U(x + d[k][0]), U(y + d[k][1]));
                if (c2 < 0 || !P_NavStandable(nav, c2) || !P_CheckPosition(probe, U(x + d[k][0]), U(y + d[k][1]))
                    || R_PointInSubsector(U(x + d[k][0]), U(y + d[k][1]))->sector != R_PointInSubsector(U(x), U(y))->sector)
                    continue;
                *sx = U(x); *sy = U(y); *px = U(x + d[k][0]); *py = U(y + d[k][1]);
                return true;
            }
        }
    return false;
}

void I_TestFrame(void)
{
    static fixed_t px, py;
    if (gamestate != GS_LEVEL || leveltime < 2) return;
    switch (stage)
    {
      case 0:
      {
        if (!P_DerpAI() || !P_NavReady()) fail("DERP's monsters or their navigation are not on");
        players[0].cheats |= CF_GODMODE;
        imp = P_SpawnMobj(player()->x, player()->y - U(4000), ONFLOORZ, MT_TROOP);
        int nav = P_NavClassOf(imp);
        boolean found = find_spot(imp, nav, &spotx, &spoty, &px, &py);
        P_RemoveMobj(imp);
        if (!found) fail("no high place on E1M1 to hang from");
        imp = P_SpawnMobj(spotx, spoty, ONFLOORZ, MT_TROOP);
        imp->target = player();
        imp->mind = MIND_HUNT;
        imp->lurk = HUNT_WAITING;
        imp->lurktic = leveltime;
        imp->lurkx = imp->x; imp->lurky = imp->y;
        imp->hang = 1;
        P_SetMobjState(imp, imp->info->seestate);
        if (P_CheckSight(player(), imp)) fail("the test's spot is in his sight from the start");
        if (!P_HangUp(imp)) fail("out of his sight at a high place, it did not go up");
        printf("Going up at (%d,%d): floor %d, ceiling %d.\n", spotx >> FRACBITS, spoty >> FRACBITS,
               imp->floorz >> FRACBITS, imp->ceilingz >> FRACBITS);
        stage = 1; stagetic = leveltime;
        break;
      }
      case 1:
        if (imp->hang == 3)
        {
            printf("Hanging after %d tics, its head %d below the ceiling; turned over %.2f.\n", leveltime - stagetic,
                   (imp->ceilingz - imp->z - imp->height) >> FRACBITS, P_HangRoll(imp, 0));
            if (P_HangRoll(imp, 0) < 1) fail("hanging, it is not drawn upside down");
            /* Beside it on its floor, and he can step under it. */
            move_to(player(), px, py);
            if (!P_CheckPosition(player(), imp->x, imp->y)) fail("he cannot walk under a hanging imp");
            /* On past it, his back to it. */
            player()->angle = R_PointToAngle2(imp->x, imp->y, px, py);
            stage = 2; stagetic = leveltime;
        }
        if (leveltime - stagetic > 3*TICRATE) fail("it never got up to the ceiling");
        break;
      case 2:
        player()->momx = player()->momy = 0;
        player()->angle = R_PointToAngle2(imp->x, imp->y, player()->x, player()->y);
        if (imp->hang == 0 && imp->z <= imp->floorz)
        {
            printf("Dropped and down after %d tics, at %d from him.\n", leveltime - stagetic,
                   P_AproxDistance(imp->x - player()->x, imp->y - player()->y) >> FRACBITS);
            if (P_HangRoll(imp, 0) > 0) fail("down again, still drawn turned over");
            puts("DERP hang smoke test passed.");
            I_Quit();
        }
        if (leveltime - stagetic > 4*TICRATE) fail("he walked on under it, back turned, and it never dropped");
        break;
    }
}

int main(int argc, char **argv)
{
    if (argc != 2) return 1;
    if (!SDL_Init(SDL_INIT_VIDEO)) return 1;
    char *args[16] = {"derp-hang-smoke", "-iwad", argv[1], "-warp", "1", "1", "-skill", "4",
                      "-nomonsters", "-nomusic", "-nosound", "-derpai"};
    myargc = 12; myargv = args;
    if (getenv("HANGLOG")) { args[12] = "-derplog"; args[13] = getenv("HANGLOG"); myargc = 14; }
    D_DoomMain();
    return 0;
}
