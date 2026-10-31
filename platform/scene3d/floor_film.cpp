/* The floor film: what the weather lays on indoor floors as it comes in,
   cell by cell, so it is seen to move and to stay. One weather lies at a
   time, so one film serves them all, each by its kind's FilmRules: dry
   ice's streams (dry_ice.cpp) and what blows in at the openings that open
   during play, and for sand and ash at the broken panes too (blown_in.cpp).
   - Indoors only, a depth per floor cell rather than per sector: it
     gathers where it falls, creeps out over a level floor with a front and
     pours over the edge where the floor drops (a step, a stair, a ledge),
     each as its kind does: dry ice runs like a heavy liquid, sand keeps its
     shape and only slides off a slope steeper than its own, snow sticks
     where it lands. The room map decides where it may lie (indoors: rooms
     and openings; outside has its own), the cells how it gets there.
     - The grid: the map's cells taken a few at a time (about 20 units). Two
       cells next to each other flow into each other when they lie in one
       sector, or across the open line between their sectors (shut doors,
       whole glass and walls stop it), worked out once a level, the gaps
       read as the doors move.
     - Each tic a cell hands what lies above its neighbour's surface over to
       it: over a level floor slowly, and only from a film thick enough to
       move and a slope steep enough (so it creeps out with a front instead
       of smearing); off an edge taller than the film over it, fast. Only
       the cells where something happens are stepped (fed, moving, or by a
       door or lift that moved); a film at rest sleeps and costs nothing.
     - It lasts by the room's air (the drafts' temperatures): dry ice
       seconds in a heated room, minutes as cold as outside; snow melts in a
       minute or two in a heated room and stays in the cold; sand and ash
       stay. Past its kind's limit (dry ice: knee height, or a basin's rim)
       it goes in eight seconds.
     - Every other tic the cells it lies in (and lay in) go into the floor
       light's spare alpha (bake_atlas.cpp's floorLight layer): dry ice
       wets nothing, so its depth is all there is (world.frag and
       fog_march.glsl raise its top over the floor there); the rest lies as
       the ground's bake lays it down (wear.glsl), the deeper of the two.
   Render-only. */
#include "internal.h"
#include "../rooms.h"

