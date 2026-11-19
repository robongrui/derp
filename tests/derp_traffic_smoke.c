/* DERP's openings (p_portals.h) on a real level: at a monster door, three
   zombies from each side, sent across. All get through, each way; nobody
   stands in the frame; those waiting stand back from it. Then the first of a
   group shot in the doorway: the next hold back for a few seconds and still
   get through.
   doom-derp-traffic-smoke <iwad> */
#include <math.h>
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
#include "p_portals.h"
#include "p_senses.h"
#include "p_tactics.h"
#include "p_bullets.h"
#include "p_director.h"
extern fixed_t tmfloorz, tmdropoffz;
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#define EACH 3
#define MOST (2*EACH)
static int line = -1, t0, count, doors, stage, killed_at, held_until, framed[MOST], idle[MOST], worst_idle, slowest;
static mobj_t *mo[MOST];
static int from[MOST], crossed[MOST];
static fixed_t lastx[MOST], lasty[MOST], goalx[MOST], goaly[MOST];
static fixed_t nx, ny, mx, my, ux, uy;

static void fail(const char *what) { I_Error("DERP traffic smoke test: %s", what); }

static boolean standable(fixed_t x, fixed_t y)
{
    int nav = P_NavClassFor(20, 56), c = P_NavCellAt(nav, x, y);
    return c >= 0 && P_NavStandable(nav, c) && !P_NavDoor(nav, c);
}

/* How far along the way through, front side positive. */
static fixed_t along(mobj_t *m) { return FixedMul(m->x - mx, nx) + FixedMul(m->y - my, ny); }

/* The next narrow monster door with room on both sides. */
static boolean next_door(void)
{
    while (++line < numlines)
    {
        line_t *ld = &lines[line];
        fixed_t len;
        if (ld->special != 1 || !ld->backsector || (ld->flags & (ML_SECRET | ML_BLOCKMONSTERS))) continue;
        mx = ld->v1->x + ld->dx/2; my = ld->v1->y + ld->dy/2;
        len = P_AproxDistance(ld->dx, ld->dy);
        if (len > 96*FRACUNIT) continue;
        ux = FixedDiv(ld->dx, len); uy = FixedDiv(ld->dy, len);
        nx = uy; ny = -ux;
        /* The line is a face: the middle of the door lies behind it. */
        if (standable(mx + 160*nx, my + 160*ny) && standable(mx - 200*nx, my - 200*ny)
            && standable(mx + 96*nx, my + 96*ny) && standable(mx - 136*nx, my - 136*ny))
            return true;
    }
    return false;
}

static mobj_t *zombie(fixed_t x, fixed_t y, fixed_t gx, fixed_t gy, int side)
{
    mobj_t *m = P_SpawnMobj(x, y, ONFLOORZ, MT_POSSESSED);
    if (!P_CheckPosition(m, x, y)) { P_RemoveMobj(m); return NULL; }
    m->angle = R_PointToAngle2(x, y, gx, gy);
    m->target = players[0].mo;
    m->mind = MIND_INVESTIGATE;
    m->goalx = gx; m->goaly = gy; m->goaltic = leveltime; m->waytic = -1000;
    m->ambushed = 1000;	/* no traps here: the traffic is what is judged */
    P_SetMobjState(m, m->info->seestate);
    goalx[count] = gx; goaly[count] = gy;
    from[count] = side; crossed[count] = 0; framed[count] = 0; idle[count] = 0;
    lastx[count] = x; lasty[count] = y;
    mo[count++] = m;
    return m;
}

static void group(int side, fixed_t gx, fixed_t gy)
{
    static const int out[EACH] = {96, 96, 150}, lat[EACH] = {-24, 24, 0};
    fixed_t off = side > 0 ? 0 : -40*FRACUNIT;	/* the far face is some 16-40 behind this one */
    for (int k = 0; k < EACH; k++)
    {
        fixed_t x = mx + off + 0, y = my;
        x = mx + side*out[k]*nx + lat[k]*ux + FixedMul(off, nx);
        y = my + side*out[k]*ny + lat[k]*uy + FixedMul(off, ny);
        if (standable(x, y)) zombie(x, y, gx, gy, side);
    }
}

