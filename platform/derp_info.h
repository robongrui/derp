/* DERPINFO: what a map's author asks of DERP, in a text lump inside the WAD
   (DERPINFO.md for mappers). One block per map, like UMAPINFO:

       map E1M1
       {
           weather = snow          // none, rain, snow, nukage, sand, ash
           weatheramount = 0.7     // 0-1
           wind = 135              // degrees, as map angles go: 0 east, 90 north
           windamount = 0.8        // 0 still air, 0.5 a steady breeze (DERP's usual), 1 a gale
           sun = 200, 50           // where it stands: angle, then height over the horizon
           sun.dusk = 260, 8       // the same for one of F4's hours (dawn, morning, noon, afternoon, dusk)
           time = dusk             // the map's own hour: dawn, morning, noon, afternoon, dusk, night
           temperature = -10       // the map's own temperature outside, -30 to 50 C
           phobos = true           // Phobos' physics: low gravity, vacuum outside, mag boots
           gravity = 0.38          // on a Phobos map, in g (Earth's); 0 weightless
           library = true          // the library of episode 0.5 (scp_7179_b/README.md)
           mirror = 8, 128, 8, 192, 16, 112   // a wall mirror: along the wall from x1,y1 to
                                   // x2,y2 (facing its right, as a linedef's front), bottom to top
       }

   Every WAD loaded may bring one; a later WAD's settings for a map win key by
   key over an earlier one's, and the player's own file (derpinfo.txt beside
   the saves, where the Aim the sun key writes) over them all. DERP mode's
   hand-picked looks for the game's own maps are written the same way
   (Game_AuthoredInfo) and lie under all of these (the renderer's mapInfo).
   What a map leaves out DERP works out as before.
   Mistakes are reported on stderr and skipped, never fatal. Shared by the
   game programs (the game reads phobos and gravity, the renderer the rest). */
#ifndef DERP_INFO_H
#define DERP_INFO_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DERP_MIRRORS 8
/* What the map asks for; each unset (as derp_map_unset leaves it) means
   "work it out as without DERPINFO". */
typedef struct {
    int weather;          /* -1 unset; 0 none, 1 rain, 2 snow, 3 nukage, 4 sand, 5 ash, 6 fog, 7 dry ice, 8 firestorm (the renderer's Weather) */
    float weatherAmount;  /* -1 unset; 0-1 */
    float wind;           /* -1 unset; 0-360 degrees */
    int sun;              /* -1 unset; 0 hidden, 1 placed at sunAngle, sunHeight */
    float sunAngle, sunHeight; /* degrees, with sun 1 */
    int phobos;           /* -1 unset; 0 off, 1 on */
    float gravity;        /* -1 unset; Earth gravities, 0 and up */
    float windAmount;     /* -1 unset; 0 still air .. 1 a gale (0.5 DERP's usual breeze) */
    int library;          /* -1 unset; 1 the map is episode 0.5's library corridor (p_library.h) */
    float farFogNear, farFogFar; /* -1 unset; a distance fog as in Turok: clear to near, solid from far (map units) */
    int farFogColor;      /* -1 unset; its colour, 0xRRGGBB */
    int mirrors;          /* wall mirrors, each mirror line adding one (none: no more) */
    float mirror[DERP_MIRRORS][6]; /* x1, y1, x2, y2 along the wall, facing the right side; bottom, top */
    /* sun.<hour>: the sun at one of F4's hours, by climate_rules.h's
       ClimateTime (TIME_DAWN 1 .. TIME_DUSK 5; 0 and 6 stay unused). */
    int hourSun[7];       /* 0 unset; 1 placed at hourSunAngle, hourSunHeight */
    float hourSunAngle[7], hourSunHeight[7];
    int time;             /* 0 unset; the map's own hour, ClimateTime TIME_DAWN 1 .. TIME_NIGHT 6 */
    int temperatureSet, temperature; /* the map's own temperature outside (C, -30 to 50), with temperatureSet 1 */
} derp_map;

void DerpInfo_Unset(derp_map *map);
/* What over sets, laid over under key by key (one layer over the one below). */
void DerpInfo_Over(derp_map *under, const derp_map *over);
/* Reads the blocks for mapname (E1M1, MAP01; any case) out of one lump's
   text into map, key over key; what it doesn't mention stays as it was.
   where names the lump in messages. The number of blocks for mapname. */
int DerpInfo_Parse(const char *text, size_t length, const char *mapname, const char *where, derp_map *map);

/* The map being played (P_SetupLevel): every DERPINFO lump in load order,
   then the player's file, read for mapname into DerpInfo_Map. */
void DerpInfo_Level(const char *mapname);
extern derp_map DerpInfo_Map;
extern char DerpInfo_MapName[9];   /* the map DerpInfo_Level last read, as given */
extern unsigned DerpInfo_Serial;   /* one more each time DerpInfo_Level reads */

/* The player's own DERPINFO, in the save folder. */
#define DERPINFO_PLAYER_FILE "derpinfo.txt"
/* The hour words of time and sun.<hour>, by ClimateTime (dawn 1 .. night 6);
   NULL for TIME_OWN. sun.<hour> takes dawn to dusk: no sun at night. */
const char *DerpInfo_TimeName(int hour);
const char *DerpInfo_HourName(int hour);   /* the same without night */

/* DERPINFO in a text file (derp_info_file.c): the player's own, or DERP
   mode's authored looks as a dev tool writes them. Sets key = value in
   mapname's block of file, the line replaced where the key stands, else
   added (with the block, if new); value NULL takes the key's lines out. The
   rest of the file stays as it was. Whether it was written, and *had whether
   the key stood there before. */
int DerpInfo_FileSet(const char *file, const char *mapname, const char *key, const char *value, int *had);
/* The file's blocks for mapname over map (DerpInfo_Parse); 0 without one. */
int DerpInfo_FileRead(const char *file, const char *mapname, derp_map *map);

#ifdef __cplusplus
}
#endif

#endif
