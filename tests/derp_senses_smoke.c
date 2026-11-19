/* DERP's senses (p_senses.h) on a real level: noises through openings and
   shut doors, waking by how loud a gun is, deaf monsters, how long spotting
   takes, the shout, and a monster that loses the player going to where it
   last saw him, searching, giving up and going home. And vanilla's flood
   with the senses off.
   doom-derp-senses-smoke <iwad> */
#include <math.h>
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
#include "p_brain.h"
extern fixed_t tmdropoffz;
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

static int stage, stagetic;
static void fail(const char *what) { I_Error("DERP senses smoke test: %s", what); }

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

static boolean by_gun;
/* Where noises from (sx, sy) reach between lo and hi units, out of the
   player's sight, standable for a zombie: the first such of a sweep. */
static boolean find_spot(fixed_t sx, fixed_t sy, int lo, int hi, boolean hidden, fixed_t *x, fixed_t *y)
{
    int nav = P_NavClassFor(20, 56), cells = P_NavCells();
    mobj_t *probe = NULL;
    for (int c = 0; c < cells; c += 37)
    {
        fixed_t px, py, f;
        int d;
        if (!P_NavStandable(nav, c) || P_NavDoor(nav, c)) continue;
        P_NavPoint(nav, c, &px, &py, &f);
        if (P_AproxDistance(px - sx, py - sy) > hi*FRACUNIT) continue;
        /* Far by the openings is not far enough: a gun also carries through
           walls, the straight line and half again. */
        if (lo >= 1150 && P_AproxDistance(px - sx, py - sy)/2*3 < 1100*FRACUNIT) continue;
        {
            /* Not on top of a monster already placed. */
            boolean taken = false;
            for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next)
                if (th->function.acp1 == (actionf_p1)P_MobjThinker && ((mobj_t *)th) != probe
                    && (((mobj_t *)th)->flags & MF_COUNTKILL)
                    && P_AproxDistance(((mobj_t *)th)->x - px, ((mobj_t *)th)->y - py) < 96*FRACUNIT)
                    taken = true;
            if (taken) continue;
        }
        d = by_gun ? P_GunDistance(sx, sy, px, py, hi + 1) : P_NoiseDistance(sx, sy, px, py, hi + 1);
        if (d < lo || d > hi) continue;
        if (!probe) probe = P_SpawnMobj(px, py, ONFLOORZ, MT_POSSESSED);
        move_to(probe, px, py);
        if (!P_CheckPosition(probe, px, py)) continue;
        if (hidden == P_CheckSight(probe, player())) continue;
        *x = px; *y = py;
        P_RemoveMobj(probe);
        return true;
    }
    if (probe) P_RemoveMobj(probe);
    return false;
}

/* A zombie as the map would place it: its home where it stands. */
static mobj_t *zombie_at(fixed_t x, fixed_t y, angle_t angle)
{
    mobj_t *mo = P_SpawnMobj(x, y, ONFLOORZ, MT_POSSESSED);
    mo->angle = angle;
    mo->spawnpoint.x = x >> FRACBITS;
    mo->spawnpoint.y = y >> FRACBITS;
    mo->spawnpoint.angle = (short)((angle >> 24)*360/256);
    mo->spawnpoint.type = 3004;
    return mo;
}

