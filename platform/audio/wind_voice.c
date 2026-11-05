/* Plays the wind; see wind_voice.h. Each wind runs only while it is heard. */
#include <math.h>
#include <string.h>
#include "wind_engines.h"
#include "wind_voice.h"

#define GLIDE_SECONDS 0.05f
#define MUFFLE_GLIDE_SECONDS 0.08f
#define EASE_SECONDS 1.5f       /* a change of strength comes on like weather */
#define TIC_SAMPLES (WIND_RATE / WIND_TIC_RATE)
/* Each wind at its own loudness against the others, and the gale at the level
   the old wind's gale played at. */
#define STILL_TRIM 15.1f
#define BREEZE_TRIM 2.27f
#define GALE_TRIM 0.166f

static const wind_gusts_t soft_gusts = { 0.07f, 16, 1.2f, 0.35f, 1 };
static const wind_gusts_t puff_gusts = { 0.10f, 7, 1.3f, 0.55f, 0.72f };
static const wind_gusts_t gale_gusts = { 0.16f, 5, 1.2f, 0.7f, 1 };

static wind_speed_t soft, puffing, surging;
static int started, tic_left;
static float left, right, send, cutoff = 20000, low[2][2];
static float to_left, to_right, to_send, to_cutoff = 20000;
static float strength, to_strength, weight[3];

/* Decibels against a gale, between the strengths that were judged by ear. */
static float level_at(float s)
{
    static const float at[4] = { 0.1f, 0.5f, 0.75f, 1 }, db[4] = { -22, -12, -6, 0 };
    if (s <= 0) return 0;
    if (s < at[0]) return powf(10, db[0] / 20) * s / at[0];
    int i = s < at[1] ? 0 : s < at[2] ? 1 : 2;
    float x = fminf((s - at[i]) / (at[i + 1] - at[i]), 1);
    return powf(10, (db[i] + (db[i + 1] - db[i]) * x) / 20);
}

static float rising(float s, float from, float to)
{
    float x = fminf(fmaxf((s - from) / (to - from), 0), 1);
    return x;
}

void WindVoice_Set(const ambience_voice_t *field, float blowing)
{
    to_left = field->left; to_right = field->right; to_send = field->send;
    if (field->cutoff > 0) to_cutoff = field->cutoff;
    to_strength = blowing;
}

int WindVoice_Sounding(void) { return left + right + to_left + to_right >= 1e-5f; }

void WindVoice_Clear(void)
{
    left = right = send = to_left = to_right = to_send = 0;
    memset(low, 0, sizeof low);
}

static void start(void)
{
    WindSpeed_Start(&soft, &soft_gusts, 0x0a1dc0deu);
    WindSpeed_Start(&puffing, &puff_gusts, 0x0a1dc0dfu);
    WindSpeed_Start(&surging, &gale_gusts, 0x0a1dc0e0u);
    strength = to_strength;
    tic_left = TIC_SAMPLES;
    started = 1;
}

/* One block of the air, whatever is heard of each wind. */
static void air(float *l, float *r, int count)
{
    float ease = 1 - expf(-count / (EASE_SECONDS * WIND_RATE));
    strength += (to_strength - strength) * ease;
    float s = strength, level = level_at(s);
    float breezing = rising(s, 0.2f, 0.35f), gale = rising(s, 0.6f, 0.9f), half = 0.5f * 3.14159265f;
    float to[3] = { level * STILL_TRIM * cosf(half * breezing),
                    level * BREEZE_TRIM * sinf(half * breezing) * cosf(half * gale),
                    level * GALE_TRIM * sinf(half * gale) };
    float own[3][WIND_BLOCK * 2];
    memset(own, 0, sizeof own);
    if (to[0] + weight[0] > 1e-4f) WindStill_Run(own[0], own[0] + WIND_BLOCK, count);
    if (to[1] + weight[1] > 1e-4f) WindBreeze_Run(own[1], own[1] + WIND_BLOCK, count, &soft, &puffing, rising(s, 0.4f, 0.55f));
    if (to[2] + weight[2] > 1e-4f) WindGale_Run(own[2], own[2] + WIND_BLOCK, count, &surging);
    for (int n = 0; n < count; ++n)
    {
        float x = (n + 1) / (float)count, a = 0, b = 0;
        for (int i = 0; i < 3; ++i)
        {
            float w = weight[i] + (to[i] - weight[i]) * x;
            a += w * own[i][n]; b += w * own[i][WIND_BLOCK + n];
        }
        l[n] = a; r[n] = b;
    }
    for (int i = 0; i < 3; ++i) weight[i] = to[i];
}

void WindVoice_Run(float *out_left, float *out_right, float *out_send, int count)
{
    if (!WindVoice_Sounding()) { left = right = send = 0; return; }
    if (!started) start();
    for (int done = 0; done < count;)
    {
        int n = count - done < WIND_BLOCK ? count - done : WIND_BLOCK;
        if (n > tic_left) n = tic_left;
        float l[WIND_BLOCK], r[WIND_BLOCK];
        air(l, r, n);
        if ((tic_left -= n) <= 0)
        {
            WindSpeed_Tic(&soft); WindSpeed_Tic(&puffing); WindSpeed_Tic(&surging);
            tic_left = TIC_SAMPLES;
        }
        float glide = 1 - expf(-n / (GLIDE_SECONDS * WIND_RATE)), muffle_glide = 1 - expf(-n / (MUFFLE_GLIDE_SECONDS * WIND_RATE));
        float next_left = left + (to_left - left) * glide, next_right = right + (to_right - right) * glide;
        float next_send = send + (to_send - send) * glide;
        cutoff *= powf(to_cutoff / cutoff, muffle_glide);
        int open = cutoff >= 0.45f * WIND_RATE;
        float keep = expf(-2 * 3.14159265f * fminf(cutoff, 0.45f * WIND_RATE) / WIND_RATE);
        for (int i = 0; i < n; ++i)
        {
            float x = (i + 1) / (float)n, a = l[i], b = r[i];
            low[0][0] = a + (low[0][0] - a) * keep; low[0][1] = low[0][0] + (low[0][1] - low[0][0]) * keep;
            low[1][0] = b + (low[1][0] - b) * keep; low[1][1] = low[1][0] + (low[1][1] - low[1][0]) * keep;
            if (!open) { a = low[0][1]; b = low[1][1]; }
            out_left[done + i] += a * (left + (next_left - left) * x);
            out_right[done + i] += b * (right + (next_right - right) * x);
            out_send[done + i] += 0.5f * (a + b) * (send + (next_send - send) * x);
        }
        left = next_left; right = next_right; send = next_send;
        done += n;
    }
}
