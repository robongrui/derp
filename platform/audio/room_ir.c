/* A traced room's tail as sound. See room_ir.h. */
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "room_ir.h"

#define PI_F 3.14159265f
/* How much of an ear's hearing is all round rather than toward where it
   points: 0.5 is a cardioid, deaf only straight behind. Less would hear
   the far side upside down, and the two ears go out of phase. */
#define EAR_ROUND 0.5f

typedef struct { float b0, b1, b2, a1, a2, z1, z2; } biquad_t;
static biquad_t biquad(int high, float hz, int rate)
{
    float w = 2 * PI_F * hz / rate, c = cosf(w), alpha = sinf(w) / (2 * 0.7071f), a0 = 1 + alpha;
    biquad_t q = {0};
    q.b0 = (high ? (1 + c) : (1 - c)) / 2 / a0;
    q.b1 = (high ? -(1 + c) : (1 - c)) / a0;
    q.b2 = q.b0;
    q.a1 = -2 * c / a0; q.a2 = (1 - alpha) / a0;
    return q;
}
static float run(biquad_t *q, float x)
{
    float y = q->b0 * x + q->z1;
    q->z1 = q->b1 * x - q->a1 * y + q->z2;
    q->z2 = q->b2 * x - q->a2 * y;
    return y;
}

static float uniform(unsigned *s)
{
    *s ^= *s << 13; *s ^= *s >> 17; *s ^= *s << 5;
    return (*s >> 8) * (1.0f / 16777216.0f) * 2 - 1;
}

/* Band edges: below 500 Hz, 500 to 2000, 2000 to 7000 (or the rate's top). */
static void band_noise(int band, int rate, unsigned *rng, float *out, int frames)
{
    float top = fminf(7000, 0.45f * rate);
    biquad_t f[2];
    int filters = 2;
    if (band == 0) { f[0] = biquad(0, 500, rate); f[1] = biquad(0, 500, rate); }
    else if (band == 1) { f[0] = biquad(1, 500, rate); f[1] = biquad(0, 2000, rate); }
    else { f[0] = biquad(1, 2000, rate); f[1] = biquad(0, top, rate); }
    double power = 0;
    for (int i = 0; i < frames; ++i)
    {
        float v = uniform(rng);
        for (int k = 0; k < filters; ++k) v = run(&f[k], v);
        out[i] = v;
        power += (double)v * v;
    }
    float norm = power > 0 ? (float)(1 / sqrt(power / frames)) : 0;
    for (int i = 0; i < frames; ++i) out[i] *= norm;
}

void RoomIR_Build(const room_trace_t *t, int rate, unsigned seed, float *w, float *x, float *y, int frames)
{
    memset(w, 0, sizeof(float) * frames); memset(x, 0, sizeof(float) * frames); memset(y, 0, sizeof(float) * frames);
    int length = (int)(t->bins * TRACE_BIN * rate);
    if (length > frames) length = frames;
    if (length <= 0) return;
    float *noise = malloc(sizeof(float) * length);
    if (!noise) return;
    unsigned rng = (seed * 747796405u + 2891336453u) | 1;
    const float per_bin = TRACE_BIN * rate;
    for (int k = 0; k < TRACE_WAYS; ++k)
    {
        float cx = cosf(k * PI_F / 4), cy = sinf(k * PI_F / 4);
        for (int b = 0; b < TRACE_BANDS; ++b)
        {
            band_noise(b, rate, &rng, noise, length);
            for (int i = 0; i < length; ++i)
            {
                /* The bin's energy spread over its samples, between bins smoothly. */
                float at = i / per_bin - 0.5f;
                int bin = (int)floorf(at);
                float f = at - bin;
                float e0 = bin >= 0 && bin < TRACE_BINS ? t->energy[bin][b][k] : 0;
                float e1 = bin + 1 >= 0 && bin + 1 < TRACE_BINS ? t->energy[bin + 1][b][k] : 0;
                if (bin < 0) e0 = e1;
                float v = sqrtf((e0 + (e1 - e0) * f) / per_bin) * noise[i];
                w[i] += v; x[i] += v * cx; y[i] += v * cy;
            }
        }
    }
    free(noise);
}

void RoomIR_Ear(float angle, float *gw, float *gx, float *gy)
{
    *gw = EAR_ROUND;
    *gx = (1 - EAR_ROUND) * cosf(angle);
    *gy = (1 - EAR_ROUND) * sinf(angle);
}
