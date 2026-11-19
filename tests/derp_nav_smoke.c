/* DERP's navigation (p_nav.h) on every map of a WAD: built the same each
   time, the monsters' starts standable, every sampled step one the game's
   own P_TryMove takes, no hole where the game stands a body and the grid
   does not, a moved floor rebuilt where it moved, the monsters'
   ways to the player found. Reports each map's numbers and build time.
   doom-derp-nav-smoke <iwad> [all] */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "doomdef.h"
#include "doomstat.h"
#include "d_main.h"
#include "g_game.h"
#include "i_system.h"
#include "m_argv.h"
#include "r_main.h"
#include "r_state.h"
#include "p_local.h"
#include "p_derp.h"
#include "p_nav.h"
#include "w_wad.h"
#include "am_map.h"
void AM_Start(void);
#include "v_video.h"
#include "z_zone.h"
extern fixed_t tmdropoffz;
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

static int maps[64][2], nummaps, current;
/* "all": every map and every monster's way (some 15 minutes for Freedoom 1
   in a Debug build); else every fourth map and 40 monsters' ways a map. */
static boolean everything;
static int totalsteps, failedsteps, totalstarts, standstarts, totalways, foundways, slowest;

static char name[9];
static const char *map_name(int episode, int map)
{
    if (gamemode == commercial) snprintf(name, sizeof name, "MAP%02d", map);
    else snprintf(name, sizeof name, "E%dM%d", episode, map);
    return name;
}
static const char *lumpname(void) { return map_name(gameepisode, gamemap); }
static void fail(const char *what) { I_Error("DERP nav smoke test (%s): %s", lumpname(), what); }

static boolean walker(mobj_t *mo)
{
    return (mo->flags & MF_COUNTKILL) && !(mo->flags & (MF_FLOAT|MF_NOGRAVITY)) && mo->health > 0;
}

/* Every monster that walks: its start within two cells of a standable one,
   and its way to the player. */
static void check_monsters(void)
{
    mobj_t *player = players[0].mo;
    static int path[4096];
    int starts = 0, stand = 0, ways = 0, found = 0;
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next)
    {
        mobj_t *mo = (mobj_t *)th;
        int nav;
        if (th->function.acp1 != (actionf_p1)P_MobjThinker || !walker(mo)) continue;
        nav = P_NavClassOf(mo);
        if (nav < 0) fail("a walker has no layer");
        ++starts;
        if (P_NavCellAt(nav, mo->x, mo->y) < 0) continue;
        ++stand;
        if (!everything && ways >= 40) continue;
        ++ways;
        if (P_NavPath(nav, mo->x, mo->y, player->x, player->y, path, 4096, 1 << 22) > 0) ++found;
    }
    totalstarts += starts; standstarts += stand; totalways += ways; foundways += found;
    printf("  monsters: %d of %d starts standable, %d of %d reach the player\n", stand, starts, found, ways);
}

/* Steps the grid allows, taken by the game itself: a probe of the layer's
   size at one cell's point moved by P_TryMove to the next. The other things
   are gone first and the lines' specials off, so a step is only a step. */
