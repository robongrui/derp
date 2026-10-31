/* Puddles: where the globs a plunge throws come down (on the liquid, the floors
   round it or a wall in the way), and the splats of the liquid they leave out
   of it. The splats are drawn by blood's decal code (blood.cpp splatArt,
   flatSplat, drawWallSplat) in the liquid's own colors. */
#include "internal.h"

namespace doom3d {
// Image keys clear of walls, lumps, blood (-0x40000000) and ragdoll cards
// (-0x50000000): a liquid's base artwork at its flat number times 16 plus the
// variant, its splats on the texel grid (gridSplat) numbered from gridKeyBase.
constexpr int puddleKeyBase=-0x48000000,gridKeyBase=-0x47000000;
// A liquid's splats take its flat's own palette indices, ordered dark to
// bright, and where its texels glow, their glow (the emission mask): nukage's
// splats glow like the pool, but only round their edge, where the stuff
// thinned out as it splatted: a bright glowing outline and the thrown droplets
// round a darker middle, as are the runs down walls. Water leaves no paint:
// where it comes down the floor or wall lies wet, darker only, as rain leaves
// it (wear.cpp wetSplash). They stay as they are (nothing to age per frame),
// the oldest going first once there are too many.
struct LiquidShade { byte index,glow; };
static std::unordered_map<int,std::vector<LiquidShade>> liquidShades;
static const std::vector<LiquidShade> &liquidRamp(int pic) {
    auto found=liquidShades.find(pic);if(found!=liquidShades.end())return found->second;
    const byte *palette=(const byte*)W_CacheLumpName((char*)"PLAYPAL",PU_CACHE);
    Image &flat=lumpImage(firstflat+pic,true);
    bool glows=flat.emission.size()*2==flat.pixels.size();
    std::vector<std::pair<float,LiquidShade>> pixels;
    for(size_t i=0;i+1<flat.pixels.size();i+=2) {
        const byte *rgb=palette+flat.pixels[i]*3;
        pixels.push_back({0.299f*rgb[0]+0.587f*rgb[1]+0.114f*rgb[2],{flat.pixels[i],glows?flat.emission[i/2]:(byte)0}});
    }
    std::stable_sort(pixels.begin(),pixels.end(),[](const auto &a,const auto &b){return a.first<b.first;});
    auto &ramp=liquidShades[pic];
    for(const auto &pixel:pixels)ramp.push_back(pixel.second);
    return ramp;
}
static int puddleKey(int pic,int variant) {return puddleKeyBase+pic*16+variant;}
static bool liquidGlows(int pic) {
    const auto &ramp=liquidRamp(pic);
    return liquidLook(pic)!=LIQUID_LOOK_WATER&&std::any_of(ramp.begin(),ramp.end(),[](const LiquidShade &s){return s.glow>0;});
}
static byte rampIndex(int pic,float shade) {
    const auto &ramp=liquidRamp(pic);
    return ramp.empty()?0:ramp[std::clamp((int)std::lround(std::clamp(shade,0.0f,1.0f)*(ramp.size()-1)),0,(int)ramp.size()-1)].index;
}
// The base artwork: the middle's dark shades (a drip run is all edge: bright
// and glowing).
static Image &puddleImage(int pic,int variant) {
    bool water=liquidLook(pic)==LIQUID_LOOK_WATER,run=variant==splatDrip&&!water,glows=run&&liquidGlows(pic);
    return splatArt(puddleKey(pic,variant),variant,[&](float shade,float,byte &index,byte &glow) {
        shade=std::clamp(shade,0.0f,1.0f);
        index=rampIndex(pic,water?shade*0.33f:run?0.75f+0.25f*shade:0.1f+0.3f*shade);
        glow=run?255:0;
    },glows);
}
// Splats on Doom's texel grid: the base artwork redrawn at one texel a unit,
// size across and stretch times that along way dir (eighths of a half turn),
// so it is drawn unturned and unscaled with its texels on the floor's and
// walls' own. Turning and stretching it as it was drawn broke its texels into
// small slanted diamonds against the flat's. The glowing edge is found again
// at this size, a texel wide, so small splats keep it.
static std::map<std::tuple<int,int,int,int,int>,int> gridKeys;
static Image &gridSplat(int pic,int variant,int size,int dir,int stretchStep,int &key) {
    auto found=gridKeys.find({pic,variant,size,dir,stretchStep});
    if(found!=gridKeys.end()) {
        key=found->second;
        auto image=images.find(key);if(image!=images.end())return image->second;
    } else {key=gridKeyBase+(int)gridKeys.size();gridKeys[{pic,variant,size,dir,stretchStep}]=key;}
    const Image &base=puddleImage(pic,variant);
    float along=size*(1+0.5f*stretchStep),across=(float)size,a=dir*doomPi/8,c=std::cos(a),sn=std::sin(a);
    int width=std::max(1,(int)std::ceil(along*std::abs(c)+across*std::abs(sn))),height=std::max(1,(int)std::ceil(along*std::abs(sn)+across*std::abs(c)));
    std::vector<byte> pixels((size_t)width*height*2,0);
    auto covered=[&](int x,int y){return x>=0&&y>=0&&x<width&&y<height&&pixels[((size_t)y*width+x)*2+1];};
    for(int y=0;y<height;++y)for(int x=0;x<width;++x) {
        float u=x+0.5f-width*0.5f,v=y+0.5f-height*0.5f;
        int bx=(int)std::floor(((u*c+v*sn)/along+0.5f)*base.width),by=(int)std::floor(((v*c-u*sn)/across+0.5f)*base.height);
        if(bx<0||by<0||bx>=base.width||by>=base.height)continue;
        size_t from=((size_t)by*base.width+bx)*2,to=((size_t)y*width+x)*2;
        if(base.pixels[from+1]>=128) {pixels[to]=base.pixels[from];pixels[to+1]=255;}
    }
    bool glows=liquidGlows(pic);
    std::vector<byte> mask(glows?(size_t)width*height:0,0);
    if(glows)for(int y=0;y<height;++y)for(int x=0;x<width;++x) {
        if(!covered(x,y)||(covered(x-1,y)&&covered(x+1,y)&&covered(x,y-1)&&covered(x,y+1)))continue;
        size_t i=(size_t)y*width+x;
        pixels[2*i]=rampIndex(pic,0.75f+0.25f*hashUnit(x,y,(uint32_t)key));mask[i]=255;
    }
    return images.emplace(key,upload(width,height,pixels.data(),0,0,glows?mask.data():nullptr)).first->second;
}
// A splat of the liquid of floor flat pic, on a floor or a wall.
// size: across in units; dir, stretchStep: as gridSplat's.
struct Puddle { float x,y,born;int sector,pic,variant,size,dir,stretchStep; };
struct PuddleWall { WallSplat splat;int pic; };
static std::vector<Puddle> puddles;
static std::vector<PuddleWall> puddleWalls;
static unsigned puddleLevelSerial;
constexpr size_t maxPuddles=160,maxPuddleWalls=96;
static void checkPuddleLevel() {
    if(puddleLevelSerial==r_levelserial)return;
    puddles.clear();puddleWalls.clear();puddleLevelSerial=r_levelserial;
}
// The lines a plunge's globs can meet: those within reach of where it went in.
static std::vector<int> linesNear(float x,float y,float reach) {
    std::vector<int> near;
    for(int i=0;i<numlines;++i) {
        float ax=units(lines[i].v1->x),ay=units(lines[i].v1->y),bx=units(lines[i].v2->x),by=units(lines[i].v2->y);
        if(std::max(ax,bx)<x-reach||std::min(ax,bx)>x+reach||std::max(ay,by)<y-reach||std::min(ay,by)>y+reach)continue;
        near.push_back(i);
    }
    return near;
}
// Each glob's arc in a few straight steps: the first wall it runs into (a
// raised floor's front, a ledge's underside, a solid wall) or the first floor
// it comes down to, found once when the plunge is made. An arc carries on past
// its own surface where the floor below falls away, up to twice its flight.
static void landGlob(GlobFlight &g,const std::vector<int> &near) {
    constexpr int steps=6;
    float speed=g.out/std::max(g.air,0.01f),sWas=0,xWas=g.x,yWas=g.y,zWas=g.z;
    g.land=GLOB_GONE;g.landT=2*g.air;
    for(int k=1;k<=2*steps;++k) {
        float s=g.air*k/steps,x=g.x+g.dx*speed*s,y=g.y+g.dy*speed*s,z=g.z+g.up*s-0.5f*g.gravity*s*s;
        float length=speed*(s-sWas);
        WallHit hit;
        if(length>0.01f&&firstSolidWall(xWas,yWas,zWas,g.dx,g.dy,(z-zWas)/length,length,hit,&near)) {
            float f=hit.t/length;
            g.land=GLOB_WALL;g.wall=hit;g.landT=sWas+(s-sWas)*f;
            g.landX=xWas+g.dx*hit.t;g.landY=yWas+g.dy*hit.t;g.landZ=zWas+(z-zWas)*f;
            return;
        }
        const sector_t *sector=R_PointInSubsector((fixed_t)(x*FRACUNIT),(fixed_t)(y*FRACUNIT))->sector;
        float floor=floorZ(sector);
        if(z<=floor+0.01f) {
            // Where the arc comes down to that floor: the later root of its height.
            float rise=floor-g.z,reach=g.up*g.up-2*g.gravity*rise;
            float t=std::clamp(reach>0?(g.up+std::sqrt(reach))/g.gravity:s,sWas,s);
            int index=int(sector-sectors);
            g.land=sector->floorpic==skyflatnum?GLOB_GONE:sectorMist[index].liquid?GLOB_LIQUID:GLOB_FLOOR;
            g.landSector=index;g.landT=t;g.landX=g.x+g.dx*speed*t;g.landY=g.y+g.dy*speed*t;g.landZ=floor;
            return;
        }
        sWas=s;xWas=x;yWas=y;zWas=z;
    }
}
// Globs come down in clusters: the ones landing close together pool into one
// splat sized by how many (its area by the liquid in it), stretched the way
// they came by how low they came in (a drop's stain: width over length is the
// sine of its angle), single ones into specks. The column's fat globs count
// twice. Those that hit a wall splat it there, the bigger ones running down.
void landGlobs(std::vector<GlobFlight> &globs,float x,float y,int liquid,float born) {
    if(globs.empty()||liquid<0||liquid>=numsectors)return;
    checkPuddleLevel();
    float reach=16;
    for(const GlobFlight &g:globs)reach=std::max(reach,std::hypot(g.x-x,g.y-y)+2*g.out+16);
    std::vector<int> near=linesNear(x,y,reach);
    for(GlobFlight &g:globs)landGlob(g,near);
    if(!settings.splashes)return;
    int pic=sectors[liquid].floorpic;
    float scale=sectorMist[liquid].thick?1.25f:1.0f;
    bool water=liquidLook(pic)==LIQUID_LOOK_WATER;
    struct Bin { float x=0,y=0,z=0,dx=0,dy=0,stretch=0,t=0,weight=0;int count=0;const GlobFlight *first=nullptr; };
    std::map<std::tuple<int,int,int>,Bin> floorBins;
    std::map<std::tuple<int,int,int,int,int>,Bin> wallBins;
    for(const GlobFlight &g:globs) {
        if(g.land!=GLOB_FLOOR&&g.land!=GLOB_WALL)continue;
        float weight=g.column?2.0f:1.0f,speed=g.out/std::max(g.air,0.01f),falling=std::max(0.0f,g.gravity*g.landT-g.up);
        float stretch=std::clamp(std::hypot(speed,falling)/std::max(falling,1.0f),1.0f,2.5f);
        Bin *bin;
        if(g.land==GLOB_FLOOR)bin=&floorBins[{g.landSector,(int)std::floor(g.landX/16),(int)std::floor(g.landY/16)}];
        else {
            const line_t &line=lines[g.wall.line];
            float along=std::hypot(g.landX-units(line.v1->x),g.landY-units(line.v1->y));
            bin=&wallBins[{g.wall.line,g.wall.side,g.wall.section,(int)std::floor(along/12),(int)std::floor(g.landZ/12)}];
        }
        if(!bin->first)bin->first=&g;
        bin->x+=g.landX*weight;bin->y+=g.landY*weight;bin->z+=g.landZ*weight;bin->dx+=g.dx*weight;bin->dy+=g.dy*weight;
        bin->stretch+=stretch*weight;bin->t+=(g.delay+g.landT)*weight;bin->weight+=weight;++bin->count;
    }
    for(auto &[key,bin]:floorBins) {
        float w=bin.weight;
        bool speck=bin.count==1&&w<2;
        float size=(speck?std::clamp(bin.first->size*3,5.0f,9.0f):std::clamp(9*std::sqrt(w),12.0f,36.0f))*scale;
        if(water) {wetSplash(std::get<0>(key),bin.x/w,bin.y/w,bin.z/w,size*0.5f);continue;}
        int variant=splatVariant(speck?BLOOD_SPECK:BLOOD_SPLAT),stretchStep=speck?0:std::clamp((int)std::lround((bin.stretch/w-1)/0.5f),0,3);
        float way=std::atan2(bin.dy,bin.dx);if(way<0)way+=doomPi;
        int dir=stretchStep?(int)std::lround(way/(doomPi/8))%8:0;
        if(puddles.size()>=maxPuddles)puddles.erase(puddles.begin());
        puddles.push_back({bin.x/w,bin.y/w,born+bin.t/w,std::get<0>(key),pic,variant,(int)std::lround(size),dir,stretchStep});
    }
    for(auto &[key,bin]:wallBins) {
        float w=bin.weight;
        if(water) {
            const line_t &line=lines[bin.first->wall.line];
            int side=line.sidenum[bin.first->wall.side];
            if(side>=0)wetSplash(int(sides[side].sector-sectors),bin.x/w,bin.y/w,bin.z/w,std::clamp(4+3*std::sqrt(w),5.0f,14.0f));
            continue;
        }
        WallSplat splat;
        if(!placeWallSplat(bin.first->wall,bin.x/w,bin.y/w,bin.z/w,std::clamp(6+5*std::sqrt(w),7.0f,22.0f)*scale,1,born+bin.t/w,
                           splatVariant(BLOOD_SPLAT),w>=2,splat))continue;
        if(puddleWalls.size()>=maxPuddleWalls)puddleWalls.erase(puddleWalls.begin());
        puddleWalls.push_back({splat,pic});
    }
}
void buildPuddles(const Uniforms &camera) {
    if(!settings.splashes||gamestate!=GS_LEVEL||floors.empty())return;
    checkPuddleLevel();
    float now=gameSeconds();
    // Drawn unturned at one texel a unit, their texel edges on whole units.
    int key;
    for(const Puddle &p:puddles) {
        if(now<p.born||camera.eye[2]<floorZ(&sectors[p.sector]))continue; // Not yet down; the back of the flat.
        const Image &image=gridSplat(p.pic,p.variant,p.size,p.dir,p.stretchStep,key);
        float x=std::round(p.x-image.width*0.5f)+image.width*0.5f,y=std::round(p.y-image.height*0.5f)+image.height*0.5f;
        flatSplat(key,image,p.sector,x,y,(float)image.height,0,1,false,false,0,1,0,1,camera);
    }
    for(const PuddleWall &p:puddleWalls) {
        if(now<p.splat.born)continue;
        const Image &image=gridSplat(p.pic,p.splat.variant,(int)std::lround(p.splat.size),0,0,key);
        WallSplat splat=p.splat;
        splat.size=(float)image.height;splat.stretch=1;
        splat.along=std::round(splat.along-image.width*0.5f)+image.width*0.5f;
        splat.dz=std::round(splat.dz+image.height*0.5f)-image.height*0.5f;
        for(int n=0;n<splat.drips;++n)splat.dripU[n]=std::round(splat.along+splat.dripU[n])-splat.along;
        drawWallSplat(splat,key,image,puddleKey(p.pic,splatDrip),puddleImage(p.pic,splatDrip),1,0,1,now,camera);
    }
}
void puddlesShutdown() {puddles.clear();puddleWalls.clear();liquidShades.clear();gridKeys.clear();}
} // namespace doom3d
