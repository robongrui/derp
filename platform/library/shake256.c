/* SHAKE256 from FIPS 202: Keccak-f[1600] over a 136-byte rate. */
#include "library.h"

#include <string.h>

#define RATE 136

static const uint64_t roundConstants[24] = {
    0x0000000000000001ull, 0x0000000000008082ull, 0x800000000000808aull, 0x8000000080008000ull,
    0x000000000000808bull, 0x0000000080000001ull, 0x8000000080008081ull, 0x8000000000008009ull,
    0x000000000000008aull, 0x0000000000000088ull, 0x0000000080008009ull, 0x000000008000000aull,
    0x000000008000808bull, 0x800000000000008bull, 0x8000000000008089ull, 0x8000000000008003ull,
    0x8000000000008002ull, 0x8000000000000080ull, 0x000000000000800aull, 0x800000008000000aull,
    0x8000000080008081ull, 0x8000000000008080ull, 0x0000000080000001ull, 0x8000000080008008ull,
};

/* Rho and pi together: lane by lane along pi's cycle, each rotated by rho. */
static const unsigned rotations[24] = {
    1, 3, 6, 10, 15, 21, 28, 36, 45, 55, 2, 14, 27, 41, 56, 8, 25, 43, 62, 18, 39, 61, 20, 44,
};
static const unsigned lanes[24] = {
    10, 7, 11, 17, 18, 3, 5, 16, 8, 21, 24, 4, 15, 23, 19, 13, 12, 2, 20, 14, 22, 9, 6, 1,
};

static uint64_t Rotate(uint64_t x, unsigned n)
{
    return (x << n) | (x >> (64 - n));
}

static void Permute(uint64_t *a)
{
    uint64_t c[5], t;
    for (int round = 0; round < 24; round++) {
        for (int i = 0; i < 5; i++)
            c[i] = a[i] ^ a[i + 5] ^ a[i + 10] ^ a[i + 15] ^ a[i + 20];
        for (int i = 0; i < 5; i++) {
            t = c[(i + 4) % 5] ^ Rotate(c[(i + 1) % 5], 1);
            for (int j = 0; j < 25; j += 5)
                a[j + i] ^= t;
        }
        t = a[1];
        for (int i = 0; i < 24; i++) {
            uint64_t next = a[lanes[i]];
            a[lanes[i]] = Rotate(t, rotations[i]);
            t = next;
        }
        for (int j = 0; j < 25; j += 5) {
            for (int i = 0; i < 5; i++)
                c[i] = a[j + i];
            for (int i = 0; i < 5; i++)
                a[j + i] ^= ~c[(i + 1) % 5] & c[(i + 2) % 5];
        }
        a[0] ^= roundConstants[round];
    }
}

/* Lanes hold their bytes little-endian whatever the machine. */
static void XorByte(shake256 *shake, unsigned at, uint8_t byte)
{
    shake->state[at / 8] ^= (uint64_t)byte << (8 * (at % 8));
}

void Shake256_Begin(shake256 *shake)
{
    memset(shake, 0, sizeof *shake);
}

void Shake256_Absorb(shake256 *shake, const void *data, size_t length)
{
    const uint8_t *bytes = data;
    for (size_t i = 0; i < length; i++) {
        XorByte(shake, shake->position++, bytes[i]);
        if (shake->position == RATE) {
            Permute(shake->state);
            shake->position = 0;
        }
    }
}

void Shake256_Squeeze(shake256 *shake, void *out, size_t length)
{
    uint8_t *bytes = out;
    if (!shake->squeezing) {
        /* SHAKE's domain bits 1111, then pad10*1 to the end of the block. */
        XorByte(shake, shake->position, 0x1f);
        XorByte(shake, RATE - 1, 0x80);
        Permute(shake->state);
        shake->position = 0;
        shake->squeezing = 1;
    }
    for (size_t i = 0; i < length; i++) {
        if (shake->position == RATE) {
            Permute(shake->state);
            shake->position = 0;
        }
        bytes[i] = (uint8_t)(shake->state[shake->position / 8] >> (8 * (shake->position % 8)));
        shake->position++;
    }
}
