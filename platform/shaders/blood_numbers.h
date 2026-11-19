// Blood's material numbers (PLAN-blood-material.md), shared by both sides
// like vertex_modes.h: world/blood_material.glsl and scene3d/blood_material.h.
#ifndef DOOM_BLOOD_NUMBERS_H
#define DOOM_BLOOD_NUMBERS_H

// How much of a coloured light's own hue blood keeps: the channels the light
// lacks rise the rest of the way toward its brightness (luminance), never past
// it, so an eye adapted to a lamp still sees red. Toward its strongest channel
// instead, bright blue panels lit blood like white light: it glowed.
#define BLOOD_HUE_KEPT 0.4
// The light on blood rolls off above BLOOD_LIGHT_KNEE toward BLOOD_LIGHT_TOP
// (its strongest channel, the hue kept), so a bright room no longer clips it
// to a neon red. On the light, not the colour: capping the colour pressed
// every shade of a pool into one flat red.
#define BLOOD_LIGHT_KNEE 0.85
#define BLOOD_LIGHT_TOP 1.3
// The wet coat's highlight face-on, against 1 where the view skims it
// (Schlick's curve, lifted: at Doom's light levels a true 0.025 never shows).
// Flying blood and wounds are round, their glints small anyway; a flat splat
// shows less face-on, or a lamp's highlight laid a pink wash over it.
#define BLOOD_COAT_FACING 0.35
#define BLOOD_DECAL_FACING 0.15
// A splat's highlight exponent: tight, a glint rather than a patch (its flat
// normal spreads a broad one across the whole splat).
#define BLOOD_DECAL_SHARP 160.0
// How much of the blood's own hue a highlight keeps over the light's colour.
#define BLOOD_COAT_HUE 0.2
// Blood on metal is a glossy film over the plate (bloodUnder), not paint: a
// sharp highlight over a deeper red. Its gloss is twice bare metal's (0.21)
// and tighter, all of it on thin blood and BLOOD_FILM_CLOT of it on a clot.
// The highlight is mostly the light's own colour, BLOOD_FILM_RED of it the
// blood's hue twice over (through the film and back): pure red on red blood
// read as more light on it, not as shine (the user saw nothing). The blood's
// own light there is BLOOD_FILM_BODY of it elsewhere. How thick, by the
// blood's brightest channel: the ramp runs from 0.20 (thickest) to 0.59; the
// merged layer's stains from about 0.31 to 0.44.
#define BLOOD_FILM_GLOSS 0.45
#define BLOOD_FILM_SHARP 48.0
#define BLOOD_FILM_RED 0.45
#define BLOOD_FILM_BODY 0.7
#define BLOOD_FILM_CLOT 0.35
#define BLOOD_FILM_THICK 0.24
#define BLOOD_FILM_THIN 0.46

#endif
