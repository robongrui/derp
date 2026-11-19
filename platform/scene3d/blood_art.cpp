/* Blood decals at Doom's own texel size: one texel a unit, never stretched or
   turned off the world's grid (the user: "a smeared pixel never looks good").
   A splat's way and stretch are drawn into its artwork instead (a streak
   lengthened along its way, droplets thrown on ahead; a smear's drag), and a
   round one's turn is one of the eight the grid keeps (mirrors, a quarter
   turn). Each picture is made the first time its kind, size, way and stretch
   are wanted, and kept. blood.cpp places them; the shapes are its splatArt's,
   drawn at their size instead of scaled to it. */
#include "internal.h"

namespace doom3d {
constexpr int bloodArtKeyBase=-0x6C000000;   // image keys clear of the others (head bursts, pieces, legs, cards, speech, puddles)

int bloodDirection(float angle) {
    int step=(int)std::lround(angle/(2*doomPi)*16);
    return ((step%16)+16)%16;
}
int bloodStretchStep(float stretch) {return stretch<1.2f?0:stretch<1.65f?1:stretch<2.15f?2:3;}
static float stretchOf(int step) {static const float stretches[4]={1,1.4f,1.9f,2.4f};return stretches[std::clamp(step,0,3)];}

// The shape at (x,y) units from its middle (x along its way), size across: whether it covers it, and its
// shade there and how far out (0 middle, 1 edge). i, j: the texel, for a speck's ragged edge.
static bool shapeAt(int variant,uint32_t seed,float size,bool streaked,float x,float y,int i,int j,float &shade,float &out) {
    float d=std::hypot(x,y),a=std::atan2(y,x),half=size*0.5f;
    if(variant==splatSmear) {
        // Dragged blood: thick where the body was (behind), thinning to streaks along the drag.
        float along=std::clamp((x+size)/(2*size),0.0f,1.0f),middle=half;
        if(x<-size||x>size)return false;
        float reach=middle*(0.95f-0.6f*along)*(0.85f+0.3f*valueNoise(x+size,0,6,seed)),side=std::abs(y);
        if(side>=reach)return false;
        float streak=valueNoise((x+size)*0.15f,y+half,1.5f,seed^3u);
        if(streak<0.2f+0.55f*along*side/reach)return false;
        shade=0.12f+0.35f*streak*(1-0.5f*along);out=side/reach;
        return true;
    }
    if(variant>=splatSpeckFirst&&variant<splatSpeckFirst+splatSpecks) {
        // A drop's splat: a blob and a stray texel or two.
        float radius=(1.6f+0.6f*(variant-splatSpeckFirst)+0.4f*hashUnit(0,0,seed))*size/8;
        float r=d+(hashUnit(i,j,seed)-0.5f)*0.9f;
        if(r<radius) {shade=0.3f+0.3f*(1-r/radius);out=r/radius;return true;}
        for(int n=int(hash32(seed)%3);n>0;--n) {
            float b=hashUnit(n,1,seed)*2*doomPi,reach=radius+1+hashUnit(n,2,seed);
            if(std::floor(std::cos(b)*reach)==std::floor(x)&&std::floor(std::sin(b)*reach)==std::floor(y)) {shade=0.35f;out=1;return true;}
        }
        return false;
    }
    bool pool=variant==splatPool;
    float phase[3];for(int k=0;k<3;++k)phase[k]=hashUnit(k,variant,seed)*2*doomPi;
    float edge=pool?size*0.40f*(0.82f+0.10f*std::sin(2*a+phase[0])+0.06f*std::sin(5*a+phase[1])+0.04f*std::sin(9*a+phase[2]))
                   :size*0.30f*(0.70f+0.22f*std::sin(3*a+phase[0])+0.12f*std::sin(7*a+phase[1]));
    edge+=(valueNoise(x+half,y+half,4,seed)-0.5f)*size*0.08f;
    if(d<edge) {
        float rim=1-d/edge;
        shade=pool?0.12f+0.30f*valueNoise(x+half,y+half,6,seed^1u)+0.12f*(1-rim)
                  :0.25f+0.45f*std::pow(rim,0.6f)+0.2f*(valueNoise(x+half,y+half,3,seed^2u)-0.5f);
        out=1-rim;
        return true;
    }
    // Droplets thrown past the edge; a streak's thrown on ahead, the way it went.
    int drops=pool?3:6+int(hash32(seed)%5);
    for(int n=0;n<drops;++n) {
        float b=hashUnit(n,1,seed)*2*doomPi,reach=size*(pool?0.42f:0.30f+0.17f*hashUnit(n,2,seed));
        if(streaked&&n<3) {b=(hashUnit(n,5,seed)-0.5f)*0.5f;reach=size*(0.42f+0.12f*n);}
        float radius=pool?1.5f+hashUnit(n,3,seed)*1.5f:0.8f+hashUnit(n,3,seed)*1.4f;
        if(std::hypot(x-std::cos(b)*reach,y-std::sin(b)*reach)<radius) {
            float s=0.35f+0.25f*hashUnit(n,4,seed);
            shade=pool?s*0.6f:s;out=1;
            return true;
        }
    }
    return false;
}

const Image &bloodArt(int variant,int seed,float size,int dir,int stretch,int tint,int &key) {
    int texels=std::clamp((int)std::lround(size*0.5f)*2,4,160);
    if(variant!=splatSmear&&stretch==0)dir=0;
    tint=std::clamp(tint,0,BLOOD_TINTS-1);
    key=bloodArtKeyBase+((((tint*32+variant)*4+(seed&3))*81+texels/2)*16+dir)*4+stretch;
    auto found=images.find(key);
    if(found!=images.end())return found->second;
    uint32_t own=hash32(variant*7919u+17u)^(uint32_t)(seed&3)*0x9e3779b9u;
    float s=stretchOf(stretch),angle=dir*2*doomPi/16,c=std::cos(angle),sn=std::sin(angle);
    // Its reach along its way and across (droplets thrown past the edge: up to 0.66 of its size ahead of a
    // streak, 0.47 round, two texels wide), then the box round it as turned, a texel to spare.
    float along=variant==splatSmear?texels+1.0f:texels*(stretch?0.7f*s:0.5f)+3,across=variant==splatSmear?texels*0.5f+1:texels*0.5f+3;
    int hx=(int)std::ceil(std::hypot(along*c,across*sn))+1,hy=(int)std::ceil(std::hypot(along*sn,across*c))+1;
    int width=2*hx,height=2*hy;
    const auto &shades=bloodPalette(tint);
    std::vector<byte> pixels((size_t)width*height*2,0);
    for(int j=0;j<height;++j)for(int i=0;i<width;++i) {
        // The texel's middle from the picture's (y up), turned back onto the shape's way, its stretch undone.
        float px=i+0.5f-hx,py=hy-(j+0.5f);
        float x=(px*c+py*sn)/s,y=-px*sn+py*c,shade,out;
        if(!shapeAt(variant,own,(float)texels,stretch>0,x,y,i,j,shade,out))continue;
        size_t at=(size_t)j*width+i;
        pixels[at*2]=shades[std::clamp((int)std::lround(std::clamp(shade,0.0f,1.0f)*(shades.size()-1)),0,(int)shades.size()-1)];
        pixels[at*2+1]=255;
    }
    return images.emplace(key,upload(width,height,pixels.data(),hx,hy)).first->second;
}

// The picture's texel (s,t) for a point (u,v) texels into its drawn box (v down), turned one of eight ways:
// mirrored across (1), down (2), a quarter turn (4).
static void turned(int orient,float u,float v,float w,float h,float &s,float &t) {
    if(orient&1)u=w-u;
    if(orient&2)v=h-v;
    if(orient&4) {s=v;t=u;} else {s=u;t=v;}
}

// A splat's square cut to its flat's pieces, as triangles' points with their texels. The pieces don't
// change, so a stain that stays put (the layer's tiles, a pool between its steps) is cut once, not each
// frame; only its height, light and age are filled in per frame. Past a few thousand the cuts become the
// older half, which those still drawn are taken back from.
struct FloorCorner { float x,y,s,t; };
struct FloorCut {
    int key,sector,orient;float x0,top,w,h;
    bool operator==(const FloorCut &o) const {return key==o.key&&sector==o.sector&&orient==o.orient&&x0==o.x0&&top==o.top&&w==o.w&&h==o.h;}
};
struct FloorCutHash { size_t operator()(const FloorCut &c) const {
    uint32_t bits[2];std::memcpy(bits,&c.x0,4);std::memcpy(bits+1,&c.top,4);
    return hash32((uint32_t)c.key*73856093u^(uint32_t)c.sector*19349663u^bits[0]*83492791u^bits[1]*2654435761u^(uint32_t)c.orient);
} };
static const std::vector<FloorCorner> &floorCut(const FloorCut &cut) {
    static std::unordered_map<FloorCut,std::vector<FloorCorner>,FloorCutHash> cuts,older;
    static unsigned cutLevel=~0u;
    if(cutLevel!=r_levelserial) {cuts.clear();older.clear();cutLevel=r_levelserial;}
    if(cuts.size()>4096) {older=std::move(cuts);cuts.clear();}
    auto found=cuts.find(cut);
    if(found!=cuts.end())return found->second;
    if(auto kept=older.find(cut);kept!=older.end()) {
        auto corners=std::move(kept->second);older.erase(kept);
        return cuts.emplace(cut,std::move(corners)).first->second;
    }
    float x0=cut.x0,top=cut.top,x1=x0+cut.w,bottom=top-cut.h;
    std::vector<Point> square={{x0,bottom},{x1,bottom},{x1,top},{x0,top}};
    std::vector<FloorCorner> corners;
    for(int leaf:sectorFloors[cut.sector]) {
        const auto &b=floorBounds[leaf];
        if(b[2]<x0||b[0]>x1||b[3]<bottom||b[1]>top)continue;
        auto polygon=floors[leaf];
        for(size_t n=0;n<square.size()&&!polygon.empty();++n) {
            Point a=square[n],d=square[(n+1)%square.size()];
            polygon=clip(polygon,a,{d.x-a.x,d.y-a.y},false);
        }
        for(size_t n=1;n+1<polygon.size();++n)
            for(Point p:{polygon[0],polygon[n],polygon[n+1]}) {
                float s,t;turned(cut.orient,p.x-x0,top-p.y,cut.w,cut.h,s,t);
                corners.push_back({p.x,p.y,s,t});
            }
    }
    return cuts.emplace(cut,std::move(corners)).first->second;
}

// What blood lies on, for its shine (world/blood_material.glsl bloodUnder): 0 not metal, 1 metal, 2 brushed
// metal along u (a wall's length, a flat's x), 3 along v (up a wall, a flat's y). The metal the plate's own
// shading sees (scene.cpp metalSurface, surfaceBinding): on metal blood is a film the plate shines through,
// not paint over it. Whole stains, not texels: blood on a painted part of a metal texture shines as well.
// Packed into the vertex's wetness as wet + 2 * this.
static int bloodUnder(int key,bool flat) {
    if(!settings.metalShading||!settings.detail)return 0;
    static std::unordered_map<int,int> known;
    auto found=known.find(key);
    if(found!=known.end())return found->second;
    char name[9]={};
    if(flat)memcpy(name,lumpinfo[-1-key].name,8);
    else if(key>0)R_TextureName(key,name);
    int under=0;
    if(name[0]&&!Game_MatteTexture(name,flat)&&(flat?Game_FlatSurface(name):Game_WallSurface(name))==GAME_SURFACE_METAL) {
        const Image &image=flat?lumpImage(-1-key,true):wallImage(key);
        under=image.blank>=0.55f?(image.brushedDown?3:2):1;
    }
    return known[key]=under;
}
static float flatUnder(const sector_t &sector,bool ceiling) {
    return 2.0f*(float)bloodUnder(-1-(firstflat+(ceiling?sector.ceilingpic:sector.floorpic)),true);
}

// On a floor (or ceiling), its middle at (x,y) put on the world's unit grid (a flat's texels).
void gridFloorSplat(int key,const Image &image,int sectorIndex,float x,float y,int orient,bool ceiling,
                    float wet,float dark,float wash,const Uniforms &camera,float raise,bool glowing) {
    const sector_t &sector=sectors[sectorIndex];
    if((ceiling?sector.ceilingpic:sector.floorpic)==skyflatnum)return;
    bool quarter=orient&4;
    float w=(float)(quarter?image.height:image.width),h=(float)(quarter?image.width:image.height);
    float ax=(float)(quarter?image.top:image.left),ay=(float)(quarter?image.left:image.top);
    float x0=std::round(x)-ax,top=std::round(y)+ay;
    const std::vector<FloorCorner> &corners=floorCut({key,sectorIndex,orient,x0,top,w,h});
    float lift=decalLift(x,y,camera)*raise,z=ceiling?ceilZ(&sector)-lift:floorZ(&sector)+lift,light=lighting(&sector);
    if(!ceiling&&waterIceAt(x,y)>=0.35f)z+=0.25f;   // on the ice laid over frozen water (water_ice.cpp, geometry.cpp)
    unsigned mode=MODE_BLOOD|sectorMode(&sector);
    float under=flatUnder(sector,ceiling);
    auto &out=batches[key];
    for(const FloorCorner &c:corners) {
        Vertex v={c.x,c.y,z,c.s,c.t,light,mode,wet+under,dark,wash};
        v.lampTint=glowing?0xFFFFFFFFu:0xFFFFFFu;   // glowing: its mask lit (blood on a lamp)
        out.push_back(v);
    }
}

// On a wall, its middle put on the wall's unit grid (along it and up), lit like the wall, not drawn from
// behind; with its drips (blood.cpp's runs), a texel wide each.
void gridWallSplat(const WallSplat &d,int key,const Image &image,const DripLooks &drips,float dark,float wash,
                   float now,const Uniforms &camera) {
    WallSpan span;if(!wallSpan(lines[d.line],d.side,d.section,span))return;
    float ex=span.b.x-span.a.x,ey=span.b.y-span.a.y,len=std::hypot(ex,ey);
    if(len<1||ex*(camera.eye[1]-span.a.y)-ey*(camera.eye[0]-span.a.x)>0)return; // Back of the wall.
    bool quarter=d.orient&4;
    float w=(float)(quarter?image.height:image.width),h=(float)(quarter?image.width:image.height);
    float ax=(float)(quarter?image.top:image.left),ay=(float)(quarter?image.left:image.top);
    float zc=std::round(span.anchor+d.dz),u0=std::round(d.along)-ax,top=zc+ay,u1=u0+w,bottom=top-h;
    float cu0=std::max(0.0f,u0),cu1=std::min(len,u1),cz0=std::max(span.bottom,bottom),cz1=std::min(span.top,top);
    const line_t &line=lines[d.line];const sector_t *front=sides[line.sidenum[d.side]].sector;
    float light=lighting(front);
    if(std::abs(ey)<0.01f)light=std::max(0.1f,light-0.04f);
    if(std::abs(ex)<0.01f)light=std::min(1.0f,light+0.04f);
    float tx=ex/len,ty=ey/len,lift=decalLift(span.a.x+tx*d.along,span.a.y+ty*d.along,camera)+0.25f,nx=ty*lift,ny=-tx*lift;
    unsigned mode=MODE_BLOOD|sectorMode(front);
    float under=2.0f*(float)bloodUnder(span.texture,false);
    auto vertex=[&](float u,float z,float s,float t) {
        Vertex v={span.a.x+tx*u+nx,span.a.y+ty*u+ny,z,s,t,light,mode,under,dark,wash};
        v.lampTint=d.layer?0xFFFFFFFFu:0xFFFFFFu;
        bakeWallCoordinates(line,d.side,u,v);
        return v;
    };
    if(cu1>cu0&&cz1>cz0&&!(d.merged&&!d.layer)) {   // merged into the layer: only its runs here
        auto corner=[&](float u,float z) {float s,t;turned(d.orient,u-u0,top-z,w,h,s,t);return vertex(u,z,s,t);};
        quad(batches[key],corner(cu0,cz1),corner(cu1,cz1),corner(cu1,cz0),corner(cu0,cz0));
    }
    // Drips run down from the splat's lower half over three seconds, slowing, in four-tic steps: the head
    // leads, the run follows, each in its own picture (dripLook). (On Phobos nothing pulls them down.)
    for(int n=0;n<(Game_Phobos()?0:d.drips);++n) {
        int look=dripLook(d,n);
        if(!drips.image[look])continue;
        const Image &drip=*drips.image[look];
        float age=std::floor((now-d.born-0.3f)*TICRATE/4)*4/TICRATE;
        if(age<=0)continue;
        float grow=std::min(1.0f,age/3),length=std::round(d.dripLength[n]*(1-(1-grow)*(1-grow))),u=std::round(d.along+d.dripU[n]);
        float from=std::round(zc-d.size*0.5f*0.35f),to=std::max(span.bottom,from-length);
        if(length<1||from<=to||u<0.5f||u>len-1.5f)continue;
        float shown=std::min((float)drip.height,from-to);
        quad(batches[drips.key[look]],vertex(u,from,0,drip.height-shown),vertex(u+drip.width,from,(float)drip.width,drip.height-shown),
             vertex(u+drip.width,to,(float)drip.width,(float)drip.height),vertex(u,to,0,(float)drip.height));
    }
}
} // namespace doom3d
