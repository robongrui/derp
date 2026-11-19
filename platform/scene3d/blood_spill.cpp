/* Blood over an edge: a pool that finds a step or a ledge runs down it. No flow is simulated: when a pool is
   laid (an event, once) one pass over its sector's lines looks for a lower floor close by. If there is one the
   pool is made broad enough to reach it, and what follows is laid at once with the times it will show: runs down
   the riser (wall splats with long drips, born when the pool gets there) and a smaller pool at the foot (born
   when they get there). That pool is laid the same way, so stairs cascade by themselves until the blood has
   thinned out. Nothing here runs per frame or per tic, and a pool with no edge near is as it always was.
   Drawing only. */
#include "internal.h"

namespace doom3d {
constexpr float poolRadius=0.4f;      // a pool's reach per unit of its size (its art's edge, blood.cpp)
constexpr float smallestSpill=20;     // a pool thinner than this goes no further
constexpr float thinning=0.8f;        // what each step leaves of the blood coming down it
constexpr float lowestDrop=4;         // less than this is a trim on the floor, not an edge
constexpr float tallestRun=120;       // the drip art's length, near enough: on a higher wall the runs give out
constexpr int edgesAtOnce=3;

namespace {
struct Edge { int line,side;float away,along;WallSpan span; };
}

// Runs down one riser from the stretch of it the pool touches, all starting about when; the time the first
// of them reaches the foot.
static float layRuns(const Edge &edge,float from,float to,float when,int tint) {
    float height=edge.span.top-edge.span.bottom,ex=edge.span.b.x-edge.span.a.x,ey=edge.span.b.y-edge.span.a.y,len=std::hypot(ex,ey);
    int splats=std::clamp(int((to-from)/12)+1,1,4);
    float over=std::clamp(tallestRun/height,1.05f,1.3f);   // the first run's length: a little past the foot
    for(int n=0;n<splats;++n) {
        float along=from+(to-from)*(n+randomRange(0.2f,0.8f))/splats,size=std::min(randomRange(9,15),height*1.2f+4);
        WallHit hit;hit.line=edge.line;hit.side=edge.side;hit.section=2;
        WallSplat wall;
        if(!placeWallSplat(hit,edge.span.a.x+ex/len*along,edge.span.a.y+ey/len*along,edge.span.top-size*0.2f,size,1,
                           when+(n?randomRange(0,0.6f):0),1+int(randomRange(0,4))%splatSplats,false,wall))continue;
        wall.drips=1+(randomRange(0,1)<0.6f);
        for(int k=0;k<wall.drips;++k) {
            wall.dripU[k]=randomRange(-0.4f,0.4f)*size;
            wall.dripLength[k]=std::min(tallestRun,height*(n+k?randomRange(0.5f,1.4f):over));   // some give out on the way
        }
        bloodWallAdd(wall,tint);
    }
    return when+0.3f+3*(1-std::sqrt(1-1/over));   // (a drip's length over its age, gridWallSplat)
}

bool bloodSpill(float x,float y,int sector,float born,float seconds,float &size,float volume,int tint) {
    if(!settings.bloodSpray||Game_Phobos()||volume<smallestSpill)return false;
    const sector_t &here=sectors[sector];
    float floor=floorZ(&here),reach=std::min(poolRadius*size*1.6f+8,40.0f);
    Edge edges[edgesAtOnce];int found=0;
    for(int n=0;n<here.linecount;++n) {
        const line_t &line=*here.lines[n];
        const sector_t *other=line.frontsector==&here?line.backsector:line.frontsector;
        if(!other||other==&here||floor-floorZ(other)<lowestDrop||other->floorpic==skyflatnum||ceilZ(other)<=floor)continue;
        Edge edge;edge.line=int(&line-lines);edge.side=line.frontsector==other?0:1;   // the side facing the drop
        if(!wallSpan(line,edge.side,2,edge.span))continue;
        float ex=edge.span.b.x-edge.span.a.x,ey=edge.span.b.y-edge.span.a.y,len=std::hypot(ex,ey);
        if(len<4)continue;
        edge.along=std::clamp(((x-edge.span.a.x)*ex+(y-edge.span.a.y)*ey)/len,0.0f,len);
        float dx=edge.span.a.x+ex/len*edge.along-x,dy=edge.span.a.y+ey/len*edge.along-y;
        edge.away=std::hypot(dx,dy);
        if(edge.away>reach)continue;
        WallHit wall;   // (a pillar between the pool and the edge)
        if(edge.away>1&&firstSolidWall(x,y,floor+2,dx/edge.away,dy/edge.away,0,edge.away,wall))continue;
        int at=std::min(found,edgesAtOnce-1);
        if(found==edgesAtOnce&&edges[at].away<=edge.away)continue;
        while(at>0&&edges[at-1].away>edge.away) {edges[at]=edges[at-1];--at;}
        edges[at]=edge;
        found=std::min(found+1,edgesAtOnce);
    }
    if(!found)return false;
    size=std::max(size,(edges[0].away+4)/poolRadius);   // broader, up to the edge
    float radius=poolRadius*size;
    for(int n=0;n<found;++n) {
        const Edge &edge=edges[n];
        if(edge.away>=radius)continue;
        float ex=edge.span.b.x-edge.span.a.x,ey=edge.span.b.y-edge.span.a.y,len=std::hypot(ex,ey);
        float half=std::sqrt(radius*radius-edge.away*edge.away),from=std::max(1.0f,edge.along-half),to=std::min(len-2,edge.along+half);
        if(to<from)continue;
        // Over its seconds a pool grows from 0.3 of its reach (buildBlood): the time it gets to this edge.
        float when=born+seconds*std::max(0.0f,(edge.away/radius-0.3f)/0.7f);
        float foot=layRuns(edge,from,to,when,tint);
        // Only the nearest edge takes the rest of the blood down; off a wall too high it never gets there.
        float left=volume*thinning,along=randomRange(from,to);
        if(n||left<smallestSpill*0.75f||edge.span.top-edge.span.bottom>tallestRun)continue;
        bloodPoolLaid(edge.span.a.x+(ex*along+ey*6)/len,edge.span.a.y+(ey*along-ex*6)/len,foot,left,2.5f,tint,left);
    }
    return true;
}
} // namespace doom3d
