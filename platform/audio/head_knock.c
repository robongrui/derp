/* His head hitting a floor's edge (Game_LibraryKnocks). */
#include "head_knock.h"
#include "synth_kit.h"

#include <stdlib.h>

#define KNOCK_SECONDS 1.4f
#define KNOCK_LEVEL   0.95f
#define DRIVE         4.0f    /* driven hard into saturation: dense, not just loud */

static float *take;
static int length, played, heard;
static unsigned seed = 0x5eedu;

/* Noise through a band (lo..hi), rising over attack seconds and dying over
   tau, its level jumping every grain seconds when grain > 0. */
static void band(float *out, int frames, int at, int rate, rng_t *r, float lo, float hi,
                 float attack, float tau, float grain, float amp)
{
    svf_t high = svf_make(lo, 0.7f, rate), low = svf_make(hi, 0.7f, rate);
    int every = grain > 0 ? (int)(grain * rate) + 1 : 0;
    float level = 1;
    for (int n = 0; n < (int)(tau * 7 * rate) && at + n < frames; ++n)
    {
        float t = n / (float)rate, env = (1 - expf(-t / attack)) * expf(-t / tau);
        if (every && n % every == 0) { float u = rng_unit(r); level = 0.2f + 0.8f * u * u; }
        svf_run(&high, rng_noise(r));
        svf_run(&low, high.high);
        out[at + n] += amp * env * level * low.low;
    }
}

static void render(int rate)
{
    rng_t r = rng_seed(seed++);
    length = (int)(KNOCK_SECONDS * rate);
    free(take);
    take = calloc((size_t)length, sizeof *take);
    if (!take) { length = 0; return; }
    /* The weight of it: the skull and the stone, a blow through the whole
       body, then the boom of it in his head. */
    band(take, length, 0, rate, &r, 30, rng_range(&r, 110, 150), 0.0005f, 0.12f, 0, 9.0f);
    band(take, length, 0, rate, &r, 18, rng_range(&r, 55, 70), 0.002f, 0.3f, 0, 14.0f);
    band(take, length, 0, rate, &r, 150, rng_range(&r, 450, 600), 0.0003f, 0.04f, 0, 3.0f);
    /* The crunch: bone giving way, hard and close, and the crush after. */
    band(take, length, 0, rate, &r, 900, rng_range(&r, 5000, 7000), 0.0002f, 0.01f, 0, 3.0f);
    band(take, length, (int)(0.008f * rate), rate, &r, 400, rng_range(&r, 2500, 3500), 0.002f, 0.05f, 0.004f, 1.5f);
    /* Splinters cracking on through the first moments. */
    for (int k = (int)rng_range(&r, 6, 10.99f); k > 0; --k)
    {
        float when = rng_range(&r, 0.002f, 0.07f);
        add_click(take, length, (int)(when * rate), rate, &r, rng_range(&r, 0.8f, 1.6f) * (1 - 6 * when), 1500, 5000);
    }
    /* The wet give under it, sloshing. */
    band(take, length, (int)(0.006f * rate), rate, &r, 180, rng_range(&r, 900, 1300), 0.004f, 0.14f, 0.003f, 2.0f);
    /* Drops coming down after. */
    for (int k = (int)rng_range(&r, 4, 7.99f); k > 0; --k)
    {
        float when = rng_range(&r, 0.15f, 0.7f);
        band(take, length, (int)(when * rate), rate, &r, rng_range(&r, 500, 800), rng_range(&r, 1800, 3000),
             0.0008f, rng_range(&r, 0.003f, 0.008f), 0, rng_range(&r, 0.15f, 0.35f) * (1 - when));
    }
    /* Into saturation, its loudest at 1. */
    float peak = 1e-6f;
    for (int n = 0; n < length; ++n) { take[n] = tanhf(DRIVE * take[n]); peak = fmaxf(peak, fabsf(take[n])); }
    for (int n = 0; n < length; ++n) take[n] /= peak;
}

void HeadKnock_Mix(float *buffer, int count, int rate, int knocks)
{
    if (knocks != heard)
    {
        heard = knocks;
        render(rate);
        played = 0;
    }
    for (int i = 0; i < count && played < length; ++i, ++played)
    {
        float s = KNOCK_LEVEL * take[played];
        buffer[2*i] += s;
        buffer[2*i+1] += s;
    }
}
