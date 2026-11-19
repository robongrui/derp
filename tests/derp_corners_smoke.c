/* DERP's corners (p_corners.h, p_tactics.c) on a real level: a soldier under
   fire near a wall end shelters behind it, hidden from the player, leans out
   to where it sees him, and stays through several shots instead of looking
   for a better spot.
   doom-derp-corners-smoke <iwad> [episode map] */
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
#include "p_tactics.h"
#include "p_corners.h"
#include "p_fire.h"
#include "p_bullets.h"
#include "tables.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

static int stage, stagetic, tried, covers, moves, shots, lastfire;
static mobj_t *soldier;
static fixed_t coverx, covery, leanx, leany, lastx, lasty;
static int watched, peak;

static void fail(const char *what) { I_Error("DERP corners smoke test: %s", what); }
static mobj_t *player(void) { return players[0].mo; }

static void move_to(mobj_t *mo, fixed_t x, fixed_t y)
{
    P_UnsetThingPosition(mo);
    mo->x = x; mo->y = y;
    P_SetThingPosition(mo);
    mo->z = mo->floorz = mo->subsector->sector->floorheight;
    mo->momx = mo->momy = mo->momz = 0;
}

/* A round from him just over mo's head: it misses, whatever stands beside it. */
static void round_past(mobj_t *mo)
{
    P_FireBulletAt(player(), mo->x, mo->y, mo->z + mo->height + 16*FRACUNIT, 0);
}

static boolean seen_at(mobj_t *probe, fixed_t x, fixed_t y)
{
    boolean seen;
    /* Only a wall at its side: it is meant to be seen there. */
    if (P_DerpCornerTier() == TIER_ENOUGH) return false;
    move_to(probe, x, y);
    /* Low cover hides it on one knee: that much of it, then. */
    probe->height = P_DerpCornerTier() == TIER_LOW ? 32*FRACUNIT : probe->info->height;
    seen = P_CheckSight(player(), probe);
    probe->height = probe->info->height;
    return seen;
}

/* The player on a spot, a soldier 250-600 off in his sight, with a corner
   near it that shelters it. */
static boolean find_scene(void)
{
    int nav = P_NavClassFor(20, 56), cells = P_NavCells();
    mobj_t *probe = NULL;
    for (int a = 0; a < cells; a += 257)
    {
        fixed_t px, py, pf;
        if (!P_NavStandable(nav, a) || P_NavDoor(nav, a) || P_NavHurts(a)) continue;
        P_NavPoint(nav, a, &px, &py, &pf);
        move_to(player(), px, py);
        for (int b = 0; b < cells; b += 1013)
        {
            fixed_t qx, qy, qf, d;
            if (!P_NavStandable(nav, b) || P_NavDoor(nav, b) || P_NavHurts(b)) continue;
            P_NavPoint(nav, b, &qx, &qy, &qf);
            d = P_AproxDistance(qx - px, qy - py);
            if (d < 250*FRACUNIT || d > 600*FRACUNIT) continue;
            if (!probe) probe = P_SpawnMobj(qx, qy, ONFLOORZ, MT_POSSESSED);
            move_to(probe, qx, qy);
            if (!P_CheckPosition(probe, qx, qy) || !P_CheckSight(player(), probe)) continue;
            tried++;
            if (!P_DerpCornerCover(probe, player(), &coverx, &covery, &leanx, &leany)) continue;
            if (seen_at(probe, coverx, covery)) fail("a corner's shelter is in his sight");
            if (P_AproxDistance(coverx - px, covery - py) < 200*FRACUNIT) { move_to(probe, qx, qy); continue; }
            /* Well inside its reach: a step towards him and a far one is gone, and it kneels instead. */
            if (P_AproxDistance(coverx - qx, covery - qy) > 224*FRACUNIT) { move_to(probe, qx, qy); continue; }
            move_to(probe, qx, qy);
            soldier = probe;
            return true;
        }
    }
    if (probe) P_RemoveMobj(probe);
    return false;
}

