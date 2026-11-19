/* E1M2's switch imp (#69, beside the switch that opens a door at x -592): it guards the switch
   (p_guards.c), and when a shot far off wakes it, it lies in wait by it out of his sight
   (p_hunters.c). He walks in from some way off, firing once on the way: it must not leave the
   switch, nor stand in his view before he comes close.
   doom-derp-switch-guard-smoke <iwad> */
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
#define IMPX (-544)
#define IMPY 1024
#define STEP 8			/* units a tic he walks */
#define FROM_LEAST 700		/* he starts this far off (straight) */
#define WALK_MOST 1600		/* and at most this far on foot */
#define SHOT_AT 900		/* units of walk left when he fires */
#define CLOSE 224		/* the lurk's own "too close" */
#define LEFT_MOST 384		/* HOLD_NEAR */
#define SEEN_MOST 35		/* tics in his view, far off, it may have */

static int stage, path[8192], n, at, seen, lastlurk = -1, lastmind = -1;
static boolean shot;
static fixed_t far, walkleft;
static mobj_t *imp;
static void fail(const char *what) { I_Error("DERP switch guard smoke test: %s", what); }
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

static mobj_t *find_imp(void)
{
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next)
    {
        mobj_t *mo = (mobj_t *)th;
        if (th->function.acp1 == (actionf_p1)P_MobjThinker && mo->type == MT_TROOP
            && P_AproxDistance(mo->x - U(IMPX), mo->y - U(IMPY)) < U(16))
            return mo;
    }
    return NULL;
}

/* In his view: in sight, and within 45 degrees of where he faces. */
static boolean in_view(mobj_t *mo)
{
    angle_t off = R_PointToAngle2(player()->x, player()->y, mo->x, mo->y) - player()->angle;
    return P_CheckSight(player(), mo) && (off < ANG45 || off > (angle_t)-ANG45);
}

