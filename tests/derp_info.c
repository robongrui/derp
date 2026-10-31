/* DERPINFO (platform/derp_info.h): reading a map's block out of the lump. */
#include "../platform/derp_info.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define CHECK(test) do { if (!(test)) { fprintf(stderr, "Failed at line %d\n", __LINE__); return 1; } } while (0)
#define NEAR(a, b) (fabsf((a) - (b)) < 1e-4f)

static int parse(const char *text, const char *mapname, derp_map *map)
{
    DerpInfo_Unset(map);
    return DerpInfo_Parse(text, strlen(text), mapname, "test", map);
}

int main(void)
{
    derp_map map;
    const char *lump =
        "// The first map snows\n"
        "map E1M1\n"
        "{\n"
        "    weather = snow\n"
        "    weatheramount = 0.7   // a bit less than a blizzard\n"
        "    wind = -45\n"
        "    windamount = 1.5   // past a gale: held at 1\n"
        "    sun = 200, 50\n"
        "}\n"
        "/* the second floats */\n"
        "map e1m2 { phobos = true; gravity = 0 }\n"
        "MAP MAP01 { sun = none\n weather = \"rain\" }\n";

    CHECK(parse(lump, "E1M1", &map) == 1);
    CHECK(map.weather == 2 && NEAR(map.weatherAmount, 0.7f) && NEAR(map.wind, 315) && NEAR(map.windAmount, 1));
    CHECK(map.sun == 1 && NEAR(map.sunAngle, 200) && NEAR(map.sunHeight, 50));
    CHECK(map.phobos == -1 && map.gravity < 0);

    CHECK(parse(lump, "E1M2", &map) == 1);
    CHECK(map.phobos == 1 && map.gravity == 0 && map.weather == -1 && map.sun == -1 && map.windAmount < 0);

    CHECK(parse(lump, "map01", &map) == 1);
    CHECK(map.sun == 0 && map.weather == 1);

    /* A map the lump doesn't name keeps everything unset. */
    CHECK(parse(lump, "E1M3", &map) == 0);
    CHECK(map.weather == -1 && map.wind < 0 && map.sun == -1 && map.phobos == -1);

    /* A later lump wins key by key; what it leaves out stays. */
    parse(lump, "E1M1", &map);
    CHECK(DerpInfo_Parse("map E1M1 { weather = rain }", 27, "E1M1", "test", &map) == 1);
    CHECK(map.weather == 1 && NEAR(map.weatherAmount, 0.7f));
    CHECK(DerpInfo_Parse("map E1M1 { weather = NUKAGE }", 29, "E1M1", "test", &map) == 1 && map.weather == 3);

    /* Mistakes are skipped, the rest still read; values are kept in range. */
    CHECK(parse("map E1M1 { weather = hail\n colour = red\n wind = \n gravity = -1\n sun = 10, 120\n"
                " weatheramount = 3\n phobos = maybe\n }", "E1M1", &map) == 1);
    CHECK(map.weather == -1 && map.wind < 0 && map.gravity < 0 && map.phobos == -1);
    CHECK(map.sun == 1 && NEAR(map.sunHeight, 90) && NEAR(map.weatherAmount, 1));

    /* Broken text never runs away: a missing brace, a stray word, an open comment. */
    CHECK(parse("map E1M1 { weather = snow", "E1M1", &map) == 1 && map.weather == 2);
    CHECK(parse("hello map E1M1 { wind = 90 } /* never closed", "E1M1", &map) == 1 && NEAR(map.wind, 90));
    CHECK(parse("", "E1M1", &map) == 0);

    /* A value list goes on past a comma at the end of a line; a key with no
       value on its line leaves the next line's key alone. */
    CHECK(parse("map E1M1 {\n sun = 30,\n 20\n wind =\n weather = rain }", "E1M1", &map) == 1);
    CHECK(map.sun == 1 && NEAR(map.sunAngle, 30) && NEAR(map.sunHeight, 20) && map.wind < 0 && map.weather == 1);

    /* Episode 0.5's library: a switch like phobos, unset where not said. */
    CHECK(parse("map MAP01 { library = true }", "MAP01", &map) == 1 && map.library == 1);
    CHECK(parse("map MAP01 { library = maybe }", "MAP01", &map) == 1 && map.library == -1);
    CHECK(parse(lump, "E1M1", &map) == 1 && map.library == -1);

    /* Wall mirrors: each line adds one, six numbers; a bad one is skipped,
       none clears them. */
    CHECK(parse("map MAP01 { mirror = 8, 128, 8, 192, 16, 112\n mirror = 1, 2, 3, 4, 5\n"
                " mirror = 0, 0, 0, 0, 1, 2\n mirror = 600, 192, 600, 128, 0, 96 }", "MAP01", &map) == 1);
    CHECK(map.mirrors == 2 && NEAR(map.mirror[0][1], 128) && NEAR(map.mirror[0][5], 112) && NEAR(map.mirror[1][0], 600));
    CHECK(parse("map MAP01 { mirror = 8, 128, 8, 192, 16, 112\n mirror = none }", "MAP01", &map) == 1 && map.mirrors == 0);
    CHECK(parse(lump, "E1M1", &map) == 1 && map.mirrors == 0);

    puts("DERPINFO checks passed");
    return 0;
}
