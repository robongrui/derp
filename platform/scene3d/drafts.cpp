/* Drafts: the wind through the buildings, and the weather it carries in as
   far as the cold reaches. Render-only, a twin of the idea behind p_air.c
   (the game's air on Phobos), not of its code.
   - Rooms and links: the room map's spaces (rooms.c): a room is all its
     joined sectors (a pedestal is part of the room round it), each opening
     (a door, a window frame, a thin recess) a small space of its own with
     no heating, and what is sheltered under a roof but open to the sky
     (a porch) is outside. Spaces link through open lines between them
     (doorways, open doors, gaps 8 units or more), and to the outside
     through open lines onto the sky or a sheltered space (doors, courtyard
     openings, the rim of a hole in a ceiling) and broken panes; whole
     glass seals. One link per line: two windows onto one courtyard each
     feel their own wind.
   - Pressure: each opening to the outside takes the wind's pressure by how
     it faces it (as buildings feel it: +0.8 facing the wind, -0.6 along it,
     -0.4 away), eased most of the way toward a mild suction where the air
     outside is trapped (trapped_air.cpp). The rooms settle where what flows in flows
     out, through each opening as through an orifice. One signed flow per
     link, read by both its rooms the opposite way round, so the two sides
     of a doorway always agree which way the air goes. One window alone
     lets nothing through; two on opposite sides make a cross-draft.
   - Temperature: rooms are heated to 20 C (by their floor area); out in
     the weather it is as cold as its kind says (WeatherKind outside: -5 C
     in snow, +6 C in rain, warm in sand and ash). A room settles between its
     heating, the air flowing in from where it came from and a slow
     exchange both ways through every opening, to the outside and to the
     rooms beside it, flow or none (a big hole in the ceiling chills a hall
     to about freezing, one broken pane barely; a side room off a cold room
     is cool too, the next one less).
   - Trails: from an opening where the outside air comes in, the jet goes
     straight in as far as its throw, then heads for where the air leaves
     each room (its outflows, by their links' flows), sliding along walls,
     into the next room through the link it reaches, warming toward
     each room's temperature as it goes and dropping what it carries.
     Snow lies along it while the jet is at or below 0 C, slush to +2 C,
     wet after; sand and ash lie as they are, never melting; rain lies wet,
     nearer the window. The trail grows as it
     keeps falling (the kind's pane time) and fades over four minutes once
     it stops. wear.glsl draws them (record kind 9), with rime round a cold
     opening (kind 6); flakes, drops, grains ride the trails (buildDrafts,
     drawn by the falling kind where it draws its own). */
#include "internal.h"
#include "../rooms.h"

