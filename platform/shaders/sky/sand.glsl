// Far sand (c.weather.w WEATHER_SAND; sky/sheet.glsl): a sandstorm is blown
// rather than falling. Two sheets of grains about 400 and 900 units out
// (the eye's own end at 384), one in some 5-pixel cells, sinking slowly
// (40 units a second, as sand.cpp's) and carried on the whole wind
// (sheetDrift: slanted that steeply the sheet would fold), so they cross
// the sky nearly flat, streaking as far as they go in a tic. How much
// a grain covers the ray; sky.frag lays it over in the colour of the eye's
// farthest grains. Part of sky.frag.
float farSand(vec3 ray,float angle,float rise) {
    Sheet s=weatherSheet(ray,angle,rise,0.0);
    float across=dot(c.weather.yz,vec2(-sin(angle),cos(angle)));
    float grains=0.0;
    for(int layer=0;layer<2;++layer)for(int phase=0;phase<2;++phase) {
        float far=layer==0?400.0:900.0,weight,life;
        vec2 p;
        Sheet d=sheetDrift(s,angle,40.0,far,1.0,phase,p,weight,life);
        vec2 home=floor(p/5.0),trail=vec2(-across,40.0)/35.0/far*s.pixel;
        for(int k=0;k<9;++k) {
            vec2 cell=home+vec2(k%3-1,k/3-1);
            float seed=sheetSeed(cell+vec2(0.0,life*977.0),layer*2+phase);
            if(seed>=0.55*c.weather.x*d.share) continue;
            vec2 at=floor(vec2(fract(seed*91.7),fract(seed*37.1))*4.0)+0.5;
            if(sheetStreak(d,p,cell*5.0+at,trail)) grains=max(grains,(layer==0?0.3:0.18)*weight);
        }
    }
    return grains*sheetFade(rise);
}
// The dust the storm raises over the sky: in the colour of the sand
// (c.settleColor) as bright as the sky's low rows, thickest at the horizon,
// in broad bands rolling downwind, smooth (a dither over this much sky
// shimmers).
vec3 sandHaze(vec3 color,vec3 low,vec3 ray,float rise) {
    vec3 q=vec3(ray.xy/max(length(ray.xy),1e-4)*3.0,rise*2.0)-vec3(c.weather.yz,0.0)*c.fog.w/400.0;
    float bands=noise3(q)*0.65+noise3(q*2.7+vec3(5.1))*0.35;
    float horizon=1.0-smoothstep(0.0,0.7,rise);
    vec3 dust=c.settleColor.rgb*max(dot(low,vec3(0.299,0.587,0.114))*1.4,0.1);
    return mix(color,dust,saturate(c.weather.x*(0.3+0.45*horizon)*(0.7+0.6*bands)));
}
