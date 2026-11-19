/* Static light as things and sprites take it: traced from the baked
   lights, or from a grid baked at level load. */
#include "internal.h"
#include "light_rooms.h"

namespace doom3d {
int staticWidth=0,staticHeight=0;
unsigned staticSerial=0;
std::vector<StaticCell> staticGrid;
static StaticSample traceStatic(float x,float y,float z,int sector,const BakeMap &map=bakeMap) {
    StaticSample sample;
    if(bakeGrid.empty())return sample;
    float groups[flickerGroups]={},window[3]={},windowTowards[3]={},windowAmount=0;
    int points=(int)bakeSources.size();
    const bool inSector=bakeInsideSector(map,sector,x,y);   // outside: every light, as gatherStatic
    for(int n:bakeGridLights(x,y)) {
        if(inSector&&!lightReachesSector(n,sector))continue;   // behind walls from here (light_rooms.cpp)
        if(shutWindowLamp(map,n))continue;
        float towards[3];
        const float *color=n<points?bakeSources[n].color:bakeAreas[n-points].color;
        bool isWindow=n>=firstWindowSource&&n<points;
        float amount=n<points?addBakedLight(map,bakeSources[n],sector,x,y,z,nullptr,isWindow?window:sample.color.data(),towards)
                             :addBakedArea(map,bakeAreas[n-points],sector,x,y,z,nullptr,sample.color.data(),towards);
        if(amount<=0)continue;
        float weight=amount*luminance(color);
        if(isWindow) {for(int c=0;c<3;++c)windowTowards[c]+=towards[c]*weight;windowAmount+=weight;continue;}
        for(int c=0;c<3;++c)sample.towards[c]+=towards[c]*weight;
        sample.amount+=weight;
        // (StaticSample's cast: a pool's light as from over the pool.)
        float way[3]={towards[0],towards[1],towards[2]},flat=std::hypot(way[0],way[1]);
        if(n>=points&&way[2]<flat*0.5f) {
            way[2]=flat*0.5f;
            float length=std::sqrt(flat*flat+way[2]*way[2]);
            for(float &c:way)c=length>1e-4f?c/length:0;
        }
        for(int c=0;c<3;++c)sample.cast[c]+=way[c]*weight;
        if(n>=points) {sample.pool+=weight;continue;}
        for(int c=0;c<3;++c)sample.lampWay[c]+=towards[c]*weight;
        groups[sourceGroups[n]]+=weight;
        if(bakeSources[n].z>z-24&&amount>sample.shadowAmount) {sample.shadowAmount=amount;sample.shadow=n;}
    }
    // Whole-level lighting: the light from no lamp, by the way it comes (StaticSample's sky and
    // glow), for the shadows it throws: a thing by a window is lit from the window, one by a
    // glowing pool from the pool. 48 ways round the sphere: those that leave through the sky,
    // and what the surfaces the others reach send back of the light baked on them (at a
    // typical albedo). Summed as a surface would take them (4 over the count: the sphere
    // against a cosine's half).
    if(settings.lightModel==1&&sector>=0&&!flatLightCells.empty()) {
        constexpr int rays=48;
        auto luma=[](const uint8_t *p){return (0.299f*p[0]+0.587f*p[1]+0.114f*p[2])/255*2;};
        for(int k=0;k<rays;++k) {
            float zc=1-2*(k+0.5f)/rays,r=std::sqrt(std::max(0.0f,1-zc*zc)),a=k*2.39996323f;
            float d[3]={r*std::cos(a),r*std::sin(a),zc};
            BakeHit hit;bool skyward=false;
            if(traceRay(map,sector,x,y,z,d,1024,SkyCeiling::escape,&hit,nullptr,&skyward)) {
                if(skyward&&settings.skyLight)for(int c=0;c<3;++c)sample.sky[c]+=d[c]*4/rays;
                continue;
            }
            float light=0;
            if(hit.kind==BakeHit::wall&&hit.line>=0&&!wallBakeCells.empty()) {
                const BakeStrip &strip=bakeStrips[(size_t)hit.line*2+(map.lines[hit.line].front==hit.sector?0:1)];
                if(strip.x>=0) {
                    int column=std::clamp((int)std::floor(((hit.x-strip.ax)*strip.dx+(hit.y-strip.ay)*strip.dy)*strip.scale),0,strip.width-1);
                    int row=std::clamp((int)std::floor((hit.z-strip.low)*strip.scale),0,strip.height-1);
                    const uint8_t *lamp=&wallBakeCells[4*((size_t)(strip.y+row)*atlasStride+strip.x+column)];
                    light=luma(lamp)+luma(lamp+4*atlasWidth);
                }
            } else if(hit.kind==BakeHit::floor||hit.kind==BakeHit::ceiling) {
                int cx=std::clamp((int)std::floor((hit.x-mapOrigin[0])/mapCell),0,mapWidth-1),cy=std::clamp((int)std::floor((hit.y-mapOrigin[1])/mapCell),0,mapHeight-1);
                size_t cell=(size_t)cy*mapWidth+cx;bool ceiling=hit.kind==BakeHit::ceiling;
                light=luma(flatCell(ceiling?ceilingLight:floorLight,cell))+luma(flatCell(ceiling?ceilingBounce:floorBounce,cell));
            }
            for(int c=0;c<3;++c)sample.glow[c]+=d[c]*light*0.4f*4/rays;
        }
    }
    float scale=capWindowLight(window);
    for(int c=0;c<3;++c) {sample.color[c]+=window[c];sample.towards[c]+=windowTowards[c]*scale;}
    sample.amount+=windowAmount*scale;
    for(int g=1;g<flickerGroups;++g)if(groups[g]>0&&groups[g]>(sample.group?groups[sample.group]:0))sample.group=g;
    if(sample.group)sample.groupShare=groups[sample.group]/sample.amount;
    return sample;
}
// Grid lighting (settings.gridSpriteLight): a StaticSample every 32 units,
// 32 units above the floor of the sector at the cell's center and, in rooms
// taller than 112, also 96 above it. Things blend the nearest cells instead
// of tracing rays every frame. Baked after each light bake (staticSerial);
// cells in void or in closed sectors (doors) hold none.
bool gridLightActive() {
    return settings.gridSpriteLight&&bakedLightsActive&&staticSerial==lightBakeSerial&&!staticGrid.empty();
}
StaticCell staticCellAt(const BakeMap &map,int gx,int gy) {
    StaticCell cell;
    float x=mapOrigin[0]+(gx+0.5f)*staticCell,y=mapOrigin[1]+(gy+0.5f)*staticCell;
    int cx=std::clamp((int)std::floor((x-mapOrigin[0])/mapCell),0,mapWidth-1);
    int cy=std::clamp((int)std::floor((y-mapOrigin[1])/mapCell),0,mapHeight-1);
    int sector=seamCells[(size_t)cy*mapWidth+cx].own;
    if(sector==noSector)return cell;
    const BakeSector &s=map.sectors[sector];
    if(s.ceiling-s.floor<16)return cell;
    cell.used=true;
    cell.low=s.floor+std::min(32.0f,(s.ceiling-s.floor)*0.5f);
    cell.at[0]=traceStatic(x,y,cell.low,sector,map);
    cell.high=cell.low;cell.at[1]=cell.at[0];
    if(s.ceiling-s.floor>112) {cell.high=s.floor+96;cell.at[1]=traceStatic(x,y,cell.high,sector,map);}
    return cell;
}
void bakeStaticGrid() {
    loadingStage(LoadStage::Grid);
    finishRelight();
    staticSerial=lightBakeSerial;staticGrid.clear();
    if(bakeGrid.empty()||seamCells.empty())return;
    uint64_t start=SDL_GetTicksNS();
    staticWidth=(int)std::ceil(mapWidth*mapCell/staticCell);staticHeight=(int)std::ceil(mapHeight*mapCell/staticCell);
    staticGrid.assign((size_t)staticWidth*staticHeight,{});
    std::atomic<size_t> used{0};
    // From an earlier visit if the bake cache has it (bake_cache.cpp).
    if(bakeCacheRestore(LoadStage::Grid,BAKE_BUILT)) {for(const StaticCell &cell:staticGrid)if(cell.used)++used;}
    else {
        parallelFor(staticHeight,[&](int gy) {
            for(int gx=0;gx<staticWidth;++gx) {
                StaticCell &cell=staticGrid[(size_t)gy*staticWidth+gx];
                cell=staticCellAt(bakeMap,gx,gy);
                if(cell.used)++used;
            }
        });
        bakeCacheKeep(LoadStage::Grid);
    }
    fprintf(stderr,"3D light grid: %zu cells of %dx%d, baked in %.0f ms.\n",used.load(),staticWidth,staticHeight,(SDL_GetTicksNS()-start)/1e6);
#ifdef DOOM_DEVTOOLS
    // DERP_BAKEDUMP=<file>: the grid as baked into <file>.grid, to hold one bake against another byte for byte.
    if(const char *dump=getenv("DERP_BAKEDUMP"))if(FILE *f=fopen((std::string(dump)+".grid").c_str(),"wb")) {
        fwrite(staticGrid.data(),sizeof(StaticCell),staticGrid.size(),f);fclose(f);
    }
#endif
}
// The grid's blend at (x,y,z): bilinear over used cells, between each
// cell's two heights. The flicker group is the heaviest cell's; the shadow
// light is the one with the most blended amount.
static StaticSample gridStatic(float x,float y,float z) {
    StaticSample result;
    float gx=(x-mapOrigin[0])/staticCell-0.5f,gy=(y-mapOrigin[1])/staticCell-0.5f;
    int x0=(int)std::floor(gx),y0=(int)std::floor(gy);float fx=gx-x0,fy=gy-y0;
    float total=0,heaviest=0;
    StaticSample parts[4];float weights[4]={};
    for(int k=0;k<4;++k) {
        int cx=x0+(k&1),cy=y0+(k>>1);
        if(cx<0||cy<0||cx>=staticWidth||cy>=staticHeight)continue;
        const StaticCell &cell=staticGrid[(size_t)cy*staticWidth+cx];
        if(!cell.used)continue;
        float w=((k&1)?fx:1-fx)*((k>>1)?fy:1-fy)+1e-4f;
        float t=cell.high>cell.low?std::clamp((z-cell.low)/(cell.high-cell.low),0.0f,1.0f):0;
        const StaticSample &a=cell.at[0],&b=cell.at[1];
        StaticSample &p=parts[k];
        for(int c=0;c<3;++c) {p.color[c]=a.color[c]+(b.color[c]-a.color[c])*t;p.towards[c]=a.towards[c]+(b.towards[c]-a.towards[c])*t;p.sky[c]=a.sky[c]+(b.sky[c]-a.sky[c])*t;p.glow[c]=a.glow[c]+(b.glow[c]-a.glow[c])*t;p.cast[c]=a.cast[c]+(b.cast[c]-a.cast[c])*t;p.lampWay[c]=a.lampWay[c]+(b.lampWay[c]-a.lampWay[c])*t;}
        p.amount=a.amount+(b.amount-a.amount)*t;p.pool=a.pool+(b.pool-a.pool)*t;
        const StaticSample &nearer=t<0.5f?a:b;
        p.group=nearer.group;p.groupShare=nearer.groupShare;p.shadow=nearer.shadow;p.shadowAmount=nearer.shadowAmount;
        weights[k]=w;total+=w;
        if(w*p.amount>heaviest) {heaviest=w*p.amount;result.group=p.group;}
    }
    if(total<=0)return result;
    float share=0,groupWeight=0;
    for(int k=0;k<4;++k) {
        if(weights[k]<=0)continue;
        float w=weights[k]/total;const StaticSample &p=parts[k];
        for(int c=0;c<3;++c) {result.color[c]+=p.color[c]*w;result.towards[c]+=p.towards[c]*w;result.sky[c]+=p.sky[c]*w;result.glow[c]+=p.glow[c]*w;result.cast[c]+=p.cast[c]*w;result.lampWay[c]+=p.lampWay[c]*w;}
        result.amount+=p.amount*w;result.pool+=p.pool*w;
        if(result.group&&p.group==result.group) {share+=p.groupShare*w;groupWeight+=w;}
        if(p.shadow<0)continue;
        float blended=0;
        for(int j=0;j<4;++j)if(weights[j]>0&&parts[j].shadow==p.shadow)blended+=parts[j].shadowAmount*weights[j]/total;
        if(blended>result.shadowAmount) {result.shadowAmount=blended;result.shadow=p.shadow;}
    }
    if(groupWeight>0)result.groupShare=share/groupWeight;
    return result;
}
// traceStatic for what keeps still: a corpse, a lamp, a pickup asks the same point every frame, and the
// trace (a few microseconds a thing) only changes with a bake, a door moving or a lamp shot out
// (liveLightSerial: zombies lit by lamps shot out over them stayed lit). Kept by the exact point; whatever moves
// fills it with points it never asks again, so past a few thousand it becomes the older half, which
// still points are taken back from as they ask: only the movers' are dropped, at the next turn.
static StaticSample tracedStatic(float x,float y,float z,int sector) {
    struct Point { float x,y,z; int sector; bool operator==(const Point &o) const {return x==o.x&&y==o.y&&z==o.z&&sector==o.sector;} };
    struct Hash { size_t operator()(const Point &p) const {
        uint32_t b[3];std::memcpy(b,&p.x,4);std::memcpy(b+1,&p.y,4);std::memcpy(b+2,&p.z,4);
        return hash32(b[0]*73856093u^b[1]*19349663u^b[2]*83492791u^(uint32_t)p.sector);
    } };
    static std::unordered_map<Point,StaticSample,Hash> traced,older;
    static unsigned tracedBake=~0u,tracedLevel=~0u,tracedLive=~0u;
    if(tracedBake!=lightBakeSerial||tracedLevel!=levelSerial||tracedLive!=liveLightSerial) {
        traced.clear();older.clear();tracedBake=lightBakeSerial;tracedLevel=levelSerial;tracedLive=liveLightSerial;
    }
    if(traced.size()>4096) {older=std::move(traced);traced.clear();}
    Point at{x,y,z,sector};
    auto found=traced.find(at);
    if(found!=traced.end())return found->second;
    if(auto kept=older.find(at);kept!=older.end()) {
        StaticSample sample=kept->second;older.erase(kept);
        return traced.emplace(at,sample).first->second;
    }
    return traced.emplace(at,traceStatic(x,y,z,sector)).first->second;
}
// Baked light at a thing's center or the eye: the static lights (from the
// grid when it is on), flickered by group (groupFlicker), plus the bounce
// between the floor and ceiling below and above it. sample receives the
// static light's details.
std::array<float,3> bakedLightAt(float x,float y,float z,const sector_t *sector,StaticSample *sample) {
    StaticSample statics;
    if(bakedLightsActive)statics=gridLightActive()?gridStatic(x,y,z):tracedStatic(x,y,z,(int)(sector-sectors));
    std::array<float,3> result=statics.color;
    fillHeadroom(result.data(),lighting(sector));
    if(statics.group) {
        float flicker=1-statics.groupShare*(1-groupFlicker(statics.group));
        for(float &c:result)c*=flicker;
    }
    if(settings.bounce&&bounceKey>=0&&!flatLightCells.empty()) {
        int cx=std::clamp((int)std::floor((x-mapOrigin[0])/mapCell),0,mapWidth-1);
        int cy=std::clamp((int)std::floor((y-mapOrigin[1])/mapCell),0,mapHeight-1);
        size_t cell=(size_t)cy*mapWidth+cx,layer=(size_t)mapWidth*mapHeight;
        // Whole-level lighting: the bounce flickers by its own share, as on the floor and
        // ceiling it is read from (bake_bounce.cpp's bounceFlicker; the basis texels' alpha).
        // A thing beside a blinking lamp stood lit by the lamp's bounce while the lamp was off.
        float shown[2]={1,1};
        if(flatDirectionCells.size()>=16*layer)for(int ceiling=0;ceiling<2;++ceiling) {
            uint8_t mark=flatDirectionCells[4*((2+ceiling)*layer+cell)+3];
            if(mark>=16)shown[ceiling]=1-(mark&15)/15.0f*(1-groupFlicker(mark>>4));
        }
        for(int c=0;c<3;++c) {
            statics.bounce[c]=(flatCell(floorBounce,cell)[c]*shown[0]+flatCell(ceilingBounce,cell)[c]*shown[1])/255.0f*bounceStrength;
            result[c]+=statics.bounce[c];
        }
    }
    if(sample)*sample=statics;
    return result;
}
// The lamp color of the sector light at a sprite lit at that level (lampTint),
// packed for Vertex::lampTint; white without static light.
unsigned packedLampTint(const StaticSample &statics,float light) {
    if(settings.lightModel==1&&bakedLightsActive)return 0xFFFFFFFFu;   // whole-level lighting has no lamp tint (world.frag)
    float tint[3];lampTint(statics.color.data(),statics.bounce.data(),settings.bounce?settings.bounceTint:0,light,tint);
    unsigned packed=0xFF000000u;
    for(int c=0;c<3;++c)packed|=(unsigned)std::lround(std::clamp(tint[c],0.0f,1.0f)*255)<<(8*c);
    return packed;
}
// The static light that casts a thing's shadow, when bake-only lights or the
// grid stand in for the dynamic copies: above the thing's feet at z.
bool staticShadow(const StaticSample &sample,float z,ShadowLight &light) {
    if(!(bakeOnlyActive||gridLightActive())||sample.shadow<0)return false;
    const BakeLight &source=bakeSources[sample.shadow];
    if(source.z<=z+8)return false;
    float amount=sample.shadowAmount*groupFlicker(sourceGroups[sample.shadow]);
    light={source.x,source.y,source.z,amount};
    return true;
}
void clearStaticGrid() {staticGrid.clear();}
} // namespace doom3d
