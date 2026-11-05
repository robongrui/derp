/* Still air: almost nothing. A faint hush, and now and then one long sigh from
 * somewhere, its band fixed, drifting across; a speck of grit a few times a
 * minute. Mostly the sound of no wind. */
#include <stdlib.h>
#include <string.h>
#include "wind_engines.h"

#define BED 0.03f
#define SPECK_FRAMES 128

typedef struct {
    svf_t band[2], top[2];
    float amp, from, to, crest;
    int at, length;
} sigh_t;

static rng_t r;
static int started;
static svf_t bed[2];
static sigh_t sigh;
static float sigh_wait, speck_wait, speck[SPECK_FRAMES], speck_pan;
static int speck_at = SPECK_FRAMES;

static void start(void)
{
    r = rng_seed(0x51aa71d1u);
    for (int e = 0; e < 2; ++e) bed[e] = svf_make(240, 0.6f, WIND_RATE);
    sigh_wait = rng_range(&r, 2, 8); speck_wait = rng_range(&r, 3, 12);
    started = 1;
}

static void new_sigh(void)
{
    float f = rng_range(&r, 320, 720);
    for (int e = 0; e < 2; ++e) { sigh.band[e] = svf_make(f, 0.8f, WIND_RATE); sigh.top[e] = svf_make(1.4f * f, 0.7f, WIND_RATE); }
    sigh.length = (int)(rng_range(&r, 5, 11) * WIND_RATE);
    sigh.amp = rng_range(&r, 0.12f, 0.4f);
    sigh.from = rng_unit(&r); sigh.to = rng_unit(&r);
    sigh.crest = rng_range(&r, 0.25f, 0.45f);
    sigh.at = 0;
}

static void new_speck(void)
{
    memset(speck, 0, sizeof speck);
    add_click(speck, SPECK_FRAMES, 0, WIND_RATE, &r, rng_range(&r, 0.08f, 0.3f), 1000, 2800);
    speck_pan = rng_unit(&r);
    speck_at = 0;
}

void WindStill_Run(float *left, float *right, int count)
{
    if (!started) start();
    float seconds = count / (float)WIND_RATE;
    if ((sigh_wait -= seconds) <= 0) { new_sigh(); sigh_wait = rng_range(&r, 14, 42); }
    if ((speck_wait -= seconds) <= 0) { new_speck(); speck_wait = rng_range(&r, 6, 28); }
    for (int n = 0; n < count; ++n)
    {
        svf_run(&bed[0], rng_noise(&r)); svf_run(&bed[1], rng_noise(&r));
        left[n] += BED * bed[0].band; right[n] += BED * bed[1].band;
        if (sigh.at < sigh.length)
        {
            float x = sigh.at++ / (float)sigh.length, e = x < sigh.crest ? x / sigh.crest : (1 - x) / (1 - sigh.crest);
            float pan = sigh.from + (sigh.to - sigh.from) * x;
            e = sigh.amp * e * e * (3 - 2 * e);
            for (int c = 0; c < 2; ++c) { svf_run(&sigh.band[c], rng_noise(&r)); svf_run(&sigh.top[c], sigh.band[c].band); }
            left[n] += e * sqrtf(1 - pan) * sigh.top[0].low;
            right[n] += e * sqrtf(pan) * sigh.top[1].low;
        }
        if (speck_at < SPECK_FRAMES)
        {
            left[n] += speck[speck_at] * sqrtf(1 - speck_pan);
            right[n] += speck[speck_at] * sqrtf(speck_pan);
            ++speck_at;
        }
    }
}