/* A shut door: noise through it costs, through it open does not. */
static void check_door(void)
{
    int tried = 0;
    for (int s = 0; s < numsectors; ++s)
    {
        sector_t *door = &sectors[s];
        if (door->ceilingheight != door->floorheight) continue;
        for (int i = 0; i < door->linecount; ++i)
            for (int j = 0; j < door->linecount; ++j)
            {
                line_t *a = door->lines[i], *b = door->lines[j];
                sector_t *sa, *sb;
                fixed_t ax, ay, bx, by, len;
                if (i == j || !a->backsector || !b->backsector) continue;
                sa = a->frontsector == door ? a->backsector : a->frontsector;
                sb = b->frontsector == door ? b->backsector : b->frontsector;
                if (sa == sb || sa->ceilingheight - sa->floorheight < 64*FRACUNIT || sb->ceilingheight - sb->floorheight < 64*FRACUNIT) continue;
                /* 32 units out from each door line's middle, on its far side. */
                len = P_AproxDistance(a->dx, a->dy);
                ax = a->v1->x + a->dx/2 + FixedMul(FixedDiv(a->dy, len), 32*FRACUNIT);
                ay = a->v1->y + a->dy/2 - FixedMul(FixedDiv(a->dx, len), 32*FRACUNIT);
                if (R_PointInSubsector(ax, ay)->sector != sa)
                {
                    ax = a->v1->x + a->dx/2 - FixedMul(FixedDiv(a->dy, len), 32*FRACUNIT);
                    ay = a->v1->y + a->dy/2 + FixedMul(FixedDiv(a->dx, len), 32*FRACUNIT);
                }
                len = P_AproxDistance(b->dx, b->dy);
                bx = b->v1->x + b->dx/2 + FixedMul(FixedDiv(b->dy, len), 32*FRACUNIT);
                by = b->v1->y + b->dy/2 - FixedMul(FixedDiv(b->dx, len), 32*FRACUNIT);
                if (R_PointInSubsector(bx, by)->sector != sb)
                {
                    bx = b->v1->x + b->dx/2 - FixedMul(FixedDiv(b->dy, len), 32*FRACUNIT);
                    by = b->v1->y + b->dy/2 + FixedMul(FixedDiv(b->dx, len), 32*FRACUNIT);
                }
                if (R_PointInSubsector(ax, ay)->sector != sa || R_PointInSubsector(bx, by)->sector != sb) continue;
                {
                    int shut = P_NoiseDistance(ax, ay, bx, by, 4000), open;
                    door->ceilingheight += 72*FRACUNIT;
                    open = P_NoiseDistance(ax, ay, bx, by, 4000);
                    door->ceilingheight -= 72*FRACUNIT;
                    if (shut == MAXINT || open == MAXINT) continue;
                    /* Another way round this one: not the door's doing. */
                    tried++;
                    if (shut - open < 300) continue;
                    printf("Hearing: through a shut door %d units, open %d.\n", shut, open);
                    return;
                }
            }
    }
    if (tried) fail("a shut door does not muffle a noise");
    printf("Hearing: no shut door on this map to try, skipped.\n");
}

static mobj_t *near_z, *far_z, *deaf_z;

static void start_hearing(void)
{
    mobj_t *pl = player();
    players[0].cheats |= CF_GODMODE;
    fixed_t x, y;
    if (P_NoiseDistance(pl->x, pl->y, pl->x, pl->y, 100) != 0) fail("a noise is not where it is made");
    if (!(P_NoiseLoudness(NOISE_SHOTGUN) > P_NoiseLoudness(NOISE_PISTOL)
          && P_NoiseLoudness(NOISE_PISTOL) > P_NoiseLoudness(NOISE_FOOTSTEP)))
        fail("guns are not louder than steps");
    check_door();
    /* One a pistol reaches, one only a shotgun does, one the map marks deaf
       in reach (DERP does not heed it). */
    if (!find_spot(pl->x, pl->y, 400, 900, true, &x, &y)) fail("no hidden spot a pistol reaches");
    near_z = zombie_at(x, y, 0);
    by_gun = true;  /* as a shot goes: it fills the room it goes off in */
    if (!find_spot(pl->x, pl->y, 1150, 1450, true, &x, &y)) fail("no hidden spot only a shotgun reaches");
    by_gun = false;
    far_z = zombie_at(x, y, 0);
    if (!find_spot(pl->x, pl->y, 200, 900, true, &x, &y)) fail("no hidden spot for a deaf one");
    deaf_z = zombie_at(x, y, 0);
    deaf_z->flags |= MF_AMBUSH;
    players[0].readyweapon = wp_pistol;
    /* One crack: it looks up and no more. The second sends it. */
    P_NoiseAlert(pl, pl);
    if (near_z->mind != MIND_NONE || !near_z->alert) fail("a lone pistol shot did more than turn a zombie");
    P_NoiseAlert(pl, pl);
}

