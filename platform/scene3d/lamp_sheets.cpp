/* The rain round a lamp. A street lamp in a downpour at night shows its
   whole light filled with bright streaks, hundreds of them, brightest by the
   lamp and toward it, thinning out at the edge of its light; the drops round
   the eye (rain.cpp) are one in a 16-unit column and end at 640 units, far
   too few to fill it. So the nearest lamps out in the rain get cards of it:
   upright, facing the eye, standing at the lamp, one a little nearer than
   it and one behind, each with the far sheet's streaks (lamp_sheet.glsl,
   with sky/sheet_cells.glsl's rain cells) lit by the lamp streak by streak.

   The lamps are those weather_lamps.cpp gathered this tic; a lamp gets cards
   only out in a yard under the sky, where it rains, and a card only where its
   middle lies in the lamp's own yard (a card standing into the room behind a
   wall would show rain indoors). Up to mostCards lamps, the strongest for
   their distance first, none behind the eye. A card reaches as far as the
   lamp lights rain (rainLampReach), from its sector's floor up to where the
   rain starts falling (skyFallTop). The streaks fall at the rain's speed,
   stepping at the tic, slanted across the card by the wind as the drops are.
   Nothing while nothing falls, in any weather but rain, or with F4 "Lamps
   light the weather" off. Render-only; rebuilt each frame (the cards turn to
   the eye), a few quads. */
#include "internal.h"
#include "../rooms.h"
#include <cstring>

