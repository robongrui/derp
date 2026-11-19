/* Sky depth (settings.skyLayers): the painted sky split into what lies at
   different distances, each ridge cut into towers round the map that keep
   its painted outline and gain depth, lit by the sun, with valley mist and
   clouds drifting between them (sky/depth.glsl traces them and projects the
   painting onto them).

   Read from the sky texture as it changes (tests/sky_layers.py is the same
   analysis with a contact sheet): a skyline cut per column between the sky's
   and the ridges' per-row colours, then inside the ridges the painter's far
   ridge, the band closer to the sky colour. Each cut is kept only where it
   sits on a hard edge, so a sky of clouds alone stays one layer. What the
   ridges hide is filled from the sky's own columns that reach further down,
   copied as whole strips so the fill keeps the painting's grain.

   The texture (RGBA8, width x height+skyLayerRows): R the sky with its hidden part
   filled, G the far ridge, B the near ridge (palette indices), A which
   ridges cover the texel (bit 0 far, bit 1 near). Then a row of the top row
   each ridge covers per column (R near, G far; 255 none), a row whose first
   texel holds the mist's level and ramp in rows, its palette index, and 1
   when the layers are on (a 1x1 zero texture when not), then a row of the
   near towers per column (crestSlices), one of the far, two of the far
   slices' skirts and two of the near ones' (crestSkirts), then how high
   the rock reaches (sky_rock_limit.cpp). The relief
   normals read from the painted shading (RG near, BA far; delightLayer) go
   to sky_light.cpp, which bakes the towers' light from them. */
#include "sky_profiles.h"
#include "sky_kind.h"

