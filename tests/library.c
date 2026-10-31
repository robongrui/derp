/* The library of episode 0.5 (platform/library/library.h): SHAKE256 against
   FIPS 202's vectors, the scramble as a true permutation, walking, notes, and
   the RSA check, with expected values worked out independently in Python. */
#include "../platform/library/library.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(test) do { if (!(test)) { fprintf(stderr, "Failed at line %d\n", __LINE__); return 1; } } while (0)

static void Hex(const uint8_t *bytes, size_t length, char *out)
{
    for (size_t i = 0; i < length; i++)
        sprintf(out + 2 * i, "%02x", bytes[i]);
}

static int ShakeIs(const void *data, size_t length, size_t outLength, const char *expected)
{
    uint8_t out[512];
    char hex[1025];
    shake256 shake;
    Shake256_Begin(&shake);
    Shake256_Absorb(&shake, data, length);
    Shake256_Squeeze(&shake, out, outLength);
    Hex(out, outLength, hex);
    return !strcmp(hex, expected);
}

/* A number from hex digits, most significant first. */
static void NumberFromHex(const char *hex, library_number *number)
{
    uint8_t bytes[4 * LIBRARY_LIMBS + 1];
    size_t digits = strlen(hex), length = (digits + 1) / 2;
    for (size_t i = 0; i < length; i++) {
        unsigned value;
        size_t at = digits - 2 * (length - i);  /* may start at "-1": one digit */
        char pair[3] = {at == (size_t)-1 ? '0' : hex[at], hex[at + 1], 0};
        sscanf(pair, "%x", &value);
        bytes[i] = (uint8_t)value;
    }
    Library_Number(number, bytes, length);
}

static int NumberIs(const library_number *number, const char *hex)
{
    library_number expected;
    NumberFromHex(hex, &expected);
    return Library_Compare(number, &expected) == 0;
}

static int Shake(void)
{
    uint8_t a3[200], counting[1000];
    memset(a3, 0xa3, sizeof a3);
    for (int i = 0; i < 1000; i++)
        counting[i] = (uint8_t)(i % 251);

    CHECK(ShakeIs("", 0, 64, "46b9dd2b0ba88d13233b3feb743eeb243fcd52ea62b81b82b50c27646ed5762f"
                             "d75dc4ddd8c0f200cb05019d67b592f6fc821c49479ab48640292eacb3b7c4be"));
    CHECK(ShakeIs("abc", 3, 64, "483366601360a8771c6863080cc4114d8db44530f8f1e1ee4f94ea37e78b5739"
                                "d5a15bef186a5386c75744c0527e1faa9f8726e462a12a4feb06bd8801e751e4"));
    /* FIPS 202's 200 bytes of 0xa3, squeezed past three blocks. */
    CHECK(ShakeIs(a3, sizeof a3, 300,
        "cd8a920ed141aa0407a22d59288652e9d9f1a7ee0c1e7c1ca699424da84a904d2d700caae7396ece96604440577da4f3"
        "aa22aeb8857f961c4cd8e06f0ae6610b1048a7f64e1074cd629e85ad7566048efc4fb500b486a3309a8f26724c0ed628"
        "001a1099422468de726f1061d99eb9e93604d5aa7467d4b1bd6484582a384317d7f47d750b8f5499512bb85a226c4243"
        "556e696f6bd072c5aa2d9b69730244b56853d16970ad817e213e470618178001c9fb56c54fefa5fee67d2da524bb3b0b"
        "61ef0e9114a92cdbb6cccb98615cfe76e3510dd88d1cc28ff99287512f24bfafa1a76877b6f37198e3a641c68a7c42d4"
        "5fa7acc10dae5f3cefb7b735f12d4e589f7a456e78c0f5e4c4471fffa5e4fa0514ae974d8c2648513b5db494cea84715"
        "6d277ad0e141c24c7839064c"));
    CHECK(ShakeIs(counting, sizeof counting, 40, "34833f03ed88bb5f083ce590c7ae5af93ede33e11f53c70e47916c7044746acbdca19a73ff13905e"));

    /* In pieces it is the same stream. */
    uint8_t whole[300], pieces[300];
    shake256 shake;
    Shake256_Begin(&shake);
    Shake256_Absorb(&shake, counting, sizeof counting);
    Shake256_Squeeze(&shake, whole, sizeof whole);
    Shake256_Begin(&shake);
    Shake256_Absorb(&shake, counting, 135);
    Shake256_Absorb(&shake, counting + 135, 1);
    Shake256_Absorb(&shake, counting + 136, 864);
    Shake256_Squeeze(&shake, pieces, 1);
    Shake256_Squeeze(&shake, pieces + 1, 135);
    Shake256_Squeeze(&shake, pieces + 136, 164);
    CHECK(!memcmp(whole, pieces, sizeof whole));
    return 0;
}

