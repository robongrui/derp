#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "doomdef.h"
#include "doomstat.h"
#include "i_sound.h"
#include "i_mixer_music.h"
#include "i_music.h"
#include "m_argv.h"
#include "w_wad.h"
#include "z_zone.h"
#include "audio/ambience.h"
#include "audio/ambience_voices.h"
#include "audio/impact_synth.h"
#include "audio/made_bank.h"
#include "audio/occlusion.h"
#include "audio/steps.h"
#include "audio/resample.h"
#include "audio/treble.h"
#include "audio/reverb.h"
#include "game_profile.h"
#include "audio/sf2_synth.h"
#include "audio/sound_options.h"
#include <SDL3/SDL.h>

#define OUTPUT_RATE 44100
#define MIX_CHANNELS 32
/* SoundFont voices, allocated up front: GeneralUser GS peaks at 286 (D_SHAWN). */
#define SF2_VOICES 512
/* Effects at full volume, center: headroom for a few at once before the limiter. */
#define SFX_GAIN 0.75f
#define LIMIT 0.9f
#define LIMIT_RELEASE_SECONDS 0.15f
/* How fast a sound's muffling follows it moving behind or out from a wall. */
#define MUFFLE_GLIDE_SECONDS 0.08f
/* Ambience at full F4 amount and full effects volume, next to a point-blank shot. */
#define AMBIENCE_LEVEL 0.35f
/* Ambience ducks under loud effects at once and comes back over this long. */
#define DUCK_RELEASE_SECONDS 1.2f
typedef struct { const byte *data; int length, rate; } sample_t;
typedef struct {
    sample_t sample;
    double position, step;
    float left, right, send;
    /* Lowpass: cutoff glides to target; two one-pole stages of state. */
    float cutoff, target, low[2];
    treble_t treble;
    int handle, measured;
} mix_channel_t;
static sample_t samples[NUMSFX];
static mix_channel_t channels[MIX_CHANNELS];
static SDL_AudioStream *stream;
static int next_handle = 1;
static int acoustics = 1, muffle_walls = 1, muffle_distance = 1;
static int resampling = RESAMPLE_ORIGINAL;
static float treble_amount = 1;
static float next_cutoff = OCCLUSION_OPEN_HZ;
static float reverb_amount = 1;
static float limiter_gain = 1;
static int made_impacts = 1, glass_fracture, ambience = 1, footsteps = 1;
static float ambience_amount = 1, duck = 1;

