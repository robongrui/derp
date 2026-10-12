/* Splashes, ripple rings and bubbles where things enter or move in
   liquids. */
#include "internal.h"

namespace doom3d {
// Splashes: per tic, things in liquid sectors are watched for landing,
// projectiles exploding at the surface, wading (rings only), and bodies
// coming to rest in it (a corpse's last death frame). Each event throws
// droplets in the colors of the liquid's flat and expanding rings; the
// reflection stays as it is.
struct SplashWatch { float z;bool missile,seen;int ripple;bool down;float x,y; };
struct Ring { float x,y,born,speed,life,strength;int sector; };
static std::unordered_map<const mobj_t*,SplashWatch> splashWatch;
static std::vector<Ring> rings;
static unsigned splashLevelSerial;
// Water throws many small, quick drops that vanish into it and a ring that
// races out; thick liquids throw a few fat drops that rise less, sit on the
// surface a while, and a ring that spreads slowly and not far. A shot
// throws a tall column instead: drops straight up, most of a storey high on
// water, a little lower and fatter on thick liquids. Drops catch the light,
// so they stay bright in dark rooms, and glowing liquids throw glowing drops.
// Rings alone, for things wading through.
static void ripple(float x,float y,int sector,float size,bool shot=false) {
    float now=leveltime/(float)TICRATE;
    bool thick=sectorMist[sector].thick;
    if(rings.size()>=24)rings.erase(rings.begin());
    rings.push_back({x,y,now,thick?10+size*0.2f:30+size*0.6f,thick?1.3f+size*0.015f:0.8f+size*0.01f,shot?1.0f:std::min(1.0f,size/40),sector});
}
static void splash(float x,float y,int sector,float size,int drops,bool shot=false) {
    float now=leveltime/(float)TICRATE;
    bool thick=sectorMist[sector].thick;
    ripple(x,y,sector,size,shot);
    dropOldLevelImpacts();
    Image &flat=lumpImage(firstflat+flattranslation[sectors[sector].floorpic],true);
    float light=flat.emissionCoverage>0.2f||sectorMist[sector].hot?1.0f:std::max(lighting(&sectors[sector]),0.8f);
    float floor=units(sectors[sector].floorheight),lift=std::clamp(size/40,0.6f,1.4f);
    if(debris.size()>480)debris.erase(debris.begin(),debris.begin()+(debris.size()-480));
    if(thick)drops=std::max(1,(drops+1)/2);
    if(shot) {
        for(int i=0;i<drops;++i) {
            float angle=randomRange(0,2*doomPi),out=randomRange(0,thick?20:30),up=thick?randomRange(170,260):randomRange(220,330);
            auto color=texelColor(flat);for(float &c:color)c=std::min(1.0f,c*1.3f);
            debris.push_back({x,y,floor+1,std::cos(angle)*out,std::sin(angle)*out,up,now,2*up/420+(thick?randomRange(0.4f,0.7f):0.2f),
                thick?randomRange(2.2f,3.2f):randomRange(1.0f,1.6f),color,light});
        }
        return;
    }
    for(int i=0;i<drops;++i) {
        float angle=randomRange(0,2*doomPi),out=thick?randomRange(10,40):randomRange(20,80);
        auto color=texelColor(flat);for(float &c:color)c=std::min(1.0f,c*1.3f);
        if(thick) debris.push_back({x,y,floor+1,std::cos(angle)*out,std::sin(angle)*out,randomRange(70,150)*lift,now,randomRange(0.8f,1.2f),randomRange(2.4f,3.6f),color,light});
        else debris.push_back({x,y,floor+1,std::cos(angle)*out,std::sin(angle)*out,randomRange(90,220)*lift,now,randomRange(0.35f,0.6f),randomRange(0.8f,1.2f),color,light});
    }
}
// Free-aimed hitscan shots stopping on a floor or ceiling (R_PlaneImpact):
// liquids splash, the pellets of one blast sharing a ring where they land
// close together; other flats take a bullet mark and chips in their color.
boolean planeImpact(sector_t *sector,boolean ceiling,fixed_t fx,fixed_t fy,fixed_t fz) {
    if(gamestate!=GS_LEVEL||levelSerial!=r_levelserial||sectorMist.size()!=(size_t)numsectors)return false;
    dropOldLevelImpacts();
    int index=int(sector-sectors);float x=units(fx),y=units(fy),z=units(fz),now=gameSeconds();
    if(!ceiling&&sectorMist[index].liquid) {
        if(!settings.splashes)return false;
        float tic=leveltime/(float)TICRATE; // As splash() stamps its rings.
        bool shared=std::any_of(rings.begin(),rings.end(),[&](const Ring &r){return r.born==tic&&std::hypot(r.x-x,r.y-y)<32;});
        size_t before=rings.size();
        splash(x,y,index,16,8,true);
        if(shared&&rings.size()>before)rings.pop_back();
        return true;
    }
    if(flatDecals.size()>=96)flatDecals.erase(flatDecals.begin());
    flatDecals.push_back({x,y,now,randomRange(5.0f,7.5f),index,(bool)ceiling,randomRange(0,1)<0.5f});
    int pic=ceiling?sector->ceilingpic:sector->floorpic;
    Image &image=lumpImage(firstflat+flattranslation[pic],true);
    float light=lighting(sector);
    if(debris.size()>512)debris.erase(debris.begin(),debris.begin()+(debris.size()-512));
    for(int i=0;i<5;++i) {
        float angle=randomRange(0,2*doomPi),out=randomRange(20,70);
        debris.push_back({x,y,ceiling?z-3:z+1,std::cos(angle)*out,std::sin(angle)*out,ceiling?randomRange(-40,0):randomRange(60,140),now,
            randomRange(0.45f,0.8f),randomRange(1.2f,2.0f),texelColor(image),light});
    }
    return false;
}
void watchSplashes() {
    if(!settings.splashes||levelSerial!=r_levelserial||sectorMist.size()!=(size_t)numsectors)return;
    if(splashLevelSerial!=r_levelserial) {splashWatch.clear();rings.clear();splashLevelSerial=r_levelserial;}
    for(auto &entry:splashWatch)entry.second.seen=false;
    for(int i=0;i<numsectors;++i) {
        if(!sectorMist[i].liquid||sectors[i].floorpic==skyflatnum)continue;
        float floor=units(sectors[i].floorheight);
        for(mobj_t *thing=sectors[i].thinglist;thing;thing=thing->snext) {
            bool missile=thing->flags&MF_MISSILE;
            if((thing->flags&MF_NOBLOCKMAP)&&!missile)continue;
            float x=units(thing->x),y=units(thing->y),z=units(thing->z);
            bool surface=z<=floor+1;
            bool down=(thing->flags&MF_CORPSE)&&thing->tics==-1;
            auto found=splashWatch.find(thing);
            if(found==splashWatch.end()) {splashWatch[thing]={z,missile,true,leveltime,down,x,y};continue;}
            SplashWatch &watch=found->second;watch.seen=true;
            float size=units(thing->radius)*2;
            if(down&&!watch.down&&surface) {
                splash(x,y,i,std::clamp(size*0.8f+16,24.0f,56.0f),6);
                watch.down=true;watch.ripple=leveltime;watch.z=z;watch.x=x;watch.y=y;continue;
            }
            watch.down=down;
            if(watch.missile&&!missile&&z<=floor+8)splash(x,y,i,48,12);
            else if(surface&&watch.z>floor+3) {
                splash(x,y,i,std::clamp(size*0.5f+(watch.z-z)*0.8f,12.0f,64.0f),std::clamp(int(4+(watch.z-z)*0.25f),4,16));
                watch.ripple=leveltime;
            } else if(surface&&!down&&std::hypot(x-watch.x,y-watch.y)>1&&leveltime-watch.ripple>=12) {
                // Monsters step without momentum, so movement is the change
                // in position since the last tic.
                ripple(x,y,i,size*0.4f);watch.ripple=leveltime;
            }
            watch.z=z;watch.missile=missile;watch.x=x;watch.y=y;
        }
    }
    for(auto it=splashWatch.begin();it!=splashWatch.end();) it=it->second.seen?std::next(it):splashWatch.erase(it);
}
// Rings: up to three full circles, the inner ones a few tics behind, growing
// at the tic rate and thickening as they grow. They start at the brightest
// texel of the liquid's flat, lit like the splash drops, step down toward
// the liquid's own color as they age, and are gone at the end.
void buildRings(Uniforms &camera) {
    if(!settings.splashes||splashLevelSerial!=r_levelserial) {rings.clear();return;}
    float now=gameSeconds();
    rings.erase(std::remove_if(rings.begin(),rings.end(),[&](const Ring &r){return now-r.born>r.life||now<r.born-0.1f;}),rings.end());
    for(const Ring &ring:rings) {
        float age=std::max(0.0f,std::floor((now-ring.born)*TICRATE)/TICRATE),fade=(1-age/ring.life)*std::max(ring.strength,0.5f);
        const sector_t &sector=sectors[ring.sector];
        float z=floorZ(&sector)+decalLift(ring.x,ring.y,camera)+0.1f,light=lighting(&sector);
        Image &flat=lumpImage(firstflat+flattranslation[sector.floorpic],true);
        if(flat.emissionCoverage>0.2f||sectorMist[ring.sector].hot)light=1;else light=std::max(light,0.8f);
        const auto &brightest=brightestColor(flat);const auto &base=sectorMist[ring.sector].color;
        std::array<float,3> albedo;
        for(int c=0;c<3;++c)albedo[c]=brightest[c];
        // The flashlight lights the whole ring by its center.
        float beamAmount=0;std::array<float,3> beamColor={};
        if(flashlightOn&&flashes.count&&flashes.lights[0].direction[3]>0&&camera.effects[2]==0) {
            const Flash &beam=flashes.lights[0];beamAmount=flashAt(ring.x,ring.y,z+1,beam);
            for(int c=0;c<3;++c)beamColor[c]=beam.color[c];
        }
        // Points along each circle close enough to touch at the nearest
        // depth it can have on screen, so it reads as one unbroken line.
        float cx=ring.x-camera.eye[0],cy=ring.y-camera.eye[1],cz=z-camera.eye[2];
        float centerDepth=cx*camera.forward[0]+cy*camera.forward[1]+cz*camera.forward[2];
        bool thick=sectorMist[ring.sector].thick;
        for(int j=0;j<3;++j) {
            float inner=age-j*(thick?0.2f:0.12f),step=fade*(1-0.25f*j);
            if(inner<0||step<0.12f)break;
            float r=4+ring.speed*inner,width=(thick?2.0f:0.8f)+r*0.06f;
            float spacing=std::max(std::max(4.0f,centerDepth-r)/(100*camera.projection[1])*0.9f,width*0.7f);
            int points=std::clamp((int)(2*doomPi*r/std::max(spacing,0.05f)),12,512);
            // Palette steps from the bright crest toward the liquid's color.
            float mixed=std::round(step*4)/4;
            std::array<float,3> albedoNow,tint;
            for(int c=0;c<3;++c) {
                albedoNow[c]=base[c]+(albedo[c]-base[c])*mixed;
                tint[c]=std::min(1.0f,albedoNow[c]*light+albedoNow[c]*beamAmount*beamColor[c]);
            }
            for(int k=0;k<points;++k) {
                float angle=k*2*doomPi/points;
                pixelBlock(particleVertices,camera,ring.x+std::cos(angle)*r,ring.y+std::sin(angle)*r,z,width,thick?0.6f:1.0f,tint,true);
            }
        }
    }
}

// A thick liquid's color for its bubbles and collars: the brightest of a few
// texels of its flat picked by hash, lit like splash drops.
std::array<float,3> gooColor(int sector,uint32_t seed) {
    Image &flat=lumpImage(firstflat+flattranslation[sectors[sector].floorpic],true);
    const byte *palette=(const byte*)W_CacheLumpName((char*)"PLAYPAL",PU_CACHE);
    int best=0,bestSum=-1;
    for(int k=0;k<4;++k) {
        int texel=std::min(4095,(int)(hashUnit(k,(int)seed,seed^0xb0bu)*4096));
        int index=flat.pixels[2*texel],sum=palette[index*3]+palette[index*3+1]+palette[index*3+2];
        if(sum>bestSum){bestSum=sum;best=index;}
    }
    float light=flat.emissionCoverage>0.2f||sectorMist[sector].hot?1.0f:std::max(lighting(&sectors[sector]),0.8f);
    return {std::min(1.0f,palette[best*3]/255.0f*light*1.15f),std::min(1.0f,palette[best*3+1]/255.0f*light*1.15f),std::min(1.0f,palette[best*3+2]/255.0f*light*1.15f)};
}
// Bubbles on thick liquids near the eye, stateless like the dust: about a
// third of the 32-unit cells over them bubble once every few seconds, at a
// new spot each time. A bubble domes up in three steps, hangs, then bursts
// into four specks and a small dotted ring. Tic-stepped.
void buildBubbles(const Uniforms &camera) {
    if(!settings.splashes||camera.effects[2]!=0||sectorMist.size()!=(size_t)numsectors)return;
    constexpr float cell=32,reach=640;
    float now=std::floor(gameSeconds()*TICRATE)/TICRATE;
    for(int i=0;i<numsectors;++i) {
        if(!sectorMist[i].thick||sectors[i].floorpic==skyflatnum)continue;
        float floor=floorZ(&sectors[i]);
        if(camera.eye[2]<floor+1)continue;
        for(int leaf:sectorFloors[i]) {
            const auto &b=floorBounds[leaf];const auto &poly=floors[leaf];
            if(poly.size()<3)continue;
            float dx=std::max({b[0]-camera.eye[0],0.0f,camera.eye[0]-b[2]}),dy=std::max({b[1]-camera.eye[1],0.0f,camera.eye[1]-b[3]});
            if(std::hypot(dx,dy)>reach)continue;
            for(int gy=(int)std::floor(b[1]/cell);gy<=(int)std::floor(b[3]/cell);++gy)
            for(int gx=(int)std::floor(b[0]/cell);gx<=(int)std::floor(b[2]/cell);++gx) {
                if(hashUnit(gx,gy,0xb0b1u)>0.35f)continue;
                float period=2.5f+4*hashUnit(gx,gy,0xb0b2u),clock=now+hashUnit(gx,gy,0xb0b3u)*period;
                int cycle=(int)std::floor(clock/period);float s=clock-cycle*period;
                if(s>=1.4f)continue;
                uint32_t seed=(uint32_t)cycle*2654435761u^(uint32_t)(gx*73856093)^(uint32_t)(gy*19349663);
                float x=(gx+0.15f+0.7f*hashUnit(gx,gy,seed))*cell,y=(gy+0.15f+0.7f*hashUnit(gx,gy,seed^1u))*cell;
                if(x<b[0]||x>b[2]||y<b[1]||y>b[3])continue;
                bool inside=true;
                for(size_t n=0;n<poly.size()&&inside;++n) {
                    Point a=poly[n],c=poly[(n+1)%poly.size()];
                    inside=(c.x-a.x)*(y-a.y)-(c.y-a.y)*(x-a.x)>=0;
                }
                if(!inside||std::hypot(x-camera.eye[0],y-camera.eye[1])>reach)continue;
                auto color=gooColor(i,seed);
                if(s<1.15f) {
                    float step=s<0.3f?1:s<0.6f?2:3;
                    pixelBlock(particleVertices,camera,x,y,floor+0.2f,1.6f*step,s<0.9f?0.6f:0.8f,color,true,&color);
                } else {
                    float q=(s-1.15f)/0.25f,r=2+8*q,z=floor+0.2f+5*std::sin(doomPi*q);
                    for(int k=0;k<4;++k)
                        pixelBlock(particleVertices,camera,x+(k&1?r:-r)*0.7f,y+(k&2?r:-r)*0.7f,z,1.2f,1,color,false,&color);
                    for(int k=0;k<8;++k) {
                        if(hashUnit(k,(int)seed,seed^7u)<q)continue;
                        float angle=(k+0.5f)*doomPi/4;
                        pixelBlock(particleVertices,camera,x+std::cos(angle)*r,y+std::sin(angle)*r,floor+0.2f,1.2f,1,color,true,&color);
                    }
                }
            }
        }
    }
}
void splashesShutdown() {splashWatch.clear();rings.clear();}
} // namespace doom3d