static int steps_of(mobjtype_t type, int samples)
{
    int nav = P_NavClassFor(mobjinfo[type].radius/FRACUNIT, mobjinfo[type].height/FRACUNIT);
    int cells = P_NavCells(), tried = 0, failed = 0, cell = 0, k;
    static const int around[8][2] = {{1,0},{-1,0},{0,1},{0,-1},{1,1},{1,-1},{-1,1},{-1,-1}};
    if (nav < 0) fail("no layer for a probe");
    /* Spread over the map: a stride prime to the grid's size. */
    for (k = 0; tried < samples && k < 8*cells; ++k, cell = (cell + 7919) % cells)
    {
        int dir = k & 7, next;
        fixed_t ax, ay, af, bx, by, bf;
        mobj_t *probe;
        if (!P_NavStandable(nav, cell)) continue;
        P_NavPoint(nav, cell, &ax, &ay, &af);
        next = P_NavCellAt(nav, ax + around[dir][0]*NAVCELL*FRACUNIT, ay + around[dir][1]*NAVCELL*FRACUNIT);
        if (next < 0 || !P_NavStep(nav, cell, next)) continue;
        /* Through a door shut now: the monster opens it, not a step to judge. */
        if (P_NavDoor(nav, cell) || P_NavDoor(nav, next)) continue;
        P_NavPoint(nav, next, &bx, &by, &bf);
        probe = P_SpawnMobj(ax, ay, ONFLOORZ, type);
        probe->z = probe->floorz = af;
        /* Something solid the grid does not count (the player, a barrel), at
           either end: not a step to judge. */
        if (!P_CheckPosition(probe, ax, ay)) { P_RemoveMobj(probe); continue; }
        {
            boolean crowded = false;
            for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next)
            {
                mobj_t *mo = (mobj_t *)th;
                if (th->function.acp1 == (actionf_p1)P_MobjThinker && mo != probe && (mo->flags & MF_SOLID)
                    && abs(mo->x - bx) < mo->radius + probe->radius && abs(mo->y - by) < mo->radius + probe->radius)
                    crowded = true;
            }
            if (crowded) { P_RemoveMobj(probe); continue; }
        }
        ++tried;
        if (!P_TryMove(probe, bx, by) && ++failed <= 5)
        {
            const char *why = "a line";
            for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next)
            {
                mobj_t *mo = (mobj_t *)th;
                if (th->function.acp1 != (actionf_p1)P_MobjThinker || mo == probe || !(mo->flags & MF_SOLID)) continue;
                if (abs(mo->x - bx) < mo->radius + probe->radius && abs(mo->y - by) < mo->radius + probe->radius)
                    why = mo->player ? "the player" : (mo->flags & MF_SHOOTABLE) ? "a shootable thing" : "a solid thing";
            }
            printf("  refused: %s from (%d,%d) floor %d to (%d,%d) floor %d (%s; tmfloor %d tmceil %d drop %d)\n", type == MT_SERGEANT ? "pinky" : "zombie",
                   ax >> FRACBITS, ay >> FRACBITS, af >> FRACBITS, bx >> FRACBITS, by >> FRACBITS, bf >> FRACBITS, why,
                   tmfloorz >> FRACBITS, tmceilingz >> FRACBITS, tmdropoffz >> FRACBITS);
        }
        P_RemoveMobj(probe);
    }
    totalsteps += tried; failedsteps += failed;
    return failed;
}

/* A floor moved: the grid follows it there as a fresh build would, and
   back to what it was. */
static void check_moved_floor(void)
{
    int nav = P_NavClassFor(20, 56), cell = -1, saved = leveltime;
    unsigned before = P_NavDigest(), followed;
    fixed_t x, y, floor;
    sector_t *sec;
    for (int c = 0; c < P_NavCells() && cell < 0; c += 97)
        if (P_NavStandable(nav, c))
        {
            P_NavPoint(nav, c, &x, &y, &floor);
            sec = R_PointInSubsector(x, y)->sector;
            /* A sector of its own floor, the cell's floor. */
            if (sec->floorheight == floor && sec->ceilingheight - sec->floorheight >= 128*FRACUNIT) cell = c;
        }
    if (cell < 0) { printf("  moved floor: no cell to try, skipped\n"); return; }
    sec->floorheight += 8*FRACUNIT;
    P_ChangeSector(sec, false);
    leveltime = 8; P_NavThink();
    followed = P_NavDigest();
    P_NavRebuild();
    P_NavClassFor(20, 56);
    if (P_NavDigest() != followed) fail("a raised floor was not rebuilt as a fresh build would be");
    if (followed == before) fail("a raised floor changed nothing in the grid");
    sec->floorheight -= 8*FRACUNIT;
    P_ChangeSector(sec, false);
    leveltime = 16; P_NavThink();
    leveltime = saved;
    if (P_NavDigest() != before) fail("lowered back, the grid is not as it was");
    /* As a load moves it: the height set with no mover to tell the grid,
       then P_NavSync (G_DoLoadGame). */
    sec->floorheight += 8*FRACUNIT;
    P_NavSync();
    if (P_NavDigest() != followed) fail("a floor moved as a load moves it was not synced as a fresh build");
    sec->floorheight -= 8*FRACUNIT;
    P_NavSync();
    if (P_NavDigest() != before) fail("synced back, the grid is not as it was");
    printf("  moved floor: rebuilt up and back, and synced as after a load\n");
}


