/* The head faces the right way: left is left, ahead is even, and moving a
   sound around it never clicks. */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "hrtf.h"

#define PI_F 3.14159265f
#define BLOCKS 4
static int failures;

static void check(int ok, const char *what)
{
    if (ok) return;
    printf("FAILED: %s\n", what);
    failures = 1;
}

typedef struct { float power[2]; int first[2]; } heard_t;

/* A click from a direction: how much reaches each ear, and when. */
static heard_t click(int head, float azimuth, float elevation, float presence)
{
    static hrtf_voice_t voice;
    float mono[HRTF_BLOCK] = { 0 }, stereo[2 * HRTF_BLOCK] = { 0 };
    mono[0] = 1;
    Hrtf_Start(&voice, head, azimuth, elevation, presence);
    Hrtf_Process(&voice, mono, stereo, HRTF_BLOCK);
    heard_t h = { { 0, 0 }, { -1, -1 } };
    for (int ear = 0; ear < 2; ++ear)
    {
        float peak = 0;
        for (int i = 0; i < HRTF_BLOCK; ++i) peak = fmaxf(peak, fabsf(stereo[2 * i + ear]));
        for (int i = 0; i < HRTF_BLOCK; ++i)
        {
            float v = stereo[2 * i + ear];
            h.power[ear] += v * v;
            if (h.first[ear] < 0 && fabsf(v) > 0.3f * peak) h.first[ear] = i;
        }
    }
    return h;
}

/* A low tone swept twice around the head: the biggest jump between samples
   against the biggest a steady tone makes. */
static float sweep_jump(int head)
{
    static hrtf_voice_t voice;
    float mono[256], stereo[2 * 256], last[2] = { 0, 0 }, jump = 0;
    Hrtf_Start(&voice, head, 0, 0, 1);
    for (int block = 0, n = 0; block < 2000; ++block)
    {
        for (int i = 0; i < 256; ++i, ++n) mono[i] = sinf(2 * PI_F * 220 * n / HRTF_RATE);
        memset(stereo, 0, sizeof stereo);
        if (block % 5 == 0) Hrtf_Aim(&voice, 4 * PI_F * block / 2000, 0, 1);
        Hrtf_Process(&voice, mono, stereo, 256);
        for (int i = 0; i < 256; ++i)
            for (int ear = 0; ear < 2; ++ear)
            {
                if (block > 4) jump = fmaxf(jump, fabsf(stereo[2 * i + ear] - last[ear]));
                last[ear] = stereo[2 * i + ear];
            }
    }
    return jump;
}

/* What every channel playing at once, all moving, costs: a share of one core. */
static void cost(void)
{
    static hrtf_voice_t voices[32];
    static float mono[HRTF_BLOCK], stereo[2 * HRTF_BLOCK];
    for (int i = 0; i < HRTF_BLOCK; ++i) mono[i] = sinf(0.05f * i);
    for (int v = 0; v < 32; ++v) Hrtf_Start(&voices[v], HRTF_KEMAR, 0.2f * v, 0, 1);
    int blocks = 2000;
    clock_t start = clock();
    for (int b = 0; b < blocks; ++b)
        for (int v = 0; v < 32; ++v)
        {
            if (b % 3 == 0) Hrtf_Aim(&voices[v], 0.2f * v + 0.05f * b, 0.1f, 1);
            Hrtf_Process(&voices[v], mono, stereo, HRTF_BLOCK);
        }
    float seconds = (float)(clock() - start) / CLOCKS_PER_SEC, played = (float)blocks * HRTF_BLOCK / HRTF_RATE;
    printf("32 moving voices: %.1f%% of a core\n", 100 * seconds / played);
}

int main(int argc, char **argv)
{
    (void)argv;
    if (argc > 1) { cost(); return 0; }
    for (int head = 0; head < HRTF_HEADS; ++head)
    {
        heard_t ahead = click(head, 0, 0, 1), left = click(head, PI_F / 2, 0, 1), right = click(head, -PI_F / 2, 0, 1);
        heard_t behind = click(head, PI_F, 0, 1), here = click(head, PI_F / 2, 0, 0);
        printf("head %d: left %.2f/%.2f at %d/%d, ahead %.2f/%.2f, behind %.2f/%.2f, here %.2f/%.2f\n", head,
               left.power[0], left.power[1], left.first[0], left.first[1], ahead.power[0], ahead.power[1],
               behind.power[0], behind.power[1], here.power[0], here.power[1]);
        check(fabsf(ahead.power[0] - ahead.power[1]) < 1e-4f && ahead.first[0] == ahead.first[1], "ahead is even");
        check(fabsf(ahead.power[0] - 1) < 0.25f, "ahead keeps its level");
        check(left.power[0] > 2 * left.power[1], "left is louder in the left ear");
        check(left.first[0] + 20 < left.first[1], "left reaches the left ear first");
        check(fabsf(left.power[0] - right.power[1]) < 1e-4f && left.first[0] == right.first[1], "right mirrors left");
        check(fabsf(here.power[0] - 1) < 1e-4f && fabsf(here.power[1] - 1) < 1e-4f && here.first[0] == here.first[1],
              "a sound at the listener has no direction");
        float jump = sweep_jump(head), steady = 2 * sinf(PI_F * 220 / HRTF_RATE) * 4;
        printf("  sweep jump %.4f, allowed %.4f\n", jump, steady);
        check(jump < steady, "moving a sound never clicks");
    }
    if (!failures) printf("hrtf ok\n");
    return failures;
}
