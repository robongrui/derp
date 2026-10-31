/* The library of episode 0.5, scp_7179_b (scp_7179_b/README.md):
   the mathematics only, with nothing of the game in it.

   Every possible book exists exactly once. A book is `length` characters over
   an alphabet of `symbols` characters, and its address is a number of the
   same size: `length` digits in base `symbols`. A seed keys a reversible
   scramble between the two, so a book follows from its address and an address
   from its book, and neighbouring addresses hold books that share nothing.

   Each reader's own book is one biography among them. Which one is fixed by
   RSA: the variation number v of the biography template with
   v^65537 mod N = target(identity, seed), N being the RSA-2048 challenge
   number, whose factors nobody knows. Checking a book is instant; finding it
   means taking a 65537th root mod N, and the one known way is factoring N
   (not proven the only one). Exactly one v below N is anyone's if 65537
   shares no factor with lambda(N): N's factors would show it, and it fails
   for about 2 in 65,536 such numbers (then a target has no v, or 65537). */
#ifndef LIBRARY_H
#define LIBRARY_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* SHAKE256 (FIPS 202), written from the standard: absorb any number of times,
   then squeeze any number of times. */
typedef struct {
    uint64_t state[25];
    unsigned position;  /* byte within the 136-byte block */
    int squeezing;
} shake256;

void Shake256_Begin(shake256 *shake);
void Shake256_Absorb(shake256 *shake, const void *data, size_t length);
void Shake256_Squeeze(shake256 *shake, void *out, size_t length);

/* What the books look like and how they stand. The address counts books
   shelf by shelf, shelves gallery by gallery, galleries floor by floor; the
   floor is whatever is left over, a number nearly as long as the address. */
typedef struct {
    const char *alphabet;  /* the symbols in digit order; distinct bytes */
    int symbols;           /* strlen(alphabet), 2-256 */
    size_t length;         /* characters in a book, and digits in an address */
    uint32_t galleries;    /* on a floor */
    uint32_t shelves;      /* in a gallery */
    uint32_t books;        /* on a shelf */
} library_format;

/* The seed as people type and share it: letters and digits, any case;
   everything else in seedText is dropped. The key is what the scramble uses. */
#define LIBRARY_SEED_TEXT 32
typedef struct {
    char text[LIBRARY_SEED_TEXT + 1];
    uint8_t key[32];
} library_seed;

void Library_Seed(const char *seedText, library_seed *seed);

/* Addresses and books are `length` digits (0 to symbols-1), most significant
   first. Each is the other's inverse; book and address may not overlap. */
void Library_BookAt(const library_format *format, const library_seed *seed, const uint8_t *address, uint8_t *book);
void Library_AddressOf(const library_format *format, const library_seed *seed, const uint8_t *book, uint8_t *address);

/* Digits to the alphabet's characters and back (text needs length + 1 bytes).
   Library_Digits returns 0 if text has the wrong length or a character the
   alphabet lacks. */
void Library_Text(const library_format *format, const uint8_t *digits, char *text);
int Library_Digits(const library_format *format, const char *text, uint8_t *digits);

/* A seed's own address, drawn evenly from the whole library (the tests; a
   recorded game's start gallery). */
void Library_Start(const library_format *format, const library_seed *seed, uint8_t *address);

/* Moves address by books (negative goes back). Returns 0, leaving address as
   it was, if that would leave the library. */
int Library_Step(const library_format *format, uint8_t *address, int64_t books);

/* b - a in books, in *books. Returns 0 if it doesn't fit 64 bits. */
int Library_Distance(const library_format *format, const uint8_t *a, const uint8_t *b, int64_t *books);

/* The address split into its floor (`length` digits, as an address) and its
   place on that floor, and joined again. */
typedef struct {
    uint32_t gallery, shelf, book;
} library_place;

void Library_Split(const library_format *format, const uint8_t *address, uint8_t *floor, library_place *place);
int Library_Join(const library_format *format, const uint8_t *floor, const library_place *place, uint8_t *address);

/* The library's top: floor 1, the highest whole floor (its last book there
   too), where everyone starts. Floors are counted down from it. */
void Library_TopFloor(const library_format *format, uint8_t *floor);

