/* Cover on E1M1, by what a human marked. In the hall with the U-shaped block, he
   at (1755, -2470): a soldier by each outer corner of the block picks that corner.
   The plan: ways into a room, a list whose walls all face them; a soldier readied
   by a far noise goes to one of those covers, and fires when he comes. Never a
   second cover under his gun; the rest is the fight's own rules.
   doom-derp-coverplan-smoke <iwad> */
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
#include "p_tactics.h"
#include "p_corners.h"
#include "p_coverplan.h"
#include "tables.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#define U(n) ((n)*FRACUNIT)
#define HIMX U(1755)
#define HIMY U(-2470)

static int stage, stagetic, strayed;
static mobj_t *guy;
static fixed_t postx, posty, mouthx, mouthy;
static void fail(const char *what) { I_Error("DERP cover plan smoke test: %s", what); }
static mobj_t *player(void) { return players[0].mo; }

static void move_to(mobj_t *mo, fixed_t x, fixed_t y)
{
    P_UnsetThingPosition(mo);
    mo->x = x; mo->y = y;
    P_SetThingPosition(mo);
    mo->z = mo->floorz = mo->subsector->sector->floorheight;
    mo->momx = mo->momy = mo->momz = 0;
}

static int room_at(fixed_t x, fixed_t y) { return P_NavRoom((int)(R_PointInSubsector(x, y)->sector - sectors)); }

static mobj_t *soldier_at(fixed_t x, fixed_t y)
{
    mobj_t *mo = P_SpawnMobj(x, y, ONFLOORZ, MT_POSSESSED);
    mo->spawnpoint.x = x >> FRACBITS;
    mo->spawnpoint.y = y >> FRACBITS;
    mo->spawnpoint.type = 3004;
    mo->squad = 9; mo->nerve = 60;
    return mo;
}

/* The corners marked good from there, and a soldier somewhere near each. */
static void check_marks(void)
{
    static const struct { int x, y, sx, sy; } marks[] = {
        {2360, -2552, 2340, -2650}, {2360, -2296, 2440, -2200},
        {2152, -2616, 2040, -2660}, {2152, -2248, 2040, -2200},
    };
    int matched = 0;
    move_to(player(), HIMX, HIMY);
    for (int i = 0; i < 4; i++)
    {
        fixed_t cx, cy, kx, ky;
        mobj_t *probe = soldier_at(U(marks[i].sx), U(marks[i].sy));
        boolean got = P_DerpCornerCover(probe, player(), &cx, &cy, &kx, &ky);
        int off = got ? P_AproxDistance(cx - U(marks[i].x), cy - U(marks[i].y)) >> FRACBITS : -1;
        printf("Marked (%d,%d): soldier at (%d,%d) picks (%d,%d), tier %d, %d off.\n", marks[i].x, marks[i].y, marks[i].sx, marks[i].sy,
               got ? cx >> FRACBITS : 0, got ? cy >> FRACBITS : 0, got ? P_DerpCornerTier() : -1, off);
        if (got && off < 96) matched++;  /* the same corner of the block, either wall of it */
        P_RemoveMobj(probe);
    }
    printf("%d of 4 marked corners picked.\n", matched);
    if (matched < 4) fail("a soldier by a corner marked good did not pick it");
}

