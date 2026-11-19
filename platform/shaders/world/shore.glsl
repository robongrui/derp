// Damp shores above liquids.
#include "pool_ice.glsl"
// Part of world.frag, which declares the resources these read.
// Shorelines (FEATURE_DAMP_SHORES). Liquids wet what they touch: walls and banks
// above the waterline turn darker up to a ragged line, read at texel
// centers so the damp edge steps in whole Doom pixels.
// The shore map's distances: shoreRange onto the land, shoreReach out over the
// liquid, where the shore ripples run further than the damp climbs (caustics.cpp).
const float shoreRange=16.0;
const float shoreReach=28.0;
float shoreDistance(vec2 xy) {
    return textureLod(shore,(xy-c.map.xy)*c.map.z/vec2(textureSize(shore,0)),0.0).r*(shoreRange+shoreReach)-shoreRange;
}
// Shore ripples in the liquid itself: each texel of the liquid asks the shore
// map how far the shore is and lies on a crest or not. The crests are lines of
// equal distance from the shore, a wavelength apart, running out and fading by
// reach, as liquids.cpp draws them in blocks (its numbers, in ripple: wavelength,
// the clock in periods, reach, strength; a negative wavelength is blood's one
// still dark line). At texel centers, so a crest is whole texels; at least a
// texel wide, or a thin one would come and go between two rows. A crest
// brightens the liquid as shaded by a step, in the liquid's own color.
vec3 shoreRipple(vec3 color,vec3 world,vec4 ripple,vec4 shape,vec3 liquid,float away) {
    vec2 at=floor(world.xy)+0.5;
    float d=shoreDistance(at);
    if(d<=0.0) return color;
    bool still=ripple.x<0.0;
    float r=1.5,level=1.0;
    if(!still) {
        // (Phases drift across the map, so one pool's walls do not pulse in lockstep.)
        float phase=0.35*(sin(at.x*0.011+at.y*0.004)+cos(at.y*0.013-at.x*0.006));
        float lead=fract(ripple.y+phase);
        r=1.5+ripple.x*(lead+max(round((d-1.5)/ripple.x-lead),0.0));
        if(r>ripple.z) return color;
        level=round((1.0-(r-1.5)/(ripple.z-1.5))*ripple.w*4.0)/4.0;
        if(level<=0.0) return color;
    }
    // As wide as the drawn ripples at least: those are blocks of Doom pixels, never
    // under one, so from a little way off they are wider than a texel, the more so
    // on a surface seen at a slant. A texel-wide line there read as fainter. Not
    // so wide that two crests close up.
    float pixel=max(dot(world-c.eye.xyz,c.forward.xyz),4.0)/(100.0*c.projection.y);   // map units to a Doom pixel
    float slant=max(abs(normalize(c.eye.xyz-world).z),0.2);
    // (And half as wide again: measured against the drawn ones, whose blocks round up
    // to whole Doom pixels, these covered a third less of the liquid.)
    float width=max(max(shape.x+r*0.06,1.0)*1.4,pixel/slant);
    if(!still) width=min(width,ripple.x*0.6);
    if(abs(d-r)>=width*0.5) return color;
    // As far as the drawn ones are shown.
    float shown=1.0-smoothstep(1100.0,1536.0,away);
    // By the eye's exposure, as the drawn crests are (flat.frag): in a dark room the
    // eye opens up and they brighten the more; without it these read as fainter.
    if(!still&&c.effects.z==0.0) shown*=c.fx.x;
    if(still) return color*mix(1.0,0.55,shown);
    float peak=max(max(liquid.r,liquid.g),max(liquid.b,0.01));
    vec3 lift=level*level*(0.88+0.12*liquid/peak)*shown;
    // The crest at the wall half as much again: drawn, its blocks lie over one another
    // there and brighten twice in places (measured: up to 1.5 times the liquid where
    // the others are 1.2).
    // It is the line the eye takes the ripples by; without it they read as faint.
    return r<1.5+ripple.x?color*pow(1.0+lift,vec3(1.5)):color*(1.0+lift);
}
uvec4 shoreCell(vec2 xy) {
    ivec2 size=textureSize(shore,0);
    return uvec4(round(texelFetch(shore,clamp(ivec2(floor((xy-c.map.xy)*c.map.z)),ivec2(0),size-1),0)*255.0));
}
// The nearest edge's liquid sector and tint; false where none is in range.
bool shoreLiquid(vec2 xy,out uint liquid,out vec3 tint) {
    uvec4 v=shoreCell(xy);
    liquid=v.g|((v.b&63u)<<8);
    tint=vec3(float(v.a>>5)/7.0,float((v.a>>2)&7u)/7.0,float(v.a&3u)/3.0);
    return (v.b&128u)!=0u;
}
float shoreHash(vec2 p) {
    uvec2 q=uvec2(ivec2(floor(p)));
    uint h=q.x*1664525u^(q.y*1013904223u+0x9e3779b9u);
    // Chris Wellons' lowbias32 (hash-prospector, public domain).
    h^=h>>16;h*=0x7feb352du;h^=h>>15;h*=0x846ca68bu;h^=h>>16;
    return float(h)/4294967295.0;
}
// Soft value noise on whole cells of p.
float poolNoise(vec2 p) {
    vec2 i=floor(p),f=smoothstep(0.0,1.0,fract(p));
    return mix(mix(shoreHash(i),shoreHash(i+vec2(1,0)),f.x),mix(shoreHash(i+vec2(0,1)),shoreHash(i+vec2(1,1)),f.x),f.y);
}
// How wet a point is, in quarter steps: the liquid wicks up to a ragged line
// 6-19 units above it, less the farther across a bank, wandering every few
// texels (wander) and climbing where the artwork is darker, as mortar and
// cracks soak up more. texel adds a per-texel jitter.
float dampness(float height,float across,vec2 wander,vec2 texel,float luma) {
    vec2 k=wander/6.0,i=floor(k),f=smoothstep(0.0,1.0,fract(k));
    float drift=mix(mix(shoreHash(i),shoreHash(i+vec2(1,0)),f.x),mix(shoreHash(i+vec2(0,1)),shoreHash(i+vec2(1,1)),f.x),f.y);
    float reach=6.0+7.0*drift+5.0*(1.0-saturate(luma*2.0))+1.5*shoreHash(texel);
    return round(saturate((reach-height-across*1.5)/3.0)*4.0)/4.0;
}
// Walls read the edge just off their visible side, floors at their texel's
// center. A floor at its liquid's height on the liquid's side of the edge is
// the liquid itself and stays as it is. ice (Phobos, poolIce): the liquid's ice
// and glow left; a pool freezing over frosts its rim along the same ragged line,
// thin as the sheet forms and creeping up as the glow fades (world.frag whitens it).
// Walls go by their texture's column u and its direction along the wall uDir: a column
// from the derived normal (dot with the map position) is off by units at E1M1's
// coordinates and scatters the line into speckle. Everything at the texel's center,
// so the line steps in whole Doom pixels however a freezing pool's short reach
// (below) magnifies it.
float shoreWet(vec3 world,vec3 normal,float u,vec2 uDir,uint sector,float luma,out vec3 tint,out vec2 ice) {
    tint=vec3(1);ice=vec2(0.0,1.0);
    bool wall=normal.z<=0.5;
    float along=floor(u);
    if(wall) world=vec3(world.xy+uDir*(along+0.5-u),floor(world.z)+0.5);
    vec2 at=wall?world.xy+normal.xy*0.25:floor(world.xy)+0.5;
    uint liquid;
    // Hot liquids (tint 0) leave no damp.
    if(!shoreLiquid(at,liquid,tint)||tint==vec3(0)) {tint=vec3(1);return 0.0;}
    float d=shoreDistance(at),water=sectorInfo[liquid].y;
    if(!wall&&d>0.0&&abs(sectorInfo[sector].y-water)<0.5) return 0.0;
    float height=(wall?world.z:sectorInfo[sector].y)-water;
    if(height<-0.5) return 0.0;
    vec2 wander=wall?vec2(along,0):floor(world.xy),texel=wall?vec2(along,floor(world.z)):floor(world.xy);
    ice=poolIce(liquid);
    float reach=ice.x>0.0?max(ice.x*(0.25+1.25*(1.0-ice.y)),0.01):1.0;
    return dampness(max(height,0.0)/reach,abs(d)/reach,wander,texel,luma);
}
// Glowing liquid edges (FEATURE_GLOW_EDGES). Vanilla walls take one texture,
// so only walls a mapper gave an edge texture (NUKE24) show the strip of
// glowing liquid at their foot; walls running straight into the pool stay
// bare. Walls at a glowing liquid's edge take that strip (liquid_edges.cpp,
// stored below the caustic frames): the edge texture's own texels, row by row
// up from the liquid's surface, painted over the wall where the art is
// liquid. Returns the paint (0 none) with the strip's color and glow.
// Only walls standing at the shore itself, not behind a bank.
float edgeStrip(vec3 world,vec3 normal,float u,out vec3 paint,out float glow) {
    paint=vec3(0);glow=0.0;
    vec2 at=world.xy+normal.xy*0.25;
    uvec4 v=shoreCell(at);
    if((v.b&192u)!=192u||abs(shoreDistance(at))>2.0) return 0.0;
    float water=sectorInfo[v.g|((v.b&63u)<<8)].y;
    int base=max(1,textureSize(causticPattern,0).y/(64*7))*64*7;
    int rows=int(round(texelFetch(causticPattern,ivec2(0,base),0).r*255.0));
    float height=world.z-water;
    int row=int(floor(height));
    if(rows==0||row<0||row>=rows) return 0.0;
    int x=int(floor(u))&63;   // the wall's own column (see shoreWet)
    float index=texelFetch(causticPattern,ivec2(x,base+1+row),0).r;
    float amount=texelFetch(causticPattern,ivec2(x,base+1+rows+row),0).r;
    glow=texelFetch(causticPattern,ivec2(x,base+1+2*rows+row),0).r;
    paint=texelFetch(palette,ivec2(int(round(index*255.0)),0),0).rgb;
    // Its pool freezing over (Phobos): the strip is the slime itself, so it goes with the
    // pool's glow, paint and light, gone as the ice turns white; the rim frost (shoreWet)
    // takes the wall.
    vec2 ice=poolIce(v.g|((v.b&63u)<<8));
    float left=mix(1.0,ice.y,ice.x);
    glow*=left;
    return amount*left;
}
