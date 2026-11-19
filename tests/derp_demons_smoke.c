/* DERP's demons (p_demons.h) on a real level: an imp up close swipes at
   once, in flurries, shoving the player, and now and then leaps away out
   of sight; from a few metres it pounces and strikes; its fireball leads a
   moving player; badly hurt it hides and heals there to three quarters,
   then comes back; a demon in plain sight and a soldier do not heal;
   nothing of it with the demons off.
   doom-derp-demons-smoke <iwad> */
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
#include "p_demons.h"
#include "p_nav.h"
#include "p_senses.h"
#include "p_tactics.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

static int stage, stagetic;
static void fail(const char *what) { I_Error("DERP demons smoke test: %s", what); }
static mobj_t *player(void) { return players[0].mo; }

static void move_to(mobj_t *mo, fixed_t x, fixed_t y)
{
    P_UnsetThingPosition(mo);
    mo->x = x; mo->y = y;
    P_SetThingPosition(mo);
    mo->z = mo->floorz = mo->subsector->sector->floorheight;
    mo->momx = mo->momy = mo->momz = 0;
}

/* A standable spot lo-hi units from (sx, sy), seen or not by the player as
   asked (-1 either), with a route from (sx, sy), and at least room units
   from its floor to its ceiling. */
static boolean find_room(fixed_t sx, fixed_t sy, int lo, int hi, int sight, int room, fixed_t *x, fixed_t *y)
{
    int nav = P_NavClassFor(20, 56), cells = P_NavCells();
    static int path[2];
    mobj_t *probe = NULL;
    for (int c = 0; c < cells; c++)
    {
        fixed_t px, py, f, d;
        if (!P_NavStandable(nav, c) || P_NavDoor(nav, c)) continue;
        P_NavPoint(nav, c, &px, &py, &f);
        d = P_AproxDistance(px - sx, py - sy);
        if (d < lo*FRACUNIT || d > hi*FRACUNIT) continue;
        if (!probe) probe = P_SpawnMobj(px, py, ONFLOORZ, MT_TROOP);
        move_to(probe, px, py);
        if (probe->subsector->sector->ceilingheight - probe->subsector->sector->floorheight < room*FRACUNIT) continue;
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

static boolean find_spot(fixed_t sx, fixed_t sy, int lo, int hi, int sight, fixed_t *x, fixed_t *y)
{
    return find_room(sx, sy, lo, hi, sight, 0, x, y);
}

/* An imp after the player, from (x, y). */
static mobj_t *hunter_at(fixed_t x, fixed_t y)
{
    mobj_t *mo = P_SpawnMobj(x, y, ONFLOORZ, MT_TROOP);
    mo->spawnpoint.x = x >> FRACBITS;
    mo->spawnpoint.y = y >> FRACBITS;
    mo->spawnpoint.type = 3001;
    mo->angle = R_PointToAngle2(x, y, player()->x, player()->y);
    mo->target = player();
    mo->mind = MIND_HUNT;
    mo->knownx = player()->x; mo->knowny = player()->y; mo->knowntic = leveltime;
    P_SetMobjState(mo, mo->info->seestate);
    return mo;
}

/* Up close: swipes, flurries, shoves, a leap away. */
static mobj_t *imp;
static fixed_t closex, closey, homex, homey;
static int firststrike, maxcombo, maxshove, fled, fledhidden;

/* Somewhere with a hiding place near: in a wide open room there is none,
   and an imp stays and fights. The player there, the imp beside him. */
static void start_close(void)
{
    int nav = P_NavClassFor(20, 56), cells = P_NavCells();
    fixed_t hx, hy;
    for (int c = 0; c < cells; c += 7)
    {
        fixed_t px, py, f;
        if (!P_NavStandable(nav, c) || P_NavDoor(nav, c)) continue;
        P_NavPoint(nav, c, &px, &py, &f);
        move_to(player(), px, py);
        if (!P_CheckPosition(player(), px, py)) continue;
        if (!find_spot(px, py, 40, 50, 1, &closex, &closey)) continue;
        imp = hunter_at(closex, closey);
        if (P_DerpHideSpot(imp, player(), true, &hx, &hy)) break;
        P_RemoveMobj(imp);
        imp = NULL;
    }
    if (!imp) fail("no spot beside the player with a hiding place near");
    homex = player()->x; homey = player()->y;
    imp->striketic = 0;
    firststrike = -1;
}

static void watch_close(void)
{
    fixed_t shove = P_AproxDistance(player()->momx, player()->momy);
    static int laststrike;
    if (getenv("DEMONSTRACE") && imp->striketic != laststrike)
        printf("tic %d strike, combo %d mind %d dist %d z %d\n", leveltime, imp->combo, imp->mind,
               P_AproxDistance(imp->x - player()->x, imp->y - player()->y) >> FRACBITS, (imp->z - imp->floorz) >> FRACBITS);
    laststrike = imp->striketic;
    if (getenv("DEMONSTRACE") && imp->mind == MIND_FLANK && !(leveltime & 3))
        printf("tic %d flank: dist %d, to cover %d, seen %d, z %d, mom %d,%d state %d dir %d strides %d/%d at %d\n", leveltime,
               P_AproxDistance(imp->x - player()->x, imp->y - player()->y) >> FRACBITS,
               P_AproxDistance(imp->x - imp->coverx, imp->y - imp->covery) >> FRACBITS,
               (int)P_CheckSight(player(), imp), (imp->z - imp->floorz) >> FRACBITS, imp->momx >> FRACBITS, imp->momy >> FRACBITS,
               (int)(imp->state - states), imp->movedir, imp->strides, imp->stridetics, imp->stridetic);
    if (imp->striketic && firststrike < 0) firststrike = imp->striketic - stagetic;
    if (imp->combo > maxcombo) maxcombo = imp->combo;
    if (shove > maxshove) maxshove = shove;
    if (imp->mind == MIND_FLANK) fled = 1;
    if (imp->mind == MIND_FLANK && !P_CheckSight(player(), imp)) fledhidden = 1;
    /* Kept up close until it has shown a flurry and a leap away: he back
       where he stood once shoved off it, it back beside him. */
    if (imp->mind == MIND_FLANK || imp->z > imp->floorz || imp->pounce) return;
    if (P_AproxDistance(player()->x - homex, player()->y - homey) > 32*FRACUNIT
        || P_AproxDistance(imp->x - player()->x, imp->y - player()->y) > 96*FRACUNIT)
    {
        move_to(player(), homex, homey);
        move_to(imp, closex, closey);
    }
}

static void check_close(void)
{
    if (firststrike < 0) fail("up close, the imp never struck");
    /* Vanilla: 16 tics of wind-up from the chase step; DERP's: 8, a tell. */
    if (firststrike > 14) fail("up close, the imp's swipe still took vanilla's wind-up");
    if (maxcombo < 1) fail("up close, the imp never swiped twice in a row");
    /* A swipe's own thrust is a few units a tic; the shove more. */
    if (maxshove < 6*FRACUNIT) fail("the imp's swipes did not shove the player");
    if (!fled) fail("up close, the imp never leapt away after its blows");
    if (!fledhidden) fail("the imp's leap away never took it out of sight");
    printf("Close: first swipe %d tics in, flurry of %d, shove %d units a tic, leapt away out of sight.\n",
           firststrike, maxcombo + 1, maxshove >> FRACBITS);
}

/* The pounce: from a few metres, through the air, a strike. */
static fixed_t farx, fary;
static int pounced, aloft, pouncestruck;

static void start_pounce(void)
{
    fixed_t px, py;
    /* Out where the ceiling is high: under a low one a leap is short. */
    if (!find_room(player()->x, player()->y, 0, 4000, -1, 160, &px, &py)) fail("no room with a high ceiling");
    move_to(player(), px, py);
    if (!find_room(px, py, 180, 260, 1, 160, &farx, &fary)) fail("no spot a few metres off in sight");
    imp = hunter_at(farx, fary);
    imp->pouncetic = -1000;
    /* He looks away: no side-steps. */
    player()->angle = R_PointToAngle2(imp->x, imp->y, player()->x, player()->y);
}

static void watch_pounce(void)
{
    if (getenv("DEMONSTRACE") && imp->pounce)
        printf("tic %d pounce %d dist %d z %d floor %d mom %d,%d,%d state %d tics %d\n", leveltime, imp->pounce,
               P_AproxDistance(imp->x - player()->x, imp->y - player()->y) >> FRACBITS, imp->z >> FRACBITS,
               imp->floorz >> FRACBITS, imp->momx >> FRACBITS, imp->momy >> FRACBITS, imp->momz >> FRACBITS,
               (int)(imp->state - states), imp->tics);
    if (imp->pounce) pounced = 1;
    if (imp->pounce >= 2 && imp->z > imp->floorz) aloft = 1;
    if (imp->pounce == 3) pouncestruck = 1;
    /* Too near for a pounce and none yet: back a few metres off. */
    if (!imp->pounce && !pouncestruck && imp->z <= imp->floorz
        && P_AproxDistance(imp->x - player()->x, imp->y - player()->y) < 140*FRACUNIT)
    {
        move_to(imp, farx, fary);
        imp->mind = MIND_HUNT;
        P_SetMobjState(imp, imp->info->seestate);
    }
}

static void check_pounce(void)
{
    if (!pounced) fail("a few metres off, the imp never pounced");
    if (!aloft) fail("the imp's pounce never left the floor");
    if (!pouncestruck) fail("the imp's pounce never struck the player");
    printf("Pounce: a crouch, a leap and a strike in the air.\n");
}

/* A fireball leads a moving player. */
static void check_lead(void)
{
    fixed_t x, y;
    mobj_t *thrower, *ball;
    angle_t straight, off;
    if (!find_spot(player()->x, player()->y, 400, 700, 1, &x, &y)) fail("no spot far off in sight");
    thrower = hunter_at(x, y);
    straight = R_PointToAngle2(x, y, player()->x, player()->y);
    /* Running across its line of fire, to its left. */
    player()->momx = FixedMul(12*FRACUNIT, finecosine[(straight + ANG90) >> ANGLETOFINESHIFT]);
    player()->momy = FixedMul(12*FRACUNIT, finesine[(straight + ANG90) >> ANGLETOFINESHIFT]);
    ball = P_SpawnMissile(thrower, player(), MT_TROOPSHOT);
    if (!(ball->flags & MF_MISSILE)) fail("the test's fireball burst at once");
    P_DerpLead(thrower, ball);
    off = ball->angle - straight;
    if (off < ANG45/16 || off > ANG90) fail("the imp's fireball did not lead a running player");
    printf("Lead: %d degrees ahead of a player running across.\n", (int)((double)off*360/4294967296.0));
    P_RemoveMobj(ball);
    /* Off: as thrown, untouched. */
    derpparts.demons = false;
    ball = P_SpawnMissile(thrower, player(), MT_TROOPSHOT);
    straight = ball->angle;
    P_DerpLead(thrower, ball);
    if (ball->angle != straight) fail("with the demons off, a fireball still led");
    derpparts.demons = true;
    P_RemoveMobj(ball);
    P_RemoveMobj(thrower);
    player()->momx = player()->momy = 0;
}

/* Badly hurt at his side: away, out of sight, healing there, back. Beside
   it a demon in plain sight and a hidden soldier, both wounded, that do not
   heal, and a hidden pinky that heals to its three quarters (112 of 150)
   and stops there. */
static mobj_t *watched, *soldier, *pinky;
static int healhidden, healed_at, lowest;

static void start_heal(void)
{
    fixed_t x, y;
    if (!find_spot(player()->x, player()->y, 40, 60, 1, &x, &y)) fail("no spot beside him for the hurt imp");
    imp = hunter_at(x, y);
    imp->health = 15;
    imp->decidetic = leveltime;
    lowest = imp->health;
    /* Asleep in sight, its back to him: it never spots him. */
    if (!find_spot(player()->x, player()->y, 500, 900, 1, &x, &y)) fail("no spot in sight for the watched imp");
    watched = P_SpawnMobj(x, y, ONFLOORZ, MT_TROOP);
    watched->angle = R_PointToAngle2(player()->x, player()->y, x, y);
    watched->health = 20;
    if (!find_spot(player()->x, player()->y, 300, 1200, 0, &x, &y)) fail("no hidden spot for the soldier");
    soldier = P_SpawnMobj(x, y, ONFLOORZ, MT_POSSESSED);
    soldier->health = 10;
    if (!find_spot(player()->x, player()->y, 400, 1600, 0, &x, &y)) fail("no hidden spot for the pinky");
    pinky = P_SpawnMobj(x, y, ONFLOORZ, MT_SERGEANT);
    pinky->health = 50;
}

static void watch_heal(void)
{
    if (imp->mind == MIND_HEAL && !P_CheckSight(player(), imp)) healhidden = 1;
    if (imp->health < lowest) lowest = imp->health;
    if (imp->health > P_DerpHealth(MT_TROOP)*3/4) fail("the imp healed past three quarters");
    if (pinky->health > P_DerpHealth(MT_SERGEANT)*3/4) fail("the pinky healed past three quarters");
    if (!healed_at && imp->health >= P_DerpHealth(MT_TROOP)*3/4) healed_at = leveltime - stagetic;
}

static void check_heal(void)
{
    if (!healhidden) fail("badly hurt, the imp did not go out of sight to heal");
    if (!healed_at) fail("hidden, the hurt imp did not heal to three quarters");
    if (imp->mind == MIND_HEAL) fail("healed, the imp did not come back");
    if (watched->health > 20) fail("a wounded demon in plain sight healed");
    if (soldier->health > 10) fail("a wounded soldier healed");
    if (pinky->health != P_DerpHealth(MT_SERGEANT)*3/4) fail("a hidden pinky did not heal to its three quarters");
    printf("Heal: from beside him it hid, healed from %d to %d in %d s, came back; a pinky healed to %d;"
           " one in sight and a soldier did not heal.\n",
           lowest, P_DerpHealth(MT_TROOP)*3/4, healed_at/TICRATE, pinky->health);
    P_RemoveMobj(watched);
    P_RemoveMobj(soldier);
    P_RemoveMobj(pinky);
    P_RemoveMobj(imp);
}

/* Off: vanilla's claws. */
static void check_off(void)
{
    imp = hunter_at(closex, closey);
    derpparts.demons = false;
    if (P_DerpMelee(imp)) fail("with the demons off, an imp still swiped its way");
    P_DerpClawed(imp);
    if (imp->striketic || imp->combo) fail("with the demons off, a claw still flurried");
    derpparts.demons = true;
    P_RemoveMobj(imp);
    printf("Demons off: vanilla's claws.\n");
}

void I_TestFrame(void)
{
    if (gamestate != GS_LEVEL || leveltime < 2) return;
    switch (stage)
    {
      case 0:
        if (!P_DerpAI() || !P_NavReady()) fail("DERP's monsters or their navigation are not on");
        players[0].cheats |= CF_GODMODE;
        start_close(); stage = 1; stagetic = leveltime; break;
      case 1:
        watch_close();
        if (leveltime - stagetic < 20*TICRATE && !(fledhidden && maxcombo >= 1 && firststrike >= 0)) break;
        check_close();
        P_RemoveMobj(imp);
        start_pounce(); stage = 2; stagetic = leveltime; break;
      case 2:
        watch_pounce();
        if (leveltime - stagetic < 25*TICRATE && !pouncestruck) break;
        check_pounce();
        P_RemoveMobj(imp);
        check_lead();
        start_heal(); stage = 3; stagetic = leveltime; break;
      case 3:
        watch_heal();
        if (leveltime - stagetic < 60*TICRATE
            && !(healed_at && imp->mind != MIND_HEAL && pinky->health >= P_DerpHealth(MT_SERGEANT)*3/4)) break;
        check_heal();
        check_off();
        puts("DERP demons smoke test passed.");
        I_Quit();
        break;
    }
}

int main(int argc, char **argv)
{
    if (argc != 2) return 1;
    if (!SDL_Init(SDL_INIT_VIDEO)) return 1;
    char *args[16] = {"derp-demons-smoke", "-iwad", argv[1], "-warp", "1", "1", "-skill", "3",
                      "-nomonsters", "-nomusic", "-nosound", "-derpai"};
    myargc = 12; myargv = args;
    D_DoomMain();
    return 0;
}
