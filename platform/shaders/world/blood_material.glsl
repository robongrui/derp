// Blood as one material (PLAN-blood-material.md): decals (MODE_BLOOD), airborne
// drops and wet wounds (MODE_WET_SPRITE); the thrown particles take the same
// rule on the CPU (scene3d/blood_material.h). Part of world.frag.
// - Its colour is the blood ramp (bloodPalette), thick dark and thin light. It
//   never ages: fresh blood stands out by its gloss (blood.cpp bloodAge).
// - The light on it keeps only part of its hue (bloodLight), and its
//   strongest channel rolls off (bloodLightCap).
// - While wet a clear coat lies on it: highlights in the light's own colour,
//   stronger where the view skims it. No sheen of the room's light at
//   grazing angles: over a floor of blood it read as a pink band across the
//   far half of the room (the user).
// No floor in the dark: there it is black liquid, shown by what light its
// coat catches.
#include "../blood_numbers.h"
vec3 bloodLight(vec3 light) {
    float brightness=dot(light,vec3(0.299,0.587,0.114));
    light=max(light,mix(light,vec3(brightness),1.0-BLOOD_HUE_KEPT));
    float top=max(light.r,max(light.g,light.b));
    if(top<=BLOOD_LIGHT_KNEE) return light;
    const float span=BLOOD_LIGHT_TOP-BLOOD_LIGHT_KNEE;
    return light*(BLOOD_LIGHT_KNEE+span*(1.0-exp(-(top-BLOOD_LIGHT_KNEE)/span)))/top;
}
// A highlight's colour from a light: the light's own, a little of the blood's hue kept.
vec3 bloodCoat(vec3 light,vec3 albedo) {
    float top=max(albedo.r,max(albedo.g,albedo.b));
    return light*mix(vec3(1),albedo/max(top,1e-3),BLOOD_COAT_HUE);
}
// A decal's vTint.r: its wetness (0-1, quarter steps) plus twice what it lies
// on (scene3d/blood_art.cpp bloodUnder): 0 not metal, 1 metal, 2 brushed
// metal along u (a wall's length, a flat's x), 3 along v (up a wall, a flat's y).
uint bloodUnder(float packed) {return uint(floor(packed*0.5+0.001));}
float bloodWetness(float packed) {return max(packed-2.0*float(bloodUnder(packed)),0.0);}
// The film's highlight colour: mostly the light's own, off its top; the rest
// went through the blood to the plate and back, the blood's hue twice over.
vec3 bloodFilmHue(vec3 albedo) {
    vec3 hue=albedo/max(max(albedo.r,albedo.g),max(albedo.b,0.05));
    return mix(vec3(1),hue*hue,BLOOD_FILM_RED);
}
// How much of the plate's gloss shows through: all through thin blood, a third through a clot.
float bloodFilm(vec3 albedo) {
    float top=max(albedo.r,max(albedo.g,albedo.b));
    return mix(BLOOD_FILM_CLOT,1.0,saturate((top-BLOOD_FILM_THICK)/(BLOOD_FILM_THIN-BLOOD_FILM_THICK)));
}
// The coat by the view: face-on a little of it shows (facing), skimming all of it.
float bloodFresnel(vec3 normal,vec3 view,float facing) {
    return mix(facing,1.0,pow(1.0-saturate(abs(dot(normal,view))),5.0));
}
