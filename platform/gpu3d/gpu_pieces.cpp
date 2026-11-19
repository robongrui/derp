/* Piece timing: which part of the picture costs the GPU what. SDL_gpu has no
   timestamp queries, so the only portable clock is a fence: each piece of the
   frame goes in a command buffer of its own, submitted and waited for, and
   the wall time from its submission to its fence is its GPU time (and the
   submission's own overhead). Waiting for every piece takes away the overlap
   of CPU and GPU work, so the frame runs slower while this is on, and only
   then: outside it the frame keeps its one command buffer and nothing here
   runs. On for the perf test (I_Render3DProfilePieces) and F4's "Show FPS" at
   "Full, with GPU times" (settings.fps 4). */
#include "internal.h"

namespace gpu3d {
namespace {
bool requested=false;   // the perf test's switch
bool active=false;      // this frame is timed piece by piece
double ms[I_RENDER3D_PIECES];
uint64_t waited=0;      // ns spent waiting for pieces this frame, kept out of its CPU time
}

bool piecesBegin() {
    active=requested||(settings.accelerated&&settings.fps==4);
    profile.frame.pieced=0;
    if(active) {for(double &m:ms)m=0;waited=0;}
    return active;
}
bool piecesOn() {return active;}

void pieceDone(SDL_GPUCommandBuffer *&command,int piece) {
    if(!active)return;
    // Textures made while this piece was recorded go through the upload buffer, which must reach the GPU first.
    flushUploads();
    uint64_t submit=SDL_GetTicksNS();
    SDL_GPUFence *fence=SDL_SubmitGPUCommandBufferAndAcquireFence(command);
    if(!fence) I_Error((char*)"GPU rendering failed: %s",SDL_GetError());
    SDL_WaitForGPUFences(device,true,&fence,1);SDL_ReleaseGPUFence(device,fence);
    uint64_t done=SDL_GetTicksNS();
    ms[piece]+=(done-submit)/1e6;waited+=done-submit;
    command=SDL_AcquireGPUCommandBuffer(device);
    if(!command) I_Error((char*)"GPU command buffer: %s",SDL_GetError());
}

void piecesEnd(SDL_GPUCommandBuffer *command,uint64_t frameStart) {
    if(!active)return;
    // The buffer the last piece left: empty, as everything was in a piece of its own.
    flushUploads();
    if(command&&!SDL_SubmitGPUCommandBuffer(command)) I_Error((char*)"GPU rendering failed: %s",SDL_GetError());
    I_Render3DFrameProfile &frame=profile.frame;
    frame.pieced=1;
    double sum=0;
    for(int i=0;i<I_RENDER3D_PIECES;++i) {frame.pieceMs[i]=ms[i];if(i!=I_RENDER3D_PIECE_PRESENT)sum+=ms[i];}
    frame.gpuMs=sum;
    frame.cpuMs=std::max(0.0,((double)(SDL_GetTicksNS()-frameStart)-(double)waited)/1e6);
}

void piecePresented(double presentMs) {if(active)profile.frame.pieceMs[I_RENDER3D_PIECE_PRESENT]=presentMs;}
} // namespace gpu3d

void I_Render3DProfilePieces(int enabled) {gpu3d::requested=enabled!=0;}

const char *I_Render3DPieceName(int piece) {
    static const char *names[I_RENDER3D_PIECES]={"uploads","liquid mirror","glass probes","glass mirror","sky cache","mist depth",
        "depth prepass","opaque world","sky","masked depth","masked walls","sprites","shadow volumes","transparent","mist, sunbeams","weapon, HUD",
        "bloom","present"};
    return piece>=0&&piece<I_RENDER3D_PIECES?names[piece]:"?";
}
