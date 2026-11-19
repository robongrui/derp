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
// Each kind in its own file (sky/rain.glsl, snow, sand, ash); the cells
// they are made of in sky/sheet_cells.glsl, which the lamps' cards share.
// Part of sky.frag.
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
// Fading out toward the zenith, where the cylinder's cells crowd.
float sheetFade(float rise) {return 1.0-smoothstep(0.9,1.8,rise);}
