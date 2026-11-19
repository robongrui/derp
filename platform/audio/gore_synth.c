/* Gore: a kill's wet burst and a body slapping into a floor or wall; with
 * the Gore rule, flesh tearing, bone snapping, a whole body blown apart, a
 * chunk landing and a step in the mess. All filtered noise only, no tones
 * and no sweeps: soft wet bodies, sloshes of spray, drops pattering down,
 * cracks as short rung bursts. Played quietly under the monster's own death
 * sound. */
#include <math.h>
#include "impact_synth.h"
#include "synth_kit.h"

/* Noise through a band (lo..hi), rising over attack seconds and dying over
   tau, its level jumping every grain seconds when grain > 0 (a slosh, not
   a hiss). */
static void wet(float *out, int frames, int at, int rate, rng_t *r, float lo, float hi,
                float attack, float tau, float grain, float amp)
{
    svf_t high = svf_make(lo, 0.6f, rate), low = svf_make(hi, 0.6f, rate);
    int length = (int)(tau * 6 * rate), every = grain > 0 ? (int)(grain * rate) + 1 : 0;
    float level = 1;
    for (int n = 0; n < length && at + n < frames; ++n)
    {
        float t = n / (float)rate, env = (1 - expf(-t / attack)) * expf(-t / tau);
        if (every && n % every == 0) { float u = rng_unit(r); level = 0.3f + 0.7f * u * u; }
        svf_run(&high, rng_noise(r));
        svf_run(&low, high.high);
        if (at + n >= 0) out[at + n] += amp * env * level * low.low;
    }
}

/* Drops landing over start..end seconds, fewer and softer as they go. */
static void patter(float *out, int frames, int rate, rng_t *r, int count, float start, float end, float amp)
{
    for (int k = 0; k < count; ++k)
    {
        float when = rng_range(r, start, end), fade = 1 - 0.6f * (when - start) / (end - start + 1e-6f);
        wet(out, frames, (int)(when * rate), rate, r, rng_range(r, 500, 900), rng_range(r, 1800, 3200),
            0.0008f, rng_range(r, 0.003f, 0.008f), 0, amp * fade * rng_range(r, 0.4f, 1));
    }
}

/* Every take at one loudness over its first 0.12 seconds. */
static void level(float *out, int frames, int rate, rng_t *r, float target)
{
    double sum = 0;
    int window = (int)(0.12f * rate);
    for (int n = 0; n < window && n < frames; ++n) sum += out[n] * out[n];
    float gain = target / (float)sqrt(sum / window + 1e-12) * rng_range(r, 0.89f, 1.12f);
    for (int n = 0; n < frames; ++n) out[n] *= gain;
}

/* Bone giving way: two to four dry cracks a few milliseconds apart, splinters after them. */
static void snap(float *out, int frames, int at, int rate, rng_t *r, float amp)
{
    int cracks = (int)rng_range(r, 2, 4.99f);
    float t = 0;
    for (int k = 0; k < cracks; ++k)
    {
        add_click(out, frames, at + (int)(t * rate), rate, r, amp * (k ? rng_range(r, 0.4f, 0.8f) : 1), 900, 3200);
        t += rng_range(r, 0.004f, 0.014f);
    }
    add_crackle(out, frames, rate, r, at / (float)rate + t, at / (float)rate + t + 0.05f, 500, 0.015f, amp * 0.3f, 1200, 3800);
}

