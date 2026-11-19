/* Shots as information (p_fire.h, PLAN_DERP_AI.md's Stage S) on a real level:
   his rounds suppress the soldier they pass and not the next, a round at a
   far wall nobody; a round is heard by what it passes; soldiers lay fire on
   where he went while a friend bounds, and stop with the bound; rounds coming
   in on him are pressure, and staying put under them pins him; a chaingunner's
   burst walks in.
   doom-derp-fire-smoke <iwad> */
#include <stdio.h>
#include <stdlib.h>
#include "doomdef.h"
#include "doomstat.h"
#include "d_main.h"
#include "i_system.h"
#include "m_argv.h"
#include "m_fixed.h"
#include "r_main.h"
#include "r_state.h"
#include "tables.h"
#include "p_local.h"
#include "p_bullets.h"
#include "p_derp.h"
#include "p_fire.h"
#include "p_barks.h"
#include "p_difficulty.h"
#include "p_rounds.h"
#include "w_wad.h"
#include "p_director.h"
#include "p_nav.h"
#include "p_senses.h"
#include "p_tactics.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

static int stage, stagetic;
static mobj_t *shot_at, *beside, *listener, *coverer, *bounder, *gunner;
static fixed_t herex, herey;
static int ducked;

static void fail(const char *what) { I_Error("DERP fire smoke test: %s", what); }
static mobj_t *player(void) { return players[0].mo; }

static void move_to(mobj_t *mo, fixed_t x, fixed_t y)
{
    P_UnsetThingPosition(mo);
    mo->x = x; mo->y = y;
    P_SetThingPosition(mo);
    mo->z = mo->floorz = mo->subsector->sector->floorheight;
    mo->momx = mo->momy = mo->momz = 0;
}

/* A place lo-hi off the player, in sight of him or not, away from any already
   taken in avoid. */
static boolean find_spot(int lo, int hi, boolean sight, const mobj_t *avoid, fixed_t *x, fixed_t *y)
{
    int nav = P_NavClassFor(20, 56), cells = P_NavCells();
    mobj_t *probe = NULL;
    for (int c = 0; c < cells; c += 7)
    {
        fixed_t px, py, f, d;
        if (!P_NavStandable(nav, c) || P_NavDoor(nav, c) || P_NavHurts(c)) continue;
        P_NavPoint(nav, c, &px, &py, &f);
        d = P_AproxDistance(px - player()->x, py - player()->y);
        if (d < lo*FRACUNIT || d > hi*FRACUNIT) continue;
        if (avoid && P_AproxDistance(px - avoid->x, py - avoid->y) < 256*FRACUNIT) continue;
        if (!probe) probe = P_SpawnMobj(px, py, ONFLOORZ, MT_POSSESSED);
        move_to(probe, px, py);
        if (!P_CheckPosition(probe, px, py) || P_CheckSight(probe, player()) != sight) continue;
        *x = px; *y = py;
        P_RemoveMobj(probe);
        return true;
    }
    if (probe) P_RemoveMobj(probe);
    return false;
}

static mobj_t *soldier_at(fixed_t x, fixed_t y, mobjtype_t type)
{
    mobj_t *mo = P_SpawnMobj(x, y, ONFLOORZ, type);
    mo->angle = R_PointToAngle2(x, y, player()->x, player()->y);
    mo->target = player();
    mo->mind = MIND_HUNT;
    mo->nerve = 30;
    mo->knownx = player()->x; mo->knowny = player()->y; mo->knowntic = leveltime;
    P_SetMobjState(mo, mo->info->seestate);
    return mo;
}

/* A round from him just over mo's head: it misses, whatever stands beside it. */
static void round_past(mobj_t *mo)
{
    P_FireBulletAt(player(), mo->x, mo->y, mo->z + mo->height + 16*FRACUNIT, 0);
}

