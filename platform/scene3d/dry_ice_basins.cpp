/* Dry ice's low spots (dry_ice.cpp): where a stream creeping in along a
   draft stops to fill before it goes on. Dry ice fog is heavier than air: in
   a gentle draft it runs downhill and fills a pit up to its rim before it
   spills on; a strong draft skims over. Approximated from the level's
   floors, never simulated:
   - Basins, found once a level: indoor sectors lower than everything round
     them (a pit, a sunken floor, a recess), filled from the lowest up until
     the fog would run away: the level is the rim, the lowest floor it can
     spill over (a shut door counts, fog leaks under it; so does the way out
     under the sky). Too shallow, too deep or too big (a hall) is no basin.
   - Islands: indoor sectors higher than everything round them (a pedestal,
     a pillar, a raised platform). The draft goes over them; the fog parts
     round them and lies on, never climbing on top.
   - A stream's course, walked along a draft's steps: what it meets in order.
     At a basin its front waits while it fills (what fills it falls out of
     the stream and runs there downhill, floor_film.cpp, which also lets
     it lie as deep as its rim and no deeper); a step up taller than the
     stream it piles up behind like a dam, then flows on over. A step too
     tall to pile up over stops a gentle stream for good. The fill times come
     from the volume over what the stream brings, so they follow the front's
     own creep: nothing to keep from tic to tic. A strong draft waits for
     neither: the basins under it fill four times slower while it goes on,
     and past a step it runs thinner. Steps down it just flows down.
   Render-only. */
#include "internal.h"
#include "../rooms.h"

