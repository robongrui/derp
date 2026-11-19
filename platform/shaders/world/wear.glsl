// Wear (scene3d/wear.cpp, scene3d/weather.cpp): what weather and neglect have
// left on the map's surfaces, all on the texel grid of Doom's one-unit texels.
// - FEATURE_WINDOW_STREAKS: rain runs down the wall below a window from its
//   sill, column by column to ragged ends, darkest at the sill; rusty on
//   metal. They darken further while it rains.
// - Under the sky, while it rains (c.up.w), the ground lies wet in steps
//   baked per map cell (weather.cpp bakeWetness, in the floor light's alpha):
//   nearly dry in the lee of walls, soaked at the foot of walls facing the
//   rain, patches damp, wet and soaked between, puddles darkest; under a
//   roof's edge downwind as far as the gusts blow it in, thinning out. Without the
//   bake, patches of puddles. Walls a little, most near their feet. Where rain blew in through a
//   broken pane the sill and floor in front of it are wet, fanning out past
//   the frame's ends as it collects. Darkening only: damp, never glossy.
//   Nukage rain (c.weatherTint) lies the same way, tinted its colour.
// - While it snows (c.weather.w WEATHER_SNOW), or sand or ash settles
//   (WEATHER_SAND, WEATHER_ASH, WEATHER_FIRESTORM), the ground under the sky lies white (or in
//   their colour, world/settled.glsl) instead, as deep as baked in the same place (weather.cpp bakeWetness):
//   in clumps of two texels with ragged rims, deep snow solid, thin snow
//   broken with the ground showing through (snowAt); against the foot of
//   the walls a ragged drift as high as the ground right in front holds,
//   climbing higher on walls facing the wind, so buildings grow out of the
//   ground, and a little caught in runs along the bricks' ledges. Where
//   it blew in through a broken pane it settles in front of it in coarse
//   clumps, fanning out past the frame's ends as it reaches in. Snow is
//   matte and smooth: no gloss, no bump (snowWear).
// - Leaks while it rains: a crack in the ceiling, damp round it, and a
//   small puddle on the floor below.
// - Water splashes (scene3d/puddles.cpp): where a plunge threw water out of
//   its pool, the floor and walls lie wet, as rain leaves them: darker only.
// - Worn plates (Game_WornPlate): plain metal plates marked by use, see
//   plateGrime. Not weather: they stay as they are.
// Records follow the sector entries in sectorInfo from c.right.w: per
// sector (first record, count, ice, seed), then two entries a record. ice
// (Phobos, decompression.cpp poolIceCode): a pool's ice sheet 0-1, plus two
// times the glow left in fifteenths (poolIce below).
//   1 streak: (1, line*2+side, sill height, amount; negative: rust), (a, b)
//   3 wet patch: (3, depth in, -, amount), (a, b) along the room's side
//   4 ceiling crack: (4, center, reach), (seed, -, -, -)
//   5 puddle: (5, center, radius), (seed, -, -, amount)
//   6 frost round a breach (Phobos), or a cold opening a draft comes in by
//     (drafts.cpp): (6, reach, -, amount), (a, b) the opening
//   7 a shot's crack in a pool's ice (Phobos): (7, center, reach), (seed, -, -, -)
//   8 water splash: (8, center x, y, z), (radius, seed, -, amount)
//   9 a draft's trail (drafts.cpp): (9, half width, amount, state 2 snow / 1 slush / 0 wet), (a, b)
//  10 a crater a blast dug indoors (disturbances.cpp): (10, center, radius), (seed, filled back 0-1, how hard, -)
// Part of world.frag, which declares the resources these read.
struct Wear { float wet,streak;bool rust;float crack,grime,frost,iceCrack,snow;bool indoors;float open;vec2 flow; };   // indoors: under a roof; open: to the wind; flow: a draft's way there, as strong as it runs (settledColor)
float wearHash(ivec2 p,uint seed) {return float(tileHash(p,seed)&0xffffu)/65535.0;}
float wearNoise(vec2 p,float cell,uint seed) {
    vec2 g=p/cell;ivec2 i=ivec2(floor(g));vec2 f=fract(g);f=f*f*(3.0-2.0*f);
    return mix(mix(wearHash(i,seed),wearHash(i+ivec2(1,0),seed),f.x),mix(wearHash(i+ivec2(0,1),seed),wearHash(i+ivec2(1,1),seed),f.x),f.y);
}
// What lies loose blowing over itself in a gust (snow, sand; powder.cpp's
// lift, wind.glsl): streaks 24 texels long and 3 across, running downwind
// at the wind's speed and the gusts' shove, a step per tic; how much at
// texel t (0 in a lull, in still air, under a roof; open: how open the
// sector is to the wind). Coherent, never a texel flickering on its own.
float blowing(vec2 t,float open) {
    float wind=length(c.weather.yz);
    if(open<=0.0||wind<=1.0||c.weather.w==WEATHER_ASH||c.weather.w==WEATHER_FIRESTORM) return 0.0;
    float tic=windTic(),lift=windLiftAt(t,tic)*open;
    if(lift<=0.0) return 0.0;
    vec2 way=c.weather.yz/wind;
    float run=dot(t,way)-wind*0.8*tic-windGustAt(t,tic),across=dot(t,vec2(-way.y,way.x));
    float n=wearNoise(vec2(floor(run/8.0),across),3.0,0x510e527fu);
    return n>0.78-0.12*min(lift,1.0)?min(lift,1.0):0.0;
}
// A rain streak's darkness at a wall texel: column (along the wall from
// the window's edge, len wide) and depth below the sill. Columns run in
// groups of three to their own lengths, a few texels missing on the way.
float streakAt(float along,float len,float depth,float below,uint seed) {
    if(along<0.0||along>len||depth<0.0) return 0.0;
    int column=int(floor(along)),row=int(floor(depth));
    float own=wearHash(ivec2(column,0),seed),group=wearHash(ivec2(column/3,1),seed);
    float reach=4.0+below*(0.2+0.8*mix(group,own,0.35)*mix(group,own,0.35));
    if(min(along,len-along)<3.0) reach*=0.5;
    if(depth<2.0) return 0.8; // The drip line just under the sill.
    if(depth>reach||wearHash(ivec2(column,row),seed^0x68bc21ebu)<0.12) return 0.0;
    return pow(1.0-depth/reach,0.7)*(0.55+0.45*own);
}
float segmentDistance(vec2 p,vec2 a,vec2 b) {
    vec2 ab=b-a;float t=clamp(dot(p-a,ab)/max(dot(ab,ab),1e-4),0.0,1.0);
    return length(p-a-ab*t);
}
// A ceiling crack at a texel t: three branches from the center, each four
// steps turning as they go and thinning, about a texel wide; damp in a
// ragged patch round the center. Returns (crack, damp).
vec2 crackAt(vec2 t,vec2 center,float reach,uint seed) {
    float r=length(t-center);
    if(r>reach+2.0) return vec2(0.0);
    float crack=0.0,start=wearHash(ivec2(0,0),seed)*6.2831853;
    for(int branch=0;branch<3;++branch) {
        float angle=start+float(branch)*2.0944+(wearHash(ivec2(branch,1),seed)-0.5)*1.2,width=0.65;
        vec2 p=center;
        for(int step=0;step<4;++step) {
            angle+=(wearHash(ivec2(branch,step+2),seed)-0.5)*1.5;
            vec2 q=p+vec2(cos(angle),sin(angle))*reach*0.25*(0.7+0.6*wearHash(ivec2(branch,step+8),seed));
            if(segmentDistance(t,p,q)<width) crack=max(crack,1.0-0.15*float(step));
            p=q;width*=0.85;
        }
    }
    float limit=reach*0.5*(0.7+0.6*wearNoise(t,6.0,seed^0x9e3779b9u));
    return vec2(crack,r<limit?0.7:0.0);
}
// Snow lying on a texel t by how deep it lies there (0-1): in clumps of two
// texels and single-texel rims round them; 1 solid, 0.55 a thin dusting
// the texture shows through, 0 bare.
float snowAt(vec2 t,float deep) {
    if(deep<=0.0) return 0.0;
    float n=0.6*wearNoise(t,10.0,0x5f356495u)+0.25*wearHash(ivec2(floor(t*0.5)),0x2c1b3c6du)+0.15*wearHash(ivec2(t),0x7feb352du);
    float edge=deep*1.1-n;
    return edge>0.12?1.0:edge>-0.06?0.55:0.0;
}
// The baked wetness at a ground texel (-1: none baked), each texel reading
// a cell up to half a cell off, so the steps meet in ragged whole texels;
// where that cell has none, its own. Under a roof only where its own cell
// has some, so none seeps through a wall from the ground outside.
float bakedWetness(vec2 t,bool outdoors) {
    ivec2 size=textureSize(flatLight,0),layer=max(size/ivec2(1,4),ivec2(1));
    vec2 jitter=vec2(wearHash(ivec2(t),0x2545f491u),wearHash(ivec2(t),0x7f4a7c15u))-0.5;
    vec2 at=(t-c.map.xy)*c.map.z;
    float own=texelFetch(flatLight,clamp(ivec2(floor(at)),ivec2(0),layer-1),0).a;
    if(!outdoors&&own<=0.0) return -1.0;
    float a=texelFetch(flatLight,clamp(ivec2(floor(at+jitter)),ivec2(0),layer-1),0).a;
    if(a<=0.0) a=own;
    return a>0.0?(a*255.0-1.0)/254.0:-1.0;
}
// What settles lying on an open ground texel t, in snowAt's steps: as the
// bake laid it, else in broad patches (as surfaceWear lays the ground).
float groundSnow(vec2 t) {
    float baked=bakedWetness(t,true);
    return snowAt(t,baked>=0.0?baked:0.25+0.65*smoothstep(0.4,0.7,wearNoise(t,44.0,0x3c6ef372u)));
}
// The way u runs along a wall in the world (world.frag sets it from the
// texture frame before surfaceWear).
vec2 wallAlong=vec2(1.0,0.0);
// How much lies on the ground along block b (four texels of u) of a wall's
// foot, half and one and a half units out: at, the point on the wall at u;
// away, out from it. Fixed blocks, so a drift's height blends from block to
// block instead of flickering column by column. Placed by u from the point
// itself, not by world coordinates against the wall's normal (rebuilt from
// screen derivatives, it wobbles enough to move a dot product by most of a
// unit out at map coordinates); each sample in a texel's middle, as walls
// along the axes lie on whole units where floor() would flip between two.
float footCover(vec2 at,float u,vec2 away,float b) {
    float sum=0.0;
    for(int i=0;i<2;++i)for(int d=1;d<=2;++d)
        sum+=groundSnow(floor(at+wallAlong*(b*4.0+1.5+2.0*float(i)-u)+away*(float(d)-0.5))+0.5);
    return sum*0.25;
}
// A draft's trail at a floor texel (record 9: half widths at a and b in
// eighths, packed a + 1024 b; amounts in 255ths, a + 256 b; state 2 snow /
// 1 slush / 0 wet; the segment a-b): a ragged band of whole texels along
// the segment, tapering from one end's width and amount to the other's,
// fullest down its middle. Snow in two-texel clumps as the window's patch
// lies; slush wet with a few white specks.
void draftTrail(vec2 t,vec4 a,vec4 b,inout Wear w) {
    vec2 ab=b.zw-b.xy;
    float along=clamp(dot(t-b.xy,ab)/max(dot(ab,ab),1e-4),0.0,1.0),d=length(t-b.xy-ab*along);
    vec2 widths=vec2(mod(a.y,1024.0),floor(a.y/1024.0))/8.0;
    if(d>=max(widths.x,widths.y)*1.25) return;   // past the raggedest edge
    float edge=mix(widths.x,widths.y,along)*(0.55+0.7*wearNoise(t,9.0,0x6a09e667u));
    if(d>=edge) return;
    float inner=1.0-d/edge,amount=mix(mod(a.z,256.0),floor(a.z/256.0),along)/255.0;
    if(inner*amount>length(w.flow)) w.flow=normalize(ab)*inner*amount;   // the way the air runs here (settledColor's ripples)
    if(a.w>1.5) {
        float clump=wearHash(ivec2(floor(t*0.5)),0x3c6ef372u);
        if(clump<inner*amount*1.3) w.snow=max(w.snow,clump<inner*amount*0.7?1.0:0.55);
    }
    else if(wearHash(ivec2(t),0x85ebca6bu)*0.6<inner) {
        w.wet=max(w.wet,amount*(0.45+0.55*inner));
        if(a.w>0.5&&wearHash(ivec2(t),0xbb67ae85u)<0.12*inner*amount) w.snow=max(w.snow,0.55);
    }
}
// A sector's records (wear.cpp): head.y counts them, the drafts' trails
// (kind 9) apart in its high bits and stored last, so only the floor's loop
// reads them: every texel of a room reads its records.
uint wearOthers(vec4 head) {return uint(head.y)&4095u;}
uint wearTrails(vec4 head) {return uint(head.y)>>12;}
// Frost round a breach at a texel: whole texels of rime thick at the opening,
// thinning out in clumps to the record's reach. Clumped on the surface's own grid
// (a wall's: along it and up), with only a little texel jitter, not salt and pepper.
float breachFrostAt(vec3 world,vec3 normal,float u,uint sector,uint base) {
    vec4 head=sectorInfo[base+sector];
    uint first=uint(head.x),count=wearOthers(head);
    vec2 t=floor(world.xy)+0.5;
    vec2 face=abs(normal.z)>0.5?world.xy:vec2(u,world.z);   // a wall's texture column (shoreWet)
    vec2 cell=floor(face)+0.5;
    float clump=0.85*wearNoise(cell,5.0,0x2f3d5a11u)+0.15*wearHash(ivec2(cell),0x51ed270bu);
    float frost=0.0;
    for(uint i=0u;i<count;++i) {
        vec4 a=sectorInfo[first+2u*i];
        if(uint(a.x)!=6u) continue;
        vec4 b=sectorInfo[first+2u*i+1u];
        float d=segmentDistance(t,b.xy,b.zw);
        if(d>a.y) continue;
        float near=1.0-d/a.y;
        if(clump<near*a.w*1.2) frost=max(frost,a.w*(0.6+0.4*near));
    }
    return frost;
}
// A water splash's wet at a floor or wall texel: a ragged patch of whole
// texels round where it came down, a little darker in the middle; on a wall
// it reaches twice as far down as up, run down by the water. A wall's texels
// are taken along it on the world grid (as the snow rim does).
float splashWet(vec3 world,vec3 normal,vec4 a,vec4 b) {
    bool wall=abs(normal.z)<=0.5;
    vec2 side=vec2(-normal.y,normal.x);
    vec3 at=wall?vec3(side*(floor(dot(world.xy,side))+0.5)+normal.xy*dot(world.xy,normal.xy),floor(world.z)+0.5)
                :vec3(floor(world.xy)+0.5,world.z);
    vec3 d=at-a.yzw;
    if(wall&&d.z<0.0) d.z*=0.5;
    float r=length(d);
    vec2 cell=wall?vec2(dot(at.xy,side),at.z):at.xy;
    float edge=b.x*(0.7+0.6*wearNoise(cell,3.0,uint(b.y)*0x85ebca6bu+11u));
    return r<edge?b.w*(r<edge-1.5?1.0:0.6):0.0;
}
// Where settledColor reads its texel (applyWear): on a wall half a unit
// inside it, as walls along the axes lie on whole units.
vec3 settledAt=vec3(0.0);
// Whether this wall texel lies under a drift heaped at its foot (world.frag
// lights those as the ground in front).
bool wallDrift=false;
Wear surfaceWear(vec3 world,vec3 normal,float u,uint sector,uint flags) {
    Wear w=Wear(0.0,0.0,false,0.0,0.0,0.0,0.0,0.0,false,0.0,vec2(0.0));
    settledAt=abs(normal.z)<=0.5?vWorld-vec3(normal.xy*0.5,0.0):vWorld;
    // Snow, and sand and ash, which lie as snow does in their own colour.
    bool snow=c.weather.w==WEATHER_SNOW||c.weather.w==WEATHER_SAND||c.weather.w==WEATHER_ASH||c.weather.w==WEATHER_FIRESTORM;
    uint base=uint(c.right.w);
    if(base!=0u) w.frost=breachFrostAt(world,normal,u,sector,base);
    vec4 info=sectorInfo[sector];
    bool outdoors=info.w>0.0||sectorOpenAir(sector);   // one ground with the open air under a porch's roof
    w.indoors=!outdoors;
    w.open=sectorOpen(sector);
    bool ground=normal.z>0.5,wall=abs(normal.z)<=0.5;
    vec2 t=floor(world.xy)+0.5;
    if(normal.z<-0.5) {
        if(base==0u) return w;
        vec4 head=sectorInfo[base+sector];
        uint first=uint(head.x),count=wearOthers(head);
        for(uint i=0u;i<count;++i) {
            vec4 a=sectorInfo[first+2u*i];
            if(uint(a.x)!=4u) continue;
            vec2 k=crackAt(t,a.yz,a.w,uint(sectorInfo[first+2u*i+1u].x)*0x9e3779b9u+1u);
            w.crack=max(w.crack,k.x);w.wet=max(w.wet,k.y);
        }
        return w;
    }
    if(ground) {
        // Under a roof only where the bake blew it in past the edge.
        float baked=c.up.w>0.0?bakedWetness(t,outdoors):-1.0;
        if(c.up.w>0.0&&snow&&(outdoors||baked>=0.0))
            w.snow=c.up.w*snowAt(t,baked>=0.0?baked:0.25+0.65*smoothstep(0.4,0.7,wearNoise(t,44.0,0x3c6ef372u)));
        else if(c.up.w>0.0&&baked>=0.0) w.wet=c.up.w*baked;
        else if(c.up.w>0.0&&outdoors) w.wet=c.up.w*(0.3+0.7*smoothstep(0.55,0.72,wearNoise(t,44.0,0x3c6ef372u)));
        // In a gust what lies loose blows off the bake's edges facing the
        // wind: where a streak of it (blowing) runs over such an edge its
        // texels thin out, the streaks running downwind with the gusts.
        if(w.snow>0.0&&baked>=0.0&&length(c.weather.yz)>1.0) {
            float blow=blowing(t,w.open);
            if(blow>0.0&&bakedWetness(t-normalize(c.weather.yz)*8.0,outdoors)<baked-0.2) w.snow*=1.0-0.6*blow;
        }
        if(base==0u) return w;
        vec4 head=sectorInfo[base+sector];
        uint first=uint(head.x),count=wearOthers(head),seed=wearSeed(head);
        for(uint i=0u;i<count;++i) {
            vec4 a=sectorInfo[first+2u*i],b=sectorInfo[first+2u*i+1u];
            if(uint(a.x)==5u) {
                float r=length(t-a.yz),edge=a.w*(0.75+0.5*wearNoise(t,4.0,uint(b.x)*0x85ebca6bu+3u));
                if(r<edge+2.0) w.wet=max(w.wet,(r<edge?1.0:0.6)*b.w);
                continue;
            }
            if(uint(a.x)==7u) {
                w.iceCrack=max(w.iceCrack,crackAt(t,a.yz,a.w,uint(b.x)*0x9e3779b9u+7u).x);
                continue;
            }
            if(uint(a.x)==8u) {w.wet=max(w.wet,splashWet(world,normal,a,b));continue;}
            if(uint(a.x)!=3u) continue;
            vec2 edge=b.zw-b.xy;float len=length(edge);
            if(len<1.0) continue;
            vec2 dir=edge/len,d=t-b.xy;
            float along=dot(d,dir),across=abs(dot(d,vec2(dir.y,-dir.x)));
            float limit=a.y*(0.5+0.5*wearNoise(vec2(along,0.0),10.0,seed^0x1b873593u));
            // It fans out past the frame's ends the further in it blows, in
            // a ragged rounded patch rather than the opening's width: snow
            // drifting, rain running out over the floor as it collects.
            float side=max(max(-along,along-len),0.0);
            float fan=(3.0+0.6*across)*(0.6+0.8*wearNoise(t,6.0,seed^0x4cf5ad43u));
            if(side>fan||across>limit) continue;
            float inner=1.0-length(vec2(across/limit,side/fan));
            if(inner<=0.0) continue;
            if(snow) {
                float clump=wearHash(ivec2(floor(t*0.5)),seed^0x85ebca6bu);
                if(clump<inner*a.w*1.3) w.snow=max(w.snow,clump<inner*a.w*0.7?1.0:0.55);
            }
            else if(wearHash(ivec2(t),seed^0x85ebca6bu)*0.5<inner) w.wet=max(w.wet,a.w*(0.55+0.45*inner));
        }
        // The drafts' trails, after the rest.
        for(uint i=count,end=count+wearTrails(head);i<end;++i) draftTrail(t,sectorInfo[first+2u*i],sectorInfo[first+2u*i+1u],w);
        // Craters dug in all that lies (kind 10, after everything that lays
        // snow): bare inside a ragged edge, filling back in clumps of two
        // texels as it blows in again, and a heaped rim round it while fresh.
        if(w.snow>0.0) for(uint i=0u;i<count;++i) {
            vec4 a=sectorInfo[first+2u*i];
            if(uint(a.x)!=10u) continue;
            vec4 b=sectorInfo[first+2u*i+1u];
            uint craterSeed=uint(b.x)*0x9e3779b9u+10u;
            float r=length(t-a.yz),edge=a.w*(0.8+0.4*wearNoise(t,4.0,craterSeed));
            if(r<edge) {if(wearHash(ivec2(floor(t*0.5)),craterSeed)>=b.y) w.snow=0.0;}
            else if(r<edge*1.3&&wearHash(ivec2(floor(t*0.5)),craterSeed^0x68e31da4u)<b.z*(1.0-b.y)) w.snow=1.0;
        }
        return w;
    }
    if(!wall) return w;
    if(outdoors&&c.up.w>0.0&&snow) {
        // What lies on the ground at the wall's foot heaps against it, so a
        // building grows out of the ground instead of standing on it. Column
        // by column along the wall, only as much as the ground in front holds
        // (groundSnow, two texels out and four columns either way, so the
        // drift rises and tapers with the ground's patches instead of copying
        // its clumps): where it lies bare the wall's foot stays bare. On the
        // wall's own grid: its texture's columns (u), rows by whole units up.
        float height=floor(world.z-info.y);
        float cover=0.0;
        int column=int(floor(u));
        if(height>=0.0&&height<40.0) {
            float along=(float(column)+0.5-2.0)/4.0,block=floor(along);
            cover=mix(footCover(world.xy,u,normal.xy,block),footCover(world.xy,u,normal.xy,block+1.0),along-block);
        }
        float wind=length(c.weather.yz),facing=wind>1.0?saturate(dot(normal.xy,-c.weather.yz/wind)):0.0;
        if(cover>0.0) {
            // The drift: its top wandering over a dozen texels, higher where
            // more lies and on walls facing the wind.
            float rim=cover*(4.0+8.0*wearNoise(vec2(float(column),0.0),12.0,0x1b873593u)+10.0*facing);
            if(height<rim) {w.snow=c.up.w*(height<rim-1.0?1.0:0.55);wallDrift=true;}
        }
        // A little caught on the ledges all up the wall: the top row of what
        // the artwork paints facing up (a brick's top under the mortar, a sill;
        // the relief sloping along v), in runs of a few texels along it, more
        // on walls facing the wind. Thin, the brick showing through.
        if(w.snow<=0.0) {
            ivec2 p=ivec2(floor(vUV)),size=textureSize(relief,0);
            float rise=(texelFetch(relief,wrapTexel(p,size),0).g*255.0-128.0)/127.0;
            float above=(texelFetch(relief,wrapTexel(p-ivec2(0,1),size),0).g*255.0-128.0)/127.0;
            if(rise>0.3&&above<=0.3&&wearHash(ivec2(column/5,p.y),0x4d2c6dfcu)<0.2+0.35*facing) w.snow=c.up.w*0.55;
        }
    }
    else if(outdoors&&c.up.w>0.0) w.wet=c.up.w*(0.2+0.3*(1.0-saturate((world.z-info.y)/64.0)));
    if(base==0u) return w;
    vec4 head=sectorInfo[base+sector];
    for(uint i=0u;i<wearOthers(head);++i) {
        vec4 a=sectorInfo[uint(head.x)+2u*i];
        if(uint(a.x)==8u) w.wet=max(w.wet,splashWet(world,normal,a,sectorInfo[uint(head.x)+2u*i+1u]));
    }
    if((flags&FEATURE_WINDOW_STREAKS)==0u||vStatic==0u) return w;
    uint first=uint(head.x),count=wearOthers(head),key=((vStatic-1u)>>3)*2u+(((vStatic-1u)>>2)&1u);
    for(uint i=0u;i<count;++i) {
        vec4 a=sectorInfo[first+2u*i];
        if(uint(a.x)!=1u||uint(a.y)!=key||world.z>=a.z) continue;
        vec4 b=sectorInfo[first+2u*i+1u];
        vec2 edge=b.zw-b.xy;float len=length(edge);
        if(len<1.0) continue;
        float along=dot(world.xy-b.xy,edge/len);
        float streak=streakAt(along,len,a.z-world.z,a.z-info.y,uint(a.y)*0x9e3779b9u)*abs(a.w);
        // Outside they darken while it rains.
        if(outdoors&&!snow) streak*=1.0+0.4*c.up.w;
        if(streak>w.streak) {w.streak=streak;w.rust=a.w<0.0;}
    }
    return w;
}
// A worn plate's grime at a texel, in thirds: packed into the painted seams
// (the relief's steep texels) and a ragged texel round them, faint dull
// blotches (a third at most) over the plate, and on walls boot scuffs in the lowest 16 units, short
// dark marks along texel rows that thin out with height. Set per wall
// section (section, vStatic) so repeats and neighbors differ.
float plateGrime(vec2 uv,vec3 world,vec3 normal,uint sector,uint section) {
    ivec2 p=ivec2(floor(uv)),size=textureSize(relief,0);
    uint seed=section*0x9e3779b9u+0x27d4eb2fu;
    float seam=0.0;
    const ivec2 near[5]=ivec2[5](ivec2(0),ivec2(1,0),ivec2(-1,0),ivec2(0,1),ivec2(0,-1));
    for(int i=0;i<5;++i) {
        vec2 slope=(texelFetch(relief,wrapTexel(p+near[i],size),0).rg*255.0-128.0)/127.0;
        seam=max(seam,smoothstep(0.1,0.3,length(slope))*(i==0?1.0:0.7));
    }
    seam*=0.55+0.45*wearHash(p,seed);
    vec2 t=vec2(p)+0.5;
    float blotch=0.6*wearNoise(t,7.0,seed^0x1b873593u)+0.4*wearNoise(t,2.5,seed^0xcc9e2d51u);
    float grime=max(seam*0.8,smoothstep(0.55,0.8,blotch)*0.45);
    if(abs(normal.z)<=0.5) {
        float height=world.z-sectorInfo[sector].y;
        if(height<16.0) {
            int mark=(p.x+int(tileHash(ivec2(0,p.y),seed)&3u))>>2;
            float chance=0.3*(1.0-height/16.0);
            if(wearHash(ivec2(mark,p.y),seed^0x85ebca6bu)<chance&&wearHash(p,seed^0xc2b2ae35u)>0.2) grime=max(grime,0.7);
        }
    }
    return round(saturate(grime)*3.0)/3.0;
}
vec3 settledColor(vec3 world,bool indoors,float open,vec2 flow);   // world/settled.glsl
// The baked light's colour on what lies (snow, sand, ash: how much, 0-1):
// mostly its brightness only. White snow shows a light's colour plainly,
// and the bake's colour (bounce off nukage, a green wall) changes cell by
// coarse cell, so the snow came out in green and brown blotches like mould.
// Dynamic lights keep theirs.
vec3 settledLight(vec3 light,float lying) {
    return lying>0.0?mix(light,vec3(dot(light,vec3(0.299,0.587,0.114))),0.75*lying):light;
}
// The wear on a texel's color, before lighting; glowing texels stay clean.
vec3 applyWear(vec3 color,Wear w,float emission) {
    float keep=1.0-emission;
    if(w.streak>0.0) color*=mix(vec3(1),w.rust?vec3(0.78,0.5,0.32):vec3(0.5,0.52,0.46),min(w.streak,1.0)*keep);
    if(w.wet>0.0) {
        color*=1.0-0.36*min(w.wet,1.0)*keep;
        // Nukage rain (c.weatherTint) leaves its colour where it lies wet.
        if(c.weatherTint.w>0.0) color=mix(color,c.weatherTint.rgb*max(dot(color,vec3(0.3,0.59,0.11))*1.4,0.08),0.45*min(w.wet,1.0)*keep);
    }
    if(w.crack>0.0) color*=1.0-0.75*w.crack*keep;
    if(w.grime>0.0) color*=mix(vec3(1),vec3(0.6,0.55,0.48),w.grime*keep);
    if(w.iceCrack>0.0) color=mix(color,vec3(0.92,0.96,1.0),w.iceCrack*0.85*keep);   // fractured ice scatters light: white
    if(w.frost>0.0) color=mix(color,vec3(0.76,0.83,0.9)*max(dot(color,vec3(0.3,0.59,0.11))*1.5,0.42),min(w.frost,1.0)*0.75*keep);
    // Snow (or sand, or ash) over it all.
    if(w.snow>0.0) color=mix(color,settledColor(settledAt,w.indoors,w.open,w.flow),w.snow*keep);
    return color;
}
