/* DERP's imp packs (p_demons.c) on a real level: three imps after the
   player from one spot come at him from round about, each from its own
   side, instead of in a file; with the demons' ways off they do not.
   doom-derp-pack-smoke <iwad> */
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
static fixed_t sx, sy;
static void fail(const char *what) { I_Error("DERP pack smoke test: %s", what); }
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

/* The player moved out into the open: a spot standable all round at 128
   and 256 units (a pack needs room to go round him), with a spot 560-760
   off in his sight for the imps. */
static boolean into_the_open(void)
{
    int nav = P_NavClassFor(20, 56), cells = P_NavCells();
    for (int c = 0; c < cells; c += 5)
    {
        fixed_t px, py, f, qx, qy;
        int open = 1;
        if (!P_NavStandable(nav, c) || P_NavDoor(nav, c)) continue;
        P_NavPoint(nav, c, &px, &py, &f);
        for (int k = 0; k < 16 && open; k++)
            for (int r = 128; r <= 256 && open; r += 128)
            {
                angle_t an = (angle_t)k*(ANG45/2);
                if (P_NavCellAt(nav, px + FixedMul(r*FRACUNIT, finecosine[an >> ANGLETOFINESHIFT]),
                                py + FixedMul(r*FRACUNIT, finesine[an >> ANGLETOFINESHIFT])) < 0) open = 0;
            }
        if (!open) continue;
        move_to(player(), px, py);
        if (find_spot(560, 760, 0, 0, 0, &qx, &qy)) { sx = qx; sy = qy; return true; }
    }
    return false;
}

static mobj_t *imp[3];
static int count = 3;
static mobj_t *zombie[2];

/* Beside the spot, free, and with a way to him (not over a wall: one with
   no way there blinks in, and that is no pack). */
static void place(mobj_t *mo, int i)
{
    static const int off[9][2] = {{0,0},{1,0},{-1,0},{0,1},{0,-1},{1,1},{-1,1},{1,-1},{-1,-1}};
    static int path[2];
    int nav = P_NavClassFor(20, 56);
    for (int k = 0; k < 9*3; k++)
    {
        fixed_t x = sx + off[(i*3 + k) % 9][0]*64*FRACUNIT, y = sy + off[(i*3 + k) % 9][1]*64*FRACUNIT;
        move_to(mo, x, y);
        if (P_CheckPosition(mo, x, y) && P_NavPath(nav, x, y, player()->x, player()->y, path, 2, 40000) > 0) return;
    }
    move_to(mo, sx, sy);
}

static void release(void)
{
    for (int i = 0; i < count; i++)
    {
        imp[i] = P_SpawnMobj(sx, sy, ONFLOORZ, MT_TROOP);
        place(imp[i], i);
        imp[i]->target = player();
        imp[i]->mind = MIND_HUNT;
        imp[i]->knownx = player()->x; imp[i]->knowny = player()->y; imp[i]->knowntic = leveltime;
        imp[i]->health = 1000;
        imp[i]->pouncetic = leveltime;	/* no pounce for a while: the approach is what is watched */
        P_SetMobjState(imp[i], imp[i]->info->seestate);
    }
}

/* Where each came in from: its bearing from him when it first got within
   ARRIVE of him (-1 not yet). A pack comes at him from round about; imps
   without it come in a file. (Once at his throat, any imps swarm round
   him, so the widest spread over time says little.) */
#define ARRIVE 160
static int arrived[3];

static int bearing(mobj_t *mo, angle_t from)
{
    int deg = (int)((double)(angle_t)(R_PointToAngle2(player()->x, player()->y, mo->x, mo->y) - from)*360/4294967296.0);
    return deg > 180 ? 360 - deg : deg;
}

static void watch(void)
{
    for (int i = 0; i < count; i++)
        if (arrived[i] < 0 && P_AproxDistance(imp[i]->x - player()->x, imp[i]->y - player()->y) < ARRIVE*FRACUNIT)
            arrived[i] = (int)((double)R_PointToAngle2(player()->x, player()->y, imp[i]->x, imp[i]->y)*360/4294967296.0);
}

/* Where it came in from, or stands now if it did not: the pack's first
   holds him with its fireballs while another goes round. */
static int from(int i)
{
    return arrived[i] >= 0 ? arrived[i]
        : (int)((double)R_PointToAngle2(player()->x, player()->y, imp[i]->x, imp[i]->y)*360/4294967296.0);
}

/* The widest angle between two of them (-1 none came in). */
static int spread(void)
{
    int widest = -1;
    for (int i = 0; i < count; i++)
        for (int j = i + 1; j < count; j++)
        {
            if (arrived[i] < 0 && arrived[j] < 0) continue;
            int deg = abs(from(i) - from(j));
            if (deg > 180) deg = 360 - deg;
            if (deg > widest) widest = deg;
        }
    return widest;
}

