/* F4 Graphics "HDR" (PLAN-hdr.md): the picture stays as SDR draws it, paper white where the
   system puts SDR white; only light over white (the bloom past it, present.frag) rises into the
   display's headroom.
   - The swapchain: extended linear (scRGB, linear, 1.0 = 80 nits) where the display takes it
     (Windows with HDR on, macOS EDR, gamescope's WSI layer); else HDR10 (PQ, BT.2020) where only
     that is offered; else SDR, and the option greys out.
   - Paper white and headroom from SDL's window properties, which follow the system (Windows' SDR
     content brightness, the Mac's brightness slider moving its EDR headroom). Where the system
     says nothing (gamescope), the BT.2408 reference white of 203 nits and 4x over it.
   - F4 "HDR brightness" moves paper white; the peak stays where the display has it, so the
     headroom over paper white shrinks as it rises. "HDR highlights": how much of the light over
     white rises.
   Screenshots draw in SDR (frame.cpp's capture, hdrUniform with sdr). */
#include "internal.h"

namespace gpu3d {
namespace {
constexpr float referenceWhiteNits=203,fallbackHeadroom=4;
SDL_GPUSwapchainComposition applied=SDL_GPU_SWAPCHAINCOMPOSITION_SDR;
bool failed=false;           // the display said it takes HDR, then the swapchain would not have it
bool checked=false,linearOffered=false,pqOffered=false;
bool systemHdr=false;        // the window's HDR state as last seen; a change asks the display again
void check() {
    bool now=SDL_GetBooleanProperty(SDL_GetWindowProperties(gameWindow),SDL_PROP_WINDOW_HDR_ENABLED_BOOLEAN,false);
    if(checked&&now==systemHdr)return;
    checked=true;systemHdr=now;failed=false;
    linearOffered=SDL_WindowSupportsGPUSwapchainComposition(device,gameWindow,SDL_GPU_SWAPCHAINCOMPOSITION_HDR_EXTENDED_LINEAR);
    pqOffered=SDL_WindowSupportsGPUSwapchainComposition(device,gameWindow,SDL_GPU_SWAPCHAINCOMPOSITION_HDR10_ST2084);
    fprintf(stderr,"HDR: system %s; extended linear %s, HDR10 %s.\n",now?"on":"off",linearOffered?"offered":"not offered",pqOffered?"offered":"not offered");
}
// The Steam Deck's own screen (Steam sets SteamDeck=1; docked, the display is another size).
bool deckPanel() {
    const char *deck=SDL_getenv("SteamDeck");
    const SDL_DisplayMode *mode=SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(gameWindow));
    return deck&&!strcmp(deck,"1")&&mode&&std::max(mode->w,mode->h)==1280&&std::min(mode->w,mode->h)==800;
}
// SDR white (scRGB: 1.0 = 80 nits) and the headroom over it. Windows reports both, the Mac's EDR
// has white at 1.0 and reports the headroom. SDL 3.4 says nothing under gamescope: there the
// Deck OLED's own screen shows SDR white at a third of its 1000-nit peak (Steam raises the
// backlight for HDR; SDL's own later gamescope code uses the same rule), any other display
// BT.2408's 203 nits under a 1000-nit peak.
void systemLevels(float &white,float &headroom) {
    SDL_PropertiesID window=SDL_GetWindowProperties(gameWindow);
    white=SDL_GetFloatProperty(window,SDL_PROP_WINDOW_SDR_WHITE_LEVEL_FLOAT,1.0f);
    headroom=SDL_GetFloatProperty(window,SDL_PROP_WINDOW_HDR_HEADROOM_FLOAT,1.0f);
#ifndef __APPLE__
    if(white<=1.0f) {
        bool deck=deckPanel();
        white=(deck?1000.0f/3:referenceWhiteNits)/80.0f;
        if(headroom<=1.0f)headroom=deck?3.0f:1000.0f/referenceWhiteNits;
    }
#endif
    if(headroom<=1.0f)headroom=fallbackHeadroom;
}
}

