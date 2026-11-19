/* A rough player for checking DERP's monsters on a real map: it heads for
   the nearest monster it can walk to, stops to fight what it sees, shoots
   (a pistol, a shotgun up close), opens what it bumps into, in god mode.
   With -derplog it leaves the monsters' own account of the game to read.
   Not a test: nothing passes or fails. devtools/ai_graph/botruns.py plays many.
   PROBE_STYLE: hunt (the default), sneak (walks, below the pace they hear, pistol
   only, a pause after each kill) or rush (runs, shotgun, stops for nothing far).
   PROBE_SEED: which of the nearest monsters it goes for, so runs differ.
   Its health is put back each tic and the damage counted: the monsters' score.
   PROBE_INVARIANTS=1: what must never happen is counted (below), and the run fails
   (exit 2) if any of it did: ctest's derp-invariants.
   doom-derp-ai-probe <iwad> <log file> [episode map] [minutes] */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "doomdef.h"
#include "doomstat.h"
#include "d_main.h"
#include "i_system.h"
#include "m_argv.h"
#include "m_random.h"
#include "r_main.h"
#include "r_state.h"
#include "p_local.h"
#include "p_derp.h"
#include "p_derplog.h"
#include "p_mind.h"
#include "p_nav.h"
#include "p_derptiming.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

extern double I_TestSpeed;
static int minutes = 6, lasttic = -1, shottic, stucktic, picktic;
enum { HUNT, SNEAK, RUSH };
static int style = HUNT, killed, resttic, taken;
static unsigned seed = 1;
static unsigned roll(void) { seed = seed*1103515245u + 12345u; return seed >> 16; }
static fixed_t stuckx, stucky;
static mobj_t *goal;
#define SKIPS 256
static int skipped[SKIPS], numskipped;

static mobj_t *player(void) { return players[0].mo; }

static boolean live_monster(mobj_t *mo)
{
    return !mo->player && (mo->flags & MF_COUNTKILL) && mo->health > 0;
}

static boolean is_skipped(mobj_t *mo)
{
    for (int i = 0; i < numskipped; i++) if (skipped[i] == mo->serial) return true;
    return false;
}

/* The nearest monster it sees, within range. */
static mobj_t *nearest_seen(int range)
{
    mobj_t *best = NULL;
    fixed_t bestd = range*FRACUNIT;
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next)
    {
        mobj_t *mo = (mobj_t *)th;
        fixed_t d;
        if (th->function.acp1 != (actionf_p1)P_MobjThinker || !live_monster(mo)) continue;
        d = P_AproxDistance(mo->x - player()->x, mo->y - player()->y);
        if (d < bestd && P_CheckSight(player(), mo)) { bestd = d; best = mo; }
    }
    return best;
}

/* One of the nearest monsters with a way there, not given up on, by the seed; with
   none, the nearest at all (straight at it: the doors on the way get opened). */
#define CHOICES 3
static mobj_t *pick_goal(void)
{
    static int path[2];
    int nav = P_NavClassFor(20, 56), n = 0;
    mobj_t *best[CHOICES] = {0}, *any = NULL;
    fixed_t bestd[CHOICES], anyd = MAXINT;
    for (int k = 0; k < CHOICES; k++) bestd[k] = MAXINT;
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next)
    {
        mobj_t *mo = (mobj_t *)th;
        fixed_t d;
        int k;
        if (th->function.acp1 != (actionf_p1)P_MobjThinker || !live_monster(mo) || is_skipped(mo)) continue;
        d = P_AproxDistance(mo->x - player()->x, mo->y - player()->y);
        if (d < anyd) { anyd = d; any = mo; }
        if (d >= bestd[CHOICES - 1]) continue;
        if (P_NavPath(nav, player()->x, player()->y, mo->x, mo->y, path, 2, 30000) <= 0) continue;
        for (k = CHOICES - 1; k > 0 && bestd[k - 1] > d; k--) { bestd[k] = bestd[k - 1]; best[k] = best[k - 1]; }
        bestd[k] = d; best[k] = mo;
        if (n < CHOICES) n++;
    }
    return n ? best[roll() % n] : any;
}