static void tracepack(void)
{
    if (getenv("PACKTRACE") && !(leveltime % 17))
        for (int i = 0; i < count; i++)
            printf("t%d imp%d #%d target %s mind %d dist %d bearing %d way %d,%d\n", leveltime - stagetic, i, imp[i]->serial,
                   imp[i]->target == player() ? "him" : imp[i]->target ? "other" : "none", imp[i]->mind, P_AproxDistance(imp[i]->x - player()->x, imp[i]->y - player()->y) >> FRACBITS,
                   (int)((double)R_PointToAngle2(player()->x, player()->y, imp[i]->x, imp[i]->y)*360/4294967296.0),
                   imp[i]->wayx >> FRACBITS, imp[i]->wayy >> FRACBITS);
}

static void start(void)
{
    for (int i = 0; i < 3; i++) arrived[i] = -1;
    release();
    /* He watches them come (to imps going round, a man looking away is an opening). */
    player()->angle = R_PointToAngle2(player()->x, player()->y, sx, sy);
    stagetic = leveltime;
}

static int with, without;

void I_TestFrame(void)
{
    if (gamestate != GS_LEVEL || leveltime < 2) return;
    /* He stands where he was put: fireballs' knock-back would move him
       (and so every imp's way) by how the player slides, not by the imps. */
    if (stage) player()->momx = player()->momy = 0;
    if (stage) { tracepack(); watch(); }
    switch (stage)
    {
      case 0:
        if (!P_DerpAI() || !P_NavReady()) fail("DERP's monsters or their navigation are not on");
        players[0].cheats |= CF_GODMODE;
        if (!into_the_open()) fail("no open place with a spot far off in sight");
        start(); stage = 1;
        break;
      case 1:
        if (leveltime - stagetic < 12*TICRATE) break;
        with = spread();
        for (int i = 0; i < count; i++) P_RemoveMobj(imp[i]);
        derpparts.demons = false;
        start(); stage = 2; break;
      case 2:
        if (leveltime - stagetic < 12*TICRATE) break;
        without = spread();
        derpparts.demons = true;
        printf("Pack: three came in from %d degrees apart as a pack, %d without.\n", with, without);
        if (with < 60) fail("a pack of imps did not come at him from round about");
        if (with < without + 30) fail("a pack came in no wider apart than imps without it");
        for (int i = 0; i < count; i++) P_RemoveMobj(imp[i]);
        /* Two imps split up: one at him from the front, one round behind. */
        count = 2;
        start(); stage = 3; break;
      case 3:
        if (leveltime - stagetic < 12*TICRATE) break;
        with = spread();
        printf("Two imps: came in %d degrees apart.\n", with);
        if (with < 75) fail("two imps did not split up, one round to his side or behind him");
        for (int i = 0; i < count; i++) P_RemoveMobj(imp[i]);
        /* One imp, soldiers fighting him from the front: round behind him. */
        count = 1;
        for (int i = 0; i < 2; i++)
        {
            zombie[i] = P_SpawnMobj(sx + (i ? 64 : -64)*FRACUNIT, sy, ONFLOORZ, MT_POSSESSED);
            zombie[i]->target = player(); zombie[i]->mind = MIND_HUNT; zombie[i]->health = 1000;
            zombie[i]->knownx = player()->x; zombie[i]->knowny = player()->y; zombie[i]->knowntic = leveltime;
            P_SetMobjState(zombie[i], zombie[i]->info->seestate);
            /* Holding fire: shot in the back, an imp turns on them (as Doom's
               monsters do), which is not what is watched here. */
            zombie[i]->reactiontime = 10000;
        }
        start(); stage = 4; break;
      case 4:
      {
        static int off = -1;
        angle_t front = R_PointToAngle2(player()->x, player()->y, (zombie[0]->x >> 1) + (zombie[1]->x >> 1),
                                        (zombie[0]->y >> 1) + (zombie[1]->y >> 1));
        /* How far round it got, near him, from the soldiers as they are then: it may hold
           its place behind him, throwing, and come no nearer, and they bound about. */
        if (P_AproxDistance(imp[0]->x - player()->x, imp[0]->y - player()->y) < 320*FRACUNIT
            && bearing(imp[0], front) > off)
            off = bearing(imp[0], front);
        if (leveltime - stagetic < 20*TICRATE) break;   /* half way round him: a long walk */
        printf("One imp with soldiers: got %d degrees round from their front.\n", off);
        if (off < 100) fail("an imp with soldiers fighting him did not go round behind him");
        puts("DERP pack smoke test passed.");
        I_Quit();
        break;
      }
    }
}

int main(int argc, char **argv)
{
    if (argc != 2) return 1;
    if (!SDL_Init(SDL_INIT_VIDEO)) return 1;
    char *args[16] = {"derp-pack-smoke", "-iwad", argv[1], "-warp", "1", "1", "-skill", "3", "-derplog", "packlog.txt",
                      "-nomonsters", "-nomusic", "-nosound", "-derpai"};
    myargc = getenv("PACKLOG") ? 14 : 12; myargv = args;
    if (!getenv("PACKLOG")) { args[8] = "-nomonsters"; args[9] = "-nomusic"; args[10] = "-nosound"; args[11] = "-derpai"; }
    D_DoomMain();
    return 0;
}