namespace doom3d {
namespace {
constexpr float cellUnits=20;   // a film cell, about
constexpr float fallOut=0.25f;  // dry ice: the share of what a stream brings that lies down (as dry_ice_basins.cpp times its front)
int step=1,width=0,height=0;
float cellArea=1;
FilmRules rules{};
std::vector<int> owner;         // per film cell: its sector, -1 not an indoor floor
std::vector<float> film,feed,limit,loose;   // loose: how readily the wind moves it (filmDrift)
const std::vector<char> *sealedNow=nullptr;   // the last step's shut lines, for filmDrift
std::vector<uint8_t> shown;     // per film cell, the alpha step it last went up showing (0: none)
std::vector<int> toX,toY;       // per film cell, to the next one along x and y: -2 shut, -1 one sector, else the line between
// Only cells where something happens are stepped: those fed, those that
// moved last tic and their neighbours. A film at rest sleeps until it is
// fed, a door or lift by it moves, or it wastes away; what lies (or still
// shows) is kept in a list for wasting away and the upload.
enum : uint8_t {queued=1,stepping=2,kept=4};
std::vector<uint8_t> state;     // per film cell, the lists it is in
std::vector<int> awake,current,lying,fed;   // to step next tic; this tic; lying or showing; fed this tic
struct Gate {int a,b,link;bool open;float drop;};   // a pair of cells across an open line: doors and lifts wake it
std::vector<Gate> gates;
std::vector<float> out,moves;   // kept between tics
std::vector<int> pairs;
std::vector<std::pair<int,float>> drops;
std::vector<float> keeps;       // per sector, the share of the film a wasting step leaves
int wasting=0;                  // tics since the film last wasted away
int box[4]={0,0,-1,-1};         // the map cells written last time
constexpr int tileCells=32;     // the upload's tiles, in map cells: only those a changed cell touches
std::vector<char> dirty;
std::vector<std::array<int,4>> rects;
unsigned built=~0u;
const FilmRules *builtFor=nullptr;   // the kind's rules it was built for
int uploaded=-1;
// ROOMSDEBUG=1: what the film costs, summed and logged every ten seconds.
const bool debug=getenv("ROOMSDEBUG")!=nullptr;
double spent[3]={0,0,0};   // ms: step, gather, upload
int stepped=0;
struct Timed {
    double &into;uint64_t start;
    explicit Timed(int what):into(spent[what]),start(debug?SDL_GetTicksNS():0) {}
    ~Timed() {if(debug)into+=(SDL_GetTicksNS()-start)/1e6;}
};
int cellAt(float x,float y) {
    int cx=(int)std::floor((x-mapOrigin[0])/mapCell)/step,cy=(int)std::floor((y-mapOrigin[1])/mapCell)/step;
    return cx<0||cy<0||cx>=width||cy>=height?-1:cy*width+cx;
}
void centre(int c,float &x,float &y) {x=mapOrigin[0]+((c%width)*step+step*0.5f)*mapCell;y=mapOrigin[1]+((c/width)*step+step*0.5f)*mapCell;}
void wake(int c) {if(owner[c]>=0&&!(state[c]&queued)) {state[c]|=queued;awake.push_back(c);}}
void keep(int c) {if(!(state[c]&kept)) {state[c]|=kept;lying.push_back(c);}}
void feedCell(int c,float v) {
    if(feed[c]<=0)fed.push_back(c);
    feed[c]+=v;wake(c);
}
// The open line between sectors a and b crossing the segment from p to q, or -2.
int crossing(int a,int b,float px,float py,float qx,float qy) {
    const sector_t &s=sectors[a];
    for(int n=0;n<s.linecount;++n) {
        const line_t *line=s.lines[n];
        if(!line->backsector||!((line->frontsector==&sectors[a]&&line->backsector==&sectors[b])||(line->frontsector==&sectors[b]&&line->backsector==&sectors[a])))continue;
        float ax=units(line->v1->x),ay=units(line->v1->y),bx=units(line->v2->x),by=units(line->v2->y);
        float ex=bx-ax,ey=by-ay,dx=qx-px,dy=qy-py,det=dx*ey-dy*ex;
        if(std::fabs(det)<1e-6f)continue;
        float t=((ax-px)*ey-(ay-py)*ex)/det,u=((ax-px)*dy-(ay-py)*dx)/det;
        if(t>=0&&t<=1&&u>=0&&u<=1)return int(line-lines);
    }
    return -2;
}
// Whether the film may pass from cell a to its neighbour b now.
bool passes(int a,int b,int link,const std::vector<char> &sealed) {
    if(link==-1)return true;
    if(link<0||sealed[link])return false;
    const sector_t &sa=sectors[owner[a]],&sb=sectors[owner[b]];
    return std::min(ceilZ(&sa),ceilZ(&sb))-std::max(floorZ(&sa),floorZ(&sb))>=2;
}
// The map cells x0..x1, y0..y1 into the floor light's alpha: each the film
// of the cells round it in its own sector, blended by how near; dry ice
// full units to the full alpha, what settles or wets as the ground's bake
// encodes it (1 + 254 x how deep), the deeper of it and the bake.
void paint(int x0,int y0,int x1,int y1) {
    for(int fy=y0;fy<=y1;++fy)for(int fx=x0;fx<=x1;++fx) {
        size_t i=(size_t)fy*mapWidth+fx;
        int own=seamCells[i].own;
        float gx=(fx+0.5f)/step-0.5f,gy=(fy+0.5f)/step-0.5f;
        int cx=(int)std::floor(gx),cy=(int)std::floor(gy);
        float tx=gx-cx,ty=gy-cy,sum=0,weight=0;
        for(int j=0;j<2;++j)for(int k=0;k<2;++k) {
            int x=cx+k,y=cy+j;
            if(x<0||y<0||x>=width||y>=height)continue;
            int c=y*width+x;
            if(owner[c]!=own||own<0)continue;
            float w=(k?tx:1-tx)*(j?ty:1-ty)+1e-4f;
            sum+=film[c]*w;weight+=w;
        }
        float depth=std::clamp((weight>0?sum/weight:0)/rules.full,0.0f,1.0f);
        uint8_t &alpha=flatCell(floorLight,i)[3];
        if(!rules.settles) {alpha=(uint8_t)std::lround(depth*255);continue;}
        uint8_t baked=dentedAlpha(i,groundBakeAt(i));   // outside its tiles keep their dents (ground_dents.cpp)
        alpha=depth>0.02f?std::max(baked,(uint8_t)(1+std::lround(depth*254))):baked;
    }
}
}

// The grid for a new level, or a new kind lying: each film cell's sector
// (by the map cell in its middle) where that is an indoor floor, how it
// joins the next ones, and how deep it may lie there before it goes fast
// (limitOf, or the kind's limit).
void filmLevel(const std::vector<char> &outside,const FilmRules &kind,float (*limitOf)(int sector)) {
    built=r_levelserial;uploaded=-1;box[0]=box[1]=0;box[2]=box[3]=-1;
    rules=kind;builtFor=&kind;
    step=std::max(1,(int)std::lround(cellUnits/mapCell));
    width=(mapWidth+step-1)/step;height=(mapHeight+step-1)/step;
    cellArea=step*mapCell*step*mapCell;
    size_t count=(size_t)width*height;
    owner.assign(count,-1);film.assign(count,0);feed.assign(count,0);limit.assign(count,rules.limit);shown.assign(count,0);loose.assign(count,rules.loose);sealedNow=nullptr;
    toX.assign(count,-2);toY.assign(count,-2);
    state.assign(count,0);out.assign(count,0);
    awake.clear();current.clear();lying.clear();fed.clear();gates.clear();wasting=0;
    for(size_t c=0;c<count;++c) {
        int fx=std::min(mapWidth-1,(int)(c%width)*step+step/2),fy=std::min(mapHeight-1,(int)(c/width)*step+step/2);
        int s=seamCells[(size_t)fy*mapWidth+fx].own;
        if(s==noSector||outside[s]||sectors[s].floorpic==skyflatnum)continue;
        owner[c]=s;
        if(limitOf)limit[c]=limitOf(s);
    }
    for(int c=0;c<(int)count;++c) {
        if(owner[c]<0)continue;
        float px,py,qx,qy;centre(c,px,py);
        for(int axis=0;axis<2;++axis) {
            int n=axis?c+width:c+1;
            if(axis?c/width+1>=height:c%width+1>=width)continue;
            if(owner[n]<0)continue;
            centre(n,qx,qy);
            int link=owner[n]==owner[c]?-1:crossing(owner[c],owner[n],px,py,qx,qy);
            (axis?toY:toX)[c]=link;
            if(link>=0)gates.push_back({c,n,link,false,NAN});
        }
    }
}
bool filmReady(const FilmRules &kind) {return built==r_levelserial&&builtFor==&kind;}
// A new tic's fallout: the last one's cleared (the step keeps it as a rate).
void filmClearFallout() {
    for(int c:fed)feed[c]=0;
    fed.clear();
}
// What a dry ice stream lets fall a second (units^3), shared among the
// steps its front has reached by how much it carries there, into the film
// cell under each step's middle.
void dryIceFilmFallout(const DraftPath &path,const DryIceCourse &course,float stream) {
    if(built!=r_levelserial||!path.flowing||path.steps.empty())return;
    const DraftStep &mouth=path.steps.front();
    float brings=fallOut*stream*2.4f*mouth.width*std::max(path.creep,1.0f)*std::min(1.0f,mouth.amount*2);
    drops.clear();
    float weight=0,along=0;
    for(const DraftStep &g:path.steps) {
        float l=std::hypot(g.bx-g.ax,g.by-g.ay),reached=std::clamp(course.front-along,0.0f,l);
        along+=l;
        if(reached<=0)break;
        float f=reached/(2*l);
        int c=cellAt(g.ax+(g.bx-g.ax)*f,g.ay+(g.by-g.ay)*f);
        if(c<0||owner[c]<0||dryIceIsland(owner[c]))continue;
        drops.push_back({c,g.amount*reached});weight+=g.amount*reached;
    }
    if(weight>0)for(const auto &d:drops)feedCell(d.first,brings*d.second/weight);
}
// What blows in lays depth units a second at (x, y), this tic.
void filmFeed(float x,float y,float depth,float looseness) {
    if(built!=r_levelserial)return;
    int c=cellAt(x,y);
    if(c<0||owner[c]<0||depth<=0)return;
    // What it lays mixes with what lies, by how much of each (per tic's worth).
    if(looseness>=0) {float add=depth/TICRATE;loose[c]=(loose[c]*film[c]+looseness*add)/std::max(film[c]+add,1e-6f);}
    feedCell(c,depth*cellArea);
}
// One cell over along the wind: the neighbour along its stronger axis, if
// the film may pass there now (the last step's shut lines).
void filmDrift(float x,float y,float dx,float dy,float rate,float dt) {
    if(built!=r_levelserial||!sealedNow||rate<=0)return;
    int c=cellAt(x,y);
    if(c<0||owner[c]<0||film[c]<=0.01f||loose[c]<=0)return;
    bool alongX=std::fabs(dx)>=std::fabs(dy);
    int n=-1,link=-2;
    if(alongX) {
        if(dx>0&&c%width+1<width) {n=c+1;link=toX[c];}
        else if(dx<0&&c%width>0) {n=c-1;link=toX[c-1];}
    }
    else {
        if(dy>0&&c/width+1<height) {n=c+width;link=toY[c];}
        else if(dy<0&&c>=width) {n=c-width;link=toY[c-width];}
    }
    if(n<0||owner[n]<0||link==-2||!passes(c,n,link,*sealedNow))return;
    float v=std::min(0.5f*film[c],film[c]*loose[c]*rate*dt);
    if(v<=0)return;
    loose[n]=(loose[n]*film[n]+loose[c]*v)/std::max(film[n]+v,1e-6f);
    film[c]-=v;film[n]+=v;
    wake(c);wake(n);keep(n);
}
// What the air itself brings in settles toward a depth per cell (target:
// units deep there, from its sector and middle), over seconds, never taking
// away what lies; at once when the level's weather has been falling a
// while (now).
void filmGather(float (*target)(int sector,float x,float y),float dt,float seconds,bool now) {
    if(built!=r_levelserial)return;
    Timed timed(1);
    for(size_t c=0;c<film.size();++c) {
        if(owner[c]<0)continue;
        float x,y;centre((int)c,x,y);
        float want=target(owner[c],x,y);
        if(want>film[c]) {film[c]+=(want-film[c])*(now?1.0f:std::min(1.0f,dt/seconds));wake((int)c);keep((int)c);}
    }
}
// One step of dt seconds, over the cells awake: what fell gathers; each
// open pair of cells hands over what lies above the lower surface (from the
// higher cell, above its lip) past the kind's slope, slowly on the level and
// fast off an edge, never more than half way to level nor more than a cell
// holds. A cell that gave, took or was fed stays awake, the rest sleep.
// Every eight tics what lies goes by the room's air (cold: 0 heated, 1 as
// cold as outside), eight tics of it at once.
void filmStep(float dt,const std::vector<char> &sealed,float (*cold)(int sector)) {
    if(built!=r_levelserial)return;
    Timed timed(0);
    ++stepped;
    sealedNow=&sealed;
    // A door or lift that moved by what lies wakes the cells either side.
    for(Gate &g:gates) {
        if(film[g.a]<=0&&film[g.b]<=0)continue;
        bool open=passes(g.a,g.b,g.link,sealed);
        float drop=floorZ(&sectors[owner[g.a]])-floorZ(&sectors[owner[g.b]]);
        if(open==g.open&&std::fabs(drop-g.drop)<=0.5f)continue;
        g.open=open;g.drop=drop;
        wake(g.a);wake(g.b);
    }
    for(int c:fed) {film[c]+=feed[c]*dt/cellArea;keep(c);}
    current.swap(awake);awake.clear();
    for(int c:current)state[c]=(state[c]&~queued)|stepping;
    moves.clear();pairs.clear();
    auto consider=[&](int a,int b,int link) {
        if(link==-2||!passes(a,b,link,sealed))return;
        float fa=floorZ(&sectors[owner[a]]),fb=floorZ(&sectors[owner[b]]);
        float ha=fa+film[a],hb=fb+film[b];
        int hi=ha>=hb?a:b,lo=hi==a?b:a;
        float fh=hi==a?fa:fb,fl=hi==a?fb:fa,sh=std::max(ha,hb),sl=std::min(ha,hb);
        float above=sh-std::max(fh,fl),drive=sh-sl;   // what lies over the lip; how far apart the surfaces are
        if(above<=0||drive<=0)return;
        bool edge=fh-fl>film[hi];                     // the floor drops away under it
        if(!edge&&(film[hi]<rules.yield||drive<=rules.slope))return;
        if(!edge)drive-=rules.slope;                  // it rests at its own slope
        float v=std::min({(edge?rules.pour:rules.creep)*drive*dt,0.5f*drive,above});
        if(v<=0)return;
        pairs.push_back(hi);pairs.push_back(lo);moves.push_back(v);out[hi]+=v;
    };
    // Each pair once: from its first cell, or from its second when the first sleeps.
    for(int c:current) {
        if(toX[c]!=-2)consider(c,c+1,toX[c]);
        if(toY[c]!=-2)consider(c,c+width,toY[c]);
        if(c%width>0&&toX[c-1]!=-2&&!(state[c-1]&stepping))consider(c-1,c,toX[c-1]);
        if(c>=width&&toY[c-width]!=-2&&!(state[c-width]&stepping))consider(c-width,c,toY[c-width]);
    }
    for(size_t i=0;i<moves.size();++i) {
        int hi=pairs[2*i],lo=pairs[2*i+1];
        float v=moves[i]*(out[hi]>film[hi]?film[hi]/out[hi]:1.0f);
        loose[lo]=(loose[lo]*film[lo]+loose[hi]*v)/std::max(film[lo]+v,1e-6f);
        film[hi]-=v;film[lo]+=v;
        wake(hi);wake(lo);keep(lo);
    }
    // What is still being laid builds up from nothing, a tic at a time; a
    // trace handed on is gone.
    auto trace=[&](int c) {if(film[c]<0.01f&&feed[c]<=0)film[c]=0;};
    for(size_t i=0;i<moves.size();++i) {out[pairs[2*i]]=0;trace(pairs[2*i]);trace(pairs[2*i+1]);}
    for(int c:current) {state[c]&=~stepping;trace(c);}
    if(++wasting<8)return;
    // How long it lasts in each sector: by the drafts' air there.
    float t=wasting*dt;
    wasting=0;
    keeps.resize(numsectors);
    for(int s=0;s<numsectors;++s)keeps[s]=std::exp(-t/(rules.lasts*std::pow(rules.coldLasts,cold?cold(s):0.0f)));
    for(int c:lying) {
        float &f=film[c],was=f;
        if(f<=0)continue;
        f=f*keeps[owner[c]]-std::max(0.0f,f-limit[c])*std::min(1.0f,t/8);
        if(f<0.01f&&feed[c]<=0)f=0;
        if(f==0||was-f>0.05f)wake(c);   // gone, or going fast: the cells round it may run again
    }
}
// Every other tic, the map cells round the film cells whose look changed
// (the alpha step they show) into the floor light's alpha and up to the
// GPU, a tile at a time (two doorways far apart are two small tiles, not
// the box between them): a film that has settled (ash over a whole wing,
// sand that came to rest) costs nothing. box keeps the bounds of all that
// lies, for filmPaint.
void filmUpload() {
    if(built!=r_levelserial||!flatLightTexture||flatLightCells.size()<(size_t)mapWidth*mapHeight*4)return;
    if(uploaded>=0&&leveltime>=uploaded&&leveltime-uploaded<2)return;
    uploaded=leveltime;
    if(debug&&leveltime%350<2&&stepped) {
        fprintf(stderr,"FILM tic %d: %zu cells, %zu lying, %zu awake or fed, %d steps: step %.3f ms, gather %.3f ms, upload %.3f ms a tic\n",leveltime,film.size(),lying.size(),awake.size()+fed.size(),stepped,
                spent[0]/stepped,spent[1]/stepped,spent[2]/stepped);
        spent[0]=spent[1]=spent[2]=0;stepped=0;
    }
    Timed timed(2);
    int lies[4]={mapWidth,mapHeight,-1,-1};
    int across=(mapWidth+tileCells-1)/tileCells;
    dirty.assign((size_t)across*((mapHeight+tileCells-1)/tileCells),0);
    rects.clear();
    for(size_t i=0;i<lying.size();) {
        int c=lying[i];
        float depth=std::clamp(film[c]/rules.full,0.0f,1.0f);
        uint8_t looks=film[c]<=0?0:(uint8_t)(1+std::lround(depth*254));
        // The map cells a film cell blends into: a step round it.
        int x0=std::max(0,(c%width)*step-step),y0=std::max(0,(c/width)*step-step);
        int x1=std::min(mapWidth-1,(c%width)*step+2*step),y1=std::min(mapHeight-1,(c/width)*step+2*step);
        if(looks) {lies[0]=std::min(lies[0],x0);lies[1]=std::min(lies[1],y0);lies[2]=std::max(lies[2],x1);lies[3]=std::max(lies[3],y1);}
        if(looks!=shown[c]) {
            shown[c]=looks;
            for(int ty=y0/tileCells;ty<=y1/tileCells;++ty)for(int tx=x0/tileCells;tx<=x1/tileCells;++tx) {
                char &d=dirty[(size_t)ty*across+tx];
                if(d)continue;
                d=1;
                int rx=tx*tileCells,ry=ty*tileCells;
                rects.push_back({rx,ry,std::min(tileCells,mapWidth-rx),std::min(tileCells,mapHeight-ry)});
            }
        }
        if(film[c]<=0&&!shown[c]) {state[c]&=~kept;lying[i]=lying.back();lying.pop_back();}   // gone and painted out
        else ++i;
    }
    box[0]=lies[0];box[1]=lies[1];box[2]=lies[2];box[3]=lies[3];
    if(rects.empty())return;
    for(const auto &r:rects)paint(r[0],r[1],r[0]+r[2]-1,r[1]+r[3]-1);
    gpuUpdateTexture(flatLightTexture,GpuFormat::RGBA8,rects,flatLightCells.data(),mapWidth*4);
}
// The film into the floor light's alpha where it lies, without uploading:
// after the ground's wetness was baked over it (weather.cpp), whose caller
// uploads the layer.
void filmPaint() {
    if(built!=r_levelserial||box[2]<0||flatLightCells.size()<(size_t)mapWidth*mapHeight*4)return;
    paint(std::max(0,box[0]),std::max(0,box[1]),std::min(mapWidth-1,box[2]),std::min(mapHeight-1,box[3]));
}
// A blast (disturbances.cpp) blows the film round (x, y) away: out of the
// cells within radius (ragged, a cell at a time) by how near the middle and
// how hard, in the blast's own room only; what settles (snow, sand, ash)
// half of it thrown onto a ragged rim just past it, the rest gone up in
// the puff; dry ice all gone. The cells wake, so the film's own rules mend
// it: dry ice creeps back in, snow and sand stay a crater until more blows
// in. Returns how deep it lay at the middle.
float filmDisturb(float x,float y,float radius,float strength,int sector,uint32_t seed) {
    if(built!=r_levelserial||radius<=0)return 0;
    int middle=cellAt(x,y);
    float was=middle>=0?film[middle]:0;
    int room=Rooms_Of(sector);
    int reach=(int)std::ceil(radius*1.4f/(step*mapCell))+1;
    int cx=(int)std::floor((x-mapOrigin[0])/mapCell)/step,cy=(int)std::floor((y-mapOrigin[1])/mapCell)/step;
    float taken=0;
    std::vector<int> rim;
    for(int gy=cy-reach;gy<=cy+reach;++gy)for(int gx=cx-reach;gx<=cx+reach;++gx) {
        if(gx<0||gy<0||gx>=width||gy>=height)continue;
        int c=gy*width+gx;
        if(owner[c]<0||Rooms_Of(owner[c])!=room)continue;
        float px,py;centre(c,px,py);
        float d=std::hypot(px-x,py-y)/radius*(0.8f+0.4f*hashUnit(gx,gy,seed));
        if(d<1) {
            float take=film[c]*std::min(1.0f,strength*(1.3f-d));
            film[c]-=take;taken+=take;wake(c);keep(c);
        }
        else if(d<1.45f)rim.push_back(c);
    }
    if(rules.settles&&!rim.empty()&&taken>0) {
        float weight=0;
        for(int c:rim)weight+=0.4f+hashUnit(c,7,seed);
        for(int c:rim) {film[c]+=0.5f*taken*(0.4f+hashUnit(c,7,seed))/weight;wake(c);keep(c);}
    }
    return was;
}
void filmMelt(float x,float y,float radius,float share) {
    if(built!=r_levelserial||radius<=0||share<=0)return;
    int reach=(int)std::ceil(radius/(step*mapCell))+1;
    int cx=(int)std::floor((x-mapOrigin[0])/mapCell)/step,cy=(int)std::floor((y-mapOrigin[1])/mapCell)/step;
    for(int gy=cy-reach;gy<=cy+reach;++gy)for(int gx=cx-reach;gx<=cx+reach;++gx) {
        if(gx<0||gy<0||gx>=width||gy>=height)continue;
        int c=gy*width+gx;
        if(owner[c]<0||film[c]<=0)continue;
        float px,py;centre(c,px,py);
        float d=std::hypot(px-x,py-y)/radius;
        if(d>=1)continue;
        film[c]-=film[c]*share*(1-d);
        wake(c);
    }
}
float filmCell() {return step*mapCell;}
// How deep the film lies at (x, y); 0 where none or outside.
float filmAt(float x,float y) {
    if(built!=r_levelserial)return 0;
    int c=cellAt(x,y);
    return c>=0?film[c]:0.0f;
}
// The deepest the film lies in a sector (for the dev tools).
float filmDeepest(int sector) {
    float deepest=0;
    if(built==r_levelserial)for(size_t c=0;c<film.size();++c)if(owner[c]==sector)deepest=std::max(deepest,film[c]);
    return deepest;
}
// For the FPS readout: the cells stepped next tic, and those lying or showing.
int filmAwake() {return built==r_levelserial?(int)awake.size():0;}
int filmLying() {return built==r_levelserial?(int)lying.size():0;}
void filmShutdown() {
    owner.clear();film.clear();feed.clear();limit.clear();loose.clear();shown.clear();toX.clear();toY.clear();sealedNow=nullptr;
    state.clear();out.clear();dirty.clear();rects.clear();awake.clear();current.clear();lying.clear();fed.clear();gates.clear();wasting=0;
    built=~0u;builtFor=nullptr;uploaded=-1;box[2]=box[3]=-1;
}
} // namespace doom3d
