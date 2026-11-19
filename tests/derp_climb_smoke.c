/* DERP's climbers (p_nav.h's navclimb, p_senses.c's Leap) on a real level:
   an imp below a ledge it can only climb (no walkable way up, or a long way
   round) gets up to the player on top, with a jump; a walker's route
   finds no such way.
   doom-derp-climb-smoke <iwad> */
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
#include "p_demons.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

static int stage, stagetic;
static void fail(const char *what) { I_Error("DERP climb smoke test: %s", what); }
static mobj_t *player(void) { return players[0].mo; }

static void move_to(mobj_t *mo, fixed_t x, fixed_t y)
{
    P_UnsetThingPosition(mo);
    mo->x = x; mo->y = y;
    P_SetThingPosition(mo);
    mo->z = mo->floorz = mo->subsector->sector->floorheight;
    mo->momx = mo->momy = mo->momz = 0;
}

/* A floor that hurts: not stepped onto from a dry one, but for a route; out of it again. */
static void check_nukage(void)
{
    mobj_t *mo = P_SpawnMobj(player()->x, player()->y, ONFLOORZ, MT_POSSESSED);
    fixed_t dx, dy;
    int s;
    P_NavClassFor(20, 56);
    for (s = 0; s < numsectors; s++)
    {
        fixed_t x = sectors[s].soundorg.x, y = sectors[s].soundorg.y;
        if (sectors[s].special != 7 || R_PointInSubsector(x, y)->sector != &sectors[s]) continue;
        if (!P_DerpKeepsOut(mo, x, y)) fail("a monster may step into nukage");
        derproute = true;
        if (P_DerpKeepsOut(mo, x, y)) fail("a route through nukage is refused");
        derproute = false;
        move_to(mo, x, y);
        if (!P_DerpBurning(mo)) fail("a monster in nukage does not know it");
        if (P_DerpKeepsOut(mo, x + 8*FRACUNIT, y)) fail("a monster in nukage may not move");
        if (!P_NavDryNear(P_NavClassOf(mo), x, y, &dx, &dy)) fail("no dry floor near the nukage");
        if (R_PointInSubsector(dx, dy)->sector->special == 7) fail("dry floor is nukage");
        printf("Nukage: kept out, and out of it by (%d,%d).\n", dx >> FRACBITS, dy >> FRACBITS);
        P_RemoveMobj(mo);
        return;
    }
    fail("no nukage to try on this map");
}

static mobj_t *imp;
static fixed_t topx, topy, topz, lowx, lowy;
static int rise, walkcells, climbcells;

/* A ledge a climber goes up: a low cell and a high one 40-100 units above
   it, 2-4 cells apart, a climber's way between them short and a walker's
   none or well round (stairs elsewhere). */
static int pairs, climbable, walkable;
static boolean find_ledge(void)
{
    static const int way[8][2] = {{1,0},{-1,0},{0,1},{0,-1},{1,1},{1,-1},{-1,1},{-1,-1}};
    static int path[64];
    int nav = P_NavClassFor(20, 56), cells = P_NavCells();
    for (int c = 0; c < cells; c++)
    {
        fixed_t cx, cy, cf;
        if (!P_NavStandable(nav, c)) continue;
        P_NavPoint(nav, c, &cx, &cy, &cf);
        for (int k = 0; k < 8; k++)
            for (int d = 2; d <= 4; d++)
            {
                fixed_t tx, ty, tf;
                int t = P_NavCellAt(nav, cx + way[k][0]*d*NAVCELL*FRACUNIT, cy + way[k][1]*d*NAVCELL*FRACUNIT), w, cl;
                if (t < 0) continue;
                P_NavPoint(nav, t, &tx, &ty, &tf);
                if (tf - cf < 40*FRACUNIT || tf - cf > 100*FRACUNIT) continue;
                pairs++;
                navclimb = true;
                cl = P_NavPath(nav, cx, cy, tx, ty, path, 64, 4000);
                navclimb = false;
                if (cl < 2 || cl > 8) continue;
                climbable++;
                w = P_NavPath(nav, cx, cy, tx, ty, path, 64, 20000);
                if (w && w < 2*cl + 10) { walkable++; continue; }
                lowx = cx; lowy = cy; topx = tx; topy = ty; topz = tf;
                rise = (tf - cf) >> FRACBITS; walkcells = w; climbcells = cl;
                return true;
            }
    }
    return false;
}

static mobj_t *held;
static fixed_t heldx, heldy;

/* A dry place with no way to the player, jumps included. */
static boolean held_spot(void)
{
    static int path[2];
    int nav = P_NavClassFor(20, 56), cells = P_NavCells(), tries = 0;
    for (int c = 0; c < cells && tries < 60; c += 211)
    {
        fixed_t px, py, pf;
        int n;
        if (!P_NavStandable(nav, c) || P_NavDoor(nav, c) || P_NavHurts(c)) continue;
        P_NavPoint(nav, c, &px, &py, &pf);
        tries++;
        navclimb = true;
        n = P_NavPath(nav, px, py, player()->x, player()->y, path, 2, 1 << 22);
        navclimb = false;
        if (n > 0) continue;
        heldx = px; heldy = py;
        return true;
    }
    return false;
}

