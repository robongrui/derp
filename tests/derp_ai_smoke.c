/* DERP's monsters (p_derp.h) on a real level: their numbers, a body thrown
   off a ledge and hurt by the fall (the kill the thrower's), one kicked over
   a pool's "block monsters" fence (and one shoved on the floor held), nukage biting
   away from home, and where they stay vanilla.
   doom-derp-ai-smoke <iwad> forced|authored
   forced: -derpai, on any map. authored: the F4 switch alone, which a map
   not id's own (Freedoom) keeps vanilla. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "doomdef.h"
#include "doomstat.h"
#include "d_main.h"
#include "g_game.h"
#include "i_system.h"
#include "m_argv.h"
#include "r_state.h"
#include "p_local.h"
#include "p_derp.h"
#include "p_swept.h"
#include "p_tactics.h"
#include "p_bullets.h"
#include "tables.h"
#include "r_main.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

static boolean forced;
static int stage;

static void fail(const char *what) { I_Error("DERP AI smoke test: %s", what); }

/* A monster frozen in its spawn state: it neither looks nor walks, but its
   body still moves, falls and is hurt (P_MobjThinker). */
static mobj_t *statue(fixed_t x, fixed_t y, mobjtype_t type)
{
    mobj_t *mo = P_SpawnMobj(x, y, ONFLOORZ, type);
    mo->tics = -1;
    return mo;
}

/* Under fire a soldier runs: a second step each chase step, not before
   the player shoots. */
static void check_sprint(void)
{
    mobj_t *player = players[0].mo, *zombie;
    angle_t an = player->angle >> ANGLETOFINESHIFT;
    fixed_t x = player->x + 96*finecosine[an], y = player->y + 96*finesine[an], before;

    zombie = statue(x, y, MT_POSSESSED);
    zombie->movedir = ((R_PointToAngle2(x, y, player->x, player->y) + ANG45/2) >> 29) & 7;
    if (P_DerpSprint(zombie)) fail("a soldier sprinted before the player fired");
    /* A round beside it: the noise alone does not make it run. */
    P_FireBulletAt(player, x + 40*finecosine[(an + ANG90/(1<<ANGLETOFINESHIFT)) & FINEMASK],
                   y + 40*finesine[(an + ANG90/(1<<ANGLETOFINESHIFT)) & FINEMASK], zombie->z + 32*FRACUNIT, 0);
    before = P_AproxDistance(zombie->x - player->x, zombie->y - player->y);
    if (!P_DerpSprint(zombie)) fail("shot at, a soldier in sight did not sprint");
    /* It builds up: the first of these is a sixth of a step, the rest more. */
    {
        fixed_t first = before - P_AproxDistance(zombie->x - player->x, zombie->y - player->y);
        if (first > 4*FRACUNIT) fail("a soldier went from a walk to a run at once");
        for (int k = 0; k < 3; k++) P_DerpSprint(zombie);
    }
    if (P_AproxDistance(zombie->x - player->x, zombie->y - player->y) > before - 8*FRACUNIT)
        fail("a soldier's sprint did not take it a step on");
    P_RemoveMobj(zombie);
    printf("Sprint: shot at, a soldier picks up speed.\n");
}

