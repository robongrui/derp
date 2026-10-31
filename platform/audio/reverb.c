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
 * Down a tube (room.h's tunnel) the sound also goes back and forth between
 * the side walls: a feedback comb at the round trip across, fed by the
 * echoes, darker and a little blurred each time round, each half of it from
 * one side wall. And the tail comes from along the tube: narrow when the
 * listener faces down it, from either side when they face a side wall.
 *
 * Room changes crossfade the echoes and glide the tail, so walking through a
 * door never clicks or warbles. */
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "reverb.h"

#define HISTORY 32768   /* longer than the farthest echo, at up to 48 kHz */
#define LINES 8
#define LINE_MAX 4096
#define FADE_SECONDS 0.04f
#define GLIDE_SECONDS 0.3f
#define SPEED 10700.0f  /* sound, map units per second, as room.c */

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

/* The flutter across a tube: at most this long a round trip (at up to 48 kHz),
   keeping at most this much each time round (well short of ringing on), as
   loud as this, and blurred by an allpass in its loop as the tube is rough. */
#define FLUTTER_MAX 8192
#define FLUTTER_FEEDBACK 0.6f
#define FLUTTER_LEVEL 1.6f
#define FLUTTER_BLUR_MS 1.9f
#define FLUTTER_BLUR_GAIN 0.45f
/* Facing down a tube, its tail narrows to this much of its usual width. */
#define TAIL_NARROWEST 0.2f

/* Down a tube the side walls, floor and ceiling answer again and again: the
   listener's sound mirrored i widths across and j heights up comes back
   from sqrt((i W)^2 + (j H)^2) away, densely, from the first echoes on until
   the tail takes over, so a tube's tail starts at once. These are the
   mirrorings past the first, each counted with its twins on the other side
   (and above and below), keeping IMAGE_KEEP of itself a bounce; the first
   echoes are room.c's own. */
#define IMAGES 27
#define IMAGE_LEVEL 0.25f   /* about a tube's first echo off a side wall */
#define IMAGE_KEEP 0.95f
static const unsigned char image_i[IMAGES] = {1, 2, 0, 2, 1, 2, 3, 0, 3, 1, 3, 2, 4, 0, 4, 1, 3, 4, 2, 5, 0, 5, 1, 4, 3, 5, 2};
static const unsigned char image_j[IMAGES] = {1, 0, 2, 1, 2, 2, 0, 3, 1, 3, 2, 3, 0, 4, 1, 4, 3, 2, 4, 0, 5, 1, 5, 3, 4, 2, 5};

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

struct reverb_s {
    int rate;
    float history[HISTORY]; unsigned written;
    tap_t taps[ROOM_TAPS];
    int smear_offset[ROOM_TAPS][COPIES];
    float lines[LINES][LINE_MAX], line_lowpass[LINES]; int line_length[LINES], line_at[LINES];
    float diffuser_memory[DIFFUSERS][DIFFUSER_MAX], spray_memory[LINES][SPRAY_MAX];
    allpass_t diffusers[DIFFUSERS], sprays[LINES];
    float send_lowpass, decay, damping, late, scatter, target_decay, target_damping, target_late, target_scatter;
    float flutter_line[FLUTTER_MAX], flutter_blur_memory[SPRAY_MAX], flutter_low; int flutter_at; allpass_t flutter_blur;
    float flutter, flutter_delay, side_pan, tail_width, tunnel;  /* delay in samples */
    float target_flutter, target_flutter_delay, target_side_pan, target_tail_width, target_tunnel;
    float across, high, target_across, target_high, image_low[2];  /* a tube's width and height, in samples of sound */
};


static int samples(const reverb_t *r, float ms, int most)
{
    int n = (int)(ms * 0.001f * r->rate);
    return n < 1 ? 1 : n >= most ? most - 1 : n;
}
reverb_t *Reverb_New(int rate)
{
    reverb_t *r = calloc(1, sizeof(*r));
    if (!r) return NULL;
    r->rate = rate;
    r->decay = r->target_decay = 0.2f;
    r->tail_width = r->target_tail_width = 1;
    r->flutter_blur = (allpass_t){ r->flutter_blur_memory, samples(r, FLUTTER_BLUR_MS, SPRAY_MAX), 0 };
    for (int i = 0; i < LINES; ++i)
    {
        r->line_length[i] = samples(r, line_ms[i], LINE_MAX);
        r->sprays[i] = (allpass_t){ r->spray_memory[i], samples(r, spray_ms[i], SPRAY_MAX), 0 };
    }
    for (int i = 0; i < DIFFUSERS; ++i)
        r->diffusers[i] = (allpass_t){ r->diffuser_memory[i], samples(r, diffuser_ms[i], DIFFUSER_MAX), 0 };
    /* Each wall smears a little differently, so the clusters don't line up. */
    for (int t = 0; t < ROOM_TAPS; ++t)
        for (int s = 0; s < SMEARS; ++s) r->smear_offset[t][s + 1] = samples(r, smear_ms[s] * (1 + 0.13f * t), HISTORY);
    return r;
}
void Reverb_Free(reverb_t *r) { free(r); }
void Reverb_Clear(reverb_t *r)
{
    memset(r->history, 0, sizeof(r->history));
    memset(r->lines, 0, sizeof(r->lines)); memset(r->line_lowpass, 0, sizeof(r->line_lowpass));
    memset(r->diffuser_memory, 0, sizeof(r->diffuser_memory)); memset(r->spray_memory, 0, sizeof(r->spray_memory));
    for (int t = 0; t < ROOM_TAPS; ++t)
    {
        tap_t *tap = &r->taps[t];
        tap->low_from[0] = tap->low_from[1] = tap->low_to[0] = tap->low_to[1] = 0;
    }
    memset(r->flutter_line, 0, sizeof(r->flutter_line)); memset(r->flutter_blur_memory, 0, sizeof(r->flutter_blur_memory));
    r->flutter_low = 0;
    r->send_lowpass = 0;
}

static float clamp1(float x, float lo) { return x < lo ? lo : x > 1 ? 1 : x; }
static float bounce_keep(float dark, int rate) { return expf(-2 * (float)M_PI * (7000 - 5500 * clamp1(dark, 0)) / rate); }

/* As rough as the wall is, the copy gives way to a smeared cluster of the
   same energy: the copy keeps sqrt(1 - s) of it, the cluster shares sqrt(s). */
static echo_t echo(const room_t *room, int i, int rate)
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
    e.keep = bounce_keep(room->dark[i], rate);
    return e;
}
void Reverb_SetRoom(reverb_t *r, const room_t *room)
{
    for (int i = 0; i < ROOM_TAPS; ++i)
    {
        tap_t *tap = &r->taps[i];
        echo_t target = echo(room, i, r->rate);
        if (tap->fading) { tap->next = target; tap->queued = 1; }
        else
        {
            tap->to = target; tap->fading = 1; tap->fade = 0;
            tap->low_to[0] = tap->low_from[0]; tap->low_to[1] = tap->low_from[1];
        }
    }
    r->target_decay = room->decay; r->target_damping = room->damping; r->target_late = room->late;
    r->target_scatter = room->scatter < 0 ? 0 : room->scatter > 1 ? 1 : room->scatter;
    r->target_flutter = clamp1(room->flutter, 0);
    float trip = room->flutter_delay * r->rate;
    r->target_flutter_delay = trip < 2 ? 2 : trip > FLUTTER_MAX - 2 ? FLUTTER_MAX - 2 : trip;
    float tunnel = clamp1(room->tunnel, 0);
    r->target_side_pan = cosf(room->axis);
    r->target_tail_width = 1 - tunnel * (1 - fmaxf(fabsf(sinf(room->axis)), TAIL_NARROWEST));
    r->target_tunnel = tunnel;
    float most = (HISTORY - 1024) / 7.1f;  /* the farthest mirroring still in the history */
    r->target_across = fminf(fmaxf(room->width / SPEED * r->rate, 1), most);
    r->target_high = fminf(fmaxf(room->height / SPEED * r->rate, 1), most);
}

