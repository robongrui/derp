// Whether the 2D layer covers a point of the screen (uv): the status bar, the menus, the library's
// questions (the hud image and its place, hudRect). Effects on the view leave it alone.
bool overlayCovers(vec2 uv) {
    vec2 hudUV=(uv-hudRect.xy)/hudRect.zw;
    return all(greaterThanEqual(hudUV,vec2(0)))&&all(lessThanEqual(hudUV,vec2(1)))&&textureLod(hud,hudUV,0.0).a>0.0;
}