static void shoot(mobj_t *at)
{
    player_t *p = &players[0];
    mobj_t *mo = player();
    fixed_t dist = P_AproxDistance(at->x - mo->x, at->y - mo->y), slope;
    boolean shotgun = style == RUSH || (style == HUNT && dist < 320*FRACUNIT);
    mo->angle = R_PointToAngle2(mo->x, mo->y, at->x, at->y);
    if (leveltime - shottic < (shotgun ? 37 : 14)) return;
    shottic = leveltime;
    p->readyweapon = shotgun ? wp_shotgun : wp_pistol;
    P_NoiseAlert(mo, mo);
    slope = P_AimLineAttack(mo, mo->angle, MISSILERANGE);
    for (int i = 0; i < (shotgun ? 7 : 1); i++)
    {
        /* A hand's wobble: most shots hit, not all. */
        angle_t an = mo->angle + ((P_Random() - P_Random()) << (shotgun ? 18 : 17));
        P_LineAttack(mo, an, MISSILERANGE, slope, 5*(P_Random()%3 + 1));
    }
}

static void walk_to(fixed_t gx, fixed_t gy)
{
    static int path[16];
    int nav = P_NavClassFor(20, 56), n;
    fixed_t wx = gx, wy = gy;
    mobj_t *mo = player();
    angle_t an;
    n = P_NavPath(nav, mo->x, mo->y, gx, gy, path, 16, 30000);
    if (n > 3) P_NavPoint(nav, path[3], &wx, &wy, NULL);
    else if (n > 1) P_NavPoint(nav, path[n - 1], &wx, &wy, NULL);
    an = R_PointToAngle2(mo->x, mo->y, wx, wy);
    mo->angle = an;
    /* Running: about the player's top speed (Quake's pace under DERP); sneaking under
       the pace they hear (P_DerpFootsteps: 5/8). */
    fixed_t pace = style == SNEAK ? P_DerpRunSpeed()*4/10 : style == RUSH ? P_DerpRunSpeed() : P_DerpRunSpeed()*9/10;
    mo->momx = FixedMul(pace, finecosine[an >> ANGLETOFINESHIFT]);
    mo->momy = FixedMul(pace, finesine[an >> ANGLETOFINESHIFT]);
}

/* Wiggling: every 4 tics each monster's move since the last sample; a
   move back against the one before (more than 120 degrees round) is a
   reversal. Reversals a minute of moving, overall and by type. */
#define TRACK 4096
static fixed_t lastx[TRACK], lasty[TRACK], lastdx[TRACK], lastdy[TRACK];
static int moves, reversals, typemoves[NUMMOBJTYPES], typereversals[NUMMOBJTYPES], mindmoves[16], mindrev[16];
static int serialrev[TRACK];

static void watch_wiggles(void)
{
    if (leveltime & 3) return;
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next)
    {
        mobj_t *mo = (mobj_t *)th;
        int i;
        fixed_t dx, dy;
        if (th->function.acp1 != (actionf_p1)P_MobjThinker || !live_monster(mo)) continue;
        i = mo->serial % TRACK;
        dx = mo->x - lastx[i]; dy = mo->y - lasty[i];
        lastx[i] = mo->x; lasty[i] = mo->y;
        if (P_AproxDistance(dx, dy) < 2*FRACUNIT || P_AproxDistance(dx, dy) > 64*FRACUNIT) continue;
        moves++; typemoves[mo->type]++; mindmoves[mo->mind & 15]++;
        if (lastdx[i] || lastdy[i])
        {
            double a = (double)dx*lastdx[i] + (double)dy*lastdy[i];
            double la = (double)P_AproxDistance(dx, dy)*P_AproxDistance(lastdx[i], lastdy[i]);
            if (a < -0.5*la)
            {
                reversals++; typereversals[mo->type]++; mindrev[mo->mind & 15]++; serialrev[i]++;
                /* WIGGLETRACE=1: each reversal with what the monster was at. */
                if (getenv("WIGGLETRACE"))
                    printf("wt %d #%d type %d mind %d tactic %d at %d,%d moved %d,%d after %d,%d dir %d goal %d,%d way %d,%d "
                           "fire %d,%d player %d,%d sees %d\n",
                           leveltime, mo->serial, mo->type, mo->mind, mo->tactic, mo->x >> FRACBITS, mo->y >> FRACBITS,
                           dx >> FRACBITS, dy >> FRACBITS, lastdx[i] >> FRACBITS, lastdy[i] >> FRACBITS, mo->movedir,
                           mo->goalx >> FRACBITS, mo->goaly >> FRACBITS, mo->wayx >> FRACBITS, mo->wayy >> FRACBITS,
                           mo->firex >> FRACBITS, mo->firey >> FRACBITS, player()->x >> FRACBITS, player()->y >> FRACBITS,
                           (int)P_CheckSight(mo, player()));
            }
        }
        lastdx[i] = dx; lastdy[i] = dy;
    }
}

