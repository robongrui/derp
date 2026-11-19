/* A pinky's charge (p_charge.h) on E1M1: it crosses the room far faster than
   it walks and lands on a player who stands there; one that misses ends it.
   doom-derp-charge-smoke <iwad> */
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
#include "tables.h"
#include "p_local.h"
#include "p_derp.h"
#include "p_charge.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

static void fail(const char *what) { I_Error("DERP charge smoke test: %s", what); }
static mobj_t *player(void) { return players[0].mo; }

static mobj_t *pinky;
static int stage, began, health;
static fixed_t fromx, fromy;

void I_TestFrame(void)
{
    if (gamestate != GS_LEVEL || leveltime < 2) return;
    switch (stage)
    {
      case 0:
      {
        angle_t an = player()->angle >> ANGLETOFINESHIFT;
        if (!P_DerpAI()) fail("DERP's monsters are not on");
        fromx = player()->x + 224*finecosine[an];
        fromy = player()->y + 224*finesine[an];
        pinky = P_SpawnMobj(fromx, fromy, ONFLOORZ, MT_SERGEANT);
        pinky->target = player();
        pinky->angle = R_PointToAngle2(pinky->x, pinky->y, player()->x, player()->y);
        pinky->health = 1000;
        if (!P_CheckSight(pinky, player())) fail("no line to him on this map");
        health = player()->health;
        began = leveltime;
        P_ChargeBegin(pinky);
        stage = 1;
        break;
      }
      case 1:
        if (P_Charging(pinky))
        {
            if (leveltime - began > 3*TICRATE) fail("a charge did not end");
            break;
        }
        printf("Charge: %d tics, %d units, him %d -> %d.\n", leveltime - began,
               P_AproxDistance(pinky->x - fromx, pinky->y - fromy) >> FRACBITS, health, player()->health);
        if (leveltime - began > 2*TICRATE) fail("a charge took too long to cross the room");
        if (player()->health >= health) fail("a charge did not land on a player standing there");
        puts("DERP charge smoke test passed.");
        I_Quit();
        break;
    }
}

int main(int argc, char **argv)
{
    if (argc != 2) return 1;
    if (!SDL_Init(SDL_INIT_VIDEO)) return 1;
    char *args[16] = {"derp-charge-smoke", "-iwad", argv[1], "-warp", "1", "1", "-skill", "3",
                      "-nomonsters", "-nomusic", "-nosound", "-derpai"};
    myargc = 12; myargv = args;
    D_DoomMain();
    return 0;
}