namespace doom3d {
namespace {
constexpr int mostCards=6;
// Units per sheet pixel, the rain's speed and how many cells of rainCell
// sheet pixels the pattern repeats over: lamp_sheet.glsl's two units,
// rain.cpp's 520 units a second, lampSheetWrap and sheet_cells.glsl's rainCell.
constexpr float sheetUnit=2,rainSpeed=520,wrapCells=4096,rainCellPixels=6;
// The cards stand this share of the lamp's reach nearer than it and behind
// it; a panel's are moved out this share from its wall, into what it lights.
constexpr float cardDepth=0.35f,panelOut=0.4f;
// How bright: at the lamp's strength, the card behind it a little dimmer
// (its drops scatter the light back, not on toward the eye); fading out
// from fadeFrom units to fadeTo away from the eye.
constexpr float cardGain=0.5f,behindShare=0.7f,fadeFrom=360,fadeTo=900;
constexpr float nearestEye=64;  // a nearer card closer to the eye than this is left out: its streaks stood huge across the view
constexpr float pale=0.25f;       // water glints paler than the flame: this far toward white
unsigned bits(float f) {unsigned u;std::memcpy(&u,&f,sizeof u);return u;}
int spaceOf(int sector,bool &outside) {
    outside=Rooms_Outside(sector)!=0;
    return outside?Rooms_Yard(sector):Rooms_Of(sector);
}
// The two cards of one lamp, along f (unit, flat, from the lamp toward the eye).
void lampCards(const WeatherLamp &l,const Uniforms &camera,float fx,float fy,float fall,float bright) {
    float rx=-fy,ry=fx,reach=rainLampBubble(l.radius,l.flame);
    bool panel=l.nx!=0||l.ny!=0;
    float ox=panel?l.nx*panelOut*reach:0,oy=panel?l.ny*panelOut*reach:0;
    float flat=std::hypot(camera.eye[0]-l.x,camera.eye[1]-l.y);
    // Slanted by the wind across the card, as the drops are: one across for so many down.
    float slant=(windNow[0]*rx+windNow[1]*ry)/rainSpeed;
    float span=panel?l.extent:0;
    for(int behind=0;behind<2;++behind) {
        float depth=(behind?-cardDepth:cardDepth)*reach+ox*fx+oy*fy;   // the card's plane from the lamp, toward the eye
        if(!behind&&flat-depth<nearestEye)continue;
        float within=std::sqrt(std::max(0.0f,reach*reach-depth*depth));
        if(within<16)continue;
        // Its middle in the lamp's own yard, out under the sky.
        float cx=l.x+fx*depth+rx*(ox*rx+oy*ry),cy=l.y+fy*depth+ry*(ox*rx+oy*ry);
        int at=int(R_PointInSubsector((fixed_t)(cx*FRACUNIT),(fixed_t)(cy*FRACUNIT))->sector-sectors);
        bool outside;
        int space=spaceOf(at,outside);
        if(!outside||!underSky(at)||(space!=l.space&&space>=0&&l.space>=0))continue;
        const sector_t &sector=sectors[at];
        float bottom=std::max(-within-span,floorZ(&sector)-l.z),top=std::min(within+span+rainSpeed/TICRATE,skyFallTop(sector,208)-l.z);
        if(top-bottom<8)continue;
        float share=bright*(behind?behindShare:1.0f);
        unsigned mode=MODE_LAMP_SHEET|MODE_PARTICLE_OUTSIDE|(behind?MODE_LAMP_SHEET_FAR:0)|(panel?MODE_LAMP_SHEET_PANEL:0);
        auto vertex=[&](float s,float t) {
            Vertex v={l.x+fx*depth+rx*s,l.y+fy*depth+ry*s,l.z+t,s/sheetUnit,t/sheetUnit,reach/sheetUnit,mode};
            float peak=std::max({l.color[0],l.color[1],l.color[2]}),scale=l.strength*share;
            v.red=(l.color[0]+(peak-l.color[0])*pale)*scale;v.green=(l.color[1]+(peak-l.color[1])*pale)*scale;v.blue=(l.color[2]+(peak-l.color[2])*pale)*scale;
            v.lightMask[0]=bits(depth/sheetUnit);v.lightMask[1]=bits(panel?span/sheetUnit:l.post);
            v.sunU=fall;v.sunV=slant;
            // A panel's facing: how far ahead of its wall a point of the card is, by its place across it.
            v.lampTint=bits(panel?rx*l.nx+ry*l.ny:0);
            v.statics=bits(panel?(depth*(fx*l.nx+fy*l.ny)+DOOM_PANEL_OFFSET)/sheetUnit:0);
            return v;
        };
        quad(lampSheetVertices,vertex(-within,top),vertex(within,top),vertex(within,bottom),vertex(-within,bottom));
    }
}
}

void buildLampSheets(const Uniforms &camera) {
    Weather kind=falling();
    if(!settings.weatherLamps||(kind!=WEATHER_RAIN&&kind!=WEATHER_NUKAGE)||fallNow<=0.01f)return;
    const auto &lamps=weatherLamps();
    struct Pick {int lamp;float score;};
    Pick picks[mostCards];
    int count=0;
    float fl=std::hypot(camera.forward[0],camera.forward[1]);
    for(int i=0;i<(int)lamps.size();++i) {
        const WeatherLamp &l=lamps[i];
        if(!l.outside||l.sector<0||!underSky(l.sector))continue;
        float dx=l.x-camera.eye[0],dy=l.y-camera.eye[1],dz=l.z-camera.eye[2],d=std::sqrt(dx*dx+dy*dy+dz*dz);
        float reach=rainLampBubble(l.radius,l.flame);
        if(d>fadeTo+reach||dx*camera.forward[0]+dy*camera.forward[1]+dz*camera.forward[2]<-reach)continue;   // too far, or behind the eye
        float score=l.strength*reach/std::max(d,64.0f);
        // Kept in order, best first.
        if(count<mostCards)picks[count++]={i,score};
        else if(score>picks[mostCards-1].score)picks[mostCards-1]={i,score};
        else continue;
        for(int at=count-1;at>0&&picks[at].score>picks[at-1].score;--at)std::swap(picks[at],picks[at-1]);
    }
    if(!count)return;
    // The fall since the level began, at the tic, in sheet pixels, wrapped where the pattern repeats.
    double now=std::floor(gameSeconds()*TICRATE)/TICRATE;
    float fall=(float)std::fmod(now*rainSpeed/sheetUnit,(double)wrapCells*rainCellPixels);
    for(int n=0;n<count;++n) {
        const WeatherLamp &l=lamps[picks[n].lamp];
        float fx=camera.eye[0]-l.x,fy=camera.eye[1]-l.y,f=std::hypot(fx,fy);
        if(f>1) {fx/=f;fy/=f;}
        else if(fl>1e-3f) {fx=-camera.forward[0]/fl;fy=-camera.forward[1]/fl;}
        else continue;
        float d=std::sqrt(f*f+(camera.eye[2]-l.z)*(camera.eye[2]-l.z));
        float fade=1-std::clamp((d-fadeFrom)/(fadeTo-fadeFrom),0.0f,1.0f);
        // Against a bright yard a lamp's rain hardly shows; in the dark it is all there is.
        float dark=std::clamp(1.35f-lighting(&sectors[l.sector]),0.35f,1.0f);
        float bright=cardGain*fade*fade*dark*std::min(1.0f,fallNow*1.5f)*(l.flame?flameRain:1.0f);
        if(bright>0.01f)lampCards(l,camera,fx,fy,fall,bright);
    }
}
} // namespace doom3d
