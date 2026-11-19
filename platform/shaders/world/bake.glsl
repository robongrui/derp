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
// 16-step nibbles (bakeAmbient). direction: with bake-only lights (bake.z
// 1) or lamps that switch (bake.z -1), the light direction texel, blended
// like the light except its flicker group (alpha), which comes from the
// nearest cell. Each cell's light and bounce show only what its flicker
// group leaves on now (bakeFlicker) before they blend, so a switched-off
// lamp leaves no blocks of light where neighboring cells disagree.
vec2 unpackAmbient(float packed) {
    float v=round(packed*255.0);
    return vec2(floor(v/16.0),mod(v,16.0))/15.0;
}
// How much of a cell's baked static light shows now: the share of its
// flicker group (direction alpha: group*16+share) follows c.flicker, so a
// flickering decoration's or a switched-off lamp's part steps away. Bounce
// takes the same share, as most of it comes from the same lamps.
float bakeFlicker(float packedAlpha) {
    if(c.bake.z==0.0) return 1.0;
    uint packed=uint(round(packedAlpha*255.0)),group=packed>>4u;
    if(group==0u) return 1.0;
    return 1.0-float(packed&15u)/15.0*(1.0-c.flicker[group>>2u][group&3u]);
}
// How much of a cell's bounce shows now. own: its basis texel's alpha, the
// share of its bounce that comes from flickering lamps (scene3d/bake_bounce.cpp);
// 0 where that was not worked out: then as the cell's lamp light (lit).
float bounceFlicker(float own,float lit) {return own==0.0?lit:bakeFlicker(own);}
// Whole-level lighting (c.bake.x 2) reads floors and ceilings as one field across the map's
// lines, so the lines inside a floor show no seam. But only where it is one surface: a cell
// of another sector counts when its floor (its ceiling) lies at this one's height. A window's
// sill, a step or a ledge beside the floor is another surface: its cells, lit by the sky
// outside or by the lamp over the step, were blended into the floor at the wall's foot, a lit
// strip under every window. (Heights from the sector buffer, so they follow moving floors.)
bool sameFlat(uint owner,uint sector,bool ceiling) {
    if(owner==sector) return true;
    if(owner==0xFFFFu||c.bake.x<=1.5) return false;
    return abs((ceiling?sectorInfo[owner].z:sectorInfo[owner].y)-(ceiling?sectorInfo[sector].z:sectorInfo[sector].y))<0.5;
}
// F4 "Quick baked light reads" (c.farFogRange.z): where the four cells agree, the card blends them.
// The texel-aligned reads below take each cell apart for three reasons only: a cell of another
// floor is left out (sameFlat), a cell's light and bounce show its flicker group's share
// (bakeFlicker), and the ambient is packed in nibbles that do not blend. Where all four cells
// are this floor's and their flicker shares are one (none, or the same lamp group's: a whole
// room under one blinking lamp), the light, bounce, basis, direction and sun are plain bilinear
// blends of the four at the weights f, the share a common factor: one filtered read each (the
// bake maps are bound with linear samplers; texelFetch ignores them), the packed parts by
// gathers. The card's blend weights are 8-bit, so the light can differ by a hair where it
// changes fastest. gathered: a gather's four texels as the cells (0,0),(1,0),(0,1),(1,1) of the
// loops below.
bool fastBake() {return c.farFogRange.z>0.0;}
vec4 cellsOf(vec4 gathered) {return gathered.wzxy;}
bool alike(vec4 v) {return all(lessThan(abs(v-v.x),vec4(1e-6)));}
// The four cells' light shares (bakeFlicker) and bounce shares (bounceFlicker), from their direction and basis alphas.
vec4 cellsLit(vec4 directionAlpha) {return vec4(bakeFlicker(directionAlpha.x),bakeFlicker(directionAlpha.y),bakeFlicker(directionAlpha.z),bakeFlicker(directionAlpha.w));}
vec4 cellsBounced(vec4 basisAlpha,vec4 lit) {
    return vec4(bounceFlicker(basisAlpha.x,lit.x),bounceFlicker(basisAlpha.y,lit.y),bounceFlicker(basisAlpha.z,lit.z),bounceFlicker(basisAlpha.w,lit.w));
}
// The heaviest cell's alpha (its flicker group and pool share), first of equals, as the loops pick it.
float heaviestAlpha(vec4 alphas,vec4 w,float floorAt) {
    float best=floorAt,a=0.0;
    for(int n=0;n<4;++n) if(w[n]>best) {best=w[n];a=alphas[n];}
    return a;
}
vec2 blendAmbient(vec4 packed,vec4 w) {
    return unpackAmbient(packed.x)*w.x+unpackAmbient(packed.y)*w.y+unpackAmbient(packed.z)*w.z+unpackAmbient(packed.w)*w.w;
}
// at: where on the map (vWorld, or for a drift heaped on a wall the ground in front).
// basis: the baked light's split over the HL2 basis (scene3d/bake_basis.cpp):
// the first two directions' shares, and z 1 where there is one (blended, so
// the shares are divided by it).
vec4 bakedSurface(vec3 at,vec3 normal,uint sector,out vec3 bounce,out vec2 ambient,out vec4 direction,out vec3 basis) {
    bounce=vec3(0);ambient=vec2(0,1);direction=vec4(0.5,0.5,0,0);basis=vec3(0);
    bool texelAligned=(uint(c.flashlightTint.w)&FEATURE_TEXEL_BAKE)!=0u,directed=c.bake.z!=0.0;
    if(abs(normal.z)>0.5&&texelAligned) {
        ivec2 lightSize=textureSize(flatLight,0),layer=max(lightSize/ivec2(1,4),ivec2(1));
        int row=normal.z<-0.5?layer.y:0;
        vec2 g=(floor(at.xy)+0.5-c.map.xy)*c.map.z-0.5;
        ivec2 q=ivec2(floor(g));vec2 f=fract(g);
        vec3 light=vec3(0),bounced=vec3(0),toward=vec3(0),split=vec3(0);vec2 open=vec2(0);float sun=0.0,total=0.0,heaviest=0.0,group=0.0;
        int directionRow=normal.z<-0.5?layer.y:0,basisRow=(normal.z<-0.5?3:2)*layer.y;
        if(fastBake()&&all(greaterThanEqual(q,ivec2(0)))&&all(lessThan(q+1,layer))) {
            vec2 size=vec2(lightSize),corner=vec2(q+1),at2=g+0.5;
            vec4 w=vec4((1.0-f.x)*(1.0-f.y),f.x*(1.0-f.y),(1.0-f.x)*f.y,f.x*f.y);
            uvec4 owner=uvec4(round(cellsOf(textureGather(seams,corner/vec2(textureSize(seams,0)),0))*65535.0));
            bool ceiling=normal.z<-0.5;
            vec4 basisAlpha=cellsOf(textureGather(flatDirection,(corner+vec2(0,basisRow))/size,3));
            vec4 directionAlpha=directed?cellsOf(textureGather(flatDirection,(corner+vec2(0,directionRow))/size,3)):vec4(0);
            vec4 lit=directed?cellsLit(directionAlpha):vec4(1),bounced=cellsBounced(basisAlpha,lit);
            if(sameFlat(owner.x,sector,ceiling)&&sameFlat(owner.y,sector,ceiling)&&sameFlat(owner.z,sector,ceiling)&&sameFlat(owner.w,sector,ceiling)&&
               alike(lit)&&alike(bounced)) {
                vec3 lightHere=textureLod(flatLight,(at2+vec2(0,row))/size,0.0).rgb*lit.x;
                vec2 bounceAt=(at2+vec2(0,row+2*layer.y))/size;
                bounce=textureLod(flatLight,bounceAt,0.0).rgb*2.0*bounced.x;
                ambient=blendAmbient(cellsOf(textureGather(flatLight,(corner+vec2(0,row+2*layer.y))/size,3)),w);
                basis=textureLod(flatDirection,(at2+vec2(0,basisRow))/size,0.0).rgb;
                if(directed) direction=vec4(textureLod(flatDirection,(at2+vec2(0,directionRow))/size,0.0).rgb,heaviestAlpha(directionAlpha,w+1e-4,0.0));
                float sunHere=normal.z>0.5?textureLod(contact,at2/vec2(textureSize(contact,0)),0.0).b:-1.0;
                return vec4(lightHere*2.0,sunHere);
            }
        }
        for(int n=0;n<4;++n) {
            ivec2 o=ivec2(n&1,n>>1),cell=clamp(q+o,ivec2(0),layer-1);
            // Whole-level lighting (c.bake.x 2) reads the light as one field: any cell of this
            // floor, whatever sector the map cut it into (sameFlat). Kept to the fragment's own
            // sector, two sectors never met in a cell and every line of the map showed as a seam.
            uint owner=uint(round(texelFetch(seams,cell,0).x*65535.0));
            if(!sameFlat(owner,sector,normal.z<-0.5)) continue;
            float w=(o.x!=0?f.x:1.0-f.x)*(o.y!=0?f.y:1.0-f.y)+1e-4,lit=1.0;
            if(directed) {
                vec4 d=texelFetch(flatDirection,cell+ivec2(0,directionRow),0);
                toward+=d.rgb*w;lit=bakeFlicker(d.a);
                if(w>heaviest) {heaviest=w;group=d.a;}
            }
            light+=texelFetch(flatLight,cell+ivec2(0,row),0).rgb*w*lit;
            vec4 b=texelFetch(flatLight,min(cell+ivec2(0,row+2*layer.y),lightSize-1),0);
            vec4 s=texelFetch(flatDirection,cell+ivec2(0,basisRow),0);
            bounced+=b.rgb*w*bounceFlicker(s.a,lit);open+=unpackAmbient(b.a)*w;
            sun+=texelFetch(contact,cell,0).b*w;total+=w;
            split+=s.rgb*w;
        }
        if(total>0.0) {
            bounce=bounced/total*2.0;ambient=open/total;basis=split/total;
            if(directed) direction=vec4(toward/total,group);
            return vec4(light/total*2.0,normal.z>0.5?sun/total:-1.0);
        }
    }
    if(abs(normal.z)>0.5) {
        ivec2 cell=ivec2(floor((at.xy-c.map.xy)*c.map.z)),size=textureSize(contact,0);
        float sun=normal.z>0.5?texelFetch(contact,clamp(cell,ivec2(0),size-1),0).b:-1.0;
        ivec2 lightSize=textureSize(flatLight,0),layer=max(lightSize/ivec2(1,4),ivec2(1));
        ivec2 lightCell=clamp(cell,ivec2(0),layer-1)+ivec2(0,normal.z<-0.5?layer.y:0);
        vec4 b=texelFetch(flatLight,min(lightCell+ivec2(0,2*layer.y),lightSize-1),0);
        if(directed) direction=texelFetch(flatDirection,clamp(cell,ivec2(0),layer-1)+ivec2(0,normal.z<-0.5?layer.y:0),0);
        float lit=bakeFlicker(direction.a);
        vec4 s=texelFetch(flatDirection,clamp(cell,ivec2(0),layer-1)+ivec2(0,(normal.z<-0.5?3:2)*layer.y),0);
        bounce=b.rgb*2.0*bounceFlicker(s.a,lit);ambient=unpackAmbient(b.a);
        basis=s.rgb;
        return vec4(texelFetch(flatLight,min(lightCell,lightSize-1),0).rgb*2.0*lit,sun);
    }
    if(vSun.x<0.0) return vec4(0,0,0,-1);
    ivec2 size=textureSize(wallBake,0),lightSize=max(size/ivec2(2,1),ivec2(1));
    if(texelAligned&&vTint.z>0.0&&(vMode&MODE_BLOOD)==0u) {
        // Wall texture u runs along the wall, v down from the anchor.
        vec2 offset=vec2(floor(vUV.x)+0.5-vUV.x,vUV.y-floor(vUV.y)-0.5);
        vec2 g=vSun+offset*vTint.z-0.5;
        ivec2 q=ivec2(floor(g)),origin=ivec2(vTint.xy);vec2 f=fract(g);
        if(fastBake()&&all(greaterThanEqual(q,max(origin,ivec2(0))))&&all(lessThan(q+1,lightSize))) {
            vec2 fullSize=vec2(size),corner=vec2(q+1),at2=g+0.5,second=vec2(lightSize.x,0);
            vec4 w=vec4((1.0-f.x)*(1.0-f.y),f.x*(1.0-f.y),(1.0-f.x)*f.y,f.x*f.y);
            vec4 basisAlpha=cellsOf(textureGather(wallDirection,(corner+second)/fullSize,3));
            vec4 directionAlpha=directed?cellsOf(textureGather(wallDirection,corner/fullSize,3)):vec4(0);
            vec4 lit=directed?cellsLit(directionAlpha):vec4(1),bounced=cellsBounced(basisAlpha,lit);
            if(alike(lit)&&alike(bounced)) {
                vec4 bakedHere=textureLod(wallBake,at2/fullSize,0.0)*vec4(vec3(lit.x),1.0);
                bounce=textureLod(wallBake,(at2+second)/fullSize,0.0).rgb*2.0*bounced.x;
                ambient=blendAmbient(cellsOf(textureGather(wallBake,(corner+second)/fullSize,3)),w);
                basis=textureLod(wallDirection,(at2+second)/fullSize,0.0).rgb;
                if(directed) direction=vec4(textureLod(wallDirection,at2/fullSize,0.0).rgb,heaviestAlpha(directionAlpha,w,-1.0));
                return vec4(bakedHere.rgb*2.0,bakedHere.a);
            }
        }
        vec4 baked=vec4(0);vec3 bounced=vec3(0),toward=vec3(0),split=vec3(0);vec2 open=vec2(0);float heaviest=-1.0,group=0.0;
        for(int n=0;n<4;++n) {
            ivec2 o=ivec2(n&1,n>>1),cell=clamp(max(q+o,origin),ivec2(0),lightSize-1);
            float w=(o.x!=0?f.x:1.0-f.x)*(o.y!=0?f.y:1.0-f.y),lit=1.0;
            if(directed) {
                vec4 d=texelFetch(wallDirection,cell,0);
                toward+=d.rgb*w;lit=bakeFlicker(d.a);
                if(w>heaviest) {heaviest=w;group=d.a;}
            }
            vec4 cellBake=texelFetch(wallBake,cell,0);
            baked+=vec4(cellBake.rgb*lit,cellBake.a)*w;
            vec4 b=texelFetch(wallBake,min(cell+ivec2(lightSize.x,0),size-1),0);
            vec4 s=texelFetch(wallDirection,min(cell+ivec2(lightSize.x,0),size-1),0);
            bounced+=b.rgb*w*bounceFlicker(s.a,lit);open+=unpackAmbient(b.a)*w;
            split+=s.rgb*w;
        }
        bounce=bounced*2.0;ambient=open;basis=split;
        if(directed) direction=vec4(toward,group);
        return vec4(baked.rgb*2.0,baked.a);
    }
    ivec2 cell=clamp(ivec2(floor(vSun)),ivec2(0),lightSize-1);
    vec4 b=texelFetch(wallBake,min(cell+ivec2(lightSize.x,0),size-1),0);
    if(directed) direction=texelFetch(wallDirection,cell,0);
    float lit=bakeFlicker(direction.a);
    vec4 s=texelFetch(wallDirection,min(cell+ivec2(lightSize.x,0),size-1),0);
    bounce=b.rgb*2.0*bounceFlicker(s.a,lit);ambient=unpackAmbient(b.a);
    basis=s.rgb;
    vec4 baked=texelFetch(wallBake,cell,0);
    return vec4(baked.rgb*2.0*lit,baked.a);
}