void Gore_Render(int kind, unsigned seed, int rate, float *out, int frames)
{
    rng_t r = rng_seed(seed);
    if (kind == IMPACT_GORE_SLAP)
    {
        /* The body's weight, a wet slap on top, a few drops shaken off. */
        wet(out, frames, 0, rate, &r, 40, rng_range(&r, 160, 220), 0.001f, rng_range(&r, 0.05f, 0.07f), 0, 1.0f);
        wet(out, frames, 0, rate, &r, 400, rng_range(&r, 2000, 2800), 0.0006f, 0.015f, 0, 0.5f);
        patter(out, frames, rate, &r, (int)rng_range(&r, 2, 5.99f), 0.04f, 0.25f, 0.12f);
        level(out, frames, rate, &r, 0.16f);
    }
    else if (kind == IMPACT_GORE_TEAR)
    {
        /* Fibres giving one after another: a dense run of soft wet ticks over a fluttering slosh, the
           weight of it under them, drops after. */
        float run = rng_range(&r, 0.12f, 0.22f);
        add_crackle(out, frames, rate, &r, 0.005f, run, 1400, run * 0.7f, 0.35f, 300, 1400);
        wet(out, frames, 0, rate, &r, rng_range(&r, 250, 400), rng_range(&r, 1200, 1800), 0.012f, run * 0.5f, 0.0025f, 0.6f);
        wet(out, frames, 0, rate, &r, 50, 260, 0.004f, 0.05f, 0, 0.5f);
        patter(out, frames, rate, &r, (int)rng_range(&r, 2, 4.99f), run, run + 0.15f, 0.1f);
        level(out, frames, rate, &r, 0.12f);
    }
    else if (kind == IMPACT_GORE_CRACK)
    {
        /* The snap and a soft wet knock under it. */
        snap(out, frames, 0, rate, &r, 0.8f);
        wet(out, frames, 0, rate, &r, 60, 300, 0.001f, 0.025f, 0, 0.5f);
        level(out, frames, rate, &r, 0.13f);
    }
    else if (kind == IMPACT_GORE_BLOWUP)
    {
        /* A heavy wet blast: the low weight, the body sloshing out torn, bones going in it, then the
           drops all round and the chunks coming down, soft knocks over half a second. */
        wet(out, frames, 0, rate, &r, 30, rng_range(&r, 180, 260), 0.002f, rng_range(&r, 0.09f, 0.14f), 0, 1.0f);
        wet(out, frames, (int)(0.003f * rate), rate, &r, 300, rng_range(&r, 1200, 1600), 0.004f, 0.14f, 0.002f, 0.7f);
        snap(out, frames, (int)(rng_range(&r, 0.005f, 0.02f) * rate), rate, &r, 0.4f);
        patter(out, frames, rate, &r, (int)rng_range(&r, 14, 24.99f), 0.1f, 0.8f, 0.12f);
        int chunks = (int)rng_range(&r, 3, 5.99f);
        for (int k = 0; k < chunks; ++k)
        {
            int at = (int)(rng_range(&r, 0.25f, 0.75f) * rate);
            wet(out, frames, at, rate, &r, 40, rng_range(&r, 220, 380), 0.001f, rng_range(&r, 0.02f, 0.035f), 0, rng_range(&r, 0.25f, 0.45f));
        }
        level(out, frames, rate, &r, 0.15f);
    }
    else if (kind == IMPACT_GORE_THUD)
    {
        /* A small wet weight on the floor, a smack on top, a drop or two. */
        wet(out, frames, 0, rate, &r, 40, rng_range(&r, 260, 420), 0.001f, rng_range(&r, 0.025f, 0.04f), 0, 1.0f);
        wet(out, frames, 0, rate, &r, 500, rng_range(&r, 1800, 2600), 0.0005f, 0.008f, 0, 0.4f);
        patter(out, frames, rate, &r, (int)rng_range(&r, 0, 2.99f), 0.03f, 0.15f, 0.1f);
        level(out, frames, rate, &r, 0.12f);
    }
    else if (kind == IMPACT_GORE_SQUELCH)
    {
        /* The boot pressing into it, a fluttering squish, then the sole pulling free, smaller and higher. */
        wet(out, frames, 0, rate, &r, rng_range(&r, 80, 140), rng_range(&r, 600, 900), 0.008f, 0.04f, 0.004f, 1.0f);
        float lift = rng_range(&r, 0.08f, 0.14f);
        wet(out, frames, (int)(lift * rate), rate, &r, rng_range(&r, 300, 500), rng_range(&r, 1100, 1600), 0.004f, 0.025f, 0.003f, 0.45f);
        level(out, frames, rate, &r, 0.1f);
    }
    else if (kind == IMPACT_GORE_STRETCH)
    {
        /* Fibres pulled taut: a few soft wet crackles close together and a faint creak of a slosh under
           them. Short, one a tic while the tear holds: slow motion spreads the run out by itself. */
        float run = rng_range(&r, 0.03f, 0.06f);
        add_crackle(out, frames, rate, &r, 0.002f, run, 900, run * 0.6f, 0.4f, 500, 2200);
        wet(out, frames, 0, rate, &r, rng_range(&r, 180, 260), rng_range(&r, 700, 1000), 0.006f, 0.03f, 0.003f, 0.35f);
        level(out, frames, rate, &r, 0.07f);
    }
    else if (kind == IMPACT_GORE_SNAP)
    {
        /* One fibre going: a wet tick, a little higher than the tear's, a drop flicked off. */
        add_click(out, frames, 0, rate, &r, 1.0f, rng_range(&r, 500, 800), rng_range(&r, 1800, 2600));
        wet(out, frames, 0, rate, &r, 300, rng_range(&r, 1400, 2000), 0.0005f, 0.012f, 0, 0.5f);
        patter(out, frames, rate, &r, (int)rng_range(&r, 0, 1.99f), 0.03f, 0.08f, 0.15f);
        level(out, frames, rate, &r, 0.08f);
    }
    else if (kind == IMPACT_BODY_STONE)
    {
        /* The floor's side of a body landing, under the slap: a dull hit, a little grit jumping. */
        wet(out, frames, 0, rate, &r, 50, rng_range(&r, 200, 280), 0.0008f, rng_range(&r, 0.018f, 0.028f), 0, 1.0f);
        add_click(out, frames, 0, rate, &r, 0.5f, 700, 2200);
        add_crackle(out, frames, rate, &r, 0.01f, 0.07f, 300, 0.03f, 0.12f, 1600, 4200);
        level(out, frames, rate, &r, 0.12f);
    }
    else if (kind == IMPACT_BODY_WOOD)
    {
        /* Boards taking it: a hollow band, a tick on top. */
        wet(out, frames, 0, rate, &r, rng_range(&r, 110, 150), rng_range(&r, 420, 560), 0.001f, rng_range(&r, 0.035f, 0.05f), 0, 1.0f);
        add_click(out, frames, 0, rate, &r, 0.4f, 900, 2400);
        level(out, frames, rate, &r, 0.12f);
    }
    else if (kind == IMPACT_BODY_METAL)
    {
        /* A plate struck by something soft and heavy: noise in a few narrow bands, no tones, dying fast. */
        float base = rng_range(&r, 180, 240);
        wet(out, frames, 0, rate, &r, 50, base, 0.0008f, 0.03f, 0, 1.0f);
        wet(out, frames, 0, rate, &r, base * 2.2f, base * 2.7f, 0.001f, 0.06f, 0, 0.5f);
        wet(out, frames, 0, rate, &r, base * 5.3f, base * 6.1f, 0.001f, 0.04f, 0, 0.25f);
        level(out, frames, rate, &r, 0.11f);
    }
    else
    {
        /* A soft wet body, the spray sloshing out, then the drops coming down. */
        wet(out, frames, 0, rate, &r, 60, rng_range(&r, 700, 1000), 0.002f, 0.04f, 0, 0.6f);
        wet(out, frames, (int)(0.004f * rate), rate, &r, 600, rng_range(&r, 1100, 1500), 0.003f, 0.07f, 0.002f, 0.4f);
        patter(out, frames, rate, &r, (int)rng_range(&r, 6, 12.99f), 0.08f, 0.4f, 0.12f);
        level(out, frames, rate, &r, 0.12f);
    }
}
