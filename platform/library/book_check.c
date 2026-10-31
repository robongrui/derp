/* Whose book it is: Montgomery arithmetic modulo RSA-2048 (library.h). */
#include "library.h"

#include <string.h>

const char Library_RSA2048[] =
    "25195908475657893494027183240048398571429282126204032027777137836043662020707595556264018525880784"
    "40691829064124951508218929855914917618450280848912007284499268739280728777673597141834727026189637"
    "50149718246911650776133798590957000973304597488084284017974291006424586918171951187461215151726546"
    "32282216869987549182422433637259085141865462043576798423387184774447920739934236584823824281198163"
    "81501067481045166037730605620161967625613384414360383390441495263443219011465754445417842402092461"
    "65157233507787077498171257724679629263863563732899121548314381678998850404453640235273819513786365"
    "64391212010397122822120720357";

int Library_Compare(const library_number *a, const library_number *b)
{
    for (int i = LIBRARY_LIMBS; i-- > 0;) {
        if (a->limb[i] != b->limb[i])
            return a->limb[i] < b->limb[i] ? -1 : 1;
    }
    return 0;
}

static int Bits(const library_number *x)
{
    for (int i = LIBRARY_LIMBS; i-- > 0;) {
        if (x->limb[i]) {
            int bits = 32 * i;
            for (uint32_t top = x->limb[i]; top; top >>= 1)
                bits++;
            return bits;
        }
    }
    return 0;
}

/* a -= b over the modulus' limbs; a borrow out of the top is dropped. */
static void Subtract(uint32_t *a, const uint32_t *b, int limbs)
{
    uint64_t borrow = 0;
    for (int i = 0; i < limbs; i++) {
        uint64_t d = (uint64_t)a[i] - b[i] - borrow;
        a[i] = (uint32_t)d;
        borrow = d >> 63;
    }
}

static int Below(const uint32_t *a, const uint32_t *b, int limbs)
{
    for (int i = limbs; i-- > 0;) {
        if (a[i] != b[i])
            return a[i] < b[i];
    }
    return 0;
}

/* x = 2x + bit mod n, for x already below n. */
static void DoubleMod(const library_modulus *m, uint32_t *x, uint32_t bit)
{
    uint32_t carry = bit;
    for (int i = 0; i < m->limbs; i++) {
        uint32_t next = x[i] >> 31;
        x[i] = x[i] << 1 | carry;
        carry = next;
    }
    if (carry || !Below(x, m->n.limb, m->limbs))
        Subtract(x, m->n.limb, m->limbs);
}

/* out = a b / 2^(32 limbs) mod n, operands below n (CIOS). */
static void Multiply(const library_modulus *m, const uint32_t *a, const uint32_t *b, uint32_t *out)
{
    uint32_t t[LIBRARY_LIMBS + 2] = {0};
    int s = m->limbs;
    for (int i = 0; i < s; i++) {
        uint64_t c = 0;
        for (int j = 0; j < s; j++) {
            c += (uint64_t)a[j] * b[i] + t[j];
            t[j] = (uint32_t)c;
            c >>= 32;
        }
        c += t[s];
        t[s] = (uint32_t)c;
        t[s + 1] = (uint32_t)(c >> 32);

        uint32_t u = t[0] * m->inverse;
        c = ((uint64_t)u * m->n.limb[0] + t[0]) >> 32;
        for (int j = 1; j < s; j++) {
            c += (uint64_t)u * m->n.limb[j] + t[j];
            t[j - 1] = (uint32_t)c;
            c >>= 32;
        }
        c += t[s];
        t[s - 1] = (uint32_t)c;
        t[s] = t[s + 1] + (uint32_t)(c >> 32);
    }
    if (t[s] || !Below(t, m->n.limb, s))
        Subtract(t, m->n.limb, s);
    memcpy(out, t, sizeof(uint32_t) * (size_t)s);
}