/* Holes in the grid: a cell it calls no place to stand where the game
   itself stands a zombie (it fits there: no line, no thing, room overhead,
   no drop it would hang over), with standable cells on both sides at its
   floor. E1M1's zigzag bridge had a strip of them (called void by a
   two-sided seg), cutting the zigzag room off from the computer room. Run
   with the monsters gone, as the steps are. */
static int totalholes;
static int check_holes(void)
{
    static const int across[4][2] = {{1,0},{0,1},{1,1},{1,-1}};
    int nav = P_NavClassFor(20, 56), cells = P_NavCells(), holes = 0, shown = 0, w;
    mobj_t *probe = NULL;
    {   /* the grid's width, from two cells a row apart */
        fixed_t x, y;
        int c = -1;
        for (int k = 0; k < cells && c < 0; ++k) if (P_NavStandable(nav, k)) c = k;
        P_NavPoint(nav, c, &x, &y, NULL);
        w = P_NavCellAt(nav, x, y + NAVCELL*FRACUNIT) - c;
    }
    if (w <= 1) return 0;
    for (int c = w + 1; c < cells - w - 1; ++c)
    {
        fixed_t ax, ay, af, bx, by, bf, x, y;
        int k;
        if (P_NavStandable(nav, c)) continue;
        for (k = 0; k < 4; ++k)
        {
            int a = c - across[k][1]*w - across[k][0], b = c + across[k][1]*w + across[k][0];
            if (!P_NavStandable(nav, a) || !P_NavStandable(nav, b) || P_NavDoor(nav, a) || P_NavDoor(nav, b)) continue;
            P_NavPoint(nav, a, &ax, &ay, &af);
            P_NavPoint(nav, b, &bx, &by, &bf);
            if (abs(af - bf) <= 24*FRACUNIT) break;
        }
        if (k == 4) continue;
        /* Where the grid itself tries in the cell (its middle, and 4 and 8
           units off): the game standing a zombie at one of them. A solid
           cell's point is its middle. */
        {
            static const signed char tries[][2] = {{0,0},{4,0},{-4,0},{0,4},{0,-4},{8,0},{-8,0},{0,8},{0,-8},
                {4,4},{-4,4},{4,-4},{-4,-4},{8,4},{8,-4},{-8,4},{-8,-4},{4,8},{-4,8},{4,-8},{-4,-8},{8,8},{-8,8},{8,-8},{-8,-8}};
            fixed_t cx, cy;
            int t, fits = 0;
            P_NavPoint(nav, c, &cx, &cy, NULL);
            for (t = 0; t < (int)(sizeof tries/sizeof tries[0]) && !fits; ++t)
            {
                x = cx + tries[t][0]*FRACUNIT; y = cy + tries[t][1]*FRACUNIT;
                if (!probe) probe = P_SpawnMobj(x, y, ONFLOORZ, MT_POSSESSED);
                if (P_CheckPosition(probe, x, y) && tmceilingz - tmfloorz >= probe->height
                    && tmfloorz - tmdropoffz <= 24*FRACUNIT && abs(tmfloorz - af) <= 24*FRACUNIT)
                    fits = 1;
            }
            if (!fits) continue;
        }
        ++holes;
        if (shown++ < 3)
            printf("  hole: (%d,%d) floor %d, sector %d: the game stands a zombie there, the grid does not\n",
                   x >> FRACBITS, y >> FRACBITS, tmfloorz >> FRACBITS, (int)(R_PointInSubsector(x, y)->sector - sectors));
    }
    if (probe) P_RemoveMobj(probe);
    printf("  holes: %d\n", holes);
    totalholes += holes;
    return holes;
}

static unsigned vertex_sum(void)
{
    unsigned h = 0;
    for (int i = 0; i < numvertexes; ++i) h = h*31 + (unsigned)vertexes[i].x*7 + (unsigned)vertexes[i].y;
    for (int i = 0; i < numsegs; ++i) h = h*31 + (unsigned)(segs[i].v1 - vertexes);
    return h;
}
static unsigned vsum;
#define WATCH(step) do { if (vertex_sum() != vsum) fail("the map's vertexes changed in " step); } while (0)

