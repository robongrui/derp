// Sky depth for a painted city (scene3d/sky_city.cpp): a few blocks round
// the map, its buildings as boxes standing where the painting's do, with
// world-sized window facades; behind them a far city of plain boxes in the
// painting's dim brown; the city burning, fires in its streets and lower
// floors; over all a pall of the painting's smoke, lit from beneath by the
// fires, explosions blooming in it, and columns of smoke rising from the
// city, leaning with the wind.
vec4 cityTexel(int i) {return texelFetch(layers,ivec2(i,depthSize().y+1),0);}
vec3 cityWall() {return layerColor(cityTexel(3).g);}
vec3 cityWindow() {return layerColor(cityTexel(3).b);}
vec3 cityGlow() {return layerColor(cityTexel(3).a);}
vec3 citySmokeDark() {return layerColor(cityTexel(4).r);}
vec3 citySmokeMid() {return layerColor(cityTexel(4).g);}
vec3 cityBlast() {return layerColor(cityTexel(4).a);}
// The fires' colour: the painting's glow turned orange, as bright, so it
// reads as fire rather than the red of lava.
vec3 cityFire() {vec3 g=cityGlow();return mix(g,vec3(1.0,0.5,0.13)*max(dot(g,vec3(0.299,0.587,0.114))*2.2,0.3),0.7);}
// Smoke over fires: the painting's smoke as dark, its cool grey warmed to
// a brown, as smoke over burning ground never shows blue.
vec3 warmSmoke(vec3 s) {return mix(s,dot(s,vec3(0.299,0.587,0.114))*vec3(1.2,0.85,0.6),0.75);}
// The fire's flicker at a place (world units) now: 0.4-1, quick.
float fireFlicker(vec2 at,uint seed) {return 0.4+0.6*cloudValue(vec2(at.x/70.0+at.y/90.0,cloudSeconds()*4.0),seed);}

