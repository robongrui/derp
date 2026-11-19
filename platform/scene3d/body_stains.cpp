/* Blood on bystanders (gore): the spray of a hit paints whoever stands in its way,
   in the colour of what it came from, a Cacodemon's blue on an imp. A stain is kept
   on the body (its height, and where round it), placed on whatever drawing shows,
   hidden on the side turned away, and goes down with the corpse in the wound mask.
   Never a hole, and nothing on top of him: the art itself takes the colour, its shading kept. Exaggerated on
   purpose, thick splats with streaks down from them. Drawing only. */
#include "internal.h"

namespace doom3d {
struct Stain { float height,side,front,radius;uint32_t seed;int tint;float from;int tic; };
static std::unordered_map<const mobj_t*,std::vector<Stain>> stains;
constexpr size_t maxStains=14;
constexpr float stainMerge=4,stainLeast=3,stainMost=9,stainGrow=1.2f;   // bigger than a drop would be: seen at a monster's size and in a fight, a true one reads as nothing
constexpr int stainsAHit=3;
constexpr float stainTint=0.88f,stainFull=1.0f;   // how far a texel goes to the blood's colour, its shading kept; texels of a stain take one or the other,
constexpr float stainFullShare=0.5f;       // this share the full one: an even tint read as a filter over him
constexpr float stainRun=1.6f;            // how far its streaks run down, in its radii, at most

// The art under a stain: each palette colour taken toward the blood's, keeping how light it is, so the monster's
// own shading and detail show through it. Worked out once per blood colour.
static byte tinted(byte colour,int tint,bool full) {
    static std::unordered_map<int,std::array<byte,256>> tables;
    int key=tint*2+(full?1:0);
    float amount=full?stainFull:stainTint;
    auto found=tables.find(key);
    if(found==tables.end()) {
        const byte *pal=playpal();
        const auto &shades=bloodPalette(tint);
        byte mid=shades.empty()?colour:shades[shades.size()*3/4];
        float br=pal[mid*3],bg=pal[mid*3+1],bb=pal[mid*3+2],blood=std::max({br,bg,bb,1.0f});
        std::array<byte,256> table{};
        for(int c=0;c<256;++c) {
            float r=pal[c*3],g=pal[c*3+1],b=pal[c*3+2],light=std::max({r,g,b})/blood;
            float wr=r+(br*light-r)*amount,wg=g+(bg*light-g)*amount,wb=b+(bb*light-b)*amount,best=1e12f;
            for(int n=16;n<256;++n) {
                float dr=pal[n*3]-wr,dg=pal[n*3+1]-wg,db=pal[n*3+2]-wb,d=dr*dr+dg*dg+db*db;
                if(d<best) {best=d;table[c]=(byte)n;}
            }
        }
        found=tables.emplace(key,table).first;
    }
    return found->second[colour];
}
// A drop landing on thing at (x,y,z) at the time from; splat: the size it would have left on a wall.
void bodyStain(const mobj_t *thing,float x,float y,float z,float splat,int tint,float from) {
    if(!thing||!thing->info)return;
    const auto &shades=bloodPalette(tint);
    if(shades.empty())return;
    float a=(float)(thing->angle*(2*doomPi/4294967296.0)),fx=std::cos(a),fy=std::sin(a);
    float ox=x-units(thing->x),oy=y-units(thing->y);
    float front=ox*fx+oy*fy,side=-ox*fy+oy*fx,height=(z-units(thing->z))/std::max(1.0f,units(thing->info->height));
    float radius=std::clamp(splat/3.5f,stainLeast,stainMost-2);
    auto &list=stains[thing];
    for(Stain &s:list)if(std::hypot(s.side-side,(s.height-height)*units(thing->info->height))<stainMerge&&s.front*front>=0) {
        s.radius=std::min(stainMost,s.radius+stainGrow);return;
    }
    // One hit's drops land together: one stain and a second at most, or they'd cycle through the list.
    int fresh=0;
    for(const Stain &s:list)fresh+=s.tic==leveltime;
    if(fresh>=stainsAHit)return;
    if(list.size()>=maxStains)list.erase(list.begin());
    uint32_t seed=hash32((uint32_t)leveltime*2654435761u^(uint32_t)list.size()*40503u^(uint32_t)(uintptr_t)thing);
    list.push_back({height,side,front,radius,seed,tint,from,leveltime});
    if(M_CheckParm((char*)"-ragdolllog"))fprintf(stderr,"gorestain height %.2f side %.1f radius %.1f\n",height,side,radius);
}
// Its stains into a drawing's wound mask (as colours, wounds.cpp), over nothing already there.
bool stainMask(const mobj_t &thing,const Image &image,bool flipped,float rx,float ry,std::vector<byte> &mask) {
    auto found=stains.find(&thing);
    if(found==stains.end()||image.pixels.empty())return false;
    int w=image.width,h=image.height;
    if(mask.size()!=(size_t)w*h)mask.assign((size_t)w*h,0);
    float a=(float)(thing.angle*(2*doomPi/4294967296.0)),fx=std::cos(a),fy=std::sin(a),now=gameSeconds();
    bool dead=thing.health<=0,any=false;
    for(const Stain &s:found->second) {
        if(now<s.from)continue;
        float ox=s.front*fx-s.side*fy,oy=s.front*fy+s.side*fx,u=s.side;
        if(!dead) {   // on the living, only from its own side; a corpse's drawing is the one it fell as
            float l=std::max(1e-3f,std::hypot(ox,oy));
            if((ox*ry-oy*rx)/l<0.05f)continue;
            u=ox*rx+oy*ry;
        }
        int col=(int)std::floor(image.left+(flipped?-u:u)),row=(int)std::floor(image.top-s.height*image.top);
        int reach=(int)std::ceil(s.radius);
        for(int dr=-reach;dr<=reach;++dr)for(int dc=-reach;dc<=reach;++dc) {
            int c=col+dc,r=row+dr;
            if(c<0||r<0||c>=w||r>=h)continue;
            size_t i=(size_t)r*w+c;
            if(!image.pixels[2*i+1]||mask[i])continue;
            if(std::hypot((float)dc,(float)dr)>s.radius*(0.6f+0.7f*hashUnit(dc,dr,s.seed)))continue;
            mask[i]=tinted(image.pixels[2*i],s.tint,hashUnit(dc,dr,s.seed^0x51u)<stainFullShare);any=true;
        }
        // Streaks down from it: painted on, not moving.
        for(int dc=-reach;dc<=reach;++dc) {
            float pick=hashUnit(dc,91,s.seed);
            if(pick>0.45f)continue;
            int c=col+dc,run=(int)(s.radius*stainRun*(0.4f+pick*1.3f));
            for(int r=row+(int)(s.radius*0.5f),end=r+run;r<end;++r) {
                if(c<0||r<0||c>=w||r>=h)continue;
                size_t i=(size_t)r*w+c;
                if(!image.pixels[2*i+1]||mask[i])continue;
                mask[i]=tinted(image.pixels[2*i],s.tint,true);any=true;
            }
        }
    }
    return any;
}
void forgetStains(const mobj_t *thing) {stains.erase(thing);}
void stainsShutdown() {stains.clear();}
}