static void check_map(void)
{
    vsum = vertex_sum();
    unsigned first, again;
    int failed;
    short *specials;
    const navlink_t *links;
    printf("%s: %d ms, %d rooms, %d links\n", lumpname(), navbuildms, P_NavRooms(), P_NavLinks(&links));
    if (!P_NavReady()) fail("no navigation");
    if (navbuildms > slowest) slowest = navbuildms;
    /* What moved since the build, followed as the game follows it, is what a
       fresh build makes now; and a fresh build makes the same twice. */
    P_NavSettle();
    first = P_NavDigest();
    unsigned parts[9];
    for (int k = 0; k < 9; ++k) parts[k] = P_NavDigestOf(k - 1);
    int cells = P_NavCells(), nav0 = P_NavClassFor(20, 56);
    fixed_t *before = malloc(sizeof *before*cells*3);
    char *doors = malloc(cells);
    for (int c = 0; c < cells; ++c) doors[c] = P_NavDoor(nav0, c);
    for (int c = 0; c < cells; ++c)
    {
        if (P_NavStandable(nav0, c)) P_NavPoint(nav0, c, &before[c*3], &before[c*3+1], &before[c*3+2]);
        else before[c*3] = before[c*3+1] = before[c*3+2] = -1;
    }
    first = P_NavDigest();
    P_NavRebuild();
    nav0 = P_NavClassFor(20, 56);
    again = P_NavDigest();
    if (first != again)
    {
        int shown = 0;
        for (int c = 0; c < cells && shown < 8; ++c)
        {
            fixed_t x = -1, y = -1, f = -1;
            if (P_NavStandable(nav0, c)) P_NavPoint(nav0, c, &x, &y, &f);
            if (x != before[c*3] || y != before[c*3+1] || f != before[c*3+2])
            {
                fixed_t px = x != -1 ? x : before[c*3], py = y != -1 ? y : before[c*3+1];
                sector_t *sec = px != -1 ? R_PointInSubsector(px, py)->sector : NULL;
                printf("  differs: cell %d was (%d,%d) floor %d, now (%d,%d) floor %d; sector %d special %d floor %d ceil %d\n", c,
                       before[c*3] >> FRACBITS, before[c*3+1] >> FRACBITS, before[c*3+2] >> FRACBITS,
                       x >> FRACBITS, y >> FRACBITS, f >> FRACBITS, sec ? (int)(sec - sectors) : -1,
                       sec ? sec->special : 0, sec ? sec->floorheight >> FRACBITS : 0, sec ? sec->ceilingheight >> FRACBITS : 0);
                ++shown;
            }
        }
        for (int c = 0; c < cells && shown < 8; ++c)
            if (doors[c] != P_NavDoor(nav0, c))
            {
                fixed_t x, y, f;
                P_NavPoint(nav0, c, &x, &y, &f);
                sector_t *sec = R_PointInSubsector(x, y)->sector;
                printf("  door mark differs: cell %d at (%d,%d) was %d now %d; sector %d floor %d ceil %d\n", c, x >> FRACBITS, y >> FRACBITS,
                       doors[c], P_NavDoor(nav0, c), (int)(sec - sectors), sec->floorheight >> FRACBITS, sec->ceilingheight >> FRACBITS);
                ++shown;
            }
        if (!shown) printf("  differs elsewhere (another layer, the rooms or the flags)\n");
        for (int k = 0; k < 9; ++k) if (parts[k] != P_NavDigestOf(k - 1)) printf("  part %d differs\n", k - 1);
        fail("rebuilt where sectors moved, not as a fresh build");
    }
    free(before);
    P_NavRebuild();
    P_NavClassFor(20, 56);	/* the layer the checks above added, added again */
    if (P_NavDigest() != again) fail("built twice, built differently");
    WATCH("the rebuilds");
    check_monsters();
    WATCH("the monsters' ways");
    check_moved_floor();
    WATCH("the moved floor");
    /* Without the monsters and the lines' specials, steps alone. */
    for (thinker_t *th = thinkercap.next; th != &thinkercap; )
    {
        thinker_t *next = th->next;
        if (th->function.acp1 == (actionf_p1)P_MobjThinker && (((mobj_t *)th)->flags & MF_COUNTKILL)) P_RemoveMobj((mobj_t *)th);
        th = next;
    }
    specials = malloc(sizeof *specials*numlines);
    for (int i = 0; i < numlines; ++i) { specials[i] = lines[i].special; lines[i].special = 0; }
    check_holes();
    WATCH("the holes");
    failed = steps_of(MT_POSSESSED, 300);
    WATCH("the zombie's steps");
    failed += steps_of(MT_SERGEANT, 300);
    WATCH("the pinky's steps");
    for (int i = 0; i < numlines; ++i) lines[i].special = specials[i];
    free(specials);
    if (failed) printf("  %d steps refused\n", failed);
}

