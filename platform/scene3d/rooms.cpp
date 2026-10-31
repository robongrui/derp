/* The room map per map cell (rooms.c works it out per sector): roof cover,
   enclosure and wind exposure, kept apart because they part ways: a porch
   keeps the rain off but not the wind, a hole in a ceiling lets the rain
   in while the hall round it stays calm. Enclosure grows under a porch
   with the distance from its open edge (192 units to closed in), so the
   back of a deep one is sheltered, but no warmer: it is still outside.
   Under the sky it is the room map's (Rooms_Enclosure), not the sun's: a
   courtyard ringed by porches is open air.
   Indoors the wind is the drafts' air, fading from the openings it moves
   through (buildIndoorWind).
   Render-only. */
#include "internal.h"
#include "../rooms.h"
extern "C" {
#include "../audio/sound_options.h"
}

namespace doom3d {
namespace {
std::vector<uint16_t> fromSky;   // per cell, cells to the nearest open sky cell
unsigned cellSerial=~0u;
// The distance under a roof to the open sky (a two-pass chamfer over the cells).
void build() {
    size_t cells=(size_t)mapWidth*mapHeight;
    if(cellSerial==r_levelserial&&fromSky.size()==cells)return;
    cellSerial=r_levelserial;
    Rooms_Build();
    fromSky.assign(cells,UINT16_MAX);
    for(size_t i=0;i<cells;++i) {int own=seamCells[i].own;if(own!=noSector&&Rooms_Kind(own)==ROOM_SKY)fromSky[i]=0;}
    auto relax=[&](size_t i,int x,int y) {
        if(x<0||y<0||x>=mapWidth||y>=mapHeight)return;
        uint16_t from=fromSky[(size_t)y*mapWidth+x];
        if(from<UINT16_MAX&&from+1<fromSky[i])fromSky[i]=(uint16_t)(from+1);
    };
    for(int cy=0;cy<mapHeight;++cy)for(int cx=0;cx<mapWidth;++cx) {
        size_t i=(size_t)cy*mapWidth+cx;if(seamCells[i].own==noSector)continue;
        relax(i,cx-1,cy);relax(i,cx,cy-1);relax(i,cx-1,cy-1);relax(i,cx+1,cy-1);
    }
    for(int cy=mapHeight-1;cy>=0;--cy)for(int cx=mapWidth-1;cx>=0;--cx) {
        size_t i=(size_t)cy*mapWidth+cx;if(seamCells[i].own==noSector)continue;
        relax(i,cx+1,cy);relax(i,cx,cy+1);relax(i,cx+1,cy+1);relax(i,cx-1,cy+1);
    }
}
int ownAt(size_t cell) {
    if(cell>=seamCells.size()||seamCells.size()<(size_t)mapWidth*mapHeight)return -1;
    int own=seamCells[cell].own;
    return own==noSector?-1:own;
}
// The wind indoors is the drafts' air (drafts.cpp): where it comes in, goes
// through or leaves at an opening, as fast as it moves there against the
// wind outside, fading with the distance over the floor (to half in 64
// units), never through a wall, a shut door or whole glass. A room the air
// doesn't move through (a dead end, a deep garage, a sealed room) lies
// calm. On coarse cells (about 16 units), built anew when the openings'
// flows change by a twentieth, looked at once a tic (roomWind is asked
// per cell: the floor film's gather asks it for every cell of the map).
constexpr float windHalf=64;
std::vector<float> indoorWind;
int windStep=1,windW=0,windH=0;
unsigned windSerial=~0u;
double windFlows=-1;
int windLooked=-1;   // the tic the flows were last looked at
int coarseOf(size_t cell) {
    int cx=(int)(cell%mapWidth)/windStep,cy=(int)(cell/mapWidth)/windStep;
    return cy*windW+cx;
}
void buildIndoorWind() {
    if(windSerial==r_levelserial&&windLooked==leveltime)return;
    windLooked=leveltime;
    double flows=0;
    for(int i=0;i<draftLinkCount();++i) {DraftLinkView v;if(draftLink(i,v))flows+=std::fabs(v.flow);}
    if(windSerial==r_levelserial&&std::fabs(flows-windFlows)<=0.05*std::max(windFlows,1.0))return;
    windSerial=r_levelserial;windFlows=flows;
    windStep=std::max(1,(int)std::lround(16/mapCell));
    windW=(mapWidth+windStep-1)/windStep;windH=(mapHeight+windStep-1)/windStep;
    std::vector<int> own((size_t)windW*windH,-1);
    for(size_t c=0;c<own.size();++c) {
        int fx=std::min(mapWidth-1,(int)(c%windW)*windStep+windStep/2),fy=std::min(mapHeight-1,(int)(c/windW)*windStep+windStep/2);
        int s=seamCells[(size_t)fy*mapWidth+fx].own;
        if(s!=noSector&&!Rooms_Outside(s))own[c]=s;
    }
    indoorWind.assign(own.size(),0);
    // The sector pairs the air passes between now: an open line (a gap over 8
    // units), not whole glass.
    std::vector<char> whole(numlines,0);
    for(int i=0;i<windowPaneCount();++i) {PaneView p;if(windowPane(i,p)&&!p.broken&&p.line>=0&&p.line<numlines)whole[p.line]=1;}
    std::unordered_set<uint64_t> joined;
    for(int i=0;i<numlines;++i) {
        const line_t &line=lines[i];
        if(!line.frontsector||!line.backsector||whole[i])continue;
        if(std::min(ceilZ(line.frontsector),ceilZ(line.backsector))-std::max(floorZ(line.frontsector),floorZ(line.backsector))<8)continue;
        uint64_t a=(uint64_t)(line.frontsector-sectors),b=(uint64_t)(line.backsector-sectors);
        joined.insert(a<b?a<<32|b:b<<32|a);
    }
    auto passes=[&](int a,int b) {
        if(a==b)return true;
        uint64_t x=(uint64_t)a,y=(uint64_t)b;
        return joined.count(x<y?x<<32|y:y<<32|x)>0;
    };
    // Each opening's air, against the wind outside, on the cells along it.
    float wind=std::max(1.0f,std::hypot(windNow[0],windNow[1]));
    for(int i=0;i<draftLinkCount();++i) {
        DraftLinkView v;
        if(!draftLink(i,v)||v.speed<=0)continue;
        float strength=std::min(1.0f,v.speed/wind),length=std::hypot(v.bx-v.ax,v.by-v.ay);
        int samples=std::max(1,(int)std::ceil(length/(windStep*mapCell*0.5f)));
        for(int k=0;k<=samples;++k)for(float side:{-1.0f,1.0f}) {
            float t=(float)k/samples,x=v.ax+(v.bx-v.ax)*t+v.nx*side*4,y=v.ay+(v.by-v.ay)*t+v.ny*side*4;
            int cx=(int)std::floor((x-mapOrigin[0])/mapCell),cy=(int)std::floor((y-mapOrigin[1])/mapCell);
            if(cx<0||cy<0||cx>=mapWidth||cy>=mapHeight)continue;
            int c=coarseOf((size_t)cy*mapWidth+cx);
            if(own[c]>=0)indoorWind[c]=std::max(indoorWind[c],strength);
        }
    }
    // Spread over the floor, fading, two passes each way.
    float along=std::exp2(-windStep*mapCell/windHalf),across=std::exp2(-windStep*mapCell*1.4142f/windHalf);
    auto relax=[&](int c,int x,int y,float fade) {
        if(x<0||y<0||x>=windW||y>=windH)return;
        int n=y*windW+x;
        if(own[n]<0||!passes(own[c],own[n]))return;
        indoorWind[c]=std::max(indoorWind[c],indoorWind[n]*fade);
    };
    for(int pass=0;pass<2;++pass) {
        for(int y=0;y<windH;++y)for(int x=0;x<windW;++x) {
            int c=y*windW+x;if(own[c]<0)continue;
            relax(c,x-1,y,along);relax(c,x,y-1,along);relax(c,x-1,y-1,across);relax(c,x+1,y-1,across);
        }
        for(int y=windH-1;y>=0;--y)for(int x=windW-1;x>=0;--x) {
            int c=y*windW+x;if(own[c]<0)continue;
            relax(c,x+1,y,along);relax(c,x,y+1,along);relax(c,x+1,y+1,across);relax(c,x-1,y+1,across);
        }
    }
}
}

bool underSky(int sector) {
    if(sector<0||sector>=numsectors)return false;
    Rooms_Build();
    return Rooms_Kind(sector)==ROOM_SKY;
}
float roomRoof(size_t cell) {
    int own=ownAt(cell);
    if(own<0)return NAN;
    return Rooms_Kind(own)==ROOM_SKY?0.0f:1.0f;
}
float roomEnclosure(size_t cell) {
    int own=ownAt(cell);
    if(own<0)return NAN;
    build();
    switch(Rooms_Kind(own)) {
    case ROOM_SKY: return Rooms_Enclosure(own);
    case ROOM_SHELTERED: {
        float t=std::clamp(fromSky[cell]*mapCell/192.0f,0.0f,1.0f);
        return 0.2f+0.8f*t*t*(3-2*t);
    }
    default: return 1;
    }
}
float roomWind(size_t cell) {
    int own=ownAt(cell);
    if(own<0||!trappedAirBaked()||falling()==WEATHER_NONE)return NAN;
    room_kind kind=Rooms_Kind(own);
    if(kind==ROOM_INDOOR||kind==ROOM_OPENING) {
        buildIndoorWind();
        int c=coarseOf(cell);
        return c>=0&&c<(int)indoorWind.size()?indoorWind[c]:0.0f;
    }
    float open=1-trappedAir(cell);
    return kind==ROOM_SKY?open:open*(1-roomEnclosure(cell));
}
void hearIndoorWind() {
    float draft=0;
    const mobj_t *mo=players[consoleplayer].mo;
    if(mo&&seamCells.size()>=(size_t)mapWidth*mapHeight) {
        int cx=(int)std::floor((units(mo->x)-mapOrigin[0])/mapCell),cy=(int)std::floor((units(mo->y)-mapOrigin[1])/mapCell);
        if(cx>=0&&cy>=0&&cx<mapWidth&&cy<mapHeight) {
            size_t cell=(size_t)cy*mapWidth+cx;int own=ownAt(cell);
            if(own>=0&&!Rooms_Outside(own)) {
                float wind=roomWind(cell);
                if(!std::isnan(wind))draft=wind;
            }
        }
    }
    I_AmbienceDraft(draft);
}
} // namespace doom3d
