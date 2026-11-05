/* A strong wind over nothing: airborne sound, a broad dense rush across fixed
 * bands with its weight between 200 and 800 Hz, the bottom cut away so nothing
 * booms. The low bands barely move and both ears hear them alike, as anything
 * far off and wide is; the higher a band, the harder the surges lift it and
 * the more it flutters on its own. No objects, so no notes. */
#include "wind_engines.h"

#define LANES 9
#define BODY 1.25f            /* how much weight under the mids */
#define TOP 0.05f             /* how hard the surges tear at the top */
#define CUT_HZ 120.0f
#define FLUTTER_EVERY 9
#define CROSS_S 0.1f

static const float centre[LANES] = { 170, 250, 370, 540, 800, 1200, 1800, 2700, 4000 };

typedef struct {
    svf_t band[2];
    float fast[2], slow[2], from[2], to[2], gain[2];
    float shape, power, lag, depth, shared, own;
} lane_t;

static rng_t r;
static int started, phase;
static lane_t lanes[LANES];
static svf_t cut[2][2];
static float keep_fast, step_fast, keep_slow, step_slow;

static void start(void)
{
    float dt = FLUTTER_EVERY / (float)WIND_RATE;
    r = rng_seed(0x6a1e6a1eu);
    keep_fast = expf(-dt / 0.05f); step_fast = sqrtf(1 - keep_fast * keep_fast);
    keep_slow = expf(-dt / 0.3f); step_slow = sqrtf(1 - keep_slow * keep_slow);
    for (int i = 0; i < LANES; ++i)
    {
        lane_t *l = &lanes[i];
        float f = centre[i], x = i / (float)(LANES - 1), shared = f < 300 ? 0.85f : f < 700 ? 0.4f : 0;
        *l = (lane_t){ .shape = powf(f / 500, -0.8f) * (f < 300 ? BODY * sqrtf(f / 300) : 1) * (1 + TOP * x * x),
                       .power = 0.5f + (1.6f + 1.2f * TOP) * x, .lag = 0.12f * i, .depth = 0.45f * x,
                       .shared = shared, .own = sqrtf(1 - shared * shared) };
        for (int e = 0; e < 2; ++e) { l->band[e] = svf_make(f, 1.2f, WIND_RATE); l->from[e] = l->to[e] = 1; }
    }
    for (int e = 0; e < 2; ++e) for (int k = 0; k < 2; ++k) cut[e][k] = svf_make(CUT_HZ, 0.7f, WIND_RATE);
    started = 1;
}

void WindGale_Run(float *left, float *right, int count, const wind_speed_t *speed)
{
    if (!started) start();
    float sum[2][WIND_BLOCK] = {{0}};
    for (int i = 0; i < LANES; ++i)
    {
        lane_t *l = &lanes[i];
        float to[2];
        for (int e = 0; e < 2; ++e) to[e] = powf(WindSpeed_Ago(speed, l->lag + (e ? CROSS_S : 0)), l->power);
        for (int n = 0; n < count; ++n)
        {
            int step = (phase + n) % FLUTTER_EVERY;
            if (step == 0)
                for (int e = 0; e < 2; ++e)
                {
                    float x = rng_gauss(&r);
                    l->fast[e] = keep_fast * l->fast[e] + step_fast * x;
                    l->slow[e] = keep_slow * l->slow[e] + step_slow * x;
                    l->from[e] = l->to[e];
                    l->to[e] = expf(l->depth * (l->fast[e] - l->slow[e]));
                }
            float common = rng_noise(&r), blend = (step + 1) / (float)FLUTTER_EVERY, x = (n + 1) / (float)count;
            for (int e = 0; e < 2; ++e)
            {
                svf_run(&l->band[e], l->shared * common + l->own * rng_noise(&r));
                float gain = l->gain[e] + (to[e] - l->gain[e]) * x;
                sum[e][n] += l->shape * gain * (l->from[e] + (l->to[e] - l->from[e]) * blend) * l->band[e].band;
            }
        }
        l->gain[0] = to[0]; l->gain[1] = to[1];
    }
    phase = (phase + count) % FLUTTER_EVERY;
    float *out[2] = { left, right };
    for (int e = 0; e < 2; ++e)
        for (int n = 0; n < count; ++n)
        {
            svf_run(&cut[e][0], sum[e][n]); svf_run(&cut[e][1], cut[e][0].high);
            out[e][n] += cut[e][1].high;
        }
}