/* NAVSHOT=<file.ppm> (and NAVSHOT_MAP=<index>, default 5): the automap of
   that map with -derpnav's overlay, every line shown and every monster
   after the player, written out for a look. */
static int shotstage;
static void take_shot(void)
{
    const char *path = getenv("NAVSHOT");
    const byte *palette = W_CacheLumpName("PLAYPAL", PU_CACHE);
    FILE *file = fopen(path, "wb");
    if (!file) fail("could not write the shot");
    fprintf(file, "P6\n%d %d\n255\n", SCREENWIDTH, SCREENHEIGHT);
    for (int i = 0; i < SCREENWIDTH*SCREENHEIGHT; ++i) fwrite(palette + 3*screens[0][i], 1, 3, file);
    fclose(file);
    printf("  shot: %s\n", path);
}

void I_TestFrame(void)
{
    if (gamestate != GS_LEVEL || gameaction != ga_nothing || leveltime < 2) return;
    if (!nummaps)
    {
        /* Every map the WAD holds, in order; the warp brought the first. */
        for (int e = 1; e <= (gamemode == commercial ? 1 : 4); ++e)
            for (int m = 1; m <= (gamemode == commercial ? 32 : 9); ++m)
                if (W_CheckNumForName((char *)map_name(e, m)) >= 0)
                {
                    static int seen;
                    if (!everything && seen++ % 4) continue;
                    maps[nummaps][0] = e; maps[nummaps][1] = m; ++nummaps;
                }
        current = 0;
    }
    if (gameepisode != maps[current][0] || gamemap != maps[current][1]) return;
    if (getenv("NAVSHOT") && current == (getenv("NAVSHOT_MAP") ? atoi(getenv("NAVSHOT_MAP")) : 5))
    {
        if (shotstage == 0)
        {
            for (int i = 0; i < numlines; ++i) lines[i].flags |= ML_MAPPED;
            for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next)
                if (th->function.acp1 == (actionf_p1)P_MobjThinker && (((mobj_t *)th)->flags & MF_COUNTKILL))
                    ((mobj_t *)th)->target = players[0].mo;
            AM_Start();
            shotstage = 1;
            return;
        }
        /* A few frames for the automap to take the screen. */
        if (shotstage < 70) { ++shotstage; return; }
        if (shotstage == 70) { take_shot(); AM_Stop(); shotstage = 71; }
    }
    check_map();
    if (++current == nummaps)
    {
        printf("Navigation: %d maps, slowest build %d ms; %d of %d starts standable; %d of %d ways found; %d of %d steps refused.\n",
               nummaps, slowest, standstarts, totalstarts, foundways, totalways, failedsteps, totalsteps);
        if (failedsteps) fail("the game refused steps the grid allows");
        printf("Holes: %d cells the game stands a zombie on and the grid does not.\n", totalholes);
        if (totalholes) fail("the grid has holes where the game stands a body");
        if (standstarts*100 < totalstarts*95) fail("too few monster starts standable");
        puts("DERP nav smoke test passed.");
        I_Quit();
    }
    G_DeferedInitNew(gameskill, maps[current][0], maps[current][1]);
}

int main(int argc, char **argv)
{
    if (argc < 2 || argc > 3) return 1;
    everything = argc == 3 && !strcmp(argv[2], "all");
    if (!SDL_Init(SDL_INIT_VIDEO)) return 1;
    char *args[16] = {"derp-nav-smoke", "-iwad", argv[1], "-warp", "1", "1", "-skill", "4",
                      "-nomusic", "-nosound", "-derpai", "-derpnav"};
    myargc = 12; myargv = args;
    D_DoomMain();
    return 0;
}
