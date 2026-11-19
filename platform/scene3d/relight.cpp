/* Background re-bake of lamp light and sun around moving sectors
   (doors, lifts, lowering walls) and shot-out lights. */
#include "internal.h"
#include "gpu_trace.h"
#include "light_rooms.h"
#include "../rooms.h"
#include <condition_variable>
#include <deque>
#include <mutex>
#ifdef DOOM_DEVTOOLS
extern "C" boolean P_TeleportMove(mobj_t *thing,fixed_t x,fixed_t y);   // for DERP_PLACE
#endif

namespace doom3d {
// Sectors that move and stay (settings.movingRelight): bakeMap follows a door,
// lift or lowering wall only once it has come to rest for good, which is
// when the game lets go of its mover (sector->specialdata): a door that stays
// open, a floor lowered for good. A door waiting to close, a lift waiting to
// come back or one that never rests keeps its mover, and bakeMap and the
// baked light keep it where the level was baked; the door light spill
// (lights.cpp's doorSpill) is all the light it lets through meanwhile.
// Re-baking every step of every door cost the frames (2026-11-18: E1M7's
// lift, 0.3 s jobs back to back, 120 fps down to 60-80). A sector that
// stays re-bakes, on a background thread, the static light within reach of
// every baked light whose radius touches the sector, and the sun on floors
// and walls up to the distance the sector's opening can throw it. Bounce,
// sky light, flow and sunbeams keep the level's start.
// One re-bake runs at a time; changes meanwhile wait for the next.
// The job runs on the light worker (lightWorkerPost) and its buffers stay
// from one job to the next, so a door re-baking every few tics allocates
// nothing: the frame thread fills them before posting and reads them again
// only once done is set.
struct Relight {
    std::atomic<bool> done{false};
    bool running=false,lights=false,only=false,grid=false,sun=false;
    BakeMap map; // bakeMap as it was when the re-bake started.
    float sunDirection[3]={},sunSpread=0;
    // Per map cell, wall strip and grid cell: 1 re-light, 2 re-sun.
    std::vector<uint8_t> cells,strips,gridCells;
    int lightBounds[4],sunBounds[4]; // Marked cells: x0, y0, x1, y1 inclusive.
    std::vector<int16_t> sunCells; // Floor sun over sunBounds; -1 unchanged.
    std::vector<size_t> gridList;
    std::vector<StaticCell> gridResults;
    // Merged lights that lost shot-out tiles in this re-bake, as they shone
    // and as they shine now (strength 0 when all went), and per sector the
    // floor light they lost and the floor light left after (every other
    // cell each way).
    std::vector<BakeLight> gone,kept;
    std::vector<double> lost,left;
    // Round moved doors (whole-level lighting): only the light that passes them is traced again
    // (bake_moved.cpp), from before, the map as the baked light holds it, to map.
    bool moved=false;
    BakeMap before;
    std::vector<std::array<float,4>> boxes;
    std::vector<char> movedSector;
    // Lamps that went out or were switched (whole-level lighting): only their own light is taken
    // out and put back (bake_moved.cpp), as they shone (was) and as they shine now.
    bool changed=false;
    ChangedLamps lamps;
    std::vector<uint16_t> litBefore;   // litCellsAround() as the job began, for dimBounce
    std::vector<int> stripList;        // the marked strips, in bake order (the worker's)
    // A far job (the exact re-bake that follows when all is quiet) gives way to anything new:
    // it stops at its next row, what it has baked stays, and its places wait again.
    bool far=false,overdue=false;   // overdue: a far job that waited too long; only a new shot stops it
    // A shot lamp in whole-level lighting, baked out the exact way (startRelight): once done it is
    // held, shown only with the bounce solved again from it (relightMovingSectors).
    bool exactShot=false,held=false;
    int heldSince=0;
    bool prepared=false;   // a prepared shot's (tickPrepared): on copies of the light, not shown
    std::atomic<bool> cancel{false};
    std::vector<std::array<float,3>> farPlaces;
    // So does a door's or lift's exact re-bake to a shot lamp (relightMovingSectors): baking the
    // same texels again gives the same light, so its sectors (these) just wait for the next job.
    std::vector<int> sectors;
} relight;
// Re-bakes share a small pool of threads that stay up between them: a moving
// door re-bakes every few frames, and starting half the cores' worth of
// threads for each phase competed with drawing. The job's own thread and at
// most 3 helpers finish a little later and leave the game its cores. Prepared shots
// (backgroundLight) take 2 of them with settings.instantShots 2: the frame's cores matter more
// there than how soon the next lamp is ready.
class RelightPool {
public:
    RelightPool() {
        unsigned helpers=std::min(3u,std::max(1u,std::thread::hardware_concurrency()/2)-1);
        for(unsigned n=0;n<helpers;++n)threads.emplace_back([this,n]{help(n);});
    }
    ~RelightPool() {
        {std::lock_guard<std::mutex> lock(mutex);stop=true;}
        wake.notify_all();
        for(auto &thread:threads)thread.join();
    }
    // work(0..count-1), on the calling thread and the helpers. It returns once
    // every helper has taken its turn at this run: a helper that woke late
    // would otherwise take the next run's counter to this run's work.
    void run(int count,const std::function<void(int)> &work) {
        const unsigned most=backgroundLight&&settings.instantShots==2?2u:(unsigned)threads.size();
        {std::lock_guard<std::mutex> lock(mutex);task=&work;target=lightTarget;total=count;working=most;next=0;arrived=0;++generation;}
        wake.notify_all();
        for(int n;(n=next++)<count;)work(n);
        std::unique_lock<std::mutex> lock(mutex);
        idle.wait(lock,[this]{return busy==0&&arrived==(int)threads.size();});
    }
private:
    void help(unsigned index) {
        unsigned seen=0;
        std::unique_lock<std::mutex> lock(mutex);
        for(;;) {
            wake.wait(lock,[&]{return stop||generation!=seen;});
            if(stop)return;
            seen=generation;++busy;++arrived;
            const std::function<void(int)> *work=task;int count=index<working?total:0;
            lightTarget=target;   // the caller's light: a prepared shot's copies, or the level's
            lock.unlock();
            for(int n;(n=next++)<count;)(*work)(n);
            lightTarget=nullptr;
            lock.lock();
            --busy;
            idle.notify_all();
        }
    }
    std::vector<std::thread> threads;
    std::mutex mutex;
    std::condition_variable wake,idle;
    const std::function<void(int)> *task=nullptr;
    LightTarget *target=nullptr;
    std::atomic<int> next{0};
    int total=0,busy=0,arrived=0;
    unsigned generation=0,working=0;
    bool stop=false;
};
static RelightPool &relightPool() {static RelightPool pool;return pool;}
void relightFor(int count,const std::function<void(int)> &work) {relightPool().run(count,work);}
// Slices of whole-map work done in play on the game's thread (weather.cpp's
// re-trace round a door at rest): a pool of their own, kept up, so a slice
// starts no threads and never waits for a re-bake's helpers.
void playFor(int count,const std::function<void(int)> &work) {
    static RelightPool pool;
    pool.run(count,work);
}
// The one long-lived thread the re-bakes and the bounce's solves run on, one
// after another in the order posted. A thread per job cost its start-up every
// few tics while a door moved, and two jobs side by side would share the
// pool's helpers, which take one caller at a time; in turn they never meet. A
// cancelled bounce solve winds down here while the frame goes on, and the
// re-bake posted after it waits behind it on this thread, not on the frame.
class LightWorker {
public:
    LightWorker() {
        relightPool();   // made first, so it is let go after this thread has stopped
        thread=std::thread([this]{loop();});
    }
    ~LightWorker() {
        {std::lock_guard<std::mutex> lock(mutex);stop=true;}
        wake.notify_all();
        thread.join();
    }
    void post(std::function<void()> task) {
        {std::lock_guard<std::mutex> lock(mutex);tasks.push_back(std::move(task));}
        wake.notify_all();
    }
    void wait() {
        std::unique_lock<std::mutex> lock(mutex);
        idle.wait(lock,[this]{return tasks.empty()&&!busy;});
    }
private:
    void loop() {
        std::unique_lock<std::mutex> lock(mutex);
        for(;;) {
            wake.wait(lock,[this]{return stop||!tasks.empty();});
            if(stop)return;
            std::function<void()> task=std::move(tasks.front());tasks.pop_front();busy=true;
            lock.unlock();
            task();
            task=nullptr;   // what it holds (a solve) is let go before it counts as done
            lock.lock();
            busy=false;
            idle.notify_all();
        }
    }
    std::thread thread;
    std::mutex mutex;
    std::condition_variable wake,idle;
    std::deque<std::function<void()>> tasks;
    bool busy=false,stop=false;
};
static LightWorker &lightWorker() {static LightWorker worker;return worker;}
void lightWorkerPost(std::function<void()> task) {lightWorker().post(std::move(task));}
void lightWorkerWait() {lightWorker().wait();}
// What the re-bakes and the bounce's solves changed goes to the GPU one
// texture a frame (relightMovingSectors), the lamp light first, then the sun,
// the bounce and the directions; all of them in one frame was a burst of
// uploads the frame paid for. Each reads the cells as they are when it goes,
// so a texture queued twice uploads its latest. No re-bake starts while any
// wait: the worker writes those cells.
struct LightUpload { GpuTextureRef texture; std::vector<std::array<int,4>> rects; const std::vector<uint8_t> *cells; size_t size; int bytesPerRow; };
static std::vector<LightUpload> lightUploads;
void queueLightUpload(const GpuTextureRef &texture,const std::vector<std::array<int,4>> &rects,const std::vector<uint8_t> &cells,int bytesPerRow) {
    if(lightTarget)return;   // a prepared shot's copies never go up (tickPrepared)
    if(!texture||rects.empty()||cells.empty())return;
    for(LightUpload &upload:lightUploads)
        if(upload.texture==texture&&upload.cells==&cells) {upload.rects.insert(upload.rects.end(),rects.begin(),rects.end());return;}
    lightUploads.push_back({texture,rects,&cells,cells.size(),bytesPerRow});
}
bool lightUploadsPending() {return !lightUploads.empty();}
#ifdef DOOM_DEVTOOLS
// DERP_FOOTPRINT (with DERP_SHOOT): when the first shot went, and the frames since; each light upload after it is
// printed, so a shot's stages show as they reach the screen.
static uint64_t footShotAt=0;
static int footFrames=0;
static const char *cellsName(const std::vector<uint8_t> *cells) {
    return cells==&flatLightCells?"floors and ceilings":cells==&wallBakeCells?"walls":cells==&flatDirectionCells?"floor directions":
           cells==&wallDirectionCells?"wall directions":cells==&contactCells?"sun":"other";
}
#endif
// The next waiting upload, or all of them.
static void uploadLight(bool all) {
    while(!lightUploads.empty()) {
        LightUpload upload=std::move(lightUploads.front());
        lightUploads.erase(lightUploads.begin());
        if(upload.cells->size()==upload.size)gpuUpdateTexture(upload.texture,GpuFormat::RGBA8,upload.rects,upload.cells->data(),upload.bytesPerRow);
#ifdef DOOM_DEVTOOLS
        if(footShotAt)fprintf(stderr,"footprint: %s uploaded %.0f ms, %d frames after the shot (tic %d).\n",cellsName(upload.cells),
                              (SDL_GetTicksNS()-footShotAt)/1e6,footFrames,leveltime);
#endif
        if(!all)return;
    }
}
// The wall strips by where they lie, in squares of stripBin units, so marking
// a place looks at the strips near it and not at every strip of the level.
// Built on first use after the strips are laid out (once a level: stopRelight
// forgets it); used on the frame thread only.
constexpr float stripBin=256;
static std::vector<std::vector<int>> stripBins;
static int binsWide=0,binsHigh=0;
static size_t binnedStrips=0,binnedOrder=0;
static std::vector<unsigned> stripSeen;   // per strip: the query that last listed it
static unsigned stripQuery=0;
static int binX(float x) {return std::clamp((int)std::floor((x-mapOrigin[0])/stripBin),0,binsWide-1);}
static int binY(float y) {return std::clamp((int)std::floor((y-mapOrigin[1])/stripBin),0,binsHigh-1);}
static void binStrips() {
    if(!stripBins.empty()&&binnedStrips==bakeStrips.size()&&binnedOrder==bakeOrder.size())return;
    binnedStrips=bakeStrips.size();binnedOrder=bakeOrder.size();
    binsWide=std::max(1,(int)std::ceil(mapWidth*mapCell/stripBin));binsHigh=std::max(1,(int)std::ceil(mapHeight*mapCell/stripBin));
    stripBins.assign((size_t)binsWide*binsHigh,{});
    stripSeen.assign(bakeStrips.size(),0);stripQuery=0;
    // Every bin its box overlaps, clamped to the map's: a strip overlapping a
    // box shares a point with it, and that point's bin holds the strip.
    for(int index:bakeOrder) {
        const BakeStrip &strip=bakeStrips[index];
        float bx=strip.ax+strip.dx*strip.length,by=strip.ay+strip.dy*strip.length;
        for(int y=binY(std::min(strip.ay,by));y<=binY(std::max(strip.ay,by));++y)
            for(int x=binX(std::min(strip.ax,bx));x<=binX(std::max(strip.ax,bx));++x)stripBins[(size_t)y*binsWide+x].push_back(index);
    }
}
void stripsNear(const std::vector<std::array<float,4>> &boxes,std::vector<int> &out) {
    out.clear();
    binStrips();
    if(stripBins.empty()||bakeOrder.empty())return;
    if(++stripQuery==0) {std::fill(stripSeen.begin(),stripSeen.end(),0);stripQuery=1;}
    for(const auto &r:boxes) {
        for(int y=binY(r[1]);y<=binY(r[3]);++y)for(int x=binX(r[0]);x<=binX(r[2]);++x)
            for(int index:stripBins[(size_t)y*binsWide+x]) {
                if(stripSeen[index]==stripQuery)continue;
                const BakeStrip &strip=bakeStrips[index];
                float bx=strip.ax+strip.dx*strip.length,by=strip.ay+strip.dy*strip.length;
                if(std::max(strip.ax,bx)<r[0]||std::min(strip.ax,bx)>r[2]||std::max(strip.ay,by)<r[1]||std::min(strip.ay,by)>r[3])continue;
                stripSeen[index]=stripQuery;out.push_back(index);
            }
    }
    std::sort(out.begin(),out.end());
}
static void forgetStripBins() {stripBins.clear();stripSeen.clear();binnedStrips=binnedOrder=0;}
static std::vector<int> relightPending;
constexpr float nearRelight=320;
constexpr float doorMost=256;  // a moved sector wider than this is re-baked whole: nearly every light's way crosses it
constexpr int farQuiet=35;     // tics of quiet before the far jobs start
static int quietSince=0;       // leveltime of the last re-bake that was not a far job
// Per sector: the floor and ceiling heights the baked lamp light holds.
static std::vector<std::array<float,2>> litHeights;
void relightBaked() {
    litHeights.resize(bakeMap.sectors.size());
    for(size_t s=0;s<litHeights.size();++s)litHeights[s]={bakeMap.sectors[s].floor,bakeMap.sectors[s].ceiling};
}
// Whole-level lighting: the bounce is solved again this far round a lamp that
// went or a sector that moved (reBounceMark, bake_bounce.cpp).
constexpr float bounceRound=384;
// The bounce to solve again for a lamp that changed: round it, and all of its room within its
// reach, so that a room whose lamps are all gone keeps none of their bounce.
static std::array<float,4> bouncePlace(const BakeLight &light) {
    float x0=light.x-bounceRound,y0=light.y-bounceRound,x1=light.x+bounceRound,y1=light.y+bounceRound,reach=bakeReach(light);
    int room=Rooms_Of(int(R_PointInSubsector((fixed_t)(light.x*FRACUNIT),(fixed_t)(light.y*FRACUNIT))->sector-sectors));
    if(room>=0)for(int s=0;s<numsectors;++s) {
        if(Rooms_Of(s)!=room)continue;
        for(int n=0;n<sectors[s].linecount;++n)for(const vertex_t *v:{sectors[s].lines[n]->v1,sectors[s].lines[n]->v2}) {
            float x=std::clamp(units(v->x),light.x-reach,light.x+reach),y=std::clamp(units(v->y),light.y-reach,light.y+reach);
            x0=std::min(x0,x);y0=std::min(y0,y);x1=std::max(x1,x);y1=std::max(y1,y);
        }
    }
    return {x0,y0,x1,y1};
}
static void markBounce(const BakeLight &light) {const auto r=bouncePlace(light);reBounceMark(r[0],r[1],r[2],r[3]);}
static std::vector<std::array<float,3>> farPending;   // x, y, reach of lights whose far part waits (startRelight)
// The far jobs wait for a quiet moment, and a level may never have one: a lift that never rests,
// monsters at the doors. A room whose lamps were shot out then kept the quick way's leftovers and
// its dimmed bounce for good. So once changed lamps are among the far places (farLamps) and have
// waited farLongest tics (farSince: since when), they are baked whatever else waits, and only a
// new shot stops that job; the bounce waits for their exact light. A door's far places alone
// still wait for quiet: a lift that never rests would have them baked again and again.
constexpr int farLongest=70;
static int farSince=0;
static bool farLamps=false;
static void addFar(const std::array<float,3> &place,bool lamp) {
    if(lamp&&!farLamps) {farLamps=true;farSince=leveltime;}
    if(std::find(farPending.begin(),farPending.end(),place)==farPending.end())farPending.push_back(place);
}
static std::vector<SurfaceLightKey> brokenPending;
// A light switch changed lamps (lampLevelsFollow): seen last frame, so the
// lamps as collected hold their new strength; then due until a job takes it.
static bool switchSeen=false,switchDue=false;
unsigned liveLightSerial=0;
unsigned lightCellsSerial=0;
static uint64_t brokenAt=0,goneAt=0;   // when a shot lamp began to wait, and that of the job taking it out (DERP_RELIGHT's timing)
void relightBrokenLight(const SurfaceLightKey &cell) {
    if(brokenPending.empty())brokenAt=SDL_GetTicksNS();
    brokenPending.push_back(cell);
}
// Whether the baked light holds the sector as it stands now: it rests for good where the
// light was last baked for it. doorSpill stands down there in Classic with light through
// doorways, whose baked doorway lamp then shines through the opening.
bool relightHoldsSector(int sector) {
    if(!settings.movingRelight||sector<0||(size_t)sector>=litHeights.size()||sector>=numsectors||sectors[sector].specialdata)return false;
    return litHeights[sector][0]==units(sectors[sector].floorheight)&&litHeights[sector][1]==units(sectors[sector].ceilingheight);
}
static void markRelight(Relight &job,float x0,float y0,float x1,float y1,uint8_t bit,bool grid=true) {
    int cx0=std::clamp((int)std::floor((x0-mapOrigin[0])/mapCell),0,mapWidth-1),cx1=std::clamp((int)std::floor((x1-mapOrigin[0])/mapCell),0,mapWidth-1);
    int cy0=std::clamp((int)std::floor((y0-mapOrigin[1])/mapCell),0,mapHeight-1),cy1=std::clamp((int)std::floor((y1-mapOrigin[1])/mapCell),0,mapHeight-1);
    for(int y=cy0;y<=cy1;++y)for(int x=cx0;x<=cx1;++x)job.cells[(size_t)y*mapWidth+x]|=bit;
    int *b=bit==1?job.lightBounds:job.sunBounds;
    b[0]=std::min(b[0],cx0);b[1]=std::min(b[1],cy0);b[2]=std::max(b[2],cx1);b[3]=std::max(b[3],cy1);
    static std::vector<int> near;
    stripsNear({{x0,y0,x1,y1}},near);
    for(int index:near)job.strips[index]|=bit;
    if(bit==1&&job.grid&&grid) {
        int gx0=std::clamp((int)std::floor((x0-mapOrigin[0])/staticCell),0,staticWidth-1),gx1=std::clamp((int)std::floor((x1-mapOrigin[0])/staticCell),0,staticWidth-1);
        int gy0=std::clamp((int)std::floor((y0-mapOrigin[1])/staticCell),0,staticHeight-1),gy1=std::clamp((int)std::floor((y1-mapOrigin[1])/staticCell),0,staticHeight-1);
        for(int y=gy0;y<=gy1;++y)for(int x=gx0;x<=gx1;++x)job.gridCells[(size_t)y*staticWidth+x]=1;
    }
}
// How much of each sector's floor light the shot-out tiles gave (their
// merged lights before, less what is left of them), against what the
// re-bake left it (every other cell each way, both).
static void measureLoss(Relight &job) {
    static const float up[3]={0,0,1};
    job.lost.assign(numsectors,0);job.left.assign(numsectors,0);
    const int *b=job.lightBounds;
    for(int y=b[1];y<=b[3];y+=2)for(int x=b[0];x<=b[2];x+=2) {
        size_t i=(size_t)y*mapWidth+x;int sector=seamCells[i].own;
        if(!(job.cells[i]&1)||sector==noSector||job.map.sectors[sector].sky)continue;
        float px=mapOrigin[0]+(x+0.5f)*mapCell,py=mapOrigin[1]+(y+0.5f)*mapCell,before[3]={},after[3]={};
        for(const BakeLight &light:job.lamps.was)addBakedLight(job.map,light,sector,px,py,job.map.sectors[sector].floor+1,up,before);
        for(const BakeLight &light:job.lamps.now)
            if(light.strength>0)addBakedLight(job.map,light,sector,px,py,job.map.sectors[sector].floor+1,up,after);
        job.lost[sector]+=luminance(before)-luminance(after);
    }
    for(double &lost:job.lost)lost=std::max(lost,0.0);
    for(int y=0;y<mapHeight;y+=2)for(int x=0;x<mapWidth;x+=2) {
        size_t i=(size_t)y*mapWidth+x;int sector=seamCells[i].own;
        if(sector==noSector)continue;
        const uint8_t *cell=flatCell(floorLight,i);
        float rgb[3]={cell[0]/255.0f*2,cell[1]/255.0f*2,cell[2]/255.0f*2};
        job.left[sector]+=luminance(rgb);
    }
}
static void runRelight(Relight &job);
// Prepared shots (PLAN-destructible-lights.md step 2a, settings.instantShots). The light worker
// bakes the shots of the unbroken fixtures nearest the eye before they happen: for each, the job a
// shot would start (startRelight's exactShot) and the bounce solved again after it, on copies of the
// level's light (lightTarget, internal.h), and keeps the texels that came out otherwise, with the
// sprites' grid cells and the sectors' dimming. When that fixture is shot and nothing else has
// changed the light since (prepStamp), startRelight puts those texels in and up at once: the room's
// light changes on the frame the fixture breaks, as step 1 would have changed it a moment later.
// One at a time, nearest first, the nearest preparedMost within preparedBudget bytes, always on the
// processor (backgroundLight: the card's time is the frame's); anything else that changes the light
// (another shot, a door left open, a switch) makes them all stale, and they are prepared again. A
// shot at a fixture not ready yet takes step 1, and stops whatever is being prepared.
// Measured 2026-11-18 (the lamp nearest the eye, shot 6 s in): on the hit frame on the PC (4K), the
// Mac and the Deck, where step 1 took 0.5-0.7 s, 0.5-1 s and 0.9-2.2 s; the same texels as step 1
// (on the Mac and the Deck in the rain, the bounce within 0.3%: see PrepStamp); frames as without;
// 40-145 MB more (the copies of the light and the bounce's buffers); 1-3.5 s a fixture on the PC.
namespace {
constexpr size_t preparedBudget=32u<<20;   // bytes of kept texels, on every machine
constexpr size_t preparedMost=8;           // and no more than the nearest so many: what a gun reaches, and a change makes them all again
constexpr float prepareReach=1536;          // fixtures this near the eye, nearest first
// What the light was made from: a prepared shot holds while all of it does.
// While weather falls, the bounce a shot would be solved with changes with it (the ground's wetness,
// the overcast: BounceSolve::prepare), so a prepared shot holds 30 s at most then: on the Mac and
// the Deck in the rain, one prepared seconds before its shot differed by 0.3% of its texels.
struct PrepStamp {
    unsigned level=~0u,bake=~0u,cells=~0u,live=~0u,settings=~0u;int bounce=-2,weather=-2;
    bool operator==(const PrepStamp &o) const {return level==o.level&&bake==o.bake&&cells==o.cells&&live==o.live&&settings==o.settings&&bounce==o.bounce&&weather==o.weather;}
};
PrepStamp prepStamp() {
    return {r_levelserial,lightBakeSerial,lightCellsSerial,liveLightSerial,settingsSerial,bounceKey,climateNow().weather?leveltime/(TICRATE*30):-1};
}
// Texels by their 4-byte index into one of the light's arrays, and what they become.
struct Texels {
    std::vector<uint32_t> at,value;
    size_t bytes() const {return at.size()*8;}
};
struct PreparedShot {
    SurfaceLightKey id{};               // the fixture: its first tile's key
    std::vector<int> sources;           // the merged lights it changes, sorted
    std::vector<BakeLight> now;         // each as the shot leaves it
    Texels flat,wall,flatDirection,wallDirection;
    std::vector<std::array<int,4>> flatRects,flatDirectionRects,wallRects;   // what goes up: flat boxes per layer, wall strips (both halves)
    std::vector<std::pair<size_t,StaticCell>> grid;
    std::vector<std::pair<int,float>> dims;               // dimShotSector
    PrepStamp stamp;
    float x=0,y=0,z=0;
    size_t bytes() const {return flat.bytes()+wall.bytes()+flatDirection.bytes()+wallDirection.bytes()+grid.size()*sizeof(StaticCell);}
};
std::vector<PreparedShot> preparedShots;
// A fixture that can be shot: its tiles (fixtureKeys), the merged lights they are part of, and its middle.
struct Fixture { std::set<SurfaceLightKey> tiles;std::vector<int> sources;float x,y,z; };
std::vector<Fixture> fixtures;
PrepStamp fixturesStamp;
// The one being prepared.
Relight prep;
LightTarget prepTarget;
PrepStamp prepSynced;            // prepTarget holds the level's light as of then
enum class PrepStage {idle,job,bounce,diff} prepStage=PrepStage::idle;
Fixture prepFixture;
PrepStamp prepBegan;
std::vector<BakeLight> prepNow;
std::vector<std::array<float,4>> prepRects;   // its bounce's places
std::shared_ptr<PreparedShot> prepResult;
std::atomic<bool> prepDiffed{false};
#ifdef DOOM_DEVTOOLS
uint64_t prepStarted=0;
#endif
}

// The fixtures that can be shot (shot_lights.cpp's grouping), with the merged lights they are part of.
static void listFixtures() {
    fixtures.clear();fixturesStamp=prepStamp();
    std::map<SurfaceLightKey,int> sourceOf;
    for(size_t n=0;n<bakeSourceKeys.size()&&n<bakeSources.size();++n)
        if(bakeSourceKeys[n][0]>=2&&bakeSources[n].strength>0)sourceOf[bakeSourceKeys[n]]=(int)n;
    std::set<SurfaceLightKey> seen;
    std::vector<SurfaceLightKey> tiles,cells;
    for(const SurfaceLight &tile:surfaceLights) {
        if(tile.strength<=0||tileShotOut(tile.key)||seen.count(tile.key))continue;
        Fixture f;float middle[3]={};
        fixtureOf(tile,tiles,cells,middle);
        f.tiles.insert(tiles.begin(),tiles.end());seen.insert(tiles.begin(),tiles.end());
        for(const SurfaceLightKey &cell:cells) {
            auto at=sourceOf.find(cell);
            if(at!=sourceOf.end()&&std::find(f.sources.begin(),f.sources.end(),at->second)==f.sources.end())f.sources.push_back(at->second);
        }
        if(f.sources.empty())continue;
        std::sort(f.sources.begin(),f.sources.end());
        f.x=middle[0];f.y=middle[1];f.z=middle[2];
        fixtures.push_back(std::move(f));
    }
}
// The bounce a changed lamp asks to be solved again (markBounce's places).
static std::array<float,4> bouncePlace(const BakeLight &light);
// Sets prep up as startRelight would for a shot of f, and posts it.
static bool startPrepared(const Fixture &f) {
    Relight &job=prep;
    job.lights=settings.bakedLights&&lightBakeKey>=0&&lightBakeKey==lightBakeInputs()&&!bakeGrid.empty()&&wallBakeTexture&&flatLightTexture;
    if(!job.lights)return false;
    job.only=bakeDirected&&wallDirectionTexture&&flatDirectionTexture;
    job.grid=staticSerial==lightBakeSerial&&!staticGrid.empty();
    job.sun=false;
    job.gone.clear();job.kept.clear();job.lost.clear();job.left.clear();
    job.lamps.was.clear();job.lamps.now.clear();job.lamps.group.clear();
    prepNow.clear();prepRects.clear();
    std::vector<std::pair<int,BakeLight>> replaced;
    for(int n:f.sources) {
        const BakeLight was=bakeSources[n];
        if(was.strength<=0)continue;
        BakeLight now=remergedBakeSource(n,f.tiles);
        job.gone.push_back(was);job.kept.push_back(now);
        job.lamps.was.push_back(was);job.lamps.now.push_back(now);job.lamps.group.push_back((size_t)n<sourceGroups.size()?sourceGroups[n]:0);
        prepNow.push_back(now);replaced.push_back({n,now});
        if(now.strength>0)listBakeSource(n,now);   // the grid lists it where it will reach, as a shot's remerge does
        prepRects.push_back(bouncePlace(was));
    }
    if(job.gone.empty())return false;
    job.cells.assign((size_t)mapWidth*mapHeight,0);job.strips.assign(bakeStrips.size(),0);
    job.gridCells.assign(job.grid?staticGrid.size():0,0);
    for(int *b:{job.lightBounds,job.sunBounds}) {b[0]=b[1]=INT_MAX;b[2]=b[3]=-1;}
    for(const auto *lamps:{&job.lamps.was,&job.lamps.now})for(const BakeLight &light:*lamps) {
        if(light.strength<=0)continue;
        float reach=bakeReach(light),near=std::min(reach,nearRelight);
        markRelight(job,light.x-reach,light.y-reach,light.x+reach,light.y+reach,1,false);
        markRelight(job,light.x-near,light.y-near,light.x+near,light.y+near,1);
    }
    if(job.lightBounds[2]<0)return false;
    job.exactShot=true;job.prepared=true;job.changed=job.moved=job.far=job.overdue=false;
    job.cancel=false;job.farPlaces.clear();job.sectors.clear();job.boxes.clear();
    job.map=bakeMap;
    job.sunCells.clear();
    job.gridList.clear();
    for(size_t i=0;i<job.gridCells.size();++i)if(job.gridCells[i])job.gridList.push_back(i);
    job.gridResults.assign(job.gridList.size(),{});
    prepTarget.sources=liveBakeSources;
    for(const auto &[n,now]:replaced)prepTarget.sources[n]=now;
    // The copies follow the level's light once a change has gone in; else they still hold it (the
    // last prepared shot's texels were put back as they were read off).
    const bool sync=!(prepSynced==prepStamp())||prepTarget.flat.size()!=liveFlatLightCells.size()||prepTarget.wall.size()!=liveWallBakeCells.size()||
                    prepTarget.flatDirection.size()!=liveFlatDirectionCells.size()||prepTarget.wallDirection.size()!=liveWallDirectionCells.size();
    prepSynced=prepStamp();prepBegan=prepStamp();prepFixture=f;
    job.done=false;job.running=true;
    lightWorkerPost([sync] {
        if(sync) {
            prepTarget.flat=liveFlatLightCells;prepTarget.wall=liveWallBakeCells;
            prepTarget.flatDirection=liveFlatDirectionCells;prepTarget.wallDirection=liveWallDirectionCells;
        }
        lightTarget=&prepTarget;backgroundLight=true;
        runRelight(prep);
        lightTarget=nullptr;backgroundLight=false;
    });
    return true;
}
// On the light worker: the texels the prepared job and bounce changed in the copies, against the
// level's light, into shot, and the copies put back as they were. box: the cells (x0, y0, x1, y1).
static void diffPrepared(PreparedShot &shot,const int box[4],const std::vector<int> &strips) {
    auto texel=[](std::vector<uint8_t> &mine,const std::vector<uint8_t> &live,Texels &out,size_t at) {
        uint32_t a,b;std::memcpy(&a,&mine[4*at],4);std::memcpy(&b,&live[4*at],4);
        if(a==b)return false;
        out.at.push_back((uint32_t)at);out.value.push_back(a);std::memcpy(&mine[4*at],&b,4);
        return true;
    };
    const size_t layer=(size_t)mapWidth*mapHeight;
    auto flats=[&](std::vector<uint8_t> &mine,const std::vector<uint8_t> &live,Texels &out,std::vector<std::array<int,4>> &rects) {
        if(mine.size()!=live.size()||live.empty()||box[2]<box[0])return;
        for(size_t of=0;of*layer*4<live.size();++of) {
            bool any=false;
            for(int y=box[1];y<=box[3];++y)for(int x=box[0];x<=box[2];++x)any|=texel(mine,live,out,of*layer+(size_t)y*mapWidth+x);
            if(any)rects.push_back({box[0],box[1]+(int)of*mapHeight,box[2]-box[0]+1,box[3]-box[1]+1});
        }
    };
    auto walls=[&](std::vector<uint8_t> &mine,const std::vector<uint8_t> &live,Texels &out,bool rects) {
        if(mine.size()!=live.size()||live.empty())return;
        for(int index:strips) {
            const BakeStrip &strip=bakeStrips[index];
            for(int half=0;half<2;++half) {
                bool any=false;
                for(int row=0;row<strip.height;++row)for(int column=0;column<strip.width;++column)
                    any|=texel(mine,live,out,(size_t)(strip.y+row)*atlasStride+half*atlasWidth+strip.x+column);
                if(any&&rects)shot.wallRects.push_back({strip.x+half*atlasWidth,strip.y,strip.width,strip.height});
            }
        }
    };
    flats(prepTarget.flat,liveFlatLightCells,shot.flat,shot.flatRects);
    flats(prepTarget.flatDirection,liveFlatDirectionCells,shot.flatDirection,shot.flatDirectionRects);
    walls(prepTarget.wall,liveWallBakeCells,shot.wall,true);
    walls(prepTarget.wallDirection,liveWallDirectionCells,shot.wallDirection,true);
    std::sort(shot.wallRects.begin(),shot.wallRects.end());shot.wallRects.erase(std::unique(shot.wallRects.begin(),shot.wallRects.end()),shot.wallRects.end());
}
static bool sameLight(const BakeLight &a,const BakeLight &b) {
    return a.x==b.x&&a.y==b.y&&a.z==b.z&&a.radius==b.radius&&a.strength==b.strength&&
           a.color[0]==b.color[0]&&a.color[1]==b.color[1]&&a.color[2]==b.color[2];
}

// Called each frame after the re-bakes (relightMovingSectors): moves the prepared shot along, and
// stops it when something else is to change the light or has.
double prepFrameMs[4]={};   // DERP_RELIGHT's log: the longest frame-thread part so far: start, bounce start, commit, fixtures
static void tickPrepared() {
    const bool want=settings.instantShots&&settings.shootableLights&&settings.lightModel==1&&settings.bakedLights&&!Game_Phobos();
    const bool quiet=!relight.running&&relightPending.empty()&&brokenPending.empty()&&farPending.empty()&&!switchDue&&
                     lightUploads.empty()&&!reBounceBusy()&&!flatLightCells.empty();
    const PrepStamp now=prepStamp();
    if(!preparedShots.empty()&&(!want||!(preparedShots.front().stamp==now)))preparedShots.clear();
    const bool stop=!want||!quiet||!(prepBegan==now);
    switch(prepStage) {
    case PrepStage::idle:break;
    case PrepStage::job:
        if(stop)prep.cancel=true;
        if(!prep.done)return;
        prep.running=false;
        if(prep.cancel) {prepStage=PrepStage::idle;prepSynced={};return;}   // the copies are part-baked: taken anew
        {
            uint64_t t0=SDL_GetTicksNS();
            lightTarget=&prepTarget;
            const bool bouncing=preparedBounceStart(prepRects);
            prepFrameMs[1]=std::max(prepFrameMs[1],(SDL_GetTicksNS()-t0)/1e6);
            lightTarget=nullptr;
            prepStage=PrepStage::bounce;
            if(bouncing)return;
        }
        [[fallthrough]];
    case PrepStage::bounce: {
        if(stop) {preparedBounceStop();prepStage=PrepStage::idle;prepSynced={};return;}
        if(!preparedBounceDone())return;
        std::vector<int> strips;
        for(int index:bakeOrder)if(prep.strips[index])strips.push_back(index);
        const std::vector<int> &bounced=preparedBounceStrips();
        strips.insert(strips.end(),bounced.begin(),bounced.end());
        std::sort(strips.begin(),strips.end());strips.erase(std::unique(strips.begin(),strips.end()),strips.end());
        int box[4]={prep.lightBounds[0],prep.lightBounds[1],prep.lightBounds[2],prep.lightBounds[3]};
        for(const auto &r:prepRects) {
            box[0]=std::min(box[0],std::clamp((int)std::floor((r[0]-mapOrigin[0])/mapCell),0,mapWidth-1));
            box[1]=std::min(box[1],std::clamp((int)std::floor((r[1]-mapOrigin[1])/mapCell),0,mapHeight-1));
            box[2]=std::max(box[2],std::clamp((int)std::floor((r[2]-mapOrigin[0])/mapCell),0,mapWidth-1));
            box[3]=std::max(box[3],std::clamp((int)std::floor((r[3]-mapOrigin[1])/mapCell),0,mapHeight-1));
        }
        uint64_t t0=SDL_GetTicksNS();
        // The bounce goes into the copies, and what changed is read off them, on the light worker:
        // nothing on this thread reads the copies.
        std::function<void()> commit=preparedBounceCommitter();
        prepResult=std::make_shared<PreparedShot>();
        prepDiffed=false;
        lightWorkerPost([shot=prepResult,box0=box[0],box1=box[1],box2=box[2],box3=box[3],strips,commit] {
            if(commit) {lightTarget=&prepTarget;commit();lightTarget=nullptr;}
            const int b[4]={box0,box1,box2,box3};
            diffPrepared(*shot,b,strips);
            prepDiffed=true;
        });
        prepFrameMs[2]=std::max(prepFrameMs[2],(SDL_GetTicksNS()-t0)/1e6);
        prepStage=PrepStage::diff;
        return;
    }
    case PrepStage::diff: {
        if(!prepDiffed)return;
        prepStage=PrepStage::idle;
        if(stop) {prepSynced={};return;}   // what it read may have changed under it
        PreparedShot &shot=*prepResult;
        shot.id=*prepFixture.tiles.begin();shot.sources=prepFixture.sources;shot.now=prepNow;shot.stamp=prepBegan;
        shot.x=prepFixture.x;shot.y=prepFixture.y;shot.z=prepFixture.z;
        for(size_t n=0;n<prep.gridList.size();++n) {
            const StaticCell &a=staticGrid[prep.gridList[n]],&b=prep.gridResults[n];
            if(a.used!=b.used||a.low!=b.low||a.high!=b.high||std::memcmp(a.at,b.at,sizeof a.at)!=0)shot.grid.push_back({prep.gridList[n],b});
        }
        for(size_t s=0;s<prep.lost.size()&&s<prep.left.size();++s)
            if(prep.lost[s]>0)shot.dims.push_back({(int)s,(float)(prep.lost[s]/(prep.lost[s]+prep.left[s]))});
#ifdef DOOM_DEVTOOLS
        if(getenv("DERP_RELIGHT"))fprintf(stderr,"prepared shot at %.0f,%.0f,%.0f: %zu texels, %.1f MB, %.0f ms at tic %d (%zu kept); frame thread at most %.1f / %.1f / %.1f / %.1f ms.\n",shot.x,shot.y,shot.z,
            shot.bytes()/8,shot.bytes()/1048576.0,(SDL_GetTicksNS()-prepStarted)/1e6,leveltime,preparedShots.size()+1,prepFrameMs[0],prepFrameMs[1],prepFrameMs[2],prepFrameMs[3]);
#endif
        preparedShots.push_back(std::move(shot));
        prepResult.reset();
        return;
    }
    }
    // Idle: the nearest fixture not prepared, within the budget.
    if(!want||!quiet)return;
    if(!(fixturesStamp==now)) {uint64_t t1=SDL_GetTicksNS();listFixtures();prepFrameMs[3]=std::max(prepFrameMs[3],(SDL_GetTicksNS()-t1)/1e6);}
    const mobj_t *eye=players[displayplayer].mo;
    if(!eye||fixtures.empty())return;
    const float ex=units(eye->x),ey=units(eye->y),ez=units(eye->z)+41;
    auto distance=[&](float x,float y,float z){return std::hypot(x-ex,y-ey,z-ez);};
    size_t used=0;for(const PreparedShot &s:preparedShots)used+=s.bytes();
    const Fixture *best=nullptr;float bestDistance=prepareReach;
    for(const Fixture &f:fixtures) {
        float d=distance(f.x,f.y,f.z);
        if(d>=bestDistance)continue;
        bool done=false;
        for(const PreparedShot &s:preparedShots)if(s.id==*f.tiles.begin()) {done=true;break;}
        if(!done) {best=&f;bestDistance=d;}
    }
    if(!best)return;
    // Over the budget: the farthest kept one goes, if it lies farther than this one.
    while((used>=preparedBudget||preparedShots.size()>=preparedMost)&&!preparedShots.empty()) {
        auto far=std::max_element(preparedShots.begin(),preparedShots.end(),[&](const PreparedShot &a,const PreparedShot &b){return distance(a.x,a.y,a.z)<distance(b.x,b.y,b.z);});
        if(distance(far->x,far->y,far->z)<=bestDistance)return;
        used-=far->bytes();preparedShots.erase(far);
    }
#ifdef DOOM_DEVTOOLS
    prepStarted=SDL_GetTicksNS();
#endif
    uint64_t t0=SDL_GetTicksNS();
    if(startPrepared(*best))prepStage=PrepStage::job;
    prepFrameMs[0]=std::max(prepFrameMs[0],(SDL_GetTicksNS()-t0)/1e6);
}
// startRelight: the shot just taken (its merged lights, before (was) and after their remerge (now))
// as prepared; then it goes in and up in this frame, and true.
static bool applyPrepared(const PrepStamp &before,const std::vector<int> &sources,const std::vector<BakeLight> &now) {
    if(preparedShots.empty()||reBounceBusy())return false;   // a solve under way would put back light from before
    std::vector<int> sorted=sources;std::sort(sorted.begin(),sorted.end());
    for(size_t k=0;k<preparedShots.size();++k) {
        PreparedShot &shot=preparedShots[k];
        if(!(shot.stamp==before)||shot.sources!=sorted||shot.now.size()!=now.size())continue;
        bool same=true;
        for(size_t i=0;i<sources.size()&&same;++i) {
            size_t at=std::find(shot.sources.begin(),shot.sources.end(),sources[i])-shot.sources.begin();
            same=at<shot.now.size()&&sameLight(shot.now[at],now[i]);
        }
        if(!same)continue;
        auto put=[](std::vector<uint8_t> &cells,const Texels &t) {
            for(size_t n=0;n<t.at.size();++n)if(4*(size_t)t.at[n]+4<=cells.size())std::memcpy(&cells[4*(size_t)t.at[n]],&t.value[n],4);
        };
        put(liveFlatLightCells,shot.flat);put(liveWallBakeCells,shot.wall);
        put(liveFlatDirectionCells,shot.flatDirection);put(liveWallDirectionCells,shot.wallDirection);
        for(const auto &[i,cell]:shot.grid)if(i<staticGrid.size())staticGrid[i]=cell;
        for(const auto &[s,share]:shot.dims)dimShotSector(s,share);
        queueLightUpload(flatLightTexture,shot.flatRects,liveFlatLightCells,mapWidth*4);
        queueLightUpload(wallBakeTexture,shot.wallRects,liveWallBakeCells,atlasStride*4);
        if(!shot.flatDirection.at.empty())queueLightUpload(flatDirectionTexture,shot.flatDirectionRects,liveFlatDirectionCells,mapWidth*4);
        if(!shot.wallDirection.at.empty())queueLightUpload(wallDirectionTexture,shot.wallRects,liveWallDirectionCells,atlasStride*4);
        uploadLight(true);
        ++lightCellsSerial;
        preparedShots.clear();   // made from the light as it was
        return true;
    }
    return false;
}

// overdue: only the far places that have waited too long; what else waits stays for the next job.
static void startRelight(bool overdue) {
    Relight &job=relight;
    job.lights=settings.bakedLights&&lightBakeKey>=0&&lightBakeKey==lightBakeInputs()&&!bakeGrid.empty()&&wallBakeTexture&&flatLightTexture;
    job.only=job.lights&&bakeDirected&&wallDirectionTexture&&flatDirectionTexture; // Directions, as bakeLights made them.
    job.grid=job.lights&&staticSerial==lightBakeSerial&&!staticGrid.empty();
    job.sun=sunBaked&&sunLevel>0&&contactTexture&&wallBakeTexture;
    // Shot lamps go in a job of their own, ahead of doors and lifts, which wait for the next:
    // alone they are taken out the quick way (job.changed), in a frame or two. Sharing a job with
    // a lift that happened to be moving, they were baked out the exact way, most of a second.
    std::vector<int> pending;if(!overdue&&brokenPending.empty())pending.swap(relightPending);
    // Shot-out fixtures (shot_lights.cpp) leave the bake. The merged lights
    // they were part of take the tiles left of them (remergeBakeSource), or
    // go dark, at once, as things read them live, and here, with no re-bake
    // reading them; the light they baked changes with this re-bake, where
    // they reached before and where they reach now. Bounce, like the rest
    // the re-bake keeps, stays as the level started.
    std::set<SurfaceLightKey> broken;
    if(!overdue) {broken.insert(brokenPending.begin(),brokenPending.end());brokenPending.clear();}
    goneAt=broken.empty()?0:brokenAt;
    job.gone.clear();job.kept.clear();job.lost.clear();job.left.clear();
    std::vector<uint8_t> goneGroups,switchedGroups;   // the lamps' flicker groups (sourceGroups)
    const PrepStamp before=prepStamp();   // as the light stood: a prepared shot must have been made from it
    std::vector<int> goneSources;
    for(size_t n=0;n<bakeSourceKeys.size()&&n<bakeSources.size();++n) {
        if(!broken.count(bakeSourceKeys[n])||bakeSources[n].strength<=0)continue;
        job.gone.push_back(bakeSources[n]);goneGroups.push_back(n<sourceGroups.size()?sourceGroups[n]:0);goneSources.push_back((int)n);
        remergeBakeSource((int)n);++liveLightSerial;
        job.kept.push_back(bakeSources[n]);
    }
    // Switched lamps (lampLevelsFollow): each merged light whose tiles now shine otherwise is
    // re-baked like one that lost tiles, but its sector is not dimmed for it: the fill already
    // follows the level.
    std::vector<BakeLight> switched;
    if(switchDue&&job.lights&&!overdue)for(size_t n=0;n<bakeSourceKeys.size()&&n<bakeSources.size();++n) {
        if(bakeSourceKeys[n][0]<2)continue;   // not a merged lamp
        BakeLight was=bakeSources[n];
        remergeBakeSource((int)n);
        BakeLight &now=bakeSources[n];
        if(now.strength>0) {now.tiles=was.tiles;now.tileCount=was.tileCount;}
        if(std::abs(now.strength-was.strength)<=0.02f*std::max(now.strength,was.strength)) {now=was;continue;}
        ++liveLightSerial;
        switched.push_back(was);switched.push_back(now);switchedGroups.push_back(n<sourceGroups.size()?sourceGroups[n]:0);
    }
    if(!overdue)switchDue=false;
    // A shot prepared before it happened (tickPrepared): in and up now, on the frame it broke.
    if(!job.gone.empty()&&pending.empty()&&switched.empty()&&settings.instantShots&&applyPrepared(before,goneSources,job.kept)) {
        relightPending.insert(relightPending.end(),pending.begin(),pending.end());
        return;
    }
    if(!job.lights&&!job.sun)return;
    job.cells.assign((size_t)mapWidth*mapHeight,0);job.strips.assign(bakeStrips.size(),0);
    job.gridCells.assign(job.grid?staticGrid.size():0,0);
    for(int *b:{job.lightBounds,job.sunBounds}) {b[0]=b[1]=INT_MAX;b[2]=b[3]=-1;}
    // A light that reaches far (a tail, or whole-level lighting: BakeLight::tail) is re-baked in
    // two goes: now what lies within nearRelight of it, where nearly all of its light falls, and
    // the rest of its reach in a job of its own once nothing else waits (farPending). All at once
    // a shot-out lamp took a second to go dark, the room standing lit meanwhile; so the lamp goes
    // out at once and its faint far light follows.
    auto markLight=[&](const BakeLight &light,bool lamp=false) {   // lamp: one that changed (addFar)
        float reach=bakeReach(light),near=std::min(reach,nearRelight);
        markRelight(relight,light.x-near,light.y-near,light.x+near,light.y+near,1);
        if(reach>near)addFar({light.x,light.y,reach},lamp);
    };
    job.lamps.was=job.gone;job.lamps.now=job.kept;job.lamps.group=goneGroups;
    for(size_t n=0;n+1<switched.size();n+=2) {
        job.lamps.was.push_back(switched[n]);job.lamps.now.push_back(switched[n+1]);job.lamps.group.push_back(switchedGroups[n/2]);
    }
    job.changed=job.lights&&settings.lightModel==1&&pending.empty()&&!job.lamps.was.empty();
    // Whole-level lighting: a shot lamp is baked out the exact way in one job, all of its reach,
    // and shown only together with the bounce solved again from it (relightMovingSectors): one
    // change. The quick way below showed its own light gone first, then a second or so later the
    // exact light, the directions and the bounce, a second change over more of the room than the
    // first (PLAN-destructible-lights.md, step 1). DERP_SHOTQUICK: the quick way, to compare.
    job.exactShot=job.changed&&switched.empty()&&!job.gone.empty();
#ifdef DOOM_DEVTOOLS
    if(getenv("DERP_SHOTQUICK"))job.exactShot=false;
#endif
    if(job.exactShot)job.changed=false;
    if(job.lights)for(const auto *lamps:{&job.lamps.was,&job.lamps.now})for(const BakeLight &light:*lamps) {
        if(light.strength<=0)continue;
        if(job.exactShot) {
            float reach=bakeReach(light),near=std::min(reach,nearRelight);
            markRelight(relight,light.x-reach,light.y-reach,light.x+reach,light.y+reach,1,false);
            markRelight(relight,light.x-near,light.y-near,light.x+near,light.y+near,1);   // and the sprites' grid near it
            continue;
        }
        if(!job.changed) {markLight(light,true);continue;}
        // All its reach at once, the quick way, and the sprites' grid near it. The exact bake
        // of all its reach follows when all is quiet: the quick way leaves a little light
        // behind (rounding, texels at the top of their range, far texels traced from one
        // point), and a room whose lamps are all shot out has to end up black.
        float reach=bakeReach(light),near=std::min(reach,nearRelight);
        markRelight(relight,light.x-reach,light.y-reach,light.x+reach,light.y+reach,1,false);
        markRelight(relight,light.x-near,light.y-near,light.x+near,light.y+near,1);
        addFar({light.x,light.y,reach},true);
    }
    for(const auto *changed:{&job.gone,&switched})for(const BakeLight &light:*changed)
        markBounce(light);
    // The far parts, when this job has nothing nearer to do.
    job.far=job.lights&&broken.empty()&&pending.empty()&&switched.empty();
    job.cancel=false;job.farPlaces.clear();
    if(job.far) {
        for(const auto &f:farPending)markRelight(relight,f[0]-f[2],f[1]-f[2],f[0]+f[2],f[1]+f[2],1);
        job.farPlaces.swap(farPending);
    } else quietSince=leveltime;
    // Doors and the like, with lights that reach far: the quick way (bake_moved.cpp).
    job.moved=job.lights&&settings.lightModel==1&&broken.empty()&&switched.empty()&&!pending.empty()&&litHeights.size()==bakeMap.sectors.size();
    job.boxes.clear();job.movedSector.assign(numsectors,0);
    const float elevation=sunElevation;
    for(int sector:pending) {
        const BakeSector &s=bakeMap.sectors[sector];
        float x0=1e9f,y0=1e9f,x1=-1e9f,y1=-1e9f,low=s.floor,high=s.ceiling;
        for(int index:s.lines) {
            const BakeLine &line=bakeMap.lines[index];
            x0=std::min({x0,line.ax,line.bx});y0=std::min({y0,line.ay,line.by});
            x1=std::max({x1,line.ax,line.bx});y1=std::max({y1,line.ay,line.by});
            int other=line.front==sector?line.back:line.front;
            if(other>=0) {low=std::min(low,bakeMap.sectors[other].floor);high=std::max(high,bakeMap.sectors[other].ceiling);}
        }
        if(x1<x0)continue;
        if(job.lights) {
            // A light that reaches far (whole-level lighting) changes where the sector lets it
            // through, not round itself: what is re-baked is what lies near the sector.
            float far=0;
            for(const BakeLight &light:bakeSources) {
                float dx=std::max({x0-light.x,0.0f,light.x-x1}),dy=std::max({y0-light.y,0.0f,light.y-y1});
                float reach=bakeReach(light);
                if(dx*dx+dy*dy>=reach*reach)continue;
                if(reach<=nearRelight)markLight(light);
                else far=std::max(far,reach);
            }
            // A pool (nukage, lava) whose light reaches the sector counts as such a light. Its whole
            // reach at once was E1M7's 16-unit door re-baking 190,000 cells, seconds on the card.
            for(size_t a=0;a<bakeAreas.size();++a) {
                const BakeArea &area=bakeAreas[a];
                if(area.high[0]+area.radius<x0||area.low[0]-area.radius>x1||area.high[1]+area.radius<y0||area.low[1]-area.radius>y1)continue;
                if(!lightReachesSector((int)(bakeSources.size()+a),sector))continue;   // its light can't get there (light_rooms.cpp)
                far=std::max(far,area.radius);
            }
            if(x1-x0>doorMost||y1-y0>doorMost)job.moved=false;
            job.boxes.push_back({x0,y0,x1,y1});job.movedSector[sector]=1;
            if(far>0) {
                // As Classic re-bakes a door: what lies near it, at once. The quick way
                // (bake_moved.cpp) where the sector is narrow; the exact bake of the same
                // place follows when all is quiet and takes up its rounding.
                float half=std::max(x1-x0,y1-y0)*0.5f;
                markRelight(relight,x0-nearRelight,y0-nearRelight,x1+nearRelight,y1+nearRelight,1);
                addFar({(x0+x1)*0.5f,(y0+y1)*0.5f,half+nearRelight},false);
            }
        }
        if(job.sun) {
            // Sun through the sector reaches floors and walls this far away from the sun.
            float reach=std::min((high-low)/std::tan(elevation)+16,1024.0f);
            float sx=-sunAzimuth[0]*reach,sy=-sunAzimuth[1]*reach;
            markRelight(relight,x0+std::min(sx,0.0f),y0+std::min(sy,0.0f),x1+std::max(sx,0.0f),y1+std::max(sy,0.0f),2);
        }
    }
    job.sectors=pending;
    if(job.lightBounds[2]<0&&job.sunBounds[2]<0)return;
    // Lamps changed: a bounce solve in flight read their old light, and its places wait again. A
    // door's re-light leaves it be: the bounce does not follow doors, and stopping it for each
    // one never let it finish beside a lift that never rests.
    if(!job.lamps.was.empty())cancelRebounce();
    // Assigned into the job's own map, which keeps its storage from the last job: a copy, no allocation.
    job.map=bakeMap;
    if(job.changed)litCellsAround(job.lightBounds,job.litBefore);
    // moved was dropped by a wide sector after some were marked the quick way: then all of it is baked whole.
    if(job.moved) {
        job.before=job.map;
        for(size_t s=0;s<litHeights.size();++s) {job.before.sectors[s].floor=litHeights[s][0];job.before.sectors[s].ceiling=litHeights[s][1];}
    }
    if(job.lights)for(int sector:pending)if((size_t)sector<litHeights.size())litHeights[sector]={job.map.sectors[sector].floor,job.map.sectors[sector].ceiling};
    for(int c=0;c<3;++c)job.sunDirection[c]=sunDirection[c];
    job.sunSpread=sunSpread;
    const int *sb=job.sunBounds;
    job.sunCells.assign(sb[2]>=0?(size_t)(sb[2]-sb[0]+1)*(sb[3]-sb[1]+1):0,-1);
    job.gridList.clear();
    for(size_t i=0;i<job.gridCells.size();++i)if(job.gridCells[i])job.gridList.push_back(i);
    job.gridResults.assign(job.gridList.size(),{});
    job.done=false;job.running=true;
    lightWorkerPost([]{runRelight(relight);});
}
// The re-bake itself, on the light worker, with a small pool (relightFor) so
// the game keeps its cores. It reads the job and the bake's layout, and
// writes the light's cells and the job's results; the frame thread leaves
// those alone until done.
// The cells' and strips' work on the graphics card (gpu_lights.cpp, gpu_bakes.cpp): listed as
// the loops in runRelight would bake them (whole, round a moved door, for changed lamps, the
// sun), traced there, and put in here. Nothing is written unless all of it ran: false, and the
// loops bake it here instead.
static bool relightOnCard(Relight &job) {
    if(!gpuRelightOn()||job.cancel)return false;
    const uint64_t listed=SDL_GetTicksNS();
    static const float up[3]={0,0,1},down[3]={0,0,-1};
    const size_t layer=(size_t)mapWidth*mapHeight;
    int y0=std::min(job.lightBounds[1],job.sunBounds[1]),y1=std::max(job.lightBounds[3],job.sunBounds[3]);
    const int *sb=job.sunBounds;
    std::vector<BakeSample> whole,moved,changed;
    std::vector<StaticTarget> wholeTargets;
    std::vector<uint8_t*> movedOut;
    std::vector<std::pair<uint8_t*,uint8_t*>> changedOut;   // the light and the flicker mark
    std::vector<std::array<float,4>> sunPoints;
    std::vector<int16_t*> sunCellOut;   // per sun point on a floor; then the strips' texels
    struct SunStrip { size_t first,count; float facing; const BakeStrip *strip; };
    std::vector<SunStrip> sunStrips;
    auto flat=[&](BakeSample &s,int sector,size_t i,bool ceiling,const BakeMap &map) {
        const BakeSector &b=map.sectors[sector];
        s={mapOrigin[0]+(i%mapWidth+0.5f)*mapCell,mapOrigin[1]+(i/mapWidth+0.5f)*mapCell,ceiling?b.ceiling-1:b.floor+1,sector,
           {0,0,ceiling?-1.0f:1.0f},0,false};
    };
    for(int y=y0;y<=y1&&y1>=y0;++y)for(int x=0;x<mapWidth;++x) {
        size_t i=(size_t)y*mapWidth+x;uint8_t mark=job.cells[i];
        if(!mark)continue;
        int sector=seamCells[i].own;
        if(mark&2) {
            int16_t *to=&job.sunCells[(size_t)(y-sb[1])*(sb[2]-sb[0]+1)+(x-sb[0])];
            if(sector==noSector||sectors[sector].floorpic==skyflatnum)*to=-1;
            else {
                sunPoints.push_back({mapOrigin[0]+(x+0.5f)*mapCell,mapOrigin[1]+(y+0.5f)*mapCell,job.map.sectors[sector].floor+1,(float)sector});
                sunCellOut.push_back(to);
            }
        }
        if(!(mark&1)||sector==noSector)continue;
        if(job.moved&&!job.movedSector[sector]) {
            for(int ceiling=0;ceiling<2;++ceiling) {
                if((ceiling?sectors[sector].ceilingpic:sectors[sector].floorpic)==skyflatnum)continue;
                BakeSample s;flat(s,sector,i,ceiling!=0,job.map);moved.push_back(s);
                movedOut.push_back(flatCell(ceiling?ceilingLight:floorLight,i));
            }
        } else if(job.moved)staticItemsOfCell(job.map,i,false,whole,wholeTargets);
        else if(job.changed) {
            for(int ceiling=0;ceiling<2;++ceiling) {
                if((ceiling?sectors[sector].ceilingpic:sectors[sector].floorpic)==skyflatnum)continue;
                BakeSample s;flat(s,sector,i,ceiling!=0,job.map);changed.push_back(s);
                changedOut.push_back({flatCell(ceiling?ceilingLight:floorLight,i),job.only?&flatDirectionCells[4*(ceiling*layer+i)+3]:nullptr});
            }
        } else staticItemsOfCell(job.map,i,job.only,whole,wholeTargets);
    }
    (void)up;(void)down;
    std::vector<int> &strips=job.stripList;
    strips.clear();
    for(int index:bakeOrder)if(job.strips[index])strips.push_back(index);
    for(int index:strips) {
        const BakeStrip &strip=bakeStrips[index];
        uint8_t mark=job.strips[index];
        const float normal[3]={strip.dy,-strip.dx,0};
        if(mark&1) {
            bool all=!job.moved||(strip.sector>=0&&strip.sector<(int)job.movedSector.size()&&job.movedSector[strip.sector]);
            if(job.moved&&!all) {
                float bx=strip.ax+strip.dx*strip.length,by=strip.ay+strip.dy*strip.length;
                for(const auto &box:job.boxes)
                    if(std::max(strip.ax,bx)>=box[0]-1&&std::min(strip.ax,bx)<=box[2]+1&&std::max(strip.ay,by)>=box[1]-1&&std::min(strip.ay,by)<=box[3]+1)all=true;
            }
            if(job.moved&&!all)forStripTexels(strip,[&](size_t i,float x,float y,float z) {
                moved.push_back({x,y,z,strip.sector,{normal[0],normal[1],normal[2]},0,true});movedOut.push_back(&wallBakeCells[4*i]);
            });
            else if(job.moved)staticItemsOfStrip(strip,false,whole,wholeTargets);
            else if(job.changed)forStripTexels(strip,[&](size_t i,float x,float y,float z) {
                changed.push_back({x,y,z,strip.sector,{normal[0],normal[1],normal[2]},0,true});
                changedOut.push_back({&wallBakeCells[4*i],job.only?&wallDirectionCells[4*i+3]:nullptr});
            });
            else staticItemsOfStrip(strip,job.only,whole,wholeTargets);
        }
        if(mark&2) {
            // sunStrip: walls fade with the angle to the sun.
            float length=std::hypot(job.sunDirection[0],job.sunDirection[1]),sunX=job.sunDirection[0]/length,sunY=job.sunDirection[1]/length;
            SunStrip s={sunPoints.size(),0,std::clamp((strip.dy*sunX-strip.dx*sunY)*1.5f,0.0f,1.0f),&strip};
            if(s.facing>0)forStripTexels(strip,[&](size_t,float x,float y,float z) {sunPoints.push_back({x,y,z,(float)strip.sector});});
            s.count=sunPoints.size()-s.first;
            sunStrips.push_back(s);
        }
    }
    // What changes first, written nowhere yet; the whole light last, which writes as it lands.
    std::vector<std::array<float,4>> movedBy;std::vector<std::array<float,20>> changedBy;std::vector<uint8_t> open;
    uint64_t t[5];t[0]=SDL_GetTicksNS();
    if(!moved.empty()&&!gpuGatherMoved(job.before,job.map,job.boxes,moved,movedBy))return false;
    t[1]=SDL_GetTicksNS();
    if(!changed.empty()&&!gpuGatherChanged(job.map,job.lamps,changed,changedBy))return false;
    t[2]=SDL_GetTicksNS();
    if(!sunPoints.empty()&&!gpuSunVisibility(sunPoints,job.sunDirection,job.sunSpread,open,job.map))return false;
    t[3]=SDL_GetTicksNS();
    if(!whole.empty()&&!gpuGatherStatic(job.map,whole,wholeTargets,job.only))return false;
    t[4]=SDL_GetTicksNS();
    static const bool timing=getenv("DERP_GPUTIMES")!=nullptr;
    if(timing)fprintf(stderr,"GPU relight: listed in %.1f ms; door %zu items %.1f ms, lamps %zu %.1f ms, sun %zu %.1f ms, whole %zu %.1f ms.\n",
        (t[0]-listed)/1e6,moved.size(),(t[1]-t[0])/1e6,changed.size(),(t[2]-t[1])/1e6,sunPoints.size(),(t[3]-t[2])/1e6,whole.size(),(t[4]-t[3])/1e6);
    // gatherMoved's end.
    for(size_t k=0;k<moved.size();++k) {
        if(movedBy[k][3]==0)continue;
        uint8_t *out=movedOut[k];
        for(int c=0;c<3;++c)out[c]=(uint8_t)std::clamp(out[c]+std::lround(movedBy[k][c]*0.5f*255),0L,255L);
    }
    // gatherChanged's end: the light, then the flicker mark.
    auto luma=[](const float rgb[3]) {return 0.299*rgb[0]+0.587*rgb[1]+0.114*rgb[2];};
    for(size_t k=0;k<changed.size();++k) {
        const auto &d=changedBy[k];
        if(d[19]==0)continue;
        uint8_t *out=changedOut[k].first,*mark=changedOut[k].second;
        float held[3]={out[0]/255.0f*2,out[1]/255.0f*2,out[2]/255.0f*2};
        for(int c=0;c<3;++c)out[c]=(uint8_t)std::clamp(out[c]+std::lround(d[c]*0.5f*255),0L,255L);
        if(!mark)continue;
        int group=*mark>>4;
        double mine=group?luma(held)*(*mark&15)/15.0:0;
        if(!group) {
            if(*mark)continue;   // a pool's sheen share
            for(int g=1;g<flickerGroups;++g)if(d[3+g]>0&&d[3+g]>d[3+group])group=g;
            if(!group)continue;
        }
        float left[3]={out[0]/255.0f*2,out[1]/255.0f*2,out[2]/255.0f*2};
        double all=luma(left);
        long share=all>0?std::lround(std::clamp((mine+d[3+group])/all,0.0,1.0)*15):0;
        *mark=share>0?(uint8_t)(group*16+share):0;
    }
    // sunCell's and sunStrip's ends.
    for(size_t k=0;k<sunCellOut.size();++k)*sunCellOut[k]=(int16_t)open[k];
    for(const SunStrip &s:sunStrips) {
        size_t k=s.first;
        forStripTexels(*s.strip,[&](size_t i,float,float,float) {
            wallBakeCells[4*i+3]=(uint8_t)std::lround((s.facing>0?s.facing*(open[k++]/255.0f):0)*255);
        });
    }
    return true;
}
static void runRelight(Relight &job) {
    uint64_t began=SDL_GetTicksNS();(void)began;
    int y0=std::min(job.lightBounds[1],job.sunBounds[1]),y1=std::max(job.lightBounds[3],job.sunBounds[3]);
    const int *sb=job.sunBounds;
    const bool card=relightOnCard(job);
    if(y1>=y0&&!card)relightFor(y1-y0+1,[&](int row) {
        int y=y0+row;
        if(job.cancel)return;
        for(int x=0;x<mapWidth;++x) {
            size_t i=(size_t)y*mapWidth+x;uint8_t mark=job.cells[i];
            if((mark&1)&&job.moved)lightCellMoved(job.before,job.map,job.boxes,job.movedSector,i);
            else if((mark&1)&&job.changed)lightCellChanged(job.map,job.lamps,i,job.only);
            else if(mark&1)lightCell(job.map,i,job.only);
            if(mark&2)job.sunCells[(size_t)(y-sb[1])*(sb[2]-sb[0]+1)+(x-sb[0])]=(int16_t)sunCell(job.map,i,job.sunDirection,job.sunSpread);
        }
    });
    std::vector<int> &strips=job.stripList;
    strips.clear();
    for(int index:bakeOrder)if(job.strips[index])strips.push_back(index);
    if(!card)relightFor((int)strips.size(),[&](int n) {
        const BakeStrip &strip=bakeStrips[strips[n]];
        if(job.cancel)return;
        if((job.strips[strips[n]]&1)&&job.moved)lightStripMoved(job.before,job.map,job.boxes,job.movedSector,strip);
        else if((job.strips[strips[n]]&1)&&job.changed)lightStripChanged(job.map,job.lamps,strip,job.only);
        else if(job.strips[strips[n]]&1)lightStrip(job.map,strip,job.only);
        if(job.strips[strips[n]]&2)sunStrip(job.map,strip,job.sunDirection,job.sunSpread);
    });
    relightFor((int)job.gridList.size(),[&](int n) {
        size_t i=job.gridList[n];
        if(job.cancel)return;
        job.gridResults[n]=staticCellAt(job.map,(int)(i%staticWidth),(int)(i/staticWidth));
    });
    if(job.lights&&!job.lamps.was.empty())measureLoss(job);
#ifdef DOOM_DEVTOOLS
    if(getenv("DERP_RELIGHT")) {
        size_t cells=0;for(uint8_t c:job.cells)cells+=c&1;
        fprintf(stderr,"3D relight: %zu cells, %zu strips in %.0f ms%s%s%s%s%s at tic %d.\n",cells,strips.size(),(SDL_GetTicksNS()-began)/1e6,job.far?" (far)":"",
                job.changed?" (quick)":job.prepared?" (prepared)":job.exactShot?" (shot, exact)":"",job.moved?" (door)":"",job.cancel?" (stopped)":"",card?" on the graphics card":"",leveltime);
        if(goneAt&&!job.prepared)fprintf(stderr,"3D relight: a shot lamp's light baked out %.0f ms after the shot.\n",(SDL_GetTicksNS()-goneAt)/1e6);
    }
#endif
    job.done=true;
}
// A finished re-bake into the level's light, on the frame thread: its
// uploads are queued (uploadLight), not made here.
static void applyRelight() {
    Relight &job=relight;
    if(job.cancel) {   // a far job that gave way: its places wait again, the sprites' grid keeps what it had
        farPending.insert(farPending.end(),job.farPlaces.begin(),job.farPlaces.end());
        job.gridList.clear();
        // A door's job that gave way to a shot: its sectors wait again, and their job brings what
        // this one baked to the GPU with its own; the shot lamp's job does not wait for uploads.
        if(!job.far) {
            for(int sector:job.sectors)if(std::find(relightPending.begin(),relightPending.end(),sector)==relightPending.end())relightPending.push_back(sector);
            return;
        }
    } else if(job.far)farLamps=false;   // the exact light is in: the bounce may follow
    const int *lb=job.lightBounds,*sb=job.sunBounds;
    if(job.lights)for(size_t n=0;n<job.gridList.size();++n)staticGrid[job.gridList[n]]=job.gridResults[n];
    // Queued in the order they go up: the lamp light on floors and walls, the
    // sun, then the directions (the bump lobes, the least seen of a frame late).
    std::vector<std::array<int,4>> rects,directions;
    for(size_t index=0;index<job.strips.size();++index) {
        if(!job.strips[index])continue;
        const BakeStrip &strip=bakeStrips[index];
        rects.push_back({strip.x,strip.y,strip.width,strip.height});
        if(job.only&&(job.strips[index]&1))directions.push_back(rects.back());
    }
    if(job.lights&&lb[2]>=0) {
        int w=lb[2]-lb[0]+1,h=lb[3]-lb[1]+1;
        queueLightUpload(flatLightTexture,{{lb[0],lb[1]+floorLight*mapHeight,w,h},{lb[0],lb[1]+ceilingLight*mapHeight,w,h}},flatLightCells,mapWidth*4);
    }
    queueLightUpload(wallBakeTexture,rects,wallBakeCells,atlasStride*4);
    if(job.lights&&lb[2]>=0) {
        if(!job.gone.empty())for(size_t s=0;s<job.lost.size();++s)
            if(job.lost[s]>0)dimShotSector((int)s,(float)(job.lost[s]/(job.lost[s]+job.left[s])));
        // Whole-level lighting: the bounce goes down at once where the lamp light round it
        // went (dimBounce); the exact bounce follows. Its uploads join the two above.
        if(job.changed)dimBounce(job.litBefore,lb);
        job.lost.clear();job.left.clear();
    }
    if(job.sun&&sb[2]>=0) {
        int w=sb[2]-sb[0]+1,h=sb[3]-sb[1]+1;
        for(int y=0;y<h;++y)for(int x=0;x<w;++x) {
            int16_t visible=job.sunCells[(size_t)y*w+x];
            if(visible>=0)contactCells[4*((size_t)(sb[1]+y)*mapWidth+sb[0]+x)+2]=(uint8_t)visible;
        }
        queueLightUpload(contactTexture,{{sb[0],sb[1],w,h}},contactCells,mapWidth*4);
    }
    if(job.lights&&lb[2]>=0&&job.only) {
        int w=lb[2]-lb[0]+1,h=lb[3]-lb[1]+1;
        queueLightUpload(flatDirectionTexture,{{lb[0],lb[1],w,h},{lb[0],lb[1]+mapHeight,w,h}},flatDirectionCells,mapWidth*4);
    }
    queueLightUpload(wallDirectionTexture,directions,wallDirectionCells,atlasStride*4);
    ++lightCellsSerial;
}
// Waits for the light worker (this re-bake, and a cancelled bounce solve still
// winding down: a full bake rewrites what they read) and applies what it
// baked, all of its uploads at once; without apply drops it, uploads too
// (the level's textures are about to go).
void finishRelight(bool apply) {
    lightWorkerWait();
    if(relight.running) {
        relight.running=relight.held=false;
        if(apply)applyRelight();
    }
    if(apply)uploadLight(true);
    else lightUploads.clear();
}
// Called each frame before geometry: follows sector heights into bakeMap,
// applies a finished re-bake and starts the next. Never waits for the worker.
void relightMovingSectors() {
#ifdef DOOM_DEVTOOLS
    if(footShotAt)++footFrames;   // this frame's number after the shot's (0, the frame it went in)
#endif
    uploadLight(false);
    if(relight.running&&relight.done&&!relight.held&&relight.exactShot&&!relight.cancel) {relight.held=true;relight.heldSince=leveltime;}
    if(relight.running&&relight.done&&!relight.held) {
        relight.running=false;applyRelight();
#ifdef DOOM_DEVTOOLS
        // DERP_RELIGHTDUMP=<file>: the light as this re-bake left it (floors, walls, directions), to hold one side's against the other's.
        if(const char *dump=getenv("DERP_RELIGHTDUMP"))if(FILE *f=fopen(dump,"wb")) {
            for(const auto *cells:{&flatLightCells,&wallBakeCells,&flatDirectionCells,&wallDirectionCells})fwrite(cells->data(),1,cells->size(),f);
            fclose(f);
        }
#endif
    }
    if(bakeMap.sectors.size()!=(size_t)numsectors)return;
    // Only a sector the game has let go of: it rests for good, at its exact heights. bakeMap follows
    // it whatever the switch says (the weather's drifts and the sun read it); the switch decides
    // whether its light is baked again.
    for(int i=0;i<numsectors;++i) {
        if(sectors[i].specialdata)continue;
        BakeSector &s=bakeMap.sectors[i];
        float floor=units(sectors[i].floorheight),ceiling=units(sectors[i].ceilingheight);
        if(floor==s.floor&&ceiling==s.ceiling)continue;
        s.floor=floor;s.ceiling=ceiling;++liveLightSerial;
        if(settings.movingRelight&&std::find(relightPending.begin(),relightPending.end(),i)==relightPending.end())relightPending.push_back(i);
    }
    if(switchSeen)switchDue=true;
    switchSeen=lampLevelsFollow();
    bool fresh=!relightPending.empty()||!brokenPending.empty()||switchDue;
    // Far places that have waited too long (farLongest) go ahead of doors and lifts, and their job
    // gives way to a new shot only, which starts their wait again.
    bool overdue=farLamps&&!farPending.empty()&&std::abs(leveltime-farSince)>=farLongest&&brokenPending.empty();
    if(relight.running&&relight.far&&(relight.overdue?!brokenPending.empty():fresh)) {
        relight.cancel=true;
        if(relight.overdue)farSince=leveltime;
    }
    // The far jobs wait until nothing has changed for a moment. The bounce is solved again after
    // them, from the exact light; until then it stands as dimBounce left it.
    // A shot lamp does not wait for a door's or lift's exact re-bake either (most of a second
    // for a wide sector): that gives way and is baked again after. Not the quick ways (moved,
    // changed): they add to what the texels hold, and are done in a frame or two anyway.
    if(relight.running&&!relight.far&&!relight.moved&&!relight.changed&&relight.lamps.was.empty()&&!brokenPending.empty())relight.cancel=true;
    // Nor for a bounce solve: a door's job queued behind one kept the worker for its whole run,
    // and the shot behind both. Its places wait again (the shot marks its own besides).
    if(!brokenPending.empty())cancelRebounce();
    bool farDue=!farPending.empty()&&(overdue||std::abs(leveltime-quietSince)>=farQuiet);
    // Not while uploads wait: they read the cells the next re-bake writes.
    bool clear=!relight.running&&lightUploads.empty();
    // The bounce first: its solve copies the light here, on this thread, so it starts only where no
    // re-bake runs, and beside a lift that never rests the next one starts in this very frame. It
    // waits for a shot lamp's own re-bakes (the quick one, then the exact one); doors do not hold it.
    // A held shot (startRelight's exactShot) lets the bounce go ahead of it: its light is in the cells.
    bool bounceClear=(!relight.running||relight.held)&&lightUploads.empty();
    reBounceTick(!bounceClear||!brokenPending.empty()||farLamps||switchDue);
    // Then the held shot goes up with that bounce, all of it in this frame. A new shot does not wait
    // for its bounce, nor anything for one that never comes (a second at most).
    if(relight.held&&(!reBounceBusy()||!brokenPending.empty()||std::abs(leveltime-relight.heldSince)>35)) {
        relight.held=relight.running=false;
        applyRelight();uploadLight(true);
    }
    if(clear&&(fresh||farDue)) {relight.overdue=overdue;startRelight(overdue);}
    tickPrepared();
#ifdef DOOM_DEVTOOLS
    // DERP_REBOUNCE=x,y: the bounce round that place is solved again at tic 20, to time it and to
    // see that a place solved again looks as it did.
    // DERP_RELIGHT=x,y: the sector there counts as moved at tics 20 and 60, to time its re-light.
    static int relit=-1;
    if(const char *at=getenv("DERP_RELIGHT");at&&(leveltime==20||leveltime==60)&&relit!=leveltime) {
        float x=0,y=0;relit=leveltime;
        if(sscanf(at,"%f,%f",&x,&y)==2)relightPending.push_back(int(R_PointInSubsector((fixed_t)(x*FRACUNIT),(fixed_t)(y*FRACUNIT))->sector-sectors));
    }
    // DERP_OPEN=x,y: the door there stands open from tic 20 (no thinker: it stays), to see its light.
    static int opened=-1;
    if(const char *at=getenv("DERP_OPEN");at&&leveltime>=20&&opened!=(int)r_levelserial) {
        float x=0,y=0,top=0;opened=(int)r_levelserial;
        if(sscanf(at,"%f,%f",&x,&y)==2) {
            sector_t *door=R_PointInSubsector((fixed_t)(x*FRACUNIT),(fixed_t)(y*FRACUNIT))->sector;
            if(Rooms_Door(int(door-sectors),&top))door->ceilingheight=(fixed_t)(top*FRACUNIT);
        }
    }
    // DERP_SWITCH=x,y: the room there goes to level 80 at tic 30, as a light switch would leave it.
    static int switched=-1;
    if(const char *at=getenv("DERP_SWITCH");at&&leveltime>=30&&switched!=(int)r_levelserial) {
        float x=0,y=0;switched=(int)r_levelserial;
        if(sscanf(at,"%f,%f",&x,&y)==2) {
            int room=Rooms_Of(int(R_PointInSubsector((fixed_t)(x*FRACUNIT),(fixed_t)(y*FRACUNIT))->sector-sectors));
            for(int s=0;s<numsectors;++s)if(Rooms_Of(s)==room)sectors[s].lightlevel=80;
        }
    }
    // DERP_BLAST=x,y,z,radius: the lamps within that radius are blown out at tic 30.
    static int blasted=-1;
    if(const char *at=getenv("DERP_BLAST");at&&leveltime>=30&&blasted!=(int)r_levelserial) {
        float x=0,y=0,z=0,radius=0;blasted=(int)r_levelserial;
        if(sscanf(at,"%f,%f,%f,%f",&x,&y,&z,&radius)==4)blastLights(x,y,z,radius);
    }
    // DERP_BUSY=x,y: the sector there counts as moved every 6 tics from tic 20, as a lift that never rests.
    if(const char *at=getenv("DERP_BUSY");at&&leveltime>=20&&leveltime%6==0) {
        float x=0,y=0;
        if(sscanf(at,"%f,%f",&x,&y)==2) {
            int s=int(R_PointInSubsector((fixed_t)(x*FRACUNIT),(fixed_t)(y*FRACUNIT))->sector-sectors);
            if(std::find(relightPending.begin(),relightPending.end(),s)==relightPending.end())relightPending.push_back(s);
        }
    }
    // DERP_LIFT=x,y[,tic]: the lift there (its tag's sectors) goes up and down for good from that tic (30), as the
    // game's own perpetual lift: to time frames while a lift moves.
    static int lifted=-1;
    if(const char *at=getenv("DERP_LIFT");at&&lifted!=(int)r_levelserial) {
        float x=0,y=0;int from=30;
        if(sscanf(at,"%f,%f,%d",&x,&y,&from)>=2&&leveltime>=from) {
            lifted=(int)r_levelserial;
            sector_t *sector=R_PointInSubsector((fixed_t)(x*FRACUNIT),(fixed_t)(y*FRACUNIT))->sector;
            int started=Game_StartLift(sector->tag);
            fprintf(stderr,"DERP_LIFT: sector %d tag %d at tic %d: %s.\n",int(sector-sectors),sector->tag,leveltime,started?"moving":"not started");
        }
    }
    // DERP_SHOOT=count,step,seed[,from]: from tic 30 (or from) a lamp picked by the seed is shot out every
    // step tics, count of them, one fixture at a time as in play. A negative seed takes the lit tiles nearest
    // the eye instead, -1 the nearest (a prepared shot's case: tickPrepared prepares those first).
    static int shootLevel=-1,shots=0;
    if(shootLevel!=(int)r_levelserial) {shootLevel=(int)r_levelserial;shots=0;}
    if(const char *at=getenv("DERP_SHOOT")) {
        int count=0,step=20,seed=1,from=30;
        if(sscanf(at,"%d,%d,%d,%d",&count,&step,&seed,&from)>=1&&shots<count&&leveltime>=from+shots*std::max(1,step)&&!surfaceLights.empty()) {
            size_t before=lampsShotCount();
            std::vector<size_t> nearest;
            if(seed<0&&players[displayplayer].mo) {
                const mobj_t *eye=players[displayplayer].mo;
                for(size_t n=0;n<surfaceLights.size();++n)if(surfaceLights[n].strength>0&&!tileShotOut(surfaceLights[n].key))nearest.push_back(n);
                auto away=[&](size_t n){const SurfaceLight &l=surfaceLights[n];return std::hypot(l.x-units(eye->x),l.y-units(eye->y),l.z-units(eye->z)-41);};
                std::sort(nearest.begin(),nearest.end(),[&](size_t a,size_t b){return away(a)<away(b);});
            }
            for(int tries=0;tries<64&&lampsShotCount()==before;++tries) {
                if(seed<0&&(size_t)(-seed-1+tries)>=nearest.size())break;
                const SurfaceLight &tile=seed<0?surfaceLights[nearest[(size_t)(-seed-1+tries)]]:
                    surfaceLights[(size_t)(hashUnit(shots,tries,(uint32_t)seed)*surfaceLights.size())%surfaceLights.size()];
                if(tile.strength<=0)continue;
                blastLights(tile.x,tile.y,tile.z,12);
                if(lampsShotCount()!=before)fprintf(stderr,"shot %d at tic %d: %.0f,%.0f,%.0f, %zu tiles.\n",shots,leveltime,tile.x,tile.y,tile.z,lampsShotCount()-before);
                if(lampsShotCount()!=before&&!footShotAt&&getenv("DERP_FOOTPRINT")) {footShotAt=SDL_GetTicksNS();footFrames=0;}
            }
            ++shots;
        }
    }
    // DERP_FOOTPRINT (with DERP_SHOOT from tic 30): every light layer as it stands before the shot, and once all
    // has been quiet for 4 seconds after it, how many texels of each changed: what a fixture's stored share
    // would have to cover. With the counts of glowing tiles, fixtures (by surface, roughly) and bake sources.
    static int footLevel=-1,footStage=0,footQuiet=-1;
    static std::vector<uint8_t> footFlat,footWall,footFlatDir,footWallDir;
    static std::vector<StaticCell> footGrid;
    if(footLevel!=(int)r_levelserial) {footLevel=(int)r_levelserial;footStage=0;footQuiet=-1;footShotAt=0;}
    if(getenv("DERP_FOOTPRINT")&&!flatLightCells.empty()) {
        bool quiet=!relight.running&&relightPending.empty()&&brokenPending.empty()&&farPending.empty()&&lightUploads.empty()&&!reBounceBusy()&&brokenTilesSteady();
        if(footStage==0&&leveltime>=24&&lampsShotCount()==0&&quiet) {
            footFlat=flatLightCells;footWall=wallBakeCells;footFlatDir=flatDirectionCells;footWallDir=wallDirectionCells;footGrid=staticGrid;
            std::set<std::array<int,4>> fixtures;
            for(const SurfaceLight &tile:surfaceLights)fixtures.insert({tile.key[0],tile.key[1],tile.key[2],tile.key[0]==1?tile.key[5]:0});
            fprintf(stderr,"footprint: before the shot at tic %d: %zu glowing tiles, about %zu fixtures, %zu bake sources (%zu window panels).\n",
                    leveltime,surfaceLights.size(),fixtures.size(),bakeSources.size(),bakeSources.size()-std::min(bakeSources.size(),(size_t)firstWindowSource));
            footStage=1;
        } else if(footStage==1&&footShotAt) {
            if(!quiet)footQuiet=-1;
            else if(footQuiet<0)footQuiet=leveltime;
            else if(leveltime-footQuiet>=140) {
                footStage=2;
                fprintf(stderr,"footprint: settled by tic %d (%.0f ms after the shot).\n",footQuiet,(SDL_GetTicksNS()-footShotAt)/1e6-4000.0);
                size_t total=0;
                auto count=[&](const char *name,const std::vector<uint8_t> &a,const std::vector<uint8_t> &b,size_t from,size_t to,size_t step,size_t run,int channels) {
                    size_t one=0,four=0;
                    if(a.size()!=b.size()||to>a.size())return;
                    for(size_t row=from;row<to;row+=step)for(size_t i=row;i<std::min(row+run,to);i+=4) {
                        int by=0;for(int c=0;c<channels;++c)by=std::max(by,std::abs(a[i+c]-b[i+c]));
                        one+=by>=1;four+=by>=4;
                    }
                    total+=one;
                    fprintf(stderr,"footprint %s: %zu texels changed, %zu of them by 4 or more.\n",name,one,four);
                };
                const size_t layer=(size_t)mapWidth*mapHeight*4;
                for(size_t of=0;of*layer<flatLightCells.size();++of) {
                    char name[48];snprintf(name,sizeof name,"flat layer %zu",of);
                    count(name,footFlat,flatLightCells,of*layer,(of+1)*layer,layer,layer,3);
                }
                const size_t row=(size_t)atlasStride*4,half=(size_t)atlasWidth*4;
                count("wall lamp",footWall,wallBakeCells,0,wallBakeCells.size(),row,half,3);
                count("wall bounce",footWall,wallBakeCells,half,wallBakeCells.size(),row,half,3);
                count("floor directions",footFlatDir,flatDirectionCells,0,flatDirectionCells.size(),flatDirectionCells.size(),flatDirectionCells.size(),4);
                count("wall directions",footWallDir,wallDirectionCells,0,wallDirectionCells.size(),wallDirectionCells.size(),wallDirectionCells.size(),4);
                size_t grid=0;
                if(footGrid.size()==staticGrid.size())for(size_t i=0;i<staticGrid.size();++i) {
                    const StaticCell &a=footGrid[i],&b=staticGrid[i];
                    grid+=a.used!=b.used||a.low!=b.low||a.high!=b.high||memcmp(a.at,b.at,sizeof a.at)!=0;
                }
                fprintf(stderr,"footprint sprite grid: %zu cells changed.\nfootprint: %zu texels in all.\n",grid,total);
            }
        }
    }
    // DERP_BAKECHECK=tic: once all is quiet after that tic, the light as the re-bakes left it is kept, everything
    // is baked anew, and what differs is printed: light a shot-out lamp left behind shows as "more before".
    static int checkLevel=-1,checkStage=0;
    static std::vector<uint8_t> checkFlat,checkWall;
    if(checkLevel!=(int)r_levelserial) {checkLevel=(int)r_levelserial;checkStage=0;}
    if(const char *at=getenv("DERP_BAKECHECK");at&&leveltime>=atoi(at)&&!flatLightCells.empty()) {
        bool quiet=!relight.running&&relightPending.empty()&&brokenPending.empty()&&farPending.empty()&&lightUploads.empty()&&!reBounceBusy();
        if(checkStage==0&&quiet&&brokenTilesSteady()) {
            checkFlat=flatLightCells;checkWall=wallBakeCells;checkStage=1;
            lightBakeKey=-1;bounceKey=-1;
            fprintf(stderr,"bake check: quiet at tic %d, %zu lamps shot; baking anew.\n",leveltime,lampsShotCount());
        } else if(checkStage==1&&lightBakeKey>=0&&bounceKey>=0&&checkFlat.size()==flatLightCells.size()) {
            checkStage=2;
            const size_t layer=(size_t)mapWidth*mapHeight;
            const char *names[4]={"floor lamp","ceiling lamp","floor bounce","ceiling bounce"};
            for(int of=0;of<4;++of) {
                size_t more=0,less=0,worst=0;int worstBy=0;double moreSum=0,lessSum=0;
                for(size_t i=0;i<layer;++i) {
                    if(seamCells[i].own==noSector)continue;
                    const uint8_t *a=&checkFlat[4*(of*layer+i)],*b=&flatLightCells[4*(of*layer+i)];
                    int by=0;   // the channel that differs most: a blue lamp has next to no luminance
                    for(int c=0;c<3;++c)if(std::abs(a[c]-b[c])>std::abs(by))by=a[c]-b[c];
                    if(by>4) {++more;moreSum+=by;if(by>worstBy) {worstBy=by;worst=i;}}
                    if(by<-4) {++less;lessSum-=by;}
                }
                const uint8_t *a=&checkFlat[4*(of*layer+worst)],*b=&flatLightCells[4*(of*layer+worst)];
                fprintf(stderr,"bake check %s: more before in %zu cells (mean %.1f), less in %zu (mean %.1f); worst at %.0f,%.0f sector %d: %d/%d/%d before, %d/%d/%d anew.\n",
                        names[of],more,more?moreSum/more:0.0,less,less?lessSum/less:0.0,mapOrigin[0]+(worst%mapWidth+0.5f)*mapCell,
                        mapOrigin[1]+(worst/mapWidth+0.5f)*mapCell,(int)seamCells[worst].own,a[0],a[1],a[2],b[0],b[1],b[2]);
            }
            if(checkWall.size()==wallBakeCells.size())for(int bounce=0;bounce<2;++bounce) {
                size_t more=0,less=0;int worstBy=0,worstStrip=-1;uint8_t wa[3]={},wb[3]={};float wz=0;
                for(int index:bakeOrder)forStripTexels(bakeStrips[index],[&](size_t i,float,float,float z) {
                    const uint8_t *a=&checkWall[4*(i+(bounce?atlasWidth:0))],*b=&wallBakeCells[4*(i+(bounce?atlasWidth:0))];
                    int by=0;
                    for(int c=0;c<3;++c)if(std::abs(a[c]-b[c])>std::abs(by))by=a[c]-b[c];
                    if(by>4) {++more;if(by>worstBy) {worstBy=by;worstStrip=index;wz=z;for(int c=0;c<3;++c) {wa[c]=a[c];wb[c]=b[c];}}}
                    if(by<-4)++less;
                });
                if(worstStrip<0) {fprintf(stderr,"bake check wall %s: more before in 0 texels, less in %zu.\n",bounce?"bounce":"lamp",less);continue;}
                const BakeStrip &strip=bakeStrips[worstStrip];
                fprintf(stderr,"bake check wall %s: more before in %zu texels, less in %zu; worst on the wall from %.0f,%.0f to %.0f,%.0f at height %.0f, sector %d: %d/%d/%d before, %d/%d/%d anew.\n",
                        bounce?"bounce":"lamp",more,less,strip.ax,strip.ay,strip.ax+strip.dx*strip.length,strip.ay+strip.dy*strip.length,wz,strip.sector,wa[0],wa[1],wa[2],wb[0],wb[1],wb[2]);
            }
        }
    }
    // DERP_PROBE=x,y;x,y;...: at tic 40 prints what the light maps hold on the floor at each place.
    // DERP_PROBE_EVERY=n: and again every n tics after, to follow a re-bake as it settles.
    static int probed=-1,probeNext=40;
    if(probed!=(int)r_levelserial) {probed=(int)r_levelserial;probeNext=40;}
    if(const char *at=getenv("DERP_PROBE");at&&leveltime>=probeNext&&!flatLightCells.empty()) {
        const char *every=getenv("DERP_PROBE_EVERY");
        probeNext=every&&atoi(every)>0?leveltime+atoi(every):INT_MAX;
        fprintf(stderr,"probe at tic %d: %zu lamps shot.\n",leveltime,lampsShotCount());
        for(const char *q=at;q&&*q;) {
            float x=0,y=0;
            if(sscanf(q,"%f,%f",&x,&y)!=2)break;
            int cx=std::clamp((int)std::floor((x-mapOrigin[0])/mapCell),0,mapWidth-1),cy=std::clamp((int)std::floor((y-mapOrigin[1])/mapCell),0,mapHeight-1);
            size_t i=(size_t)cy*mapWidth+cx;int s=seamCells[i].own;
            const uint8_t *l=flatCell(floorLight,i),*b=flatCell(floorBounce,i);
            fprintf(stderr,"probe %.0f,%.0f sector %d kind %d outside %d yard %d level %.3f lamp %d/%d/%d bounce %d/%d/%d sky %d open %d sun %d info.w %.3f\n",x,y,s,
                    s==noSector?-1:(int)Rooms_Kind(s),s==noSector?-1:Rooms_Outside(s),s==noSector?-1:Rooms_Yard(s),s==noSector?0.0f:lighting(&sectors[s]),
                    l[0],l[1],l[2],b[0],b[1],b[2],b[3]>>4,b[3]&15,contactCells.empty()?-1:contactCells[4*i+2],
                    s==noSector?0.0f:(sectors[s].ceilingpic==skyflatnum?packOutdoor(&sectors[s]):openAirYard(s)>=0?-packOutdoor(&sectors[openAirYard(s)]):0.0f));
            if(flatDirectionCells.size()>=16*(size_t)mapWidth*mapHeight) {
                size_t layer=(size_t)mapWidth*mapHeight;
                fprintf(stderr,"probe marks: lamp groups %d, floor mark %d, ceiling mark %d, floor bounce mark %d, group flicker %.2f\n",lampGroupCount,
                        flatDirectionCells[4*i+3],flatDirectionCells[4*(layer+i)+3],flatDirectionCells[4*(2*layer+i)+3],
                        groupFlicker(flatDirectionCells[4*i+3]>>4));
            }
            q=strchr(q,';');if(q)++q;
        }
    }
    // DERP_PLACE=x,y: the player himself stands there from tic 20 (the render camera is -renderview's), to
    // see what follows where he stands and not where the eye is.
    static int placed=-1;
    if(const char *at=getenv("DERP_PLACE");at&&leveltime>=20&&placed!=(int)r_levelserial&&players[displayplayer].mo) {
        float x=0,y=0;placed=(int)r_levelserial;
        if(sscanf(at,"%f,%f",&x,&y)==2) {
            mobj_t *mo=players[displayplayer].mo;
            P_TeleportMove(mo,(fixed_t)(x*FRACUNIT),(fixed_t)(y*FRACUNIT));
            mo->z=mo->floorz;players[displayplayer].viewz=mo->z+41*FRACUNIT;
        }
    }
    static int tested=-1;
    if(const char *at=getenv("DERP_REBOUNCE");at&&leveltime>=20&&tested!=(int)r_levelserial) {
        float x=0,y=0;tested=(int)r_levelserial;
        if(sscanf(at,"%f,%f",&x,&y)==2)reBounceMark(x-bounceRound,y-bounceRound,x+bounceRound,y+bounceRound);
    }
#endif
}
bool relightRunning() {return relight.running;}
// Waits for any re-bake in flight and drops what it would apply. The worker
// thread stays up, idle, for the next level.
void stopRelight() {
    prep.cancel=true;preparedBounceStop();preparedShots.clear();fixtures.clear();prepStage=PrepStage::idle;prepSynced={};fixturesStamp={};
    dropRebounce();finishRelight(false);litHeights.clear();relightPending.clear();brokenPending.clear();farPending.clear();switchSeen=switchDue=false;
    farLamps=false;
    forgetStripBins();
}
} // namespace doom3d
