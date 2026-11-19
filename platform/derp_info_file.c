/* DERPINFO in a text file (derp_info.h), in the lump's words: the player's
   own derpinfo.txt in the save folder, read after every WAD's so it wins
   (the Aim the sun key writes it, scene3d/sun_aim.cpp), or DERP mode's
   authored looks as the dev tools write them. Written a line at a time; what
   the writer doesn't touch stays as it was left. */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "derp_info.h"

/* The whole file, 0-terminated (free it), or NULL without one. */
static char *readAll(const char *file, size_t *length)
{
    FILE *f = fopen(file, "rb");
    char *text;
    long size;
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    text = (char *)malloc(size > 0 ? (size_t)size + 1 : 1);
    *length = text && size > 0 ? fread(text, 1, (size_t)size, f) : 0;
    if (text) text[*length] = 0;
    fclose(f);
    return text;
}

int DerpInfo_FileRead(const char *file, const char *mapname, derp_map *map)
{
    size_t length;
    char *text = readAll(file, &length);
    int blocks;
    if (!text) return 0;
    blocks = DerpInfo_Parse(text, length, mapname, file, map);
    free(text);
    return blocks;
}

/* The line's first word (letters, digits, . and _) into word; where it ends. */
static const char *firstWord(const char *line, char *word, size_t size)
{
    size_t n = 0;
    while (*line == ' ' || *line == '\t') ++line;
    while (*line && (isalnum((unsigned char)*line) || *line == '.' || *line == '_')) {
        if (n + 1 < size) word[n++] = *line;
        ++line;
    }
    word[n] = 0;
    return line;
}

static int sameWord(const char *a, const char *b)
{
    for (; *a && *b; ++a, ++b)
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
    return *a == *b;
}

int DerpInfo_FileSet(const char *file, const char *mapname, const char *key, const char *value, int *had)
{
    size_t length = 0, count = 0, i;
    char *text = readAll(file, &length), **lines, *at;
    /* The map's block being read: -1 outside, 0 its name seen, 1 inside. */
    int state = -1;
    size_t close = (size_t)-1;   /* the line of its last editable block's } */
    FILE *out;
    int ended = !text || !length || text[length - 1] == '\n';   /* the last line has its end */
    *had = 0;
    if (!text && !value) return 1;
    /* Into lines, their ends cut off (put back on writing). */
    lines = (char **)malloc((length + 2) * sizeof *lines);
    if (!lines) { free(text); return 0; }
    for (at = text; at && *at;) {
        lines[count++] = at;
        at = strchr(at, '\n');
        if (at) *at++ = 0;
    }
    for (i = 0; i < count; ++i) {
        char word[64], name[64];
        const char *rest = firstWord(lines[i], word, sizeof word);
        if (state < 0 && sameWord(word, "map")) {
            firstWord(rest, name, sizeof name);
            if (!sameWord(name, mapname)) continue;
            /* A block on one line (map E1M1 { sun = 1, 2 }) is left alone. */
            if (strchr(rest, '}')) continue;
            state = strchr(rest, '{') ? 1 : 0;
            continue;
        }
        if (state == 0) { if (strchr(lines[i], '{')) state = 1; continue; }
        if (state == 1) {
            if (strchr(lines[i], '}')) { close = i; state = -1; continue; }
            if (sameWord(word, key)) { lines[i] = NULL; *had = 1; }
        }
    }
    out = fopen(file, "wb");
    if (!out) { free(lines); free(text); return 0; }
    if (!text)
        fputs("// Your own DERPINFO (DERPINFO.md), over every WAD's: the Aim the sun key\n"
              "// writes here, and you may too.\n", out);
    for (i = 0; i < count; ++i) {
        if (i == close && value) fprintf(out, "    %s = %s\n", key, value);
        if (!lines[i]) continue;
        /* The file's last line had no end; give it one before more follows. */
        if (i + 1 < count || ended || (value && close == (size_t)-1))
            fprintf(out, "%s\n", lines[i]);
        else fputs(lines[i], out);
    }
    if (value && close == (size_t)-1)
        fprintf(out, "%smap %s\n{\n    %s = %s\n}\n", count ? "\n" : "", mapname, key, value);
    fclose(out);
    free(lines);
    free(text);
    return 1;
}
