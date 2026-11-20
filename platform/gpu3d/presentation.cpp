/* How frames reach the display (F4 Graphics "Frame presentation", "Frame
   limit"), worked out before each frame takes its swapchain image:
   - VSync: shown at the display's refresh, two frames in flight; a frame
     that runs over waits for the next refresh (60 halves to 30).
   - VSync, 3 frames: the same with a third frame in flight, so running
     over costs one refresh, not half the rate; a frame more of delay.
   - Fast: mailbox; drawn as fast as the GPU goes and the newest finished
     frame shown at each refresh, no tearing, no halving. Where the driver
     has no mailbox, VSync with 3 frames.
   - Adaptive: VSync while the frames keep up; while they keep running over
     the refresh, uncapped (a tear rather than a halved rate), back to VSync
     once they keep up again for a second.
   - Off: uncapped (immediate, else mailbox): tearing, lowest delay; with a
     G-Sync or FreeSync display and a limit just under its refresh, neither.
   The frame limit holds the uncapped modes (Fast, Adaptive while uncapped,
   Off) to a rate: the display's, or a set one. Profiling (the perf smoke
   test) always runs uncapped and unlimited. */
#include "internal.h"

namespace gpu3d {
namespace {
SDL_GPUPresentMode applied=SDL_GPU_PRESENTMODE_VSYNC;
int appliedFrames=2;
bool adaptiveUncapped=false;   // Adaptive: running uncapped since frames kept running over
int late=0,early=0,counted=0;  // Adaptive: frames over the refresh, frames well under it, of those counted
uint64_t lastFrame=0;          // when the last frame started (ns)
uint64_t nextFrame=0;          // the limit: when the next frame may start (ns), 0 none set
// The driver's answers, asked once per device, window and display. On Vulkan each question
// re-reads the surface's capabilities, formats and present modes: asked every frame (Fast,
// Adaptive uncapped, Off) it cost 2.2 ms a frame on an RTX 2080 and stalled 50-60 ms every
// 2-8 s, hitches with nothing else going on (2026-11-20).
bool supports(SDL_GPUPresentMode mode) {
    static SDL_GPUDevice *askedDevice=nullptr;
    static SDL_Window *askedWindow=nullptr;
    static SDL_DisplayID askedDisplay=0;
    static signed char known[3]={-1,-1,-1};   // per present mode: -1 not asked yet, else the answer
    SDL_DisplayID display=SDL_GetDisplayForWindow(gameWindow);
    if(device!=askedDevice||gameWindow!=askedWindow||display!=askedDisplay) {
        askedDevice=device;askedWindow=gameWindow;askedDisplay=display;
        std::fill(known,known+3,(signed char)-1);
    }
    int m=(int)mode;
    if(m<0||m>=3)return SDL_WindowSupportsGPUPresentMode(device,gameWindow,mode);
    if(known[m]<0)known[m]=SDL_WindowSupportsGPUPresentMode(device,gameWindow,mode)?1:0;
    return known[m]==1;
}
// The display's refresh period (ns), 60 Hz where it doesn't say.
uint64_t refreshPeriod() {
    const SDL_DisplayMode *mode=SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(gameWindow));
    float hz=mode&&mode->refresh_rate>1?mode->refresh_rate:60.0f;
    return (uint64_t)(1e9/hz);
}
// The limit (ns between frames, 0 none) for the chosen setting.
uint64_t limitPeriod() {
    switch(settings.frameLimit) {
    case 1: return refreshPeriod();
    case 2: return (uint64_t)(1e9/30);
    case 3: return (uint64_t)(1e9/60);
    case 4: return (uint64_t)(1e9/120);
    case 5: return (uint64_t)(1e9/144);
    case 6: return (uint64_t)(1e9/240);
    default: return 0;
    }
}
}

