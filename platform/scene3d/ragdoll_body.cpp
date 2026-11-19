/* The body a ragdoll fall moves (ragdoll.cpp): a rigged drawing raised into
   a relief. Each texel rises as the tube sprite_normals.cpp finds under it,
   at true depth; the side drawing (rotation 3) then sets how far the body
   reaches forward and how far back, per band (legs, trunk, head), so a body
   lying down is as deep as its own side view rather than a round bolster.
   Front and back are raised separately (the back painted from the drawing
   seen from behind, rotation 5), walls close the outline, and every
   corner carries the weights of the parts it follows. From the lab:
   tests/death_relief.py (mesh, weights, side_profile). The arms (gore
   branch) are found here too, beside the torso under the shoulders, and
   follow an upper arm and a forearm of their own. */
#include "ragdoll.h"
#include <memory>

namespace doom3d {
static float ramp(float z,float joint,float half=1) {return std::clamp((z-joint)/(2*half)+0.5f,0.0f,1.0f);}

// Which parts a rest point follows: below the hips a leg (left or right of
// where the legs part, blended over 3 units), its thigh above the knee and
// shin below; above them pelvis, chest and head, each blended over a unit
// or so at its joint. Texels the rig carries (a gun) ride the gun bone alone.
// An arm's texels (class): its upper arm, its forearm past the elbow (blended
// over 1.5 units), and where it leaves the torso half the chest's (over 2.5).
// A leg's texels (class, under the pelvis's blend): that leg's thigh and shin alone, never the other's.
enum { TEXEL_BODY,TEXEL_CARRIED,TEXEL_ARM_L,TEXEL_ARM_R,TEXEL_LEG_L,TEXEL_LEG_R };
struct Arm { bool found=false;V3 start,elbow,hand; };
struct Joints { float hip,chest,neck,knee[2],split;Arm arm[2]; };
static void weigh(const Joints &j,V3 p,int cls,ReliefCorner &out) {
    float w[DOLL_PARTS]={};
    if(cls==TEXEL_CARRIED)w[PART_GUN]=1;
    else if(cls==TEXEL_ARM_L||cls==TEXEL_ARM_R) {
        int s=cls==TEXEL_ARM_R;const Arm &a=j.arm[s];
        V3 along=a.hand-a.start;float len=std::max(1.0f,length(along));
        float t=dot(p-a.start,along)/len,attach=std::clamp(length(p-a.start)/2.5f,0.0f,1.0f);
        float fore=ramp(t,length(a.elbow-a.start),1.5f);
        w[s?PART_UPPER_R:PART_UPPER_L]=attach*(1-fore);w[s?PART_FORE_R:PART_FORE_L]=attach*fore;w[PART_CHEST]=1-attach;
    }
    else {
        float up=ramp(p.z,j.hip,1.5f);
        if(up>0) {
            float c=ramp(p.z,j.chest),hd=ramp(p.z,j.neck);
            w[PART_PELVIS]+=up*(1-c);w[PART_CHEST]+=up*c*(1-hd);w[PART_HEAD]+=up*c*hd;
        }
        if(up<1) {
            float left=cls==TEXEL_LEG_L?1.0f:cls==TEXEL_LEG_R?0.0f:std::clamp(0.5f-(p.x-j.split)/3,0.0f,1.0f);
            for(int s=0;s<2;++s) {
                float share=s?1-left:left;
                if(share<=0)continue;
                float shin=1-ramp(p.z,j.knee[s]);
                w[s?PART_THIGH_R:PART_THIGH_L]+=(1-up)*share*(1-shin);
                w[s?PART_SHIN_R:PART_SHIN_L]+=(1-up)*share*shin;
            }
        }
    }
    int order[DOLL_PARTS];
    for(int i=0;i<DOLL_PARTS;++i)order[i]=i;
    std::sort(order,order+DOLL_PARTS,[&](int a,int b){return w[a]>w[b];});
    float sum=w[order[0]]+w[order[1]]+w[order[2]]+w[order[3]];
    for(int k=0;k<4;++k) {out.part[k]=(uint8_t)order[k];out.w[k]=sum>0?w[order[k]]/sum:(k==0);}
}

// How far the body reaches forward and back at a height, from the side
// drawing (the body's face toward its left, its back to the right, the
// origin on the drawing's plane). One reach each way per band, a low
// percentile of the rows' (an arm or a gun held out reaches forward in some
// rows; taken per row they raised the chest into a fin); in the legs, which
// the side view's stride parts, each leg is as deep as its own run, split
// evenly. Blended between the bands' middles.
struct Reach { float z,front,back; };
static std::vector<Reach> sideReach(const Image &side,bool flip,int top) {
    static const float bands[3][2]={{0,0.45f},{0.45f,0.8f},{0.8f,1.2f}};
    int left=flip?side.width-side.left:side.left;
    std::vector<std::pair<float,std::pair<float,float>>> rows;   // height, (front, back)
    for(int r=0;r<side.height;++r) {
        std::vector<std::pair<int,int>> runs;
        int start=-1;
        for(int c=0;c<=side.width;++c) {
            int texel=flip?side.width-1-c:c;
            bool on=c<side.width&&side.pixels[((size_t)r*side.width+texel)*2+1];
            if(on&&start<0)start=c;
            if(!on&&start>=0) {if(c-start>=2)runs.push_back({start-left,c-left});start=-1;}
        }
        if(runs.empty())continue;
        float z=(float)(side.top-r-1);
        if(z+0.5f<0.45f*top) {
            std::vector<int> widths;
            for(auto &run:runs)widths.push_back(run.second-run.first);
            std::sort(widths.begin(),widths.end());
            float w=widths[widths.size()/2]/2.0f;
            rows.push_back({z,{w,w}});
        } else {
            auto nearest=*std::min_element(runs.begin(),runs.end(),[](const std::pair<int,int> &a,const std::pair<int,int> &b) {
                auto off=[](const std::pair<int,int> &q){return q.first<=0&&q.second>=0?0:std::min(std::abs(q.first),std::abs(q.second));};
                return off(a)<off(b);
            });
            rows.push_back({z,{std::max(0.5f,(float)-nearest.first),std::max(0.5f,(float)nearest.second)}});
        }
    }
    std::vector<Reach> marks;
    for(const auto &band:bands) {
        std::vector<float> fronts,backs;
        for(auto &row:rows)
            if(row.first+0.5f>=band[0]*top&&row.first+0.5f<band[1]*top) {fronts.push_back(row.second.first);backs.push_back(row.second.second);}
        if(fronts.empty())continue;
        std::sort(fronts.begin(),fronts.end());std::sort(backs.begin(),backs.end());
        size_t at=(size_t)(0.4f*(fronts.size()-1));
        marks.push_back({(band[0]+std::min(band[1],1.0f))/2*top,fronts[at],backs[at]});
    }
    // Above the legs, row by row after all (the user: the bands' blocks read as a gingerbread man): each
    // row the median of those within 4 units of it (an arm or a gun held out for a few rows makes no fin),
    // and no further than its band's reach and 4 units. The legs keep their band.
    if(marks.size()<2)return marks;
    std::vector<Reach> rowMarks;
    for(const Reach &m:marks)if(m.z<0.45f*top)rowMarks.push_back(m);
    for(auto &row:rows) {
        float z=row.first+0.5f;
        if(z<0.45f*top)continue;
        std::vector<float> fronts,backs;
        for(auto &other:rows)
            if(other.first+0.5f>=0.45f*top&&std::abs(other.first-row.first)<=4) {fronts.push_back(other.second.first);backs.push_back(other.second.second);}
        std::sort(fronts.begin(),fronts.end());std::sort(backs.begin(),backs.end());
        const Reach &band=z<0.8f*top?marks[std::min<size_t>(1,marks.size()-1)]:marks.back();
        rowMarks.push_back({z,std::min(fronts[fronts.size()/2],band.front+4),std::min(backs[backs.size()/2],band.back+4)});
    }
    std::sort(rowMarks.begin(),rowMarks.end(),[](const Reach &a,const Reach &b){return a.z<b.z;});
    return rowMarks;
}
static void reachAt(const std::vector<Reach> &marks,float z,float &front,float &back) {
    if(z<=marks.front().z) {front=marks.front().front;back=marks.front().back;return;}
    for(size_t i=1;i<marks.size();++i)
        if(z<=marks[i].z) {
            float w=(z-marks[i-1].z)/(marks[i].z-marks[i-1].z);
            front=marks[i-1].front+(marks[i].front-marks[i-1].front)*w;back=marks[i-1].back+(marks[i].back-marks[i-1].back)*w;
            return;
        }
    front=marks.back().front;back=marks.back().back;
}

// The back's colours: each texel's from the drawing seen from behind, mirrored onto the body (the
// monster's right, on the front drawing's left, is on the back drawing's right), at the same height;
// where the outlines differ, the nearest opaque texel a few away (the same row first), else the front's.
static std::vector<byte> backColours(const std::vector<byte> &pixels,int w,int h,int left,int top,const Image &back,bool flip) {
    std::vector<byte> out((size_t)w*h);
    auto opaque=[&](int c,int r){return c>=0&&r>=0&&c<back.width&&r<back.height&&back.pixels[((size_t)r*back.width+c)*2+1];};
    for(int r=0;r<h;++r)for(int c=0;c<w;++c) {
        size_t i=(size_t)r*w+c;out[i]=pixels[i*2];
        int cb=flip?c-left+back.left:back.left+left-c-1,rb=r+back.top-top;
        int best=-1,bestDistance=INT_MAX;
        for(int dr=-2;dr<=2;++dr)for(int dc=-3;dc<=3;++dc) {
            int d=dc*dc+2*dr*dr;
            if(d<bestDistance&&opaque(cb+dc,rb+dr)) {bestDistance=d;best=(rb+dr)*back.width+cb+dc;}
        }
        if(best>=0)out[i]=back.pixels[(size_t)best*2];
    }
    return out;
}

// A ball's corner on its four nearest shell points, the nearest the most.
constexpr int ballShellPoints=48;
static void weighShell(const std::vector<V3> &shell,ReliefCorner &out) {
    std::array<int,ballShellPoints> order;
    for(int i=0;i<ballShellPoints;++i)order[i]=i;
    auto gap=[&](int i){V3 d=shell[i]-out.p;return dot(d,d);};
    std::partial_sort(order.begin(),order.begin()+4,order.end(),[&](int a,int b){return gap(a)<gap(b);});
    float sum=0;
    for(int k=0;k<4;++k) {out.part[k]=(uint8_t)order[k];out.w[k]=1/(gap(order[k])+1);sum+=out.w[k];}
    for(int k=0;k<4;++k)out.w[k]/=sum;
}
static std::unordered_map<int,std::unique_ptr<Relief>> reliefs;   // by lump, null where none
void reliefsShutdown() {reliefs.clear();}
// A side drawing's colour at u units forward of the origin as drawn (the way the body faces in it: left in
// the left side's drawing, right in the right's) and z over the feet: its texel there, else the nearest
// opaque one a texel or two away (the same row first); -1 where it has none.
static int sideColour(const Image &side,bool flip,float u,float z) {
    int left=flip?side.width-side.left:side.left;
    int c=(int)std::floor(u)+left,r=(int)std::floor(side.top-z);
    int best=-1,bestDistance=INT_MAX;
    for(int dr=-2;dr<=2;++dr)for(int dc=-2;dc<=2;++dc) {
        int x=c+dc,y=r+dr,d=dc*dc+2*dr*dr;
        if(d>=bestDistance||x<0||y<0||x>=side.width||y>=side.height)continue;
        size_t texel=(size_t)y*side.width+(flip?side.width-1-x:x);
        if(side.pixels[texel*2+1]) {bestDistance=d;best=side.pixels[texel*2];}
    }
    return best;
}

// The gaps in a drawing closed, each texel the colour of the nearest drawn one: all it encloses, and clefts up to
// twice reach wide. The Mancubus' cannons and hoses stand off his belly, and the doll has no art for what's between:
// turned, those gaps were holes down into him.
static void closeGaps(std::vector<byte> &pixels,int w,int h,int reach) {
    auto on=[&](const std::vector<byte> &m,int c,int r){return c>=0&&r>=0&&c<w&&r<h&&m[(size_t)r*w+c];};
    std::vector<byte> drawn((size_t)w*h),grown((size_t)w*h,0),closed((size_t)w*h,0);
    for(size_t i=0;i<drawn.size();++i)drawn[i]=pixels[i*2+1]!=0;
    for(int r=0;r<h;++r)for(int c=0;c<w;++c)
        for(int j=-reach;j<=reach&&!grown[(size_t)r*w+c];++j)for(int i=-reach;i<=reach;++i)
            if(i*i+j*j<=reach*reach&&on(drawn,c+i,r+j)) {grown[(size_t)r*w+c]=1;break;}
    // Shrunk back, the world past the picture's edge counting as empty.
    for(int r=0;r<h;++r)for(int c=0;c<w;++c) {
        bool keep=grown[(size_t)r*w+c];
        for(int j=-reach;j<=reach&&keep;++j)for(int i=-reach;i<=reach;++i)
            if(i*i+j*j<=reach*reach&&!on(grown,c+i,r+j)) {keep=false;break;}
        closed[(size_t)r*w+c]=keep||drawn[(size_t)r*w+c];
    }
    // What the outside can't reach is enclosed.
    std::vector<byte> outside((size_t)w*h,0);std::vector<int> stack;
    auto seed=[&](int c,int r){size_t i=(size_t)r*w+c;if(!closed[i]&&!outside[i]) {outside[i]=1;stack.push_back((int)i);}};
    for(int c=0;c<w;++c) {seed(c,0);seed(c,h-1);}
    for(int r=0;r<h;++r) {seed(0,r);seed(w-1,r);}
    while(!stack.empty()) {
        int i=stack.back(),c=i%w,r=i/w;stack.pop_back();
        if(c>0)seed(c-1,r);
        if(c+1<w)seed(c+1,r);
        if(r>0)seed(c,r-1);
        if(r+1<h)seed(c,r+1);
    }
    const std::vector<byte> art=pixels;
    for(int r=0;r<h;++r)for(int c=0;c<w;++c) {
        size_t n=(size_t)r*w+c;
        if(drawn[n]||outside[n])continue;
        int best=INT_MAX;byte colour=0;
        for(int j=-12;j<=12;++j)for(int i=-12;i<=12;++i)
            if(i*i+j*j<best&&on(drawn,c+i,r+j)) {best=i*i+j*j;colour=art[((size_t)(r+j)*w+c+i)*2];}
        if(best==INT_MAX)continue;
        pixels[n*2]=colour;pixels[n*2+1]=255;
    }
}
const Relief *reliefFor(int lump,int sideLump,bool sideFlip,int rightLump,bool rightFlip,int backLump,bool backFlip,bool ball) {
    if(lump<0)return nullptr;
    int key=ball?-1-lump:lump;   // (a drawing's rig and its ball kept apart)
    auto found=reliefs.find(key);
    if(found!=reliefs.end())return found->second.get();
    auto &slot=reliefs[key];
    RigJoints rig{};
    // A ball takes a rig's depth and closed gaps too where its drawing has one (the Mancubus as a soft body).
    if(!rigJoints(lump,rig)&&!ball)return nullptr;
    const Image &image=lumpImage(lump,false);
    int w=image.width,h=image.height,left=image.left,top=image.top;
    std::vector<byte> pixels=image.pixels;
    if(rig.depth>0)closeGaps(pixels,w,h,4);
    auto opaque=[&](int c,int r){return c>=0&&r>=0&&c<w&&r<h&&pixels[((size_t)r*w+c)*2+1];};
    // Front and back heights: the tube, reshaped per row to the side drawing's reach
    // (each texel in proportion to its tube height, within 0.3-1.3 of the tube's).
    std::vector<float> tube=spriteTubeHeights(w,h,pixels.data()),front=tube,back=tube;
    if(sideLump>=0&&sideLump!=lump) {
        std::vector<Reach> marks=sideReach(lumpImage(sideLump,false),sideFlip,top);
        if(!marks.empty())
            for(int r=0;r<h;++r) {
                float peak=0;
                for(int c=0;c<w;++c)peak=std::max(peak,tube[(size_t)r*w+c]);
                if(peak<=0)continue;
                float f,b;reachAt(marks,(float)(top-r-1),f,b);
                f=std::clamp(f,0.3f*peak,1.3f*peak);b=std::clamp(b,0.3f*peak,1.3f*peak);
                if(rig.depth>0) {f=std::min(f,rig.depth);b=std::min(b,rig.depth);}
                for(int c=0;c<w;++c) {size_t i=(size_t)r*w+c;front[i]=f*tube[i]/peak;back[i]=b*tube[i]/peak;}
            }
    }
    auto relief=std::make_unique<Relief>();
    relief->width=w;relief->height=h;
    relief->colour.resize((size_t)w*h);
    for(size_t i=0;i<relief->colour.size();++i)relief->colour[i]=pixels[i*2];
    relief->backColour=backLump>=0&&backLump!=lump?backColours(pixels,w,h,left,top,lumpImage(backLump,false),backFlip):relief->colour;
    relief->tall=ball?1.0f:1.2f;   // a ball stays round: its shell is no skeleton to lengthen
    {
        float deepest=0,tallH=h*relief->tall;
        for(size_t i=0;i<tube.size();++i)deepest=std::max(deepest,front[i]+back[i]);
        int size=(int)std::ceil(std::sqrt((float)w*w+tallH*tallH+deepest*deepest))+8;
        relief->cardSize=(size+3)&~3;
    }
    // The skeleton as the drawing stands: the rig's legs, the chest joint 45% of the way from the
    // hips to the neck, shoulders a little under the neck at 80% of the body's half width there.
    float hip=rig.hip[0][1],mid=(rig.hip[0][0]+rig.hip[1][0])/2;
    relief->hasHead=rig.hasHead;
    float neck=rig.hasHead?rig.neck:0.8f*top,headU=rig.hasHead?rig.headMiddle:mid,chest=hip+0.45f*(neck-hip);
    relief->neck=neck;relief->headMiddle=headU;relief->headWidth=rig.headWidth;relief->guided=rig.guided;
    float half=7;
    {
        int row=std::clamp(top-(int)(neck-3),0,h-1);float lo=1e9f,hi=-1e9f;
        for(int c=0;c<w;++c)if(opaque(c,row)) {lo=std::min(lo,(float)(c-left));hi=std::max(hi,(float)(c-left));}
        if(hi>=lo)half=std::max(2.0f,0.8f*std::min(mid-lo,hi-mid));
    }
    DollPose &rest=relief->rest;
    rest[P_HEAD]={headU,0,(float)top-4};rest[P_NECK]={headU,0,neck};
    rest[P_SH_L]={mid-half,0,neck-3};rest[P_SH_R]={mid+half,0,neck-3};rest[P_SPINE]={mid,0,chest};
    rest[P_HIP_L]={rig.hip[0][0],0,rig.hip[0][1]};rest[P_HIP_R]={rig.hip[1][0],0,rig.hip[1][1]};
    rest[P_KNEE_L]={rig.knee[0][0],0,rig.knee[0][1]};rest[P_KNEE_R]={rig.knee[1][0],0,rig.knee[1][1]};
    rest[P_ANK_L]={rig.ankle[0][0],0,rig.ankle[0][1]};rest[P_ANK_R]={rig.ankle[1][0],0,rig.ankle[1][1]};
    Joints joints={hip,chest,neck,{rig.knee[0][1],rig.knee[1][1]},rig.split,{}};
    // The arms: under the shoulders, outside the torso (the shoulders' half width and a bit) and the
    // legs (the hips' and four units), above the knees, not carried (the gun): each side's texels.
    // The hand the far third's middle from the shoulder, where it leaves the torso the near fifth's,
    // the elbow halfway; a side with too few hangs inside the torso, drawing nothing (a demon).
    std::vector<int> classes((size_t)w*h,TEXEL_BODY);
    if(!ball) {
        float knee=std::max(rig.knee[0][1],rig.knee[1][1]),shoulder=neck-3,torso=half+1.5f;
        float legs=std::max(std::abs(rig.hip[0][0]-mid),std::abs(rig.hip[1][0]-mid))+4;
        std::vector<std::pair<float,V3>> side[2];
        for(int r=0;r<h;++r)for(int c=0;c<w;++c) {
            size_t i=(size_t)r*w+c;
            if(!opaque(c,r))continue;
            if(rig.carried&&(*rig.carried)[i]) {classes[i]=TEXEL_CARRIED;continue;}
            V3 q{c-left+0.5f,0,top-r-0.5f};
            float off=std::abs(q.x-mid);
            if(q.z>shoulder||q.z<knee||off<=torso||(q.z<hip&&off<=legs))continue;
            int s=q.x>mid;
            V3 sh=s?V3{mid+half,0,shoulder}:V3{mid-half,0,shoulder};
            side[s].push_back({length(q-sh),q});
            classes[i]=s?TEXEL_ARM_R:TEXEL_ARM_L;
        }
        for(int s=0;s<2;++s) {
            Arm &a=joints.arm[s];
            V3 sh=s?V3{mid+half,0,shoulder}:V3{mid-half,0,shoulder};
            if(side[s].size()<6) {
                for(int &k:classes)if(k==(s?TEXEL_ARM_R:TEXEL_ARM_L))k=TEXEL_BODY;
                float in=s?-1.0f:1.0f;
                a.start=sh;a.elbow=sh+V3{in,0,-6};a.hand=sh+V3{in,0,-11};
                continue;
            }
            std::sort(side[s].begin(),side[s].end(),[](const std::pair<float,V3> &x,const std::pair<float,V3> &y){return x.first<y.first;});
            auto mean=[&](size_t from,size_t to){V3 m{};for(size_t k=from;k<to;++k)m=m+side[s][k].second;return m*(1.0f/(to-from));};
            size_t n=side[s].size();
            a.found=true;a.start=mean(0,std::max<size_t>(1,n/5));a.hand=mean(n-std::max<size_t>(1,n/3),n);
            a.elbow=middle(a.start,a.hand);
        }
    }
    rest[P_ELB_L]=joints.arm[0].elbow;rest[P_ELB_R]=joints.arm[1].elbow;rest[P_HAND_L]=joints.arm[0].hand;rest[P_HAND_R]=joints.arm[1].hand;
    relief->arms[0]=joints.arm[0].found;relief->arms[1]=joints.arm[1].found;
    // A ball has no skeleton: its corners follow a shell of points round it, spread evenly (a golden spiral), and its
    // doll's points are a cross through its middle, there for the blood and shots that ask for a point on the body.
    if(ball) {
        double sx=0,sz=0;long n=0;
        for(int r=0;r<h;++r)for(int c=0;c<w;++c)if(opaque(c,r)) {sx+=c-left+0.5;sz+=top-r-0.5;++n;}
        if(n<16)return nullptr;
        relief->ball=true;relief->ballMiddle={(float)(sx/n),0,(float)(sz/n)};relief->ballRadius=std::sqrt(n/(float)doomPi);
        V3 m=relief->ballMiddle;float k=relief->ballRadius*0.5f;
        for(int i=0;i<ballShellPoints;++i) {
            float up=1-2*(i+0.5f)/ballShellPoints,ring=std::sqrt(1-up*up),turn=i*2.39996f;
            relief->shell.push_back(m+V3{ring*std::cos(turn),ring*std::sin(turn),up}*relief->ballRadius);
        }
        rest[P_SPINE]=m;rest[P_NECK]=m+V3{0,0,k};rest[P_HEAD]=m+V3{0,0,1.6f*k};
        for(int s=0;s<2;++s) {
            float x=s?k:-k;
            rest[s?P_SH_R:P_SH_L]=m+V3{x,0,0};rest[s?P_ELB_R:P_ELB_L]=m+V3{1.3f*x,0,-0.5f*k};rest[s?P_HAND_R:P_HAND_L]=m+V3{1.3f*x,0,-k};
            rest[s?P_HIP_R:P_HIP_L]=m+V3{x,0,-k};rest[s?P_KNEE_R:P_KNEE_L]=m+V3{x,0,-1.4f*k};rest[s?P_ANK_R:P_ANK_L]=m+V3{x,0,-1.8f*k};
        }
    }
    // Each texel under the hips' blend to one leg, the one whose bones (hip, knee, ankle) lie nearer: blended
    // across a straight split, the imp's knock knees and touching feet followed both legs, and the skin
    // between them stretched as they parted (the user: fused feet). Its own corners, so the legs part
    // cleanly; they meet the body's only above the blend, where both follow the pelvis alone.
    if(!ball) {
        auto segment=[](V3 q,V3 a,V3 b) {V3 d=b-a;float l2=d.x*d.x+d.z*d.z,t=l2>1e-6f?std::clamp(((q.x-a.x)*d.x+(q.z-a.z)*d.z)/l2,0.0f,1.0f):0;
                                         return std::hypot(q.x-a.x-d.x*t,q.z-a.z-d.z*t);};
        for(int r=0;r<h;++r)for(int c=0;c<w;++c) {
            size_t i=(size_t)r*w+c;
            if(!opaque(c,r)||classes[i]!=TEXEL_BODY)continue;
            V3 q{c-left+0.5f,0,top-r-0.5f};
            if(q.z>=hip+1.5f)continue;
            float l=std::min(segment(q,rest[P_HIP_L],rest[P_KNEE_L]),segment(q,rest[P_KNEE_L],rest[P_ANK_L]));
            float rr=std::min(segment(q,rest[P_HIP_R],rest[P_KNEE_R]),segment(q,rest[P_KNEE_R],rest[P_ANK_R]));
            classes[i]=l<=rr?TEXEL_LEG_L:TEXEL_LEG_R;
        }
    }
    if(M_CheckParm((char*)"-ragdolllog"))
        for(int s=0;s<2;++s)fprintf(stderr,"ragdollarm lump %d side %d found %d shoulder %.1f,%.1f elbow %.1f,%.1f hand %.1f,%.1f\n",lump,s,(int)joints.arm[s].found,
                                    rest[s?P_SH_R:P_SH_L].x,rest[s?P_SH_R:P_SH_L].z,joints.arm[s].elbow.x,joints.arm[s].elbow.z,joints.arm[s].hand.x,joints.arm[s].hand.z);
    // Corners: each the mean of the four texels around it (empty ones 0), so the outline sits half raised.
    auto cornerHeight=[&](const std::vector<float> &heights,int c,int r) {
        float sum=0;
        for(int a=0;a<2;++a)for(int b=0;b<2;++b) {int x=c-a,y=r-b;if(opaque(x,y))sum+=heights[(size_t)y*w+x];}
        return sum/4;
    };
    std::unordered_map<uint32_t,uint32_t> index;
    auto corner=[&](int c,int r,int side,int cls)->uint32_t {
        uint32_t key=(((uint32_t)r*(w+1)+c)*2+side)*8+cls;   // cls under 8
        auto at=index.find(key);
        if(at!=index.end())return at->second;
        ReliefCorner rc;
        float d=side?cornerHeight(back,c,r):-cornerHeight(front,c,r);
        rc.p={(float)(c-left),d,(float)(top-r)};
        if(ball)weighShell(relief->shell,rc);
        else weigh(joints,rc.p,cls,rc);
        relief->corners.push_back(rc);
        return index[key]=(uint32_t)relief->corners.size()-1;
    };
    static const int edges[4][4]={{0,-1,0,1},{1,0,1,2},{0,1,2,3},{-1,0,3,0}};   // neighbour, then the edge's two corners
    relief->frontFace.assign((size_t)w*h,-1);
    for(int r=0;r<h;++r)for(int c=0;c<w;++c) {
        if(!opaque(c,r))continue;
        int cls=classes[(size_t)r*w+c];
        const int cc[4][2]={{c,r},{c+1,r},{c+1,r+1},{c,r+1}};
        ReliefFace face{};face.c=(uint16_t)c;face.r=(uint16_t)r;
        face.kind=FACE_FRONT;face.dim=1;
        relief->frontFace[(size_t)r*w+c]=(int32_t)relief->faces.size();
        for(int k=0;k<4;++k)face.corner[k]=corner(cc[k][0],cc[k][1],0,cls);
        relief->faces.push_back(face);
        face.kind=FACE_BACK;
        for(int k=0;k<4;++k)face.corner[k]=corner(cc[3-k][0],cc[3-k][1],1,cls);
        relief->faces.push_back(face);
        for(const auto &e:edges) {
            if(opaque(c+e[0],r+e[1]))continue;
            const int *p=cc[e[2]],*q=cc[e[3]];
            face.kind=FACE_WALL;face.dim=0.7f;
            face.corner[0]=corner(p[0],p[1],0,cls);face.corner[1]=corner(p[0],p[1],1,cls);
            face.corner[2]=corner(q[0],q[1],1,cls);face.corner[3]=corner(q[0],q[1],0,cls);
            relief->faces.push_back(face);
        }
    }
    // Each face shows the drawing seen most nearly head-on to it (the user: from the side the body was a
    // gingerbread man, the front drawing seen edge-on): front and back their own, the flanks the side
    // drawings (x is the body's left, y toward its back), darker the more edge-on it stands to that one.
    // Where two meet (between 37 and 53 degrees) the drawings rarely agree texel for texel, so the seam
    // shows the body's common colour at that height instead: the most frequent inside its outline.
    std::vector<int> rowColour(h,-1);
    for(int r=0;r<h;++r) {
        int counts[256]={},most=0;
        for(int rr=std::max(0,r-1);rr<=std::min(h-1,r+1);++rr)
            for(int c=0;c<w;++c)if(opaque(c,rr)&&opaque(c-1,rr)&&opaque(c+1,rr))++counts[pixels[((size_t)rr*w+c)*2]];
        for(int k=0;k<256;++k)if(counts[k]>most) {most=counts[k];rowColour[r]=k;}
    }
    const Image *sides[2]={sideLump>=0&&sideLump!=lump?&lumpImage(sideLump,false):nullptr,
                           rightLump>=0&&rightLump!=lump?&lumpImage(rightLump,false):nullptr};
    bool sideFlips[2]={sideFlip,rightFlip};
    for(ReliefFace &face:relief->faces) {
        V3 a=relief->corners[face.corner[0]].p,b=relief->corners[face.corner[1]].p,c=relief->corners[face.corner[2]].p,d=relief->corners[face.corner[3]].p;
        V3 n=unit(cross(d-b,c-a)),m=(a+b+c+d)*0.25f;
        size_t texel=(size_t)face.r*w+face.c;
        face.colour=face.kind==FACE_BACK?relief->backColour[texel]:relief->colour[texel];
        float across=std::abs(n.x),along=std::abs(n.y);
        const Image *drawn=sides[n.x<0];
        if(drawn&&across>along*0.75f) {
            float u=n.x<0?-m.y:m.y;   // forward is -y: left in the left side's drawing, right in the right's
            int colour=across>along*1.33f?sideColour(*drawn,sideFlips[n.x<0],u,m.z):-1;
            if(colour<0)colour=rowColour[face.r];
            if(colour>=0) {face.colour=(byte)colour;face.dim=std::max(face.dim,0.9f)*(0.4f+0.6f*std::max(across,along));continue;}
        }
        face.dim*=0.4f+0.6f*along;
    }
    slot=std::move(relief);
    return slot.get();
}

// A part's frame: from its joint toward the next one, turned about that line
// by the side the part's points give (the hips for the legs and pelvis, the
// shoulders for chest, head and gun). An arm's side is the trunk's way up and
// half the shoulder line: an arm held out along the shoulder line, or hanging
// along the trunk, still has one.
static void frame(const DollPose &p,DollPart part,V3 &origin,V3 axes[3]) {
    static const int parts[DOLL_PARTS][4]={      // origin (-1: between the hips), tip, side from, side to (-1: the arm's)
        {-1,P_SPINE,P_HIP_L,P_HIP_R},{P_SPINE,P_NECK,P_SH_L,P_SH_R},{P_NECK,P_HEAD,P_SH_L,P_SH_R},
        {P_HIP_L,P_KNEE_L,P_HIP_L,P_HIP_R},{P_KNEE_L,P_ANK_L,P_HIP_L,P_HIP_R},
        {P_HIP_R,P_KNEE_R,P_HIP_L,P_HIP_R},{P_KNEE_R,P_ANK_R,P_HIP_L,P_HIP_R},{P_SPINE,P_NECK,P_SH_L,P_SH_R},
        {P_SH_L,P_ELB_L,-1,-1},{P_ELB_L,P_HAND_L,-1,-1},{P_SH_R,P_ELB_R,-1,-1},{P_ELB_R,P_HAND_R,-1,-1}};
    const int *d=parts[part];
    origin=d[0]<0?middle(p[P_HIP_L],p[P_HIP_R]):p[d[0]];
    V3 u=unit(p[d[1]]-origin),s=d[2]<0?p[P_NECK]-p[P_SPINE]+(p[P_SH_R]-p[P_SH_L])*0.5f:p[d[3]]-p[d[2]];
    s=unit(s-u*dot(s,u));
    axes[0]=u;axes[1]=s;axes[2]=cross(u,s);
}
void partMotions(const DollPose &rest,const DollPose &now,std::array<PartMotion,DOLL_PARTS> &out) {
    for(int part=0;part<DOLL_PARTS;++part) {
        V3 o0,o1,b0[3],b1[3];
        frame(rest,(DollPart)part,o0,b0);frame(now,(DollPart)part,o1,b1);
        PartMotion &k=out[part];
        // m = B1 * B0^T: each rest axis goes to its axis now.
        for(int i=0;i<3;++i)for(int j=0;j<3;++j) {
            float sum=0;
            for(int a=0;a<3;++a) {
                const float b1i=i==0?b1[a].x:i==1?b1[a].y:b1[a].z,b0j=j==0?b0[a].x:j==1?b0[a].y:b0[a].z;
                sum+=b1i*b0j;
            }
            k.m[i][j]=sum;
        }
        k.t={0,0,0};
        V3 moved=carry(k,o0);
        k.t=o1-moved;
    }
}
}
