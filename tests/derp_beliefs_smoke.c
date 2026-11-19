/* DERP's monsters act on what they believe, not on where the player is
   (p_senses.c, p_tactics.c) on a real level: shot by a player it sees, a
   zombie knows where he is; shot from out of its sight far off, it knows
   only roughly, the rougher the farther; and a soldier out of sight hides
   from where it thinks he is.
   doom-derp-beliefs-smoke <iwad> */
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
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

static void fail(const char *what) { I_Error("DERP beliefs smoke test: %s", what); }
static mobj_t *player(void) { return players[0].mo; }

static void move_to(mobj_t *mo, fixed_t x, fixed_t y)
{
    P_UnsetThingPosition(mo);
    mo->x = x; mo->y = y;
    P_SetThingPosition(mo);
    mo->z = mo->floorz = mo->subsector->sector->floorheight;
    mo->momx = mo->momy = mo->momz = 0;
}

/* A standable spot lo-hi units from the player, seen by him or not as asked. */
static boolean find_spot(int lo, int hi, int sight, fixed_t *x, fixed_t *y)
{
    int nav = P_NavClassFor(20, 56), cells = P_NavCells();
    mobj_t *probe = NULL;
    for (int c = 0; c < cells; c++)
    {
        fixed_t px, py, f, d;
        if (!P_NavStandable(nav, c) || P_NavDoor(nav, c)) continue;
        P_NavPoint(nav, c, &px, &py, &f);
        d = P_AproxDistance(px - player()->x, py - player()->y);
        if (d < lo*FRACUNIT || d > hi*FRACUNIT) continue;
        if (!probe) probe = P_SpawnMobj(px, py, ONFLOORZ, MT_POSSESSED);
        move_to(probe, px, py);
        if (!P_CheckPosition(probe, px, py) || (int)P_CheckSight(probe, player()) != sight) continue;
        *x = px; *y = py;
        P_RemoveMobj(probe);
        return true;
    }
    if (probe) P_RemoveMobj(probe);
    return false;
}

static void check(void)
{
    fixed_t x, y, off, d;
    mobj_t *seen, *hidden;
    int rough = 0;

    if (!find_spot(300, 600, 1, &x, &y)) fail("no spot in sight");
    seen = P_SpawnMobj(x, y, ONFLOORZ, MT_POSSESSED);
    seen->health = 1000;
    P_DamageMobj(seen, player(), player(), 1);
    if (seen->knownx != player()->x || seen->knowny != player()->y)
        fail("shot by a player it sees, a zombie did not know where he is");

    if (!find_spot(800, 1400, 0, &x, &y)) fail("no spot out of sight far off");
    hidden = P_SpawnMobj(x, y, ONFLOORZ, MT_POSSESSED);
    hidden->health = 1000;
    d = P_AproxDistance(x - player()->x, y - player()->y);
    /* Several shots: the guesses are rough, never far beyond a quarter of the distance. */
    for (int i = 0; i < 8; i++)
    {
        P_DamageMobj(hidden, player(), player(), 1);
        off = P_AproxDistance(hidden->knownx - player()->x, hidden->knowny - player()->y);
        if (off > d/4 + d/8) fail("shot from out of sight, a zombie's guess was wild");
        if (off > 16*FRACUNIT) rough++;
    }
    if (!rough) fail("shot from out of sight far off, a zombie knew exactly where he is");
    printf("Beliefs: seen, exact; unseen at %d units, %d of 8 guesses rough (at most a quarter off).\n",
           d >> FRACBITS, rough);

    /* Out of sight, it hides from where it thinks he is: the spot it finds
       is out of sight of that place (a stand-in there, half his height). */
    {
        fixed_t hx, hy, bx, by;
        mobj_t *at, *belief;
        if (!find_spot(200, 700, 1, &bx, &by)) fail("no place for the belief");
        hidden->knownx = bx; hidden->knowny = by;
        if (!P_DerpHideFrom(hidden, false, false, &hx, &hy)) fail("no hiding place from where it thinks he is");
        belief = P_SpawnMobj(bx, by, ONFLOORZ, MT_POSSESSED);
        at = P_SpawnMobj(hx, hy, ONFLOORZ, MT_POSSESSED);
        belief->height = 28*FRACUNIT;
        if (P_CheckSight(at, belief)) fail("out of sight, a monster hid where it would be seen from where it thinks he is");
        P_RemoveMobj(at);
        P_RemoveMobj(belief);
        printf("Beliefs: out of sight it hides from where it thinks he is.\n");
    }
    P_RemoveMobj(seen);
    P_RemoveMobj(hidden);
}

void I_TestFrame(void)
{
    static int stage;
    if (gamestate != GS_LEVEL || leveltime < 2 || stage) return;
    stage = 1;
    if (!P_DerpAI() || !P_NavReady()) fail("DERP's monsters or their navigation are not on");
    players[0].cheats |= CF_GODMODE;
    check();
    puts("DERP beliefs smoke test passed.");
    I_Quit();
}

int main(int argc, char **argv)
{
    if (argc != 2) return 1;
    if (!SDL_Init(SDL_INIT_VIDEO)) return 1;
    char *args[16] = {"derp-beliefs-smoke", "-iwad", argv[1], "-warp", "1", "1", "-skill", "3",
                      "-nomonsters", "-nomusic", "-nosound", "-derpai"};
    myargc = 12; myargv = args;
    D_DoomMain();
    return 0;
}