/* Across heights: he on a floor well above or below the soldier, every
   shelter it picks hidden from his eye by P_CheckSight the way he looks,
   which is not the way it looks (eye to body, not the same both ways). */
static void check_heights(void)
{
    int nav = P_NavClassFor(20, 56), cells = P_NavCells(), pairs = 0, found = 0, seen = 0;
    fixed_t hx = player()->x, hy = player()->y;
    mobj_t *probe = NULL;
    for (int a = 0; a < cells; a += 131)
    {
        fixed_t px, py, pf;
        if (!P_NavStandable(nav, a) || P_NavDoor(nav, a) || P_NavHurts(a)) continue;
        P_NavPoint(nav, a, &px, &py, &pf);
        move_to(player(), px, py);
        for (int b = 0; b < cells; b += 509)
        {
            fixed_t qx, qy, qf, d, cx, cy, kx, ky;
            if (!P_NavStandable(nav, b) || P_NavDoor(nav, b) || P_NavHurts(b)) continue;
            P_NavPoint(nav, b, &qx, &qy, &qf);
            d = P_AproxDistance(qx - px, qy - py);
            if (d < 200*FRACUNIT || d > 700*FRACUNIT) continue;
            if (abs(qf - pf) < 24*FRACUNIT) continue;
            if (!probe) probe = P_SpawnMobj(qx, qy, ONFLOORZ, MT_POSSESSED);
            move_to(probe, qx, qy);
            if (!P_CheckPosition(probe, qx, qy) || !P_CheckSight(player(), probe)) continue;
            pairs++;
            if (!P_DerpCornerCover(probe, player(), &cx, &cy, &kx, &ky)) continue;
            found++;
            if (seen_at(probe, cx, cy))
            {
                if (!seen++) printf("Seen: he (%d,%d) floor %d, shelter (%d,%d) floor %d.\n", px >> FRACBITS, py >> FRACBITS,
                                    pf >> FRACBITS, cx >> FRACBITS, cy >> FRACBITS, probe->z >> FRACBITS);
            }
        }
    }
    if (probe) P_RemoveMobj(probe);
    move_to(player(), hx, hy);
    printf("Across heights: %d in his sight, %d with a corner, %d of those shelters in his sight.\n", pairs, found, seen);
    if (seen) fail("a shelter across a height is in his sight");
}

/* Nobody takes a shelter in use: a second soldier beside the first looks
   elsewhere, and hiding from him too. */
static void check_shared(void)
{
    mobj_t *other = P_SpawnMobj(soldier->x + 40*FRACUNIT, soldier->y, ONFLOORZ, MT_POSSESSED);
    fixed_t cx, cy;
    if (soldier->mind == MIND_COVER && P_DerpHideSpot(other, player(), false, &cx, &cy)
        && P_AproxDistance(cx - soldier->coverx, cy - soldier->covery) < 48*FRACUNIT)
        fail("a second soldier picked a shelter already in use");
    P_RemoveMobj(other);
    printf("Shared: a second soldier looks elsewhere.\n");
}

/* Ducked at a shelter from him, for that long. */
static void back_in_cover(int tics)
{
    fixed_t cx, cy, kx, ky;
    if (soldier->mind != MIND_COVER)
    {
        if (!P_DerpCornerCover(soldier, player(), &cx, &cy, &kx, &ky) && !P_DerpHideSpot(soldier, player(), false, &cx, &cy))
            fail("no cover from him here");
        soldier->coverx = cx; soldier->covery = cy;
        soldier->peekx = cx; soldier->peeky = cy;
        soldier->mind = MIND_COVER;
        soldier->tactictic = leveltime;
    }
    soldier->tactic = STEP_COVER_DUCKED;
    soldier->waytic = leveltime + tics;
    move_to(soldier, soldier->coverx, soldier->covery);
}

