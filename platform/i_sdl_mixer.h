/* The SDL mixer's state, shared by its two files and no others:
 * i_sdl_sound.c (the channels, the mix, DOOM's sound calls) and
 * i_sdl_level_sound.c (sound through the whole level on authored maps). */
#ifndef DOOM_I_SDL_MIXER_H
#define DOOM_I_SDL_MIXER_H
#include "doomdef.h"
#include "audio/beyond.h"
#include "audio/hrtf.h"
#include "audio/made_bank.h"
#include "audio/treble.h"
#include <SDL3/SDL.h>

#define MIX_CHANNELS 32

typedef struct { const byte *data; int length, rate; } sample_t;
typedef struct {
    sample_t sample;
    double position, step;
    /* Where the gains are going, and where they are on the way there. */
    float left, right, send, gain;
    float now_left, now_right, now_send, now_gain;
    float send_scale;  /* 1 for world sounds; personal source gain correction */
    /* Lowpass: cutoff glides to target; two one-pole stages of state. */
    float cutoff, target, low[2];
    /* How much of it is in the space beyond (beyond.h) rather than the
       listener's room, gliding to its target. */
    float beyond, beyond_target;
    /* How much of it the air carries (vacuum.h), gliding to its target. */
    float air, air_target;
    treble_t treble;
    int handle, measured;
    /* Where a placed sound is, for the natural falloff, how far it carries
       (I_SoundReach), and how it was last panned, pitched and how loud DOOM
       asked for it. A made-up sound stays where it was made (anchored), at
       level times the effects volume, no brighter than most. */
    float x, y, z, reach, volume, level, most, least;
    int placed, anchored, separation, pitch;
    /* Out of sight it is heard from the opening it comes through: how far
       its direction has turned that way (bend), and how far it should. */
    float way_x, way_y, bend, bend_target;
    /* On headphones, what the head does to it. */
    int spatial;
    hrtf_voice_t hrtf;
    /* Authored maps: how much of it the way through the level lets through
       (0-1), how loud it is where it is made (0-1), the room it is made in
       and the reverb of that room it rings in (-1 none, room_reverbs.h),
       and whether it came from too far for one of DOOM's channels. */
    float heard, source, far_send, now_far_send;
    int key, slot, far;
} mix_channel_t;

extern sample_t samples[NUMSFX];
extern mix_channel_t channels[MIX_CHANNELS];
/* NULL without sound; the game thread locks it to change a channel. */
extern SDL_AudioStream *stream;
/* The bigger space beyond the listener's room (beyond.h). */
extern beyond_t beyond;
/* The tic the listener last made a sound of their own (a shot). */
extern int own_tic;
/* The next play_through is a far sound; the handle of the last it started. */
extern int through_far, last_through;
/* -levelsoundlog: time spent on level sound (game thread, mixer, the mix in
   all) and how the listener's room was had (from the grid, or measured). */
extern Uint64 spent_game_ns, spent_mix_ns, spent_audio_ns, room_ns;
extern int room_live, room_baked;
/* Authored maps, F4: the listener's room from the baked grid (room_grid.h),
   or 0: measured live from rays every other tic, as everywhere else. */
extern int room_grid;

/* Authored maps: sounds fade by the way through the level. */
int by_path(void);
/* How much farther than DOOM's own a sound carries. */
float reach_of(int id);
/* DOOM's separation for a map point, as the listener faces. */
int pan_at(float x, float y);
/* Sets a channel's levels from DOOM's volume (0-15), separation and pitch. */
void gains(mix_channel_t *channel, float volume, int separation, int pitch);
/* A made-up or far sound at a map point (i_sdl_sound.c). */
void play_through(const made_sample_t *sample, float x, float y, float z, float level, float reach, float most, int personal);

/* i_sdl_level_sound.c: once a tic, and after the listener's room is measured. */
void Mixer_LevelTic(void);
void Mixer_LevelRooms(void);
#endif