/* Every address of a small library: each book comes up exactly once, and
   each book leads back to its address. */
static int SmallLibrary(void)
{
    const library_format format = {"abc", 3, 7, 2, 3, 4};
    enum { BOOKS = 2187 };  /* 3^7 */
    static uint8_t seen[BOOKS];
    uint8_t address[7] = {0}, book[7], back[7], floor[7];
    library_seed seed;
    library_place place;
    Library_Seed("7Q3K", &seed);
    for (int n = 0; n < BOOKS; n++) {
        int index = 0;
        Library_BookAt(&format, &seed, address, book);
        Library_AddressOf(&format, &seed, book, back);
        CHECK(!memcmp(address, back, sizeof address));
        for (int i = 0; i < 7; i++)
            index = index * 3 + book[i];
        CHECK(!seen[index]);
        seen[index] = 1;

        Library_Split(&format, address, floor, &place);
        CHECK(place.gallery < 2 && place.shelf < 3 && place.book < 4);
        CHECK(Library_Join(&format, floor, &place, back) && !memcmp(address, back, sizeof address));
        CHECK(Library_Step(&format, address, 1) == (n < BOOKS - 1));
    }
    /* At the last book: nothing further, and a failed step changes nothing. */
    memcpy(back, address, sizeof address);
    CHECK(!Library_Step(&format, address, 1) && !memcmp(address, back, sizeof address));
    CHECK(Library_Step(&format, address, -(BOOKS - 1)));
    CHECK(!memcmp(address, "\0\0\0\0\0\0\0", 7));
    CHECK(!Library_Step(&format, address, -1) && !memcmp(address, "\0\0\0\0\0\0\0", 7));
    /* Past the last whole floor there is no room for a full one. */
    memset(floor, 0, sizeof floor);
    floor[6] = 91;
    place.gallery = 1;
    place.shelf = 2;
    place.book = 3;
    CHECK(!Library_Join(&format, floor, &place, back));
    return 0;
}

static int Differences(const uint8_t *a, const uint8_t *b, size_t length)
{
    int n = 0;
    for (size_t i = 0; i < length; i++)
        n += a[i] != b[i];
    return n;
}

