/* A reader's biography (library.h): their answers made fit for the
   library's alphabet, the template (biography_template.c) written out with
   the wordings a variation number picks, laid out in lines and pages, and
   read back from a book's text. */
#include "library.h"
#include "biography_template.h"

#include <stdlib.h>
#include <string.h>

/* --- The answers --- */

static const char *const ones[20] = {
    "zero", "one", "two", "three", "four", "five", "six", "seven", "eight", "nine", "ten",
    "eleven", "twelve", "thirteen", "fourteen", "fifteen", "sixteen", "seventeen", "eighteen", "nineteen",
};
static const char *const tens[10] = {
    "", "", "twenty", "thirty", "forty", "fifty", "sixty", "seventy", "eighty", "ninety",
};

static void Append(char *out, size_t size, const char *words)
{
    size_t used = strlen(out);
    if (used && used + 1 < size)
        out[used++] = ' ', out[used] = 0;
    strncat(out, words, size - 1 - used);
}

/* 1-99 in words. */
static void Below100(char *out, size_t size, int n)
{
    if (n < 20) {
        Append(out, size, ones[n]);
        return;
    }
    Append(out, size, tens[n / 10]);
    if (n % 10)
        Append(out, size, ones[n % 10]);
}

/* A year as people say it: nineteen eighty four, nineteen oh five, two
   thousand six, twenty ten, nineteen hundred, seven seventy one. */
static void Year(char *out, size_t size, int year)
{
    out[0] = 0;
    if (year < 100) {
        Below100(out, size, year);
    } else if (year >= 1000 && year % 1000 < 10) {
        Below100(out, size, year / 1000);
        Append(out, size, "thousand");
        if (year % 10)
            Append(out, size, ones[year % 10]);
    } else {
        Below100(out, size, year / 100);
        if (year % 100 == 0)
            Append(out, size, "hundred");
        else if (year % 100 < 10)
            Append(out, size, "oh"), Append(out, size, ones[year % 100]);
        else
            Below100(out, size, year % 100);
    }
}

/* Latin-1's letters (U+00C0-U+00FF, as UTF-8 C3 80-BF) in plain letters. */
static const char *const latin1[64] = {
    "a", "a", "a", "a", "ae", "a", "ae", "c", "e", "e", "e", "e", "i", "i", "i", "i",
    "d", "n", "o", "o", "o", "o", "oe", "", "oe", "u", "u", "u", "ue", "y", "th", "ss",
    "a", "a", "a", "a", "ae", "a", "ae", "c", "e", "e", "e", "e", "i", "i", "i", "i",
    "d", "n", "o", "o", "o", "o", "oe", "", "oe", "u", "u", "u", "ue", "y", "th", "y",
};

int Library_Answer(int fact, const char *answer, char *out)
{
    char text[LIBRARY_FACT_SIZE] = "";
    size_t used = 0;
    int space = 0, digits = 0, number = 0;
    const unsigned char *c = (const unsigned char *)answer;

    for (; *c; c++) {
        const char *letters = NULL;
        char one[2] = {0, 0};
        if (*c >= 'a' && *c <= 'z')
            one[0] = (char)*c;
        else if (*c >= 'A' && *c <= 'Z')
            one[0] = (char)(*c - 'A' + 'a');
        else if (*c >= '0' && *c <= '9') {
            digits++;
            number = number < 100000 ? number * 10 + (*c - '0') : number;
            continue;
        } else if (*c == 0xc3 && c[1] >= 0x80 && c[1] <= 0xbf)
            letters = latin1[*++c - 0x80];
        else if (*c == '\'' || *c >= 0x80)
            continue;  /* o'brien is obrien; other scripts can't be spelled */
        else {
            space = used > 0;
            continue;
        }
        if (!letters)
            letters = one;
        if (space && used + 1 < sizeof text)
            text[used++] = ' ';
        space = 0;
        for (; *letters && used + 1 < sizeof text; letters++)
            text[used++] = *letters;
        text[used] = 0;
    }

    if (fact == LIBRARY_YEAR) {
        if (!digits || used || number < 1 || number > 9999)
            return 0;
        Year(out, LIBRARY_FACT_SIZE, number);
        return 1;
    }
    if (digits)
        return 0;  /* numbers in names are spelled out by the reader */
    if (fact == LIBRARY_PET && (!strcmp(text, "none") || !strcmp(text, "no") || !strcmp(text, "no pet")))
        text[0] = 0;
    if (!text[0] && fact != LIBRARY_PET)
        return 0;
    strcpy(out, text);
    return 1;
}

