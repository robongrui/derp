/* Conservative defaults; external masks can identify arbitrary custom artwork. */
#ifndef DOOM_EMISSIVE_H
#define DOOM_EMISSIVE_H
#include <string.h>

enum doom_emissive_kind { DOOM_EMISSIVE_NONE, DOOM_EMISSIVE_LAMP,
    DOOM_EMISSIVE_PANEL, DOOM_EMISSIVE_LAVA, DOOM_EMISSIVE_SLIME, DOOM_EMISSIVE_MAGMA,
    DOOM_EMISSIVE_GLASS /* backlit stained glass: every coloured pane, softly */,
    DOOM_EMISSIVE_DIFFUSE /* pale ceiling panels lit from behind: a dim, broad glow */,
    DOOM_EMISSIVE_STEP /* stair risers with warm lamps painted in under the lip */,
    DOOM_EMISSIVE_KEY /* key-door strips: a mask rule only (doom_key_door); their kind stays LAMP */,
    DOOM_EMISSIVE_SWITCH /* switches: the small red or green light on the plate, whichever is lit */ };
/* Lists end with NULL; a trailing '*' marks a prefix, otherwise the name must match exactly. */
static inline int doom_emissive_match(const char *name, const char *const *list)
{
    for (; *list; ++list) {
        size_t n=strlen(*list);
        if ((*list)[n-1]=='*' ? !strncmp(name,*list,n-1) : !strcmp(name,*list)) return 1;
    }
    return 0;
}
/* Names are uppercase, NUL-terminated WAD names (at most eight characters).
   Covers Doom, Doom II, Final Doom, SIGIL, Legacy of Rust and Freedoom artwork. */
static inline int doom_emissive_kind(const char *name, int flat)
{
    static const char *const lava[]={"LAVA*","BLAVA*",0};
    static const char *const fireWalls[]={"FIREBLU*","FIRELAV*","FIREMAG*","FIREWAL*","FIREGLO*",
        "FLMWAL*","DBRAIN*",0};
    static const char *const magmaFlats[]={"RROCK0*","MFLR9_5","MFLR9_6","SLIME09","SLIME10",
        "SLIME11","SLIME12",0};
    static const char *const magmaWalls[]={"CRACKLE*","ROCKRED*","ROCKLAV*",0};
    static const char *const slimeWalls[]={"SFALL*","SLADRIP*","SLADFAL*",0};
    static const char *const lampFlats[]={"TLITE*","CEIL1_2*","CEIL1_3*","GRNLITE*","FLAT22*",
        "MLITE*","KCF_LIT*","FLOOR8_*","FLAT2","FLAT17","FLAT23","FLOOR1_7","TCLTA",0};
    static const char *const lampWalls[]={"TLITE*","LITE*","LIGHT*","AQLITE*","TEKLITE*",
        "METLITE*","SHAWLIT*","WOODLIT*","STONLIT*","BRICKLIT*","DOORBLU*","DOORRED*",
        "DOORYEL*","KCMRK*","TCMRK*","KSW_LOCK","KSW_OPEN","KSW_COM*","BSTONE3","SILVER2",
        "TCGRDB","TEKGREN5",
        /* Doom II's riveted panels with a yellow strip light (MAP03's tower). */
        "METAL6","METAL7",0};
    static const char *const panelFlats[]={"CONS1_*",0};
    /* Doom 1 E2's warehouse ceiling grid (E2M2's crate rooms): its pale tan
       panels read as diffusers; the software renderer shows those rooms
       bright, and nothing else there lights them. */
    static const char *const diffuseFlats[]={"CEIL3_4",0};
    static const char *const panelWalls[]={"COMP*","AQCOMP*","TEKWALL*","PLANET*","SILVER3",
        "EXITSIGN","EIXTSIGN","EXITSTON","PNK4EXIT",
        /* Doom II's pipe wall with a gauge: four green lamps in a dark slot. */
        "PIPEWAL1",0};
    /* Legacy of Rust ships unlit variants such as COMP1OFF and LITE4OFF. */
    if (strstr(name,"OFF")) return DOOM_EMISSIVE_NONE;
    /* Switches (SW1*, and SW2* once thrown): the lamp on the plate glows, red
       or green as the artwork has it lit in that state; levers, faces and
       skulls with no lamp pass nothing by the pixel rule. Not the two of
       flesh and gore, whose reds are no lamps. */
    if (!flat && (!strncmp(name,"SW1",3) || !strncmp(name,"SW2",3)))
        return !strncmp(name+3,"SKIN",4) || !strncmp(name+3,"SKULL",5) ? DOOM_EMISSIVE_NONE : DOOM_EMISSIVE_SWITCH;
    if (doom_emissive_match(name,lava) || (!flat && doom_emissive_match(name,fireWalls)))
        return DOOM_EMISSIVE_LAVA;
    if (doom_emissive_match(name,flat?magmaFlats:magmaWalls)) return DOOM_EMISSIVE_MAGMA;
    if (!strncmp(name,"NUK",3) || (!flat && doom_emissive_match(name,slimeWalls)))
        return DOOM_EMISSIVE_SLIME;
    if (doom_emissive_match(name,flat?lampFlats:lampWalls)) return DOOM_EMISSIVE_LAMP;
    if (doom_emissive_match(name,flat?panelFlats:panelWalls)) return DOOM_EMISSIVE_PANEL;
    if (flat && doom_emissive_match(name,diffuseFlats)) return DOOM_EMISSIVE_DIFFUSE;
    /* Doom 1's lit riser: STEP1's one orange lamp per tile under a grey lip.
       (STEP5's warm panels and STEP6's bevels were tried and read wrong.) */
    if (!flat && !strcmp(name,"STEP1")) return DOOM_EMISSIVE_STEP;
    return DOOM_EMISSIVE_NONE;
}
/* The strips that mark a key door (keycards, and the skull keys' "2" set).
   Their glow mask takes DOOM_EMISSIVE_KEY: by the lamp rule only the one
   brightest shade of the blue and red strips passed, at 40%, and the doors
   were hard to tell apart in a dim hall. */
