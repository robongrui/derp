/* DERP's director (p_director.h) on a real level: an unseen monster on its
   way somewhere covers ground faster than a seen one (still slower than
   the player runs); a nudge sends an idle monster to look round his room,
   not at him; his damage raises the tension; searchers hold back while he
   is badly hurt and unseen; nothing of it with the director off.
   doom-derp-director-smoke <iwad> */
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
#include "p_director.h"
#include "p_nav.h"
#include "p_senses.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

static int stage, stagetic;
static void fail(const char *what) { I_Error("DERP director smoke test: %s", what); }
static mobj_t *player(void) { return players[0].mo; }
void A_Look(mobj_t *actor);
static boolean asleep(mobj_t *mo) { return mo->state->action.acp1 == (actionf_p1)A_Look; }

static void move_to(mobj_t *mo, fixed_t x, fixed_t y)
{
    P_UnsetThingPosition(mo);
    mo->x = x; mo->y = y;
    P_SetThingPosition(mo);
    mo->z = mo->floorz = mo->subsector->sector->floorheight;
    mo->momx = mo->momy = mo->momz = 0;
}

/* A standable spot lo-hi units (straight) from (sx, sy), seen or not by the
   player as asked, with a route from (sx, sy). */
static boolean find_spot(fixed_t sx, fixed_t sy, int lo, int hi, int sight, fixed_t *x, fixed_t *y)
{
    int nav = P_NavClassFor(20, 56), cells = P_NavCells();
    static int path[2];
    mobj_t *probe = NULL;
    for (int c = 0; c < cells; c += 13)
    {
        fixed_t px, py, f, d;
        if (!P_NavStandable(nav, c) || P_NavDoor(nav, c)) continue;
        P_NavPoint(nav, c, &px, &py, &f);
        d = P_AproxDistance(px - sx, py - sy);
        if (d < lo*FRACUNIT || d > hi*FRACUNIT) continue;
        if (!probe) probe = P_SpawnMobj(px, py, ONFLOORZ, MT_POSSESSED);
        move_to(probe, px, py);
        if (!P_CheckPosition(probe, px, py)) continue;
        if (sight >= 0 && sight != (int)P_CheckSight(probe, player())) continue;
        if (P_NavPath(nav, sx, sy, px, py, path, 2, 40000) <= 0) continue;
        *x = px; *y = py;
        P_RemoveMobj(probe);
        return true;
    }
    if (probe) P_RemoveMobj(probe);
    return false;
}

static mobj_t *zombie_at(fixed_t x, fixed_t y)
{
    mobj_t *mo = P_SpawnMobj(x, y, ONFLOORZ, MT_POSSESSED);
    mo->spawnpoint.x = x >> FRACBITS;
    mo->spawnpoint.y = y >> FRACBITS;
    mo->spawnpoint.type = 3004;
    return mo;
}

/* Offstage: one walk with the director, one without, from the same spot to
   the same goal, both out of the player's sight. */
static mobj_t *walker;
static fixed_t startx, starty, goalx, goaly, walked_on, walked_off;

static void send(void)
{
    move_to(walker, startx, starty);
    P_SetMobjState(walker, walker->info->seestate);
    walker->target = player();
    walker->mind = MIND_INVESTIGATE;
    walker->goalx = goalx; walker->goaly = goaly;
    walker->goaltic = leveltime;
    walker->waytic = -1000;
}

static void start_offstage(void)
{
    if (!find_spot(player()->x, player()->y, 1200, 2600, 0, &startx, &starty)) fail("no hidden spot to walk from");
    if (!find_spot(startx, starty, 700, 1200, 0, &goalx, &goaly)) fail("no hidden goal to walk to");
    walker = zombie_at(startx, starty);
    send();
}

static void end_walk(fixed_t *walked)
{
    *walked = P_AproxDistance(walker->x - startx, walker->y - starty);
}