void Library_Identity(const library_reader *reader, char *identity)
{
    identity[0] = 0;
    for (int f = 0; f < LIBRARY_FACTS; f++) {
        if (f)
            strcat(identity, ", ");
        strcat(identity, reader->fact[f]);
    }
}

/* --- The template, compiled for one reader --- */

/* Every piece of text in one arena; an item is fixed text (one option) or a
   wording (2-16 options), each option a span of the arena. */
typedef struct {
    uint32_t at, length;
} span;

typedef struct {
    int options, first;
} item;

typedef struct {
    const library_reader *reader;
    char *chars;
    size_t used, size;
    span *spans;
    int spanCount, spanSize;
    item *items;
    int itemCount, itemSize;
    int bits, ok;
} compiled;

static void Grow(void **array, int *size, int count, size_t element, int *ok)
{
    if (count < *size)
        return;
    int bigger = *size ? *size * 2 : 256;
    void *grown = realloc(*array, (size_t)bigger * element);
    if (!grown) {
        *ok = 0;
        return;
    }
    *array = grown;
    *size = bigger;
}

static void Put(compiled *c, const char *text, size_t length)
{
    if (c->used + length > c->size) {
        size_t bigger = c->size ? c->size * 2 : 65536;
        while (bigger < c->used + length)
            bigger *= 2;
        char *grown = realloc(c->chars, bigger);
        if (!grown) {
            c->ok = 0;
            return;
        }
        c->chars = grown;
        c->size = bigger;
    }
    memcpy(c->chars + c->used, text, length);
    c->used += length;
}

static const char *Fact(const library_reader *reader, char key)
{
    switch (key) {
    case 'N': return reader->fact[LIBRARY_NAME];
    case 'Y': return reader->fact[LIBRARY_YEAR];
    case 'B': return reader->fact[LIBRARY_BIRTHPLACE];
    case 'H': return reader->fact[LIBRARY_HOMETOWN];
    case 'S': return reader->fact[LIBRARY_SIGN];
    case 'P': return reader->fact[LIBRARY_PET];
    case 'F': return reader->fact[LIBRARY_FOOD];
    }
    return "";
}

/* The text from..to with the reader's facts and pet put in. */
static void Expand(compiled *c, const char *from, const char *to)
{
    int pet = c->reader->fact[LIBRARY_PET][0] != 0, branch = -1;
    for (const char *s = from; s < to; s++) {
        if (*s == '<')
            branch = 0;
        else if (*s == '|' && branch >= 0)
            branch = 1;
        else if (*s == '>')
            branch = -1;
        else if (branch == (pet ? 1 : 0))
            continue;
        else if (*s == '$' && s + 1 < to) {
            const char *fact = Fact(c->reader, *++s);
            Put(c, fact, strlen(fact));
        } else
            Put(c, s, 1);
    }
}

static void AddSpan(compiled *c, const char *from, const char *to)
{
    Grow((void **)&c->spans, &c->spanSize, c->spanCount, sizeof *c->spans, &c->ok);
    if (!c->ok)
        return;
    size_t at = c->used;
    Expand(c, from, to);
    c->spans[c->spanCount].at = (uint32_t)at;
    c->spans[c->spanCount].length = (uint32_t)(c->used - at);
    c->spanCount++;
}

