/* An explosion's fireball (explosions.cpp), in place of the game's sprite: a
   picture drawn afresh on tics, a Doom pixel a unit, in the palette's own
   fire colours. A dozen and more lobes burst out of the middle, lopsided
   (most of them one way, seeded per blast), hot white at first, cooling
   through yellow, orange and red in a few tics; cooled, a lobe is smoke,
   rolling up and swelling while holes are eaten into it until nothing is
   left. Smoke in front hides the fire behind it except through its holes.
   Two pictures of the same size: the fire (drawn bright, blooming) and the
   smoke (lit by the room and by the blast's own afterglow); a pixel is in
   one or the other. Fast hit, slow recovery: the burst takes two or three
   tics, the smoke three seconds. Drawing only. */
#include "explosion_grid.h"

namespace doom3d {
constexpr int fireKeyBase=-0x76000000;   // image keys clear of the others (wobbles and layers end at -0x74...)
constexpr float smokeUnder=0.22f;        // cooler than this a lobe is smoke
static int fireSerial=0;

// Value noise that tiles, two octaves, 64 x 64: looked up, not worked out, for every pixel of every lobe.
const float *blastNoise() {
    static float table[64*64];static bool made=false;
    if(made)return table;
    made=true;
    auto octave=[](int i,int j,int cells,uint32_t seed) {
        float x=i*cells/64.0f,y=j*cells/64.0f;int ix=(int)x,iy=(int)y;
        float fx=x-ix,fy=y-iy;fx=fx*fx*(3-2*fx);fy=fy*fy*(3-2*fy);
        auto at=[&](int cx,int cy){return hashUnit(cx%cells,cy%cells,seed);};
        float top=at(ix,iy)+(at(ix+1,iy)-at(ix,iy))*fx,bottom=at(ix,iy+1)+(at(ix+1,iy+1)-at(ix,iy+1))*fx;
        return top+(bottom-top)*fy;
    };
    for(int j=0;j<64;++j)for(int i=0;i<64;++i)table[j*64+i]=0.6f*octave(i,j,8,0x51ed270bu)+0.4f*octave(i,j,16,0x2f6b3a1du);
    return table;
}
static byte nearestIndex(int r,int g,int b) {
    const byte *palette=playpal();
    int best=0,bestDistance=INT_MAX;
    for(int i=0;i<256;++i) {
        int dr=palette[3*i]-r,dg=palette[3*i+1]-g,db=palette[3*i+2]-b,d=dr*dr+dg*dg+db*db;
        if(d<bestDistance) {bestDistance=d;best=i;}
    }
    return (byte)best;
}
// The fire's colours, hottest first, and the smoke's, darkest first.
const byte *blastFireRamp() {
    static byte ramp[6];static bool made=false;
    if(!made) {
        made=true;
        static const int rgb[6][3]={{255,255,215},{255,231,107},{243,163,43},{215,95,11},{163,47,7},{99,23,7}};
        for(int n=0;n<6;++n)ramp[n]=nearestIndex(rgb[n][0],rgb[n][1],rgb[n][2]);
    }
    return ramp;
}
static const byte *smokeRamp() {
    static byte ramp[4];static bool made=false;
    if(!made) {
        made=true;
        static const int grey[4]={28,44,62,84};
        for(int n=0;n<4;++n)ramp[n]=nearestIndex(grey[n],grey[n]-3,grey[n]-6);
    }
    return ramp;
}

// scale: 1 a rocket's, more a barrel's. hot: how many tics the fire lasts. onFloor: nothing bursts
// downward. headroom: the room over its middle, in units (a low ceiling presses it flat and wide).
void startBlastPicture(BlastPicture &p,float scale,uint32_t seed,int hot,bool onFloor,float headroom,bool burst,const float *out) {
    auto rnd=[&] {seed=hash32(seed+0x9e3779b9u);return (float)(seed>>8)/16777216.0f;};
    p.reach=44*scale;p.burst=burst;
    if(burst) {
        // Flashbulb's burst (PLAN-explosions-flashbulb.md): a dense knot, then a few broad lobes all round it that
        // give it weight at its peak (Doom-like's size), then pull apart outward while the middle burns through.
        // A barrel's are more and climb. hot: the tic the last of it is gone by.
        p.size=std::clamp(((int)(p.reach*5.4f)+3)/4*4,96,320);
        p.middle=0.6f;p.hot=hot;p.drawnStep=-1;p.lobes.clear();
        float press=std::clamp(headroom/(p.reach*1.6f),0.35f,1.0f);
        // The fire has a direction (the plan's next pass): its lobes are points with ways of their own in the world,
        // uneven in size and travel, so the peak is one burst with weight and not a ring of equal puffs.
        auto lobe=[&](float x,float y,float z,float reach,float radius,float birth,float rise,bool core=false) {
            FireLobe l{};
            float w[3]={x+(rnd()-0.5f)*0.3f,y+(rnd()-0.5f)*0.3f,z+(rnd()-0.5f)*0.3f};
            if(onFloor&&!core)w[2]=std::abs(w[2])*0.9f+0.05f;
            w[2]*=press;
            float len=std::max(1e-3f,std::sqrt(w[0]*w[0]+w[1]*w[1]+w[2]*w[2]));
            for(int c=0;c<3;++c)l.way[c]=w[c]/len;
            l.core=core;l.reach=reach;l.radius=p.reach*radius;l.birth=birth;l.rise=rise*press;
            l.heat=core?1.5f:0.78f+0.14f*rnd();   // (the knot white, the lobes orange from the start)
            l.tau=core?3.5f:9.0f;l.life=1;
            l.depth=core?-1+0.1f*rnd():rnd();
            l.ox=(int)(rnd()*64);l.oy=(int)(rnd()*64);
            float a=rnd()*2*doomPi;l.dx=std::cos(a);l.dy=std::sin(a);   // (the knot's own small scatter, in the picture)
            p.lobes.push_back(l);
        };
        for(int n=0;n<3;++n)lobe(0,0,0,rnd()*0.15f,0.6f+0.15f*rnd(),0,0,true);
        bool barrel=scale>1.1f,surface=out&&out[0]*out[0]+out[1]*out[1]+out[2]*out[2]>0.25f;
        float side=rnd()<0.5f?-1.0f:1.0f,turn=rnd()*2*doomPi,hx=std::cos(turn),hy=std::sin(turn);
        if(barrel) {
            // A broad lobe pushed out low, a smaller one the other way, and an off-centre tongue that climbs a
            // tic or two behind them; three of middling size between.
            lobe(hx,hy,0.08f,0.6f,0.72f,0,0);
            lobe(-hx*0.8f+hy*0.6f,-hy*0.8f-hx*0.6f,0.15f,0.7f,0.5f,0.3f,0.2f);
            lobe(hx*0.35f*side,hy*0.35f*side,0.95f,1.25f,0.42f,1.7f,1.7f);
            lobe(hy,-hx,0.6f,0.85f,0.44f,0.4f,0.9f);
            lobe(-hy,hx,0.5f,0.8f,0.38f,0.7f,0.8f);
            lobe(-hx*0.3f,-hy*0.3f,0.9f,0.75f,0.36f,0.9f,1.1f);
        } else if(surface) {
            // Off a wall or the floor: the two largest away from it, two smaller spread along its face each way,
            // two between.
            const float *n=out;
            float t1[3]={-n[1],n[0],0},l1=std::sqrt(t1[0]*t1[0]+t1[1]*t1[1]);
            if(l1<0.3f) {t1[0]=hx;t1[1]=hy;t1[2]=0;} else {t1[0]/=l1;t1[1]/=l1;}
            float t2[3]={n[1]*t1[2]-n[2]*t1[1],n[2]*t1[0]-n[0]*t1[2],n[0]*t1[1]-n[1]*t1[0]};
            if(t2[2]<0)for(float &c:t2)c=-c;   // (the second way along the face: the one that goes up)
            auto to=[&](float a,float b,float c,float reach,float radius,float birth) {
                lobe(n[0]*a+t1[0]*b+t2[0]*c,n[1]*a+t1[1]*b+t2[1]*c,n[2]*a+t1[2]*b+t2[2]*c,reach,radius,birth,0.3f);
            };
            to(0.95f,0.3f*side,0.25f,1.2f,0.62f,0);
            to(0.8f,-0.5f*side,-0.05f,0.95f,0.5f,0.2f);
            to(0.25f,1.0f,0.1f,0.95f,0.34f,0.3f);
            to(0.25f,-1.0f,0.05f,0.85f,0.3f,0.5f);
            to(0.5f,0.2f*side,0.9f,0.85f,0.4f,0.6f);
            to(0.55f,-0.3f*side,-0.6f,0.6f,0.28f,0.4f);
        } else {
            // In the open: all round, no two alike.
            static const float sizes[6]={0.62f,0.5f,0.42f,0.36f,0.32f,0.3f},reaches[6]={1.1f,0.95f,1.0f,0.8f,0.9f,0.7f};
            for(int n=0;n<6;++n) {
                float a=turn+(n*2.4f)+(rnd()-0.5f)*0.6f,zc=(rnd()*2-1)*0.7f,flat=std::sqrt(1-zc*zc);
                lobe(flat*std::cos(a),flat*std::sin(a),zc,reaches[n],sizes[n],n*0.15f,0.4f);
            }
        }
        // And what is left of it: one thin wisp of smoke that rises from where it was, from tic eight on.
        lobe(hx*0.2f,hy*0.2f,1,0.35f,0.26f,8,1.3f);
        p.lobes.back().wisp=true;p.lobes.back().heat=0.15f;p.lobes.back().depth=2;
        std::sort(p.lobes.begin(),p.lobes.end(),[](const FireLobe &a,const FireLobe &b){return a.depth<b.depth;});
        return;
    }
    p.size=std::clamp(((int)(p.reach*3.4f)+3)/4*4,96,256);
    p.middle=0.62f;p.hot=hot;p.drawnStep=-1;
    float press=std::clamp(headroom/(p.reach*1.6f),0.35f,1.0f);
    float main=rnd()*2*doomPi;   // most of it goes this way
    int count=20+(int)((scale-1)*20);   // many small ones: a few big round ones read as a cartoon's cloud (the user)
    p.lobes.clear();
    for(int n=0;n<count;++n) {
        FireLobe l;
        bool core=n<3;
        float a=core||rnd()<0.4f?rnd()*2*doomPi:main+(rnd()+rnd()-1)*1.3f;
        l.dx=std::cos(a);l.dy=std::sin(a);
        if(onFloor&&!core)l.dy=std::abs(l.dy)*0.9f+0.1f;
        l.dy*=press;l.dx*=1+(1-press)*0.5f;
        l.reach=core?rnd()*0.22f:0.45f+0.55f*rnd();
        l.radius=p.reach*(core?0.5f+0.15f*rnd():0.2f+0.2f*rnd());
        l.birth=core?0:rnd()*hot*0.3f;
        l.heat=core?1.3f:0.95f+0.25f*rnd();
        l.tau=hot*(core?0.5f:0.3f+0.15f*rnd());
        l.rise=(0.5f+0.6f*rnd())*press;
        l.life=30+30*rnd();
        l.depth=core?-1+0.1f*n:rnd();
        l.ox=(int)(rnd()*64);l.oy=(int)(rnd()*64);
        p.lobes.push_back(l);
    }
    std::sort(p.lobes.begin(),p.lobes.end(),[](const FireLobe &a,const FireLobe &b){return a.depth<b.depth;});
}

static void show(int &key,int size,const std::vector<byte> &pixels) {
    static std::vector<byte> flat;   // no normals: the pictures are lit whole
    if(!key)key=fireKeyBase-(++fireSerial&0x00ffffff);
    auto found=images.find(key);
    if(found==images.end()) {
        flat.assign((size_t)size*size*2,0);
        images.emplace(key,upload(size,size,pixels.data(),0,0,nullptr,flat.data()));
        return;
    }
    std::vector<byte> packed((size_t)size*size*4,0);
    for(size_t i=0;i<(size_t)size*size;++i) {packed[4*i]=pixels[2*i];packed[4*i+1]=pixels[2*i+1];}
    gpuUpdateTexture(found->second.texture,GpuFormat::RGBA8,{{0,0,size,size}},packed.data(),size*4);
    found->second.pixels=pixels;
}

// The pictures at age tics after the burst (step: what tells one drawing from the next). False once nothing is left.
bool drawBlastPicture(BlastPicture &p,float age,int step,float acrossX,float acrossY) {
    if(step==p.drawnStep)return p.fire||p.smoke||age<p.hot;
    p.drawnStep=step;
    const int size=p.size;const float *noise=blastNoise();
    static std::vector<float> heat,shade;
    heat.assign((size_t)size*size,-1);shade.assign((size_t)size*size,0);
    float centre=size*0.5f,row=size*p.middle;
    for(const FireLobe &l:p.lobes) {
        float a=age-l.birth;
        if(a<=0)continue;
        float eaten=std::clamp((a-l.tau*1.6f)/l.life,0.0f,1.0f);
        float grown=1-std::exp(-a/2.0f),up=44*(p.reach/44)*(1-std::exp(-a/30));
        float cx=centre+l.dx*l.reach*p.reach*grown,cy=row-l.dy*l.reach*p.reach*grown-l.rise*up;
        float r=l.radius*(0.4f+0.6f*grown)*(1+0.008f*std::min(a,70.0f));
        float t=l.heat*std::exp(-a/l.tau);
        // Cooled, it is thin: holes all through it, not only at its rim.
        float cool=std::clamp((0.45f-t)/0.3f,0.0f,1.0f),body=1.15f-0.3f*cool,rough=0.85f+0.45f*cool,least=0.14f+0.2f*cool+eaten*1.3f;
        float wayX=l.dx,wayY=l.dy;   // across the picture and up it
        if(p.burst&&!l.core) {wayX=l.way[0]*acrossX+l.way[1]*acrossY;wayY=l.way[2];}
        if(p.burst) {
            // Doom's own eighteen tics (8, 6 and 4 a frame there). Fastest just after it goes off; at its peak
            // by tic four and held a tic or two; from tic five the lobes travel on outward, cool and shrink, the
            // knot in the middle burning through first; what is left by tic twelve is dark red shreds that go
            // out by eighteen. It ends by cooling, not by being switched off at its brightest.
            float last=(float)p.hot;
            eaten=l.wisp?std::clamp(a/14,0.0f,1.0f):l.core?std::clamp((a-3.5f)/3.5f,0.0f,1.0f):std::clamp((a-7)/std::max(1.0f,last-7),0.0f,1.0f);
            grown=1-std::exp(-a/1.3f);
            float travel=grown+0.5f*std::clamp((a-5)/9,0.0f,1.0f);
            cx=centre+wayX*l.reach*p.reach*travel;cy=row-wayY*l.reach*p.reach*travel-l.rise*a*(l.wisp?2.2f:1.0f);
            r=l.radius*(0.45f+0.55f*grown)*(1-0.35f*std::clamp((a-6)/10,0.0f,1.0f));
            body=1.15f;rough=1.0f+0.4f*eaten;least=0.14f+eaten*(l.wisp?1.0f:1.3f);   // (ragged from the first: round they were a cartoon's puffs)
            if(l.wisp) {rough=1.3f;least+=0.25f;}   // thin
        }
        if(eaten>=1)continue;
        int drift=(int)(a*0.6f);
        int x0=std::max(0,(int)(cx-r)),x1=std::min(size-1,(int)(cx+r)),y0=std::max(0,(int)(cy-r)),y1=std::min(size-1,(int)(cy+r));
        for(int j=y0;j<=y1;++j)for(int i=x0;i<=x1;++i) {
            float dx=i+0.5f-cx,dy=j+0.5f-cy,q=(dx*dx+dy*dy)/(r*r);
            if(q>=1)continue;
            float h=std::sqrt(1-q),n=noise[((j+l.oy+drift)&63)*64+((i+l.ox)&63)];
            if(h*body+(n-0.5f)*rough<least)continue;   // its ragged rim, and the holes eaten into it
            // The burst opens from its middle: clear there by tic six, so what it threw is seen through it.
            if(p.burst&&std::hypot(i+0.5f-centre,j+0.5f-row)<p.reach*1.1f*std::clamp((age-4.5f)/4,0.0f,1.0f)*(0.7f+0.6f*n)&&!l.wisp)continue;
            // Grain: each texel a little hotter or cooler than its neighbours, so the colours break up into one
            // another texel by texel as painted fire does, not in smooth bands.
            float grain=hashUnit(i,j,(uint32_t)step*0x9e3779b9u^(uint32_t)l.ox)-0.5f;
            float local=t*(0.45f+0.55f*h)+((n-0.5f)*0.45f+grain*0.2f)*std::min(1.0f,t*2);
            size_t at=(size_t)j*size+i;
            if(local>=smokeUnder)heat[at]=std::max(heat[at],local);
            else if(p.burst&&!l.wisp)continue;   // (no smoke but its one wisp)
            else if(heat[at]<smokeUnder||n>0.55f) {   // smoke over fire: the fire shows through its thin places
                heat[at]=0;shade[at]=std::clamp(0.2f+h*0.6f+(n-0.5f)*0.7f+grain*0.3f-eaten*0.3f,0.0f,0.999f);
            }
        }
    }
    static std::vector<byte> fire,smoke;
    fire.assign((size_t)size*size*2,0);smoke.assign((size_t)size*size*2,0);
    const byte *hotRamp=blastFireRamp(),*coolRamp=smokeRamp();
    static const float bands[5]={0.85f,0.68f,0.52f,0.40f,0.30f};
    p.fire=p.smoke=false;
    for(size_t at=0;at<(size_t)size*size;++at) {
        float t=heat[at];
        if(t<0)continue;
        if(t>=smokeUnder) {
            int band=0;while(band<5&&t<bands[band])++band;
            fire[2*at]=hotRamp[band];fire[2*at+1]=255;p.fire=true;
        } else {smoke[2*at]=coolRamp[(int)(shade[at]*4)];smoke[2*at+1]=255;p.smoke=true;}
    }
    show(p.fireKey,size,fire);show(p.smokeKey,size,smoke);
    return p.fire||p.smoke||age<p.hot;   // (nothing yet on its first tic: not burnt out)
}
void dropBlastPicture(BlastPicture &p) {
    for(int *key:{&p.fireKey,&p.smokeKey}) {
        if(!*key)continue;
        images.erase(*key);
        for(auto *group:{&batches,&mirrorBatches,&shadowBatches,&decalBatches})group->erase(*key);
        *key=0;
    }
}
} // namespace doom3d
