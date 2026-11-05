/* A breeze: air moving, nothing more, and no rumble under it. Thin fixed bands
 * per ear, each with its own small fast flutter and none of the slow, so they
 * stay independent above a few hertz and share everything below, the speed
 * (McDermott & Simoncelli: that is what reads as wind). Nothing moves in
 * pitch; a stronger air lifts the higher bands more. Soft air holds; puffs
 * sink lower between them and lift harder. */
#include "wind_engines.h"

#define BODY_LANES 4
#define AIR_LANES 5
#define LANES (BODY_LANES + AIR_LANES)
#define FLUTTER_EVERY 9       /* samples; the flutter is slow beside them */
#define FLUTTER_SLOW_S 0.3f
/* Brings each group to the same level at steady wind. */
#define BODY_NORM 0.484f
#define AIR_NORM 0.342f
#define CROSS_S 0.35f         /* a gust reaches one ear before the other */
#define PUFF_MAKEUP 1.57f     /* puffs sit lower; as loud as soft air, taken over time */

typedef struct {
    svf_t band[2];
    float fast[2], slow[2], from[2], to[2];
    float keep_fast, step_fast, keep_slow, step_slow, amp, depth, shared, own;
} lane_t;

typedef struct { float mix, power, gate, lag; } layer_t;

static const layer_t soft_body = { 0.20f, 1.6f, 0, 0.1f }, soft_air = { 0.17f, 2.2f, 0.4f, 0.3f };
static const layer_t puff_body = { 0.20f, 2.0f, 0, 0.1f }, puff_air = { 0.20f, 2.5f, 0.35f, 0.3f };

static rng_t r;
static int started, phase;
static lane_t lanes[LANES];
static float gain[2][2];     /* body and air, each ear, where the last block ended */

static void lane_make(lane_t *l, float f, float tilt, float depth, float shared)
{
    float tau = fminf(0.06f, 0.06f * powf(f / 300, -2.0f / 3)), dt = FLUTTER_EVERY / (float)WIND_RATE;
    *l = (lane_t){ .keep_fast = expf(-dt / tau), .keep_slow = expf(-dt / FLUTTER_SLOW_S),
                   .amp = powf(f / 300, -tilt) * sqrtf(f / 300), .depth = depth, .shared = shared, .own = sqrtf(1 - shared * shared) };
    l->step_fast = sqrtf(1 - l->keep_fast * l->keep_fast);
    l->step_slow = sqrtf(1 - l->keep_slow * l->keep_slow);
    for (int e = 0; e < 2; ++e) { l->band[e] = svf_make(f, 1.5f, WIND_RATE); l->from[e] = l->to[e] = 1; }
}

static void start(void)
{
    r = rng_seed(0xb4eeb4eeu);
    for (int i = 0; i < BODY_LANES; ++i) lane_make(&lanes[i], 170 * powf(800.0f / 170, (i + 0.5f) / BODY_LANES), 0.9f, 0.175f, 0.2f);
    for (int i = 0; i < AIR_LANES; ++i) lane_make(&lanes[BODY_LANES + i], 800 * powf(4600.0f / 800, (i + 0.5f) / AIR_LANES), 0.75f, 0.245f, 0);
    started = 1;
}

static float lift(const layer_t *a, float u)
{
    float x = u > a->gate ? u - a->gate : 0;
    return a->mix * powf(x / (1 - a->gate), a->power);
}

/* One lane's samples, added to out[ear]. */
static void lane_run(lane_t *l, float *left, float *right, int count, int at)
{
    float *out[2] = { left, right };
    for (int n = 0; n < count; ++n)
    {
        int step = (at + n) % FLUTTER_EVERY;
        if (step == 0)
            for (int e = 0; e < 2; ++e)
            {
                float x = rng_gauss(&r);
                l->fast[e] = l->keep_fast * l->fast[e] + l->step_fast * x;
                l->slow[e] = l->keep_slow * l->slow[e] + l->step_slow * x;
                l->from[e] = l->to[e];
                l->to[e] = expf(l->depth * (l->fast[e] - l->slow[e]));
            }
        float common = rng_noise(&r), blend = (step + 1) / (float)FLUTTER_EVERY;
        for (int e = 0; e < 2; ++e)
        {
            svf_run(&l->band[e], l->shared * common + l->own * rng_noise(&r));
            out[e][n] += l->amp * (l->from[e] + (l->to[e] - l->from[e]) * blend) * l->band[e].band;
        }
    }
}

/* The body or the air alone at steady gain, for the group norms above. */
void WindBreeze_Texture(float *left, float *right, int count, int air)
{
    if (!started) start();
    for (int i = air ? BODY_LANES : 0; i < (air ? LANES : BODY_LANES); ++i) lane_run(&lanes[i], left, right, count, phase);
    phase = (phase + count) % FLUTTER_EVERY;
}

void WindBreeze_Run(float *left, float *right, int count, const wind_speed_t *soft, const wind_speed_t *puffing, float puffs)
{
    if (!started) start();
    float body[2][WIND_BLOCK] = {{0}}, air[2][WIND_BLOCK] = {{0}}, to[2][2];
    float makeup = 1 + (PUFF_MAKEUP - 1) * puffs;
    for (int i = 0; i < BODY_LANES; ++i) lane_run(&lanes[i], body[0], body[1], count, phase);
    for (int i = BODY_LANES; i < LANES; ++i) lane_run(&lanes[i], air[0], air[1], count, phase);
    phase = (phase + count) % FLUTTER_EVERY;
    for (int e = 0; e < 2; ++e)
    {
        float late = e ? CROSS_S : 0;
        float s_body = lift(&soft_body, WindSpeed_Ago(soft, soft_body.lag + late));
        float p_body = lift(&puff_body, WindSpeed_Ago(puffing, puff_body.lag + late));
        float s_air = lift(&soft_air, WindSpeed_Ago(soft, soft_air.lag + late));
        float p_air = lift(&puff_air, WindSpeed_Ago(puffing, puff_air.lag + late));
        to[0][e] = BODY_NORM * makeup * (s_body + (p_body - s_body) * puffs);
        to[1][e] = AIR_NORM * makeup * (s_air + (p_air - s_air) * puffs);
    }
    for (int n = 0; n < count; ++n)
    {
        float x = (n + 1) / (float)count;
        left[n] += (gain[0][0] + (to[0][0] - gain[0][0]) * x) * body[0][n] + (gain[1][0] + (to[1][0] - gain[1][0]) * x) * air[0][n];
        right[n] += (gain[0][1] + (to[0][1] - gain[0][1]) * x) * body[1][n] + (gain[1][1] + (to[1][1] - gain[1][1]) * x) * air[1][n];
    }
    for (int g = 0; g < 2; ++g) for (int e = 0; e < 2; ++e) gain[g][e] = to[g][e];
}
