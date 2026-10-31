// Sector light across the map: seams between sectors, contact shading,
// sun patches and sky sectors, and light flowing through openings.
// Part of world.frag, which declares the resources these read.
// The header's seed (low 16 bits), how open the sector's ground is to the
// wind (bits 16-19, in fifteenths: 1 - rooms.c's enclosure out under the
// sky, under a roof in the open air the open ground's it opens onto, 0
// indoors; wear.cpp). Whether a sector is the open air under a roof (a porch,
// a doorless recess off a yard, rooms.c) its own entry says (w negative,
// scene.cpp's sectorInfo). The open air is one ground: what lies there lies
// as on the open ground round it, the same ripples, gusts and patches across
// a roof's line.
uint wearSeed(vec4 head) {return uint(head.w)&0xffffu;}
float sectorOpen(uint sector) {
    uint base=uint(c.right.w);
    if(base==0u) return sectorInfo[sector].w>0.0?1.0:0.0;
    return float((uint(sectorInfo[base+sector].w)>>16)&15u)/15.0;
}
bool sectorOpenAir(uint sector) {return sectorInfo[sector].w<0.0;}   // under a roof (scene.cpp's sectorInfo)
// Sector light blended across open lines, from cells of this sector or of
// the neighbor it blends with, by the floor or the ceiling weight. Other cells
// (void, unrelated sectors) are skipped.
float seamLight(vec2 xy,uint sector,bool ceiling,float fallback) {
    vec2 g=(xy-c.map.xy)*c.map.z-0.5;ivec2 q=ivec2(floor(g));vec2 f=fract(g);
    ivec2 size=textureSize(seams,0);
    float value=0.0,total=0.0;
    for(int n=0;n<4;++n) {
        ivec2 o=ivec2(n&1,n>>1);
        uvec4 cell=uvec4(round(texelFetch(seams,clamp(q+o,ivec2(0),size-1),0)*65535.0));
        if(cell.x!=sector&&cell.y!=sector) continue;
        float w=(o.x!=0?f.x:1.0-f.x)*(o.y!=0?f.y:1.0-f.y);
        float l=sectorInfo[cell.x].x;
        if(cell.y!=0xFFFFu) l=mix(l,sectorInfo[cell.y].x,float(ceiling?cell.z>>8:cell.z&255u)/510.0);
        value+=l*w;total+=w;
    }
    return total>0.0001?value/total:fallback;
}
// Contact shading: floors and ceilings darken near walls, walls near their
// sector's floor and (non-sky) ceiling and in inside corners, each as a
// crease: darkest at the contact and mostly gone within half its reach.
// Sector heights follow moving floors. Distances in the contact map run to
// contactRange (scene3d/map.cpp).
const float contactRange=48.0;
float crease(float distance,float reach) {float open=1.0-saturate(distance/reach);return open*open;}
// A floor's or ceiling's distance to the nearest wall, blended from the four
// nearest cells of this sector only, so the bright cells on top of a step
// do not wash out the crease at its foot.
float flatContact(vec2 xy,uint sector,bool ceiling) {
    vec2 g=(xy-c.map.xy)*c.map.z-0.5;ivec2 q=ivec2(floor(g));vec2 f=fract(g);
    ivec2 size=textureSize(contact,0);
    float value=0.0,total=0.0;
    for(int n=0;n<4;++n) {
        ivec2 o=ivec2(n&1,n>>1),cell=clamp(q+o,ivec2(0),size-1);
        if(uint(round(texelFetch(seams,cell,0).x*65535.0))!=sector) continue;
        float w=(o.x!=0?f.x:1.0-f.x)*(o.y!=0?f.y:1.0-f.y)+1e-4;
        vec4 v=texelFetch(contact,cell,0);
        value+=(ceiling?v.g:v.r)*w;total+=w;
    }
    return total>0.0?value/total*contactRange:contactRange;
}
float contactShade(vec3 world,vec3 normal,uint sector) {
    vec4 info=sectorInfo[sector];
    if(normal.z>0.5) return 1.0-0.5*crease(flatContact(world.xy,sector,false),40.0);
    if(normal.z<-0.5) return 1.0-0.4*crease(flatContact(world.xy,sector,true),32.0);
    float shade=1.0-0.45*crease(world.z-info.y,40.0);
    if(info.w<=0.0) shade*=1.0-0.35*crease(info.z-world.z,32.0);
    // Inside corners: a point cornerReach out from the wall lies that far
    // from the wall itself, but only as far from a wall meeting this one as
    // the fragment is from the corner. Walls holding up both floor and
    // ceiling shade the corner's full height; low and high ones (ledges,
    // steps, lintels) only near the floor or ceiling.
    const float cornerReach=32.0;
    vec2 uv=(world.xy+normal.xy*cornerReach-c.map.xy)*c.map.z/vec2(textureSize(contact,0));
    vec2 near=textureLod(contact,uv,0.0).rg*contactRange;
    vec2 corner=vec2(crease(near.x,cornerReach),crease(near.y,cornerReach));
    float cornered=max(min(corner.x,corner.y),
        max(corner.x*crease(world.z-info.y,48.0),info.w<=0.0?corner.y*crease(info.z-world.z,48.0):0.0));
    return shade*(1.0-0.5*cornered);
}
// Sky sectors: x how enclosed their opening is (0 open air, 1 a hole in a
// ceiling; the weather's open share reads it), y the light of the roofed
// sectors round their opening (lightRule's D; 0 for none); unpacks
// packOutdoor in scene3d/bake_sun.cpp. 0 for any other sector, the open air
// under a roof too (w negative: its yard's, for the weather).
vec2 outdoorShape(uint sector) {
    float v=sectorInfo[sector].w-1.0;
    return v>=0.0?vec2(floor(v)/255.0,round(fract(v)*256.0)/255.0):vec2(0.0);
}
// A blizzard's whiteout (c.climate.z, whiteout.glsl) or a thick fog
// (WEATHER_FOG) lights open ground from all round, as a whiteout is known
// for: no darker corners and wall feet, no shade where less of the sky is
// seen. How much, for a sector out under the sky; 0 for any other. Under the
// whiteout's haze those bands stood grey on the snow along every wall of
// DOOM 2's streets.
float evenAir() {
    float fog=c.weather.w==WEATHER_FOG&&c.bank.w>0.0?0.75*saturate(c.weather.x*1.25):0.0;
    return max(c.climate.z,fog);
}
float flatAir(uint sector) {return sectorInfo[sector].w>0.0?evenAir():0.0;}
// flatAir for a fragment: floors and ceilings by their own sector, walls by
// the sector they face, a cell and a half out along the normal (the fog
// bank's way, world.frag): a building's wall belongs to the closed or roofed
// sector behind it and kept its dark foot on the street.
float flatAirAt(vec3 world,vec3 normal,uint sector) {
    if(abs(normal.z)<=0.5&&c.map.z>0.0) {
        ivec2 cell=clamp(ivec2(floor((world.xy+normal.xy*1.5/c.map.z-c.map.xy)*c.map.z)),ivec2(0),textureSize(seams,0)-1);
        uint own=uint(round(texelFetch(seams,cell,0).x*65535.0));
        if(own!=0xFFFFu) sector=own;
    }
    return flatAir(sector);
}
// The one light rule, for every surface (the full account at lightRule in
// scene3d/bake_sun.cpp, kept equivalent): three anchors from the sector's
// light, D seeing neither sky nor sun, K seeing all the sky, U in full sun,
// joined by what the bakes traced there as
//     D + (K - D) * open + (U - K) * visible.
// Under a roof the light is the room's (D) and sky and sun rise from it;
// under the sky it is open sunlit ground's (U), open shade falls below it,
// and ground that sees no sky lies at the rooms round the opening. visible
// is -1 where there is no sun bake (ceilings). Returns the level and the
// parts of it that take the sun's and the sky's tint. even: a whiteout's
// even light there (flatAir), which takes the sun's shadows and the shade of
// seeing less sky.
vec3 lightRule(uint sector,float light,float visible,float open,bool windowLight,float even) {
    bool sun=c.sun.w>0.0&&visible>=0.0;
    float v=sun?visible:0.0;
    if(sectorInfo[sector].w>0.0) {
        float shade=sun?c.outdoor.x*(1.0-even):0.0,gain=sun?c.outdoor.y*(1.0-even):0.0,room=outdoorShape(sector).y;
        float k=light*(1.0-shade),u=light*(1.0+gain),h=mix(saturate(open),1.0,even);
        float d=c.fx2.y<=0.0?k:room>0.0?min(room,light):k*0.75;
        if(room>0.0) k=max(k,min(room,light));
        float level=d+(k-d)*h+(u-k)*v;
        return vec3(level,light*gain*v,c.outdoor.z*h*level);
    }
    float lift=c.fx2.y>0.0&&!windowLight?max(0.0,c.fx2.y-light)*0.7:0.0;
    float u=max(c.sun.w,light),h=mix(saturate(open*1.2),1.0,even);
    v*=1.0-even;
    return vec3(light+lift*h+(u-light)*v,(u-light)*v,lift*h);
}
// lightRule blended across open lines as seamLight blends the sector light:
// each of the four nearest cells takes the rule of its sector and of the
// neighbor it blends with, at their current light plus add (the flow), times
// contact, so the rules of a roof and the sky beside it meet without a
// step. Returns the blended rule (level, sun part, sky part) and, in w, the
// blended light it started from.
vec4 seamRule(vec2 xy,uint sector,bool ceiling,float fallback,float add,float contact,float visible,float open,bool windowLight,out vec2 parts) {
    vec2 g=(xy-c.map.xy)*c.map.z-0.5;ivec2 q=ivec2(floor(g));vec2 f=fract(g);
    ivec2 size=textureSize(seams,0);
    vec4 value=vec4(0);float total=0.0;
    for(int n=0;n<4;++n) {
        ivec2 o=ivec2(n&1,n>>1);
        uvec4 cell=uvec4(round(texelFetch(seams,clamp(q+o,ivec2(0),size-1),0)*65535.0));
        if(cell.x!=sector&&cell.y!=sector) continue;
        float w=(o.x!=0?f.x:1.0-f.x)*(o.y!=0?f.y:1.0-f.y);
        float l=(sectorInfo[cell.x].x+add)*contact;
        vec4 r=vec4(lightRule(cell.x,l,visible,open,windowLight,flatAir(cell.x)),l);
        if(cell.y!=0xFFFFu) {
            float m=(sectorInfo[cell.y].x+add)*contact;
            r=mix(r,vec4(lightRule(cell.y,m,visible,open,windowLight,flatAir(cell.y)),m),float(ceiling?cell.z>>8:cell.z&255u)/510.0);
        }
        value+=r*w;total+=w;
    }
    if(total<=0.0001) {float l=(fallback+add)*contact;value=vec4(lightRule(sector,l,visible,open,windowLight,flatAir(sector)),l);total=1.0;}
    value/=total;
    parts=value.yz;
    return value;
}
// Sector light flow (bakeFlow): the cell's light rises toward the current
// light of the sector it sees through openings, in 1/16 steps. Walls read
// the cell one step toward their visible side. Keep flowLightAt in
// scene3d/bake_indirect.cpp equivalent.
float flowLight(vec3 world,vec3 normal,uint sector,float light) {
    vec2 at=abs(normal.z)>0.5?world.xy:world.xy+normal.xy/c.map.z;
    if((uint(c.flashlightTint.w)&FEATURE_TEXEL_BAKE)!=0u&&abs(normal.z)>0.5) at=floor(at)+0.5;
    ivec2 cell=clamp(ivec2(floor((at-c.map.xy)*c.map.z)),ivec2(0),textureSize(seams,0)-1);
    uvec4 s=uvec4(round(texelFetch(seams,cell,0)*65535.0));
    if(s.x!=sector||s.w==0xFFFFu) return light;
    return light+round(max(0.0,sectorInfo[s.w].x-light)*texelFetch(contact,cell,0).a*0.8*16.0)/16.0;
}
// The light a fragment takes from its sector and the bakes, in Doom's 1/16
// steps: seams and flow across sector edges, contact shading, the baked
// surface data, sun and sky light, occlusion and the distance fade. shown is
// the light before contact shading; sunShare and skyShare the parts that take
// the sun's and the sky's tint.
struct SectorLit { float light,shown,sunShare,skyShare,occlusion; vec4 baked,direction; vec3 bounce; };
SectorLit sectorLighting(vec3 normal,bool mapped,uint sector,uint bakeFlags) {
    float light=vLight,sunShare=0.0;
    bool seamed=mapped&&c.map.w>0.0&&abs(normal.z)>0.5;
    float seamedLight=light;
    if(seamed) light=seamedLight=seamLight(vWorld.xy,sector,normal.z<0.0,light);
    if(mapped&&(bakeFlags&FEATURE_LIGHT_FLOW)!=0u) light=flowLight(vWorld,normal,sector,light);
    // A whiteout's even light (flatAir): by the sector, and wherever the
    // bake saw any sky (ambient.x), as on a map that is all streets the
    // sectors don't tell the open air; rooms that see none keep their shade.
    vec3 bounce=vec3(0);vec2 ambient=vec2(0,1);vec4 direction=vec4(0.5,0.5,0,0);
    vec4 baked=mapped?bakedSurface(normal,sector,bounce,ambient,direction):vec4(0,0,0,-1);
    float even=mapped?max(flatAirAt(vWorld,normal,sector),evenAir()*smoothstep(0.0,0.25,ambient.x)):0.0;
    float flowAdd=light-seamedLight;
    // Static light fills the headroom above this light, which seams and flow
    // already carry smoothly across sector edges, so a bright sector beside a
    // dark one shows no edge in the lamp's color.
    float shownLight=light;
    // Contact shading darkens the baked lamp and bounce light too (through
    // occlusion), or lamp-lit rooms would wash their corners out.
    float contact=mapped&&c.map.w>0.0?mix(contactShade(vWorld-vec3(0.0,0.0,vLift),normal,sector),1.0,even):1.0;
    light*=contact;
    // Sun and sky light: the one rule (lightRule), blended across seams on
    // floors and ceilings, its change from the sector light in 1/16 steps.
    // With window light (FEATURE_WINDOW_LIGHT) the openings light indoor
    // surfaces from the bake, with direction, so they take no sky lift.
    float skyShare=0.0;
    if(mapped&&(c.sun.w>0.0||c.fx2.y>0.0)) {
        bool windowLight=(bakeFlags&FEATURE_WINDOW_LIGHT)!=0u;
        vec2 parts;
        vec4 r=seamed?seamRule(vWorld.xy,sector,normal.z<0.0,seamedLight,flowAdd,contact,baked.a,ambient.x,windowLight,parts)
                     :vec4(lightRule(sector,light,baked.a,ambient.x,windowLight,even),light);
        light=r.w+round((r.x-r.w)*16.0)/16.0;
        sunShare=r.y/max(r.x,0.001);skyShare=r.z/max(r.x,0.001);
    }
    // Baked occlusion (FEATURE_BAKED_OCCLUSION) darkens the sector and baked light, not dynamic lights.
    float occlusion=1.0;
    if(mapped&&(bakeFlags&FEATURE_BAKED_OCCLUSION)!=0u) {occlusion=round((1.0-0.45*(1.0-mix(ambient.y,1.0,even)))*16.0)/16.0;light*=occlusion;}
    occlusion*=contact;
    if(c.effects.z==0.0) light*=clamp(1.0-vDistance/3200.0,0.35,1.0);
    else light=1.0;
    return SectorLit(light,shownLight,sunShare,skyShare,occlusion,baked,direction,bounce);
}