namespace doom3d {
float skyRowHorizon=0.70f,skyRowScale=0.68f,skyShrink=0.9f;
int skyPaintRows=0;
uint32_t skyHash(uint32_t seed,int i) {
    uint32_t h=seed*0x9E3779B9u^(uint32_t)i*0x85EBCA6Bu;
    h^=h>>13;h*=0xC2B2AE35u;h^=h>>16;return h;
}
namespace {
using Rgb=SkyRgb;
using Sky=SkyImage;
float dist(const Rgb &a,const Rgb &b) {return skyDist(a,b);}
struct RowModel { float mean[3],spread[3]; };

// Value noise, periodic across the sky's width.
uint32_t hash(uint32_t seed,int i) {return skyHash(seed,i);}
float noise1(float x,int period,uint32_t seed) {
    int i=(int)std::floor(x);float f=x-i;f=f*f*(3-2*f);
    auto at=[&](int k){return (hash(seed,((k%period)+period)%period)&0xFFFFFF)/16777216.0f;};
    return at(i)+(at(i+1)-at(i))*f;
}

// Mean and spread per row of the texels inside(x, y), widened over
// neighbouring rows until at least 12 texels speak.
template<class Inside> std::vector<RowModel> rowModels(const Sky &s,Inside inside) {
    std::vector<std::array<double,7>> sums(s.h,std::array<double,7>{});
    for(int y=0;y<s.h;++y)for(int x=0;x<s.w;++x) if(inside(x,y)) {
        const Rgb &c=s.rgb[y*s.w+x];float v[3]={c.r,c.g,c.b};
        sums[y][0]+=1;
        for(int k=0;k<3;++k){sums[y][1+k]+=v[k];sums[y][4+k]+=v[k]*v[k];}
    }
    std::vector<RowModel> out(s.h);
    for(int y=0;y<s.h;++y) {
        std::array<double,7> acc{};
        for(int reach=2;;reach*=2) {
            acc={};
            for(int yy=std::max(0,y-reach);yy<std::min(s.h,y+reach+1);++yy)for(int k=0;k<7;++k)acc[k]+=sums[yy][k];
            if(acc[0]>=12||reach>=s.h)break;
        }
        double n=std::max(acc[0],1.0);
        for(int k=0;k<3;++k) {
            double m=acc[1+k]/n;
            out[y].mean[k]=(float)m;out[y].spread[k]=(float)std::sqrt(std::max(acc[4+k]/n-m*m,0.0));
        }
    }
    return out;
}
float misfit(const Rgb &c,const RowModel &m) {
    float v[3]={c.r,c.g,c.b},sum=0;
    for(int k=0;k<3;++k){float d=(v[k]-m.mean[k])/(m.spread[k]+8);sum+=d*d;}
    return sum;
}

constexpr float jump=0.35f,edgeLike=1.5f;
// Cost of cutting column x at each row 0..h: texels from lo above the cut
// that look like the lower class (q), below it the upper one.
std::vector<float> cutCost(const Sky &s,const std::vector<float> &q,int x,int lo,float edgeWeight=edgeLike) {
    std::vector<float> cost(s.h+1,1e9f);
    float c=0;
    for(int r=lo;r<s.h;++r)c+=1-q[r*s.w+x];
    cost[lo]=c;
    for(int y=lo;y<s.h;++y) {
        float p=q[y*s.w+x];c+=p-(1-p);
        cost[y+1]=c-(y+1<s.h?edgeWeight*s.edge[(y+1)*s.w+x]:0);
    }
    return cost;
}
// One row per column minimising the costs plus `jump` per row of step
// between neighbours (an L1 distance transform, linear per column).
std::vector<int> bestPath(const std::vector<std::vector<float>> &costs) {
    int n=(int)costs[0].size(),w=(int)costs.size();
    std::vector<float> acc=costs[0],best(n);
    std::vector<std::vector<int>> back(w);
    for(int x=1;x<w;++x) {
        std::vector<int> arg(n);
        for(int y=0;y<n;++y){best[y]=acc[y];arg[y]=y;}
        for(int y=1;y<n;++y) if(best[y-1]+jump<best[y]){best[y]=best[y-1]+jump;arg[y]=arg[y-1];}
        for(int y=n-2;y>=0;--y) if(best[y+1]+jump<best[y]){best[y]=best[y+1]+jump;arg[y]=arg[y+1];}
        for(int y=0;y<n;++y)acc[y]=best[y]+costs[x][y];
        back[x]=std::move(arg);
    }
    std::vector<int> path(w);
    path[w-1]=(int)(std::min_element(acc.begin(),acc.end())-acc.begin());
    for(int x=w-1;x>0;--x)path[x-1]=back[x][path[x]];
    return path;
}
// Close dips narrower than five columns: dark crevices in rock are rock,
// not sky seen through a slit (a 1D opening on the line).
std::vector<int> noSlits(const std::vector<int> &line) {
    int w=(int)line.size();std::vector<int> low(w),out(w);
    for(int x=0;x<w;++x){low[x]=line[x];for(int d=-2;d<=2;++d)low[x]=std::min(low[x],line[((x+d)%w+w)%w]);}
    for(int x=0;x<w;++x){out[x]=low[x];for(int d=-2;d<=2;++d)out[x]=std::max(out[x],low[((x+d)%w+w)%w]);}
    return out;
}

// The other way round: fill peaks narrower than five columns (a 1D closing).
std::vector<int> noSpikes(const std::vector<int> &line) {
    int w=(int)line.size();std::vector<int> high(w),out(w);
    for(int x=0;x<w;++x){high[x]=line[x];for(int d=-2;d<=2;++d)high[x]=std::max(high[x],line[((x+d)%w+w)%w]);}
    for(int x=0;x<w;++x){out[x]=high[x];for(int d=-2;d<=2;++d)out[x]=std::min(out[x],high[((x+d)%w+w)%w]);}
    return out;
}

// Sky above, ridges below: two per-row colour models refined over a few
// rounds, one cut per column.
std::vector<int> findSkyline(const Sky &s,std::vector<RowModel> &sky) {
    int band=std::max(2,s.h/8);
    std::vector<int> line;
    std::vector<float> q(s.w*s.h);
    for(int round=0;round<5;++round) {
        std::vector<RowModel> ground;
        if(line.empty()) {
            sky=rowModels(s,[&](int,int y){return y<band;});
            ground=rowModels(s,[&](int,int y){return y>=s.h-band;});
        } else {
            sky=rowModels(s,[&](int x,int y){return y<line[x];});
            ground=rowModels(s,[&](int x,int y){return y>=line[x];});
        }
        for(int y=0;y<s.h;++y)for(int x=0;x<s.w;++x) {
            float ds=misfit(s.rgb[y*s.w+x],sky[y]),dg=misfit(s.rgb[y*s.w+x],ground[y]);
            q[y*s.w+x]=ds/(ds+dg+1e-9f);
        }
        std::vector<std::vector<float>> costs(s.w);
        for(int x=0;x<s.w;++x)costs[x]=cutCost(s,q,x,0);
        line=bestPath(costs);
    }
    return noSlits(line);
}

// How hard a cut is: the colour change across it (two rows each side) over
// the texture's typical change across the same span. A silhouette sits on a
// strong, steady edge; a line through clouds doesn't.
float seamContrast(const Sky &s,const std::vector<int> &lower,const std::vector<int> &cols) {
    auto across=[&](int x,int y,float &out) {
        if(y<2||y>s.h-2)return false;
        Rgb a{},b{};
        for(int j:{1,2}){const Rgb &c=s.rgb[(y-j)*s.w+x];a.r+=c.r/2;a.g+=c.g/2;a.b+=c.b/2;}
        for(int j:{0,1}){const Rgb &c=s.rgb[(y+j)*s.w+x];b.r+=c.r/2;b.g+=c.g/2;b.b+=c.b/2;}
        out=dist(a,b);return true;
    };
    double seam=0,typical=0;int ns=0,nt=0;float v;
    for(int x:cols) if(across(x,lower[x],v)){seam+=v;++ns;}
    for(int x=0;x<s.w;x+=3)for(int y=2;y<s.h-1;y+=3) if(across(x,y,v)){typical+=v;++nt;}
    if(!ns||!nt)return 0;
    return (float)((seam/ns)/(typical/nt+1e-6));
}

// Otsu's threshold over values, and how much of their variance it explains.
float otsu(const std::vector<float> &values,float &eta) {
    constexpr int bins=64;
    float lo=*std::min_element(values.begin(),values.end()),hi=*std::max_element(values.begin(),values.end());
    eta=0;
    if(hi-lo<1e-6f)return lo;
    std::array<int,bins> hist{};
    double mean=0;
    for(float v:values){hist[std::min(bins-1,(int)((v-lo)/(hi-lo)*bins))]++;mean+=v;}
    double n=(double)values.size();mean/=n;
    double var=0;for(float v:values)var+=(v-mean)*(v-mean);var=std::max(var/n,1e-9);
    double best=0,w0=0,s0=0;float thr=lo;
    for(int i=0;i<bins-1;++i) {
        double centre=lo+(i+0.5)*(hi-lo)/bins;
        w0+=hist[i];s0+=hist[i]*centre;
        if(w0==0||w0==n)continue;
        double m0=s0/w0,m1=(mean*n-s0)/(n-w0),between=w0*(n-w0)/n/n*(m0-m1)*(m0-m1);
        if(between>best){best=between;thr=(float)(lo+(i+1)*(hi-lo)/bins);}
    }
    eta=(float)(best/var);
    return thr;
}

// Rock standing in painted cloud of its own colour (Doom's E3: red pillars
// in red cloud): no colour edge tells them apart, but rock has grain and
// cloud is smooth. Each texel's grain (how much the light changes across
// and down it, over seven texels round), Otsu's threshold over it, and the
// cut between the smooth above and the grainy below. Taken only where the
// rock is plainly grainier than the sky over it (2.2 times) and stands in
// most columns: a cloud sky's cut ran along its bottom.
bool grainSkyline(const Sky &s,std::vector<int> &line) {
    const int w=s.w,h=s.h;
    std::vector<float> l(w*h),across(w*h,0),down(w*h,0),grain(w*h);
    for(int i=0;i<w*h;++i)l[i]=0.3f*s.rgb[i].r+0.59f*s.rgb[i].g+0.11f*s.rgb[i].b;
    for(int y=1;y<h-1;++y)for(int x=0;x<w;++x) {
        across[y*w+x]=std::fabs(l[y*w+(x+1)%w]-l[y*w+(x+w-1)%w]);
        down[y*w+x]=std::fabs(l[(y+1)*w+x]-l[(y-1)*w+x]);
    }
    for(int y=0;y<h;++y)for(int x=0;x<w;++x) {
        float sum=0;int n=0;
        for(int dy=-3;dy<=3;++dy) {
            int yy=y+dy;if(yy<0||yy>=h)continue;
            for(int dx=-3;dx<=3;++dx){int i=yy*w+(x+dx+w)%w;sum+=across[i]+down[i];++n;}
        }
        grain[y*w+x]=sum/n;
    }
    float eta,t=otsu(grain,eta);
    std::vector<float> q(w*h);
    for(int i=0;i<w*h;++i)q[i]=std::clamp((grain[i]-0.6f*t)/(0.8f*t),0.0f,1.0f);
    std::vector<std::vector<float>> costs(w);
    for(int x=0;x<w;++x)costs[x]=cutCost(s,q,x,0,0);
    line=noSlits(bestPath(costs));
    double above=0,below=0;int na=0,nb=0,bottom=0,clinging=0;
    for(int x=0;x<w;++x) {
        for(int y=0;y<h;++y) {if(y<line[x]){above+=grain[y*w+x];++na;}else{below+=grain[y*w+x];++nb;}}
        bottom+=line[x]>=h-2;clinging+=line[x]<=2;
    }
    if(!na||!nb)return false;
    return (below/nb)>=2.2*(above/na)&&bottom<w/4&&clinging<w/5;
}

// A red sky (hell's: Doom's second episode) over dark rock: colour alone
// put the black ridge with the dark maroon clouds, and with it the cliffs'
// shadowed crevices, deep slits into the rock. The sky is pure red; rock,
// black or brown, isn't: each texel's purity (how much redder it is than
// its other channels, as a share of its red; near-black counts as rock),
// and the cut between the pure above and the rest below. Only where the
// painting's top is red sky, and taken where the cut parts them plainly.
bool redSkyline(const Sky &s,std::vector<int> &line) {
    const int w=s.w,h=s.h;
    auto purity=[](const Rgb &c){return c.r<24?0.0f:(c.r-std::max(c.g,c.b))/c.r;};
    int red=0,band=std::max(2,h/8);
    for(int i=0;i<w*band;++i)red+=purity(s.rgb[i])>=0.6f;
    if(red<0.6f*w*band)return false;
    std::vector<float> q(w*h);
    for(int i=0;i<w*h;++i){float t=std::clamp((purity(s.rgb[i])-0.45f)/0.3f,0.0f,1.0f);q[i]=1-t*t*(3-2*t);}
    std::vector<std::vector<float>> costs(w);
    for(int x=0;x<w;++x)costs[x]=cutCost(s,q,x,0);
    std::vector<int> cut=noSlits(bestPath(costs));
    double above=0,below=0;int na=0,nb=0,bottom=0,clinging=0;
    for(int x=0;x<w;++x) {
        for(int y=0;y<h;++y) {if(y<cut[x]){above+=purity(s.rgb[y*w+x]);++na;}else{below+=purity(s.rgb[y*w+x]);++nb;}}
        bottom+=cut[x]>=h-2;clinging+=cut[x]<=2;
    }
    if(!na||!nb||above/na<0.65||below/nb>0.45||bottom>=w/4||clinging>=w/5)return false;
    line=cut;
    return true;
}

// Under a red sky, the far ridge as the run of near-black rock from the
// skyline down to where lit rock begins, per column (the near rock's top),
// runs narrower than five columns dropped (a crevice by the crest is no
// ridge) and gaps narrower than eleven closed: a few lighter texels in the
// ridge (by Doom's E2 seam) stood in the near ring as a black obelisk.
std::vector<int> darkFar(const Sky &s,const std::vector<int> &line) {
    std::vector<int> cut(s.w);
    for(int x=0;x<s.w;++x) {
        int y=line[x];
        while(y<s.h){const Rgb &c=s.rgb[y*s.w+x];if(0.299f*c.r+0.587f*c.g+0.114f*c.b>=24)break;++y;}
        cut[x]=y;
    }
    std::vector<int> deep(s.w);
    for(int x=0;x<s.w;++x)deep[x]=cut[x]-line[x];
    deep=noSlits(deep);
    int w=s.w;std::vector<int> high(w);
    for(int x=0;x<w;++x){high[x]=deep[x];for(int d=-5;d<=5;++d)high[x]=std::max(high[x],deep[((x+d)%w+w)%w]);}
    for(int x=0;x<w;++x){deep[x]=high[x];for(int d=-5;d<=5;++d)deep[x]=std::min(deep[x],high[((x+d)%w+w)%w]);}
    for(int x=0;x<s.w;++x)cut[x]=std::min(s.h,line[x]+deep[x]);
    return cut;
}

// Inside the ridges the far ridge sits closer to the sky colour than the
// near one: rank each ground texel along near -> sky and cut the lighter
// band off the top.
std::vector<int> findFar(const Sky &s,const std::vector<int> &line,const std::vector<RowModel> &sky,float &eta,float &visible) {
    int band=std::max(2,s.h/8);
    double near[3]={};int n=0;
    for(int y=s.h-band;y<s.h;++y)for(int x=0;x<s.w;++x) if(y>=line[x]) {
        const Rgb &c=s.rgb[y*s.w+x];near[0]+=c.r;near[1]+=c.g;near[2]+=c.b;++n;
    }
    eta=visible=0;
    if(!n)return line;
    for(double &v:near)v/=n;
    std::vector<float> t(s.w*s.h,0),values;
    for(int y=0;y<s.h;++y) {
        double d[3]={sky[y].mean[0]-near[0],sky[y].mean[1]-near[1],sky[y].mean[2]-near[2]};
        double dd=std::max(d[0]*d[0]+d[1]*d[1]+d[2]*d[2],1.0);
        for(int x=0;x<s.w;++x) if(y>=line[x]) {
            const Rgb &c=s.rgb[y*s.w+x];
            float v=(float)std::clamp(((c.r-near[0])*d[0]+(c.g-near[1])*d[1]+(c.b-near[2])*d[2])/dd,-0.5,1.5);
            t[y*s.w+x]=v;values.push_back(v);
        }
    }
    if(values.size()<16)return line;
    float thr=otsu(values,eta);
    std::vector<float> q(s.w*s.h,0);
    for(int y=0;y<s.h;++y)for(int x=0;x<s.w;++x) if(y>=line[x])
        q[y*s.w+x]=1/(1+std::exp(std::clamp((t[y*s.w+x]-thr)/0.08f,-40.0f,40.0f)));
    std::vector<std::vector<float>> costs(s.w);
    for(int x=0;x<s.w;++x)costs[x]=cutCost(s,q,x,line[x]);
    std::vector<int> cut=bestPath(costs);
    int shown=0;for(int x=0;x<s.w;++x)shown+=cut[x]-line[x]>=2;
    visible=(float)shown/s.w;
    return cut;
}

// A layer (-1 empty): column x is real from top[x] to have[x] and goes on
// to the bottom in strips of donor columns that match the rows above and
// reach further. crest lines donors up on their own top row (rock under a
// ridge line), otherwise on the same row (the sky's gradient). Neighbouring
// columns prefer the next donor column at the same offset, so the copies
// come out as patches rather than vertical streaks. Below where any sky
// reaches (haze >= 0) the sky takes its horizon colour; nobody sees that
// deep but the edge a parallax shift uncovers.
std::vector<int> fill(const Sky &s,const std::vector<int> &top,const std::vector<int> &have,bool crest,int haze,uint32_t seed) {
    const int w=s.w,h=s.h,k=4;
    std::vector<int> out(w*h,-1);
    std::vector<std::pair<int,int>> used(w*h,{-1,-1});
    for(int x=0;x<w;++x)for(int r=top[x];r<have[x];++r)out[r*w+x]=s.index[r*w+x];
    auto score=[&](int x,int r,int d,int dr) {
        float sum=0;int n=0;
        for(int j=1;j<=k;++j) {
            if(r-j<top[x]||dr-j<top[d])break;
            sum+=dist(s.color(out[(r-j)*w+x]),s.color(s.index[(dr-j)*w+d]));++n;
        }
        float v=n?sum/n:120;
        v-=3.0f*std::min({have[d]-dr,h-r,16});
        std::pair<int,int> left=used[r*w+(x+w-1)%w];
        if(left.first==(d+w-1)%w&&left.second==dr)v-=80;
        return v+40.0f*std::min(std::abs(d-x),w-std::abs(d-x))/w;
    };
    uint32_t rng=seed|1;
    for(int x=0;x<w;++x) {
        int r=have[x];
        while(r<h) {
            std::vector<std::pair<int,int>> cands;
            for(int d=0;d<w;++d) if(d!=x) cands.push_back({d,r+(crest?top[d]-top[x]:0)});
            std::pair<int,int> left=used[r*w+(x+w-1)%w];
            if(left.first>=0)cands.push_back({(left.first+1)%w,left.second});
            cands.erase(std::remove_if(cands.begin(),cands.end(),[&](const std::pair<int,int> &c){return c.second<top[c.first]||c.second>=have[c.first];}),cands.end());
            if(cands.empty()&&haze>=0){out[r*w+x]=haze;++r;continue;}
            if(cands.empty()) {
                // Deeper than any donor at this alignment: any offset will do.
                for(int tries=0;tries<48;++tries) {
                    rng=rng*1664525u+1013904223u;int d=(int)((rng>>8)%w);
                    for(int dr=top[d]+k;dr<have[d];dr+=2)cands.push_back({d,dr});
                }
            }
            if(cands.empty()) {
                if(r-1<top[x])break;
                out[r*w+x]=out[(r-1)*w+x];++r;continue;
            }
            std::pair<int,int> best=cands[0];float bestScore=1e30f;
            for(const auto &c:cands){float v=score(x,r,c.first,c.second);if(v<bestScore){bestScore=v;best=c;}}
            int d=best.first,dr=best.second;
            while(r<h&&dr>=top[d]&&dr<have[d]) {out[r*w+x]=s.index[dr*w+d];used[r*w+x]={d,dr};++r;++dr;}
        }
    }
    return out;
}

// The far ridge's hidden rock, grown texel by texel from the rock that
// shows (column top[x] to have[x]): each takes the shown texel whose
// neighbours (two to the left, three above) best match those already
// grown, trying the texels that continue its neighbours' sources first and
// a few dozen others. Copied as whole donor columns (fill) it stood in
// vertical stripes once moving bared it; grown this way the grain runs on
// without seams. Falls back to fill where nothing shows.
std::vector<int> growRock(const Sky &s,const std::vector<int> &top,const std::vector<int> &have,uint32_t seed) {
    const int w=s.w,h=s.h;
    std::vector<std::pair<int,int>> shown;
    for(int x=0;x<w;++x)for(int y=top[x];y<have[x];++y)shown.push_back({x,y});
    if(shown.size()<64)return fill(s,top,have,true,-1,seed);
    std::vector<int> out(w*h,-1);
    std::vector<std::pair<int,int>> from(w*h,{-1,-1});
    for(const auto &t:shown){out[t.second*w+t.first]=s.index[t.second*w+t.first];from[t.second*w+t.first]=t;}
    static const int around[5][2]={{-1,0},{-2,0},{-1,-1},{0,-1},{1,-1}};
    uint32_t rng=seed|1;
    for(int y=0;y<h;++y)for(int x=0;x<w;++x) {
        if(y<top[x]||out[y*w+x]>=0)continue;
        std::vector<std::pair<int,int>> cands;
        for(const auto &d:around) {
            int nx=((x+d[0])%w+w)%w,ny=y+d[1];
            if(ny<0||out[ny*w+nx]<0)continue;
            std::pair<int,int> src=from[ny*w+nx];
            int cx=((src.first-d[0])%w+w)%w,cy=src.second-d[1];
            if(cy>=top[cx]&&cy<have[cx])cands.push_back({cx,cy});
        }
        for(int k=0;k<40;++k){rng=rng*1664525u+1013904223u;cands.push_back(shown[(rng>>8)%shown.size()]);}
        std::pair<int,int> best=cands[0];float bestScore=1e30f;
        for(const auto &c:cands) {
            float score=0;int n=0;
            for(const auto &d:around) {
                int nx=((x+d[0])%w+w)%w,ny=y+d[1],sx=((c.first+d[0])%w+w)%w,sy=c.second+d[1];
                if(ny<0||out[ny*w+nx]<0||sy<top[sx]||sy>=h||s.index.empty())continue;
                int sv=sy<have[sx]?s.index[sy*w+sx]:out[sy*w+sx];
                if(sv<0)continue;
                score+=dist(s.color(out[ny*w+nx]),s.color(sv));++n;
            }
            score=n?score/n:200;
            if(score<bestScore){bestScore=score;best=c;}
        }
        out[y*w+x]=s.index[best.second*w+best.first];
        from[y*w+x]=best;
    }
    return out;
}

// The far ridge line: the skyline where the far band shows, guessed across
// the gaps (and kept behind the near ridge there).
std::vector<int> farTop(const std::vector<int> &line,const std::vector<int> &cut,int h,uint32_t seed) {
    int w=(int)line.size();
    std::vector<char> seen(w);bool any=false;
    for(int x=0;x<w;++x){seen[x]=cut[x]-line[x]>=3;any|=seen[x]!=0;}
    if(!any)return {};
    std::vector<int> f(w);
    for(int x=0;x<w;++x) {
        if(seen[x]){f[x]=line[x];continue;}
        int a=1,b=1;
        while(!seen[((x-a)%w+w)%w])++a;
        while(!seen[(x+b)%w])++b;
        float v=(line[((x-a)%w+w)%w]*b+line[(x+b)%w]*a)/(float)(a+b);
        v+=4*(noise1(x/8.0f,std::max(1,w/8),seed)-0.5f)*std::min(1.0f,std::min(a,b)/4.0f);
        // Well below the near crest: on its ring the far ridge rises into
        // view from off the middle, and a guess as tall as the near ridge
        // stood up there as a wall nobody painted (a quarter of the way down
        // still stood out as a pale block).
        f[x]=std::min(h,std::max((int)std::lround(v),line[x]+std::max(10,(h-line[x])/2)));
    }
    // Falling no faster than two rows a column, both ways round: where the
    // painted far ridge met the guess under it, the step stood as a cliff.
    for(int pass=0;pass<2;++pass)for(int n=0;n<2*w;++n) {
        int x=pass?(2*w-1-n)%w:n%w,from=pass?(x+1)%w:(x+w-1)%w;
        f[x]=std::max(line[x],std::min(f[x],f[from]+2));
    }
    return f;
}

// The painting's speckle in the open sky, smoothed: there it describes
// nothing and reads as a printed picture. Each texel takes the median by
// brightness of its 3x3 neighbours in the same layer, so it stays a palette
// colour, edges stay put and the texels stay whole.
void calm(const Sky &s,std::vector<int> &layer,int passes) {
    const int w=s.w,h=s.h;
    auto bright=[&](int i){Rgb c=s.color(i);return 0.299f*c.r+0.587f*c.g+0.114f*c.b;};
    for(int pass=0;pass<passes;++pass) {
        std::vector<int> out=layer;
        for(int y=0;y<h;++y)for(int x=0;x<w;++x) {
            if(layer[y*w+x]<0)continue;
            std::pair<float,int> near[9];int n=0;
            for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx) {
                int yy=y+dy;if(yy<0||yy>=h)continue;
                int v=layer[yy*w+(x+dx+w)%w];
                if(v>=0)near[n++]={bright(v),v};
            }
            std::nth_element(near,near+n/2,near+n);
            out[y*w+x]=near[n/2].second;
        }
        layer.swap(out);
    }
}

// sky.frag shows the texture's row fraction v at rise (skyRowHorizon-v)/skyRowScale
// (rise: height over horizontal distance). The ridges are built a little smaller
// than painted, squeezed toward the horizon (sky.frag's ridgeShrink).
float riseOfRow(float row,int h) {return (skyRowHorizon-row/h)/skyRowScale;}
#define ridgeShrink skyShrink
float ridgeRise(float row,int h) {return ridgeShrink*riseOfRow(row,h);}

// The towers stand on a flat floor this far under the reference eye, in
// near-ring radii (sky.frag's floorDrop); the row a rise shows at as built.
constexpr float floorDrop=skyFloorDrop;
float rowOfRise(float rise,int h) {return (skyRowHorizon-rise/ridgeShrink*skyRowScale)*h;}

// A crest's peaks that stand at least three rows clear of their saddles,
// each with the saddle after it (its lowest point before the next peak),
// going round; none if it never rises a row or two off the floor.
struct CrestPeaks { std::vector<int> peaks,saddles; };
CrestPeaks crestPeaks(const std::vector<int> &crest,float floorRow) {
    int w=(int)crest.size();
    auto wrap=[&](int x){return ((x%w)+w)%w;};
    auto height=[&](int x){return floorRow-crest[wrap(x)];};   // rows over the floor
    CrestPeaks out;
    std::vector<int> &peaks=out.peaks;
    for(int x=0;x<w;++x) {
        float hx=height(x);
        if(hx<=1||height(x-1)>=hx)continue;   // the first column of a top only
        bool top=true;
        for(int d=1;d<=3&&top;++d)top=height(x+d)<=hx&&height(x-d)<=hx;
        if(!top)continue;
        int end=x;while(end+1<x+w&&height(end+1)==hx)++end;
        peaks.push_back(wrap((x+end)/2));
    }
    if(peaks.empty()) {
        int best=0;for(int x=1;x<w;++x)if(height(x)>height(best))best=x;
        if(height(best)<=1)return out;
        peaks.push_back(best);
    }
    std::sort(peaks.begin(),peaks.end());
    peaks.erase(std::unique(peaks.begin(),peaks.end()),peaks.end());
    // The lowest point going from a to b, and where.
    auto saddle=[&](int a,int b,int &at) {
        int len=wrap(b-a);if(!len)len=w;
        float low=1e9f;at=a;
        for(int i=0;i<=len;++i){float v=height(a+i);if(v<low){low=v;at=wrap(a+i);}}
        return low;
    };
    while(peaks.size()>1) {
        size_t n=peaks.size(),weakest=0;float least=1e9f;
        for(size_t i=0;i<n;++i) {
            int at;
            float clear=height(peaks[i])-std::max(saddle(peaks[(i+n-1)%n],peaks[i],at),saddle(peaks[i],peaks[(i+1)%n],at));
            if(clear<least){least=clear;weakest=i;}
        }
        if(least>=3)break;
        peaks.erase(peaks.begin()+weakest);
    }
    for(size_t i=0;i<peaks.size();++i) {
        int at;saddle(peaks[i],peaks[(i+1)%peaks.size()],at);
        out.saddles.push_back(at);
    }
    return out;
}
float seededDepth(uint32_t seed,int i,int peak,float nearest,float farthest) {
    return nearest+(farthest-nearest)*((hash(seed,i*7+peak)&0xFFFF)/65535.0f);
}
// Each slice's depth, seeded; where one mountain stands in front of another
// (overlaps, sky_overlap.cpp) the front one a good step nearer.
std::vector<float> sliceDepths(const CrestPeaks &cp,uint32_t seed,float nearest,float farthest,const std::vector<SkyOverlap> &overlaps) {
    std::vector<float> depths(cp.peaks.size());
    for(size_t i=0;i<depths.size();++i)depths[i]=seededDepth(seed,(int)i,cp.peaks[i],nearest,farthest);
    for(int pass=0;pass<4;++pass)for(const SkyOverlap &o:overlaps)
        if(depths[o.front]>depths[o.back]-0.12f)depths[o.front]=std::max(0.6f,depths[o.back]-0.12f);
    return depths;
}
byte depthByte(float depth) {return (byte)std::clamp((int)std::lround((depth-0.5f)/1.5f*255),1,255);}

// Yangshuo's karst, which the DOOM sky was painted from, stands as separate
// towers. The near crest is cut at its saddles into slices, one per peak
// (crestPeaks; cuts elsewhere cracked painted rock open as the slices
// parted), and each slice is a tower: its outline from the reference eye is
// the painted crest itself, so the artwork stays as painted; it only gains
// depth. Each stands at its own seeded depth between nearest and farthest
// times the ring's radius (scaled with it, so from the reference eye it
// stands where it was painted) and reaches deepest toward the eye and away
// at its peak, thinning toward its saddles, where moving opens the gaps
// between slices at different depths: where the artist painted valleys.
// Per column one texel of row: where in its slice it lies (-1 left saddle,
// 0 peak, 1 right saddle; *127+128), the slice's half-depth at its peak in
// columns (as wide as a column at its depth), and its depth (A: 0.5+a/255*1.5
// times the ring's radius; 0 none). Returns the tallest top over the
// reference eye, in ring radii.
float crestSlices(const std::vector<int> &crest,int h,float floorRow,const CrestPeaks &cp,const std::vector<float> &depths,byte *row) {
    int w=(int)crest.size();
    auto wrap=[&](int x){return ((x%w)+w)%w;};
    auto height=[&](int x){return floorRow-crest[wrap(x)];};
    float tallest=0;
    for(size_t i=0;i<cp.peaks.size();++i) {
        int from=wrap(cp.saddles[(i+cp.peaks.size()-1)%cp.peaks.size()]+1),peak=cp.peaks[i],to=cp.saddles[i];
        int left=wrap(peak-from),right=wrap(to-peak),half=std::clamp((int)std::lround(0.4f*(left+right+1)),3,40);
        float depth=depths[i];
        for(int x=from,n=0;n<=left+right;x=wrap(x+1),++n) {
            if(height(x)<=1)continue;
            float t=n<left?-(float)(left-n)/std::max(left,1):(float)(n-left)/std::max(right,1);
            byte *p=row+4*x;
            p[0]=(byte)std::lround(t*127+128);p[1]=(byte)half;p[2]=0;p[3]=depthByte(depth);
            tallest=std::max(tallest,ridgeRise((float)crest[x],h)*depth);
        }
    }
    return tallest;
}

// Where a slice ends at its saddle it stood as a sheer wall from anywhere
// but the reference eye, against a neighbour at another depth. So past its
// saddles each slice carries on as a skirt about as wide as its edge stands
// tall, falling away with the round shoulder and flaring feet of
// sky/depth.glsl's towerProfile, at the slice's own depth; sky/depth.glsl
// cuts it off at the painted crest, so from the reference eye it stays
// hidden behind the neighbour and the painting is unchanged. Per column, up
// to two skirts reaching it, tallest first, in two rows: the offset from the
// column to the slice's edge column (+128), the skirt's width in columns,
// the crest row at the edge, and the slice's depth (as crestSlices).
// Where a mountain stands in front of the next (overlaps) its skirt on that
// side is its own painted body instead, down to the floor under the traced
// edge, no falling away: width 0, the row it starts at in that column.
void crestSkirts(const std::vector<int> &crest,float floorRow,const CrestPeaks &cp,const std::vector<float> &depths,const std::vector<SkyOverlap> &overlaps,byte *rows) {
    int w=(int)crest.size();
    auto wrap=[&](int x){return ((x%w)+w)%w;};
    auto height=[&](int x){return floorRow-crest[wrap(x)];};
    struct Skirt { int edge,width,top; float depth; };
    std::vector<std::vector<Skirt>> at(w);
    for(const SkyOverlap &o:overlaps)for(int x=0;x<w;++x)if(o.top[x]>=0)at[x].push_back({x,0,o.top[x],depths[o.front]});
    for(size_t i=0;i<cp.peaks.size();++i) {
        int from=wrap(cp.saddles[(i+cp.peaks.size()-1)%cp.peaks.size()]+1),to=cp.saddles[i];
        float depth=depths[i];
        for(int side=0;side<2;++side) {
            int edge=side?to:from,step=side?1:-1;
            if(height(edge)<=1)continue;
            bool covered=false;
            for(const SkyOverlap &o:overlaps)covered|=o.front==(int)i&&o.side==side;
            if(covered)continue;
            int width=std::clamp((int)std::lround(height(edge)),3,48);
            for(int d=1;d<=width;++d)at[wrap(edge+step*d)].push_back({edge,width,crest[edge],depth});
        }
    }
    for(int x=0;x<w;++x) {
        std::vector<Skirt> &here=at[x];
        std::sort(here.begin(),here.end(),[](const Skirt &a,const Skirt &b){return (a.width==0)!=(b.width==0)?a.width==0:a.top<b.top;});
        for(size_t k=0;k<here.size()&&k<2;++k) {
            const Skirt &sk=here[k];
            int off=((sk.edge-x)%w+w)%w;if(off>w/2)off-=w;
            byte *p=rows+4*((int)k*w+x);
            p[0]=(byte)std::clamp(off+128,0,255);p[1]=(byte)sk.width;p[2]=(byte)sk.top;p[3]=depthByte(sk.depth);
        }
    }
}

// The painting lit from above, as the weapons are painted lit (weapon
// lighting.h): its broad light-to-dark sweep taken mostly back out on each
// texel's own palette ramp, so sky.frag can light the towers by the sun
// instead, and normals from the painted shading read as height (bright
// standing proud) with a rounded rim under the crest. The texture's own
// specks stay: on rock they are its grain, not a highlight. The layer wraps
// round the horizon, so it is lit three wide and the middle kept. The
// layer's texels become the de-lit ones; its normals (x along the columns,
// y down, as unsigned bytes) go to out's channels ch and ch+1.
void delightLayer(const Sky &s,std::vector<int> &layer,std::vector<byte> &out,int ch,bool rim) {
    const int w=s.w,h=s.h,wide=3*w;
    std::vector<byte> pixels(2*wide*h,0);
    for(int y=0;y<h;++y)for(int x=0;x<wide;++x) {
        int v=layer[y*w+x%w];
        if(v>=0){pixels[2*(y*wide+x)]=(byte)v;pixels[2*(y*wide+x)+1]=255;}
    }
    doom_weapon_maps maps=doom_weapon_delight(wide,h,pixels.data(),s.palette,false,rim);
    for(int y=0;y<h;++y)for(int x=0;x<w;++x) {
        int i=y*w+x;size_t m=(size_t)y*wide+w+x;
        if(layer[i]<0)continue;
        layer[i]=maps.pixels[2*m];
        out[4*i+ch]=maps.normals[2*m];out[4*i+ch+1]=maps.normals[2*m+1];
    }
}

GpuTextureRef layersTexture;
SkyPaint paint;   // what sky_light.cpp lights
int bakedSky=-1,bakedSetting=-1;
bool layersOn=false,farOn=false,cityOn=false;   // cityOn: the near ring is a painted city's buildings (sky_city.cpp)
float maxTop[2]={};   // the near and far rings' tallest tower top, over the reference eye in each ring's radii
// Where the painting is seen from: the map's middle at a typical eye
// height out under the sky, and the near ring's radius (taken as the level
// starts).
unsigned referenceLevel=~0u;
float reference[3]={},radius=50000;

// The crest the towers' faces are shaped from: the tallest within four
// columns, softened over five. Shaped from each column's own crest the faces
// stood in planks, every column dropping from its own height at its own
// distance, and caught the light in vertical stripes; sky/depth.glsl cuts the
// smooth faces off at the painted outline instead.
byte shapedCrest(const std::vector<int> &crest,int x) {
    int w=(int)crest.size(),sum=0;
    for(int d=-2;d<=2;++d) {
        int tallest=255;
        for(int e=-4;e<=4;++e)tallest=std::min(tallest,crest[((x+d+e)%w+w)%w]);
        sum+=tallest;
    }
    return (byte)std::min(crest[x],(int)std::lround(sum/5.0));
}
// A ridge along the foot of a sky of clouds (stock, the first rock row per
// column), raised on the far ring: the near ring's rows are the spire's, and
// from out there it stands behind whatever else the sky has.
void farRidge(const Sky &s,const unsigned char *ridge,bool paintedRock,uint32_t seed,std::vector<byte> &texels,std::vector<byte> &normals) {
    std::vector<int> crest(ridge,ridge+s.w);
    Rgb mean{};int n=0;
    for(int x=0;x<s.w;++x)for(int y=std::max(0,crest[x]-3);y<crest[x];++y){const Rgb &c=s.rgb[y*s.w+x];mean.r+=c.r;mean.g+=c.g;mean.b+=c.b;++n;}
    if(n){mean.r/=n;mean.g/=n;mean.b/=n;}
    std::vector<int> sky=fill(s,std::vector<int>(s.w,0),crest,false,s.nearest(mean),seed);
    calm(s,sky,1);
    std::vector<int> far(s.w*s.h,-1);
    for(int i=0;i<s.w*s.h;++i)if(i/s.w>=crest[i%s.w])far[i]=s.index[i];
    {
        std::vector<int> painted=far;
        delightLayer(s,far,normals,2,false);
        if(paintedRock)far=painted;   // the normals stay, for the sun
    }
    for(int i=0;i<s.w*s.h;++i) {
        byte *t=&texels[4*i];
        t[0]=(byte)sky[i];
        if(far[i]>=0){t[1]=(byte)far[i];t[3]|=1;}
    }
    byte *covered=&texels[4*s.w*s.h];
    for(int x=0;x<s.w;++x){covered[4*x+1]=(byte)crest[x];covered[4*x+3]=shapedCrest(crest,x);}
    std::vector<int> farCrest=noSpikes(crest);
    float farFloor=rowOfRise(-floorDrop/2.8f,s.h);
    CrestPeaks peaks=crestPeaks(farCrest,farFloor);
    std::vector<float> depths=sliceDepths(peaks,seed+7,0.9f,1.25f,{});
    maxTop[1]=crestSlices(farCrest,s.h,farFloor,peaks,depths,&texels[4*s.w*(s.h+3)]);
    crestSkirts(farCrest,farFloor,peaks,depths,{},&texels[4*s.w*(s.h+4)]);
    farOn=maxTop[1]>0;
}
// A bake done: its bytes to what lights it, the rock's limits read off them,
// and up to the card.
void publishLayers(int w,int h,std::vector<byte> &texels,std::vector<byte> &normals,bool far) {
    paint.w=w;paint.h=h;paint.texels=std::move(texels);paint.normals=std::move(normals);
    paint.far=far;paint.maxTop[0]=maxTop[0];paint.maxTop[1]=maxTop[1];
    skyRockLimits(paint,cityOn,farOn);
    layersTexture=gpuCreateTexture(GpuFormat::RGBA8,w,h+skyLayerRows,paint.texels.data(),4*w);
    layersOn=true;
}
// A sky of clouds alone (clean: the painting without what stands in it), with
// its painted field and ridge if it has a profile, and its spire if it has
// one: backdrop, cloud, ground and landmark all together.
// Or its trees (sky_trees.cpp: how much tree per texel).
void bakeCloudScene(const Sky &art,const Sky &clean,const SkyCloudProfile *profile,uint32_t seed,const std::vector<int> *left=nullptr,const std::vector<int> *right=nullptr,
                    const std::vector<byte> *treeAlpha=nullptr) {
    std::vector<byte> texels,normals(4*art.w*art.h,128);
    maxTop[0]=maxTop[1]=0;farOn=false;
    bakeCloudSky(clean,texels,profile&&profile->skyRows?profile->skyRows:clean.h);
    if(profile)skyCloudFieldBake(clean,*profile,texels);
    if(left)spireTexels(art,*left,*right,texels);
    if(treeAlpha)treeTexels(art,clean,profile->treeRow,*treeAlpha,texels);
    if(profile&&profile->ridge)farRidge(clean,profile->ridge,profile->paintedRock,seed,texels,normals);
    publishLayers(art.w,art.h,texels,normals,farOn);
}

void bake() {
    layersOn=false;
    skyCloudFieldClear();
    skyRowHorizon=0.70f;skyRowScale=0.68f;skyPaintRows=0;skyShrink=0.9f;
    if(cityOn)referenceLevel=~0u;   // the rings' radius follows what the sky is
    cityOn=false;
    const byte empty[4]={0,0,0,0};
    layersTexture=gpuCreateTexture(GpuFormat::RGBA8,1,1,empty,4);
    farOn=false;
    paint=SkyPaint{0,0,{},{},false,{0,0},paint.serial+1};
    if(!settings.skyLayers||Game_Phobos())return;
    const Image &image=wallImage(skytexture);
    Sky s;s.w=image.width;s.h=image.height;
    if(s.w<16||s.h<16||(int)image.pixels.size()<2*s.w*s.h||s.h>255)return;
    s.palette=(const byte*)W_CacheLumpName((char*)"PLAYPAL",PU_CACHE);
    s.index.resize(s.w*s.h);s.rgb.resize(s.w*s.h);s.edge.assign(s.w*s.h,0);
    for(int i=0;i<s.w*s.h;++i){s.index[i]=image.pixels[2*i+1]?image.pixels[2*i]:0;s.rgb[i]=s.color(s.index[i]);}
    float top=0;
    for(int y=1;y<s.h;++y)for(int x=0;x<s.w;++x){float e=dist(s.rgb[(y-1)*s.w+x],s.rgb[y*s.w+x]);s.edge[y*s.w+x]=e;top=std::max(top,e);}
    for(float &e:s.edge)e/=std::max(top,1.0f);

    uint32_t seed=hash((uint32_t)skytexture,s.w*977+s.h);
    const SkyCloudProfile *profile=skyCloudProfile(s);
    // For tuning: -skyback sets how much of the painting shows behind.
    SkyCloudProfile trial;
    int back=M_CheckParm((char*)"-skyback");
    if(profile&&back&&back+1<myargc){trial=*profile;trial.backdrop=(float)atof(myargv[back+1]);profile=&trial;}
    if(profile){skyPaintRows=profile->skyRows;if(profile->shrink>0)skyShrink=std::lround(255*profile->shrink)/255.0f;}   // as the shader gets it
    if(profile&&profile->horizon>0){skyRowHorizon=profile->horizon;skyRowScale=skyRowHorizon*(0.68f/0.70f);}
    // A painted city is cut along its dark buildings instead (sky_city.cpp).
    {
        std::vector<byte> texels;float tallest=0,farTallest=0;
        if(bakeCity(s,seed,texels,tallest,farTallest)) {
            maxTop[0]=tallest;maxTop[1]=farTallest;farOn=true;
            // Its clouds, from the smoke over its skyline.
            if(profile) {
                SkyCloudProfile field=*profile;
                field.skyRows=s.h*3/8;
                skyCloudFieldBake(s,field,texels);
            }
            std::vector<byte> flat(4*s.w*s.h,128);
            cityOn=true;
            publishLayers(s.w,s.h,texels,flat,false);
            referenceLevel=~0u;
            skyCloudNoiseMake();   // its plumes (sky/city.glsl)
            return;
        }
    }
    // Dead trees in fog: a sky of clouds with the trees taken out, and the
    // trees laid back over it.
    if(profile&&profile->treeRow>0) {
        std::vector<byte> alpha;
        Sky clean;
        if(findTrees(s,profile->treeRow,alpha,clean)) {bakeCloudScene(s,clean,profile,seed,nullptr,nullptr,&alpha);return;}
    }
    std::vector<RowModel> skyModel;
    std::vector<int> line=findSkyline(s,skyModel);
    bool red=redSkyline(s,line);
    if(red)skyModel=rowModels(s,[&](int x,int y){return y<line[x];});
    float eta,visible;
    // Under a red sky the far ridge is the dark one (Doom's second episode:
    // black behind sunlit cliffs), not the one nearer the sky's colour.
    std::vector<int> cut=red?darkFar(s,line):findFar(s,line,skyModel,eta,visible);
    std::vector<int> all(s.w),shown;
    for(int x=0;x<s.w;++x){all[x]=x;if(cut[x]-line[x]>=3)shown.push_back(x);}
    int clinging=0;for(int v:line)clinging+=v<=2;
    // A skyline must sit on a hard edge and not cling to the top rows; a far
    // ridge needs its own hard edge where it shows.
    bool skyOk=seamContrast(s,line,all)>=4&&clinging<0.2f*s.w;
    bool farOk=skyOk&&!shown.empty()&&seamContrast(s,cut,shown)>=3.4f&&(red||(eta>0.5f&&visible>0.1f));
    // Its mountains marked by hand, where no edge finds them: one ring of them.
    if(profile&&profile->crest) {
        line.assign(profile->crest,profile->crest+s.w);
        cut=line;skyOk=true;farOk=false;
    }
    // No colour edge, but grainy rock under smooth cloud: one ring of it.
    if(!skyOk&&grainSkyline(s,line)) {skyOk=true;cut=line;}
    if(!skyOk) {
        // Clouds alone, or something standing in an open sky (Heretic's
        // spire)? A long hard vertical edge, a quarter of the painting tall,
        // is no cloud's: one that is no spire either is left as it is.
        int longest=0;
        for(int x=0;x<s.w;++x)for(int y=0,run=0;y<s.h;++y) {
            run=dist(s.rgb[y*s.w+x],s.rgb[y*s.w+(x+1)%s.w])>90?run+1:0;
            longest=std::max(longest,run);
        }
        // A spire (a hard edge a fifth of the painting tall at least, and
        // something straying from the sky in a narrow run up from the
        // bottom): the clouds round it as a cloud sky's, each of its texels
        // taken out for its row's sky; the spire itself raised on its own.
        std::vector<int> left,right;
        if(longest<s.h/5||!findSpire(s,left,right)) {
            if(longest<s.h/4)bakeCloudScene(s,s,profile,seed);
            return;
        }
        // Behind it the painting runs on: each of its texels takes the one
        // as far the other side of its left edge.
        Sky clean=s;
        for(int y=0;y<s.h;++y)for(int x=left[y];x<=right[y];++x) {
            int to=((x%s.w)+s.w)%s.w,from=(((2*left[y]-1-x)%s.w)+s.w)%s.w;
            clean.rgb[y*s.w+to]=s.rgb[y*s.w+from];clean.index[y*s.w+to]=s.index[y*s.w+from];
        }
        bakeCloudScene(s,clean,profile,seed,&left,&right);
        return;
    }

    std::vector<Rgb> low;
    for(int x=0;x<s.w;++x)for(int y=std::max(0,line[x]-3);y<line[x];++y)low.push_back(s.rgb[y*s.w+x]);
    Rgb mean{};for(const Rgb &c:low){mean.r+=c.r;mean.g+=c.g;mean.b+=c.b;}
    if(!low.empty()){mean.r/=low.size();mean.g/=low.size();mean.b/=low.size();}
    // A far band only where it is thick. The thin light rim the cut leaves
    // along a near crest is the painter's soft edge into the air: as a far
    // crest on its ring it stood up behind the near one as spikes, and as the
    // near tower's top it lay on it as a crown of stripes: the near tower
    // starts where the rock does.
    int thick=std::max(6,s.h/12);
    std::vector<int> rock=farOk?cut:line;
    if(farOk)for(int x=0;x<s.w;++x)if(cut[x]-line[x]<thick)cut[x]=line[x];
    std::vector<int> far;
    std::vector<int> f=farOk?farTop(line,cut,s.h,seed):std::vector<int>{};
    if(!f.empty()) {
        std::vector<int> have(s.w);
        for(int x=0;x<s.w;++x)have[x]=cut[x]-line[x]>=3?cut[x]:f[x];
        far=growRock(s,f,have,seed+1);
    }
    // Nor does it stay in the sky: built smaller than painted and moving with
    // the eye, the towers left its bits of edge floating there. The sky's
    // fill covers it like any hidden part.
    const std::vector<int> &nearTop=rock;
    std::vector<int> sky=fill(s,std::vector<int>(s.w,0),line,false,s.nearest(mean),seed);

    // The mist: the light end of the far band, else the sky just above the
    // ridges. It pools halfway from the far crest down to the deepest gap
    // between near ridges, thickening towards the bottom.
    std::vector<Rgb> pool;
    for(int x=0;x<s.w;++x) {
        int from=far.empty()?std::max(0,line[x]-4):line[x],to=far.empty()?line[x]:std::min(cut[x],s.h);
        for(int y=from;y<to;++y)pool.push_back(s.rgb[y*s.w+x]);
    }
    std::sort(pool.begin(),pool.end(),[](const Rgb &a,const Rgb &b){return a.r+a.g+a.b<b.r+b.g+b.b;});
    Rgb mist{128,128,128};
    if(!pool.empty()) {
        mist={0,0,0};size_t from=pool.size()*7/10,n=pool.size()-from;
        for(size_t i=from;i<pool.size();++i){mist.r+=pool[i].r/n;mist.g+=pool[i].g/n;mist.b+=pool[i].b/n;}
    }
    std::vector<int> crests(line);std::sort(crests.begin(),crests.end());
    int deepest=*std::max_element(nearTop.begin(),nearTop.end());
    float level=(crests[s.w/2]+deepest)/2.0f,ramp=std::max(6.0f,(deepest-level)/2);

    std::vector<int> nearLayer(s.w*s.h,-1);
    for(int i=0;i<s.w*s.h;++i) if(i/s.w>=nearTop[i%s.w])nearLayer[i]=s.index[i];
    // Only the open sky: on the ridges the grain is the rock's texture, and
    // smoothed they turn to blobs.
    calm(s,sky,1);
    std::vector<byte> texels(4*s.w*(s.h+skyLayerRows),0),normals(4*s.w*s.h,128);
    {
        std::vector<int> painted=nearLayer;
        delightLayer(s,nearLayer,normals,0,true);
        if(profile&&profile->paintedRock)nearLayer=painted;   // the normals stay, for the sun
    }
    if(!far.empty())delightLayer(s,far,normals,2,false);   // no rim: each step in its crest turned a column dark
    for(int i=0;i<s.w*s.h;++i) {
        byte *t=&texels[4*i];
        t[0]=(byte)sky[i];
        int fv=far.empty()?-1:far[i];
        if(fv>=0){t[1]=(byte)fv;t[3]|=1;}
        if(nearLayer[i]>=0){t[2]=(byte)nearLayer[i];t[3]|=2;}
    }
    // The top row each layer covers, per column (R near, G far): the towers'
    // painted outline. And the crest the towers' faces are shaped from (B
    // near, A far).
    byte *covered=&texels[4*s.w*s.h];
    for(int x=0;x<s.w;++x) {
        covered[4*x]=(byte)nearTop[x];covered[4*x+1]=(byte)(far.empty()?255:f[x]);
        covered[4*x+2]=shapedCrest(nearTop,x);covered[4*x+3]=far.empty()?255:shapedCrest(f,x);
    }
    byte *p=&texels[4*s.w*(s.h+1)];
    p[0]=(byte)std::clamp((int)std::lround(level),0,255);p[1]=(byte)std::clamp((int)std::lround(ramp),1,255);
    p[2]=(byte)s.nearest(mist);p[3]=1;
    bakeHell(s,line,&p[20],profile?profile->hellKind:0);   // a red sky over them: hell's (sky/hell.glsl)
    // A hanging one's tendrils, each its own (sky_tendrils.cpp).
    if(p[20]==3)bakeTendrils(s,nearTop,texels);
    // A writhing sky's dark shapes, blurred, in the far ridge's channel: its
    // motion follows them (sky/hell.glsl), so its faces move whole.
    if(p[20]==1&&far.empty()) {
        const int w=s.w,h=s.h;
        std::vector<float> dark(w*h),soft(w*h);
        float low=1e9f,high=-1e9f;
        for(int i=0;i<w*h;++i) {
            Rgb c=s.color(sky[i]);
            dark[i]=-(0.299f*c.r+0.587f*c.g+0.114f*c.b);
        }
        for(int pass=0;pass<2;++pass) {
            for(int y=0;y<h;++y)for(int x=0;x<w;++x) {
                float sum=0;
                for(int d=-7;d<=7;++d)sum+=dark[y*w+(x+d+w)%w];
                soft[y*w+x]=sum/15;
            }
            for(int y=0;y<h;++y)for(int x=0;x<w;++x) {
                float sum=0;
                for(int d=-7;d<=7;++d)sum+=soft[std::clamp(y+d,0,h-1)*w+x];
                dark[y*w+x]=sum/15;
            }
        }
        for(float v:dark){low=std::min(low,v);high=std::max(high,v);}
        for(int i=0;i<w*h;++i)texels[4*i+1]=(byte)std::lround(255*(dark[i]-low)/std::max(high-low,1.0f));
    }
    // Its clouds, from the sky with the rock taken out of it.
    if(profile) {
        Sky clear=s;
        for(int i=0;i<s.w*s.h;++i){clear.index[i]=(byte)sky[i];clear.rgb[i]=s.color(sky[i]);}
        // Where its rock fills the painting most of the sky down there is the
        // fill, the same few strips over and over: built smaller than painted,
        // the mountains showed it as a repeating band. With low banks to take
        // over, neither the sky nor its clouds use the rows a quarter of the
        // columns don't reach.
        SkyCloudProfile field=*profile;
        int realRows=crests[s.w*3/4];
        if(field.bank>0)field.skyRows=realRows;
        skyCloudFieldBake(clear,field,texels);
        if(field.bank>0)texels[4*(s.w*(s.h+1)+15)+1]=(byte)realRows;
    }
    // The towers, from crests without peaks narrower than five columns
    // (needles), on the floor as each ring sees it.
    // Near: the mountains standing in front of each other (sky_overlap.cpp)
    // and the shade in the rock as depth (sky_relief.cpp); far: slices.
    std::vector<int> nearCrest=noSpikes(nearTop);
    float nearFloor=rowOfRise(-floorDrop/1.0f,s.h);
    CrestPeaks nearPeaks=crestPeaks(nearCrest,nearFloor);
    std::vector<SkyOverlap> overlaps=skyOverlaps(s,nearCrest,nearPeaks.peaks,nearPeaks.saddles,nearFloor);
    std::vector<float> nearDepths=sliceDepths(nearPeaks,seed,0.85f,1.2f,overlaps);
    maxTop[0]=crestSlices(nearCrest,s.h,nearFloor,nearPeaks,nearDepths,&texels[4*s.w*(s.h+2)]);
    crestSkirts(nearCrest,nearFloor,nearPeaks,nearDepths,overlaps,&texels[4*s.w*(s.h+6)]);   // near ones too: cracks between slices showed the sky
    skyShadowRelief(s,nearLayer,texels);
    if(!far.empty()) {
        std::vector<int> farCrest=noSpikes(f);
        float farFloor=rowOfRise(-floorDrop/2.8f,s.h);
        CrestPeaks farPeaks=crestPeaks(farCrest,farFloor);
        std::vector<float> farDepths=sliceDepths(farPeaks,seed+7,0.9f,1.25f,{});
        maxTop[1]=crestSlices(farCrest,s.h,farFloor,farPeaks,farDepths,&texels[4*s.w*(s.h+3)]);
        crestSkirts(farCrest,farFloor,farPeaks,farDepths,{},&texels[4*s.w*(s.h+4)]);
    } else maxTop[1]=0;
    farOn=maxTop[1]>0;
    publishLayers(s.w,s.h,texels,normals,farOn);
}
} // namespace

