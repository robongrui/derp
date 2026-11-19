// Real volumetric clouds, the painting as their weather map. The recipe is
// Guerrilla's Nubis (Schneider, SIGGRAPH 2015, 2022): density is noise minus
// what the profile leaves out, saturate(noise-(1-profile)), profile being the
// height shape times the coverage. So the painted field (sky/cloud_field.glsl)
// says where cloud lies, how tall and in what tone, and the noise volume
// (scene3d/sky_cloud_noise.cpp) makes it look like cloud. Light is Beer's law
// toward the sun, a few samples, two octaves of scattering (Wrenninge's, by way
// of Frostbite: it's what keeps cloud from looking like smoke), the dark
// powder edges, and a cheap ambient off the profile (pow(1-profile,0.5)). It
// all comes out as one tone, a place on the painting's own colour ramp.
bool hellSky();   // sky/hell.glsl, included after this file
vec3 cityUnderglow(vec3 cloud,vec3 ray,vec3 at,float cover);   // sky/city.glsl
vec3 hellDark();
vec3 hellBright();
const int volumeSteps=32,volumeLightSteps=4;
const float volumeNoiseTile=0.7,volumeDetailTile=0.11;   // in near-ring radii
// Over hell the flames burn up into a lid of smoke: 0 low down, where its art
// is the sky, to 1 past the art's top edge.
float volumeLid=0.0;
// The height shape: a flatter base, rounding off toward a top the field sets.
float volumeProfile(vec4 f,float height) {
    float top=max(f.g,0.35);
    return smoothstep(0.0,0.14,height)*(1.0-smoothstep(0.55*top,top,height));
}
// The field and the profile at a place; 0 where no cloud can be.
float volumeShape(vec2 at,float height,out vec4 f) {
    vec2 size=vec2(textureSize(cloudField,0));
    f=textureLod(cloudField,at/(fieldTexelWidth()*vec2(fieldStretch(),1.0)*size),0.0);
    return volumeProfile(f,height)*(0.42+0.56*max(fieldAmount(f.r),volumeLid))*(1.0-0.8*(1.0-volumeLid)*fieldGap(at));
}
// far: how far past telling billows apart. Out there a cloud is its coverage,
// and noise finer than a pixel would only shimmer.
float volumeBody(vec2 at,float height,float thick,float profile,float far) {
    float n=textureLod(cloudNoise,vec3(at,height*thick)/volumeNoiseTile,0.0).r;
    return saturate(mix(n,0.62,far)-(1.0-profile));
}
// Low cloud on a ring round the map (sky/depth.glsl): E1's clouds between the
// mountains, made of the same noise as the deck over them. strength: 1 a sea
// that hides the horizon behind the rock, less a few lying across its feet.
// Fixed to the world, turning slowly with the wind; whole tiles a turn, no seam.
float fieldBank() {return cloudSkyTexel(15).r;}
vec3 volumeBankOver(vec3 color,vec3 p,vec3 ray,float strength,float top,uint seed) {
    float rise=p.z/max(length(p.xy),1.0);
    float up=saturate(rise/top);
    if(up>=1.0)return color;
    float turn=atan(p.y,p.x)/6.2831853;
    float drift=cloudSeconds()*cloudSpeed()*0.12*(seed==0u?1.0:-0.6);
    vec3 at=vec3((turn+drift)*14.0,rise*2.2,float(seed)*0.37);   // as tall as wide: stretched, they lay in streaks
    float n=textureLod(cloudNoise,at,0.0).r,fine=textureLod(cloudNoise,at*vec3(3.1,3.1,1.0),0.0).g;
    float profile=strength*(1.0-up*up);
    float d=saturate(n+0.2*(fine-0.5)-(1.0-profile));
    if(d<=0.0)return color;
    float alpha=1.0-exp(-d*5.0);
    // Tops catch what light there is, bellies and thick cores stay dark.
    float tone=0.12+0.34*up+0.2*(1.0-saturate(d*2.2))+0.14*(fine-0.5);
    float glow=pow(saturate(dot(ray,sunDir())),6.0)*sunOn();
    vec3 cloud=weatherTinted(fieldRamp(saturate(tone+0.2*glow)));
    return mix(color,cloud,alpha*smoothstep(-0.02,0.0,ray.z));   // nothing below the map
}
// The painting, turned down toward the clear sky: the clouds in front carry the
// sky now, and at full strength its own clouds fought them.
// Only low down: overhead the painting has run out and is stretched to reach.
vec3 volumeBackdrop(vec3 painted,vec3 ray) {
    float rise=ray.z/max(length(ray.xy),1e-4),share=fieldBackdrop()*(1.0-smoothstep(0.35,0.85,rise));
    // And not below where its real sky ends (sky_layers.cpp), but for rock.
    float rows=cloudSkyTexel(15).g*255.0;
    if(rows>0.0) {
        float end=(skyHorizon()-rows/float(paintSize().y))/skyScale();
        share*=smoothstep(end-0.03,end+0.1,rise);
    }
    vec3 sky=mix(fieldRamp(mix(0.72,0.5,saturate(ray.z*1.4))),painted,share);
    // Weather comes between: what shows through its gaps is more of it, not the
    // fair sky, which stood out of a storm as bright flecks.
    vec2 look=volumeWeather();
    if(weatherClouds())sky=weatherTinted(mix(sky,fieldRamp(look.x+0.08),0.85*look.y));
    // The band a low sun lights along the horizon.
    float under=lowLight();
    float side=sunSide(ray);
    return sky+c.sun.rgb*(under*(0.08+0.75*side*side)*exp(-max(rise,0.0)*3.0))*sky;
}
// The low level over a sky of clouds alone: dark broken scud under the deck,
// driven faster, seen against it. Only overhead: circling the horizon, low cloud
// looked like it didn't belong.
vec3 volumeScud(vec3 color,vec3 ray,float solid) {
    float strength=max(cloudSkyTexel(15).b,weatherScud());
    float rise=ray.z/max(length(ray.xy),1e-4),fade=smoothstep(0.12,0.42,rise);
    if(strength<=0.0||fade<=0.0||(!cloudSky()&&solid<1e29))return color;   // a plane through mountains is a ruled line
    vec3 o=c.eye.xyz-c.skyLayers.xyz;
    float t=(0.3*c.skyShape.x-o.z)/ray.z;
    if(t<=0.0||t>=solid)return color;
    vec2 q=(o.xy+ray.xy*t)/c.skyShape.x-cloudDrift()*1.6+c.skyCloud.xy;
    float n=textureLod(cloudNoise,vec3(q*1.1,0.7+cloudSeconds()*0.0006),0.0).r;
    float broad=textureLod(cloudNoise,vec3(q*0.25,0.15),0.0).r,fine=textureLod(cloudNoise,vec3(q*4.3,0.4),0.0).g;
    vec2 look=volumeWeather();
    float amount=strength*(0.3+0.75*broad)*(1.0+0.35*look.y);
    float d=saturate((n+0.3*(fine-0.5)-(1.0-amount))*2.6);
    if(d<=0.0)return color;
    float tone=mix(0.08+0.2*n+0.2*(1.0-d),0.7*look.x,look.y);
    return mix(color,weatherTinted(fieldRamp(tone))*weatherShade(),0.8*d*fade);
}
float henyey(float mu,float g) {return (1.0-g*g)/pow(1.0+g*g-2.0*g*mu,1.5);}
vec3 volumeOver(vec3 color,vec3 ray,float solid) {
    if(ray.z<=0.0)return color;
    // Never over mountains: a level layer through their tops read as a line
    // ruled across the range. A spire, in a sky of clouds alone, they do cross.
    if(!cloudSky()&&solid<1e29)return color;
    float rise=ray.z/max(length(ray.xy),1e-4);
    vec2 fade=fieldFade();
    float reach=smoothstep(fade.x,fade.y,rise);
    volumeLid=hellSky()?smoothstep(0.82,1.2,rise):0.0;
    if(reach<=0.0)return color;
    vec3 o=highEye();
    float thick=fieldThickness(),low=cloudDeck*c.skyShape.x-0.5*thick;
    float t0=fieldSurface(o,ray,low),t1=min(fieldSurface(o,ray,low+thick),solid);
    if(t0<=0.0||t1<=t0)return color;
    t1=min(t1,t0+1.5*c.skyShape.x);   // a slant through the shell is opaque long before it ends
    float a=dot(ray.xy,ray.xy)/(fieldCurve*c.skyShape.x),dt=(t1-t0)/float(volumeSteps);
    float thickR=thick/c.skyShape.x,sigma=fieldOpacity()*(3.0+7.0*volumeLid)*weatherThick()*dt/thick;
    vec2 way=windWay(),across=vec2(-way.y,way.x);
    vec3 s=sunDir();
    s.z=max(s.z,0.08);
    vec3 sun=vec3(dot(s.xy,way),dot(s.xy,across),s.z);
    float mu=dot(ray,sunDir()),on=sunOn(),seconds=cloudSeconds();
    float phase=min(0.5*(mix(henyey(mu,0.65),henyey(mu,-0.2),0.4)+1.0),1.3);   // toward the sun it washed them out
    float sum=0.0,through=1.0,sunlit=0.0,rake=rakeStrength(),stretch=redClouds()?1.0:1.0+rake;   // red skies burst into lit blobs past their own reach
    float entered=t0;
    vec2 look=volumeWeather();
    for(int i=0;i<volumeSteps;++i) {
        float t=t0+dt*(float(i)+0.5);
        float height=((o.z+ray.z*t+a*t*t)-low)/thick;
        vec2 at=fieldAt(o.xy+ray.xy*t);
        vec4 f;
        float profile=volumeShape(at,height,f);
        if(profile<=0.02)continue;
        float far=smoothstep(2.5,7.0,t/c.skyShape.x);
        float d=volumeBody(at,height,thickR,profile,far);
        if(d<=0.0)continue;
        // Edges eaten by the finer noise, drifting through the body.
        float detail=textureLod(cloudNoise,vec3(at+vec2(seconds*0.0003,seconds*-0.0002),height*thickR)/volumeDetailTile,0.0).g;
        d=saturate(d-(1.0-detail)*(1.0-saturate(d*2.5))*(0.45+0.45*windTear())*(1.0-far));
        if(d<=0.0)continue;
        if(through>=1.0)entered=t;
        float step_=exp(-d*sigma);
        // Toward the sun: how much cloud stands in the way.
        float depth=0.0;
        for(int j=1;j<=volumeLightSteps;++j) {
            float reachOut=thickR*0.16*float(j*j)*stretch;
            float h=height+sun.z*reachOut/thickR;
            if(h>=1.0)break;
            vec4 g;
            vec2 toward=at+sun.xy*reachOut;
            float p=volumeShape(toward,h,g);
            if(p>0.02)depth+=volumeBody(toward,h,thickR,p,far)*(0.5+float(j));
        }
        depth*=fieldOpacity()*1.0;
        float beer=exp(-depth),scattered=0.5*exp(-0.25*depth);
        float powder=mix(1.0,1.0-exp(-2.0*(d*4.0+depth)),0.6*(1.0-saturate(mu)));
        float direct=(beer+scattered)*phase*powder*on;
        // Bellies in shade, tops open to the sky, wherever the sun isn't.
        float ambient=sqrt(1.0-profile)*0.35+0.45*height*height;
        // Stretched: light that all lands mid-ramp reads as a flat wash.
        float tone=saturate((0.8*direct+0.6*ambient-0.42)*1.7+0.42);
        tone=mix(tone,f.a,0.22);
        tone=mix(tone,look.x,look.y);
        // Frostbite's integration: true at few steps, where a plain sum isn't.
        sum+=through*(tone-tone*step_);
        sunlit+=through*(1.0-step_)*beer;
        through*=step_;
        if(through<0.02)break;
    }
    float cover=1.0-through;
    if(cover<=0.002)return color;
    // Over hell's art they are smoke: dark, to deepen it and not to wash it out.
    float tone=sum/cover;
    // A low sun: lit in the lanes the deck leaves it, dark in the long shadows
    // between, and all of it brighter on the sun's side of the sky than behind you.
    rake*=(1.0-look.y)*on;
    float lane=rake>0.0?sunRake(o.xy+ray.xy*entered):1.0,side=sunSide(ray);
    tone=mix(tone,tone*(0.72+0.45*lane)*(0.72+0.42*side),rake);
    vec3 cloud=fieldRamp(hellSky()?0.4*tone:tone);
    // The lid is smoke, near black, only what's lit from below reaching the
    // fire's colour: hell's ramp is all flame, and on it smoke stayed bright red.
    if(hellSky()) {
        float lit=smoothstep(0.5,0.85,tone);
        cloud=mix(cloud,mix(hellDark()*0.3,hellBright(),lit*lit),volumeLid);
    }
    cloud=weatherTinted(cloud)*weatherShade();
    // A low sun gets under them: their bellies warm, most toward it, thick cores
    // keeping their dark.
    vec3 tint=c.sun.rgb/max(max(c.sun.r,c.sun.g),max(c.sun.b,1e-3));
    // The sun on them, past what the painting's colours can say: faces with a
    // clear way to it stand out bright, and round it their thin edges light up.
    float open=(1.0-look.y)*on;
    // A low ray crosses far more cloud than the sun's light has to: along it
    // the deck reads as solid, which took the sun's disc and every lit edge with
    // it and left the cloud before it a dark stain with nothing behind it. What
    // gets through goes by how thick the cloud is as a column.
    float slant=1.0-smoothstep(0.25,0.75,rise),column=pow(max(through,0.02),saturate(ray.z));
    float glare=hellSky()?0.0:open*slant*c.skyState.z;
    if(open>0.0&&!hellSky()) {
        float toward=saturate(mu),lit=sunlit/cover;
        cloud+=tint*open*(0.2*lit*lit*lit*(0.1+0.9*toward*toward*toward)+(citySky()?0.1:0.3)*4.0*cover*(1.0-cover)*pow(toward,10.0));
        // Backlit, as cloud round a low sun is: dark cores, edges alight.
        cloud+=tint*glare*(0.18+0.7*column)*pow(toward,12.0);
    }
    cloud+=tint*rake*lane*(0.02+0.2*side*side)*cover;
    float under=lowLight();
    if(under>0.0) {
        float toward=0.45+0.55*saturate(dot(normalize(ray.xy),normalize(sunDir().xy))*0.5+0.5);
        cloud=mix(cloud,cloud*tint*1.5+tint*0.12,under*toward*(1.0-0.5*cover));
    }
    if(citySky())cloud=cityUnderglow(cloud,ray,o+ray*t0,cover);
    cloud=mix(cloud,color,airHaze(t0,low,5.0));
    // In fog the bank hides them; patches showing through read as stains.
    float hidden=c.weather.w==WEATHER_FOG||c.weather.w==WEATHER_DRY_ICE?0.7*saturate(c.weather.x*1.25):0.0;
    float veil=cover*reach*(1.0-hidden);
    // And the sun itself, past the share of it the cloud left in color, as
    // sky.frag paints it: its own colour, not light added to a sky already that
    // bright, which showed no disc at all.
    vec2 seen=sunSeen(ray);
    // At any height: these clouds are not deep enough to put the sun out. It burns through all
    // but their thick cores whole, and through those as a pale patch; only the weather closing
    // in (open) takes it. Veiled by the cloud's cover alone, a high sun behind a wisp stood as
    // a dim half disc.
    float pierce=hellSky()?0.0:open*c.skyState.z*(1.0-0.7*smoothstep(0.55,1.0,cover));
    float share=c.fx2.w*max(glare*max(0.35+0.65*column-(1.0-veil),0.0),max(pierce-(1.0-veil),0.0))*(1.0-hidden);
    vec3 over=mix(color,cloud,veil);
    // Hotter than its light: a low sun's colour is all but the sky's round it.
    over=mix(over,max(over,c.sun.rgb*0.6+0.5),saturate(seen.x*share*1.6));
    return over+c.sun.rgb*(seen.x*1.5+saturate(seen.y*2.0))*share;
}
