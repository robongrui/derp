/* DERP's fire teams (p_tactics.c) on a real level: two zombies fighting
   the player near each other take turns, one moving to a new fire position
   (a bound) while the other holds and covers it; never both at once.
   doom-derp-team-smoke <iwad> */
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
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

static int stage, stagetic, lasttic;
static void fail(const char *what) { I_Error("DERP team smoke test: %s", what); }
static mobj_t *player(void) { return players[0].mo; }

static void move_to(mobj_t *mo, fixed_t x, fixed_t y)
{
    P_UnsetThingPosition(mo);
    mo->x = x; mo->y = y;
    P_SetThingPosition(mo);
    mo->z = mo->floorz = mo->subsector->sector->floorheight;
    mo->momx = mo->momy = mo->momz = 0;
}

/* A standable spot lo-hi units from the player and within near of (nx, ny)
   (near 0: anywhere), in his sight. */
static boolean find_spot(int lo, int hi, fixed_t nx, fixed_t ny, int near, fixed_t *x, fixed_t *y)
{
    int nav = P_NavClassFor(20, 56), cells = P_NavCells();
    mobj_t *probe = NULL;
    for (int c = 0; c < cells; c++)
    {
        fixed_t px, py, f, d;
        if (!P_NavStandable(nav, c) || P_NavDoor(nav, c)) continue;
        P_NavPoint(nav, c, &px, &py, &f);
        d = P_AproxDistance(px - player()->x, py - player()->y);
        if (d < lo*FRACUNIT || d > hi*FRACUNIT) continue;
        if (near && (P_AproxDistance(px - nx, py - ny) > near*FRACUNIT
                     || P_AproxDistance(px - nx, py - ny) < 64*FRACUNIT)) continue;
        if (!probe) probe = P_SpawnMobj(px, py, ONFLOORZ, MT_POSSESSED);
        move_to(probe, px, py);
        if (!P_CheckPosition(probe, px, py) || !P_CheckSight(probe, player())) continue;
        *x = px; *y = py;
        P_RemoveMobj(probe);
        return true;
    }
    if (probe) P_RemoveMobj(probe);
    return false;
}

static mobj_t *z[2];
static fixed_t lastx[2], lasty[2], startx[2], starty[2];
static int bounds[2], was[2], both, held_still = 1, covered_tics;

static mobj_t *soldier(fixed_t x, fixed_t y)
{
    mobj_t *mo = P_SpawnMobj(x, y, ONFLOORZ, MT_SHOTGUY);  /* round him is the assault man's (p_roles.h) */
    mo->target = player();
    mo->mind = MIND_HUNT;
    mo->knownx = player()->x; mo->knowny = player()->y; mo->knowntic = leveltime;
    mo->health = 1000;	/* their shots at him do not matter; nobody dies */
    P_SetMobjState(mo, mo->info->seestate);
    return mo;
}

static boolean bounding(mobj_t *mo) { return mo->mind == MIND_HUNT && mo->tactic == STEP_BOUNDING; }

static void watch(void)
{
    int b0 = bounding(z[0]), b1 = bounding(z[1]);
    if (leveltime == lasttic) return;
    if (b0 && b1) both = 1;
    for (int i = 0; i < 2; i++)
    {
        /* The other one, while one bounds: it holds its ground. */
        if (bounding(z[!i]) && !bounding(z[i]))
        {
            covered_tics++;
            if (P_AproxDistance(z[i]->x - lastx[i], z[i]->y - lasty[i]) > 6*FRACUNIT*(leveltime - lasttic))
                held_still = 0;
        }
        lastx[i] = z[i]->x; lasty[i] = z[i]->y;
    }
    for (int i = 0; i < 2; i++)
    {
        if (bounding(z[i]) && !was[i]) bounds[i]++;
        was[i] = bounding(z[i]);
        if (getenv("TEAMTRACE") && !(leveltime % 35))
            printf("t%d z%d mind %d tactic %d kneel %d state %d sees %d decide %d dist %d\n", leveltime, i, z[i]->mind, z[i]->tactic, z[i]->kneeltic, (int)(z[i]->state - states), (int)P_CheckSight(z[i], player()), z[i]->decidetic,
                   P_AproxDistance(z[i]->x - player()->x, z[i]->y - player()->y) >> FRACBITS);
    }
    lasttic = leveltime;
}

void I_TestFrame(void)
{
    if (gamestate != GS_LEVEL || leveltime < 2) return;
    switch (stage)
    {
      case 0:
      {
        fixed_t x, y, x2, y2;
        if (!P_DerpAI() || !P_NavReady()) fail("DERP's monsters or their navigation are not on");
        players[0].cheats |= CF_GODMODE;
        if (!find_spot(330, 560, 0, 0, 0, &x, &y)) fail("no spot in range in sight");
        if (!find_spot(330, 560, x, y, 400, &x2, &y2)) fail("no second spot near the first");
        z[0] = soldier(x, y); z[1] = soldier(x2, y2);
        for (int i = 0; i < 2; i++) { lastx[i] = startx[i] = z[i]->x; lasty[i] = starty[i] = z[i]->y; }
        lasttic = leveltime;
        stage = 1; stagetic = leveltime; break;
      }
      case 1:
        watch();
        if (leveltime - stagetic < 12*TICRATE) break;
        printf("Team: bounds %d and %d, both at once %s, the other held %s (%d tics covering).\n",
               bounds[0], bounds[1], both ? "yes" : "never", held_still ? "still" : "moving", covered_tics);
        if (!bounds[0] && !bounds[1]) fail("two soldiers fighting him near each other never moved in turn");
        if (both) fail("both soldiers of a team bounded at once");
        if (!held_still) fail("while one bounded, the other did not hold its ground");
        /* A friend's stray shot hurts; it does not start a feud. */
        if (z[0]->target != player() || z[1]->target != player()) fail("soldiers of a team turned on each other");
        /* Soldiers and demons of one room are no squad: an imp's death
           alerts a zombie but is no loss of its own; a soldier's is. */
        {
            mobj_t *imp = P_SpawnMobj(startx[0], starty[0], ONFLOORZ, MT_TROOP);
            mobj_t *mate = P_SpawnMobj(startx[0], starty[0], ONFLOORZ, MT_POSSESSED);
            mobj_t *ear = P_SpawnMobj(startx[0], starty[0], ONFLOORZ, MT_POSSESSED);
            P_DerpLossHeard(ear, imp);
            if (ear->losses || !ear->alert) fail("an imp's death was a zombie's loss, or did not alert it");
            P_DerpLossHeard(ear, mate);
            if (ear->losses != 1) fail("a fellow soldier's death was no loss to a zombie of its room");
            if (imp->squad == ear->squad) fail("an imp and a zombie of one room were one squad");
            P_RemoveMobj(imp); P_RemoveMobj(mate); P_RemoveMobj(ear);
            printf("Squads: an imp's death alerts a zombie of its room but is not its loss; a soldier's is.\n");
        }
        puts("DERP team smoke test passed.");
        I_Quit();
        break;
    }
}

int main(int argc, char **argv)
{
    if (argc != 2) return 1;
    if (!SDL_Init(SDL_INIT_VIDEO)) return 1;
    char *args[16] = {"derp-team-smoke", "-iwad", argv[1], "-warp", "1", "1", "-skill", "3",
                      "-nomonsters", "-nomusic", "-nosound", "-derpai"};
    myargc = 12; myargv = args;
    D_DoomMain();
    return 0;
}
