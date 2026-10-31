// Far snow (c.weather.w WEATHER_SNOW; sky/sheet.glsl): two sheets of flakes
// one Doom pixel, about 500 and 1100 units out, one in some 7-pixel cells,
// drifting down at the snow's speed (42 units a second) and carried across
// on as much of the wind as snow.cpp's flakes ride (snowCarried: a third in
// a breeze, nearly all of it in a gale; sheetDrift, as ash: slanted that
// steeply the sheet would fold), shoved along by the same gusts rolling
// downwind (snowGust, wind.glsl's windGustAt), swaying wider and quicker the
// harder it blows. How much a flake covers the ray (sky.frag brightens the
// sky there). Part of sky.frag.
float snowWind() {return windAmountNow();}
float snowStorm() {return clamp(c.weather.x*snowWind(),0.0,1.0);}   // snow.cpp's storm: a blizzard's share
float snowCarried() {return 0.33+1.2*max(0.0,snowWind()-0.5);}          // snow.cpp's carried
// The gusts (wind.glsl) at the point far units out along the ray.
float snowGust(vec3 ray,float far) {return windGustAt(c.eye.xy+normalize(ray.xy)*far,windTic());}
float farSnow(vec3 ray,float angle,float rise) {
    Sheet s=weatherSheet(ray,angle,rise,0.0);
    float flakes=0.0,carried=snowCarried(),wind=snowWind();
    float speed=length(c.weather.yz);
    vec2 heading=speed>1.0?c.weather.yz/speed:vec2(0.0);
    float across=dot(heading,vec2(-sin(angle),cos(angle)));
    for(int layer=0;layer<2;++layer)for(int phase=0;phase<2;++phase) {
        float weight,life,far=layer==0?500.0:1100.0;
        vec2 p;
        Sheet d=sheetDrift(s,angle,42.0,far,carried,phase,p,weight,life);
        p.x-=across*snowGust(ray,far)/far*s.pixel;
        vec2 home=floor(p/7.0);
        for(int k=0;k<9;++k) {
            vec2 cell=home+vec2(k%3-1,k/3-1);
            float seed=sheetSeed(cell+vec2(0.0,life*977.0),layer*2+phase);
            if(seed>=0.5*c.weather.x*(1.0+0.8*snowStorm())*d.share) continue;   // thicker in a blizzard
            vec2 at=floor(vec2(fract(seed*91.7),fract(seed*37.1))*6.0)+0.5;
            at.x+=floor(sin(s.tic*1.6*(1.0+wind)+seed*40.0)*1.5*(1.0+2.0*wind)+0.5);
            if(sheetPixel(d,p,cell*7.0+at)) flakes=max(flakes,(layer==0?0.5:0.3)*weight);
        }
    }
    return flakes*sheetFade(rise);
}
// A blizzard's whiteout over the sky (snow.cpp's snowHazeUniforms does the
// ground): pale as the snow in the sky's low light, thickest toward the
// horizon, as thick as the storm. Smooth: a dither over this much sky shimmers.
vec3 snowHaze(vec3 color,vec3 low,float rise) {
    float storm=snowStorm();
    if(storm<=0.05) return color;
    vec3 white=vec3(0.84,0.86,0.9)*(0.45+0.7*dot(low,vec3(0.299,0.587,0.114)));
    float horizon=1.0-smoothstep(-0.05,0.9,rise);
    return mix(color,white,saturate(storm*(0.45+0.45*horizon)));
}
