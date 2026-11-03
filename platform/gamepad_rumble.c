/* A gamepad's rumble; see gamepad_rumble.h. The player is mythic: the
   rumble is a few heavy thumps on the low motor, fast to hit and slow to
   fade, and what happens around them stays under their own guns, so the
   hands always know what they did. */
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "game_profile.h"
#include "gamepad_rumble.h"
#include <SDL3/SDL.h>

#define RATE 35          /* curve values a second, one a tic */
#define LONGEST (3*RATE) /* the longest curve kept */
#define VOICES 16
/* What reaches the player from around them, next to their own. */
#define WORLD_SHARE 0.55f
/* Below this a sound's bass rumbles nothing: a footstep isn't an explosion. */
#define LOW_FLOOR 0.05f
#define LOW_GAIN 3.5f
#define HIGH_FLOOR 0.08f
#define HIGH_GAIN 2.5f
/* Sends at least this often, each lasting a little longer, so a hitch in
   the frames lets the motors stop instead of buzzing on. */
#define RESEND_SECONDS 0.05f
#define SEND_MS 120

typedef struct { int frames; unsigned char low[LONGEST], high[LONGEST]; } curve_t;
typedef struct {
    const curve_t *curve;   /* null: shape */
    game_rumble shape;
    float age, gain;
    int world, used;
} voice_t;

static curve_t **curves;
static int curve_count;
static voice_t voices[VOICES];
static float sent[3], since_sent;
static int sent_any;

void Rumble_Analyse(int id, const unsigned char *samples, int length, int rate)
{
    if (id < 0 || rate <= 0 || length <= 0) return;
    if (id >= curve_count)
    {
        curve_t **grown = realloc(curves, (size_t)(id + 1) * sizeof(*curves));
        if (!grown) return;
        memset(grown + curve_count, 0, (size_t)(id + 1 - curve_count) * sizeof(*curves));
        curves = grown; curve_count = id + 1;
    }
    free(curves[id]); curves[id] = NULL;
    curve_t curve = { 0 };
    /* Two one-pole stages at 120 Hz: the bass a motor can carry. */
    float keep = expf(-2 * SDL_PI_F * 120 / rate), low1 = 0, low2 = 0, last_loud = 0;
    int per = rate / RATE > 0 ? rate / RATE : 1, last_felt = -1;
    for (int frame = 0; frame < LONGEST && frame * per < length; ++frame)
    {
        double bass = 0, rest = 0;
        int count = 0;
        for (int i = frame * per; i < (frame + 1) * per && i < length; ++i, ++count)
        {
            float x = (samples[i] - 128) / 128.0f;
            low1 = x + (low1 - x) * keep;
            low2 = low1 + (low2 - low1) * keep;
            bass += low2 * low2;
            rest += (x - low1) * (x - low1);
        }
        if (!count) break;
        float low = sqrtf((float)(bass / count)), loud = sqrtf((float)(rest / count));
        /* The high motor feels the hits: loudness rising, not lasting. */
        float hit = loud - last_loud > 0 ? loud - last_loud : 0;
        last_loud = loud;
        float l = (low - LOW_FLOOR) * LOW_GAIN, h = (hit * 2 + loud * 0.3f - HIGH_FLOOR) * HIGH_GAIN;
        l = l < 0 ? 0 : l > 1 ? 1 : l;
        h = h < 0 ? 0 : h > 1 ? 1 : h;
        curve.low[frame] = (unsigned char)(l * 255);
        curve.high[frame] = (unsigned char)(h * 255);
        if (curve.low[frame] || curve.high[frame]) last_felt = frame;
    }
    if (last_felt < 0) return;
    curve.frames = last_felt + 1;
    curves[id] = malloc(sizeof(curve));
    if (curves[id]) *curves[id] = curve;
}

static voice_t *free_voice(void)
{
    voice_t *oldest = &voices[0];
    for (int i = 0; i < VOICES; ++i)
    {
        if (!voices[i].used) return &voices[i];
        if (voices[i].age > oldest->age) oldest = &voices[i];
    }
    return oldest;
}

