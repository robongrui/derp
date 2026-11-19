/* The voice pack (p_barks.c, p_chatter.c): the new lumps are found under their names,
   a soldier at rest near him chats, and word of a fight stops it. The pack is a separate
   download: without BARKS_WAD this says so and passes.
   doom-derp-voices-smoke <iwad> */
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
#include "p_barks.h"
#include "p_chatter.h"
#include "p_derp.h"
#include "p_nav.h"
#include "w_wad.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#define U(n) ((n)*FRACUNIT)

static int stage, stagetic;
static void fail(const char *what) { I_Error("DERP voices smoke test: %s", what); }
static mobj_t *player(void) { return players[0].mo; }

static void move_to(mobj_t *mo, fixed_t x, fixed_t y)
{
    P_UnsetThingPosition(mo);
    mo->x = x; mo->y = y;
    P_SetThingPosition(mo);
    mo->z = mo->floorz = mo->subsector->sector->floorheight;
    mo->ceilingz = mo->subsector->sector->ceilingheight;
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
    fail("a soldier is not where the map puts it");
    return NULL;
}

void I_TestFrame(void)
{
    static const char *lumps[] = { "BKA841", "BKB991", "BKAA01", "BKCA510", "BKBA91", "BKDC11", "BKAA01A" };
    if (gamestate != GS_LEVEL || leveltime < 2) return;
    switch (stage)
    {
      case 0:
        if (!getenv("BARKS_WAD"))
        {
            puts("No voice pack here: nothing to test.");
            I_Quit();
        }
        for (size_t i = 0; i < sizeof lumps/sizeof *lumps; i++)
            if (W_CheckNumForName((char *)lumps[i]) < 0)
            {
                printf("missing %s\n", lumps[i]);
                fail("a line of the pack is not under the name the game asks for");
            }
        players[0].cheats |= CF_GODMODE;
        /* West of the zigzag room's door, shut: he hears them, they do not see him. */
        move_to(player(), U(1480), U(-2496));
        stage = 1; stagetic = leveltime;
        break;
      case 1:
        player()->momx = player()->momy = 0;
        if (P_ChatterSaid())
        {
            printf("First line after %d tics.\n", leveltime - stagetic);
            stage = 2; stagetic = leveltime;
        }
        if (leveltime - stagetic > 60*TICRATE) fail("a minute near soldiers at rest and nobody spoke");
        break;
      case 2:
        {
            /* Who was talking, from what the first line left: its own earshot is clear now. */
            P_ChatterStop();
            mobj_t *west = at(-192, -3296), *beside = at(-160, -3232), *east = at(3008, -3520);
            if (P_DerpSpeaking(west) && P_DerpSpeaking(east)) fail("word came and somebody went on talking");
            if (!P_DerpBark(west, BARK_NOISE)) fail("a soldier with nobody near it could not speak");
            if (!P_DerpBark(east, BARK_NOTHING)) fail("a voice across the map stopped one here");
            if (P_DerpBark(beside, BARK_LOSTYOU)) fail("two beside each other talked at once");
        }
        puts("DERP voices smoke test passed.");
        I_Quit();
        break;
    }
}

int main(int argc, char **argv)
{
    if (argc != 2) return 1;
    if (!SDL_Init(SDL_INIT_VIDEO)) return 1;
    char *args[16] = {"derp-voices-smoke", "-iwad", argv[1], "-warp", "1", "1", "-skill", "4",
                      "-nomusic", "-nosound", "-derpai"};
    myargc = 11; myargv = args;
    derpauthored = true;	/* Where the voices live: not left out of it. */
    /* The spoken lines, and the cries (the imps' among the lumps above) from derp-cries.wad. */
    if (getenv("BARKS_WAD"))
    {
        args[myargc++] = "-file"; args[myargc++] = getenv("BARKS_WAD");
        if (getenv("CRIES_WAD")) args[myargc++] = getenv("CRIES_WAD");
    }
    if (getenv("VOICESLOG")) { args[myargc++] = "-derplog"; args[myargc++] = getenv("VOICESLOG"); }
    D_DoomMain();
    return 0;
}
