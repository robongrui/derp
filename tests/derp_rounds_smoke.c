/* What each gun fires under DERP's rules (p_rounds.h, PLAN-weapons.md): the
   pattern's size, the trigger's drift, energy lost in flight, the slayer's
   third of a pellet, nine pellets from a shotgun guy and three from the
   Mastermind, rockets and plasma at their speed, the BFG's branches.
   doom-derp-rounds-smoke <iwad> */
#include <math.h>
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
#include "p_nav.h"
#include "p_rounds.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

void A_SPosAttack(mobj_t *actor);

static int stage;

static void fail(const char *what) { I_Error("DERP rounds smoke test: %s", what); }
static mobj_t *player(void) { return players[0].mo; }

static double degrees(angle_t a) { return (double)(int)a/11930464.71; }

static void check_pattern(void)
{
    int inside = 0, n = 4000;
    double up = 0;
    for (int i = 0; i < n; i++)
    {
        angle_t a = 0;
        fixed_t s = 0;
        double x, y;
        P_RoundScatter(ROUND_PELLET, &a, &s);
        x = degrees(a);
        y = atan((double)s/FRACUNIT)*57.29578;
        if (fabs(x) <= 5.61 && fabs(y) <= 2.81) ++inside;
        up += fabs(y);
    }
    printf("Pattern: %d%% of pellets inside 5.6 by 2.8 degrees, mean %.2f up or down.\n", inside*100/n, up/n);
    if (inside != n) fail("a pellet flew outside the pump's spread");
    if (up == 0) fail("pellets fly flat");
}

/* Mean size of the nth shot of a held trigger. */
static double held(int nth)
{
    double sum = 0;
    int was = leveltime;
    for (int i = 0; i < 1500; i++)
    {
        angle_t a = 0;
        fixed_t s = 0;
        leveltime += 400;	/* the drift faded */
        for (int k = 0; k < nth; k++) { a = 0; s = 0; P_RoundPlayerAim(player(), &a, &s); }
        sum += fabs(degrees(a));
    }
    leveltime = was;
    return sum/1500;
}

static void check_drift(void)
{
    double first, sixth;
    players[0].readyweapon = wp_chaingun;
    first = held(1); sixth = held(6);
    printf("Drift: chaingun's first round %.2f degrees off, sixth %.2f.\n", first, sixth);
    if (first > 0.3) fail("a first round is not accurate");
    if (sixth < 3*first) fail("a held trigger does not drift");
    players[0].readyweapon = wp_pistol;
}

static void check_numbers(void)
{
    if (P_RoundSpeed(ROUND_PISTOL) != 343*FRACUNIT) fail("a pistol round's speed");
    if (P_RoundDamage(ROUND_PISTOL, 10, 0) != 10) fail("a round lost energy before it flew");
    if (P_RoundDamage(ROUND_PELLET, 10, 1280*FRACUNIT) != 5) fail("a pellet at 40 m keeps half");
    if (P_RoundDamage(ROUND_PELLET, 10, 6000*FRACUNIT) != 5) fail("a pellet goes on losing");
    if (P_RoundDamage(ROUND_PISTOL, 0, 6000*FRACUNIT) != 0) fail("a harmless shot grew");
    if (P_RoundTaken(player(), NULL, ROUND_PELLET, 15) != 5) fail("the slayer takes a third of a pellet");
    if (P_RoundTaken(player(), NULL, ROUND_PISTOL, 15) != 15) fail("the slayer's third is for pellets");
    {
        mobj_t *z = P_SpawnMobj(player()->x, player()->y, ONFLOORZ, MT_POSSESSED);
        mobj_t *imp = P_SpawnMobj(player()->x, player()->y, ONFLOORZ, MT_TROOP);
        if (P_RoundTaken(z, player(), ROUND_PISTOL, 5) != 5) fail("a body shot killed outright");
        z->headtic = imp->headtic = leveltime;
        if (P_RoundTaken(z, player(), ROUND_PISTOL, 5) != z->health) fail("a pistol headshot did not kill a soldier");
        if (P_RoundTaken(z, player(), ROUND_ROTARY, 5) != 5) fail("a chaingun headshot killed outright");
        if (P_RoundTaken(z, z, ROUND_PISTOL, 5) != 5) fail("a monster's headshot killed outright");
        if (P_RoundTaken(imp, player(), ROUND_PISTOL, 5) != 5) fail("a pistol headshot killed a demon");
        P_RemoveMobj(z); P_RemoveMobj(imp);
    }
    printf("Numbers: speeds, energy, the slayer's third, a pistol headshot.\n");
}

static void check_shooters(void)
{
    angle_t an = player()->angle >> ANGLETOFINESHIFT;
    fixed_t x = player()->x + 128*finecosine[an], y = player()->y + 128*finesine[an];
    /* The spider behind him: nobody shoots through its own side. */
    mobj_t *guy = P_SpawnMobj(x, y, ONFLOORZ, MT_SHOTGUY), *spider = P_SpawnMobj(2*player()->x - x, 2*player()->y - y, ONFLOORZ, MT_SPIDER);
    int before;
    guy->target = spider->target = player();
    before = P_BulletCount();
    A_SPosAttack(guy);
    if (P_BulletCount() - before != PUMP_PELLETS) fail("a shotgun guy did not fire a full shell");
    before = P_BulletCount();
    A_SPosAttack(spider);
    if (P_BulletCount() - before != 3) fail("the Mastermind fired a shell");
    P_RemoveMobj(guy); P_RemoveMobj(spider);
    printf("Shooters: nine pellets from a shotgun guy, three rounds from the Mastermind.\n");
}

