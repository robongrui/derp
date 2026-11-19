/* The bounce's light from one pass to the next (BounceSolve::run, bake_bounce.cpp), in half
   floats (PLAN-bounce-energy.md step 1). The level keeps its bounce in 8 bits at half scale, at
   most 2.0: drawn, that is plenty, but the passes before the last carry the sky and the sun at
   their true strength (skyInGain, sunInGain), many times that, and in 8 bits it was clipped at
   2.0 before the next pass could pass it on; faint light from the later passes rounded away at
   the other end. Half floats keep both ends. Only the passes between see it: the last writes
   the level's 8 bits as before.
   Two buffers, read and written in turn: a pass gathers from the one the pass before wrote and
   writes the other. Both start as the level's bounce, so what a region's solve does not reach
   (rays landing outside its rects) reads the same in both. Per buffer: the flats' two bounce
   layers (floor, then ceiling, a cell each), then the atlas' bounce half (a texel each, by
   row*atlasWidth+column), three halves each. */
#ifndef DOOM_SCENE3D_BOUNCE_CARRY_H
#define DOOM_SCENE3D_BOUNCE_CARRY_H
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <vector>

namespace doom3d {

inline uint16_t floatToHalf(float value) {
    if(!(value>0))return 0;   // the light is never negative; NaN and zero alike
    if(value>=65504.0f)return 0x7BFF;
    uint32_t bits;std::memcpy(&bits,&value,4);
    int exponent=(int)((bits>>23)&255)-127+15;
    uint32_t mantissa=bits&0x7FFFFF;
    if(exponent<=0) {   // below the halves' normal range
        if(exponent<-10)return 0;
        mantissa|=0x800000;
        int shift=14-exponent;
        uint32_t half=mantissa>>shift,rest=mantissa&((1u<<shift)-1),mid=1u<<(shift-1);
        if(rest>mid||(rest==mid&&(half&1)))++half;
        return (uint16_t)half;
    }
    uint32_t half=(uint32_t)exponent<<10|mantissa>>13,rest=mantissa&0x1FFF;
    if(rest>0x1000||(rest==0x1000&&(half&1)))++half;   // to nearest, ties to even
    return (uint16_t)std::min<uint32_t>(half,0x7BFF);
}
inline float halfToFloat(uint16_t half) {
    uint32_t exponent=(half>>10)&31,mantissa=half&1023,bits;
    if(exponent==0) {
        if(!mantissa)return 0;
        float value=mantissa/1024.0f/16384.0f;   // 2^-14 * mantissa/1024
        return half&0x8000?-value:value;
    }
    bits=(uint32_t)(half&0x8000)<<16|(exponent==31?255u:exponent-15+127)<<23|mantissa<<13;
    float value;std::memcpy(&value,&bits,4);
    return value;
}

struct BounceCarry {
    std::vector<uint16_t> buffers[2];
    int reading=0;
    size_t layer=0,wallStart=0;   // in entries (three halves each): wallStart = 2*layer
    int width=0;                  // atlasWidth
    // From the level's 8-bit bounce: the flat cells' four layers (floorBounce and ceilingBounce
    // at bounceLayer and bounceLayer+1) and the atlas (its bounce half from column width on).
    void start(const uint8_t *flatCells,size_t layerCells,int bounceLayer,const uint8_t *wallCells,int atlasWidth,int stride,int height) {
        layer=layerCells;wallStart=2*layer;width=atlasWidth;reading=0;
        std::vector<uint16_t> &first=buffers[0];
        size_t halves=3*(wallStart+(size_t)atlasWidth*height);
        first.assign(halves+(halves&1),0);   // even: the card reads it two halves to a word
        static const auto fromByte=[] {std::vector<uint16_t> t(256);for(int v=0;v<256;++v)t[v]=floatToHalf(v/255.0f*2);return t;}();
        for(int ceiling=0;ceiling<2;++ceiling) {
            const uint8_t *from=flatCells+4*(size_t)(bounceLayer+ceiling)*layer;
            uint16_t *to=&first[3*(size_t)ceiling*layer];
            for(size_t i=0;i<layer;++i)for(int c=0;c<3;++c)to[3*i+c]=fromByte[from[4*i+c]];
        }
        for(int row=0;row<height;++row) {
            const uint8_t *from=wallCells+4*((size_t)row*stride+atlasWidth);
            uint16_t *to=&first[3*(wallStart+(size_t)row*atlasWidth)];
            for(int column=0;column<atlasWidth;++column)for(int c=0;c<3;++c)to[3*column+c]=fromByte[from[4*column+c]];
        }
        buffers[1]=first;
    }
    bool on() const {return !buffers[0].empty();}
    size_t flatEntry(bool ceiling,size_t cell) const {return (ceiling?layer:0)+cell;}
    // An atlas texel by its index in the lamp half (row*stride+column, as the rays keep it).
    size_t wallEntry(size_t at,int stride) const {return wallStart+(at/stride)*width+at%stride;}
    // Every ray that lands reads one: by table, not bit by bit (the processor's passes took a
    // tenth longer so).
    void read(size_t entry,float out[3]) const {
        const float *table=halfTable();
        const uint16_t *h=&buffers[reading][3*entry];
        for(int c=0;c<3;++c)out[c]=table[h[c]];
    }
    void write(size_t entry,const float value[3]) {
        uint16_t *h=&buffers[reading^1][3*entry];
        for(int c=0;c<3;++c)h[c]=floatToHalf(value[c]);
    }
    void flip() {reading^=1;}
    // The light past the last pass (PLAN-bounce-energy.md step 3). Each pass adds a bounce, a share
    // of the one before; at the surfaces' return (about 0.75-0.85 with Surfaces return light at 3) five
    // passes hold half to three quarters of it. So the passes are carried on as a series: the change a
    // pass made against the one before it says how fast they shrink, and the rest of the series is
    // added to what the last pass reads. The last pass then gathers from light as it would stand after
    // all the bounces, and its sky, sun and shoulder are as before.
    // How fast a room's bounces shrink is the room's: its surfaces' return and how closed it is. So the
    // ratio is taken per group (the caller's: a room, or a sector outside), the changes summed over all
    // its entries; per entry it was the rays' noise, and the rest of the series (r/(1-r)) made much of
    // a little noise: E1M6's yard edge overshot the 20-pass reference by more than the tail was.
    // groupOf(entry): its group, below groups, or -1 for none.
    static float luma(const float c[3]) {return 0.299f*c[0]+0.587f*c[1]+0.114f*c[2];}
    static const float *halfTable() {
        static const std::vector<float> table=[] {std::vector<float> t(65536);for(uint32_t h=0;h<65536;++h)t[h]=halfToFloat((uint16_t)h);return t;}();
        return table.data();
    }
    // The entries in chunks, for the solve's runner (run(count, work(chunk))).
    static constexpr int chunks=64;
    size_t chunkEnd(int k) const {size_t n=buffers[reading].size()/3;return n*(size_t)k/chunks;}
    // The luminance a pass added, summed per group: between what it read and what it wrote.
    template<class GroupOf,class Run> void sumChange(std::vector<double> &sums,int groups,const GroupOf &groupOf,const Run &run) const {
        const float *f=halfTable();
        const std::vector<uint16_t> &was=buffers[reading],&is=buffers[reading^1];
        std::vector<std::vector<double>> part(chunks);
        run(chunks,[&](int k) {
            std::vector<double> &s=part[k];s.assign(groups,0);
            for(size_t e=chunkEnd(k),end=chunkEnd(k+1);e<end;++e) {
                int g=groupOf(e);
                if(g<0)continue;
                float d[3];for(int c=0;c<3;++c)d[c]=f[is[3*e+c]]-f[was[3*e+c]];
                s[g]+=luma(d);
            }
        });
        sums.assign(groups,0);
        for(const auto &s:part)for(int g=0;g<groups;++g)sums[g]+=s[g];
    }
    std::vector<double> change;   // noteChange's sums
    // noteChange, after a pass (before flip); extendTail after the pass after it (before flip): the
    // series' rest onto what that one wrote. The ratio is held under most (nothing returns all of it).
    template<class GroupOf,class Run> void noteChange(int groups,const GroupOf &groupOf,const Run &run) {sumChange(change,groups,groupOf,run);}
    template<class GroupOf,class Run> void extendTail(int groups,const GroupOf &groupOf,const Run &run,float most=0.9f) {
        if((int)change.size()!=groups)return;
        std::vector<double> now;
        sumChange(now,groups,groupOf,run);
        std::vector<float> rest(groups,0);
        for(int g=0;g<groups;++g)if(change[g]>1e-3&&now[g]>0) {float r=(float)std::min(now[g]/change[g],(double)most);rest[g]=r/(1-r);}
        const float *f=halfTable();
        const std::vector<uint16_t> &was=buffers[reading];
        std::vector<uint16_t> &is=buffers[reading^1];
        run(chunks,[&](int k) {
            for(size_t e=chunkEnd(k),end=chunkEnd(k+1);e<end;++e) {
                int g=groupOf(e);
                if(g<0||rest[g]<=0)continue;
                for(int c=0;c<3;++c) {
                    float b=f[is[3*e+c]],d=b-f[was[3*e+c]];
                    if(d>0)is[3*e+c]=floatToHalf(b+d*rest[g]);
                }
            }
        });
    }
    const std::vector<uint16_t> &readable() const {return buffers[reading];}
    // A region's solve: from the level's own carry (levelCarry), the cells x0..x1, y0..y1 and the
    // strips' texels: all its rays can reach. The rest is never read; the buffers are kept from
    // one solve to the next (SolveBuffers), so they are not cleared.
    template<class Strip> void startFrom(const std::vector<uint16_t> &level,size_t layerCells,int atlasWidth,
                                         int x0,int y0,int x1,int y1,int mapWidth,const std::vector<int> &indices,const std::vector<Strip> &all) {
        layer=layerCells;wallStart=2*layer;width=atlasWidth;reading=0;
        for(auto &b:buffers)if(b.size()!=level.size())b.resize(level.size());
        auto copy=[&](size_t entry,size_t count) {
            for(auto &b:buffers)std::memcpy(&b[3*entry],&level[3*entry],6*count);
        };
        for(int ceiling=0;ceiling<2;++ceiling)for(int y=y0;y<=y1;++y)copy((ceiling?layer:0)+(size_t)y*mapWidth+x0,(size_t)(x1-x0+1));
        for(int index:indices) {
            const Strip &strip=all[index];
            if(strip.x<0)continue;
            for(int row=0;row<strip.height;++row)copy(wallStart+(size_t)(strip.y+row)*atlasWidth+strip.x,(size_t)strip.width);
        }
    }
};

// The level's carry: what its bounce's last pass read (the pass before it, at true strength).
// A region's solve starts from it (BounceSolve::copyLight), so the light its rays find outside its
// rects is what the level's own last pass found, and a region solved again with nothing changed
// comes out as the level's bake. Kept with the bake (bake_cache.cpp), put in by a region's commit;
// empty when the bounce has one pass or keeps 8 bits (DERP_BOUNCE8). levelCarryLock: a prepared
// shot's solve copies from it on the light worker while the frame thread may commit.
extern std::vector<uint16_t> levelCarry;
extern std::mutex levelCarryLock;
size_t levelCarrySize();   // the size it has for this level when kept
bool bounceEightBits();    // DERP_BOUNCE8 (dev tools)

} // namespace doom3d
#endif
