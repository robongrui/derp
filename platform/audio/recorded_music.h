/* Recorded music: an MP3 or Ogg Vorbis song streamed into the mixer in place
   of a score, from a soundtrack pack (see platform/music_pack.c). */
#ifndef DOOM_AUDIO_RECORDED_MUSIC_H
#define DOOM_AUDIO_RECORDED_MUSIC_H
#include <stddef.h>

/* Whether a lump's bytes are a recording (MP3 or Ogg) rather than a score. */
int Recorded_Is (const void* data, size_t size);
/* Takes over the bytes (from malloc) and readies them to play at the
   mixer's rate, gain times as loud; 0 if they can't be read, and they are
   freed. Under the audio lock. */
int Recorded_Load (void* bytes, size_t size, int rate, float gain);
void Recorded_Unload (void);
void Recorded_Start (int loop);
void Recorded_Stop (void);
int Recorded_Playing (void);
/* Adds the next frames to an interleaved stereo buffer. */
void Recorded_Mix (float* stereo, int frames, float gain);
#endif
