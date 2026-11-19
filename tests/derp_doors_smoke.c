/* DERP's monsters through doors (p_senses.c's Walk, p_enemy.c's P_Move) on a
   real level: at each monster door (type 1, no secret face, not fenced off
   from monsters) with room on both sides, three zombies sent to look past
   it all get through within 10 s. Before DERP's door fixes the second and
   third took 4-9 s or never (a door already up counted as a step taken
   while the frame stopped them; the crowding side-step walked into the
   wall; a waiting monster never tried again).
   doom-derp-doors-smoke <iwad> */
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
extern fixed_t tmfloorz, tmdropoffz;
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#define GROUP 3
static int line = -1, t0, crossed[GROUP], count, doors, slowest;
static mobj_t *mo[GROUP];
static fixed_t bx, by, nx, ny, mx, my, ux, uy;

static void fail(const char *what) { I_Error("DERP doors smoke test: %s", what); }

static boolean standable(fixed_t x, fixed_t y)
{
    int nav = P_NavClassFor(20, 56), c = P_NavCellAt(nav, x, y);
    return c >= 0 && P_NavStandable(nav, c) && !P_NavDoor(nav, c);
}

/* The next monster door, shut, with a spot 160 past it to send them to. */
static boolean next_door(void)
{
    while (++line < numlines)
    {
        line_t *ld = &lines[line];
        fixed_t len;
        if (ld->special != 1 || !ld->backsector || (ld->flags & (ML_SECRET | ML_BLOCKMONSTERS))) continue;
        if (ld->backsector->ceilingheight > ld->backsector->floorheight) continue;
        mx = ld->v1->x + ld->dx/2; my = ld->v1->y + ld->dy/2;
        len = P_AproxDistance(ld->dx, ld->dy);
        ux = FixedDiv(ld->dx, len); uy = FixedDiv(ld->dy, len);
        nx = uy; ny = -ux;	/* the front side's normal */
        bx = mx - 160*nx; by = my - 160*ny;
        if (standable(bx, by)) return true;
    }
    return false;
}

/* The k-th one's start: in front, side by side, the last behind them. */
static boolean spot(int k, fixed_t *x, fixed_t *y)
{
    static const int out[GROUP] = {36, 36, 84}, along[GROUP] = {0, 1, -1};
    *x = mx + out[k]*nx + along[k]*40*ux; *y = my + out[k]*ny + along[k]*40*uy;
    return standable(*x, *y)
        && abs((R_PointInSubsector(*x, *y)->sector->floorheight - R_PointInSubsector(bx, by)->sector->floorheight) >> FRACBITS) <= 24;
}

static void shut_doors(void)
{
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next)
        if (th->function.acp1 == (actionf_p1)T_VerticalDoor)
        {
            vldoor_t *d = (vldoor_t *)th;
            d->sector->ceilingheight = d->sector->floorheight;
            d->sector->specialdata = NULL;
            P_RemoveThinker(th);
        }
}

void I_TestFrame(void)
{
    if (gamestate != GS_LEVEL || leveltime < 2) return;
    if (!P_DerpAI() || !P_NavReady()) fail("DERP's monsters or their navigation are not on");
    players[0].cheats |= CF_GODMODE;
    if (!count)
    {
        if (!next_door())
        {
            printf("Doors: %d tried, slowest %d tics.\n", doors, slowest);
            if (!doors) fail("no monster door with room on both sides on this map");
            puts("DERP doors smoke test passed.");
            I_Quit();
            return;
        }
        for (int k = 0; k < GROUP; k++)
        {
            fixed_t x, y;
            if (!spot(k, &x, &y)) continue;
            mobj_t *m = P_SpawnMobj(x, y, ONFLOORZ, MT_POSSESSED);
            if (!P_CheckPosition(m, x, y) || tmfloorz - tmdropoffz > 24*FRACUNIT) { P_RemoveMobj(m); continue; }
            m->angle = R_PointToAngle2(x, y, bx, by);
            m->target = players[0].mo;
            m->mind = MIND_INVESTIGATE;
            m->goalx = bx; m->goaly = by; m->goaltic = leveltime; m->waytic = -1000;
            P_SetMobjState(m, m->info->seestate);
            crossed[count] = 0;
            mo[count++] = m;
        }
        t0 = leveltime;
        return;
    }
    int all = 1, t = 0;
    for (int k = 0; k < count; k++)
    {
        /* Past the line by 32, or it saw the player through it (then it
           fights), or it chose to wait by the doorway (a trap, p_tactics.c). */
        if (!crossed[k] && (FixedMul(mo[k]->x - mx, nx) + FixedMul(mo[k]->y - my, ny) < -32*FRACUNIT
                            || mo[k]->mind == MIND_HUNT || mo[k]->mind == MIND_AMBUSH))
            crossed[k] = leveltime - t0;
        if (!crossed[k])
        {
            all = 0;
            /* Through the door is where it is going, whatever the door's own noise says. */
            mo[k]->mind = MIND_INVESTIGATE;
            mo[k]->goalx = bx; mo[k]->goaly = by; mo[k]->goaltic = leveltime;
        }
        else if (crossed[k] > t) t = crossed[k];
    }
    if (getenv("DOORTRACE") && !((leveltime - t0) % 17))
        for (int k = 0; k < count; k++)
            printf("  t%d #%d at (%d,%d) mind %d dir %d way %d,%d\n", leveltime - t0, mo[k]->serial, mo[k]->x >> FRACBITS,
                   mo[k]->y >> FRACBITS, mo[k]->mind, mo[k]->movedir, mo[k]->wayx >> FRACBITS, mo[k]->wayy >> FRACBITS);
    if (!all && leveltime - t0 <= 10*TICRATE) return;
    printf("door line %d at (%d,%d), %d zombies: %s in %d tics\n", line, mx >> FRACBITS, my >> FRACBITS, count,
           all ? "all through" : "not all through", all ? t : leveltime - t0);
    if (!all) fail("zombies did not all get through a door within 10 s");
    doors++;
    if (t > slowest) slowest = t;
    for (int k = 0; k < count; k++) P_RemoveMobj(mo[k]);
    count = 0;
    shut_doors();
}

int main(int argc, char **argv)
{
    if (argc != 2) return 1;
    if (!SDL_Init(SDL_INIT_VIDEO)) return 1;
    char *args[16] = {"derp-doors-smoke", "-iwad", argv[1], "-warp", "1", "1", "-skill", "3",
                      "-nomonsters", "-nomusic", "-nosound", "-derpai"};
    myargc = 12; myargv = args;
    D_DoomMain();
    return 0;
}
