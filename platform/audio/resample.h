/* Reading an 8-bit effect between its samples, for the F4 Sound choice. */
#ifndef DOOM_AUDIO_RESAMPLE_H
#define DOOM_AUDIO_RESAMPLE_H

/* Original holds each sample, as the mixer always did: DOOM's sizzle.
   Smooth is a 4-point cubic that keeps some edge. Clean is a windowed sinc
   that drops the sizzle and keeps only what the sample holds. */
enum { RESAMPLE_ORIGINAL, RESAMPLE_SMOOTH, RESAMPLE_CLEAN };

/* Builds the Clean filter table; once, before mixing. */
void Resample_Init(void);
/* The unsigned 8-bit sample at a fractional position, as -1..1. */
float Resample_At(const unsigned char *data, int length, double position, int mode);
#endif
