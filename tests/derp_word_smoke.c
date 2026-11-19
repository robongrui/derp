/* Word round a squad (p_word.c), on E1M1 as a played run had it. One of the zigzag room
   goes off to look and those in earshot know; a friend of theirs hears gunfire far off
   and the word goes round: those told get ready though the gun is too far for them to
   hear, those too far to be told sleep on. Then the director turns to press him and
   one ready goes in.
   doom-derp-word-smoke <iwad> */
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
#include "p_bullets.h"
#include "p_derp.h"
#include "p_director.h"
#include "p_fire.h"
#include "p_nav.h"
#include "p_portals.h"
#include "p_senses.h"
#include "p_tactics.h"
#include "p_word.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#define U(n) ((n)*FRACUNIT)

static int stage, stagetic;
static mobj_t *leaver, *hearer, *told, *distant, *gunner, *behind, *gunleaver, *flanker;
static void fail(const char *what) { I_Error("DERP word smoke test: %s", what); }
static mobj_t *player(void) { return players[0].mo; }

static void move(mobj_t *mo, fixed_t x, fixed_t y)
{
    P_UnsetThingPosition(mo);
    mo->x = x; mo->y = y;
    P_SetThingPosition(mo);
    mo->z = mo->floorz = mo->subsector->sector->floorheight;
    mo->momx = mo->momy = mo->momz = 0;
}

/* The soldier the map put at (x, y). */
static mobj_t *at(int x, int y)
{
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next)
    {
        mobj_t *mo = (mobj_t *)th;
        if (th->function.acp1 == (actionf_p1)P_MobjThinker && P_DerpSoldier(mo->type)
            && mo->spawnpoint.x == x && mo->spawnpoint.y == y) return mo;
    }
    fail("a soldier of the zigzag room is not where the map puts it");
    return NULL;
}

static void fire_on_him(void)
{
    if (!gunner)
    {
        gunner = P_SpawnMobj(player()->x + U(96), player()->y, ONFLOORZ, MT_POSSESSED);
        gunner->flags &= ~(MF_SOLID|MF_SHOOTABLE);
    }
    if (!(leveltime % 6))
        P_FireBulletAt(gunner, player()->x + U(48), player()->y + U(48), player()->z + U(32), 0);
}

