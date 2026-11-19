/* A shot striking a wall near the player: stone cracking, wood knocked, a metal
 * ping, something soft. Clicks and filtered noise only, short: an enemy's miss
 * beside your head, not a gun. */
#include <math.h>
#include "impact_synth.h"
#include "synth_kit.h"

/* Noise in a narrow band, gone within tau seconds: a plate's ring without a tone. */
static void band(float *out, int frames, int rate, rng_t *r, float lo, float hi, float tau, float amp)
{
    svf_t high = svf_make(lo, 0.7f, rate), low = svf_make(hi, 0.7f, rate);
    int length = (int)(tau * 6 * rate);
    for (int n = 0; n < length && n < frames; ++n)
    {
        float t = n / (float)rate;
        svf_run(&high, rng_noise(r));
        svf_run(&low, high.high);
        out[n] += amp * (1 - expf(-t / 0.0005f)) * expf(-t / tau) * low.low;
    }
}

void Hit_Render(int kind, unsigned seed, int rate, float *out, int frames)
{
    rng_t r = rng_seed(seed);
    if (kind == IMPACT_HIT_STONE)
    {
        /* The crack, a dull knock under it, grit trickling after. */
        add_click(out, frames, 0, rate, &r, 1.0f, 1200, 3800);
        band(out, frames, rate, &r, 120, rng_range(&r, 500, 700), 0.012f, 0.8f);
        add_crackle(out, frames, rate, &r, 0.015f, 0.14f, 260, 0.05f, 0.18f, 1800, 4800);
    }
    else if (kind == IMPACT_HIT_WOOD)
    {
        /* A hollow knock, a splintery tick on it. */
        band(out, frames, rate, &r, rng_range(&r, 260, 340), rng_range(&r, 800, 1100), 0.022f, 1.0f);
        add_click(out, frames, 0, rate, &r, 0.6f, 900, 2600);
        add_crackle(out, frames, rate, &r, 0.004f, 0.05f, 300, 0.02f, 0.12f, 1100, 3000);
    }
    else if (kind == IMPACT_HIT_METAL)
    {
        /* A ping: a click and a few narrow bands dying fast. */
        float base = rng_range(&r, 1400, 2100);
        add_click(out, frames, 0, rate, &r, 0.7f, 1800, 4800);
        band(out, frames, rate, &r, base, base * 1.08f, 0.05f, 1.0f);
        band(out, frames, rate, &r, base * 1.6f, base * 1.7f, 0.03f, 0.5f);
    }
    else if (kind == IMPACT_BALLOON)
    {
        /* Skin stretching: creaks crowding closer and louder over a low strain, all noise, no tones. */
        for (int n = 0; n < frames; ++n)
        {
            float t = n / (float)frames;
            out[n] += 0.3f * t * t * rng_noise(&r);
        }
        svf_t low = svf_make(420, 0.8f, rate);
        for (int n = 0; n < frames; ++n) { svf_run(&low, out[n]); out[n] = low.low; }
        add_crackle(out, frames, rate, &r, 0.05f, 2.1f, 260, 1.0f, 0.5f, 300, 1500);
    }
    else if (kind == IMPACT_PLAYER_ARMOR)
    {
        /* Plate taking it: the weight under it, a hard click, a short dull band. */
        band(out, frames, rate, &r, 30, rng_range(&r, 150, 200), 0.07f, 1.0f);
        add_click(out, frames, 0, rate, &r, 0.7f, 900, 2800);
        band(out, frames, rate, &r, rng_range(&r, 420, 520), rng_range(&r, 800, 950), 0.03f, 0.35f);
    }
    else if (kind == IMPACT_PLAYER_BODY)
    {
        /* Deep, with no ring: something the size of a door walked into. */
        band(out, frames, rate, &r, 28, rng_range(&r, 130, 170), 0.09f, 1.0f);
        band(out, frames, rate, &r, 180, rng_range(&r, 600, 800), 0.02f, 0.3f);
    }
    else if (kind == IMPACT_PLAYER_CLAW)
    {
        /* A rake: a few wet tears close together, the weight under them. */
        band(out, frames, rate, &r, 40, 200, 0.05f, 0.8f);
        add_crackle(out, frames, rate, &r, 0.0f, 0.09f, 900, 0.05f, 0.5f, 400, 1800);
        band(out, frames, rate, &r, 500, rng_range(&r, 1600, 2200), 0.03f, 0.3f);
    }
    else if (kind == IMPACT_PLAYER_BURN)
    {
        /* A thump, then the skin of it fizzing away. */
        band(out, frames, rate, &r, 30, 170, 0.06f, 0.9f);
        add_crackle(out, frames, rate, &r, 0.02f, 0.38f, 900, 0.14f, 0.3f, 1800, 4800);
    }
    else
    {
        /* Something soft taking it. */
        band(out, frames, rate, &r, 80, rng_range(&r, 400, 600), 0.02f, 1.0f);
        add_click(out, frames, 0, rate, &r, 0.25f, 400, 1400);
    }
    float peak = 1e-6f;
    for (int n = 0; n < frames; ++n) peak = fmaxf(peak, fabsf(out[n]));
    float gain = 0.35f / peak * rng_range(&r, 0.85f, 1.1f);
    for (int n = 0; n < frames; ++n) out[n] *= gain;
}