/* He goes where he can see its shelter, and stays there, not firing. */
static void start_flank(void)
{
    int nav = P_NavClassFor(20, 56), cells = P_NavCells();
    boolean placed = false;
    /* It must be ducked at the shelter to be caught there. */
    back_in_cover(6*TICRATE);
    lastx = soldier->coverx; lasty = soldier->covery;
    for (int c = 0; c < cells && !placed; c += 3)
    {
        fixed_t x, y, f, d;
        if (!P_NavStandable(nav, c) || P_NavDoor(nav, c) || P_NavHurts(c)) continue;
        P_NavPoint(nav, c, &x, &y, &f);
        d = P_AproxDistance(x - lastx, y - lasty);
        if (d < 200*FRACUNIT || d > 500*FRACUNIT) continue;
        move_to(player(), x, y);
        placed = P_CheckSight(soldier, player());
    }
    if (!placed) fail("nowhere that sees the shelter");
}

/* He is gone, somewhere it cannot see or hear him. */
static void start_gone(void)
{
    int nav = P_NavClassFor(20, 56), cells = P_NavCells();
    boolean placed = false;
    back_in_cover(20*TICRATE);
    for (int c = 0; c < cells && !placed; c += 3)
    {
        fixed_t x, y, f;
        if (!P_NavStandable(nav, c) || P_NavDoor(nav, c) || P_NavHurts(c)) continue;
        P_NavPoint(nav, c, &x, &y, &f);
        if (P_AproxDistance(x - soldier->x, y - soldier->y) < 1500*FRACUNIT) continue;
        move_to(player(), x, y);
        placed = !P_CheckSight(soldier, player());
    }
    if (!placed) fail("nowhere out of its sight");
    soldier->knowntic = leveltime - 3*TICRATE;
}

/* Windows need no corners of their own: a sill is too low to hide behind, and
   the wall beside one ends in a jamb. How many window lines have a corner at
   an end. */
static void count_windows(void)
{
    const corner_t *list;
    int n = P_Corners(&list), windows = 0, with = 0;
    for (int i = 0; i < numlines; i++)
    {
        line_t *ld = &lines[i];
        fixed_t top, bottom, step;
        boolean near = false;
        if (!ld->backsector) continue;
        top = ld->frontsector->ceilingheight < ld->backsector->ceilingheight ? ld->frontsector->ceilingheight : ld->backsector->ceilingheight;
        bottom = ld->frontsector->floorheight > ld->backsector->floorheight ? ld->frontsector->floorheight : ld->backsector->floorheight;
        step = abs(ld->frontsector->floorheight - ld->backsector->floorheight);
        if (step < 16*FRACUNIT || step >= 48*FRACUNIT || top - bottom < 16*FRACUNIT || top - bottom >= 72*FRACUNIT) continue;
        windows++;
        for (int c = 0; c < n && !near; c++)
            near = P_AproxDistance(list[c].x - ld->v1->x, list[c].y - ld->v1->y) < 33*FRACUNIT
                || P_AproxDistance(list[c].x - ld->v2->x, list[c].y - ld->v2->y) < 33*FRACUNIT;
        if (near) with++;
    }
    printf("Windows: %d lines like one, %d with a corner at an end.\n", windows, with);
}