static void AddItem(compiled *c, int options)
{
    Grow((void **)&c->items, &c->itemSize, c->itemCount, sizeof *c->items, &c->ok);
    if (!c->ok)
        return;
    c->items[c->itemCount].options = options;
    c->items[c->itemCount].first = c->spanCount;
    c->itemCount++;
}

static void Compile(compiled *c, const char *text)
{
    while (*text && c->ok) {
        const char *open = strchr(text, '{');
        const char *end = open ? open : text + strlen(text);
        if (end > text) {
            AddItem(c, 1);
            AddSpan(c, text, end);
        }
        if (!open)
            return;
        const char *close = strchr(open, '}');
        int options = 1, depth = 0;
        for (const char *s = open + 1; s < close; s++) {
            depth += *s == '<' ? 1 : *s == '>' ? -1 : 0;
            options += *s == '|' && !depth;
        }
        AddItem(c, options);
        const char *option = open + 1;
        for (const char *s = open + 1; s <= close; s++) {
            depth += *s == '<' ? 1 : *s == '>' ? -1 : 0;
            if (s == close || (*s == '|' && !depth)) {
                AddSpan(c, option, s);
                option = s + 1;
            }
        }
        for (int o = options; o > 1; o >>= 1)
            c->bits++;
        text = close + 1;
    }
}

static int Begin(compiled *c, const library_reader *reader)
{
    memset(c, 0, sizeof *c);
    c->reader = reader;
    c->ok = 1;
    Compile(c, Biography_Opening);
    for (int d = 0; d < BIOGRAPHY_DAYS; d++) {
        char opener[64] = "one ";
        strcat(opener, Biography_Weekdays[d % 7]);
        strcat(opener, " in ");
        strcat(opener, Biography_Months[d * 7 % 12]);
        strcat(opener, ", ");
        Compile(c, opener);
        Compile(c, Biography_Days[d % BIOGRAPHY_DAY_TEMPLATES]);
    }
    Compile(c, Biography_Closing);
    return c->ok && c->bits == LIBRARY_VARIATION_BITS;
}

static void End(compiled *c)
{
    free(c->chars);
    free(c->spans);
    free(c->items);
}

/* Variation bits from the most significant: the first wording takes the top. */
static int Bits(const library_number *v, int from, int count)
{
    int value = 0;
    for (int i = 0; i < count; i++) {
        int bit = LIBRARY_VARIATION_BITS - 1 - from - i;
        value = value << 1 | (int)(v->limb[bit / 32] >> (bit % 32) & 1);
    }
    return value;
}

static int Width(int options)
{
    int bits = 0;
    while (options > 1)
        options >>= 1, bits++;
    return bits;
}

/* --- Lines and pages --- */

/* Words go on a line while one space is left after them, so every line ends
   in a space and a book read as a stream still has its words apart. */
typedef struct {
    char *text;
    size_t length, at;
    int line, page;  /* characters on a line, on a page */
    char word[LIBRARY_FACT_SIZE * 2];
    int wordLength, ok;
} layout;

static void Fill(layout *l, size_t to)
{
    if (to > l->length) {
        l->ok = 0;
        to = l->length;
    }
    while (l->at < to)
        l->text[l->at++] = ' ';
}

static void LineEnd(layout *l)
{
    if (l->at % l->line)
        Fill(l, l->at + l->line - l->at % l->line);
}

static void Word(layout *l)
{
    int column = (int)(l->at % l->line), length = l->wordLength;
    if (!length)
        return;
    l->wordLength = 0;
    if (column && column + 1 + length >= l->line)
        LineEnd(l), column = 0;
    if (column)
        Fill(l, l->at + 1);
    for (int i = 0; i < length && l->ok; i++) {
        if (l->at >= l->length)
            l->ok = 0;
        else
            l->text[l->at++] = l->word[i];
    }
}

