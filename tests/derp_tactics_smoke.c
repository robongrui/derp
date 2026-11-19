/* DERP's squads and tactics (p_tactics.h) on a real level: a scream heard
   and counted, a headshot that kills in silence, its body found, cover
   under fire with a peek, a retreat when badly hurt, an ambush after
   losses, a friend kneeling for a shot; vanilla's aim with tactics off.
   doom-derp-tactics-smoke <iwad> */
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
#include "p_tactics.h"
#include "p_bullets.h"
#include "tables.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

static int stage, stagetic;
static void fail(const char *what) { I_Error("DERP tactics smoke test: %s", what); }
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

/* A spot whose noise distance from (sx, sy) is lo-hi, in or out of the
   player's sight as asked, standable, not on another monster. */
static mobj_t *unseen;	/* when set: the spot must not see it */
static boolean find_spot(fixed_t sx, fixed_t sy, int lo, int hi, int sight, fixed_t *x, fixed_t *y)
{
    int nav = P_NavClassFor(20, 56), cells = P_NavCells();
    mobj_t *probe = NULL;
    for (int c = 0; c < cells; c += 11)
    {
        fixed_t px, py, f;
        int d;
        boolean taken = false;
        if (!P_NavStandable(nav, c) || P_NavDoor(nav, c)) continue;
        P_NavPoint(nav, c, &px, &py, &f);
        if (P_AproxDistance(px - sx, py - sy) > hi*FRACUNIT) continue;
        for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next)
            if (th->function.acp1 == (actionf_p1)P_MobjThinker && ((mobj_t *)th) != probe
                && (((mobj_t *)th)->flags & MF_COUNTKILL)
                && P_AproxDistance(((mobj_t *)th)->x - px, ((mobj_t *)th)->y - py) < 96*FRACUNIT)
                taken = true;
        if (taken) continue;
        d = P_NoiseDistance(sx, sy, px, py, hi + 1);
        if (d < lo || d > hi) continue;
        if (!probe) probe = P_SpawnMobj(px, py, ONFLOORZ, MT_POSSESSED);
        move_to(probe, px, py);
        if (!P_CheckPosition(probe, px, py)) continue;
        if (sight >= 0 && sight != (int)P_CheckSight(probe, player())) continue;
        if (unseen && P_CheckSight(probe, unseen)) continue;
        *x = px; *y = py;
        P_RemoveMobj(probe);
        return true;
    }
    if (probe) P_RemoveMobj(probe);
    return false;
}

static mobj_t *soldier_at(fixed_t x, fixed_t y, angle_t angle)
{
    mobj_t *mo = P_SpawnMobj(x, y, ONFLOORZ, MT_POSSESSED);
    mo->angle = angle;
    mo->spawnpoint.x = x >> FRACBITS;
    mo->spawnpoint.y = y >> FRACBITS;
    mo->spawnpoint.type = 3004;
    return mo;
}

/* Killed by the player's shot, in the head or the legs: just enough to
   kill (a death that screams, A_Scream), or far more (a gibbing, A_XScream). */
static void kill(mobj_t *mo, boolean head, boolean gib)
{
    /* Straight at his middle, turned to face the shot so his head is drawn
       there: the face (46, under the picture's top at 50) or the legs. */
    mo->angle = R_PointToAngle2(mo->x, mo->y, player()->x, player()->y);
    trace.x = player()->x; trace.y = player()->y;
    trace.dx = mo->x - player()->x; trace.dy = mo->y - player()->y;
    P_DerpShotAt(mo, player(), trace.x, trace.y, mo->z + (head ? 46 : 8)*FRACUNIT, 0);
    P_DamageMobj(mo, player(), player(), gib ? 1000 : mo->health);
}

