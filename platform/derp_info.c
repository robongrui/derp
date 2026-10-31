/* DERPINFO (derp_info.h): reading a map's block out of the lump. A small
   tokenizer (words, numbers, quoted strings, { } = , ; and // or block
   comments), then key = value [, value] pairs inside map NAME { ... }. */
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "derp_info.h"
#include "w_wad.h"
#include "z_zone.h"

derp_map DerpInfo_Map = { -1, -1, -1, -1, 0, 0, -1, -1, -1, -1, -1, -1, -1 };

void DerpInfo_Unset(derp_map *map)
{
    map->weather = -1; map->weatherAmount = -1; map->wind = -1;
    map->sun = -1; map->sunAngle = map->sunHeight = 0;
    map->phobos = -1; map->gravity = -1; map->windAmount = -1;
    map->library = -1;
    map->farFogNear = map->farFogFar = -1; map->farFogColor = -1;
    map->mirrors = 0;
}

/* No <strings.h> on MSVC. */
static int same(const char *a, const char *b)
{
    for (; *a && *b; ++a, ++b)
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
    return *a == *b;
}

typedef struct {
    const char *at, *end, *where;
    int line;
    char token[64];
} reader;

/* The next token into r->token (empty at the end); 1 when there is one. */
static int next(reader *r)
{
    size_t n = 0;
    for (;;) {
        while (r->at < r->end && isspace((unsigned char)*r->at)) { if (*r->at == '\n') ++r->line; ++r->at; }
        if (r->end - r->at >= 2 && r->at[0] == '/' && r->at[1] == '/') {
            while (r->at < r->end && *r->at != '\n') ++r->at;
            continue;
        }
        if (r->end - r->at >= 2 && r->at[0] == '/' && r->at[1] == '*') {
            r->at += 2;
            while (r->at < r->end && !(r->end - r->at >= 2 && r->at[0] == '*' && r->at[1] == '/')) { if (*r->at == '\n') ++r->line; ++r->at; }
            r->at = r->at < r->end ? r->at + 2 : r->end;
            continue;
        }
        break;
    }
    r->token[0] = 0;
    if (r->at >= r->end) return 0;
    if (strchr("{}=,;", *r->at)) { r->token[0] = *r->at++; r->token[1] = 0; return 1; }
    if (*r->at == '"') {
        ++r->at;
        while (r->at < r->end && *r->at != '"' && *r->at != '\n') { if (n + 1 < sizeof r->token) r->token[n++] = *r->at; ++r->at; }
        if (r->at < r->end && *r->at == '"') ++r->at;
        r->token[n] = 0;
        return 1;
    }
    while (r->at < r->end && !isspace((unsigned char)*r->at) && !strchr("{}=,;\"", *r->at)
           && !(r->end - r->at >= 2 && r->at[0] == '/' && (r->at[1] == '/' || r->at[1] == '*'))) {
        if (n + 1 < sizeof r->token) r->token[n++] = *r->at;
        ++r->at;
    }
    r->token[n] = 0;
    return 1;
}

static void complain(const reader *r, const char *what)
{
    fprintf(stderr, "DERPINFO (%s, line %d): %s\n", r->where, r->line, what);
}

static int number(const char *token, float *out)
{
    char *stop;
    double v = strtod(token, &stop);
    if (!*token || *stop || !isfinite(v)) return 0;
    *out = (float)v;
    return 1;
}

static int flag(const char *token, int *out)
{
    if (same(token, "true") || same(token, "on") || same(token, "yes") || same(token, "1")) { *out = 1; return 1; }
    if (same(token, "false") || same(token, "off") || same(token, "no") || same(token, "0")) { *out = 0; return 1; }
    return 0;
}