static void wiggle_report(void)
{
    /* A move sample is 4 tics: 525 samples are a minute of moving. */
    printf("Wiggles: %d reversals in %d moving samples (%.1f a minute of moving).\n",
           reversals, moves, moves ? reversals*525.0/moves : 0.0);
    for (int k = 0; k < 3; k++)
    {
        int best = 0;
        for (int i = 1; i < TRACK; i++) if (serialrev[i] > serialrev[best]) best = i;
        if (serialrev[best]) printf("  #%d: %d reversals\n", best, serialrev[best]);
        serialrev[best] = 0;
    }
    for (int m = 0; m < 16; m++)
        if (mindmoves[m] > 50)
            printf("  mind %d: %.1f a minute (%d samples)\n", m,mindrev[m]*525.0/mindmoves[m], mindmoves[m]);
    for (int t = 0; t < NUMMOBJTYPES; t++)
        if (typemoves[t] > 50)
            printf("  type %d: %.1f a minute (%d samples)\n", t, typereversals[t]*525.0/typemoves[t], typemoves[t]);
}

/* What must never happen (PROBE_INVARIANTS), each said once per monster in the log:
   stuck     meaning to go somewhere (investigate, hunt, search, return, regroup, heal,
             flank), out of his sight, not lurking, hanging, kneeling or holding the way
             out behind him, and no 24 units of progress in STUCK_SECS. (A regroup gives
             up after 30 s, so the limit sits above that.)
   flipflop  more than FLIPS changes of mind within a second.
   hurting   a monster that floors hurt (not an imp) standing on one outside its home
             sector for HURT_TICS. */
#define STUCK_SECS 32
#define FLIPS 6
#define HURT_TICS 70
static int invariants, inv_stuck, inv_flip, inv_hurt;
static fixed_t invx[TRACK], invy[TRACK];
static int invtic[TRACK], invmind[TRACK], invflips[TRACK], invfliptic[TRACK], invhurt[TRACK], invsaid[TRACK];

static boolean going(int mind)
{
    return mind == MIND_INVESTIGATE || mind == MIND_HUNT || mind == MIND_SEARCH || mind == MIND_RETURN
        || mind == MIND_REGROUP || mind == MIND_HEAL || mind == MIND_FLANK;
}

static boolean hurts(const sector_t *s) { return s->special == 4 || s->special == 5 || s->special == 7 || s->special == 16; }

