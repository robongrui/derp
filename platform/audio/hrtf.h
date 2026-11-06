/* A sound's direction on headphones: what a measured head does to it on the
 * way to each ear. A voice is one sound's delay and pair of short filters,
 * read from a table (scripts/make_hrtf.py) that keeps a sound dead ahead as
 * it is, so only what changes around the head is added to the mix. */
#ifndef DOOM_AUDIO_HRTF_H
#define DOOM_AUDIO_HRTF_H

#define HRTF_RATE 44100
#define HRTF_TAPS 64
/* Elevations from -60 to 60 degrees, azimuths all the way round. */
#define HRTF_RINGS 9
#define HRTF_RING_DEGREES 15
#define HRTF_STEPS 72
#define HRTF_STEP_DEGREES 5
/* The most samples one Hrtf_Process takes, and one ear can lag the other. */
#define HRTF_BLOCK 512
#define HRTF_LINE 48

enum { HRTF_KEMAR, HRTF_KU100, HRTF_HEADS };

typedef struct {
    int head, fade, fading;
    float azimuth, elevation, presence;     /* what the filters were made for */
    float filter[2][HRTF_TAPS], next[2][HRTF_TAPS];
    float delay[2], delay_to[2];
    float line[HRTF_LINE];                  /* the sound just before this block */
    float tail[2][HRTF_TAPS - 1];           /* each ear's, delayed */
} hrtf_voice_t;

/* Azimuth in radians, left positive; elevation in radians, up positive.
   presence: 1 is a sound out there, 0 one at the listener, with no direction
   at all. Start is a new sound, there at once; Aim moves one, crossfading. */
void Hrtf_Start(hrtf_voice_t *voice, int head, float azimuth, float elevation, float presence);
void Hrtf_Aim(hrtf_voice_t *voice, float azimuth, float elevation, float presence);
/* Adds count (at most HRTF_BLOCK) samples of mono to interleaved stereo. */
void Hrtf_Process(hrtf_voice_t *voice, const float *mono, float *stereo, int count);
#endif