static void check_pistol(void)
{
    if (asleep(near_z) || near_z->mind != MIND_INVESTIGATE || near_z->target != player())
        fail("a pistol shot in reach did not wake a zombie to look");
    if (!asleep(far_z)) fail("a pistol shot woke a zombie out of its reach");
    if (asleep(deaf_z) || deaf_z->mind != MIND_INVESTIGATE) fail("a zombie the map marks deaf did not go to look");
    {
        int d = P_NoiseDistance(player()->x, player()->y, near_z->x, near_z->y, 4000);
        fixed_t off = P_AproxDistance(near_z->goalx - player()->x, near_z->goaly - player()->y);
        /* Near the shot, or at the doorway the sound came in by: not past it. */
        if (off > d*FRACUNIT) fail("a zombie thinks the shot came from far off where it did");
        printf("Hearing: pistol heard %d units off, the zombie looks %d from the shot; one at %d slept on.\n",
               d, off >> FRACBITS, P_NoiseDistance(player()->x, player()->y, far_z->x, far_z->y, 4000));
    }
    players[0].readyweapon = wp_shotgun;
    P_NoiseAlert(player(), player());
    if (far_z->mind != MIND_INVESTIGATE) fail("a shotgun did not wake what only it reaches");
    printf("Hearing: the shotgun woke it; the one marked deaf went to look too.\n");
    P_RemoveMobj(near_z); P_RemoveMobj(far_z); P_RemoveMobj(deaf_z);
}

/* Spotting: dark slower than lit, the edge of view slower than the middle,
   a running player sooner, behind never, at arm's length at once. */
static mobj_t *watcher, *friend_z;
static int watched_from;

static void check_spot_times(void)
{
    mobj_t *pl = player();
    fixed_t x, y;
    sector_t *sec = pl->subsector->sector;
    short light = sec->lightlevel;
    int lit, dark, edge, running;
    if (!find_spot(pl->x, pl->y, 300, 900, false, &x, &y)) fail("no spot in sight of the player");
    watcher = zombie_at(x, y, R_PointToAngle2(x, y, pl->x, pl->y));
    sec->lightlevel = 255; lit = P_DerpSpotTics(watcher, pl);
    sec->lightlevel = 16; dark = P_DerpSpotTics(watcher, pl);
    sec->lightlevel = 255;
    watcher->angle += ANG45 + ANG45/3;
    edge = P_DerpSpotTics(watcher, pl);
    watcher->angle += ANG90;
    if (P_DerpSpotTics(watcher, pl) != -1) fail("a zombie sees behind its back");
    watcher->angle = R_PointToAngle2(x, y, pl->x, pl->y);
    pl->momx = 20*FRACUNIT;
    running = P_DerpSpotTics(watcher, pl);
    pl->momx = 0;
    sec->lightlevel = light;
    if (!(dark > lit*3 && edge > lit && running < lit && lit > 1))
    {
        printf("  lit %d dark %d edge %d running %d\n", lit, dark, edge, running);
        fail("spotting does not go by light, view and movement");
    }
    printf("Spotting at %d units: lit %d tics, dark %d, at the edge of view %d, running %d.\n",
           P_AproxDistance(x - pl->x, y - pl->y) >> FRACBITS, lit, dark, edge, running);
    /* A friend out of sight but in earshot hears the shout. */
    if (!find_spot(watcher->x, watcher->y, 200, 900, true, &x, &y)) fail("no hidden spot near the watcher");
    friend_z = zombie_at(x, y, 0);
    watched_from = leveltime;
    players[0].cheats |= CF_GODMODE;
}

