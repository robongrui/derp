/* Blood on window glass: a drop that meets an unbroken pane (spray.cpp) splats on it, on the side it came from,
   and runs down it further than down a wall (glass holds nothing back). Seen from both sides, through the glass;
   gone with the pane when it breaks. Lit as the room on its side, glinting while fresh. Drawing only. */
#include "internal.h"

namespace doom3d {
namespace {
struct GlassStain {
    int pane;float along,z,size,born,side;   // side: +1 on the pane's normal side (glassPaneAt), -1 the other
    int variant,seed,tint,drips;float dripU[2],dripLength[2];
};
std::vector<GlassStain> stains;
unsigned stainSerial=0;
constexpr size_t mostStains=96;
constexpr float stainLasts=180;      // as wall blood's runs
constexpr float runSeconds=2;        // a run reaches its length (walls: three)

void checkLevel() {
    if(stainSerial==r_levelserial)return;
    stains.clear();stainSerial=r_levelserial;
}
}

void glassBloodLand(int pane,float x,float y,float z,float fromX,float fromY,float splat,float born,int kind,int tint) {
    GlassFace face;
    if(tint==BLOOD_NONE||!glassPaneAt(pane,face))return;
    checkLevel();
    float ex=face.bx-face.ax,ey=face.by-face.ay,len=std::hypot(ex,ey);
    if(len<1)return;
    ex/=len;ey/=len;
    GlassStain s{};
    s.pane=pane;s.along=std::clamp((x-face.ax)*ex+(y-face.ay)*ey,0.0f,len);s.z=std::clamp(z,face.bottom,face.top);
    s.size=splat;s.born=born;s.tint=tint;s.variant=splatVariant(kind);s.seed=(int)randomRange(0,4);
    s.side=(fromX-face.ax)*ey-(fromY-face.ay)*ex>=0?1.0f:-1.0f;
    s.drips=splat>=7?1+(randomRange(0,1)<0.5f):0;
    for(int n=0;n<s.drips;++n) {s.dripU[n]=randomRange(-0.3f,0.3f)*splat;s.dripLength[n]=randomRange(16,48);}
    if(stains.size()>=mostStains)stains.erase(stains.begin());
    stains.push_back(s);
}

void buildGlassBlood(const Uniforms &camera,float now) {
    checkLevel();
    GlassFace face;
    stains.erase(std::remove_if(stains.begin(),stains.end(),[&](const GlassStain &s){return now-s.born>stainLasts||!glassPaneAt(s.pane,face);}),stains.end());
    for(const GlassStain &s:stains) {
        if(now<s.born||!glassPaneAt(s.pane,face))continue;
        float ex=face.bx-face.ax,ey=face.by-face.ay,len=std::hypot(ex,ey);
        if(len<1)continue;
        float tx=ex/len,ty=ey/len,nx=ty*s.side,ny=-tx*s.side;
        const line_t &line=lines[face.line];
        // The room on its side, for its light; and which side of the map line that is, for the bake.
        float mx=face.ax+tx*s.along,my=face.ay+ty*s.along;
        const sector_t *sector=R_PointInSubsector((fixed_t)((mx+nx*8)*FRACUNIT),(fixed_t)((my+ny*8)*FRACUNIT))->sector;
        float lx=units(line.v1->x),ly=units(line.v1->y),lex=units(line.v2->x)-lx,ley=units(line.v2->y)-ly,ll=std::max(1e-3f,std::hypot(lex,ley));
        int mapSide=lex*(my+ny*8-ly)-ley*(mx+nx*8-lx)>0?1:0;
        float light=lighting(sector),lift=0.3f+decalLift(mx,my,camera);
        unsigned mode=MODE_BLOOD|sectorMode(sector);
        float wet,dark;bloodLayerAge(now-s.born,wet,dark);
        auto vertex=[&](float u,float z,float tu,float tv) {
            float wx=face.ax+tx*u+nx*lift,wy=face.ay+ty*u+ny*lift;
            Vertex v={wx,wy,z,tu,tv,light,mode,wet,dark,0};
            v.lampTint=0xFFFFFFu;
            bakeWallCoordinates(line,mapSide,((wx-lx)*lex+(wy-ly)*ley)/ll,v);
            return v;
        };
        int key;const Image &art=bloodArt(s.variant,s.seed,s.size,0,0,s.tint,key);
        float u0=std::round(s.along)-art.left,top=std::round(s.z)+art.top,u1=u0+art.width,bottom=top-art.height;
        float cu0=std::max(0.0f,u0),cu1=std::min(len,u1),cz0=std::max(face.bottom,bottom),cz1=std::min(face.top,top);
        if(cu1>cu0&&cz1>cz0)
            quad(batches[key],vertex(cu0,cz1,cu0-u0,top-cz1),vertex(cu1,cz1,cu1-u0,top-cz1),
                 vertex(cu1,cz0,cu1-u0,top-cz0),vertex(cu0,cz0,cu0-u0,top-cz0));
        // Its runs down the glass, the head leading (blood.cpp's run pictures), down to the sill.
        DripLooks drips=bloodDrips(s.tint);
        WallSplat d{};d.along=s.along;d.dz=s.z;
        for(int n=0;n<s.drips;++n) {
            float age=std::floor((now-s.born-0.2f)*TICRATE/4)*4/TICRATE;
            if(age<=0)continue;
            int look=dripLook(d,n);
            if(!drips.image[look])continue;
            const Image &drip=*drips.image[look];
            float grow=std::min(1.0f,age/runSeconds),length=std::round(s.dripLength[n]*(1-(1-grow)*(1-grow)));
            float u=std::round(s.along+s.dripU[n]),from=std::round(s.z-s.size*0.5f*0.35f),to=std::max(face.bottom,from-length);
            if(length<1||from<=to||u<0.5f||u>len-1.5f)continue;
            float shown=std::min((float)drip.height,from-to);
            quad(batches[drips.key[look]],vertex(u,from,0,drip.height-shown),vertex(u+drip.width,from,(float)drip.width,drip.height-shown),
                 vertex(u+drip.width,to,(float)drip.width,(float)drip.height),vertex(u,to,0,(float)drip.height));
        }
    }
}
} // namespace doom3d