/* Deaths: a scream heard and counted; a headshot, nothing heard. */
static mobj_t *screamer, *ear, *silenced, *ear2, *finder;
static void start_deaths(void)
{
    fixed_t x, y;
    if (!find_spot(player()->x, player()->y, 1600, 2400, 0, &x, &y)) fail("no hidden spot far off");
    screamer = soldier_at(x, y, 0);
    if (!find_spot(x, y, 100, 700, -1, &x, &y)) fail("no spot near the screamer");
    ear = soldier_at(x, y, 0);
    ear->squad = screamer->squad = 7;	/* one squad */
    ear->nerve = 50;
    kill(screamer, false, false);
    /* A barrel going up beside it: heard, but no loss. */
    {
        /* Past its blast's 128 units, or the hearer would die of it. */
        mobj_t *barrel = P_SpawnMobj(ear->x + 200*FRACUNIT, ear->y, ONFLOORZ, MT_BARREL);
        P_DamageMobj(barrel, player(), player(), 1000);
    }
}

static void check_scream(void)
{
    if (asleep(ear) || ear->losses != 1)	/* the screamer, not the barrel */
    {
        printf("  ear asleep %d losses %d mind %d; screamer health %d state %d silent %d; noise distance %d\n", asleep(ear), ear->losses,
               ear->mind, screamer->health, (int)(screamer->state - states), screamer->silent,
               P_NoiseDistance(screamer->x, screamer->y, ear->x, ear->y, 4000));
        fail("a death scream was not heard, or not counted");
    }
    printf("Deaths: a scream heard, 1 loss counted (a barrel going up beside it none); the hearer up, nerve %d.\n", ear->nerve);
    /* A gibbing is heard as a death too. */
    {
        fixed_t x = ear->x, y = ear->y;
        mobj_t *gibbed;
        ear->losses = 0;
        gibbed = soldier_at(x + 96*FRACUNIT, y, 0);
        gibbed->squad = 7;
        kill(gibbed, false, true);
    }
}

static void check_gibbing(void)
{
    if (ear->losses != 1) fail("a gibbing was not heard as a loss");
    printf("Deaths: a gibbing heard as one too.\n");
    P_RemoveMobj(ear);
}

static void start_headshot(void)
{
    fixed_t x, y, bx, by;
    P_DerpForgetBodies();	/* The stages before left theirs about. */
    if (!find_spot(player()->x, player()->y, 1600, 2400, 0, &x, &y)) fail("no hidden spot far off for a headshot");
    silenced = soldier_at(x, y, 0);
    bx = x; by = y;
    /* Near enough to hear a scream, out of sight of the body (else it would
       find it by looking, as it should). */
    unseen = silenced;
    if (!find_spot(bx, by, 100, 740, -1, &x, &y)) fail("no spot in earshot but out of sight of the silenced one");
    unseen = NULL;
    ear2 = soldier_at(x, y, 0);
    ear2->nerve = 50;
    ear2->squad = silenced->squad = 8;
    kill(silenced, true, false);
    if (!silenced->silent) fail("a headshot was not silent");
}

static void check_silence(void)
{
    if (!asleep(ear2) || ear2->losses) fail("a headshot's death was heard");
    printf("Deaths: shot through the head, a soldier fell without a sound; the one beside it slept on.\n");
    P_RemoveMobj(ear2);
    /* One who comes to see it: in sight of the body. */
    {
        int nav = P_NavClassFor(20, 56);
        fixed_t x, y, f;
        int cell = -1;
        for (int c = 0; c < P_NavCells() && cell < 0; c += 7)
        {
            if (!P_NavStandable(nav, c)) continue;
            P_NavPoint(nav, c, &x, &y, &f);
            if (P_AproxDistance(x - silenced->x, y - silenced->y) < 128*FRACUNIT
                || P_AproxDistance(x - silenced->x, y - silenced->y) > 512*FRACUNIT) continue;
            {
                mobj_t *probe = soldier_at(x, y, R_PointToAngle2(x, y, silenced->x, silenced->y));
                boolean sees = P_CheckSight(probe, silenced) && !P_CheckSight(probe, player());
                if (sees) { finder = probe; cell = c; }
                else P_RemoveMobj(probe);
            }
        }
        if (!finder) fail("no spot in sight of the body");
        finder->squad = 8;
        finder->nerve = 50;
    }
}

