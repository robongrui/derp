/* Plays a room_t. The early echoes read a history of the send at each wall's
 * round trip; they feed a small feedback delay network (eight lines mixed by
 * a Householder matrix, each line damped) that makes the tail. Both are kept
 * a little dark and lo-fi, like late-90s positional audio, not a lush hall;
 * each echo loses the treble its walls take, a wooden one duller than steel.
 *
 * How rough the room is (its scatter) decides how the sound comes back, as
 * roughness does for light. Off smooth walls an echo is one clean copy and
 * the tail rings - a steel tech room. Off rough ones each echo is smeared
 * into a short cluster spread a little around its wall, a chain of allpasses sprays every hit into a dense
 * wash before the network, and an allpass in each line keeps scattering it
 * as it goes round, so nothing rings - a stone castle goes flat.
 *
 * Room changes crossfade the echoes and glide the tail, so walking through a
 * door never clicks or warbles. */
#include <math.h>
#include <string.h>
#include "reverb.h"

#define HISTORY 32768   /* longer than the farthest echo, at up to 48 kHz */
#define LINES 8
#define LINE_MAX 4096
#define FADE_SECONDS 0.04f
#define GLIDE_SECONDS 0.3f

static const float line_ms[LINES] = {29.7f, 37.1f, 41.1f, 43.7f, 53.1f, 59.9f, 67.3f, 73.1f};

/* Before the network, at full scatter. */
#define DIFFUSERS 4
#define DIFFUSER_MAX 1024
static const float diffuser_ms[DIFFUSERS] = {4.77f, 3.59f, 12.73f, 9.31f};
static const float diffuser_gain[DIFFUSERS] = {0.75f, 0.75f, 0.65f, 0.65f};

/* In each line, at full scatter. */
#define SPRAY_MAX 512
#define SPRAY_GAIN 0.6f
static const float spray_ms[LINES] = {1.13f, 1.71f, 2.37f, 2.93f, 3.41f, 4.07f, 4.61f, 5.23f};

/* A rough wall's echo: the copy and five smeared after it, spaced unevenly
   so they don't comb, each a little off the wall's direction. */
#define SMEARS 5
#define COPIES (SMEARS + 1)
static const float smear_ms[SMEARS] = {1.7f, 3.9f, 6.2f, 9.1f, 13.3f};
static const float smear_gain[SMEARS] = {0.45f, 0.38f, 0.3f, 0.22f, 0.15f};
static const float smear_pan[SMEARS] = {-0.3f, 0.25f, -0.15f, 0.35f, -0.2f};

/* An echo as heard: each copy's gain per side, and how much treble its
   walls take (a one-pole lowpass, the tail's per-bounce cutoff). */
typedef struct { int delay, copies; float left[COPIES], right[COPIES], keep; } echo_t;
typedef struct { echo_t from, to, next; int fading, queued; float fade, low_from[2], low_to[2]; } tap_t;
typedef struct { float *buffer; int length, at; } allpass_t;

static int rate = 44100;
static float history[HISTORY]; static unsigned written;
static tap_t taps[ROOM_TAPS];
static int smear_offset[ROOM_TAPS][COPIES];
static float lines[LINES][LINE_MAX], line_lowpass[LINES]; static int line_length[LINES], line_at[LINES];
static float diffuser_memory[DIFFUSERS][DIFFUSER_MAX], spray_memory[LINES][SPRAY_MAX];
static allpass_t diffusers[DIFFUSERS], sprays[LINES];
static float send_lowpass, decay = 0.2f, damping, late, scatter,
    target_decay = 0.2f, target_damping, target_late, target_scatter;

