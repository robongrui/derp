// traceRay (platform/baked_lighting.h) on the graphics card, line for line:
// the sector walk, the box tree with its stack, the tie rule (the line listed
// first), the start rules, sky ceilings, masked middles, decorations, the
// 256-step cap. Every change to traceRay is made here too; the trace check
// (scene3d/gpu_trace_check.cpp, DERP_GPUTRACECHECK) is the guard.
//
// The map is one buffer of 32-bit words (scene3d/gpu_trace.cpp flattens
// BakeMap into it); mapA and mapB, from the kernel's uniform block, give where
// each part starts:
//   mapA.x sectors, 12 words each: floor, ceiling, the ceiling a shut door
//          opens to (OpenDoors), flags (1 sky, 2 roofless), line start and
//          count in the lists, tree start in the nodes (-1: none), order start
//          and thing start in the lists, thing count, and the floor and
//          ceiling of the map before (a re-bake round a moved door);
//   mapA.y lines, 8 words: ax, ay, bx, by, front, back, mask (-1: none);
//   mapA.z tree nodes, 8 words: low x, low y, high x, high y, first, count;
//   mapA.w lists of ints (a sector's lines, its tree order, its things);
//   mapB.x masks, 12 words: first coverage bit, width, height, -, u,
//          direction, anchor, bottom, top, the line's length;
//   mapB.y decorations, 8 words: x, y, top, left, width, height, first bit;
//   mapB.z coverage, one bit a texel.
// Kept plain for the DXIL to come: no integer division or modulo of
// negatives, every output written. precise keeps the compiler from fusing
// multiplies into adds, so crossings land as the processor's do.

// A macro, not a const: uintBitsToFloat isn't a constant expression, and newer glslang (16.5,
// Homebrew's on the Intel Mac runner) refuses it as a global const's initializer.
#define traceInf uintBitsToFloat(0x7F800000u)
const int hitNone=0,hitFloor=1,hitCeiling=2,hitWall=3,hitThing=4;
// Ray flags, as the processor's arguments: SkyCeiling::escape, the map with
// shut doors open (OpenDoors::of), roofless sectors' ceilings taken for sky,
// the heights of the map before (gatherMoved's before).
const uint rayEscape=1u,rayDoors=2u,rayRoofless=4u,rayBefore=8u;

struct TraceHit { int kind,sector,line,part; float at; bool skyward; };

float mapF(uint i) {return uintBitsToFloat(mapWords[i]);}
int mapI(uint i) {return int(mapWords[i]);}
bool coverBit(uint bit) {return (mapWords[mapB.z+(bit>>5)]&(1u<<(bit&31u)))!=0u;}

