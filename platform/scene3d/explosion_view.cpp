/* The physical explosion as the eye sees it (explosion_grid.cpp simulates it):
   the grid looked through along rays from the eye, one through each texel of
   a card that stands across the view through the middle of the gas, into two
   pictures of two-unit texels in the palette's colours. A ray stops at the
   map's own walls in the grid, so a pillar hides what is behind it. Along it
   smoke and dust take the light out and give back the light they stand in (the
   baked light of their column, and the fire's own from inside), heat glows
   through what is in front of it. A texel is fire where enough glow gets out,
   else smoke where enough is in the way, else nothing: decided against a noise
   and a grain, so thin smoke breaks into holes and colours break up texel by
   texel as painted fire does. What hangs right at the eye is left out (a
   sprite in the face is a wall of squares). The pictures are held for tics as
   Doom's frames are (explosions.cpp). */
#include "explosion_grid.h"

namespace doom3d {
namespace {
using G=BlastGrid;
constexpr int wide=272,tall=128;           // texels: the box's diagonal across, and over its height up
constexpr float texel=2;                  // map units a texel
constexpr float nearestPlane=56,eyeClear=40;   // the card no nearer the eye than this; nothing drawn of what is nearer than that
constexpr int viewKeyBase=-0x78000000;
int viewSerial=0;

// The palette's nearest colour to (r,g,b), each 0-1 and past: looked up, found once a cell.
byte paletteIndex(float r,float g,float b) {
    static std::vector<int16_t> nearest(32*32*32,-1);
    int cr=std::clamp((int)(r*31+0.5f),0,31),cg=std::clamp((int)(g*31+0.5f),0,31),cb=std::clamp((int)(b*31+0.5f),0,31);
    int16_t &index=nearest[(cr*32+cg)*32+cb];
    if(index<0) {
        const byte *palette=playpal();
        int best=0,bestDistance=INT_MAX,wr=cr*255/31,wg=cg*255/31,wb=cb*255/31;
        for(int i=0;i<256;++i) {
            int dr=palette[3*i]-wr,dg=palette[3*i+1]-wg,db=palette[3*i+2]-wb,d=dr*dr+dg*dg+db*db;
            if(d<bestDistance) {bestDistance=d;best=i;}
        }
        index=(int16_t)best;
    }
    return (byte)index;
}
void show(int &key,const std::vector<byte> &pixels) {
    static std::vector<byte> flat;
    if(!key)key=viewKeyBase-(++viewSerial&0x00ffffff);
    auto found=images.find(key);
    if(found==images.end()) {
        flat.assign((size_t)wide*tall*2,0);
        images.emplace(key,upload(wide,tall,pixels.data(),0,0,nullptr,flat.data()));
        return;
    }
    std::vector<byte> packed((size_t)wide*tall*4,0);
    for(size_t i=0;i<(size_t)wide*tall;++i) {packed[4*i]=pixels[2*i];packed[4*i+1]=pixels[2*i+1];}
    gpuUpdateTexture(found->second.texture,GpuFormat::RGBA8,{{0,0,wide,tall}},packed.data(),wide*4);
    found->second.pixels=pixels;
}
// A field between its cells. By a wall, a floor or a ceiling the solid corners are left out (their share
// goes to the open ones), so the gas stands right up against the surface: blended with the wall's nothing,
// it faded out half a cell short of it, and fire under a ceiling ended in a straight line below it.
float tap(const G &g,const std::vector<float> &f,int c,float fx,float fy,float fz) {
    constexpr int sx=1,sy=G::nx,sz=G::nx*G::ny;
    if(g.nearSolid[c]) {
        float sum=0,weight=0;
        for(int n=0;n<8;++n) {
            int at=c+(n&1)+((n>>1)&1)*sy+(n>>2)*sz;
            if(g.solid[at])continue;
            float wt=((n&1)?fx:1-fx)*((n&2)?fy:1-fy)*((n&4)?fz:1-fz);
            sum+=f[at]*wt;weight+=wt;
        }
        return weight>1e-4f?sum/weight:0;
    }
    float low=(f[c]*(1-fx)+f[c+sx]*fx)*(1-fy)+(f[c+sy]*(1-fx)+f[c+sy+sx]*fx)*fy;
    float high=(f[c+sz]*(1-fx)+f[c+sz+sx]*fx)*(1-fy)+(f[c+sz+sy]*(1-fx)+f[c+sz+sy+sx]*fx)*fy;
    return low+(high-low)*fz;
}
} // namespace

bool blastGridCard(BlastGrid &g,const Uniforms &camera,int step,BlastGridCard &card) {
    if(step==g.drawnStep) {card=g.card;return g.fire||g.smoke_;}
    static const bool timed=getenv("DERP_EXPLODE")!=nullptr;   // test runs: what a picture costs
    uint64_t began=timed?SDL_GetTicksNS():0;
    g.drawnStep=step;g.fire=g.smoke_=false;
    static std::vector<byte> fire,smoke;
    fire.assign((size_t)wide*tall*2,0);smoke.assign((size_t)wide*tall*2,0);
    const float *eye=camera.eye;
    // The card: across the eye's way to the middle of the gas, through that middle, no nearer than nearestPlane.
    float dx=g.massAt[0]-eye[0],dy=g.massAt[1]-eye[1],l=std::hypot(dx,dy);
    if(l<1) {dx=camera.forward[0];dy=camera.forward[1];l=std::max(1e-3f,std::hypot(dx,dy));dx/=l;dy/=l;l=0;}
    else {dx/=l;dy/=l;}
    float depth=std::max(l,nearestPlane),ax=dy,ay=-dx;   // (across the view, to its right)
    float px=eye[0]+dx*depth,py=eye[1]+dy*depth;         // the card's middle on the ground
    float bottom=std::floor((g.massAt[2]-tall*texel*0.5f)/texel)*texel;
    bottom=std::clamp(bottom,g.z0-16,g.z0+G::nz*G::cell-tall*texel+16);
    const float boxLow[3]={g.x0+0.5f*G::cell,g.y0+0.5f*G::cell,g.z0+0.5f*G::cell};
    const float boxHigh[3]={g.x0+(G::nx-1.5f)*G::cell,g.y0+(G::ny-1.5f)*G::cell,g.z0+(G::nz-1.5f)*G::cell};
    float nearestGas=1e9f;
    if(g.hi[0]>=g.lo[0]) {
        const float *noise=blastNoise();const byte *ramp=blastFireRamp();
        float glowing=std::min(1.6f,g.heatSum/30);
        std::vector<char> rowFire(tall,0),rowSmoke(tall,0);
        std::vector<float> rowNearest(tall,1e9f);   // the nearest gas drawn in each row, along the eye's way
        parallelFor(tall,[&](int j) {
            float z=bottom+(tall-1-j+0.5f)*texel;
            for(int i=0;i<wide;++i) {
                float s=(i+0.5f-wide/2)*texel;
                // From the eye through this texel: where the ray is in the box.
                float dir[3]={px+ax*s-eye[0],py+ay*s-eye[1],z-eye[2]};
                float length=std::sqrt(dir[0]*dir[0]+dir[1]*dir[1]+dir[2]*dir[2]);
                for(float &d:dir)d/=length;
                float from=eyeClear,to=1e9f;
                for(int n=0;n<3;++n) {
                    if(std::abs(dir[n])<1e-5f) {if(eye[n]<boxLow[n]||eye[n]>boxHigh[n])to=-1;continue;}
                    float a=(boxLow[n]-eye[n])/dir[n],b=(boxHigh[n]-eye[n])/dir[n];
                    from=std::max(from,std::min(a,b));to=std::min(to,std::max(a,b));
                }
                if(to<=from)continue;
                float grain=hashUnit(i,j,(uint32_t)step*0x9e3779b9u^g.seed)-0.5f,n=noise[((j+step/2)&63)*64+(i&63)];
                float through=1,glow=0,lit[3]={0,0,0},first=1e9f,pale=0;   // pale: how much of what is seen is dust, not soot
                for(float t=from+(grain+0.5f)*G::cell;t<to&&through>0.03f;t+=G::cell) {
                    float wx=eye[0]+dir[0]*t,wy=eye[1]+dir[1]*t,wz=eye[2]+dir[2]*t;
                    float fxAll=(wx-g.x0)/G::cell-0.5f,fyAll=(wy-g.y0)/G::cell-0.5f,fzAll=(wz-g.z0)/G::cell-0.5f;
                    int ci=(int)fxAll,cj=(int)fyAll,ck=(int)fzAll;
                    if(ci<0||cj<0||ck<0||ci>=G::nx-1||cj>=G::ny-1||ck>=G::nz-1)continue;
                    int here=G::at((int)(fxAll+0.5f),(int)(fyAll+0.5f),(int)(fzAll+0.5f));
                    if(g.solid[here])break;   // the map's own wall: nothing shows through it
                    // And the floor and the ceiling where they really are, not where the 8-unit cells put them: a cell
                    // is open when its middle is, so its gas stood up to four units into the ceiling over it.
                    const auto &span=g.between[(size_t)(here/G::nx%G::ny)*G::nx+here%G::nx];
                    if(wz<span[0]||wz>span[1])break;
                    // Nothing in this cell's part of the box: on.
                    if(ci<g.lo[0]-1||ci>g.hi[0]||cj<g.lo[1]-1||cj>g.hi[1]||ck<g.lo[2]-1||ck>g.hi[2])continue;
                    int c=G::at(ci,cj,ck);float fx=fxAll-ci,fy=fyAll-cj,fz=fzAll-ck;
                    float heat=tap(g,g.heat,c,fx,fy,fz),soot=tap(g,g.smoke,c,fx,fy,fz),dust=tap(g,g.dust,c,fx,fy,fz);
                    float burning=std::max(0.0f,heat-0.2f);
                    if(burning>0) {glow+=through*burning*0.75f;through*=1-std::min(0.45f,burning*0.3f);if(first>1e8f)first=(wx-eye[0])*dx+(wy-eye[1])*dy;}
                    float stuff=soot*0.36f+dust*0.4f;
                    if(stuff>0.01f) {
                        float cover=1-std::exp(-stuff),share=dust*0.4f/stuff;
                        const auto &room=g.light[(size_t)(here/G::nx%G::ny)*G::nx+here%G::nx];
                        // The fire's own light on it, from where the heat is: falling off with the distance.
                        float hx=wx-g.hotAt[0],hy=wy-g.hotAt[1],hz=wz-g.hotAt[2],own=glowing/(1+(hx*hx+hy*hy+hz*hz)/2500);
                        const float fireLight[3]={1.0f,0.55f,0.22f};
                        for(int ch=0;ch<3;++ch) {
                            float albedo=0.2f+(g.dustColour[ch]-0.2f)*share;
                            lit[ch]+=through*cover*albedo*(room[ch]+own*fireLight[ch]);
                        }
                        pale+=through*cover*share;
                        through*=1-cover;
                        if(cover>0.05f&&first>1e8f)first=(wx-eye[0])*dx+(wy-eye[1])*dy;
                    }
                }
                size_t at=(size_t)j*wide+i;
                // Mottled: hotter and cooler patches a few texels across, and the grain.
                float patch=noise[((j/2+step)&63)*64+((i/2+17)&63)];
                float seen=glow*(0.5f+patch)+((n-0.5f)*0.3f+grain*0.18f)*std::min(1.0f,glow*3);
                if(seen>0.2f) {
                    static const float bands[5]={1.5f,1.05f,0.72f,0.48f,0.32f};
                    int band=0;while(band<5&&seen<bands[band])++band;
                    fire[2*at]=ramp[band];fire[2*at+1]=255;rowFire[j]=1;rowNearest[j]=std::min(rowNearest[j],first);
                } else {
                    float cover=1-through;
                    if(cover>0.24f+(n-0.5f)*0.5f+grain*0.12f) {
                        float shade=1/std::max(cover,0.05f),rolls=0.6f+0.8f*n+grain*0.3f;   // rolls of it lighter and darker
                        // Smoke is grey: it takes a coloured room's hue only in part, and is never bright (under a blue
                        // lamp's full colour, unbounded, it was a flat magenta wall).
                        float seenAs[3]={lit[0]*shade,lit[1]*shade,lit[2]*shade},grey=0.3f*seenAs[0]+0.59f*seenAs[1]+0.11f*seenAs[2];
                        // Dust is not soot: it is the room's own stuff, as light as the wall it came off, and keeps
                        // more of its colour (a rocket's cloud, rolled off as smoke is, was a dark smoke again).
                        float dusty=std::clamp(pale/std::max(1-through,0.05f),0.0f,1.0f);
                        for(float &ch:seenAs)ch=grey+(ch-grey)*(0.35f+0.45f*dusty);
                        // (Rolled off toward a third of white, then its rolls: bounded flat, it was one colour all over.)
                        float peak=std::max({seenAs[0],seenAs[1],seenAs[2],1e-4f}),held=peak/(1+peak/(0.32f+0.4f*dusty));   // (dust up to 1.2 of white was too bright in some rooms; the user)
                        for(float &ch:seenAs)ch*=held/peak*rolls;
                        smoke[2*at]=paletteIndex(seenAs[0],seenAs[1],seenAs[2]);smoke[2*at+1]=255;rowSmoke[j]=1;rowNearest[j]=std::min(rowNearest[j],first);
                    }
                }
            }
        });
        for(int j=0;j<tall;++j) {g.fire|=rowFire[j]!=0;g.smoke_|=rowSmoke[j]!=0;nearestGas=std::min(nearestGas,rowNearest[j]);}
    }
    show(g.fireKey,fire);show(g.smokeKey,smoke);
    if(timed&&step%12==0)fprintf(stderr,"Blast picture at step %d: %.2f ms.\n",step,(SDL_GetTicksNS()-began)/1e6);
    g.card={g.fireKey,g.smokeKey,g.fire,g.smoke_,px,py,ax,ay,bottom,wide*texel,tall*texel,wide,tall,nearestGas<1e8f?std::max(0.0f,depth-nearestGas):0.0f};
    card=g.card;
    return g.fire||g.smoke_;
}
} // namespace doom3d
