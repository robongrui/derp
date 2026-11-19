/* The launcher's text (launcher_font.c): SDL's own 8x8 pixel font, bundled
 * in SDL itself, drawn at whole multiples so it stays chunky and needs no
 * WAD. Every letter is 8 pixels wide at scale 1. */
#ifndef DERP_LAUNCHER_FONT_H
#define DERP_LAUNCHER_FONT_H
#include <SDL3/SDL.h>
#ifdef __cplusplus
extern "C" {
#endif

#define FONT_CELL 8

float Font_Width(const char *text, int scale);
/* Draws text with its top left at x, y; returns its width. */
float Font_Draw(SDL_Renderer *renderer, float x, float y, const char *text, int scale, SDL_Color color);
/* At most width wide: a path loses its start ("...\folder\file.wad"), other text its end. */
float Font_DrawFitted(SDL_Renderer *renderer, float x, float y, const char *text, int scale, SDL_Color color,
                      float width, int keep_end);
/* Wrapped at spaces to width, at most lines lines; returns the height used. */
float Font_DrawWrapped(SDL_Renderer *renderer, float x, float y, const char *text, int scale, SDL_Color color,
                       float width, int lines);

#ifdef __cplusplus
}
#endif
#endif