static unsigned little16(const byte *p) { return p[0] | (unsigned)p[1] << 8; }
static uint32_t little32(const byte *p)
{
    return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
/* volume is DOOM's 0-15 effects scale, already lowered with distance.
   Panning follows DMX: full level on both sides at the center (separation
   128), fading on the far side only. The reverb send falls off slower than
   the dry sound, so far sounds come out wetter. */
static void gains(mix_channel_t *channel, float volume, int separation, int pitch)
{
    float level = SDL_clamp(volume, 0.0f, 15.0f) / 15.0f;
    separation = SDL_clamp(separation, 1, 254);
    channel->left = SFX_GAIN * level * SDL_min(1.0f, (254 - separation) / 127.0f);
    channel->right = SFX_GAIN * level * SDL_min(1.0f, separation / 127.0f);
    channel->send = SFX_GAIN * sqrtf(level);
    channel->step = channel->sample.rate / (double)OUTPUT_RATE * pow(2.0, (pitch - 128) / 64.0);
    Treble_Tune(&channel->treble, (float)(0.5 * channel->step * OUTPUT_RATE), OUTPUT_RATE);
}

static void audio(void *userdata, SDL_AudioStream *output, int additional, int total)
{
    (void)userdata; (void)total;
    float buffer[512 * 2], send[512];
    int frames = (additional + (int)sizeof(float)*2 - 1) / ((int)sizeof(float)*2);
    while (frames > 0)
    {
        int count = SDL_min(frames, 512);
        memset(buffer, 0, (size_t)count * 2 * sizeof(float));
        memset(send, 0, (size_t)count * sizeof(float));
        for (int c = 0; c < MIX_CHANNELS; ++c)
        {
            mix_channel_t *channel = &channels[c];
            if (!channel->handle) continue;
            channel->cutoff *= powf(channel->target / channel->cutoff, 1 - expf(-count / (MUFFLE_GLIDE_SECONDS * OUTPUT_RATE)));
            float keep = expf(-2 * SDL_PI_F * channel->cutoff / OUTPUT_RATE);
            /* Held samples bring their own sizzle up there instead. */
            int treble = resampling != RESAMPLE_ORIGINAL && treble_amount > 0;
            for (int i = 0; i < count; ++i)
            {
                int position = (int)channel->position;
                if (position >= channel->sample.length) { channel->handle = 0; break; }
                float value = Resample_At(channel->sample.data, channel->sample.length, channel->position, resampling);
                if (treble) value = Treble_Process(&channel->treble, value, treble_amount);
                channel->low[0] = value + (channel->low[0] - value) * keep;
                channel->low[1] = channel->low[0] + (channel->low[1] - channel->low[0]) * keep;
                value = channel->low[1];
                buffer[2*i] += value * channel->left;
                buffer[2*i+1] += value * channel->right;
                send[i] += value * channel->send;
                channel->position += channel->step;
            }
        }
        /* Ambience ducks under what the effects play, then fades back. */
        float peak = 0;
        for (int i = 0; i < 2 * count; ++i) peak = SDL_max(peak, fabsf(buffer[i]));
        float target = 1 / (1 + 1.5f * peak);
        duck += (target - duck) * (target < duck ? 0.5f : 1 - expf(-count / (DUCK_RELEASE_SECONDS * OUTPUT_RATE)));
        AmbienceVoices_Mix(buffer, send, count, OUTPUT_RATE, resampling, SFX_GAIN, duck);
        if (acoustics) Reverb_Process(send, buffer, count, reverb_amount);
        MixerMusic_Render(buffer, count);
        /* A peak limiter: drops at once to keep the mix under LIMIT, recovers
           slowly, so a burst of shots ducks the mix instead of crackling. */
        float release = 1 - expf(-1.0f / (LIMIT_RELEASE_SECONDS * OUTPUT_RATE));
        for (int i = 0; i < count; ++i)
        {
            float peak = SDL_max(fabsf(buffer[2*i]), fabsf(buffer[2*i+1]));
            limiter_gain += (1 - limiter_gain) * release;
            if (peak * limiter_gain > LIMIT) limiter_gain = LIMIT / peak;
            buffer[2*i] = SDL_clamp(buffer[2*i] * limiter_gain, -1.0f, 1.0f);
            buffer[2*i+1] = SDL_clamp(buffer[2*i+1] * limiter_gain, -1.0f, 1.0f);
        }
        if (!SDL_PutAudioStreamData(output, buffer, count * 2 * (int)sizeof(float))) return;
        frames -= count;
    }
}

void I_InitSound(void)
{
    if (M_CheckParm("-nosound")) return;
    for (int i = 1; i < NUMSFX; ++i)
    {
        int lump = I_GetSfxLumpNum(&S_sfx[i]);
        int length = W_LumpLength(lump);
        byte *data = W_CacheLumpNum(lump, PU_SOUND);
        Game_SfxLoaded(i, data);
        if (length < 8 || little16(data) != 3) continue;
        uint32_t size = little32(data + 4);
        if (!size || size > (uint32_t)(length - 8)) continue;
        samples[i] = (sample_t){ data + 8, (int)size, (int)little16(data + 2) };
    }
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO))
    { fprintf(stderr, "Audio unavailable: %s\n", SDL_GetError()); return; }
    SDL_AudioSpec spec = { SDL_AUDIO_F32, 2, OUTPUT_RATE };
    MadeBank_Build();
    Reverb_Init(OUTPUT_RATE);
    Resample_Init();
    stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, audio, NULL);
    if (!stream) { fprintf(stderr, "Audio unavailable: %s\n", SDL_GetError()); return; }
    SDL_ResumeAudioStreamDevice(stream);
    fprintf(stderr, "SDL sound output ready.\n");
}
void I_ShutdownSound(void)
{
    SDL_DestroyAudioStream(stream); stream = NULL;
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
}
static void play_at(const made_sample_t *sample, float x, float y, float z, float level);
static void play_kind(int kind, float x, float y, float z, float level);

/* Every tic the ambience voices follow the listener; every other the room is
   measured for the reverb. */
void I_UpdateSound(void)
{
    static int measured = -1, ambient = -1;
    if (!stream) return;
    if (gametic != ambient)
    {
        ambient = gametic;
        ambience_voice_t voices[AMBIENT_KINDS][AMBIENCE_LAYERS];
        ambience_strike_t strike;
        float level = ambience ? ambience_amount * AMBIENCE_LEVEL * SDL_clamp(snd_SfxVolume, 0, 15) / 15.0f : 0;
        Ambience_Update(voices, level, muffle_walls, muffle_distance, &strike);
        SDL_LockAudioStream(stream);
        AmbienceVoices_Set(voices);
        SDL_UnlockAudioStream(stream);
        if (strike.due) play_at(MadeBank_Strike(gametic), strike.x, strike.y, strike.z, strike.level);
        step_event_t steps[4];
        int count = footsteps ? Steps_Tic(steps, 4) : 0;
        for (int i = 0; i < count; ++i) play_kind(steps[i].kind, steps[i].x, steps[i].y, steps[i].z, steps[i].level);
    }
    if (!acoustics || (gametic >= measured && gametic - measured < 2)) return;
    measured = gametic;
    room_t room;
    Room_Measure(&room);
    SDL_LockAudioStream(stream);
    Reverb_SetRoom(&room);
    SDL_UnlockAudioStream(stream);
}
void I_SetSoundOptions(int enabled, float amount, int walls, int distance)
{
    if (stream) SDL_LockAudioStream(stream);
    if (enabled && !acoustics) Reverb_Clear();
    acoustics = enabled != 0;
    reverb_amount = SDL_clamp(amount, 0.0f, 2.0f);
    muffle_walls = walls != 0;
    muffle_distance = distance != 0;
    if (stream) SDL_UnlockAudioStream(stream);
}
void I_SetSfxPlayback(int mode, float treble)
{
    if (stream) SDL_LockAudioStream(stream);
    resampling = SDL_clamp(mode, RESAMPLE_ORIGINAL, RESAMPLE_CLEAN);
    treble_amount = SDL_clamp(treble, 0.0f, 2.0f);
    if (stream) SDL_UnlockAudioStream(stream);
}

