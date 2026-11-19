/* Low cover on E1M1, at the two blocks before the exit stairs (48 units high):
   a head shows over one, so a standing soldier is seen, one on its knee is not.
   From up the stairs it kneels behind a block, out of his sight, and stands to
   fire over it; on the block's own floor its gun does not clear the top, so it
   kneels and steps out to the end. Near enough, it is up and fighting.
   doom-derp-lowcover-smoke <iwad> */
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
#include "p_bullets.h"
#include "tables.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#define U(n) ((n)*FRACUNIT)

static int stage, stagetic;
static mobj_t *guy;
static int down_hidden, down_seen, ups, shots_up, shots_out, lastfire;
#define DOWN_MOST 150	/* of 700 tics: it fights from its knee, it does not only pass through it */
/* On the block's own floor it kneels for less (103-146 of 700 on 2026-11-07, no single cause): how
   long is a pin for the bot report, not this test; that it kneels, steps out and does not fire
   through the block is what this checks. */
#define DOWN_LEAST 35
static void fail(const char *what) { I_Error("DERP low cover smoke test: %s", what); }
static mobj_t *player(void) { return players[0].mo; }

static void move_to(mobj_t *mo, fixed_t x, fixed_t y)
{
    P_UnsetThingPosition(mo);
    mo->x = x; mo->y = y;
    P_SetThingPosition(mo);
    mo->z = mo->floorz = mo->subsector->sector->floorheight;
    mo->momx = mo->momy = mo->momz = 0;
}

static void start(fixed_t px, fixed_t py, fixed_t gx, fixed_t gy)
{
    move_to(player(), px, py);
    guy = P_SpawnMobj(gx, gy, ONFLOORZ, MT_POSSESSED);
    guy->angle = R_PointToAngle2(gx, gy, px, py);
    guy->spawnpoint.x = gx >> FRACBITS;
    guy->spawnpoint.y = gy >> FRACBITS;
    guy->spawnpoint.type = 3004;
    P_SetMobjState(guy, guy->info->seestate);
    guy->target = player();
    guy->mind = MIND_HUNT;
    guy->squad = 9; guy->nerve = 60;
    guy->sighttic = leveltime;	/* Past the first look at him. */
    guy->knownx = px; guy->knowny = py; guy->knowntic = leveltime;
    guy->tactictic = leveltime - 10*TICRATE;
    down_hidden = down_seen = ups = shots_up = shots_out = 0;
    lastfire = guy->firetic;
}

static void watch(void)
{
    boolean down = guy->kneeltic != 0;
    if (guy->mind == MIND_COVER && guy->lowcover)
    {
        if (down && guy->tactic == STEP_COVER_DUCKED)
        {
            if (guy->height >= guy->info->height) fail("on its knee at full height");
            if (P_CheckSight(player(), guy)) down_seen++; else down_hidden++;
        }
        if (!down && guy->tactic == STEP_COVER_LEANING) ups++;
    }
    if (guy->firetic != lastfire && guy->lowcover)
    {
        /* Over the top: fired nearer its cover than its peek spot (the two can lie 24 apart). */
        boolean here = guy->peekx == guy->coverx && guy->peeky == guy->covery
            || P_AproxDistance(guy->x - guy->coverx, guy->y - guy->covery)
               < P_AproxDistance(guy->x - guy->peekx, guy->y - guy->peeky);
        lastfire = guy->firetic;
        if (here) shots_up++; else shots_out++;
    }
    if (!(leveltime % 20)) P_FireBulletAt(player(), guy->x, guy->y, guy->z + guy->height + U(16), 0);
    if (!(leveltime % 35))
        printf("  t%d mind %d tactic %d low %d kneel %d height %d at (%d,%d) cover (%d,%d) peek (%d,%d) hidden %d seen %d ups %d shots %d/%d\n",
               leveltime, guy->mind, guy->tactic, guy->lowcover, guy->kneeltic != 0, guy->height >> FRACBITS,
               guy->x >> FRACBITS, guy->y >> FRACBITS, guy->coverx >> FRACBITS, guy->covery >> FRACBITS,
               guy->peekx >> FRACBITS, guy->peeky >> FRACBITS, down_hidden, down_seen, ups, shots_up, shots_out);
}

