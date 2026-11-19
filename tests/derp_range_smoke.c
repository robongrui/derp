/* DERP's soldiers keep to their gun's range (p_tactics.c, KeepRange) on a
   real level: a zombie in sight too close backs off facing him, one in its
   band holds it, one beyond it closes in to a fire position of its own (a
   second from the same spot fans out, not in line behind); with the tactics
   off, vanilla's chase walks straight up.
   doom-derp-range-smoke <iwad> */
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

static int stage, stagetic;
static void fail(const char *what) { I_Error("DERP range smoke test: %s", what); }
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

static mobj_t *near_z, *mid_z, *far_z, *far2_z;
static fixed_t near0, mid0, far0;
static fixed_t nearx, neary;

static fixed_t dist(mobj_t *mo) { return P_AproxDistance(mo->x - player()->x, mo->y - player()->y); }

static mobj_t *hunter(fixed_t x, fixed_t y)
{
    mobj_t *mo = P_SpawnMobj(x, y, ONFLOORZ, MT_POSSESSED);
    mo->target = player();
    mo->mind = MIND_HUNT;
    mo->sighttic = leveltime;	/* Past the first look at him. */
    mo->angle = R_PointToAngle2(x, y, player()->x, player()->y);
    P_SetMobjState(mo, mo->info->seestate);
    return mo;
}

static void start(void)
{
    fixed_t x, y;
    if (!find_spot(100, 160, &nearx, &neary)) fail("no spot close in sight");
    near_z = hunter(nearx, neary);
    if (!find_spot(400, 520, &x, &y)) fail("no spot in range in sight");
    mid_z = hunter(x, y);
    if (!find_spot(850, 1100, &x, &y)) fail("no spot far in sight");
    far_z = hunter(x, y);
    /* A second from the same place: it must not file in behind the first. */
    far2_z = hunter(x, y);
    move_to(far2_z, x + 40*FRACUNIT, y);
    if (!P_CheckPosition(far2_z, far2_z->x, far2_z->y)) move_to(far2_z, x, y + 40*FRACUNIT);
    near0 = dist(near_z); mid0 = dist(mid_z); far0 = dist(far_z);
}

static void check(void)
{
    printf("Range: close %d -> %d, in range %d -> %d, far %d -> %d units.\n",
           near0 >> FRACBITS, dist(near_z) >> FRACBITS, mid0 >> FRACBITS, dist(mid_z) >> FRACBITS,
           far0 >> FRACBITS, dist(far_z) >> FRACBITS);
    if (dist(near_z) < near0 + 48*FRACUNIT) fail("too close, a zombie did not back off");
    if (dist(mid_z) < 320*FRACUNIT || dist(mid_z) > 800*FRACUNIT) fail("in its range, a zombie did not hold it");
    if (dist(far_z) > far0 - 96*FRACUNIT) fail("beyond its range, a zombie did not close in");
    {
        angle_t off = R_PointToAngle2(near_z->x, near_z->y, player()->x, player()->y) - near_z->angle;
        if (off > ANG45 && off < (angle_t)-ANG45) fail("backing off, a zombie turned its back on him");
    }
    if (P_AproxDistance(far_z->firex - far2_z->firex, far_z->firey - far2_z->firey) < 64*FRACUNIT)
        fail("two zombies closing in from one place chose one fire position");
    printf("Fire positions: two from one place %d units apart, the close one facing him.\n",
           P_AproxDistance(far_z->firex - far2_z->firex, far_z->firey - far2_z->firey) >> FRACBITS);
}

void I_TestFrame(void)
{
    if (gamestate != GS_LEVEL || leveltime < 2) return;
    switch (stage)
    {
      case 0:
        if (!P_DerpAI() || !P_NavReady()) fail("DERP's monsters or their navigation are not on");
        players[0].cheats |= CF_GODMODE;
        start(); stage = 1; stagetic = leveltime; break;
      case 1:
        if (leveltime - stagetic < 4*TICRATE) break;
        check();
        /* Off: vanilla's chase walks the close one in. */
        derpparts.tactics = false;
        /* Alone, and after him: a friend's stray round has it after that friend
           with the tactics off (it would walk off, and that is not backing off). */
        P_RemoveMobj(mid_z); P_RemoveMobj(far_z); P_RemoveMobj(far2_z);
        /* A fresh one: moved, it finished the step it was in the middle of. */
        P_RemoveMobj(near_z);
        near_z = hunter(nearx, neary);
        near0 = dist(near_z);
        stage = 2; stagetic = leveltime; break;
      case 2:
        if (leveltime - stagetic < 2*TICRATE) break;
        if (dist(near_z) > near0 + 16*FRACUNIT) fail("with the tactics off, a zombie still backed off");
        printf("Tactics off: the close one stayed on him (%d units).\n", dist(near_z) >> FRACBITS);
        puts("DERP range smoke test passed.");
        I_Quit();
        break;
    }
}

int main(int argc, char **argv)
{
    if (argc != 2) return 1;
    if (!SDL_Init(SDL_INIT_VIDEO)) return 1;
    char *args[16] = {"derp-range-smoke", "-iwad", argv[1], "-warp", "1", "1", "-skill", "3",
                      "-nomonsters", "-nomusic", "-nosound", "-derpai"};
    myargc = 12; myargv = args;
    D_DoomMain();
    return 0;
}