std::vector<int> skyFill(const SkyImage &s,const std::vector<int> &top,const std::vector<int> &have,bool crest,int haze,uint32_t seed) {return fill(s,top,have,crest,haze,seed);}

const GpuTextureRef &skyLayersImage() {
    if(!layersTexture||bakedSky!=skytexture||bakedSetting!=settings.skyLayers) {
        bakedSky=skytexture;bakedSetting=settings.skyLayers;bake();
    }
    return layersTexture;
}
const SkyPaint &skyPaint() {skyLayersImage();return paint;}
SkyKind skyKind() {
    const SkyPaint &p=skyPaint();
    SkyKind k;
    if(!settings.skyLayers||p.w<16||p.texels.size()<4*(size_t)p.w*(p.h+2))return k;
    const byte *row=&p.texels[4*(size_t)p.w*(p.h+1)];
    auto at=[&](int i,int channel){return (int)row[4*i+channel];};
    k.valid=true;
    k.cloud=at(1,3)>0;k.city=at(3,0)>0;k.hell=at(5,0);k.spire=at(6,0)>0;k.tree=at(7,0)>0;k.field=at(12,0)>0;
#ifdef DOOM_DEVTOOLS
    // DERP_SKYKIND=1: each sky's kind once, to learn which kinds the games' skies come in.
    static unsigned told=~0u;
    if(getenv("DERP_SKYKIND")&&told!=p.serial) {
        told=p.serial;
        fprintf(stderr,"sky kind: cloud %d city %d hell %d spire %d tree %d field %d\n",k.cloud,k.city,k.hell,k.spire,k.tree,k.field);
    }
#endif
    return k;
}

