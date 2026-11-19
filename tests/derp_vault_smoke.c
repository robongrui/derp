/* An imp facing him in the open jumps over his head and comes down behind him
   (p_demons.c), on E1M1's computer room, ceiling high enough. It clears him, it does
   not strike him in the air, and neither is left stuck on the other.
   doom-derp-vault-smoke <iwad> */
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

#define U(n) ((n)*FRACUNIT)

static int stage, stagetic, starthealth;
static fixed_t rushfrom;
static fixed_t over;	/* how high it was when it passed him */
static mobj_t *imp, *pair[2];
static void fail(const char *what) { I_Error("DERP vault smoke test: %s", what); }
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

static mobj_t *hunter(fixed_t x, fixed_t y)
{
    mobj_t *mo = P_SpawnMobj(x, y, ONFLOORZ, MT_TROOP);
    mo->target = player();
    mo->mind = MIND_HUNT;
    mo->knownx = player()->x; mo->knowny = player()->y; mo->knowntic = leveltime;
    P_SetMobjState(mo, mo->info->seestate);
    return mo;
}

/* Which side of him it is on, along the line it started on. */
static fixed_t side(mobj_t *mo) { return mo->x - player()->x; }

static void face_it(void)
{
    player()->angle = R_PointToAngle2(player()->x, player()->y, imp->x, imp->y);
    player()->momx = player()->momy = 0;
}

void I_TestFrame(void)
{
    if (gamestate != GS_LEVEL || leveltime < 2) return;
    switch (stage)
    {
      case 0:
        if (!P_DerpAI() || !P_NavReady()) fail("DERP's monsters or their navigation are not on");
        players[0].cheats |= CF_GODMODE;
        move_to(player(), U(1900), U(-2400));
        imp = hunter(U(2140), U(-2400));
        stage = 1; stagetic = leveltime;
        break;
      case 1:
        face_it();
        /* Kept at leap range until it jumps: this is not about its other moves. */
        if (!imp->vault && P_AproxDistance(imp->x - player()->x, imp->y - player()->y) < U(160))
            move_to(imp, U(2140), U(-2400));
        imp->health = imp->info->spawnhealth;
        if (imp->vault)
        {
            printf("It crouched after %d tics, from %d units off.\n", leveltime - stagetic,
                   P_AproxDistance(imp->x - player()->x, imp->y - player()->y) >> FRACBITS);
            starthealth = player()->health;
            stage = 2; stagetic = leveltime;
        }
        if (leveltime - stagetic > 60*TICRATE) fail("in sixty seconds in the open an imp never jumped over him");
        break;
      case 2:
        player()->momx = player()->momy = 0;
        if (abs(side(imp)) < U(36) && imp->z > over) over = imp->z;
        if (!imp->pounce)
        {
            printf("Down at (%d,%d), %d units past him, %d tics, highest over him %d.\n", imp->x >> FRACBITS, imp->y >> FRACBITS,
                   (int)(abs(side(imp)) >> FRACBITS), leveltime - stagetic, over >> FRACBITS);
            if (side(imp) > 0) fail("it came down on the side it left");
            if (over < player()->z + player()->height) fail("it went through him, not over him");
            if (player()->health < starthealth) fail("it struck him on the way over");
            stage = 3; stagetic = leveltime;
        }
        if (leveltime - stagetic > 3*TICRATE) fail("a jump over him never came down");
        break;
      case 3:
        /* Both free to walk: he can step, and it is an ordinary thing again. */
        if (leveltime - stagetic == TICRATE)
        {
            if (imp->vault && P_AproxDistance(imp->x - player()->x, imp->y - player()->y) >= U(36))
                fail("it landed clear and is still marked as going over him");
            if (!P_TryMove(player(), player()->x + U(8), player()->y)
                && !P_TryMove(player(), player()->x - U(8), player()->y))
                fail("after the jump he cannot step either way");
            /* Two come down on one spot, as a pair off a ledge did: they get out of it. */
            move_to(player(), U(2900), U(-2760));
            pair[0] = hunter(U(3223), U(-3330));
            pair[1] = hunter(U(3215), U(-3348));
            stage = 4; stagetic = leveltime;
        }
        break;
      case 4:
        player()->momx = player()->momy = 0;
        if (P_AproxDistance(pair[0]->x - U(3223), pair[0]->y + U(3330)) > U(32)
            && P_AproxDistance(pair[1]->x - U(3215), pair[1]->y + U(3348)) > U(32))
        {
            printf("The pair got out of each other in %d tics.\n", leveltime - stagetic);
            /* One badly hurt with no healing left: it comes at him, and flinches at nothing. */
            P_RemoveMobj(pair[0]); P_RemoveMobj(pair[1]); P_RemoveMobj(imp);
            move_to(player(), U(1900), U(-2400));
            imp = hunter(U(2300), U(-2400));
            imp->health = 15;
            imp->healed = imp->info->spawnhealth;
            stage = 5; stagetic = leveltime;
        }
        if (leveltime - stagetic > 5*TICRATE) fail("two imps on one spot could not move");
        break;
      case 5:
        face_it();
        if (imp->rushtic)
        {
            statenum_t before = imp->state - states;
            int health = imp->health;
            P_DamageMobj(imp, NULL, player(), 1);
            if (imp->health >= health) fail("the test's blow did not land");
            if (imp->state - states == imp->info->painstate && before != imp->info->painstate) fail("its last rush flinched");
            rushfrom = P_AproxDistance(imp->x - player()->x, imp->y - player()->y);
            printf("Last rush from %d units off, after %d tics.\n", rushfrom >> FRACBITS, leveltime - stagetic);
            stage = 6; stagetic = leveltime;
        }
        if (leveltime - stagetic > 15*TICRATE) fail("hurt, with no healing left and him in sight, an imp never rushed him");
        break;
      case 6:
        face_it();
        if (P_AproxDistance(imp->x - player()->x, imp->y - player()->y) < U(72))
        {
            int tics = leveltime - stagetic;
            printf("At him in %d tics: %d units a tic.\n", tics, tics ? (rushfrom >> FRACBITS)/tics : 0);
            if (tics && (rushfrom >> FRACBITS)/tics < 7) fail("that was no sprint");
            puts("DERP vault smoke test passed.");
            I_Quit();
        }
        if (leveltime - stagetic > 6*TICRATE) fail("its last rush never reached him");
        break;
    }
}

int main(int argc, char **argv)
{
    if (argc != 2) return 1;
    if (!SDL_Init(SDL_INIT_VIDEO)) return 1;
    char *args[16] = {"derp-vault-smoke", "-iwad", argv[1], "-warp", "1", "1", "-skill", "4",
                      "-nomonsters", "-nomusic", "-nosound", "-derpai"};
    myargc = 12; myargv = args;
    if (getenv("VAULTLOG")) { args[12] = "-derplog"; args[13] = getenv("VAULTLOG"); myargc = 14; }
    D_DoomMain();
    return 0;
}
