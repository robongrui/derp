#include <math.h>
#include <stdlib.h>
#include <string.h>
extern double I_TimeScale;
#include "doomdef.h"
#include "doomstat.h"
#include "i_sound.h"
#include "i_mixer_music.h"
#include "i_music.h"
#include "m_argv.h"
#include "r_state.h"
#include "w_wad.h"
#include "z_zone.h"
#include "audio/room.h"
#include "audio/ambience.h"
#include "audio/ambience_voices.h"
#include "audio/beyond.h"
#include "audio/flyby.h"
#include "audio/hidden.h"
#include "audio/impact_synth.h"
#include "audio/made_bank.h"
#include "audio/occlusion.h"
#include "audio/steps.h"
#include "audio/resample.h"
#include "audio/treble.h"
#include "audio/vacuum.h"
#include "audio/reverb.h"
#include "audio/ringing.h"
#include "audio/head_knock.h"
#include "audio/slow_whoosh.h"
#include "audio/falling_air.h"
#include "audio/hrtf.h"
#include "audio/wind_voice.h"
#include "audio/library_acoustics.h"
#include "audio/level_sound.h"
#include "audio/room_grid.h"
#include "audio/room_records.h"
#include "audio/room_tails.h"
#include "audio/room_records.h"
#include "audio/room_reverbs.h"
#include "game_profile.h"
#include "audio/sf2_synth.h"
#include "audio/sound_options.h"
#include "i_sdl_mixer.h"
#include "gamepad_rumble.h"
#include <SDL3/SDL.h>

#define OUTPUT_RATE 44100
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
/* How fast a sound's share of the space beyond follows it, and how fast
   the way into that space opens and closes as the listener moves. */
#define BEYOND_GLIDE_SECONDS 0.15f
#define COUPLING_GLIDE_SECONDS 0.5f
/* How fast a sound fades as the air carrying it thins (vacuum.h). */
#define AIR_GLIDE_SECONDS 0.25f
/* Pan and level arrive once a tic; between tics they glide, or a fast turn ticks. */
#define PAN_GLIDE_SECONDS 0.015f
/* DOOM's stereo swing, and how near a sound is before it has no side: at the
   listener a sound's direction means nothing. */
#define PAN_SWING 96
#define PAN_NEAR 64.0f
/* Both sides' power at the center, where DMX has them 126/127 and 1. */
#define PAN_POWER (1 + (126.0f / 127) * (126.0f / 127))
/* How much of the way to the opening it comes through a sound turns each tic,
   and the furthest it turns: a hint of the way round, never the other ear. */
#define BEND_GLIDE 0.2f
#define BEND_MOST (SDL_PI_F / 4)
/* Where the listener's ears are above their feet (occlusion.c's EAR). */
#define EAR_HEIGHT 41.0f
/* Ambience ducks under loud effects at once and comes back over this long. */
#define DUCK_RELEASE_SECONDS 1.2f
/* Authored maps (level_sound.h): far-off sounds at once, at most; how much a
   close fight ducks them (at once) and how slowly they come back. */
#define FAR_VOICES 6
#define FAR_DUCK 2.0f
#define FAR_DUCK_RELEASE_SECONDS 2.5f
sample_t samples[NUMSFX];
mix_channel_t channels[MIX_CHANNELS];
SDL_AudioStream *stream;
static int next_handle = 1;
static int acoustics = 1, muffle_walls = 1, muffle_distance = 1;
static int resampling = RESAMPLE_ORIGINAL;
static float treble_amount = 1;
static float next_cutoff = OCCLUSION_OPEN_HZ;
static float next_x, next_y, next_air = 1, next_air_hz = OCCLUSION_OPEN_HZ;
static float next_z, next_bend, next_way_x, next_way_y;
static int next_placed, next_own, natural_falloff = 1;
/* 0 speakers; past that, the head on headphones. */
static int sound_output;
static float reverb_amount = 1;
/* The listener's room, and the bigger space beyond it (measured on the game
   thread; the mixer only has its coupling). */
static reverb_t *room_reverb, *beyond_reverb;
beyond_t beyond;
static float coupling, coupling_target, next_beyond;
static float limiter_gain = 1;
static int made_impacts = 1, glass_fracture, ambience = 1, footsteps = 1;
static float ambience_amount = 1, duck = 1;
static float next_heard = 1, far_duck = 1;
static int next_key = -1;
int own_tic = -1000;
/* -levelsoundlog: time spent on level sound, game thread (tics) and mixer. */
Uint64 spent_game_ns, spent_mix_ns, spent_audio_ns;
Uint64 room_ns; int room_live, room_baked; /* the listener's room, measured or from the grid */
/* The next play_through is a far sound (I_FarSound); the last one started. */
int through_far, last_through;