/* S1. */
static void start_suppression(void)
{
    fixed_t x, y;
    if (!find_spot(300, 700, true, NULL, &x, &y)) fail("no spot in sight for a soldier");
    shot_at = soldier_at(x, y, MT_POSSESSED);
    if (!find_spot(300, 900, true, shot_at, &x, &y)) fail("no second spot in sight");
    beside = soldier_at(x, y, MT_POSSESSED);
    shot_at->reactiontime = beside->reactiontime = 10000;	/* the shooting is his */
    shot_at->boundtic = beside->boundtic = leveltime;	/* and neither sets off on a bound */
    players[0].readyweapon = wp_pistol;
}

static void check_suppression(void)
{
    int a = P_DerpSuppression(shot_at), b = P_DerpSuppression(beside);
    printf("Suppression: it ducked after %d tics.\n", ducked);
    printf("Suppression: the one shot past %d, the other %d; mind %d (cover %d), kneeling %d; the other mind %d.\n",
           a, b, shot_at->mind, MIND_COVER, P_DerpKneeling(shot_at), beside->mind);
    if (a < 50) fail("rounds going by did not suppress a soldier");
    if (b) fail("rounds past one soldier suppressed another");
    if (shot_at->mind != MIND_COVER && !P_DerpKneeling(shot_at)) fail("suppressed, the soldier neither took cover nor knelt");
    if (beside->mind == MIND_COVER) fail("a soldier nobody fired near took cover");
    P_RemoveMobj(shot_at); P_RemoveMobj(beside);
}

/* A round into a wall far from anybody suppresses nobody. */
static void check_wall(void)
{
    fixed_t x, y;
    mobj_t *far;
    if (!find_spot(300, 700, true, NULL, &x, &y)) fail("no spot in sight for a soldier");
    far = soldier_at(x, y, MT_POSSESSED);
    /* The other way from it. */
    P_FireBulletAt(player(), 2*player()->x - x, 2*player()->y - y, player()->z + 32*FRACUNIT, 0);
    P_FireBulletAt(player(), 2*player()->x - x, 2*player()->y - y, player()->z + 32*FRACUNIT, 0);
    if (P_DerpSuppression(far)) fail("rounds the other way suppressed a soldier");
    P_RemoveMobj(far);
    printf("Suppression: rounds the other way, nobody.\n");
}

/* S5: no noise made, a round arrives. */
static void start_heard(void)
{
    fixed_t x, y;
    if (!find_spot(300, 700, true, NULL, &x, &y)) fail("no spot in sight for a listener");
    listener = P_SpawnMobj(x, y, ONFLOORZ, MT_POSSESSED);
    listener->angle = R_PointToAngle2(player()->x, player()->y, x, y);	/* its back to him */
    listener->flags |= MF_AMBUSH;
    round_past(listener);
}

static void check_heard(void)
{
    fixed_t off;
    if (listener->mind != MIND_INVESTIGATE) fail("a round going by was not heard");
    off = P_AproxDistance(listener->goalx - player()->x, listener->goaly - player()->y);
    printf("Heard: a round went by, it looks %d units from him (it stood %d off).\n", off >> FRACBITS,
           P_AproxDistance(listener->x - player()->x, listener->y - player()->y) >> FRACBITS);
    if (off >= P_AproxDistance(listener->x - player()->x, listener->y - player()->y))
        fail("a round was heard from the wrong way");
    P_RemoveMobj(listener);
}

/* S2. A soldier in sight of where he stood; he goes out of its sight; a friend behind it bounds. */
static void keep_bounding(void)
{
    bounder->mind = MIND_HUNT;
    bounder->tactic = STEP_BOUNDING;
    bounder->tactictic = leveltime;
    bounder->knowntic = leveltime;
    bounder->firex = bounder->x; bounder->firey = bounder->y;
}