static void start_scene(int oneway)
{
    fixed_t fx = mx + 200*nx, fy = my + 200*ny, bx = mx - 240*nx, by = my - 240*ny;
    count = 0;
    group(1, bx, by);
    if (!oneway) group(-1, fx, fy);
    t0 = leveltime;
}

static void clear_scene(void)
{
    for (int k = 0; k < count; k++) if (mo[k]->health > 0) P_RemoveMobj(mo[k]);
    count = 0;
}

/* Who is through, who stands still in the frame. */
static int watch(void)
{
    int all = 1;
    for (int k = 0; k < count; k++)
    {
        fixed_t a;
        if (mo[k]->health <= 0) continue;
        a = along(mo[k]);
        if (!crossed[k] && (from[k] > 0 ? a < -72*FRACUNIT : a > 32*FRACUNIT)) crossed[k] = leveltime - t0;
        /* Across is where it is going, whatever the door's noise says. */
        if (!crossed[k] && mo[k]->mind != MIND_HUNT)
        {
            mo[k]->mind = MIND_INVESTIGATE;
            mo[k]->goalx = goalx[k]; mo[k]->goaly = goaly[k]; mo[k]->goaltic = leveltime;
        }
        if (!crossed[k] && mo[k]->mind != MIND_HUNT && mo[k]->mind != MIND_AMBUSH) all = 0;
        if (P_AproxDistance(mo[k]->x - lastx[k], mo[k]->y - lasty[k]) < FRACUNIT
            && P_NavDoor(P_NavClassOf(mo[k]), P_NavCellAt(P_NavClassOf(mo[k]), mo[k]->x, mo[k]->y)))
        {
            if (++idle[k] > worst_idle) worst_idle = idle[k];
        }
        else idle[k] = 0;
        lastx[k] = mo[k]->x; lasty[k] = mo[k]->y;
    }
    return all;
}

/* Every door down: he is behind a shut one, as when one closes on them. */
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

/* He stands behind a door with a friend just fallen in it; a soldier and an imp
   on the other side, with word of him. */
static mobj_t *chokers[2], *gun;
static int covered, imp_went;

static void start_choke(void)
{
    mobj_t *fallen, *him = players[0].mo;
    fixed_t hx = 0, hy = 0;
    boolean hidden = false;
    fallen = P_SpawnMobj(mx - 8*nx, my - 8*ny, ONFLOORZ, MT_POSSESSED);
    P_DamageMobj(fallen, NULL, NULL, 10000);
    shut_doors();
    chokers[0] = P_SpawnMobj(mx + 150*nx, my + 150*ny, ONFLOORZ, MT_POSSESSED);
    chokers[1] = P_SpawnMobj(mx + 150*nx + 48*ux, my + 150*ny + 48*uy, ONFLOORZ, MT_TROOP);
    /* He stands beyond the door, somewhere neither can see him from. */
    for (int out = 120; out <= 320 && !hidden; out += 40)
        for (int lat = -96; lat <= 96 && !hidden; lat += 32)
        {
            hx = mx - out*nx + lat*ux; hy = my - out*ny + lat*uy;
            if (!standable(hx, hy)) continue;
            P_UnsetThingPosition(him);
            him->x = hx; him->y = hy;
            P_SetThingPosition(him);
            him->z = him->floorz = him->subsector->sector->floorheight;
            hidden = !P_CheckSight(chokers[0], him) && !P_CheckSight(chokers[1], him);
        }
    if (!hidden) fail("nowhere behind the door out of their sight");
    for (int k = 0; k < 2; k++)
    {
        mobj_t *m = chokers[k];
        m->target = him;
        m->mind = MIND_HUNT;
        m->knownx = hx; m->knowny = hy; m->knowntic = leveltime;
        m->decidetic = leveltime;
        P_SetMobjState(m, m->info->seestate);
    }
    t0 = leveltime;
}