static void check_numbers(void)
{
    mobj_t *player = players[0].mo, *zombie, *imp;
    int pains = 0, i, saved = leveltime;

    zombie = statue(player->x, player->y, MT_POSSESSED);
    imp = statue(player->x, player->y, MT_TROOP);
    if (zombie->health != 40 || P_DerpHealth(MT_SHOTGUY) != 50 || P_DerpHealth(MT_CHAINGUY) != 90)
        fail("the soldiers did not spawn tougher");
    if (imp->health != 60) fail("the imp's health changed");
    if (P_DerpStep(zombie) != 10*FRACUNIT) fail("the zombie's step is not a quarter more than vanilla's");
    /* Small hits, one a tic: the soldier walks through nearly all of them. */
    for (i = 0; i < 400; ++i) { leveltime = saved + 1 + i; pains += P_DerpPain(zombie, 5); }
    if (pains > 40) fail("a soldier flinches at small hits");
    /* A shotgun at arm's length: seven pellets in one tic stagger it. */
    leveltime = saved + 1000;
    for (i = 0, pains = 0; i < 7; ++i) pains |= P_DerpPain(zombie, 10);
    if (!pains) fail("a close shotgun blast did not stagger a soldier");
    /* Demons keep vanilla's pain. */
    for (i = 0, pains = 0; i < 400; ++i) { leveltime = saved + 2000 + i; pains += P_DerpPain(imp, 5); }
    if (pains < 250) fail("an imp lost its pain");
    leveltime = saved;
    /* A real blow (P_DamageMobj) remembers who threw it. */
    P_DamageMobj(zombie, player, player, 5);
    if (zombie->shover != player->serial || !zombie->shover) fail("a blow did not remember its thrower");
    /* The player at Quake's pace: some 9 units a tic, not 16.7; tuned, a
       harder push and grip to the same top speed (push/(1 - friction)). */
    if (P_UseResponsiveMovement()
        ? (P_DerpPace(1000) != 880 || P_DerpGrip(true, 0xc000) != 0x999a || P_DerpGrip(false, 0xa000) != 0x8000)
        : P_DerpPace(1000) != 550)
        fail("the player's pace is not Quake's");
    if (P_DerpRunSpeed() < 9*FRACUNIT || P_DerpRunSpeed() > 10*FRACUNIT)
        fail("the player's pace is not Quake's");
    /* Switched off, the numbers are vanilla's. */
    derpparts.numbers = false;
    if (P_DerpPace(1000) != 1000) fail("numbers off, the player's pace is not vanilla's");
    if (P_DerpHealth(MT_POSSESSED) != 20 || P_DerpStep(zombie) != 8*FRACUNIT) fail("numbers off are not vanilla's");
    derpparts.numbers = true;
    P_RemoveMobj(zombie);
    P_RemoveMobj(imp);
    printf("Numbers: soldiers 40/50/90, a step of 10, no flinch at small hits; demons as vanilla.\n");
}

/* A ledge to throw a zombie off: a two-sided line, not blocking, with the
   low floor at least drop below the high one and room on both sides. fence:
   one that fences monsters out instead (ML_BLOCKMONSTERS, a pool's edge). */
typedef struct { fixed_t x, y, dx, dy; sector_t *high, *low; } ledge_t;
static boolean find_ledge(int drop, boolean fence, ledge_t *out)
{
    mobj_t probe;
    for (int i = 0; i < numlines; ++i)
    {
        line_t *ld = &lines[i];
        if (!ld->frontsector || !ld->backsector || (ld->flags & ML_BLOCKING) || !(ld->flags & ML_BLOCKMONSTERS) != !fence) continue;
        sector_t *f = ld->frontsector, *b = ld->backsector;
        boolean front_high = f->floorheight - b->floorheight >= drop*FRACUNIT;
        if (!front_high && b->floorheight - f->floorheight < drop*FRACUNIT) continue;
        double lx = (double)ld->dx/FRACUNIT, ly = (double)ld->dy/FRACUNIT, len = sqrt(lx*lx + ly*ly);
        if (len < 96) continue;
        /* The normal to the front (right of v1 to v2), turned toward the low side. */
        double nx = ly/len, ny = -lx/len;
        if (front_high) { nx = -nx; ny = -ny; }
        fixed_t mx = ld->v1->x + ld->dx/2, my = ld->v1->y + ld->dy/2;
        fixed_t sx = mx - (fixed_t)(nx*24*FRACUNIT), sy = my - (fixed_t)(ny*24*FRACUNIT);
        fixed_t lx2 = mx + (fixed_t)(nx*96*FRACUNIT), ly2 = my + (fixed_t)(ny*96*FRACUNIT);
        sector_t *high = front_high ? f : b, *low = front_high ? b : f;
        if (R_PointInSubsector(sx, sy)->sector != high || R_PointInSubsector(lx2, ly2)->sector != low) continue;
        if (high->ceilingheight - high->floorheight < 64*FRACUNIT || low->ceilingheight - low->floorheight < 64*FRACUNIT) continue;
        memset(&probe, 0, sizeof probe);
        probe.type = MT_POSSESSED; probe.info = &mobjinfo[MT_POSSESSED];
        probe.radius = probe.info->radius; probe.height = probe.info->height; probe.flags = probe.info->flags;
        if (!P_CheckPosition(&probe, sx, sy) || tmfloorz != high->floorheight) continue;
        out->x = sx; out->y = sy; out->high = high; out->low = low;
        out->dx = (fixed_t)(nx*FRACUNIT); out->dy = (fixed_t)(ny*FRACUNIT);
        return true;
    }
    return false;
}

static ledge_t ledge;
static mobj_t *thrown, *held;
static int thrown_health, kills_before, drop;