static inline int doom_key_door(const char *name)
{
    return !strncmp(name,"DOORBLU",7) || !strncmp(name,"DOORRED",7) || !strncmp(name,"DOORYEL",7);
}
/* Plain coloured paint that is a light only where the mapper lit it as one:
   FLOOR1_6 (two dark reds) is red surface paint across E3M7's ceilings, but
   E2M1's inverted cross at 255 between rooms at 192 is a glowing fixture.
   The sector test is the renderer's (lit_paint.cpp); this names the flats. */
static inline int doom_paint_lamp(const char *name, int flat)
{
    return flat && !strcmp(name,"FLOOR1_6");
}
/* Small lamps painted dim that are drawn brighter and light their room as a
   fixture does (wallImage): PIPEWAL1's gauge, four green lamps at under half
   brightness in sewers that have little other light. */
static inline int doom_dim_fixture(const char *name)
{
    return !strcmp(name,"PIPEWAL1");
}
/* Machinery that throws electric sparks when shot, glowing or not: computer
   panels, tech walls and their light fixtures (shot_lights.cpp). */
static inline int doom_spark_texture(const char *name, int flat)
{
    static const char *const walls[]={"COMP*","AQCOMP*","TEKWALL*","TEKLITE*","TEKGREN*","PLANET*",
        "SILVER*","SPACEW*","METLITE*","LITE*","TLITE*",
        "METAL6","METAL7",0};
    static const char *const flats[]={"CONS1_*","TLITE*",0};
    if (strstr(name,"OFF")) return 0;
    return doom_emissive_match(name,flat?flats:walls);
}
/* Keep dark outlines and housing shaded; hue tests avoid lighting pale metal. */
static inline unsigned char doom_emissive_pixel(int kind, const unsigned char *rgb,
                                               unsigned char coverage)
{
    if (!coverage || kind == DOOM_EMISSIVE_NONE) return 0;
    int r=rgb[0],g=rgb[1],b=rgb[2];
    int hi=r>g?r:g; if (b>hi) hi=b;
    int lo=r<g?r:g; if (b<lo) lo=b;
    int threshold=160;
    if (kind==DOOM_EMISSIVE_LAVA) {
        if (r<g+24 || r<b+40) return 0;
        threshold=80;
    } else if (kind==DOOM_EMISSIVE_MAGMA) {
        /* Red-to-yellow cracks glow; brown and tan rock around them stays shaded. */
        if (!(r>=100 && r>=g+60 && b*2<r) && !(r>=200 && g>=160 && b*2<g)) return 0;
        threshold=96;
    } else if (kind==DOOM_EMISSIVE_SLIME) {
        if (g<r+24 || g<b+24) return 0;
        threshold=64;
    } else if (kind==DOOM_EMISSIVE_GLASS) {
        /* Coloured panes glow in their own colour, dim ones less; the dark
           lead between them and the grey stone frame stay shaded. */
        if (hi-lo<32) return 0;
        int value=(hi-40)*200/160;
        return (unsigned char)(value<0?0:value>200?200:value);
    } else if (kind==DOOM_EMISSIVE_DIFFUSE) {
        /* Only the panels' pale faces, at half strength; the frame stays shaded. */
        if (hi<=165) return 0;
        int value=(hi-165)*128/26;
        return (unsigned char)(value>128?128:value);
    } else if (kind==DOOM_EMISSIVE_STEP) {
        /* The warm lamp glow, by its painted falloff; the grey lip, the dark
           brown frame and the separators stay shaded. */
        /* Dim: a pool brighter than the room, peaking at its centre only. */
        if (r<88 || r<b*2) return 0;
        int value=(r-80)*120/75;
        return (unsigned char)(value>120?120:value);
    } else if (kind==DOOM_EMISSIVE_KEY) {
        /* The whole coloured strip glows, blue, red and yellow alike, by its
           painted shading; the brown housing and the dark track stay shaded. */
        int blue=b*5>r*7 && b*5>g*7, red=r*5>g*9 && r*5>b*9, yellow=r>b*2 && g>b*2;
        if (!blue && !red && !yellow) return 0;
        int value=(hi-90)*255/90;
        return (unsigned char)(value<0?0:value>255?255:value);
    } else if (kind==DOOM_EMISSIVE_SWITCH) {
        /* Only a lit lamp: pure bright red, or clear green. The unlit lamp
           beside it, red brick, green marble and vines stay shaded (checked
           on every switch of Doom and Doom II). */
        int red=r>=170 && r>g*4 && r>b*4, green=g>=110 && g*2>r*3 && g*2>b*3;
        if (!red && !green) return 0;
        int value=(hi-100)*255/80;
        return (unsigned char)(value>255?255:value);
    } else if (kind==DOOM_EMISSIVE_PANEL) {
        if (hi-lo<40) return 0;
        if (!(g>r*1.4 && g>b*1.2) && !(b>r*1.4 && b>g*1.2) &&
            !(r>g*2 && r>b*2)) return 0;
        threshold=64;
    } else {
        /* Bright neutral strips and saturated colored bulbs. */
        if (hi-lo<48 && lo<170) return 0;
    }
    if (hi<=threshold) return 0;
    int value=(hi-threshold)*255/48;
    return (unsigned char)(value>255?255:value);
}
#endif
