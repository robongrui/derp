#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "doomdef.h"
#include "doomstat.h"
#include "i_sound.h"
#include "i_opl_music.h"
#include "m_argv.h"
#include "w_wad.h"
#include "z_zone.h"
#include "audio/occlusion.h"
#include "audio/reverb.h"
#include "audio/sound_options.h"
#include <SDL3/SDL.h>

#define OUTPUT_RATE 44100
#define MIX_CHANNELS 32
/* Effects at full volume, center: headroom for a few at once before the limiter. */
#define SFX_GAIN 0.75f
#define LIMIT 0.9f
#define LIMIT_RELEASE_SECONDS 0.15f
/* How fast a sound's muffling follows it moving behind or out from a wall. */
#define MUFFLE_GLIDE_SECONDS 0.08f
typedef struct { const byte *data; int length, rate; } sample_t;
typedef struct {
    sample_t sample;
    double position, step;
    float left, right, send;
    /* Lowpass: cutoff glides to target; two one-pole stages of state. */
    float cutoff, target, low[2];
    int handle, measured;
} channel_t;
static sample_t samples[NUMSFX];
static channel_t channels[MIX_CHANNELS];
static SDL_AudioStream *stream;
static int next_handle = 1;
static int acoustics = 1, muffle_walls = 1, muffle_distance = 1;
static float next_cutoff = OCCLUSION_OPEN_HZ;
static float reverb_amount = 1;
static float limiter_gain = 1;

