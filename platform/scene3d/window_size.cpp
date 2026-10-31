/* F4's "Window size": the window itself made exactly a standard size in
   pixels (1280x720, 1920x1080, ...), for recordings and screenshots at a
   known size. The picture inside it still draws at "Resolution": this moves
   the window's edges, never the render size. */
#include "internal.h"

namespace doom3d {
namespace {
int appliedSize=-1;   // settings.windowSize as last applied; -1 before the first
}

void applyWindowSize() {
    if(!gameWindow||settings.windowSize==appliedSize)return;
    bool first=appliedSize<0;
    appliedSize=settings.windowSize;
    // Render checks draw to a hidden window of the size they asked for, and
    // -width and -height on the command line win for that session.
    if(SDL_GetWindowFlags(gameWindow)&SDL_WINDOW_HIDDEN)return;
    if(first&&(M_CheckParm((char*)"-width")||M_CheckParm((char*)"-height")))return;
    if(!settings.windowSize) {SDL_SetWindowResizable(gameWindow,true);return;}
    // The sizes are 16:9 heights; SDL takes window coordinates, which a
    // Retina display counts at half the pixels.
    float density=SDL_GetWindowPixelDensity(gameWindow);if(density<=0)density=1;
    int w=(int)std::lround(settings.windowSize*16/9/density),h=(int)std::lround(settings.windowSize/density);
    // A fixed size is a windowed thing: picking one leaves fullscreen (F11
    // goes back), and the window stays that size until Free is picked.
    if(SDL_GetWindowFlags(gameWindow)&SDL_WINDOW_FULLSCREEN)SDL_SetWindowFullscreen(gameWindow,false);
    SDL_SetWindowResizable(gameWindow,false);
    SDL_SetWindowSize(gameWindow,w,h);
    SDL_SetWindowPosition(gameWindow,SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED);
}
} // namespace doom3d
