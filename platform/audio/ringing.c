/* Ringing ears (Game_LibraryRinging: hitting an edge in the library's chasm). */
#include "ringing.h"

#include <math.h>

#define RING_HZ      4100.0f   /* tinnitus sits about here */
#define BEAT_HZ      2.3f      /* two close tones, beating slowly */
#define RING_LEVEL   0.05f
#define MUFFLED      0.85f     /* how much of the world goes at the loudest */
#define GLIDE        0.002f    /* per sample: the level follows the game's in a few hundred */

static float phase, beat, level;

void Ringing_Mix(float *buffer, int count, int rate, float target)
{
    if (target <= 0 && level < 1e-4f) { level = 0; return; }
    float step = 2 * 3.14159265f * RING_HZ / rate, beat_step = 2 * 3.14159265f * BEAT_HZ / rate;
    for (int i = 0; i < count; ++i)
    {
        level += (target - level) * GLIDE;
        float world = 1 - MUFFLED * level;
        float tone = RING_LEVEL * level * sinf(phase) * (0.75f + 0.25f * sinf(beat));
        buffer[2*i] = buffer[2*i] * world + tone;
        buffer[2*i+1] = buffer[2*i+1] * world + tone;
        phase += step;
        beat += beat_step;
        if (phase > 6.2831853f) phase -= 6.2831853f;
        if (beat > 6.2831853f) beat -= 6.2831853f;
    }
}
