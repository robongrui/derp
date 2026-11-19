#include "sky_profiles.h"
#include "../games/doom/sky_stock.h"
#include "../games/heretic/sky_stock.h"

namespace doom3d {
namespace {
struct Known { uint32_t art; SkyCloudProfile profile; };
// FNV-1a of the width, height and composed palette indices.
const Known known[]={
    // Doom's first: its painting stays whole, its own mist and low clouds stay
    // among the mountains, the volume over them.
    {0x78E77509u,{0.50f,22.0f,16.0f,0.022f,1.3f,0.26f,0.0f,0.07f,4.5f,true,0,0,nullptr,0,nullptr,false,0,0,0,0.85f,0,0.68f,0.55f}},
    // Doom's fourth sky: long orange banks, dark pockets, bright streaks.
    {0x750A7322u,{0.60f,10.0f,5.0f,0.020f,1.8f,0.22f,0.0f,0.07f,5.0f,true,0,0,nullptr,0.45f,nullptr,false,0,0,0,0,0.50f}},
    // Doom's second: slow crimson weather behind sunlit cliffs and a black range.
    {0xE1E78131u,{0.55f,14.0f,12.0f,0.022f,1.5f,0.26f,0.0f,0.07f,7.0f,true,0,0,nullptr,0.35f,nullptr,false,0.55f,0.3f,1.0f}},
    // Doom's third: pillars no edge finds, marked by hand, in a thick red sky.
    {0xE3D1E3EDu,{0.50f,20.0f,16.0f,0.022f,1.3f,0.30f,0.0f,0.07f,6.0f,true,0,0,nullptr,0.5f,doom_sky3_crest}},
    // Doom 2's last: its art is its sky, faces and all; thin dark smoke over it.
    // Its one long range has valleys at the usual horizon's row, so it takes a
    // lower horizon, or only the peaks stand up, as several small mountains.
    {0x596AF453u,{0.50f,3.0f,3.0f,0.022f,1.2f,0.26f,0.0f,0.07f,2.0f,false,0,0.86f,nullptr,0,nullptr,true}},
    // Heretic's second: a broad crimson sea over charcoal crags.
    {0x3BD23825u,{0.55f,16.0f,12.0f,0.022f,1.5f,0.26f,0.0f,0.07f,7.0f,true,0,0.86f,nullptr,0.3f,nullptr,true,0.7f,0.1f,0.8f}},
    {0x4258CE95u,{0.55f,16.0f,12.0f,0.022f,1.5f,0.26f,0.0f,0.07f,7.0f,true,0,0.86f,nullptr,0.3f,nullptr,true,0.7f,0.1f,0.8f}},
    // Doom 2's city: the clouds of its first sky, in its own smoke and fire.
    {0xF611BB68u,{0.50f,9.0f,9.0f,0.024f,1.3f,0.30f,0.0f,0.07f,6.0f,false,0,0,nullptr,0,nullptr,false,0,0,0,0.68f}},
    // Doom 2's first: heavy brown masses over dark openings, slow.
    {0x40CCE5C3u,{0.50f,9.0f,9.0f,0.024f,1.3f,0.30f,0.0f,0.07f,6.0f,true,0,0,nullptr,0.45f,nullptr,false,0,0,0,0,0.55f}},
    // Heretic's first: a charcoal storm. The 2025 art goes on below the DOS crop
    // to ochre hills, which want a lower horizon to stand above it.
    {0x1099D784u,{0.70f,12.0f,6.0f,0.022f,1.2f,0.28f,0.0f,0.07f,7.0f,true,0,0,nullptr,0.35f,nullptr,false,0,0,0,0,0.50f}},
    {0x83E1F090u,{0.75f,12.0f,6.0f,0.022f,1.2f,0.28f,0.0f,0.07f,7.0f,true,140,0.92f,heretic_sky1_ridge,0.4f,nullptr,true,0,0,0,0,0.50f}},
    // Heretic's third: thin cobalt banks round the spire. The 2025 art's
    // blue mountains sit under them.
    {0xD4C477EDu,{0.70f,40.0f,24.0f,0.022f,1.6f,0.24f,0.0f,0.07f,6.0f,true,0,0,nullptr,0.35f,nullptr,false,0,0,0,0,0.40f}},
    {0x724069A0u,{0.70f,40.0f,24.0f,0.022f,1.6f,0.24f,0.0f,0.07f,6.0f,true,108,0.80f,heretic_sky3_ridge,0.35f,nullptr,false,0,0,0,0,0.40f}},
    // Heretic's fourth (2025 only): black tendrils hanging from a black top
    // into a red sky, over grey peaks (crest marked: no edge finds them under
    // that top). Hell's art kept whole, as Doom 2's last, but its shapes hang
    // and curl from above (sky/hell.glsl).
    {0x990A4A0Cu,{0.50f,3.0f,3.0f,0.022f,1.2f,0.26f,0.0f,0.07f,1.0f,false,0,0.86f,nullptr,0,heretic_sky4_crest,true,0,0,0,0,0,0,0,3,0}},
    // Heretic's fifth (2025 only): a low grey overcast over a swamp, dead
    // trees hung with moss standing in the fog along its foot. The trees
    // stay where they were painted, kept crisp (sky_trees.cpp), Doom's first
    // valley mist drifting over their feet; the overcast is the volume's,
    // heavy and slow.
    {0xB73F0F8Eu,{0.85f,4.0f,2.0f,0.040f,2.5f,0.30f,0.0f,0.07f,5.0f,false,120,0.80f,nullptr,1.0f,nullptr,false,0,0,0,0,0.4f,0,0.60f,0,95}},
};
uint32_t artHash(const SkyImage &s) {
    uint32_t v=0x811C9DC5u;
    auto mix=[&](byte b){v=(v^b)*0x01000193u;};
    for(int n:{s.w,s.h})for(int i=0;i<4;++i)mix((byte)((uint32_t)n>>(8*i)));
    for(byte b:s.index)mix(b);
    return v;
}
} // namespace

const SkyCloudProfile *skyCloudProfile(const SkyImage &s) {
    uint32_t art=artHash(s);
    for(const Known &k:known)if(k.art==art)return &k.profile;
    return nullptr;
}
} // namespace doom3d
