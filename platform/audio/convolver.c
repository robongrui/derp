/* Uniformly partitioned convolution. See convolver.h. */
#include <stdlib.h>
#include <string.h>
#include "convolver.h"
#include "fft.h"

#define SIZE (2 * CONV_BLOCK)  /* each FFT: the last block and this one */
#define BINS (CONV_BLOCK + 1)  /* what a real signal's spectrum needs */

struct conv_input_s { int parts, head; float last[CONV_BLOCK]; float *re, *im; };
struct conv_slot_s { int most, parts, channels; float *re, *im; };

conv_input_t *Conv_NewInput(int parts)
{
    conv_input_t *in = calloc(1, sizeof(*in));
    if (!in) return NULL;
    in->parts = parts > 0 ? parts : 1;
    in->re = calloc((size_t)in->parts * BINS, sizeof(float));
    in->im = calloc((size_t)in->parts * BINS, sizeof(float));
    if (!in->re || !in->im) { Conv_FreeInput(in); return NULL; }
    return in;
}
void Conv_FreeInput(conv_input_t *in)
{
    if (!in) return;
    free(in->re); free(in->im); free(in);
}
void Conv_ClearInput(conv_input_t *in)
{
    memset(in->re, 0, sizeof(float) * in->parts * BINS);
    memset(in->im, 0, sizeof(float) * in->parts * BINS);
    memset(in->last, 0, sizeof(in->last));
    in->head = 0;
}

void Conv_Push(conv_input_t *in, const float *block)
{
    float re[SIZE], im[SIZE];
    memcpy(re, in->last, sizeof(in->last));
    memcpy(re + CONV_BLOCK, block, sizeof(float) * CONV_BLOCK);
    memset(im, 0, sizeof(im));
    Fft_Complex(re, im, SIZE, 0);
    memcpy(in->re + in->head * BINS, re, sizeof(float) * BINS);
    memcpy(in->im + in->head * BINS, im, sizeof(float) * BINS);
    in->head = (in->head + 1) % in->parts;
    memcpy(in->last, block, sizeof(in->last));
}

conv_slot_t *Conv_NewSlot(int parts, int channels)
{
    conv_slot_t *s = calloc(1, sizeof(*s));
    if (!s) return NULL;
    s->most = parts > 0 ? parts : 1;
    s->channels = channels < 1 ? 1 : channels > CONV_CHANNELS ? CONV_CHANNELS : channels;
    s->re = calloc((size_t)s->channels * s->most * BINS, sizeof(float));
    s->im = calloc((size_t)s->channels * s->most * BINS, sizeof(float));
    if (!s->re || !s->im) { Conv_FreeSlot(s); return NULL; }
    return s;
}
void Conv_FreeSlot(conv_slot_t *s)
{
    if (!s) return;
    free(s->re); free(s->im); free(s);
}

void Conv_Load(conv_slot_t *s, const short *const channel[CONV_CHANNELS], int length, float scale)
{
    int parts = length > CONV_BLOCK ? (length - 1) / CONV_BLOCK : 0;
    s->parts = parts > s->most ? s->most : parts;
    for (int c = 0; c < s->channels; ++c)
        for (int p = 0; p < s->parts; ++p)
        {
            float re[SIZE] = {0}, im[SIZE] = {0};
            for (int i = 0; i < CONV_BLOCK; ++i)
            {
                int n = CONV_BLOCK + p * CONV_BLOCK + i;
                re[i] = n < length ? channel[c][n] * scale : 0;
            }
            Fft_Complex(re, im, SIZE, 0);
            size_t row = ((size_t)c * s->most + p) * BINS;
            memcpy(s->re + row, re, sizeof(float) * BINS);
            memcpy(s->im + row, im, sizeof(float) * BINS);
        }
}

void Conv_Run(const conv_input_t *in, const conv_slot_t *s, float out[CONV_CHANNELS][CONV_BLOCK])
{
    float ar[CONV_CHANNELS][BINS] = {{0}}, ai[CONV_CHANNELS][BINS] = {{0}};
    int parts = s->parts < in->parts ? s->parts : in->parts;
    for (int p = 0; p < parts; ++p)
    {
        int row = ((in->head - 1 - p) % in->parts + in->parts) % in->parts;
        const float *xr = in->re + row * BINS, *xi = in->im + row * BINS;
        for (int c = 0; c < s->channels; ++c)
        {
            size_t at = ((size_t)c * s->most + p) * BINS;
            const float *hr = s->re + at, *hi = s->im + at;
            float *yr = ar[c], *yi = ai[c];
            for (int k = 0; k < BINS; ++k)
            {
                yr[k] += xr[k] * hr[k] - xi[k] * hi[k];
                yi[k] += xr[k] * hi[k] + xi[k] * hr[k];
            }
        }
    }
    float re[SIZE], im[SIZE];
    if (s->channels < 3)
    {
        /* One channel back alone. */
        for (int k = 0; k < BINS; ++k)
        {
            re[k] = ar[0][k]; im[k] = ai[0][k];
            if (k > 0 && k < CONV_BLOCK) { re[SIZE - k] = ar[0][k]; im[SIZE - k] = -ai[0][k]; }
        }
        Fft_Complex(re, im, SIZE, 1);
        memcpy(out[0], re + CONV_BLOCK, sizeof(float) * CONV_BLOCK);
        return;
    }
    /* w and x back at once, as the real and imaginary parts of one transform. */
    for (int k = 0; k < BINS; ++k)
    {
        re[k] = ar[0][k] - ai[1][k]; im[k] = ai[0][k] + ar[1][k];
        if (k > 0 && k < CONV_BLOCK)
        {
            re[SIZE - k] = ar[0][k] + ai[1][k]; im[SIZE - k] = ar[1][k] - ai[0][k];
        }
    }
    Fft_Complex(re, im, SIZE, 1);
    memcpy(out[0], re + CONV_BLOCK, sizeof(float) * CONV_BLOCK);
    memcpy(out[1], im + CONV_BLOCK, sizeof(float) * CONV_BLOCK);
    for (int k = 0; k < BINS; ++k)
    {
        re[k] = ar[2][k]; im[k] = ai[2][k];
        if (k > 0 && k < CONV_BLOCK) { re[SIZE - k] = ar[2][k]; im[SIZE - k] = -ai[2][k]; }
    }
    Fft_Complex(re, im, SIZE, 1);
    memcpy(out[2], re + CONV_BLOCK, sizeof(float) * CONV_BLOCK);
}
