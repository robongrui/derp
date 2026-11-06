/* See hrtf.h. The table holds the left ear; the right ear hears the left's
 * filter from the mirrored direction, as a symmetric head does. */
#include <math.h>
#include <string.h>
#include "hrtf.h"

#include "hrtf_kemar.inc"
#include "hrtf_ku100.inc"

#define PI_F 3.14159265f
/* How long a filter takes over from the last, within a tic. */
#define FADE_SAMPLES 512
/* The time between the ears follows a little behind: it bends pitch if it jumps. */
#define DELAY_GLIDE_SECONDS 0.01f
/* Moves smaller than this keep the filters they have: a still sound costs no fades. */
#define SAME_RADIANS 0.0175f
#define SAME_PRESENCE 0.02f

typedef struct {
    const float *scale;
    const short (*delays)[HRTF_STEPS];
    const short (*filters)[HRTF_STEPS][HRTF_TAPS];
} head_t;
static const head_t heads[HRTF_HEADS] = {
    { &hrtf_kemar_scale, hrtf_kemar_delays, hrtf_kemar_filters },
    { &hrtf_ku100_scale, hrtf_ku100_delays, hrtf_ku100_filters },
};

/* Where a direction falls on the table: two rings, two steps, and how far
   between them. */
typedef struct { int ring[2], step[2]; float up, along; } spot_t;
static spot_t spot_at(float azimuth, float elevation)
{
    spot_t s;
    float ring = (elevation * 180 / PI_F + 60) / HRTF_RING_DEGREES;
    ring = ring < 0 ? 0 : ring > HRTF_RINGS - 1 ? HRTF_RINGS - 1 : ring;
    float step = azimuth * 180 / PI_F / HRTF_STEP_DEGREES;
    step -= HRTF_STEPS * floorf(step / HRTF_STEPS);
    s.ring[0] = (int)ring; s.ring[1] = s.ring[0] + 1 < HRTF_RINGS ? s.ring[0] + 1 : s.ring[0];
    s.up = ring - s.ring[0];
    s.step[0] = (int)step % HRTF_STEPS; s.step[1] = (s.step[0] + 1) % HRTF_STEPS;
    s.along = step - floorf(step);
    return s;
}

static void filter_at(const head_t *head, float azimuth, float elevation, float presence, float *out)
{
    spot_t s = spot_at(azimuth, elevation);
    float weight[4] = { (1 - s.up) * (1 - s.along), (1 - s.up) * s.along, s.up * (1 - s.along), s.up * s.along };
    const short *corner[4] = { head->filters[s.ring[0]][s.step[0]], head->filters[s.ring[0]][s.step[1]],
                               head->filters[s.ring[1]][s.step[0]], head->filters[s.ring[1]][s.step[1]] };
    float scale = *head->scale * presence;
    for (int k = 0; k < HRTF_TAPS; ++k)
        out[k] = scale * (weight[0] * corner[0][k] + weight[1] * corner[1][k] + weight[2] * corner[2][k] + weight[3] * corner[3][k]);
    out[0] += 1 - presence;
}

/* In samples, how much later the right ear hears it than the left. */
static float delay_at(const head_t *head, float azimuth, float elevation)
{
    spot_t s = spot_at(azimuth, elevation);
    float low = head->delays[s.ring[0]][s.step[0]] * (1 - s.along) + head->delays[s.ring[0]][s.step[1]] * s.along;
    float high = head->delays[s.ring[1]][s.step[0]] * (1 - s.along) + head->delays[s.ring[1]][s.step[1]] * s.along;
    return (low + (high - low) * s.up) / 16;
}

static void set_delay(hrtf_voice_t *voice, float azimuth, float elevation, float presence)
{
    float later = delay_at(&heads[voice->head], azimuth, elevation) * presence;
    voice->delay_to[0] = later < 0 ? -later : 0;
    voice->delay_to[1] = later > 0 ? later : 0;
}

void Hrtf_Start(hrtf_voice_t *voice, int head, float azimuth, float elevation, float presence)
{
    memset(voice, 0, sizeof *voice);
    voice->head = head;
    voice->azimuth = azimuth; voice->elevation = elevation; voice->presence = presence;
    filter_at(&heads[head], azimuth, elevation, presence, voice->filter[0]);
    filter_at(&heads[head], -azimuth, elevation, presence, voice->filter[1]);
    set_delay(voice, azimuth, elevation, presence);
    voice->delay[0] = voice->delay_to[0]; voice->delay[1] = voice->delay_to[1];
}

void Hrtf_Aim(hrtf_voice_t *voice, float azimuth, float elevation, float presence)
{
    set_delay(voice, azimuth, elevation, presence);
    float turned = azimuth - voice->azimuth;
    turned -= 2 * PI_F * floorf(turned / (2 * PI_F) + 0.5f);
    if (voice->fading || (fabsf(turned) < SAME_RADIANS && fabsf(elevation - voice->elevation) < SAME_RADIANS
                          && fabsf(presence - voice->presence) < SAME_PRESENCE)) return;
    voice->azimuth = azimuth; voice->elevation = elevation; voice->presence = presence;
    filter_at(&heads[voice->head], azimuth, elevation, presence, voice->next[0]);
    filter_at(&heads[voice->head], -azimuth, elevation, presence, voice->next[1]);
    voice->fading = 1; voice->fade = 0;
}

static void convolve(const float *filter, const float *in, float *out, int count)
{
    memset(out, 0, (size_t)count * sizeof *out);
    for (int k = 0; k < HRTF_TAPS; ++k)
    {
        float tap = filter[k];
        const float *from = in + HRTF_TAPS - 1 - k;
        for (int i = 0; i < count; ++i) out[i] += tap * from[i];
    }
}

void Hrtf_Process(hrtf_voice_t *voice, const float *mono, float *stereo, int count)
{
    float signal[HRTF_LINE + HRTF_BLOCK], in[HRTF_TAPS - 1 + HRTF_BLOCK], out[HRTF_BLOCK], blend[HRTF_BLOCK];
    memcpy(signal, voice->line, sizeof voice->line);
    memcpy(signal + HRTF_LINE, mono, (size_t)count * sizeof *mono);
    float glide = 1 - expf(-count / (DELAY_GLIDE_SECONDS * HRTF_RATE));
    int faded = voice->fade;
    for (int ear = 0; ear < 2; ++ear)
    {
        float delay = voice->delay[ear], step = (voice->delay_to[ear] - delay) * glide / count;
        memcpy(in, voice->tail[ear], sizeof voice->tail[ear]);
        for (int i = 0; i < count; ++i, delay += step)
        {
            float at = HRTF_LINE + i - delay;
            int whole = (int)at;
            in[HRTF_TAPS - 1 + i] = signal[whole] + (signal[whole + 1] - signal[whole]) * (at - whole);
        }
        voice->delay[ear] = delay;
        memcpy(voice->tail[ear], in + count, sizeof voice->tail[ear]);
        convolve(voice->filter[ear], in, out, count);
        if (voice->fading)
        {
            convolve(voice->next[ear], in, blend, count);
            for (int i = 0; i < count; ++i)
            {
                float t = (float)(faded + i) / FADE_SAMPLES;
                out[i] += (blend[i] - out[i]) * (t < 1 ? t : 1);
            }
        }
        for (int i = 0; i < count; ++i) stereo[2 * i + ear] += out[i];
    }
    memcpy(voice->line, signal + count, sizeof voice->line);
    if (voice->fading && (voice->fade += count) >= FADE_SAMPLES)
    {
        memcpy(voice->filter, voice->next, sizeof voice->filter);
        voice->fading = 0;
    }
}