// How far down the towers snow lies, 0 none to 1 to the floor: set as the
// level loads from the weather and temperature chosen, and only again when
// those settings change. Never from how hard it falls this moment: that laid
// snow on the peaks and took it off again with every gust.
float lyingSnow() {return std::clamp((falling()==WEATHER_SNOW?0.55f:0.0f)+0.35f*climateCold(),0.0f,1.0f);}

void skyLayersUniforms(Uniforms &camera) {
    skyLayersImage();
    skyLightUpdate();
    skyCloudFieldUpdate();
    if(referenceLevel!=r_levelserial) {
        referenceLevel=r_levelserial;
        float lo[2]={1e9f,1e9f},hi[2]={-1e9f,-1e9f};
        for(int i=0;i<numvertexes;++i) {
            float x=units(vertexes[i].x),y=units(vertexes[i].y);
            lo[0]=std::min(lo[0],x);lo[1]=std::min(lo[1],y);hi[0]=std::max(hi[0],x);hi[1]=std::max(hi[1],y);
        }
        std::vector<float> floors;
        for(int i=0;i<numsectors;++i) if(sectors[i].ceilingpic==skyflatnum)floors.push_back(units(sectors[i].floorheight));
        if(floors.empty())for(int i=0;i<numsectors;++i)floors.push_back(units(sectors[i].floorheight));
        std::nth_element(floors.begin(),floors.begin()+floors.size()/2,floors.end());
        reference[0]=numvertexes?(lo[0]+hi[0])/2:0;reference[1]=numvertexes?(lo[1]+hi[1])/2:0;
        reference[2]=(floors.empty()?0:floors[floors.size()/2])+41;
        // The near ring far out, the far one further still: up close (three
        // map widths) walking the yard swung them like a model railway, and
        // parallax that strong told the eye they were small. Everything
        // scales with the distance, so from the middle they look the same.
        float halfDiagonal=numvertexes?std::hypot(hi[0]-lo[0],hi[1]-lo[1])/2:2000;
        // A city stands a few blocks off (walking past must shift it as
        // buildings do; at the map's doorstep it slid and towered like a
        // model): only mountains are far.
        radius=cityOn?std::clamp(4*halfDiagonal,10000.0f,40000.0f):std::clamp(20*halfDiagonal,40000.0f,120000.0f);
    }
    for(int c=0;c<3;++c)camera.skyLayers[c]=reference[c];
    camera.skyLayers[3]=layersOn?1.0f+lyingSnow():0.0f;   // 1 + how far down the towers snow lies
    camera.skyShape[0]=radius;camera.skyShape[1]=1.6f*radius;camera.skyShape[2]=skyFarRing*radius;camera.skyShape[3]=1.9f*radius;
    // The level's steady wind for the clouds, whether or not anything falls
    // (the weather's own wind reaches the shaders only then).
    windHeading(camera.skyWind[0],camera.skyWind[1]);camera.skyWind[2]=windAmount();camera.skyWind[3]=skyRowHorizon;
    // Each map under this sky has its own weather: its own place in the field,
    // and whatever cover and backdrop its mood picks.
    uint32_t place=hash(0x5C10D5u,gameepisode*64+gamemap);
    const game_mood &mood=levelMood();
    camera.skyCloud[0]=(place&0xFFFF)/8192.0f;camera.skyCloud[1]=(place>>16)/8192.0f;
    camera.skyCloud[2]=mood.cloudCover;camera.skyCloud[3]=mood.cloudBackdrop;
    // What the clouds answer to (sky/cloud_state.glsl): the amount chosen, not
    // what's falling this moment, and how dark the outdoors lie with no sun.
    camera.skyState[0]=falling()==WEATHER_NONE?0:std::clamp(climateNow().fallAmount>0?climateNow().fallAmount/4.0f:mood.rainAmount,0.0f,1.0f);
    camera.skyState[1]=mood.sun?0:std::clamp((0.9f-mood.outdoor)/0.35f,0.0f,1.0f);
    skySunUpdate(camera,paint,layersOn&&!cityOn);
    camera.skyState[2]=skySunClear;camera.skyState[3]=sunshower()?1.0f:0.0f;
    camera.skyExtent[0]=maxTop[0];camera.skyExtent[1]=maxTop[1];camera.skyExtent[2]=farOn?1.0f:0.0f;camera.skyExtent[3]=floorDrop;
}

void skyLayersShutdown() {skyCloudFieldClear();skyCloudNoiseShutdown();layersTexture=nullptr;paint=SkyPaint{};bakedSky=bakedSetting=-1;layersOn=farOn=false;referenceLevel=~0u;}
} // namespace doom3d
