/* BakeMap flattened for the GPU tracer, and slicing of the kernels' work
   (gpu_trace.h). */
#include "gpu_trace.h"
#include "open_doors.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <mutex>

namespace doom3d {
namespace {
// Each thread's own map (the load's bakes, the light worker's re-bakes), all let go when the
// renderer shuts down, or when their thread ends before it.
struct MapSlot;
std::mutex mapsLock;
std::vector<MapSlot*> maps;
struct MapSlot {
    gpucompute::Buffer *buffer=nullptr;
    MapSlot() {std::lock_guard<std::mutex> hold(mapsLock);maps.push_back(this);}
    ~MapSlot() {
        std::lock_guard<std::mutex> hold(mapsLock);
        maps.erase(std::remove(maps.begin(),maps.end(),this),maps.end());
        gpucompute::destroy(buffer);
    }
};
thread_local MapSlot mapSlot;
thread_local std::vector<uint32_t> words;
uint32_t f2u(float f) {uint32_t u;std::memcpy(&u,&f,4);return u;}
} // namespace

bool gpuBakesOn() {
    static int forced=-2;
    if(forced==-2) {const char *e=getenv("DERP_GPUBAKES");forced=e&&*e?atoi(e)!=0:-1;}
    bool wanted=forced>=0?forced==1:settings.gpuBakes!=0;
    return wanted&&gpucompute::available();
}
bool gpuRelightOn() {
    // Prepared shots (backgroundLight) always on the processor: the card's time is the frame's
    // (at 4K a prepared fixture's 0.25 s of card work cost every frame about a millisecond while
    // they went on), and its slices don't stop for a real shot.
    if(backgroundLight)return false;
    static int forced=-2;
    if(forced==-2) {const char *e=getenv("DERP_GPURELIGHT");forced=e&&*e?atoi(e)!=0:-1;}
    static const bool deck=[]{const char *d=getenv("SteamDeck");return d&&!strcmp(d,"1");}();
    // Auto on a Mac: the processor. Metal answers each submission some 8 ms late however small, so
    // play slices of a few milliseconds crawl (E1M1's shot: 17 s on an M2's card, 0.45 s on its
    // processor), and slices big enough to beat that hold the frames as long.
#ifdef __APPLE__
    constexpr bool apple=true;
#else
    constexpr bool apple=false;
#endif
    bool wanted=forced>=0?forced==1:settings.gpuRelight==1||(settings.gpuRelight==0&&!deck&&!apple);
    return wanted&&gpucompute::available();
}
gpucompute::Buffer *gpuMapBuffer() {return mapSlot.buffer;}
void gpuTraceShutdown() {
    gpuLightsShutdown();
    std::lock_guard<std::mutex> hold(mapsLock);
    for(MapSlot *slot:maps)gpucompute::destroy(slot->buffer);
}
bool gpuUploadMap(const BakeMap &map,const OpenDoors *doors,const std::vector<char> *roofless,GpuMapUniforms &u,gpucompute::Buffer **target,const BakeMap *before) {
    if(before&&before->sectors.size()!=map.sectors.size())before=nullptr;
    words.clear();
    const size_t sectorCount=map.sectors.size(),lineCount=map.lines.size();
    // Coverage of masks and decorations, a bit a texel; one copy per picture.
    std::vector<uint32_t> bits;
    std::map<std::pair<const unsigned char*,long long>,uint32_t> pictures;
    auto picture=[&](const unsigned char *pixels,int width,int height) {
        auto key=std::make_pair(pixels,(long long)width<<32|(unsigned)height);
        auto found=pictures.find(key);
        if(found!=pictures.end())return found->second;
        uint32_t start=(uint32_t)bits.size()*32;
        size_t count=(size_t)width*height;
        bits.resize(bits.size()+(count+31)/32,0);
        for(size_t i=0;i<count;++i)if(pixels[2*i+1])bits[start/32+i/32]|=1u<<(i%32);
        pictures[key]=start;
        return start;
    };
    std::vector<uint32_t> masks,things,nodes,lists;
    std::vector<int> maskOf(lineCount,-1);
    for(size_t i=0;i<lineCount;++i) {
        const BakeMask &m=map.lines[i].mask;
        if(!m.pixels||m.width<1||m.height<1)continue;
        const BakeLine &l=map.lines[i];
        maskOf[i]=(int)(masks.size()/12);
        masks.insert(masks.end(),{picture(m.pixels,m.width,m.height),(uint32_t)m.width,(uint32_t)m.height,0,
            f2u(m.u),f2u(m.direction),f2u(m.anchor),f2u(m.bottom),f2u(m.top),f2u(std::hypot(l.bx-l.ax,l.by-l.ay)),0,0});
    }
    for(const BakeThing &t:map.things)
        things.insert(things.end(),{f2u(t.x),f2u(t.y),f2u(t.top),f2u(t.left),(uint32_t)t.width,(uint32_t)t.height,
                                    t.pixels&&t.width>0&&t.height>0?picture(t.pixels,t.width,t.height):0,0});
    std::vector<uint32_t> sectorWords;sectorWords.reserve(sectorCount*12);
    for(size_t s=0;s<sectorCount;++s) {
        const BakeSector &b=map.sectors[s];
        float door=b.ceiling;
        if(doors&&doors->any&&s<doors->door.size()&&doors->door[s])door=doors->map.sectors[s].ceiling;
        uint32_t flags=(b.sky?1u:0u)|(roofless&&s<roofless->size()&&(*roofless)[s]?2u:0u);
        uint32_t lineStart=(uint32_t)lists.size();
        for(int l:b.lines)lists.push_back((uint32_t)l);
        int tree=-1;uint32_t orderStart=(uint32_t)lists.size();
        if(!b.tree.empty()) {
            tree=(int)(nodes.size()/8);
            for(const BakeNode &n:b.tree)
                nodes.insert(nodes.end(),{f2u(n.low[0]),f2u(n.low[1]),f2u(n.high[0]),f2u(n.high[1]),(uint32_t)n.first,(uint32_t)n.count,0,0});
            for(int o:b.order)lists.push_back((uint32_t)o);
        }
        uint32_t thingStart=(uint32_t)lists.size();
        for(int t:b.things)lists.push_back((uint32_t)t);
        const BakeSector &was=before?before->sectors[s]:b;
        sectorWords.insert(sectorWords.end(),{f2u(b.floor),f2u(b.ceiling),f2u(door),flags,lineStart,(uint32_t)b.lines.size(),
            (uint32_t)tree,orderStart,thingStart,(uint32_t)b.things.size(),f2u(was.floor),f2u(was.ceiling)});
    }
    std::vector<uint32_t> lineWords;lineWords.reserve(lineCount*8);
    for(size_t i=0;i<lineCount;++i) {
        const BakeLine &l=map.lines[i];
        lineWords.insert(lineWords.end(),{f2u(l.ax),f2u(l.ay),f2u(l.bx),f2u(l.by),(uint32_t)l.front,(uint32_t)l.back,(uint32_t)maskOf[i],0});
    }
    auto append=[&](const std::vector<uint32_t> &part) {
        uint32_t at=(uint32_t)words.size();
        words.insert(words.end(),part.begin(),part.end());
        // Each part on a 16-byte line, so a record never straddles one for nothing.
        while(words.size()%4)words.push_back(0);
        return at;
    };
    u.mapA[0]=append(sectorWords);u.mapA[1]=append(lineWords);u.mapA[2]=append(nodes);u.mapA[3]=append(lists);
    u.mapB[0]=append(masks);u.mapB[1]=append(things);u.mapB[2]=append(bits);u.mapB[3]=(uint32_t)sectorCount;
    words.push_back(0);   // never empty
    size_t bytes=words.size()*4;
    gpucompute::Buffer *&buffer=target?*target:mapSlot.buffer;
    if(!buffer||gpucompute::size(buffer)<bytes) {
        gpucompute::destroy(buffer);
        buffer=gpucompute::create(bytes+bytes/4);
        if(!buffer)return false;
    }
    gpucompute::upload(buffer,words.data(),bytes);
    return true;
}

// What each kind of card work does in a millisecond at load, as gpuSliced's play slices start
// from it (PLAN-gpu-relight-slices.md). Only slices that showed the card's speed count: their
// fence not yet done when waited for (one already done measured how long the thread was away,
// drawing the loading screen, and gave rates tens of times too high), and of those the median,
// not the best. A level restored from the bake cache bakes nothing at load and measures none,
// so the rates are kept in gpu-slice-rates.txt beside bake-cache/ in the save folder, a line
// per card, backend and kind; written as the bake cache is (not with DERP_NOBAKECACHE).
// A kind is named with its lighting model (rateKind): a whole-level lamp item traces every lamp
// within 1024 units and costs about four times a Classic one, and with one rate for both, a
// shot's first job took about a second or held each frame 15-20 ms, by which model baked last.
// Lines from before (no model in the kind) are dropped when the file is written next.
namespace {
std::mutex ratesLock;
std::map<std::string,std::vector<double>> loadSamples;   // this session's
std::map<std::string,double> savedRates;               // the file's, for this card
bool savedRead=false;
const char ratesFile[]="gpu-slice-rates.txt";
bool ratesFileOn() {const char *v=getenv("DERP_NOBAKECACHE");return !(v&&*v&&strcmp(v,"0")!=0);}
std::string cardKey() {return std::string(gpuDeviceName())+" | "+gpucompute::backend();}
double medianOf(std::vector<double> v) {
    if(v.empty())return 0;
    std::nth_element(v.begin(),v.begin()+v.size()/2,v.end());
    return v[v.size()/2];
}
// Lines "card | backend<TAB>kind<TAB>rate": this card's into savedRates, once (under ratesLock).
void readSavedRates() {
    if(savedRead)return;
    savedRead=true;
    if(!ratesFileOn())return;
    FILE *file=fopen(ratesFile,"r");
    if(!file)return;
    const std::string card=cardKey();
    char line[512];
    while(fgets(line,sizeof line,file)) {
        char *kind=strchr(line,'\t'),*rate=kind?strchr(kind+1,'\t'):nullptr;
        if(!rate)continue;
        *kind++=0;*rate++=0;
        if(card==line&&atof(rate)>0&&strstr(kind," | "))savedRates[kind]=atof(rate);
    }
    fclose(file);
}
// This card's lines anew, with what this session measured over what was saved (under ratesLock).
void writeSavedRates() {
    if(!ratesFileOn())return;
    readSavedRates();   // this card's other kinds stay
    const std::string card=cardKey();
    std::vector<std::string> others;
    if(FILE *file=fopen(ratesFile,"r")) {
        char line[512];
        while(fgets(line,sizeof line,file)) {
            const char *tab=strchr(line,'\t');
            if(tab&&std::string(line,(size_t)(tab-line))!=card)others.push_back(line);
        }
        fclose(file);
    }
    for(const auto &[kind,samples]:loadSamples)if(samples.size()>=5)savedRates[kind]=medianOf(samples);
    const std::string temporary=std::string(ratesFile)+".new";
    FILE *file=fopen(temporary.c_str(),"w");
    if(!file)return;
    for(const std::string &line:others)fputs(line.c_str(),file);
    for(const auto &[kind,rate]:savedRates)fprintf(file,"%s\t%s\t%.1f\n",card.c_str(),kind.c_str(),rate);
    bool ok=fclose(file)==0;
    if(ok) {remove(ratesFile);ok=rename(temporary.c_str(),ratesFile)==0;}
    if(!ok)remove(temporary.c_str());
}
// A kind of work as its rates are kept: with the lighting model it bakes for.
std::string rateKind(const char *what) {return std::string(what)+(settings.lightModel==1?" | whole level":" | classic");}
// What the kind does in a millisecond: this session's median, else the saved one; a door's or
// changed lamps' light (no load of their own) go by the lamps' light. 0: not known.
double knownRate(const std::string &what,const char *&from) {
    std::lock_guard<std::mutex> hold(ratesLock);
    readSavedRates();
    for(const std::string &kind:{what,rateKind("the lamps' light")}) {
        auto measured=loadSamples.find(kind);
        if(measured!=loadSamples.end()&&measured->second.size()>=5) {from="load";return medianOf(measured->second);}
        auto saved=savedRates.find(kind);
        if(saved!=savedRates.end()) {from="saved";return saved->second;}
    }
    from="none";
    return 0;
}
}

bool gpuSliced(size_t count,size_t firstSlice,size_t most,const std::function<void(size_t,size_t)> &run,const char *what) {
    // The time a slice may take, and the most items one may hold (results must fit their buffer).
    // At load the card does nothing else: a few slices are in flight at once, sized by how long
    // they take. In play (the light worker's re-bakes) the frames share the card, and a frame
    // waits behind whatever was queued ahead of it: one slice at a time, of about 3 ms, sized by
    // the rate this work has kept up in play. A kind's first job in play starts at a quarter of
    // what the work does at load (knownRate: play slices hold the frames' turns too, and door
    // items cost more than the lamps' at load), and the rate never sinks below a tenth of it:
    // with nothing to hold it, at a busy card the slices once shrank to one a frame, and a door's
    // light took seconds. With no rate known at all, no slice is smaller than the first.
    // (SDL_gpu orders the slices' use of a buffer they share: each pass ends with a barrier.)
    const bool loading=loadingOnThisThread();
    const double target=loading?25:3,ceiling=60;
    const size_t inFlight=loading?3:1;
    thread_local std::map<std::string,double> playRates;   // as kept up in play, per kind
    const char *from="none";
    const std::string kind=rateKind(what);
    const double known=loading?0:knownRate(kind,from),guard=known*0.1;
    double &rate=playRates[kind];
#ifdef DOOM_DEVTOOLS
    // DERP_GPUTIMES=1: per job, where its first slice's size came from, how many slices it took,
    // how many sat at the 64-item least, and its longest slice (PLAN-gpu-relight-slices.md).
    static const bool logJobs=getenv("DERP_GPUTIMES")!=nullptr;
    const char *source=loading?"load":rate>0?"kept":from;
    size_t slices=0,atLeast=0;double longest=0;const uint64_t began=SDL_GetTicksNS();
#endif
    if(!loading&&rate<=0)rate=known*0.25;
    if(!loading)rate=std::max(rate,guard);
    const size_t least=loading?1:known>0?64:std::min(firstSlice,most);
    // In play each slice goes behind a mark (gpucompute::mark): what it costs the card is the time
    // from the mark's being done to its own, not the wait behind the frames queued ahead of it. Timed
    // from sending, at 4K and no frame limit the wait was most of it, and the slices shrank to a tenth.
    // Only where the slice was still running when the mark was done: on Metal the mark's fence often
    // signals with the slice's, which then took no time, its rate grew without end, and the next
    // slice held the card for 370 ms (the Mac, E1M1). There it is timed from sending, as before.
    struct Flight { uint64_t ticket; size_t n; uint64_t sent,mark; };
    std::vector<Flight> flights;
    size_t slice=std::clamp<size_t>(!loading&&rate>0?(size_t)(rate*target):firstSlice,least,most),first=0,landed=0;
#ifdef DOOM_DEVTOOLS
    const size_t firstSize=slice;const double startRate=loading?0:rate;
#endif
    uint64_t last=SDL_GetTicksNS();
    bool ok=true;
    while(ok&&(first<count||!flights.empty())) {
        if(first<count&&flights.size()<inFlight) {
            size_t n=std::min(slice,count-first);
            uint64_t mark=loading?0:gpucompute::mark(),sent=SDL_GetTicksNS();
            run(first,n);
            uint64_t ticket=gpucompute::submit();
            if(!ticket) {ok=false;break;}
            flights.push_back({ticket,n,sent,mark});
            first+=n;
            continue;
        }
        Flight f=flights.front();flights.erase(flights.begin());
        uint64_t from=f.sent;
        if(f.mark&&gpucompute::wait(f.mark)) {
            uint64_t marked=SDL_GetTicksNS();
            if(!gpucompute::done(f.ticket))from=std::max(from,marked);
        }
        const bool waited=!gpucompute::done(f.ticket);   // still on the card: the wait measures it
        if(!gpucompute::wait(f.ticket)) {ok=false;break;}
        uint64_t now=SDL_GetTicksNS();
        double ms=(now-(inFlight>1?last:from))/1e6;last=now;
        landed+=f.n;
        loadingTick((float)landed/std::max<size_t>(1,count));
#ifdef DOOM_DEVTOOLS
        ++slices;atLeast+=f.n<=64&&f.n<count;longest=std::max(longest,ms);
#endif
        // In play an average, not the best: items differ a hundredfold in cost (round a door, the
        // cells whose lamps cross it against those whose lamps do not), and a slice sized by the
        // cheapest held the frame behind it for tens of milliseconds. One that ran long halves it.
        if(f.n>=64) {
            double now=f.n/std::max(ms,0.05);
            if(loading) {if(waited) {std::lock_guard<std::mutex> hold(ratesLock);loadSamples[kind].push_back(now);}}
            else {
                // At most twice the rate so far: one slice measured too fast must not size the next.
                if(rate>0)now=std::min(now,2*rate);
                rate=rate>0?0.7*rate+0.3*now:now;
                if(ms>3*target)rate*=0.5;
                rate=std::max(rate,guard);
            }
        }
        if(loading) {
            // The time between slices landing is what one takes the card, with others queued behind it.
            if(f.n==slice) {
                if(ms<target*0.5)slice=std::min({slice*2,slice+(size_t)(slice*(target/std::max(ms,0.5)-1)),most});
                else if(ms>ceiling)slice=std::max<size_t>(1,slice/2);
            }
        } else slice=std::clamp<size_t>((size_t)(rate*target),least,most);
    }
    for(const Flight &f:flights)gpucompute::wait(f.ticket);
    if(loading&&ok) {std::lock_guard<std::mutex> hold(ratesLock);if(loadSamples[kind].size()>=5)writeSavedRates();}
#ifdef DOOM_DEVTOOLS
    if(logJobs) {
        double typical=0;size_t samples=0;
        if(loading) {std::lock_guard<std::mutex> hold(ratesLock);auto s=loadSamples.find(kind);if(s!=loadSamples.end()) {typical=medianOf(s->second);samples=s->second.size();}}
        fprintf(stderr,"GPU slices: %s, %s: %zu items in %zu slices (%zu at 64), longest %.1f ms, %.0f ms in all; first slice %zu, start rate %.1f/ms (%s); load rate %.1f/ms of %zu samples.\n",
            kind.c_str(),loading?"load":"play",count,slices,atLeast,longest,(SDL_GetTicksNS()-began)/1e6,firstSize,startRate,source,typical,samples);
    }
#endif
    if(!ok)fprintf(stderr,"GPU bakes: %s failed; baking it on the processor.\n",what);
    return ok;
}

bool gpuTraceRays(const std::vector<GpuRay> &rays,std::vector<GpuRayResult> &results,const OpenDoors *doors,const BakeMap &map) {
    results.assign(rays.size(),{});
    if(rays.empty())return true;
    GpuMapUniforms mu;
    if(!gpuUploadMap(map,doors,nullptr,mu))return false;
    gpucompute::Buffer *in=gpucompute::create(rays.size()*sizeof(GpuRay));
    // Results a slice at a time: a buffer for the largest slice.
    constexpr size_t most=1u<<21;
    gpucompute::Buffer *out=gpucompute::create(std::min(rays.size(),most)*sizeof(GpuRayResult));
    bool ok=in&&out;
    if(ok) {
        gpucompute::upload(in,rays.data(),rays.size()*sizeof(GpuRay));
        const uint32_t group=gpucompute::groupSize(gpucompute::Kernel::TraceRays);
        ok=gpuSliced(rays.size(),1u<<16,most,[&](size_t first,size_t n) {
            struct { uint32_t mapA[4],mapB[4],job[4]; } job={};
            std::memcpy(job.mapA,mu.mapA,16);std::memcpy(job.mapB,mu.mapB,16);
            job.job[0]=(uint32_t)first;job.job[1]=(uint32_t)n;
            gpucompute::dispatch(gpucompute::Kernel::TraceRays,{gpuMapBuffer(),in},{out},&job,sizeof(job),(uint32_t)((n+group-1)/group));
            gpucompute::download(out,&results[first],n*sizeof(GpuRayResult));
        },"tracing rays");
    }
    gpucompute::destroy(in);gpucompute::destroy(out);
    return ok;
}
} // namespace doom3d
