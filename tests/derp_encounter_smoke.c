/* What a map keeps in (p_encounter.h), on E1M3: the caged imps behind the
   windows stay in, by jump as by foot, until the gate that holds them opens.
   doom-derp-encounter-smoke <iwad> */
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
#include "p_nav.h"
#include "p_encounter.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

static void fail(const char *what) { I_Error("DERP encounter smoke test: %s", what); }

#define CAGE_X (-2240*FRACUNIT)
#define CAGE_Y (-2500*FRACUNIT)
#define OUT_X (-2150*FRACUNIT)
#define OUT_Y (-2450*FRACUNIT)

static int route(void)
{
    static int path[2];
    int nav = P_NavClassFor(20, 56), n;
    navclimb = true;
    n = P_NavPath(nav, CAGE_X, CAGE_Y, OUT_X, OUT_Y, path, 2, 1 << 22);
    navclimb = false;
    return n;
}

void I_TestFrame(void)
{
    sector_t *cage, *out;

    if (gamestate != GS_LEVEL || leveltime < 2) return;
    if (!P_DerpAI() || !P_NavReady()) fail("DERP's monsters or their navigation are not on");
    cage = R_PointInSubsector(CAGE_X, CAGE_Y)->sector;
    out = R_PointInSubsector(OUT_X, OUT_Y)->sector;
    if (cage == out) fail("this is not the E1M3 it knows");
    if (P_EncounterMayJump(cage, out)) fail("a jump out of the cage is allowed with its gate shut");
    if (route() > 0) fail("a climber has a route out of the cage with its gate shut");
    printf("Held: no jump and no route out of E1M3's cage.\n");
    /* The gate between (sector tag 10, a switch's): up. */
    for (int s = 0; s < numsectors; s++)
        if (sectors[s].tag == 10)
            sectors[s].ceilingheight = sectors[s].floorheight + 128*FRACUNIT;
    P_NavRebuild();
    P_NavClassFor(20, 56);  /* -nomonsters: no layer of the grid until one is asked for */
    if (!P_EncounterMayJump(cage, out)) fail("still held with its gate open");
    printf("Let out: the gate open, it may jump.\n");
    puts("DERP encounter smoke test passed.");
    I_Quit();
}

int main(int argc, char **argv)
{
    if (argc != 2) return 1;
    if (!SDL_Init(SDL_INIT_VIDEO)) return 1;
    char *args[16] = {"derp-encounter-smoke", "-iwad", argv[1], "-warp", "1", "3", "-skill", "3",
                      "-nomonsters", "-nomusic", "-nosound", "-derpai"};
    myargc = 12; myargv = args;
    D_DoomMain();
    return 0;
}
