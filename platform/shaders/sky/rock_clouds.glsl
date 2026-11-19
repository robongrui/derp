// Cloud shadows sliding over the sky's mountains. Not the real shadow of the
// clouds overhead: their field is as good as solid over an even sky, and its gaps
// are bigger than a mountain, so that one lay on the rock as one flat tone. These
// are patches sized to a mountain, cast from the deck along the sun and carried
// by the same wind as the deck, and nobody out there checks them against a cloud.
// One noise read a rock pixel. Part of sky.frag.
// What the patches make of the light on the rock at p, as a factor. Dark rock
// shows shade poorly: the open sun between the patches brightens it as much as
// they darken it, so the mountain as a whole stays as bright as it was. Only
// where there is sun: under a lid it is all shade already.
float rockClouds(vec3 p,int ring) {
    if(!fieldSky()||hellSky())return 1.0;
    vec2 look=volumeWeather();
    float strength=(1.0-look.y)*sunOn();
    if(strength<=0.0)return 1.0;
    vec3 s=sunDir();
    // Height barely counts: cast truly it moved the pattern twice as fast up a
    // mountain as across it, and the patches lay in level bands, like strata.
    vec2 at=fieldAt(p.xy+s.xy*(cloudDeck*c.skyShape.x-0.2*p.z)/max(s.z,0.2));
    float n=textureLod(cloudNoise,vec3(at*0.55,0.45),0.0).r;
    // The more cloud the sky has, the more of the rock lies under it. Wide soft
    // edges: a cloud's shadow has no outline from out here, and a tight one read
    // like the contact shadow of something standing on the rock.
    float line=mix(0.62,0.48,saturate(fieldAmount(0.0))),shade=smoothstep(line-0.24,line+0.24,n);
    float depth=strength*(ring==0?1.0:0.7);
    return mix(1.0+0.3*depth,1.0-0.45*depth,shade);
}
