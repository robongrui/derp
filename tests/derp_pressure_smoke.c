/* DERP's pressure budget (p_director.c) on a real level: a dozen monsters out
   of sight hear one far shot; some set off, the rest ready where they stand
   (alert, not gone) instead of the whole level coming at once. With the
   director off they all go.
   doom-derp-pressure-smoke <iwad> */
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
#include "p_director.h"
#include "p_bullets.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#define MOST 8
#define CROWD 16
static mobj_t *gun;
static int stagetic;
static int stage, count;
static mobj_t *crowd[CROWD];

static void fail(const char *what) { I_Error("DERP pressure smoke test: %s", what); }
static mobj_t *player(void) { return players[0].mo; }

/* Out of his sight, 520 to 800 units off, a walk from him. */
static void place(void)
{
    static int path[2];
    int nav = P_NavClassFor(20, 56), cells = P_NavCells();
    count = 0;
    for (int c = 0; c < cells && count < CROWD; c += 41)
    {
        fixed_t px, py, pf, d;
        boolean near = false;
        if (!P_NavStandable(nav, c) || P_NavDoor(nav, c) || P_NavHurts(c)) continue;
        P_NavPoint(nav, c, &px, &py, &pf);
        d = P_AproxDistance(px - player()->x, py - player()->y);
        if (d < 520*FRACUNIT || d > 800*FRACUNIT) continue;
        for (int k = 0; k < count; k++)
            if (P_AproxDistance(crowd[k]->x - px, crowd[k]->y - py) < 64*FRACUNIT) near = true;
        if (near || P_NavPath(nav, px, py, player()->x, player()->y, path, 2, 40000) <= 0) continue;
        mobj_t *mo = P_SpawnMobj(px, py, ONFLOORZ, MT_POSSESSED);
        if (!P_CheckPosition(mo, px, py) || P_CheckSight(mo, player())) { P_RemoveMobj(mo); continue; }
        crowd[count++] = mo;
    }
}

static void tally(int *went, int *ready)
{
    *went = *ready = 0;
    for (int k = 0; k < count; k++)
    {
        if (crowd[k]->mind == MIND_INVESTIGATE) ++*went;
        else if (crowd[k]->mind == MIND_NONE && crowd[k]->alert) ++*ready;
    }
}

