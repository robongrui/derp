/* The physical explosion (F4 Explosions: Physical; PLAN-explosions.md): hot gas
   simulated on a coarse grid round the blast, with the map in it, so the room
   decides the shape. 48 x 48 x 28 cells of 8 units (384 across: the room and what opens off it); a cell under a floor, over
   a ceiling or in the void is solid (a shut door is). Stable Fluids, a step a
   tic: the burst is a source of volume in the middle for a few tics (the solver
   finds its way out: up under the sky, flat under a low ceiling, through a
   doorway), then a little of it is drawn back (the blast wave's suction); heat
   rises, a swirl term puts back the rolling a coarse grid loses, heat cools to
   smoke, its rim first. Two mixes: a rocket is a charge (a hard short burst, a
   small hot middle, the room's dust lifted off the floor and the walls the
   front crosses), a barrel is fuel (fed for a quarter of a second, slow to
   cool, sooty). Cost where active: a step works only on the cells that hold
   anything and a few round them, on one thread (the engine's parallelFor
   starts its threads afresh each call: a dozen calls a step cost 19 ms); it
   lives two to three seconds, then is freed. Drawing only. */
#include "explosion_grid.h"
#include <queue>

namespace doom3d {
namespace {
using G=BlastGrid;
constexpr int nx=G::nx,ny=G::ny,nz=G::nz,sx=1,sy=nx,sz=nx*ny;
constexpr float frontSpeed=3;   // cells a tic the front runs out at: things answer as it arrives
constexpr int mostTics=250;   // (steps: half as many a tic once it only smokes)

inline bool inside(int i,int j,int k) {return i>=0&&j>=0&&k>=0&&i<nx&&j<ny&&k<nz;}
// The cells a step works on: what holds anything and a margin, never the box's outermost layer.
struct Region { int i0,i1,j0,j1,k0,k1; };
template<class Work> void each(const Region &r,Work work) {
    for(int k=r.k0;k<=r.k1;++k)for(int j=r.j0;j<=r.j1;++j) {
        int c=G::at(r.i0,j,k);
        for(int i=r.i0;i<=r.i1;++i,++c)work(i,j,k,c);
    }
}
// A field at (x,y,z) in cells. skipSolid: its solid corners left out, their share going to the others
// (nearSolid: whether there is a solid corner there at all).
inline float sample(const G &g,const std::vector<float> &f,float x,float y,float z,bool skipSolid) {
    x=std::clamp(x,0.0f,nx-1.001f);y=std::clamp(y,0.0f,ny-1.001f);z=std::clamp(z,0.0f,nz-1.001f);
    int i=(int)x,j=(int)y,k=(int)z,c=G::at(i,j,k);float fx=x-i,fy=y-j,fz=z-k;
    if(!skipSolid||!g.nearSolid[c]) {
        float low=(f[c]*(1-fx)+f[c+sx]*fx)*(1-fy)+(f[c+sy]*(1-fx)+f[c+sy+sx]*fx)*fy;
        float high=(f[c+sz]*(1-fx)+f[c+sz+sx]*fx)*(1-fy)+(f[c+sz+sy]*(1-fx)+f[c+sz+sy+sx]*fx)*fy;
        return low+(high-low)*fz;
    }
    float sum=0,weight=0;
    for(int n=0;n<8;++n) {
        int at=c+(n&1)+((n>>1)&1)*sy+(n>>2)*sz;
        if(g.solid[at])continue;
        float wt=((n&1)?fx:1-fx)*((n&2)?fy:1-fy)*((n&4)?fz:1-fz);
        sum+=f[at]*wt;weight+=wt;
    }
    return weight>1e-4f?sum/weight:0;
}
// Carried along the flow (u, v, w: the velocities to go back along).
void advect(const G &g,const Region &r,std::vector<float> &to,const std::vector<float> &from,bool scalar,float sink,
            const std::vector<float> &u,const std::vector<float> &v,const std::vector<float> &w) {
    each(r,[&](int i,int j,int k,int c) {
        to[c]=g.solid[c]?0:sample(g,from,i-u[c],j-v[c],k-w[c]+sink,scalar);
    });
}
// The burst and the suction after it: volume made (or taken) in the middle, per cell a tic.
float volumeSource(const G &g,int tic,float &coreRadius) {
    coreRadius=(g.fuel?2.4f:2.0f)*g.scale;
    float reach=(g.fuel?5.2f:5.4f)*g.scale,volume=4.19f*(reach*reach*reach-coreRadius*coreRadius*coreRadius);
    float core=4.19f*coreRadius*coreRadius*coreRadius;
    int burst=g.fuel?6:2;
    if(tic<burst)return volume/burst/core;
    if(tic<burst+4)return 0;   // (a fifth of it drawn back pulled the top of a rocket's fire down in the middle: flat, with horns)
    return 0;
}
// A wall gives its neighbour's own pressure back (nothing through it); the region's edge is open air.
// expand: how fast what burns swells, a tic, per unit of its heat over 0.4 (the fire is gas being made: with
// that told to the solver its push goes on and the walls steer it; untold, the solver took every push back
// out as soon as it was given, and the fire froze where the front had left it).
void project(G &g,const Region &r,float source,float coreRadius,float expand,int passes) {
    each(r,[&](int i,int j,int k,int c) {
        g.p[c]=0;
        if(g.solid[c]) {g.div[c]=0;return;}
        float d=0.5f*(g.u[c+sx]-g.u[c-sx]+g.v[c+sy]-g.v[c-sy]+g.w[c+sz]-g.w[c-sz]);
        float dx=i-g.bx,dy=j-g.by,dz=k-g.bz;
        if(source!=0&&dx*dx+dy*dy+dz*dz<coreRadius*coreRadius)d-=source;
        // (A charge's dust is pushed on as its fire would be: it is what its front is made of.)
        if(expand>0) {float pushed=std::max(0.0f,g.heat[c]-0.4f)+(g.fuel?0:0.4f*g.dust[c]);if(pushed>0)d-=expand*pushed;}
        g.div[c]=-d;
    });
    // (The region's own rim is left at 0: open. Gauss-Seidel, over-relaxed.)
    Region in={r.i0+1,r.i1-1,r.j0+1,r.j1-1,r.k0+1,r.k1-1};
    for(int pass=0;pass<passes;++pass)each(in,[&](int,int,int,int c) {
        if(g.solid[c])return;
        float own=g.p[c];
        auto nb=[&](int n){return g.solid[n]?own:g.p[n];};
        float sum=nb(c+sx)+nb(c-sx)+nb(c+sy)+nb(c-sy)+nb(c+sz)+nb(c-sz);
        g.p[c]=own+1.5f*((g.div[c]+sum)/6-own);
    });
    each(in,[&](int,int,int,int c) {
        if(g.solid[c]) {g.u[c]=g.v[c]=g.w[c]=0;return;}
        float own=g.p[c];
        auto nb=[&](int n){return g.solid[n]?own:g.p[n];};
        g.u[c]-=0.5f*(nb(c+sx)-nb(c-sx));g.v[c]-=0.5f*(nb(c+sy)-nb(c-sy));g.w[c]-=0.5f*(nb(c+sz)-nb(c-sz));
    });
}
// The swirl a coarse grid loses, put back (vorticity confinement): a, b, c hold the curl, div its size.
void swirl(G &g,const Region &r,float strength) {
    each(r,[&](int,int,int,int c) {
        g.a[c]=0.5f*(g.w[c+sy]-g.w[c-sy]-g.v[c+sz]+g.v[c-sz]);
        g.b[c]=0.5f*(g.u[c+sz]-g.u[c-sz]-g.w[c+sx]+g.w[c-sx]);
        g.c[c]=0.5f*(g.v[c+sx]-g.v[c-sx]-g.u[c+sy]+g.u[c-sy]);
        g.div[c]=std::sqrt(g.a[c]*g.a[c]+g.b[c]*g.b[c]+g.c[c]*g.c[c]);
    });
    Region in={r.i0+1,r.i1-1,r.j0+1,r.j1-1,r.k0+1,r.k1-1};
    each(in,[&](int,int,int,int c) {
        if(g.solid[c])return;
        float gx=0.5f*(g.div[c+sx]-g.div[c-sx]),gy=0.5f*(g.div[c+sy]-g.div[c-sy]),gz=0.5f*(g.div[c+sz]-g.div[c-sz]);
        float l=std::sqrt(gx*gx+gy*gy+gz*gz);
        if(l<1e-4f)return;
        gx/=l;gy/=l;gz/=l;
        g.u[c]+=strength*(gy*g.c[c]-gz*g.b[c]);g.v[c]+=strength*(gz*g.a[c]-gx*g.c[c]);g.w[c]+=strength*(gx*g.b[c]-gy*g.a[c]);
    });
}
void step(G &g) {
    int tic=g.tics++;
    float coreRadius,source=volumeSource(g,tic,coreRadius);
    int burst=g.fuel?6:2,feed=g.fuel?9:4;float front=tic*frontSpeed;
    // A rocket is a charge, not fuel (the user: it felt like petrol set alight, a car bomb): its fire is a small
    // white middle gone in a few tics, it leaves next to no smoke, and what its front carries through the room is
    // the room's own dust, which hangs under a second and sinks. A barrel is fuel and burns.
    float cooling=g.fuel?0.04f:0.2f,soot=g.fuel?0.05f:0.008f,lift=g.dry?(g.fuel?0.2f:1.0f):0;   // (no dust off a liquid: its splashes are the front's, explosions.cpp)
    // What it works on: what holds anything, the blast's middle while it bursts, the front while it lifts dust.
    // The fireball itself, while it bursts: a ball of heat set down each tic, growing to its full reach, lopsided
    // (three bulges, seeded), stopped by the map's solid cells. Left to the flow alone it came out square: a
    // coarse grid's burst runs harder along its axes (the user: cut off flat at the top, in any room).
    // The fire is a front that goes out over several tics, by the way round through open space (fillOrder: the open
    // cells nearest first, as many as its ball would hold in the open and more where it is hemmed in). Each cell
    // is set alight as the front reaches it and given the front's own speed along its way; behind the front the
    // fire cools and smokes as the rest does. So a wall sends it over the wall, a corridor along the corridor, a
    // doorway lets a tongue through, a pillar is gone round, and it is seen going there. (Set down whole in two
    // tics, it only cooled where it stood: the room clipped it and sent it nowhere; the user: it does not care
    // about its placement, it could be a sprite.)
    int fillTics=g.fuel?12:4;
    if(tic<fillTics) {
        float gone=1-(tic+1)/(float)fillTics;
        size_t want=std::min(g.fillOrder.size(),(size_t)(g.fillOrder.size()*(1-gone*gone)));   // fast at first, slowing
        for(size_t n=g.fillDone;n<want;++n) {
            int c=g.fillOrder[n];
            float speed=(g.fuel?1.4f:2.8f)*(1-0.6f*n/g.fillOrder.size());
            if(g.fuel||n<g.fillOrder.size()/8)g.heat[c]=std::max(g.heat[c],g.fuel?1.35f:1.9f);   // (a charge: its nearest eighth alone is fire)
            else if(g.dry)g.dust[c]=std::max(g.dust[c],0.75f*(1-0.6f*n/g.fillOrder.size()));   // (1.3: too much of it; the user)
            g.u[c]=g.fillWay[n][0]*speed;g.v[c]=g.fillWay[n][1]*speed;g.w[c]=g.fillWay[n][2]*speed;
            const int here[3]={c%nx,c/nx%ny,c/(nx*ny)};
            for(int a=0;a<3;++a) {
                if(g.hi[0]<g.lo[0]&&a==0) {for(int b=0;b<3;++b)g.lo[b]=g.hi[b]=here[b];}
                g.lo[a]=std::min(g.lo[a],here[a]);g.hi[a]=std::max(g.hi[a],here[a]);
            }
        }
        g.fillDone=std::max(g.fillDone,want);
    }
    int reach=(int)std::ceil(std::max(coreRadius,front<15?front+2:0.0f));
    int low[3]={(int)g.bx-reach,(int)g.by-reach,(int)g.bz-reach},high[3]={(int)g.bx+reach+1,(int)g.by+reach+1,(int)g.bz+reach+1};
    if(g.hi[0]>=g.lo[0])for(int n=0;n<3;++n) {low[n]=std::min(low[n],g.lo[n]);high[n]=std::max(high[n],g.hi[n]);}
    // (A wide margin while it bursts: the fire grows faster than four cells a tic then, and with four it was cut to
    // the region's box each tic: a square fireball with a flat top, seen wherever no low ceiling hid it; the user.)
    int margin=5;
    Region r={std::max(1,low[0]-margin),std::min(nx-2,high[0]+margin),std::max(1,low[1]-margin),std::min(ny-2,high[1]+margin),
              std::max(1,low[2]-margin),std::min(nz-2,high[2]+margin)};
    each(r,[&](int i,int j,int k,int c) {
        if(g.solid[c])return;
        float dx=i-g.bx,dy=j-g.by,dz=k-g.bz,d=std::sqrt(dx*dx+dy*dy+dz*dz);
        if(tic<feed&&d<coreRadius)g.heat[c]=std::max(g.heat[c],g.fuel?1.35f:1.9f);

        // The front crossing a floor or a wall lifts what lies on it: the room's dust, the more the nearer.
        // (Not the ceiling over it: what that lets go falls as specks, explosions.cpp; a layer of it hung there flat.)
        if(front<15&&g.reached[c]&&std::abs(d-front)<frontSpeed*0.5f&&
           (g.solid[c-sx]||g.solid[c+sx]||g.solid[c-sy]||g.solid[c+sy]||g.solid[c-sz]))
            g.dust[c]=std::min(1.6f,g.dust[c]+lift*1.5f/(1+d*d/30)*(0.6f+0.8f*hashUnit(i,j*37+k,g.seed)));
        // Hot gas rises; soot and dust weigh it down.
        // (Fresh smoke is still warm and rises with it: weighed down, it hung where the fire had been.)
        g.w[c]+=0.075f*g.heat[c]+0.012f*g.smoke[c]-0.02f*g.dust[c];
        g.u[c]*=0.985f;g.v[c]*=0.985f;g.w[c]*=0.985f;
    });
    swirl(g,r,0.4f);
    g.a=g.u;g.b=g.v;g.c=g.w;
    advect(g,r,g.u,g.a,false,0,g.a,g.b,g.c);advect(g,r,g.v,g.b,false,0,g.a,g.b,g.c);advect(g,r,g.w,g.c,false,0,g.a,g.b,g.c);
    // (A quarter a tic, and four times the volume hemmed in, lost the open ground its mushroom: a blob that
    // swelled and went black before it could rise.)
    float swell=tic<fillTics?0.12f:std::max(0.0f,0.12f*(1-(tic-fillTics)/10.0f));
    project(g,r,source,coreRadius,swell,tic<fillTics+10?12:8);
    advect(g,r,g.a,g.heat,true,0,g.u,g.v,g.w);
    advect(g,r,g.b,g.smoke,true,0,g.u,g.v,g.w);
    advect(g,r,g.c,g.dust,true,0.1f,g.u,g.v,g.w);   // dust sinks through the air that carries it
    // The smoke thins fast while it burns under it, slowly once it hangs: a layer under the ceiling for some
    // seconds, drifting out by whatever opening the box holds.
    float keeps=g.fuel?(g.heatSum>1?0.98f:0.965f):0.9f;   // (a barrel's smoke hangs a second and a half, not three and a half; the user)
    float sum=0,at[3]={},most=0,mass=0,middle[3]={};
    int lo[3]={nx,ny,nz},hi[3]={-1,-1,-1};
    each(r,[&](int i,int j,int k,int c) {
        if(g.solid[c]) {g.heat[c]=g.smoke[c]=g.dust[c]=0;return;}
        float h=g.a[c];
        // Its rim mixes with the air and cools first (by how much colder the cells round it are): hot inside, as fire is.
        float round=0;int count=0;
        for(int n:{c-sx,c+sx,c-sy,c+sy,c-sz,c+sz})if(!g.solid[n]) {round+=g.a[n];++count;}
        float colder=count?std::max(0.0f,h-round/count):0;
        float smoke=std::min(1.6f,g.b[c]*keeps+soot*std::max(0.0f,h-0.2f)),dust=g.c[c]*(g.fuel?0.98f:0.905f);
        h=std::max(0.0f,h-h*cooling-0.03f*h*h-(g.fuel?0.35f:0.05f)*colder);
        // Out through the box's sides and its top: gone, thinning over its last six cells. (Cut in its last two, a
        // fire rising in a tall room ended in a flat line at the box's top, as if under a low ceiling; the user.)
        int edge=std::min({i,j,nx-1-i,ny-1-j,nz-1-k});
        if(edge<6) {float left=0.55f+0.075f*edge;h*=left;smoke*=left;dust*=left;}
        g.heat[c]=h;g.smoke[c]=smoke;g.dust[c]=dust;
        float holds=std::max({h,smoke,dust});
        if(holds<0.02f)return;
        most=std::max(most,holds);
        sum+=h;at[0]+=h*i;at[1]+=h*j;at[2]+=h*k;
        float all=h+smoke+dust;mass+=all;middle[0]+=all*i;middle[1]+=all*j;middle[2]+=all*k;
        const int here[3]={i,j,k};
        for(int n=0;n<3;++n) {lo[n]=std::min(lo[n],here[n]);hi[n]=std::max(hi[n],here[n]);}
    });
    std::copy(lo,lo+3,g.lo);std::copy(hi,hi+3,g.hi);
    g.heatSum=sum;
    if(sum>0.5f) {
        g.hotAt[0]=g.x0+(at[0]/sum+0.5f)*G::cell;g.hotAt[1]=g.y0+(at[1]/sum+0.5f)*G::cell;g.hotAt[2]=g.z0+(at[2]/sum+0.5f)*G::cell;
    }
    if(mass>0.1f) {
        g.massAt[0]=g.x0+(middle[0]/mass+0.5f)*G::cell;g.massAt[1]=g.y0+(middle[1]/mass+0.5f)*G::cell;g.massAt[2]=g.z0+(middle[2]/mass+0.5f)*G::cell;
    }
    g.live=g.tics<mostTics&&(most>0.05f||g.tics<12);
}
} // namespace

BlastGrid *startBlastGrid(float x,float y,float z,bool fuel,float scale,uint32_t seed) {
    G *made=new G;G &g=*made;
    g.fuel=fuel;g.scale=scale;g.seed=seed;
    const sector_t *own=R_PointInSubsector((fixed_t)(x*FRACUNIT),(fixed_t)(y*FRACUNIT))->sector;
    // Two layers of floor under it; the blast two fifths of the way up at most.
    g.x0=x-nx*G::cell/2;g.y0=y-ny*G::cell/2;g.z0=std::max(floorZ(own)-2*G::cell,z-nz*G::cell*0.3f);   // (most of the box over the blast: what burns goes up)
    g.z0=std::floor(g.z0/G::cell)*G::cell;g.x0=std::floor(g.x0/G::cell)*G::cell;g.y0=std::floor(g.y0/G::cell)*G::cell;
    g.bx=(x-g.x0)/G::cell-0.5f;g.by=(y-g.y0)/G::cell-0.5f;g.bz=(z-g.z0)/G::cell-0.5f;
    g.solid.assign(G::cells,0);g.reached.assign(G::cells,0);g.nearSolid.assign(G::cells,0);
    for(auto *f:{&g.u,&g.v,&g.w,&g.heat,&g.smoke,&g.dust,&g.p,&g.div,&g.a,&g.b,&g.c})f->assign(G::cells,0);
    g.light.assign((size_t)nx*ny,{0,0,0});g.between.assign((size_t)nx*ny,{1e9f,-1e9f});
    for(int j=0;j<ny;++j)for(int i=0;i<nx;++i) {
        float cx=g.x0+(i+0.5f)*G::cell,cy=g.y0+(j+0.5f)*G::cell;
        int mx=(int)std::floor((cx-mapOrigin[0])/mapCell),my=(int)std::floor((cy-mapOrigin[1])/mapCell);
        bool nothing=seamCells.empty()||mx<0||my<0||mx>=mapWidth||my>=mapHeight||seamCells[(size_t)my*mapWidth+mx].own==noSector;
        const sector_t *sector=nothing?nullptr:R_PointInSubsector((fixed_t)(cx*FRACUNIT),(fixed_t)(cy*FRACUNIT))->sector;
        float floor=sector?floorZ(sector):0,ceiling=sector?ceilZ(sector):0;bool sky=sector&&sector->ceilingpic==skyflatnum;
        for(int k=0;k<nz;++k) {
            float cz=g.z0+(k+0.5f)*G::cell;
            g.solid[G::at(i,j,k)]=!sector||cz<floor||(!sky&&cz>ceiling);
        }
        if(sector)g.between[(size_t)j*nx+i]={floor,sky?1e9f:ceiling};
        // The light there, as a thing standing in it would take it (flicker left out: it is read once).
        if(sector&&ceiling>floor)g.light[(size_t)j*nx+i]=bakedLightAt(cx,cy,floor+std::min(48.0f,(ceiling-floor)*0.5f),sector);
    }
    // What the blast can get to: out from its own cell through the open ones.
    int bi=std::clamp((int)std::lround(g.bx),1,nx-2),bj=std::clamp((int)std::lround(g.by),1,ny-2),bk=std::clamp((int)std::lround(g.bz),1,nz-2);
    g.solid[G::at(bi,bj,bk)]=0;
    std::vector<int> open={G::at(bi,bj,bk)};g.reached[open[0]]=1;
    for(size_t n=0;n<open.size();++n) {
        int c=open[n],i=c%nx,j=c/nx%ny,k=c/(nx*ny);
        for(int m=0;m<6;++m) {
            int a=i+(m==0)-(m==1),b=j+(m==2)-(m==3),e=k+(m==4)-(m==5);
            if(!inside(a,b,e))continue;
            int to=G::at(a,b,e);
            if(g.solid[to]||g.reached[to])continue;
            g.reached[to]=1;open.push_back(to);
        }
    }
    // The way round: each open cell's distance from the blast through open space (26 ways a step, none through a
    // solid corner), and the cell it was come to from; then nearest first, lopsided (three bulges, seeded) and ragged.
    {
        constexpr float furthest=34;
        std::vector<float> far(G::cells,1e9f);
        std::vector<int> from(G::cells,-1);
        using Step=std::pair<float,int>;
        std::priority_queue<Step,std::vector<Step>,std::greater<Step>> next;
        int origin=G::at(bi,bj,bk);far[origin]=0;next.push({0.0f,origin});
        while(!next.empty()) {
            auto [d,c]=next.top();next.pop();
            if(d>far[c]||d>furthest)continue;
            int i=c%nx,j=c/nx%ny,k=c/(nx*ny);
            for(int dk=-1;dk<=1;++dk)for(int dj=-1;dj<=1;++dj)for(int di=-1;di<=1;++di) {
                if(!di&&!dj&&!dk)continue;
                int a=i+di,b=j+dj,e=k+dk;
                if(a<1||b<1||e<1||a>=nx-1||b>=ny-1||e>=nz-1)continue;
                if(g.solid[G::at(a,b,e)]||g.solid[G::at(a,j,k)]||g.solid[G::at(i,b,k)]||g.solid[G::at(i,j,e)])continue;
                float to=d+std::sqrt((float)(di*di+dj*dj+dk*dk));
                int n=G::at(a,b,e);
                if(to<far[n]) {far[n]=to;from[n]=c;next.push({to,n});}
            }
        }
        float bulge[3][3];
        for(int n=0;n<3;++n) {
            float zc=hashUnit(n,1,seed)*1.4f-0.4f,a=hashUnit(n,2,seed)*2*doomPi,flat=std::sqrt(std::max(0.0f,1-std::min(zc*zc,1.0f)));
            bulge[n][0]=flat*std::cos(a);bulge[n][1]=flat*std::sin(a);bulge[n][2]=std::min(zc,1.0f);
        }
        float ballReach=(fuel?5.2f:5.4f)*scale;
        std::vector<Step> order;int open=0;
        for(int c=0;c<G::cells;++c) {
            if(far[c]>furthest)continue;
            int i=c%nx,j=c/nx%ny,k=c/(nx*ny);
            float dx=i-g.bx,dy=j-g.by,dz=k-g.bz,d=std::sqrt(dx*dx+dy*dy+dz*dz),out=1;
            if(d<=ballReach*2)++open;
            if(d>0.5f)for(int n=0;n<3;++n) {float along=std::max(0.0f,(dx*bulge[n][0]+dy*bulge[n][1]+dz*bulge[n][2])/d);out+=0.25f*along*along*along*along;}
            // (Where the way round is as good as straight, the straight distance: steps on a grid make a ball
            // with flat faces and cut corners, which read as a box.)
            float way=far[c]<=d*1.12f+0.5f?d:far[c];
            order.push_back({way/(out*(0.92f+0.16f*hashUnit(i,j*31+k,seed^0x51u))),c});
        }
        std::sort(order.begin(),order.end());
        // Hemmed in, it goes further: what a wall, a floor or a corridor keeps it out of comes out the other ways,
        // and more of it (a blast in a corridor runs far down it). By how much of its open ball the map leaves it.
        // (Looked for twice as far out as its ball goes: a rocket's ball just fits a corridor 96 wide, and by its own
        // reach alone it was not hemmed in there at all.)
        float whole=4.19f*ballReach*ballReach*ballReach,hemmed=1-std::min(1.0f,open/(whole*8));
        size_t most=std::min(order.size(),(size_t)(whole*(1+1.6f*hemmed)));
        for(size_t n=0;n<most;++n) {
            int c=order[n].second,back=c;
            for(int steps=0;steps<3&&from[back]>=0;++steps)back=from[back];
            // The way it is going there: along its way round (three cells of it), or straight out of the middle.
            float w[3]={(float)(c%nx-back%nx),(float)(c/nx%ny-back/nx%ny),(float)(c/(nx*ny)-back/(nx*ny))};
            float l=std::sqrt(w[0]*w[0]+w[1]*w[1]+w[2]*w[2]);
            g.fillOrder.push_back(c);
            g.fillWay.push_back(l>0.5f?std::array<float,3>{w[0]/l,w[1]/l,w[2]/l}:std::array<float,3>{0,0,1});
        }
    }
    // Where a sample may have a solid corner (the slow way round them is taken only there).
    for(int k=0;k<nz-1;++k)for(int j=0;j<ny-1;++j)for(int i=0;i<nx-1;++i) {
        int c=G::at(i,j,k);
        g.nearSolid[c]=g.solid[c]|g.solid[c+sx]|g.solid[c+sy]|g.solid[c+sy+sx]|g.solid[c+sz]|g.solid[c+sz+sx]|g.solid[c+sz+sy]|g.solid[c+sz+sy+sx];
    }
    // The room's dust: its floor's colour, paled.
    if(own->floorpic!=skyflatnum) {
        const auto &average=averageColor(lumpImage(firstflat+flattranslation[own->floorpic],true));
        for(int c=0;c<3;++c)g.dustColour[c]=average[c]+(0.55f-average[c])*0.2f;   // (paled by nearly half it stood out too bright in dark rooms; the user)
    }
    g.dry=!overLiquid(x,y);
    g.hotAt[0]=g.massAt[0]=x;g.hotAt[1]=g.massAt[1]=y;g.hotAt[2]=g.massAt[2]=z;
    return made;
}
// On to age tics after the blast (three steps a call at most: a frame missed is caught up). False once nothing is left.
bool blastGridTic(BlastGrid &g,int age) {
    static const bool timed=getenv("DERP_EXPLODE")!=nullptr;   // test runs: what a step costs
    // (Past its fire it is stepped every other tic: smoke that only drifts.)
    int wanted=age<70?age:70+(age-70)/2;
    for(int n=0;n<3&&g.tics<=wanted&&g.live;++n) {
        uint64_t from=timed?SDL_GetTicksNS():0;
        step(g);
        if(timed&&g.tics%10==1)fprintf(stderr,"Blast grid tic %d: step %.2f ms, heat %.1f.\n",g.tics,(SDL_GetTicksNS()-from)/1e6,g.heatSum);
    }
    return g.live;
}
bool blastGridHeat(const BlastGrid &g,float at[3]) {
    std::copy(g.hotAt,g.hotAt+3,at);
    return g.heatSum>0.5f;
}
void dropBlastGrid(BlastGrid *g) {
    if(!g)return;
    for(int key:{g->fireKey,g->smokeKey}) {
        if(!key)continue;
        images.erase(key);
        for(auto *group:{&batches,&mirrorBatches,&shadowBatches,&decalBatches})group->erase(key);
    }
    delete g;
}
} // namespace doom3d