static unsigned little16(const byte *p) { return p[0] | (unsigned)p[1] << 8; }
static uint32_t little32(const byte *p)
{
    return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
/* How much of a sound reaches the listener from distance units away (DOOM's
   rough distance), 0-1. DOOM fades it in a straight line from 160 to 1200
   units. The natural falloff loses half its strength each time the distance
   doubles, as in air, and eases out to nothing at the same 1200, so nothing
   new is heard and nothing pops off. */
static int natural(void) { return natural_falloff && Game_HasEffect(GAME_EFFECT_NATURAL_FALLOFF); }
static float distance_gain(float distance)
{
    if (distance < 160) return 1;
    if (distance >= 1200) return 0;
    if (!natural()) return (1200 - distance) / 1040;
    float t = distance <= 900 ? 0 : (distance - 900) / 300;
    return 160 / distance * (1 - t * t * (3 - 2 * t));
}
static float listener_distance(float x, float y)
{
    const mobj_t *listener = players[consoleplayer].mo;
    if (!listener) return 0;
    float adx = fabsf(x - listener->x / (float)FRACUNIT), ady = fabsf(y - listener->y / (float)FRACUNIT);
    return adx + ady - SDL_min(adx, ady) / 2;
}
/* The game's own sounds at a point keep its volume unless the natural
   falloff sets it from where they are. DOOM's boss maps (map 8) play every
   sound at full strength; they stay as they are. */
static int falls_off(void) { return natural() && gamemap != 8; }
/* Authored maps: sounds fade by the way through the level, not by distance
   alone; boss maps keep DOOM's every sound at full strength. */
int by_path(void) { return LevelSound_On() && gamemap != 8; }
/* Loud effects carry farther, but only as the natural falloff hears them:
   DOOM's own straight fade keeps its 1200 units. */
float reach_of(int id) { return (falls_off() || by_path()) && id > 0 && id < NUMSFX ? SDL_max(Game_SoundCarry(id), 1.0f) : 1; }
fixed_t I_SoundReach(int id) { return (fixed_t)(reach_of(id) * FRACUNIT); }

/* How much of a sound at a point reaches the listener: by the way there on
   authored maps, by distance otherwise. */
static float heard_from(float x, float y, float z, float reach)
{
    if (!by_path()) return distance_gain(listener_distance(x, y) / reach);
    occlusion_path_t path;
    Occlusion_Path((fixed_t)(x * FRACUNIT), (fixed_t)(y * FRACUNIT), (fixed_t)(z * FRACUNIT), &path);
    return LevelSound_Gain(path.length, path.loss, reach);
}
/* The room a sound at a point rings in, on authored maps. */
static int key_at(float x, float y)
{
    return LevelSound_On() ? RoomRecords_KeyAt(x, y) : -1;
}

/* Where a sound seems to be: as far as it is, but turned a little toward the
   opening it comes through, by as much as the way round is long. */
static void heard_at(const mix_channel_t *channel, float *x, float *y)
{
    const mobj_t *listener = players[consoleplayer].mo;
    *x = channel->x; *y = channel->y;
    if (!listener || channel->bend <= 0) return;
    float lx = listener->x / (float)FRACUNIT, ly = listener->y / (float)FRACUNIT;
    float dx = *x - lx, dy = *y - ly, wx = channel->way_x - lx, wy = channel->way_y - ly;
    float apart = hypotf(dx, dy), way = hypotf(wx, wy);
    if (apart < 1 || way < 1) return;
    float turn = SDL_clamp(atan2f(dx * wy - dy * wx, dx * wx + dy * wy), -BEND_MOST, BEND_MOST) * channel->bend;
    float heading = atan2f(dy, dx) + turn;
    *x = lx + cosf(heading) * apart; *y = ly + sinf(heading) * apart;
}
/* Which way a point lies from the listener's view, in radians, left positive. */
static float azimuth_to(float x, float y)
{
    const mobj_t *listener = players[consoleplayer].mo;
    if (!listener) return 0;
    float dx = x - listener->x / (float)FRACUNIT, dy = y - listener->y / (float)FRACUNIT;
    return atan2f(dy, dx) - listener->angle * (2 * SDL_PI_F / 4294967296.0f);
}
/* How much direction a sound there has: none at the listener, so something
   at arm's length can't snap from ear to ear. */
static float presence_at(float x, float y) { return SDL_min(listener_distance(x, y) / PAN_NEAR, 1.0f); }
/* DOOM's separation for a point (128 ahead or behind, 32 full left, 224 full
   right): the swing of S_AdjustSoundParams. */
int pan_at(float x, float y) { return 128 - (int)(PAN_SWING * sinf(azimuth_to(x, y)) * presence_at(x, y)); }
/* Up or down to a sound at height z, from the listener's ears. */
static float elevation_to(float x, float y, float z)
{
    const mobj_t *listener = players[consoleplayer].mo;
    if (!listener) return 0;
    float dx = x - listener->x / (float)FRACUNIT, dy = y - listener->y / (float)FRACUNIT;
    return atan2f(z - (listener->z / (float)FRACUNIT + EAR_HEIGHT), hypotf(dx, dy));
}
/* Turns a sound on headphones to where it is heard, at once for a new one. */
static void aim(mix_channel_t *channel, int start)
{
    if (!channel->spatial) return;
    float x, y, z = Occlusion_Height(channel->x, channel->y, channel->z);
    heard_at(channel, &x, &y);
    float azimuth = azimuth_to(x, y), elevation = elevation_to(x, y, z), presence = presence_at(x, y);
    if (start) Hrtf_Start(&channel->hrtf, sound_output - 1, azimuth, elevation, presence);
    else Hrtf_Aim(&channel->hrtf, azimuth, elevation, presence);
}
/* volume is DOOM's 0-15 effects scale, already lowered with distance.
   The far side fades as DMX's does, from full level at the center (separation
   128). DMX left the near side at full level too, so a sound lost power as it
   turned from ahead to beside the listener; here the near side makes that up,
   and a sound holds its power all the way round. The reverb send falls off
   slower than the dry sound, so far sounds come out wetter. */
void gains(mix_channel_t *channel, float volume, int separation, int pitch)
{
    float level = SDL_clamp(volume, 0.0f, 15.0f) / 15.0f;
    separation = SDL_clamp(separation, 1, 254);
    float far = SDL_min(1.0f, SDL_min(254 - separation, separation) / 127.0f), near = sqrtf(PAN_POWER - far * far);
    channel->left = SFX_GAIN * level * (separation < 128 ? near : far);
    channel->right = SFX_GAIN * level * (separation < 128 ? far : near);
    channel->gain = SFX_GAIN * level;
    /* A sound ringing in its own far room only stirs the listener's as much as
       it arrives; its room gets it as loud as it is made. */
    channel->send = SFX_GAIN * (channel->slot >= 0 ? level : sqrtf(level)) * channel->send_scale;
    channel->far_send = channel->slot >= 0 ? SFX_GAIN * sqrtf(SDL_clamp(channel->source, 0.0f, 1.0f)) * channel->send_scale : 0;
    channel->volume = volume; channel->separation = separation; channel->pitch = pitch;
    channel->step = channel->sample.rate / (double)OUTPUT_RATE * pow(2.0, (pitch - 128) / 64.0);
    Treble_Tune(&channel->treble, (float)(0.5 * channel->step * OUTPUT_RATE), OUTPUT_RATE);
}

static void audio(void *userdata, SDL_AudioStream *output, int additional, int total)
{
    (void)userdata; (void)total;
    Uint64 audio_start = SDL_GetTicksNS();
    float buffer[512 * 2], send[512], send_beyond[512], send_own[512], mono[512];
    const float pan_glide = 1 - expf(-1 / (PAN_GLIDE_SECONDS * OUTPUT_RATE));
    int frames = (additional + (int)sizeof(float)*2 - 1) / ((int)sizeof(float)*2);
    while (frames > 0)
    {
        int count = SDL_min(frames, 512);
        memset(buffer, 0, (size_t)count * 2 * sizeof(float));
        memset(send, 0, (size_t)count * sizeof(float));
        memset(send_beyond, 0, (size_t)count * sizeof(float));
        memset(send_own, 0, (size_t)count * sizeof(float));
        RoomReverbs_Begin(count);
        /* What is close and loud now (the far voices' own excluded) ducks the far ones. */
        float close = 0;
        for (int c = 0; c < MIX_CHANNELS; ++c)
            if (channels[c].handle && !channels[c].far) close += channels[c].now_gain;
        float duck_to = 1 / (1 + FAR_DUCK * close);
        far_duck += (duck_to - far_duck) * (duck_to < far_duck ? 1 : 1 - expf(-count / (FAR_DUCK_RELEASE_SECONDS * OUTPUT_RATE)));
        coupling += (coupling_target - coupling) * (1 - expf(-count / (COUPLING_GLIDE_SECONDS * OUTPUT_RATE)));
        for (int c = 0; c < MIX_CHANNELS; ++c)
        {
            mix_channel_t *channel = &channels[c];
            if (!channel->handle) continue;
            /* Slow motion: effects down with it, by the root (the world's quarter speed an octave down: two was mud). */
            double slowed = sqrt(I_TimeScale);
            channel->cutoff *= powf(channel->target / channel->cutoff, 1 - expf(-count / (MUFFLE_GLIDE_SECONDS * OUTPUT_RATE)));
            float keep = expf(-2 * SDL_PI_F * channel->cutoff / OUTPUT_RATE);
            /* A sound fills the space it is in and spills through the
               opening into the other: a little of the listener's own into
               the space beyond, a little of that space's into their room. */
            channel->beyond += (channel->beyond_target - channel->beyond) * (1 - expf(-count / (BEYOND_GLIDE_SECONDS * OUTPUT_RATE)));
            float there = channel->beyond;
            channel->air += (channel->air_target - channel->air) * (1 - expf(-count / (AIR_GLIDE_SECONDS * OUTPUT_RATE)));
            float air = channel->air;
            channel->now_send += (channel->send - channel->now_send) * (1 - expf(-count / (PAN_GLIDE_SECONDS * OUTPUT_RATE)));
            channel->now_far_send += (channel->far_send - channel->now_far_send) * (1 - expf(-count / (PAN_GLIDE_SECONDS * OUTPUT_RATE)));
            float *far_room = channel->slot >= 0 ? RoomReverbs_Send(channel->slot) : NULL, far_send = channel->now_far_send;
            /* The listener's own: it spills into the rooms next door. */
            int own = !channel->placed && !channel->anchored;
            float ducked = channel->far ? far_duck : 1;
            float send_here = channel->now_send * (1 - there + coupling * there);
            float send_there = channel->now_send * (there + coupling * sqrtf(coupling) * (1 - there));
            /* Held samples bring their own sizzle up there instead. */
            int treble = resampling != RESAMPLE_ORIGINAL && treble_amount > 0;
            float left = channel->now_left, right = channel->now_right, gain = channel->now_gain;
            int i;
            for (i = 0; i < count; ++i)
            {
                int position = (int)channel->position;
                if (position >= channel->sample.length) { channel->handle = 0; break; }
                float value = Resample_At(channel->sample.data, channel->sample.length, channel->position, resampling);
                if (treble) value = Treble_Process(&channel->treble, value, treble_amount);
                if (far_room) far_room[i] += value * air * far_send;
                channel->low[0] = value + (channel->low[0] - value) * keep;
                channel->low[1] = channel->low[0] + (channel->low[1] - channel->low[0]) * keep;
                value = channel->low[1] * air * ducked;
                if (own) send_own[i] += value * channel->now_send;
                if (channel->spatial)
                {
                    gain += (channel->gain - gain) * pan_glide;
                    mono[i] = value * gain;
                }
                else
                {
                    left += (channel->left - left) * pan_glide;
                    right += (channel->right - right) * pan_glide;
                    buffer[2*i] += value * left;
                    buffer[2*i+1] += value * right;
                }
                send[i] += value * send_here;
                send_beyond[i] += value * send_there;
                channel->position += channel->step * slowed;
            }
            channel->now_left = left; channel->now_right = right; channel->now_gain = gain;
            if (channel->spatial)
            {
                /* A sound that just ended still rings out of the filter. */
                if (i < count) memset(mono + i, 0, (size_t)(count - i) * sizeof *mono);
                Hrtf_Process(&channel->hrtf, mono, buffer, count);
            }
        }
        /* Ambience ducks under what the effects play, then fades back. */
        float peak = 0;
        for (int i = 0; i < 2 * count; ++i) peak = SDL_max(peak, fabsf(buffer[i]));
        float target = 1 / (1 + 1.5f * peak);
        duck += (target - duck) * (target < duck ? 0.5f : 1 - expf(-count / (DUCK_RELEASE_SECONDS * OUTPUT_RATE)));
        AmbienceVoices_Mix(buffer, send, count, OUTPUT_RATE, resampling, SFX_GAIN, duck);
        /* Slow motion: the room rings on as long again at a quarter speed, by the same root as the pitch. */
        Reverb_Stretch = (float)(1 / sqrt(SDL_max(I_TimeScale, 0.05)));
        if (acoustics && room_reverb && beyond_reverb)
        {
            Reverb_Process(room_reverb, send, buffer, count, reverb_amount);
            /* The space beyond is only heard through its opening. */
            Reverb_Process(beyond_reverb, send_beyond, buffer, count, reverb_amount * sqrtf(coupling));
            RoomTails_ProcessBeyond(send_beyond, buffer, count, reverb_amount * sqrtf(coupling));
            if (LevelSound_On())
            {
                Uint64 mix_start = SDL_GetTicksNS();
                RoomReverbs_AddOwn(send_own, count);
                RoomReverbs_Mix(buffer, count, reverb_amount * far_duck);
                spent_mix_ns += SDL_GetTicksNS() - mix_start;
                /* The listener's room's traced tail, where it has one. */
                RoomTails_Process(send, buffer, count, reverb_amount);
            }
        }
        FallingAir_Mix(buffer, count, OUTPUT_RATE, Game_LibraryFallSpeed());
        Ringing_Mix(buffer, count, OUTPUT_RATE, Game_LibraryRinging());
        HeadKnock_Mix(buffer, count, OUTPUT_RATE, Game_LibraryKnocks());
        SlowWhoosh_Mix(buffer, count, OUTPUT_RATE);
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
        if (!SDL_PutAudioStreamData(output, buffer, count * 2 * (int)sizeof(float))) break;
        frames -= count;
    }
    spent_audio_ns += SDL_GetTicksNS() - audio_start;
}

static void play_kind(int kind, float x, float y, float z, float level, float reach, int personal);
/* A shot that passes close by whizzes; one a tic, so a shotgun's pellets or a
   chaingun's burst don't pile up. */
static void shot_traced(const mobj_t *shooter, const mobj_t *hit, float x1, float y1, float z1,
                        float x2, float y2, float z2)
{
    static int last = -1;
    float x, y, z, level;
    if (!made_impacts || gametic == last) return;
    if (!Flyby_Near(players[consoleplayer].mo, shooter, hit, x1, y1, z1, x2, y2, z2, &x, &y, &z, &level)) return;
    last = gametic;
    play_kind(IMPACT_FLYBY, x, y, z, level, 1, 0);
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
        Rumble_Analyse(i, samples[i].data, samples[i].length, samples[i].rate);
    }
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO))
    { fprintf(stderr, "Audio unavailable: %s\n", SDL_GetError()); return; }
    SDL_AudioSpec spec = { SDL_AUDIO_F32, 2, OUTPUT_RATE };
    MadeBank_Build();
    room_reverb = Reverb_New(OUTPUT_RATE);
    beyond_reverb = Reverb_New(OUTPUT_RATE);
    RoomReverbs_Init(OUTPUT_RATE);
    RoomTails_Init(OUTPUT_RATE);
    I_SetSoundOutput(sound_output);
    Resample_Init();
    stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, audio, NULL);
    if (!stream) { fprintf(stderr, "Audio unavailable: %s\n", SDL_GetError()); return; }
    SDL_ResumeAudioStreamDevice(stream);
    Game_TraceShots(shot_traced);
    fprintf(stderr, "SDL sound output ready.\n");
}
void I_ShutdownSound(void)
{
    RoomGrid_Stop();
    Game_TraceShots(NULL);
    SDL_DestroyAudioStream(stream); stream = NULL;
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
}
static void play_at(const made_sample_t *sample, float x, float y, float z, float level, float reach);