void I_TestFrame(void)
{
    if (gamestate != GS_LEVEL || leveltime < 2) return;
    switch (stage)
    {
      case 0:
        if (!P_DerpAI() || !P_NavReady()) fail("DERP's monsters or their navigation are not on");
        players[0].cheats |= CF_GODMODE;
        /* Up the stairs, the soldier on the far side of the north block. */
        start(U(180), U(-3232), U(400), U(-3060));
        stage = 1; stagetic = leveltime; break;
      case 1:
        watch();
        if (leveltime - stagetic >= 20*TICRATE)
        {
            if (down_hidden < DOWN_MOST) fail("from up the stairs, it hardly knelt out of his sight");
            if (guy->peekx != guy->coverx || guy->peeky != guy->covery) fail("from up the stairs, it stepped out instead of standing");
            if (down_seen > down_hidden/4) fail("on its knee, he could see it");
            if (!ups) fail("it never stood up from its low wall");
            if (!shots_up) fail("it never fired over its low wall");
            printf("From above: hidden on its knee %d tics, stood %d, %d shots over the top.\n", down_hidden, ups, shots_up);
            P_RemoveMobj(guy);
            /* The block's own floor, across the room from it. */
            start(U(470), U(-3232), U(215), U(-3040));
            stage = 2; stagetic = leveltime;
        }
        break;
      case 2:
        watch();
        if (leveltime - stagetic >= 20*TICRATE)
        {
            if (down_hidden < DOWN_LEAST) fail("on its floor, it never knelt out of his sight");
            if (guy->peekx == guy->coverx && guy->peeky == guy->covery) fail("on its floor, it meant to fire through the block");
            if (!shots_out) fail("on its floor, it never stepped out to fire");
            /* Shots nearer the cover than the peek are counted, not failed: whether the block
               was in their line this test cannot tell (the soldier fires from beside the spot,
               and the line to him may clear the block's corner). A pin for the bot report. */
            printf("Level: hidden on its knee %d tics, %d shots from the end, %d from by the cover.\n",
                   down_hidden, shots_out, shots_up);
            /* He walks up to it, somewhere beside it that it can see (a fixed offset once put him
               behind the block): up and fighting, not left on one knee. */
            {
                static const int off[][2] = { {-64, 0}, {-64, 40}, {0, -64}, {0, 64}, {64, 0} };
                int k;
                for (k = 0; k < 5; k++)
                {
                    move_to(player(), guy->x + U(off[k][0]), guy->y + U(off[k][1]));
                    if (P_CheckSight(guy, player())) break;
                }
                if (k == 5) fail("no spot beside the soldier in its sight to put him on");
            }
            stage = 3; stagetic = leveltime;
        }
        break;
      case 3:
        if (!(leveltime % 35))
            printf("  t%d him on top: mind %d tactic %d kneel %d low %d sees him %d dist %d\n", leveltime, guy->mind, guy->tactic,
                   guy->kneeltic != 0, guy->lowcover, (int)P_CheckSight(guy, player()),
                   P_AproxDistance(guy->x - player()->x, guy->y - player()->y) >> FRACBITS);
        if (leveltime - stagetic >= 3*TICRATE)
        {
            if (guy->mind == MIND_COVER && guy->tactic == STEP_COVER_DUCKED) fail("he stood over it and it stayed ducked");
            if (guy->kneeltic && guy->lowcover) fail("left on one knee with him on top of it");
            puts("DERP low cover smoke test passed.");
            I_Quit();
        }
        break;
    }
}

int main(int argc, char **argv)
{
    if (argc != 2) return 1;
    if (!SDL_Init(SDL_INIT_VIDEO)) return 1;
    char *args[16] = {"derp-lowcover-smoke", "-iwad", argv[1], "-warp", "1", "1", "-skill", "3",
                      "-nomonsters", "-nomusic", "-nosound", "-derpai"};
    myargc = 12; myargv = args;
    D_DoomMain();
    return 0;
}
