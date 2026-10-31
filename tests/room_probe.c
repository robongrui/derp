/* Measures what the reverb hears at chosen spots of a level, without sound
   or a window, for comparing rooms before and after a change to room.c:

     doom-room-probe <iwad> [-file pwad] [-warp e m | -warp m] [-out dir] [-name prefix]
                     [-at name:x,y,facing]... [-walk name:x0,y0,x1,y1,steps,facing]...

   Facings are degrees, as map angles turn (90 faces north). Each spot is
   measured as Room_MeasureAt and Beyond_Measure give it to the mixer, and
   written to <out>/<prefix>-<name>.room: the room_t field by field, one
   "key value" line each, the echoes as "tap k delay gain spread dark pan",
   the floor plan's volume and ray reaches, then the space beyond.
   doom-sfx-render -room plays such a file. A walk measures steps spots
   from (x0, y0) to (x1, y1), each to <prefix>-<name>-<step>.room. One line
   per spot goes to the console. The game's config is <out>/probe.cfg,
   never the player's. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "doomdef.h"
#include "doomstat.h"
#include "d_main.h"
#include "i_system.h"
#include "m_argv.h"
#include "../platform/audio/beyond.h"
#include "../platform/audio/room.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#define MAX_SPOTS 1024

typedef struct { char name[32]; float x, y, facing; } spot_t;
static spot_t spots[MAX_SPOTS];
static int spot_count;
static const char *out_dir = ".", *prefix = "probe";

static void write_room(const char *name, const spot_t *s, const room_t *room, const room_outline_t *outline,
                       const beyond_t *beyond)
{
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s-%s.room", out_dir, prefix, name);
    FILE *f = fopen(path, "w");
    if (!f) I_Error("Room probe: can't write %s", path);
    fprintf(f, "x %g\ny %g\nfacing %g\n", s->x, s->y, s->facing);
    fprintf(f, "decay %.6f\ndamping %.6f\nscatter %.6f\nlate %.6f\n", room->decay, room->damping, room->scatter, room->late);
    fprintf(f, "tunnel %.6f\naxis %.6f\nwidth %.1f\nheight %.1f\nflutter_delay %.6f\nflutter %.6f\n",
            room->tunnel, room->axis, room->width, room->height, room->flutter_delay, room->flutter);
    for (int k = 0; k < ROOM_TAPS; ++k)
        fprintf(f, "tap %d %.6f %.6f %.6f %.6f %.6f\n", k, room->delay[k], room->gain[k], room->spread[k], room->dark[k], room->pan[k]);
    fprintf(f, "volume %.1f\nreach", outline->volume);
    for (int i = 0; i < ROOM_RAYS; ++i) fprintf(f, " %.0f", outline->reach[i]);
    fprintf(f, "\nbeyond_coupling %.6f\nbeyond_decay %.6f\nbeyond_late %.6f\nbeyond_volume %.1f\n",
            beyond->coupling, beyond->room.decay, beyond->room.late, beyond->outline.volume);
    fclose(f);
    char full[96];
    snprintf(full, sizeof(full), "%s-%s", prefix, name);
    printf("spot %-36s decay %.2f late %.2f damping %.2f scatter %.2f  tunnel %.2f width %4.0f flutter %.2f  beyond %.2f\n",
           full, room->decay, room->late, room->damping, room->scatter, room->tunnel, room->width, room->flutter,
           beyond->coupling);
}

static void measure(const char *name, const spot_t *s)
{
    room_t room;
    room_outline_t outline;
    beyond_t beyond;
    float facing = s->facing * (float)M_PI / 180;
    Room_MeasureAt(s->x, s->y, facing, &room, &outline);
    Beyond_Measure(&room, &outline, facing, &beyond);
    write_room(name, s, &room, &outline, &beyond);
}

void I_TestFrame(void)
{
    if (gamestate != GS_LEVEL || gametic < 2) return;
    for (int i = 0; i < spot_count; ++i) measure(spots[i].name, &spots[i]);
    /* The walks. */
    for (int i = 1; i < myargc - 1; ++i)
    {
        char name[32];
        float x0, y0, x1, y1, facing;
        int steps;
        if (strcmp(myargv[i], "-walk")
            || sscanf(myargv[i + 1], "%31[^:]:%f,%f,%f,%f,%d,%f", name, &x0, &y0, &x1, &y1, &steps, &facing) != 7)
            continue;
        for (int n = 0; n < steps; ++n)
        {
            float t = steps > 1 ? n / (float)(steps - 1) : 0;
            spot_t s = { "", x0 + (x1 - x0) * t, y0 + (y1 - y0) * t, facing };
            snprintf(s.name, sizeof(s.name), "%s-%02d", name, n);
            measure(s.name, &s);
        }
    }
    fflush(stdout);
    I_Quit();
}

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: %s <iwad> [-file pwad] [-warp ...] [-out dir] [-name prefix] [-at name:x,y,facing]... [-walk name:x0,y0,x1,y1,steps,facing]...\n", argv[0]); return 2; }
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
    if (!SDL_Init(SDL_INIT_VIDEO)) return 1;
    static char *args[256], config[1024];
    int count = 0;
    args[count++] = "probe"; args[count++] = "-iwad"; args[count++] = argv[1];
    for (int i = 2; i < argc; ++i)
    {
        if (!strcmp(argv[i], "-out") && i + 1 < argc) out_dir = argv[++i];
        else if (!strcmp(argv[i], "-name") && i + 1 < argc) prefix = argv[++i];
        else if (!strcmp(argv[i], "-at") && i + 1 < argc)
        {
            spot_t *s = &spots[spot_count];
            if (spot_count >= MAX_SPOTS || sscanf(argv[++i], "%31[^:]:%f,%f,%f", s->name, &s->x, &s->y, &s->facing) != 4)
            { fprintf(stderr, "Bad spot %s\n", argv[i]); return 2; }
            ++spot_count;
        }
        else if (count < 240) args[count++] = argv[i];
    }
    snprintf(config, sizeof(config), "%s/probe.cfg", out_dir);
    args[count++] = "-config"; args[count++] = config;
    args[count++] = "-nosound"; args[count++] = "-nomusic"; args[count++] = "-nomonsters";
    args[count] = NULL;
    myargc = count; myargv = args;
    D_DoomMain();
    return 0;
}