static void Lay(layout *l, const char *s, size_t length)
{
    for (size_t i = 0; i < length; i++) {
        char ch = s[i];
        if (ch != ' ' && ch != '\n' && ch != '\v' && ch != '\f') {
            if (l->wordLength + 1 < (int)sizeof l->word)
                l->word[l->wordLength++] = ch;
            if (l->wordLength >= l->line - 1)
                Word(l);  /* a word longer than a line runs on */
            continue;
        }
        Word(l);
        if (ch == '\n') {
            LineEnd(l);
            if (l->at % l->page)
                Fill(l, l->at + l->line);
        } else if (ch == '\v') {
            LineEnd(l);
            Fill(l, l->at + l->line);
        } else if (ch == '\f') {
            LineEnd(l);
            if (l->at % l->page)
                Fill(l, l->at + l->page - l->at % l->page);
        }
    }
}

static int Render(const compiled *c, const library_number *v, int line, int lines, char *text, size_t length)
{
    layout l = {text, length, 0, line, line * lines, {0}, 0, 1};
    int from = 0;
    for (int i = 0; i < c->itemCount; i++) {
        const item *it = &c->items[i];
        int width = Width(it->options);
        const span *s = &c->spans[it->first + Bits(v, from, width)];
        from += width;
        Lay(&l, c->chars + s->at, s->length);
    }
    Word(&l);
    Fill(&l, length);
    text[length] = 0;
    return l.ok;
}

int Library_Biography(const library_reader *reader, const library_number *v, int line, int lines, char *text, size_t length)
{
    compiled c;
    int ok = Begin(&c, reader) && Render(&c, v, line, lines, text, length);
    End(&c);
    return ok;
}

/* --- Reading a book back --- */

/* The book read as a stream: any run of spaces is one, none at the start.
   A pattern's whitespace asks for that one space before its next letter. */
typedef struct {
    const char *text;
    size_t length, at;
    int pending, started;
} stream;

static int Match(stream *s, const char *pattern, size_t length)
{
    for (size_t i = 0; i < length; i++) {
        char ch = pattern[i];
        if (ch == ' ' || ch == '\n' || ch == '\v' || ch == '\f') {
            s->pending = s->started;
            continue;
        }
        if (s->pending) {
            if (s->at >= s->length || s->text[s->at] != ' ')
                return 0;
            while (s->at < s->length && s->text[s->at] == ' ')
                s->at++;
            s->pending = 0;
        }
        if (s->at >= s->length || s->text[s->at] != ch)
            return 0;
        s->at++;
        s->started = 1;
    }
    return 1;
}

static void SetBits(library_number *v, int from, int count, int value)
{
    for (int i = 0; i < count; i++) {
        int bit = LIBRARY_VARIATION_BITS - 1 - from - i;
        if (value >> (count - 1 - i) & 1)
            v->limb[bit / 32] |= 1u << (bit % 32);
    }
}

int Library_ReadBiography(const library_reader *reader, const char *text, size_t length, int line, int lines, library_number *v)
{
    compiled c;
    stream s = {text, length, 0, 0, 0};
    int ok = Begin(&c, reader), from = 0;

    while (s.at < length && text[s.at] == ' ')
        s.at++;
    memset(v, 0, sizeof *v);
    /* No two wordings of one place begin alike, so at most one fits. */
    for (int i = 0; ok && i < c.itemCount; i++) {
        const item *it = &c.items[i];
        int width = Width(it->options), o;
        for (o = 0; o < it->options; o++) {
            stream tried = s;
            const span *sp = &c.spans[it->first + o];
            if (Match(&tried, c.chars + sp->at, sp->length)) {
                s = tried;
                break;
            }
        }
        if (o == it->options)
            ok = 0;
        else
            SetBits(v, from, width, o);
        from += width;
    }
    while (ok && s.at < length)
        ok = text[s.at++] == ' ';

    /* The words fit; the lines and pages must too. */
    if (ok) {
        char *written = malloc(length + 1);
        ok = written && Render(&c, v, line, lines, written, length) && !memcmp(written, text, length);
        free(written);
    }
    End(&c);
    return ok;
}
