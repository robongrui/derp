// Whether the 2D layer covers a point of the screen (uv): the status bar, the menus, the library's
// questions (the hud image and its place, hudRect). Effects on the view leave it alone.
// The status bar apart from the frame (barSpan.z; scene3d/status_bar.cpp): its rows of the image
// lie in bar, and across barSpan.x-y beside it its wide sides cover the screen whole; the
// frame keeps the rows above barSpan.w.
// The hourglass (barGlass; scene3d/hourglass.cpp) in the bar's DOOM pixels, the bar apart or in the
// HUD's frame (its rows 168-200): its texel there, none outside it.
vec4 hourglassColor(vec2 uv) {
    if(barGlass.z<=0.0) return vec4(0.0);
    vec2 origin,pixel;
    if(barSpan.z>0.0) {origin=bar.xy;pixel=bar.zw/vec2(320.0,max((1.0-barSpan.w)*200.0,1.0));}
    else {pixel=hudRect.zw/vec2(320.0,200.0);origin=hudRect.xy+vec2(0.0,168.0)*pixel;}
    vec2 at=(uv-origin)/pixel-barGlass.xy;
    if(any(lessThan(at,vec2(0.0)))||any(greaterThanEqual(at,barGlass.zw))) return vec4(0.0);
    return texelFetch(barGlassImage,ivec2(at),0);
}
float overlayCover(vec2 uv) {
    if(hourglassColor(uv).a>0.0) return 1.0;
    if(barSpan.z>0.0&&uv.y>=bar.y) {
        if(uv.x<barSpan.x||uv.x>barSpan.y) return 0.0;
        vec2 at=(uv-bar.xy)/bar.zw;
        if(at.x<0.0||at.x>1.0) return 1.0;
        return textureLod(hud,vec2(at.x,mix(barSpan.w,1.0,at.y)),0.0).a;
    }
    vec2 hudUV=(uv-hudRect.xy)/hudRect.zw;
    if(any(lessThan(hudUV,vec2(0)))||any(greaterThan(hudUV,vec2(1)))) return 0.0;
    if(barSpan.z>0.0&&hudUV.y>=barSpan.w) return 0.0;
    return textureLod(hud,hudUV,0.0).a;
}
bool overlayCovers(vec2 uv) {return overlayCover(uv)>0.0;}
// What the 2D layer shows there (rgb) and how much of it (a): the HUD image's texel in the frame
// or in the bar apart, and beside the bar the wide STBAR's sides (barSide.xy: left and width in
// uv) carried on to the screen's edges, or else the greyed border flat (barSide.zw: its repeats
// per uv, lined up with the bar; scene3d/status_bar.cpp).
vec4 overlayColor(vec2 uv) {
    vec4 glass=hourglassColor(uv);
    if(glass.a>0.0) return vec4(glass.rgb,1.0);
    if(barSpan.z>0.0&&uv.y>=bar.y) {
        if(uv.x<barSpan.x||uv.x>barSpan.y) return vec4(0.0);
        vec2 at=(uv-bar.xy)/bar.zw;
        if(at.x>=0.0&&at.x<=1.0) return textureLod(hud,vec2(at.x,mix(barSpan.w,1.0,at.y)),0.0);
        if(barSide.y>0.0) {
            // The wide STBAR, and past its ends its own sides (the columns beyond the classic bar's
            // 320) again and again, mirrored each time so no seam shows. Not their last 4 columns:
            // the bevel where the classic bar begins (the KEX bar's 125-128 and 447-450).
            float width=float(textureSize(barWide,0).x),side=max((width-320.0)*0.5-4.0,1.0);
            float column=(uv.x-barSide.x)/barSide.y*width;
            float past=column<0.0?-column:column-width;
            if(past>0.0) {
                float t=mod(past,2.0*side),c=t<side?t:2.0*side-t;
                column=column<0.0?c:width-c;
            }
            vec4 wide=textureLod(barWide,vec2(clamp(column/width,0.0,1.0),at.y),0.0);
            if(wide.a>0.0) return vec4(wide.rgb,1.0);
        }
        if(barSide.z>0.0) return vec4(textureLod(barFlat,(uv-bar.xy)*barSide.zw,0.0).rgb,1.0);
        return vec4(0.0,0.0,0.0,1.0);
    }
    vec2 hudUV=(uv-hudRect.xy)/hudRect.zw;
    if(any(lessThan(hudUV,vec2(0)))||any(greaterThan(hudUV,vec2(1)))) return vec4(0.0);
    if(barSpan.z>0.0&&hudUV.y>=barSpan.w) return vec4(0.0);
    return textureLod(hud,hudUV,0.0);
}