namespace doom3d {
namespace {
constexpr float skimSpeed=60;    // the knob: an inflow faster than this beats the fog's weight and skims
constexpr float climb=32;        // the tallest step a gentle stream piles up over
constexpr float pileHeight=24;   // the most it piles up behind a step
constexpr float pileLength=48;   // how far back from the step the pile reaches
constexpr float minDepth=6,maxDepth=64,maxArea=393216;   // a basin's bounds (maxArea: about 630 units square)
struct Basin { float level,volume; std::vector<int> sectors; };
std::vector<Basin> basins;
std::vector<int> basinOf;        // per sector: its basin, -1 none
std::vector<char> island;        // per sector: higher than all round it
bool indoor(int s) {return !Rooms_Outside(s);}
}

// Each sector a seed if nothing next to it is lower: grown by its lowest
// neighbour while that's no lower than the fog's top (raising the top to it),
// until the lowest neighbour lies below the top (it spills there) or out
// under the sky (it would run out). What lies under the final top is the
// basin; area is each sector's floor area. Then the islands.
void findDryIceBasins(const std::vector<float> &area) {
    basins.clear();
    basinOf.assign(numsectors,-1);
    island.assign(numsectors,0);
    std::vector<std::vector<int>> next(numsectors);
    for(int i=0;i<numlines;++i) {
        const line_t &line=lines[i];
        if(!line.frontsector||!line.backsector||line.frontsector==line.backsector)continue;
        int f=int(line.frontsector-sectors),k=int(line.backsector-sectors);
        next[f].push_back(k);next[k].push_back(f);
    }
    std::vector<char> in(numsectors,0);
    std::vector<int> region;
    for(int seed=0;seed<numsectors;++seed) {
        if(basinOf[seed]>=0||!indoor(seed)||sectors[seed].floorpic==skyflatnum)continue;
        float bottom=floorZ(&sectors[seed]);
        bool lowest=true;
        for(int n:next[seed])if(floorZ(&sectors[n])<bottom-0.5f)lowest=false;
        if(!lowest)continue;
        region.assign(1,seed);in[seed]=1;
        float top=bottom,size=area[seed];
        bool basin=false;
        for(int round=0;round<64;++round) {
            int low=-1;float lowZ=INFINITY;
            for(int s:region)for(int n:next[s])if(!in[n]&&floorZ(&sectors[n])<lowZ) {lowZ=floorZ(&sectors[n]);low=n;}
            if(low<0)break;   // nothing round it at all
            if(lowZ<top-0.5f) {basin=top-bottom>=minDepth;break;}
            if(!indoor(low)||sectors[low].floorpic==skyflatnum) {top=std::max(top,lowZ);basin=top-bottom>=minDepth;break;}
            top=std::max(top,lowZ);
            size+=area[low];
            if(top-bottom>maxDepth||size>maxArea)break;
            region.push_back(low);in[low]=1;
        }
        for(int s:region)in[s]=0;
        if(!basin)continue;
        Basin b{top,0,{}};
        for(int s:region) {
            float depth=top-floorZ(&sectors[s]);
            if(depth<1||basinOf[s]>=0)continue;
            basinOf[s]=(int)basins.size();
            b.volume+=area[s]*depth;b.sectors.push_back(s);
        }
        if(!b.sectors.empty())basins.push_back(std::move(b));
    }
    for(int s=0;s<numsectors;++s) {
        if(!indoor(s)||next[s].empty())continue;
        bool high=true;
        for(int n:next[s])if(floorZ(&sectors[n])>floorZ(&sectors[s])-0.5f)high=false;
        island[s]=high;
    }
}

// A stream's course along path, at most stream units deep at its opening.
void dryIceCourse(const DraftPath &path,float stream,DryIceCourse &out) {
    size_t count=path.steps.size();
    out.thin.assign(count,1);out.piles.clear();
    if(basinOf.size()!=(size_t)numsectors) {out.front=path.reach;return;}
    bool strong=path.speed>=skimSpeed;
    float budget=path.reach,along=0,thin=1,creep=std::max(path.creep,1.0f);
    int inBasin=-1;
    for(size_t k=0;k<count;++k) {
        const DraftStep &g=path.steps[k];
        float l=std::hypot(g.bx-g.ax,g.by-g.ay),at=along+l*0.5f;
        int from=g.from,to=g.sector;
        if(from<0||from>=numsectors||to<0||to>=numsectors) {out.thin[k]=thin;along+=l;continue;}
        // What the stream brings a second (units^3): its ribbon's width,
        // creeping at the front's pace, a quarter of it staying.
        float feed=std::max(0.25f*stream*2.4f*g.width*creep*std::min(1.0f,g.amount*2),1.0f);
        // How far it climbs here: from the lower floor, or from the top of
        // the basin it's leaving (full by now, or the front would be waiting).
        float low=floorZ(&sectors[from]);
        if(inBasin>=0&&basinOf[from]==inBasin)low=std::max(low,basins[inBasin].level);
        float rise=floorZ(&sectors[to])-low;
        int b=basinOf[to];
        if(island[to]) {}   // it parts round
        else if(b>=0&&b!=inBasin) {
            float hold=creep*std::clamp(basins[b].volume/feed,3.0f,90.0f)*(strong?4.0f:1.0f);
            if(!strong) {
                if(budget<at+hold) {out.thin[k]=thin;out.front=std::min(budget,at);return;}
                budget-=hold;
            }
        } else if(b<0&&rise>stream) {
            if(strong)thin*=0.5f;
            else {
                float tall=std::min(rise,pileHeight);
                float hold=creep*std::clamp(2.4f*g.width*pileLength*tall*0.5f/feed,3.0f,90.0f);
                float full=std::clamp((budget-at)/hold,0.0f,1.0f);
                out.piles.push_back({at,stream+(tall-stream)*full});
                if(rise>climb||budget<at+hold) {out.thin[k]=thin;out.front=std::min(budget,at);return;}
                budget-=hold;
            }
        }
        if(b!=inBasin)inBasin=b;
        out.thin[k]=thin;
        along+=l;
    }
    out.front=budget;
}

// How deep a sector lies under its basin's rim (0 in none).
float dryIceBasinRim(int sector) {
    if(sector<0||sector>=(int)basinOf.size()||basinOf[sector]<0)return 0;
    return std::max(0.0f,basins[basinOf[sector]].level-floorZ(&sectors[sector]));
}
bool dryIceIsland(int sector) {return sector>=0&&sector<(int)island.size()&&island[sector];}
float dryIcePileLength() {return pileLength;}
int dryIceBasinCount() {return (int)basins.size();}
void dryIceBasinsShutdown() {basins.clear();basinOf.clear();island.clear();}
} // namespace doom3d
