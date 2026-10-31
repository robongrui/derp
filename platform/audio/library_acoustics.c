/* The library's acoustics (episode 0.5). See library_acoustics.h. */
#include <string.h>
#include "library_acoustics.h"
#include "../game_profile.h"

/* Among the books: what the measured room keeps. */
#define BOOKS_DECAY    0.5f   /* of its tail's length */
#define BOOKS_MOST     0.75f  /* seconds, at the longest */
#define BOOKS_LATE     0.45f  /* of its tail's level */
#define BOOKS_ECHO     0.55f  /* of each echo's level */
#define BOOKS_DARK     0.85f  /* at least this dark: paper and wood take the treble */

/* The abyss. */
#define ABYSS_DECAY    7.0f   /* seconds */
#define ABYSS_LATE     0.6f
#define ABYSS_DARK     0.8f
#define ABYSS_COUPLING 0.85f  /* at the railing, of the most a space beyond is heard */

static float atLeast(float v, float least) { return v < least ? least : v; }

void LibraryAcoustics_Shape(room_t *room, beyond_t *beyond)
{
    if (!Game_InLibrary()) return;
    float side, open = Game_LibraryChasm(&side);
    room->decay *= BOOKS_DECAY;
    room->room_decay *= BOOKS_DECAY;
    if (room->decay > BOOKS_MOST) room->decay = BOOKS_MOST;
    if (room->room_decay > BOOKS_MOST) room->room_decay = BOOKS_MOST;
    room->late *= BOOKS_LATE;
    room->damping = atLeast(room->damping, BOOKS_DARK);
    room->scatter = atLeast(room->scatter, 0.85f);
    room->tunnel = room->flutter = 0;
    for (int k = 0; k < ROOM_TAPS; ++k)
    {
        room->gain[k] *= BOOKS_ECHO;
        room->dark[k] = atLeast(room->dark[k], BOOKS_DARK);
        room->spread[k] = atLeast(room->spread[k], 0.7f);
    }
    /* The chasm: echoes off the far side and the storeys under it, ever
       fainter and smeared, from its side; then the long tail. */
    room_t *abyss = &beyond->room;
    memset(abyss, 0, sizeof(*abyss));
    static const float delays[3] = {0.08f, 0.19f, 0.36f}, gains[3] = {0.12f, 0.07f, 0.04f};
    for (int k = 0; k < 3; ++k)
    {
        abyss->delay[k] = delays[k];
        abyss->gain[k] = gains[k];
        abyss->spread[k] = 1;
        abyss->dark[k] = 0.9f;
        abyss->pan[k] = 0.7f * side;
    }
    abyss->decay = abyss->room_decay = ABYSS_DECAY;
    abyss->damping = ABYSS_DARK;
    abyss->scatter = 1;
    abyss->late = ABYSS_LATE;
    abyss->width = 384;
    abyss->height = 2048;
    beyond->coupling = ABYSS_COUPLING * open;
}
