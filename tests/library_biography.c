/* The reader's book (platform/library/biography.c): answers made fit for the
   alphabet, the template's 2048 bits of wording, a biography read back to its
   variation number and nothing else, and the whole chain on a practice
   modulus, from answers to the book on its shelf and back. */
#include "../platform/library/library.h"
#include "../platform/library/biography_template.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(test) do { if (!(test)) { fprintf(stderr, "Failed at line %d\n", __LINE__); return 1; } } while (0)

#define LINE 80
#define LINES 40
static const library_format format = {"abcdefghijklmnopqrstuvwxyz ,.", 29, 410 * LINE * LINES, 100, 8, 252};

static int AnswerIs(int fact, const char *answer, const char *expected)
{
    char out[LIBRARY_FACT_SIZE];
    return Library_Answer(fact, answer, out) && !strcmp(out, expected);
}

static int Answers(void)
{
    char out[LIBRARY_FACT_SIZE];
    CHECK(AnswerIs(LIBRARY_NAME, "  Margaret   ELLISON ", "margaret ellison"));
    CHECK(AnswerIs(LIBRARY_NAME, "Sean O'Brien-Smith", "sean obrien smith"));
    CHECK(AnswerIs(LIBRARY_HOMETOWN, "Z\xc3\xbcrich", "zuerich"));
    CHECK(AnswerIs(LIBRARY_HOMETOWN, "S\xc3\xa3o Paulo", "sao paulo"));
    CHECK(AnswerIs(LIBRARY_FOOD, "Fish & chips!", "fish chips"));
    CHECK(AnswerIs(LIBRARY_YEAR, "1984", "nineteen eighty four"));
    CHECK(AnswerIs(LIBRARY_YEAR, "1905", "nineteen oh five"));
    CHECK(AnswerIs(LIBRARY_YEAR, "1900", "nineteen hundred"));
    CHECK(AnswerIs(LIBRARY_YEAR, "2006", "two thousand six"));
    CHECK(AnswerIs(LIBRARY_YEAR, "2000", "two thousand"));
    CHECK(AnswerIs(LIBRARY_YEAR, "2010", "twenty ten"));
    CHECK(AnswerIs(LIBRARY_YEAR, "1066", "ten sixty six"));
    CHECK(AnswerIs(LIBRARY_YEAR, "771", "seven seventy one"));
    CHECK(AnswerIs(LIBRARY_YEAR, "40", "forty"));
    CHECK(AnswerIs(LIBRARY_PET, "None", ""));
    CHECK(AnswerIs(LIBRARY_PET, "", ""));
    CHECK(AnswerIs(LIBRARY_PET, "Rex", "rex"));
    CHECK(!Library_Answer(LIBRARY_NAME, "", out));
    CHECK(!Library_Answer(LIBRARY_NAME, " .,- ", out));
    CHECK(!Library_Answer(LIBRARY_NAME, "R2D2", out));
    CHECK(!Library_Answer(LIBRARY_YEAR, "nineteen", out));
    CHECK(!Library_Answer(LIBRARY_YEAR, "0", out));
    CHECK(!Library_Answer(LIBRARY_YEAR, "10000", out));
    CHECK(!Library_Answer(LIBRARY_YEAR, "year 1984", out));
    return 0;
}

/* No two wordings of one place begin alike before a fact or an end, so a
   text was written one way only, whatever the reader's facts. */
static int Unambiguous(const char *text)
{
    for (const char *open = strchr(text, '{'); open; open = strchr(open + 1, '{')) {
        const char *close = strchr(open, '}'), *a = open + 1;
        int options = 0;
        while (a < close) {
            const char *aEnd = a;
            while (aEnd < close && *aEnd != '|')
                aEnd++;
            options++;
            CHECK(*a != '$' && *a != '<' && *a != ' ' && aEnd[-1] != ' ');
            for (const char *b = aEnd + 1; b < close;) {
                const char *bEnd = b;
                while (bEnd < close && *bEnd != '|')
                    bEnd++;
                size_t i = 0;
                while (a + i < aEnd && b + i < bEnd && a[i] == b[i] && a[i] != '$' && a[i] != '<')
                    i++;
                CHECK(a + i < aEnd && b + i < bEnd && a[i] != b[i]);
                b = bEnd + 1;
            }
            a = aEnd + 1;
        }
        CHECK(options >= 2 && options <= 16 && !(options & (options - 1)));
    }
    return 0;
}

static void Reader(library_reader *reader, int pet)
{
    memset(reader, 0, sizeof *reader);
    Library_Answer(LIBRARY_NAME, "Margaret Ellison", reader->fact[LIBRARY_NAME]);
    Library_Answer(LIBRARY_YEAR, "1984", reader->fact[LIBRARY_YEAR]);
    Library_Answer(LIBRARY_BIRTHPLACE, "Edinburgh", reader->fact[LIBRARY_BIRTHPLACE]);
    Library_Answer(LIBRARY_HOMETOWN, "Leith", reader->fact[LIBRARY_HOMETOWN]);
    Library_Answer(LIBRARY_SIGN, "Capricorn", reader->fact[LIBRARY_SIGN]);
    Library_Answer(LIBRARY_PET, pet ? "Biscuit" : "none", reader->fact[LIBRARY_PET]);
    Library_Answer(LIBRARY_FOOD, "Porridge", reader->fact[LIBRARY_FOOD]);
}

