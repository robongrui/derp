/* Text in SDL's built-in pixel font (launcher_font.h). */
#include <string.h>
#include "launcher_font.h"

float Font_Width(const char *text, int scale)
{
    return (float)(SDL_utf8strlen(text) * FONT_CELL * scale);
}

float Font_Draw(SDL_Renderer *renderer, float x, float y, const char *text, int scale, SDL_Color color)
{
    float sx, sy;
    if (!text || !*text) return 0;
    /* The clip rectangle is scaled with the drawing: give it in the text's scale. */
    SDL_Rect clip;
    int clipped = SDL_RenderClipEnabled(renderer) && SDL_GetRenderClipRect(renderer, &clip);
    SDL_GetRenderScale(renderer, &sx, &sy);
    SDL_SetRenderScale(renderer, (float)scale, (float)scale);
    if (clipped)
    {
        SDL_Rect scaled = { clip.x / scale, clip.y / scale, (clip.x + clip.w) / scale - clip.x / scale,
                            (clip.y + clip.h) / scale - clip.y / scale };
        SDL_SetRenderClipRect(renderer, &scaled);
    }
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    SDL_RenderDebugText(renderer, SDL_floorf(x / scale), SDL_floorf(y / scale), text);
    SDL_SetRenderScale(renderer, sx, sy);
    if (clipped) SDL_SetRenderClipRect(renderer, &clip);
    return Font_Width(text, scale);
}

float Font_DrawFitted(SDL_Renderer *renderer, float x, float y, const char *text, int scale, SDL_Color color,
                      float width, int keep_end)
{
    int fits = (int)(width / (FONT_CELL * scale));
    int length = (int)strlen(text);
    if (length <= fits) return Font_Draw(renderer, x, y, text, scale, color);
    if (fits < 4) return 0;
    char line[512];
    if (fits > (int)sizeof(line) - 1) fits = (int)sizeof(line) - 1;
    if (keep_end)
    {
        memcpy(line, "...", 3);
        memcpy(line + 3, text + length - (fits - 3), (size_t)(fits - 3));
    }
    else
    {
        memcpy(line, text, (size_t)(fits - 3));
        memcpy(line + fits - 3, "...", 3);
    }
    line[fits] = 0;
    return Font_Draw(renderer, x, y, line, scale, color);
}

float Font_DrawWrapped(SDL_Renderer *renderer, float x, float y, const char *text, int scale, SDL_Color color,
                       float width, int lines)
{
    int per_line = (int)(width / (FONT_CELL * scale));
    float line_height = (float)(FONT_CELL * scale + scale * 3);
    char line[512];
    int drawn = 0;
    const char *p = text;
    if (per_line < 8 || per_line > (int)sizeof(line) - 1) return 0;
    while (*p && drawn < lines)
    {
        while (*p == ' ') ++p;
        int length = (int)strlen(p), cut = length;
        if (length > per_line)
        {
            cut = per_line;
            while (cut > 0 && p[cut] != ' ') --cut;
            if (!cut) cut = per_line;
        }
        if (drawn == lines - 1 && length > per_line)
        {
            /* The last line that fits ends the text. */
            cut = per_line - 3;
            memcpy(line, p, (size_t)cut);
            memcpy(line + cut, "...", 4);
        }
        else
        {
            memcpy(line, p, (size_t)cut);
            line[cut] = 0;
        }
        Font_Draw(renderer, x, y + drawn * line_height, line, scale, color);
        ++drawn;
        p += cut;
    }
    return drawn * line_height;
}
