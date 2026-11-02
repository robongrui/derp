/* Re-bricked walls: textures painted as oversized bricks (DOOM's 64 x 32
   ones stand two metres by one beside the player) laid anew with bricks a
   half or a quarter the size, from their own artwork. The bricks are found
   from the seams (wallTiles, painted_tiles.h: rectangles from one seam to the
   next). Each big brick's border strips, its mortar and its painted bevels,
   are measured from the bricks' mean brightness row by row and column by
   column; each small brick keeps those strips at their painted thickness and
   corners whole, framing a piece of a big brick's face taken from a random
   spot, as a nine-slice does. The small bricks are laid in running bond,
   each from its own big brick and spot, so the wall varies without any
   mortar being cut where none was painted. Only plain brick textures: one
   brick size over the whole texture. Whole texels in the WAD's own palette
   indices. */
#include "internal.h"

namespace doom3d {
namespace {
struct Cell { int x,y,w,h; };
// The texture's cells, from the anchors wallTiles gives each texel: one per
// tile, at its anchor; rows without joints (noTileAxis) have none.
std::vector<Cell> paintedCells(const std::vector<uint16_t> &anchors,int w,int h,size_t &covered) {
    std::map<std::pair<int,int>,Cell> found;covered=0;
    for(int y=0;y<h;++y)for(int x=0;x<w;++x) {
        uint16_t a=anchors[(size_t)y*w+x];
        if(a==noPaintedTile)continue;
        int dx=(a>>8)-128,dy=(a&255)-128;
        if(dx==noTileAxis||dy==noTileAxis)continue;
        ++covered;
        int ax=((x+dx)%w+w)%w,ay=((y+dy)%h+h)%h;
        Cell &c=found.try_emplace({ax,ay},Cell{ax,ay,1,1}).first->second;
        c.w=std::max(c.w,1-dx);c.h=std::max(c.h,1-dy);
    }
    std::vector<Cell> cells;
    for(auto &[at,c]:found)cells.push_back(c);
    return cells;
}
// How many strips at each end of a brightness profile stand apart from its
// middle: the mortar and bevels. At least one at the start (the seam), at
// most a third of the brick.
std::pair<int,int> borderStrips(const std::vector<float> &profile) {
    int n=(int)profile.size();
    std::vector<float> middle(profile.begin()+n/4,profile.begin()+n-n/4);
    std::sort(middle.begin(),middle.end());
    float centre=middle[middle.size()/2],spread=0;
    for(float v:middle)spread+=std::fabs(v-centre);
    float limit=std::max(0.012f,3*spread/middle.size());
    int start=0,end=0;
    while(start<n/3&&std::fabs(profile[start]-centre)>limit)++start;
    while(end<n/3&&std::fabs(profile[n-1-end]-centre)>limit)++end;
    return {std::max(start,1),end};
}
}
// The texture (w x h, index, coverage pairs) re-laid with bricks 1/split the
// size, the same size; empty when it is no plain brick texture or its bricks
// are too small to split. why, when given, says what it found.
std::vector<byte> rebrickedWall(int w,int h,const std::vector<byte> &pixels,int split,std::string *why) {
    if(why)why->clear();
    auto refuse=[&](const char *reason){if(why)*why=why->empty()?reason:*why+": "+reason;return std::vector<byte>();};
    size_t texels=(size_t)w*h;
    if(split<2||w<16||h<16||pixels.size()<texels*2)return refuse("TOO SMALL");
    for(size_t i=0;i<texels;++i)if(pixels[2*i+1]!=255)return refuse("SEE-THROUGH");
    const byte *palette=playpal();
    std::vector<float> height(texels),luma(texels);
    for(size_t i=0;i<texels;++i) {
        const byte *p=palette+pixels[2*i]*3;
        luma[i]=(0.299f*p[0]+0.587f*p[1]+0.114f*p[2])/255.0f;
        height[i]=std::log(luma[i]+0.03f);
    }
    size_t covered;
    std::vector<Cell> cells=paintedCells(wallTiles(height,w,h),w,h,covered);
    if(cells.size()<2)return refuse("NO BRICKS FOUND");
    if(covered<texels*95/100)return refuse("NOT ALL BRICK");
    int cw=cells[0].w,ch=cells[0].h;
    for(const Cell &c:cells)if(c.w!=cw||c.h!=ch)return refuse("MIXED BRICK SIZES");
    auto texel=[&](int x,int y){return ((size_t)((y%h+h)%h))*w+(x%w+w)%w;};
    // The bricks' mean brightness down and across them, for the strips.
    std::vector<float> rows(ch),columns(cw);
    for(const Cell &c:cells)for(int y=0;y<ch;++y)for(int x=0;x<cw;++x) {
        float v=luma[texel(c.x+x,c.y+y)];rows[y]+=v;columns[x]+=v;
    }
    auto [top,bottom]=borderStrips(rows);
    auto [left,right]=borderStrips(columns);
    int sw=cw/split,sh=ch/split,iw=sw-left-right,ih=sh-top-bottom;
    char found[96];
    snprintf(found,sizeof found,"%d BRICKS %dX%d, STRIPS T%d B%d L%d R%d",(int)cells.size(),cw,ch,top,bottom,left,right);
    if(why)*why=found;
    if(iw<2||ih<2||w%sw||h%sh||sw%2)return refuse("BRICKS TOO SMALL TO SPLIT");
    std::vector<byte> out(pixels);
    int ox=cells[0].x,oy=cells[0].y,courses=h/sh,perRow=w/sw;
    uint32_t seed=hash32((uint32_t)(w*31+h)^(uint32_t)split*0x9e3779b9u);
    for(int row=0;row<courses;++row)for(int i=0;i<perRow;++i) {
        uint32_t pick=hash32(seed^(uint32_t)row*73856093u^(uint32_t)i*19349663u);
        const Cell &from=cells[pick%cells.size()];
        int fx=left+(int)(hash32(pick+1)%(uint32_t)(cw-left-right-iw+1));
        int fy=top+(int)(hash32(pick+2)%(uint32_t)(ch-top-bottom-ih+1));
        int bx=ox+(row%2)*(sw/2)+i*sw,by=oy+row*sh;
        for(int v=0;v<sh;++v)for(int u=0;u<sw;++u) {
            int sx=u<left?u:u>=sw-right?cw-(sw-u):fx+(u-left);
            int sy=v<top?v:v>=sh-bottom?ch-(sh-v):fy+(v-top);
            memcpy(&out[2*texel(bx+u,by+v)],&pixels[2*texel(from.x+sx,from.y+sy)],2);
        }
    }
    return out;
}
}
