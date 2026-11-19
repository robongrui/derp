/* Every cover the corners give a map, with the map itself, as JSON for
   devtools/covers (not a test: nothing here can fail).
   doom-derp-covers-dump <iwad> <episode> <map> <out.json> */
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
#include "p_corners.h"
#include "p_tactics.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

static const char *out;
#define U(v) ((v)/65536.0)

void I_TestFrame(void)
{
    const corner_t *list;
    FILE *f;
    int n, i, nav;

    if (gamestate != GS_LEVEL || leveltime < 2) return;
    if (!P_DerpAI() || !P_NavReady()) I_Error("covers dump: DERP's navigation is not on");
    f = fopen(out, "w");
    if (!f) I_Error("covers dump: cannot write %s", out);
    nav = P_NavClassFor(20, 56);
    fprintf(f, "{\"sectors\":[");
    for (i = 0; i < numsectors; i++)
        fprintf(f, "%s[%g,%g,%d,%d]", i ? "," : "", U(sectors[i].floorheight), U(sectors[i].ceilingheight),
                sectors[i].special, P_NavRoom(i));
    fprintf(f, "],\"lines\":[");
    for (i = 0; i < numlines; i++)
    {
        const line_t *l = &lines[i];
        fprintf(f, "%s[%g,%g,%g,%g,%d,%d,%d]", i ? "," : "", U(l->v1->x), U(l->v1->y), U(l->v2->x), U(l->v2->y),
                (int)(l->frontsector - sectors), l->backsector ? (int)(l->backsector - sectors) : -1, l->flags);
    }
    fprintf(f, "],\"things\":[");
    {
        thinker_t *th;
        int first = 1;
        for (th = thinkercap.next; th != &thinkercap; th = th->next)
        {
            mobj_t *mo = (mobj_t *)th;
            if (th->function.acp1 != (actionf_p1)P_MobjThinker || !(mo->flags & MF_COUNTKILL)) continue;
            fprintf(f, "%s[%g,%g,%d]", first ? "" : ",", U(mo->x), U(mo->y), mo->type);
            first = 0;
        }
    }
    fprintf(f, "],\"start\":[%g,%g],\"corners\":[", U(players[0].mo->x), U(players[0].mo->y));
    n = P_Corners(&list);
    for (i = 0; i < n; i++)
    {
        const corner_t *c = &list[i];
        fixed_t px = 0, py = 0, pf = 0, ex = 0, ey = 0;
        int why = P_DerpCornerSpot(c, nav, 24*FRACUNIT, &px, &py, &pf, &ex, &ey);
        fprintf(f, "%s[%g,%g,%g,%g,%g,%g,%g,%g,%g,%g,%g,%d]", i ? "," : "", U(c->x), U(c->y), U(c->dx), U(c->dy),
                U(c->nx), U(c->ny), U(px), U(py), U(pf), U(ex), U(ey), why);
    }
    fprintf(f, "]}\n");
    fclose(f);
    printf("%d corners to %s\n", n, out);
    I_Quit();
}

int main(int argc, char **argv)
{
    if (argc != 5) return 1;
    if (!SDL_Init(SDL_INIT_VIDEO)) return 1;
    out = argv[4];
    char *args[16] = {"derp-covers-dump", "-iwad", argv[1], "-warp", argv[2], argv[3], "-skill", "4",
                      "-nomusic", "-nosound", "-derpai"};
    myargc = 11; myargv = args;
    D_DoomMain();
    return 0;
}
