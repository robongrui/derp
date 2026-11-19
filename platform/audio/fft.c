/* A plain radix-2 FFT. See fft.h. */
#include <math.h>
#include "fft.h"

int Fft_Size(int n)
{
    int size = 1;
    while (size < n) size <<= 1;
    return size;
}

void Fft_Complex(float *re, float *im, int n, int back)
{
    for (int i = 1, j = 0; i < n; ++i)
    {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j)
        {
            float t = re[i]; re[i] = re[j]; re[j] = t;
            t = im[i]; im[i] = im[j]; im[j] = t;
        }
    }
    for (int length = 2; length <= n; length <<= 1)
    {
        double angle = (back ? 2 : -2) * 3.14159265358979323846 / length;
        double wr = cos(angle), wi = sin(angle);
        for (int i = 0; i < n; i += length)
        {
            double cr = 1, ci = 0;
            for (int k = 0; k < length / 2; ++k)
            {
                int a = i + k, b = a + length / 2;
                float tr = (float)(re[b] * cr - im[b] * ci), ti = (float)(re[b] * ci + im[b] * cr);
                re[b] = re[a] - tr; im[b] = im[a] - ti;
                re[a] += tr; im[a] += ti;
                double next = cr * wr - ci * wi;
                ci = cr * wi + ci * wr; cr = next;
            }
        }
    }
    if (!back) return;
    float scale = 1.0f / n;
    for (int i = 0; i < n; ++i) { re[i] *= scale; im[i] *= scale; }
}