static void start_laying(void)
{
    fixed_t x, y, sx, sy;
    int nav = P_NavClassFor(20, 56), cells = P_NavCells();
    boolean placed = false;
    if (!find_spot(300, 700, true, NULL, &sx, &sy)) fail("no spot in sight to lay fire from");
    coverer = soldier_at(sx, sy, MT_SHOTGUY);	/* (Doom 1's data has no chaingunner to draw) */
    herex = player()->x; herey = player()->y;
    /* Friend: out of the line to the place. */
    {
        angle_t an = R_PointToAngle2(herex, herey, sx, sy) >> ANGLETOFINESHIFT;
        bounder = soldier_at(sx + 64*finecosine[an], sy + 64*finesine[an], MT_POSSESSED);
    }
    /* He goes somewhere out of its sight. */
    for (int c = 0; c < cells && !placed; c += 11)
    {
        fixed_t pf;
        if (!P_NavStandable(nav, c) || P_NavDoor(nav, c) || P_NavHurts(c)) continue;
        P_NavPoint(nav, c, &x, &y, &pf);
        if (P_AproxDistance(x - herex, y - herey) < 200*FRACUNIT || P_AproxDistance(x - herex, y - herey) > 600*FRACUNIT) continue;
        move_to(player(), x, y);
        if (!P_CheckSight(coverer, player()) && !P_CheckSight(bounder, player())) placed = true;
    }
    if (!placed) fail("nowhere for him out of the soldier's sight");
    coverer->knownx = herex; coverer->knowny = herey;
    coverer->knowntic = leveltime;
    coverer->firetic = 0;
}

static int laid, laying_tics;
static void watch_laying(void)
{
    if (P_DerpLaying(coverer)) ++laying_tics;
    if (P_BulletCount() > laid) laid = P_BulletCount();
}

static void check_laying(void)
{
    printf("Laying fire: %d tics, %d rounds in flight at most, facing the place: %s.\n", laying_tics, laid,
           R_PointToAngle2(coverer->x, coverer->y, herex, herey) - coverer->angle < ANG45/2
           || coverer->angle - R_PointToAngle2(coverer->x, coverer->y, herex, herey) < ANG45/2 ? "yes" : "no");
    if (!laying_tics) fail("with a friend on a bound, a soldier did not lay fire");
    if (!laid) fail("laying fire, a soldier fired nothing");
}

static void check_stopped(void)
{
    if (P_DerpLaying(coverer)) fail("the bound over, a soldier went on laying fire");
    printf("Laying fire: stopped with the bound.\n");
    P_RemoveMobj(coverer); P_RemoveMobj(bounder);
    move_to(player(), herex, herey);
}

/* S4, S3: rounds coming in on him. */
static void start_pinning(void)
{
    fixed_t x, y;
    if (!find_spot(300, 700, true, NULL, &x, &y)) fail("no spot in sight for a shooter");
    gunner = soldier_at(x, y, MT_POSSESSED);
    gunner->reactiontime = 10000;
}

static void watch_pinning(void)
{
    if (!(leveltime % 6))
        P_FireBulletAt(gunner, player()->x + 48*FRACUNIT, player()->y + 48*FRACUNIT, player()->z + 32*FRACUNIT, 0);
}

static void check_pinned(void)
{
    fixed_t x = player()->x;
    if (!P_FireHeavy()) fail("rounds coming in on him did not count as heavy");
    if (!P_DerpPinned()) fail("staying put under fire for four seconds, he was not pinned");
    printf("Pinned: heavy fire, he stayed put; pinned.\n");
    player()->x += 200*FRACUNIT;
    if (P_DerpPinned()) fail("he moved off and was still pinned");
    player()->x = x;
    printf("Pinned: he moved, no longer.\n");
}

/* S6. */
static void check_walk(void)
{
    angle_t off, was = 0;
    int walked = 0;
    gunner->type = MT_CHAINGUY;
    gunner->burst = 0;
    while (P_DerpWalkFire(gunner, &off))
    {
        angle_t size = (int)off < 0 ? (angle_t)-(int)off : off;
        if (walked && size >= was) fail("a burst did not walk in");
        was = size;
        ++walked;
        if (walked > 8) fail("a burst walked on for ever");
    }
    printf("Burst: %d rounds walk in, then its usual spread.\n", walked);
    if (walked < 2) fail("a burst did not walk");
    P_RemoveMobj(gunner);
}

