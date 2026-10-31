/* Blood splats on floors, walls and ceilings from hits, and pools under corpses.
   The splat artwork and decal drawing are shared with liquid puddles (puddles.cpp). */
#include "internal.h"

namespace doom3d {
// Blood: decals drawn in the world pass and lit like the surface under them.
// Their artwork (splatArt) is generated per WAD from the BLUD sprite's own
// palette indices, ordered dark to bright by how often each is used.
constexpr int bloodKeyBase=-0x40000000; // Image keys clear of walls and lumps: plus tint*16 plus the variant.
// Enemy-appropriate blood (F4): the tint a monster bleeds in (Game_BloodTint).
int bloodTint(const mobj_t *thing) {
    if(!settings.monsterBlood||!thing)return BLOOD_RED;
    game_blood_tint tint=Game_BloodTint(thing->type);
    return tint==GAME_BLOOD_NONE?BLOOD_NONE:(int)tint;
}
bool bleedsBlood(const mobj_t &thing) {return !(thing.flags&MF_NOBLOOD)&&bloodTint(&thing)!=BLOOD_NONE;}
// The other tints are the red turned: its channels swapped (red to green or
// blue), each shade the nearest palette colour, so they stay in the palette's
// own ramps. Green comes out a little darker: the palette's greens are bright.
byte bloodRemap(byte index,int tint) {
    if(tint<=BLOOD_RED||tint>=BLOOD_TINTS)return index;
    static byte remap[BLOOD_TINTS][256];static bool made=false;
    if(!made) {
        made=true;
        const byte *palette=playpal();
        for(int t=BLOOD_GREEN;t<BLOOD_TINTS;++t)for(int i=0;i<256;++i) {
            const byte *c=palette+i*3;
            int r=c[1],g=t==BLOOD_GREEN?c[0]*7/8:c[2],b=t==BLOOD_GREEN?c[2]:c[0];
            int best=0,bestDistance=INT_MAX;
            for(int n=0;n<256;++n) {
                int dr=palette[n*3]-r,dg=palette[n*3+1]-g,db=palette[n*3+2]-b,d=dr*dr+dg*dg+db*db;
                if(d<bestDistance) {bestDistance=d;best=n;}
            }
            remap[t][i]=(byte)best;
        }
    }
    return remap[tint][index];
}
static std::vector<byte> bloodShades[BLOOD_TINTS];static bool bloodShadesLoaded=false;
const std::vector<byte> &bloodPalette(int tint) {
    tint=std::clamp(tint,0,BLOOD_TINTS-1);
    if(bloodShadesLoaded)return bloodShades[tint];
    bloodShadesLoaded=true;
    int sprite=Game_Sprite(GAME_SPRITE_BLOOD);
    if(sprite<0||sprite>=numsprites)return bloodShades[tint];
    const byte *palette=(const byte*)W_CacheLumpName((char*)"PLAYPAL",PU_CACHE);
    std::vector<std::pair<float,byte>> pixels;
    const auto &blood=sprites[sprite];
    for(int frame=0;frame<blood.numframes;++frame) {
        const Image &image=lumpImage(firstspritelump+blood.spriteframes[frame].lump[0],false);
        for(size_t i=0;i+1<image.pixels.size();i+=2) if(image.pixels[i+1]) {
            const byte *rgb=palette+image.pixels[i]*3;
            pixels.push_back({0.299f*rgb[0]+0.587f*rgb[1]+0.114f*rgb[2],image.pixels[i]});
        }
    }
    std::sort(pixels.begin(),pixels.end());
    for(const auto &pixel:pixels)for(int t=0;t<BLOOD_TINTS;++t)bloodShades[t].push_back(bloodRemap(pixel.second,t));
    return bloodShades[tint];
}
bool bloodOn() {return settings.blood&&gamestate==GS_LEVEL&&!floors.empty()&&!bloodPalette().empty();}
static byte bloodIndex(float shade,int tint=BLOOD_RED) {
    const auto &shades=bloodPalette(tint);
    return shades[std::clamp((int)std::lround(std::clamp(shade,0.0f,1.0f)*(shades.size()-1)),0,(int)shades.size()-1)];
}
std::array<float,3> bloodColor(float shade,int tint) {
    if(bloodPalette().empty())return {0.5f,0,0};
    const byte *rgb=playpal()+bloodIndex(shade,tint)*3;
    return {rgb[0]/255.0f,rgb[1]/255.0f,rgb[2]/255.0f};
}
// The game's airborne blood carries no word of who bled it: the tint is
// learned as it is spilled (R_BloodSpilled: shots and crushers) and kept
// here, drawing only, for as long as a blood thing lives.
struct Spilled { int tint,tic; };
static std::unordered_map<const mobj_t*,Spilled> spilledTints;
void bloodSpilled(mobj_t *blood,mobj_t *bleeder) {
    if(!blood||Game_ThingRole(blood->type)!=GAME_THING_BLOOD)return;
    if(spilledTints.size()>256)
        for(auto it=spilledTints.begin();it!=spilledTints.end();) it=leveltime-it->second.tic>4*TICRATE?spilledTints.erase(it):std::next(it);
    spilledTints[blood]={bloodTint(bleeder),leveltime};
}
int bloodSpriteTint(const mobj_t *blood) {
    if(!settings.monsterBlood)return BLOOD_RED;
    auto found=spilledTints.find(blood);
    // Blood lives a few tics; an older entry is a freed thing's, its memory reused.
    return found==spilledTints.end()||leveltime-found->second.tic>4*TICRATE||leveltime<found->second.tic?BLOOD_RED:found->second.tint;
}
Image &bloodSpriteImage(int lump,int tint) {
    int key=bloodSpriteKey(lump,tint);
    auto found=images.find(key);
    if(found!=images.end())return found->second;
    const Image &source=lumpImage(lump,false);
    std::vector<byte> pixels(source.pixels);
    for(size_t i=0;i+1<pixels.size();i+=2)if(pixels[i+1])pixels[i]=bloodRemap(pixels[i],tint);
    auto normals=spriteNormals(source.width,source.height,pixels.data());
    return images.emplace(key,upload(source.width,source.height,pixels.data(),source.left,source.top,nullptr,normals.data())).first->second;
}
Image &splatArt(int key,int variant,const std::function<void(float,float,byte&,byte&)> &paint,bool glows) {
    auto found=images.find(key);if(found!=images.end())return found->second;
    uint32_t seed=hash32(variant*7919u+17u);
    int width=variant==splatPool?64:variant==splatDrip?3:variant==splatSmear?32:variant>=splatSpeckFirst?8:32;
    int height=variant==splatDrip?40:variant==splatSmear?16:width;
    std::vector<byte> pixels((size_t)width*height*2,0),mask(glows?(size_t)width*height:0,0);
    auto put=[&](int x,int y,float shade,float out=1) {
        if(x<0||y<0||x>=width||y>=height)return;
        size_t i=(size_t)y*width+x;byte glow=0;
        paint(shade,std::clamp(out,0.0f,1.0f),pixels[2*i],glow);pixels[2*i+1]=255;
        if(glows)mask[i]=glow;
    };
    if(variant==splatDrip) {
        // A run one texel wide down to a bead three wide: drawn from the
        // bottom up as it grows, so the bead leads.
        for(int y=0;y<height-4;++y)put(1,y,0.22f+0.12f*valueNoise(0,(float)y,3,seed));
        for(int y=height-4;y<height;++y)for(int x=0;x<3;++x)
            if(x==1||(y>height-4&&y<height-1))put(x,y,x==1&&y==height-3?0.6f:0.4f);
    } else if(variant==splatSmear) {
        // Dragged blood: thick where the body was, thinning to streaks
        // along the drag, broken where the streaks run dry.
        float middle=height*0.5f;
        for(int y=0;y<height;++y)for(int x=0;x<width;++x) {
            float along=(x+0.5f)/width,reach=middle*(0.95f-0.6f*along)*(0.85f+0.3f*valueNoise((float)x,0,6,seed));
            float d=std::abs(y+0.5f-middle);
            if(d>=reach)continue;
            float streak=valueNoise((float)x*0.15f,(float)y,1.5f,seed^3u);
            if(streak<0.2f+0.55f*along*d/reach)continue;
            put(x,y,0.12f+0.35f*streak*(1-0.5f*along),d/reach);
        }
    } else if(variant>=splatSpeckFirst) {
        // A drop's splat at one texel a unit: a blob and a stray texel or two.
        float center=width*0.5f,radius=1.6f+0.6f*(variant-splatSpeckFirst)+0.4f*hashUnit(0,0,seed);
        for(int y=0;y<height;++y)for(int x=0;x<width;++x) {
            float d=std::hypot(x+0.5f-center,y+0.5f-center)+(hashUnit(x,y,seed)-0.5f)*0.9f;
            if(d<radius)put(x,y,0.3f+0.3f*(1-d/radius),d/radius);
        }
        for(int n=int(hash32(seed)%3);n>0;--n) {
            float a=hashUnit(n,1,seed)*2*doomPi,r=radius+1+hashUnit(n,2,seed);
            put((int)(center+std::cos(a)*r),(int)(center+std::sin(a)*r),0.35f);
        }
    } else {
        bool pool=variant==splatPool;
        int size=width;float center=size*0.5f;
        float phase[3];for(int i=0;i<3;++i)phase[i]=hashUnit(i,variant,seed)*2*doomPi;
        for(int y=0;y<size;++y)for(int x=0;x<size;++x) {
            float dx=x+0.5f-center,dy=y+0.5f-center,d=std::hypot(dx,dy),a=std::atan2(dy,dx);
            float edge=pool?size*0.40f*(0.82f+0.10f*std::sin(2*a+phase[0])+0.06f*std::sin(5*a+phase[1])+0.04f*std::sin(9*a+phase[2]))
                           :size*0.30f*(0.70f+0.22f*std::sin(3*a+phase[0])+0.12f*std::sin(7*a+phase[1]));
            edge+=(valueNoise((float)x,(float)y,4,seed)-0.5f)*size*0.08f;
            if(d>=edge)continue;
            float rim=1-d/edge;
            put(x,y,pool?0.12f+0.30f*valueNoise((float)x,(float)y,6,seed^1u)+0.12f*(1-rim)
                        :0.25f+0.45f*std::pow(rim,0.6f)+0.2f*(valueNoise((float)x,(float)y,3,seed^2u)-0.5f),1-rim);
        }
        // Droplets thrown past the edge.
        int drops=pool?3:6+int(hash32(seed)%5);
        for(int n=0;n<drops;++n) {
            float a=hashUnit(n,1,seed)*2*doomPi,reach=size*(pool?0.42f:0.30f+0.17f*hashUnit(n,2,seed));
            float radius=pool?1.5f+hashUnit(n,3,seed)*1.5f:0.8f+hashUnit(n,3,seed)*1.4f,shade=0.35f+0.25f*hashUnit(n,4,seed);
            float px=center+std::cos(a)*reach,py=center+std::sin(a)*reach;
            for(int y=(int)(py-radius);y<=(int)(py+radius)+1;++y)for(int x=(int)(px-radius);x<=(int)(px+radius)+1;++x)
                if(std::hypot(x+0.5f-px,y+0.5f-py)<radius)put(x,y,pool?shade*0.6f:shade);
        }
    }
    return images.emplace(key,upload(width,height,pixels.data(),0,0,glows?mask.data():nullptr)).first->second;
}
static int bloodKey(int variant,int tint) {return bloodKeyBase+tint*16+variant;}
static Image &bloodImage(int variant,int tint) {
    return splatArt(bloodKey(variant,tint),variant,[tint](float shade,float,byte &index,byte &){index=bloodIndex(shade,tint);},false);
}
int splatVariant(int kind) {
    int pick=int(randomRange(0,4));
    return kind==BLOOD_SPECK?splatSpeckFirst+pick%splatSpecks:kind==BLOOD_SMEAR?splatSmear:1+pick%splatSplats;
}
struct BloodFloor { float x,y,born,size,angle,stretch;int sector,variant;bool flip,ceiling;int tint; };
using BloodWall=WallSplat;
struct BloodPool { float x,y,born,size,angle;int sector,tint; };
static std::vector<BloodFloor> bloodFloors;static std::vector<BloodWall> bloodWalls;static std::vector<BloodPool> bloodPools;
static std::unordered_map<const mobj_t*,bool> pooledCorpses;
static unsigned bloodLevelSerial;
constexpr size_t maxFloorBlood=320,maxWallBlood=160;
static void checkBloodLevel() {
    if(bloodLevelSerial==r_levelserial)return;
    bloodFloors.clear();bloodWalls.clear();bloodPools.clear();pooledCorpses.clear();spilledTints.clear();bloodLevelSerial=r_levelserial;
}
void bloodFloorSplat(float x,float y,int sector,float size,float angle,float stretch,bool ceiling,float born,int kind,int tint) {
    if(!bloodOn()||sector<0||sector>=numsectors)return;
    checkBloodLevel();
    if(bloodFloors.size()>=maxFloorBlood)bloodFloors.erase(bloodFloors.begin());
    bloodFloors.push_back({x,y,born,size,angle,std::max(1.0f,stretch),sector,splatVariant(kind),kind!=BLOOD_SMEAR&&randomRange(0,1)<0.5f,ceiling,
                           std::clamp(tint,0,BLOOD_TINTS-1)});
}
bool placeWallSplat(const WallHit &hit,float x,float y,float z,float size,float stretch,float born,int variant,bool drips,WallSplat &wall) {
    WallSpan span;if(!wallSpan(lines[hit.line],hit.side,hit.section,span))return false;
    float ex=span.b.x-span.a.x,ey=span.b.y-span.a.y,len=std::hypot(ex,ey);if(len<1)return false;
    if(z<span.bottom||z>span.top)return false;
    float along=std::clamp(((x-span.a.x)*ex+(y-span.a.y)*ey)/len,0.0f,len);
    wall={hit.line,hit.side,hit.section,along,z-span.anchor,born,size,std::max(1.0f,stretch),variant,randomRange(0,1)<0.5f,0,{},{}};
    // Runs from a heavy splat: one or two, down from its lower half.
    if(drips) {
        wall.drips=1+(randomRange(0,1)<0.4f);
        for(int n=0;n<wall.drips;++n) {wall.dripU[n]=randomRange(-0.3f,0.3f)*size;wall.dripLength[n]=randomRange(8,26);}
    }
    return true;
}
void bloodWallSplat(const WallHit &hit,float x,float y,float z,float size,float stretch,float born,int kind,bool drips,int tint) {
    if(!bloodOn())return;
    checkBloodLevel();
    BloodWall wall;
    if(!placeWallSplat(hit,x,y,z,size,stretch,born,splatVariant(kind),drips,wall))return;
    wall.tint=std::clamp(tint,0,BLOOD_TINTS-1);
    if(bloodWalls.size()>=maxWallBlood)bloodWalls.erase(bloodWalls.begin());
    bloodWalls.push_back(wall);
}
// Hitscan blood without the spray: splats land on the floor a little past
// the hit after their fall, and with harder hits on a wall close behind.
static void placeSplats(float x,float y,float z,float dx,float dy,int damage,int tint) {
    float now=gameSeconds();
    int count=1+(randomRange(0,30)<damage);
    for(int i=0;i<count;++i) {
        float reach=randomRange(4,36),side=randomRange(-10,10);
        float px=x+dx*reach-dy*side,py=y+dy*reach+dx*side,sx=px-x,sy=py-y,span=std::hypot(sx,sy);
        WallHit wall;
        if(span>0.5f&&firstSolidWall(x,y,z,sx/span,sy/span,0,span,wall))continue;
        const sector_t *sector=R_PointInSubsector((fixed_t)(px*FRACUNIT),(fixed_t)(py*FRACUNIT))->sector;
        float drop=z-floorZ(sector);
        if(sector->floorpic==skyflatnum||drop<0||drop>160)continue;
        float fall=(70+std::sqrt(4900+2450*drop))/1225; // Doom gravity after the 2 unit/tic hop.
        bloodFloorSplat(px,py,int(sector-sectors),randomRange(14,24)*(damage>=15?1.25f:1.0f),randomRange(0,2*doomPi),1,false,now+fall,BLOOD_SPLAT,tint);
    }
    if(randomRange(0,1)>=std::min(0.75f,damage/16.0f))return;
    float sz=z+randomRange(-10,6);
    WallHit hit;
    if(!firstSolidWall(x,y,sz,dx,dy,0,96,hit))return;
    bloodWallSplat(hit,x+dx*hit.t,y+dy*hit.t,sz,randomRange(16,26),1,now+hit.t/400,BLOOD_SPLAT,false,tint);
}
void bloodHit(mobj_t *target,mobj_t *shooter,fixed_t fx,fixed_t fy,fixed_t fz,fixed_t fdx,fixed_t fdy,fixed_t slope,int damage) {
    shotThrough(target,fx,fy,fz,fdx,fdy,damage);
    bodyHit(target,fx,fy,fz,fdx,fdy,damage);
    woundHit(target,fx,fy,fz,fdx,fdy,slope,damage,shooter);
    if(!bloodOn())return;
    checkBloodLevel();
    int tint=bloodTint(target);
    if(tint==BLOOD_NONE)return;
    float x=units(fx),y=units(fy),z=units(fz),dx=units(fdx),dy=units(fdy),length=std::hypot(dx,dy);
    if(length<0.001f)return;
    dx/=length;dy/=length;
    if(settings.bloodSpray)sprayHit(target,x,y,z,dx,dy,units(slope),damage);
    else placeSplats(x,y,z,dx,dy,damage,tint);
}
// The vertex lamp tint's alpha scales a decal's emission: how much of a
// glowing liquid's splat still glows (blood: none to scale).
static unsigned glowTint(float glow) {return 0xFFFFFFu|(unsigned)std::lround(std::clamp(glow,0.0f,1.0f)*255)<<24;}
// A rotated rectangle of a splat clipped to its sector's floor (or ceiling)
// polygons: size across, stretched along angle.
void flatSplat(int key,const Image &image,int sectorIndex,float cx,float cy,float size,float angle,float stretch,bool flip,bool ceiling,
               float wet,float dark,float wash,float glow,const Uniforms &camera) {
    const sector_t &sector=sectors[sectorIndex];
    if((ceiling?sector.ceilingpic:sector.floorpic)==skyflatnum)return;
    float h=size*0.5f,w=h*stretch*image.width/std::max(1,image.height),c=std::cos(angle),s=std::sin(angle),reach=std::hypot(w,h);
    Point across={c,s},up={-s,c};
    auto corner=[&](float u,float v){return Point{cx+across.x*u+up.x*v,cy+across.y*u+up.y*v};};
    std::vector<Point> square={corner(-w,-h),corner(w,-h),corner(w,h),corner(-w,h)};
    float lift=decalLift(cx,cy,camera),z=ceiling?ceilZ(&sector)-lift:floorZ(&sector)+lift,light=lighting(&sector);
    if(!ceiling&&waterIceAt(cx,cy)>=0.35f)z+=0.25f;   // on the ice laid over frozen water (water_ice.cpp, geometry.cpp)
    unsigned mode=MODE_BLOOD|sectorMode(&sector);
    auto &out=batches[key];
    for(int leaf:sectorFloors[sectorIndex]) {
        const auto &b=floorBounds[leaf];
        if(b[2]<cx-reach||b[0]>cx+reach||b[3]<cy-reach||b[1]>cy+reach)continue;
        auto polygon=floors[leaf];
        for(size_t n=0;n<square.size()&&!polygon.empty();++n) {
            Point a=square[n],d=square[(n+1)%square.size()];
            polygon=clip(polygon,a,{d.x-a.x,d.y-a.y},false);
        }
        auto vertex=[&](Point p) {
            float u=((p.x-cx)*across.x+(p.y-cy)*across.y+w)/(2*w),v=(h-((p.x-cx)*up.x+(p.y-cy)*up.y))/size;
            Vertex out={p.x,p.y,z,(flip?1-u:u)*image.width,v*image.height,light,mode,wet,dark,wash};
            out.lampTint=glowTint(glow);
            return out;
        };
        for(size_t n=1;n+1<polygon.size();++n)
            for(Point p:{polygon[0],polygon[n],polygon[n+1]})out.push_back(vertex(p));
    }
}
// A wall splat with its drips, lit like the wall; not drawn from behind.
void drawWallSplat(const WallSplat &d,int key,const Image &image,int dripKey,const Image &drip,float dark,float wash,float glow,
                   float now,const Uniforms &camera) {
    WallSpan span;if(!wallSpan(lines[d.line],d.side,d.section,span))return;
    float ex=span.b.x-span.a.x,ey=span.b.y-span.a.y,len=std::hypot(ex,ey);
    if(len<1||ex*(camera.eye[1]-span.a.y)-ey*(camera.eye[0]-span.a.x)>0)return; // Back of the wall.
    float zc=span.anchor+d.dz,h=d.size*0.5f,hu=h*d.stretch*image.width/std::max(1,image.height);
    float u0=std::max(0.0f,d.along-hu),u1=std::min(len,d.along+hu),z0=std::max(span.bottom,zc-h),z1=std::min(span.top,zc+h);
    if(u1<=u0||z1<=z0)return;
    const line_t &line=lines[d.line];const sector_t *front=sides[line.sidenum[d.side]].sector;
    float light=lighting(front);
    if(std::abs(ey)<0.01f)light=std::max(0.1f,light-0.04f);
    if(std::abs(ex)<0.01f)light=std::min(1.0f,light+0.04f);
    float tx=ex/len,ty=ey/len,lift=decalLift(span.a.x+tx*d.along,span.a.y+ty*d.along,camera)+0.25f,nx=ty*lift,ny=-tx*lift;
    unsigned mode=MODE_BLOOD|sectorMode(front);
    auto vertex=[&](float u,float z,float s,float t) {
        Vertex v={span.a.x+tx*u+nx,span.a.y+ty*u+ny,z,s,t,light,mode,0,dark,wash};
        v.lampTint=glowTint(glow);
        bakeWallCoordinates(line,d.side,u,v);
        return v;
    };
    auto texU=[&](float u){float t=(u-(d.along-hu))/(2*hu);return (d.flip?1-t:t)*image.width;};
    auto texV=[&](float z){return ((zc+h)-z)/(2*h)*image.height;};
    quad(batches[key],vertex(u0,z1,texU(u0),texV(z1)),vertex(u1,z1,texU(u1),texV(z1)),
         vertex(u1,z0,texU(u1),texV(z0)),vertex(u0,z0,texU(u0),texV(z0)));
    // Drips run down from the splat's lower half over three seconds,
    // slowing, in four-tic steps: the bead leads, the run follows.
    // (On Phobos nothing pulls them down.)
    for(int n=0;n<(Game_Phobos()?0:d.drips);++n) {
        float age=std::floor((now-d.born-0.3f)*TICRATE/4)*4/TICRATE;
        if(age<=0)continue;
        float grow=std::min(1.0f,age/3),length=std::round(d.dripLength[n]*(1-(1-grow)*(1-grow))),u=d.along+d.dripU[n];
        float top=zc-h*0.35f,bottom=std::max(span.bottom,top-length);
        if(length<1||top<=bottom||u<0.5f||u>len-1.5f)continue;
        float shown=std::min((float)drip.height,top-bottom);
        quad(batches[dripKey],vertex(u,top,0,drip.height-shown),vertex(u+drip.width,top,(float)drip.width,drip.height-shown),
             vertex(u+drip.width,bottom,(float)drip.width,(float)drip.height),vertex(u,bottom,0,(float)drip.height));
    }
}
// Wet blood steps from glossy to matte, darkening as it dries; values move
// in quarters and eighths at tic-rate ages, never smoothly.
static void bloodAge(float age,float wetFor,float dryOver,float &wet,float &dark) {
    float fresh=std::clamp(1-(age-wetFor)/dryOver,0.0f,1.0f);
    wet=settings.bloodShine?std::ceil(fresh*4)/4:0;
    dark=1-std::round((1-fresh)*0.28f*8)/8;
}
// Rained on (under the open sky, or on the floor a broken window lets the
// rain soak), blood never dries dark: it pales over 6 seconds, its red
// fading toward a dull grey in four steps (never brighter), on the floor
// spreading as it thins (spread: up to 1.6 times as big), then the rain
// takes it texel by texel, all gone 21 seconds after it was spilled. Not a
// simulation: exaggerated, so it shows. Returns what the shader reads
// (world.frag): the pale step (0-4) times two plus how much is washed away
// (in eighths, below 1); 2 or more with the wash at 1: gone.
static bool rainedOn(int sector,float x,float y) {
    if(!settings.weather||weatherNow(WEATHER_RAIN)<=0.01f)return false;
    return underSky(sector)||rainSoaked(sector,x,y);
}
static float rainWash(bool rained,float age,float &dark,float &spread) {
    spread=1;
    if(!rained)return 0;
    float pale=std::ceil(std::clamp(age/6,0.0f,1.0f)*4);
    dark=1;spread=1+0.15f*pale;
    float wash=std::clamp(std::floor((age-6)/15*8)/8,0.0f,1.0f);
    return wash>=1?-1:pale*2+wash;
}
// Phobos: blood spilled where there is no air boils. Its boiling point falls
// below body heat, so for its first boilFor seconds it froths, spitting pale
// pink vapour a few units up (paled from its own tint); the boiling cools what
// is left and it freezes: no gloss from then on, dark and dull. x, y, z: its
// middle; along, up: the surface's ways across it (a wall's vapour leaves
// sideways off it).
constexpr float boilFor=3;
static bool boiling(int sector,float age,float x,float y,float z,float size,float ox,float oy,float oz,
                    const Uniforms &camera,float &wet,float &dark,int tint) {
    if(!Game_Phobos()||Game_Air(sector)>=0.25f)return false;
    wet=0;dark*=0.72f;
    if(age<0||age>boilFor||std::hypot(x-camera.eye[0],y-camera.eye[1])>800)return true;
    uint32_t seed=hash32((uint32_t)std::lround(x*13+y*7+z*3));
    int specks=std::clamp((int)(size/3),3,10);
    float fade=1-age/boilFor;
    std::array<float,3> hue={1,0.62f,0.6f};
    if(tint!=BLOOD_RED) {
        auto c=bloodColor(0.9f,tint);float top=std::max({c[0],c[1],c[2],0.01f});
        for(int k=0;k<3;++k)hue[k]=0.6f+0.4f*c[k]/top;
    }
    for(int n=0;n<specks;++n) {
        float life=0.3f+0.25f*hashUnit(n,0,seed),clock=age+life*hashUnit(n,1,seed);
        int cycle=(int)std::floor(clock/life);
        float t=(clock-cycle*life)/life;
        uint32_t own=seed^(uint32_t)cycle*2654435761u;
        float r=size*0.4f*std::sqrt(hashUnit(n,2,own)),a=2*doomPi*hashUnit(n,3,own),rise=(2+5*hashUnit(n,4,own))*t;
        float px=x+r*std::cos(a),py=y+r*std::sin(a),pz=z;
        px+=ox*rise;py+=oy*rise;pz+=oz*rise;
        float pale=(0.55f+0.3f*hashUnit(n,5,own))*fade*(1-t);
        pixelBlock(particleVertices,camera,px,py,pz,0.5f,1,{pale*hue[0],pale*hue[1],pale*hue[2]});
    }
    return true;
}
void buildBlood(const Uniforms &camera) {
    if(!settings.blood||bloodPalette().empty())return;
    checkBloodLevel();
    float now=gameSeconds();
    // Pools: corpses that came to rest, once each, sized to the body.
    for(auto &entry:pooledCorpses)entry.second=false;
    for(int i=0;i<numsectors;++i)for(mobj_t *thing=sectors[i].thinglist;thing;thing=thing->snext) {
        if(!(thing->flags&MF_CORPSE)||!bleedsBlood(*thing))continue;
        auto seen=pooledCorpses.find(thing);
        if(seen!=pooledCorpses.end()) {seen->second=true;continue;}
        if(thing->z>thing->floorz||thing->momx||thing->momy||sectors[i].floorpic==skyflatnum)continue;
        pooledCorpses[thing]=true;
        if(bloodPools.size()>=64)bloodPools.erase(bloodPools.begin());
        bloodPools.push_back({units(thing->x),units(thing->y),now,std::clamp(units(thing->radius)*2.4f,28.0f,64.0f),randomRange(0,2*doomPi),i,bloodTint(thing)});
    }
    for(auto it=pooledCorpses.begin();it!=pooledCorpses.end();) it=it->second?std::next(it):pooledCorpses.erase(it);
    for(const auto &pool:bloodPools) {
        float age=now-pool.born;
        // The pool spreads over four seconds in eight steps.
        float grow=0.3f+0.7f*std::min(1.0f,std::floor(age*2)/8);
        float wet,dark;bloodAge(age,4,6,wet,dark);
        boiling(pool.sector,age,pool.x,pool.y,floorZ(&sectors[pool.sector]),pool.size*grow,0,0,1,camera,wet,dark,pool.tint);
        float spread,wash=rainWash(rainedOn(pool.sector,pool.x,pool.y),age,dark,spread);
        if(wash<0)continue;
        flatSplat(bloodKey(splatPool,pool.tint),bloodImage(splatPool,pool.tint),pool.sector,pool.x,pool.y,pool.size*grow*spread,pool.angle,1,false,false,wet,dark,wash,0,camera);
    }
    bloodFloors.erase(std::remove_if(bloodFloors.begin(),bloodFloors.end(),[&](const BloodFloor &d){return now-d.born>180;}),bloodFloors.end());
    for(const auto &d:bloodFloors) {
        if(now<d.born)continue;
        if(d.ceiling?camera.eye[2]>ceilZ(&sectors[d.sector]):camera.eye[2]<floorZ(&sectors[d.sector]))continue; // Back of the flat.
        float wet,dark;bloodAge(now-d.born,0,3,wet,dark);
        boiling(d.sector,now-d.born,d.x,d.y,d.ceiling?ceilZ(&sectors[d.sector]):floorZ(&sectors[d.sector]),d.size,0,0,d.ceiling?-1.0f:1.0f,camera,wet,dark,d.tint);
        float spread,wash=rainWash(!d.ceiling&&rainedOn(d.sector,d.x,d.y),now-d.born,dark,spread);
        if(wash<0)continue;
        flatSplat(bloodKey(d.variant,d.tint),bloodImage(d.variant,d.tint),d.sector,d.x,d.y,d.size*spread,d.angle,d.stretch,d.flip,d.ceiling,d.ceiling?0:wet,dark,wash,0,camera);
    }
    // Wall splats stay matte; thin spray doesn't glint believably.
    bloodWalls.erase(std::remove_if(bloodWalls.begin(),bloodWalls.end(),[&](const BloodWall &d){return now-d.born>180;}),bloodWalls.end());
    for(const auto &d:bloodWalls) {
        if(now<d.born)continue;
        WallSpan span;if(!wallSpan(lines[d.line],d.side,d.section,span))continue;
        float ex=span.b.x-span.a.x,ey=span.b.y-span.a.y,len=std::hypot(ex,ey);
        if(len<1||ex*(camera.eye[1]-span.a.y)-ey*(camera.eye[0]-span.a.x)>0)continue; // Back of the wall.
        float wet,dark;bloodAge(now-d.born,0,3,wet,dark);
        const line_t &line=lines[d.line];const sector_t *front=sides[line.sidenum[d.side]].sector;
        boiling(int(front-sectors),now-d.born,span.a.x+ex/len*d.along,span.a.y+ey/len*d.along,span.anchor+d.dz,d.size,
                            ey/len,-ex/len,0,camera,wet,dark,d.tint);
        float spread,wash=rainWash(settings.weather&&weatherNow(WEATHER_RAIN)>0.01f&&underSky(int(front-sectors)),now-d.born,dark,spread);
        if(wash<0)continue;
        drawWallSplat(d,bloodKey(d.variant,d.tint),bloodImage(d.variant,d.tint),bloodKey(splatDrip,d.tint),bloodImage(splatDrip,d.tint),dark,wash,0,now,camera);
    }
#ifdef DOOM_DEVTOOLS
    devBlood(camera);
#endif
    buildSpray(camera);
}
// Stepped with the game's wrap (carried.cpp): onto the floors and walls built
// alike; what lands off the map has left with the rest.
void bloodCarried(float dx,float dy) {
    for(auto &d:bloodFloors) {d.x+=dx;d.y+=dy;d.sector=sectorCarried(d.x,d.y);}
    for(auto &d:bloodPools) {d.x+=dx;d.y+=dy;d.sector=sectorCarried(d.x,d.y);}
    for(auto &d:bloodWalls)d.line=lineCarried(d.line,dx,dy);
    bloodFloors.erase(std::remove_if(bloodFloors.begin(),bloodFloors.end(),[](const BloodFloor &d){return d.sector<0;}),bloodFloors.end());
    bloodPools.erase(std::remove_if(bloodPools.begin(),bloodPools.end(),[](const BloodPool &d){return d.sector<0;}),bloodPools.end());
    bloodWalls.erase(std::remove_if(bloodWalls.begin(),bloodWalls.end(),[](const BloodWall &d){return d.line<0;}),bloodWalls.end());
}
void bloodShutdown() {
    bloodFloors.clear();bloodWalls.clear();bloodPools.clear();pooledCorpses.clear();spilledTints.clear();
    for(auto &shades:bloodShades)shades.clear();
    bloodShadesLoaded=false;
    sprayShutdown();
}
} // namespace doom3d