static unsigned little16(const byte *p) { return p[0] | (unsigned)p[1] << 8; }
static uint32_t little32(const byte *p)
{
    return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
/* volume is DOOM's 0-15 effects scale, already lowered with distance.
   Panning follows DMX: full level on both sides at the center (separation
   128), fading on the far side only. The reverb send falls off slower than
   the dry sound, so far sounds come out wetter. */
static void gains(channel_t *channel, int volume, int separation, int pitch)
{
    float level = SDL_clamp(volume, 0, 15) / 15.0f;
    separation = SDL_clamp(separation, 1, 254);
    channel->left = SFX_GAIN * level * SDL_min(1.0f, (254 - separation) / 127.0f);
    channel->right = SFX_GAIN * level * SDL_min(1.0f, separation / 127.0f);
    channel->send = SFX_GAIN * sqrtf(level);
    channel->step = channel->sample.rate / (double)OUTPUT_RATE * pow(2.0, (pitch - 128) / 64.0);
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
            channel_t *channel = &channels[c];
            if (!channel->handle) continue;
            channel->cutoff *= powf(channel->target / channel->cutoff, 1 - expf(-count / (MUFFLE_GLIDE_SECONDS * OUTPUT_RATE)));
            float keep = expf(-2 * SDL_PI_F * channel->cutoff / OUTPUT_RATE);
            for (int i = 0; i < count; ++i)
            {
                int position = (int)channel->position;
                if (position >= channel->sample.length) { channel->handle = 0; break; }
                float value = ((int)channel->sample.data[position] - 128) / 128.0f;
                channel->low[0] = value + (channel->low[0] - value) * keep;
                channel->low[1] = channel->low[0] + (channel->low[1] - channel->low[0]) * keep;
                value = channel->low[1];
                buffer[2*i] += value * channel->left;
                buffer[2*i+1] += value * channel->right;
                send[i] += value * channel->send;
                channel->position += channel->step;
            }
        }
        if (acoustics) Reverb_Process(send, buffer, count, reverb_amount);
        OPL_RenderMusic(buffer, count);
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

int I_GetSfxLumpNum(sfxinfo_t *sfx)
{
    char name[9];
    if (sfx->link) sfx = sfx->link;
    snprintf(name, sizeof(name), "ds%s", sfx->name);
    int lump = W_CheckNumForName(name);
    return lump >= 0 ? lump : W_GetNumForName("dspistol");
}
void I_InitSound(void)
{
    if (M_CheckParm("-nosound")) return;
    for (int i = 1; i < NUMSFX; ++i)
    {
        int lump = I_GetSfxLumpNum(&S_sfx[i]);
        int length = W_LumpLength(lump);
        byte *data = W_CacheLumpNum(lump, PU_SOUND);
        S_sfx[i].data = data;
        if (length < 8 || little16(data) != 3) continue;
        uint32_t size = little32(data + 4);
        if (!size || size > (uint32_t)(length - 8)) continue;
        samples[i] = (sample_t){ data + 8, (int)size, (int)little16(data + 2) };
    }
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO))
    { fprintf(stderr, "Audio unavailable: %s\n", SDL_GetError()); return; }
    SDL_AudioSpec spec = { SDL_AUDIO_F32, 2, OUTPUT_RATE };
    Reverb_Init(OUTPUT_RATE);
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
/* Measures the room every other tic and hands it to the reverb. */
void I_UpdateSound(void)
{
    static int measured = -1;
    if (!stream || !acoustics) return;
    if (gametic >= measured && gametic - measured < 2) return;
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

static channel_t *find(int handle)
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
    channel_t *channel = find(handle);
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
int I_StartSound(int id, int volume, int separation, int pitch, int priority)
{
    (void)priority;
    float cutoff = next_cutoff;
    next_cutoff = OCCLUSION_OPEN_HZ;
    if (!stream || id < 1 || id >= NUMSFX || !samples[id].data) return 0;
    SDL_LockAudioStream(stream);
    int slot = 0;
    for (int i = 0; i < MIX_CHANNELS; ++i)
    {
        if (!channels[i].handle) { slot = i; break; }
        if (channels[i].handle < channels[slot].handle) slot = i;
    }
    channel_t *channel = &channels[slot];
    channel->sample = samples[id];
    channel->position = 0;
    channel->cutoff = channel->target = cutoff;
    channel->low[0] = channel->low[1] = 0;
    channel->measured = gametic;
    if (next_handle == INT_MAX) next_handle = 1;
    channel->handle = next_handle++;
    gains(channel, volume, separation, pitch);
    int handle = channel->handle;
    SDL_UnlockAudioStream(stream);
    return handle;
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
        if (channels[i].handle == handle) gains(&channels[i], volume, separation, pitch);
    SDL_UnlockAudioStream(stream);
}

/* OPL music renders inside the mixer above, so these take the stream lock. */
static void lock_music(void) { if (stream) SDL_LockAudioStream(stream); }
static void unlock_music(void) { if (stream) SDL_UnlockAudioStream(stream); }

void I_OPL_ShutdownMusic(void) { lock_music(); OPL_UnRegisterSong(); unlock_music(); }
void I_OPL_SetMusicVolume(int volume) { lock_music(); OPL_SetMusicVolume(volume); unlock_music(); }
void I_OPL_PauseSong(void) { lock_music(); OPL_PauseSong(); unlock_music(); }
void I_OPL_ResumeSong(void) { lock_music(); OPL_ResumeSong(); unlock_music(); }
void I_OPL_StopSong(void) { lock_music(); OPL_StopSong(); unlock_music(); }
void I_OPL_PlaySong(int looping) { lock_music(); OPL_PlaySong(looping != 0); unlock_music(); }
int I_OPL_RegisterSong(void *data)
{
    int size = 0;
    if (!stream) return 0;
    for (int i = 0; i < numlumps; ++i)
        if (lumpcache[i] == data) { size = W_LumpLength(i); break; }
    lock_music();
    int registered = OPL_InitMusic(OUTPUT_RATE) && OPL_RegisterSong(data, size);
    unlock_music();
    return registered;
}

#ifndef __APPLE__
/* The Mac uses its system synthesizer unless -opl is given; see i_mac_music.c. */
void I_InitMusic(void) { }
void I_ShutdownMusic(void) { I_OPL_ShutdownMusic(); }
void I_SetMusicVolume(int volume) { I_OPL_SetMusicVolume(volume); }
void I_PauseSong(int handle) { (void)handle; I_OPL_PauseSong(); }
void I_ResumeSong(int handle) { (void)handle; I_OPL_ResumeSong(); }
int I_RegisterSong(void *data) { return I_OPL_RegisterSong(data); }
void I_PlaySong(int handle, int looping) { if (handle) I_OPL_PlaySong(looping); }
void I_StopSong(int handle) { (void)handle; I_OPL_StopSong(); }
void I_UnRegisterSong(int handle) { (void)handle; I_OPL_ShutdownMusic(); }
#endif