static float read_history(const reverb_t *r, int delay) { return r->history[(r->written - (unsigned)delay) & (HISTORY - 1)]; }
/* Between samples, for delays that glide. */
static float read_history_at(const reverb_t *r, float delay)
{
    int whole = (int)delay;
    float t = delay - whole, a = read_history(r, whole), b = read_history(r, whole + 1);
    return a + (b - a) * t;
}

/* One wall's echo into left and right, through its walls' lowpass. */
static void hear(const reverb_t *r, const echo_t *e, int t, float *low, float *left, float *right)
{
    float l = 0, rr = 0;
    for (int k = 0; k < e->copies; ++k)
    {
        float v = read_history(r, e->delay + r->smear_offset[t][k]);
        l += e->left[k] * v; rr += e->right[k] * v;
    }
    low[0] = l + (low[0] - l) * e->keep; low[1] = rr + (low[1] - rr) * e->keep;
    *left = low[0]; *right = low[1];
}

static float pass(allpass_t *a, float x, float g)
{
    float held = a->buffer[a->at], into = x - g * held;
    a->buffer[a->at] = into;
    if (++a->at >= a->length) a->at = 0;
    return held + g * into;
}

/* The flutter line, delay samples back (between samples, so it glides). */
static float read_flutter(const reverb_t *r, float delay)
{
    float at = r->flutter_at - delay;
    if (at < 0) at += FLUTTER_MAX;
    int i = (int)at, j = i + 1 < FLUTTER_MAX ? i + 1 : 0;
    float t = at - i;
    return r->flutter_line[i] + (r->flutter_line[j] - r->flutter_line[i]) * t;
}