bool maskBlocks(int line,float u,float height) {
    int mask=mapI(mapA.y+uint(line)*8u+6u);
    if(mask<0)return false;
    uint m=mapB.x+uint(mask)*12u;
    int width=mapI(m+1u),rows=mapI(m+2u);
    float mu=mapF(m+4u),direction=mapF(m+5u),anchor=mapF(m+6u),bottom=mapF(m+7u),top=mapF(m+8u),length=mapF(m+9u);
    if(width<1||rows<1||height<bottom||height>=top)return false;
    precise float distance=u*length;
    precise float column=floor(mu+direction*distance);
    int xi=int(column),x=abs(xi)%width;
    if(xi<0&&x!=0)x=width-x;
    precise float row=floor(anchor-height);
    int y=clamp(int(row),0,rows-1);
    return coverBit(mapWords[m]+uint(y*width+x));
}
bool thingBlocks(int thing,vec3 o,vec3 d,float from,float to,inout float at) {
    uint p=mapB.y+uint(thing)*8u;
    float tx=mapF(p),ty=mapF(p+1u),top=mapF(p+2u),left=mapF(p+3u);
    int width=mapI(p+4u),rows=mapI(p+5u);
    precise float h2=d.x*d.x+d.y*d.y;
    if(h2<1e-6)return false;
    precise float t=((tx-o.x)*d.x+(ty-o.y)*d.y)/h2;
    if(t<=from+1e-3||t>=to)return false;
    precise float px=o.x+d.x*t,py=o.y+d.y*t,pz=o.z+d.z*t;
    precise float lateral=((px-tx)*d.y-(py-ty)*d.x)/sqrt(h2);
    precise float fc=floor(lateral-left),fr=floor(top-pz);
    int column=int(fc),row=int(fr);
    if(column<0||row<0||column>=width||row>=rows)return false;
    if(!coverBit(mapWords[p+6u]+uint(row*width+column)))return false;
    at=t;return true;
}
bool nodeSpan(int node,vec3 o,vec3 d,out float near,out float far) {
    uint n=mapA.z+uint(node)*8u;
    near=-traceInf;far=traceInf;
    for(int axis=0;axis<2;++axis) {
        float low=mapF(n+uint(axis)),high=mapF(n+2u+uint(axis)),origin=o[axis],along=d[axis];
        if(abs(along)<1e-12) {
            if(origin<low||origin>high)return false;
            continue;
        }
        precise float a=(low-origin)/along,b=(high-origin)/along;
        near=max(near,min(a,b));far=min(far,max(a,b));
    }
    return near<=far;
}
// The nearest crossing ahead; on a tie the line listed first.
void testLine(int position,uint lineStart,int sector,int previous,float t,vec3 o,vec3 d,
              inout float best,inout float along,inout int crossed,inout int first) {
    int index=mapI(mapA.w+lineStart+uint(position));
    if(index==previous)return;
    uint l=mapA.y+uint(index)*8u;
    float ax=mapF(l),ay=mapF(l+1u),bx=mapF(l+2u),by=mapF(l+3u);
    int front=mapI(l+4u),back=mapI(l+5u);
    precise float ex=bx-ax,ey=by-ay,ox=ax-o.x,oy=ay-o.y;
    precise float det=d.x*ey-d.y*ex;
    if(abs(det)<1e-9)return;
    precise float crossing=(ox*ey-oy*ex)/det,u=(ox*d.y-oy*d.x)/det;
    // A ray starting on one of its sector's lines, or a hair past it, crosses
    // it at once when it heads out through it (the front lies right of a to b).
    if(crossing<=t+1e-4) {
        if(previous>=0||crossing<-0.01||front==back)return;
        if(front==sector?det>=0.0:back==sector?det<=0.0:true)return;
    }
    if((crossing<best||(crossing==best&&position<first))&&u>=0.0&&u<=1.0) {best=crossing;along=u;crossed=index;first=position;}
}
void traceStop(out TraceHit hit,int kind,int sector,int line,int part,float at) {
    hit.kind=kind;hit.sector=sector;hit.line=line;hit.part=part;hit.at=at;hit.skyward=false;
}
// True when the ray travels length without hitting anything, or (escape)
// leaves through a sky ceiling; hit says what stopped it, or that it left
// through the sky (skyward). A ray that ran out of steps or sectors returns
// false with kind none, as the processor's does.
bool traceRay(int sector,vec3 o,vec3 d,float length,uint flags,out TraceHit hit) {
    traceStop(hit,hitNone,-1,-1,0,0.0);
    bool escape=(flags&rayEscape)!=0u,doors=(flags&rayDoors)!=0u,roofless=(flags&rayRoofless)!=0u,before=(flags&rayBefore)!=0u;
    uint floorAt=before?10u:0u,ceilingAt=before?11u:doors?2u:1u;
    float t=0.0;int previous=-1;
    int stack[32];
    for(int step=0;step<256&&sector>=0;++step) {
        uint s=mapA.x+uint(sector)*12u;
        float floorZ=mapF(s+floorAt),ceilingZ=mapF(s+ceilingAt);
        uint sectorFlags=mapWords[s+3u];
        bool sSky=(sectorFlags&1u)!=0u||(roofless&&(sectorFlags&2u)!=0u);
        bool openTop=sSky&&!escape;
        precise float exitT=traceInf;
        float best=traceInf,along=0.0;int crossed=-1,first=0x7FFFFFFF;
        if(d.z>0.0&&!openTop)exitT=(ceilingZ-o.z)/d.z;
        if(d.z<0.0)exitT=(floorZ-o.z)/d.z;
        uint lineStart=mapWords[s+4u];
        int lineCount=mapI(s+5u),tree=mapI(s+6u);
        uint orderStart=mapWords[s+7u];
        if(tree<0) {for(int n=0;n<lineCount;++n)testLine(n,lineStart,sector,previous,t,o,d,best,along,crossed,first);}
        else {
            // Past the floor, ceiling or ray's end a crossing changes nothing.
            float reach=min(exitT,length);
            int depth=0;stack[depth++]=0;
            while(depth>0) {
                int node=stack[--depth];
                float near,far;
                if(!nodeSpan(tree+node,o,d,near,far)||far<t||near>best||near>reach)continue;
                uint n=mapA.z+uint(tree+node)*8u;
                int nodeFirst=mapI(n+4u),nodeCount=mapI(n+5u);
                if(nodeCount>0) {
                    for(int k=nodeFirst;k<nodeFirst+nodeCount;++k)
                        testLine(mapI(mapA.w+orderStart+uint(k)),lineStart,sector,previous,t,o,d,best,along,crossed,first);
                    continue;
                }
                // Nearer child last, so it is searched first.
                float a,b,an,bn;
                bool hitA=nodeSpan(tree+nodeFirst,o,d,an,a),hitB=nodeSpan(tree+nodeFirst+1,o,d,bn,b);
                if(!hitA&&!hitB)continue;
                bool aFirst=!hitB||(hitA&&an<=bn);
                if(depth>30)continue;   // never on a real map: the tree is a few levels deep
                stack[depth++]=aFirst?nodeFirst+1:nodeFirst;stack[depth++]=aFirst?nodeFirst:nodeFirst+1;
            }
        }
        float nearest=min(min(exitT,best),length),closest=traceInf,at=0.0;
        uint thingStart=mapWords[s+8u];
        int thingCount=mapI(s+9u);
        for(int k=0;k<thingCount;++k)
            if(thingBlocks(mapI(mapA.w+thingStart+uint(k)),o,d,t,nearest,at)&&at<closest)closest=at;
        if(closest<traceInf) {traceStop(hit,hitThing,sector,-1,0,closest);return false;}
        if(length<=exitT&&length<=best)return true;
        if(exitT<=best) {
            if(d.z>0.0&&sSky&&escape) {hit.skyward=true;return true;}
            traceStop(hit,d.z>0.0?hitCeiling:hitFloor,sector,-1,0,max(exitT,t));return false;
        }
        uint l=mapA.y+uint(crossed)*8u;
        int front=mapI(l+4u),back=mapI(l+5u);
        int next=front==sector?back:front;
        if(next<0) {traceStop(hit,hitWall,sector,crossed,0,best);return false;}
        uint ns=mapA.x+uint(next)*12u;
        float nextFloor=mapF(ns+floorAt),nextCeiling=mapF(ns+ceilingAt);
        uint nextFlags=mapWords[ns+3u];
        bool nextSky=(nextFlags&1u)!=0u||(roofless&&(nextFlags&2u)!=0u);
        precise float height=o.z+d.z*best;
        if(height<nextFloor) {traceStop(hit,hitWall,sector,crossed,2,best);return false;}
        if(height>nextCeiling) {
            if(!(sSky&&nextSky)) {traceStop(hit,hitWall,sector,crossed,1,best);return false;}
            if(escape) {hit.skyward=true;return true;}
        }
        if(maskBlocks(crossed,along,height)) {traceStop(hit,hitWall,sector,crossed,0,best);return false;}
        sector=next;previous=crossed;t=best;
    }
    return false;
}
