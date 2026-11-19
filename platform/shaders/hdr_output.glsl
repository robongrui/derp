// The picture as the display takes it (gpu3d/hdr_output.cpp, PLAN-hdr.md), for the two passes that
// draw into the swapchain (present.frag, panel.frag). hdr.x: 0 SDR, 1 extended linear (scRGB),
// 2 HDR10 (PQ, BT.2020); 3 and 4 the same with vivid colours: the picture's primaries read as
// Display P3's, as the Steam Deck OLED shows SDR (gamescope stretches SDR to the panel's own wide
// gamut; shown accurately, HDR looked duller beside it); hdr.y: paper white (scRGB: the OS's SDR white, 1.0 being 80 nits; PQ: in
// nits); hdr.z: the display's headroom over paper white; hdr.w: how far light over white rises into it.
// SDR: the colour as drawn. HDR: the drawn picture at paper white, read as gamma 2.2 so DOOM's
// blacks stay black (the piecewise sRGB curve lifts them), and above it the light over white
// (linear, in paper whites), its brightest channel eased into the headroom so a fireball stays
// orange instead of clipping to white.
vec3 encodeOutput(vec3 color,vec3 above,vec4 hdr) {
    if(hdr.x<0.5) return color;
    bool vivid=hdr.x>2.5;
    float mode=vivid?hdr.x-2.0:hdr.x;
    vec3 linear=pow(saturate(color),vec3(2.2));
    float room=max(hdr.z-1.0,0.0),most=max(above.r,max(above.g,above.b));
    if(room>0.0&&most>1e-5) linear+=above*(room*(1.0-exp(-most/room))/most);
    // scRGB: BT.709 primaries; vivid ones from Display P3 (below zero where P3 reaches past them).
    const mat3 p3To709=mat3(1.2249,-0.0420,-0.0197, -0.2247,1.0419,-0.0786, 0.0,0.0,1.0979);
    if(mode<1.5) return (vivid?p3To709*linear:linear)*hdr.y;
    // HDR10: BT.709 (or vivid, P3) primaries carried into BT.2020, then the PQ curve.
    const mat3 to2020=mat3(0.6274,0.0691,0.0164, 0.3293,0.9195,0.0880, 0.0433,0.0114,0.8956);
    const mat3 p3To2020=mat3(0.7538,0.0457,-0.0012, 0.1986,0.9418,0.0176, 0.0476,0.0125,0.9836);
    vec3 level=clamp((vivid?p3To2020:to2020)*linear*(hdr.y/10000.0),0.0,1.0);
    const float m1=0.1593017578125,m2=78.84375,c1=0.8359375,c2=18.8515625,c3=18.6875;
    vec3 p=pow(level,vec3(m1));
    return pow((c1+c2*p)/(1.0+c3*p),vec3(m2));
}