namespace doom3d {
namespace {
// An opening: space a, space b (-1: the outside, at pressure outside), the
// sector on a's side, its ends, sill and lintel, area, the unit normal
// pointing into a, and the flow through it, positive into a (units^3 a
// second); the slow exchange both ways the flow leaves aside (cold air
// sinking in, warm air out at the top).
struct Link { int a,b,sector; float ax,ay,bx,by,bottom,top,area,nx,ny,outside,flow,exchange; bool pane=false; int line=-1; };
// A trail: segments of the jet from an opening where outside air comes in,
// each a capsule (a to b, half width), how much it carries there, the
// jet's temperature, what state it lies in (2 snow, 1 slush, 0 wet), and
// the rooms it is in.
struct Segment { float ax,ay,bx,by,width,amount,z,jet; int state,sector,next; };
// traced: the flow it was traced for (0 once it is left lying). A trail is
// traced anew only when its opening's flow changes by a fifth (a door, a
// pane): the old one stays lying, fading, while the new one builds. reach:
// how far in along it what the air carries has crept (units; it creeps as
// fast as the air comes in); linger: 1 while it flows, warming away over
// four minutes once it stops.
struct Trail { int line; std::vector<Segment> segments; float soak=0,frost=0,frostReach=0,cold=0,traced=0; bool flowing=false; int room=-1; float ax=0,ay=0,bx=0,by=0,sill=0,speed=0,reach=0,linger=1; };
std::vector<Link> links;
std::vector<std::vector<int>> roomLinks;
std::vector<float> pressure,temperature,heating;
std::vector<Trail> trails;
unsigned draftSerial=~0u;
int solvedTic=-1;
float outsideNow=NAN;   // the outside temperature the last solve used (NAN: none yet, or nothing falls)
float solvedSpeed=NAN;  // and the wind speed
constexpr float roomWarmth=20,heatPerArea=0.5f,discharge=0.6f,mixing=3;
// How fast what the air carries creeps in at an inflow's speed.
float creepSpeed(float speed) {return std::clamp(speed*0.8f,4.0f,80.0f);}

// The space a sector's air is part of, or -1 outside (the open air: under
// the sky, a porch, or open onto them with no door or glass, rooms.c).
int spaceOf(int s) {
    if(s<0||s>=numsectors||Rooms_Outside(s))return -1;
    room_kind kind=Rooms_Kind(s);
    return kind==ROOM_INDOOR||kind==ROOM_OPENING?Rooms_Of(s):-1;
}
int sectorAt(float x,float y) {
    int cx=(int)std::floor((x-mapOrigin[0])/mapCell),cy=(int)std::floor((y-mapOrigin[1])/mapCell);
    if(cx<0||cy<0||cx>=mapWidth||cy>=mapHeight)return -1;
    int own=seamCells[(size_t)cy*mapWidth+cx].own;
    return own==noSector?-1:own;
}
float outsideTemperature(Weather kind) {return climateTemperature(kind);}   // the player's, or the kind's own
// The wind's pressure on an opening whose normal points in along (nx, ny),
// the trapped air outside easing it to a mild suction: the mean of a fan of
// cells 24 to 128 units out, so two windows side by side on one wall feel
// the same air rather than each its own cell's.
float windPressure(float nx,float ny,float cx,float cy,float hx,float hy) {
    float facing=nx*hx+ny*hy;
    float p=facing>=0?-0.6f+1.4f*facing:-0.6f-0.2f*facing;
    float trapped=0;int seen=0;
    for(float out:{24.0f,64.0f,128.0f})for(float side:{-48.0f,0.0f,48.0f}) {
        int ox=(int)std::floor((cx-nx*out-ny*side-mapOrigin[0])/mapCell),oy=(int)std::floor((cy-ny*out+nx*side-mapOrigin[1])/mapCell);
        if(ox<0||oy<0||ox>=mapWidth||oy>=mapHeight)continue;
        int own=seamCells[(size_t)oy*mapWidth+ox].own;
        if(own==noSector||spaceOf(own)>=0)continue;   // only the air outside, open or under a porch
        trapped+=trappedAir((size_t)oy*mapWidth+ox);++seen;
    }
    trapped=seen?trapped/seen:0;
    return p+(-0.3f-p)*0.7f*trapped;   // even a walled courtyard keeps some of the wind's push
}
// Whether two links are the same opening, as open and as pressed on.
bool sameOpening(const Link &x,const Link &y) {
    return x.a==y.a&&x.b==y.b&&x.sector==y.sector&&x.ax==y.ax&&x.ay==y.ay&&x.bx==y.bx&&x.by==y.by&&x.bottom==y.bottom&&x.top==y.top&&x.outside==y.outside&&x.pane==y.pane;
}
// Whether the openings may have changed since the last look: a sector's
// floor or ceiling moved (a door, a lift), a pane broke, the wind turned.
// Otherwise findLinks would find what it found.
std::vector<std::array<float,2>> heights;
std::vector<char> panesBroken;
float headingSeen[2]={NAN,NAN};
bool openingsMoved(float hx,float hy) {
    bool moved=heights.size()!=(size_t)numsectors||panesBroken.size()!=(size_t)windowPaneCount()||hx!=headingSeen[0]||hy!=headingSeen[1];
    heights.resize(numsectors);panesBroken.resize(windowPaneCount());
    headingSeen[0]=hx;headingSeen[1]=hy;
    for(int s=0;s<numsectors;++s) {
        std::array<float,2> h{floorZ(&sectors[s]),ceilZ(&sectors[s])};
        if(h!=heights[s]) {heights[s]=h;moved=true;}
    }
    for(int i=0;i<(int)panesBroken.size();++i) {
        PaneView p;char broken=windowPane(i,p)&&p.broken;
        if(broken!=panesBroken[i]) {panesBroken[i]=broken;moved=true;}
    }
    return moved;
}
// The openings, found anew when they may have changed: doors and panes.
void findLinks(float hx,float hy) {
    links.clear();
    roomLinks.assign(Rooms_Count(),{});
    std::vector<int> paneOf(numlines,-1);
    for(int i=0;i<windowPaneCount();++i) {PaneView p;if(windowPane(i,p)&&p.line>=0&&p.line<numlines)paneOf[p.line]=i;}
    for(int i=0;i<numlines;++i) {
        const line_t &line=lines[i];
        if(!line.frontsector||!line.backsector||line.frontsector==line.backsector)continue;
        int f=int(line.frontsector-sectors),k=int(line.backsector-sectors);
        int fs=spaceOf(f),ks=spaceOf(k);
        bool fr=fs>=0,kr=ks>=0;
        if((!fr&&!kr)||fs==ks)continue;   // outside both, or inside one room (a pedestal's edge)
        float bottom=std::max(floorZ(line.frontsector),floorZ(line.backsector)),top=std::min(ceilZ(line.frontsector),ceilZ(line.backsector));
        if(top-bottom<8)continue;
        if(paneOf[i]>=0) {PaneView p;if(windowPane(paneOf[i],p)&&!p.broken)continue;}  // Whole glass seals.
        float ax=units(line.v1->x),ay=units(line.v1->y),bx=units(line.v2->x),by=units(line.v2->y);
        float length=std::hypot(bx-ax,by-ay);if(length<4)continue;
        // The front side lies to the right of v1->v2: the normal into the front.
        float nx=(by-ay)/length,ny=-(bx-ax)/length;
        int sector=fr?f:k,a=fr?fs:ks,b=fr&&kr?ks:-1;
        if(sector==k) {nx=-nx;ny=-ny;}
        Link link{a,b,sector,ax,ay,bx,by,bottom,top,length*(top-bottom),nx,ny,0,0,0,paneOf[i]>=0,i};
        link.exchange=discharge*link.area*mixing;
        if(b<0) {
            // Onto an enclosed sky (a hole in a ceiling, a light well) one
            // suction all round: no draft from one side of it to the other.
            const sector_t *out=fr?line.backsector:line.frontsector;
            float enclosed=underSky(int(out-sectors))?Rooms_Enclosure(int(out-sectors)):0;
            link.outside=enclosed>0.5f?-0.3f:windPressure(nx,ny,(ax+bx)/2,(ay+by)/2,hx,hy);
        }
        roomLinks[a].push_back((int)links.size());
        if(b>=0)roomLinks[b].push_back((int)links.size());
        links.push_back(link);
    }
}
// The rooms' pressures so what flows in flows out, each opening an orifice
// (flow as the root of the pressure difference), linearised about the last
// guess and evened out room by room; then the flows. Air spaces first (the
// rooms joined by open links, as p_air.c's regions): one with no way out,
// or whose ways out all stand at one pressure, is still, its rooms at that
// pressure and no flow anywhere in it (one broken window: no draft, however
// many rooms behind it). The rest start at their ways out's mean, so a long
// run of rooms settles instead of leaving a little difference everywhere
// that the root would make a draft of.
void solvePressure(float speed) {
    int count=(int)roomLinks.size();
    pressure.assign(count,0);
    std::vector<int> space(count,-1);
    std::vector<char> still;
    for(int first=0;first<count;++first) {
        if(space[first]>=0||roomLinks[first].empty())continue;
        int id=(int)still.size();
        std::vector<int> members={first};space[first]=id;
        for(size_t k=0;k<members.size();++k)for(int i:roomLinks[members[k]]) {
            const Link &l=links[i];
            for(int r:{l.a,l.b})if(r>=0&&space[r]<0) {space[r]=id;members.push_back(r);}
        }
        float low=INFINITY,high=-INFINITY,sum=0,weight=0;
        for(int r:members)for(int i:roomLinks[r]) {
            const Link &l=links[i];
            if(l.b>=0)continue;
            low=std::min(low,l.outside);high=std::max(high,l.outside);sum+=l.outside*l.area;weight+=l.area;
        }
        float mean=weight>0?sum/weight:0;
        for(int r:members)pressure[r]=mean;
        still.push_back(!(high-low>=0.01f));
    }
    // Each round the linearised network solved outright (conjugate gradients,
    // scaled by each room's total conductance, from the last round's answer):
    // sweeping room by room left a dead end's far rooms (E1M1's east half
    // behind its shut doors) at a stale pressure, and the root turned that
    // into a draft into a room with no way out. Rounds stop once the
    // pressures hold still.
    std::vector<double> conductance(links.size()),p(count),r(count),z(count),d(count),q(count),diagonal(count);
    std::vector<char> solving(count);
    for(int s=0;s<count;++s)solving[s]=!roomLinks[s].empty()&&!still[space[s]];
    auto apply=[&](const std::vector<double> &x,std::vector<double> &out) {   // the network's matrix times x (outside held at 0)
        for(int s=0;s<count;++s) {
            out[s]=0;
            if(!solving[s])continue;
            for(int i:roomLinks[s]) {
                const Link &l=links[i];int o=l.a==s?l.b:l.a;
                out[s]+=conductance[i]*(x[s]-(o>=0&&solving[o]?x[o]:0));
            }
        }
    };
    for(int s=0;s<count;++s)p[s]=pressure[s];
    for(int round=0;round<48;++round) {
        for(size_t i=0;i<links.size();++i) {
            const Link &l=links[i];
            double other=l.b<0?l.outside:p[l.b],dp=std::fabs(other-p[l.a]);
            // The floor only guards the division: a higher one makes a wide
            // opening (a hall's strips of other light) look narrow, and its
            // flow from the root out of step with the flows round it.
            conductance[i]=discharge*l.area*speed/std::sqrt(std::max(dp,1e-6));
        }
        // Right-hand side: what the outside and the held rooms push in.
        std::vector<double> before=p;
        double rr=0,bb=0;
        apply(p,q);
        for(int s=0;s<count;++s) {
            diagonal[s]=1;r[s]=0;
            if(!solving[s])continue;
            double push=0,total=0;
            for(int i:roomLinks[s]) {
                const Link &l=links[i];int o=l.a==s?l.b:l.a;
                total+=conductance[i];
                if(o<0)push+=conductance[i]*l.outside;else if(!solving[o])push+=conductance[i]*p[o];
            }
            diagonal[s]=total>0?total:1;
            r[s]=push-q[s];bb+=push*push;
        }
        for(int s=0;s<count;++s) {z[s]=r[s]/diagonal[s];d[s]=z[s];rr+=r[s]*z[s];}
        for(int k=0;k<count&&rr>1e-20*std::max(bb,1e-30);++k) {
            apply(d,q);
            double dq=0;for(int s=0;s<count;++s)dq+=d[s]*q[s];
            if(dq<=0)break;
            double alpha=rr/dq,next=0;
            for(int s=0;s<count;++s) {p[s]+=alpha*d[s];r[s]-=alpha*q[s];z[s]=r[s]/diagonal[s];next+=r[s]*z[s];}
            for(int s=0;s<count;++s)d[s]=z[s]+next/rr*d[s];
            rr=next;
        }
        // Halfway back toward the last round: the root's linearisation overshoots.
        double moved=0,range=0;
        for(int s=0;s<count;++s) {
            if(!solving[s])continue;
            p[s]=0.5*(p[s]+before[s]);
            moved=std::max(moved,std::fabs(p[s]-before[s]));range=std::max(range,std::fabs(p[s]));
        }
        if(round>4&&moved<=1e-7*std::max(range,1e-3))break;
    }
    for(int s=0;s<count;++s)pressure[s]=(float)p[s];
    for(Link &l:links) {
        float dp=(l.b<0?l.outside:pressure[l.b])-pressure[l.a];
        l.flow=still[space[l.a]]?0:discharge*l.area*speed*(dp<0?-1.0f:1.0f)*std::sqrt(std::fabs(dp));
    }
}
// The rooms' temperatures: heating against the air flowing in, each inflow
// at the temperature of where it came from, and the slow exchange both ways
// through every opening with what lies beyond it.
void solveTemperature(float outside) {
    int count=(int)roomLinks.size();
    temperature.assign(count,roomWarmth);
    for(int sweep=0;sweep<48;++sweep)for(int s=0;s<count;++s) {
        if(roomLinks[s].empty())continue;
        float heat=heating[s]*roomWarmth,weight=heating[s];
        for(int i:roomLinks[s]) {
            const Link &l=links[i];
            heat+=l.exchange*(l.b<0?outside:temperature[l.a==s?l.b:l.a]);weight+=l.exchange;
            float in=l.a==s?l.flow:-l.flow;
            if(in<=0)continue;
            heat+=in*(l.b<0?outside:temperature[l.a==s?l.b:l.a]);weight+=in;
        }
        temperature[s]=weight>0?heat/weight:roomWarmth;
    }
}
// The nearest point of a link's opening to (x, y).
void nearestOn(const Link &l,float x,float y,float &px,float &py) {
    float ex=l.bx-l.ax,ey=l.by-l.ay,t=std::clamp(((x-l.ax)*ex+(y-l.ay)*ey)/std::max(ex*ex+ey*ey,1e-4f),0.0f,1.0f);
    px=l.ax+ex*t;py=l.ay+ey*t;
}
// Which way the air heads at (x, y) in room s: toward where it leaves,
// each outflow by how much goes out there and how near it is, those of
// the rooms opening off it too (a step, a pedestal or a pillar's sector is
// part of the room around it). Not pushed out from the inflows: a big
// sector's dead-end corner draws no air (E1M1's start, in the ring of
// sector round the FLAT14 room).
bool flowAt(int s,float x,float y,float &vx,float &vy) {
    vx=vy=0;
    auto add=[&](int r,int skip) {
        for(int i:roomLinks[r]) {
            const Link &l=links[i];
            if(l.b==skip||l.a==skip)continue;   // the link between the two: counted once, from s
            float out=-(l.a==r?l.flow:-l.flow)/(l.top-l.bottom),px,py;
            if(out<=0)continue;
            nearestOn(l,x,y,px,py);
            float dx=px-x,dy=py-y,d=std::sqrt(dx*dx+dy*dy)+1;
            vx+=out*dx/(d*(d+32));vy+=out*dy/(d*(d+32));
        }
    };
    add(s,-2);
    for(int i:roomLinks[s]) {const Link &l=links[i];int other=l.a==s?l.b:l.a;if(other>=0)add(other,s);}
    float speed=std::hypot(vx,vy);
    if(speed<1e-6f)return false;
    vx/=speed;vy/=speed;return true;
}
// Whether rooms s and t share an open link.
bool linked(int s,int t) {
    for(int i:roomLinks[s]) {const Link &l=links[i];if((l.a==s&&l.b==t)||(l.a==t&&l.b==s))return true;}
    return false;
}
// A jet from an outside inflow, from fraction along of the opening, one of
// jets across it: straight in at first, as far as its throw (growing with
// the opening's size and the speed), then turning with the room's flow and
// along the walls it meets; warming toward each room's temperature over a
// length growing the same way, laying down what it carries: rain wet;
// what settles as it lies, or as slush and then wet where it melts. What is
// part of the air itself (fog, dry ice: airborne) falls out of it slowest
// and rides the jet furthest; of what settles without melting, the light
// (ash) further than the heavy (sand), by its kind's carry.
void traceTrail(const Link &l,float along,int jets,float outside,bool settles,bool melts,bool airborne,float carry,Trail &trail) {
    trail.segments.clear();
    float width=std::hypot(l.bx-l.ax,l.by-l.ay)/jets,speed=l.flow/(discharge*l.area),size=std::sqrt(l.area/jets);
    float warming=size*std::clamp(speed/30,0.5f,4.0f)*1.5f,straight=size*std::clamp(speed/20,0.5f,3.0f);
    float x=l.ax+(l.bx-l.ax)*along+l.nx*4,y=l.ay+(l.by-l.ay)*along+l.ny*4,jet=outside,carried=std::clamp(speed/30,0.4f,1.0f);
    float z=std::min(l.top,floorZ(&sectors[l.sector])+48);
    int s=l.a;   // the space the jet is in
    constexpr float step=8;
    float sx=x,sy=y,travelled=0;
    float dx=l.nx,dy=l.ny;
    // Whether a step from (x, y) along (vx, vy) stays in the building, and
    // the sector and space it lands in.
    int ts=-1;
    auto stepTo=[&](float vx,float vy,int &t) {
        t=sectorAt(x+vx*step,y+vy*step);ts=spaceOf(t);
        return ts>=0&&(ts==s||linked(s,ts));
    };
    for(int n=0;n<96;++n) {
        float vx=0,vy=0;
        if(flowAt(s,x,y,vx,vy)) {
            float turn=0.3f*std::clamp(travelled/straight,0.0f,1.0f);
            dx+=(vx-dx)*turn;dy+=(vy-dy)*turn;
            float length=std::hypot(dx,dy);
            if(length<1e-3f)break;
            dx/=length;dy/=length;
        }
        // Into a wall (or out under the sky): along the wall instead, first
        // the way the room's flow leans, or it ends there.
        int t;
        if(!stepTo(dx,dy,t)) {
            float lean=-dy*vx+dx*vy,first=lean>=0?1.0f:-1.0f;
            bool turned=false;
            for(float side:{first,-first})
                if(!turned&&stepTo(-dy*side,dx*side,t)) {float ax=-dy*side;dy=dx*side;dx=ax;turned=true;}
            if(!turned)break;
        }
        float nx=x+dx*step,ny=y+dy*step;
        float melt=jet>2?0.8f:2.5f;         // drops fall out sooner than flakes
        jet+=(temperature[ts]-jet)*step/warming;
        carried*=std::exp(-step/(warming*(airborne?4.0f:!settles?0.5f:melts?melt:carry)));
        z+=(floorZ(&sectors[t])-z)*step/warming;
        x=nx;y=ny;s=ts;travelled+=step;
        if(n%3==2) {
            int state=!settles?0:!melts||jet<=0?2:jet<=2?1:0;
            float half=std::min(64.0f,width*0.35f+travelled*0.15f);
            int start=sectorAt(sx,sy);
            trail.segments.push_back({sx,sy,x,y,half,carried,z,jet,state,start<0?t:start,t});
            sx=x;sy=y;
        }
        if(carried<0.08f)break;
    }
}
}

// Each tic: the network looked at every eight tics while something falls
// (doors and panes open and shut), a trail traced anew where its opening's
// flow changed, and every trail soaking in or fading. The wind is steady,
// so with the same openings the solve would come out as it did: it is
// kept, and the openings are only found again when a sector moved, a pane
// broke or the wind turned (a door on its way solves again).
// ROOMSDEBUG=1 times each solve.
void watchDrafts() {
    if(gamestate!=GS_LEVEL||seamCells.size()<(size_t)mapWidth*mapHeight||bakeMap.sectors.size()!=(size_t)numsectors)return;
    Weather kind=falling();
    // Nothing falling and nothing left lying from before (Clear): no rooms
    // heated, no air solved, until weather comes.
    if(kind==WEATHER_NONE&&(draftSerial!=r_levelserial||trails.empty()))return;
    if(draftSerial!=r_levelserial) {
        draftSerial=r_levelserial;solvedTic=-1;trails.clear();heights.clear();
        // Each room heated by all its floor; openings not at all, nor a room
        // open to the outside with no door or glass between (a tunnel, a garage).
        Rooms_Build();
        heating.assign(Rooms_Count(),0);
        for(const SeamCell &c:seamCells)if(c.own!=noSector&&Rooms_Kind(c.own)==ROOM_INDOOR&&!Rooms_Mouth(c.own))heating[Rooms_Of(c.own)]+=mapCell*mapCell*heatPerArea;
    }
    const WeatherKind &weather=weatherKind(kind);
    const float tic=weatherTicSeconds();
    if(kind!=WEATHER_NONE&&fallNow>0.02f&&(solvedTic<0||leveltime-solvedTic>=8||leveltime<solvedTic)) {
        // The level's first solve: what blows in by the openings already
        // open has been blowing a while, its trails grown all the way in.
        bool settled=solvedTic<0;
        solvedTic=leveltime;
        uint64_t lookStart=SDL_GetTicksNS();
        float hx,hy;windHeading(hx,hy);
        bakeTrappedAir(hx,hy);
        uint64_t baked=SDL_GetTicksNS();
        float speed=std::max(1.0f,std::hypot(windNow[0],windNow[1])),outside=outsideTemperature(kind);
        bool same=!settled&&speed==solvedSpeed&&outside==outsideNow;
        bool moved=openingsMoved(hx,hy);
        uint64_t looked=SDL_GetTicksNS();
        if(moved||!same) {
            std::vector<Link> before;before.swap(links);
            findLinks(hx,hy);
            same=same&&links.size()==before.size()&&std::equal(links.begin(),links.end(),before.begin(),sameOpening);
            if(same)links.swap(before);
        }
        uint64_t start=SDL_GetTicksNS(),solved=start,warmed=start;
        if(!same) {
            solvePressure(speed);
            solved=SDL_GetTicksNS();
            solveTemperature(outside);
            warmed=SDL_GetTicksNS();
        }
        outsideNow=outside;solvedSpeed=speed;
        for(Trail &t:trails)t.flowing=false;
        for(const Link &l:links) {
            if(l.b>=0||l.flow<=0)continue;
            float speed=l.flow/(discharge*l.area);
            if(speed<6)continue;   // a trickle: no draft to speak of
            // A jet for every 64 units of the opening, fanning out across it.
            int jets=std::clamp((int)std::lround(std::hypot(l.bx-l.ax,l.by-l.ay)/64),1,4);
            for(int j=0;j<jets;++j) {
                int line=int(std::lround(l.ax*7+l.ay*13+l.bx*17+l.by*19))*4+j;
                Trail *trail=nullptr;
                for(Trail &t:trails)if(t.line==line&&t.traced>0)trail=&t;
                if(trail&&std::fabs(l.flow-trail->traced)<=0.2f*trail->traced) {trail->flowing=!trail->segments.empty();continue;}
                // A broken pane was glass when the level started: what it lets in always creeps.
                float frost=trail?trail->frost:0,reach=trail?trail->reach:settled&&!l.pane?INFINITY:0;
                if(trail) {trail->traced=0;trail->frost=0;}   // left lying, fading; the rime and the front go on with the new one
                trails.push_back({});trail=&trails.back();
                trail->line=line;trail->traced=l.flow;trail->frost=frost;trail->reach=reach;trail->soak=settled&&!l.pane?1.0f:0.0f;
                traceTrail(l,(j+0.5f)/jets,jets,outside,weather.settles,weather.melts,!weather.trails,weather.carry,*trail);
                trail->flowing=!trail->segments.empty();
                trail->room=l.sector;trail->ax=l.ax;trail->ay=l.ay;trail->bx=l.bx;trail->by=l.by;trail->sill=l.bottom;trail->speed=speed;
                // Rime round a cold opening, by how cold and how strong the inflow (the first jet carries it).
                trail->cold=j?0:std::clamp(-outside/5,0.0f,1.0f)*std::clamp(speed/60,0.0f,1.0f);
                trail->frostReach=24+40*std::clamp(speed/60,0.0f,1.0f);
            }
        }
        static const bool debug=getenv("ROOMSDEBUG")!=nullptr;
        uint64_t end=SDL_GetTicksNS();
        if(debug)fprintf(stderr,"DRAFTS tic %d: %zu links, %d spaces%s; trapped air %.2f ms, openings %.2f ms, find %.2f ms, pressure %.2f ms, "
                         "temperature %.2f ms, trails %.2f ms\n",leveltime,links.size(),(int)roomLinks.size(),same?" (kept)":"",(baked-lookStart)/1e6,
                         (looked-baked)/1e6,(start-looked)/1e6,(solved-start)/1e6,(warmed-solved)/1e6,(end-warmed)/1e6);
    }
    for(Trail &t:trails) {
        if(t.flowing&&fallNow>0.02f)t.soak=std::min(1.0f,t.soak+fallNow*tic/weather.paneSeconds);
        else t.soak=std::max(0.0f,t.soak-tic/240);
        t.frost=t.flowing?std::min(t.cold,t.frost+tic/20):std::max(0.0f,t.frost-tic/240);
        if(t.flowing) {t.reach+=creepSpeed(t.speed)*creepShare(weather)*tic;t.linger=1;}   // sand far slower
        else t.linger=std::max(0.0f,t.linger-tic/240);
    }
    trails.erase(std::remove_if(trails.begin(),trails.end(),[](const Trail &t){return !t.flowing&&t.soak<=0&&t.frost<=0&&t.linger<=0;}),trails.end());
    // A door swinging leaves a few old trails at once: past 64 the faintest lying ones go.
    while(trails.size()>64) {
        auto faintest=std::min_element(trails.begin(),trails.end(),[](const Trail &a,const Trail &b) {return a.flowing!=b.flowing?!a.flowing:a.soak<b.soak;});
        trails.erase(faintest);
    }
}

// The trails as wear records (wear.glsl kind 9: (9, half widths, amounts,
// state), (a, b)), in each room a segment touches, and rime round cold
// openings (kind 6). Every texel of a room reads all its records, so a
// straight run of segments in one sector goes as one record, tapering from
// its first segment's width and amount to its last's (each packed in eighths
// and 255ths: a + 1024 b, a + 256 b): a trail is a handful of records, not
// one per 24 units.
void draftMarks(std::vector<WearMark> &out) {
    if(draftSerial!=r_levelserial)return;
    const WeatherKind &kind=weatherKind(falling());
    bool lays=kind.trails&&!kind.filmPanes;   // fog and dry ice leave no trail lying, only the rime; sand and ash lie in the floor film
    auto record=[&](int sector,const Segment &first,const Segment &last,float soak,std::vector<WearMark> &to) {
        float widths=std::round(std::min(first.width,127.0f)*8)+1024*std::round(std::min(last.width,127.0f)*8);
        float amounts=std::round(std::clamp(first.amount*soak,0.0f,1.0f)*255)+256*std::round(std::clamp(last.amount*soak,0.0f,1.0f)*255);
        to.push_back({sector,{9,widths,amounts,(float)first.state},{first.ax,first.ay,last.bx,last.by}});
    };
    std::vector<Segment> lying;
    for(const Trail &t:trails) {
        // As far in as its front has crept (a pane shot during play: from
        // the sill on in), thinning out over the last 32 units before it.
        lying.clear();
        float along=0;
        for(const Segment &g:t.segments) {
            if(along>=t.reach)break;
            float l=std::hypot(g.bx-g.ax,g.by-g.ay),end=std::min(along+l,t.reach);
            Segment s=g;
            if(end<along+l&&l>0) {float f=(end-along)/l;s.bx=g.ax+(g.bx-g.ax)*f;s.by=g.ay+(g.by-g.ay)*f;s.next=s.sector;}
            float front=std::clamp((t.reach-end)/32,0.0f,1.0f);
            s.amount*=front*front*(3-2*front);
            lying.push_back(s);
            along+=l;
        }
        if(lays&&t.soak>0.01f)for(size_t i=0;i<lying.size();) {
            const Segment &g=lying[i];
            size_t j=i;
            if(g.next==g.sector) {
                // On along the run while it stays in the sector, the same state,
                // within about 11 degrees of its first segment and 128 units long.
                float dx=g.bx-g.ax,dy=g.by-g.ay,length=std::hypot(dx,dy);
                while(j+1<lying.size()&&length>0) {
                    const Segment &n=lying[j+1];
                    float nx=n.bx-n.ax,ny=n.by-n.ay,nl=std::hypot(nx,ny);
                    if(n.sector!=g.sector||n.next!=g.sector||n.state!=g.state||nl<=0||(dx*nx+dy*ny)/(length*nl)<0.98f||std::hypot(n.bx-g.ax,n.by-g.ay)>128)break;
                    ++j;
                }
            }
            record(g.sector,g,lying[j],t.soak,out);
            if(g.next!=g.sector)record(g.next,g,g,t.soak,out);
            i=j+1;
        }
        // On the opening's room and the room the jet goes on into (a pane's frame, then the room).
        if(t.frost>0.01f&&t.room>=0) {
            out.push_back({t.room,{6,t.frostReach,0,t.frost},{t.ax,t.ay,t.bx,t.by}});
            int next=t.segments.empty()?t.room:t.segments.front().next;
            if(next!=t.room)out.push_back({next,{6,t.frostReach,0,t.frost},{t.ax,t.ay,t.bx,t.by}});
        }
    }
}

// One flake or drop the air carries in (here and blown_in.cpp): the kind's
// own where it draws its own (sand's grains, ash's flakes and embers), else
// a snowflake, or a drop where what it carries lies wet.
void drawCarried(const Uniforms &camera,const WeatherKind &weather,float x,float y,float z,const sector_t &sector,uint32_t seed,float alpha,bool drop) {
    static const std::array<float,3> snowAlbedo={0.84f,0.86f,0.9f},dropAlbedo={0.55f,0.58f,0.62f};
    if(weather.carried) {weather.carried(camera,x,y,z,sector,seed,alpha);return;}
    const auto &albedo=drop?dropAlbedo:snowAlbedo;
    pixelBlock(particleVertices,camera,x,y,z,0.6f,drop?2.0f:1.0f,weatherColor(sector,x,y,z,albedo),false,&albedo,true,nullptr,alpha);
}
// Flakes and drops riding the trails near the eye: in at the opening, along
// the jet, sinking as it goes, stirred by it (air_push.cpp's airStir); where
// it lies wet the flakes are drops; what settles lies a moment where it
// comes down instead of vanishing.
void buildDrafts(const Uniforms &camera) {
    if(draftSerial!=r_levelserial||fallNow<=0.02f)return;
    const WeatherKind &weather=weatherKind(falling());
    if(!weather.trails)return;   // fog draws its own (dry_ice.cpp)
    float now=std::floor(gameSeconds()*TICRATE)/TICRATE;
    for(const Trail &t:trails) {
        if(!t.flowing||t.segments.empty())continue;
        float mx=(t.ax+t.bx)/2,my=(t.ay+t.by)/2;
        if(std::hypot(mx-camera.eye[0],my-camera.eye[1])>768)continue;
        // Timed on the whole trail, drawn no further in than its front has
        // crept: timed on the crept length, every flake's cycle stretched as
        // the front crept on and they slid back out toward the window.
        float full=t.segments.size()*24.0f,length=std::min(full,t.reach);
        if(length<8)continue;
        uint32_t seed=hash32((uint32_t)t.line*2654435761u^0x3c6ef372u);
        bool rolls=weather.lift<INFINITY;   // sand skips along in a crowd (blown_in.cpp's too)
        int count=std::min(rolls?96:48,(int)std::lround(length/(rolls?6.0f:12.0f)*fallNow));
        // In at half again the air's speed through the opening, slowing to
        // a stop at the far end: across the trail in that time (one to
        // eight seconds), so they rush in a gale and drift in a light air.
        float cross=std::clamp(2*full/std::max(t.speed*1.5f,20.0f),1.0f,8.0f);
        for(int n=0;n<count;++n) {
            // Flying for period, then (what settles) lying a moment where it came down.
            float period=cross*(0.8f+0.4f*hashUnit(n,0,seed)),rest=weather.settles?1.2f:0.0f,whole=period+rest;
            float clock=now+hashUnit(n,1,seed)*whole*5;
            int cycle=(int)std::floor(clock/whole);
            float seconds=clock-cycle*whole,age=std::min(1.0f,seconds/period);
            uint32_t own=seed^(uint32_t)cycle*2654435761u;
            // How far along it has come: quick through the opening, slowing.
            float along=full*(1-(1-age)*(1-age))*(0.5f+0.5f*hashUnit(cycle,2,own));
            if(along>length)continue;   // past the front: not that far in yet
            size_t k=std::min(t.segments.size()-1,(size_t)(along/24));
            const Segment &g=t.segments[k];
            if(g.amount<0.1f)continue;
            float f=along/24-k,side=(hashUnit(cycle,3,own)-0.5f)*g.width*1.4f;
            float dx=g.bx-g.ax,dy=g.by-g.ay,len=std::max(std::hypot(dx,dy),1e-3f);
            float x=g.ax+dx*f-dy/len*side,y=g.ay+dy*f+dx/len*side;
            int s=sectorAt(x,y);
            if(spaceOf(s)<0)continue;
            float floor=floorZ(&sectors[s]),top=std::min(ceilZ(&sectors[s])-2,g.z+16*hashUnit(cycle,4,own));
            float z=floor+(top-floor)*(1-age)*(0.6f+0.4f*hashUnit(cycle,5,own));
            float alpha=0.75f*std::min(1.0f,g.amount*2);
            if(seconds>=period) {
                // Lying where it came down, fading (drops are gone: they soak in).
                if(g.state!=0)drawCarried(camera,weather,x,y,floor+0.5f,sectors[s],own,alpha*(1-(seconds-period)/rest),false);
                continue;
            }
            if(z<floor+0.5f)continue;
            // The jet's stir (air_push.cpp): wandering further in, swirling
            // where it turns, at the opening's edges and round its end; and
            // any blast's shove. Never through a wall.
            float turn=0;
            if(k+1<t.segments.size()) {
                const Segment &h=t.segments[k+1];
                turn=std::remainder(std::atan2(h.by-h.ay,h.bx-h.ax)-std::atan2(dy,dx),2*(float)doomPi);
            }
            float push[3],stir[3];
            airShift(s,x,y,z,now,now,push);
            airStir(along,full,g.width,dx/len,dy/len,turn,side/std::max(g.width*0.7f,1.0f),now,own,stir);
            for(int i=0;i<3;++i)push[i]+=stir[i]*std::min(1.0f,age*4);   // from nothing at the opening
            int room=spaceOf(s);
            airKeep(x,y,push,[&](float px,float py){int t2=sectorAt(px,py);return spaceOf(t2)>=0&&(spaceOf(t2)==room||linked(room,spaceOf(t2)));});
            x+=push[0];y+=push[1];
            int at=sectorAt(x,y);
            if(at<0)continue;
            z=std::clamp(z+push[2],floorZ(&sectors[at])+0.5f,ceilZ(&sectors[at])-2);
            drawCarried(camera,weather,x,y,z,sectors[at],own,alpha,g.state==0);
        }
    }
}

// The air's temperature at (x, y): out under the sky the outside's, in a
// room the room's, colder in a jet's path (toward the jet's own down its
// middle). NAN while nothing falls (no solve to read). room receives the
// sector, -1 outside the map.
float airTemperature(float x,float y,int *room) {
    int s=draftSerial==r_levelserial&&!seamCells.empty()?sectorAt(x,y):-1;
    if(room)*room=s;
    if(s<0||std::isnan(outsideNow)||fallNow<=0.02f||temperature.size()!=(size_t)Rooms_Count())return NAN;
    int space=spaceOf(s);
    if(space<0)return outsideNow;   // under the sky, or a porch
    float t=temperature[space];
    for(const Trail &trail:trails) {
        if(!trail.flowing)continue;
        for(const Segment &g:trail.segments) {
            float ex=g.bx-g.ax,ey=g.by-g.ay,u=std::clamp(((x-g.ax)*ex+(y-g.ay)*ey)/std::max(ex*ex+ey*ey,1e-4f),0.0f,1.0f);
            float d=std::hypot(g.ax+ex*u-x,g.ay+ey*u-y);
            if(d<g.width)t=std::min(t,temperature[space]+(g.jet-temperature[space])*(1-d/g.width));
        }
    }
    return t;
}

// A room's share of the outside air: how far the drafts take it from its
// own warmth toward the outside's (the solve is linear in the outside's
// temperature, so what is in the air fills the rooms as the cold, or the
// heat of sand and ash, does). NAN before a solve, or with the outside as
// warm as the rooms (nothing to tell the air apart by).
float draftInflow(int line) {
    if(draftSerial!=r_levelserial||std::isnan(outsideNow))return NAN;
    for(const Link &l:links)if(l.line==line&&l.b<0)return std::max(0.0f,l.flow/(discharge*l.area));
    return 0;
}
float draftOutsideShare(int sector) {
    if(draftSerial!=r_levelserial||std::isnan(outsideNow)||temperature.size()!=(size_t)Rooms_Count()||std::fabs(outsideNow-roomWarmth)<0.5f)return NAN;
    int space=spaceOf(sector);
    if(space<0)return 1;   // under the sky, or a porch
    return std::clamp((temperature[space]-roomWarmth)/(outsideNow-roomWarmth),0.0f,1.0f);
}
// The fog's share: the same, while the outside is the colder.
float draftFogShare(int sector) {return outsideNow<roomWarmth?draftOutsideShare(sector):NAN;}

// The trails the outside air flows or flowed in along, for what rides them
// (dry_ice.cpp): each from its opening's sill, step by step, as far as it
// has crept, still lying while it lingers.
void draftPaths(std::vector<DraftPath> &out) {
    out.clear();
    if(draftSerial!=r_levelserial)return;
    for(const Trail &t:trails) {
        if(t.segments.empty()||(!t.flowing&&t.linger<=0))continue;
        DraftPath path{t.line,t.sill,t.ax,t.ay,t.bx,t.by,t.reach,t.soak,t.linger,t.speed,creepSpeed(t.speed),{}};
        for(const Segment &g:t.segments)path.steps.push_back({g.ax,g.ay,g.bx,g.by,g.width,g.amount,g.z,g.next,g.sector,g.jet,g.state});
        path.flowing=t.flowing;
        out.push_back(std::move(path));
    }
}
int draftLinkCount() {return draftSerial==r_levelserial?(int)links.size():0;}
bool draftLink(int index,DraftLinkView &out) {
    if(index<0||index>=draftLinkCount())return false;
    const Link &l=links[index];
    out={l.a,l.b,l.ax,l.ay,l.bx,l.by,l.nx,l.ny,l.flow,l.area>0?std::fabs(l.flow)/(discharge*l.area):0.0f,l.outside,l.bottom,l.top,l.sector,l.pane};
    return true;
}
float draftTemperature(int space) {
    if(draftSerial!=r_levelserial||std::isnan(outsideNow)||space<0||space>=(int)temperature.size())return NAN;
    return temperature[space];
}

void draftsShutdown() {links.clear();roomLinks.clear();trails.clear();heights.clear();panesBroken.clear();draftSerial=~0u;solvedTic=-1;outsideNow=solvedSpeed=NAN;}
} // namespace doom3d