static boolean spotted(void)
{
    if (getenv("SENSESTRACE") && !(leveltime % 10))
        printf("  t%d watcher awareness %d asleep %d mind %d sees %d tics %d alert %d\n", leveltime, watcher->awareness, asleep(watcher),
               watcher->mind, P_CheckSight(watcher, player()), P_DerpSpotTics(watcher, player()), watcher->alert);
    return !asleep(watcher) && watcher->mind == MIND_HUNT;
}

static void check_spotted(void)
{
    int took = leveltime - watched_from;
    if (took < 3) fail("spotted at once, not over time");
    /* Brought: up and after the player, looking (or, a tic or two on, seeing him). */
    if (asleep(friend_z) || friend_z->target != player()
        || (friend_z->mind != MIND_INVESTIGATE && friend_z->mind != MIND_HUNT))
    {
        printf("  friend asleep %d mind %d target %p; shout distance %d, straight %d\n", asleep(friend_z), friend_z->mind,
               (void *)friend_z->target, P_NoiseDistance(watcher->x, watcher->y, friend_z->x, friend_z->y, 4000),
               P_AproxDistance(watcher->x - friend_z->x, watcher->y - friend_z->y) >> FRACBITS);
        fail("the shout did not bring a friend");
    }
    printf("Spotting: took %d tics in plain view; the shout brought a friend out of sight.\n", took);
    P_RemoveMobj(friend_z);
}

/* Memory: the player vanishes from sight; the zombie goes where it last
   saw him, not where he is, then searches, then goes home. */
static fixed_t lastx, lasty, homex, homey;
static fixed_t closest_last, closest_new;

static void start_memory(void)
{
    fixed_t x, y;
    lastx = player()->x; lasty = player()->y;
    homex = watcher->spawnpoint.x << FRACBITS; homey = watcher->spawnpoint.y << FRACBITS;
    /* Far off, out of sight and earshot. */
    if (!find_spot(lastx, lasty, 2200, 3600, true, &x, &y)) fail("nowhere far and hidden to send the player");
    move_to(player(), x, y);
    if (P_CheckSight(watcher, player())) fail("the player is still in sight where he went");
    closest_last = closest_new = MAXINT;
}

static void watch_memory(void)
{
    fixed_t dl = P_AproxDistance(watcher->x - lastx, watcher->y - lasty);
    fixed_t dn = P_AproxDistance(watcher->x - player()->x, watcher->y - player()->y);
    if (dl < closest_last) closest_last = dl;
    if (dn < closest_new) closest_new = dn;
}

static void check_searching(void)
{
    if (watcher->mind != MIND_SEARCH) fail("having lost the player, the zombie is not searching");
    if (closest_last > 96*FRACUNIT) fail("the zombie did not go where it last saw the player");
    printf("Memory: went to where it last saw him (within %d units), now searching.\n", closest_last >> FRACBITS);
    /* Searching long enough: hurry it to giving up. */
    watcher->searchtic -= 18*TICRATE;
}

static void trace_watcher(void)
{
    if (getenv("SENSESTRACE") && !(leveltime % TICRATE))
        printf("  t%d mind %d at (%d,%d) way (%d,%d) goal (%d,%d) dir %d state %d\n", leveltime, watcher->mind,
               watcher->x >> FRACBITS, watcher->y >> FRACBITS, watcher->wayx >> FRACBITS, watcher->wayy >> FRACBITS,
               watcher->goalx >> FRACBITS, watcher->goaly >> FRACBITS, watcher->movedir, (int)(watcher->state - states));
}

