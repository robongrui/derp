/* The library's books and addresses: the scramble between them, walking,
   and the notes people share (library.h). */
#include "library.h"

#include <ctype.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Eight rounds of a Feistel network over the two halves of the digits; four
   would already be a sound permutation, the rest is margin. */
#define ROUNDS 8

/* Digits drawn evenly from SHAKE256's stream: bytes past the last whole
   multiple of the alphabet are skipped, so no symbol comes up more often. */
typedef struct {
    shake256 shake;
    uint8_t buffer[512];
    size_t used;
    int symbols, limit;
} digit_stream;

static void Stream_Begin(digit_stream *stream, const char *purpose, const library_seed *seed, int symbols)
{
    Shake256_Begin(&stream->shake);
    Shake256_Absorb(&stream->shake, purpose, strlen(purpose) + 1);
    Shake256_Absorb(&stream->shake, seed->key, sizeof seed->key);
    stream->used = sizeof stream->buffer;
    stream->symbols = symbols;
    stream->limit = 256 - 256 % symbols;
}

static void Stream_Absorb(digit_stream *stream, const void *data, size_t length)
{
    Shake256_Absorb(&stream->shake, data, length);
}

static uint8_t Stream_Digit(digit_stream *stream)
{
    for (;;) {
        if (stream->used == sizeof stream->buffer) {
            Shake256_Squeeze(&stream->shake, stream->buffer, sizeof stream->buffer);
            stream->used = 0;
        }
        uint8_t byte = stream->buffer[stream->used++];
        if (byte < stream->limit)
            return (uint8_t)(byte % stream->symbols);
    }
}

void Library_Seed(const char *seedText, library_seed *seed)
{
    size_t n = 0;
    for (const char *c = seedText; *c && n < LIBRARY_SEED_TEXT; c++) {
        if (isalnum((unsigned char)*c))
            seed->text[n++] = (char)toupper((unsigned char)*c);
    }
    seed->text[n] = 0;
    shake256 shake;
    Shake256_Begin(&shake);
    Shake256_Absorb(&shake, "DERP library seed", 18);
    Shake256_Absorb(&shake, seed->text, n);
    Shake256_Squeeze(&shake, seed->key, sizeof seed->key);
}

/* One round: half `to` moves by digits drawn from the seed, the round and
   the whole of half `from`; sign -1 takes the same step back. */
static void Round(const library_format *format, const library_seed *seed, int round,
                  const uint8_t *from, size_t fromLength, uint8_t *to, size_t toLength, int sign)
{
    digit_stream stream;
    uint8_t header[10] = {(uint8_t)round, (uint8_t)(format->symbols - 1)};
    for (int i = 0; i < 8; i++)
        header[2 + i] = (uint8_t)((uint64_t)format->length >> (8 * i));
    Stream_Begin(&stream, "DERP library round", seed, format->symbols);
    Stream_Absorb(&stream, header, sizeof header);
    Stream_Absorb(&stream, from, fromLength);
    for (size_t i = 0; i < toLength; i++) {
        int step = Stream_Digit(&stream);
        to[i] = (uint8_t)((to[i] + (sign > 0 ? step : format->symbols - step)) % format->symbols);
    }
}

void Library_BookAt(const library_format *format, const library_seed *seed, const uint8_t *address, uint8_t *book)
{
    size_t left = format->length / 2, right = format->length - left;
    memcpy(book, address, format->length);
    for (int round = 0; round < ROUNDS; round++) {
        if (round % 2 == 0)
            Round(format, seed, round, book + left, right, book, left, 1);
        else
            Round(format, seed, round, book, left, book + left, right, 1);
    }
}

void Library_AddressOf(const library_format *format, const library_seed *seed, const uint8_t *book, uint8_t *address)
{
    size_t left = format->length / 2, right = format->length - left;
    memcpy(address, book, format->length);
    for (int round = ROUNDS - 1; round >= 0; round--) {
        if (round % 2 == 0)
            Round(format, seed, round, address + left, right, address, left, -1);
        else
            Round(format, seed, round, address, left, address + left, right, -1);
    }
}

void Library_Text(const library_format *format, const uint8_t *digits, char *text)
{
    for (size_t i = 0; i < format->length; i++)
        text[i] = format->alphabet[digits[i]];
    text[format->length] = 0;
}

