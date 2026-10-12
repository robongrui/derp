// Lighting of world sprites drawn with silhouette normals.
// Part of world.frag, which declares the resources these read.
// Detail sprites (mode 8): dynamic lights and, with bake-only lights, the
// static light in the tint shade the silhouette normals, added to pulse;
// wet blood glints into specular. color: the sprite's texel.
void billboardLight(vec4 color,inout vec3 pulse,inout vec3 specular) {
    // Billboards face the camera horizontally and stand upright.
    ivec2 size=textureSize(image,0);
    vec2 packed=texelFetch(image,clamp(ivec2(floor(vUV)),ivec2(0),size-1),0).ba*2.0-1.0;
    if((vMode&16u)!=0u) packed.x=-packed.x;
    vec3 toward=normalize(vec3(-c.forward.x,-c.forward.y,0.0001));
    vec3 across=vec3(c.right.xy,0);
    vec3 n=normalize(across*packed.x-vec3(0,0,1)*packed.y+toward*sqrt(saturate(1.0-dot(packed,packed))));
    vec3 view=normalize(c.eye.xyz-vWorld);
    uvec2 mask=vLightMask;
    // Mode 2048: airborne blood glints on its rounded normals like the
    // floor blood, in a brighter palette red and two hard steps.
    bool wetSprite=(vMode&2048u)!=0u;
    vec3 wetColor=wetSprite?texelFetch(paletteLUT,ivec3(round(saturate(color.rgb*1.8+0.04)*31.0)),0).rgb:vec3(0);
    if(c.bake.z>0.0&&vStatic!=0u) {
        // Bake-only lights: the static light in the tint takes the
        // same shaping as a dynamic light from where it mostly comes.
        vec4 statics=unpackUnorm4x8(vStatic);
        vec3 toLight=normalize(statics.xyz*2.0-1.0);
        float facing=saturate(dot(n,toLight));
        float rim=pow(1.0-saturate(dot(n,view)),2.0)*facing*saturate(0.35-dot(toLight,view));
        vec3 tint=pulse;
        pulse*=mix(1.0,0.4+0.6*facing+1.6*rim,statics.w);
        if(wetSprite) {
            float highlight=pow(saturate(dot(n,normalize(toLight+view))),24.0);
            highlight=highlight>0.5?1.0:highlight>0.2?0.5:0.0;
            specular+=wetColor*tint*statics.w*highlight*0.8;
        }
    }
    while(any(notEqual(mask,uvec2(0)))) {
        uint word=mask.x!=0u?0u:1u;
        uint i=word*32u+uint(findLSB(mask[word]));
        mask[word]&=mask[word]-1u;
        float amount=flashAt(vWorld,i);
        if(amount<=0.0) continue;
        if(c.bake.x>0.0) {
            float luminance=amount*max(dot(lights[i].color.rgb,vec3(0.299,0.587,0.114)),0.001);
            amount*=mix(1.0,bakeShoulder(luminance,bakeHeadroom(vLight))/luminance,lights[i].baked);
        }
        vec3 toLight=normalize(lights[i].position.xyz-vWorld);
        float facing=saturate(dot(n,toLight));
        // Backlit edges: silhouette normals that face the light glow.
        float rim=pow(1.0-saturate(dot(n,view)),2.0)*facing*saturate(0.35-dot(toLight,view));
        // A static light's flat share already arrives as baked tint;
        // this keeps only its shaping across the silhouette.
        float bakedShare=c.bake.x>0.0?lights[i].baked:0.0;
        pulse+=lights[i].color.rgb*amount*(0.4+0.6*facing+1.6*rim-bakedShare);
        if(wetSprite) {
            float highlight=pow(saturate(dot(n,normalize(toLight+view))),24.0);
            highlight=highlight>0.5?1.0:highlight>0.2?0.5:0.0;
            specular+=wetColor*lights[i].color.rgb*amount*highlight*0.8;
        }
    }
}