static void watch_invariants(void)
{
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next)
    {
        mobj_t *mo = (mobj_t *)th;
        int k;
        if (th->function.acp1 != (actionf_p1)P_MobjThinker || !live_monster(mo)) continue;
        k = mo->serial % TRACK;
        if (P_AproxDistance(mo->x - invx[k], mo->y - invy[k]) >= 24*FRACUNIT || !going(mo->mind) || mo->lurk || mo->hang
            || mo->kneeltic || leveltime < mo->holdtic || P_CheckSight(mo, player()))
        {
            invx[k] = mo->x; invy[k] = mo->y; invtic[k] = leveltime;
        }
        else if (leveltime - invtic[k] > STUCK_SECS*TICRATE && !(invsaid[k] & 1))
        {
            invsaid[k] |= 1; inv_stuck++;
            P_DerpLog(mo, "INVARIANT stuck: %d s without 24 units of progress, out of his sight", STUCK_SECS);
        }
        if (leveltime - invfliptic[k] >= TICRATE) { invfliptic[k] = leveltime; invflips[k] = 0; }
        if (mo->mind != invmind[k])
        {
            invmind[k] = mo->mind;
            if (++invflips[k] > FLIPS && !(invsaid[k] & 2))
            {
                invsaid[k] |= 2; inv_flip++;
                P_DerpLog(mo, "INVARIANT flip-flop: %d changes of mind within a second", invflips[k]);
            }
        }
        if (mo->type != MT_TROOP && mo->z <= mo->floorz && hurts(mo->subsector->sector)
            && mo->subsector->sector - sectors != mo->homesector)
        {
            if (++invhurt[k] > HURT_TICS && !(invsaid[k] & 4))
            {
                invsaid[k] |= 4; inv_hurt++;
                P_DerpLog(mo, "INVARIANT hurting floor: standing in it for %d tics", invhurt[k]);
            }
        }
        else invhurt[k] = 0;
    }
}

/* What routes cost, a tic at a time: cells searched (the same on any machine) and
   the navigation time it took here. The worst tics are a hitch in the game. */
#define WORST 8
static unsigned cellsbefore, cellstotal;
static double navtotal;
static struct { int tic; unsigned cells; double ms; } worst[WORST];
static int ticsover[3];   /* tics past 20k, 50k and 100k cells */
static uint64_t probe_clock(void) { return SDL_GetTicksNS(); }

static void watch_routes(void)
{
    uint64_t times[DERP_TIMING_COUNT];
    unsigned cells = navsearched - cellsbefore;
    double ms;
    P_DerpTimingTake(times);
    ms = times[DERP_TIMING_NAV]/1e6;
    cellsbefore = navsearched;
    cellstotal += cells;
    navtotal += ms;
    ticsover[0] += cells > 20000;
    ticsover[1] += cells > 50000;
    ticsover[2] += cells > 100000;
    for (int i = 0; i < WORST; i++)
        if (cells > worst[i].cells)
        {
            memmove(&worst[i + 1], &worst[i], (WORST - 1 - i)*sizeof worst[0]);
            worst[i].tic = leveltime; worst[i].cells = cells; worst[i].ms = ms;
            break;
        }
}

static void route_report(void)
{
    printf("Routes: %u cells searched, %.1f ms; tics past 20k/50k/100k cells: %d/%d/%d\n",
           cellstotal, navtotal, ticsover[0], ticsover[1], ticsover[2]);
    for (int i = 0; i < WORST && worst[i].cells; i++)
        printf("  tic %5d: %7u cells, %6.2f ms\n", worst[i].tic, worst[i].cells, worst[i].ms);
}

/* The end of the run: the summary, and in invariants mode the verdict as the exit code. */
static void summary(void);

static void finish(void)
{
    summary();
    if (invariants)
    {
        printf("Invariants: %d stuck, %d flip-flop, %d hurting floor.\n", inv_stuck, inv_flip, inv_hurt);
        fflush(stdout);
        exit(inv_stuck || inv_flip || inv_hurt ? 2 : 0);
    }
    I_Quit();
}

static void summary(void)
{
    int alive = 0, total = 0;
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next)
    {
        mobj_t *mo = (mobj_t *)th;
        if (th->function.acp1 != (actionf_p1)P_MobjThinker || mo->player || !(mo->flags & MF_COUNTKILL)) continue;
        total++;
        if (mo->health > 0) alive++;
    }
    printf("Probe: %d s played, %d of %d monsters killed, %d given up on, %d damage taken.\n",
           leveltime/TICRATE, total - alive, total, numskipped, taken);
    route_report();
    wiggle_report();
    P_DerpLog(NULL, "probe ends: %d of %d killed, %d damage taken", total - alive, total, taken);
}

