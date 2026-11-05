/* The speed of the air; see wind_speed.h. Stages a factor 4 apart sum to the
 * -5/3 of real turbulence, the fastest left out, as a level moving near 4 Hz
 * is what the ear likes least. A slow log-normal scale makes rough and calm
 * stretches (Boettcher et al.), and the waits between squalls are Pareto, so
 * they cluster and leave long lulls instead of keeping time. */
#include <math.h>
#include "wind_speed.h"

#define ROUGHNESS 0.5f     /* the slow scale's spread */
#define ROUGHNESS_S 150.0f /* and how slowly it moves */
#define LONGEST_WAIT 150.0f

static const float stage_seconds[3] = { 40, 10, 2.5f }, stage_share[3] = { 0.82f, 0.47f, 0.27f };

static float squall_wait(wind_speed_t *s)
{
    float wait = s->gusts.wait * powf(fmaxf(rng_unit(&s->r), 1e-4f), -1 / s->gusts.cluster);
    return fminf(wait, LONGEST_WAIT);
}

void WindSpeed_Start(wind_speed_t *s, const wind_gusts_t *gusts, unsigned seed)
{
    *s = (wind_speed_t){ .gusts = *gusts, .r = rng_seed(seed) };
    for (int i = 0; i < 3; ++i) s->stage[i] = rng_gauss(&s->r);
    s->calm = rng_gauss(&s->r);
    s->next = gusts->wait * rng_range(&s->r, 0.3f, 1.5f);
    for (int n = 0; n < WIND_SPEED_PAST; ++n) WindSpeed_Tic(s);
}

static void new_squall(wind_speed_t *s)
{
    for (int i = 0; i < WIND_SQUALLS; ++i)
    {
        wind_squall_t *q = &s->squalls[i];
        if (q->big > 0) continue;
        q->big = 0.4f - 0.6f * logf(fmaxf(rng_unit(&s->r), 1e-6f));
        q->rise = rng_range(&s->r, 0.7f, 1.8f);
        q->fall = rng_range(&s->r, 2.5f, 7);
        q->born = s->seconds;
        return;
    }
}

void WindSpeed_Tic(wind_speed_t *s)
{
    float v = 0, leaning = 0;
    for (int i = 0; i < 3; ++i)
    {
        float keep = expf(-1.0f / (stage_seconds[i] * WIND_TIC_RATE));
        s->stage[i] = keep * s->stage[i] + sqrtf(1 - keep * keep) * rng_gauss(&s->r);
        v += stage_share[i] * s->stage[i];
    }
    float calm_keep = expf(-1.0f / (ROUGHNESS_S * WIND_TIC_RATE));
    s->calm = calm_keep * s->calm + sqrtf(1 - calm_keep * calm_keep) * rng_gauss(&s->r);
    s->seconds += 1.0f / WIND_TIC_RATE;
    if (s->seconds >= s->next) { new_squall(s); s->next = s->seconds + squall_wait(s); }
    for (int i = 0; i < WIND_SQUALLS; ++i)
    {
        wind_squall_t *q = &s->squalls[i];
        if (q->big <= 0) continue;
        float x = s->seconds - q->born;
        if (x > 8 * q->fall) { q->big = 0; continue; }
        leaning += q->big * (1 - expf(-x / q->rise)) * expf(-x / q->fall);
    }
    /* A squall leans on the air, it doesn't explode it: past the steady wind
       the speed saturates, a big one topping out near 2.3. */
    float x = s->gusts.intensity * expf(ROUGHNESS * s->calm - ROUGHNESS * ROUGHNESS) * v + s->gusts.squall * leaning;
    float speed = fmaxf(x > 0 ? 1 + x / (1 + x / 1.6f) : 1 + x, 0.2f);
    s->at = (s->at + 1) % WIND_SPEED_PAST;
    s->past[s->at] = s->gusts.scale * speed;
}

float WindSpeed_Ago(const wind_speed_t *s, float seconds)
{
    float tics = fminf(fmaxf(seconds * WIND_TIC_RATE, 0), WIND_SPEED_PAST - 2);
    int whole = (int)tics;
    float a = s->past[(s->at - whole + WIND_SPEED_PAST) % WIND_SPEED_PAST];
    float b = s->past[(s->at - whole - 1 + WIND_SPEED_PAST) % WIND_SPEED_PAST];
    return a + (b - a) * (tics - whole);
}
