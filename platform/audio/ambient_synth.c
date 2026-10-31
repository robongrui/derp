/* Names of the ambient kinds, which synth plays each, and how loud. */
#include <math.h>
#include <stdlib.h>
#include "ambient_synth.h"

static const char *const names[AMBIENT_KINDS] = {
    "lamp", "crt", "computer", "teleporter", "nukage", "slimefall", "blood", "water",
    "lava", "fire", "magma", "wind", "rain", "glass-rain", "room",
};
/* Brings each kind to about -18 to -24 dB, so all fill the 8 bits alike and
   none hisses; magma stays the faintest, and water's drops peak where the
   rest do over its silence. */
static const float gains[AMBIENT_KINDS] = {
    0.56f, 0.75f, 1.0f, 0.86f, 1.93f, 3.16f, 1.6f, 1.6f, 1.64f, 2.0f, 4.7f, 1.2f, 1.6f, 1.4f, 2.0f,
};

/* How loud each plays next to the rest. A steady lamp buzz everywhere grates,
   so lamps play far under the liquids; a computer's bleeps are made loud to
   fill the 8 bits and played back down, a little under where they were first
   heard. Standing water's drips play up, or they go unheard. Outside the
   wind is the bed the nukage bubbles in, not the other way round. Rain plays
   under the wind's level: a steady hiss everywhere tires the ear; on the
   windows it taps under it again, heard up close by the glass. */
static const float levels[AMBIENT_KINDS] = {
    0.35f, 1, 0.14f, 1, 0.8f, 1, 1, 1.8f, 1, 1, 1, 1.0f, 0.9f, 0.8f, 0.25f,
};

float Ambient_Level(int kind) { return kind >= 0 && kind < AMBIENT_KINDS ? levels[kind] : 0; }
const char *Ambient_Name(int kind) { return kind >= 0 && kind < AMBIENT_KINDS ? names[kind] : "?"; }

int Ambient_Stepped(int kind) { return kind == AMBIENT_WIND || kind == AMBIENT_RAIN || kind == AMBIENT_GLASS_RAIN; }
/* The wind is a breeze where nothing says otherwise; rain a downpour. */
float Ambient_UsualStrength(int kind) { return kind == AMBIENT_WIND ? 0.5f : 1; }

float Ambient_Loudness(int kind, float strength)
{
    if (!Ambient_Stepped(kind)) return 1;
    float lightest = 1.0f / AMBIENT_STEPS, s = strength > 1 ? 1 : strength;
    if (s <= 0) return 0;
    return sqrtf(fmaxf(s, lightest) / Ambient_UsualStrength(kind)) * (s < lightest ? s / lightest : 1);
}

static void render(int kind, unsigned seed, int rate, float *out, int frames, const float *light, int tics, float strength)
{
    if (kind <= AMBIENT_TELEPORTER) Machine_Render(kind, seed, rate, out, frames, light, tics);
    else if (kind <= AMBIENT_WATER) Liquid_Render(kind, seed, rate, out, frames);
    else Fire_Render(kind, seed, rate, out, frames, strength);
}

static float *rendered(int kind, unsigned seed, int rate, int frames, const float *light, int tics, float strength)
{
    float *own = calloc((size_t)frames, sizeof(float));
    if (!own) return NULL;
    render(kind, seed, rate, own, frames, light, tics, strength);
    for (int n = 0; n < frames; ++n) own[n] *= gains[kind];
    return own;
}

void Ambient_Render(int kind, unsigned seed, int rate, float *out, int frames,
                    const float *light, int tics)
{
    if (kind < 0 || kind >= AMBIENT_KINDS) return;
    float *own = rendered(kind, seed, rate, frames, light, tics, Ambient_UsualStrength(kind));
    if (!own) return;
    for (int n = 0; n < frames; ++n) out[n] += own[n];
    free(own);
}

static void measure(const float *x, int frames, float *rms, float *peak)
{
    double sum = 0;
    float top = 0;
    for (int n = 0; n < frames; ++n) { sum += (double)x[n] * x[n]; top = fmaxf(top, fabsf(x[n])); }
    *rms = frames > 0 ? (float)sqrt(sum / frames) : 0;
    *peak = top;
}

void Ambient_RenderStrength(int kind, float strength, unsigned seed, int rate, float *out, int frames)
{
    if (kind < 0 || kind >= AMBIENT_KINDS) return;
    float usual = Ambient_UsualStrength(kind);
    if (!Ambient_Stepped(kind) || fabsf(strength - usual) < 1e-4f) { Ambient_Render(kind, seed, rate, out, frames, NULL, 0); return; }
    float *own = rendered(kind, seed, rate, frames, NULL, 0, strength);
    if (!own) return;
    /* The usual take sets how loud: as loud on the average, and its peaks
       no higher than the usual's or where the 8 bits round them off no more
       than softly (Impact_To8Bit), whichever is higher: only a drip here and
       there is rounded off. Measured once a kind, rate and length, whatever
       the seed: on so many seconds of noise the seed hardly shows. */
    static struct { int rate, frames; float rms, peak; } usual_take[AMBIENT_KINDS];
    if (usual_take[kind].rate != rate || usual_take[kind].frames != frames)
    {
        float *same = rendered(kind, seed, rate, frames, NULL, 0, usual);
        if (!same) { free(own); return; }
        measure(same, frames, &usual_take[kind].rms, &usual_take[kind].peak);
        usual_take[kind].rate = rate; usual_take[kind].frames = frames;
        free(same);
    }
    float rms, peak, usual_rms = usual_take[kind].rms, usual_peak = usual_take[kind].peak;
    measure(own, frames, &rms, &peak);
    float scale = rms > 1e-6f ? usual_rms / rms : 1;
    if (peak * scale > fmaxf(usual_peak, 1.2f)) scale = fmaxf(usual_peak, 1.2f) / peak;
    for (int n = 0; n < frames; ++n) out[n] += scale * own[n];
    free(own);
}
