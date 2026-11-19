// Hell's sky (scene3d/sky_hell.cpp flags it: a deep red painting over the
// rock): the painting's own sky alive. Doom 2's is flames with faces in them, so
// it moves as flames do: rooted at the ridge, its tips swaying from side to side
// and never climbing, the glow flickering where it stands. Smooth, the sky being
// a photograph. Part of sky.frag, after sky/depth.glsl.
vec4 hellTexel() {return texelFetch(layers,ivec2(5,paintSize().y+1),0);}
float hellKind() {return round(hellTexel().r*255.0);}
// The writhing kind (sky_hell.cpp), and the hanging one: black tendrils from a
// black top (the 2025 Heretic's fourth), its art kept whole the same way.
#ifdef SKY_HELL
bool hellSky() {return SKY_HELL!=0;}   // a sky variant built for one kind (gpu3d's skyTwin)
#else
bool hellSky() {float k=hellKind();return k==1.0||k==3.0;}
#endif
bool hellHangs() {return hellKind()==3.0;}
// A sky lit by what burns rather than the sun: hell's, a red one, a city's, or
// one whose own colours are fire. Night doesn't put it out.
bool skyBurns() {
    vec3 m=fieldRamp(0.6);
    return hellSky()||redClouds()||citySky()||m.r>3.0*m.b;
}
vec3 hellDark() {return paintColor(hellTexel().g);}
vec3 hellBright() {return paintColor(hellTexel().b);}
// The sky layer between its four nearest texels, round the horizon (also
// sky/depth.glsl's ceiling overhead).
vec3 hellSample(vec2 uv,ivec2 size) {
    vec2 q=uv-0.5,i=floor(q),f=q-i;
    vec3 c4[4];
    for(int k=0;k<4;++k) {
        ivec2 at=ivec2(wrapColumn(int(i.x)+(k&1),size.x),clamp(int(i.y)+(k>>1),0,size.y-1));
        c4[k]=layerColor(texelFetch(layers,at,0).r);
    }
    return mix(mix(c4[0],c4[1],f.x),mix(c4[2],c4[3],f.x),f.y);
}
// Noise round the horizon (whole turns of cell texels) and down it, drifting
// up at rise texels a second.
float hellNoise(vec2 uv,float cell,float rise,ivec2 size,uint seed) {
    int period=max(1,int(round(4.0*float(size.x)/cell)));
    vec2 p=vec2(uv.x*float(period)/(4.0*float(size.x)),(uv.y+rise*cloudSeconds())/cell);
    return layerNoise2(p,period,seed);
}
// How dark the art is thereabouts (sky_layers.cpp's blurred copy of it, in the
// far ridge's channel, which this sky has no use for).
float hellShape(vec2 uv,ivec2 size) {
    vec2 q=uv-0.5,i=floor(q),f=q-i;
    float c4[4];
    for(int k=0;k<4;++k)c4[k]=texelFetch(layers,ivec2(wrapColumn(int(i.x)+(k&1),size.x),clamp(int(i.y)+(k>>1),0,size.y-1)),0).g;
    return mix(mix(c4[0],c4[1],f.x),mix(c4[2],c4[3],f.x),f.y);
}
// The flames sway: sideways only, a slow wave going round the horizon as wind
// goes through a fire, nothing at their roots and most at their tips. Its timing
// follows the art's own dark shapes, so all of a face moves together; noise on
// its own tore through them. Kept gentle: how far neighbours part is the
// stretch, and past a third or so the art folds over itself into streaks.
vec2 hellSway(vec2 uv,ivec2 size) {
    float seconds=cloudSeconds(),shape=hellShape(uv,size)*2.0;
    float turn=uv.x/(4.0*float(size.x))*6.2831853*8.0;   // whole waves a turn: no seam
    float wide=hellNoise(uv,96.0,0.0,size,841u)*1.5;
    // Wind fans them: wider and quicker as it rises.
    float wind=c.skyWind.z;
    // Hanging tendrils each move on their own (scene3d/sky_tendrils.cpp: how
    // far a texel swings with its tendril, G, filtered; which tendril, the
    // mask's upper bits): still at the black they hang from, swinging more
    // the further down, each at its own slow pace and phase, reaching down a
    // little as they curl (the texel they show taken from above) and drawing
    // back. The red round each goes with it, fading out before the next.
    if(hellHangs()) {
        ivec2 at=ivec2(wrapColumn(int(floor(uv.x)),size.x),clamp(int(floor(uv.y)),0,size.y-1));
        uint id=uint(round(texelFetch(layers,at,0).a*255.0))>>2u;
        if(id==0u)return vec2(0.0);
        float swing=hellShape(uv,size);
        float phase=float(pcgHash(id*977u)&1023u)/1023.0*6.2831853,pace=0.35+0.25*float(pcgHash(id*7919u)&255u)/255.0;
        float reach=0.5+0.5*sin(seconds*pace*0.7+phase*1.7);
        return swing*vec2((3.4+2.0*wind)*sin(seconds*(pace+0.3*wind)+phase),-1.8*reach);
    }
    float tip=smoothstep(0.8,0.25,uv.y/float(size.y));
    return vec2((3.5+3.0*wind)*tip*sin(seconds*(0.35+0.6*wind)-turn+shape+wide),0.0);
}
// Overhead: the glow its flames burn up into, or the black its tendrils hang from.
vec3 hellOverhead() {return hellHangs()?hellDark()*0.6:mix(hellDark(),hellBright(),0.7);}
vec3 hellSkyColor(vec2 uv,ivec2 size) {
    vec3 color=hellSample(uv+hellSway(uv,size),size);
    float flicker=sin(cloudSeconds()*0.8+hellNoise(uv,32.0,0.0,size,829u)*6.2831853);
    color=mix(color,hellBright(),0.12*max(flicker,0.0));
    return mix(color,hellDark(),0.1*max(-flicker,0.0));
}
// The smoke under hell's sky along ray (solid: how far the rock stands, as
// deckOver's), on the deck's plane and drifting with its wind: dark, its
// thin edges lit in the sky's bright red from below, fading into the sky
// toward the horizon.
vec3 hellSmoke(vec3 color,vec3 ray,float solid) {
    if(ray.z<=0.02||solid<1e29)return color;
    vec3 o=highEye();
    float t=deckDistance(o,ray);
    if(t<=0.0)return color;
    vec2 g=(o.xy+ray.xy*t)/(0.012*c.skyShape.x)-cloudDrift()/0.012;
    float n=0.55*cloudValue(g/30.0,881u)+0.3*cloudValue(g/11.0,883u)+0.15*cloudValue(g/4.0,887u);
    float d=saturate((n-0.48)*3.0);
    if(d<=0.0)return color;
    vec3 smoke=mix(hellDark()*0.45,hellBright()*0.8,0.6*(1.0-d)*(1.0-d));
    return mix(color,mix(smoke,color,airHaze(t,cloudDeck*c.skyShape.x,2.0)),0.8*d*smoothstep(0.02,0.25,ray.z));
}
