/* The air rushing by while falling (Game_LibraryFallSpeed: the library's chasm). */
#include "falling_air.h"

#include <math.h>
#include <stdint.h>

#define FROM        2.0f      /* units a tic: the air starts */
#define FULL        62.0f     /* and is all there (the rush's top: scene3d/rush.cpp) */
#define AIR_LEVEL   0.22f
#define ROAR_LEVEL  0.5f      /* the deep part, from about half speed */
#define ROAR_HZ     90.0f
#define DULL_HZ     250.0f    /* how bright the air is, slow to fast */
#define BRIGHT_HZ   3800.0f
#define PRESSED     0.45f     /* how much of the world goes under it at full speed */
#define RISE_S      0.15f     /* it comes up fast and dies away slowly */
#define FALL_S      1.2f
#define GUST_MIN_S  0.12f     /* buffets: a new swell in each ear every so often */
#define GUST_MAX_S  0.4f

static uint32_t seed = 0x2545f491u;
static float level, air[2], roar[2], gust[2] = {1, 1}, gustTo[2] = {1, 1};
static int gustLeft[2];

static float noise(void)
{
    seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
    return (seed >> 8) * (2.0f / 16777216.0f) - 1;
}

static float onePole(float hz, int rate) {return 1 - expf(-2 * 3.14159265f * hz / rate);}

void FallingAir_Mix(float *buffer, int count, int rate, float speed)
{
    float target = (speed - FROM) / (FULL - FROM);
    target = target < 0 ? 0 : target > 1 ? 1 : target;
    if (target <= 0 && level < 1e-4f) { level = 0; return; }
    float rise = 1 - expf(-1 / (RISE_S * rate)), fall = 1 - expf(-1 / (FALL_S * rate));
    /* Brightness and roar follow the level, set once a buffer. */
    float bright = onePole(DULL_HZ + (BRIGHT_HZ - DULL_HZ) * level * sqrtf(level), rate);
    float deep = onePole(ROAR_HZ, rate), glide = onePole(4.0f, rate);
    float roarShare = level > 0.5f ? (level - 0.5f) * 2 : 0;
    for (int i = 0; i < count; ++i)
    {
        level += (target - level) * (target > level ? rise : fall);
        float common = noise();
        for (int c = 0; c < 2; ++c)
        {
            if (--gustLeft[c] <= 0)
            {
                gustTo[c] = 0.6f + 0.65f * (noise() * 0.5f + 0.5f);
                gustLeft[c] = (int)((GUST_MIN_S + (GUST_MAX_S - GUST_MIN_S) * (noise() * 0.5f + 0.5f)) * rate);
            }
            gust[c] += (gustTo[c] - gust[c]) * glide;
            float n = 0.7f * common + 0.3f * noise();
            air[c] += (n - air[c]) * bright;
            roar[c] += (n - roar[c]) * deep;
            float sound = (AIR_LEVEL * air[c] + ROAR_LEVEL * roarShare * roar[c] * 4) * gust[c] * level;
            buffer[2*i+c] = buffer[2*i+c] * (1 - PRESSED * level) + sound;
        }
    }
}
