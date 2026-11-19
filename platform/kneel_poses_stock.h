/* Kneeling legs, posed by hand in tests/kneel_pose.py, which writes this file. Per view (Doom's
   rotations 1-8 as the eye sees them: 6-8 are the drawings for 4-2 flipped, their right and left
   swapped, so a pose always names the body's own legs), per leg (the body's right, then its left):
   the thigh and then the shin, each as an angle from straight down (degrees, positive toward the
   picture's right) and a share of the drawing's own bone length (skeletons_stock.h's hip, knee and
   ankle), then 1 where the shin lies behind its own thigh. The hips stay where they were drawn, the legs' texels follow their bones, and the picture
   drops until its lowest texel is where the drawing's lowest was. front: the leg drawn over the other. */
#ifndef DOOM_KNEEL_POSES_STOCK_H
#define DOOM_KNEEL_POSES_STOCK_H

struct doom_kneel_leg { float thigh, thighShare, shin, shinShare; int shinBehind; };
struct doom_kneel_view { doom_kneel_leg leg[2]; int front; };
static const doom_kneel_view doom_kneel_views[8] = {
    {{{-6.8f, 0.989f, 180.0f, 0.371f, 1}, {141.3f, 0.189f, 2.9f, 0.958f, 0}}, 1}, // view 1
    {{{-10.8f, 1.000f, 119.5f, 0.754f, 1}, {-103.6f, 0.629f, 13.9f, 0.986f, 0}}, 1}, // view 2
    {{{-8.5f, 0.993f, 111.8f, 1.000f, 1}, {-98.5f, 0.993f, 16.7f, 0.999f, 0}}, 1}, // view 3
    {{{-1.2f, 0.982f, 119.5f, 0.754f, 0}, {-100.7f, 0.792f, 10.0f, 0.972f, 1}}, 0}, // view 4
    {{{6.8f, 0.989f, 180.0f, 0.371f, 0}, {-141.3f, 0.189f, -2.9f, 0.958f, 1}}, 0}, // view 5
    {{{10.8f, 1.000f, -119.5f, 0.754f, 0}, {103.6f, 0.629f, -13.9f, 0.986f, 1}}, 0}, // view 6
    {{{8.5f, 0.993f, -111.8f, 1.000f, 0}, {98.5f, 0.993f, -16.7f, 0.999f, 1}}, 0}, // view 7
    {{{1.2f, 0.982f, -119.5f, 0.754f, 1}, {100.7f, 0.792f, -10.0f, 0.972f, 0}}, 1}, // view 8
};

#endif
