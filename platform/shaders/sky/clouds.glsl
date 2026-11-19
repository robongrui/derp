// Sky depth's clouds (scene3d/sky_layers.cpp, sky/depth.glsl), in the three
// layers weather has: low cloud and valley mist between the towers (depth.
// glsl's rings), a middle deck cloudDeck near-ring radii over the reference
// eye, and high cirrus far above it (cirrusOver). All fixed to the world and
// carried by the level's steady wind (c.skyWind), faster the higher, a step
// per tic. The deck throws its shadows on the towers; how much of the sky it
// fills is the weather's (cloudCover): scattered patches in fair weather, a
// grey sheet as it closes in. Lit as clouds are: a high sun lights their
// tops, a low one their bellies, warm; thin edges glow when you look toward
// the sun; under overcast the sun's light is gone and they lie flat and
// grey. Smooth, the sky being a photograph, not pixel art.
const float cloudDeck=0.75;
// Where the painting stands (scene3d/internal.h's skyHorizon): its row
// fraction v shows at rise (skyHorizon()-v)/skyScale(). Mirrors have the plain
// sky, as painted.
float skyHorizon() {return c.skyLayers.w>0.0&&c.skyWind.w>0.0?c.skyWind.w:0.70;}
float skyScale() {float h=skyHorizon();return h==0.70?0.68:h*(0.68/0.70);}
// A sky of clouds alone (sky_layers.cpp's bakeCloudSky): its painted clouds
// are taken out and the engine's take their place, rebuilt as the weather
// the painting shows (cloudRecipe), in its own colours (its dark, light and
// middle cloud) and as much of the sky as the painting had of them.
const int layerRows=20;   // sky_layers.cpp's skyLayerRows: rows of settings and rock limits under the painting
ivec2 paintSize() {ivec2 s=textureSize(layers,0);return ivec2(s.x,s.y-layerRows);}
vec4 cloudSkyTexel(int i) {return texelFetch(layers,ivec2(i,paintSize().y+1),0);}
#ifdef SKY_CLOUD
bool cloudSky() {return SKY_CLOUD!=0;}   // a sky variant built for one kind (gpu3d's skyTwin)
#else
bool cloudSky() {return cloudSkyTexel(1).a>0.5/255.0;}
#endif
// A painted city (sky_city.cpp): its painted smoke is its sky, so the
// engine's clouds stay away from it.
#ifdef SKY_CITY
bool citySky() {return SKY_CITY!=0;}   // a sky variant built for one kind (gpu3d's skyTwin)
#else
bool citySky() {return cloudSkyTexel(3).r>0.5/255.0;}
#endif
// A red sky painted as clouds over rock (sky_hell.cpp's second kind):
// its clouds in its own reds (texel 5: dark, bright, middle).
bool redClouds() {return round(cloudSkyTexel(5).r*255.0)==2.0;}
// 0 not a cloud sky; 1 wind-sheared streaks, 2 broken cloud with sunlit
// rims, 3 an overcast with a bright break low down.
int cloudRecipe() {return int(round(cloudSkyTexel(1).a*255.0));}
vec3 paintColor(float index) {return texelFetch(palette,ivec2(int(round(index*255.0)),0),0).rgb;}
uint cloudHash(ivec2 p,uint seed) {return pcgHash(uint(p.x)*0x8DA6B343u^pcgHash(uint(p.y)^seed));}
float cloudValue(vec2 p,uint seed) {
    ivec2 i=ivec2(floor(p));vec2 f=fract(p);f=f*f*(3.0-2.0*f);
    float a=float(cloudHash(i,seed)&0xFFFFu),b=float(cloudHash(i+ivec2(1,0),seed)&0xFFFFu);
    float d=float(cloudHash(i+ivec2(0,1),seed)&0xFFFFu),e=float(cloudHash(i+ivec2(1,1),seed)&0xFFFFu);
    return mix(mix(a,b,f.x),mix(d,e,f.x),f.y)/65535.0;
}
vec3 sunDir() {return vec3(cos(c.fx2.z)*cos(c.outdoor.w),sin(c.fx2.z)*cos(c.outdoor.w),sin(c.outdoor.w));}
float sunOn() {return saturate(c.sun.w*1.5);}   // falls under overcast (scene.cpp's overcast)
float lowSun() {return 1.0-smoothstep(0.08,0.45,c.outdoor.w);}
// How much of the sky is cloud: a third on a clear day, most of it while it
// rains, snows or ash falls, nearly all of it in fog, less in a sandstorm
// (dust, not cloud), as hard as the weather comes down.
float cloudCover() {
    float kind=c.weather.w,heavy=0.3;
    if(kind==WEATHER_RAIN||kind==WEATHER_NUKAGE)heavy=c.skyState.w>0.0?0.42:0.8;   // a sunshower: broken
    else if(kind==WEATHER_SNOW)heavy=0.75;
    else if(kind==WEATHER_ASH||kind==WEATHER_FIRESTORM)heavy=0.85;
    else if(kind==WEATHER_SAND)heavy=0.45;
    else if(kind==WEATHER_FOG)heavy=0.9;   // dry ice lies under a clear sky
    float fair=cloudSky()?cloudSkyTexel(2).r:0.32;
    return mix(fair,max(heavy,fair),saturate(c.weather.x*2.0));
}
// How overcast the deck is: 0 scattered fair-weather cloud, 1 a grey lid.
float overcastDeck() {return saturate((cloudCover()-0.5)/0.35);}
// The level's steady wind (c.skyWind): where it blows, and how fast it
// carries the low clouds between the towers, in units a second; a light
// drift even in still air down here, more aloft.
// Where the high clouds are seen from: the eye moved only a tenth as far
// from the map's middle as it is. Shifting against the mountains as you
// walked, the deck read as near; it drifts on the wind all the same.
vec3 highEye() {return (c.eye.xyz-c.skyLayers.xyz)*vec3(0.1,0.1,1.0);}
// How much air lies between the eye and a high cloud t along the way, past
// the layer's own height h: overhead next to none, toward the horizon most;
// fade is how far a layer's own haze reaches (near-ring radii).
float airHaze(float t,float h,float fade) {return 1.0-exp(-max(t-h,0.0)/(fade*c.skyShape.x));}
vec2 windWay() {return dot(c.skyWind.xy,c.skyWind.xy)>1e-6?normalize(c.skyWind.xy):vec2(0.94,0.34);}
float lowWind() {return 30.0+900.0*c.skyWind.z*c.skyWind.z;}
float cloudSeconds() {return c.fog.w;}   // smooth, for the high clouds over the open sky
float cloudTics() {return floor(c.fog.w*35.0)/35.0;}   // a step per tic, for the low clouds and mist among the mountains
// How fast the deck goes, in near-ring radii a second, whatever the map's size.
// Clouds that high hardly seem to move: a stiff breeze takes it a quarter of a
// degree across the sky a second, a gale one; in still air it all but stands.
float cloudSpeed() {return 0.00025+0.013*c.skyWind.z*c.skyWind.z;}
// The deck's drift so far, in near-ring radii.
vec2 cloudDrift() {return windWay()*cloudSpeed()*cloudSeconds();}
// Value noise over a plane and a third coordinate (rows), for the clouds
// between the towers.
float cloudValue3(vec2 p,float r,uint seed) {
    float i=floor(r),f=r-i;f=f*f*(3.0-2.0*f);
    return mix(cloudValue(p,seed+uint(int(i))*977u),cloudValue(p,seed+uint(int(i)+1)*977u),f);
}
// The deck's density at q (reference eye space): whole cloud texels, about
// a sky texel across overhead, smooth.
float deckDensity(vec2 q) {
    int recipe=cloudRecipe();
    if(recipe==1) {
        // Wind-sheared streaks: whole square cells, the noise drawn out
        // eightfold along the wind, thin and fibrous across it (drawn-out
        // cells read as stacked blocks).
        vec2 way=windWay(),across=vec2(-way.y,way.x);
        vec2 at=vec2(dot(q,way),dot(q,across))-vec2(dot(cloudDrift(),way),0.0)*c.skyShape.x;
        vec2 s=at/(0.008*c.skyShape.x);
        float n=0.55*cloudValue(s/vec2(48.0,6.0),71u)+0.3*cloudValue(s/vec2(20.0,2.5),83u)+0.15*cloudValue(s/vec2(8.0,1.2),97u);
        // In separate bands with clear sky between, broad across the wind:
        // one even sheet of streaks lay over the sky like a carpet.
        float bands=smoothstep(0.45,0.7,cloudValue(vec2(s.x/160.0,s.y/30.0),61u));
        return saturate((n-(1.0-cloudCover()))*3.0)*bands;
    }
    // Cloud skies' clouds are bigger and softer, as their paintings are; an
    // overcast's closes into a lid but for its break (deckOver).
    // Over mountains too, big enough to be far: small sharp cells read as
    // small things close by.
    float scale=cloudSky()?0.014:0.01,cell=scale*c.skyShape.x;
    vec2 g=q/cell-cloudDrift()/scale;
    if(recipe==3)return saturate(0.75+0.6*(cloudValue(g/18.0,71u)-0.5));

    // Broad shapes and enough small detail that their drift reads as drift,
    // not as cloud forming where it stands; a cloud sky's are big and soft,
    // whole clouds, not a speckled ceiling.
    float n=cloudSky()?0.6*cloudValue(g/18.0,71u)+0.3*cloudValue(g/7.0,83u)+0.1*cloudValue(g/3.0,97u)
                      :0.55*cloudValue(g/40.0,71u)+0.33*cloudValue(g/14.0,83u)+0.12*cloudValue(g/5.0,97u);
    // Overcast closes the gaps: a lid, not a patchwork whose holes of
    // brighter sky read as light blotches.
    return saturate((n-(1.0-cloudCover()))*3.0+overcastDeck()*2.0);
}
// The deck's shadow on p (reference eye space): its density where the way
// from p to the sun passes through it.
float cloudShadow(vec3 p) {
    vec3 s=sunDir();
    if(s.z<=0.02)return 0.0;
    float t=(cloudDeck*c.skyShape.x-p.z)/s.z;
    return t>0.0?deckDensity(p.xy+s.xy*t):0.0;
}
// A cloud texel's colour over the sky behind it (behind) and the painting's
// haze (haze): top 1 its top, 0 its belly; density its thickness; ray the
// way the eye looks. Fair-weather cloud stands whiter than the sky behind
// it; as the weather closes in it turns darker than that sky, thickest
// darkest (taken from the sky itself, which the weather has already dimmed
// by then). A low sun warms the bellies.
vec3 cloudColor(vec3 behind,vec3 haze,float top,float density,vec3 ray) {
    float on=sunOn(),low=lowSun(),lit=mix(top,1.0-top,low),grey=overcastDeck();
    vec3 color=mix(mix(behind,mix(haze,vec3(1.0),0.5),0.55+0.1*top),behind*(0.9-0.12*density),grey);
    if(cloudSky()) {
        // The painting's own clouds: light where they're thin or lit,
        // dark where they're thick.
        vec4 k=cloudSkyTexel(1);
        vec3 dark=paintColor(k.r),light=paintColor(k.g),middle=paintColor(k.b);
        int recipe=cloudRecipe();
        // An overcast is a lid no sun gets through: its dark grey, a little
        // lighter where thin.
        if(recipe==3)return mix(dark,middle,0.4*(1.0-density));
        if(recipe==2) {
            // Broken cloud: thick cores in its middle tone (its darkest
            // stood in them as black holes), thin edges in the warm light a
            // low sun gave them in the painting.
            color=mix(middle,light,saturate(0.25+0.55*top-0.3*density));
            color=mix(color,paintColor(cloudSkyTexel(2).b),0.75*(1.0-density));
        } else color=mix(dark,light,saturate(0.3+0.55*top-0.35*density));
    } else if(redClouds()) {
        // A red sky's: its middle red, lit toward its brightest, thick
        // cores toward its darkest; under overcast a dark red lid.
        vec4 k=cloudSkyTexel(5);
        vec3 dark=paintColor(k.g),light=paintColor(k.b),middle=paintColor(k.a);
        color=mix(mix(middle,light,saturate(0.2+0.6*top)),dark,0.55*density);
        color=mix(color,mix(dark,middle,0.3*(1.0-density)),grey);
    }
    color+=c.sun.rgb*lit*on*(0.15+0.4*low)*(1.0-0.7*grey);
    color+=c.sun.rgb*pow(saturate(dot(ray,sunDir())),6.0)*(1.0-density)*on*0.5;
    return min(color,vec3(1.0));
}
// The deck over the open sky (solid: the distance to the towers, 1e30 none),
// seen from below, so its bellies; fading into the haze low down, where its
// texels crowd together. Never over the towers: the deck's plane passes
// under the far ones' tops, and where it came before them it laid its grey
// over every tower above one height and none below, a level line across
// the mountains that read as the top of a range in front of them.
// Where the deck lies along ray from o: a flat plane over mountains; a
// cloud sky's, with nothing to set it against, curves down toward the
// horizon like the sky's own dome (bowed as the earth is, far more), so its
// streaks run on into the distance instead of converging on a flat ceiling.
float deckDistance(vec3 o,vec3 ray) {
    float h=cloudDeck*c.skyShape.x-o.z;
    if(!cloudSky())return ray.z>0.0?h/ray.z:-1.0;
    float a=dot(ray.xy,ray.xy)/(2.0*4.0*c.skyShape.x),disc=ray.z*ray.z+4.0*a*h;
    return a>1e-9?(-ray.z+sqrt(max(disc,0.0)))/(2.0*a):h/max(ray.z,1e-4);
}
vec3 deckOver(vec3 color,vec3 ray,float solid,vec3 haze) {
    if(ray.z<=(cloudSky()?0.0:0.02)||solid<1e29||citySky())return color;   // nothing under the horizon
    vec3 o=highEye();
    float t=deckDistance(o,ray);
    if(t<=0.0)return color;
    float d=deckDensity(o.xy+ray.xy*t);
    // In fog the bank hides the deck; patches of it showing through read
    // as stains.
    float hidden=c.weather.w==WEATHER_FOG||c.weather.w==WEATHER_DRY_ICE?0.7*saturate(c.weather.x*1.25):0.0;
    // A cloud sky's deck reaches down to the horizon, as its painting did.
    float reach=cloudSky()?smoothstep(0.0,0.06,ray.z):smoothstep(0.06,0.3,ray.z);
    int recipe=cloudRecipe();
    float rise=ray.z/max(length(ray.xy),1e-4),breakRise=(skyHorizon()-cloudSkyTexel(2).g*255.0/float(paintSize().y))/skyScale();
    // An overcast opens above its break, where the painting's bright band
    // lies in the sky behind.
    if(recipe==3)d*=smoothstep(breakRise+0.03,breakRise+0.16,rise);
    vec3 dark=paintColor(cloudSkyTexel(1).r);
    // Seen through more air toward the horizon, it takes the sky's colour
    // behind it: the deck is far, not a ceiling.
    if(d>0.0)color=mix(color,mix(cloudColor(color,haze,0.0,d,ray),color,airHaze(t,cloudDeck*c.skyShape.x,2.5)),d*reach*(1.0-hidden));
    if(recipe==3&&abs(rise-breakRise-0.04)<0.09) {
        // Scud: ragged dark fragments low against the break, faster than
        // the lid above them.
        float low=0.25*c.skyShape.x,s=(low-o.z)/ray.z;
        vec2 g=(o.xy+ray.xy*s-cloudDrift()*c.skyShape.x*1.5)/(0.008*c.skyShape.x);
        float frag=0.7*cloudValue(g/5.0,127u)+0.3*cloudValue(g/2.0,139u);
        float edge=1.0-abs(rise-breakRise-0.04)/0.09;
        color=mix(color,dark,0.85*smoothstep(0.66,0.74,frag+0.08*edge)*(1.0-hidden));
    }
    return color;
}
// The high layer: cirrus cirrusDeck near-ring radii up, far over the deck.
// Thin streaks drawn out along its own wind, which aloft blows faster and a
// little turned from the wind below (veering with height): fastest of all,
// yet slowest on screen, being farthest. Translucent and white even when
// all below is grey, and lit by the sun longest: a low sun warms it first
// and most, pink at dusk. Fair weather shows most of it; as the weather
// closes in it thickens into a pale veil, which the deck under it hides.
const float cirrusDeck=2.2;
vec2 highWay() {vec2 w=windWay();float a=0.45;return vec2(w.x*cos(a)-w.y*sin(a),w.x*sin(a)+w.y*cos(a));}
float cirrusDensity(vec2 q) {
    vec2 way=highWay(),across=vec2(-way.y,way.x);
    q-=way*cloudSpeed()*2.0*c.skyShape.x*cloudSeconds();
    // Cells long along the streaks and narrow across them.
    vec2 cell=vec2(0.05,0.008)*c.skyShape.x,g=vec2(dot(q,way),dot(q,across))/cell;
    float n=0.6*cloudValue(g/vec2(6.0,14.0),131u)+0.4*cloudValue(g/vec2(2.0,5.0),151u);
    float veil=overcastDeck(),amount=mix(0.45,0.75,veil);
    float d=saturate((n-(1.0-amount))*2.5)*(0.55+0.45*veil);
    return d;
}
vec3 cirrusOver(vec3 color,vec3 ray,float solid) {
    if(ray.z<=0.03||solid<1e29||citySky())return color;
    vec3 o=highEye();
    float t=(cirrusDeck*c.skyShape.x-o.z)/ray.z;
    if(t<=0.0)return color;
    float d=cirrusDensity(o.xy+ray.xy*t);
    if(d<=0.0)return color;
    float on=sunOn(),low=lowSun();
    vec3 white=cloudSky()?min(paintColor(cloudSkyTexel(1).g)*1.15,vec3(1.0)):redClouds()?min(paintColor(cloudSkyTexel(5).b)*1.1,vec3(1.0)):mix(vec3(0.92),vec3(1.0),on);
    vec3 cirrus=mix(white,c.sun.rgb*1.15,low*on*0.7);
    // Only a little haze: the cirrus stays bright.
    return mix(color,mix(min(cirrus,vec3(1.0)),color,0.6*airHaze(t,cirrusDeck*c.skyShape.x,6.0)),d*0.45*smoothstep(0.05,0.35,ray.z));
}
