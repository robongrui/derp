/* What the legs left by a cut through the waist don't keep (Gore's halves,
   wounds.cpp cutMask), authored by hand for the monsters of DOOM and DOOM II
   as sprite_eyes_stock.h does the eyes: per death drawing, rectangles of
   texels under the cut that belong to the upper half (the zombieman's hands
   hanging down beside his hips, still there in his corpse; the chaingunner's
   chaingun; the imp's claws). An entry holds only for the original artwork:
   the lump's size and a hash of its pixels must match (FNV-1a over the
   decoded palette index and coverage pairs, transparent pixels as two
   zeros), so a PWAD's replacement keeps it all. */
#ifndef DOOM_GORE_CUTS_STOCK_H
#define DOOM_GORE_CUTS_STOCK_H
#include <cstddef>
#include <vector>

// Rectangles as x0,y0,x1,y1, inclusive, in the drawing as authored (unmirrored); {-1} ends them.
struct doom_stock_cut_entry { const char *name; int width, height; unsigned hash; int drops[4 * 3 + 1]; };
static const doom_stock_cut_entry doom_stock_cut_table[] = {
    {"POSSH0",39,55,0x2c7b8171u,{6,20,14,26, 32,22,38,29, -1}},   // both hands at the hips
    {"POSSI0",35,46,0x62154af2u,{25,16,34,28, -1}},               // the right hand, thrown up and out
    {"POSSJ0",43,34,0x626a47e4u,{30,14,42,33, -1}},               // the right hand and its drips
    {"POSSK0",48,27,0xbcd66239u,{40,11,47,21, -1}},               // the right hand, landed
    {"POSSL0",47,17,0x2cc65c9fu,{37,5,46,16, -1}},                // the right forearm and hand, lying
    // The shotgun guy: as the zombieman (the shotgun across his thighs stays, as the rifle).
    {"SPOSH0",33,60,0x60c0a2f4u,{0,25,7,31, 27,22,32,27, -1}},
    {"SPOSI0",35,50,0xed3d3c98u,{25,20,34,32, 1,19,6,21, -1}},
    {"SPOSJ0",42,35,0x48281e02u,{30,14,41,33, -1}},
    {"SPOSK0",48,27,0xe4c525e6u,{40,11,47,21, -1}},
    {"SPOSL0",52,17,0x3a8a1e5fu,{38,5,51,16, -1}},
    // The chaingunner: his left forearm with the chaingun, his right forearm (the belt stays on his hips).
    {"CPOSH0",43,61,0x62744442u,{0,28,17,42, 29,28,42,36, -1}},
    {"CPOSI0",48,64,0xa4c9a398u,{3,30,12,51, 33,30,47,45, -1}},
    {"CPOSJ0",59,59,0xc5361f44u,{9,28,22,51, 42,28,58,40, -1}},
    {"CPOSK0",65,49,0xb8f82ce7u,{9,22,25,45, 49,22,64,30, -1}},
    {"CPOSL0",64,37,0x0e606451u,{0,16,23,33, 48,16,63,27, -1}},
    {"CPOSM0",65,25,0xea173f81u,{0,9,26,21, 47,9,64,19, -1}},
    {"CPOSN0",65,21,0x3be814a8u,{0,5,26,17, 47,5,64,15, -1}},
    // The SS: his bare hands (the gloved ones don't show; the gun at his hip stays).
    {"SSWVI0",29,54,0x5d7bd949u,{0,23,6,30, 19,23,25,27, -1}},
    {"SSWVJ0",33,46,0xd84a9142u,{0,19,6,22, -1}},
    {"SSWVK0",40,42,0x0b86e97bu,{0,21,6,28, 33,18,39,24, -1}},
    {"SSWVM0",51,16,0x3c9c3167u,{6,4,11,9, 40,5,44,7, -1}},
    // The imp: its claws and forearms hanging beside its hips (its feet's claws stay).
    {"TROOI0",42,62,0x6e8b2345u,{0,26,10,36, 34,26,41,36, -1}},
    {"TROOJ0",41,59,0x7d231cceu,{0,26,12,35, 32,26,37,30, -1}},
    {"TROOK0",40,54,0xed566381u,{8,26,15,29, 33,32,35,35, -1}},
    {"TROOL0",48,46,0x0d48c282u,{39,22,47,29, -1}},
    {"TROOM0",58,22,0x6cdd7d63u,{12,9,16,11, 51,10,57,16, -1}},
    // The Baron and the Hell Knight, one drawing in two colours: the hands.
    {"BOSSI0",48,73,0x3bede229u,{0,31,8,37, 36,31,46,50, -1}},
    {"BOSSJ0",55,64,0x7ad04a2cu,{0,27,10,33, 43,27,54,33, -1}},
    {"BOSSK0",56,54,0xf9f71a2au,{0,22,10,28, 42,22,51,28, -1}},
    {"BOSSL0",59,36,0x366f7ac3u,{0,13,9,21, 46,13,54,21, 9,13,46,19, -1}},
    {"BOSSM0",60,30,0x516f8dfbu,{0,11,14,27, 49,11,59,23, 15,11,33,30, -1}},
    {"BOSSN0",60,31,0xb7cc0a0du,{0,11,14,27, 49,11,59,23, 15,11,33,30, -1}},
    {"BOSSO0",60,31,0x4e8ae952u,{0,11,14,27, 49,11,59,23, 15,11,33,30, -1}},
    {"BOS2I0",48,73,0xa43b3771u,{0,31,8,37, 36,31,46,50, -1}},
    {"BOS2J0",55,64,0x2b190346u,{0,27,10,33, 43,27,54,33, -1}},
    {"BOS2K0",56,54,0xdbc588efu,{0,22,10,28, 42,22,51,28, -1}},
    {"BOS2L0",59,36,0x4680d295u,{0,13,9,21, 46,13,54,21, 9,13,46,19, -1}},
    {"BOS2M0",60,30,0xe0fc2a95u,{0,11,14,27, 49,11,59,23, 15,11,33,30, -1}},
    {"BOS2N0",60,31,0x9c88fa84u,{0,11,14,27, 49,11,59,23, 15,11,33,30, -1}},
    {"BOS2O0",60,31,0x93f6203cu,{0,11,14,27, 49,11,59,23, 15,11,33,30, -1}},
    // The Revenant: the arm hanging down, and both arms lying.
    {"SKELL1",66,72,0xd8529c21u,{0,33,19,54, -1}},
    {"SKELP0",62,38,0x9a1111b3u,{0,14,17,23, 40,14,61,27, -1}},
    // The Arch-vile: a hand, then all of the torso: he curls up as he falls, and by height it came back
    // under a top already gone.
    {"VILER0",38,80,0x137dd237u,{30,35,37,43, -1}},
    {"VILES0",47,76,0x63228ddau,{33,34,46,48, -1}},
    {"VILET0",55,70,0x8c877ca3u,{0,24,54,38, 18,38,54,54, -1}},
    {"VILEU0",67,58,0x513003f0u,{24,18,66,48, -1}},
    {"VILEV0",73,47,0x8041335fu,{24,14,72,37, 18,8,26,24, -1}},
    {"VILEW0",74,37,0xb79a9d47u,{25,10,73,28, 16,8,25,17, -1}},
    {"VILEX0",74,25,0x1807d9a3u,{24,6,73,16, -1}},
    {"VILEY0",74,22,0x1c43a83cu,{25,4,73,13, -1}},
    {"VILEZ0",74,22,0xd4761056u,{25,4,73,13, -1}},
    // The cyberdemon dies an explosion: no legs to leave (goreHefty leaves him out).
};

/* When pixels (w*h index and coverage pairs) are stock artwork listed here,
   sets drop (w*h) to 1 where a texel isn't the legs' and returns true. */
inline bool doom_stock_cut_drops(int w, int h, const unsigned char *pixels, std::vector<unsigned char> &drop)
{
    for (const doom_stock_cut_entry &e : doom_stock_cut_table) {
        if (e.width != w || e.height != h) continue;
        unsigned hash = 2166136261u;
        for (size_t i = 0; i < (size_t)w * h * 2; ++i) hash = (hash ^ pixels[i]) * 16777619u;
        if (hash != e.hash) continue;
        drop.assign((size_t)w * h, 0);
        for (const int *r = e.drops; r[0] >= 0; r += 4)
            for (int y = r[1]; y <= r[3] && y < h; ++y)
                for (int x = r[0]; x <= r[2] && x < w; ++x) drop[(size_t)y * w + x] = 1;
        return true;
    }
    return false;
}
#endif
