// The fall's rush (scene3d rush.cpp; present.frag): falling through the library's chasm, the
// picture smears along lines from the point it falls toward, as a camera rushing on sees it;
// short near that point, long toward the screen's edges, clear in the middle, all of it growing
// with the speed.
// c.rush: that point in screen uv, homogeneous (xy over z; z 0: it lies off at the side and the
// lines run parallel), and how hard (w, 0-1). The smear steps in whole Doom pixels; the status
// bar is neither smeared nor smeared into the view.
vec3 rush(vec3 color,vec2 uv) {
    vec2 size=vec2(textureSize(image,0)),doomPixel=max(c.fx2.x,1.0)/size;
    if(overlayCovers(uv)) return color;
    vec2 away=uv*c.rush.z-c.rush.xy;
    float spread=length(away);
    if(spread<1e-5) return color;
    // How far from the point (uv; far off when it lies at the side), and from the middle.
    float fromPoint=abs(c.rush.z)>1e-3?spread/abs(c.rush.z):1.0;
    // Clear in the middle as the eye fixes there, and where the weapon is held.
    vec2 middle=(uv-0.5)*vec2(size.x/size.y,1.0);
    float edge=smoothstep(0.12,0.75,length(middle));
    float weapon=(1.0-smoothstep(0.08,0.2,abs(uv.x-0.5)))*smoothstep(0.5,0.65,uv.y);
    float reach=c.rush.w*0.14*min(1.0,fromPoint*1.6)*edge*(1.0-weapon);
    if(reach<doomPixel.y) return color;
    vec2 step=normalize(away)*reach/15.0;
    vec3 sum=color;float weight=1.0;
    for(int i=1;i<16;++i) {
        // Back along the line toward the point: where this pixel's light comes from.
        vec2 at=(floor((uv-step*float(i))/doomPixel)+0.5)*doomPixel;
        if(any(lessThan(at,vec2(0)))||any(greaterThan(at,vec2(1)))||overlayCovers(at)) continue;
        float w=1.0-float(i)/16.0;
        sum+=textureLod(image,at,0.0).rgb*w;weight+=w;
    }
    return sum/weight;
}