typedef struct { int handle; float x, y, z; } made_at_t;
/* Channel updates gathered to go in under one lock (I_BeginSoundUpdate). */
static int batching;
static void hold_channels(void);
static void end_updates(void);

/* A new level's one-off sound work, done in the frame that loads it, before
   the renderer's loading screen draws (d_main.c runs S_UpdateSounds first).
   The rays hear each wall and flat as a material read from its artwork
   (room.c); read only as the first rays met them (the first room measured,
   the room records, the baked grid's start) it stalled the first played
   frame for a tenth of a second or more. */
static void prepare_level(void)
{
    static unsigned prepared = ~0u;
    if (!stream || gamestate != GS_LEVEL || prepared == r_levelserial) return;
    prepared = r_levelserial;
    Uint64 start = SDL_GetTicksNS();
    Room_Prepare();
    if (M_CheckParm("-levelsoundlog"))
        fprintf(stderr, "levelsound: materials read in %.1f ms at level load\n", (SDL_GetTicksNS() - start) / 1e6);
}

/* Every tic the ambience voices follow the listener; every other the room is
   measured for the reverb. */
static void update_sound(void)
{
    static int measured = -1, ambient = -1;
    if (gametic != ambient)
    {
        ambient = gametic;
        Vacuum_Follow();
        Mixer_LevelTic();
        ambience_voice_t voices[AMBIENT_KINDS][AMBIENCE_LAYERS];
        ambience_voice_t computer_body;
        ambience_strike_t strike;
        float level = ambience ? ambience_amount * AMBIENCE_LEVEL * SDL_clamp(snd_SfxVolume, 0, 15) / 15.0f : 0;
        Ambience_Update(voices, level, muffle_walls, muffle_distance, &strike, &computer_body);
        ambience_voice_t wind;
        float blowing;
        Ambience_WindField(&wind, &blowing);
        SDL_LockAudioStream(stream);
        AmbienceVoices_Set(voices);
        AmbienceVoices_SetComputerBody(&computer_body);
        WindVoice_Set(&wind, blowing);
        made_at_t made[MIX_CHANNELS];
        int anchored = 0;
        for (int c = 0; c < MIX_CHANNELS; ++c)
            if (channels[c].handle && channels[c].anchored)
                made[anchored++] = (made_at_t){ channels[c].handle, channels[c].x, channels[c].y, channels[c].z };
        int gathered = anchored && !batching;
        if (gathered) hold_channels();
        SDL_UnlockAudioStream(stream);
        /* A made-up sound stays where it was made while the listener moves
           and turns; all of them back under one lock. */
        for (int c = 0; c < anchored; ++c)
            I_SoundAt(made[c].handle, (fixed_t)(made[c].x * FRACUNIT), (fixed_t)(made[c].y * FRACUNIT), (fixed_t)(made[c].z * FRACUNIT));
        if (gathered) end_updates();
        if (strike.due) play_at(MadeBank_Strike(gametic), strike.x, strike.y, strike.z, strike.level, 1);
        step_event_t steps[8];
        int count = footsteps ? Steps_Tic(steps, 8) : 0;
        for (int i = 0; i < count; ++i)
            play_kind(steps[i].kind, steps[i].x, steps[i].y, steps[i].z, steps[i].level, 1, steps[i].personal);
        /* Now and then something behind the walls, with the ambience. */
        hidden_event_t hidden;
        if (ambience && Hidden_Tic(&hidden))
            play_through(MadeBank_Hidden(hidden.kind, (int)(hidden.take % MADE_VARIANTS)), hidden.x, hidden.y, hidden.z,
                         hidden.level * ambience_amount, hidden.reach, hidden.cutoff, 0);
    }
    if (!acoustics || (gametic >= measured && gametic - measured < 2)) return;
    measured = gametic;
    room_t room;
    room_outline_t outline;
    const mobj_t *listener = players[consoleplayer].mo;
    float facing = listener ? listener->angle * (2 * SDL_PI_F / 4294967296.0f) : 0;
    /* On authored maps from the baked grid, where it has the spot yet. */
    Uint64 room_start = SDL_GetTicksNS();
    int baked = LevelSound_On() && room_grid && listener && gamestate == GS_LEVEL
                && RoomGrid_Listener(listener->x / (float)FRACUNIT, listener->y / (float)FRACUNIT, facing, &room, &beyond);
    if (!baked)
    {
        Room_Measure(&room, &outline);
        Beyond_Measure(&room, &outline, facing, &beyond);
    }
    room_ns += SDL_GetTicksNS() - room_start;
    room_baked += baked; room_live += !baked;
    LibraryAcoustics_Shape(&room, &beyond);
    SDL_LockAudioStream(stream);
    /* On authored maps a room with a traced tail rings through it, so
       reverb.c's tail (and what it adds down a tube) goes quiet there. */
    if (LevelSound_On() && listener && gamestate == GS_LEVEL
        && RoomTails_Listen(RoomRecords_KeyAt(listener->x / (float)FRACUNIT, listener->y / (float)FRACUNIT), facing))
    { room.late = 0; room.tunnel = 0; room.flutter = 0; }
    else RoomTails_Listen(-1, facing);
    /* The space beyond rings as its room's traced tail, from where it is.
       Part of the listener's own room by the room map, it is in their
       traced tail already. */
    int here = listener && LevelSound_On() && gamestate == GS_LEVEL
               ? RoomRecords_KeyAt(listener->x / (float)FRACUNIT, listener->y / (float)FRACUNIT) : -1;
    int beyond_key = here >= 0 && beyond.coupling > 0 ? RoomRecords_KeyAt(beyond.outline.x, beyond.outline.y) : -1;
    int own_room = beyond_key >= 0 && beyond_key == here && RoomTails_Far(here);
    if (RoomTails_Beyond(own_room ? -1 : beyond_key, pan_at(beyond.outline.x, beyond.outline.y)) || own_room)
    { beyond.room.late = 0; beyond.room.tunnel = 0; beyond.room.flutter = 0; }
    if (room_reverb) Reverb_SetRoom(room_reverb, &room);
    if (beyond_reverb) Reverb_SetRoom(beyond_reverb, &beyond.room);
    coupling_target = beyond.coupling;
    SDL_UnlockAudioStream(stream);
    Uint64 rooms_start = SDL_GetTicksNS();
    Mixer_LevelRooms();
    spent_game_ns += SDL_GetTicksNS() - rooms_start;
}