static mobj_t *throw_zombie(int health)
{
    mobj_t *mo = statue(ledge.x, ledge.y, MT_POSSESSED);
    mo->health = health;
    mo->momx = 10*ledge.dx;
    mo->momy = 10*ledge.dy;
    P_DerpShove(mo, players[0].mo);
    return mo;
}

static void start_ledges(void)
{
    static const int drops[] = {256, 192, 128, 96};
    for (int i = 0; i < 4 && !drop; ++i)
        if (find_ledge(drops[i], false, &ledge)) drop = drops[i];
    if (!drop) fail("no ledge on this map to throw a zombie off");
    /* First one that survives the fall, hurt. */
    thrown = throw_zombie(1000);
    thrown_health = thrown->health;
}

static void check_fall(void)
{
    fixed_t low = ledge.low->floorheight;
    /* Down in the low sector, on the ground: not always its floor, since in the air it
       now flies on over a "block monsters" line and may come down across a step there
       (E1M1: 24 up, astride one, which it may step off: P_DerpAstride). */
    if (thrown->subsector->sector != ledge.low || thrown->z != thrown->floorz || thrown->z > low + 24*FRACUNIT)
        fail("a thrown zombie did not go over the ledge");
    if (thrown->health >= thrown_health) fail("the fall did not hurt");
    printf("Ledges: thrown over a drop of %d units, the fall took %d.\n",
           (ledge.high->floorheight - ledge.low->floorheight)/FRACUNIT, thrown_health - thrown->health);
    P_RemoveMobj(thrown);
    /* Then one the fall kills: the kill the thrower's. */
    kills_before = players[0].killcount;
    held = throw_zombie(1);
}

static void check_kill(void)
{
    if (held->health > 0 || players[0].killcount != kills_before + 1)
        fail("the fall's kill was not the thrower's");
    printf("Ledges: the fall's kill went to the player who threw it.\n");
    P_RemoveMobj(held);
    /* Ledges off: the edge holds it, as in vanilla. */
    derpparts.ledges = false;
    thrown = throw_zombie(1000);
}

static void check_held(void)
{
    if (thrown->subsector->sector != ledge.high || thrown->z != ledge.high->floorheight)
        fail("with ledges off a zombie still went over");
    derpparts.ledges = true;
    P_RemoveMobj(thrown);
    printf("Ledges off: the edge held it.\n");
}

/* Kicked off its feet toward a pool fenced with "block monsters" (half of
   E1M1's nukage edges), it flies in over the fence; shoved along the floor,
   the fence still holds it (P_DerpThrown). */
static ledge_t fence;
static boolean fenced;
static mobj_t *kicked, *pushed;
static void start_pool(void)
{
    fenced = find_ledge(1, true, &fence);
    if (!fenced) { printf("Pool: no fenced pool edge on this map, skipped.\n"); return; }
    kicked = statue(fence.x, fence.y, MT_POSSESSED);
    kicked->health = 1000;    /* A soft kick (2 a tic, and the sweep's own 2): E1M1's pool here is a channel some
       100 wide, and a full slide's kick carries him over it onto the far bank. */
    if (!P_Sweep(kicked, R_PointToAngle2(0, 0, fence.dx, fence.dy), 2*fence.dx, 2*fence.dy))
        fail("a zombie at a pool's edge could not be swept");
}

static void check_pool(void)
{
    if (!fenced) return;
    if (kicked->subsector->sector != fence.low)
        fail("a kicked zombie stopped at a pool's fence");
    printf("Pool: kicked over a \"block monsters\" edge, it landed in the pool.\n");
    P_RemoveMobj(kicked);
    pushed = statue(fence.x, fence.y, MT_POSSESSED);
    pushed->health = 1000;
    pushed->momx = 10*fence.dx;
    pushed->momy = 10*fence.dy;
}

static void check_fenced(void)
{
    if (!fenced) return;
    if (pushed->subsector->sector != fence.high)
        fail("shoved along the floor, a zombie went through a pool's fence");
    printf("Pool: shoved along the floor, the fence held it.\n");
    P_RemoveMobj(pushed);
}