static void check_found(void)
{
    if (finder->alert != 2 || finder->mind != MIND_INVESTIGATE || finder->losses != 1)
        fail("the body was not found, or the finder not on alert");
    printf("Bodies: found by sight; the finder on alert (scale %d%%), looking where it lay.\n", P_DerpAlertScale(finder));
    P_RemoveMobj(finder);
}

/* A place for him and a spot 300-700 off in his sight with something to get
   behind near it: an open room has none. He is moved there. */
static boolean cover_scene(fixed_t *x, fixed_t *y)
{
    int nav = P_NavClassFor(20, 56), cells = P_NavCells();
    mobj_t *probe = NULL;
    fixed_t cx, cy, kx, ky;
    for (int a = 0; a < cells; a += 257)
    {
        fixed_t px, py, pf;
        if (!P_NavStandable(nav, a) || P_NavDoor(nav, a) || P_NavHurts(a)) continue;
        P_NavPoint(nav, a, &px, &py, &pf);
        P_UnsetThingPosition(player());
        player()->x = px; player()->y = py;
        P_SetThingPosition(player());
        player()->z = player()->floorz = player()->subsector->sector->floorheight;
        for (int b = 0; b < cells; b += 1013)
        {
            fixed_t qx, qy, qf, d;
            if (!P_NavStandable(nav, b) || P_NavDoor(nav, b) || P_NavHurts(b)) continue;
            P_NavPoint(nav, b, &qx, &qy, &qf);
            d = P_AproxDistance(qx - px, qy - py);
            if (d < 300*FRACUNIT || d > 700*FRACUNIT) continue;
            if (!probe) probe = P_SpawnMobj(qx, qy, ONFLOORZ, MT_POSSESSED);
            P_UnsetThingPosition(probe);
            probe->x = qx; probe->y = qy;
            P_SetThingPosition(probe);
            probe->z = probe->floorz = probe->subsector->sector->floorheight;
            if (!P_CheckPosition(probe, qx, qy) || !P_CheckSight(probe, player())) continue;
            if (!P_DerpCornerCover(probe, player(), &cx, &cy, &kx, &ky)) continue;
            P_RemoveMobj(probe);
            *x = qx; *y = qy;
            return true;
        }
    }
    if (probe) P_RemoveMobj(probe);
    return false;
}

/* A round from him just over mo's head: it misses, whatever stands beside it. */
static void round_past(mobj_t *mo)
{
    P_FireBulletAt(player(), mo->x, mo->y, mo->z + mo->height + 16*FRACUNIT, 0);
}

/* Cover: under fire, a soldier ducks out of sight, then peeks again. */
static mobj_t *coverer;
static int hidden_tics, seen_after_hiding;
static void start_cover(void)
{
    fixed_t x, y;
    if (!cover_scene(&x, &y)) fail("no spot in sight with cover near it");
    coverer = soldier_at(x, y, R_PointToAngle2(x, y, player()->x, player()->y));
    P_SetMobjState(coverer, coverer->info->seestate);
    coverer->target = player();
    coverer->mind = MIND_HUNT;
    coverer->squad = 9; coverer->nerve = 60;
    players[0].readyweapon = wp_pistol;
    P_NoiseAlert(player(), player());	/* he fires */
    hidden_tics = seen_after_hiding = 0;
}

static boolean hit_in_cover;
static void watch_cover(void)
{
    /* Hit once in cover: it thinks again, it does not drop its cover. */
    if (!hit_in_cover && coverer->mind == MIND_COVER)
    {
        P_DamageMobj(coverer, player(), player(), 1);
        if (coverer->mind != MIND_COVER) fail("a hit made a soldier drop its cover");
        hit_in_cover = true;
    }
    boolean sees = P_CheckSight(coverer, player());
    if (getenv("TACTICSDEBUG") && !(leveltime % 17))
        printf("  t%d coverer mind %d tactic %d sees %d at (%d,%d) cover (%d,%d) peek (%d,%d)\n", leveltime, coverer->mind, coverer->tactic,
               sees, coverer->x >> FRACBITS, coverer->y >> FRACBITS, coverer->coverx >> FRACBITS, coverer->covery >> FRACBITS,
               coverer->peekx >> FRACBITS, coverer->peeky >> FRACBITS);
    if (!sees) ++hidden_tics;
    else if (hidden_tics > 10) seen_after_hiding = 1;
    /* Keep him firing now and then, as he would. */
    if (!(leveltime % 20)) { P_NoiseAlert(player(), player()); round_past(coverer); }
}