/* A soldier asleep a long way off, looking straight at him. */
static mobj_t *far_off;
static void start_far(void)
{
    fixed_t x = 0, y = 0, px, py, pf;
    int nav = P_NavClassFor(20, 56), cells = P_NavCells();
    boolean found = false;
    /* He goes where there is a long sightline. */
    for (int c = 0; c < cells && !found; c += 523)
    {
        if (!P_NavStandable(nav, c) || P_NavDoor(nav, c) || P_NavHurts(c)) continue;
        P_NavPoint(nav, c, &px, &py, &pf);
        move_to(player(), px, py);
        found = find_spot(1250, 3000, true, NULL, &x, &y);
    }
    if (!found) { printf("Far: no such sightline on this map, skipped.\n"); return; }
    far_off = P_SpawnMobj(x, y, ONFLOORZ, MT_POSSESSED);
    far_off->angle = R_PointToAngle2(x, y, player()->x, player()->y);
}

/* A soldier he can see, a friend lying in wait out of his sight behind it. */
static mobj_t *bait, *lurker;
static int baited;

static void start_bait(void)
{
    int nav = P_NavClassFor(20, 56), cells = P_NavCells();
    boolean found = false;
    fixed_t sx = 0, sy = 0, lx = 0, ly = 0;
    P_DirectorStance(false);
    for (int a = 0; a < cells && !found; a += 389)
    {
        fixed_t px, py, pf, here;
        if (!P_NavStandable(nav, a) || P_NavDoor(nav, a) || P_NavHurts(a)) continue;
        P_NavPoint(nav, a, &px, &py, &pf);
        move_to(player(), px, py);
        if (!find_spot(250, 500, true, NULL, &sx, &sy)) continue;
        here = P_AproxDistance(sx - px, sy - py);
        for (int b = 0; b < cells && !found; b += 13)
        {
            fixed_t qf, d;
            static int path[2];
            if (!P_NavStandable(nav, b) || P_NavDoor(nav, b) || P_NavHurts(b)) continue;
            P_NavPoint(nav, b, &lx, &ly, &qf);
            d = P_AproxDistance(lx - sx, ly - sy);
            if (d < 200*FRACUNIT || d > 500*FRACUNIT || P_AproxDistance(lx - px, ly - py) <= here + 64*FRACUNIT) continue;
            if (P_NavPath(nav, sx, sy, lx, ly, path, 2, 3000) <= 0) continue;
            lurker = soldier_at(lx, ly, MT_POSSESSED);
            if (!P_CheckPosition(lurker, lx, ly) || P_CheckSight(player(), lurker)) { P_RemoveMobj(lurker); continue; }
            found = true;
        }
    }
    if (!found) fail("nowhere for a soldier with a friend out of sight behind it");
    lurker->mind = MIND_AMBUSH; lurker->tactic = STEP_WAITING; lurker->coverx = lx; lurker->covery = ly;
    lurker->tactictic = leveltime; lurker->reactiontime = 10000;
    bait = soldier_at(sx, sy, MT_POSSESSED);
    bait->reactiontime = 10000;
}

