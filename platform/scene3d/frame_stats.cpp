/* The numbers of F4's "Show FPS" readout (stats_overlay.cpp draws them):
   - every frame, the backend's four moments (statsFrame): when it started
     waiting for the display, got its image, had the scene built and
     submitted it. From them the frame rate, the frame times for the graph,
     and where a frame's time went: the game's tics (less DERP's own work in
     them), the scene, encoding and submitting, the wait.
   - with the full readout only (statsTiming), each system's CPU time, tic
     work and frame work alike (timed, statsTicWork); otherwise the timers
     don't read the clock.
   - with the GPU times (settings.fps 4), each piece of the frame's GPU work
     as gpu3d/gpu_pieces.cpp timed it, mean and worst like the systems.
   - once a second, the CPU load: this process's, and the whole system's
     where the system says (cpu_time.h).
   The readout's view is gathered four times a second, so numbers stay readable. */
#include "internal.h"
#include "../cpu_time.h"
extern "C" {
#include "../audio/sound_options.h"
}

namespace doom3d {
bool statsTiming=false;
bool statsTracing=false;
namespace {
struct Slot { double frame=0,total=0,worst=0; int items=-1; };
std::array<Slot,(size_t)Stat::Count> slots;
StatsView view;
std::array<float,statsHistory> recent{};   // frame times (ms), a ring
size_t newest=0;
uint64_t lastWait=0,lastSubmitted=0,windowStart=0,loadStart=0;
double ticWork=0;   // DERP's work in the game's tics since the last frame was submitted (ns)
// Over the current window: frames, the sums of each part (ms), the worst frame.
int frames=0;
double sumInterval=0,worstInterval=0,sumGame=0,sumEffects=0,sumScene=0,sumSubmit=0,sumWait=0;
double processStart=0,busyStart=0,totalStart=0;
// The GPU pieces over the window, from the frames timed piece by piece (profile.frame.pieced):
// the sums and worst of each and of their total, and how many such frames.
std::array<double,I_RENDER3D_PIECES> pieceTotal{},pieceWorst{};
double piecesTotal=0,piecesWorst=0;
int piecedFrames=0;
void clearPieces() {pieceTotal.fill(0);pieceWorst.fill(0);piecesTotal=piecesWorst=0;piecedFrames=0;}

// Keep the last stretch of gameplay in memory so disk writes can't cause a hitch.
constexpr size_t traceCapacity=65536;
struct TraceRow {
    int episode,map,tic,tics,ai,loading,capture,visible,focused,menu,skipped;
    double frame,game,effects,audio,nav,sense,brain,coordination,setup,pacing,acquire,scene,encode,submit;
};
std::vector<TraceRow> traceRows;
FILE *traceFile;
const char *tracePath;
size_t traceCount=0;
int traceSkipped=0;

void saveFrameTrace() {
    fputs("frame,episode,map,level_tic,game_tics,ai_active,loading,capture,visible,focused,menu,skipped_presents,"
          "frame_ms,game_other_ms,effects_ms,audio_ms,ai_nav_ms,ai_sense_ms,ai_brain_ms,ai_coordination_ms,"
          "setup_ms,pacing_ms,acquire_ms,scene_ms,encode_ms,submit_ms\n",traceFile);
    size_t first=traceCount-std::min(traceCount,traceRows.size());
    for(size_t i=first;i<traceCount;++i) {
        const TraceRow &r=traceRows[i%traceRows.size()];
        fprintf(traceFile,"%zu,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,"
                          "%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f\n",
            i+1,r.episode,r.map,r.tic,r.tics,r.ai,r.loading,r.capture,r.visible,r.focused,r.menu,r.skipped,
            r.frame,r.game,r.effects,r.audio,r.nav,r.sense,r.brain,r.coordination,r.setup,r.pacing,r.acquire,r.scene,r.encode,r.submit);
    }
    bool failed=ferror(traceFile)!=0;
    if(fclose(traceFile)!=0||failed)fprintf(stderr,"Could not finish frame trace: %s\n",tracePath);
}
}

bool startFrameTrace() {
    int arg=M_CheckParm((char*)"-frametrace");
    if(!arg)return false;
    if(arg+1>=myargc||myargv[arg+1][0]=='-') {
        fprintf(stderr,"Usage: -frametrace <output.csv>\n");
        return false;
    }
    tracePath=myargv[arg+1];
    traceFile=fopen(tracePath,"w");
    if(!traceFile) {
        fprintf(stderr,"Could not open frame trace: %s\n",tracePath);
        return false;
    }
    traceRows.resize(traceCapacity);
    if(std::atexit(saveFrameTrace)!=0) {
        fclose(traceFile);
        fprintf(stderr,"Could not register frame trace: %s\n",tracePath);
        return false;
    }
    // SDL's real clock is independent of the game's slow-motion scale.
    auto clock=[]() -> uint64_t {return SDL_GetTicksNS();};
    Game_SetAiTiming(clock);
    I_SetAudioTiming(clock);
    statsTracing=true;
    return true;
}

void skipFrameTrace() {++traceSkipped;}

void traceFrame(const FrameTraceTimes &t,bool loading,bool capture,bool visible,bool focused) {
    static uint64_t previousSubmit=0;
    static int previousTic=0;
    game_ai_timing ai=Game_TakeAiTiming();
    double audio=I_TakeAudioTiming()/1e6,effects=ticWork/1e6;
    if(previousSubmit&&gamestate==GS_LEVEL) {
        double aiMs=(ai.nav+ai.sense+ai.brain+ai.coordination)/1e6;
        double game=std::max(0.0,(t.renderStart-previousSubmit)/1e6-aiMs-audio-effects);
        bool menu=menuactive||paused||I_Render3DSettingsOpen()||I_Render3DPageOpen();
        traceRows[traceCount%traceRows.size()]={
            gameepisode,gamemap,leveltime,gametic-previousTic,Game_AiActive(),loading,capture,visible,focused,menu,traceSkipped,
            (t.submitted-previousSubmit)/1e6,game,effects,audio,ai.nav/1e6,ai.sense/1e6,ai.brain/1e6,ai.coordination/1e6,
            (t.waitStart-t.renderStart)/1e6,(t.paced-t.waitStart)/1e6,(t.acquired-t.paced)/1e6,(t.prepared-t.acquired)/1e6,
            (t.submitStart-t.prepared)/1e6,(t.submitted-t.submitStart)/1e6};
        ++traceCount;
    }
    previousSubmit=t.submitted;
    previousTic=gametic;
    traceSkipped=0;
}

void statsAdd(Stat stat,uint64_t ns) {slots[(size_t)stat].frame+=ns/1e6;}
void statsCount(Stat stat,int items) {slots[(size_t)stat].items=items;}
void statsTicWork(uint64_t start) {if(start)ticWork+=(double)(SDL_GetTicksNS()-start);}
const StatsView &statsView() {return view;}

void statsFrame(uint64_t waitStart,uint64_t acquired,uint64_t prepared,uint64_t submitted) {
    statsTiming=settings.accelerated&&settings.fps>=3;
    if(settings.fps<2) {lastWait=0;frames=0;ticWork=0;clearPieces();return;}
    // F4's GPU times: this frame's pieces, as gpu3d/gpu_pieces.cpp timed them.
    if(profile.frame.pieced) {
        double sum=0;
        for(int i=0;i<I_RENDER3D_PIECES;++i) {
            double ms=profile.frame.pieceMs[i];
            pieceTotal[(size_t)i]+=ms;pieceWorst[(size_t)i]=std::max(pieceWorst[(size_t)i],ms);sum+=ms;
        }
        piecesTotal+=sum;piecesWorst=std::max(piecesWorst,sum);++piecedFrames;
    }
    if(lastWait&&lastSubmitted&&waitStart>lastWait) {
        double interval=(waitStart-lastWait)/1e6,effects=ticWork/1e6;
        double game=std::max(0.0,(waitStart-std::min(waitStart,lastSubmitted))/1e6-effects);
        newest=(newest+1)%statsHistory;recent[newest]=(float)interval;
        ++frames;sumInterval+=interval;worstInterval=std::max(worstInterval,interval);
        sumGame+=game;sumEffects+=effects;sumWait+=(acquired-waitStart)/1e6;
        sumScene+=(prepared-acquired)/1e6;sumSubmit+=(submitted-prepared)/1e6;
    }
    // STATSDEBUG=1 (with the full readout): each system that took over 4 ms of a frame.
    static const bool debug=getenv("STATSDEBUG")!=nullptr;
    for(size_t i=0;i<slots.size();++i)if(debug&&slots[i].frame>4)
        fprintf(stderr,"STATS tic %d: system %d took %.2f ms\n",leveltime,(int)i,slots[i].frame);
    for(Slot &slot:slots) {slot.total+=slot.frame;slot.worst=std::max(slot.worst,slot.frame);slot.frame=0;}
    ticWork=0;lastWait=waitStart;lastSubmitted=submitted;
    if(!windowStart)windowStart=submitted;
    // Four times a second: the window's averages become the view.
    if(submitted-windowStart>=250000000&&frames>0) {
        double seconds=(submitted-windowStart)/1e9;
        view.fps=(float)(frames/seconds);
        view.frameMs=(float)(sumInterval/frames);view.worstMs=(float)worstInterval;
        view.gameMs=(float)(sumGame/frames);view.effectsMs=(float)(sumEffects/frames);
        view.sceneMs=(float)(sumScene/frames);view.submitMs=(float)(sumSubmit/frames);view.waitMs=(float)(sumWait/frames);
        for(size_t i=0;i<slots.size();++i) {
            Slot &slot=slots[i];
            view.systemMs[i]=(float)(slot.total/frames);view.systemWorst[i]=(float)slot.worst;view.items[i]=slot.items;
            slot.total=slot.worst=0;slot.items=-1;
        }
        for(size_t i=0;i<statsHistory;++i)view.recent[i]=recent[(newest+1+i)%statsHistory];
        view.gpuPieces=piecedFrames>0;
        for(size_t i=0;i<(size_t)I_RENDER3D_PIECES;++i) {
            view.pieceMs[i]=piecedFrames?(float)(pieceTotal[i]/piecedFrames):0;view.pieceWorst[i]=(float)pieceWorst[i];
        }
        view.piecesMs=piecedFrames?(float)(piecesTotal/piecedFrames):0;view.piecesWorst=(float)piecesWorst;
#ifdef DOOM_DEVTOOLS
        // DERP_PIECES: each window's GPU pieces as text (stderr), for timing runs nobody watches.
        static const bool printPieces=getenv("DERP_PIECES")!=nullptr;
        if(printPieces&&piecedFrames) {
            fprintf(stderr,"pieces %d frames: all %.3f opaque %.3f",piecedFrames,piecesTotal/piecedFrames,pieceTotal[I_RENDER3D_PIECE_OPAQUE]/piecedFrames);
            for(size_t i=0;i<(size_t)I_RENDER3D_PIECES;++i)fprintf(stderr," %zu:%.3f",i,pieceTotal[i]/piecedFrames);
            fprintf(stderr,"\n");
        }
        // DERP_SYSTEMS (with the full readout): each window's scene time and each system's share, by stat number.
        static const bool printSystems=getenv("DERP_SYSTEMS")!=nullptr;
        if(printSystems&&statsTiming) {
            fprintf(stderr,"systems tic %d: scene %.3f",leveltime,view.sceneMs);
            for(size_t i=0;i<slots.size();++i)fprintf(stderr," %zu:%.3f",i,view.systemMs[i]);
            fprintf(stderr,"\n");
        }
#endif
        clearPieces();
        frames=0;sumInterval=worstInterval=sumGame=sumEffects=sumScene=sumSubmit=sumWait=0;
        windowStart=submitted;
        ++view.serial;
    }
    // Once a second: the CPU load since the last.
    if(!loadStart||submitted-loadStart>=1000000000) {
        double process,busy,total;
        I_CpuTimes(&process,&busy,&total);
        if(loadStart) {
            double seconds=(submitted-loadStart)/1e9;
            view.cores=std::max(1,SDL_GetNumLogicalCPUCores());
            view.load=(float)(100*(process-processStart)/(seconds*view.cores));
            view.systemLoad=total>totalStart?(float)(100*(busy-busyStart)/(total-totalStart)):-1;
        }
        processStart=process;busyStart=busy;totalStart=total;loadStart=submitted;
    }
}
} // namespace doom3d