static int BigLibrary(void)
{
    const library_format format = {"abcdefghijklmnopqrstuvwxyz ,.", 29, 3200, 400, 8, 32};
    const size_t length = 3200;
    uint8_t *address = malloc(length), *next = malloc(length), *book = malloc(length), *other = malloc(length);
    char *text = malloc(length + 1);
    library_seed seed, typed, different;
    int64_t books;
    Library_Seed("7Q3K", &seed);
    Library_Seed(" 7q-3k ", &typed);
    Library_Seed("7Q3L", &different);
    CHECK(!strcmp(typed.text, "7Q3K") && !memcmp(seed.key, typed.key, sizeof seed.key));

    Library_Start(&format, &seed, address);
    Library_BookAt(&format, &seed, address, book);
    Library_AddressOf(&format, &seed, book, other);
    CHECK(!memcmp(address, other, length));

    /* Neighbours share nothing: about 28 characters in 29 differ. */
    memcpy(next, address, length);
    CHECK(Library_Step(&format, next, 1));
    Library_BookAt(&format, &seed, next, other);
    CHECK(Differences(book, other, length) > 3000);
    Library_BookAt(&format, &different, address, other);
    CHECK(Differences(book, other, length) > 3000);

    /* Text and back. */
    Library_Text(&format, book, text);
    CHECK(strlen(text) == length && Library_Digits(&format, text, other) && !memcmp(book, other, length));
    text[17] = '!';
    CHECK(!Library_Digits(&format, text, other));
    text[17] = 'a';
    text[length - 1] = 0;
    CHECK(!Library_Digits(&format, text, other));

    /* Distances: near, both ways, and too far to count. */
    CHECK(Library_Step(&format, next, 4));
    CHECK(Library_Distance(&format, address, next, &books) && books == 5);
    CHECK(Library_Distance(&format, next, address, &books) && books == -5);
    CHECK(Library_Distance(&format, address, address, &books) && books == 0);
    memset(other, 0, length);
    CHECK(!Library_Distance(&format, other, address, &books));

    /* Notes: floors counted down from the top, the rest from 1, and every
       note leads back. */
    char note[LIBRARY_NOTE_SIZE];
    library_seed readSeed;
    uint8_t *floor = malloc(length), *top = malloc(length);
    library_place place = {0, 0, 0};
    Library_TopFloor(&format, top);
    CHECK(Library_Join(&format, top, &place, next));
    CHECK(Library_Note(&format, &seed, next, note));
    CHECK(!strcmp(note, "seed 7Q3K / floor 1 / gallery 1 / shelf 1 / book 1"));
    CHECK(Library_ReadNote(&format, note, &readSeed, other) && !memcmp(other, next, length));
    CHECK(!strcmp(readSeed.text, "7Q3K"));
    /* Nothing above the top. */
    memcpy(floor, top, length);
    CHECK(!Library_Step(&format, floor, 1) || memcmp(floor, top, length) > 0);

    memcpy(floor, top, length);
    CHECK(Library_Step(&format, floor, -48213));
    place.gallery = 3; place.shelf = 1; place.book = 4;
    CHECK(Library_Join(&format, floor, &place, next));
    CHECK(Library_Note(&format, &seed, next, note));
    CHECK(!strcmp(note, "seed 7Q3K / floor 48214 / gallery 4 / shelf 2 / book 5"));
    CHECK(Library_ReadNote(&format, note, &readSeed, other) && !memcmp(other, next, length));

    /* As people might type it: other case, separators, other dots. */
    CHECK(Library_ReadNote(&format, "SEED 7q3k Â· Floor 48,214 Â· gallery: 4 Â· shelf 2 Â· book 5",
                           &readSeed, other) && !memcmp(other, next, length));

    /* Not notes. */
    CHECK(!Library_ReadNote(&format, "seed 7Q3K / floor 1 / gallery 1 / shelf 1", &readSeed, other));
    CHECK(!Library_ReadNote(&format, "seed 7Q3K / floor 1 / floor 2 / gallery 1 / shelf 1 / book 1", &readSeed, other));
    CHECK(!Library_ReadNote(&format, "seed 7Q3K / floor 0 / gallery 1 / shelf 1 / book 1", &readSeed, other));
    CHECK(!Library_ReadNote(&format, "seed 7Q3K / floor 1 / gallery 0 / shelf 1 / book 1", &readSeed, other));
    CHECK(!Library_ReadNote(&format, "seed 7Q3K / floor 1 / gallery 401 / shelf 1 / book 1", &readSeed, other));
    CHECK(!Library_ReadNote(&format, "seed 7Q3K / floor 1 / gallery 1 / shelf 0 / book 1", &readSeed, other));
    CHECK(!Library_ReadNote(&format, "seed 7Q3K / floor 1 / gallery 1 / shelf 1 / book 33", &readSeed, other));
    CHECK(!Library_ReadNote(&format, "seed 7Q3K / floor up / gallery 1 / shelf 1 / book 1", &readSeed, other));
    CHECK(!Library_ReadNote(&format, "the seed 7Q3K / floor 1 / gallery 1 / shelf 1 / book 1", &readSeed, other));

    free(top);
    free(address); free(next); free(book); free(other); free(text); free(floor);
    return 0;
}

/* A whole book of Borges' size, 410 pages of 40 lines of 80 characters. */
static int BorgesBook(void)
{
    const library_format format = {"abcdefghijklmnopqrstuvwxyz ,.", 29, 1312000, 400, 8, 32};
    uint8_t *address = malloc(format.length), *book = malloc(format.length), *back = malloc(format.length);
    library_seed seed;
    Library_Seed("7Q3K", &seed);
    Library_Start(&format, &seed, address);
    Library_BookAt(&format, &seed, address, book);
    Library_AddressOf(&format, &seed, book, back);
    CHECK(!memcmp(address, back, format.length));
    free(address); free(book); free(back);
    return 0;
}

static int Primes(uint32_t below, uint32_t *primes)
{
    int n = 0;
    for (uint32_t p = 2; p < below; p++) {
        int prime = 1;
        for (int i = 0; i < n && primes[i] * primes[i] <= p; i++) {
            if (p % primes[i] == 0) {
                prime = 0;
                break;
            }
        }
        if (prime)
            primes[n++] = p;
    }
    return n;
}