void I_TestFrame(void)
{
    if (gamestate != GS_LEVEL || leveltime < 2) return;
    switch (stage)
    {
      case 0:
        if (!P_DerpAI() || !P_NavReady()) fail("DERP's monsters or their navigation are not on");
        check_nukage();
        players[0].cheats |= CF_GODMODE;
        if (!find_ledge()) { printf("ledges: %d pairs, %d climbable, %d also walkable\n", pairs, climbable, walkable); fail("no ledge on this map only a climber gets up"); }
        move_to(player(), topx, topy);
        imp = P_SpawnMobj(lowx, lowy, ONFLOORZ, MT_TROOP);
        imp->target = player();
        imp->mind = MIND_HUNT;
        imp->knownx = topx; imp->knowny = topy; imp->knowntic = leveltime;
        imp->health = 1000;
        imp->pouncetic = leveltime;
        P_SetMobjState(imp, imp->info->seestate);
        stage = 1; stagetic = leveltime; break;
      case 1:
        if (getenv("CLIMBTRACE") && (imp->z > imp->floorz || leveltime - imp->leaptic < 3))
            printf("t%d imp at %d,%d z %d floor %d ceil %d mom %d,%d,%d leap %d,%d land %d,%d,%d\n", leveltime,
                   imp->x >> FRACBITS, imp->y >> FRACBITS, imp->z >> FRACBITS, imp->floorz >> FRACBITS,
                   imp->ceilingz >> FRACBITS, imp->momx >> FRACBITS, imp->momy >> FRACBITS, imp->momz >> FRACBITS,
                   imp->leapx >> FRACBITS, imp->leapy >> FRACBITS, imp->landx >> FRACBITS, imp->landy >> FRACBITS,
                   imp->landz >> FRACBITS);
        if (imp->floorz >= topz - FRACUNIT && imp->z <= imp->floorz && imp->leaptic)
        {
            printf("Climb: an imp got up a %d-unit ledge in %d tics (a walker's way: %s; a climber's: %d cells).\n",
                   rise, leveltime - stagetic, walkcells ? "the long way round" : "none", climbcells);
            /* The ledge's way has its jump (no step between two cells, P_NavJump);
               a jump that did not land twice is left out of its routes. */
            {
                static int path[64];
                int nav = P_NavClassFor(20, 56), n, j, from = -1, to = -1;
                navclimb = true;
                n = P_NavPath(nav, lowx, lowy, topx, topy, path, 64, 4000);
                navclimb = false;
                for (j = 1; j < n && from < 0; j++)
                    if (P_NavJump(nav, path[j - 1], path[j])) { from = path[j - 1]; to = path[j]; }
                if (from < 0) fail("the ledge's way has no jump in it");
                imp->badfails = 0;
                for (j = 0; j < 2; j++)
                {
                    imp->jumping = true;
                    imp->jumpfrom = from; imp->jumpto = to;
                    imp->leaptic = leveltime - 10;
                    imp->landx = imp->x + 200*FRACUNIT; imp->landy = imp->y;
                    imp->landz = imp->floorz + 100*FRACUNIT;
                    P_DerpJumped(imp);
                }
                if (imp->badfails != 2 || imp->badfrom != from || imp->badto != to)
                    fail("two jumps that did not land were not counted against that jump");
                navclimb = true;
                navbadfrom = from; navbadto = to;
                n = P_NavPath(nav, lowx, lowy, topx, topy, path, 64, 4000);
                navbadfrom = navbadto = -1;
                navclimb = false;
                for (j = 1; j < n; j++)
                    if (path[j - 1] == from && path[j] == to) fail("a failed jump is still on the way");
                printf("Failed jump: counted twice, left out of the way (%d cells %s).\n", n, n ? "round" : ": no other way");
            }
            /* A place with no way to him, not even a jump (a map's cage, a
               closet): an imp there, told of him, stays. */
            if (held_spot())
            {
                held = P_SpawnMobj(heldx, heldy, ONFLOORZ, MT_TROOP);
                held->target = player();
                held->mind = MIND_INVESTIGATE;
                held->goalx = player()->x; held->goaly = player()->y;
                held->goaltic = leveltime; held->waytic = -1000;
                held->health = 1000;
                P_SetMobjState(held, held->info->seestate);
                stage = 2; stagetic = leveltime;
                break;
            }
            puts("No place shut off from him on this map: held group not tried.");
            puts("DERP climb smoke test passed.");
            I_Quit();
        }
        if (leveltime - stagetic > 8*TICRATE)
        {
            printf("imp at %d,%d z %d floor %d (top %d) leaptic %d ready %d\n", imp->x >> FRACBITS, imp->y >> FRACBITS,
                   imp->z >> FRACBITS, imp->floorz >> FRACBITS, topz >> FRACBITS, imp->leaptic, imp->leapready);
            fail("an imp did not climb the ledge to the player");
        }
        break;
      case 2:
        if (leveltime - stagetic < 5*TICRATE) break;
        printf("Held: an imp shut off from him is %d units from where it was after 5 s, %s.\n",
               P_AproxDistance(held->x - heldx, held->y - heldy) >> FRACBITS, held->blinktic ? "blinked" : "no blink");
        if (held->blinktic) fail("an imp shut off from him blinked out");
        puts("DERP climb smoke test passed.");
        I_Quit();
        break;
    }
}

int main(int argc, char **argv)
{
    if (argc != 2) return 1;
    if (!SDL_Init(SDL_INIT_VIDEO)) return 1;
    char *args[16] = {"derp-climb-smoke", "-iwad", argv[1], "-warp", "1", "1", "-skill", "3",
                      "-nomonsters", "-nomusic", "-nosound", "-derpai"};
    myargc = 12; myargv = args;
    D_DoomMain();
    return 0;
}