// How near q lies to one of the fires the plumes rise from, 0 far to 1 in it
// (below): what burns is where the smoke comes from.
float plumeFire(vec2 q);
// A building at q among a ring's (ring radius r): a box as deep as it is
// wide, sheer walls and a flat roof. A near one's front stands where the
// reference eye sees the painted skyline, so the painting is its facade; a
// far one's top is its own (sky_city.cpp). which as towerGround's, its
// second value the box's half-depth.
float buildingGround(vec2 q,float r,int ring,ivec2 size,out vec4 which) {
    int x=wrapColumn(int(floor(aroundU(q,size))),size.x);
    which=vec4(0.0,0.0,-1e30,float(x));
    vec4 t=texelFetch(layers,ivec2(x,size.y+2+ring),0);
    if(t.a==0.0)return floorHeight();
    float depth=(0.5+t.a*1.5)*r,halfDepth=max(0.5*t.g*255.0*radPerColumn(size)*depth,0.03*depth);
    float front=depth-halfDepth,top=(ring==0?paintedRise(x,0,size):ridgeRise(round(t.b*255.0),size))*front;
    if(abs(length(q)-depth)>=halfDepth||top<=floorHeight())return floorHeight();
    which=vec4(depth,halfDepth,top,float(x));
    return top;
}
// A building's face at p, a world-sized facade: storeys 112 units high,
// windows 96 apart (lit as many as its seed gives it, a few flickering
// with the fires), the same size near and far, so a tall building shows its
// dozens of floors (one painted texel a floor read as a model) and no
// window's size changes with its distance. Where a window grows smaller
// than a pixel the facade fades to its average, rather than crawl. Walls:
// the near buildings' dark silhouettes' colour, the far ones' dim brown.
// Sides in shade; lit from below by the fires in the streets; dusted by
// the air between.
vec3 buildingColor(vec3 p,vec3 o,int ring,ivec2 size,vec4 which) {
    // The streets between the buildings: burning.
    if(which.z<-1e29)return mix(citySmokeDark(),cityFire(),0.3+(0.25+0.45*plumeFire(p.xy))*fireFlicker(p.xy,401u));
    int x=int(which.w);
    vec4 t=texelFetch(layers,ivec2(x,size.y+2+ring),0);
    float front=which.x-which.y,rho=length(p.xy),inside=rho-front;
    int left=x-int(round(t.r*255.0));
    bool side=inside>16.0;
    // Along the face in world units: round the front, or in along a side.
    float along=side?inside+float(left)*997.0:atan(p.y,p.x)*front;
    float up=p.z-floorHeight();
    vec2 cell=vec2(along/96.0,up/112.0),f=fract(cell);
    uint seed=uint(left+ring*4099)*0x9E3779B9u;
    // How much of this building is lit: hardly any, the city's dying. Most of what
    // glows is burning.
    float share=0.015+0.06*float(pcgHash(seed)&255u)/255.0;
    ivec2 win=ivec2(floor(cell));
    bool pane=f.x>0.25&&f.x<0.75&&f.y>0.3&&f.y<0.8;
    float lit=float(pcgHash(seed^uint(win.x)*131u^uint(win.y)*7919u)&1023u)/1023.0<share?1.0:0.0;
    vec3 wall=ring==0?layerColor(cityTexel(4).b):cityWall(),window=cityWindow();
    vec3 color=pane?mix(wall*0.6,window,lit):wall;
    // Burning buildings: their lower floors' windows ablaze, flickering.
    // By a plume's fire it's alight, the nearer the higher up.
    float fireHere=plumeFire(p.xy);
    float burning=max(0.3*float(pcgHash(seed^0x51EDu)&255u)/255.0,fireHere>0.05?0.4+0.6*fireHere:0.0);
    float flicker=fireFlicker(vec2(along,up*0.3),seed);
    if(burning>0.35&&pane&&win.y<4+int(26.0*fireHere)&&float(pcgHash(seed^uint(win.x)*733u^uint(win.y)*271u)&255u)/255.0<0.4+0.35*fireHere)
        color=mix(cityFire(),cityBlast(),0.3*flicker)*(0.7+0.3*flicker);
    // A pixel's footprint here (render pixels: c.fx2.x per Doom pixel, 200
    // Doom pixels tall): windows smaller than about two of them fade out.
    float footprint=length(p-o)/(c.projection.y*100.0*c.fx2.x);
    vec3 average=wall*0.85+window*share*0.25;
    // Past its windows a facade still has its shape, or a far tower is one flat
    // colour and reads as a missing texture: blocks of floors in their own shade,
    // ribs up it, whole sections blown out black, the odd one on fire. Blocks of
    // a few pixels at any distance, in whole doublings.
    float lod=exp2(max(round(log2(footprint/40.0)),0.0));
    vec2 block=vec2(along/(288.0*lod),up/(336.0*lod));
    ivec2 at=ivec2(floor(block));
    float h=float(pcgHash(seed^uint(at.x)*2971u^uint(at.y)*6113u)&1023u)/1023.0;
    float rib=fract(block.x)<0.3?0.72:1.0,floors=fract(block.y)<0.22?0.8:1.0;
    vec3 far=wall*(0.65+0.6*h)*rib*floors;
    far=h<0.16?wall*0.18:far;   // blown out
    if(h>0.965)far=mix(cityFire(),cityBlast(),0.25*flicker)*(0.6+0.4*flicker);
    color=mix(color,far,smoothstep(20.0,48.0,footprint));
    color*=side?0.6:1.0;
    // Every building lit from below by the fires in the streets: brightest
    // at its foot, fading up its walls, stronger where the street burns
    // harder (in stretches along it, a burning building's own the most),
    // flickering. On the wall's colour and glowing over it.
    float street=0.3+0.7*smoothstep(0.25,0.8,cloudValue(vec2(along/450.0+float(left)*0.37,float(ring)*5.0),419u));
    if(burning>0.35)street=max(street,0.85);
    float glows=max(320.0,0.3*(which.z-floorHeight()))*(1.0+2.5*fireHere);   // a tall building up its lower third, past that by a fire
    vec3 under=cityFire()*exp(-up/glows)*street*(0.6+0.4*flicker)*(side?0.75:1.0);
    color+=under*0.7+color*under*2.0;
    float dust=1.0-exp(-length(p-o)/(mix(5.0,1.6,thickAir())*c.skyShape.x));
    return mix(color,mix(citySmokeDark(),cityFire(),0.12),dust);
}
// The sky over the city at a rise (height over distance), where the pall
// doesn't reach: its glow at the horizon, dark smoke above.
vec3 citySkyColor(float rise) {
    vec3 low=mix(citySmokeMid(),cityFire(),0.55);
    return mix(low,citySmokeDark(),smoothstep(-0.02,0.5,rise));
}
// The sky behind everything over the city: the fires' glow, dying as it climbs.
vec3 cityHeavens(vec3 o,vec3 ray,ivec2 size) {
    vec3 fire=cityFire(),horizon=mix(citySmokeDark()*0.6,fire,0.5);
    if(ray.z<=0.005)return horizon;
    float rise=ray.z/max(length(ray.xy),1e-4);
    return mix(horizon,mix(warmSmoke(citySmokeDark())*0.3,fire,0.12),smoothstep(0.0,0.7,rise));
}
// The clouds over the city (sky/cloud_volume.glsl), lit from under by what burns:
// over the fires and toward the horizon, thin cloud most. A firestorm's whole
// sky is this, any other weather a share of it, and the dark brings it out.
vec3 cityUnderglow(vec3 cloud,vec3 ray,vec3 at,float cover) {
    float storm=c.weather.w==WEATHER_FIRESTORM?1.0:0.3,dark=lowSun()+c.skyState.y;
    float rise=ray.z/max(length(ray.xy),1e-4);
    float light=storm*(0.4+0.6*saturate(dark))*(0.35+0.65*plumeFire(at.xy*0.5))*exp(-max(rise,0.0)*1.6)*(1.1-0.6*cover);
    return mix(cloud,cityFire()*(0.45+1.1*dot(cloud,vec3(0.299,0.587,0.114))),saturate(light));
}
// The low layer over the burning city along ray from o, under the clouds and so
// in front of them: rolling smoke with gaps they show through, near black where
// thickest, lit from beneath where the city burns hardest, the billows'
// undersides catching the light at their edges (where the smoke thins toward the
// eye). In it explosions bloom: each at a seeded place, flaring in a moment, then
// swelling and fading into glowing smoke over seconds, again somewhere else.
vec3 cityLow(vec3 sky,vec3 o,vec3 ray,float solid) {
    if(ray.z<=0.02||solid<1e29)return sky;
    vec3 fire=cityFire();
    float ceiling=0.45*c.skyShape.x,t=(ceiling-o.z)/ray.z,s=cloudSeconds();
    if(t<=0.0)return sky;
    vec2 q=o.xy+ray.xy*t-windWay()*cloudSpeed()*c.skyShape.x*0.9*s;
    vec2 g=q/(0.03*c.skyShape.x);
    vec2 toward=-normalize(ray.xy+vec2(1e-4))*0.35;   // a step back toward the eye
    float billow=0.55*cloudValue3(g/6.0,s*0.05,301u)+0.3*cloudValue3(g/2.5,s*0.12,313u)+0.15*cloudValue(g,317u);
    float behind=0.55*cloudValue3((g+toward*6.0)/6.0,s*0.05,301u)+0.3*cloudValue3((g+toward*2.5)/2.5,s*0.12,313u)+0.15*cloudValue(g+toward,317u);
    float thick=smoothstep(0.3,0.72,billow);
    float burns=max(0.3*smoothstep(0.45,0.85,cloudValue(g/12.0,331u)),plumeFire(o.xy+ray.xy*t));   // over what burns, not where the wind has got to
    float edge=saturate((billow-behind)*6.0)*(1.0-0.5*thick);   // an underside catching the light
    vec3 color=mix(warmSmoke(citySmokeMid())*0.4,warmSmoke(citySmokeDark())*0.2,thick);
    color+=fire*(0.1+0.12*smoothstep(0.2,0.7,cloudValue(g/12.0,331u)))*(1.0-0.6*thick);   // all of it lit a little from below
    color+=fire*burns*((1.0-thick)*0.6+edge*1.1)*(0.8+0.2*fireFlicker(q/40.0,337u));
    float cover=smoothstep(0.44,0.6,billow);
    for(int i=0;i<6;++i) {
        float period=7.0+float(i)*2.3,start=float(pcgHash(uint(i)*977u+5u)&1023u)/1023.0;
        float cycle=floor(s/period+start),age=(s/period+start-cycle)*period;
        uint h=pcgHash(uint(int(cycle))*0x9E3779B9u^uint(i)*7919u);
        float a=float(h&0xFFFFu)/65535.0*6.2831853,d=(0.2+1.1*float((h>>16)&255u)/255.0)*c.skyShape.x;
        vec2 at=vec2(cos(a),sin(a))*d+windWay()*cloudSpeed()*c.skyShape.x*0.9*age;   // carried off with the smoke since it went up
        float radius=0.08*c.skyShape.x*(0.35+0.65*min(age/1.5,1.0));
        float core=1.0-smoothstep(0.0,1.0,length(o.xy+ray.xy*t-at)/radius-0.4*(billow-0.5));
        float flare=age<0.2?age/0.2:exp(-(age-0.2)/2.2);
        float blast=saturate(core*min(1.0,flare*1.8));
        color=mix(color,mix(fire,cityBlast()*1.3,flare*flare),blast);
        cover=max(cover,blast);
    }
    // Far off it thins out, and the horizon is the fires'.
    return mix(sky,weatherTinted(color),cover*exp(-t/(2.0*c.skyShape.x)));
}
// The painting's sky: charcoal smoke against masses lit white-hot from below.
vec3 cityHot() {return vec3(1.0,0.82,0.7);}
vec3 cityCharcoal() {return mix(citySmokeDark(),citySmokeMid(),0.35)*0.55;}
// The smoke's plumes: each from a fixed place in the city, bent over downwind as
// it climbs, swaying as gusts run up it, widening into the clouds over it. In
// near-ring radii over the floor.
const int plumeCount=10;
vec2 plumeFoot(int i) {
    uint h=pcgHash(uint(i)*0x9E3779B9u+17u);
    float a=float(h&0xFFFFu)/65535.0*6.2831853,d=0.45+0.35*float((h>>16)&255u)/255.0;   // this side of the skyline: the map is 0.25 across, the first buildings 0.5 out
    return vec2(cos(a),sin(a))*d*c.skyShape.x;
}
vec2 plumeMiddle(int i,float climb) {
    vec2 way=windWay(),across=vec2(-way.y,way.x);
    float phase=float(i)*2.4,bent=(0.12+0.5*c.skyWind.z)*climb*(0.5+climb);
    float sway=climb*(0.03*sin(climb*5.0-cloudSeconds()*0.45+phase)+0.014*sin(climb*11.0-cloudSeconds()*0.8+phase*1.7));
    return plumeFoot(i)+(way*bent+across*sway)*c.skyShape.x;
}
float plumeFire(vec2 q) {
    float near=0.0;
    for(int i=0;i<plumeCount;++i)near=max(near,1.0-length(q-plumeFoot(i))/(0.3*c.skyShape.x));
    return saturate(near);
}
float plumeWidth(float climb) {return (0.012+0.11*climb)*c.skyShape.x;}
// The plumes along ray from o: each is met where the ray passes nearest its
// middle, so it stands in the world and shifts as you walk.
vec3 citySmoke(vec3 color,vec3 o,vec3 ray) {
    float a=dot(ray.xy,ray.xy),best=0.0,bestClimb=0.0,bestSide=0.0,bestSeed=0.0,bestRound=0.0;
    if(a<1e-6)return color;
    vec2 flat_=ray.xy/sqrt(a),across=vec2(-flat_.y,flat_.x);
    for(int i=0;i<plumeCount;++i) {
        float t=dot(plumeFoot(i)-o.xy,ray.xy)/a;
        if(t<=0.0)continue;
        float climb=(o.z+ray.z*t-floorHeight())/c.skyShape.x;
        t=dot(plumeMiddle(i,max(climb,0.0))-o.xy,ray.xy)/a;
        climb=(o.z+ray.z*t-floorHeight())/c.skyShape.x;
        if(t<=0.0||climb<=0.0||climb>0.8)continue;
        vec2 from=(o+ray*t).xy-plumeMiddle(i,climb);
        float width=plumeWidth(climb),edge=saturate((1.0-length(from)/width)*1.4)*(1.0-smoothstep(0.5,0.8,climb));
        if(edge>best){best=edge;bestClimb=climb;bestSide=dot(from,across)/c.skyShape.x;bestRound=dot(from,across)/width;bestSeed=float(i);}
    }
    if(best<=0.0)return color;
    // Billows, rolling up it.
    vec3 at=vec3(bestSide*4.6+bestSeed*0.31,bestClimb*4.6-cloudSeconds()*0.1,bestSeed*0.13);
    float n=textureLod(cloudNoise,at,0.0).r,fine=textureLod(cloudNoise,at*vec3(3.0,3.0,1.0),0.0).g;
    // Solid at its heart, only its edges soft and eaten: with holes the buildings
    // showed through and it read as behind them, cut hard it read as paper.
    float d=saturate((n+0.3*(fine-0.5)-(1.0-1.4*best))*2.6);
    if(d<=0.0)return color;
    // Black, as what burns makes it, in the darkest of the clouds' own colours:
    // billows shaded by the noise and rounded off to one side, the fires lighting
    // it from its root.
    float shade=saturate(0.62*n+0.7*(fine-0.5)+0.3*bestRound);
    vec3 smoke=(fieldSky()?fieldRamp(0.02+0.2*shade):mix(warmSmoke(cityCharcoal()),warmSmoke(citySmokeMid()),0.4*shade))*0.7;
    // Against the fire's sky it stands out black, and catches it down one side.
    smoke+=cityFire()*0.16*saturate(bestRound+0.3)*(c.weather.w==WEATHER_FIRESTORM?1.0:0.4);
    float lit=exp(-max(bestClimb-0.1,0.0)/0.05)+0.35*exp(-bestClimb/0.2)*(1.0-0.7*d);   // the floor lies below the horizon: this much of its root shows
    smoke=mix(smoke,cityFire(),saturate(lit))*(0.9+0.1*fireFlicker(vec2(bestSide*400.0,bestClimb*300.0),421u));
    // And it spreads out and is lost in the deck as it climbs.
    return mix(color,smoke,d*(1.0-smoothstep(0.2,0.5,bestClimb)));
}
