/* Far rooms ringing. See room_reverbs.h. */
#include <math.h>
#include <string.h>
#include "doomdef.h"
#include "doomstat.h"
#include "acoustic_graph.h"
#include "level_sound.h"
#include "occlusion.h"
#include "far_tail.h"
#include "reverb.h"
#include "room_reverbs.h"
#include "room_tails.h"

#define BLOCK 512
#define GLIDE_SECONDS 0.1f     /* how fast a slot's loudness, lowpass and side follow the listener */
#define RING_OUT_TICS 35       /* a room's reverb is kept this long past its tail */
#define THROUGH_WALL_DB 30.0f  /* no way to the room at all */
#define THROUGH_WALL_HZ 500.0f
#define PAN_POWER (1 + (126.0f / 127) * (126.0f / 127))  /* as the mixer pans (i_sdl_sound.c) */
/* A far room is heard dark, through an opening: its reverb runs at a
   quarter of the output rate (11025 Hz), a quarter of the work. */
#define DOWN FAR_DOWN
#define TRACED_HOLD 3.0f       /* seconds a traced tail may last (room_tails.c's longest) */
#define BLEND_SECONDS 0.3f     /* from the reverb to a tail traced while the room rang */

typedef struct {
    int key, fed;
    float own;                     /* share of the listener's own sounds it gets */
    float hold;                    /* seconds its tail lasts */
    reverb_t *reverb;
    /* Its room's traced tail (room_tails.h), in place of the reverb from
       when it was claimed; NULL plays the reverb. */
    far_tail_t *tail;
    const conv_slot_t *traced;
    float blend;                   /* 0 the reverb .. 1 the traced tail */
    float gain, cutoff, left, right;   /* targets */
    float now_gain, now_cutoff, now_left, now_right, low[2];
    float send[BLOCK];
    /* Into the slow rate and back: the sum toward the next slow sample, how
       far along it is, and the last two slow samples out, to glide between. */
    float gathered, before, after;
    int phase;
} slot_t;

static slot_t slots[ROOM_SLOTS];
static int rate = 44100, blocked, in_use = ROOM_SLOTS;

void RoomReverbs_SetCount(int count)
{
    in_use = count < 0 ? 0 : count > ROOM_SLOTS ? ROOM_SLOTS : count;
    for (int i = in_use; i < ROOM_SLOTS; ++i) slots[i].key = -1;
}

void RoomReverbs_SetOwn(int slot, float amount)
{
    if (slot >= 0 && slot < ROOM_SLOTS) slots[slot].own = amount;
}

void RoomReverbs_Init(int output_rate)
{
    rate = output_rate;
    for (int i = 0; i < ROOM_SLOTS; ++i)
    {
        slots[i].reverb = Reverb_New(rate / DOWN);
        slots[i].tail = FarTail_New(TRACED_HOLD, rate / DOWN);
        slots[i].key = -1;
    }
}

void RoomReverbs_SetEars(int headphones)
{
    for (int i = 0; i < ROOM_SLOTS; ++i) if (slots[i].reverb) Reverb_SetEars(slots[i].reverb, headphones);
}

void RoomReverbs_Clear(void)
{
    for (int i = 0; i < ROOM_SLOTS; ++i)
    {
        if (slots[i].reverb) Reverb_Clear(slots[i].reverb);
        FarTail_Clear(slots[i].tail);
        slots[i].traced = NULL;
        slots[i].key = -1;
        slots[i].low[0] = slots[i].low[1] = 0;
        slots[i].gathered = slots[i].before = slots[i].after = 0;
        slots[i].phase = 0;
    }
}

int RoomReverbs_Claim(int key, const room_record_t *record)
{
    if (key < 0 || !record) return -1;
    int free_slot = -1;
    for (int i = 0; i < in_use; ++i)
    {
        if (!slots[i].reverb) continue;
        if (slots[i].key == key)
        {
            slots[i].fed = gametic;
            /* Traced since (again, or at last): the newer tail over the same
               ringing, which its history has kept; from the reverb, blended. */
            const conv_slot_t *now = RoomTails_Far(key);
            if (now) { slots[i].traced = now; slots[i].hold = TRACED_HOLD; }
            return i;
        }
        if (slots[i].key < 0 && free_slot < 0) free_slot = i;
    }
    if (free_slot < 0) return -1;
    slot_t *s = &slots[free_slot];
    Reverb_Clear(s->reverb);
    Reverb_SetRoom(s->reverb, &record->room);
    FarTail_Clear(s->tail);
    s->traced = s->tail ? RoomTails_Far(key) : NULL;
    s->blend = s->traced ? 1.0f : 0.0f;
    s->key = key; s->fed = gametic; s->hold = s->traced ? TRACED_HOLD : record->room.decay; s->own = 0;
    s->low[0] = s->low[1] = 0;
    s->gathered = s->before = s->after = 0;
    s->phase = 0;
    s->now_gain = s->gain = 0;
    s->now_cutoff = s->cutoff = OCCLUSION_OPEN_HZ;
    s->now_left = s->now_right = s->left = s->right = 0;
    return free_slot;
}

void RoomReverbs_Expire(void)
{
    for (int i = 0; i < ROOM_SLOTS; ++i)
        if (slots[i].key >= 0 && (gametic < slots[i].fed || gametic - slots[i].fed > (int)(slots[i].hold * (float)TICRATE) + RING_OUT_TICS))
            slots[i].key = -1;
}

int RoomReverbs_Key(int slot) { return slot >= 0 && slot < ROOM_SLOTS ? slots[slot].key : -1; }