static void check_cover(void)
{
    if (coverer->mind != MIND_COVER && !hidden_tics) fail("under fire, the soldier did not take cover");
    if (hidden_tics < 10) fail("in cover, the soldier was never out of sight");
    printf("Cover: under fire it ducked out of sight (%d tics hidden)%s.\n", hidden_tics,
           seen_after_hiding ? " and peeked out again" : "");
    if (!seen_after_hiding) fail("in cover, the soldier never peeked");
    P_RemoveMobj(coverer);
}

/* Retreat: badly hurt and afraid, out of sight, away. */
static mobj_t *coward;
static fixed_t coward_from;
static void start_retreat(void)
{
    fixed_t x, y;
    if (!find_spot(player()->x, player()->y, 300, 700, 1, &x, &y)) fail("no spot in sight for a retreat");
    coward = soldier_at(x, y, R_PointToAngle2(x, y, player()->x, player()->y));
    P_SetMobjState(coward, coward->info->seestate);
    coward->target = player();
    coward->mind = MIND_HUNT;
    coward->squad = 10; coward->nerve = 5;
    coward->health = 8;
    coward_from = P_AproxDistance(x - player()->x, y - player()->y);
}

static void check_retreat(void)
{
    fixed_t now = P_AproxDistance(coward->x - player()->x, coward->y - player()->y);
    if (getenv("TACTICSDEBUG") || coward->mind != MIND_RETREAT || P_CheckSight(coward, player()) || now <= coward_from)
        printf("  coward mind %d health %d sees %d now %d from %d cover (%d,%d) at (%d,%d) state %d\n", coward->mind, coward->health,
               P_CheckSight(coward, player()), now >> FRACBITS, coward_from >> FRACBITS, coward->coverx >> FRACBITS,
               coward->covery >> FRACBITS, coward->x >> FRACBITS, coward->y >> FRACBITS, (int)(coward->state - states));
    if (coward->mind != MIND_RETREAT) fail("badly hurt and afraid, the soldier did not retreat");
    if (P_CheckSight(coward, player())) fail("retreating, the soldier did not get out of sight");
    printf("Retreat: hurt, it hid out of sight, %d units off (was %d).\n", now >> FRACBITS, coward_from >> FRACBITS);
    P_RemoveMobj(coward);
}

/* Ambush: friends heard dying, the player unseen: it holds by a doorway. */
static mobj_t *ambusher;
static fixed_t held_x, held_y;
static void start_ambush(void)
{
    fixed_t x, y;
    if (!find_spot(player()->x, player()->y, 900, 1600, 0, &x, &y)) fail("no hidden spot for an ambush");
    ambusher = soldier_at(x, y, 0);
    P_SetMobjState(ambusher, ambusher->info->seestate);
    ambusher->target = player();
    ambusher->squad = 11; ambusher->nerve = 30; ambusher->losses = 3; ambusher->fallen = 3;
    ambusher->mind = MIND_SEARCH;
    ambusher->searchtic = leveltime;
    /* Trouble heard near its own room, far from where the player is. */
    ambusher->knownx = x; ambusher->knowny = y; ambusher->knowntic = leveltime;
    ambusher->decidetic = leveltime;
    /* Not alone: a friend of the squad beside it, so it holds rather than regroups. */
    {
        mobj_t *friend = soldier_at(x + 48*FRACUNIT, y, 0);
        friend->squad = 11;
    }
}