static void check_home(void)
{
    if (watcher->mind != MIND_NONE || !asleep(watcher)) fail("the zombie did not give up and go home");
    if (P_AproxDistance(watcher->x - homex, watcher->y - homey) > 96*FRACUNIT)
    {
        int nav = P_NavClassOf(watcher), path[4];
        printf("  gave up at (%d,%d), home (%d,%d), %d units; route home %d; time since search %d\n",
               watcher->x >> FRACBITS, watcher->y >> FRACBITS, homex >> FRACBITS, homey >> FRACBITS,
               P_AproxDistance(watcher->x - homex, watcher->y - homey) >> FRACBITS,
               P_NavPath(nav, watcher->x, watcher->y, homex, homey, path, 4, 200000), leveltime - watcher->searchtic);
        {
            static int route[16];
            int n = P_NavPath(nav, watcher->x, watcher->y, homex, homey, route, 16, 200000);
            for (int k = 0; k < n && k < 6; ++k)
            {
                fixed_t px, py, pf;
                P_NavPoint(nav, route[k], &px, &py, &pf);
                printf("  route %d: (%d,%d) floor %d door %d\n", k, px >> FRACBITS, py >> FRACBITS, pf >> FRACBITS, P_NavDoor(nav, route[k]));
            }
            for (int d = 0; d < 8; ++d)
            {
                static const int dx[8] = {12,8,0,-8,-12,-8,0,8}, dy[8] = {0,8,12,8,0,-8,-12,-8};
                boolean ok = P_CheckPosition(watcher, watcher->x + dx[d]*FRACUNIT, watcher->y + dy[d]*FRACUNIT);
                printf("  dir %d: fits %d tmfloor %d tmceil %d drop %d z %d\n", d, ok, tmfloorz >> FRACBITS, tmceilingz >> FRACBITS,
                       tmdropoffz >> FRACBITS, watcher->z >> FRACBITS);
            }
            for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next)
            {
                mobj_t *mo = (mobj_t *)th;
                if (th->function.acp1 == (actionf_p1)P_MobjThinker && mo != watcher
                    && P_AproxDistance(mo->x - watcher->x, mo->y - watcher->y) < 96*FRACUNIT)
                    printf("  near: type %d at (%d,%d) flags %x\n", mo->type, mo->x >> FRACBITS, mo->y >> FRACBITS, mo->flags);
            }
        }
        fail("the zombie gave up away from home");
    }
    if (closest_new < 512*FRACUNIT) fail("the zombie went to where the player really was");
    printf("Memory: gave up, home again (awareness %d); never nearer the player than %d units.\n",
           watcher->awareness, closest_new >> FRACBITS);
}

/* Endings that must not hang it: the monster it fought dies; one born in
   play (no home) gives up; the player dies while it hunts him. */
static mobj_t *fighter, *foe, *stray, *hunter;
static fixed_t strayx, strayy;

static void start_endings(void)
{
    mobj_t *pl = player();
    fixed_t x, y;
    if (!find_spot(pl->x, pl->y, 200, 900, true, &x, &y)) fail("no spot for the fighters");
    fighter = zombie_at(x, y, 0);
    foe = zombie_at(x + 64*FRACUNIT, y, 0);
    P_SetMobjState(fighter, fighter->info->seestate);
    fighter->target = foe;
    fighter->mind = MIND_HUNT;
    P_DamageMobj(foe, NULL, NULL, 1000);
    /* Out of earshot of the fight (the foe's death is heard round about). */
    if (!find_spot(fighter->x, fighter->y, 900, 2400, true, &x, &y)) fail("no spot for a stray");
    stray = P_SpawnMobj(x, y, ONFLOORZ, MT_POSSESSED);	/* born in play: no home */
    strayx = x; strayy = y;
    P_SetMobjState(stray, stray->info->seestate);
    stray->target = pl;
    stray->mind = MIND_SEARCH;
    stray->knownx = x; stray->knowny = y; stray->knowntic = leveltime;
    stray->goalx = x; stray->goaly = y; stray->goaltic = leveltime;
    stray->searchtic = leveltime - 25*TICRATE;
}