void I_TestFrame(void)
{
    int went, ready;
    if (gamestate != GS_LEVEL || leveltime < 2) return;
    switch (stage)
    {
      case 0:
        if (!P_DerpAI() || !P_NavReady()) fail("DERP's monsters or their navigation are not on");
        players[0].cheats |= CF_GODMODE;
        /* Somewhere with room for a crowd round him. */
        {
            int nav = P_NavClassFor(20, 56), cells = P_NavCells();
            for (int c = 0; c < cells && count <= MOST; c += 997)
            {
                fixed_t px, py, pf;
                if (!P_NavStandable(nav, c) || P_NavDoor(nav, c) || P_NavHurts(c)) continue;
                P_NavPoint(nav, c, &px, &py, &pf);
                for (int k = 0; k < count; k++) P_RemoveMobj(crowd[k]);
                count = 0;
                P_UnsetThingPosition(player());
                player()->x = px; player()->y = py;
                P_SetThingPosition(player());
                player()->z = player()->floorz = player()->subsector->sector->floorheight;
                place();
            }
        }
        printf("Crowd: %d zombies out of sight of (%d,%d).\n", count, player()->x >> FRACBITS, player()->y >> FRACBITS);
        if (count <= MOST) fail("no place on this map with a crowd round it");
        /* Through a wall: out of a pistol's reach by the openings, near enough
           in a straight line, it hears it all the same. */
        {
            mobj_t *walled = NULL;
            for (int k = 0; k < count && !walled; k++)
                if (P_NoiseDistance(player()->x, player()->y, crowd[k]->x, crowd[k]->y, 6000) > 1100
                    && P_AproxDistance(crowd[k]->x - player()->x, crowd[k]->y - player()->y) < 640*FRACUNIT)
                    walled = crowd[k];
            players[0].readyweapon = wp_pistol;
            P_DerpNoiseAlert(player(), player());
            tally(&went, &ready);
            printf("A pistol: %d of %d heard it%s.\n", went + ready, count, walled ? ", one through a wall" : "");
            if (walled && walled->mind == MIND_NONE && !walled->alert) fail("a pistol shot did not carry through a wall");
            for (int k = 0; k < count; k++) { crowd[k]->mind = MIND_NONE; crowd[k]->alert = 0; crowd[k]->target = NULL; }
            P_DirectorLevel();
        }
        players[0].readyweapon = wp_bfg;	/* heard by all: the budget is what is judged */
        P_DerpNoiseAlert(player(), player());
        tally(&went, &ready);
        printf("One shot: %d set off, %d ready where they stand, %d heard nothing.\n", went, ready, count - went - ready);
        if (went > 4) fail("more set off on one noise than a wave allows");
        if (went + ready > MOST && !ready) fail("those over the budget did not ready");
        stage = 1;
        break;
      case 1:
        /* With the director off, the old way: all who hear go. */
        for (int k = 0; k < count; k++) { crowd[k]->mind = MIND_NONE; crowd[k]->alert = 0; crowd[k]->target = NULL; }
        derpparts.director = false;
        P_DerpNoiseAlert(player(), player());
        tally(&went, &ready);
        derpparts.director = true;
        printf("Director off: %d set off, %d ready.\n", went, ready);
        if (ready) fail("with the director off a monster was held back");
        /* A shout beside them, of him a long way off: far, however near the shouter. */
        for (int k = 0; k < count; k++) { crowd[k]->mind = MIND_NONE; crowd[k]->alert = 0; crowd[k]->target = NULL; }
        P_DirectorLevel();
        crowd[0]->knownx = player()->x; crowd[0]->knowny = player()->y; crowd[0]->knowntic = leveltime;
        crowd[0]->target = player();
        crowd[0]->mind = MIND_HUNT;
        P_DerpNoise(crowd[0], NOISE_SHOUT, crowd[0]->x, crowd[0]->y);
        tally(&went, &ready);
        printf("A shout: %d set off, %d ready.\n", went, ready);
        if (went > 5) fail("a shout sent everybody round the shouter");
        /* The budget full, then a fight going on and the director pressing: the rest listen. */
        for (int k = 0; k < count; k++) { crowd[k]->mind = MIND_NONE; crowd[k]->alert = 0; crowd[k]->target = NULL; }
        P_DirectorLevel();
        P_DirectorStance(true);
        for (int k = 0; k < MOST; k++)
        {
            crowd[k]->target = player(); crowd[k]->mind = MIND_HUNT; crowd[k]->reactiontime = 10000;
            crowd[k]->knowntic = leveltime; crowd[k]->knownx = crowd[k]->x; crowd[k]->knowny = crowd[k]->y;
        }
        gun = P_SpawnMobj(player()->x, player()->y, ONFLOORZ, MT_POSSESSED);
        gun->flags &= ~MF_SOLID;
        gun->reactiontime = 10000;
        stage = 2; stagetic = leveltime;
        break;
      case 2:
        for (int k = 0; k < MOST; k++) { crowd[k]->knowntic = leveltime; crowd[k]->mind = MIND_HUNT; }
        if (leveltime - stagetic < 12) break;
        /* Quiet: nobody more. */
        P_DerpNoiseAlert(player(), player());
        tally(&went, &ready);
        printf("Budget full, no fight on: %d more set off.\n", went);
        if (went) fail("with the budget full and no fight on, more set off");
        for (int k = MOST; k < count; k++) { crowd[k]->mind = MIND_NONE; crowd[k]->target = NULL; }
        stage = 3; stagetic = leveltime;
        break;
      case 3:
        for (int k = 0; k < MOST; k++) { crowd[k]->knowntic = leveltime; crowd[k]->mind = MIND_HUNT; }
        if (!(leveltime % 4)) P_FireBulletAt(gun, player()->x + 48*FRACUNIT, player()->y, player()->z + 32*FRACUNIT, 0);
        if (leveltime - stagetic < TICRATE) break;
        P_DerpNoiseAlert(player(), player());
        tally(&went, &ready);
        printf("A fight on: %d more came to it, the budget being full.\n", went);
        if (!went) fail("with a fight going on near them, nobody went in");
        if (went > 4) fail("a fight going on sent more than a wave");
        puts("DERP pressure smoke test passed.");
        I_Quit();
        break;
    }
}

int main(int argc, char **argv)
{
    if (argc != 2) return 1;
    if (!SDL_Init(SDL_INIT_VIDEO)) return 1;
    char *args[16] = {"derp-pressure-smoke", "-iwad", argv[1], "-warp", "1", "3", "-skill", "3",
                      "-nomonsters", "-nomusic", "-nosound", "-derpai"};
    myargc = 12; myargv = args;
    D_DoomMain();
    return 0;
}
