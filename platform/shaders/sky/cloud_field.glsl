// The painted cloud field (scene3d/sky_cloud_field.cpp): a sky's own clouds,
// baked from its painting, the weather map sky/cloud_volume.glsl marches.
// Each field texel: R how much cloud, G the column's top, B its base (0-1 up
// the shell), A its tone, the place on the painting's own colour ramp its top
// takes in the sun. Every colour comes off that ramp: light moves a cloud
// along the painting's colours and adds none of its own. Smooth, the sky being
// a photograph.
#ifdef SKY_FIELD
bool fieldSky() {return SKY_FIELD!=0;}   // a sky variant built for one kind (gpu3d's skyTwin)
#else
bool fieldSky() {return cloudSkyTexel(12).r>0.5/255.0;}
#endif
float fieldThickness() {return cloudSkyTexel(12).g*255.0/256.0*c.skyShape.x;}
vec2 fieldFade() {return cloudSkyTexel(12).ba*255.0/128.0;}
float fieldTexelWidth() {return cloudSkyTexel(13).r*255.0/4096.0;}
float fieldStretch() {return cloudSkyTexel(13).g*255.0/16.0;}
float fieldOpacity() {return cloudSkyTexel(13).b*255.0/8.0;}
bool fieldCirrus() {return cloudSkyTexel(13).a>0.5/255.0;}
// How much of the painting shows behind; the rest is clear sky off the ramp.
float fieldBackdrop() {return c.skyCloud.w>0.0?c.skyCloud.w:cloudSkyTexel(14).r;}
// The painting's colours, darkest to brightest, sixteen of them.
vec3 fieldRamp(float x) {
    float at=saturate(x)*15.0;
    int i=min(int(at),14);
    return mix(cloudSkyTexel(16+i).rgb,cloudSkyTexel(17+i).rgb,at-float(i));
}
// Where the field lies under q (reference eye space): its long side along the
// wind, as streaks blow, drifting down it. The flow bends, slowly and for good
// (fixed to the world, the cloud drifting through it): laid out straight, the
// painting's big masses ran overhead like beams.
vec2 fieldAt(vec2 q) {
    vec2 way=windWay(),across=vec2(-way.y,way.x);
    vec2 at=vec2(dot(q,way),dot(q,across))/c.skyShape.x;
    at+=vec2(0.30*sin(1.7*at.y+0.6)+0.11*sin(4.1*at.y+2.0),0.34*sin(1.3*at.x+1.9)+0.13*sin(3.3*at.x+0.4));
    at.x/=1.0+1.6*windTear();   // a gale draws it out along the wind
    at.x-=dot(cloudDrift(),way);
    at+=c.skyCloud.xy;
    return at;
}
vec2 fieldUV(vec2 q) {return fieldAt(q)/(fieldTexelWidth()*vec2(fieldStretch(),1.0)*vec2(textureSize(cloudField,0)));}
// How thick the cloud is at uv: the weather adds to what the painting has,
// and overcast closes it.
float fieldAmount(float painted) {
    float own=c.skyCloud.z+cloudSkyTexel(15).a;
    if(weatherClouds())return mix(saturate(painted+own+weatherCover()),0.85,weatherLid());
    float more=max(cloudCover()-(cloudSky()?cloudSkyTexel(2).r:0.32),0.0);
    return mix(saturate(painted+more+own),0.85,overcastDeck());
}
// The breaks in the deck at (fieldAt): big slow gaps drifting with it, 0 none to 1
// a clear hole. An even sky has nothing else for the sun to come through.
float fieldGap(vec2 at) {
    float share=sunBreaks();
    return share>0.0?share*smoothstep(0.55,0.78,textureLod(cloudNoise,vec3(at*0.3,0.31),0.0).r):0.0;
}
// For sky/rays.glsl and the towers' shadows: how much the field stands in the
// way at q, so the sun comes through where you can see it does.
float fieldBlock(vec2 q) {
    float a=fieldAmount(textureLod(cloudField,fieldUV(q),0.0).r)*(1.0-0.85*fieldGap(fieldAt(q)));
    return saturate((a-0.2)*2.5);
}
// The shell's curve, as the earth's is: barely. A cloud layer runs out to many
// times its height, and clouds shrink into streaks on the way; curving down
// within three heights of the eye, it was the inside of a crystal ball.
const float fieldCurve=120.0;
// How far along ray the shell's surface at height d over the reference eye lies.
float fieldSurface(vec3 o,vec3 ray,float d) {
    float h=d-o.z,a=dot(ray.xy,ray.xy)/(fieldCurve*c.skyShape.x),disc=ray.z*ray.z+4.0*a*h;
    return a>1e-9?(-ray.z+sqrt(max(disc,0.0)))/(2.0*a):h/max(ray.z,1e-4);
}