void I_TestFrame(void)
{
    mobj_t *seen;
    if (gamestate != GS_LEVEL || leveltime < 2 || leveltime == lasttic) return;
    lasttic = leveltime;
    /* Hurt, and made whole: the hurt is the monsters' score. */
    if (players[0].health < 100)
    {
        taken += 100 - players[0].health;
        players[0].health = player()->health = 100;
    }
    /* NOSTRIDES=1: the steps jump as vanilla's (to compare). */
    if (getenv("NOSTRIDES")) derpparts.strides = false;
    watch_wiggles();
    watch_routes();
    if (invariants) watch_invariants();
    if (leveltime > minutes*60*TICRATE) finish();
    /* Fight what it sees near; keep walking at what is far. */
    seen = nearest_seen(style == SNEAK ? 800 : 1200);
    if (seen) shoot(seen);
    {
        /* A sneak takes a breath after each kill. */
        int now = 0;
        for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next)
        {
            mobj_t *mo = (mobj_t *)th;
            if (th->function.acp1 == (actionf_p1)P_MobjThinker && !mo->player && (mo->flags & MF_COUNTKILL) && mo->health <= 0) now++;
        }
        if (now > killed && style == SNEAK) resttic = leveltime + 2*TICRATE;
        killed = now;
    }
    if (leveltime < resttic && !seen)
        return;
    if (seen && P_AproxDistance(seen->x - player()->x, seen->y - player()->y)
        < (style == RUSH ? 200 : style == SNEAK ? 800 : 600)*FRACUNIT)
        return;
    if (!goal || goal->health <= 0 || leveltime - picktic > 3*TICRATE)
    {
        goal = pick_goal();
        picktic = leveltime;
        if (!goal) finish();
    }
    walk_to(goal->x, goal->y);
    if (seen) player()->angle = R_PointToAngle2(player()->x, player()->y, seen->x, seen->y);
    /* Stuck: open what is there; still stuck, give that one up. */
    if (P_AproxDistance(player()->x - stuckx, player()->y - stucky) > 32*FRACUNIT)
    {
        stuckx = player()->x; stucky = player()->y; stucktic = leveltime;
    }
    else if (leveltime - stucktic > TICRATE)
    {
        player()->angle = R_PointToAngle2(player()->x, player()->y, goal->x, goal->y);
        P_UseLines(&players[0]);
        if (leveltime - stucktic > 8*TICRATE && numskipped < SKIPS)
        {
            skipped[numskipped++] = goal->serial;
            P_DerpLog(goal, "probe gives up on reaching it");
            goal = NULL;
            stucktic = leveltime;
        }
    }
}

int main(int argc, char **argv)
{
    if (argc < 3) return 1;
    if (!SDL_Init(SDL_INIT_VIDEO)) return 1;
    const char *episode = argc > 4 ? argv[3] : "1", *map = argc > 4 ? argv[4] : "1";
    if (argc > 5) minutes = atoi(argv[5]);
    /* PROBE_SPEED: the game's clock this many times as fast (as fast as it keeps up). */
    if (getenv("PROBE_SPEED")) I_TestSpeed = atof(getenv("PROBE_SPEED"));
    if (getenv("PROBE_STYLE")) style = !strcmp(getenv("PROBE_STYLE"), "sneak") ? SNEAK : !strcmp(getenv("PROBE_STYLE"), "rush") ? RUSH : HUNT;
    if (getenv("PROBE_SEED")) seed = (unsigned)atoi(getenv("PROBE_SEED"))*2654435761u + 1;
    invariants = getenv("PROBE_INVARIANTS") != NULL;
    char *args[20] = {"derp-ai-probe", "-iwad", argv[1], "-warp", (char *)episode, (char *)map, "-skill", "4",
                      "-nomusic", "-nosound", "-derpai", "-derplog", argv[2]};
    myargc = 13; myargv = args;
    /* PROBE_FILE: a WAD more (the game loads derp-voices.wad by itself, a test does not). */
    if (getenv("PROBE_FILE")) { args[myargc++] = "-file"; args[myargc++] = getenv("PROBE_FILE"); }
    P_DerpTimingSet(probe_clock);
    D_DoomMain();
    return 0;
}
