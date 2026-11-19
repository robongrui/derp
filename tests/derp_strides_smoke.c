/* DERP's strides (p_strides.h) on a real level: a zombie walking at the
   player moves a share of its step each tic, not a jump every chase step,
   and covers the same ground as without; with the strides off, the jumps.
   doom-derp-strides-smoke <iwad> */
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

static int stage, stagetic, lasttic;
static void fail(const char *what) { I_Error("DERP strides smoke test: %s", what); }
static mobj_t *player(void) { return players[0].mo; }

static void move_to(mobj_t *mo, fixed_t x, fixed_t y)
{
    P_UnsetThingPosition(mo);
    mo->x = x; mo->y = y;
    P_SetThingPosition(mo);
    mo->z = mo->floorz = mo->subsector->sector->floorheight;
    mo->momx = mo->momy = mo->momz = 0;
}

/* A standable spot lo-hi units from the player, in his sight, with a route to him. */
static boolean find_spot(int lo, int hi, fixed_t *x, fixed_t *y)
{
    int nav = P_NavClassFor(20, 56), cells = P_NavCells();
    static int path[2];
    mobj_t *probe = NULL;
    for (int c = 0; c < cells; c++)
    {
        fixed_t px, py, f, d;
        if (!P_NavStandable(nav, c) || P_NavDoor(nav, c)) continue;
        P_NavPoint(nav, c, &px, &py, &f);
        d = P_AproxDistance(px - player()->x, py - player()->y);
        if (d < lo*FRACUNIT || d > hi*FRACUNIT) continue;
        if (!probe) probe = P_SpawnMobj(px, py, ONFLOORZ, MT_POSSESSED);
        move_to(probe, px, py);
        if (!P_CheckPosition(probe, px, py) || !P_CheckSight(probe, player())) continue;
        if (P_NavPath(nav, px, py, player()->x, player()->y, path, 2, 40000) <= 0) continue;
        *x = px; *y = py;
        P_RemoveMobj(probe);
        return true;
    }
    if (probe) P_RemoveMobj(probe);
    return false;
}

static mobj_t *zombie;
static fixed_t startx, starty, lastx, lasty;
static fixed_t biggest[2], walked[2];

static void send(void)
{
    move_to(zombie, startx, starty);
    zombie->target = player();
    zombie->mind = MIND_HUNT;
    zombie->movecount = 0;
    zombie->reactiontime = 10000;	/* it walks: a stop to shoot in one run and not the other is no measure */
    P_SetMobjState(zombie, zombie->info->seestate);
    lastx = zombie->x; lasty = zombie->y;
    lasttic = leveltime;
}

/* The ground covered each tic (a frame may hold several, or none). */
static void watch(int run)
{
    fixed_t d;
    if (leveltime == lasttic) return;
    d = P_AproxDistance(zombie->x - lastx, zombie->y - lasty)/(leveltime - lasttic);
    if (d > biggest[run]) biggest[run] = d;
    walked[run] += P_AproxDistance(zombie->x - lastx, zombie->y - lasty);
    lastx = zombie->x; lasty = zombie->y;
    lasttic = leveltime;
}

void I_TestFrame(void)
{
    if (gamestate != GS_LEVEL || leveltime < 2) return;
    switch (stage)
    {
      case 0:
        if (!P_DerpAI() || !P_NavReady()) fail("DERP's monsters or their navigation are not on");
        players[0].cheats |= CF_GODMODE;
        /* A walker, not a fighter: in range a soldier holds its ground. */
        derpparts.tactics = false;
        if (!find_spot(400, 700, &startx, &starty)) fail("no spot in sight to walk from");
        zombie = P_SpawnMobj(startx, starty, ONFLOORZ, MT_POSSESSED);
        send(); stage = 1; stagetic = leveltime; break;
      case 1:
        watch(0);
        if (leveltime - stagetic < 2*TICRATE) break;
        derpparts.strides = false;
        send(); stage = 2; stagetic = leveltime; break;
      case 2:
        watch(1);
        if (leveltime - stagetic < 2*TICRATE) break;
        derpparts.strides = true;
        printf("Strides: at most %d units a tic (off: %d), %d units in 2 s (off: %d).\n",
               biggest[0] >> FRACBITS, biggest[1] >> FRACBITS, walked[0] >> FRACBITS, walked[1] >> FRACBITS);
        /* A step is 10 units every 4 tics: walked out, some 3 a tic. */
        if (biggest[0] > 7*FRACUNIT) fail("walking its steps out, a zombie still jumped");
        if (biggest[1] < 9*FRACUNIT) fail("with the strides off, a zombie did not step as vanilla's");
        if (walked[0] < walked[1]*3/4) fail("walking its steps out, a zombie covered less ground");
        puts("DERP strides smoke test passed.");
        I_Quit();
        break;
    }
}

int main(int argc, char **argv)
{
    if (argc != 2) return 1;
    if (!SDL_Init(SDL_INIT_VIDEO)) return 1;
    char *args[16] = {"derp-strides-smoke", "-iwad", argv[1], "-warp", "1", "1", "-skill", "3",
                      "-nomonsters", "-nomusic", "-nosound", "-derpai"};
    myargc = 12; myargv = args;
    D_DoomMain();
    return 0;
}