static int Rsa2048(void)
{
    library_modulus modulus;
    library_number target, power, minusOne, one = {{1}}, two = {{2}}, e = {{65537}};
    library_seed seed;
    CHECK(strlen(Library_RSA2048) == 617);
    CHECK(Library_Modulus(&modulus, Library_RSA2048));
    CHECK(modulus.bits == 2048 && modulus.limbs == 64);
    CHECK(modulus.n.limb[63] == 0xc7970ceeu && modulus.n.limb[0] == 0x361cc7e5u);

    /* As a product of two large primes: nothing small divides it, and Fermat's
       test says it is composite. */
    static uint32_t primes[10000];
    int count = Primes(100000, primes);
    for (int i = 0; i < count; i++)
        CHECK(Library_Remainder(&modulus.n, primes[i]) != 0);
    minusOne = modulus.n;
    minusOne.limb[0] -= 1;
    Library_Power(&modulus, &two, &minusOne, &power);
    CHECK(Library_Compare(&power, &one) != 0);

    /* A reader's target and its power, as Python's hashlib and pow() have them. */
    Library_Seed("7Q3K", &seed);
    Library_Target(&modulus, "margaret ellison born in edinburgh", &seed, &target);
    CHECK(NumberIs(&target,
        "46832a8c3af78719c90c93eba83809bcd03539f35993accd2a052704006ad7ac0cff210ff59cc94298ab413c2b9a4baa"
        "9d8a9fcc42e43612ba37f362f0ea453fce7f9b2f386586ef819e6d8ccec649d2ee227f8460a76c1845b2b0d094eeaed1"
        "54829bbe1cd431e6948f7288609f7d4c3389be00cc42fc3bb3f918e48805d1983a7f145c22314aa5e457739e9b5d89d8"
        "11df0057479b8b48be130f7ffb8cc12f34022d607bdacb4feae210d80db67d4d06c3299d97b3801d09a3e25d6e5f78af"
        "0027f91ffec21af6f5ac9c5fcf85e9d1914878046613dbf83f7f27718514fbad4218f8c40391d02e8b213e5fa1055170"
        "75cb4381284320f7142afde8cdb725f1"));
    Library_Power(&modulus, &target, &e, &power);
    CHECK(NumberIs(&power,
        "c230b07a2b479706d3220a111fef576928f854f6d7bc6f1253697aa06764f98a2c0a347c2e2b4e6a15e0f179747a66e2"
        "08b1bea34cac4af41a13f0f0976312f254358d271f2a8fec9d4804d554b2c6c914ba3361a9bec6f0d1996a898a918912"
        "f45dea438e7be14248a22d6f7e5b6dd29b1d2792422832ae2d421099110add2208857d92f8be13162799eecf1f82757d"
        "9e149c5e77e0e51529dea698b1a48f06581756eb68d2169dd61366d52b28321805d33b29e675abb79327f845ac60f3a0"
        "7ab716011a20adeb81f90841e868ae9a9aa69f5c8abe8146247092a7ea67f0392b9817a9bcb93325e82eb3f68727c13e"
        "ddce15071dad9a2a7b88337e07bc30ab"));
    CHECK(!Library_IsTheirs(&modulus, &target, &target));

    /* Not moduli. */
    char tooBig[700];
    memset(tooBig, '9', sizeof tooBig - 1);
    tooBig[sizeof tooBig - 1] = 0;
    CHECK(!Library_Modulus(&modulus, "10"));
    CHECK(!Library_Modulus(&modulus, "1"));
    CHECK(!Library_Modulus(&modulus, "12a5"));
    CHECK(!Library_Modulus(&modulus, ""));
    CHECK(!Library_Modulus(&modulus, tooBig));
    return 0;
}

/* Practice moduli whose factors are known, so the book can be found: its
   variation number is the target to the private exponent. */
static int Practice(const char *n, const char *d, const char *v)
{
    library_modulus modulus;
    library_number target, exponent, found, other;
    library_seed seed;
    CHECK(Library_Modulus(&modulus, n));
    Library_Seed("7Q3K", &seed);
    Library_Target(&modulus, "margaret ellison born in edinburgh", &seed, &target);
    NumberFromHex(d, &exponent);
    Library_Power(&modulus, &target, &exponent, &found);
    CHECK(NumberIs(&found, v));
    CHECK(Library_IsTheirs(&modulus, &found, &target));

    other = found;
    other.limb[0] ^= 1;
    CHECK(!Library_IsTheirs(&modulus, &other, &target));
    CHECK(!Library_IsTheirs(&modulus, &modulus.n, &target));
    Library_Target(&modulus, "margaret ellison born in glasgow", &seed, &other);
    CHECK(!Library_IsTheirs(&modulus, &found, &other));
    return 0;
}

int main(void)
{
    if (Shake() || SmallLibrary() || BigLibrary() || BorgesBook() || Rsa2048())
        return 1;
    if (Practice("6732696301172969473", "2d0d32c1219d101", "1714230ea0d0e705"))
        return 1;
    if (Practice("33519519824856492748935062495514615318698414551480983444308903609304981895369925169389644065503532"
                 "65037893905650110661540867718945811151039530284955468317",
                 "100beff4100beff4100beff4100beff4100beff4100beff4100beff4176d40c4bddb4224bddb4224bddb4224bddb4224"
                 "bddb4224bddb4224bddb44f060226801",
                 "18207e71174be441bf6d428a8a52f54cfada694dd65c58ab16ee795f71ef0cf1782ede61e24384f930fe375409db7d15"
                 "7ecc5955de943dadddda499a14400a3d"))
        return 1;
    printf("Library: all checks passed\n");
    return 0;
}
