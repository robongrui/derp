/* Void patterns (gore): a big burst throws a small fan of fine spatter onto the nearest wall, and a monster standing in
   the way keeps the wall behind him clean in his shape, the way a forensic photo shows where someone stood. His
   outline is his sprite seen from the burst, put on the wall as a lamp throws a shadow (the flashlight's silhouette,
   shadows.cpp), but once, texel by texel on the wall's own grid, into its blood layer (blood_layer.cpp): it stays
   after he walks off or falls. He takes that blood instead, a stain on the side that faced it (body_stains.cpp), so
   a bloody monster and the clean shape behind him come as a pair. Monsters only, not the player (the user).
   Drawing only: nothing in the playsim, nothing per frame. */
#include "internal.h"

namespace doom3d {
// One small fan on the nearest wall: the burst's mist where it meets a wall close by, not a coat over the room. The
// drops (spray.cpp) stay the real spill; a fan on every wall round it read as blood going everywhere (the user).
constexpr float voidReach=128;     // walls further from the burst get no fan
constexpr int voidWalls=1;

namespace {
// A monster in the way: his drawing as the burst sees it, standing on its card through his middle.
struct Blocker {
    const mobj_t *thing;float x,y,feet,radius,height;
    float ux,uy;            // from the burst to him, over the floor
    float rx,ry;            // his drawing's right as the burst sees it
    const Image *image;bool flip;int caught;
};
struct WallFan { WallHit hit;WallSpan span;float tx,ty,nx,ny,len,along,distance; };

// Which of his drawing's rotations the burst sees (as geometry.cpp picks one for the eye).
unsigned rotationFrom(float dx,float dy,angle_t facing) {
    angle_t angle=(angle_t)(int64_t)(std::atan2(dy,dx)*(4294967296.0/(2*doomPi)));
    return (angle-facing+(unsigned)(ANG45/2)*9)>>29;
}
bool blockerFor(const mobj_t &m,float bx,float by,Blocker &b) {
    const state_t *s=m.state;
    if(!s||s->sprite<0||s->sprite>=numsprites)return false;
    const auto &sprite=sprites[s->sprite];int frame=s->frame&FF_FRAMEMASK;
    if(frame>=sprite.numframes)return false;
    const auto &sf=sprite.spriteframes[frame];
    float x=units(m.x),y=units(m.y),dx=x-bx,dy=y-by,l=std::hypot(dx,dy);
    if(l<4||l>voidReach)return false;
    unsigned rotation=sf.rotate?rotationFrom(dx,dy,m.angle):0;
    b={&m,x,y,units(m.z),units(m.radius),units(m.height),dx/l,dy/l,dy/l,-dx/l,
       &lumpImage(firstspritelump+sf.lump[rotation],false),sf.flip[rotation]!=0,0};
    return true;
}
// Does the line from the burst to (px,py,pz) pass through his drawing? Where it crosses his card, his texel there.
bool blocks(Blocker &b,float bx,float by,float bz,float px,float py,float pz) {
    float wx=px-bx,wy=py-by,toward=wx*b.ux+wy*b.uy,ahead=(b.x-bx)*b.ux+(b.y-by)*b.uy;
    if(ahead<std::max(4.0f,b.radius)||toward<=ahead+1)return false;   // the bleeder himself, or the texel not beyond him
    float s=ahead/toward,qx=bx+wx*s,qy=by+wy*s,qz=bz+(pz-bz)*s;
    const Image &image=*b.image;
    int across=(int)std::floor((qx-b.x)*b.rx+(qy-b.y)*b.ry),row=(int)std::floor(b.feet+image.top-qz);
    int column=b.flip?image.width-1-image.left-across:across+image.left;
    if(column<0||row<0||column>=image.width||row>=image.height)return false;
    return image.pixels[((size_t)row*image.width+column)*2+1]!=0;
}
float hashAt(int x,int y,uint32_t seed) {return hashUnit(x,y,seed);}
}

void voidBurst(const mobj_t *source,float x,float y,float z,float dx,float dy,float spread,float strength,int tint) {
    if(!goreOn()||!bloodOn()||tint==BLOOD_NONE)return;
    uint32_t seed=hash32((uint32_t)leveltime*2654435761u^(uint32_t)std::lround(x*7+y*13+z*3));
    // The walls round it: rays over the floor at the burst's height across the spread, one fan a wall.
    std::vector<WallFan> walls;
    float heading=std::atan2(dy,dx);
    int rays=spread>=doomPi-0.01f?16:9;
    for(int i=0;i<rays;++i) {
        float a=heading+(rays==16?(i+0.5f)/16*2*doomPi:spread*((i+0.5f)/rays*2-1));
        float ux=std::cos(a),uy=std::sin(a);WallHit hit;WallFan fan;
        if(!firstSolidWall(x,y,z,ux,uy,0,voidReach,hit)||!wallSpan(lines[hit.line],hit.side,hit.section,fan.span))continue;
        if(std::any_of(walls.begin(),walls.end(),[&](const WallFan &w){return w.hit.line==hit.line&&w.hit.side==hit.side&&w.hit.section==hit.section;}))continue;
        fan.hit=hit;
        float sx=fan.span.b.x-fan.span.a.x,sy=fan.span.b.y-fan.span.a.y;fan.len=std::hypot(sx,sy);
        if(fan.len<4)continue;
        fan.tx=sx/fan.len;fan.ty=sy/fan.len;fan.nx=fan.ty;fan.ny=-fan.tx;
        float rx=x-fan.span.a.x,ry=y-fan.span.a.y;
        fan.distance=std::abs(rx*fan.nx+ry*fan.ny);
        // The fan's middle: straight across from the burst, or where the ray met it if that's off the wall.
        fan.along=rx*fan.tx+ry*fan.ty;
        if(fan.along<0||fan.along>fan.len)fan.along=(x+ux*hit.t-fan.span.a.x)*fan.tx+(y+uy*hit.t-fan.span.a.y)*fan.ty;
        if(z<fan.span.bottom-8||z>fan.span.top+8)continue;
        walls.push_back(fan);
    }
    if(walls.empty())return;
    std::sort(walls.begin(),walls.end(),[](const WallFan &a,const WallFan &b){return a.distance<b.distance;});
    if(walls.size()>voidWalls)walls.resize(voidWalls);
    // Monsters near enough to stand in the way, as the burst sees them.
    std::vector<Blocker> blockers;
    for(int i=0;i<numsectors;++i)for(const mobj_t *m=sectors[i].thinglist;m;m=m->snext) {
        if(m==source||!(m->flags&MF_COUNTKILL)||m->player)continue;
        Blocker b;if(blockerFor(*m,x,y,b))blockers.push_back(b);
    }
    float now=gameSeconds();
    for(const WallFan &w:walls) {
        float fade=std::clamp(1.0f-w.distance/voidReach,0.0f,1.0f);
        if(fade<=0)continue;
        // Wider the further it flies, a fine spray thinning to specks at its rim.
        float reach=std::clamp(0.5f*w.distance+10.0f,14.0f,40.0f);
        int u0=(int)std::floor(std::max(0.0f,w.along-reach*1.2f)),u1=(int)std::ceil(std::min(w.len,w.along+reach*1.2f));
        int h0=(int)std::floor(std::max(w.span.bottom,z-reach*1.2f)-w.span.anchor),h1=(int)std::ceil(std::min(w.span.top,z+reach*1.2f)-w.span.anchor);
        uint32_t own=seed^(uint32_t)w.hit.line*40503u;
        float tilt=hashAt(1,2,own)*2*doomPi;   // where its rays lie, its own every burst (never symmetric)
        for(int gh=h0;gh<h1;++gh)for(int gu=u0;gu<u1;++gu) {
            float pz=w.span.anchor+gh+0.5f;
            float du=gu+0.5f-w.along,dz=pz-z,r=std::hypot(du,dz)/reach;
            if(r>1.25f)continue;
            float angle=std::atan2(dz,du)+tilt;
            float streak=0.5f+0.5f*std::sin(angle*7+3*std::sin(angle*3+tilt));       // the spray's rays
            float clump=hashAt(gu>>2,gh>>2,own^0x2bu);                                  // and its clumps
            float density=std::min(1.0f,std::max(0.0f,1-r)*1.6f)*(0.6f+0.4f*streak)*(0.75f+0.45f*clump)*(dz<0?0.9f:1.0f);
            density*=0.75f*fade*strength;
            float roll=hashAt(gu,gh,own);
            bool speck=r<1.2f&&hashAt(gu,gh,own^0x77u)<0.01f*(1.3f-r)*strength;
            if(roll>=density*1.25f&&!speck)continue;
            float px=w.span.a.x+w.tx*(gu+0.5f)+w.nx*0.5f,py=w.span.a.y+w.ty*(gu+0.5f)+w.ny*0.5f;
            bool clean=false;
            for(Blocker &b:blockers)if(blocks(b,x,y,z,px,py,pz)) {++b.caught;clean=true;break;}
            if(clean)continue;
            bloodLayerWallTexel(w.hit.line,w.hit.side,w.hit.section,tint,gu,gh,speck?40:(int)(30+50*std::min(1.0f,density)),now);
        }
    }
    if(M_CheckParm((char*)"-ragdolllog")) {
        fprintf(stderr,"voidburst at %.0f,%.0f,%.0f walls %zu blockers %zu caught",x,y,z,walls.size(),blockers.size());
        for(const Blocker &b:blockers)fprintf(stderr," %d",b.caught);
        fputc('\n',stderr);
    }
    // Who kept the wall clean took the blood: a stain on the side that faced the burst, as large as his share.
    for(const Blocker &b:blockers) {
        if(b.caught<12)continue;
        float sz=std::clamp(z,b.feet+4,b.feet+std::max(8.0f,b.height-4));
        bodyStain(b.thing,b.x-b.ux*b.radius,b.y-b.uy*b.radius,sz,std::clamp(8.0f+b.caught/30.0f,10.0f,26.0f),tint,now);
    }
}
} // namespace doom3d