void Rumble_Sound(int id, float gain, int own)
{
    game_rumble shape;
    const curve_t *curve = id >= 0 && id < curve_count ? curves[id] : NULL;
    int tuned = own && Game_SoundRumble(id, &shape);
    if (gain < 0.05f || (!tuned && !curve)) return;
    voice_t *voice = free_voice();
    memset(voice, 0, sizeof(*voice));
    voice->used = 1;
    voice->gain = own ? 1 : gain * WORLD_SHARE;
    voice->world = !own;
    if (tuned) voice->shape = shape;
    else voice->curve = curve;
}

/* A hand-picked shape's strength at an age: up, held, then fading out
   slowly (squared), a heavy thing settling. */
static float envelope(const game_rumble *shape, float age)
{
    if (age < shape->attack) return age / shape->attack;
    age -= shape->attack;
    if (age < shape->hold) return 1;
    age -= shape->hold;
    if (shape->decay <= 0 || age >= shape->decay) return -1;
    float left = 1 - age / shape->decay;
    return left * left;
}

/* Several at once add up without clipping into a flat buzz: the strongest
   counts whole, the rest a share, and the sum eases toward full. */
static float mix(float strongest, float sum)
{
    float total = strongest + 0.35f * (sum - strongest);
    return total <= 0 ? 0 : (1 - expf(-2 * total)) / (1 - expf(-2.0f));
}

void Rumble_Frame(struct SDL_Gamepad *pad, float seconds, int strength, int triggers, int world)
{
    float most[3] = { 0, 0, 0 }, sum[3] = { 0, 0, 0 };
    for (int i = 0; i < VOICES; ++i)
    {
        voice_t *voice = &voices[i];
        if (!voice->used) continue;
        float level[3];
        if (voice->curve)
        {
            int frame = (int)(voice->age * RATE);
            if (frame >= voice->curve->frames) { voice->used = 0; continue; }
            level[0] = voice->curve->low[frame] / 255.0f;
            level[1] = voice->curve->high[frame] / 255.0f;
            level[2] = 0;
        }
        else
        {
            float e = envelope(&voice->shape, voice->age);
            if (e < 0) { voice->used = 0; continue; }
            level[0] = voice->shape.low * e;
            level[1] = voice->shape.high * e;
            level[2] = voice->shape.trigger * e;
        }
        voice->age += seconds;
        if (voice->world && !world) continue;
        for (int c = 0; c < 3; ++c)
        {
            float v = level[c] * voice->gain;
            sum[c] += v;
            if (v > most[c]) most[c] = v;
        }
    }
    if (!pad) return;
    float scale = strength >= 2 ? 1 : strength == 1 ? 0.5f : 0, out[3];
    for (int c = 0; c < 3; ++c) out[c] = mix(most[c], sum[c]) * scale;
    if (!triggers) out[2] = 0;
    since_sent += seconds;
    int quiet = out[0] == 0 && out[1] == 0 && out[2] == 0;
    int changed = fabsf(out[0] - sent[0]) > 0.01f || fabsf(out[1] - sent[1]) > 0.01f || fabsf(out[2] - sent[2]) > 0.01f;
    if (quiet && !sent_any) return;
    if (!changed && since_sent < RESEND_SECONDS && !quiet) return;
    SDL_RumbleGamepad(pad, (Uint16)(out[0] * 65535), (Uint16)(out[1] * 65535), quiet ? 0 : SEND_MS);
    SDL_RumbleGamepadTriggers(pad, 0, (Uint16)(out[2] * 65535), quiet ? 0 : SEND_MS);
    for (int c = 0; c < 3; ++c) sent[c] = out[c];
    since_sent = 0;
    sent_any = !quiet;
}

void Rumble_Stop(struct SDL_Gamepad *pad)
{
    if (pad) { SDL_RumbleGamepad(pad, 0, 0, 0); SDL_RumbleGamepadTriggers(pad, 0, 0, 0); }
    memset(voices, 0, sizeof(voices));
    sent_any = 0;
}
