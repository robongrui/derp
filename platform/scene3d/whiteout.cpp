/* The whiteout close by (c.climate.z, whiteout.glsl): how much of the near
   view the weather in the air takes, for every kind that fills it, by how
   much of it there is and how hard it blows. The shapes follow what is
   measured of real visibility, the distances are DOOM's (a quarter mile of
   heavy snow is some 13000 units, past most maps): falling snow thickens
   the air in still air already and blowing snow only past a breeze, then
   steeply (visibility falls as the wind's fifth power); sand needs the wind
   to lift it at all, past about the same knee; ash is mostly its fall, the
   wind lifting a little more; a fog thins in the wind. */
#include "internal.h"

namespace doom3d {
namespace {
// At full amount, the whiteout for each wind setting: still, breeze,
// strong, gale (windAmount 0, 0.5, 0.75, 1), straight between them.
struct WhiteoutRule { Weather kind; float at[4]; };
const WhiteoutRule rules[]={
    {WEATHER_SNOW,   {0.3f,0.35f,0.65f,1.0f}},
    {WEATHER_SAND,   {0.0f,0.1f,0.6f,1.0f}},
    {WEATHER_ASH,    {0.25f,0.3f,0.45f,0.6f}},
    {WEATHER_DRY_ICE,{0.15f,0.1f,0.05f,0.03f}},
};
}
float whiteoutNow() {
    if(!settings.weather)return 0;
    Weather kind=falling();
    for(const WhiteoutRule &rule:rules) {
        if(rule.kind!=kind)continue;
        float wind=windAmount(),value;
        if(wind<=0.5f)value=rule.at[0]+(rule.at[1]-rule.at[0])*wind/0.5f;
        else if(wind<=0.75f)value=rule.at[1]+(rule.at[2]-rule.at[1])*(wind-0.5f)/0.25f;
        else value=rule.at[2]+(rule.at[3]-rule.at[2])*(wind-0.75f)/0.25f;
        return value*std::clamp(fallNow,0.0f,1.0f);
    }
    return 0;
}
} // namespace doom3d