/* The skill levels: easier aims worse, fires less, spots later. */
static void check_skill(void)
{
    fixed_t x, y;
    mobj_t *s;
    skill_t was = gameskill;
    int easy_fire = 0, hard_fire = 0, easy_spot, hard_spot;
    double easy_aim = 0, hard_aim = 0;
    /* TUNE_CHECK: -derptune given, it wins on every skill. */
    if (getenv("TUNE_CHECK"))
    {
        const difficulty_t *d = P_Difficulty();
        if (d->aim != 6 || d->trigger != 40 || d->eyes != 200) fail("-derptune was not taken");
        printf("Skill: -derptune taken.\n");
        return;
    }
    if (!find_spot(300, 700, true, NULL, &x, &y)) fail("no spot in sight for the skills");
    s = soldier_at(x, y, MT_POSSESSED);
    for (int skill = sk_baby; skill <= sk_hard; skill += sk_hard - sk_baby)
    {
        gameskill = skill;
        int fired = 0, spot = P_DerpSpotTics(s, player());
        double spread = 0;
        for (int k = 0; k < 4000; k++)
        {
            angle_t a = 0;
            fixed_t slope = 0;
            fired += P_DerpTrigger();
            P_RoundMonsterAim(&a, &slope, 256);
            spread += abs((int)a)/(double)ANG45;
        }
        if (skill == sk_baby) { easy_fire = fired; easy_spot = spot; easy_aim = spread; }
        else { hard_fire = fired; hard_spot = spot; hard_aim = spread; }
    }
    gameskill = was;
    P_RemoveMobj(s);
    printf("Skill: fires %d against %d in 4000, spots in %d against %d tics, aim off %.0f%% more.\n",
           easy_fire, hard_fire, easy_spot, hard_spot, 100*(easy_aim/hard_aim - 1));
    if (hard_fire != 4000) fail("on Ultra-Violence a chance to fire was not taken");
    if (easy_fire >= hard_fire*3/4) fail("the easiest skill fires as often as the hardest");
    if (easy_spot <= hard_spot) fail("the easiest skill spots as fast as the hardest");
    if (easy_aim <= hard_aim*1.2) fail("the easiest skill aims as well as the hardest");
}

/* The trace from him through mo's middle, shifted side to side by off units. */
static void aim_through(mobj_t *mo, int off)
{
    angle_t an = R_PointToAngle2(player()->x, player()->y, mo->x, mo->y) + ANG90;
    trace.x = player()->x + off*finecosine[an>>ANGLETOFINESHIFT];
    trace.y = player()->y + off*finesine[an>>ANGLETOFINESHIFT];
    trace.dx = mo->x - player()->x;
    trace.dy = mo->y - player()->y;
}

/* A zombieman's face: his picture tops out at 50, the shoulders at 41. */
#define FACE_Z (46*FRACUNIT)

/* Only the drawn head, only under Authored: the air over it, a shoulder,
   and the same face with Authored off all miss. off: units to his left
   (the head of POSSA1 is drawn -8..+2 across). turn: a quarter turn of
   the soldier, to his left (POSSA7, POSSA3 mirrored: its head +3..+13)
   or right (POSSA3: -13..-3), where the middle of the body is no head. */
static void check_head_zone(void)
{
    static const struct { int z, off, turn; boolean authored, head; const char *what; } shots[] = {
        { 46, 0, 0, true, true, "the face" },
        { 42, 4, 0, true, true, "the chin, a little aside" },
        { 53, 0, 0, true, false, "the air over the head" },
        { 38, 0, 0, true, false, "the chest" },
        { 46, 12, 0, true, false, "beside the head, a shoulder's width out" },
        { 46, -8, 1, true, true, "side-on, mirrored: the drawn head" },
        { 46, 0, 1, true, false, "side-on, mirrored: the middle of the body" },
        { 46, 8, 1, true, false, "side-on, mirrored: where the head would be unmirrored" },
        { 46, 8, -1, true, true, "side-on: the drawn head" },
        { 46, 0, -1, true, false, "side-on: the middle of the body" },
        { 46, 0, 0, false, false, "the face with Authored off" },
    };
    fixed_t x, y;
    boolean was = derpauthored;
    if (!find_spot(300, 700, true, NULL, &x, &y)) fail("no spot in sight for a head");
    mobj_t *s = soldier_at(x, y, MT_POSSESSED);
    angle_t facing = s->angle;
    for (int k = 0; k < (int)(sizeof shots/sizeof shots[0]); k++)
    {
        derpauthored = shots[k].authored;
        s->angle = facing + shots[k].turn*ANG90;
        s->headtic = -1;
        aim_through(s, shots[k].off);
        P_DerpShotAt(s, player(), trace.x, trace.y, s->z + shots[k].z*FRACUNIT, 0);
        if ((s->headtic == leveltime) != shots[k].head)
        {
            printf("Head zone wrong for %s.\n", shots[k].what);
            fail(shots[k].head ? "a shot through the drawn head missed it" : "a shot off the drawn head counted as one");
        }
    }
    derpauthored = was;
    P_RemoveMobj(s);
    printf("Head: the drawn head only, under Authored only.\n");
}