static int samples(float ms, int most)
{
    int n = (int)(ms * 0.001f * rate);
    return n < 1 ? 1 : n >= most ? most - 1 : n;
}
void Reverb_Init(int output_rate)
{
    rate = output_rate;
    for (int i = 0; i < LINES; ++i)
    {
        line_length[i] = samples(line_ms[i], LINE_MAX);
        sprays[i] = (allpass_t){ spray_memory[i], samples(spray_ms[i], SPRAY_MAX), 0 };
    }
    for (int i = 0; i < DIFFUSERS; ++i) diffusers[i] = (allpass_t){ diffuser_memory[i], samples(diffuser_ms[i], DIFFUSER_MAX), 0 };
    /* Each wall smears a little differently, so the clusters don't line up. */
    for (int t = 0; t < ROOM_TAPS; ++t)
        for (int s = 0; s < SMEARS; ++s) smear_offset[t][s + 1] = samples(smear_ms[s] * (1 + 0.13f * t), HISTORY);
    Reverb_Clear();
}
void Reverb_Clear(void)
{
    memset(history, 0, sizeof(history));
    memset(lines, 0, sizeof(lines)); memset(line_lowpass, 0, sizeof(line_lowpass));
    memset(diffuser_memory, 0, sizeof(diffuser_memory)); memset(spray_memory, 0, sizeof(spray_memory));
    for (int t = 0; t < ROOM_TAPS; ++t) taps[t].low_from[0] = taps[t].low_from[1] = taps[t].low_to[0] = taps[t].low_to[1] = 0;
    send_lowpass = 0;
}

static float clamp1(float x, float lo) { return x < lo ? lo : x > 1 ? 1 : x; }
static float bounce_keep(float dark) { return expf(-2 * (float)M_PI * (7000 - 5500 * clamp1(dark, 0)) / rate); }

/* As rough as the wall is, the copy gives way to a smeared cluster of the
   same energy: the copy keeps sqrt(1 - s) of it, the cluster shares sqrt(s). */
static echo_t echo(const room_t *room, int i)
{
    echo_t e = {0};
    e.delay = (int)(room->delay[i] * rate);
    if (e.delay < 1) e.delay = 1;
    if (e.delay >= HISTORY - 1024) e.delay = HISTORY - 1024;  /* room for the smear */
    float pan = clamp1(room->pan[i], -1), s = clamp1(room->spread[i], 0), gain[COPIES], energy = 0;
    float weights = 0;
    for (int k = 0; k < SMEARS; ++k) weights += smear_gain[k] * smear_gain[k];
    gain[0] = sqrtf(1 - s);
    for (int k = 0; k < SMEARS; ++k) gain[k + 1] = sqrtf(s / weights) * smear_gain[k];
    for (int k = 0; k < COPIES; ++k) energy += gain[k] * gain[k];
    e.copies = s > 0 ? COPIES : 1;
    for (int k = 0; k < e.copies; ++k)
    {
        float p = k ? clamp1(pan + s * smear_pan[k - 1], -1) : pan, g = room->gain[i] * gain[k] / sqrtf(energy);
        e.left[k] = g * sqrtf(0.5f * (1 - p)); e.right[k] = g * sqrtf(0.5f * (1 + p));
    }
    e.keep = bounce_keep(room->dark[i]);
    return e;
}
void Reverb_SetRoom(const room_t *room)
{
    for (int i = 0; i < ROOM_TAPS; ++i)
    {
        tap_t *tap = &taps[i];
        echo_t target = echo(room, i);
        if (tap->fading) { tap->next = target; tap->queued = 1; }
        else
        {
            tap->to = target; tap->fading = 1; tap->fade = 0;
            tap->low_to[0] = tap->low_from[0]; tap->low_to[1] = tap->low_from[1];
        }
    }
    target_decay = room->decay; target_damping = room->damping; target_late = room->late;
    target_scatter = room->scatter < 0 ? 0 : room->scatter > 1 ? 1 : room->scatter;
}

static float read_history(int delay) { return history[(written - (unsigned)delay) & (HISTORY - 1)]; }

/* One wall's echo into left and right, through its walls' lowpass. */
static void hear(const echo_t *e, int t, float *low, float *left, float *right)
{
    float l = 0, r = 0;
    for (int k = 0; k < e->copies; ++k)
    {
        float v = read_history(e->delay + smear_offset[t][k]);
        l += e->left[k] * v; r += e->right[k] * v;
    }
    low[0] = l + (low[0] - l) * e->keep; low[1] = r + (low[1] - r) * e->keep;
    *left = low[0]; *right = low[1];
}

