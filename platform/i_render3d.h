/* Accelerated 3D renderer: gpu3d/ draws the scene from scene3d/ with
   SDL_gpu (Metal on macOS, Vulkan elsewhere). */
#ifndef DOOM_RENDER3D_H
#define DOOM_RENDER3D_H
#ifdef __cplusplus
extern "C" {
#endif
struct SDL_Window;
int I_Render3DInit(struct SDL_Window *window);
/* graphics.cfg's game rules (Authored, Phobos, the weather) set before
   D_DoomMain: a -warp level loads before the window and I_Render3DInit, and
   was set up without DERP's monsters (no guards, no cover plans). In the save
   folder, after the command line is set. */
void I_Render3DEarlySettings(void);
void I_Render3DShutdown(void);
void I_Render3DStartFrame(void);
void I_Render3DPresent(const unsigned *pixels, const unsigned *palette);
void I_Render3DLook(float delta);
/* A palette flash (damage, pickup, radiation suit) as a colour laid over the finished
   picture: r, g, b (0-1, gamma applied) and how much of it (0 none). */
void I_Render3DScreenTint(const float tint[4]);
/* The pickup flash by item (F4 Pickup flash): r, g, b (negative: the palette flash's own
   colour) and amount; shown instead of the palette's pickup gold while amount > 0. */
void I_Render3DPickupTint(const float tint[4]);
void I_Render3DSettings(void);
/* The in-game F4 panel (settings_menu.cpp), opened by I_Render3DSettings.
   It draws a 640x400 image that the renderer scales by whole pixels; clicks
   use that image's coordinates (button 0: hover, 1: left, 3: right).
   I_Render3DSettingsMouse takes window coordinates. */
#define DOOM_SETTINGS_WIDTH 640
#define DOOM_SETTINGS_HEIGHT 400
void I_Render3DSettingsMenu(void);
int I_Render3DSettingsOpen(void);
void I_Render3DSettingsKey(int scancode, int shift);
void I_Render3DSettingsClick(int x, int y, int button);
/* The Bindings page waits for a key, mouse button or pad button to bind
   (bind_keys.h's codes); Esc or the pad's Start cancels. */
int I_Render3DSettingsCapturing(void);
void I_Render3DSettingsBind(int code);
void I_Render3DSettingsDraw(unsigned *pixels);
/* A library book open (scene3d/book_view.cpp): its page drawn in the panel's
   image while F4's panel is closed. */
int I_Render3DPageOpen(void);
void I_Render3DPageDraw(unsigned *pixels);
void I_Render3DSettingsMouse(float x, float y, int button);
/* The loading screen, in the same 640x400 image and font: title, a bar
   fraction (0-1) full and the stage under it, on black. */
void I_Render3DLoadingDraw(unsigned *pixels, const char *title, const char *stage, float fraction);
void I_Render3DScreenshot(void);
void I_Render3DToggleFlashlight(void);
/* Aim the sun where the crosshair points, kept for this map and hour in the
   player's derpinfo.txt; back: that aim taken out again (scene3d/sun_aim.cpp). */
void I_Render3DSunAim(int back);
/* The view level again (a pad's Center view). */
void I_Render3DCenterView(void);
/* The key left of 1: steps F4's "Show FPS" on (off, window title, FPS, full) and saves it. */
void I_Render3DCycleStats(void);
int I_Render3DIsAccelerated(void);
/* Frame profiling for the performance smoke test (tests/perf_smoke.c). While
   enabled, frames present without vsync and each waits for the GPU so its
   time can be read; the view stays level. skip leaves out lights of the kinds
   1 << I_RENDER3D_LIGHT_* and the I_RENDER3D_SKIP_* passes, so their cost
   shows as the difference. */
enum {
    I_RENDER3D_LIGHT_FLASHLIGHT, I_RENDER3D_LIGHT_SHOT, I_RENDER3D_LIGHT_BARREL,
    I_RENDER3D_LIGHT_DECORATION, I_RENDER3D_LIGHT_PROJECTILE, I_RENDER3D_LIGHT_DOOR,
    I_RENDER3D_LIGHT_SURFACE, I_RENDER3D_LIGHT_FOG, I_RENDER3D_LIGHT_KINDS
};
#define I_RENDER3D_SKIP_LIGHTS 0xffu
#define I_RENDER3D_SKIP_REFLECTIONS (1u << 8)
#define I_RENDER3D_SKIP_BLOOM (1u << 9)
#define I_RENDER3D_SKIP_FOG (1u << 10)
#define I_RENDER3D_SKIP_MIST (1u << 11)
typedef struct {
    float x, y, z, radius;
    int kind;      /* I_RENDER3D_LIGHT_* */
    int thing;     /* The source thing's mobjtype_t, or -1. */
    int blockers;  /* Wall segments each pixel it reaches tests. */
    int triangles; /* World triangles within its radius. */
    float perPixel; /* Blockers a pixel tests on average, with slices. */
} I_Render3DLightProfile;
/* The pieces of a frame's GPU work in the order they run, each timed on its
   own while piece timing is on (I_Render3DProfilePieces, or F4's "Show FPS"
   at "Full, with GPU times"). */
enum {
    I_RENDER3D_PIECE_UPLOADS, I_RENDER3D_PIECE_LIQUID_MIRROR, I_RENDER3D_PIECE_GLASS_PROBES,
    I_RENDER3D_PIECE_GLASS_MIRROR, I_RENDER3D_PIECE_SKY_CACHE, I_RENDER3D_PIECE_MIST_DEPTH,
    I_RENDER3D_PIECE_PREPASS, I_RENDER3D_PIECE_OPAQUE, I_RENDER3D_PIECE_SKY, I_RENDER3D_PIECE_MASKED_DEPTH,
    I_RENDER3D_PIECE_MASKED, I_RENDER3D_PIECE_CUTOUTS,
    I_RENDER3D_PIECE_VOLUMES, I_RENDER3D_PIECE_TRANSPARENT, I_RENDER3D_PIECE_MIST, I_RENDER3D_PIECE_OVERLAYS,
    I_RENDER3D_PIECE_BLOOM, I_RENDER3D_PIECE_PRESENT, I_RENDER3D_PIECES
};
typedef struct {
    unsigned serial;  /* Counts profiled frames. */
    int world;        /* The frame drew the 3D view. */
    double cpuMs;     /* Building, encoding and submitting the frame. */
    double gpuMs;     /* From submission until the GPU finished. */
    double prepareMs; /* Scene build, including level-load bakes. */
    double geometryMs, lightsMs, spritesMs, masksMs;
    int lights, blockers, triangles, litTriangles, lightTriangles, reflection;
    I_Render3DLightProfile light[64];
    /* The frame was timed piece by piece; then pieceMs holds each piece's
       time from its submission until the GPU finished it (0: it had nothing
       to draw), gpuMs their sum without the present, and cpuMs the frame's
       time less those waits. */
    int pieced;
    double pieceMs[I_RENDER3D_PIECES];
} I_Render3DFrameProfile;
void I_Render3DProfile(int enabled, unsigned skip);
/* Piece timing: each piece of the frame's GPU work in a command buffer of its
   own, submitted and waited for, so the frame runs slower while it is on. On
   with F4's GPU times as well. */
void I_Render3DProfilePieces(int enabled);
/* A piece's short name ("opaque world"). */
const char *I_Render3DPieceName(int piece);
/* Feature knobs (scene3d/profile_knobs.cpp), for the perf test's -features:
   each switches one part of the world shader off in the frames drawn while it
   is set, by its feature flag or the camera uniform that gates it, and only
   while profiling is on. Knob 0 is none. Names in parentheses are aggregates,
   at the end; the last is every feature knob at once. A few knobs use the
   skip bits, which I_Render3DProfile overwrites: set the knob after it. */
int I_Render3DKnobCount(void);
const char *I_Render3DKnobName(int knob);
/* What it clears, in the camera's terms ("bake.z", "FEATURE_CAUSTICS"). */
const char *I_Render3DKnobClears(int knob);
void I_Render3DProfileKnob(int knob);
const I_Render3DFrameProfile *I_Render3DLastFrame(void);
/* Drops lights still fading in or out from the previous view. */
void I_Render3DProfileTeleported(void);
/* The next screenshot goes to path instead of Screenshots/. */
void I_Render3DScreenshotTo(const char *path);
#ifdef __cplusplus
}
#endif
#endif
