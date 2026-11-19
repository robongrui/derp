/* The baked lamp light round a door as it moves, and where a lamp went out
   or was switched: only what changed is traced again (relight.cpp,
   whole-level lighting). */
#include "internal.h"
#include "light_rooms.h"

namespace doom3d {
// With whole-level lighting every lamp reaches a long way, and baking a texel
// anew means tracing every lamp within that reach: a second or more for the
// floor round one door, so the door had opened before its first light showed.
// But a moving door changes one thing only: whether the straight way from a
// lamp to a texel, where it runs over the door, is open. So for each texel
// only the lamps whose way to it crosses the door's box are traced, as the map
// stood before and as it stands now, and the difference is added to what the
// texel holds. Nearly every lamp drops out at the box test, and the light
// comes through with the door.
// What it leaves: the direction and flicker texels and the bump basis (the
// light through a door is a small share of them), window panels (capped as a
// group), and a rounding of half a step per move; the exact bake that follows
// once the door rests takes all three up (relight.cpp's far jobs).
namespace {
// Whether the way from a to b passes the box (x0, y0, x1, y1), grown.
bool crosses(const std::array<float,4> &box,float ax,float ay,float bx,float by,float grow) {
    float t0=0,t1=1,d[2]={bx-ax,by-ay},a[2]={ax,ay},low[2]={box[0]-grow,box[1]-grow},high[2]={box[2]+grow,box[3]+grow};
    for(int k=0;k<2;++k) {
        if(std::fabs(d[k])<1e-6f) {if(a[k]<low[k]||a[k]>high[k])return false;continue;}
        float u=(low[k]-a[k])/d[k],v=(high[k]-a[k])/d[k];
        if(u>v)std::swap(u,v);
        t0=std::max(t0,u);t1=std::min(t1,v);
        if(t0>t1)return false;
    }
    return true;
}
void gatherMoved(const BakeMap &before,const BakeMap &now,const std::vector<std::array<float,4>> &boxes,int sector,
                 float x,float y,float z,const float normal[3],uint8_t *out) {
    int points=(int)bakeSources.size();
    float delta[3]={};bool any=false;
    const bool inSector=bakeInsideSector(now,sector,x,y);   // outside: every light, as gatherStatic
    for(int n:bakeGridLights(x,y)) {
        if(n>=firstWindowSource&&n<points)continue;
        if(inSector&&!lightReachesSector(n,sector))continue;   // behind walls from here (light_rooms.cpp)
        float lx,ly,grow;
        if(n<points) {const BakeLight &light=bakeSources[n];lx=light.x;ly=light.y;grow=light.tileCount>1?144.0f:16.0f;}
        else {
            const BakeArea &area=bakeAreas[n-points];
            lx=(area.low[0]+area.high[0])*0.5f;ly=(area.low[1]+area.high[1])*0.5f;
            grow=std::max(area.high[0]-area.low[0],area.high[1]-area.low[1])*0.5f+16;
        }
        bool hit=false;
        for(const auto &box:boxes)if(crosses(box,x,y,lx,ly,grow)) {hit=true;break;}
        if(!hit)continue;
        float was[3]={},is[3]={};
        if(n<points) {addBakedLight(before,bakeSources[n],sector,x,y,z,normal,was);addBakedLight(now,bakeSources[n],sector,x,y,z,normal,is);}
        else {addBakedArea(before,bakeAreas[n-points],sector,x,y,z,normal,was);addBakedArea(now,bakeAreas[n-points],sector,x,y,z,normal,is);}
        for(int c=0;c<3;++c)delta[c]+=is[c]-was[c];
        any=true;
    }
    if(!any)return;
    for(int c=0;c<3;++c)out[c]=(uint8_t)std::clamp(out[c]+std::lround(delta[c]*0.5f*255),0L,255L);
}
}
// A lamp that went out or was switched changes one thing too: its own light.
// So the texels in its reach lose what it gave (was) and take what it gives
// now, and no other lamp is traced: the lamp is dark in a frame or two over
// all its reach, where baking those texels anew took over a second.
// The texel's flicker mark (its direction texel's alpha: group*16 + the share
// of its light that group gives) goes with the light: a flickering lamp that
// is gone must not leave the light of the others blinking in its rhythm.
// Far from the lamp it is traced as one point, not tile by tile: its shadows
// are soft there anyway, and a fixture of many tiles among many others cost
// a dozen traces a texel.
// What it leaves: a texel at the top of its range held less than its lamps
// gave, so it comes out a little too dark, and the light's direction; the
// exact bake of the lamp's near surroundings follows when all is quiet.
namespace {
constexpr float tilesWithin=320;   // nearer than this a lamp is traced by its tiles
double lumaOf(const float rgb[3]) {return 0.299*rgb[0]+0.587*rgb[1]+0.114*rgb[2];}
void gatherChanged(const BakeMap &map,const ChangedLamps &lamps,int sector,float x,float y,float z,const float normal[3],uint8_t *out,uint8_t *mark) {
    float delta[3]={};
    double groups[flickerGroups]={};   // what each flicker group's lamps gain here (mostly: lose)
    bool any=false;
    for(size_t n=0;n<lamps.was.size();++n) {
        float before[3]={},after[3]={};
        for(int now=0;now<2;++now) {
            const BakeLight &lamp=now?lamps.now[n]:lamps.was[n];
            if(lamp.strength<=0)continue;
            float dx=lamp.x-x,dy=lamp.y-y;
            if(lamp.tileCount>1&&dx*dx+dy*dy>tilesWithin*tilesWithin) {
                BakeLight point=lamp;point.tiles=nullptr;point.tileCount=0;
                addBakedLight(map,point,sector,x,y,z,normal,now?after:before);
            } else addBakedLight(map,lamp,sector,x,y,z,normal,now?after:before);
        }
        float change[3]={after[0]-before[0],after[1]-before[1],after[2]-before[2]};
        if(change[0]==0&&change[1]==0&&change[2]==0)continue;
        for(int c=0;c<3;++c)delta[c]+=change[c];
        groups[lamps.group[n]]+=lumaOf(change);
        any=true;
    }
    if(!any)return;
    float held[3]={out[0]/255.0f*2,out[1]/255.0f*2,out[2]/255.0f*2};
    for(int c=0;c<3;++c)out[c]=(uint8_t)std::clamp(out[c]+std::lround(delta[c]*0.5f*255),0L,255L);
    if(!mark)return;
    // The mark: its group's light before, plus what that group's lamps gained, over the light now.
    int group=*mark>>4;
    double mine=group?lumaOf(held)*(*mark&15)/15.0:0;
    if(!group) {   // none yet: a group whose lamps came on here takes it
        if(*mark)return;   // a pool's sheen share
        for(int g=1;g<flickerGroups;++g)if(groups[g]>0&&groups[g]>groups[group])group=g;
        if(!group)return;
    }
    float left[3]={out[0]/255.0f*2,out[1]/255.0f*2,out[2]/255.0f*2};
    double all=lumaOf(left);
    long share=all>0?std::lround(std::clamp((mine+groups[group])/all,0.0,1.0)*15):0;
    *mark=share>0?(uint8_t)(group*16+share):0;
}
}
void lightCellChanged(const BakeMap &map,const ChangedLamps &lamps,size_t i,bool only) {
    static const float up[3]={0,0,1},down[3]={0,0,-1};
    int sector=seamCells[i].own;
    if(sector==noSector)return;
    float px=mapOrigin[0]+(i%mapWidth+0.5f)*mapCell,py=mapOrigin[1]+(i/mapWidth+0.5f)*mapCell;
    const BakeSector &s=map.sectors[sector];
    const size_t layer=(size_t)mapWidth*mapHeight;
    if(sectors[sector].floorpic!=skyflatnum)
        gatherChanged(map,lamps,sector,px,py,s.floor+1,up,flatCell(floorLight,i),only?&flatDirectionCells[4*i+3]:nullptr);
    if(sectors[sector].ceilingpic!=skyflatnum)
        gatherChanged(map,lamps,sector,px,py,s.ceiling-1,down,flatCell(ceilingLight,i),only?&flatDirectionCells[4*(layer+i)+3]:nullptr);
}
void lightStripChanged(const BakeMap &map,const ChangedLamps &lamps,const BakeStrip &strip,bool only) {
    const float normal[3]={strip.dy,-strip.dx,0};
    forStripTexels(strip,[&](size_t i,float x,float y,float z) {
        gatherChanged(map,lamps,strip.sector,x,y,z,normal,&wallBakeCells[4*i],only?&wallDirectionCells[4*i+3]:nullptr);
    });
}
// A floor/ceiling cell, as lightCell. The moved sectors' own cells are baked whole (their heights changed).
void lightCellMoved(const BakeMap &before,const BakeMap &now,const std::vector<std::array<float,4>> &boxes,const std::vector<char> &moved,size_t i) {
    static const float up[3]={0,0,1},down[3]={0,0,-1};
    int sector=seamCells[i].own;
    if(sector==noSector)return;
    if(moved[sector]) {lightCell(now,i,false);return;}
    float px=mapOrigin[0]+(i%mapWidth+0.5f)*mapCell,py=mapOrigin[1]+(i/mapWidth+0.5f)*mapCell;
    const BakeSector &s=now.sectors[sector];
    if(sectors[sector].floorpic!=skyflatnum)gatherMoved(before,now,boxes,sector,px,py,s.floor+1,up,flatCell(floorLight,i));
    if(sectors[sector].ceilingpic!=skyflatnum)gatherMoved(before,now,boxes,sector,px,py,s.ceiling-1,down,flatCell(ceilingLight,i));
}
// A wall strip, as lightStrip. Strips of the moved sectors and of the walls round them are baked whole.
void lightStripMoved(const BakeMap &before,const BakeMap &now,const std::vector<std::array<float,4>> &boxes,const std::vector<char> &moved,const BakeStrip &strip) {
    float bx=strip.ax+strip.dx*strip.length,by=strip.ay+strip.dy*strip.length;
    bool whole=strip.sector>=0&&strip.sector<(int)moved.size()&&moved[strip.sector];
    for(const auto &box:boxes)
        if(std::max(strip.ax,bx)>=box[0]-1&&std::min(strip.ax,bx)<=box[2]+1&&std::max(strip.ay,by)>=box[1]-1&&std::min(strip.ay,by)<=box[3]+1)whole=true;
    if(whole) {lightStrip(now,strip,false);return;}
    const float normal[3]={strip.dy,-strip.dx,0};
    forStripTexels(strip,[&](size_t i,float x,float y,float z) {gatherMoved(before,now,boxes,strip.sector,x,y,z,normal,&wallBakeCells[4*i]);});
}
} // namespace doom3d