void RoomReverbs_SetPath(int slot, float gain, float cutoff, int separation)
{
    if (slot < 0 || slot >= ROOM_SLOTS) return;
    slot_t *s = &slots[slot];
    separation = separation < 1 ? 1 : separation > 254 ? 254 : separation;
    float far = fminf(1.0f, (float)(separation < 127 ? separation : 254 - separation) / 127.0f), near = sqrtf(PAN_POWER - far * far);
    s->gain = gain; s->cutoff = cutoff;
    s->left = separation < 128 ? near : far;
    s->right = separation < 128 ? far : near;
}

static float plain_fade(float distance, const void *data) { (void)data; return LevelSound_Gain(distance, 0, 1); }

void RoomReverbs_Path(const room_record_t *record, float *gain, float *cutoff, float *from_x, float *from_y)
{
    *gain = 0; *cutoff = THROUGH_WALL_HZ; *from_x = record->x; *from_y = record->y;
    const mobj_t *listener = players[consoleplayer].mo;
    if (!listener) return;
    Acoustic_Follow();
    acoustic_heard_t heard;
    float lx = listener->x / (float)FRACUNIT, ly = listener->y / (float)FRACUNIT;
    if (Acoustic_Heard(record->x, record->y, record->sector, plain_fade, NULL, &heard))
    {
        *gain = heard.gain; *cutoff = heard.cutoff;
        *from_x = heard.from_x; *from_y = heard.from_y;
    }
    else *gain = LevelSound_Gain(hypotf(record->x - lx, record->y - ly), THROUGH_WALL_DB, 1);
}

void RoomReverbs_Begin(int count)
{
    blocked = count < BLOCK ? count : BLOCK;
    for (int i = 0; i < ROOM_SLOTS; ++i)
        if (slots[i].key >= 0) memset(slots[i].send, 0, (size_t)blocked * sizeof(float));
}

void RoomReverbs_AddOwn(const float *own, int count)
{
    count = count < blocked ? count : blocked;
    for (int i = 0; i < ROOM_SLOTS; ++i)
    {
        slot_t *s = &slots[i];
        if (s->key < 0 || s->own <= 0) continue;
        for (int k = 0; k < count; ++k) s->send[k] += own[k] * s->own;
    }
}

float *RoomReverbs_Send(int slot)
{
    return slot >= 0 && slot < ROOM_SLOTS && slots[slot].key >= 0 ? slots[slot].send : NULL;
}

void RoomReverbs_Mix(float *stereo, int count, float amount)
{
    float slow_in[BLOCK / DOWN + 1], ring[(BLOCK / DOWN + 1) * 2], slow_out[BLOCK / DOWN + 1];
    count = count < blocked ? count : blocked;
    float glide = 1 - expf(-count / (GLIDE_SECONDS * rate));
    const float slow_rate = (float)rate / DOWN;
    for (int i = 0; i < ROOM_SLOTS; ++i)
    {
        slot_t *s = &slots[i];
        if (s->key < 0 || !s->reverb) continue;
        /* Down to the slow rate, a box average of every DOWN samples. */
        int start = s->phase, n = 0;
        for (int k = 0; k < count; ++k)
        {
            s->gathered += s->send[k];
            if (++s->phase == DOWN) { slow_in[n++] = s->gathered / DOWN; s->gathered = 0; s->phase = 0; }
        }
        /* The traced tail's history is fed even before its room is traced. */
        float traced[BLOCK / DOWN + 1];
        if (s->tail) FarTail_Run(s->tail, s->traced, slow_in, traced, n);
        float blend_from = s->blend;
        if (s->traced && s->blend < 1) s->blend = fminf(1, s->blend + count / (BLEND_SECONDS * rate));
        if (blend_from < 1)
        {
            memset(ring, 0, (size_t)n * 2 * sizeof(float));
            if (n) Reverb_Process(s->reverb, slow_in, ring, n, amount);
        }
        float gain = s->now_gain, left = s->now_left, right = s->now_right;
        s->now_gain += (s->gain - s->now_gain) * glide;
        s->now_left += (s->left - s->now_left) * glide;
        s->now_right += (s->right - s->now_right) * glide;
        s->now_cutoff *= powf(s->cutoff / fmaxf(s->now_cutoff, 1), glide);
        float keep = expf(-2 * 3.14159265f * fminf(s->now_cutoff, 0.45f * slow_rate) / slow_rate);
        for (int k = 0; k < n; ++k)
        {
            float b = blend_from + (s->blend - blend_from) * k / (float)(n > 0 ? n : 1);
            float value = (b > 0 ? b * amount * traced[k] : 0) + (b < 1 ? (1 - b) * 0.5f * (ring[2 * k] + ring[2 * k + 1]) : 0);
            s->low[0] = value + (s->low[0] - value) * keep;
            s->low[1] = s->low[0] + (s->low[1] - s->low[0]) * keep;
            slow_out[k] = s->low[1];
        }
        /* Back up to the output rate, gliding from one slow sample to the next. */
        float step = 1.0f / count;
        int phase = start, m = 0;
        for (int k = 0; k < count; ++k)
        {
            if (++phase == DOWN) { phase = 0; s->before = s->after; s->after = slow_out[m++]; }
            float t = k * step, g = gain + (s->now_gain - gain) * t;
            float value = (s->before + (s->after - s->before) * (phase / (float)DOWN)) * g;
            stereo[2 * k] += value * (left + (s->now_left - left) * t);
            stereo[2 * k + 1] += value * (right + (s->now_right - right) * t);
        }
    }
}
