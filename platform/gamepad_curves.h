/* How a gamepad's sticks answer (i_sdl_gamepad.c), kept apart so the
   tests can check it without a pad (tests/gamepad_curves.c). */
#ifndef DOOM_GAMEPAD_CURVES_H
#define DOOM_GAMEPAD_CURVES_H
/* A stick's axes (-1 to 1) through a round dead zone: the direction kept,
   the tilt measured from the zone's edge (0) to the rim (1). */
void Pad_Deadzone(float x, float y, float zone, float *out_x, float *out_y);
/* How fast a look stick turns at a tilt (0-1), as a share of full speed:
   slow and fine near the middle, full at the rim. */
float Pad_Curve(float tilt);
/* The turn boost: held at the rim for PAD_BOOST_DELAY seconds, turning
   speeds up over PAD_BOOST_RAMP to PAD_BOOST times, so a turn around is
   quick; any less tilt drops it at once. *held carries the time held. */
#define PAD_BOOST 1.75f
#define PAD_BOOST_DELAY 0.3f
#define PAD_BOOST_RAMP 0.25f
float Pad_Boost(float tilt, float seconds, float *held);
#endif
