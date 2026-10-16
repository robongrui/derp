/* DOOM's answers to platform/game_materials.h: the texture families of the
   DOOM IWADs, Final DOOM, SIGIL, Legacy of Rust and Freedoom. */
#include <ctype.h>
#include <string.h>
#include "../../emissive.h"
#include "../../screen_glass.h"
#include "../../audio/ambient_synth.h"
#include "../../audio/impact_synth.h"
#include "../../game_materials.h"

static int starts(const char *name, const char *prefix) { return !strncmp(name, prefix, strlen(prefix)); }

static int prefixed(const char *name, const char *const *list)
{
    for (; *list; ++list)
    {
        size_t i = 0;
        while ((*list)[i] && toupper((unsigned char)name[i]) == (*list)[i]) ++i;
        if (!(*list)[i]) return 1;
    }
    return 0;
}

int Game_GlowKind(const char *name, int flat) { return doom_emissive_kind(name, flat); }
int Game_SparkTexture(const char *name, int flat) { return doom_spark_texture(name, flat); }
int Game_ScreenTexture(const char *name, int flat) { return doom_screen_texture(name, flat); }

/* Doom 2's SLIME13-16 share the name but are metal floors; map.cpp also
   asks that the flat animates, which leaves them out. */
game_liquid Game_LiquidFlat(const char *name)
{
    if (!strncmp(name, "FWATER", 6)) return GAME_LIQUID_WATER;
    if (!strncmp(name, "NUKAGE", 6) || !strncmp(name, "SLIME", 5) || !strncmp(name, "BLOOD", 5)
        || !strncmp(name, "LAVA", 4))
        return GAME_LIQUID_THICK;
    return GAME_LIQUID_NONE;
}

int Game_HotFlat(const char *name) { return !strncmp(name, "LAVA", 4); }

int Game_DamagingFloor(int special)
{
    return special == 4 || special == 5 || special == 7 || special == 11 || special == 16;
}

/* SLIME13 on are metal in DOOM II, only the first eight are sludge. */
int Game_FloorStep(const char *name)
{
    static const char *const metals[] = { "FLOOR4", "FLOOR5", "FLAT20", "FLAT22", "FLAT23", "CEIL3", "COMP",
        "GATE", "STEP", "TLITE", "CONS1", NULL };
    static const char *const woods[] = { "FLAT5_1", "FLAT5_2", "CEIL1_1", "CRATOP", NULL };
    static const char *const soils[] = { "GRASS", "RROCK19", "RROCK20", "MFLR8_3", "FLAT10", NULL };
    int kind = IMPACT_STEP_STONE;
    int sludge = starts(name, "SLIME") && name[5] == '0' && name[6] >= '1' && name[6] <= '8';
    if (starts(name, "FWATER") || starts(name, "SWATER") || starts(name, "WATER")) kind = IMPACT_SPLASH_WATER;
    else if (starts(name, "NUKAGE") || starts(name, "BLOOD") || starts(name, "LAVA") || sludge) kind = IMPACT_SPLASH_THICK;
    else
    {
        for (int k = 0; metals[k]; ++k) if (starts(name, metals[k])) kind = IMPACT_STEP_METAL;
        for (int k = 0; woods[k]; ++k) if (starts(name, woods[k])) kind = IMPACT_STEP_WOOD;
        for (int k = 0; soils[k]; ++k) if (starts(name, soils[k])) kind = IMPACT_STEP_SOFT;
    }
    return kind;
}

/* The IWADs' texture families; PWAD names that match none sound like stone. */
game_surface Game_WallSurface(const char *name)
{
    static const char *const metals[] = {"STARTAN", "STARG", "STARBR", "COMP", "METAL", "SUPPORT",
        "SHAWN", "DOOR", "BIGDOOR", "EXITDOOR", "LITE", "PIPE", "PLAT", "TEKWALL", "TEKGREN", "TEKLITE",
        "SLADWALL", "SILVER", "BRONZE", "SPACEW", "NUKE", "SW1", "SW2", "STEP", "MIDGRATE", "MIDBARS",
        "ICKDOOR", "ICKWALL", "TEKBRON", "BROWNPIP", NULL};
    static const char *const woods[] = {"WOOD", "PANEL", "PANBOOK", "PANBORD", "PANCASE", NULL};
    static const char *const fleshes[] = {"SKIN", "SKSNAKE", "SKSPINE", "SP_FACE", "FLESH", "SLOPPY", "SPCDOOR", NULL};
    if (prefixed(name, woods)) return GAME_SURFACE_WOOD;
    if (prefixed(name, fleshes)) return GAME_SURFACE_FLESH;
    if (prefixed(name, metals)) return GAME_SURFACE_METAL;
    return GAME_SURFACE_STONE;
}