static void Variation(const char *purpose, library_number *v)
{
    uint8_t bytes[LIBRARY_VARIATION_BITS / 8];
    shake256 shake;
    Shake256_Begin(&shake);
    Shake256_Absorb(&shake, purpose, strlen(purpose));
    Shake256_Squeeze(&shake, bytes, sizeof bytes);
    Library_Number(v, bytes, sizeof bytes);
}

static int Biographies(void)
{
    char *text = malloc(format.length + 1);
    uint8_t *digits = malloc(format.length);
    library_reader reader, other;
    library_number v, back, zero = {{0}}, ones;
    memset(&ones, 0xff, sizeof ones);
    CHECK(text && digits);

    for (int pet = 0; pet < 2; pet++) {
        Reader(&reader, pet);
        const library_number *tried[3] = {&zero, &ones, &v};
        Variation(pet ? "with a pet" : "without", &v);
        for (int t = 0; t < 3; t++) {
            CHECK(Library_Biography(&reader, tried[t], LINE, LINES, text, format.length));
            CHECK(strlen(text) == format.length);
            CHECK(Library_Digits(&format, text, digits));  /* the alphabet's only */
            CHECK(Library_ReadBiography(&reader, text, format.length, LINE, LINES, &back));
            CHECK(Library_Compare(&back, tried[t]) == 0);
        }
        /* Its title page, its first page of life, its last line before the end. */
        CHECK(!strncmp(text + 18 * LINE, "the life of margaret ellison ", 29));
        CHECK(!strncmp(text + LINES * LINE, "you were born in edinburgh in nineteen eighty four,", 51));
        CHECK(strstr(text, pet ? "you had biscuit, and you loved biscuit" : "you never had a pet"));
        CHECK(strstr(text, "you opened this one, and read your life in it"));

        /* One wording changed reads as its own variation; a letter changed, a
           line moved, or another reader's facts don't read at all. */
        Library_Biography(&reader, &v, LINE, LINES, text, format.length);
        char *changed = strstr(text, "you woke early");
        if (!changed)
            changed = strstr(text, "you slept late");
        CHECK(changed);
        char *edit = malloc(format.length + 1);
        memcpy(edit, text, format.length + 1);
        edit[changed - text + 4] = 'x';
        CHECK(!Library_ReadBiography(&reader, edit, format.length, LINE, LINES, &back));
        memcpy(edit, text, format.length + 1);
        memmove(edit + LINES * LINE + 1, edit + LINES * LINE, LINE);  /* a space slipped in */
        edit[LINES * LINE] = ' ';
        CHECK(!Library_ReadBiography(&reader, edit, format.length, LINE, LINES, &back));
        free(edit);
        other = reader;
        Library_Answer(LIBRARY_HOMETOWN, "Glasgow", other.fact[LIBRARY_HOMETOWN]);
        CHECK(!Library_ReadBiography(&other, text, format.length, LINE, LINES, &back));
    }

    /* Gibberish, as the shelves hold it. */
    library_seed seed;
    uint8_t *address = malloc(format.length);
    Library_Seed("7Q3K", &seed);
    Library_Start(&format, &seed, address);
    Library_BookAt(&format, &seed, address, digits);
    Library_Text(&format, digits, text);
    CHECK(!Library_ReadBiography(&reader, text, format.length, LINE, LINES, &back));
    free(address);
    free(text);
    free(digits);
    return 0;
}

/* With a modulus whose factors are known the book can be worked out: its
   variation number is the target to the private exponent. Written into the
   library, read off the shelf at its address, it checks as the reader's. */
static int Found(void)
{
    library_modulus modulus;
    library_reader reader;
    library_number target, d = {{0}}, v, back;
    library_seed seed;
    char identity[LIBRARY_IDENTITY_SIZE];
    char *text = malloc(format.length + 1);
    uint8_t *digits = malloc(format.length), *address = malloc(format.length), *book = malloc(format.length);
    CHECK(text && digits && address && book);

    /* The first practice modulus of tests/library.c and its private exponent. */
    CHECK(Library_Modulus(&modulus, "6732696301172969473"));
    d.limb[1] = 0x02d0d32c;
    d.limb[0] = 0x1219d101;
    Reader(&reader, 1);
    Library_Identity(&reader, identity);
    CHECK(!strcmp(identity, "margaret ellison, nineteen eighty four, edinburgh, leith, capricorn, biscuit, porridge"));
    Library_Seed("7Q3K", &seed);
    Library_Target(&modulus, identity, &seed, &target);
    Library_Power(&modulus, &target, &d, &v);
    CHECK(Library_IsTheirs(&modulus, &v, &target));

    CHECK(Library_Biography(&reader, &v, LINE, LINES, text, format.length));
    CHECK(Library_Digits(&format, text, digits));
    Library_AddressOf(&format, &seed, digits, address);
    Library_BookAt(&format, &seed, address, book);
    Library_Text(&format, book, text);
    CHECK(Library_ReadBiography(&reader, text, format.length, LINE, LINES, &back));
    CHECK(Library_IsTheirs(&modulus, &back, &target));
    back.limb[0] ^= 1;
    CHECK(!Library_IsTheirs(&modulus, &back, &target));
    free(text); free(digits); free(address); free(book);
    return 0;
}

int main(void)
{
    if (Unambiguous(Biography_Opening) || Unambiguous(Biography_Closing))
        return 1;
    for (int i = 0; i < BIOGRAPHY_DAY_TEMPLATES; i++)
        if (Unambiguous(Biography_Days[i]))
            return 1;
    if (Answers() || Biographies() || Found())
        return 1;
    printf("Library biography: all checks passed\n");
    return 0;
}
