// Lighting of walls and flats.
// Part of world.frag, which declares the resources these read.
#ifndef HAS_DYNAMIC
#define HAS_DYNAMIC true   // (world.frag's QUIET variants set it false; glass.frag keeps it)
#endif
// Walls and flats: the static light from the bake (shaped by its direction
// with bake-only lights), bounce, and dynamic lights with bump detail and
// gloss, added to pulse; highlights and wet blood glints into specular.
// glassy surfaces use the glass dome (screenNormal) for their normal.
// Below this a light's bump detail and gloss do not change the picture.
#define FAINT_LIGHT 0.002
// What the bump detail makes of the sector's light, its sun and sky
// (surfaceLight sets it, world.frag multiplies the light by it); 1 elsewhere.
float daylightShade=1.0;
// Directional baked light (scene3d/bake_basis.cpp), as Half-Life 2's
// radiosity normal mapping: the baked light's split over three directions
// tilted out of the surface 120 degrees apart (basis: the first two shares,
// z where there is one), taken by a bumped normal n of a surface facing face.
// Its brightness against the flat surface's, bounded. Light from straight
// out splits evenly; the more of it one direction takes, the lower it skims
// the surface, and raking light brings out relief: the bounds widen from
// 0.5..1.6 to 0.25..2.5, ridges catching it out of their own shade.
float basisShade(vec3 n,vec3 face,vec3 basis) {
    vec3 t=abs(face.z)>0.5?vec3(1,0,0):vec3(-face.y,face.x,0),b=abs(face.z)>0.5?vec3(0,1,0):vec3(0,0,1);
    vec3 m=vec3(dot(n,t),dot(n,b),dot(n,face));
    vec2 two=basis.xy/max(basis.z,1e-3);vec3 share=vec3(two,max(1.0-two.x-two.y,0.0));
    const vec3 b0=vec3(-0.40824829,0.70710678,0.57735027),b1=vec3(-0.40824829,-0.70710678,0.57735027),b2=vec3(0.81649658,0.0,0.57735027);
    vec3 w=vec3(saturate(dot(m,b0)),saturate(dot(m,b1)),saturate(dot(m,b2)));w*=w;
    float raking=saturate((max(share.x,max(share.y,share.z))-1.0/3.0)*1.5);
    return clamp(3.0*dot(w,share)/max(w.x+w.y+w.z,1e-4),mix(0.5,0.25,raking),mix(1.6,2.5,raking));
}
// Where a light's highlight comes from. A glowing wall panel is a vertical
// span of its wall (direction.x: half its height, as flashAt takes it), so
// it shines from the point of that line nearest the mirrored view: a tube's
// highlight is a straight streak from the lamp toward the eye, from every
// side. From its middle alone a brushed ceiling bent the streak into an arc,
// right for a bulb and wrong for the tube the artwork shows. Anything else
// shines from its center.
vec3 highlightFrom(uint i,vec3 view,vec3 n) {
    vec3 mid=lights[i].position.xyz-vWorld;
    float extent=lights[i].direction.w>0.0?0.0:lights[i].direction.x;
    if(extent<=0.0) return normalize(mid);
    vec3 r=reflect(-view,n),a=mid-vec3(0,0,extent),d=vec3(0,0,2.0*extent);
    float rd=dot(r,d),t=(dot(r,a)*rd-dot(a,d))/max(dot(d,d)-rd*rd,1e-4);
    return normalize(a+d*saturate(t));
}
void surfaceLight(SectorLit lit,bool mapped,vec3 normal,vec3 screenNormal,bool glassy,bool blood,vec4 color,
                  vec3 dp1,vec3 dp2,vec2 duv1,vec2 duv2,inout vec3 pulse,inout vec3 specular) {
    uint bakeFlags=uint(c.flashlightTint.w);
    vec4 baked=lit.baked,direction=lit.direction;vec3 bounce=lit.bounce;float shownLight=lit.shown,occlusion=lit.occlusion;
    vec3 specularBefore=specular;
    uvec2 mask=HAS_DYNAMIC?vLightMask:uvec2(0u);   // (world.frag's QUIET: none reaches here)
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
    // Sun and sky light (their shares of the sector's) take bump detail too.
    bool daylight=mapped&&!glassy&&!blood&&(lit.sunShare>0.0||lit.skyShare>0.0);
    // With the HL2 basis baked, all the baked light does.
    bool directional=mapped&&!glassy&&!blood&&lit.basis.z>0.5;
    bool detail=(uint(c.flashlightTint.w)&FEATURE_SURFACE_DETAIL)!=0u&&(any(notEqual(mask,uvec2(0)))||staticDetail||ice||daylight||directional);
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
    // Walls keep their tiles level: with all baked light on the bumps (bake_basis.cpp)
    // the crooked lie read as a wall of wobbling plates.
    bool tiles=(bakeFlags&(normal.z>0.5?FEATURE_UNEVEN_TILES:FEATURE_UNEVEN_WALLS))!=0u&&abs(normal.z)>0.5&&!blood&&snowWear<=0.0;
    vec3 bumped=glassy?screenNormal:detail?detailNormal(vUV,normal,dp1,dp2,duv1,duv2,tiles,busy):normal;
    float gloss=0.0,shininess=glassy?64.0:24.0;
    vec3 glossHue=vec3(1);   // the gloss highlights' colour over the light's (blood on metal: bloodFilmHue)
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
        // Grime on a worn plate is not bare metal. Metal shines at 60%: with
        // all baked light on the bumps (bake_basis.cpp) the seams carry the
        // shape, and full sheen on top washed the panels over.
        if((uint(round(frameMean.a))&32u)!=0u&&!glassy&&!blood) {
            metal=metalness(index)*(plateWear>=0.0?1.0-plateWear:1.0)*(1.0-snowWear);
            gloss*=0.6;
        }
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
        // Blood on metal (bloodUnder, scene3d/blood_art.cpp) is a film, not paint: the plate's highlight
        // shows through it, as sharp as bare metal's and brushed the same way, so the plate's streak runs
        // on across the stain without a break, dyed the blood's red; more through thin blood than a clot.
        // The grain in whole texel rows of the world's grid, as the plate's own.
        uint under=blood?bloodUnder(vTint.r):0u;
        if(under>0u) {
            gloss=BLOOD_FILM_GLOSS*bloodFilm(color.rgb);shininess=BLOOD_FILM_SHARP;glossHue=bloodFilmHue(color.rgb);
            if(under>=2u) {
                bool level=abs(normal.z)>0.5,down=under==3u;
                vec3 run=normalize(vec3(-normal.y,normal.x,0.0)+vec3(1e-6,0.0,0.0));   // along a wall
                vec3 along=level?(down?vec3(0,1,0):vec3(1,0,0)):(down?vec3(0,0,1):run);
                vec3 across=level?(down?vec3(1,0,0):vec3(0,1,0)):(down?run:vec3(0,0,1));
                float lean=(float(tileHash(ivec2(int(floor(dot(vWorld,across))),0),0x5bd1e995u)&255u)/255.0-0.5)*0.3;
                bumped=normalize(bumped+across*lean);
                brushed=1.0;brush=normalize(along-bumped*dot(along,bumped));
            }
        }
    }
    // Snow (snowWear) lies smooth and matte over the artwork's bumps and sheen.
    if(snowWear>0.0) {bumped=normalize(mix(bumped,normal,snowWear));gloss*=1.0-snowWear;}
    // The sun's and sky's share of the light on the bump detail and the
    // painted tiles' lean (tiles.glsl): what leans toward the sun catches
    // more of it and what leans away less, so each brick of a wall in the
    // sun takes it its own way; under the open sky what faces up takes
    // more of the sky than what faces down. The tiles lean three times as
    // far for it as for lamps: their few degrees alone change the light by
    // less than its 1/16 steps, and a brick should take the sun its own way.
    // Bounded; not stepped itself, as it is whole per texel and per tile.
    // With the HL2 basis (directional) everything but the sun takes the
    // baked light's directions instead: the sector's light, the sky, the
    // lamps and the bounce (bakedShade, below).
    float bakedShade=1.0;
    if((daylight||directional)&&detail) {
        vec3 toSun=vec3(cos(c.fx2.z)*cos(c.outdoor.w),sin(c.fx2.z)*cos(c.outdoor.w),sin(c.outdoor.w));
        vec3 day=tiles?tiltNormal(bumped,2.0*tileSlope(ivec2(floor(vUV)),textureSize(relief,0),vStatic),dp1,dp2,duv1,duv2):bumped;
        float flatSun=saturate(dot(normal,toSun));
        float sunRatio=daylight&&flatSun>0.05?clamp(saturate(dot(day,toSun))/flatSun,0.4,1.8):1.0;
        if(directional) {
            bakedShade=basisShade(day,normal,lit.basis);
            daylightShade=1.0+lit.sunShare*(sunRatio-1.0)+(1.0-lit.sunShare)*(bakedShade-1.0);
        } else {
            float skyRatio=(0.6+0.4*day.z)/(0.6+0.4*normal.z);
            daylightShade=1.0+lit.sunShare*(sunRatio-1.0)+lit.skyShare*(skyRatio-1.0);
        }
        // A proud brick's edge shades the mortar beside it from the sun (world/brick_wear.glsl).
        if(daylight&&tiles&&abs(normal.z)<=0.5) daylightShade-=lit.sunShare*(1.0-brickShadow(toSun,wallAlong));
    }
    vec3 view=normalize(c.eye.xyz-vWorld);
    // Wet blood's coat (world/blood_material.glsl) glints under the lamps and
    // dynamic lights in each light's own colour: a small, sharp highlight in
    // two hard steps, so a flat splat never lights up as a whole, stronger
    // where the view skims it.
    bool wet=blood&&bloodWetness(vTint.r)>0.0;
    float wetCoat=wet?bloodWetness(vTint.r)*bloodFresnel(normal,view,BLOOD_DECAL_FACING):0.0;
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
                specular+=staticLight*glossHue*staticShare*(1.0-sheenShare)*highlight*gloss*grazing;
                if(sheenShare>0.0) {
                    float horizon=pow(1.0-saturate(dot(reflect(-view,bumped),normal)),6.0);
                    specular+=staticLight*glossHue*sheenShare*horizon*gloss*1.5;
                }
            }
            if(wet) {
                float highlight=pow(saturate(dot(bumped,normalize(toLight+view))),BLOOD_DECAL_SHARP);
                highlight=highlight>0.6?1.0:highlight>0.3?0.5:0.0;
                specular+=bloodCoat(staticLight,color.rgb)*staticShare*highlight*wetCoat*0.6*grazing;
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
    // The baked lamps (unless their bake-only direction shaped them above)
    // and the bounce on the bump detail by the HL2 basis.
    if(bakedShade!=1.0) {
        if(!(useBake&&c.bake.z>0.0&&staticShare>0.0&&detail)) staticLight*=bakedShade;
        bounce*=bakedShade;
    }
    // Doom's light falls with distance, and the baked light with it as the
    // sector light does (sectorLighting): added whole, a far lamp-lit wall
    // stood out brighter than the room it is in. Glowing texels keep their
    // own glow, so the lamp itself still reads from afar.
    float distanceFade=clamp(1.0-vDistance/3200.0,0.35,1.0);
    staticLight*=distanceFade;bounce*=distanceFade;
    pulse+=staticLight;lampLight+=max(staticLight,vec3(0));
    if(mapped) {pulse+=bounce*c.bake.y*occlusion;lampLight+=bounce*c.bake.y*occlusion;}
    // The baked light's shadow (c.shadowSkip.y 2 or 4). One rule: a shadow takes the light that
    // comes straight from the lamps, by how much of it the bake says comes one way on balance
    // (staticShare: all of it under one lamp, little between several), and leaves what has
    // bounced. So how deep it is follows from the place, with nothing more to tell it: deep
    // close under a lamp, where the lamp is most of the light; shallow far from it, where the
    // light is mostly what has gone round the room; nothing where the lamp does not reach.
    // (Half the bounce went with it before, and six tenths where no lamp shone: shadows stood
    // black in rooms that were hardly lit.) Eight tenths of it: Doom's lamps are panels and
    // tubes, not points, and a body never hides all of one.
    // Under a wide light (4: a glowing pool, a window, a lit room next door) a body hides only
    // part of it: a third, and there of the bounce too, which is where a window's light is.
    if(c.shadowSkip.y>1.5&&c.shadowSkip.y<2.5) shadowRemoved=max(staticLight,vec3(0))*staticShare*0.8;
    else if(c.shadowSkip.y>3.5) {
        float direct=dot(max(staticLight,vec3(0)),vec3(0.299,0.587,0.114));
        shadowRemoved=max(staticLight,vec3(0))*staticShare;
        if(mapped) shadowRemoved+=bounce*c.bake.y*occlusion*mix(0.6,0.5*staticShare,saturate(direct/0.05));
        shadowRemoved*=0.33;
    }
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
                    vec3 toLight=highlightFrom(i,view,bumped);
                    shine=highlightAt(bumped,normalize(toLight+view),min(shininess,40.0),brush,brushed)*
                          saturate(dot(normal,toLight)*4.0)*(wet?1.0:gloss);
                }
                if(most*shine<FAINT_LIGHT) continue;
            }
        }
        // Every light tests its walls from the texel's centre, so a wall's or a ledge's shadow
        // has the artwork's fat pixels for an edge, as the things' shadow volumes have. With
        // FEATURE_STIPPLED_SHADOWS the edge breaks into a dither besides: the lamps' by the
        // texel's place in a 2x2 pattern, the flashlight's by its own finer stipple at the
        // pixel (flashStipple).
        bool lamp=lights[i].direction.w<0.0,dithered=(bakeFlags&FEATURE_STIPPLED_SHADOWS)!=0u;
        amount=flashShadowed(lamp||!dithered?texelPoint:vWorld,i,!dithered?0.0:lamp?texelDither:stipple,amount);
        if(amount<=0.0) continue;
        amount*=mix(1.0,staticKept,bakedShare);
        if(classicBeam(i)) beamLight+=lights[i].color.rgb*beamSteps(amount)*flashFacing(vWorld,bumped,i);
        else {
            vec3 added=lights[i].color.rgb*amount*(flashFacing(vWorld,bumped,i)-bakedShare*flashFacing(vWorld,normal,i));
            pulse+=added;addedLight+=max(added,vec3(0));
        }
        if(gloss>0.0) {
            vec3 toLight=highlightFrom(i,view,bumped);
            float highlight=highlightAt(bumped,normalize(toLight+view),shininess,brush,brushed);
            specular+=lights[i].color.rgb*glossHue*amount*highlight*gloss*saturate(dot(normal,toLight)*4.0);
        }
        if(wet) {
            vec3 toLight=highlightFrom(i,view,bumped);
            float highlight=pow(saturate(dot(bumped,normalize(toLight+view))),BLOOD_DECAL_SHARP);
            highlight=highlight>0.6?1.0:highlight>0.3?0.5:0.0;
            specular+=bloodCoat(lights[i].color.rgb,color.rgb)*amount*highlight*wetCoat*0.6*saturate(dot(normal,toLight)*4.0);
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
