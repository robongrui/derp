/* An imp through a window, on two maps: E1M3's, its own height (56 units, the sill
   at 88), where the jump goes no higher than the opening lets it in; and E1M1's
   big one, a 16-unit strip with a blocking line either side, where an arc that
   comes down on the strip has to go on across or stay for ever. With "fence":
   an imp come down astride E1M1's monsters' fence, where every step touched it.
   With "secret": no way to the yard on foot until he uses the secret door.
   doom-derp-window-smoke <iwad> <map: 3, or 1> [fence|secret] */
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
#include "p_senses.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

static void fail(const char *what) { I_Error("DERP window smoke test: %s", what); }
static mobj_t *player(void) { return players[0].mo; }

static int map3, fence, secret;

/* Past the window, the far side of it; off the fence, well away from it. */
static boolean through(const mobj_t *mo)
{
    if (fence)
        return P_AproxDistance(mo->x - 3143*FRACUNIT, mo->y + 3499*FRACUNIT) > 48*FRACUNIT;
    return map3 ? mo->y < -2240*FRACUNIT : mo->x < 2736*FRACUNIT;
}

static void move_to(mobj_t *mo, fixed_t x, fixed_t y)
{
    P_UnsetThingPosition(mo);
    mo->x = x; mo->y = y;
    P_SetThingPosition(mo);
    mo->z = mo->floorz = mo->subsector->sector->floorheight;
    mo->momx = mo->momy = mo->momz = 0;
}

static mobj_t *imp;
static int stage, began;

void I_TestFrame(void)
{
    if (gamestate != GS_LEVEL || leveltime < 2) return;
    switch (stage)
    {
      case 0:
        if (!P_DerpAI() || !P_NavReady()) fail("DERP's monsters or their navigation are not on");
        if (secret)
        {
            static int path[2];
            int nav = P_NavClassFor(20, 56), before, after;
            before = P_NavPath(nav, 2980*FRACUNIT, -3840*FRACUNIT, 2880*FRACUNIT, -3840*FRACUNIT, path, 2, 1 << 22);
            if (before > 0) fail("a walker has a way through a secret door nobody has used");
            P_NavSecretFound(&lines[243]);
            after = P_NavPath(nav, 2980*FRACUNIT, -3840*FRACUNIT, 2880*FRACUNIT, -3840*FRACUNIT, path, 2, 1 << 22);
            printf("Secret: route %d cells before, %d after.\n", before, after);
            if (after <= 0) fail("no way through a secret door he has used");
            if (lines[243].flags & ML_SECRET) fail("a used secret door is still secret");
            puts("Secret: no way, then a way.");
            puts("DERP window smoke test passed.");
            I_Quit();
        }
        if (map3)
        {
            move_to(player(), -1976*FRACUNIT, -2300*FRACUNIT);
            imp = P_SpawnMobj(-1950*FRACUNIT, -2195*FRACUNIT, ONFLOORZ, MT_TROOP);
        }
        else if (fence)
        {
            move_to(player(), 2850*FRACUNIT, -3050*FRACUNIT);
            imp = P_SpawnMobj(3143*FRACUNIT, -3499*FRACUNIT, ONFLOORZ, MT_TROOP);
        }
        else
        {
            move_to(player(), 2650*FRACUNIT, -3180*FRACUNIT);
            imp = P_SpawnMobj(2921*FRACUNIT, -3189*FRACUNIT, ONFLOORZ, MT_TROOP);
        }
        P_NavClassFor(20, 56);
        if (fence) imp->leaptic = 1;  /* it came down here */
        imp->target = player();
        imp->mind = MIND_INVESTIGATE;
        imp->goalx = imp->knownx = player()->x;
        imp->goaly = imp->knowny = player()->y;
        imp->goaltic = imp->knowntic = leveltime;
        imp->health = 1000;
        imp->waytic = -1000;
        P_SetMobjState(imp, imp->info->seestate);
        began = leveltime;
        stage = 1;
        break;
      case 1:
        /* Nobody to fight here: it goes on looking where he is. */
        player()->flags |= MF_SHADOW;
        if (through(imp) && imp->z <= imp->floorz)
        {
            printf("Window: through in %d tics, %d failed jumps.\n", leveltime - began, imp->badfails);
            if (imp->badfails) fail("an imp failed the jump and got there another way");
            puts("DERP window smoke test passed.");
            I_Quit();
        }
        if (leveltime - began > 12*TICRATE)
        {
            printf("imp at %d,%d z %d badfails %d\n", imp->x >> FRACBITS, imp->y >> FRACBITS,
                   imp->z >> FRACBITS, imp->badfails);
            fail(fence ? "an imp astride a monsters' fence never got off it"
                       : "an imp did not get through a window");
        }
        break;
    }
}

int main(int argc, char **argv)
{
    if (argc < 3) return 1;
    if (!SDL_Init(SDL_INIT_VIDEO)) return 1;
    map3 = argv[2][0] == '3';
    fence = argc > 3 && argv[3][0] == 'f';
    secret = argc > 3 && argv[3][0] == 's';
    char *args[16] = {"derp-window-smoke", "-iwad", argv[1], "-warp", "1", argv[2], "-skill", "3",
                      "-nomonsters", "-nomusic", "-nosound", "-derpai"};
    myargc = 12; myargv = args;
    D_DoomMain();
    return 0;
}