static mobj_t *away, *home;
static sector_t *pool;
static void start_floors(void)
{
    for (int i = 0; i < numsectors && !pool; ++i)
        if (sectors[i].special == 5 || sectors[i].special == 7 || sectors[i].special == 16 || sectors[i].special == 4)
            for (int j = 0; j < numsubsectors && !pool; ++j)
            {
                subsector_t *ss = &subsectors[j];
                seg_t *seg = &segs[ss->firstline];
                if (ss->sector != &sectors[i]) continue;
                /* A spot inside the pool: between a seg's start and the subsector's middle. */
                fixed_t cx = 0, cy = 0;
                for (int k = 0; k < ss->numlines; ++k) { cx += segs[ss->firstline+k].v1->x/ss->numlines; cy += segs[ss->firstline+k].v1->y/ss->numlines; }
                (void)seg;
                if (R_PointInSubsector(cx, cy)->sector != &sectors[i]) continue;
                pool = &sectors[i];
                away = statue(cx, cy, MT_POSSESSED); away->health = 1000;
                away->homesector = -1;
                home = statue(cx, cy, MT_POSSESSED); home->health = 1000;
            }
    if (!pool) printf("Floors: no hurting floor on this map, skipped.\n");
}

static void check_floors(void)
{
    if (!pool) return;
    if (away->health >= 1000) fail("nukage did not hurt a monster away from home");
    if (home->health != 1000) fail("nukage hurt a monster in its own pool");
    printf("Floors: a monster away from home took %d, one at home none.\n", 1000 - away->health);
    P_RemoveMobj(away); P_RemoveMobj(home);
}

static void check_vanilla(void)
{
    mobj_t *player = players[0].mo, *zombie;
    /* On id's own map the switch is what turns them on; on any other, not. */
    if (P_StockMap(gameepisode, gamemap))
    {
        if (!P_DerpAI()) fail("the authored switch left DERP's monsters off on id's own map");
        printf("Authored on id's map: DERP's monsters.\n");
        return;
    }
    if (P_DerpAI()) fail("DERP's monsters play on a map not id's own");
    zombie = statue(player->x, player->y, MT_POSSESSED);
    if (zombie->health != 20 || P_DerpStep(zombie) != 8*FRACUNIT) fail("monsters off are not vanilla's");
    P_RemoveMobj(zombie);
    printf("Authored on Freedoom: vanilla monsters (not id's map).\n");
}

void I_TestFrame(void)
{
    if (gamestate != GS_LEVEL) return;
    if (stage == 0 && gametic >= 5)
    {
        if (!forced) { check_vanilla(); puts("DERP AI smoke test passed."); I_Quit(); }
        if (!P_DerpAI()) fail("-derpai did not switch DERP's monsters on");
        gameskill = sk_nightmare;
        if (P_DerpAI()) fail("DERP's monsters play on Nightmare");
        gameskill = sk_medium;
        check_numbers();
        check_sprint();
        start_ledges();
        start_floors();
        start_pool();
        stage = 1;
    }
    else if (stage == 1 && gametic >= 5 + 3*TICRATE)
    {
        check_fall();
        check_floors();
        check_pool();
        stage = 2;
    }
    else if (stage == 2 && gametic >= 5 + 6*TICRATE)
    {
        check_kill();
        check_fenced();
        stage = 3;
    }
    else if (stage == 3 && gametic >= 5 + 9*TICRATE)
    {
        check_held();
        /* A demo, even with -derpai, spawns its first level vanilla:
           G_DoPlayDemo sets the level up before demoplayback is set. */
        G_DeferedPlayDemo("demo1");
        stage = 4;
    }
    else if (stage == 4 && demoplayback)
    {
        int soldiers = 0;
        for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next)
        {
            mobj_t *mo = (mobj_t *)th;
            if (th->function.acp1 != (actionf_p1)P_MobjThinker) continue;
            if (mo->type != MT_POSSESSED && mo->type != MT_SHOTGUY && mo->type != MT_CHAINGUY) continue;
            ++soldiers;
            if (mo->health > mobjinfo[mo->type].spawnhealth) fail("a demo's level spawned DERP's soldiers");
        }
        if (P_DerpAI()) fail("DERP's monsters play in a demo");
        printf("Demo: its first level spawned %d soldiers, all vanilla.\n", soldiers);
        puts("DERP AI smoke test passed.");
        I_Quit();
    }
}

int main(int argc, char **argv)
{
    if (argc != 3) return 1;
    forced = !strcmp(argv[2], "forced");
    derpauthored = !forced;
    if (!SDL_Init(SDL_INIT_VIDEO)) return 1;
    char *args[16] = {"derp-ai-smoke", "-iwad", argv[1], "-warp", "1", "1", "-skill", "3",
                      "-nomonsters", "-nomusic", "-nosound"};
    int count = 11;
    if (forced) args[count++] = "-derpai";
    myargc = count; myargv = args;
    D_DoomMain();
    return 0;
}