void Reverb_Process(reverb_t *r, const float *send, float *stereo, int frames, float amount)
{
    int rate = r->rate;
    float glide = 1 - expf(-frames / (GLIDE_SECONDS * rate));
    float decay_from = r->decay, late_from = r->late;
    r->decay += (r->target_decay - r->decay) * glide;
    r->damping += (r->target_damping - r->damping) * glide;
    r->late += (r->target_late - r->late) * glide;
    r->scatter += (r->target_scatter - r->scatter) * glide;
    /* What a tube adds moves sample by sample from where the last block left
       it, so walking into a tube or turning in one never zippers or clicks. */
    float ramp = 1.0f / frames, side_from = r->side_pan, width_from = r->tail_width, tunnel_from = r->tunnel;
    r->side_pan += (r->target_side_pan - r->side_pan) * glide;
    r->tail_width += (r->target_tail_width - r->tail_width) * glide;
    /* A tube appearing is mirrored at its own size at once, quietly at first. */
    if (r->tunnel <= 0 && r->target_tunnel > 0) { r->across = r->target_across; r->high = r->target_high; }
    float across_from = r->across, high_from = r->high;
    r->across += (r->target_across - r->across) * glide;
    r->high += (r->target_high - r->high) * glide;
    r->tunnel += (r->target_tunnel - r->tunnel) * glide;
    if (r->target_tunnel <= 0 && r->tunnel < 1e-3f) r->tunnel = 0;
    int images = tunnel_from > 0 || r->tunnel > 0;
    float image_from[IMAGES], image_step[IMAGES], image_left[IMAGES], image_right[IMAGES], left_step[IMAGES], right_step[IMAGES];
    for (int k = 0; images && k < IMAGES; ++k)
    {
        float i = image_i[k], j = image_j[k], twins = i > 0 && j > 0 ? 4 : 2;
        image_from[k] = sqrtf(i * i * across_from * across_from + j * j * high_from * high_from);
        image_step[k] = (sqrtf(i * i * r->across * r->across + j * j * r->high * r->high) - image_from[k]) * ramp;
        float g = IMAGE_LEVEL * sqrtf(0.5f * twins) * powf(IMAGE_KEEP, i + j - 1) / sqrtf(i * i + j * j);
        /* Off the side walls, from either side by turns. */
        float turn = i > 0 ? (k & 1 ? 0.6f : -0.6f) : 0;
        float p0 = turn * clamp1(side_from, -1), p1 = turn * clamp1(r->side_pan, -1);
        image_left[k] = g * tunnel_from * sqrtf(0.5f * (1 - p0)); image_right[k] = g * tunnel_from * sqrtf(0.5f * (1 + p0));
        left_step[k] = (g * r->tunnel * sqrtf(0.5f * (1 - p1)) - image_left[k]) * ramp;
        right_step[k] = (g * r->tunnel * sqrtf(0.5f * (1 + p1)) - image_right[k]) * ramp;
    }
    /* A flutter starting up starts clean, at its own round trip; one dying
       away stops once it can't be heard. */
    float flutter_from = r->flutter_delay, flutter_step = 0, flutter_was = r->flutter;
    if (r->flutter <= 0 && r->target_flutter > 0)
    {
        memset(r->flutter_line, 0, sizeof(r->flutter_line)); memset(r->flutter_blur_memory, 0, sizeof(r->flutter_blur_memory));
        r->flutter_low = 0;
        flutter_from = r->flutter_delay = r->target_flutter_delay;
    }
    else flutter_step = (r->target_flutter_delay - r->flutter_delay) * glide / frames;
    r->flutter += (r->target_flutter - r->flutter) * glide;
    if (r->target_flutter <= 0 && r->flutter < 1e-3f) r->flutter = 0;
    r->flutter_delay += flutter_step * frames;
    int fluttering = flutter_was > 0 || r->flutter > 0;
    float flutter_blur = FLUTTER_BLUR_GAIN * r->scatter;
    int narrowed = width_from < 1 || r->tail_width < 1;
    /* A line's loop runs through its allpass too: count that in its decay. */
    /* Each line's gain, and the tail's level, move sample by sample from
       where the last block left them: a tail changing length never clicks. */
    float gain[LINES], gain_step[LINES];
    for (int j = 0; j < LINES; ++j)
    {
        float trip = -3.0f * (r->line_length[j] + r->sprays[j].length) / rate;
        gain[j] = powf(10, trip / decay_from);
        gain_step[j] = (powf(10, trip / r->decay) - gain[j]) / frames;
    }
    /* Even the smoothest room scatters a little. */
    float diffuse = 0.2f + 0.8f * r->scatter, spray = SPRAY_GAIN * r->scatter;
    /* Each bounce keeps less treble in dark rooms: 7 kHz for metal, 1.5 kHz for wood. */
    float cutoff = 7000 - 5500 * r->damping;
    float keep = expf(-2 * (float)M_PI * cutoff / rate);
    /* The send loses its upsampling hiss before it echoes. */
    float input_keep = expf(-2 * (float)M_PI * 5000 / rate);
    float fade_step = 1 / (FADE_SECONDS * rate);
    float tail_level = late_from * 1.1f, tail_step = (r->late - late_from) * 1.1f / frames;

    for (int i = 0; i < frames; ++i)
    {
        r->send_lowpass = send[i] + (r->send_lowpass - send[i]) * input_keep;
        r->history[r->written & (HISTORY - 1)] = r->send_lowpass;

        float early_left = 0, early_right = 0;
        for (int t = 0; t < ROOM_TAPS; ++t)
        {
            tap_t *tap = &r->taps[t];
            float old_left, old_right, now_left, now_right;
            hear(r, &tap->from, t, tap->low_from, &old_left, &old_right);
            if (!tap->fading) { early_left += old_left; early_right += old_right; continue; }
            hear(r, &tap->to, t, tap->low_to, &now_left, &now_right);
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

        if (images)
        {
            float l = 0, rr = 0;
            for (int k = 0; k < IMAGES; ++k)
            {
                float v = read_history_at(r, image_from[k] + image_step[k] * i);
                l += (image_left[k] + left_step[k] * i) * v; rr += (image_right[k] + right_step[k] * i) * v;
            }
            r->image_low[0] = l + (r->image_low[0] - l) * keep; r->image_low[1] = rr + (r->image_low[1] - rr) * keep;
            early_left += r->image_low[0]; early_right += r->image_low[1];
        }

        /* The tail grows from the echoes, plus a little of the sound itself. */
        float input = 0.3f * (early_left + early_right) + 0.12f * r->send_lowpass;
        for (int d = 0; d < DIFFUSERS; ++d) input = pass(&r->diffusers[d], input, diffuser_gain[d] * diffuse);
        float out[LINES], sum = 0, tail_left = 0, tail_right = 0;
        for (int j = 0; j < LINES; ++j)
        {
            float value = pass(&r->sprays[j], r->lines[j][r->line_at[j]], spray);
            r->line_lowpass[j] = value + (r->line_lowpass[j] - value) * keep + 1e-18f - 1e-18f;
            out[j] = r->line_lowpass[j] * gain[j];
            gain[j] += gain_step[j];
            sum += out[j];
            if (j & 1) tail_right += (j & 2 ? -1 : 1) * r->line_lowpass[j];
            else tail_left += (j & 2 ? -1 : 1) * r->line_lowpass[j];
        }
        sum *= 2.0f / LINES;
        for (int j = 0; j < LINES; ++j)
        {
            r->lines[j][r->line_at[j]] = out[j] - sum + input;
            if (++r->line_at[j] >= r->line_length[j]) r->line_at[j] = 0;
        }
        ++r->written;
        if (narrowed)
        {
            /* The tail narrowed toward the tube's ends, as loud. */
            float width = width_from + (r->tail_width - width_from) * i * ramp, norm = sqrtf(2 / (1 + width * width));
            float mid = 0.5f * (tail_left + tail_right), wide = 0.5f * (tail_left - tail_right) * width;
            tail_left = (mid + wide) * norm; tail_right = (mid - wide) * norm;
        }
        float out_left = early_left + tail_level * tail_left, out_right = early_right + tail_level * tail_right;
        tail_level += tail_step;
        if (fluttering)
        {
            /* Across and back, each time a little darker: one wall answers
               after the full round trip, the other half way, each from
               where it is as the listener faces - hard either side facing
               down the tube. */
            float flutter = flutter_was + (r->flutter - flutter_was) * i * ramp;
            float flutter_feedback = FLUTTER_FEEDBACK * flutter, flutter_level = FLUTTER_LEVEL * flutter;
            float side = clamp1(side_from + (r->side_pan - side_from) * i * ramp, -1);
            float wall_a_left = sqrtf(0.5f * (1 + side)), wall_a_right = sqrtf(0.5f * (1 - side));
            float trip = flutter_from + flutter_step * i;
            float back = pass(&r->flutter_blur, read_flutter(r, trip), flutter_blur);
            r->flutter_low = back + (r->flutter_low - back) * keep + 1e-18f - 1e-18f;
            float half = read_flutter(r, 0.5f * trip);
            r->flutter_line[r->flutter_at] = 0.5f * (early_left + early_right) + flutter_feedback * r->flutter_low;
            if (++r->flutter_at >= FLUTTER_MAX) r->flutter_at = 0;
            float a = flutter_level * r->flutter_low, b = flutter_level * flutter_feedback * half;
            out_left += a * wall_a_left + b * wall_a_right;
            out_right += a * wall_a_right + b * wall_a_left;
        }
        stereo[2 * i] += amount * out_left;
        stereo[2 * i + 1] += amount * out_right;
    }
}