static void check_ambush(void)
{
    if (ambusher->mind != MIND_AMBUSH)
    {
        printf("  ambusher mind %d losses %d nerve %d sees %d room %d at (%d,%d) state %d\n", ambusher->mind, ambusher->losses,
               ambusher->nerve, P_CheckSight(ambusher, player()), P_NavRoom(ambusher->subsector->sector - sectors),
               ambusher->x >> FRACBITS, ambusher->y >> FRACBITS, (int)(ambusher->state - states));
        fail("having heard its friends die, the soldier did not set an ambush");
    }
    held_x = ambusher->x; held_y = ambusher->y;
}

static void end_ambush(void)
{
    /* Its wait over: it searches, and does not set the same ambush again. */
    ambusher->tactictic -= 31*TICRATE;
}

static void check_no_second_ambush(void)
{
    if (ambusher->mind == MIND_AMBUSH) fail("its ambush over, the soldier set the same ambush again");
    printf("Ambush: its wait over, it went searching rather than lying in wait again.\n");
}

static void check_held(void)
{
    if (ambusher->mind != MIND_AMBUSH || P_AproxDistance(ambusher->x - held_x, ambusher->y - held_y) > 4*FRACUNIT)
    {
        printf("  ambusher mind %d tactic %d at (%d,%d) held (%d,%d) spot (%d,%d) sees %d since %d\n", ambusher->mind, ambusher->tactic,
               ambusher->x >> FRACBITS, ambusher->y >> FRACBITS, held_x >> FRACBITS, held_y >> FRACBITS,
               ambusher->coverx >> FRACBITS, ambusher->covery >> FRACBITS, P_CheckSight(ambusher, player()),
               leveltime - ambusher->tactictic);
        fail("in ambush, the soldier did not hold still");
    }
    printf("Ambush: losses heard, it took a spot by a doorway out of its view and held.\n");
}

/* A friend in the line of fire kneels. */
static mobj_t *shooter, *front;
static void start_kneel(void)
{
    fixed_t x, y;
    angle_t an;
    if (!find_spot(player()->x, player()->y, 400, 800, 1, &x, &y)) fail("no spot in sight to shoot from");
    an = R_PointToAngle2(x, y, player()->x, player()->y);
    shooter = soldier_at(x, y, an);
    shooter->tics = -1;	/* it does nothing on its own */
    shooter->target = player();
    shooter->squad = 12;
    front = soldier_at(x + FixedMul(96*FRACUNIT, finecosine[an >> ANGLETOFINESHIFT]),
                       y + FixedMul(96*FRACUNIT, finesine[an >> ANGLETOFINESHIFT]), an);
    front->tics = -1;	/* it stands where it is */
    front->squad = 12;
    if (!P_DerpAllyInTheWay(shooter)) fail("a friend in the line of fire was not noticed");
    if (!P_DerpKneeling(front) || front->height >= front->info->height) fail("the friend in the way did not kneel");
    printf("Kneel: the friend in front went down to %d units for the shot.\n", front->height >> FRACBITS);
    /* The shot, as the soldier aims it: over the kneeling friend. */
    {
        angle_t an2 = R_PointToAngle2(shooter->x, shooter->y, player()->x, player()->y);
        int before = front->health;
        P_LineAttack(shooter, an2, MISSILERANGE, P_DerpAim(shooter, an2), 1);
        if (front->health != before) fail("the shot hit the kneeling friend");
    }
    printf("Kneel: the shot went over it.\n");
}

static void check_stood(void)
{
    if (P_DerpKneeling(front) || front->height != front->info->height) fail("the kneeling friend never stood up");
    printf("Kneel: and stood up again after.\n");
    /* Standing, it is in the way: the same shot hits it. */
    {
        angle_t an2 = R_PointToAngle2(shooter->x, shooter->y, player()->x, player()->y);
        int before = front->health;
        P_LineAttack(shooter, an2, MISSILERANGE, P_DerpAim(shooter, an2), 1);
        if (front->health == before) fail("the shot passed a standing friend in the way");
    }
    printf("Kneel: standing, the same shot hits it.\n");
    derpparts.tactics = false;
    if (P_DerpAllyInTheWay(shooter)) fail("with tactics off, a friend in the way is still spared");
    derpparts.tactics = true;
    printf("Tactics off: no friend spared, as vanilla.\n");
}