game_surface Game_FlatSurface(const char *name)
{
    static const char *const liquids[] = {"NUKAGE", "FWATER", "SWATER", "SLIME", "BLOOD", "LAVA", "WATER", NULL};
    static const char *const soils[] = {"GRASS", "RROCK19", "RROCK20", "MFLR8_3", "FLAT10", NULL};
    static const char *const woods[] = {"FLAT5_1", "FLAT5_2", "CEIL1_1", "CRATOP", NULL};
    static const char *const metals[] = {"FLOOR4", "FLOOR5", "FLAT14", "FLAT20", "FLAT22", "FLAT23",
        "CEIL3", "COMP", "GATE", "STEP", "TLITE", "SLIME13", "CONS1", NULL};
    if (prefixed(name, liquids)) return GAME_SURFACE_LIQUID;
    if (prefixed(name, soils)) return GAME_SURFACE_SOIL;
    if (prefixed(name, woods)) return GAME_SURFACE_WOOD;
    if (prefixed(name, metals)) return GAME_SURFACE_METAL;
    return GAME_SURFACE_STONE;
}

/* Lamps, the console flats, liquids, hot rock and teleporter pads. */
int Game_AmbientFlat(const char *name, int ceiling)
{
    int glow = doom_emissive_kind(name, 1);
    if (glow == DOOM_EMISSIVE_LAMP) return AMBIENT_LAMP;
    if (glow == DOOM_EMISSIVE_PANEL) return AMBIENT_COMPUTER;
    if (ceiling) return -1;
    if (glow == DOOM_EMISSIVE_LAVA) return AMBIENT_LAVA;
    if (glow == DOOM_EMISSIVE_MAGMA) return AMBIENT_MAGMA;
    if (starts(name, "NUKAGE") || starts(name, "SLIME")) return AMBIENT_NUKAGE;
    if (starts(name, "BLOOD")) return AMBIENT_BLOOD;
    if (starts(name, "FWATER")) return AMBIENT_WATER;
    if (starts(name, "GATE")) return AMBIENT_TELEPORTER;
    return -1;
}

/* Light fixtures and the lit strips beside key doors, computers and
   screens, falls of liquid, fire and hot rock. Walls with only a lamp or a
   few lights painted in (BSTONE3, the LED-dotted TEKWALL) stay quiet, and so
   does the Icon of Sin's wall. */
int Game_AmbientWall(const char *name)
{
    static const char *const lamps[] = { "TLITE", "LITE", "LIGHT", "AQLITE", "TEKLITE", "METLITE",
        "DOORBLU", "DOORRED", "DOORYEL", 0 };
    static const char *const falls[] = { "SFALL", "SLADFAL", "BLODGR", "BLODRIP", "WFALL", 0 };
    int glow = doom_emissive_kind(name, 0);
    for (int i = 0; falls[i]; ++i) if (starts(name, falls[i])) return AMBIENT_SLIMEFALL;
    if (glow == DOOM_EMISSIVE_LAVA) return starts(name, "DBRAIN") ? -1 : AMBIENT_FIRE;
    if (glow == DOOM_EMISSIVE_MAGMA) return AMBIENT_MAGMA;
    if (glow == DOOM_EMISSIVE_LAMP)
    {
        for (int i = 0; lamps[i]; ++i) if (starts(name, lamps[i])) return AMBIENT_LAMP;
        return -1;
    }
    if (glow == DOOM_EMISSIVE_PANEL)
    {
        if (starts(name, "COMP") || starts(name, "AQCOMP")) return AMBIENT_COMPUTER;
        if (starts(name, "PLANET") || starts(name, "SILVER3")) return AMBIENT_CRT;
    }
    return -1;
}
