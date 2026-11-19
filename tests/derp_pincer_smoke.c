/* Two imps set out after him at once and come at him from two sides (p_pincer.c), on
   E1M1 as a played run had it: both from the zigzag room, he by the computer room.
   The second takes a way clear of the first's, the first waits out of sight, and they
   go in together.
   doom-derp-pincer-smoke <iwad> */
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
#include "p_pincer.h"
#include "p_senses.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#define STALKING 4	/* LURK_STALKING */
#define U(n) ((n)*FRACUNIT)

static int stage, stagetic, apart = -1;
static mobj_t *imp[2];
static void fail(const char *what) { I_Error("DERP pincer smoke test: %s", what); }
static mobj_t *player(void) { return players[0].mo; }

static void move_to(mobj_t *mo, fixed_t x, fixed_t y)
{
    P_UnsetThingPosition(mo);
    mo->x = x; mo->y = y;
    P_SetThingPosition(mo);
    mo->z = mo->floorz = mo->subsector->sector->floorheight;
    mo->momx = mo->momy = mo->momz = 0;
}

static mobj_t *stalker(fixed_t x, fixed_t y)
{
    mobj_t *mo = P_SpawnMobj(x, y, ONFLOORZ, MT_TROOP);
    mo->target = player();
    mo->mind = MIND_HUNT;
    mo->knownx = player()->x; mo->knowny = player()->y; mo->knowntic = leveltime;
    P_SetMobjState(mo, mo->info->seestate);	/* (Runs its chase: before the stalk.) */
    mo->lurk = STALKING;
    mo->lurktic = leveltime;
    mo->lurkx = x; mo->lurky = y;
    return mo;
}

static int from(mobj_t *mo)
{
    return (int)((R_PointToAngle2(player()->x, player()->y, mo->x, mo->y) >> 24)*360/256);
}

void I_TestFrame(void)
{
    if (gamestate != GS_LEVEL || leveltime < 2) return;
    switch (stage)
    {
      case 0:
        if (!P_DerpAI() || !P_NavReady()) fail("DERP's monsters or their navigation are not on");
        players[0].cheats |= CF_GODMODE;
        move_to(player(), U(2200), U(-2640));
        imp[0] = stalker(U(3440), U(-3472));
        imp[1] = stalker(U(3360), U(-3504));
        P_PincerPair(imp[1], player());
        if (!imp[1]->pincer) fail("two imps set out together and found no second way in");
        printf("Paired: #%d goes round #%d's way in at (%d,%d), %d wide.\n", imp[1]->serial, imp[0]->serial,
               imp[1]->flankx >> FRACBITS, imp[1]->flanky >> FRACBITS, imp[1]->flankr);
        stage = 1; stagetic = leveltime;
        break;
      case 1:
        player()->momx = player()->momy = 0;
        if (!(leveltime % 175))
            printf("  t%d #0 (%d,%d) lurk %d, #1 (%d,%d) lurk %d\n", leveltime - stagetic, imp[0]->x >> FRACBITS, imp[0]->y >> FRACBITS,
                   imp[0]->lurk, imp[1]->x >> FRACBITS, imp[1]->y >> FRACBITS, imp[1]->lurk);
        if (!imp[0]->lurk && !imp[1]->lurk)
        {
            apart = abs(from(imp[0]) - from(imp[1]));
            if (apart > 180) apart = 360 - apart;
            printf("They went in %d degrees apart, after %d seconds.\n", apart, (leveltime - stagetic)/TICRATE);
            if (apart < 45) fail("two imps went in from the same side");
            puts("DERP pincer smoke test passed.");
            I_Quit();
        }
        if (leveltime - stagetic > 70*TICRATE) fail("two imps never went in together");
        break;
    }
}

int main(int argc, char **argv)
{
    if (argc != 2) return 1;
    if (!SDL_Init(SDL_INIT_VIDEO)) return 1;
    char *args[16] = {"derp-pincer-smoke", "-iwad", argv[1], "-warp", "1", "1", "-skill", "3",
                      "-nomonsters", "-nomusic", "-nosound", "-derpai"};
    myargc = 12; myargv = args;
    if (getenv("PINCERLOG")) { args[12] = "-derplog"; args[13] = getenv("PINCERLOG"); myargc = 14; }
    D_DoomMain();
    return 0;
}
