// What lies where weather settles (wear.glsl's snow: snow, sand, ash), per
// texel on Doom's texel grid, in the kind's albedo (c.settleColor). Snow: a
// quarter of its texels a shade greyer. Sand: in ripples across the wind,
// wavering, a lighter crest and a darker trough each, creeping downwind
// the harder it blows; indoors the same across a draft's trail where one
// runs over it (flow: its way, as strong as it runs, wear.glsl), and where
// no wind combs it grainy instead (a few texels lighter, a few darker). Ash: grey all over, a few
// texels darker cinders and a few lighter flecks.
// Out in the wind (open: how open the sector is to it, wear.glsl's
// sectorOpen) snow and sand blow over themselves in a gust: lighter streaks
// drawn out along the wind, running downwind with the gusts (wind.glsl),
// turning as it turns, gone in a lull, in still air or under a roof.
// Part of world.frag, after wear.glsl (whose hashes it uses).
vec3 settledColor(vec3 world,bool indoors,float open,vec2 flow) {
    vec2 t=floor(world.xy+vec2(world.z))+0.5;
    float wind=length(c.weather.yz),tic=windTic();
    vec2 way=wind>1.0?c.weather.yz/wind:vec2(1.0,0.0);
    float streak=blowing(t,open);   // blown over itself (wear.glsl)
    if(c.weather.w==WEATHER_SAND&&indoors) {
        float grain=wearHash(ivec2(t),0x6c8e9cf5u);
        // Combed by a draft: ripples across it, fading out at its edge into grain.
        float combed=length(flow);
        if(combed>0.15&&grain<smoothstep(0.15,0.5,combed)) {
            vec2 along=flow/combed;
            float wave=fract(dot(t,along)/5.0-tic*0.1*combed+0.9*wearNoise(t,12.0,0x5be0cd19u));
            return c.settleColor.rgb*(wave<0.3?1.08:wave<0.55?1.0:0.88);
        }
        return c.settleColor.rgb*(grain<0.15?0.86:grain>0.85?1.1:1.0);
    }
    if(c.weather.w==WEATHER_SAND) {
        float creep=tic*windAmountNow()*open*0.15;   // ripples creeping downwind
        float wave=fract(dot(t,way)/6.0-creep+0.9*wearNoise(t,14.0,0x5be0cd19u)+0.3*wearNoise(t,5.0,0x1f83d9abu));
        return c.settleColor.rgb*(wave<0.3?1.1:wave<0.55?1.0:0.86)*(1.0+0.12*streak);
    }
    float grain=wearHash(ivec2(t),0x6c8e9cf5u);
    if(c.weather.w==WEATHER_ASH) return c.settleColor.rgb*(grain<0.12?0.6:grain>0.94?1.35:1.0);
    return c.settleColor.rgb*(grain<0.25?0.9:1.0)*(1.0+0.15*streak);
}