int Library_Digits(const library_format *format, const char *text, uint8_t *digits)
{
    int value[256];
    for (int i = 0; i < 256; i++)
        value[i] = -1;
    for (int i = 0; i < format->symbols; i++)
        value[(unsigned char)format->alphabet[i]] = i;
    for (size_t i = 0; i < format->length; i++) {
        if (!text[i] || value[(unsigned char)text[i]] < 0)
            return 0;
        digits[i] = (uint8_t)value[(unsigned char)text[i]];
    }
    return text[format->length] == 0;
}

void Library_Start(const library_format *format, const library_seed *seed, uint8_t *address)
{
    digit_stream stream;
    Stream_Begin(&stream, "DERP library start", seed, format->symbols);
    for (size_t i = 0; i < format->length; i++)
        address[i] = Stream_Digit(&stream);
}

/* digits ± amount, wrapping round the whole library; 1 if it wrapped. */
static int Add(const library_format *format, uint8_t *digits, uint64_t amount, int sign)
{
    int carry = 0;
    for (size_t i = format->length; i-- > 0 && (amount || carry);) {
        int d = digits[i] + sign * ((int)(amount % (uint64_t)format->symbols) + carry);
        amount /= (uint64_t)format->symbols;
        carry = 0;
        if (d >= format->symbols) {
            d -= format->symbols;
            carry = 1;
        } else if (d < 0) {
            d += format->symbols;
            carry = 1;
        }
        digits[i] = (uint8_t)d;
    }
    return amount || carry;
}

int Library_Step(const library_format *format, uint8_t *address, int64_t books)
{
    uint64_t amount = books < 0 ? (uint64_t)-(books + 1) + 1 : (uint64_t)books;
    int sign = books < 0 ? -1 : 1;
    if (Add(format, address, amount, sign)) {
        Add(format, address, amount, -sign);
        return 0;
    }
    return 1;
}

int Library_Distance(const library_format *format, const uint8_t *a, const uint8_t *b, int64_t *books)
{
    int cmp = memcmp(a, b, format->length);
    const uint8_t *high = cmp > 0 ? a : b, *low = cmp > 0 ? b : a;
    /* high - low, most significant first, borrowing as it goes from the end. */
    uint8_t *difference = malloc(format->length ? format->length : 1);
    if (!difference)
        return 0;
    int borrow = 0;
    for (size_t i = format->length; i-- > 0;) {
        int d = high[i] - low[i] - borrow;
        borrow = d < 0;
        difference[i] = (uint8_t)(d < 0 ? d + format->symbols : d);
    }
    uint64_t value = 0;
    int fits = 1;
    for (size_t i = 0; i < format->length && fits; i++) {
        if (value > (UINT64_MAX - difference[i]) / (uint64_t)format->symbols)
            fits = 0;
        else
            value = value * (uint64_t)format->symbols + difference[i];
    }
    free(difference);
    if (!fits || value > (cmp > 0 ? (uint64_t)INT64_MAX + 1 : (uint64_t)INT64_MAX))
        return 0;
    *books = cmp > 0 ? (int64_t)(0 - value) : (int64_t)value;
    return 1;
}

static uint64_t PerFloor(const library_format *format)
{
    return (uint64_t)format->galleries * format->shelves * format->books;
}

void Library_Split(const library_format *format, const uint8_t *address, uint8_t *floor, library_place *place)
{
    uint64_t perFloor = PerFloor(format), rest = 0;
    for (size_t i = 0; i < format->length; i++) {
        rest = rest * (uint64_t)format->symbols + address[i];
        floor[i] = (uint8_t)(rest / perFloor);
        rest %= perFloor;
    }
    place->book = (uint32_t)(rest % format->books);
    place->shelf = (uint32_t)(rest / format->books % format->shelves);
    place->gallery = (uint32_t)(rest / format->books / format->shelves);
}

int Library_Join(const library_format *format, const uint8_t *floor, const library_place *place, uint8_t *address)
{
    if (place->gallery >= format->galleries || place->shelf >= format->shelves || place->book >= format->books)
        return 0;
    uint64_t perFloor = PerFloor(format);
    uint64_t carry = ((uint64_t)place->gallery * format->shelves + place->shelf) * format->books + place->book;
    for (size_t i = format->length; i-- > 0;) {
        uint64_t t = floor[i] * perFloor + carry;
        address[i] = (uint8_t)(t % (uint64_t)format->symbols);
        carry = t / (uint64_t)format->symbols;
    }
    return carry == 0;
}

