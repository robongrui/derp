// Baked light: how much of a static light a sector's headroom shows, and the
// bake maps' light, sun, bounce, ambient and light direction at a surface.
// Part of world.frag, which declares the resources these read.
// Static light fills only the headroom the sector's own light level leaves,
// easing toward a limit (fillHeadroom in baked_lighting.h). The bake keeps the
// full light, so a sector whose light changes (flicker, switches) is capped by
// its current level; the static lights' dynamic copies take the same share for
// their detail and gloss.
float bakeHeadroom(float sectorLight) {return max(1.25-sectorLight,0.0);}
float bakeShoulder(float light,float headroom) {
    float knee=headroom*0.5,rest=headroom-knee;
    if(light<=knee||rest<=0.0) return min(light,knee);
    return knee+rest*(1.0-exp(-(light-knee)/rest));
}
// The share of a baked static light (by its luminance) a surface shows.
float bakeKept(vec3 baked,float headroom) {
    float luminance=dot(baked,vec3(0.299,0.587,0.114));
    return luminance>0.0?bakeShoulder(luminance,headroom)/luminance:1.0;
}
// Baked surface data: rgb static light, a sun visibility (negative where a
// surface has none, as on ceilings), and bounce light. Floors keep their sun
// in the contact map's blue channel.
// Texel-aligned (FEATURE_TEXEL_BAKE): the four nearest cells blend smoothly, read at the
// center of the texture pixel the fragment lies in, so light steps follow
// the artwork's own pixels instead of the bake grid. Floors skip cells of
// other sectors like seamLight; walls stay inside their atlas strip, whose
// origin and scale arrive in vTint. Otherwise one nearest sample per cell.
// ambient: sky visibility and openness, packed in the bounce alpha as two
// 16-step nibbles (bakeAmbient). direction: with bake-only lights (bake.z),
// the light direction texel, blended like the light except its flicker
// group (alpha), which comes from the nearest cell.
vec2 unpackAmbient(float packed) {
    float v=round(packed*255.0);
    return vec2(floor(v/16.0),mod(v,16.0))/15.0;
}
vec4 bakedSurface(vec3 normal,uint sector,out vec3 bounce,out vec2 ambient,out vec4 direction) {
    bounce=vec3(0);ambient=vec2(0,1);direction=vec4(0.5,0.5,0,0);
    bool texelAligned=(uint(c.flashlightTint.w)&FEATURE_TEXEL_BAKE)!=0u,directed=c.bake.z>0.0;
    if(abs(normal.z)>0.5&&texelAligned) {
        ivec2 lightSize=textureSize(flatLight,0),layer=max(lightSize/ivec2(1,4),ivec2(1));
        int row=normal.z<-0.5?layer.y:0;
        vec2 g=(floor(vWorld.xy)+0.5-c.map.xy)*c.map.z-0.5;
        ivec2 q=ivec2(floor(g));vec2 f=fract(g);
        vec3 light=vec3(0),bounced=vec3(0),toward=vec3(0);vec2 open=vec2(0);float sun=0.0,total=0.0,heaviest=0.0,group=0.0;
        int directionRow=normal.z<-0.5?layer.y:0;
        for(int n=0;n<4;++n) {
            ivec2 o=ivec2(n&1,n>>1),cell=clamp(q+o,ivec2(0),layer-1);
            if(uint(round(texelFetch(seams,cell,0).x*65535.0))!=sector) continue;
            float w=(o.x!=0?f.x:1.0-f.x)*(o.y!=0?f.y:1.0-f.y)+1e-4;
            light+=texelFetch(flatLight,cell+ivec2(0,row),0).rgb*w;
            vec4 b=texelFetch(flatLight,min(cell+ivec2(0,row+2*layer.y),lightSize-1),0);
            bounced+=b.rgb*w;open+=unpackAmbient(b.a)*w;
            sun+=texelFetch(contact,cell,0).b*w;total+=w;
            if(directed) {
                vec4 d=texelFetch(flatDirection,cell+ivec2(0,directionRow),0);
                toward+=d.rgb*w;
                if(w>heaviest) {heaviest=w;group=d.a;}
            }
        }
        if(total>0.0) {
            bounce=bounced/total*2.0;ambient=open/total;
            if(directed) direction=vec4(toward/total,group);
            return vec4(light/total*2.0,normal.z>0.5?sun/total:-1.0);
        }
    }
    if(abs(normal.z)>0.5) {
        ivec2 cell=ivec2(floor((vWorld.xy-c.map.xy)*c.map.z)),size=textureSize(contact,0);
        float sun=normal.z>0.5?texelFetch(contact,clamp(cell,ivec2(0),size-1),0).b:-1.0;
        ivec2 lightSize=textureSize(flatLight,0),layer=max(lightSize/ivec2(1,4),ivec2(1));
        ivec2 lightCell=clamp(cell,ivec2(0),layer-1)+ivec2(0,normal.z<-0.5?layer.y:0);
        vec4 b=texelFetch(flatLight,min(lightCell+ivec2(0,2*layer.y),lightSize-1),0);
        bounce=b.rgb*2.0;ambient=unpackAmbient(b.a);
        if(directed) direction=texelFetch(flatDirection,clamp(cell,ivec2(0),layer-1)+ivec2(0,normal.z<-0.5?layer.y:0),0);
        return vec4(texelFetch(flatLight,min(lightCell,lightSize-1),0).rgb*2.0,sun);
    }
    if(vSun.x<0.0) return vec4(0,0,0,-1);
    ivec2 size=textureSize(wallBake,0),lightSize=max(size/ivec2(2,1),ivec2(1));
    if(texelAligned&&vTint.z>0.0&&(vMode&256u)==0u) {
        // Wall texture u runs along the wall, v down from the anchor.
        vec2 offset=vec2(floor(vUV.x)+0.5-vUV.x,vUV.y-floor(vUV.y)-0.5);
        vec2 g=vSun+offset*vTint.z-0.5;
        ivec2 q=ivec2(floor(g)),origin=ivec2(vTint.xy);vec2 f=fract(g);
        vec4 baked=vec4(0);vec3 bounced=vec3(0),toward=vec3(0);vec2 open=vec2(0);float heaviest=-1.0,group=0.0;
        for(int n=0;n<4;++n) {
            ivec2 o=ivec2(n&1,n>>1),cell=clamp(max(q+o,origin),ivec2(0),lightSize-1);
            float w=(o.x!=0?f.x:1.0-f.x)*(o.y!=0?f.y:1.0-f.y);
            baked+=texelFetch(wallBake,cell,0)*w;
            vec4 b=texelFetch(wallBake,min(cell+ivec2(lightSize.x,0),size-1),0);
            bounced+=b.rgb*w;open+=unpackAmbient(b.a)*w;
            if(directed) {
                vec4 d=texelFetch(wallDirection,cell,0);
                toward+=d.rgb*w;
                if(w>heaviest) {heaviest=w;group=d.a;}
            }
        }
        bounce=bounced*2.0;ambient=open;
        if(directed) direction=vec4(toward,group);
        return vec4(baked.rgb*2.0,baked.a);
    }
    ivec2 cell=clamp(ivec2(floor(vSun)),ivec2(0),lightSize-1);
    vec4 b=texelFetch(wallBake,min(cell+ivec2(lightSize.x,0),size-1),0);
    bounce=b.rgb*2.0;ambient=unpackAmbient(b.a);
    if(directed) direction=texelFetch(wallDirection,cell,0);
    vec4 baked=texelFetch(wallBake,cell,0);
    return vec4(baked.rgb*2.0,baked.a);
}
