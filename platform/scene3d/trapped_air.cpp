/* Trapped air: where the level's wind (wind.cpp) can't blow straight across
   the ground. Air in a hole in a ceiling, a pit or the wind shadow of a tall
   wall is caught out of the prevailing wind and eddies, so what falls
   there comes down steeper and from every side, spread out over the floor
   instead of pushed to one end (bakeWetness spreads its paths by it).
   Baked per map cell for the wind's heading, like the wetness it serves:
   an open cell is as trapped as the share of six shallow paths back up the
   wind (rising one unit in six and in three, a little either side of the
   heading) that something stops before they reach the sky: a wall stands
   in the way within six or three times its height. A cell under a roof
   takes the air of the nearest open cell, where what blows in comes from.
   The open air under a roof (a porch, a doorless recess off a yard, rooms.c)
   is open ground for it as for the ground's bake: its roof stops nothing,
   or the porch's roof marked a band of the yard behind it as trapped, its
   edge running straight downwind from the porch's corner.
   Render-only. */
#include "internal.h"
#include "../rooms.h"

namespace doom3d {
namespace {
std::vector<uint8_t> trapped; // 0 open to the wind - 255 caught, per map cell
unsigned trappedSerial=~0u;
float trappedHeading[2]={0,0};
}
void bakeTrappedAir(float hx,float hy) {
    size_t cells=(size_t)mapWidth*mapHeight;
    if(seamCells.size()<cells||bakeMap.sectors.size()!=(size_t)numsectors)return;
    if(trappedSerial==r_levelserial&&trapped.size()==cells&&trappedHeading[0]==hx&&trappedHeading[1]==hy)return;
    trappedSerial=r_levelserial;trappedHeading[0]=hx;trappedHeading[1]=hy;
    trapped.assign(cells,0);
    std::vector<char> open(cells,0),roofless(numsectors,0);
    for(int s=0;s<numsectors;++s)roofless[s]=Rooms_Outside(s)&&!underSky(s);
    for(size_t i=0;i<cells;++i) {int own=seamCells[i].own;open[i]=own!=noSector&&(underSky(own)||roofless[own]);}
    uint32_t seed=weatherSeed()^0x1b56c4e9u;
    parallelFor(mapHeight,[&](int cy) {
        for(int cx=0;cx<mapWidth;++cx) {
            size_t i=(size_t)cy*mapWidth+cx;
            if(!open[i])continue;
            int own=seamCells[i].own;
            float x=mapOrigin[0]+(cx+0.5f)*mapCell,y=mapOrigin[1]+(cy+0.5f)*mapCell,z=bakeMap.sectors[own].floor+1;
            float turn=(hashUnit(cx,cy,seed)-0.5f)*0.1f;
            int stopped=0;
            for(int n=0;n<6;++n) {
                float yaw=turn+(n%3-1)*0.25f,rise=n<3?1/6.0f:1/3.0f,c=std::cos(yaw),s=std::sin(yaw),length=std::sqrt(1+rise*rise);
                float d[3]={-(hx*c-hy*s)/length,-(hx*s+hy*c)/length,rise/length};
                stopped+=!traceRay(bakeMap,own,x,y,z,d,INFINITY,SkyCeiling::escape,nullptr,&roofless);
            }
            trapped[i]=(uint8_t)std::lround(stopped/6.0f*255);
        }
    });
    // Under a roof: the nearest open cell's air (a two-pass chamfer carrying
    // the value along with the distance).
    std::vector<uint16_t> near(cells,UINT16_MAX);
    for(size_t i=0;i<cells;++i)if(open[i])near[i]=0;
    auto relax=[&](size_t i,int cx,int cy,int dx,int dy) {
        int x=cx+dx,y=cy+dy;
        if(x<0||y<0||x>=mapWidth||y>=mapHeight)return;
        size_t from=(size_t)y*mapWidth+x;
        if(near[from]<UINT16_MAX&&near[from]+1<near[i]) {near[i]=(uint16_t)(near[from]+1);trapped[i]=trapped[from];}
    };
    for(int cy=0;cy<mapHeight;++cy)for(int cx=0;cx<mapWidth;++cx) {
        size_t i=(size_t)cy*mapWidth+cx;
        relax(i,cx,cy,-1,0);relax(i,cx,cy,0,-1);relax(i,cx,cy,-1,-1);relax(i,cx,cy,1,-1);
    }
    for(int cy=mapHeight-1;cy>=0;--cy)for(int cx=mapWidth-1;cx>=0;--cx) {
        size_t i=(size_t)cy*mapWidth+cx;
        relax(i,cx,cy,1,0);relax(i,cx,cy,0,1);relax(i,cx,cy,1,1);relax(i,cx,cy,-1,1);
    }
}
float trappedAir(size_t cell) {return cell<trapped.size()?trapped[cell]/255.0f:0.0f;}
bool trappedAirBaked() {return trappedSerial==r_levelserial&&trapped.size()==(size_t)mapWidth*mapHeight;}
} // namespace doom3d