/* Through the head with the pistol: silent. With anything else: heard. */
static void check_silent_gun(void)
{
    static const weapontype_t guns[2] = { wp_pistol, wp_shotgun };
    weapontype_t was = players[0].readyweapon;
    fixed_t x, y;
    if (!find_spot(300, 700, true, NULL, &x, &y)) fail("no spot in sight for a headshot");
    for (int k = 0; k < 2; k++)
    {
        mobj_t *s = soldier_at(x, y, MT_POSSESSED);
        players[0].readyweapon = guns[k];
        aim_through(s, 0);
        P_DerpShotAt(s, player(), trace.x, trace.y, s->z + FACE_Z, 0);
        P_DamageMobj(s, player(), player(), s->health);
        if (s->silent != (k == 0)) fail(k ? "a shotgun's kill through the head was silent" : "the pistol's kill through the head was not silent");
    }
    players[0].readyweapon = was;
    printf("Silent: the pistol through the head, not the shotgun.\n");
}

static mobj_t *pacer;
static int pacefire, paced;
static void start_pace(void)
{
    fixed_t x, y;
    if (!find_spot(400, 600, true, NULL, &x, &y)) fail("no spot in sight for a soldier");
    pacer = soldier_at(x, y, MT_POSSESSED);
    pacer->reactiontime = 0;
    pacefire = pacer->firetic;
}

/* The one check before an attack. */
static void check_permission(void)
{
    fixed_t x, y;
    mobj_t *s;
    if (!find_spot(300, 700, true, NULL, &x, &y)) fail("no spot in sight for a soldier");
    s = soldier_at(x, y, MT_POSSESSED);
    if (P_DerpMayFire(s)) fail("a soldier fired the moment he came into view");
    s->beattic = leveltime;
    if (!P_DerpMayFire(s)) fail("a soldier in a plain hunt may not fire");
    s->tactic = STEP_BOUNDING;
    /* In sight it fires, whatever it is doing, walking on (p_gunwalk.h). */
    if (!P_DerpMayFire(s)) fail("a soldier on a bound holds its fire");
    s->tactic = 0;
    s->mind = MIND_RETREAT;
    if (!P_DerpMayFire(s)) fail("a soldier running away holds its fire");
    s->tactic = STEP_BAIT;
    if (P_DerpMayFire(s)) fail("a soldier faking a retreat fires back");
    s->mind = MIND_COVER; s->tactic = STEP_COVER_GOING;
    if (!P_DerpMayFire(s)) fail("a soldier on its way to cover holds its fire");
    s->tactic = STEP_COVER_LEANING;
    if (!P_DerpMayFire(s)) fail("a soldier at the lean may not fire");
    /* Right on top of it, on a bound or not. */
    s->mind = MIND_HUNT; s->tactic = STEP_BOUNDING;
    move_to(s, player()->x + 100*FRACUNIT, player()->y);
    if (!P_DerpMayFire(s)) fail("with him on top of it a soldier may not fire");
    P_RemoveMobj(s);
    printf("Permission: always in sight, but for a fake retreat.\n");
}

/* A demon stopped in the lane: nothing into its back, and fire again once it goes. */
static mobj_t *rifle, *demon;
static int rifle_fired, into_back;

