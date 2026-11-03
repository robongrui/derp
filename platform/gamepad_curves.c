/* How a gamepad's sticks answer; see gamepad_curves.h. */
#include <math.h>
#include "gamepad_curves.h"

void Pad_Deadzone(float x, float y, float zone, float *out_x, float *out_y)
{
    float length = sqrtf(x * x + y * y);
    if (length <= zone || zone >= 1) { *out_x = *out_y = 0; return; }
    float tilt = (length - zone) / (1 - zone);
    if (tilt > 1) tilt = 1;
    *out_x = x / length * tilt;
    *out_y = y / length * tilt;
}

/* Square: half tilt turns at a quarter speed, aim stays fine. */
float Pad_Curve(float tilt) { return tilt * tilt; }

float Pad_Boost(float tilt, float seconds, float *held)
{
    if (tilt < 0.95f) { *held = 0; return 1; }
    *held += seconds;
    float t = (*held - PAD_BOOST_DELAY) / PAD_BOOST_RAMP;
    if (t <= 0) return 1;
    if (t > 1) t = 1;
    return 1 + (PAD_BOOST - 1) * t;
}