/* Offstage, at most as fast as the player runs, for slow monsters and fast. */
static void check_speed_cap(void)
{
    static const mobjtype_t types[] = {MT_POSSESSED, MT_SERGEANT, MT_TROOP, MT_VILE};
    for (int i = 0; i < 4; ++i)
    {
        mobj_t *mo = P_SpawnMobj(startx, starty, ONFLOORZ, types[i]);
        int steps;
        /* In its chase frames (set straight, so no action runs and sends it
           back to watching): their tics are what a chase step takes. */
        mo->state = &states[mo->info->seestate];
        mo->tics = mo->state->tics;
        mo->target = player();
        mo->mind = MIND_INVESTIGATE;
        steps = P_DirectorSteps(mo);
        if (P_CheckSight(player(), mo)) fail("the speed check's spot is in sight");
        if ((fixed_t)steps*P_DerpStep(mo) > 16*FRACUNIT*(mo->state->tics > 0 ? mo->state->tics : 1))
            fail("offstage, a monster would outrun the player");
        printf("Offstage: type %d takes %d step(s) of %d a chase step of %d tics.\n", types[i], steps,
               P_DerpStep(mo) >> FRACBITS, mo->state->tics);
        P_RemoveMobj(mo);
    }
}

static void check_offstage(void)
{
    printf("Offstage: walked %d unseen, %d seen.\n", walked_on >> FRACBITS, walked_off >> FRACBITS);
    if (walked_on < walked_off*3/2) fail("unseen, a monster on its way covered no more ground than seen");
    if (walked_on > 16*3*TICRATE*FRACUNIT) fail("offstage, a monster outran vanilla's Doomguy");
    printf("Offstage: unseen, it covered %d units in 3 s; walking, %d.\n", walked_on >> FRACBITS, walked_off >> FRACBITS);
    check_speed_cap();
}

/* A nudge after a quiet: an idle one sent to look round his room. */
static void check_nudge(void)
{
    fixed_t x, y, sx, sy;
    mobj_t *idle, *sent, *watching = NULL;
    /* One idle in sight of him, but not looking: never the one sent (it
       would know where he is without spotting him). */
    if (find_spot(player()->x, player()->y, 520, 1000, 1, &sx, &sy)) watching = zombie_at(sx, sy);
    if (!find_spot(player()->x, player()->y, 600, 1800, 0, &x, &y)) fail("no hidden spot for an idle monster");
    idle = zombie_at(x, y);
    sent = P_DirectorNudge();
    if (watching && sent == watching) fail("the nudge woke a monster in sight of the player");
    if (sent != idle) fail("a quiet's nudge did not send the idle monster");
    if (watching) { printf("Nudge: one in sight of him was passed over.\n"); P_RemoveMobj(watching); }
    if (asleep(idle) || idle->mind != MIND_INVESTIGATE) fail("the nudged monster did not go to look");
    if (P_NavRoom(R_PointInSubsector(idle->goalx, idle->goaly)->sector - sectors)
        != P_NavRoom(player()->subsector->sector - sectors))
        fail("the nudged monster was not sent to the player's room");
    if (P_AproxDistance(idle->goalx - player()->x, idle->goaly - player()->y) < 128*FRACUNIT)
        fail("the nudged monster was sent right at the player");
    printf("Nudge: an idle one %d units off sent to look round his room, %d units from him.\n",
           P_AproxDistance(x - player()->x, y - player()->y) >> FRACBITS,
           P_AproxDistance(idle->goalx - player()->x, idle->goaly - player()->y) >> FRACBITS);
    P_RemoveMobj(idle);
}

/* Tension, and holding back. */
static mobj_t *searcher;
static fixed_t heldx, heldy;
static int tension_before;

static void start_tension(void)
{
    fixed_t x, y;
    tension_before = P_DirectorTension();
    players[0].cheats &= ~CF_GODMODE;
    P_DamageMobj(player(), NULL, NULL, 80);
    players[0].cheats |= CF_GODMODE;
    if (!find_spot(player()->x, player()->y, 700, 1800, 0, &x, &y)) fail("no hidden spot for a searcher");
    searcher = zombie_at(x, y);
    P_SetMobjState(searcher, searcher->info->seestate);
    searcher->target = player();
    searcher->mind = MIND_SEARCH;
    searcher->searchtic = leveltime;
    searcher->knownx = x; searcher->knowny = y; searcher->knowntic = leveltime;
    searcher->goalx = x + 256*FRACUNIT; searcher->goaly = y; searcher->goaltic = leveltime;
}

