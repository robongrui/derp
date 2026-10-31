/* The steady sounds of what the map's textures show: lamps buzzing, screens
 * and computers humming, nukage bubbling, lava and fire, water, wind, rain
 * on the windows. Made
 * up from nothing like the impact effects, for the WAD has none. Each kind
 * renders any length; a seed picks the variation, so two pools never
 * bubble in step. */
#ifndef DOOM_AUDIO_AMBIENT_SYNTH_H
#define DOOM_AUDIO_AMBIENT_SYNTH_H

enum {
    AMBIENT_LAMP,        /* LITE, TLITE, ceiling lights: a ballast's buzz */
    AMBIENT_CRT,         /* screens: mains hum, static, a whine where the rate holds it */
    AMBIENT_COMPUTER,    /* COMP walls: bleeps in little tunes */
    AMBIENT_TELEPORTER,  /* GATE flats: a low throb and a shimmer */
    AMBIENT_NUKAGE,      /* thick green bubbling, now and then a big blorp */
    AMBIENT_SLIMEFALL,   /* SFALL and the like: liquid pouring */
    AMBIENT_BLOOD,       /* slower and deeper than nukage */
    AMBIENT_WATER,       /* lapping, a drip now and then */
    AMBIENT_LAVA,        /* a low rumble, heavy bubbles bursting, a sizzle */
    AMBIENT_FIRE,        /* fire walls: a roar and crackling */
    AMBIENT_MAGMA,       /* cracked hot rock: faint ticks over a rumble */
    AMBIENT_WIND,        /* under the sky: gusts, a whistle */
    AMBIENT_RAIN,        /* under the sky while it rains: a hiss of drops, a patter, drips */
    AMBIENT_GLASS_RAIN,  /* window panes while it rains: drops tapping the glass, a trickle */
    AMBIENT_ROOM,        /* the room tone: faint dark air, when nothing else is heard */
    AMBIENT_KINDS
};

const char *Ambient_Name(int kind);
/* How loud the kind plays next to the others, 1 for most. */
float Ambient_Level(int kind);
/* Adds frames of the kind into out (mono, about -1..1). light is the
   sector's light per tic (35 a second), 0 its darkest and 1 its brightest,
   or NULL for steady; a lamp buzzes with it and cuts out when it drops. */
void Ambient_Render(int kind, unsigned seed, int rate, float *out, int frames,
                    const float *light, int tics);

/* Wind and rain (rain on the windows too) come in AMBIENT_STEPS strengths,
   0.1 to 1, a take each: a light air to a gale, a drizzle to a downpour.
   Ambient_Render makes the usual one (a breeze, a downpour). */
#define AMBIENT_STEPS 10
int Ambient_Stepped(int kind);
float Ambient_UsualStrength(int kind);
/* A take at a strength (0.1-1), as loud as the usual take whatever its
   strength, so all fill the 8 bits alike; how hard it blows or rains is
   heard in the take, and how loud in Ambient_Loudness. */
void Ambient_RenderStrength(int kind, float strength, unsigned seed, int rate, float *out, int frames);
/* How loud a stepped kind plays at a strength (0-1) against its usual:
   with the square root of it, fading out under the lightest take. */
float Ambient_Loudness(int kind, float strength);

/* The pieces, in machine_synth.c, liquid_synth.c and fire_synth.c. */
void Machine_Render(int kind, unsigned seed, int rate, float *out, int frames,
                    const float *light, int tics);
void Liquid_Render(int kind, unsigned seed, int rate, float *out, int frames);
/* strength only for wind and rain (Ambient_RenderStrength). */
void Fire_Render(int kind, unsigned seed, int rate, float *out, int frames, float strength);
#endif
