/* The numbers of F4's "Show FPS" readout (stats_overlay.cpp draws them):
   - every frame, the backend's four moments (statsFrame): when it started
     waiting for the display, got its image, had the scene built and
     submitted it. From them the frame rate, the frame times for the graph,
     and where a frame's time went: the game's tics (less DERP's own work in
     them), the scene, encoding and submitting, the wait.
   - with the full readout only (statsTiming), each system's CPU time, tic
     work and frame work alike (timed, statsTicWork); otherwise the timers
     don't read the clock.
   - once a second, the CPU load: this process's, and the whole system's
     where the system says (cpu_time.h).
   The readout's view is gathered four times a second, so numbers stay readable. */
#include "internal.h"
#include "../cpu_time.h"

namespace doom3d {
bool statsTiming=false;
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
}

void statsAdd(Stat stat,uint64_t ns) {slots[(size_t)stat].frame+=ns/1e6;}
void statsCount(Stat stat,int items) {slots[(size_t)stat].items=items;}
void statsTicWork(uint64_t start) {if(start)ticWork+=(double)(SDL_GetTicksNS()-start);}
const StatsView &statsView() {return view;}

void statsFrame(uint64_t waitStart,uint64_t acquired,uint64_t prepared,uint64_t submitted) {
    statsTiming=settings.accelerated&&settings.fps>=3;
    if(settings.fps<2) {lastWait=0;frames=0;return;}
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