/* A round stops in a living body; the next, through the one just killed, keeps
   a quarter for what stands behind. */
static void check_through(void)
{
    angle_t an = player()->angle >> ANGLETOFINESHIFT;
    fixed_t x = player()->x + 96*finecosine[an], y = player()->y + 96*finesine[an];
    fixed_t bx = player()->x + 180*finecosine[an], by = player()->y + 180*finesine[an];
    mobj_t *front = P_SpawnMobj(x, y, ONFLOORZ, MT_POSSESSED), *back = P_SpawnMobj(bx, by, ONFLOORZ, MT_BRUISER);
    fixed_t z = front->z + 32*FRACUNIT;
    players[0].readyweapon = wp_pistol;
    front->health = 1000; back->health = 1000;
    P_FireBulletAt(player(), x, y, z, 40);
    if (front->health != 960 || back->health != 1000) fail("a living body did not stop a round");
    front->health = 40;
    P_FireBulletAt(player(), x, y, z, 40);
    if (front->health > 0) fail("the round did not kill the one in front");
    if (back->health != 1000) fail("a round went on after killing");
    P_FireBulletAt(player(), x, y, z, 40);
    printf("Through: the one behind took %d of 40 past the body.\n", 1000 - back->health);
    if (back->health != 990) fail("a round through a fresh body did not keep a quarter");
    P_RemoveMobj(front); P_RemoveMobj(back);
}

static void check_missiles(void)
{
    angle_t an = player()->angle >> ANGLETOFINESHIFT;
    mobj_t *mark = P_SpawnMobj(player()->x + 96*finecosine[an], player()->y + 96*finesine[an], ONFLOORZ, MT_POSSESSED);
    mobj_t *rocket = P_SpawnMissile(player(), mark, MT_ROCKET), *plasma;
    fixed_t r = P_AproxDistance(rocket->momx, rocket->momy);
    P_RemoveMobj(rocket);
    plasma = P_SpawnMissile(player(), mark, MT_PLASMA);
    if (r < 26*FRACUNIT || r > 34*FRACUNIT) fail("a rocket's speed");
    r = P_AproxDistance(plasma->momx, plasma->momy);
    P_RemoveMobj(plasma);
    P_RemoveMobj(mark);
    if (r < 26*FRACUNIT || r > 34*FRACUNIT) fail("a plasma bolt's speed");
    printf("Missiles: rockets and plasma at 30.\n");
}

static void check_spray(void)
{
    angle_t an = player()->angle >> ANGLETOFINESHIFT;
    fixed_t x = player()->x + 96*finecosine[an], y = player()->y + 96*finesine[an];
    mobj_t *ball = P_SpawnMobj(x, y, player()->z + 32*FRACUNIT, MT_BFG);
    mobj_t *a = P_SpawnMobj(x + 64*FRACUNIT, y, ONFLOORZ, MT_BRUISER), *b = P_SpawnMobj(x - 64*FRACUNIT, y, ONFLOORZ, MT_BRUISER);
    int health = player()->health, lost, sharers = 0;
    a->health = b->health = 100000;
    ball->target = player();
    /* Whatever else it sees shares too: the room's barrels. */
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next)
    {
        mobj_t *mo = (mobj_t *)th;
        if (th->function.acp1 == (actionf_p1)P_MobjThinker && (mo->flags & MF_SHOOTABLE) && !mo->player
            && P_AproxDistance(mo->x - x, mo->y - y) <= 1024*FRACUNIT + mo->radius && P_CheckSight(ball, mo))
            ++sharers;
    }
    P_RoundSpray(ball);
    lost = 200000 - a->health - b->health;
    printf("Spray: %d shared by two (%d, %d); he lost %d.\n", lost, 100000 - a->health, 100000 - b->health, health - player()->health);
    printf("Spray: %d shared it in all.\n", sharers);
    if (lost/2*sharers < 2000 || lost/2*sharers > 3500) fail("the branches did not share stock's yield");
    if (a->health != b->health) fail("the branches did not share it evenly");
    if (player()->health != health) fail("the branches hurt him");
    P_RemoveMobj(ball); P_RemoveMobj(a); P_RemoveMobj(b);
}

void I_TestFrame(void)
{
    if (gamestate != GS_LEVEL || leveltime < 2) return;
    switch (stage)
    {
      case 0:
        if (leveltime < TICRATE) break;
        if (!P_DerpAI() || !P_BulletsTravel()) fail("the bullets do not travel");
        players[0].cheats |= CF_GODMODE;
        check_pattern(); check_drift(); check_numbers(); check_shooters(); check_through(); check_missiles(); check_spray();
        puts("DERP rounds smoke test passed.");
        I_Quit();
        break;
    }
}

int main(int argc, char **argv)
{
    if (argc != 2) return 1;
    if (!SDL_Init(SDL_INIT_VIDEO)) return 1;
    char *args[16] = {"derp-rounds-smoke", "-iwad", argv[1], "-warp", "1", "1", "-skill", "3",
                      "-nomonsters", "-nomusic", "-nosound", "-derpai"};
    myargc = 12; myargv = args;
    D_DoomMain();
    return 0;
}