static float pass(allpass_t *a, float x, float g)
{
    float held = a->buffer[a->at], into = x - g * held;
    a->buffer[a->at] = into;
    if (++a->at >= a->length) a->at = 0;
    return held + g * into;
}

void Reverb_Process(const float *send, float *stereo, int frames, float amount)
{
    float glide = 1 - expf(-frames / (GLIDE_SECONDS * rate));
    decay += (target_decay - decay) * glide;
    damping += (target_damping - damping) * glide;
    late += (target_late - late) * glide;
    scatter += (target_scatter - scatter) * glide;
    /* A line's loop runs through its allpass too: count that in its decay. */
    float gain[LINES];
    for (int j = 0; j < LINES; ++j) gain[j] = powf(10, -3 * (line_length[j] + sprays[j].length) / (decay * rate));
    /* Even the smoothest room scatters a little. */
    float diffuse = 0.2f + 0.8f * scatter, spray = SPRAY_GAIN * scatter;
    /* Each bounce keeps less treble in dark rooms: 7 kHz for metal, 1.5 kHz for wood. */
    float cutoff = 7000 - 5500 * damping;
    float keep = expf(-2 * (float)M_PI * cutoff / rate);
    /* The send loses its upsampling hiss before it echoes. */
    float input_keep = expf(-2 * (float)M_PI * 5000 / rate);
    float fade_step = 1 / (FADE_SECONDS * rate);
    float tail_level = late * 1.1f;

    for (int i = 0; i < frames; ++i)
    {
        send_lowpass = send[i] + (send_lowpass - send[i]) * input_keep;
        history[written & (HISTORY - 1)] = send_lowpass;

        float early_left = 0, early_right = 0;
        for (int t = 0; t < ROOM_TAPS; ++t)
        {
            tap_t *tap = &taps[t];
            float old_left, old_right, now_left, now_right;
            hear(&tap->from, t, tap->low_from, &old_left, &old_right);
            if (!tap->fading) { early_left += old_left; early_right += old_right; continue; }
            hear(&tap->to, t, tap->low_to, &now_left, &now_right);
            float f = tap->fade;
            early_left += old_left * (1 - f) + now_left * f;
            early_right += old_right * (1 - f) + now_right * f;
            if ((tap->fade += fade_step) >= 1)
            {
                tap->from = tap->to;
                tap->low_from[0] = tap->low_to[0]; tap->low_from[1] = tap->low_to[1];
                tap->fading = tap->queued; tap->fade = 0;
                if (tap->queued) { tap->to = tap->next; tap->queued = 0; }
            }
        }

        /* The tail grows from the echoes, plus a little of the sound itself. */
        float input = 0.3f * (early_left + early_right) + 0.12f * send_lowpass;
        for (int d = 0; d < DIFFUSERS; ++d) input = pass(&diffusers[d], input, diffuser_gain[d] * diffuse);
        float out[LINES], sum = 0, tail_left = 0, tail_right = 0;
        for (int j = 0; j < LINES; ++j)
        {
            float value = pass(&sprays[j], lines[j][line_at[j]], spray);
            line_lowpass[j] = value + (line_lowpass[j] - value) * keep + 1e-18f - 1e-18f;
            out[j] = line_lowpass[j] * gain[j];
            sum += out[j];
            if (j & 1) tail_right += (j & 2 ? -1 : 1) * line_lowpass[j];
            else tail_left += (j & 2 ? -1 : 1) * line_lowpass[j];
        }
        sum *= 2.0f / LINES;
        for (int j = 0; j < LINES; ++j)
        {
            lines[j][line_at[j]] = out[j] - sum + input;
            if (++line_at[j] >= line_length[j]) line_at[j] = 0;
        }
        ++written;
        stereo[2 * i] += amount * (early_left + tail_level * tail_left);
        stereo[2 * i + 1] += amount * (early_right + tail_level * tail_right);
    }
}