static void check_endings(void)
{
    if (fighter->mind == MIND_HUNT && fighter->target == foe) fail("a monster still hunts a dead one");
    if (stray->mind != MIND_NONE || !asleep(stray)) fail("a monster with no home did not settle");
    if (P_AproxDistance(stray->x - strayx, stray->y - strayy) > 128*FRACUNIT) fail("a monster with no home wandered off to settle");
    printf("Endings: its foe dead, a monster let it go; one born in play settled where it was.\n");
    /* One hunting the player, in sight, when he dies. */
    {
        mobj_t *pl = player();
        fixed_t x, y;
        if (!find_spot(pl->x, pl->y, 200, 900, false, &x, &y)) fail("no spot in sight for a hunter");
        hunter = zombie_at(x, y, R_PointToAngle2(x, y, pl->x, pl->y));
        P_SetMobjState(hunter, hunter->info->seestate);
        hunter->target = pl;
        hunter->mind = MIND_HUNT;
        players[0].cheats &= ~CF_GODMODE;
        P_DamageMobj(pl, NULL, NULL, 10000);
        if (pl->health > 0) fail("the player did not die");
    }
}

static void check_player_death(void)
{
    if (hunter->mind != MIND_NONE) fail("a monster still hunts the dead player");
    printf("Endings: the player died while hunted; the hunter let him be (no endless look and chase).\n");
}

static void check_vanilla(void)
{
    mobj_t *pl = player();
    derpparts.senses = false;
    pl->subsector->sector->soundtarget = NULL;
    P_NoiseAlert(pl, pl);
    if (pl->subsector->sector->soundtarget != pl) fail("with the senses off, a shot does not flood as vanilla's");
    if (P_BrainLook(watcher)) fail("with the senses off, DERP's look still runs");
    derpparts.senses = true;
    printf("Senses off: vanilla's flood and look.\n");
}

void I_TestFrame(void)
{
    if (gamestate != GS_LEVEL || leveltime < 2) return;
    switch (stage)
    {
      case 0:
        /* The senses alone: a soldier under fire would take cover
           (p_tactics.h, tested on its own). */
        derpparts.tactics = false;
        if (!P_DerpAI() || !P_NavReady()) fail("DERP's monsters or their navigation are not on");
        start_hearing(); stage = 1; stagetic = leveltime; break;
      case 1:
        if (leveltime - stagetic < 12) break;	/* asleep monsters get up at their next look */
        check_pistol(); stage = 5; stagetic = leveltime; break;
      case 5:
        /* The shots' flash lights him for a moment: wait it out. */
        if (leveltime - stagetic < 10) break;
        check_spot_times(); stage = 2; break;
      case 2:
        if (spotted()) { check_spotted(); start_memory(); stage = 3; stagetic = leveltime; }
        else if (leveltime - watched_from > 10*TICRATE) fail("a zombie in plain view never spotted the player");
        break;
      case 3:
        watch_memory();
        if (watcher->mind == MIND_SEARCH && closest_last <= 96*FRACUNIT) { check_searching(); stage = 4; stagetic = leveltime; }
        else if (leveltime - stagetic > 30*TICRATE) fail("the zombie never got to searching");
        break;
      case 4:
        watch_memory();
        trace_watcher();
        if (watcher->mind == MIND_NONE && asleep(watcher)) { check_home(); check_vanilla(); start_endings(); stage = 6; stagetic = leveltime; }
        else if (leveltime - stagetic > 40*TICRATE) fail("the zombie never went home");
        break;
      case 6:
        if (leveltime - stagetic < 2*TICRATE) break;
        check_endings(); stage = 7; stagetic = leveltime; break;
      case 7:
        if (leveltime - stagetic < 2*TICRATE) break;
        check_player_death(); puts("DERP senses smoke test passed."); I_Quit();
        break;
    }
}

int main(int argc, char **argv)
{
    if (argc != 2) return 1;
    if (!SDL_Init(SDL_INIT_VIDEO)) return 1;
    char *args[16] = {"derp-senses-smoke", "-iwad", argv[1], "-warp", "1", "1", "-skill", "3",
                      "-nomonsters", "-nomusic", "-nosound", "-derpai"};
    myargc = 12; myargv = args;
    D_DoomMain();
    return 0;
}
