// The bounce's rays (BounceSolve::run in scene3d/bake_bounce.cpp): a
// sample's frame and its nth ray from the pattern (RayTable), and what the
// ray met as the solve keeps it (RaySeen in its wide form). Needs trace.glsl,
// the samples, the table and the strips, and the uniforms grid (the map's
// origin, its cell, the rays' reach) and dims (map width and height, atlas
// stride and width).
const uint rayNothing=0u,rayEscaped=1u,raySky=2u,rayBare=3u,rayWall=4u,rayFlat=5u,rayThing=6u;

struct BounceSample { vec3 o,normal,tangent,bitangent; int sector; uint flags,turn; };
BounceSample bounceSample(uint s) {
    uvec4 a=samples[2u*s],b=samples[2u*s+1u];
    BounceSample bs;
    bs.o=vec3(uintBitsToFloat(a.x),uintBitsToFloat(a.y),uintBitsToFloat(a.z));
    bs.normal=vec3(uintBitsToFloat(b.x),uintBitsToFloat(b.y),uintBitsToFloat(b.z));
    bs.sector=int(a.w&0xFFFFFFu);bs.flags=a.w>>24;bs.turn=b.w;
    // The frame as the gather makes it.
    vec3 helper=abs(bs.normal.z)<0.9?vec3(0.0,0.0,1.0):vec3(1.0,0.0,0.0);
    precise vec3 tangent=cross(helper,bs.normal);
    tangent/=sqrt(dot(tangent,tangent));
    precise vec3 bitangent=cross(bs.normal,tangent);
    bs.tangent=tangent;bs.bitangent=bitangent;
    return bs;
}
vec3 bounceDirection(BounceSample bs,uint ray,uint rays) {
    vec4 p=table[bs.turn*rays+ray];
    precise vec3 d=bs.tangent*p.x+bs.bitangent*p.y+bs.normal*p.z;
    return d;
}
// kind << 29 | surface in y, texel or cell in x.
uvec2 bounceTrace(BounceSample bs,vec3 d,bool skyDome) {
    TraceHit hit;
    uint kind=rayNothing,surface=0u,at=0u;
    vec3 o=bs.o;
    if(traceRay(bs.sector,o,d,grid.w,bs.flags|rayEscape,hit)||hit.kind==hitNone) {
        // Only a ray that left through the sky brings its light; from a door's own floor
        // the sky is seen through the map as it stands (the door shut).
        bool reached=hit.skyward&&skyDome;
        if(reached&&(bs.flags&rayDoors)!=0u) {
            TraceHit real;
            reached=traceRay(bs.sector,o,d,grid.w,(bs.flags&~rayDoors)|rayEscape,real)&&real.skyward;
        }
        kind=reached?raySky:rayEscaped;
    } else if(hit.kind==hitWall) {
        uint l=mapA.y+uint(hit.line)*8u;
        int side=mapI(l+4u)==hit.sector?0:1;
        uint strip=uint(hit.line)*2u+uint(side);
        surface=strip*3u+uint(hit.part);
        vec4 box=strips[3u*strip],line=strips[3u*strip+1u],scale=strips[3u*strip+2u];
        kind=rayBare;
        if(box.x>=0.0) {
            precise vec3 h=o+d*hit.at;
            precise float along=(h.x-line.x)*line.z+(h.y-line.y)*line.w;
            precise float fc=floor(along*scale.x),fr=floor((h.z-scale.y)*scale.x);
            int column=clamp(int(fc),0,int(box.z)-1),row=clamp(int(fr),0,int(box.w)-1);
            kind=rayWall;at=uint(int(box.y)+row)*dims.z+uint(int(box.x)+column);
        }
    } else if(hit.kind==hitFloor||hit.kind==hitCeiling) {
        precise vec3 h=o+d*hit.at;
        precise float fx=floor((h.x-grid.x)/grid.z),fy=floor((h.y-grid.y)/grid.z);
        int cx=clamp(int(fx),0,int(dims.x)-1),cy=clamp(int(fy),0,int(dims.y)-1);
        kind=rayFlat;surface=uint(hit.sector)*2u+(hit.kind==hitCeiling?1u:0u);at=uint(cy)*dims.x+uint(cx);
    } else if(hit.kind==hitThing&&hit.line>=0) {
        // A decoration: lit as the floor in front of it on the ray's side (the gather's landed).
        uint p=mapB.y+uint(hit.line)*12u;
        if(mapI(p+10u)>=0) {
            precise vec3 h=o+d*hit.at;
            precise float across=sqrt(d.x*d.x+d.y*d.y),back=float(mapI(p+4u))*0.5+grid.z;
            precise float bx=across>1e-6?h.x-d.x/across*back:h.x,by=across>1e-6?h.y-d.y/across*back:h.y;
            precise float fx=floor((bx-grid.x)/grid.z),fy=floor((by-grid.y)/grid.z);
            int cx=clamp(int(fx),0,int(dims.x)-1),cy=clamp(int(fy),0,int(dims.y)-1);
            kind=rayThing;surface=uint(hit.line);at=uint(cy)*dims.x+uint(cx);
        }
    }
    return uvec2(at,kind<<29|surface);
}
