/* The flash painted on a monster's firing frames (lumpImage in
   scene3d/images.cpp). Doom draws those frames fullbright, the whole body
   with them; here the body stays lit by the room and only the flash keeps
   that brightness. Artists painted it in the palette's yellow and orange
   ramps, which skin, metal and brains stay clear of by being paler or
   duller: a blob of those colors is the flash, and it spreads a little into
   the white core and the orange fringe around it. Specks are dropped (lit
   rivets, gold in the armor). An arch-vile burning keeps most of its body:
   that one is painted as fire. */
#ifndef DOOM_SPRITE_FLASH_H
#define DOOM_SPRITE_FLASH_H
#include <algorithm>
#include <vector>

/* pixels: palette index and coverage pairs; palette: PLAYPAL RGB.
   Returns per pixel 255 on the flash, 0 elsewhere. */
inline std::vector<unsigned char> doom_sprite_flash(int w, int h, const unsigned char *pixels, const unsigned char *palette)
{
    const size_t n = (size_t)w * h;
    std::vector<unsigned char> flash(n, 0);
    auto color = [&](size_t i) { return palette + pixels[i * 2] * 3; };
    // Saturated yellow to orange, or a pale yellow only at full brightness (the revenant's flares).
    auto core = [&](size_t i) {
        const unsigned char *c = color(i);
        int r = c[0], g = c[1], b = c[2], hi = std::max({r, g, b}), lo = std::min({r, g, b});
        if (hi < 176 || hi == lo || r != hi || g < b) return false;
        float hue = 60.0f * (g - b) / (hi - lo), saturation = (float)(hi - lo) / hi;
        return hue >= 28 && (saturation >= 0.5f || (hi >= 235 && saturation >= 0.25f && hue >= 38));
    };
    for (size_t i = 0; i < n; ++i) flash[i] = pixels[i * 2 + 1] && core(i) ? 255 : 0;
    // Each blob of them, kept from four texels up.
    std::vector<char> seen(n, 0);
    std::vector<size_t> blob, stack;
    for (size_t start = 0; start < n; ++start) {
        if (!flash[start] || seen[start]) continue;
        blob.clear(); stack.assign(1, start); seen[start] = 1;
        while (!stack.empty()) {
            size_t i = stack.back(); stack.pop_back(); blob.push_back(i);
            int x = (int)(i % w), y = (int)(i / w);
            for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) {
                if (x + dx < 0 || y + dy < 0 || x + dx >= w || y + dy >= h) continue;
                size_t next = (size_t)(y + dy) * w + x + dx;
                if (flash[next] && !seen[next]) { seen[next] = 1; stack.push_back(next); }
            }
        }
        if (blob.size() < 4) for (size_t i : blob) flash[i] = 0;
    }
    // Two texels out into anything bright and warm or white.
    for (int pass = 0; pass < 2; ++pass) {
        std::vector<unsigned char> grown = flash;
        for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
            size_t i = (size_t)y * w + x;
            if (flash[i] || !pixels[i * 2 + 1]) continue;
            const unsigned char *c = color(i);
            if (c[0] < 150 || c[0] < c[1] || c[1] < c[2]) continue;
            for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx)
                if (x + dx >= 0 && y + dy >= 0 && x + dx < w && y + dy < h && flash[(size_t)(y + dy) * w + x + dx]) grown[i] = 255;
        }
        flash.swap(grown);
    }
    return flash;
}
#endif