/* A note to share: "seed 7Q3K / floor 48214 / gallery 57 / shelf 4 / book 17",
   floors counted down from the top (1), gallery, shelf and book from 1.
   Library_Note returns 0 if the address is too far down to say so (2^63
   floors or more). Library_ReadNote takes any case, extra spaces and
   thousands separators, sets seed and address, and returns 0 for anything
   that isn't a note or points outside the library. */
#define LIBRARY_NOTE_SIZE 128
int Library_Note(const library_format *format, const library_seed *seed, const uint8_t *address, char *note);
int Library_ReadNote(const library_format *format, const char *note, library_seed *seed, uint8_t *address);

/* Numbers for the RSA check: up to 2048 bits, 32-bit limbs, least significant
   first. */
#define LIBRARY_LIMBS 64
typedef struct {
    uint32_t limb[LIBRARY_LIMBS];
} library_number;

/* An odd modulus of up to 2048 bits, set up for Montgomery multiplication. */
typedef struct {
    library_number n;
    int limbs;          /* in use */
    int bits;
    uint32_t inverse;   /* -1/n mod 2^32 */
    library_number r2;  /* 2^(64 limbs) mod n */
} library_modulus;

/* RSA-2048, from RSA Laboratories' factoring challenge (1991): 617 digits,
   the product of two primes nobody has found. */
extern const char Library_RSA2048[];

/* From decimal digits. Returns 0 for an even, too small (< 3) or too large
   modulus, or for anything but digits. */
int Library_Modulus(library_modulus *modulus, const char *decimal);

/* Big-endian bytes, as the template reads a variation number off a book.
   Returns 0 if they don't fit 2048 bits. */
int Library_Number(library_number *number, const uint8_t *bytes, size_t length);
int Library_Compare(const library_number *a, const library_number *b);
/* number mod a small divisor, for checks. */
uint32_t Library_Remainder(const library_number *number, uint32_t divisor);

/* base^exponent mod modulus; base must be below the modulus. */
void Library_Power(const library_modulus *modulus, const library_number *base, const library_number *exponent, library_number *out);

/* A reader's target: SHAKE256 of their identity (already normalized and in
   the library's alphabet) and the seed, stretched 128 bits past the modulus
   and reduced by it. */
void Library_Target(const library_modulus *modulus, const char *identity, const library_seed *seed, library_number *target);

/* Whether variation number v is the reader's: v below the modulus and
   v^65537 mod modulus equal to target. */
int Library_IsTheirs(const library_modulus *modulus, const library_number *v, const library_number *target);

/* The reader: what the questions at the start ask, made fit for the
   alphabet (lower case, single spaces, accents spelled out, the year in
   words). The pet may be empty: none. */
enum {
    LIBRARY_NAME, LIBRARY_YEAR, LIBRARY_BIRTHPLACE, LIBRARY_HOMETOWN,
    LIBRARY_SIGN, LIBRARY_PET, LIBRARY_FOOD, LIBRARY_FACTS
};
#define LIBRARY_FACT_SIZE 64
typedef struct {
    char fact[LIBRARY_FACTS][LIBRARY_FACT_SIZE];
} library_reader;

/* One answer, typed (ASCII, or UTF-8 Latin-1 letters), as its fact. Returns
   0 for an answer that can't be one: nothing but the pet left empty, digits
   anywhere but the year, a year that isn't a number from 1 to 9999. */
int Library_Answer(int fact, const char *answer, char *out);

/* The facts in one line for Library_Target. */
#define LIBRARY_IDENTITY_SIZE (LIBRARY_FACTS * (LIBRARY_FACT_SIZE + 2))
void Library_Identity(const library_reader *reader, char *identity);

/* The reader's biography with variation number v (its top bit is the first
   wording's), `length` characters in lines of `line` and pages of `lines`
   lines, padded with spaces (text needs length + 1). Returns 0 if it doesn't
   fit. Library_ReadBiography returns 1 and sets v only if text is exactly
   such a biography: gibberish fails at its first letters. */
#define LIBRARY_VARIATION_BITS 2048
int Library_Biography(const library_reader *reader, const library_number *v, int line, int lines, char *text, size_t length);
int Library_ReadBiography(const library_reader *reader, const char *text, size_t length, int line, int lines, library_number *v);

#ifdef __cplusplus
}
#endif

#endif
