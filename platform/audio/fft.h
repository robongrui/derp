/* A plain radix-2 FFT for the room tails' convolution (PLAN-convolution.md). */
#ifndef DOOM_AUDIO_FFT_H
#define DOOM_AUDIO_FFT_H

/* In place over n complex values (n a power of two), forward or back; back
   divides by n, so forward then back gives the input again. */
void Fft_Complex(float *re, float *im, int n, int back);
/* The power of two at least n. */
int Fft_Size(int n);
#endif
