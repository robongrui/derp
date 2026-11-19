// A spire in an open sky (scene3d/sky_spire.cpp; Heretic's third sky): one
// tower, round, standing a near-ring radius (c.skyShape.x) off where the
// painting's first copy has it, as wide at each height as it is painted
// there, as the reference eye sees it, its painted face wrapped round it;
// above the painting's top it rises on as wide as its top row, into the
// clouds. Lit by the sun round its curve, dusted by the air between, its
// shading smooth (the sky being a photograph). Part of sky.frag, after
// sky/depth.glsl.
vec4 spireTexel() {return texelFetch(layers,ivec2(6,paintSize().y+1),0);}
#ifdef SKY_SPIRE
bool spireSky() {return SKY_SPIRE!=0;}   // a sky variant built for one kind (gpu3d's skyTwin)
#else
bool spireSky() {return spireTexel().r>0.5/255.0;}
#endif
// The spire's painted run at row y: its left and right columns from its
// foot's middle.
vec2 spireRun(int y,ivec2 size) {return texelFetch(layers,ivec2(y,size.y+2),0).rg*255.0-128.0;}
// Its foot's direction from the reference eye, and the way across it in
// which the painting's columns run.
vec2 spireWay(ivec2 size) {float a=-(spireTexel().b*255.0+0.5)/(4.0*float(size.x))*6.2831853;return vec2(cos(a),sin(a));}
// At height z: its painted row, its middle's offset across (world units)
// and its radius.
void spireAt(float z,ivec2 size,out float row,out float middle,out float radius) {
    float d=c.skyShape.x,top=spireTexel().g*255.0;
    row=clamp((skyHorizon()-z/d*skyScale())*float(size.y),top+0.5,float(size.y)-0.5);
    int y0=int(floor(row-0.5)),y1=min(y0+1,size.y-1);
    vec2 run=mix(spireRun(max(y0,int(top)),size),spireRun(max(y1,int(top)),size),fract(row-0.5));
    float column=radPerColumn(size)*d;
    middle=(run.x+run.y+1.0)*0.5*column;radius=(run.y-run.x+1.0)*0.5*column;
}
// How far along ray from o it stands (-1: missed).
float traceSpire(vec3 o,vec3 ray,ivec2 size) {
    vec2 way=spireWay(size),across=vec2(way.y,-way.x),foot=way*c.skyShape.x;
    float reach=0.2*c.skyShape.x,t0=-1.0,t1=-1.0;
    vec2 rel=o.xy-foot;
    float a=dot(ray.xy,ray.xy),b=dot(rel,ray.xy),k=dot(rel,rel)-reach*reach,disc=b*b-a*k;
    if(disc<0.0||a<1e-8)return -1.0;
    t0=max((-b-sqrt(disc))/a,0.0);t1=(-b+sqrt(disc))/a;
    if(t1<=t0)return -1.0;
    float last=t0;
    for(int i=1;i<=64;++i) {
        float t=t0+(t1-t0)*float(i)/64.0;
        vec3 p=o+ray*t;
        float row,middle,radius;
        spireAt(p.z,size,row,middle,radius);
        if(p.z>floorHeight()&&length(p.xy-foot-across*middle)<radius) {
            float lo=last,hi=t;
            for(int j=0;j<6;++j) {
                float m=(lo+hi)*0.5;vec3 q=o+ray*m;
                spireAt(q.z,size,row,middle,radius);
                if(length(q.xy-foot-across*middle)<radius)hi=m;else lo=m;
            }
            return hi;
        }
        last=t;
    }
    return -1.0;
}
vec3 spireColor(vec3 p,vec3 o,ivec2 size) {
    vec2 way=spireWay(size),across=vec2(way.y,-way.x),foot=way*c.skyShape.x;
    float row,middle,radius;
    spireAt(p.z,size,row,middle,radius);
    vec2 rel=p.xy-foot-across*middle;
    float side=clamp(dot(rel,across)/max(radius,1.0),-0.95,0.95);
    float column=spireTexel().b*255.0+(middle+side*radius)/(radPerColumn(size)*c.skyShape.x);
    vec3 color=layerColor(texelFetch(layers,ivec2(wrapColumn(int(floor(column)),size.x),int(row)),0).b);
    vec3 n=vec3(normalize(rel),0.0),s=sunDir();
    float sun=saturate(dot(n,s))*sunOn(),sky=0.5+0.25*dot(n.xy,-way);
    color*=sky+0.9*sun*c.sun.rgb;
    float dust=1.0-exp(-length(p-o)/(3.0*c.skyShape.x));
    return mix(color,hazeColor(size),0.5*dust);
}