SDL_GPUSwapchainComposition hdrWanted() {
    if(!device||!gameWindow||!settings.hdr)return SDL_GPU_SWAPCHAINCOMPOSITION_SDR;
    check();
    if(failed)return SDL_GPU_SWAPCHAINCOMPOSITION_SDR;
#if defined(__linux__)
    // gamescope: HDR10 first, half the bytes of a float swapchain for the same picture.
    if(pqOffered)return SDL_GPU_SWAPCHAINCOMPOSITION_HDR10_ST2084;
#endif
    if(linearOffered)return SDL_GPU_SWAPCHAINCOMPOSITION_HDR_EXTENDED_LINEAR;
    if(pqOffered)return SDL_GPU_SWAPCHAINCOMPOSITION_HDR10_ST2084;
    return SDL_GPU_SWAPCHAINCOMPOSITION_SDR;
}
void hdrApplied(SDL_GPUSwapchainComposition composition,bool worked) {
    if(!worked) {
        if(composition!=SDL_GPU_SWAPCHAINCOMPOSITION_SDR) {
            failed=true;
            fprintf(stderr,"HDR: the swapchain would not take it (%s); staying in SDR.\n",SDL_GetError());
        }
        return;
    }
    applied=composition;
    SDL_GPUTextureFormat format=SDL_GetGPUSwapchainTextureFormat(device,gameWindow);
    if(format==swapchainFormat)return;
    SDL_WaitForGPUIdle(device);
    swapchainFormat=format;
    if(!createOutputPipelines())I_Error((char*)"Could not create the GPU output pipelines for the new swapchain");
}
bool hdrSceneWanted() {
#ifdef DOOM_DEVTOOLS
    // DERP_HDRSCENE=1: the float view on an SDR display too (shown clipped), to check it draws alike.
    static const bool forced=getenv("DERP_HDRSCENE")!=nullptr;
    if(forced)return true;
#endif
    return settings.hdr&&settings.hdrScene&&applied!=SDL_GPU_SWAPCHAINCOMPOSITION_SDR;
}
void hdrUniform(float hdr[4],bool sdr) {
    hdr[0]=hdr[1]=hdr[2]=hdr[3]=0;
    if(sdr||applied==SDL_GPU_SWAPCHAINCOMPOSITION_SDR||!gameWindow)return;
    float white,headroom;systemLevels(white,headroom);
    float brightness=std::clamp(settings.hdrBrightness,0.25f,4.0f);
    hdr[2]=std::max(headroom/brightness,1.0f);
    hdr[3]=std::max(settings.hdrHighlights,0.0f);
    if(applied==SDL_GPU_SWAPCHAINCOMPOSITION_HDR_EXTENDED_LINEAR) {hdr[0]=1;hdr[1]=white*brightness;}
    else {hdr[0]=2;hdr[1]=white*80.0f*brightness;}
    // F4 "HDR colours": vivid (2) as the Deck OLED shows SDR, stretched to its wide panel; Auto (0)
    // vivid there, accurate elsewhere, where the system shows SDR accurately too.
    if(settings.hdrColors==2||(settings.hdrColors==0&&deckPanel()))hdr[0]+=2;
}
} // namespace gpu3d

using namespace gpu3d;
bool gpuHdrOffered() {
    if(!device||!gameWindow)return false;
    check();
    return (linearOffered||pqOffered)&&!failed;
}
const char *gpuHdrName() {
    static char name[64];
    if(applied==SDL_GPU_SWAPCHAINCOMPOSITION_SDR)return settings.hdr?"HDR NOT OFFERED, SDR":"";
    float hdr[4];hdrUniform(hdr);
    bool vivid=hdr[0]>2.5f;
    if(vivid)hdr[0]-=2;
    if(hdr[0]==1)snprintf(name,sizeof name,"HDR SCRGB%s, WHITE %.0f NITS, %.1fX",vivid?" VIVID":"",hdr[1]*80,hdr[2]);
    else snprintf(name,sizeof name,"HDR10%s, WHITE %.0f NITS, %.1fX",vivid?" VIVID":"",hdr[1],hdr[2]);
    return name;
}