int Library_Modulus(library_modulus *modulus, const char *decimal)
{
    memset(modulus, 0, sizeof *modulus);
    uint32_t *n = modulus->n.limb;
    if (!*decimal)
        return 0;
    for (const char *c = decimal; *c; c++) {
        if (*c < '0' || *c > '9')
            return 0;
        uint64_t carry = (uint64_t)(*c - '0');
        for (int i = 0; i < LIBRARY_LIMBS; i++) {
            carry += (uint64_t)n[i] * 10;
            n[i] = (uint32_t)carry;
            carry >>= 32;
        }
        if (carry)
            return 0;
    }
    modulus->bits = Bits(&modulus->n);
    modulus->limbs = (modulus->bits + 31) / 32;
    if (!(n[0] & 1) || modulus->bits < 2)
        return 0;

    /* Newton's iteration doubles the correct low bits of 1/n each step. */
    uint32_t x = n[0];
    for (int i = 0; i < 5; i++)
        x *= 2 - n[0] * x;
    modulus->inverse = 0 - x;

    uint32_t *r2 = modulus->r2.limb;
    r2[0] = 1;
    for (int i = 0; i < 64 * modulus->limbs; i++)
        DoubleMod(modulus, r2, 0);
    return 1;
}

int Library_Number(library_number *number, const uint8_t *bytes, size_t length)
{
    memset(number, 0, sizeof *number);
    for (size_t i = 0; i < length; i++) {
        size_t place = length - 1 - i;  /* bytes from the least significant */
        if (place >= 4 * LIBRARY_LIMBS) {
            if (bytes[i])
                return 0;
            continue;
        }
        number->limb[place / 4] |= (uint32_t)bytes[i] << (8 * (place % 4));
    }
    return 1;
}

uint32_t Library_Remainder(const library_number *number, uint32_t divisor)
{
    uint64_t rest = 0;
    for (int i = LIBRARY_LIMBS; i-- > 0;)
        rest = (rest << 32 | number->limb[i]) % divisor;
    return (uint32_t)rest;
}

void Library_Power(const library_modulus *modulus, const library_number *base, const library_number *exponent, library_number *out)
{
    uint32_t one[LIBRARY_LIMBS] = {1}, b[LIBRARY_LIMBS], x[LIBRARY_LIMBS];
    Multiply(modulus, base->limb, modulus->r2.limb, b);
    Multiply(modulus, one, modulus->r2.limb, x);
    for (int bit = Bits(exponent); bit-- > 0;) {
        Multiply(modulus, x, x, x);
        if (exponent->limb[bit / 32] >> (bit % 32) & 1)
            Multiply(modulus, x, b, x);
    }
    memset(out, 0, sizeof *out);
    Multiply(modulus, x, one, out->limb);
}

void Library_Target(const library_modulus *modulus, const char *identity, const library_seed *seed, library_number *target)
{
    uint8_t stretched[4 * LIBRARY_LIMBS + 16], length[8];
    size_t identityLength = strlen(identity), bytes = (size_t)(modulus->bits + 128 + 7) / 8;
    for (int i = 0; i < 8; i++)
        length[i] = (uint8_t)((uint64_t)identityLength >> (8 * i));
    shake256 shake;
    Shake256_Begin(&shake);
    Shake256_Absorb(&shake, "DERP library target", 20);
    Shake256_Absorb(&shake, seed->key, sizeof seed->key);
    Shake256_Absorb(&shake, length, sizeof length);
    Shake256_Absorb(&shake, identity, identityLength);
    Shake256_Squeeze(&shake, stretched, bytes);

    memset(target, 0, sizeof *target);
    for (size_t i = 0; i < 8 * bytes; i++)
        DoubleMod(modulus, target->limb, stretched[i / 8] >> (7 - i % 8) & 1);
}

int Library_IsTheirs(const library_modulus *modulus, const library_number *v, const library_number *target)
{
    library_number e = {{65537}}, power;
    if (Library_Compare(v, &modulus->n) >= 0)
        return 0;
    Library_Power(modulus, v, &e, &power);
    return Library_Compare(&power, target) == 0;
}