void I_TestFrame(void)
{
    if (gamestate != GS_LEVEL || leveltime < 2) return;
    switch (stage)
    {
      case 0:
      {
        if (!P_DerpAI() || !P_NavReady()) fail("DERP's monsters or their navigation are not on");
        players[0].cheats |= CF_GODMODE;
        imp = find_imp();
        if (!imp) fail("no imp by the switch");
        if (!imp->guard) fail("the switch imp is not on guard duty");
        /* (The level's start is a long way round.) A place some way off he walks in from: the
           longest walk in, up to WALK_MOST. */
        int nav = P_NavClassOf(imp), best = 0, dx, dy;
        fixed_t bx = 0, by = 0;
        for (dy = -WALK_MOST; dy <= WALK_MOST; dy += 64)
            for (dx = -WALK_MOST; dx <= WALK_MOST; dx += 64)
            {
                fixed_t x = imp->guardx + U(dx), y = imp->guardy + U(dy), d = P_AproxDistance(U(dx), U(dy));
                int cell = P_NavCellAt(nav, x, y);
                if (cell < 0 || !P_NavStandable(nav, cell) || P_NavHurts(cell) || d < U(FROM_LEAST) || d > U(WALK_MOST))
                    continue;
                /* (By its length: navwalk is its cost, and the pit's nukage costs more.) */
                int cells = P_NavPath(nav, x, y, imp->guardx, imp->guardy, path, 8192, 400000);
                if (cells <= 0 || cells*NAVCELL > WALK_MOST || cells*NAVCELL <= best)
                    continue;
                best = cells*NAVCELL; bx = x; by = y;
            }
        if (!best)
        {
            for (int r = 64; r <= 1024; r += 64)
            {
                int got = 0, all = 0;
                for (int a = 0; a < 16; a++)
                {
                    fixed_t x = imp->guardx + r*finecosine[a*FINEANGLES/16], y = imp->guardy + r*finesine[a*FINEANGLES/16];
                    int cell = P_NavCellAt(nav, x, y);
                    if (cell < 0 || !P_NavStandable(nav, cell)) continue;
                    all++;
                    if (P_NavPath(nav, x, y, imp->guardx, imp->guardy, path, 8192, 400000) > 0) got++;
                }
                sector_t *s = R_PointInSubsector(imp->guardx + U(r), imp->guardy)->sector;
                printf("  at %d: %d standable, %d with a way; east of it sector %d floor %d special %d\n", r, all, got,
                       (int)(s - sectors), s->floorheight >> FRACBITS, s->special);
            }
            sector_t *s = imp->subsector->sector;
            printf("  its sector %d floor %d ceiling %d special %d tag %d\n", (int)(s - sectors), s->floorheight >> FRACBITS,
                   s->ceilingheight >> FRACBITS, s->special, s->tag);
            fail("nowhere to walk to the switch from");
        }
        move_to(player(), bx, by);
        n = P_NavPath(nav, player()->x, player()->y, imp->guardx, imp->guardy, path, 8192, 400000);
        if (n <= 0) fail("no way to the switch");
        walkleft = U(n*NAVCELL);
        printf("Imp #%d at (%d,%d) guards (%d,%d); he walks in from (%d,%d), %d units.\n", imp->serial,
               imp->x >> FRACBITS, imp->y >> FRACBITS, imp->guardx >> FRACBITS, imp->guardy >> FRACBITS,
               bx >> FRACBITS, by >> FRACBITS, n*NAVCELL);
        stage = 1;
        break;
      }
      case 1:
      {
        fixed_t x, y, d;
        mobj_t *me = player();
        int nav = P_NavClassOf(imp);
        if (imp->health <= 0) fail("the imp died: something else got it");
        /* On along his way, a step a tic; a door in front of him he opens. */
        if (at < n - 1)
        {
            P_NavPoint(nav, path[at + 1], &x, &y, NULL);
            d = P_AproxDistance(x - me->x, y - me->y);
            me->angle = R_PointToAngle2(me->x, me->y, x, y);
            if (d <= U(STEP)) { at++; move_to(me, x, y); walkleft -= d; }
            else
            {
                move_to(me, me->x + FixedMul(U(STEP), finecosine[me->angle >> ANGLETOFINESHIFT]),
                        me->y + FixedMul(U(STEP), finesine[me->angle >> ANGLETOFINESHIFT]));
                walkleft -= U(STEP);
            }
            P_UseLines(&players[0]);
        }
        if (walkleft < U(SHOT_AT) && !shot)
        {
            shot = true;
            printf("%5d  he fires, %d units of walk left\n", leveltime, walkleft >> FRACBITS);
            P_NoiseAlert(me, me);
        }
        fixed_t post = P_AproxDistance(imp->x - imp->guardx, imp->y - imp->guardy);
        fixed_t him = P_AproxDistance(imp->x - me->x, imp->y - me->y);
        if (post > far) far = post;
        if (him > U(CLOSE) && in_view(imp)) seen++;
        if (imp->lurk != lastlurk || imp->mind != lastmind)
        {
            printf("%5d  imp at (%d,%d), %d from the switch, %d from him: mind %d lurk %d hang %d%s\n", leveltime,
                   imp->x >> FRACBITS, imp->y >> FRACBITS, post >> FRACBITS, him >> FRACBITS, imp->mind, imp->lurk,
                   imp->hang, in_view(imp) ? ", in his view" : "");
            lastlurk = imp->lurk;
            lastmind = imp->mind;
        }
        if (at >= n - 1 || him < U(CLOSE) || leveltime > 120*TICRATE)
        {
            printf("Done at %d: %d from him; at most %d from the switch; in his view far off %d tics.\n", leveltime,
                   him >> FRACBITS, far >> FRACBITS, seen);
            if (far > U(LEFT_MOST)) fail("it left the switch");
            if (seen > SEEN_MOST) fail("he had it in view long before he came close");
            puts("DERP switch guard smoke test passed.");
            I_Quit();
        }
        break;
      }
    }
}

int main(int argc, char **argv)
{
    if (argc != 2) return 1;
    if (!SDL_Init(SDL_INIT_VIDEO)) return 1;
    char *args[16] = {"derp-switch-guard-smoke", "-iwad", argv[1], "-warp", "1", "2", "-skill", "4",
                      "-nomusic", "-nosound", "-derpai"};
    myargc = 11; myargv = args;
    if (getenv("GUARDLOG")) { args[11] = "-derplog"; args[12] = getenv("GUARDLOG"); myargc = 13; }
    D_DoomMain();
    return 0;
}
