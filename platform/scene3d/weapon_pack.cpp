/* Weapon frames rendered from 3D models (PLAN-weapon-models.md): an optional
   pack that stands in for the relit weapon's guessed maps (weapon_lighting.h)
   with ones measured from a model, in the same layout relitWeaponArt builds.

   The pack is a folder: -weaponpack <dir>, else weapon-pack beside the
   program. pack.txt lists one frame a line: "<LUMP> <leftoffset> <topoffset>
   <hash>", the hash (FNV-1a, hex) of the WAD lump the frame was made for.
   <LUMP>.png is 2w+2 wide, RGBA: the left w columns hold the palette index,
   coverage and normal x/y, the gloss sits two columns past them in red. A
   frame stands in only while the lump in play is that same artwork, so a
   PWAD's own weapon always shows as its author drew it. */
#include "internal.h"

namespace doom3d {
namespace {
struct PackFrame { int left=0,top=0; unsigned hash=0; };
std::string packFolder;
std::map<std::string,PackFrame> packFrames;
std::map<int,std::unique_ptr<Image>> packImages; // by lump; null: no frame for it
bool packRead=false;

void readPack() {
    packRead=true;
    int arg=M_CheckParm((char*)"-weaponpack");
    if(arg&&arg+1<myargc) packFolder=myargv[arg+1];
    else {const char *base=SDL_GetBasePath();packFolder=std::string(base?base:"./")+"weapon-pack";}
    FILE *list=fopen((packFolder+"/pack.txt").c_str(),"r");
    if(!list) return;
    char name[16];int left,top;unsigned hash;
    while(fscanf(list,"%15s %d %d %x",name,&left,&top,&hash)==4) packFrames[name]=PackFrame{left,top,hash};
    fclose(list);
    printf("Weapon pack: %zu frames from %s\n",packFrames.size(),packFolder.c_str());
}
unsigned lumpHash(int lump) {
    const byte *data=(const byte*)W_CacheLumpNum(lump,PU_CACHE);
    unsigned hash=2166136261u;
    for(int i=0,n=W_LumpLength(lump);i<n;++i) hash=(hash^data[i])*16777619u;
    return hash;
}
std::unique_ptr<Image> loadFrame(int lump) {
    char name[9];memcpy(name,lumpinfo[lump].name,8);name[8]=0;
    auto found=packFrames.find(name);
    if(found==packFrames.end()||lumpHash(lump)!=found->second.hash) return nullptr;
    SDL_Surface *loaded=SDL_LoadPNG((packFolder+"/"+name+".png").c_str());
    SDL_Surface *surface=loaded?SDL_ConvertSurface(loaded,SDL_PIXELFORMAT_RGBA32):nullptr;
    SDL_DestroySurface(loaded);
    if(!surface||surface->w<4||((surface->w-2)&1)) {
        if(surface) SDL_DestroySurface(surface);
        printf("Weapon pack: %s.png is missing or not 2w+2 wide\n",name);
        return nullptr;
    }
    int stride=surface->w,w=(stride-2)/2,h=surface->h;
    std::vector<byte> packed((size_t)stride*h*4);
    auto image=std::make_unique<Image>();
    image->pixels.resize((size_t)w*h*2);
    for(int y=0;y<h;++y) {
        const byte *row=(const byte*)surface->pixels+(size_t)y*surface->pitch;
        memcpy(&packed[(size_t)y*stride*4],row,(size_t)stride*4);
        // Only red of the gloss column is the pack's: green is the shader's calm (cloth), none here.
        for(int x=w;x<stride;++x)packed[((size_t)y*stride+x)*4+1]=0;
        for(int x=0;x<w;++x) {image->pixels[((size_t)y*w+x)*2]=row[x*4];image->pixels[((size_t)y*w+x)*2+1]=row[x*4+1];}
    }
    SDL_DestroySurface(surface);
    image->opaque=false;image->width=w;image->height=h;
    image->left=found->second.left;image->top=found->second.top;
    image->texture=gpuCreateTexture(GpuFormat::RGBA8,stride,h,packed.data(),stride*4);
    if(!image->texture) I_Error((char*)"Could not allocate a GPU texture");
    return image;
}
} // namespace

Image *weaponPackImage(int lump) {
    if(!packRead) readPack();
    if(packFrames.empty()) return nullptr;
    auto found=packImages.find(lump);
    if(found==packImages.end()) found=packImages.emplace(lump,loadFrame(lump)).first;
    return found->second.get();
}
} // namespace doom3d
