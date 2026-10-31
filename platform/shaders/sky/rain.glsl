// Far rain and nukage rain (c.weather.w WEATHER_RAIN, WEATHER_NUKAGE;
// sky/sheet.glsl): two sheets of streaks one Doom pixel wide and as long as
// a drop falls in a tic, as rain.cpp draws them, so each tic's takes up
// where the last left off; about 640 and 1400 units out (the eye's own drops
// end at 640), one in some 6-pixel cells. How much a streak covers the ray;
// sky.frag lays it over in the colour of the eye's farthest drops. Part of
// sky.frag.
float farRain(vec3 ray,float angle,float rise) {
    Sheet s=weatherSheet(ray,angle,rise,1.0/520.0);
    float drops=0.0;
    for(int layer=0;layer<2;++layer) {
        float far=layer==0?640.0:1400.0;
        vec2 p=sheetPoint(s,520.0,far),home=floor(p/6.0);
        float trail=520.0/35.0/far*s.pixel;
        for(int k=0;k<9;++k) {
            vec2 cell=home+vec2(k%3-1,k/3-1);
            float seed=sheetSeed(cell,layer);
            if(seed>=0.6*c.weather.x*s.share) continue;
            vec2 at=floor(vec2(fract(seed*91.7),fract(seed*37.1))*5.0)+0.5;
            if(sheetStreak(s,p,cell*6.0+at,vec2(0.0,trail))) drops=max(drops,layer==0?0.26:0.15);
        }
    }
    return drops*sheetFade(rise);
}