void I_TestFrame(void)
{
    if (gamestate != GS_LEVEL || leveltime < 2) return;
    switch (stage)
    {
      case 0:
        if (!P_DerpAI() || !P_NavReady()) fail("DERP's monsters or their navigation are not on");
        players[0].cheats |= CF_GODMODE;
        start_deaths(); stage = 1; stagetic = leveltime; break;
      case 1:
        if (leveltime - stagetic < 2*TICRATE) break;
        check_scream(); stage = 10; stagetic = leveltime; break;
      case 10:
        if (leveltime - stagetic < 2*TICRATE) break;
        check_gibbing(); start_headshot(); stage = 2; stagetic = leveltime; break;
      case 2:
        if (leveltime - stagetic < 2*TICRATE) break;
        check_silence(); stage = 3; stagetic = leveltime; break;
      case 3:
        if (finder->alert == 2) { check_found(); start_cover(); stage = 4; stagetic = leveltime; }
        else if (leveltime - stagetic > 3*TICRATE) fail("the body in sight was never found");
        break;
      case 4:
        watch_cover();
        if (leveltime - stagetic >= 8*TICRATE) { check_cover(); start_retreat(); stage = 5; stagetic = leveltime; }
        break;
      case 5:
        /* At its hiding spot (which may be some way off), or out of time. */
        if ((leveltime - stagetic >= TICRATE && coward->mind == MIND_RETREAT
             && P_AproxDistance(coward->x - coward->coverx, coward->y - coward->covery) < 12*FRACUNIT)
            || leveltime - stagetic >= 9*TICRATE)
        { check_retreat(); start_ambush(); stage = 6; stagetic = leveltime; }
        break;
      case 6:
        if (leveltime - stagetic >= TICRATE) { check_ambush(); stage = 7; stagetic = leveltime; }
        break;
      case 7:
        if (ambusher->mind == MIND_AMBUSH && P_AproxDistance(ambusher->x - held_x, ambusher->y - held_y) < FRACUNIT
            && leveltime - stagetic > 4*TICRATE)
        { held_x = ambusher->x; held_y = ambusher->y; stage = 8; stagetic = leveltime; }
        else { held_x = ambusher->x; held_y = ambusher->y; }
        /* The doorway may be a long walk off. */
        if (leveltime - stagetic > 40*TICRATE)
        {
            printf("  ambusher mind %d at (%d,%d) spot (%d,%d)\n", ambusher->mind, ambusher->x >> FRACBITS, ambusher->y >> FRACBITS,
                   ambusher->coverx >> FRACBITS, ambusher->covery >> FRACBITS);
            fail("the ambusher never settled on its spot");
        }
        break;
      case 8:
        if (leveltime - stagetic >= 2*TICRATE) { check_held(); end_ambush(); stage = 11; stagetic = leveltime; }
        break;
      case 11:
        if (leveltime - stagetic >= 3*TICRATE) { check_no_second_ambush(); start_kneel(); stage = 9; stagetic = leveltime; }
        break;
      case 9:
        if (leveltime - stagetic >= 3*TICRATE) { check_stood(); puts("DERP tactics smoke test passed."); I_Quit(); }
        break;
    }
}

int main(int argc, char **argv)
{
    if (argc != 2) return 1;
    derpauthored = true;	/* Headshots are Authored's alone. */
    if (!SDL_Init(SDL_INIT_VIDEO)) return 1;
    char *args[16] = {"derp-tactics-smoke", "-iwad", argv[1], "-warp", "1", "1", "-skill", "3",
                      "-nomonsters", "-nomusic", "-nosound", "-derpai"};
    myargc = 12; myargv = args;
    D_DoomMain();
    return 0;
}
