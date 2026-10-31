/* Powder: the drafts' snow in the floor film, and the wind moving what lies
   loose. Render-only.
   - Where a draft's trail (drafts.cpp) lies as snow (at or below 0 C), what
     it carries settles into the floor film (floor_film.cpp) as powder, as
     much as the trail has soaked in, up to a layer as deep as it falls
     hard. Slush and wet stay the trail's wear records. So powder lies at
     the door, slush further in, wet beyond, as cold as the air coming in
     is (the climate menu's Temperature sets how far that reaches).
   - How loose it lies is how cold the jet was where it settled: at about
     -5 C or colder it blows about like sand, at 0 C it hardly moves.
   - The gusts at an opening (wind.cpp's windGustRate) drift loose film
     (powder, sand, ash) along the trail and the doorway's stream it comes
     in by, a cell at a time downwind (filmDrift): nothing in a lull or in
     still air, harder the stronger the wind and the gust. Powder carried
     further in warms and melts by the film's own rules. */
#include "internal.h"

namespace doom3d {
namespace {
std::vector<DraftPath> paths;
float gameNow() {return levelSeconds();}
}

void powderDrift(const std::vector<DraftStep> &steps,float reach,float speed,float ox,float oy,float dt) {
    float lift=std::clamp(windAmount()-0.3f+3*windGustRate(ox,oy,gameNow()),0.0f,1.5f);
    if(lift<=0||steps.empty())return;
    float rate=0.5f*lift*std::clamp(speed/60,0.0f,1.5f),along=0;
    for(const DraftStep &g:steps) {
        float l=std::hypot(g.bx-g.ax,g.by-g.ay);
        if(along>=reach||l<=0)break;
        along+=l;
        float dx=(g.bx-g.ax)/l,dy=(g.by-g.ay)/l,mx=(g.ax+g.bx)/2,my=(g.ay+g.by)/2;
        for(float side:{-0.5f,0.0f,0.5f})filmDrift(mx-dy*side*g.width,my+dx*side*g.width,dx,dy,rate*std::min(1.0f,g.amount*2),dt);
    }
}

void watchPowder(const WeatherKind &kind,float dt) {
    if(!kind.film)return;
    draftPaths(paths);
    bool snow=kind.melts&&kind.settles;
    float cap=kind.film->full*std::min(1.0f,fallNow*1.25f);
    static const bool debug=getenv("ROOMSDEBUG")!=nullptr;
    if(debug&&leveltime%350==0) {
        int flowing=0,snowy=0;
        for(const DraftPath &p:paths) {flowing+=p.flowing;for(const DraftStep &g:p.steps)snowy+=g.state==2;}
        fprintf(stderr,"POWDER tic %d: %zu trails, %d flowing, %d snow steps\n",leveltime,paths.size(),flowing,snowy);
    }
    for(const DraftPath &p:paths) {
        if(!p.flowing||p.steps.empty())continue;
        if(snow&&fallNow>0.02f&&p.soak>0) {
            float along=0;
            for(const DraftStep &g:p.steps) {
                float l=std::hypot(g.bx-g.ax,g.by-g.ay);
                if(along>=p.reach||l<=0)break;
                along+=l;
                if(g.state!=2||g.amount<0.1f||std::isnan(g.jet))continue;
                float looseness=std::clamp(-g.jet/5,0.0f,1.0f),dx=(g.bx-g.ax)/l,dy=(g.by-g.ay)/l;
                float mx=(g.ax+g.bx)/2,my=(g.ay+g.by)/2;
                for(float side:{-0.4f,0.0f,0.4f}) {
                    float x=mx-dy*side*g.width,y=my+dx*side*g.width;
                    if(filmAt(x,y)<cap)filmFeed(x,y,0.04f*fallNow*g.amount*p.soak*(1-std::fabs(side)),looseness);
                }
            }
        }
        powderDrift(p.steps,p.reach,p.speed,(p.ax+p.bx)/2,(p.ay+p.by)/2,dt);
    }
}
} // namespace doom3d
