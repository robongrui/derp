/* The relit weapon: its light probe of the surroundings, draws, and the
   crosshair. */
#include "internal.h"

namespace doom3d {
// Weapon frames to relight (weapon_lighting.h), keyed below the WAD lumps:
// the de-lit artwork with its normals in B/A, two transparent columns, then
// its gloss in R.
static Image relitWeaponArt(int w,int h,int left,int top,const byte *pixels) {
    int stride=2*w+2;
    const byte *palette=(const byte*)W_CacheLumpName((char*)"PLAYPAL",PU_CACHE);
    auto maps=doom_weapon_delight(w,h,pixels,palette);
    std::vector<byte> packed((size_t)stride*h*4,0);
    for(int y=0;y<h;++y)for(int x=0;x<w;++x) {
        size_t i=(size_t)y*w+x;byte *art=&packed[((size_t)y*stride+x)*4];
        art[0]=maps.pixels[2*i];art[1]=maps.pixels[2*i+1];art[2]=maps.normals[2*i];art[3]=maps.normals[2*i+1];
        packed[((size_t)y*stride+w+2+x)*4]=maps.gloss[i];
    }
    Image image;image.pixels=std::move(maps.pixels);image.opaque=false;
    image.width=w;image.height=h;image.left=left;image.top=top;
    image.texture=gpuCreateTexture(GpuFormat::RGBA8,stride,h,packed.data(),stride*4);
    if(!image.texture) I_Error((char*)"Could not allocate a GPU texture");
    return image;
}
static Image &weaponImage(int lump) {
    // A frame rendered from a model, where the weapon pack has one (weapon_pack.cpp).
    if(settings.weaponArt) if(Image *packed=weaponPackImage(lump)) return *packed;
    int key=INT_MIN/2-lump; auto found=images.find(key); if(found!=images.end()) return found->second;
    const Image &source=lumpImage(lump,false);
    return images.emplace(key,relitWeaponArt(source.width,source.height,source.left,source.top,source.pixels.data())).first->second;
}
// At level load (sprite_precache.cpp): a weapon frame's relit image made
// ahead, so its first showing in play costs nothing.
void precacheWeaponImage(int lump) {if(settings.weaponLighting)weaponImage(lump);}
// What the relit weapon sees around it, in the view's frame: ahead, behind,
// right, left, up and down (rgb, w unused). Five rays a direction walk the
// bake map from the eye; each hit shows its surface's average color under
// its sector and baked light, plus its glow, as world.frag draws it. Eased
// over about a tenth of a second so single rays changing what they hit don't
// flicker. The shader lights the weapon with it as an ambient cube, and its
// glossy and grazing edges mirror it, so a bright wall ahead creeps over the
// weapon's outline even when no light reaches the player.
float weaponProbe[24]={};
static std::array<float,18> weaponProbeTarget={};
static uint64_t weaponProbeTime=0;static unsigned weaponProbeSerial=~0u;
static void updateWeaponProbe(const Uniforms &camera,sector_t *sector) {
    uint64_t now=SDL_GetTicksNS();
    bool fresh=weaponProbeSerial!=r_levelserial;
    weaponProbeSerial=r_levelserial;
    if(bakeMap.sectors.size()!=(size_t)numsectors)return;
    const float *axes[3]={camera.forward,camera.right,camera.up};
    float eye[3]={camera.eye[0],camera.eye[1],camera.eye[2]};
    const BakeSector &own=bakeMap.sectors[sector-sectors];
    eye[2]=std::clamp(eye[2],own.floor+1,std::max(own.floor+1,own.ceiling-1));
    bool glow=settings.emissive;
    auto lit=[&](const std::array<float,3> &albedo,const Image *image,int at,const uint8_t *baked,float out[3]) {
        float level=lighting(&sectors[at]),shown[3]={};
        if(baked) {for(int c=0;c<3;++c)shown[c]=baked[c]/255.0f*2;fillHeadroom(shown,level);}
        float cover=glow&&image&&image->emissionWeight>0?image->emissionCoverage:0;
        for(int c=0;c<3;++c)out[c]=albedo[c]*(level+shown[c])*(1-cover)+(cover>0?image->emissionColor[c]*cover*0.9f:0);
    };
    auto radiance=[&](const BakeHit &hit,float out[3]) {
        if(hit.kind==BakeHit::thing) {lit({0.4f,0.4f,0.4f},nullptr,hit.sector,nullptr,out);return;}
        if(hit.kind==BakeHit::wall) {
            const line_t &line=lines[hit.line];
            int side=line.frontsector==&sectors[hit.sector]?0:1;
            if(line.sidenum[side]<0) side^=1;
            const side_t &facing=sides[line.sidenum[side]];
            short texture=hit.part==1?facing.toptexture:hit.part==2?facing.bottomtexture:facing.midtexture;
            Image *image=texture>0?&wallImage(texturetranslation[texture]):nullptr;
            const uint8_t *baked=nullptr;
            const BakeStrip &strip=bakeStrips.size()>(size_t)hit.line*2+side?bakeStrips[(size_t)hit.line*2+side]:BakeStrip{};
            if(!wallBakeCells.empty()&&strip.x>=0) {
                float along=(hit.x-strip.ax)*strip.dx+(hit.y-strip.ay)*strip.dy;
                int column=std::clamp((int)std::floor(along*strip.scale),0,strip.width-1);
                int row=std::clamp((int)std::floor((hit.z-strip.low)*strip.scale),0,strip.height-1);
                baked=&wallBakeCells[4*((size_t)(strip.y+row)*atlasStride+strip.x+column)];
            }
            lit(image?averageColor(*image):std::array<float,3>{0.5f,0.5f,0.5f},image,hit.sector,baked,out);
            return;
        }
        bool ceiling=hit.kind==BakeHit::ceiling;
        int pic=ceiling?sectors[hit.sector].ceilingpic:sectors[hit.sector].floorpic;
        Image &image=lumpImage(firstflat+flattranslation[pic],true);
        const uint8_t *baked=nullptr;
        if(!flatLightCells.empty()) {
            int cx=std::clamp((int)std::floor((hit.x-mapOrigin[0])/mapCell),0,mapWidth-1);
            int cy=std::clamp((int)std::floor((hit.y-mapOrigin[1])/mapCell),0,mapHeight-1);
            baked=flatCell(ceiling?ceilingLight:floorLight,(size_t)cy*mapWidth+cx);
        }
        lit(averageColor(image),&image,hit.sector,baked,out);
    };
    std::array<float,18> target={};
    for(int axis=0;axis<6;++axis) {
        float sign=axis&1?-1.0f:1.0f;const float *main=axes[axis/2],*a=axes[(axis/2+1)%3],*b=axes[(axis/2+2)%3];
        // The axis and four rays tilted 35 degrees around it.
        static const float tilt[5][2]={{0,0},{0.7f,0},{-0.7f,0},{0,0.7f},{0,-0.7f}};
        for(const auto &t:tilt) {
            float direction[3],length=0,out[3]={};
            for(int c=0;c<3;++c){direction[c]=main[c]*sign+a[c]*t[0]+b[c]*t[1];length+=direction[c]*direction[c];}
            length=std::sqrt(length);for(float &c:direction)c/=length;
            BakeHit hit;
            if(traceRay(bakeMap,(int)(sector-sectors),eye[0],eye[1],eye[2],direction,2048,SkyCeiling::escape,&hit)||hit.kind==BakeHit::none) {
                // Open sky above, or nothing within reach: the sky's light, or the room's.
                float level=lighting(sector);
                for(int c=0;c<3;++c)out[c]=direction[2]>0.3f&&skyLevel>0?skyTint[c]*skyLevel:0.5f*level;
            } else radiance(hit,out);
            for(int c=0;c<3;++c)target[axis*3+c]+=out[c]/5;
        }
    }
    float seconds=fresh||!weaponProbeTime?1.0f:std::min(0.1f,(now-weaponProbeTime)/1e9f);
    weaponProbeTime=now;float blend=1-std::exp(-seconds/0.1f);
    for(int i=0;i<18;++i) {
        weaponProbeTarget[i]+=(target[i]-weaponProbeTarget[i])*blend;
        weaponProbe[(i/3)*4+i%3]=weaponProbeTarget[i];
    }
}
// The weapon's place on the screen at the last tic (beforeTic), so its bob,
// raise and lower blend between tics like the world. The flash follows the
// weapon, so every layer moves by the weapon's blend.
static fixed_t oldWeaponX,oldWeaponY;
static bool oldWeaponSaved;
void saveWeaponPlace() {
    const pspdef_t &weapon=players[displayplayer].psprites[0];
    oldWeaponX=weapon.sx;oldWeaponY=weapon.sy;oldWeaponSaved=weapon.state!=nullptr;
}
bool muzzleFireHidesFlash(int weapon);   // muzzle_fire.cpp
std::vector<SpriteDraw> weaponDraws(const Uniforms &camera) {
    std::vector<SpriteDraw> draws;
    player_t *player=&players[displayplayer];
    float shiftX=0,shiftY=0;
    if(ticSaved&&oldWeaponSaved&&player->psprites[0].state) {
        float dx=units(oldWeaponX)-units(player->psprites[0].sx),dy=units(oldWeaponY)-units(player->psprites[0].sy);
        // A jump (a new weapon's own place) is taken whole.
        if(std::abs(dx)<64&&std::abs(dy)<64) {shiftX=dx*(1-renderFraction);shiftY=dy*(1-renderFraction);}
    }
    if(settings.weaponLighting&&camera.effects[2]==0&&player->mo)updateWeaponProbe(camera,player->mo->subsector->sector);
    // Authored's kick (p_kick.c): his leg over the gun, which gives it room.
    int kickFrame=0;float kickDip=0;
    bool kicking=Game_Kick(displayplayer,&kickFrame,&kickDip)!=0;
    // Partial invisibility: the hands go to a spectre's fuzz, flickering in
    // its last 128 tics as id's did (R_DrawPSprite), the warning it runs out.
    int invisible=player->powers[pw_invisibility];
    unsigned fuzz=invisible>4*32||(invisible&8)?MODE_FUZZ:0u;
    // One layer: a weapon frame, its flash, or the kicking leg.
    // most: the most of the light round the eye it takes (a gun all of it).
    auto layer=[&](Image &image,float x,float y,bool flipped,bool bright,bool relit,Image *blood,float most) {
        std::vector<Vertex> weapon;
        float u0=flipped?image.width:0,u1=flipped?0:image.width;
        float light=bright?1.0f:sunLightAt(camera.eye[0],camera.eye[1],player->mo->subsector->sector,
                flowLightAt(camera.eye[0],camera.eye[1],player->mo->subsector->sector,
                seamLightAt(camera.eye[0],camera.eye[1],player->mo->subsector->sector,roomPulse(player->mo->subsector->sector,lighting(player->mo->subsector->sector)))));
        screenQuad(weapon,x/160-1,1-y/100,image.width/160.0f,image.height/100.0f,u0,0,u1,image.height,light,
            MODE_SCREEN|MODE_SPRITE|fuzz|(relit?MODE_RELIT|(flipped?MODE_FLIPPED:0u):0u)|(bright?MODE_WEAPON_FLASH:0u));
        // The weapon takes a restrained, capped share of the light around the
        // eye so nearby lamps tint it without washing out the artwork.
        if(!bright) {
            StaticSample statics;
            auto baked=bakedLightAt(camera.eye[0],camera.eye[1],camera.eye[2],player->mo->subsector->sector,&statics);
            // Whole-level lighting: the light round the eye is all the weapon has (no level lies
            // under it: indoors the room's fill, outside only the sun), so it takes it in full.
            if(settings.lightModel==1&&bakedLightsActive)for(float &c:baked)c=std::min(1.0f,c);
            else for(float &c:baked)c=std::min(0.5f,c*0.4f);
            for(float &c:baked)c=std::min(c,most);
            unsigned tint=packedLampTint(statics,light);
            for(auto &vertex:weapon) {vertex.red=baked[0];vertex.green=baked[1];vertex.blue=baked[2];vertex.lampTint=tint;}
            if(relit) {
                // Lights that reach the weapon, which the shader places about
                // ten units ahead of the eye; the flashlight only tints it.
                // With bake-only lights the static light's direction comes
                // packed as for sprites.
                float low[3],high[3];unsigned mask[2]={},packedStatics=0;
                for(int c=0;c<3;++c) {
                    float at=camera.eye[c]+camera.forward[c]*10-camera.up[c]*5;
                    low[c]=at-12;high[c]=at+12;
                }
                for(unsigned n=0;n<flashes.count;++n) {
                    const auto &light=flashes.lights[n];
                    if(!fogOnly[n]&&light.direction[3]<=0&&doom_flash_reaches_bounds(&light,low,high))mask[n/32]|=1u<<(n%32);
                }
                float towards=std::sqrt(statics.towards[0]*statics.towards[0]+statics.towards[1]*statics.towards[1]+statics.towards[2]*statics.towards[2]);
                if(bakeOnlyActive&&statics.amount>0&&towards>1e-6f) {
                    for(int c=0;c<3;++c)packedStatics|=(unsigned)std::lround((statics.towards[c]/towards*0.5f+0.5f)*255)<<(8*c);
                    packedStatics|=(unsigned)std::lround(std::clamp(towards/statics.amount,0.0f,1.0f)*255)<<24;
                }
                for(auto &vertex:weapon) {std::copy(mask,mask+2,vertex.lightMask);vertex.statics=packedStatics;}
            }
        }
        // Gore: the blood on it, over its frame, lit as the frame (gun_blood.cpp).
        std::vector<Vertex> coat;
        if(blood) {coat=weapon;for(Vertex &vertex:coat)vertex.mode&=~(MODE_RELIT|MODE_FLIPPED);}
        draws.push_back({&image,std::move(weapon)});
        if(blood)draws.push_back({blood,std::move(coat)});
    };
    // Lower beside a narrow status bar, to reach the screen's bottom (status_bar.cpp).
    const float base=viewwindowy+viewheight/2.0f-100+weaponDrop();
    for(int i=0;i<NUMPSPRITES;++i) {
        auto psp=player->psprites[i];if(!psp.state)continue;
        // A bullet gun's flash frame gives way to the fire drawn at its barrel (muzzle_fire.cpp).
        if(i==ps_flash&&muzzleFireHidesFlash(player->readyweapon))continue;
        auto sf=sprites[psp.state->sprite].spriteframes[psp.state->frame&FF_FRAMEMASK];
        bool bright=psp.state->frame&FF_FULLBRIGHT;
        // MODE_RELIT on the screen: the weapon relit per pixel from its own
        // normals, its painted light taken out.
        bool relit=settings.weaponLighting&&!bright;
        int lump=firstspritelump+sf.lump[0];
        Image &image=relit?weaponImage(lump):lumpImage(lump,false);
        // The blood coat follows id's artwork; a model frame has another outline.
        bool modelled=relit&&settings.weaponArt&&weaponPackImage(lump);
        layer(image,units(psp.sx)+shiftX-image.left-kickDip*12,base+units(psp.sy)+shiftY-image.top+kickDip*36,
              sf.flip[0],bright,relit,i==0&&!modelled?gunBloodImage(lump):nullptr,1);
    }
    if(kicking) {
        // Placed as a weapon frame at rest is: sx 1, sy 32.
        // Relit with the guns, its paint kept (kick_view.cpp); as painted, its paint carries its own
        // light, so under a bright lamp it stops short of white.
        bool relit=settings.weaponLighting;
        Image &image=kickImage(kickFrame,relit);
        layer(image,1.0f-image.left,base+32-image.top,false,false,relit,nullptr,relit?1.0f:0.75f);
    }
    return draws;
}
std::vector<Vertex> crosshairVertices(float worldW,float worldH) {
    std::vector<Vertex> cross;
    if(!worldPending||!settings.crosshair||menuactive)return cross;
    float px=2/worldW,py=2/worldH;
    screenQuad(cross,-7*px,py,5*px,2*py,0,0,1,1);screenQuad(cross,2*px,py,5*px,2*py,0,0,1,1);
    screenQuad(cross,-px,7*py,2*px,5*py,0,0,1,1);screenQuad(cross,-px,-2*py,2*px,5*py,0,0,1,1);
    return cross;
}
} // namespace doom3d