static mix_channel_t *find(int handle)
{
    for (int i = 0; i < MIX_CHANNELS; ++i)
        if (channels[i].handle == handle) return &channels[i];
    return NULL;
}
/* Measured when a sound starts, then every other tic as it or the listener moves. */
void I_SoundAt(int handle, fixed_t x, fixed_t y, fixed_t z)
{
    if (!stream) return;
    if (!handle) { next_cutoff = Occlusion_Cutoff(x, y, z, muffle_walls, muffle_distance); return; }
    SDL_LockAudioStream(stream);
    mix_channel_t *channel = find(handle);
    int due = channel && (gametic < channel->measured || gametic - channel->measured >= 2);
    if (due) channel->measured = gametic;
    SDL_UnlockAudioStream(stream);
    if (!due) return;
    float cutoff = Occlusion_Cutoff(x, y, z, muffle_walls, muffle_distance);
    SDL_LockAudioStream(stream);
    if ((channel = find(handle))) channel->target = cutoff;
    SDL_UnlockAudioStream(stream);
}
void I_SubmitSound(void) { }
void I_SetChannels(void) { }
/* Takes the free channel, or the oldest. */
static int start_sample(sample_t sample, float volume, int separation, int pitch, float cutoff)
{
    SDL_LockAudioStream(stream);
    int slot = 0;
    for (int i = 0; i < MIX_CHANNELS; ++i)
    {
        if (!channels[i].handle) { slot = i; break; }
        if (channels[i].handle < channels[slot].handle) slot = i;
    }
    mix_channel_t *channel = &channels[slot];
    channel->sample = sample;
    channel->position = 0;
    channel->cutoff = channel->target = cutoff;
    channel->low[0] = channel->low[1] = 0;
    Treble_Reset(&channel->treble);
    channel->measured = gametic;
    if (next_handle == INT_MAX) next_handle = 1;
    channel->handle = next_handle++;
    gains(channel, volume, separation, pitch);
    int handle = channel->handle;
    SDL_UnlockAudioStream(stream);
    return handle;
}
int I_StartSound(int id, int volume, int separation, int pitch, int priority)
{
    (void)priority;
    float cutoff = next_cutoff;
    next_cutoff = OCCLUSION_OPEN_HZ;
    if (!stream || id < 1 || id >= NUMSFX || !samples[id].data) return 0;
    return start_sample(samples[id], (float)volume, separation, pitch, cutoff);
}

/* A made-up sound at a map point, loud and panned as DOOM does it for a
   thing there (S_AdjustSoundParams), muffled like any effect. level scales
   the effects volume. */
static void play_at(const made_sample_t *sample, float x, float y, float z, float level)
{
    const mobj_t *listener = players[consoleplayer].mo;
    if (!stream || !sample || !listener || gamestate != GS_LEVEL || level <= 0) return;
    float dx = x - listener->x / (float)FRACUNIT, dy = y - listener->y / (float)FRACUNIT;
    float adx = fabsf(dx), ady = fabsf(dy), distance = adx + ady - SDL_min(adx, ady) / 2;
    if (distance > 1200) return;
    float volume = snd_SfxVolume * level * (distance < 160 ? 1 : (1200 - distance) / 1040);
    float angle = atan2f(dy, dx) - listener->angle * (2 * SDL_PI_F / 4294967296.0f);
    int separation = 128 - (int)(96 * sinf(angle));
    float cutoff = Occlusion_Cutoff((fixed_t)(x * FRACUNIT), (fixed_t)(y * FRACUNIT), (fixed_t)(z * FRACUNIT),
                                    muffle_walls, muffle_distance);
    start_sample((sample_t){ sample->data, sample->length, sample->rate }, volume, separation, 128, cutoff);
}