void I_TestFrame(void)
{
    const corner_t *list;
    if (gamestate != GS_LEVEL || leveltime < 2) return;
    if (stage) player()->momx = player()->momy = 0;
    switch (stage)
    {
      case 0:
        /* A soldier in a hunt reconsiders its cover every 3 s, counted from the map's start. */
        if (leveltime < 4*TICRATE) break;
        if (!P_DerpAI() || !P_NavReady()) fail("DERP's monsters or their navigation are not on");
        players[0].cheats |= CF_GODMODE;
        printf("Corners: %d wall sides on this map.\n", P_Corners(&list));
        if (!P_Corners(&list)) fail("no corners found");
        check_heights();
        count_windows();
        if (!find_scene()) fail("no soldier in his sight with a corner to get behind");
        printf("Scene: soldier at (%d,%d), shelter (%d,%d), lean (%d,%d), he at (%d,%d); %d spots tried.\n",
               soldier->x >> FRACBITS, soldier->y >> FRACBITS, coverx >> FRACBITS, covery >> FRACBITS,
               leanx >> FRACBITS, leany >> FRACBITS, player()->x >> FRACBITS, player()->y >> FRACBITS, tried);
        player()->angle = R_PointToAngle2(player()->x, player()->y, soldier->x, soldier->y);
        soldier->target = player();
        soldier->mind = MIND_HUNT;
        soldier->nerve = 30;
        soldier->knownx = player()->x; soldier->knowny = player()->y; soldier->knowntic = leveltime;
        P_SetMobjState(soldier, soldier->info->seestate);
        stage = 1; stagetic = leveltime;
        break;
      case 1:
        /* He keeps firing, at nobody: under fire. */
        if (!(leveltime % 20)) { P_DerpNoiseAlert(player(), player()); round_past(soldier); }
        if (soldier->mind == MIND_COVER)
        {
            if (!covers++) { lastx = soldier->coverx; lasty = soldier->covery; }
            if (P_AproxDistance(soldier->coverx - lastx, soldier->covery - lasty) > 32*FRACUNIT)
            {
                moves++;
                lastx = soldier->coverx; lasty = soldier->covery;
            }
        }
        if (P_DerpSuppression(soldier) > peak) peak = P_DerpSuppression(soldier);
        if (soldier->firetic != lastfire) { lastfire = soldier->firetic; shots++; }
        if (leveltime - stagetic < 25*TICRATE) break;
        printf("Most suppressed: %d.\n", peak);
        printf("In 25 s: %d tics in cover, %d changes of cover, %d shots.\n", covers, moves, shots);
        if (!covers) fail("under fire, the soldier did not take cover");
        if (shots < 2) fail("the soldier did not fire from its cover");
        if (moves > 1) fail("the soldier kept changing its cover");
        check_shared();
        start_flank(); stage = 2; stagetic = leveltime;
        break;
      case 2:
        /* Flanked: no second cover under his gun. Back to the fight and its rules. */
        if (soldier->mind == MIND_COVER && P_AproxDistance(soldier->coverx - lastx, soldier->covery - lasty) > 32*FRACUNIT)
            fail("flanked in his sight, the soldier ran for another cover");
        if (soldier->mind != MIND_COVER)
        {
            printf("Flanked: after %d tics, back to the fight (mind %d).\n", leveltime - stagetic, soldier->mind);
            start_gone(); stage = 3; stagetic = leveltime;
            break;
        }
        if (leveltime - stagetic > 6*TICRATE) fail("he stood in view of its shelter and the soldier stayed");
        break;
      case 3:
        if (soldier->mind == MIND_COVER && soldier->tactic == STEP_COVER_WATCHING && !watched) watched = leveltime - stagetic;
        if (soldier->mind != MIND_COVER && watched)
        {
            printf("Gone: it watched where he went from %d tics on, gave up after %d.\n", watched, leveltime - stagetic);
            puts("DERP corners smoke test passed.");
            I_Quit();
        }
        if (leveltime - stagetic > 20*TICRATE)
        {
            printf("  mind %d tactic %d known %d ago\n", soldier->mind, soldier->tactic, leveltime - soldier->knowntic);
            fail("he gone, the soldier did not watch and then leave");
        }
        break;
    }
}

int main(int argc, char **argv)
{
    if (argc != 2 && argc != 4) return 1;
    if (!SDL_Init(SDL_INIT_VIDEO)) return 1;
    char *args[16] = {"derp-corners-smoke", "-iwad", argv[1], "-warp", argc == 4 ? argv[2] : "1", argc == 4 ? argv[3] : "1", "-skill", "3",
                      "-nomonsters", "-nomusic", "-nosound", "-derpai"};
    myargc = 12; myargv = args;
    D_DoomMain();
    return 0;
}
