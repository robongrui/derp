// The cells the weather's sheets are made of: shared by the far sheets in
// the sky (sky/sheet.glsl, part of sky.frag) and the rain's cards round the
// lamps (lamp_sheet.glsl, part of flat.frag), so the rain by a lamp is the
// rain past the sky. A sheet is laid out in sheet pixels, in which whatever
// falls stays put; toSheet takes a Doom pixel across and down on the screen
// into the sheet, toScreen back (share and the rest: sky/sheet.glsl).
struct Sheet { float pixel,tic,slant,share; vec2 g; mat2 toSheet,toScreen; };
// A cell's seed, 0-1. An integer hash: sin() of the big numbers the falling
// rows reach loses its precision on the GPU, and neighboring cells then put
// their drops side by side in dashes.
float sheetSeed(vec2 cell,int layer) {
    uvec2 q=uvec2(ivec2(cell)+ivec2(layer*7919,layer*131));
    uint h=q.x*0x8da6b343u^q.y*0xd8163841u;
    h^=h>>16;h*=0x7feb352du;h^=h>>15;h*=0x846ca68bu;h^=h>>16;
    return float(h&0xffffffu)/16777216.0;
}
// Whether the ray's point p (sheet pixels) lies on a Doom pixel square at.
bool sheetPixel(Sheet s,vec2 p,vec2 at) {
    vec2 off=s.toScreen*(p-at);
    return max(abs(off.x),abs(off.y))<0.5;
}
// Whether p lies on a streak whose head is at head, trailing back along its
// way by trail (sheet pixels): one Doom pixel thick across its run on the
// screen, at least one long, steep (rain) or flat (blown sand) alike.
bool sheetStreak(Sheet s,vec2 p,vec2 head,vec2 trail) {
    vec2 run=s.toScreen*trail,mid=s.toScreen*(p-head)-run*0.5;
    if(abs(run.y)>=abs(run.x)) {
        float tall=max(1.0,floor(abs(run.y)+0.5));
        float side=mid.x-(abs(run.y)>1e-3?run.x*mid.y/run.y:0.0);
        return abs(mid.y)<tall*0.5&&abs(side)<0.5;
    }
    float wide=max(1.0,floor(abs(run.x)+0.5));
    return abs(mid.x)<wide*0.5&&abs(mid.y-run.y*mid.x/run.x)<0.5;
}
// Rain: one drop in some cells of rainCell sheet pixels, the share that
// holds one as heavy as the rain (rainShare, of c.weather.x); where in its
// cell the drop's head is, on a whole sheet pixel.
const float rainCell=6.0,rainShare=0.6;
vec2 rainHead(vec2 cell,float seed) {return cell*rainCell+floor(vec2(fract(seed*91.7),fract(seed*37.1))*5.0)+0.5;}