void presentationWait() {
    if(!device||!gameWindow)return;
    uint64_t now=SDL_GetTicksNS(),interval=lastFrame?now-lastFrame:0;
    // Adaptive: count frames over the refresh (vsync holds them to a whole
    // second refresh) and well under it (uncapped, they would keep up).
    if(settings.presentation==3&&!profile.enabled&&interval>0) {
        uint64_t period=refreshPeriod();
        if(!adaptiveUncapped) {late+=interval>period*3/2;++counted;}
        else {early+=interval<period*9/10;++counted;}
        if(!adaptiveUncapped&&counted>=30) {adaptiveUncapped=late>=6;late=counted=0;}
        else if(adaptiveUncapped&&counted>=60) {adaptiveUncapped=early<55;early=counted=0;}
    }
    else adaptiveUncapped=false;
    // The mode: the chosen one, or the next best the driver has.
    SDL_GPUPresentMode mode=SDL_GPU_PRESENTMODE_VSYNC;
    int frames=2;
    bool uncapped=profile.enabled||settings.presentation==4||(settings.presentation==3&&adaptiveUncapped);
    if(uncapped) mode=supports(SDL_GPU_PRESENTMODE_IMMEDIATE)?SDL_GPU_PRESENTMODE_IMMEDIATE:supports(SDL_GPU_PRESENTMODE_MAILBOX)?SDL_GPU_PRESENTMODE_MAILBOX:SDL_GPU_PRESENTMODE_VSYNC;
    else if(settings.presentation==2&&supports(SDL_GPU_PRESENTMODE_MAILBOX)) mode=SDL_GPU_PRESENTMODE_MAILBOX;
    if(mode==SDL_GPU_PRESENTMODE_VSYNC&&(settings.presentation==1||settings.presentation==2))frames=3;
    if(frames!=appliedFrames) {SDL_SetGPUAllowedFramesInFlight(device,(Uint32)frames);appliedFrames=frames;}
    // With the composition F4 "HDR" asks for (hdr_output.cpp).
    static SDL_GPUSwapchainComposition composed=SDL_GPU_SWAPCHAINCOMPOSITION_SDR;
    SDL_GPUSwapchainComposition composition=hdrWanted();
    if(mode!=applied||composition!=composed) {
        bool worked=SDL_SetGPUSwapchainParameters(device,gameWindow,composition,mode);
        if(worked)applied=mode;
        if(composition!=composed)hdrApplied(composition,worked);
        if(worked)composed=composition;
    }
    // The limit, for what is not held to the refresh already: frames start
    // on a fixed schedule one period apart, so the time this function spends
    // (the driver queries above) and the delay's own overshoot don't add to
    // every frame (they did: 60 ran at 55, 120 at 102). A frame a whole
    // period late, or a schedule left from a slower limit, starts it over.
    uint64_t limit=profile.enabled||applied==SDL_GPU_PRESENTMODE_VSYNC?0:limitPeriod();
    if(limit) {
        uint64_t t=SDL_GetTicksNS();
        if(!nextFrame||t>nextFrame+limit||nextFrame>t+limit)nextFrame=t;
        else if(t<nextFrame)SDL_DelayPrecise(nextFrame-t);
        nextFrame+=limit;
    }
    else nextFrame=0;
    lastFrame=SDL_GetTicksNS();
}
} // namespace gpu3d

// The mode in use, named by the choice it serves (scene3d.h): where the
// driver lacks the chosen one it shows the fallback.
const char *gpuPresentationName() {
    using namespace gpu3d;
    bool vsync=applied==SDL_GPU_PRESENTMODE_VSYNC,mailbox=applied==SDL_GPU_PRESENTMODE_MAILBOX;
    switch(settings.presentation) {
    case 0: return "VSYNC";
    case 1: return "VSYNC, 3 FRAMES";
    case 2: return mailbox?"FAST (MAILBOX)":"FAST: NO MAILBOX, VSYNC 3 FRAMES";
    case 3: return vsync?"ADAPTIVE, NOW VSYNC":mailbox?"ADAPTIVE, NOW UNCAPPED (MAILBOX)":"ADAPTIVE, NOW UNCAPPED";
    default: return vsync?"OFF: NO UNCAPPED MODE, VSYNC":mailbox?"OFF (MAILBOX)":"OFF (IMMEDIATE)";
    }
}