void I_UpdateSound(void)
{
    if (!stream) return;
    uint64_t start = I_BeginAudioTiming();
    update_sound();
    I_EndAudioTiming(start);
}

void I_SetSoundOptions(int enabled, float amount, int walls, int distance, int falloff)
{
    if (stream) SDL_LockAudioStream(stream);
    if (enabled && !acoustics && room_reverb && beyond_reverb) { Reverb_Clear(room_reverb); Reverb_Clear(beyond_reverb); }
    acoustics = enabled != 0;
    reverb_amount = SDL_clamp(amount, 0.0f, 2.0f);
    muffle_walls = walls != 0;
    muffle_distance = distance != 0;
    natural_falloff = falloff != 0;
    if (stream) SDL_UnlockAudioStream(stream);
}
void I_SetSoundOutput(int output)
{
    if (stream) SDL_LockAudioStream(stream);
    sound_output = SDL_clamp(output, 0, HRTF_HEADS);
    if (room_reverb) Reverb_SetEars(room_reverb, sound_output != 0);
    if (beyond_reverb) Reverb_SetEars(beyond_reverb, sound_output != 0);
    RoomReverbs_SetEars(sound_output != 0);
    RoomTails_SetEars(sound_output != 0);
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
/* What the way to a sound at a point does to it, measured off the stream
   lock: it walks the map. */
typedef struct { float cutoff, share, air, air_hz, bend, way_x, way_y, heard; int key; } way_heard_t;
static void measure_way(fixed_t x, fixed_t y, fixed_t z, float reach, way_heard_t *w)
{
    float fx = x / (float)FRACUNIT, fy = y / (float)FRACUNIT;
    w->cutoff = Occlusion_Cutoff(x, y, z, muffle_walls, muffle_distance);
    w->share = Beyond_Share(&beyond, fx, fy);
    w->air = Vacuum_Carry(x, y, 0, &w->air_hz);
    w->bend = Occlusion_Opening(x, y, z, &w->way_x, &w->way_y);
    w->heard = by_path() ? heard_from(fx, fy, z / (float)FRACUNIT, reach) : 1;
    w->key = key_at(fx, fy);
}
/* The rest are with the stream locked. */
static void store_way(mix_channel_t *channel, const way_heard_t *w)
{
    channel->heard = w->heard; channel->key = w->key;
    channel->target = SDL_max(SDL_min(SDL_min(w->cutoff, w->air_hz), channel->most), channel->least);
    channel->beyond_target = w->share; channel->air_target = w->air;
    channel->bend_target = w->bend;
    if (w->bend > 0) { channel->way_x = w->way_x; channel->way_y = w->way_y; }
}
static void move_channel(mix_channel_t *channel, float fx, float fy, float fz)
{
    if (!channel->placed && !channel->anchored) return;
    channel->x = fx; channel->y = fy; channel->z = fz;
    float heard = by_path() ? channel->heard : distance_gain(listener_distance(fx, fy) / channel->reach);
    float volume = channel->anchored ? snd_SfxVolume * channel->level * heard
                 : falls_off() || by_path() ? snd_SfxVolume * heard : channel->volume;
    channel->bend += (channel->bend_target - channel->bend) * BEND_GLIDE;
    float hx, hy;
    heard_at(channel, &hx, &hy);
    gains(channel, volume, pan_at(hx, hy), channel->pitch);
    aim(channel, 0);
}
static void set_params(int handle, int volume, int separation)
{
    for (int i = 0; i < MIX_CHANNELS; ++i)
    {
        mix_channel_t *channel = &channels[i];
        if (channel->handle != handle) continue;
        float level = channel->placed && by_path() ? snd_SfxVolume * channel->heard
            : channel->placed && falls_off()
            ? snd_SfxVolume * distance_gain(listener_distance(channel->x, channel->y) / channel->reach) : (float)volume;
        /* Past DOOM's reach the game gives no side: the mixer pans it. */
        int side = separation;
        if (channel->placed && by_path()) { float hx, hy; heard_at(channel, &hx, &hy); side = pan_at(hx, hy); }
        /* A sound keeps the pitch it started with: DOOM updates moving sounds
           at the normal pitch, which would snap a random one back a tic in. */
        gains(channel, level, side, channel->pitch);
    }
}
static int measure_due(int measured) { return gametic < measured || gametic - measured >= 2; }

/* A frame's updates of the playing sounds (I_BeginSoundUpdate), so the
   audio thread waits on the lock twice a frame, not a few times per sound:
   the channels as they were at the start, and what the game asked of them,
   applied in order under one lock at the end. */
#define UPDATES_MOST (4 * MIX_CHANNELS)
typedef struct { int handle, measured; float reach; } held_t;
typedef struct { int handle, moved, measured, volume, separation; float x, y, z; way_heard_t way; } update_t;
static held_t held[MIX_CHANNELS];
static update_t updates[UPDATES_MOST];
static int update_count;
static void hold_channels(void)
{
    for (int c = 0; c < MIX_CHANNELS; ++c) held[c] = (held_t){ channels[c].handle, channels[c].measured, channels[c].reach };
    update_count = 0;
    batching = 1;
}
static held_t *held_find(int handle)
{
    for (int c = 0; c < MIX_CHANNELS; ++c)
        if (held[c].handle == handle) return &held[c];
    return NULL;
}
static void apply_updates(void)
{
    for (int i = 0; i < update_count; ++i)
    {
        const update_t *u = &updates[i];
        if (!u->moved) { set_params(u->handle, u->volume, u->separation); continue; }
        mix_channel_t *channel = find(u->handle);
        if (!channel) continue;
        move_channel(channel, u->x, u->y, u->z);
        if (u->measured) { channel->measured = gametic; store_way(channel, &u->way); }
    }
    update_count = 0;
}
static update_t *next_update(void)
{
    if (update_count == UPDATES_MOST) { SDL_LockAudioStream(stream); apply_updates(); SDL_UnlockAudioStream(stream); }
    return &updates[update_count++];
}
static void end_updates(void)
{
    if (!batching) return;
    batching = 0;
    SDL_LockAudioStream(stream);
    apply_updates();
    SDL_UnlockAudioStream(stream);
}

/* Measured when a sound starts, then every other tic as it or the listener moves. */
void I_SoundAt(int handle, fixed_t x, fixed_t y, fixed_t z)
{
    if (!stream) return;
    if (!handle)
    {
        next_cutoff = Occlusion_Cutoff(x, y, z, muffle_walls, muffle_distance);
        next_x = x / (float)FRACUNIT; next_y = y / (float)FRACUNIT; next_z = z / (float)FRACUNIT; next_placed = 1;
        next_bend = Occlusion_Opening(x, y, z, &next_way_x, &next_way_y);
        next_air = Vacuum_Carry(x, y, 0, &next_air_hz);
        next_beyond = Beyond_Share(&beyond, next_x, next_y);
        next_heard = by_path() ? heard_from(next_x, next_y, next_z, 1) : 1;
        next_key = key_at(next_x, next_y);
        return;
    }
    float fx = x / (float)FRACUNIT, fy = y / (float)FRACUNIT, fz = z / (float)FRACUNIT;
    if (batching)
    {
        held_t *h = held_find(handle);
        update_t *u = next_update();
        *u = (update_t){ handle, 1, 0, 0, 0, fx, fy, fz };
        if (h && measure_due(h->measured))
        {
            h->measured = gametic; u->measured = 1;
            measure_way(x, y, z, h->reach, &u->way);
        }
        return;
    }
    SDL_LockAudioStream(stream);
    mix_channel_t *channel = find(handle);
    float reach = channel ? channel->reach : 1;
    if (channel) move_channel(channel, fx, fy, fz);
    int due = channel && measure_due(channel->measured);
    if (due) channel->measured = gametic;
    SDL_UnlockAudioStream(stream);
    if (!due) return;
    way_heard_t way;
    measure_way(x, y, z, reach, &way);
    SDL_LockAudioStream(stream);
    if ((channel = find(handle))) store_way(channel, &way);
    SDL_UnlockAudioStream(stream);
}
/* The next I_StartSound is the listener's own: on Phobos heard through the suit. */
void I_SoundOwn(void) { next_own = 1; }
void I_SubmitSound(void) { }
void I_SetChannels(void) { }
/* Takes the free channel, or the quietest (the oldest of equals), so a
   faint far-off sound gives way to a near one. */
static int start_sample(sample_t sample, float volume, int separation, int pitch, float cutoff, float there, float send_scale, float air)
{
    SDL_LockAudioStream(stream);
    int slot = 0;
    for (int i = 0; i < MIX_CHANNELS; ++i)
    {
        if (!channels[i].handle) { slot = i; break; }
        float level = channels[i].left + channels[i].right, quietest = channels[slot].left + channels[slot].right;
        if (level < quietest || (level == quietest && channels[i].handle < channels[slot].handle)) slot = i;
    }
    mix_channel_t *channel = &channels[slot];
    channel->sample = sample;
    channel->position = 0;
    channel->cutoff = channel->target = cutoff;
    channel->beyond = channel->beyond_target = there;
    channel->air = channel->air_target = air;
    channel->low[0] = channel->low[1] = 0;
    Treble_Reset(&channel->treble);
    channel->measured = gametic;
    channel->placed = channel->anchored = channel->spatial = 0;
    channel->bend = channel->bend_target = 0;
    channel->reach = 1;
    channel->most = OCCLUSION_OPEN_HZ;
    channel->least = 0;
    channel->heard = 1; channel->source = SDL_clamp(volume, 0.0f, 15.0f) / 15.0f;
    channel->key = channel->slot = -1; channel->far = 0;
    channel->far_send = channel->now_far_send = 0;
    channel->send_scale = send_scale;
    if (next_handle == INT_MAX) next_handle = 1;
    channel->handle = next_handle++;
    gains(channel, volume, separation, pitch);
    channel->now_left = channel->left; channel->now_right = channel->right;
    channel->now_send = channel->send; channel->now_gain = channel->gain;
    int handle = channel->handle;
    SDL_UnlockAudioStream(stream);
    return handle;
}
/* A sound just started out in the world: heard from where it is, or the
   opening it comes through, and through the head on headphones. */
static void place(mix_channel_t *channel, float bend, float way_x, float way_y)
{
    float x, y;
    channel->bend = channel->bend_target = bend;
    channel->way_x = way_x; channel->way_y = way_y;
    heard_at(channel, &x, &y);
    gains(channel, channel->volume, pan_at(x, y), channel->pitch);
    channel->now_left = channel->left; channel->now_right = channel->right;
    channel->spatial = sound_output != 0;
    aim(channel, 1);
}
int I_StartSound(int id, int volume, int separation, int pitch, int priority)
{
    (void)priority;
    float cutoff = next_cutoff, x = next_x, y = next_y, z = next_z, level = (float)volume, there = next_beyond;
    float bend = next_bend, way_x = next_way_x, way_y = next_way_y, heard = next_heard;
    int key = next_key;
    next_bend = 0; next_heard = 1; next_key = -1;
    float air = next_air, air_hz = next_air_hz;
    int placed = next_placed, own = next_own;
    next_cutoff = OCCLUSION_OPEN_HZ; next_placed = 0; next_beyond = 0; next_air = 1; next_air_hz = OCCLUSION_OPEN_HZ; next_own = 0;
    const mobj_t *listener = players[consoleplayer].mo;
    if (own && listener) air = Vacuum_Carry(listener->x, listener->y, 1, &air_hz);
    if (own) own_tic = gametic;
    cutoff = SDL_min(cutoff, air_hz);
    if (id < 1 || id >= NUMSFX || !samples[id].data) return 0;
    float reach = reach_of(id);
    /* A pad feels it as it reaches the player (gamepad_rumble.c): their own
       whole, the rest by distance, less through walls. Sounds from nowhere
       (the menus) aren't felt. */
    if (placed || own)
    {
        float distance = placed ? listener_distance(x, y) : 0;
        int mine = own || distance < 1;
        float felt = mine ? 1 : distance_gain(distance / reach) * (cutoff < 1500 ? 0.6f : 1);
        Rumble_Sound(id, mine ? 1 : felt * air, mine);
    }
    if (!stream) return 0;
    /* As loud as it is made: DOOM has already taken distance off volume. */
    float source = SDL_clamp(placed && by_path() ? (float)snd_SfxVolume : level, 0.0f, 15.0f) / 15.0f;
    if (placed && by_path()) level = snd_SfxVolume * heard;
    else if (placed && falls_off()) level = snd_SfxVolume * distance_gain(listener_distance(x, y) / reach);
    if (placed) separation = pan_at(x, y);
    int handle = start_sample(samples[id], level, separation, pitch, cutoff, placed ? there : 0, 1, air);
    SDL_LockAudioStream(stream);
    mix_channel_t *channel = find(handle);
    if (channel)
    {
        channel->placed = placed; channel->x = x; channel->y = y; channel->z = z; channel->reach = reach;
        channel->heard = heard; channel->key = placed ? key : -1; channel->source = source;
        if (placed) place(channel, bend, way_x, way_y);
    }
    SDL_UnlockAudioStream(stream);
    return handle;
}

/* A DMX sound lump by name, whole and dry, from no place (the library's
   ending, f_library.c): nothing if the WADs lack it. */
int I_SoundLump(const char *name)
{
    int lump = W_CheckNumForName((char *)name);
    if (!stream || lump < 0) return 0;
    int length = W_LumpLength(lump);
    byte *data = W_CacheLumpNum(lump, PU_STATIC);
    if (length < 8 || little16(data) != 3) return 0;
    uint32_t size = little32(data + 4);
    if (!size || size > (uint32_t)(length - 8)) return 0;
    sample_t sample = { data + 8, (int)size, (int)little16(data + 2) };
    return start_sample(sample, 127, 128, 128, OCCLUSION_OPEN_HZ, 0, 0, 1);
}

/* A made-up sound at a map point, loud and panned as DOOM does it for a
   thing there (S_AdjustSoundParams, or the natural falloff), muffled like
   any effect. level scales the effects volume; reach how much farther than
   DOOM's sounds it carries, as a louder sound would. Made-up sounds stay
   where they were made: those that follow a thing are short. most caps the
   treble, for a sound heard through a wall whatever the way to it. Personal
   movement keeps source loudness and the effects volume linear in its
   room send; only distance makes it relatively wetter. */
static float through_least;
void play_through(const made_sample_t *sample, float x, float y, float z, float level, float reach, float most, int personal)
{
    const mobj_t *listener = players[consoleplayer].mo;
    last_through = 0;
    if (!stream || !sample || !listener || gamestate != GS_LEVEL || level <= 0) return;
    float heard;
    if (by_path() && !personal) heard = heard_from(x, y, z, reach);
    else
    {
        float distance = listener_distance(x, y);
        if (distance > 1200 * reach) return;
        heard = distance_gain(distance / reach);
    }
    if (heard <= 0) return;
    float volume = snd_SfxVolume * level * heard;
    /* A far-off sound never takes a channel from a nearer one, and only so
       many play at once: the quietest gives way. */
    if (through_far)
    {
        float wanted = SFX_GAIN * SDL_clamp(volume, 0.0f, 15.0f) / 15.0f * 1.4f;
        int far_count = 0, quietest_far = -1, free_slot = 0;
        float quietest = 1e9f;
        SDL_LockAudioStream(stream);
        for (int i = 0; i < MIX_CHANNELS; ++i)
        {
            if (!channels[i].handle) { free_slot = 1; continue; }
            float loud = channels[i].left + channels[i].right;
            quietest = SDL_min(quietest, loud);
            if (!channels[i].far) continue;
            ++far_count;
            if (quietest_far < 0 || loud < channels[quietest_far].left + channels[quietest_far].right) quietest_far = i;
        }
        int go = 1;
        if (far_count >= FAR_VOICES)
        {
            if (channels[quietest_far].left + channels[quietest_far].right >= wanted) go = 0;
            else { channels[quietest_far].handle = 0; free_slot = 1; }
        }
        if (go && !free_slot && quietest >= wanted) go = 0;
        SDL_UnlockAudioStream(stream);
        if (!go) return;
    }
    float source = SDL_clamp(snd_SfxVolume, 0, 15) / 15.0f * level;
    float send_scale = personal ? sqrtf(SDL_clamp(source, 0.0f, 1.0f)) : 1;
    float air_hz, air = Vacuum_Carry((fixed_t)(x * FRACUNIT), (fixed_t)(y * FRACUNIT), personal, &air_hz);
    if (air <= 0) return;
    float cutoff = SDL_min(SDL_min(most, air_hz), Occlusion_Cutoff((fixed_t)(x * FRACUNIT), (fixed_t)(y * FRACUNIT), (fixed_t)(z * FRACUNIT),
                                                  muffle_walls, muffle_distance));
    float way_x = 0, way_y = 0, bend = personal ? 0 : Occlusion_Opening((fixed_t)(x * FRACUNIT), (fixed_t)(y * FRACUNIT), (fixed_t)(z * FRACUNIT), &way_x, &way_y);
    int handle = start_sample((sample_t){ sample->data, sample->length, sample->rate }, volume, pan_at(x, y), 128, cutoff,
                              Beyond_Share(&beyond, x, y), send_scale, air);
    last_through = handle;
    /* The listener's own movement stays with them. */
    if (personal) return;
    SDL_LockAudioStream(stream);
    mix_channel_t *channel = find(handle);
    if (channel)
    {
        channel->anchored = 1; channel->x = x; channel->y = y; channel->z = z;
        channel->reach = reach; channel->level = level; channel->most = most; channel->least = through_least;
        channel->heard = heard; channel->far = through_far; channel->key = key_at(x, y);
        channel->source = SDL_clamp(snd_SfxVolume * level, 0.0f, 15.0f) / 15.0f;
        place(channel, bend, way_x, way_y);
    }
    SDL_UnlockAudioStream(stream);
}

static void play_at(const made_sample_t *sample, float x, float y, float z, float level, float reach)
{
    play_through(sample, x, y, z, level, reach, OCCLUSION_OPEN_HZ, 0);
}

/* A variation of the kind, never one of the last two of it. */
/* A shout carries farther than DOOM's own sounds, as a raised voice does. */
#define BARK_REACH 1.5f
/* And over the mix: what they say is what he goes by. */
#define BARK_BOOST 1.5f

/* Enough of a voice to tell the words, through a door. */
#define BARK_CLEAR_HZ 1800.0f
static int last_bark, bark_clear;
int I_BarkVoice(void) { return last_bark; }
void I_BarkClear(void) { bark_clear = 1; }

int I_BarkLump(const char *name, fixed_t x, fixed_t y, fixed_t z, int loud, int pitch)
{
    int lump = W_CheckNumForName((char *)name), clear = bark_clear;
    last_bark = 0;
    bark_clear = 0;
    if (!stream || lump < 0) return 0;
    int length = W_LumpLength(lump);
    byte *data = W_CacheLumpNum(lump, PU_STATIC);
    if (length < 8 || little16(data) != 3) return 0;
    uint32_t size = little32(data + 4);
    if (!size || size > (uint32_t)(length - 8)) return 0;
    made_sample_t sample = { data + 8, (int)size, (int)little16(data + 2)*pitch/100 };
    through_least = clear ? BARK_CLEAR_HZ : 0;
    play_through(&sample, x / (float)FRACUNIT, y / (float)FRACUNIT, z / (float)FRACUNIT, loud / 100.0f * BARK_BOOST, BARK_REACH,
                 OCCLUSION_OPEN_HZ, 0);
    through_least = 0;
    last_bark = last_through;
    return (int)((uint64_t)size * TICRATE / (uint32_t)sample.rate);
}

static void play_kind(int kind, float x, float y, float z, float level, float reach, int personal)
{
    static int last[IMPACT_KINDS][2], started;
    static unsigned turn = 0x1234567u;
    if (kind < 0 || kind >= IMPACT_KINDS) return;
    if (!started) { started = 1; for (int k = 0; k < IMPACT_KINDS; ++k) last[k][0] = last[k][1] = -1; }
    int count = MadeBank_Variants(kind), variant;
    do { turn = turn * 1103515245u + 12345u; variant = (int)((turn >> 16) % (unsigned)count); }
    while (variant == last[kind][0] || variant == last[kind][1]);
    last[kind][1] = last[kind][0];
    last[kind][0] = variant;
    play_through(MadeBank_Impact(kind, variant), x, y, z, level, reach, OCCLUSION_OPEN_HZ, personal);
}
void I_MadeSoundLevel(int kind, float x, float y, float z, float scale)
{
    if (!made_impacts || kind < 0 || kind >= IMPACT_KINDS) return;
    if (glass_fracture && kind == IMPACT_GLASS_BREAK) kind = IMPACT_GLASS_FRACTURE;
    if (glass_fracture && kind == IMPACT_GLASS_BIG) kind = IMPACT_GLASS_FRACTURE_BIG;
    float level = kind <= IMPACT_SPARK_ZAP ? 0.6f : kind <= IMPACT_SCREEN_DIES ? 0.7f
                : kind == IMPACT_GLASS_TINKLE ? 0.6f : kind == IMPACT_GORE_BURST ? 0.5f
                : kind == IMPACT_GORE_SLAP ? 0.7f : kind == IMPACT_GORE_TEAR || kind == IMPACT_GORE_CRACK ? 0.6f
                : kind == IMPACT_GORE_BLOWUP ? 0.8f : kind == IMPACT_GORE_THUD ? 0.5f
                : kind == IMPACT_GORE_STRETCH ? 0.35f : kind == IMPACT_GORE_SNAP ? 0.4f
                : kind == IMPACT_BODY_METAL ? 0.4f : kind == IMPACT_BODY_STONE || kind == IMPACT_BODY_WOOD ? 0.55f
                : kind == IMPACT_HIT_METAL ? 0.35f : kind == IMPACT_BALLOON ? 0.9f : kind >= IMPACT_PLAYER_ARMOR ? 0.8f : kind >= IMPACT_HIT_STONE ? 0.6f : 1.0f;
    /* A window breaking is loud: heard across a courtyard, where an
       enemy's stray shot can take it out. */
    int breaking = kind >= IMPACT_GLASS_SHATTER && kind <= IMPACT_GLASS_FRACTURE_BIG;
    play_kind(kind, x, y, z, level * scale, breaking ? 2.0f : 1.0f, 0);
}
void I_MadeSound(int kind, float x, float y, float z) { I_MadeSoundLevel(kind, x, y, z, 1); }
void I_AmbienceSilence(float x, float y, float z, float radius) { Ambience_Silence(x, y, z, radius); }
void I_AmbienceGlaze(int line, int whole) { Ambience_Glaze(line, whole); RoomRecords_Glass(line); RoomGrid_Glass(line); RoomTails_Glass(line, whole); }
/* The tails' tracers too: they read the map, and the next level's
   materials are read at its load (prepare_level). */
void I_LevelEnding(void) { RoomGrid_Stop(); RoomTails_Stop(); }
void I_AmbienceShowGlass(int shown) { Ambience_ShowGlass(shown); }
void I_AmbienceRain(float amount) { Ambience_Rain(amount); }
void I_AmbienceSnow(float amount) { Ambience_Snow(amount); Room_Snow(amount); }
void I_AmbienceDraft(float amount) { Ambience_Draft(amount); }
void I_AmbienceWind(float amount) { Ambience_Wind(amount); }
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
void I_SetAlternativeComputerSounds(int enabled)
{
    if (stream) SDL_LockAudioStream(stream);
    AmbienceVoices_AlternativeComputer(enabled);
    if (stream) SDL_UnlockAudioStream(stream);
}
void I_StopSound(int handle)
{
    if (!stream) return;
    SDL_LockAudioStream(stream);
    for (int i = 0; i < MIX_CHANNELS; ++i)
        if (channels[i].handle == handle) channels[i].handle = 0;
    SDL_UnlockAudioStream(stream);
    held_t *h = batching && handle ? held_find(handle) : NULL;
    if (h) h->handle = 0;
}
int I_SoundVoices(void)
{
    if (!stream) return 0;
    int voices = 0;
    SDL_LockAudioStream(stream);
    for (int i = 0; i < MIX_CHANNELS; ++i)
        if (channels[i].handle) ++voices;
    SDL_UnlockAudioStream(stream);
    return voices;
}
int I_SoundIsPlaying(int handle)
{
    if (!stream || !handle) return 0;
    if (batching) return held_find(handle) != NULL;
    int playing = 0;
    SDL_LockAudioStream(stream);
    for (int i = 0; i < MIX_CHANNELS; ++i)
        if (channels[i].handle == handle) playing = 1;
    SDL_UnlockAudioStream(stream);
    return playing;
}
void I_UpdateSoundParams(int handle, int volume, int separation, int pitch)
{
    (void)pitch;
    if (!stream) return;
    if (batching)
    {
        update_t *u = next_update();
        *u = (update_t){ handle, 0, 0, volume, separation };
        return;
    }
    SDL_LockAudioStream(stream);
    set_params(handle, volume, separation);
    SDL_UnlockAudioStream(stream);
}

/* S_UpdateSounds brackets its pass over the playing sounds with these. A
   new level's sound work is done at the first, in the frame that loaded it. */
void I_BeginSoundUpdate(void)
{
    prepare_level();
    if (!stream || batching) return;
    SDL_LockAudioStream(stream);
    hold_channels();
    SDL_UnlockAudioStream(stream);
}
void I_EndSoundUpdate(void) { end_updates(); }

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

int I_Mixer_RegisterRecorded(void *bytes, int size, float gain)
{
    if (!stream) { free(bytes); return 0; }
    lock_music();
    int registered = MixerMusic_RegisterRecorded(bytes, size, OUTPUT_RATE, gain);
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