/* A room he is not in, and a soldier out of his sight by a cover for his way in. */
static boolean find_scene(fixed_t *sx, fixed_t *sy)
{
    const corner_t *corners;
    int rooms = P_NavRooms();
    fixed_t hx = player()->x, hy = player()->y;
    P_Corners(&corners);
    mobj_t *probe = soldier_at(hx, hy);
    for (int room = 0; room < rooms; room++)
    {
        int doors[4], n = P_CoverPlanDoors(room, hx, hy, doors, 4);
        for (int i = 0; i < n; i++)
        {
            const planspot_t *list;
            int count = P_CoverPlanSpots(doors[i], &list);
            fixed_t mx, my;
            P_CoverPlanMouth(doors[i], &mx, &my);
            for (int j = 0; j < count; j++)
            {
                if (list[j].corner >= 0 && !P_CornerFaces(&corners[list[j].corner], mx, my)) fail("a cover whose wall does not face the way in");
                if (j && list[j].rank < list[j - 1].rank) fail("the list is not best first");
                if (list[j].rank != PLAN_ENTRANCE) continue;
                move_to(probe, list[j].x, list[j].y);
                if (P_CheckSight(player(), probe)) continue;
                *sx = list[j].x; *sy = list[j].y;
                mouthx = mx; mouthy = my;
                printf("Room %d, %d way(s) in, the first %d covers; soldier at (%d,%d), mouth (%d,%d).\n", room, n, count,
                       *sx >> FRACBITS, *sy >> FRACBITS, mx >> FRACBITS, my >> FRACBITS);
                P_RemoveMobj(probe);
                return true;
            }
        }
    }
    P_RemoveMobj(probe);
    return false;
}

void I_TestFrame(void)
{
    fixed_t x, y;
    if (gamestate != GS_LEVEL || leveltime < 2) return;
    switch (stage)
    {
      case 0:
        if (!P_DerpAI() || !P_NavReady()) fail("DERP's monsters or their navigation are not on");
        players[0].cheats |= CF_GODMODE;
        check_marks();
        if (P_CoverPlanDoors(room_at(HIMX, HIMY), HIMX, HIMY, (int[4]){0}, 4)) fail("a way in for someone already in the room");
        if (!find_scene(&x, &y)) fail("no room with an entrance cover out of his sight");
        guy = soldier_at(x + U(16), y);
        P_MindReady(guy, player()->x, player()->y);
        stage = 1; stagetic = leveltime; break;
      case 1:
        /* Readied: it takes a cover for the way in and waits there. */
        if (guy->mind == MIND_AMBUSH && guy->fightsfrom && leveltime - stagetic > TICRATE
            && P_AproxDistance(guy->x - guy->coverx, guy->y - guy->covery) < U(24))
        {
            printf("Took cover at (%d,%d), fires from (%d,%d).\n", guy->coverx >> FRACBITS, guy->covery >> FRACBITS,
                   guy->peekx >> FRACBITS, guy->peeky >> FRACBITS);
            postx = guy->coverx; posty = guy->covery;
            /* He comes through, and it hears him. */
            move_to(player(), mouthx, mouthy);
            P_NoiseAlert(player(), player());
            stage = 2; stagetic = leveltime;
        }
        else if (leveltime - stagetic > 15*TICRATE)
        {
            printf("  mind %d tactic %d at (%d,%d) cover (%d,%d) fights %d\n", guy->mind, guy->tactic, guy->x >> FRACBITS, guy->y >> FRACBITS,
                   guy->coverx >> FRACBITS, guy->covery >> FRACBITS, guy->fightsfrom);
            fail("the readied soldier never got to a cover");
        }
        break;
      case 2:
        /* Sprung: it fights from there, and does not hop or run. */
        {
            int off = P_AproxDistance(guy->x - postx, guy->y - posty) >> FRACBITS;
            if (off > strayed) strayed = off;
            if (!(leveltime % 20)) P_NoiseAlert(player(), player());
            if (leveltime - stagetic >= 8*TICRATE)
            {
                printf("Sprung: mind %d tactic %d, strayed %d from its cover, fired t%d.\n", guy->mind, guy->tactic, strayed, guy->firetic);
                if (guy->firetic < stagetic) fail("he came through and it never fired");
                if (guy->coverx != postx || guy->covery != posty) fail("it took another cover under his gun");
                puts("DERP cover plan smoke test passed.");
                I_Quit();
            }
        }
        break;
    }
}

int main(int argc, char **argv)
{
    if (argc != 2) return 1;
    if (!SDL_Init(SDL_INIT_VIDEO)) return 1;
    char *args[16] = {"derp-coverplan-smoke", "-iwad", argv[1], "-warp", "1", "1", "-skill", "3",
                      "-nomonsters", "-nomusic", "-nosound", "-derpai"};
    myargc = 12; myargv = args;
    D_DoomMain();
    return 0;
}
