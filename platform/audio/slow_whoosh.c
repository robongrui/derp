/* Slow motion's whoosh, F.E.A.R.-style. In: air sucked in, a band of noise and a
 * tone sweeping down as they swell, cut off short on a low thump. Out: the
 * release, a quick burst sweeping up and away. Each ear has its own noise, so it
 * sits wide and in the head rather than at a point. Made in the mixer at the
 * output rate; slow motion never slows it. */
#include <math.h>
#include "slow_whoosh.h"
#include "synth_kit.h"
#include "../slow_motion.h"

#define WHOOSH_LEVEL 0.4f
#define IN_SECONDS 0.6f
#define OUT_SECONDS 0.8f
#define THUMP_SECONDS 0.25f
#define VOICES 4

typedef struct
{
    int playing, out, at;   /* at: frames since it began */
    rng_t noise[2];
    svf_t band[2];
    float tone, thump;      /* phases */
} whoosh_t;

static whoosh_t voices[VOICES];
static int seen_starts, seen_ends;
static unsigned seed = 1;

static void begin(int out)
{
    whoosh_t *v = &voices[0];
    for (int i = 0; i < VOICES; ++i)   /* a free one, else the oldest */
    {
        if (!voices[i].playing) { v = &voices[i]; break; }
        if (voices[i].at > v->at) v = &voices[i];
    }
    *v = (whoosh_t){ 1, out, 0, { rng_seed(seed), rng_seed(seed + 7919) } };
    seed += 2;
}

/* One frame of a whoosh, both ears; 0 once it is over. */
static int frame(whoosh_t *v, int rate, float *left, float *right)
{
    float t = v->at / (float)rate, freq, env, tone_freq, tone_level, thump = 0;
    if (!v->out)
    {
        float x = fminf(t / IN_SECONDS, 1);
        freq = 2800 * powf(0.08f, x);
        env = t < IN_SECONDS ? powf(x, 2.2f) * fminf(1, (1 - x) / 0.04f) : 0;
        tone_freq = 520 * powf(0.18f, x); tone_level = 0.12f;
        if (t >= IN_SECONDS)
        {
            float s = t - IN_SECONDS, f = 38 + 37 * expf(-s / 0.08f);
            v->thump += 2 * SYNTH_PI * f / rate;
            thump = 0.35f * (1 - expf(-s / 0.003f)) * expf(-s / THUMP_SECONDS) * sinf(v->thump);
            if (s > 5 * THUMP_SECONDS) return 0;
        }
    }
    else
    {
        float x = t / OUT_SECONDS;
        if (x >= 1) return 0;
        freq = 220 * powf(14, fminf(1.6f * x, 1));
        env = (1 - expf(-t / 0.008f)) * expf(-4.5f * x);
        tone_freq = 110 + 370 * fminf(1.6f * x, 1); tone_level = 0.08f;
    }
    if ((v->at & 15) == 0)
        for (int e = 0; e < 2; ++e) svf_tune(&v->band[e], freq * (e ? 1.06f : 0.95f), 1.6f, rate);
    v->tone += 2 * SYNTH_PI * tone_freq / rate;
    float tone = tone_level * env * sinf(v->tone);
    for (int e = 0; e < 2; ++e) svf_run(&v->band[e], rng_noise(&v->noise[e]));
    *left = WHOOSH_LEVEL * (env * 0.9f * v->band[0].band + tone + thump);
    *right = WHOOSH_LEVEL * (env * 0.9f * v->band[1].band + tone + thump);
    ++v->at;
    return 1;
}

void SlowWhoosh_Mix(float *buffer, int count, int rate)
{
    int starts = SlowMotion_Starts, ends = SlowMotion_Ends;
    /* A quick on-off within one block: only the latest of each kind is heard. */
    if (starts != seen_starts) begin(0);
    if (ends != seen_ends) begin(1);
    seen_starts = starts; seen_ends = ends;
    for (int i = 0; i < VOICES; ++i)
    {
        whoosh_t *v = &voices[i];
        for (int n = 0; v->playing && n < count; ++n)
        {
            float left, right;
            if (!frame(v, rate, &left, &right)) { v->playing = 0; break; }
            buffer[2 * n] += left; buffer[2 * n + 1] += right;
        }
    }
}
