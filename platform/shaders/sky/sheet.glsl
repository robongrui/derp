// Far weather sheets: what falls past the map's sky, drawn on the sky
// itself. weather.cpp drops rain, snow, sand and ash only in the map's open
// sectors near the eye, and the sky (ceilings and the band over lower
// neighbors included) hides everything past it, so they would end at the
// courtyard walls and the sky ceiling. Past them they fall on in sheets,
// world-anchored like the sky, each at a distance, falling at its kind's
// speed and across on the wind (lean: units across for one down), stepping
// at the tic rate. A sheet is slanted rather than scrolled sideways:
// scrolled, each way round the sky would move by its own share of the wind,
// and as the level went on what falls would bunch up and smear into arcs.
// It is drawn as pixelBlock draws the near ones: how big a cell of the
// sheet shows on the screen follows from the camera (the ray's change per
// Doom pixel across and down), so looking up, where the cylinder's cells
// shrink and skew, they keep their shape and fewer cells hold one (share).
// Each kind in its own file (sky/rain.glsl, snow, sand, ash). Part of
// sky.frag.
struct Sheet { float pixel,tic,slant,share; vec2 g; mat2 toSheet,toScreen; };
Sheet weatherSheet(vec3 ray,float angle,float rise,float lean) {
    Sheet s;
    s.pixel=100.0*c.projection.y; // Doom pixels per radian, as pixelBlock counts them
    s.tic=floor(c.fog.w*35.0)/35.0;
    vec2 across=vec2(-sin(angle),cos(angle));
    s.slant=dot(c.weather.yz,across)*lean;
    s.g=vec2(angle,rise)*s.pixel;
    // The ray scaled to reach the screen plane one unit ahead, and how it
    // moves for a Doom pixel across and down.
    vec3 onScreen=ray/max(dot(ray,c.forward.xyz),1e-3);
    vec3 dx=c.right.xyz/(100.0*c.projection.y),dy=-c.up.xyz/100.0/c.projection.y;
    float flatLength=max(length(onScreen.xy),1e-4);
    vec2 gx=vec2(onScreen.x*dx.y-onScreen.y*dx.x,dx.z-onScreen.z*dot(onScreen.xy,dx.xy)/(flatLength*flatLength))/flatLength*s.pixel;
    vec2 gy=vec2(onScreen.x*dy.y-onScreen.y*dy.x,dy.z-onScreen.z*dot(onScreen.xy,dy.xy)/(flatLength*flatLength))/flatLength*s.pixel;
    gx.x/=flatLength;gy.x/=flatLength;
    // The slant turns with the way round the sky, and high up that counts
    // as much as the azimuth itself.
    float turning=dot(c.weather.yz,vec2(-cos(angle),-sin(angle)))*lean*s.g.y/s.pixel;
    mat2 sheet=mat2(vec2(gx.x*(1.0+turning)+s.slant*gx.y,gx.y),vec2(gy.x*(1.0+turning)+s.slant*gy.y,gy.y));
    float spread=abs(determinant(sheet));
    s.toSheet=sheet;
    s.toScreen=inverse(sheet);
    s.share=spread>1e-8?min(1.0,1.0/spread):0.0;
    return s;
}
// Where the ray meets a sheet far units out, falling at speed, now: in
// sheet pixels, in which whatever falls stays put.
vec2 sheetPoint(Sheet s,float speed,float far) {return vec2(s.g.x+s.slant*s.g.y,s.g.y+speed/far*s.pixel*s.tic);}
// A sheet carried across on the wind instead (sand, ash; made unslanted,
// lean 0): a slant as steep as theirs turns the sheet over on itself where
// the wind blows toward or away from the eye, and what falls there smears
// into long arcs. Carried, each way round the sky moves by its own share of
// the wind, so the sheet would bunch up as the level went on: it lives in
// two phases half a life apart, each carried from where its life starts
// and fading in and out over it, a life short enough that the carrying
// never squeezes or stretches the sheet by more than half. carried: the
// share of the wind it rides. The phase's own sheet (how its pixels land
// on the screen), where the ray meets it (p), its weight now (the two sum
// to 1) and which life it is in (for the seeds, so none comes back).
Sheet sheetDrift(Sheet s,float angle,float speed,float far,float carried,int phase,out vec2 p,out float weight,out float life) {
    float wind=max(length(c.weather.yz)*carried,1.0),span=0.5*far/wind;
    float clock=s.tic/span+0.5*float(phase),age=fract(clock)*span;
    life=floor(clock);
    weight=1.0-abs(2.0*fract(clock)-1.0);
    float across=dot(c.weather.yz,vec2(-sin(angle),cos(angle)))*carried;
    p=vec2(s.g.x-across*age/far*s.pixel,s.g.y+speed/far*s.pixel*s.tic);
    // Carried further one way round than the next, the sheet's pixels widen
    // or narrow across.
    float widen=1.0+dot(c.weather.yz,vec2(cos(angle),sin(angle)))*carried*age/far;
    Sheet d=s;
    d.toSheet[0].x*=widen;d.toSheet[1].x*=widen;
    float spread=abs(determinant(d.toSheet));
    d.toScreen=inverse(d.toSheet);
    d.share=spread>1e-8?min(1.0,1.0/spread):0.0;
    return d;
}
// A cell's seed, 0-1. An integer hash: sin() of the big numbers the falling
// rows reach loses its precision on the GPU, and neighboring cells then put
// their drops side by side in dashes.
float sheetSeed(vec2 cell,int layer) {
    uvec2 q=uvec2(ivec2(cell)+ivec2(layer*7919,layer*131));
    uint h=q.x*0x8da6b343u^q.y*0xd8163841u;
    h^=h>>16;h*=0x7feb352du;h^=h>>15;h*=0x846ca68bu;h^=h>>16;
    return float(h&0xffffffu)/16777216.0;
}
// Whether the ray's point p (sheet pixels) lies on a Doom pixel square at.
bool sheetPixel(Sheet s,vec2 p,vec2 at) {
    vec2 off=s.toScreen*(p-at);
    return max(abs(off.x),abs(off.y))<0.5;
}
// Whether p lies on a streak whose head is at head, trailing back along its
// way by trail (sheet pixels): one Doom pixel thick across its run on the
// screen, at least one long, steep (rain) or flat (blown sand) alike.
bool sheetStreak(Sheet s,vec2 p,vec2 head,vec2 trail) {
    vec2 run=s.toScreen*trail,mid=s.toScreen*(p-head)-run*0.5;
    if(abs(run.y)>=abs(run.x)) {
        float tall=max(1.0,floor(abs(run.y)+0.5));
        float side=mid.x-(abs(run.y)>1e-3?run.x*mid.y/run.y:0.0);
        return abs(mid.y)<tall*0.5&&abs(side)<0.5;
    }
    float wide=max(1.0,floor(abs(run.x)+0.5));
    return abs(mid.x)<wide*0.5&&abs(mid.y-run.y*mid.x/run.x)<0.5;
}
// Fading out toward the zenith, where the cylinder's cells crowd.
float sheetFade(float rise) {return 1.0-smoothstep(0.9,1.8,rise);}
