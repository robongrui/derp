// Far firestorm (c.weather.w WEATHER_FIRESTORM; sky/sheet.glsl): the air
// over a burning city past the map's sky. Two sheets about 450 and 1000
// units out, carried across on most of the wind (sheetDrift): sparks one
// Doom pixel rising at some 35 units a second, flickering, and flakes of
// soot sinking at 12 (firestorm.cpp's). How much soot covers the ray
// (sky.frag lays it over in c.fallColor), and in spark how much a glowing
// one does (in sparkGlow). Part of sky.frag.
const vec3 sparkGlow=vec3(1.0,0.58,0.18);
float farFirestorm(vec3 ray,float angle,float rise,out float spark) {
    Sheet s=weatherSheet(ray,angle,rise,0.0);
    float soot=0.0;
    spark=0.0;
    for(int layer=0;layer<2;++layer)for(int kind=0;kind<2;++kind)for(int phase=0;phase<2;++phase) {
        float weight,life;
        vec2 p;
        Sheet d=sheetDrift(s,angle,kind==0?-35.0:12.0,layer==0?450.0:1000.0,0.8,phase,p,weight,life);
        vec2 home=floor(p/7.0);
        for(int k=0;k<9;++k) {
            vec2 cell=home+vec2(k%3-1,k/3-1);
            float seed=sheetSeed(cell+vec2(0.0,life*977.0),layer*4+kind*2+phase);
            if(seed>=(kind==0?0.22:0.12)*c.weather.x*d.share) continue;
            vec2 at=floor(vec2(fract(seed*91.7),fract(seed*37.1))*6.0)+0.5;
            at.x+=floor(sin(s.tic*(kind==0?3.7:2.1)+seed*40.0)*(kind==0?2.0:1.0)+0.5);
            if(!sheetPixel(d,p,cell*7.0+at)) continue;
            float strength=(layer==0?0.6:0.35)*weight;
            if(kind==0) spark=max(spark,strength*1.5*(0.5+0.5*sheetSeed(vec2(cell.x,floor(s.tic*17.5)),layer+11)));
            else soot=max(soot,strength);
        }
    }
    // Thinning out toward the horizon and the zenith, as ash's sheets do.
    float fade=sheetFade(rise)*smoothstep(0.0,0.3,rise);
    spark*=fade;
    return soot*fade;
}
// The smoke the fires raise over the sky: a dark brown pall in slow billows
// rolling downwind (over a city sky depth builds, its own pall already
// hangs there: sky/city.glsl), and from beneath, toward the horizon, the
// fires' orange glow, strong and slowly flickering. Smooth: a dither over
// this much sky shimmers.
vec3 firestormHaze(vec3 color,vec3 ray,float rise) {
    float horizon=1.0-smoothstep(-0.05,0.45,rise);
    vec3 q=vec3(ray.xy/max(length(ray.xy),1e-4)*2.0,rise*1.2)*1.5-vec3(c.weather.yz,0.0)*c.fog.w/900.0;
    if(!(c.skyLayers.w>0.0&&citySky())) {
        float billow=noise3(q)*0.7+noise3(q*2.3+vec3(9.7))*0.3;
        vec3 pall=vec3(0.1,0.075,0.06)*(0.7+0.6*billow);
        color=mix(color,pall,saturate(c.weather.x*(0.6+0.25*horizon)));
    }
    float flicker=0.8+0.2*noise3(vec3(q.xy*1.7,c.fog.w*0.6));
    return color+sparkGlow*0.16*horizon*horizon*horizon*flicker*c.weather.x;
}