void I_TestFrame(void)
{
    if (gamestate != GS_LEVEL || leveltime < 2) return;
    switch (stage)
    {
      case 0:
        if (!P_DerpAI() || !P_NavReady()) fail("DERP's monsters or their navigation are not on");
        if (!P_Portals()) fail("no openings on this map");
        players[0].cheats |= CF_GODMODE;
        printf("Openings: %d.\n", P_Portals());
        if (!next_door())
        {
            if (!doors) fail("no narrow monster door with room on both sides");
            printf("Traffic: %d doors both ways, slowest %d tics, at most %d tics stood in a frame.\n", doors, slowest, worst_idle);
            if (worst_idle > 3*TICRATE) fail("a monster stood in a doorway for over 3 s");
            line = -1;
            next_door();
            start_scene(1);
            stage = 2;
            break;
        }
        printf("Door line %d, %d wide.\n", line, P_PortalWidth(mx, my));
        /* Nobody stops to fire in the frame. */
        {
            mobj_t *m = P_SpawnMobj(mx - 8*nx, my - 8*ny, ONFLOORZ, MT_POSSESSED);
            m->target = players[0].mo;
            m->movedir = 8;  /* standing: walking through, it may */
            if (P_AproxDistance(m->x - players[0].mo->x, m->y - players[0].mo->y) > 192*FRACUNIT
                && (!P_PortalInFrame(m) || P_DerpMayFire(m)))
                fail("a zombie in a door frame may stop to fire");
            P_RemoveMobj(m);
        }
        start_scene(0);
        if (count < 2*EACH - 1) { clear_scene(); break; }
        stage = 1;
        break;
      case 1:
      {
        int all = watch();
        if (getenv("TRAFFICTRACE") && !((leveltime - t0) % 70))
            for (int k = 0; k < count; k++)
                printf("  t%d #%d side %d along %d (%d,%d) mind %d dir %d way %d,%d goal %d,%d crossed %d\n", leveltime - t0, mo[k]->serial, from[k],
                       along(mo[k]) >> FRACBITS, mo[k]->x >> FRACBITS, mo[k]->y >> FRACBITS, mo[k]->mind, mo[k]->movedir,
                       mo[k]->wayx >> FRACBITS, mo[k]->wayy >> FRACBITS, mo[k]->goalx >> FRACBITS, mo[k]->goaly >> FRACBITS, crossed[k]);
        if (!all && leveltime - t0 <= 30*TICRATE) break;
        if (!all) fail("zombies from both sides did not all get through within 30 s");
        for (int k = 0; k < count; k++) if (crossed[k] > slowest) slowest = crossed[k];
        doors++;
        clear_scene();
        stage = doors >= 3 ? 9 : 0;
        break;
      }
      case 9:
        printf("Traffic: %d doors both ways, slowest %d tics, at most %d tics stood in a frame.\n", doors, slowest, worst_idle);
        if (worst_idle > 3*TICRATE) fail("a monster stood in a doorway for over 3 s");
        start_scene(1);
        stage = 2;
        break;
      case 2:
        /* The first one in the frame is shot there. */
        watch();
        for (int k = 0; k < count && !killed_at; k++)
            if (mo[k]->health > 0 && labs(along(mo[k])) < 48*FRACUNIT)
            {
                P_DamageMobj(mo[k], NULL, NULL, 10000);
                killed_at = leveltime;
                held_until = 0;
            }
        if (killed_at) { stage = 3; break; }
        if (leveltime - t0 > 15*TICRATE) fail("nobody reached the door");
        break;
      case 3:
      {
        int all = watch();
        /* Nobody else in the frame for a few seconds. */
        if (!held_until)
            for (int k = 0; k < count; k++)
                if (mo[k]->health > 0 && labs(along(mo[k])) < 24*FRACUNIT) held_until = leveltime;
        if (!all && leveltime - killed_at <= 25*TICRATE) break;
        if (!all) fail("after a death in the doorway the others never got through");
        printf("A death in the doorway: the next went in after %d tics, all through after %d.\n",
               held_until ? held_until - killed_at : -1, leveltime - killed_at);
        if (held_until && held_until - killed_at < 3*TICRATE) fail("the next soldier walked straight into where its friend fell");
        clear_scene();
        start_choke();
        stage = 4;
        break;
      }
      case 4:
      {
        /* He stays there. It covers the way out, it does not come through. */
        chokers[0]->knowntic = chokers[1]->knowntic = leveltime;
        if (chokers[1]->mind == MIND_FLANK) imp_went = 1;
        fixed_t a = along(chokers[0]);
        if (a < 0) fail("with him holding the door a soldier came through it");
        if (chokers[0]->mind == MIND_AMBUSH && !covered) covered = leveltime - t0;
        if (covered && chokers[0]->mind != MIND_AMBUSH) fail("it took cover from the door and left it at once");
        if (getenv("TRAFFICTRACE") && !((leveltime - t0) % 35)) printf("  t%d soldier mind %d tactic %d decide %d at (%d,%d) state %d sees %d\n", leveltime - t0, chokers[0]->mind, chokers[0]->tactic, chokers[0]->decidetic - leveltime, chokers[0]->x >> FRACBITS, chokers[0]->y >> FRACBITS, (int)(chokers[0]->state - states), P_CheckSight(chokers[0], players[0].mo));
        if (leveltime - t0 < 6*TICRATE) break;
        printf("Choke: the soldier took cover after %d tics at (%d,%d), %d units from the line through; the imp's mind %d (flank %d) at (%d,%d).\n",
               covered, chokers[0]->x >> FRACBITS, chokers[0]->y >> FRACBITS,
               abs(FixedMul(chokers[0]->x - mx, ux) + FixedMul(chokers[0]->y - my, uy)) >> FRACBITS,
               chokers[1]->mind, MIND_FLANK, chokers[1]->x >> FRACBITS, chokers[1]->y >> FRACBITS);
        if (!covered) fail("with him holding a door and a friend fallen there, a soldier did not take cover");
        if (abs(FixedMul(chokers[0]->x - mx, ux) + FixedMul(chokers[0]->y - my, uy)) < 44*FRACUNIT)
            fail("covering a door, a soldier stood in the line of it");
        if (!imp_went) fail("with him holding a door, an imp did not set off round him");
        /* Then a fight starts on his side of the door, the director pressing. */
        P_DirectorStance(true);
        gun = P_SpawnMobj(players[0].mo->x + 64*ux, players[0].mo->y + 64*uy, ONFLOORZ, MT_POSSESSED);
        gun->reactiontime = 10000;
        stage = 5; t0 = leveltime;
        break;
      }
      case 5:
        chokers[0]->knowntic = leveltime;
        if (!(leveltime % 5))
            P_FireBulletAt(gun, players[0].mo->x + 32*ux, players[0].mo->y + 32*uy, players[0].mo->z + 32*FRACUNIT, 0);
        if (chokers[0]->mind != MIND_AMBUSH)
        {
            printf("Listening: rounds on him, it went in after %d tics (mind %d).\n", leveltime - t0, chokers[0]->mind);
            puts("DERP traffic smoke test passed.");
            I_Quit();
        }
        if (leveltime - t0 > 6*TICRATE) fail("with a fight going on next door, a soldier stayed at its post");
        break;
    }
}

int main(int argc, char **argv)
{
    if (argc != 2) return 1;
    if (!SDL_Init(SDL_INIT_VIDEO)) return 1;
    char *args[16] = {"derp-traffic-smoke", "-iwad", argv[1], "-warp", "1", "1", "-skill", "3",
                      "-nomonsters", "-nomusic", "-nosound", "-derpai"};
    myargc = 12; myargv = args;
    D_DoomMain();
    return 0;
}
