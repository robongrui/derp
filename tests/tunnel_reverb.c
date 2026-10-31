/* The reverb down a tube (reverb.c's flutter, mirrorings and narrowed tail)
   at every rate the mixer might run: it never rings on or blows up, dies
   away once the sound stops, and switching between rooms - into a tube, out
   of it, to a wider one, turning in it - never clicks. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "reverb.h"

static int failures;
static void expect(int ok, const char *what, int rate)
{
    if (!ok) { fprintf(stderr, "FAIL at %d Hz: %s\n", rate, what); ++failures; }
}

/* A stone tube width across, as room.c measures one (the echoes roughly
   so), or with tunnel 0 the box room.c gives the same air. */
static room_t tube(float width, float tunnel, float axis)
{
    room_t room = {0};
    for (int k = 0; k < 8; ++k)
    {
        float side = fabsf(sinf(k * 3.14159265f / 4 - axis)) > 0.5f;
        float distance = side ? 0.5f * width : 400;
        room.delay[k] = 2 * distance / 10700;
        room.gain[k] = 0.35f / (1 + distance / 320);
        room.spread[k] = side ? 0.65f : 1; room.dark[k] = 0.45f;
        room.pan[k] = -sinf(k * 3.14159265f / 4);
    }
    room.delay[8] = 2 * 87 / 10700.0f; room.gain[8] = 0.18f; room.spread[8] = 0.65f; room.dark[8] = 0.45f;
    room.decay = 1 + 2 * tunnel; room.damping = 0.45f; room.scatter = 0.65f; room.late = 0.75f + 0.12f * tunnel;
    room.tunnel = tunnel; room.axis = axis; room.width = width; room.height = 128;
    room.flutter_delay = 2 * width / 10700; room.flutter = 0.8f * tunnel;
    return room;
}

/* The steepest step between samples, the tone's own taken out: where it
   jumps past what the tone and the room do anyway, it clicks. */
static float roughest(const float *stereo, long from, long to)
{
    float most = 0;
    for (long n = from + 2; n < to; ++n)
        for (int c = 0; c < 2; ++c)
        {
            float bend = stereo[2 * n + c] - 2 * stereo[2 * (n - 1) + c] + stereo[2 * (n - 2) + c];
            most = fmaxf(most, fabsf(bend));
        }
    return most;
}

static void run(int rate)
{
    reverb_t *reverb = Reverb_New(rate);
    if (!reverb) { expect(0, "no memory", rate); return; }
    const int block = 512;
    long frames = 8L * rate;
    float *send = calloc((size_t)frames, sizeof(float)), *stereo = calloc((size_t)frames * 2, sizeof(float));
    if (!send || !stereo) { expect(0, "no memory", rate); return; }

    /* Loud noise for three seconds into a narrow smooth tube, then nothing:
       it must stay bounded and die away. */
    unsigned seed = 1;
    for (long n = 0; n < 3L * rate; ++n) { seed = seed * 1664525u + 1013904223u; send[n] = ((seed >> 9) / 4194304.0f - 1); }
    room_t narrow = tube(64, 1, 0);
    narrow.flutter = 1; narrow.scatter = 0; narrow.damping = 0; narrow.decay = 3;
    Reverb_SetRoom(reverb, &narrow);
    for (long n = 0; n < frames; n += block)
        Reverb_Process(reverb, send + n, stereo + 2 * n, (int)(frames - n < block ? frames - n : block), 1);
    float peak = 0, last = 0;
    for (long n = 0; n < frames; ++n)
    {
        float v = fmaxf(fabsf(stereo[2 * n]), fabsf(stereo[2 * n + 1]));
        if (!(v == v) || v > 1e6f) { peak = INFINITY; break; }
        peak = fmaxf(peak, v);
        if (n >= frames - rate / 2) last = fmaxf(last, v);
    }
    expect(peak < 20, "the tube blows up", rate);
    expect(last < peak * 1e-3f, "the tube rings on after the sound stops", rate);

    /* A quiet steady tone while the rooms change every half second. */
    Reverb_Clear(reverb);
    memset(stereo, 0, sizeof(float) * 2 * (size_t)frames);
    for (long n = 0; n < frames; ++n) send[n] = 0.3f * sinf(2 * 3.14159265f * 220 * n / rate);
    room_t rooms[] = { tube(128, 0, 0), tube(128, 1, 0), tube(128, 1, 1.57f), tube(256, 0.7f, 0.3f),
                       tube(96, 1, 3.0f), tube(128, 0, 0), tube(160, 1, 0), tube(128, 0, 0) };
    int count = sizeof(rooms) / sizeof(rooms[0]);
    long change = rate / 2 / block * block;
    for (long n = 0; n < frames; n += block)
    {
        if (n % change == 0 && n / change < count) Reverb_SetRoom(reverb, &rooms[n / change]);
        Reverb_Process(reverb, send + n, stereo + 2 * n, (int)(frames - n < block ? frames - n : block), 1);
    }
    /* A click is a millisecond far rougher than those a few either side of
       it; a room changing its sound over tens of milliseconds is no click. */
    long ms = rate / 1000;
    for (int i = 1; i < count; ++i)
    {
        long at = i * change;
        for (long w = -5; w < 120; ++w)
        {
            float here = roughest(stereo, at + w * ms, at + (w + 1) * ms), around = 0;
            for (long v = 2; v <= 6; ++v)
                around = fmaxf(around, fmaxf(roughest(stereo, at + (w - v) * ms, at + (w - v + 1) * ms),
                                             roughest(stereo, at + (w + v) * ms, at + (w + v + 1) * ms)));
            char what[96];
            snprintf(what, sizeof(what), "the change to room %d clicks %ld ms in (%.4f against %.4f)", i, w, here, around);
            if (here > 3 * around + 1e-5f) { expect(0, what, rate); break; }
        }
    }
    free(send); free(stereo);
    Reverb_Free(reverb);
}

int main(void)
{
    static const int rates[] = { 11025, 22050, 44100, 48000 };
    for (int i = 0; i < 4; ++i) run(rates[i]);
    if (failures) return 1;
    puts("Tunnel reverb: bounded, dies away, no clicks at 11-48 kHz.");
    return 0;
}