static int search_began;
static void check_tension(void)
{
    if (P_DirectorTension() < tension_before + 300) fail("the player's damage did not raise the tension");
    if (players[0].health >= 25 || !P_DirectorHoldBack()) fail("badly hurt and unseen, the searchers were not held back");
    heldx = searcher->x; heldy = searcher->y;
    search_began = searcher->searchtic;
}

static void check_held(void)
{
    if (P_AproxDistance(searcher->x - heldx, searcher->y - heldy) > 2*FRACUNIT) fail("held back, a searcher still walked");
    if (searcher->searchtic <= search_began) fail("held back, a searcher's search was used up while it waited");
    printf("Director: his damage raised the tension to %d; hurt and unseen, a searcher lingered, its search kept.\n", P_DirectorTension());
    /* The walker on its way unseen: steps with the director, one without;
       held back with it, not without (he is still badly hurt here). */
    P_SetMobjState(walker, walker->info->seestate);
    walker->mind = MIND_INVESTIGATE;
    if (P_DirectorSteps(walker) <= 1) fail("unseen on its way, a monster got no offstage steps");
    derpparts.director = false;
    if (P_DirectorSteps(walker) != 1 || P_DirectorHoldBack() || P_DirectorNudge())
        fail("with the director off, it still moves monsters");
    derpparts.director = true;
    players[0].health = player()->health = 100;
    printf("Director off: no offstage steps, no nudges.\n");
}

void I_TestFrame(void)
{
    if (gamestate != GS_LEVEL || leveltime < 2) return;
    switch (stage)
    {
      case 0:
        if (!P_DerpAI() || !P_NavReady()) fail("DERP's monsters or their navigation are not on");
        players[0].cheats |= CF_GODMODE;
        start_offstage(); stage = 1; stagetic = leveltime; break;
      case 1:
        if (getenv("DIRECTORTRACE") && !(leveltime % 8))
            printf("tic %d mind %d steps %d state %d dist %d seen %d at %d,%d dir %d floor %d goal %d,%d way %d,%d sector %d\n",
                   leveltime, walker->mind, P_DirectorSteps(walker),
                   (int)(walker->state - states), P_AproxDistance(walker->x - startx, walker->y - starty) >> FRACBITS,
                   (int)P_CheckSight(player(), walker), walker->x >> FRACBITS, walker->y >> FRACBITS, walker->movedir,
                   walker->floorz >> FRACBITS, goalx >> FRACBITS, goaly >> FRACBITS, walker->wayx >> FRACBITS,
                   walker->wayy >> FRACBITS, (int)(walker->subsector->sector - sectors));
        if (leveltime - stagetic < 3*TICRATE) break;
        end_walk(&walked_on);
        derpparts.director = false;
        send(); stage = 2; stagetic = leveltime; break;
      case 2:
        if (leveltime - stagetic < 3*TICRATE) break;
        end_walk(&walked_off);
        derpparts.director = true;
        check_offstage();
        P_RemoveMobj(walker);
        check_nudge();
        walker = zombie_at(startx, starty);
        start_tension(); stage = 3; stagetic = leveltime; break;
      case 3:
        if (leveltime - stagetic < TICRATE) break;
        check_tension(); stage = 4; stagetic = leveltime; break;
      case 4:
        if (leveltime - stagetic < 2*TICRATE) break;
        check_held(); puts("DERP director smoke test passed."); I_Quit();
        break;
    }
}

int main(int argc, char **argv)
{
    if (argc != 2) return 1;
    if (!SDL_Init(SDL_INIT_VIDEO)) return 1;
    char *args[16] = {"derp-director-smoke", "-iwad", argv[1], "-warp", "1", "1", "-skill", "3",
                      "-nomonsters", "-nomusic", "-nosound", "-derpai"};
    myargc = 12; myargv = args;
    D_DoomMain();
    return 0;
}
