/* Frame pacing: the steady moment in each frame. With vsync one call in the
   frame waits for the display: taking the next swapchain image on some
   drivers, presenting the frame on others. The moment that call returns
   follows the display's rhythm, while other moments in the frame drift with
   the work done before them (more of it while walking and turning). So the
   mouse is read there (I_LatchMouse) and the world blended to the last such
   moment (steadyClock): turning and walking stay even, and even together. */
#include "internal.h"

namespace doom3d {
// The game's clock (tics): how long each call waits on average, and when it last returned.
static double acquireWait,submitWait,acquireEnd=-1,submitEnd=-1;

double paceStart() {return I_GetTimeExact();}
static bool acquireWaits() {return acquireWait>=submitWait;}
static void waited(double &average,double &end,double start,bool steady) {
    end=I_GetTimeExact();
    average+=(end-start-average)*0.1;
    if(steady)I_LatchMouse();
}
void paceAcquired(double start) {waited(acquireWait,acquireEnd,start,acquireWaits());}
void paceSubmitted(double start) {waited(submitWait,submitEnd,start,!acquireWaits());}
double steadyClock(double now) {
    double end=acquireWaits()?acquireEnd:submitEnd;
    // None yet (hidden render checks) or long ago (a loading screen, a pause): now.
    return end<0||now-end>4?now:end;
}
} // namespace doom3d