void Library_TopFloor(const library_format *format, uint8_t *floor)
{
    uint8_t *last = malloc(format->length ? format->length : 1);
    library_place place;
    if (!last) {
        memset(floor, 0, format->length);
        return;
    }
    memset(last, format->symbols - 1, format->length);
    Library_Split(format, last, floor, &place);
    if (place.gallery != format->galleries - 1 || place.shelf != format->shelves - 1 || place.book != format->books - 1)
        Library_Step(format, floor, -1);
    free(last);
}

int Library_Note(const library_format *format, const library_seed *seed, const uint8_t *address, char *note)
{
    uint8_t *top = malloc(2 * format->length);
    if (!top)
        return 0;
    uint8_t *floor = top + format->length;
    library_place place;
    int64_t below;
    Library_TopFloor(format, top);
    Library_Split(format, address, floor, &place);
    int ok = Library_Distance(format, floor, top, &below) && below >= 0 && below < INT64_MAX;
    free(top);
    if (!ok)
        return 0;
    snprintf(note, LIBRARY_NOTE_SIZE, "seed %s / floor %" PRId64 " / gallery %" PRIu32 " / shelf %" PRIu32 " / book %" PRIu32,
             seed->text[0] ? seed->text : "-", below + 1, place.gallery + 1, place.shelf + 1, place.book + 1);
    return 1;
}
/* A signed number as people write it: + or - (or U+2212) in front, and
   commas, dots, apostrophes or underscores between the digits. */
static const char *ReadNumber(const char *c, int64_t *value)
{
    int negative = 0;
    uint64_t n = 0;
    if (*c == '+' || *c == '-') {
        negative = *c++ == '-';
    } else if (!strncmp(c, "\xe2\x88\x92", 3)) {
        negative = 1;
        c += 3;
    }
    if (!isdigit((unsigned char)*c))
        return NULL;
    for (;;) {
        if (isdigit((unsigned char)*c)) {
            if (n > ((uint64_t)INT64_MAX - (uint64_t)(*c - '0')) / 10)
                return NULL;
            n = n * 10 + (uint64_t)(*c++ - '0');
        } else if (strchr(",.'_", *c) && *c && isdigit((unsigned char)c[1])) {
            c++;
        } else {
            break;
        }
    }
    *value = negative ? -(int64_t)n : (int64_t)n;
    return c;
}

/* Whether text starts with word, in any case, and the word ends there. */
static int IsWord(const char *text, const char *word)
{
    size_t n = 0;
    for (; word[n]; n++) {
        if (tolower((unsigned char)text[n]) != word[n])
            return 0;
    }
    return !isalpha((unsigned char)text[n]);
}

int Library_ReadNote(const library_format *format, const char *note, library_seed *seed, uint8_t *address)
{
    static const char *const keys[] = {"seed", "floor", "gallery", "shelf", "book"};
    int64_t value[5] = {0};
    char seedText[LIBRARY_SEED_TEXT + 1] = "";
    int seen = 0;
    const char *c = note;
    while (*c) {
        if (!isalpha((unsigned char)*c)) {
            c++;
            continue;
        }
        int key = -1;
        for (int k = 0; k < 5 && key < 0; k++) {
            if (IsWord(c, keys[k]))
                key = k;
        }
        if (key < 0 || seen & (1 << key))
            return 0;
        seen |= 1 << key;
        c += strlen(keys[key]);
        while (*c == ' ' || *c == ':' || *c == '=')
            c++;
        if (key == 0) {
            size_t n = 0;
            while (isalnum((unsigned char)*c) || *c == '-') {
                if (n < LIBRARY_SEED_TEXT && *c != '-')
                    seedText[n++] = *c;
                c++;
            }
            seedText[n] = 0;
        } else if (!(c = ReadNumber(c, &value[key]))) {
            return 0;
        }
    }
    if (seen != 31)
        return 0;
    Library_Seed(seedText, seed);

    uint8_t *floor = malloc(format->length ? format->length : 1);
    if (!floor)
        return 0;
    library_place place;
    Library_TopFloor(format, floor);
    int ok = value[1] >= 1 && Library_Step(format, floor, -(value[1] - 1))
          && value[2] >= 1 && value[2] <= format->galleries
          && value[3] >= 1 && value[3] <= format->shelves
          && value[4] >= 1 && value[4] <= format->books;
    if (ok) {
        place.gallery = (uint32_t)(value[2] - 1);
        place.shelf = (uint32_t)(value[3] - 1);
        place.book = (uint32_t)(value[4] - 1);
        ok = Library_Join(format, floor, &place, address);
    }
    free(floor);
    return ok;
}
