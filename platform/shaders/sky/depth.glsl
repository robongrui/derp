// Sky depth (scene3d/sky_layers.cpp): the painting's ridges cut at their
// saddles into towers round the map, as Yangshuo stands, the near ring
// (c.skyShape.x) well clear of the map, the far ring (z) much further out;
// between them a ring of valley mist (y) and one of clouds (w). Each tower
// keeps its painted outline (built a little smaller, ridgeShrink) at its own
// depth, and reaches toward the eye and away steeply (towerProfile), deepest
// at its peak; all stand on a flat floor a little under the eye (c.skyExtent.w
// near radii down). A sky pixel's ray is traced against them; where it hits,
// the point is looked up in the painting as the reference eye
// (c.skyLayers.xyz, the map's middle) sees it, so from there the sky is the
// painting and from anywhere else the towers stand where they are, in
// perspective, their texels staying on them. The sun lights them by their
// shape tilted by the painting's relief, and the air between dusts them by
// distance.
const float footReach=0.6;   // a tower's feet reach out this share of its height
// A sky of its own (sky_cloud_field.cpp's texel 14) may build its mountains
// smaller, so they don't loom, put more air before them, or keep them hard.
#define ridgeShrink skyRidgeShrink()
float skyRidgeShrink() {return fieldSky()&&cloudSkyTexel(14).g>0.0?cloudSkyTexel(14).g:0.9;}
float layerNoise(float x,int period,uint seed) {
    float i=floor(x),f=x-i;f=f*f*(3.0-2.0*f);
    int a=int(i)-period*int(floor(i/float(period))),b=a+1==period?0:a+1;
    return mix(float(pcgHash(seed^pcgHash(uint(a)))&0xFFFFu),float(pcgHash(seed^pcgHash(uint(b)))&0xFFFFu),f)/65535.0;
}
float layerNoise2(vec2 p,int period,uint seed) {
    float r=floor(p.y),f=p.y-r;f=f*f*(3.0-2.0*f);
    return mix(layerNoise(p.x,period,seed+uint(int(r))*977u),layerNoise(p.x,period,seed+uint(int(r)+1)*977u),f);
}
vec3 layerColor(float index) {return texelFetch(palette,ivec2(int(round(index*255.0)),0),0).rgb;}
int wrapColumn(int x,int w) {return x-w*int(floor(float(x)/float(w)));}
// The painting's size (the layers texture less its eight rows of settings).
ivec2 depthSize() {ivec2 s=textureSize(layers,0);return ivec2(s.x,s.y-layerRows);}
float radPerColumn(ivec2 size) {return 6.2831853/(4.0*float(size.x));}
// Texel column (going on round the horizon, four paintings to a turn) of a
// map position q, as from the reference eye; the row from its height too.
float aroundU(vec2 q,ivec2 size) {return -atan(q.y,q.x)/6.2831853*float(size.x)*4.0;}
float ridgeRise(float row,ivec2 size) {return ridgeShrink*(skyHorizon()-row/float(size.y))/skyScale();}
float paintedRow(vec3 p,ivec2 size) {return (skyHorizon()-p.z/max(length(p.xy),1.0)/ridgeShrink*skyScale())*float(size.y);}
float floorHeight() {return -c.skyExtent.w*c.skyShape.x;}
// A karst tower's height (0-1) at s of the way from its middle depth out to
// its front or back: a round cap, steepest halfway down, and feet that flare
// out toward the eye (straight walls all at one distance read as cardboard).
// (sky_light.cpp builds the same.)
float towerProfile(float s) {s=saturate(s);return (1.0-s)*(1.0-s)*(1.0+2.0*s);}
// A column's painted crest on a ring, as a rise, and the smoothed crest its
// face is shaped from (sky_layers.cpp).
float paintedRise(int x,int ring,ivec2 size) {return ridgeRise(texelFetch(layers,ivec2(wrapColumn(x,size.x),size.y),0)[ring==0?0:1]*255.0,size);}
float shapedRise(int x,int ring,ivec2 size) {return ridgeRise(texelFetch(layers,ivec2(wrapColumn(x,size.x),size.y),0)[ring==0?2:3]*255.0,size);}
// The ground at q (relative to the reference eye) among a ring's towers (ring
// radius r): the tower whose slice q's direction from the reference eye falls
// in, where q lies within its depth there, else the floor. Its face is shaped
// from the smoothed crest, so neighbouring columns run on as one rock face,
// and cut off at the painted crest of the column, as far off as it stands,
// so its outline from the reference eye is the painting's. Its feet reach
// out with its height, furthest under its peak. All slices also carry on past their saddles
// as skirts (sky_layers.cpp's crestSkirts), falling away, cut off at the
// painted crest so the reference eye never sees them; where a mountain
// stands in front of the next (sky_overlap.cpp) its skirt is its own body,
// no falling away, from the row it starts at in that column. The rock's
// shade (sky_relief.cpp, the mask's upper bits) sets its face back, so the
// painted crevices are grooves: at z, the texel the reference eye sees
// there. which: the tower's middle depth, its reach toward and away there
// (negative: a mountain in front, its top row from its top), its top, and
// the column (top -1e30: the floor).
float rockShade(int x,vec2 q,float z,ivec2 size) {
    float row=clamp(paintedRow(vec3(q,z),size),0.0,float(size.y)-1.0);
    uint mask=uint(round(texelFetch(layers,ivec2(wrapColumn(x,size.x),int(row)),0).a*255.0));
    return (mask&2u)!=0u?float(mask>>2u)/63.0:0.0;   // in the sky, those bits are a hanging hell's tendrils
}
// A painted city's buildings, its sky and smoke (sky/city.glsl, included
// after this file).
float buildingGround(vec2 q,float r,int ring,ivec2 size,out vec4 which);
vec3 buildingColor(vec3 p,vec3 o,int ring,ivec2 size,vec4 which);
vec3 citySkyColor(float rise);
vec3 hellOverhead();
vec3 cityHeavens(vec3 o,vec3 ray,ivec2 size);
vec3 citySmoke(vec3 color,vec3 o,vec3 ray,ivec2 size);
// Hell's writhing sky (sky/hell.glsl) and a spire in an open sky
// (sky/spire.glsl), both included after this file too.
bool hellSky();
vec3 hellSkyColor(vec2 uv,ivec2 size);
vec3 hellSample(vec2 uv,ivec2 size);
bool spireSky();
float traceSpire(vec3 o,vec3 ray,ivec2 size);
vec3 spireColor(vec3 p,vec3 o,ivec2 size);
float towerGround(vec2 q,float z,float r,int ring,ivec2 size,out vec4 which) {
    if(citySky())return buildingGround(q,r,ring,size,which);
    float set=ring==0?1.0-0.25*rockShade(int(floor(aroundU(q,size))),q,z,size):1.0;
    int x=wrapColumn(int(floor(aroundU(q,size))),size.x);
    float best=floorHeight(),rho=length(q),painted=paintedRise(x,ring,size);
    which=vec4(0.0,0.0,-1e30,float(x));
    vec4 t=texelFetch(layers,ivec2(x,size.y+2+ring),0);
    if(t.a>0.0) {
        float depth=(0.5+t.a*1.5)*r,along=(t.r*255.0-128.0)/127.0,top=painted*depth,shape=shapedRise(x,ring,size)*depth;
        float reach=footReach*(shape-floorHeight())*(0.55+0.45*sqrt(max(1.0-along*along,0.0)))*set;
        float u=abs(rho-depth)/max(reach,1.0);
        if(top>floorHeight()&&u<1.0) {best=min(floorHeight()+(shape-floorHeight())*towerProfile(u),top);which=vec4(depth,reach,top,float(x));}
    }
    for(int k=0;k<2;++k) {
        vec4 s=texelFetch(layers,ivec2(x,size.y+(ring==0?6:4)+k),0);
        if(s.a==0.0)break;
        float depth=(0.5+s.a*1.5)*r,edge=ridgeRise(round(s.b*255.0),size)*depth;
        bool front=s.g==0.0;
        float beyond=front?0.0:abs(round(s.r*255.0)-128.0)/round(s.g*255.0);
        float top=min(floorHeight()+(edge-floorHeight())*towerProfile(beyond),painted*depth);
        if(top<=best)continue;
        float reach=footReach*(top-floorHeight())*0.55*(front?set:1.0),u=abs(rho-depth)/max(reach,1.0);
        if(u>=1.0)continue;
        float height=floorHeight()+(top-floorHeight())*towerProfile(u);
        if(height>best) {best=height;which=vec4(depth,front?-reach:reach,top,float(x));}
    }
    return best;
}
// Where a ray from o along d leaves the circle of radius r round the
// reference eye (-1: it never meets it).
float circleExit(vec2 o,vec2 d,float r) {
    float a=dot(d,d),b=dot(o,d),k=dot(o,o)-r*r,disc=b*b-a*k;
    if(disc<0.0||a<1e-8)return -1.0;
    return (-b+sqrt(disc))/a;
}
// How steep a rise from the reference eye clears every rock of a ring over
// count columns from first on (sky_rock_limit.cpp): any stretch of them is two
// overlapping runs of a power of two, each one texel of its row.
float rockLimit(int first,int count,int ring,ivec2 size) {
    count=min(count,size.x);
    int level=findMSB(count);
    vec4 a=texelFetch(layers,ivec2(wrapColumn(first,size.x),size.y+8+level),0);
    vec4 b=texelFetch(layers,ivec2(wrapColumn(first+count-(1<<level),size.x),size.y+8+level),0);
    return (ring==0?max(a.r*256.0+a.g,b.r*256.0+b.g):max(a.b*256.0+a.a,b.b*256.0+b.a))*255.0/65535.0*8.0;
}
// Whether the way from a to b stands clear over every rock of a ring. All rock
// under a column stays under its limit's cone round the reference eye, and the
// inside of a cone is convex: a straight way with both ends in it never leaves.
// Open sky costs this, not a march that finds nothing.
bool overRock(vec3 a,vec3 b,int ring,ivec2 size) {
    float from=aroundU(a.xy,size),turn=4.0*float(size.x),way=aroundU(b.xy,size)-from;
    way-=turn*round(way/turn);
    int first=int(floor(min(from,from+way)))-1,last=int(floor(max(from,from+way)))+1;
    float limit=rockLimit(first,last-first+1,ring,size);
    return a.z>=limit*length(a.xy)&&b.z>=limit*length(b.xy);
}
// The first point along the ray on a ring's towers or the floor among them,
// as the distance along it (-1: none), marched across the band the towers
// can stand in (tallest: the highest top), then narrowed down.
float traceTowers(vec3 o,vec3 d,float r,float tallest,int ring,ivec2 size,int steps,out vec4 which) {
    float reach=footReach*(tallest-floorHeight());
    float t0=max(circleExit(o.xy,d.xy,max(0.85*r-reach,1.0)),0.0),t1=circleExit(o.xy,d.xy,1.25*r+reach);
    which=vec4(0.0,0.0,-1e30,0.0);
    if(t1<=t0)return -1.0;
    if(overRock(o+d*t0,o+d*t1,ring,size))return -1.0;
    float dt=(t1-t0)/float(steps),last=t0;
    for(int i=1;i<=steps;++i) {
        float t=t0+dt*float(i);
        vec3 p=o+d*t;
        if(p.z<towerGround(p.xy,p.z,r,ring,size,which)) {
            float lo=last,hi=t;
            for(int j=0;j<6;++j) {float m=(lo+hi)*0.5;vec3 q=o+d*m;if(q.z<towerGround(q.xy,q.z,r,ring,size,which))hi=m;else lo=m;}
            towerGround((o+d*hi).xy,(o+d*hi).z,r,ring,size,which);
            return hi;
        }
        last=t;
    }
    return -1.0;
}
// The colour the air between takes: the painting's own haze (the mist's).
vec3 hazeColor(ivec2 size) {return layerColor(texelFetch(layers,ivec2(0,size.y+1),0).b);}
// How much the weather thickens the air between the towers: fog, dry ice,
// sand and ash as hard as they come, rain and snow a little.
float thickAir() {
    float kind=c.weather.w,k=0.0;
    if(kind==WEATHER_FOG||kind==WEATHER_DRY_ICE)k=1.0;
    else if(kind==WEATHER_SAND||kind==WEATHER_ASH||kind==WEATHER_FIRESTORM)k=0.8;
    else if(kind==WEATHER_RAIN||kind==WEATHER_NUKAGE||kind==WEATHER_SNOW)k=0.3;
    return k*saturate(c.weather.x*2.0);
}
// How far down the towers snow lies (0 none, 1 to the floor), built up and
// melted slowly on the CPU (sky_layers.cpp's lyingSnow).
float snowLine() {return saturate(c.skyLayers.w-1.0);}
// A layer of the painting (channel ch: 0 sky, 1 far, 2 near) at painting
// position uv. A cloud sky's engine-made sky is smooth, filtered between its
// four nearest texels (those the layer doesn't cover, by mask bit, counting
// as the nearest one that does); a painting kept on mountains is shown in
// its own texels: filtered, it looked bilinearly upscaled.
vec3 layerBlend(vec2 uv,int ch,uint mask,ivec2 size) {
    if(!cloudSky()) {
        vec4 t=texelFetch(layers,ivec2(wrapColumn(int(floor(uv.x)),size.x),clamp(int(floor(uv.y)),0,size.y-1)),0);
        return layerColor(t[ch==0?0:ch==1?1:2]);
    }
    vec2 q=uv-0.5,i=floor(q),f=q-i;
    vec3 c4[4];bool ok[4];int any=-1;
    for(int k=0;k<4;++k) {
        ivec2 at=ivec2(wrapColumn(int(i.x)+(k&1),size.x),clamp(int(i.y)+(k>>1),0,size.y-1));
        vec4 t=texelFetch(layers,at,0);
        ok[k]=mask==0u||(uint(round(t.a*255.0))&mask)!=0u;
        c4[k]=layerColor(t[ch==0?0:ch==1?1:2]);
        if(ok[k]&&any<0)any=k;
    }
    if(any<0)return c4[0];
    for(int k=0;k<4;++k)if(!ok[k])c4[k]=c4[any];
    return mix(mix(c4[0],c4[1],f.x),mix(c4[2],c4[3],f.x),f.y);
}
// The baked light (sky_light.cpp, four paintings round; the lower half where
// snow can lie) at painting position uv of the four, a texel's own.
vec4 lightBlend(vec2 uv,int lower,ivec2 size) {
    return texelFetch(layerLight,ivec2(wrapColumn(int(floor(uv.x)),4*size.x),clamp(int(floor(uv.y)),0,size.y-1)+lower*size.y),0);
}
// A tower texel (row y, on ring's towers at p; fromTop rows under its
// painted crest) lit as sky_light.cpp baked it for the sun as it stands,
// four paintings round: by the sun (with the haze's scattered light) as
// strongly as it shines now, warm, the far towers less through the air
// between, and by the sky, cool in the sky's own colour; snow where the
// weather lays it, the rock dark with wet in rain; dusted by its distance
// from the eye (o), the air thicker in fog, sand and ash, and the valley air
// thickest low down over the feet. Facing the sun, the rim of every crest
// catches the light the haze scatters forward round it.
// The shadow of what's overhead on p: the clouds you can see.
float towerShadow(vec3 p) {
    vec3 s=sunDir();
    if(!fieldSky()||s.z<=0.02)return cloudShadow(p);
    return fieldBlock(p.xy+s.xy*(cloudDeck*c.skyShape.x-p.z)/s.z);
}
vec3 litTower(vec3 color,float y,float fromTop,int ring,vec3 p,vec3 o,ivec2 size) {
    vec2 at=vec2(aroundU(p.xy,size),y);
    vec4 baked=lightBlend(at,0,size)*1.5,snowBake=lightBlend(at,1,size);
    vec2 light=ring==0?baked.rg:baked.ba,snowAt=ring==0?snowBake.rg:snowBake.ba;
    float on=(ring==0?0.85:0.5)*saturate(c.sun.w*1.5);
    vec3 skyTint=mix(vec3(1.0),c.fx.yzw/max(max(c.fx.y,c.fx.z),max(c.fx.w,1e-3)),0.3);
    vec3 sunTint=mix(vec3(1.0),c.sun.rgb/max(max(c.sun.r,c.sun.g),max(c.sun.b,1e-3)),0.6);
    vec3 lit=mix(light.y*skyTint,0.45*light.y*skyTint+0.95*light.x*(1.0-0.65*towerShadow(p))*sunTint,on);   // the deck's shadows sliding over (sky/clouds.glsl)
    // Rain darkens the rock; snow lies on the flat ground high enough up,
    // ragged per texel, in the colour of what settles.
    float wet=c.weather.w==WEATHER_RAIN||c.weather.w==WEATHER_NUKAGE?saturate(c.weather.x*2.0):0.0;
    color*=1.0-0.25*wet;
    float line=snowLine(),ragged=float(pcgHash(uint(int(floor(at.x)))*977u^uint(int(floor(at.y)))*7919u^uint(ring))&255u)/255.0;
    // A dusting, not a coat: thick bright snow stood out crisp and read as
    // close; thin, on the flattest ground only, half the rock showing
    // through and already greyed by the air, it stays far off.
    if(line>0.0) {
        float lies=step(0.3,snowAt.x-0.5*ragged)*step(0.0,snowAt.y-(1.0-line)-0.15*(ragged-0.5));
        color=mix(color,mix(c.settleColor.rgb,hazeColor(size),0.35),(ring==0?0.5:0.35)*lies);
    }
    color*=lit*rockClouds(p,ring);
    // The rock stands in the sky's light, not a day of its own: its shade takes
    // that sky's colour, and its crests the glow behind them.
    if(fieldSky()&&fieldBank()>0.0) {
        vec3 skyLight=fieldRamp(0.72);
        skyLight/=max(max(skyLight.r,skyLight.g),max(skyLight.b,1e-3));
        color=mix(color,color*skyLight*1.3,0.45*fieldBank());
        color+=fieldRamp(0.85)*0.22*fieldBank()*saturate(1.0-fromTop/4.0);
    }
    vec3 s=sunDir();
    float rim=saturate(1.0-fromTop/2.5)*pow(saturate(dot(normalize(p-o),s)),3.0)*sunOn();
    color+=c.sun.rgb*rim*(ring==0?0.35:0.2);
    // The dust by the ring's own distance, a little of each tower's real
    // depth in it: by that alone, neighbouring slices at different depths
    // hazed by different amounts, a seam down a mountain in thick weather.
    float ringDistance=length(p.xy)>1.0?length(p.xy/length(p.xy)*(ring==0?c.skyShape.x:c.skyShape.z)-o.xy):0.0;
    float thick=thickAir();
    float dust=1.0-exp(-mix(ringDistance,length(p-o),0.25)/(mix(3.5,1.1,thick)*c.skyShape.x)),low=mix(0.55,0.8,thick)*exp(-(p.z-floorHeight())/(mix(0.1,0.2,thick)*c.skyShape.x));
    // A ridge under a sky of clouds is all the ground that sky has: it keeps
    // its colour, where a far range among mountains melts into the mist.
    float air=1.0-(1.0-dust)*(1.0-low);
    // Rock that keeps its painted shading stands hard against the sky, its
    // crevices black: the art's own contrast, and a little more for what the
    // light took.
    if(fieldSky()&&cloudSkyTexel(14).a>0.5)return mix(saturate((color-0.28)*1.45+0.28),hazeColor(size),(0.2+cloudSkyTexel(14).b)*air);
    if(fieldSky())air=saturate(air+cloudSkyTexel(14).b*(1.0-air));
    return mix(color,hazeColor(size),cloudSky()?0.4*air:air);
}
// The painting's texel for point p on a tower (which, from towerGround).
// Its column is the one its direction from the reference eye falls in; its
// row is how far p lies under the tower's top, or out from it where the
// ground is flatter than 45 degrees (in painted rows at the tower's depth).
// Down the faces the painting keeps its proportions; over the cap, which
// the reference eye sees edge on and so in a row or two, it walks on down
// the painting instead of smearing one row across the top. The floor is
// haze.
vec3 towerColor(vec3 p,vec3 o,int ring,ivec2 size,vec4 which) {
    if(citySky())return buildingColor(p,o,ring,size,which);
    if(which.z<-1e29)return hazeColor(size);
    int x=int(which.w);
    // A mountain in front starts at its own row (its top), not the column's crest.
    float covered=which.y<0.0?(skyHorizon()-which.z/which.x/ridgeShrink*skyScale())*float(size.y):texelFetch(layers,ivec2(x,size.y),0)[ring==0?0:1]*255.0;
    float rowHeight=which.x*ridgeShrink/(skyScale()*float(size.y)),fromCrest=max(which.z-p.z,abs(length(p.xy)-which.x));
    float y=floor(clamp(covered+fromCrest/rowHeight,covered,float(size.y)-0.5));
    vec2 uv=vec2(float(x)+0.5,y+0.5);
    vec3 color=layerBlend(uv,ring==0?2:1,ring==0?2u:1u,size);
    return litTower(color,y+0.5,y-covered,ring,p,o,size);
}
// The valley mist on its ring at p: the band's ragged top and its churn
// fixed to the world and carried slowly by the level's wind, half as fast as
// the clouds above it, a step per tic, in whole cells; its density in
// quarter steps toward its colour (smooth, it looked bilinearly upscaled).
vec3 ringMist(vec3 color,vec3 p,ivec2 size) {
    vec4 mist=texelFetch(layers,ivec2(0,size.y+1),0);
    // Fog and the like raise it and thicken it.
    float level=mist.r*255.0-mist.g*255.0*1.5*thickAir(),ramp=mist.g*255.0*(1.0+thickAir());
    vec2 m=floor(vec2(aroundU(p.xy,size),paintedRow(p,size)));
    float cell=radPerColumn(size)*c.skyShape.y;
    vec2 g=floor((p.xy-windWay()*lowWind()*0.5*cloudTics())/cell);
    float edge=0.65*cloudValue(g/32.0,11u)+0.35*cloudValue(g/8.0,23u);
    float top=level+ramp*(edge-0.5);
    if(m.y<top-ramp*0.5)return color;
    float d=(m.y-top)/ramp+0.7*(cloudValue3(g/8.0,m.y/3.0,37u)-0.5);
    return mix(color,layerColor(mist.b),clamp(floor(d*4.0),0.0,4.0)/4.0);
}
// Clouds on their ring at p, flowing between the towers at about half their
// height: puffs of two sizes drifting with the wind a tic at a time, whole
// cells, quarter steps of density, as many as the weather brings
// (cloudCover) and lit as the deck is (sky/clouds.glsl's cloudColor), their
// tops above the band's middle, their bellies under it.
vec3 ringClouds(vec3 color,vec3 p,ivec2 size,vec3 ray) {
    vec4 mist=texelFetch(layers,ivec2(0,size.y+1),0);
    float level=mist.r*255.0,ramp=mist.g*255.0;
    vec2 m=floor(vec2(aroundU(p.xy,size),paintedRow(p,size)));
    float band=level-0.9*ramp,spread=1.1*ramp;   // above the valley mist, below the far tops
    float envelope=saturate(1.0-abs(m.y-band)/spread);
    if(envelope<=0.0)return color;
    // Fixed to the world and carried by the level's wind (sky/clouds.glsl),
    // in cells about a texel wide where they stand: drifting across the
    // view where the wind blows across it, toward or away where it blows
    // along it.
    float cell=radPerColumn(size)*c.skyShape.w;
    vec2 g=floor((p.xy-windWay()*lowWind()*cloudTics())/cell);
    float puff=0.62*cloudValue3(g/16.0,m.y/5.0,51u)+0.38*cloudValue3(g/8.0,m.y/2.5,67u);
    // Heavier weather brings a few more of them, never a filled band: filled,
    // its ragged top stood in front of the far towers as one more range.
    float d=clamp(floor((puff*envelope*1.7-0.62+(cloudCover()-0.32)*0.25)*10.0),0.0,4.0)/4.0;
    if(d<=0.0)return color;
    // Over the sky's haze, not over what stands behind: before a mountain
    // an overcast cloud darker than the rock lay on it as a shaded strip.
    vec3 haze=hazeColor(size);
    // Under overcast they all but go, and never darker than the haze: a
    // darker band of them, ragged on top, stood before the far towers as a
    // range that wasn't there. The grey lid of the deck covers the sky then.
    float grey=overcastDeck();
    // Under a red sky they are smoke in the valley's haze: lit in its reds
    // they stood in front of the dark ridge as holes into the sky.
    if(redClouds())return mix(color,mix(haze,paintColor(cloudSkyTexel(5).g),0.35),d*0.8);
    vec3 cloud=max(cloudColor(haze,haze,saturate(0.5+(band-m.y)/spread),d,ray),mix(haze,vec3(1.0),0.1));
    return mix(color,mix(cloudColor(haze,haze,saturate(0.5+(band-m.y)/spread),d,ray),cloud,grey),d*(1.0-0.9*grey));
}
// The sky's own layer (its hidden part filled) at uv, filtered: hell's
// writhing (sky/hell.glsl) or as it is.
vec3 skyLayer(vec2 uv,ivec2 size) {return hellSky()?hellSkyColor(uv,size):layerBlend(uv,0,0u,size);}
// The sky along ray (uv: where the painting shows it). Above the painting's
// top it turns into a ceiling of its own clouds overhead, its upper half laid
// flat in perspective, mirrored at its edges: stretched up, the top row stood
// in streaks, and the haze that hid them lay over the sky as a band of fog;
// mirrored up the cylinder, each row closed into a ring round the zenith and
// looking up was a tunnel.
vec3 depthSky(vec2 uv,ivec2 size,vec3 ray) {
    float h=float(size.y);
    if(citySky())return citySkyColor((skyHorizon()-uv.y/h)/skyScale());
    // A painting of clouds comes round four times a turn, and clouds, unlike
    // mountains, show it: a slow bend over the whole turn makes each copy its
    // own (whole turns of noise, so no seam).
    if(cloudSky()) {
        float turn=uv.x/(4.0*float(size.x));
        uv+=vec2(44.0,10.0)*(vec2(layerNoise(turn*7.0,7,901u),layerNoise(turn*5.0,5,907u))-0.5);
    }
    // Hell's art is faces, and mirrored they were a mess: it ends at its top
    // edge, in a deep glow the smoke's lid lies over (sky/cloud_volume.glsl).
    if(hellSky()) {
        float end=ray.z>0.0?smoothstep(0.1*h,-0.02*h,uv.y):0.0;
        return mix(end<1.0?hellSkyColor(vec2(uv.x,max(uv.y,0.5)),size):vec3(0.0),hellOverhead(),end);
    }
    vec3 color=skyLayer(vec2(uv.x,abs(uv.y)),size);   // mirrored once up to where the ceiling has taken over
    float overhead=ray.z>0.0?smoothstep(0.08*h,-0.25*h,uv.y):0.0;
    if(overhead<=0.0)return color;
    vec2 q=ray.xy/max(ray.z,1e-3)*0.5*h,m=mod(q,vec2(4.0*float(size.x),h));
    m.y=m.y<0.5*h?m.y:h-m.y;   // the upper half, mirrored
    // Smooth up there: its texels, stretched overhead, read as blocks.
    vec2 at=vec2(m.x,m.y+0.5);
    return mix(color,hellSky()?hellSkyColor(at,size):hellSample(at,size),overhead);
}
// What the eye sees along ray: the nearest tower it hits, else the sky; over
// the far towers and the sky, the clouds and then the mist on their rings
// between the near towers and the far, never over the near towers. By the
// layer, not the distance: the far towers' flaring feet reach in through
// the mist's ring, and where a foot came before it the mist stopped in a
// hard line, the top of a range that wasn't there. solid: how far along the
// ray the tower stands (1e30 none), for the cloud deck sky.frag lays over it.
vec3 skyDepth(vec3 ray,vec2 uv,out float solid) {
    ivec2 size=depthSize();
    vec3 o=c.eye.xyz-c.skyLayers.xyz,color=citySky()?cityHeavens(o,ray,size):depthSky(uv,size,ray);
    if(fieldSky()&&!hellSky()&&!citySky())color=volumeBackdrop(color,ray);   // hell's art is its sky, the city's its fires' glow
    vec4 which;
    solid=1e30;
    bool near=false;
    // A spire stands before the clouds (but its top up in the deck is
    // the deck's to cover: no solid there). The painted field goes by real
    // distance instead: what lies nearer than the spire crosses in front.
    if(spireSky()) {
        float s=traceSpire(o,ray,size);
        if(s>0.0) {vec3 p=o+ray*s;solid=fieldSky()||p.z<cloudDeck*c.skyShape.x?s:1e30;return spireColor(p,o,size);}
    }
    // Fine steps: buildings are thin from the side, and the far ridge seen at
    // a slant let the sky through holes where coarse steps passed over it.
    float t=traceTowers(o,ray,c.skyShape.x,c.skyExtent.x*c.skyShape.x,0,size,128,which);
    if(t>0.0) {color=towerColor(o+ray*t,o,0,size,which);solid=t;near=true;}
    else if(c.skyExtent.z>0.0) {
        t=traceTowers(o,ray,c.skyShape.z,c.skyExtent.y*c.skyShape.z,1,size,citySky()?128:192,which);
        if(t>0.0) {color=towerColor(o+ray*t,o,1,size,which);solid=t;}
    }
    // Green air lies over the rock too: red pillars stood out of it as if pasted,
    // and over hell's flames, which are its sky.
    if(fieldSky()&&(solid<1e29||hellSky())&&c.weather.w==WEATHER_NUKAGE)color=mix(color,weatherTinted(color),0.7);
    // Without mountains (a cloud sky) there is no valley between them for
    // low cloud and mist to drift through: circling the horizon alone, they
    // looked like they didn't belong.
    // A sky with low banks: a sea of them behind the near mountains, hiding what
    // the fill made of the sky there, and a few across their feet.
    if(fieldSky()&&fieldBank()>0.0) {
        float behind=circleExit(o.xy,ray.xy,1.5*c.skyShape.x),before=circleExit(o.xy,ray.xy,0.62*c.skyShape.x);
        if(!near&&behind>0.0&&behind<solid)color=volumeBankOver(color,o+ray*behind,ray,0.98*fieldBank(),0.3,0u);
        if(before>0.0&&before<solid)color=volumeBankOver(color,o+ray*before,ray,0.72*fieldBank(),0.22,1u);
        return color;
    }
    if(near||cloudSky()||citySky()||hellSky())return color;   // no valley mist in hell: pale, it lay across the fire
    float clouds=circleExit(o.xy,ray.xy,c.skyShape.w);
    if(clouds>0.0)color=ringClouds(color,o+ray*clouds,size,ray);
    float m=circleExit(o.xy,ray.xy,c.skyShape.y);
    if(m>0.0)color=ringMist(color,o+ray*m,size);
    return color;
}
