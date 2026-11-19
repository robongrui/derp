/* Blood as one material on the CPU: the thrown particles take the rule the
   shader gives decals and blood sprites (shaders/world/blood_material.glsl),
   from the same numbers (shaders/blood_numbers.h). */
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include "../shaders/blood_numbers.h"

namespace doom3d {
// The light on blood keeps only part of its hue: the channels it lacks rise toward its brightness, never past
// it. By a green lamp blood stays a darker red; bright blue light doesn't light it like white.
inline std::array<float,3> bloodLight(const std::array<float,3> &light) {
    float brightness=0.299f*light[0]+0.587f*light[1]+0.114f*light[2];
    std::array<float,3> out;
    for(int c=0;c<3;++c)out[c]=std::max(light[c],brightness+((float)BLOOD_HUE_KEPT)*(light[c]-brightness));
    return out;
}
// How bright the light on blood gets: rolled off above BLOOD_LIGHT_KNEE toward BLOOD_LIGHT_TOP, so a bright room
// no longer clips it neon and a pool's shades stay apart.
inline float bloodLightCap(float level) {
    const float knee=(float)BLOOD_LIGHT_KNEE,span=(float)(BLOOD_LIGHT_TOP-BLOOD_LIGHT_KNEE);
    return level<=knee?level:knee+span*(1-std::exp(-(level-knee)/span));
}
}
