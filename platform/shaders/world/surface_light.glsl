// Lighting of walls and flats.
// Part of world.frag, which declares the resources these read.
// Walls and flats: the static light from the bake (shaped by its direction
// with bake-only lights), bounce, and dynamic lights with bump detail and
// gloss, added to pulse; highlights and wet blood glints into specular.
// glassy surfaces use the glass dome (screenNormal) for their normal.
// Below this a light's bump detail and gloss do not change the picture.
#define FAINT_LIGHT 0.002
void surfaceLight(SectorLit lit,bool mapped,vec3 normal,vec3 screenNormal,bool glassy,bool blood,vec4 color,
                  vec3 dp1,vec3 dp2,vec2 duv1,vec2 duv2,inout vec3 pulse,inout vec3 specular) {
    uint bakeFlags=uint(c.flashlightTint.w);
    vec4 baked=lit.baked,direction=lit.direction;vec3 bounce=lit.bounce;float shownLight=lit.shown,occlusion=lit.occlusion;
    vec3 specularBefore=specular;
    uvec2 mask=vLightMask;
    // Bake-only lights: the bake's direction (share in b) stands in
    // for the static lights' dynamic copies, so its light also takes
    // bump detail and gloss where it is bright enough to show them.
    bool useBake=mapped&&c.bake.x>0.0;
    float staticKept=useBake?bakeKept(baked.rgb,bakeHeadroom(shownLight)):1.0;
    vec3 staticLight=useBake?baked.rgb*staticKept*occlusion:vec3(0);
    staticLight=settledLight(staticLight,snowWear);bounce=settledLight(bounce,snowWear);   // on snow the bake's brightness only (world/wear.glsl)
    float staticShare=useBake&&c.bake.z>0.0?direction.b:0.0;
    bool staticDetail=staticShare>0.05&&dot(staticLight,vec3(0.299,0.587,0.114))>0.02;
    // Ice floors (frameMean.a 1, iceFlat) always show their grooves.
    uint material=uint(round(frameMean.a))&15u;
    bool ice=material==1u&&!blood&&!glassy;
    bool detail=(uint(c.flashlightTint.w)&FEATURE_SURFACE_DETAIL)!=0u&&(any(notEqual(mask,uvec2(0)))||staticDetail||ice);
    // How bare this texel of a metal texture (frameMean.a +32) is; on blank
    // plates (+64) how brushed it is and which way (+128: along v, else u;
    // highlightAt).
    float metal=0.0,brushed=0.0;vec3 brush=vec3(0);
    // Glass is smooth: its bulge replaces the artwork's bump detail,
    // and it takes a tighter, stronger highlight than painted metal.
    float busy=0.0;
    // Snow evens out the painted tiles' tilt wherever any lies: under a thin
    // dusting each tile still caught the light its own way, a floor of
    // lighter and darker squares through the white.
    bool tiles=(bakeFlags&(normal.z>0.5?FEATURE_UNEVEN_TILES:FEATURE_UNEVEN_WALLS))!=0u&&!blood&&snowWear<=0.0;
    vec3 bumped=glassy?screenNormal:detail?detailNormal(vUV,normal,dp1,dp2,duv1,duv2,tiles,busy):normal;
    float gloss=0.0,shininess=glassy?64.0:24.0;
    if(glassy) gloss=0.9;
    else if(ice&&detail) {gloss=0.8;shininess=56.0;}
    else if(detail) {
        ivec2 p=wrapTexel(ivec2(floor(vUV)),textureSize(image,0));
        uint index=uint(round(texelFetch(image,p,0).r*255.0));
        float ramp=0.0;
        if(material>=2u) materialGloss(material,index,gloss,ramp);
        else {gloss=paletteGloss(index);ramp=paletteShininess(index);}
        // FEATURE_VARIED_HIGHLIGHTS: each ramp's sharpness is
        // halved where the artwork is grainy and doubled where it runs
        // smooth, per texel. The highlight keeps its energy, so broad
        // ones dim and tight ones brighten against the dark around them;
        // grainy artwork also loses up to a quarter of its gloss.
        if((bakeFlags&FEATURE_VARIED_HIGHLIGHTS)!=0u) {
            shininess=ramp*exp2(1.0-2.0*busy);
            gloss*=(shininess+2.0)/26.0*(1.0-0.25*busy);
        }
        // Worn plates (plateWear) have lost their polish: a broad, dim
        // sheen on the bare metal, none where grime sits.
        if(plateWear>=0.0) {shininess*=0.5;gloss*=0.4*(1.0-plateWear);}
        // Grime on a worn plate is not bare metal.
        if((uint(round(frameMean.a))&32u)!=0u&&!glassy&&!blood)
            metal=metalness(index)*(plateWear>=0.0?1.0-plateWear:1.0)*(1.0-snowWear);
        // Brushing fades out where the artwork is grainy: painted detail is
        // busy enough without brush lines over it.
        if((uint(round(frameMean.a))&64u)!=0u) brushed=metal*(1.0-busy);
        if(brushed>0.0) {
            vec3 tangent,bitangent;textureFrame(normal,dp1,dp2,duv1,duv2,tangent,bitangent);
            bool down=(uint(round(frameMean.a))&128u)!=0u;
            vec3 along=normalize(down?bitangent:tangent),across=normalize(down?tangent:bitangent);
            // Grain in whole texel rows: each row along the brush leans its
            // own way across it, so the streak breaks into chunky lines.
            float lean=(float(tileHash(ivec2(down?p.x:p.y,0),0x5bd1e995u)&255u)/255.0-0.5)*0.3*brushed;
            bumped=normalize(bumped+across*lean);
            brush=normalize(along-bumped*dot(along,bumped));
        }
    }
    // Snow (snowWear) lies smooth and matte over the artwork's bumps and sheen.
    if(snowWear>0.0) {bumped=normalize(mix(bumped,normal,snowWear));gloss*=1.0-snowWear;}
    vec3 view=normalize(c.eye.xyz-vWorld);
    // Wet blood glints only under dynamic lights, in a brighter palette
    // color of its own red rather than white: a small, sharp highlight
    // in two hard steps, so a flat splat never lights up as a whole.
    bool wet=blood&&vTint.r>0.0;
    vec3 wetColor=wet?texelFetch(paletteLUT,ivec3(round(saturate(color.rgb*1.8+0.04)*31.0)),0).rgb:vec3(0);
    // Static lights come from the bake; their dynamic copies only add
    // what it lacks: flicker, bump detail and gloss. The bake's flicker
    // groups (bakeFlicker) are already applied to it. With bake-only lights
    // the bake supplies the rest itself: shading from where the light comes
    // from.
    // A pool's own surface (alpha without a group: its share) is lit
    // by the glow hugging it, not from above: it takes no overhead
    // highlight but a sheen where its ripples mirror the horizon,
    // which shows from afar at grazing angles.
    if(useBake&&c.bake.z>0.0) {
        uint packed=uint(round(direction.a*255.0)),group=packed>>4u;
        float sheenShare=group==0u?float(packed&15u)/15.0:0.0;
        if(staticShare>0.0) {
            vec2 frame=direction.rg*2.0-1.0;
            vec3 t=abs(normal.z)>0.5?vec3(1,0,0):vec3(-normal.y,normal.x,0);
            vec3 b=abs(normal.z)>0.5?vec3(0,1,0):vec3(0,0,1);
            vec3 toLight=normalize(t*frame.x+b*frame.y+normal*sqrt(saturate(1.0-dot(frame,frame))));
            vec3 shaded=staticLight;
            if(detail) {
                float flatFacing=mix(1.0,saturate(dot(normal,toLight)),0.75);
                float bumpFacing=mix(1.0,saturate(dot(bumped,toLight)),0.75);
                shaded*=mix(1.0,bumpFacing/flatFacing,staticShare);
            }
            float grazing=saturate(dot(normal,toLight)*4.0);
            if(gloss>0.0) {
                float highlight=highlightAt(bumped,normalize(toLight+view),shininess,brush,brushed);
                specular+=staticLight*staticShare*(1.0-sheenShare)*highlight*gloss*grazing;
                if(sheenShare>0.0) {
                    float horizon=pow(1.0-saturate(dot(reflect(-view,bumped),normal)),6.0);
                    specular+=staticLight*sheenShare*horizon*gloss*1.5;
                }
            }
            if(wet) {
                float highlight=pow(saturate(dot(bumped,normalize(toLight+view))),40.0);
                highlight=highlight>0.6?1.0:highlight>0.3?0.5:0.0;
                specular+=wetColor*staticLight*staticShare*highlight*vTint.r*0.6*grazing;
            }
            staticLight=shaded;
        }
    }
    // Ice glints where the edges of its grooves tilt the room's light
    // toward the eye: only what they catch beyond the flat surface's own
    // grazing sheen, so the glints sit on ridges over the dark ice, not as
    // a wash over the whole floor.
    if(ice&&detail) {
        float flatSheen=pow(1.0-saturate(dot(reflect(-view,normal),normal)),8.0);
        float ridge=pow(1.0-saturate(dot(reflect(-view,bumped),normal)),8.0);
        specular+=vec3(lit.light*saturate(ridge-flatSheen)*0.6);
    }
    pulse+=staticLight;lampLight+=max(staticLight,vec3(0));
    if(mapped) {pulse+=bounce*c.bake.y*occlusion;lampLight+=bounce*c.bake.y*occlusion;}
    float stipple=flashStipple(vWorld,normal);
    // Lamps (shots, projectiles, barrels) test their walls once per Doom
    // texel, from its center, and from the lamp corner its place in a 2x2
    // dither picks, so their shadows step with the artwork and soften like
    // the bake's. Far off, where a texel is smaller than a pixel, cells of
    // several texels hold still instead.
    vec3 texelPoint=vWorld;float texelDither=0.0;
    if(any(notEqual(mask,uvec2(0)))) {
        float grow=exp2(max(0.0,ceil(log2(max(max(length(duv1),length(duv2)),1e-6)))));
        vec2 cell=floor(vUV/grow),offset=(cell+0.5)*grow-vUV;
        float det=duv1.x*duv2.y-duv1.y*duv2.x;
        if(abs(det)>1e-12) {
            vec2 w=vec2(duv2.y*offset.x-duv2.x*offset.y,duv1.x*offset.y-duv1.y*offset.x)/det;
            vec3 shift=dp1*w.x+dp2*w.y;
            if(dot(shift,shift)<grow*grow*2.0) texelPoint+=shift;
        }
        ivec2 q=ivec2(cell)&1;
        texelDither=(float(2*(q.x^q.y)+q.y)+0.5)/2.0-1.0;
    }
    while(any(notEqual(mask,uvec2(0)))) {
        uint word=mask.x!=0u?0u:1u;
        uint i=word*32u+uint(findLSB(mask[word]));
        mask[word]&=mask[word]-1u;
        float bakedShare=useBake?lights[i].baked:0.0;
        float amount=flashAtOpen(vWorld,normal,i);
        if(amount<=0.0) continue;
        // A baked light's copy adds only its bump detail and gloss, mostly
        // too faint to show: bound what it can add without walls in the way
        // and skip its wall tests (the bulk of its cost) when it is too faint.
        // Cheapest bounds first; shading and shine each stay at most 1.
        if(bakedShare>0.0&&!classicBeam(i)) {
            vec3 hue=lights[i].color.rgb;
            float most=amount*mix(1.0,staticKept,bakedShare)*max(hue.r,max(hue.g,hue.b));
            if(most<FAINT_LIGHT) continue;
            if(most*abs(flashFacing(vWorld,bumped,i)-bakedShare*flashFacing(vWorld,normal,i))<FAINT_LIGHT) {
                float shine=0.0;
                if(gloss>0.0||wet) {
                    vec3 toLight=normalize(lights[i].position.xyz-vWorld);
                    shine=highlightAt(bumped,normalize(toLight+view),min(shininess,40.0),brush,brushed)*
                          saturate(dot(normal,toLight)*4.0)*(wet?1.0:gloss);
                }
                if(most*shine<FAINT_LIGHT) continue;
            }
        }
        bool lamp=lights[i].direction.w<0.0;
        amount=flashShadowed(lamp?texelPoint:vWorld,i,lamp?texelDither:stipple,amount);
        if(amount<=0.0) continue;
        amount*=mix(1.0,staticKept,bakedShare);
        if(classicBeam(i)) beamLight+=lights[i].color.rgb*beamSteps(amount)*flashFacing(vWorld,bumped,i);
        else {
            vec3 added=lights[i].color.rgb*amount*(flashFacing(vWorld,bumped,i)-bakedShare*flashFacing(vWorld,normal,i));
            pulse+=added;addedLight+=max(added,vec3(0));
        }
        if(gloss>0.0) {
            vec3 toLight=normalize(lights[i].position.xyz-vWorld);
            float highlight=highlightAt(bumped,normalize(toLight+view),shininess,brush,brushed);
            specular+=lights[i].color.rgb*amount*highlight*gloss*saturate(dot(normal,toLight)*4.0);
        }
        if(wet) {
            vec3 toLight=normalize(lights[i].position.xyz-vWorld);
            float highlight=pow(saturate(dot(bumped,normalize(toLight+view))),40.0);
            highlight=highlight>0.6?1.0:highlight>0.3?0.5:0.0;
            specular+=wetColor*lights[i].color.rgb*amount*highlight*vTint.r*0.6*saturate(dot(normal,toLight)*4.0);
        }
    }
    // Ice glints in its own deep blue, not white, so the ridges keep the
    // contrast of the original art instead of washing it pale. Bare metal's
    // highlights take its colour the same way: gold on brass, blue-grey on
    // steel.
    float hued=ice&&detail?0.8:metal;
    if(hued>0.0) {
        vec3 hue=color.rgb/max(max(color.r,color.g),max(color.b,0.05));
        specular=specularBefore+(specular-specularBefore)*mix(vec3(1),hue,hued);
    }
}
