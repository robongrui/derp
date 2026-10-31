// Far ash (c.weather.w WEATHER_ASH; sky/sheet.glsl): the fall from an
// eruption. Two sheets of flakes one Doom pixel, about 450 and 1000 units
// out, one in some 6-pixel cells, drifting down at the ash's speed (30 units
// a second, as ash.cpp's) and carried across on half its wind (sheetDrift:
// slanted that steeply the sheet would fold), swaying, each
// tumbling from its lighter face to its darker one and back. One in twenty
// is an ember still glowing, flickering as it goes. How much an ash flake
// covers the ray (sky.frag lays it over in the colour of the eye's farthest
// flakes), and in ember how much a glowing one does. Part of sky.frag.
const vec3 emberGlow=vec3(1.0,0.48,0.14);
float farAsh(vec3 ray,float angle,float rise,out float ember) {
    Sheet s=weatherSheet(ray,angle,rise,0.0);
    float flakes=0.0;
    ember=0.0;
    for(int layer=0;layer<2;++layer)for(int phase=0;phase<2;++phase) {
        float weight,life;
        vec2 p;
        Sheet d=sheetDrift(s,angle,30.0,layer==0?450.0:1000.0,0.5,phase,p,weight,life);
        vec2 home=floor(p/6.0);
        for(int k=0;k<9;++k) {
            vec2 cell=home+vec2(k%3-1,k/3-1);
            float seed=sheetSeed(cell+vec2(0.0,life*977.0),layer*2+phase);
            if(seed>=0.6*c.weather.x*d.share) continue;
            vec2 at=floor(vec2(fract(seed*91.7),fract(seed*37.1))*5.0)+0.5;
            at.x+=floor(sin(s.tic*2.3+seed*40.0)*1.5+0.5);
            if(!sheetPixel(d,p,cell*6.0+at)) continue;
            float strength=(layer==0?0.5:0.3)*weight;
            if(fract(seed*53.9)<0.05) {
                float flicker=0.55+0.45*sheetSeed(vec2(cell.x,floor(s.tic*17.5)),layer+7);
                ember=max(ember,strength*1.4*flicker);
            }
            else {
                float face=fract(s.tic*(0.8+fract(seed*7.3))+seed*3.1)<0.5?1.0:0.55;
                flakes=max(flakes,strength*face);
            }
        }
    }
    // Thinning out toward the horizon: the walls under the skyline have only
    // the near flakes in front of them (ash.cpp), and a sheet as thick down
    // there as overhead drew a hard line along every skyline.
    float fade=sheetFade(rise)*smoothstep(0.0,0.3,rise);
    ember*=fade;
    return flakes*fade;
}
// The pall the eruption hangs over the sky: the ash's colour (c.settleColor)
// as bright as the sky's low rows, thickest low down, in slow billows
// rolling downwind, and toward the horizon a dull red underglow, as over
// the fires that raise it. Smooth: a dither over this much sky shimmers.
vec3 ashHaze(vec3 color,vec3 low,vec3 ray,float rise) {
    vec3 q=vec3(ray.xy/max(length(ray.xy),1e-4)*2.0,rise*1.2)*1.5-vec3(c.weather.yz,0.0)*c.fog.w/900.0;
    float billow=noise3(q)*0.7+noise3(q*2.3+vec3(9.7))*0.3;
    vec3 pall=c.settleColor.rgb*(0.35+0.9*dot(low,vec3(0.299,0.587,0.114)))*(0.75+0.5*billow);
    float horizon=1.0-smoothstep(-0.05,0.5,rise);
    color=mix(color,pall,saturate(c.weather.x*(0.55+0.25*horizon)));
    return color+vec3(0.16,0.045,0.012)*horizon*horizon*c.weather.x;
}