static void start_lane(void)
{
    fixed_t x, y;
    if (!find_spot(300, 500, true, NULL, &x, &y)) fail("no spot in sight for a lane");
    rifle = soldier_at(x, y, MT_POSSESSED);
    rifle->boundtic = leveltime;
    demon = P_SpawnMobj(x + (player()->x - x)/4, y + (player()->y - y)/4, ONFLOORZ, MT_TROOP);
    demon->target = player();
    demon->tics = -1;
    rifle_fired = rifle->firetic;
    if (!P_DerpLaneBlocked(rifle)) fail("a demon in the line does not count as a blocked lane");
}

static void watch_lane(void)
{
    rifle->boundtic = rifle->knowntic = leveltime;
    if (rifle->firetic != rifle_fired)
    {
        rifle_fired = rifle->firetic;
        P_AimLineAttack(rifle, R_PointToAngle2(rifle->x, rifle->y, player()->x, player()->y), MISSILERANGE);
        if (linetarget == demon) ++into_back;
    }
    /* Keep the demon in its line, wherever it stepped. */
    move_to(demon, rifle->x + (player()->x - rifle->x)/4, rifle->y + (player()->y - rifle->y)/4);
}

void I_TestFrame(void)
{
    if (gamestate != GS_LEVEL || leveltime < 2) return;
    switch (stage)
    {
      case 0:
        if (leveltime < 4*TICRATE) break;
        if (!P_DerpAI() || !P_NavReady()) fail("DERP's monsters or their navigation are not on");
        if (!P_BulletsTravel()) fail("the bullets do not travel");
        players[0].cheats |= CF_GODMODE;
        start_suppression(); stage = 1; stagetic = leveltime; break;
      case 1:
        if (!(leveltime % 20)) round_past(shot_at);
        if (!ducked && (shot_at->mind == MIND_COVER || P_DerpKneeling(shot_at))) ducked = leveltime - stagetic;
        shot_at->knowntic = beside->knowntic = leveltime;
        if (leveltime - stagetic < 5*TICRATE) break;
        check_suppression(); check_wall(); start_heard(); stage = 2; stagetic = leveltime; break;
      case 2:
        if (leveltime - stagetic < 4) break;
        check_heard(); start_laying(); stage = 3; stagetic = leveltime; break;
      case 3:
        keep_bounding();
        watch_laying();
        if (leveltime - stagetic < 2*TICRATE) break;
        check_laying();
        bounder->tactic = 0;
        stage = 4; stagetic = leveltime; break;
      case 4:
        if (leveltime - stagetic < TICRATE) break;
        check_stopped(); start_pinning(); stage = 5; stagetic = leveltime; break;
      case 5:
        watch_pinning();
        if (leveltime - stagetic < 4*TICRATE) break;
        check_pinned(); check_walk(); check_permission(); check_skill(); start_lane();
        stage = 6; stagetic = leveltime;
        break;
      case 6:
        watch_lane();
        if (leveltime - stagetic < 6*TICRATE) break;
        printf("Lane: %d shots into the demon's back in 6 s.\n", into_back);
        if (into_back) fail("a soldier fired into the back of a demon in its lane");
        P_RemoveMobj(demon);
        rifle_fired = rifle->firetic;
        stage = 7; stagetic = leveltime;
        break;
      case 7:
        rifle->boundtic = rifle->knowntic = leveltime;
        if (rifle->firetic != rifle_fired)
        {
            printf("Lane: clear, it fired after %d tics.\n", leveltime - stagetic);
            P_RemoveMobj(rifle);
            start_far();
            stage = 8; stagetic = leveltime;
        }
        if (leveltime - stagetic > 8*TICRATE) fail("the lane clear, the soldier never fired again");
        break;
      case 8:
        if (far_off && leveltime - stagetic < 6*TICRATE)
        {
            if (far_off->mind != MIND_NONE) fail("a sleeping soldier picked him out from too far off");
            break;
        }
        if (far_off) printf("Far: it watched %d units off and did not make him out.\n",
                            P_AproxDistance(far_off->x - player()->x, far_off->y - player()->y) >> FRACBITS);
        if (far_off) P_RemoveMobj(far_off);
        start_bait();
        stage = 9; stagetic = leveltime;
        break;
      case 9:
        bait->boundtic = bait->knowntic = leveltime;
        if (bait->mind == MIND_RETREAT && !baited) baited = leveltime - stagetic;
        if (!baited)
        {
            bait->decidetic = leveltime;	/* it has its chance every tic: the dice are not what is tested */
            if (leveltime - stagetic > 5*TICRATE) fail("in a lull with a friend in wait behind it, a soldier never fell back");
            break;
        }
        if (bait->mind == MIND_RETREAT)
        {
            if (leveltime - stagetic > 12*TICRATE) fail("a fake retreat never turned");
            break;
        }
        printf("Bait: it ran after %d tics, turned on him %d units from its friend.\n", baited,
               P_AproxDistance(bait->x - lurker->x, bait->y - lurker->y) >> FRACBITS);
        if (P_AproxDistance(bait->x - lurker->x, bait->y - lurker->y) > 256*FRACUNIT) fail("a fake retreat did not end near the friend in wait");
        stage = 10; stagetic = leveltime;
        break;
      case 10:
        /* The shouts, if the recordings are here: one voice at a time. */
        if (W_CheckNumForName("BKA001") < 0)
        {
            if (getenv("BARKS_WAD")) fail("barks.wad was given but holds no shouts");
            printf("Barks: no recordings here, skipped.\n");
        }
        else
        {
            if (!P_DerpBark(lurker, BARK_KEY))
            {
                if (leveltime - stagetic > 10*TICRATE) fail("a soldier never got to shout");
                break;
            }
            if (P_DerpBark(bait, BARK_REGROUP)) fail("two soldiers shouted at once");
            if (!P_DerpBody(bait, BARK_PAIN)) fail("a soldier's pain waited its turn behind a shout");
            if (!P_DerpBody(bait, BARK_DEATH)) fail("no death scream of its own for a zombieman");
            if (W_CheckNumForName("BKB241") < 0) fail("no death scream of its own for a shotgun guy");
            printf("Barks: one shouted, the next had to wait.\n");
        }
        start_pace();
        stage = 11; stagetic = leveltime;
        break;
      case 11:
        /* A soldier in sight of him, in the open, a long way off: it fires at a pace. */
        pacer->boundtic = pacer->knowntic = leveltime;
        if (pacer->firetic != pacefire) { pacefire = pacer->firetic; ++paced; }
        if (leveltime - stagetic < 6*TICRATE) break;
        printf("Pace: %d shots in 6 s with him in sight.\n", paced);
        if (paced < 3) fail("a soldier in sight of him hardly fired");
        P_RemoveMobj(pacer);
        /* Last: a silent corpse is a body for the others to find. */
        check_head_zone();
        check_silent_gun();
        puts("DERP fire smoke test passed.");
        I_Quit();
        break;
    }
}

int main(int argc, char **argv)
{
    if (argc != 2) return 1;
    derpauthored = true;	/* Headshots are Authored's alone. */
    if (!SDL_Init(SDL_INIT_VIDEO)) return 1;
    char *args[16] = {"derp-fire-smoke", "-iwad", argv[1], "-warp", "1", "1", "-skill", "3",
                      "-nomonsters", "-nomusic", "-nosound", "-derpai"};
    myargc = 12; myargv = args;
    /* The shouts, and the pain cries from derp-cries.wad (the pain waits behind no shout). */
    if (getenv("BARKS_WAD"))
    {
        args[myargc++] = "-file"; args[myargc++] = getenv("BARKS_WAD");
        if (getenv("CRIES_WAD")) args[myargc++] = getenv("CRIES_WAD");
    }
    if (getenv("TUNE_CHECK")) { args[myargc++] = "-derptune"; args[myargc++] = "6"; args[myargc++] = "40"; args[myargc++] = "200"; args[myargc++] = "45"; }
    D_DoomMain();
    return 0;
}