/* One key's values (up to six) into map; whether they made sense. */
static int apply(const char *key, char values[][64], int count, derp_map *map)
{
    float a, b;
    int on, i;
    if (same(key, "weather")) {
        if (count != 1) return 0;
        if (same(values[0], "none")) map->weather = 0;
        else if (same(values[0], "rain")) map->weather = 1;
        else if (same(values[0], "snow")) map->weather = 2;
        else if (same(values[0], "nukage")) map->weather = 3;
        else if (same(values[0], "sand")) map->weather = 4;
        else if (same(values[0], "ash")) map->weather = 5;
        else if (same(values[0], "fog")) map->weather = 6;
        else if (same(values[0], "dryice")) map->weather = 7;
        else return 0;
        return 1;
    }
    if (same(key, "weatheramount")) {
        if (count != 1 || !number(values[0], &a)) return 0;
        map->weatherAmount = a < 0 ? 0 : a > 1 ? 1 : a;
        return 1;
    }
    if (same(key, "wind")) {
        if (count != 1 || !number(values[0], &a)) return 0;
        a = fmodf(a, 360); map->wind = a < 0 ? a + 360 : a;
        return 1;
    }
    if (same(key, "windamount")) {
        if (count != 1 || !number(values[0], &a)) return 0;
        map->windAmount = a < 0 ? 0 : a > 1 ? 1 : a;
        return 1;
    }
    if (same(key, "sun")) {
        if (count == 1 && (same(values[0], "none") || same(values[0], "hidden"))) { map->sun = 0; return 1; }
        if (count != 2 || !number(values[0], &a) || !number(values[1], &b)) return 0;
        a = fmodf(a, 360);
        map->sun = 1; map->sunAngle = a < 0 ? a + 360 : a;
        map->sunHeight = b < 1 ? 1 : b > 90 ? 90 : b;
        return 1;
    }
    if (same(key, "phobos")) {
        if (count != 1 || !flag(values[0], &on)) return 0;
        map->phobos = on;
        return 1;
    }
    if (same(key, "library")) {
        if (count != 1 || !flag(values[0], &on)) return 0;
        map->library = on;
        return 1;
    }
    if (same(key, "farfog")) {
        if (count == 1 && same(values[0], "none")) { map->farFogNear = map->farFogFar = -1; return 1; }
        if (count != 2 || !number(values[0], &a) || !number(values[1], &b) || a < 0 || b <= a) return 0;
        map->farFogNear = a; map->farFogFar = b;
        return 1;
    }
    if (same(key, "farfogcolor")) {
        char *end;
        long rgb = count == 1 ? strtol(values[0], &end, 16) : -1;
        if (count != 1 || *end || strlen(values[0]) != 6 || rgb < 0) return 0;
        map->farFogColor = (int)rgb;
        return 1;
    }
    if (same(key, "mirror")) {
        float *m = map->mirror[map->mirrors < DERP_MIRRORS ? map->mirrors : 0];
        if (count == 1 && same(values[0], "none")) { map->mirrors = 0; return 1; }
        if (count != 6 || map->mirrors == DERP_MIRRORS) return 0;
        for (i = 0; i < 6; ++i) if (!number(values[i], &m[i])) return 0;
        if (m[5] <= m[4] || (m[0] == m[2] && m[1] == m[3])) return 0;
        ++map->mirrors;
        return 1;
    }
    if (same(key, "gravity")) {
        if (count != 1 || !number(values[0], &a) || a < 0) return 0;
        map->gravity = a;
        return 1;
    }
    return -1;
}

int DerpInfo_Parse(const char *text, size_t length, const char *mapname, const char *where, derp_map *map)
{
    reader r = { text, text + length, where, 1, { 0 } };
    int found = 0;
    while (next(&r)) {
        char name[64], message[160];
        int mine;
        if (!same(r.token, "map")) {
            snprintf(message, sizeof message, "expected \"map\", found \"%s\"", r.token);
            complain(&r, message);
            continue;
        }
        if (!next(&r) || strchr("{}=,;", r.token[0])) { complain(&r, "a map name should follow \"map\""); continue; }
        snprintf(name, sizeof name, "%s", r.token);
        mine = same(name, mapname);
        if (!next(&r) || r.token[0] != '{') { complain(&r, "expected { after the map name"); continue; }
        found += mine;
        while (next(&r) && r.token[0] != '}') {
            char key[64], values[6][64];
            int count = 0, known;
            if (r.token[0] == ';') continue;
            snprintf(key, sizeof key, "%s", r.token);
            if (!next(&r) || r.token[0] != '=') {
                snprintf(message, sizeof message, "expected = after \"%s\"", key);
                complain(&r, message);
                if (r.token[0] == '}') break;
                continue;
            }
            /* The values follow on the key's line (after a comma, on the next). */
            char ended = 0;
            int valueLine = r.line;
            for (;;) {
                const char *mark = r.at;
                int line = r.line;
                if (!next(&r)) break;
                if (valueLine >= 0 && r.line != valueLine) { r.at = mark; r.line = line; break; }
                if (strchr("{}=,;", r.token[0])) { ended = r.token[0]; break; }
                if (count < 6) snprintf(values[count], sizeof values[count], "%s", r.token);
                ++count;
                valueLine = r.line;
                /* A comma goes on to the next value; anything else is the next key. */
                mark = r.at; line = r.line;
                if (!next(&r) || r.token[0] != ',') { r.at = mark; r.line = line; break; }
                valueLine = -1;
            }
            if (!count) {
                snprintf(message, sizeof message, "\"%s\" has no value", key);
                complain(&r, message);
                if (ended == '}') break;
                continue;
            }
            if (!mine) continue;
            known = count >= 1 && count <= 6 ? apply(key, values, count, map) : 0;
            if (known < 0) snprintf(message, sizeof message, "unknown key \"%s\" (skipped)", key);
            else if (!known) snprintf(message, sizeof message, "\"%s\" can't be set to that (skipped)", key);
            if (known <= 0) complain(&r, message);
        }
    }
    return found;
}

void DerpInfo_Level(const char *mapname)
{
    int i, blocks = 0;
    DerpInfo_Unset(&DerpInfo_Map);
    for (i = 0; i < numlumps; ++i) {
        char where[64];
        if (strncmp(lumpinfo[i].name, "DERPINFO", 8)) continue;
        snprintf(where, sizeof where, "lump %d", i);
        blocks += DerpInfo_Parse((const char *)W_CacheLumpNum(i, PU_CACHE), (size_t)W_LumpLength(i), mapname, where, &DerpInfo_Map);
    }
    if (blocks) {
        const derp_map *m = &DerpInfo_Map;
        fprintf(stderr, "DERPINFO %s: weather %d, amount %.2f, wind %.0f at %.2f, sun %d (%.0f, %.0f), phobos %d, gravity %g, library %d.\n",
                mapname, m->weather, m->weatherAmount, m->wind, m->windAmount, m->sun, m->sunAngle, m->sunHeight, m->phobos, m->gravity, m->library);
    }
}