void I_TestFrame(void)
{
    if (gamestate != GS_LEVEL || leveltime < 2) return;
    switch (stage)
    {
      case 0:
        if (!P_DerpAI() || !P_NavReady()) fail("DERP's monsters or their navigation are not on");
        players[0].cheats |= CF_GODMODE;
        leaver = at(1696, -2688);
        hearer = at(1920, -2176);
        told = at(2272, -2432);
        distant = at(3008, -3520);
        flanker = at(2464, -2432);
        /* Off to look at something else: no word of a fight yet. */
        P_WordOut(leaver, false, 0, 0);
        stage = 1; stagetic = leveltime;
        break;
      case 1:
        if (leveltime - stagetic < 2*TICRATE) break;
        if (!hearer->wordtic || !told->wordtic) fail("those in earshot were not told");
        if (distant->wordtic) fail("one 1700 units off was told");
        printf("#%d goes: #%d and #%d know, #%d does not.\n", leaver->serial, hearer->serial, told->serial, distant->serial);
        if (told->wordgo || told->tactic == STEP_READIED) fail("it got ready with no fight to hear of");
        /* A gun, in earshot of one of them and not of the other. */
        P_DerpRoundHeard(hearer, leaver, false, U(941), U(-3133));
        stage = 2; stagetic = leveltime;
        break;
      case 2:
        if (told->mind == MIND_AMBUSH && flanker->mind == MIND_AMBUSH)
        {
            printf("#%d ready at (%d,%d) after %d tics, facing the fight; #%d at (%d,%d).\n", told->serial,
                   told->coverx >> FRACBITS, told->covery >> FRACBITS, leveltime - stagetic,
                   flanker->serial, flanker->coverx >> FRACBITS, flanker->covery >> FRACBITS);
            /* Beside the way he comes in by, not across the room. */
            if (abs((flanker->coverx >> FRACBITS) - 1696) > 64 || abs((flanker->covery >> FRACBITS) + 2496) > 240)
                fail("nobody took the side of the door");
            if (distant->mind != MIND_NONE || distant->tactic == STEP_READIED) fail("the word got to one too far off");
            stage = 3; stagetic = leveltime;
            break;
        }
        if (leveltime - stagetic > 6*TICRATE) fail("told of a fight, it never took a post");
        break;
      case 3:
        P_DirectorStance(true);
        if (!told->guard) fire_on_him();
        if (told->guard) { puts("(It guards something: it stays.)"); puts("DERP word smoke test passed."); I_Quit(); }
        if (told->mind == MIND_INVESTIGATE)
        {
            printf("Director presses: #%d goes in after %d tics.\n", told->serial, leveltime - stagetic);
            /* His bullet into a wall, a zombie the far side of it: it hears the strike. */
            behind = P_SpawnMobj(U(2000), U(-2980), ONFLOORZ, MT_POSSESSED);
            move(player(), U(2000), U(-2700));
            P_FireBulletAt(player(), U(2000), U(-2900), player()->z + U(36), 0);
            stage = 4; stagetic = leveltime;
            break;
        }
        if (leveltime - stagetic > 15*TICRATE) fail("the director pressed and the one ready stayed");
        break;
      case 4:
        if (behind->mind != MIND_NONE)
        {
            printf("The strike heard through the wall after %d tics.\n", leveltime - stagetic);
            /* One goes for gunfire: that is news of the fight, no second gun needed. */
            gunleaver = at(2976, -3264);
            P_DerpRoundHeard(gunleaver, player(), true, gunleaver->x + U(300), gunleaver->y);
            if (gunleaver->mind != MIND_INVESTIGATE) fail("the one near a shot did not go to look");
            stage = 5; stagetic = leveltime;
            break;
        }
        if (leveltime - stagetic > 2*TICRATE) fail("a bullet struck a wall and the one behind it heard nothing");
        break;
      case 5:
        if (distant->mind != MIND_NONE || distant->tactic == STEP_READIED)
        {
            printf("#%d left for gunfire, #%d ready after %d tics.\n", gunleaver->serial, distant->serial, leveltime - stagetic);
            /* Held back from an opening where one fell: it waits this side of the wall. */
            {
                mobj_t *held = P_SpawnMobj(U(2200), U(-2325), ONFLOORZ, MT_POSSESSED);
                mobj_t *fell = P_SpawnMobj(U(2148), U(-2524), ONFLOORZ, MT_POSSESSED);
                fixed_t wx, wy;
                held->serial = 66;
                P_DirectorStance(false);
                P_PortalDeath(fell);
                if (!P_PortalWait(held, U(2088), U(-2312), &wx, &wy)) fail("a soldier was not held back where one just fell");
                printf("Held back: it waits at (%d,%d).\n", wx >> FRACBITS, wy >> FRACBITS);
                if (wy > U(-2304)) fail("held back, it was sent to the far side of a wall");
            }
            puts("DERP word smoke test passed.");
            I_Quit();
        }
        if (leveltime - stagetic > 4*TICRATE) fail("one left for gunfire and those in earshot heard nothing of it");
        break;
    }
}

int main(int argc, char **argv)
{
    if (argc != 2) return 1;
    if (!SDL_Init(SDL_INIT_VIDEO)) return 1;
    char *args[16] = {"derp-word-smoke", "-iwad", argv[1], "-warp", "1", "1", "-skill", "4",
                      "-nomusic", "-nosound", "-derpai"};
    myargc = 11; myargv = args;
    if (getenv("WORDLOG")) { args[11] = "-derplog"; args[12] = getenv("WORDLOG"); myargc = 13; }
    D_DoomMain();
    return 0;
}
