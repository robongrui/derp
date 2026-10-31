/* His head hitting a floor's edge in the library's chasm (Game_LibraryKnocks):
 * the skull meeting the stone at falling speed, a dull heavy crunch, bone
 * splintering, a wet give, a few drops after. Noise only, no tones. Played
 * once a knock, in the middle of his head, over the ringing that follows. */
#ifndef DOOM_AUDIO_HEAD_KNOCK_H
#define DOOM_AUDIO_HEAD_KNOCK_H

/* Into an interleaved stereo buffer of count frames at rate, after the
   ringing, before the music; knocks counts the knocks so far, a new one
   starts the sound. */
void HeadKnock_Mix(float *buffer, int count, int rate, int knocks);
#endif