/* A variation of the kind, never the same as the last of it. */
static void play_kind(int kind, float x, float y, float z, float level)
{
    static unsigned last[IMPACT_KINDS], turn = 0x1234567u;
    if (kind < 0 || kind >= IMPACT_KINDS) return;
    turn = turn * 1103515245u + 12345u;
    unsigned variant = (last[kind] + 1 + (turn >> 16) % (MADE_VARIANTS - 1)) % MADE_VARIANTS;
    last[kind] = variant;
    play_at(MadeBank_Impact(kind, (int)variant), x, y, z, level);
}
void I_MadeSound(int kind, float x, float y, float z)
{
    if (!made_impacts || kind < 0 || kind >= IMPACT_KINDS) return;
    if (glass_fracture && kind == IMPACT_GLASS_BREAK) kind = IMPACT_GLASS_FRACTURE;
    if (glass_fracture && kind == IMPACT_GLASS_BIG) kind = IMPACT_GLASS_FRACTURE_BIG;
    float level = kind <= IMPACT_SPARK_ZAP ? 0.6f : kind <= IMPACT_SCREEN_DIES ? 0.7f
                : kind == IMPACT_GLASS_TINKLE ? 0.6f : 1.0f;
    play_kind(kind, x, y, z, level);
}
void I_AmbienceSilence(float x, float y, float z, float radius) { Ambience_Silence(x, y, z, radius); }
void I_AmbienceGlaze(int line, int whole) { Ambience_Glaze(line, whole); }
void I_AmbienceShowGlass(int shown) { Ambience_ShowGlass(shown); }
void I_SetMadeSounds(int impacts, int glass, int ambient, float amount, int steps)
{
    if (stream) SDL_LockAudioStream(stream);
    made_impacts = impacts != 0;
    footsteps = steps != 0;
    glass_fracture = glass == 1;
    if (!ambient && ambience) AmbienceVoices_Clear();
    ambience = ambient != 0;
    ambience_amount = SDL_clamp(amount, 0.0f, 2.0f);
    if (stream) SDL_UnlockAudioStream(stream);
}
void I_StopSound(int handle)
{
    if (!stream) return;
    SDL_LockAudioStream(stream);
    for (int i = 0; i < MIX_CHANNELS; ++i)
        if (channels[i].handle == handle) channels[i].handle = 0;
    SDL_UnlockAudioStream(stream);
}
int I_SoundIsPlaying(int handle)
{
    if (!stream || !handle) return 0;
    int playing = 0;
    SDL_LockAudioStream(stream);
    for (int i = 0; i < MIX_CHANNELS; ++i)
        if (channels[i].handle == handle) playing = 1;
    SDL_UnlockAudioStream(stream);
    return playing;
}
void I_UpdateSoundParams(int handle, int volume, int separation, int pitch)
{
    if (!stream) return;
    SDL_LockAudioStream(stream);
    for (int i = 0; i < MIX_CHANNELS; ++i)
        if (channels[i].handle == handle) gains(&channels[i], (float)volume, separation, pitch);
    SDL_UnlockAudioStream(stream);
}

/* OPL and SoundFont music render inside the mixer above, so these take
   the stream lock. */
static void lock_music(void) { if (stream) SDL_LockAudioStream(stream); }
static void unlock_music(void) { if (stream) SDL_UnlockAudioStream(stream); }

void I_Mixer_UnRegisterSong(void) { lock_music(); MixerMusic_UnRegister(); unlock_music(); }
void I_Mixer_SetMusicVolume(int volume) { lock_music(); MixerMusic_SetVolume(volume); unlock_music(); }
void I_Mixer_PauseSong(void) { lock_music(); MixerMusic_Pause(); unlock_music(); }
void I_Mixer_ResumeSong(void) { lock_music(); MixerMusic_Resume(); unlock_music(); }
void I_Mixer_StopSong(void) { lock_music(); MixerMusic_Stop(); unlock_music(); }
void I_Mixer_PlaySong(int looping) { lock_music(); MixerMusic_Play(looping != 0); unlock_music(); }
int I_Mixer_RegisterSong(void *data, int soundfont)
{
    int size = 0;
    if (!stream) return 0;
    for (int i = 0; i < numlumps; ++i)
        if (lumpcache[i] == data) { size = W_LumpLength(i); break; }
    lock_music();
    int registered = MixerMusic_Register(data, size, OUTPUT_RATE, soundfont != 0);
    unlock_music();
    return registered;
}

/* Reading a bank takes a while, so only the swap holds the lock. */
int I_Mixer_LoadSoundFont(const char *path)
{
    sf2_bank_t *bank = SF2_Open(path, OUTPUT_RATE, SF2_VOICES);
    if (!bank) return 0;
    lock_music();
    sf2_bank_t *last = MixerMusic_UseBank(bank);
    unlock_music();
    SF2_Close(last);
    return 1;
}
