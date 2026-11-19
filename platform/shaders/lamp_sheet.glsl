// The rain round a lamp (MODE_LAMP_SHEET; scene3d/lamp_sheets.cpp lays the
// cards): a street lamp in a downpour at night shows hundreds of bright
// streaks through the whole of its light, more than the drops near the eye
// hold. A card stands upright facing the eye at the lamp, one a little
// nearer and one behind, each with the far sheet's streaks
// (sky/sheet_cells.glsl's rain cells, one Doom pixel thick, as long as a
// drop falls in a tic, slanted by the wind, falling at the rain's speed and
// stepping at the tic). Each streak is lit by the lamp where its head is,
// by the lamp's own falloff, a panel only ahead of its wall and a post lamp
// out to the sides; brighter the nearer it lies to the lamp as seen from
// the eye (the drops lying toward a lamp scatter its light forward); fewer
// of them and dimmer toward the edge of the light, so the card's own edge
// never shows, and none at all past it. Whole Doom pixels: the pattern is
// read at the middle of the Doom pixel each fragment lies in. Added over the
// scene (lampSheetPipeline), the light past white where the scene is HDR.
// Part of flat.frag.
//
// The card's vertices carry: vUV, where on it in sheet pixels (two units
// each), from the point the lamp is seen through; vLight, how far the lamp
// reaches; vLightMask.x, how far the card stands from the lamp toward the
// eye (negative behind); vLightMask.y, a panel's half height, else a post
// lamp's share; vSun, the fall (wrapped at lampSheetWrap cells) and the
// wind's slant across the card; vLampTint, vStatic, a panel's facing (what
// ahead of its wall is along the card, and at its middle); vTint, the
// lamp's colour at full strength.
const float lampSheetWrap=4096.0;
// The drops lying toward the lamp from the eye: up to this much brighter
// within this many radians of it (as weather_lamps.cpp's forwardGain for the
// drops near the eye).
const float lampForwardGain=0.75,lampGlare=0.25;
// Of the far sheet's cells, the share a card holds: the sheet stands for a
// whole depth of rain, a card for the little that falls by one lamp.
const float lampSheetShare=0.6;
// What the lamp gives a streak whose head is at h (sheet pixels on the card).
float lampSheetLight(vec2 h,float depth,float reach,float shape,bool panel) {
    float along=panel?max(0.0,abs(h.y)-shape):h.y;
    float f=1.0-length(vec3(h.x,along,depth))/reach;
    if(f<=0.0) return 0.0;
    // The lights' own curve over the bubble (reach: scene3d's rainLampBubble, not the lamp's
    // whole reach, over which three torches made one wall of neon).
    float amount=f*f*(3.0-2.0*f),full=length(vec3(h,depth));
    if(panel) {
        // Ahead of its wall only, as doom_panel_emission.
        float t=clamp((h.x*uintBitsToFloat(vLampTint)+uintBitsToFloat(vStatic))/max(full,0.5),0.0,1.0);
        amount*=t*t*(3.0-2.0*t);
    }
    else if(shape>0.0) {
        // A post lamp out to the sides, not up or down (baked_lighting.h's bakePostShare).
        float rise=full>1e-3?h.y/full:0.0,side=sqrt(max(0.0,1.0-rise*rise)),least=rise<0.0?0.12:0.3;
        amount*=1.0+shape*((least+(1.0-least)*side)*1.2-1.0);
    }
    return amount;
}
vec3 lampSheet() {
    // The middle of this fragment's Doom pixel, on the card.
    vec2 q=vUV,dx=dFdx(q),dy=dFdy(q);
    float doomPixel=max(c.fx2.x,1.0);
    vec2 middle=(floor(gl_FragCoord.xy/doomPixel)+0.5)*doomPixel-gl_FragCoord.xy;
    q+=dx*middle.x+dy*middle.y;
    float reach=vLight,depth=uintBitsToFloat(vLightMask.x),shape=uintBitsToFloat(vLightMask.y);
    float trail=520.0/35.0/2.0;   // as far as a drop falls in a tic (rain.cpp), in sheet pixels
    float span=(vMode&MODE_LAMP_SHEET_PANEL)!=0u?shape:0.0;
    float within=sqrt(max(reach*reach-depth*depth,0.0));
    if(abs(q.x)>within||q.y<-within-span||q.y>within+span+trail*1.4) discard;
    float fall=vSun.x,slant=vSun.y;
    Sheet s;
    s.toSheet=mat2(vec2(1.0,0.0),vec2(slant,1.0))*mat2(dx,dy)*doomPixel;
    s.toScreen=inverse(s.toSheet);
    vec2 p=vec2(q.x+slant*q.y,q.y+fall),home=floor(p/rainCell);
    int layer=(vMode&MODE_LAMP_SHEET_FAR)!=0u?12:11;
    bool panel=(vMode&MODE_LAMP_SHEET_PANEL)!=0u;
    // Sheet pixels per Doom pixel here: a far lamp's cells crowd on the
    // screen, and fewer hold a drop, as the far sheets' share.
    float perPixel=max(length(dx*doomPixel),1e-4),cellOnScreen=rainCell/perPixel;
    float crowd=clamp(cellOnScreen*cellOnScreen/16.0,0.15,1.0),most=rainShare*c.weather.x*crowd;
    float best=0.0;
    for(int k=0;k<9;++k) {
        // The heads at and below p: a streak trails up behind its head.
        vec2 cell=home+vec2(k%3-1,k/3-2);
        float seed=sheetSeed(mod(cell,lampSheetWrap),layer);
        if(seed>=most) continue;
        vec2 head=rainHead(cell,seed),h=vec2(head.x-slant*(head.y-fall),head.y-fall);
        float light=lampSheetLight(h,depth,reach,shape,panel);
        // Fewer toward the edge of the light, all of them in its inner part.
        if(light<=0.0||seed>=most*lampSheetShare*min(1.0,2.0*light)) continue;
        if(!sheetStreak(s,p,head,vec2(0.0,trail*(1.0+0.3*light)))) continue;
        float glare=1.0+lampForwardGain*(1.0-smoothstep(0.0,lampGlare,length(h)/perPixel/(100.0*c.projection.y)));
        best=max(best,light*glare);
    }
    if(best<=0.0) discard;
    // Each streak at one of a few levels, as the palette would hold it.
    // (To the nearest, not the next above: the faintest are not drawn at all.)
    best=floor(min(best,1.5)*6.0+0.5)/6.0;
    if(best<=0.0) discard;
    vec3 color=vTint*best;
    // The air between, as the particles take it (flat.frag): only what it
    // lets through, since this is light added and the haze is there already.
    if((vMode&MODE_PARTICLE_OUTSIDE)!=0u&&c.haze.w>0.0)
        color*=1.0-c.haze.w*0.6*(1.0-exp(-max(length(vWorld-c.eye.xyz)-256.0,0.0)/1400.0));
    if(c.effects.z==0.0) {
        float way=length(vWorld-c.eye.xyz);
        if(c.materials.z>0.0&&way>0.01) color*=exp(-heightFogTau(c.eye.z,vWorld.z,way,c.fog.x,c.fog.y,c.fog.z));
        color*=c.fx.x;
    }
    return color;
}
