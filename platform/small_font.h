/* The game's small font ('!' to '_', Game_FontLump: DOOM's STCFN, Heretic's
 * FONTA), uppercase as in DOOM's messages, for the F4 panel and the FPS
 * readout. Glyphs are brightness masks, so the artwork's shading survives
 * any text color: shade 0 is transparent, otherwise the palette color's
 * brightness relative to the font's brightest pixel. */
#ifndef DOOM_SMALL_FONT_H
#define DOOM_SMALL_FONT_H
#include <string>

namespace smallFont {
// Reads the font from the WAD the first time it is called.
void load();
// The tallest letter's height in pixels, and a string's width.
int letterHeight();
int width(const std::string &text);
// Draws text into an image of width x height 32-bit pixels, from pixel x with
// the letters' tops at top; color is scaled per byte by each pixel's shade,
// so any byte order works. Returns where the text ends.
int draw(unsigned *image,int width,int height,const std::string &text,int x,int top,unsigned color);
}
#endif
