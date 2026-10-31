/* The player's eyes (Game_LibraryEyes: the library's day, its knocks and its first questions):
   the lids closing over the view as he tires or drops asleep, and the dark when he is out
   (eyes.glsl, in the present pass). Drawing only. */
#include "internal.h"

namespace doom3d {
// view: the world view's place on the screen (uv: x, y, width, height).
void closedEyes(Uniforms &camera,const float view[4]) {
    float lids=0,black=0;
    if(!Game_LibraryEyes(&lids,&black)) {lids=black=0;}
    camera.eyes[0]=std::clamp(lids,0.0f,1.0f);camera.eyes[1]=std::clamp(black,0.0f,1.0f);
    camera.eyes[2]=view[1];camera.eyes[3]=view[3];
}
} // namespace doom3d
