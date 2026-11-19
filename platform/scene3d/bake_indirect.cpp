/* Indirect light baked at level load: sky light and occlusion, and light
   flowing between sectors. The bounce is bake_bounce.cpp. */
#include "internal.h"
#include "open_doors.h"
#include "gpu_trace.h"

namespace doom3d {
int bakeThingsKey=-1,ambientKey=-1,flowKey=-1;
float skyLevel=0; // Typical sky-sector light; indoor sky light rises toward it.
std::array<float,3> skyTint={1,1,1};
// Decorations in the bakes: static, non-glowing things that stand on the
// floor or hang from the ceiling (columns, trees, hanging bodies) occlude
// the sun, baked lights, bounce, sky and flow rays with their spawn-state
// sprite. Glowing ones are light sources and stay out, so a lamp never
// shadows its own light. Changing the setting re-bakes the level.
void syncBakeThings() {
    int key=settings.thingShadows;
    if(key==bakeThingsKey)return;
    bool rebake=bakeThingsKey>=0;
    bakeThingsKey=key;
    bakeMap.things.clear();
    for(auto &sector:bakeMap.sectors)sector.things.clear();
    if(key)for(int i=0;i<numsectors;++i)for(mobj_t *thing=sectors[i].thinglist;thing;thing=thing->snext) {
        if(thing->player||(thing->flags&(MF_COUNTKILL|MF_SPECIAL|MF_MISSILE|MF_NOBLOCKMAP|MF_SHOOTABLE|MF_CORPSE)))continue;
        if(!(thing->flags&(MF_SOLID|MF_SPAWNCEILING))||(thing->frame&FF_FULLBRIGHT))continue;
        if(thing->sprite<0||thing->sprite>=numsprites||(thing->frame&FF_FRAMEMASK)>=sprites[thing->sprite].numframes)continue;
        const Image &image=lumpImage(firstspritelump+sprites[thing->sprite].spriteframes[thing->frame&FF_FRAMEMASK].lump[0],false);
        float x=units(thing->x),y=units(thing->y),half=image.width*0.5f;
        int index=(int)bakeMap.things.size();
        bakeMap.things.push_back({x,y,units(thing->z)+image.top,-(float)image.left,image.width,image.height,image.pixels.data()});
        // Also list it in neighbors its sprite reaches across a line.
        std::vector<int> reach={i};
        for(int j=0;j<sectors[i].linecount;++j) {
            const line_t &line=*sectors[i].lines[j];
            const sector_t *other=line.frontsector==&sectors[i]?line.backsector:line.frontsector;
            if(!other)continue;
            float ax=units(line.v1->x),ay=units(line.v1->y),ex=units(line.v2->x)-ax,ey=units(line.v2->y)-ay,length2=ex*ex+ey*ey;
            if(length2<0.01f)continue;
            float t=std::clamp(((x-ax)*ex+(y-ay)*ey)/length2,0.0f,1.0f);
            int o=int(other-sectors);
            if(std::hypot(x-ax-ex*t,y-ay-ey*t)<half&&std::find(reach.begin(),reach.end(),o)==reach.end())reach.push_back(o);
        }
        for(int s:reach)bakeMap.sectors[s].things.push_back(index);
    }
    if(rebake) {sunBaked=false;lightBakeKey=-1;bounceKey=-1;ambientKey=-1;flowKey=-1;}
    if(key)fprintf(stderr,"3D bake: %zu decorations occlude baked light.\n",bakeMap.things.size());
}
// Spreads samples taken every step cells over the cells between them,
// bilinearly within one sector or wall strip; values are per sample channels.
// The samples are listed first and evaluated in one batch (evaluate, on the
// processor or the graphics card: gpu_bakes.cpp), then spread. Floors and
// ceilings: flatRequest(sector,x,y,ceiling,sample,value) at block centers,
// false when the value needs no rays (it set it), write(cell,ceiling,value)
// per cell. Walls: a sample every step texels of each strip, wallWrite(atlas
// texel,value).
template<int Channels,class FlatRequest,class FlatWrite,class WallWrite,class Evaluate>
static void bakeSpread(int step,FlatRequest flatRequest,FlatWrite flatWrite,WallWrite wallWrite,Evaluate evaluate) {
    using Value=std::array<float,Channels>;
    int blocksX=(mapWidth+step-1)/step,blocksY=(mapHeight+step-1)/step;
    std::vector<Value> samples((size_t)blocksX*blocksY*2);
    std::vector<int> sampleSector((size_t)blocksX*blocksY,noSector);
    // What each sample asks, by row of blocks, row of cells (a cell with no sample of its own
    // sector round it takes one of its own) and strip; where it lands in the batch.
    std::vector<std::vector<BakeSample>> blockAsks(blocksY),cellAsks(mapHeight),stripAsks(bakeOrder.size());
    std::vector<std::vector<size_t>> blockAt(blocksY);   // per row: b*2+ceiling of each ask
    std::vector<std::vector<std::pair<size_t,Value>>> cellFixed(mapHeight);   // cells' values that need no rays (cell*2+ceiling)
    parallelFor(blocksY,[&](int by) {
        for(int bx=0;bx<blocksX;++bx) {
            int x=std::min(bx*step+step/2,mapWidth-1),y=std::min(by*step+step/2,mapHeight-1);
            size_t b=(size_t)by*blocksX+bx;int sector=seamCells[(size_t)y*mapWidth+x].own;
            sampleSector[b]=sector;if(sector==noSector)continue;
            for(int ceiling=0;ceiling<2;++ceiling) {
                BakeSample ask;
                if(flatRequest(sector,x,y,ceiling!=0,ask,samples[b*2+ceiling])) {blockAsks[by].push_back(ask);blockAt[by].push_back(b*2+ceiling);}
            }
        }
    });
    // The weights a cell blends its blocks with; their total is 0 when it has none (it is sampled itself).
    auto cellBlend=[&](int x,int y,int sector,float w[4],size_t at[4]) {
        float gx=(x-step/2)/(float)step,gy=(y-step/2)/(float)step;
        int x0=(int)std::floor(gx),y0=(int)std::floor(gy);float fx=gx-x0,fy=gy-y0,total=0;
        for(int k=0;k<4;++k) {
            int bx=x0+(k&1),by=y0+(k>>1);w[k]=0;at[k]=0;
            if(bx<0||by<0||bx>=blocksX||by>=blocksY)continue;
            size_t b=(size_t)by*blocksX+bx;if(sampleSector[b]!=sector)continue;
            w[k]=((k&1)?fx:1-fx)*((k>>1)?fy:1-fy)+1e-4f;at[k]=b;total+=w[k];
        }
        return total;
    };
    parallelFor(mapHeight,[&](int y) {
        for(int x=0;x<mapWidth;++x) {
            size_t i=(size_t)y*mapWidth+x;int sector=seamCells[i].own;
            if(sector==noSector)continue;
            float w[4];size_t at[4];
            if(cellBlend(x,y,sector,w,at)>0)continue;
            for(int ceiling=0;ceiling<2;++ceiling) {
                BakeSample ask;Value value;
                if(flatRequest(sector,x,y,ceiling!=0,ask,value))cellAsks[y].push_back(ask);
                else cellFixed[y].push_back({i*2+ceiling,value});
            }
        }
    });
    parallelFor((int)bakeOrder.size(),[&](int n) {
        const BakeStrip &strip=bakeStrips[bakeOrder[n]];
        int columns=(strip.width+step-1)/step,rows=(strip.height+step-1)/step;
        for(int cy=0;cy<rows;++cy)for(int cx=0;cx<columns;++cx) {
            int column=std::min(cx*step+step/2,strip.width-1),row=std::min(cy*step+step/2,strip.height-1);
            float along=std::clamp((column+0.5f)/strip.scale,0.5f,strip.length-0.5f);
            float z=std::min(strip.low+(row+0.5f)/strip.scale,strip.high);
            stripAsks[n].push_back({strip.ax+strip.dx*along+strip.dy*0.5f,strip.ay+strip.dy*along-strip.dx*0.5f,z,strip.sector,
                                    {strip.dy,-strip.dx,0},(uint32_t)bakeOrder[n]*7919u+cy*131u+cx,true});
        }
    });
    // One batch, in that order.
    std::vector<BakeSample> asks;
    std::vector<size_t> blockStart(blocksY+1),cellStart(mapHeight+1),stripStart(bakeOrder.size()+1);
    for(int by=0;by<blocksY;++by) {blockStart[by]=asks.size();asks.insert(asks.end(),blockAsks[by].begin(),blockAsks[by].end());}
    blockStart[blocksY]=asks.size();
    for(int y=0;y<mapHeight;++y) {cellStart[y]=asks.size();asks.insert(asks.end(),cellAsks[y].begin(),cellAsks[y].end());}
    cellStart[mapHeight]=asks.size();
    for(size_t n=0;n<bakeOrder.size();++n) {stripStart[n]=asks.size();asks.insert(asks.end(),stripAsks[n].begin(),stripAsks[n].end());}
    stripStart[bakeOrder.size()]=asks.size();
    std::vector<Value> values(asks.size());
    evaluate(asks,values);
    for(int by=0;by<blocksY;++by)for(size_t k=0;k<blockAt[by].size();++k)samples[blockAt[by][k]]=values[blockStart[by]+k];
    parallelFor(mapHeight,[&](int y) {
        size_t asked=cellStart[y],fixed=0;
        for(int x=0;x<mapWidth;++x) {
            size_t i=(size_t)y*mapWidth+x;int sector=seamCells[i].own;
            if(sector==noSector)continue;
            float w[4];size_t at[4];
            float total=cellBlend(x,y,sector,w,at);
            for(int ceiling=0;ceiling<2;++ceiling) {
                Value value;
                if(total>0) {
                    Value sum={};
                    for(int k=0;k<4;++k)if(w[k]>0)for(int c=0;c<Channels;++c)sum[c]+=samples[at[k]*2+ceiling][c]*w[k];
                    for(int c=0;c<Channels;++c)value[c]=sum[c]/total;
                } else if(fixed<cellFixed[y].size()&&cellFixed[y][fixed].first==i*2+ceiling)value=cellFixed[y][fixed++].second;
                else value=values[asked++];
                flatWrite(i,ceiling!=0,value);
            }
        }
    });
    parallelFor((int)bakeOrder.size(),[&](int n) {
        const BakeStrip &strip=bakeStrips[bakeOrder[n]];
        int columns=(strip.width+step-1)/step,rows=(strip.height+step-1)/step;
        std::vector<Value> coarse(values.begin()+stripStart[n],values.begin()+stripStart[n+1]);
        if(coarse.size()!=(size_t)columns*rows)return;
        smoothCoarse(coarse,columns,rows);
        for(int row=0;row<strip.height;++row)for(int column=0;column<strip.width;++column) {
            float gx=std::clamp((column-step/2)/(float)step,0.0f,columns-1.0f),gy=std::clamp((row-step/2)/(float)step,0.0f,rows-1.0f);
            int x0=std::min((int)gx,columns-1),y0=std::min((int)gy,rows-1),x1=std::min(x0+1,columns-1),y1=std::min(y0+1,rows-1);
            float fx=gx-x0,fy=gy-y0;Value value;
            for(int c=0;c<Channels;++c)
                value[c]=(coarse[(size_t)y0*columns+x0][c]*(1-fx)+coarse[(size_t)y0*columns+x1][c]*fx)*(1-fy)+
                         (coarse[(size_t)y1*columns+x0][c]*(1-fx)+coarse[(size_t)y1*columns+x1][c]*fx)*fy;
            wallWrite((size_t)(strip.y+row)*atlasStride+strip.x+column,value);
        }
    });
}
// Sky light and ambient occlusion, baked together from 64 cosine-weighted
// rays per sample (8 rings of 8), every ~8 units: the share that escapes through a sky
// ceiling (walls see at most half the sky, so theirs counts double) and how
// open the surroundings are within 64 units. Each is stored in 16 steps,
// like Doom's light levels, packed into the alpha of the bounce layers:
// sky in the high nibble, openness in the low one.
void bakeAmbient() {
    loadingStage(LoadStage::Ambient);
    finishRelight();
    ambientKey=bakeThingsKey;
    if(flatLightCells.empty()||wallBakeCells.empty())return;
    uint64_t start=SDL_GetTicksNS();
    std::vector<float> skyLights;
    for(int i=0;i<numsectors;++i)if(sectors[i].ceilingpic==skyflatnum)skyLights.push_back(lighting(&sectors[i]));
    skyLevel=0;
    if(!skyLights.empty()) {
        std::nth_element(skyLights.begin(),skyLights.begin()+skyLights.size()/2,skyLights.end());
        skyLevel=daylightLevel(skyLights[skyLights.size()/2]);
        skyTint=skyColor();
    }
    // Occlusion counts only hits within 64 units; rays run on only to find
    // the sky, so on maps without one they stop there, open either way.
    float reach=skyLights.empty()?64:INFINITY;
    const OpenDoors doors;
    // Every sample casts the same rays (no per-sample rotation): outdoors the
    // sky share sets a wall's brightness in Doom's 1/16 steps (lightRule), and
    // samples that each guessed it with rays of their own crossed those steps
    // at random, blotching facades even out in the open, where a wall sees
    // almost exactly the top step (MAP13). Shared rays change only where the
    // buildings round them do. 64 rays, 8 rings of 8 (16, 4 of 4, with Settings::fastBakes).
    const int side=settings.fastBakes?4:8;
    auto gather=[&](int sector,float x,float y,float z,const float normal[3],uint32_t,std::array<float,2> &out) {
        float sky=0,open=0;
        const BakeMap &map=doors.of(sector);
        const int rays=side*side;
        for(int n=0;n<rays;++n) {
            float direction[3];BakeHit hit;
            cosineDirection(normal,((n%side)+0.5f)/side,((n/side)+0.5f)/side,direction);
            if(traceRay(map,sector,x,y,z,direction,reach,SkyCeiling::escape,&hit)) {sky+=reach==INFINITY;open+=1;continue;}
            if(hit.kind==BakeHit::none) {open+=1;continue;}
            open+=std::min(1.0f,std::sqrt((hit.x-x)*(hit.x-x)+(hit.y-y)*(hit.y-y)+(hit.z-z)*(hit.z-z))/64);
        }
        out={sky/rays,open/rays};
    };
    auto pack=[](float sky,float open) {
        return (uint8_t)((std::lround(std::clamp(sky,0.0f,1.0f)*15)<<4)|std::lround(std::clamp(open,0.0f,1.0f)*15));
    };
    // From an earlier visit if the bake cache has it (bake_cache.cpp).
    bool fresh=!bakeCacheRestore(LoadStage::Ambient,BAKE_BUILT);
    // On the graphics card when it can (gpu_bakes.cpp), else on every core here.
    bool card=gpuBakesOn();
    auto evaluate=[&](const std::vector<BakeSample> &asks,std::vector<std::array<float,2>> &values) {
        if(card&&gpuAmbient(asks,side,reach,doors,values))return;
        card=false;
        parallelFor((int)((asks.size()+255)/256),[&](int c) {
            for(size_t k=(size_t)c*256;k<std::min(asks.size(),(size_t)c*256+256);++k) {
                const BakeSample &a=asks[k];
                gather(a.sector,a.x,a.y,a.z,a.normal,a.seed,values[k]);
                if(a.wall)values[k][0]*=2;
            }
        });
    };
    if(fresh)bakeSpread<2>(std::max(1,(int)std::lround(8/mapCell)),
        [&](int sector,int x,int y,bool ceiling,BakeSample &ask,std::array<float,2> &out) {
            out={0,1};
            int pic=ceiling?sectors[sector].ceilingpic:sectors[sector].floorpic;
            if(pic==skyflatnum)return false;
            const BakeSector &s=doors.of(sector).sectors[sector];
            ask={mapOrigin[0]+(x+0.5f)*mapCell,mapOrigin[1]+(y+0.5f)*mapCell,ceiling?s.ceiling-1:s.floor+1,sector,
                 {0,0,ceiling?-1.0f:1.0f},(uint32_t)(y*mapWidth+x)*2+ceiling,false};
            return true;
        },
        [&](size_t cell,bool ceiling,const std::array<float,2> &value) {flatCell(ceiling?ceilingBounce:floorBounce,cell)[3]=pack(value[0],value[1]);},
        [&](size_t texel,const std::array<float,2> &value) {wallBakeCells[4*(texel+atlasWidth)+3]=pack(value[0],value[1]);},
        evaluate);
    if(fresh)bakeCacheKeep(LoadStage::Ambient);
    uploadFlatLight();
    uploadWallBake();
    fprintf(stderr,"3D sky light and occlusion: sky level %.2f, baked in %.0f ms%s.\n",skyLevel,(SDL_GetTicksNS()-start)/1e6,!fresh?" (cached)":card?" on the graphics card":"");
}
// Sector light flow: from points every ~16 units, at chest height, 12 level
// rays check which other sectors are in plain view at 48, 96 and 160 units.
// The most visible one is stored per cell as its flow neighbor (seam map,
// fourth channel) with a weight (contact map alpha); during play the cell's
// light rises toward that sector's current light, so flickering rooms
// flicker faintly beyond their openings. Doors count as they start (closed),
// but for a shut door's own floor, baked as it stands open (OpenDoors).
void bakeFlow() {
    loadingStage(LoadStage::Flow);
    finishRelight();
    flowKey=flowInputs();
    bool windows=flowKey&2,doorways=flowKey&4;
    if(seamCells.empty())return;
    if(doorways)Rooms_Build();
    uint64_t start=SDL_GetTicksNS();
    for(auto &cell:seamCells)cell.flow=noSector;
    struct Flow { int neighbor=noSector;float weight=0; };
    int step=std::max(1,(int)std::lround(16/mapCell));
    int blocksX=(mapWidth+step-1)/step,blocksY=(mapHeight+step-1)/step;
    std::vector<Flow> blocks((size_t)blocksX*blocksY);
    std::vector<int> blockSector((size_t)blocksX*blocksY,noSector);
    const OpenDoors doors;
    // Both loops, from an earlier visit if the bake cache has them (bake_cache.cpp).
    bool fresh=!bakeCacheRestore(LoadStage::Flow,BAKE_BUILT);
    if(fresh)parallelFor(blocksY,[&](int by) {
        std::vector<std::pair<int,float>> score;
        for(int bx=0;bx<blocksX;++bx) {
            int x=std::min(bx*step+step/2,mapWidth-1),y=std::min(by*step+step/2,mapHeight-1);
            size_t b=(size_t)by*blocksX+bx;int sector=seamCells[(size_t)y*mapWidth+x].own;
            blockSector[b]=sector;if(sector==noSector)continue;
            const BakeMap &map=doors.of(sector);
            const BakeSector &s=map.sectors[sector];
            if(s.ceiling-s.floor<8)continue;
            float px=mapOrigin[0]+(x+0.5f)*mapCell,py=mapOrigin[1]+(y+0.5f)*mapCell,pz=s.floor+std::min(40.0f,(s.ceiling-s.floor)*0.5f);
            float rotation=hashUnit(bx,by,0x51f1u);
            score.clear();
            for(int k=0;k<12;++k) {
                float angle=(k+rotation)*2*doomPi/12,direction[3]={std::cos(angle),std::sin(angle),0};
                static const std::pair<float,float> lengths[]={{48,1.0f},{96,0.6f},{160,0.35f}};
                for(auto [length,weight]:lengths) {
                    if(!traceRay(map,sector,px,py,pz,direction,length,SkyCeiling::open))break;
                    float ex=px+direction[0]*length,ey=py+direction[1]*length;
                    int end=int(R_PointInSubsector((fixed_t)(ex*FRACUNIT),(fixed_t)(ey*FRACUNIT))->sector-sectors);
                    if(end==sector)continue;
                    if(windows&&sectors[end].ceilingpic==skyflatnum&&sectors[sector].ceilingpic!=skyflatnum)continue;
                    // With light through doorways (bake_doorways.cpp) another room lights this
                    // one from the bake, with direction: no lift from its level on top. Within
                    // a room the flow stays.
                    if(doorways&&sectors[end].ceilingpic!=skyflatnum&&sectors[sector].ceilingpic!=skyflatnum&&
                       !(Rooms_Of(end)>=0&&Rooms_Of(end)==Rooms_Of(sector)))continue;
                    auto found=std::find_if(score.begin(),score.end(),[&](const auto &e){return e.first==end;});
                    if(found==score.end())score.push_back({end,weight});else found->second+=weight;
                }
            }
            for(const auto &entry:score)if(entry.second>blocks[b].weight)blocks[b]={entry.first,entry.second};
            blocks[b].weight=std::min(1.0f,blocks[b].weight*2/(12*1.95f));
        }
    });
    // Cells blend the weights of nearby same-sector blocks that share the
    // strongest one's neighbor.
    if(fresh)parallelFor(mapHeight,[&](int y) {
        for(int x=0;x<mapWidth;++x) {
            size_t i=(size_t)y*mapWidth+x;int sector=seamCells[i].own;
            contactCells[4*i+3]=0;
            if(sector==noSector)continue;
            float gx=(x-step/2)/(float)step,gy=(y-step/2)/(float)step;
            int x0=(int)std::floor(gx),y0=(int)std::floor(gy);float fx=gx-x0,fy=gy-y0;
            int neighbor=noSector;float strongest=0,sum=0,total=0;
            for(int pass=0;pass<2;++pass)for(int k=0;k<4;++k) {
                int bx=x0+(k&1),by=y0+(k>>1);
                if(bx<0||by<0||bx>=blocksX||by>=blocksY)continue;
                size_t b=(size_t)by*blocksX+bx;if(blockSector[b]!=sector)continue;
                const Flow &flow=blocks[b];
                if(pass==0) {if(flow.weight>strongest){strongest=flow.weight;neighbor=flow.neighbor;}continue;}
                float w=((k&1)?fx:1-fx)*((k>>1)?fy:1-fy)+1e-4f;
                sum+=flow.neighbor==neighbor?flow.weight*w:0;total+=w;
            }
            if(neighbor==noSector||total<=0)continue;
            seamCells[i].flow=(uint16_t)neighbor;
            contactCells[4*i+3]=(uint8_t)std::lround(std::clamp(sum/total,0.0f,1.0f)*255);
        }
    });
    if(fresh)bakeCacheKeep(LoadStage::Flow);
    seamTexture=gpuCreateTexture(GpuFormat::RGBA16Uint,mapWidth,mapHeight,seamCells.data(),mapWidth*8);
    contactTexture=gpuCreateTexture(GpuFormat::RGBA8,mapWidth,mapHeight,contactCells.data(),mapWidth*4);
    if(!seamTexture||!contactTexture)I_Error((char*)"Could not allocate surface maps");
    fprintf(stderr,"3D light flow: every %d cells, baked in %.0f ms.\n",step,(SDL_GetTicksNS()-start)/1e6);
}
// CPU twin of flowLight in world.frag, for sprites and the weapon.
float flowLightAt(float x,float y,const sector_t *sector,float light) {
    if(!settings.lightFlow||settings.lightModel==1||flowKey<0||seamCells.empty())return light;
    int cx=(int)std::floor((x-mapOrigin[0])/mapCell),cy=(int)std::floor((y-mapOrigin[1])/mapCell);
    if(cx<0||cy<0||cx>=mapWidth||cy>=mapHeight)return light;
    size_t i=(size_t)cy*mapWidth+cx;const SeamCell &cell=seamCells[i];
    if(cell.own!=(uint16_t)(sector-sectors)||cell.flow==noSector)return light;
    return light+std::round(std::max(0.0f,lighting(&sectors[cell.flow])-light)*contactCells[4*i+3]/255.0f*0.8f*16)/16;
}
void indirectNewLevel() {bounceKey=-1;ambientKey=-1;flowKey=-1;bakeThingsKey=-1;}
void indirectShutdown() {indirectNewLevel();skyLevel=0;}
} // namespace doom3d
