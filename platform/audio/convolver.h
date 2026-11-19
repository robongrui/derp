/* Uniformly partitioned convolution (overlap-save) for the traced room
 * tails (room_tails.h): the input's last blocks kept as spectra, shared by
 * every response it plays through; each response cut into blocks, kept as
 * spectra too. A block of output costs one FFT in, two back and a multiply
 * per part per channel. */
#ifndef DOOM_AUDIO_CONVOLVER_H
#define DOOM_AUDIO_CONVOLVER_H

#define CONV_BLOCK 128   /* samples a block, at the tail's rate */
#define CONV_CHANNELS 3  /* at most: a tail's w, x and y (room_ir.h) */

typedef struct conv_input_s conv_input_t;
typedef struct conv_slot_s conv_slot_t;

/* History for responses up to parts blocks long. */
conv_input_t *Conv_NewInput(int parts);
void Conv_FreeInput(conv_input_t *in);
void Conv_ClearInput(conv_input_t *in);
/* The next CONV_BLOCK samples in. */
void Conv_Push(conv_input_t *in, const float *block);

/* A response of up to parts blocks in channels channels (1 to CONV_CHANNELS). */
conv_slot_t *Conv_NewSlot(int parts, int channels);
void Conv_FreeSlot(conv_slot_t *slot);
/* A response into a slot: its channels, length samples each, times scale.
   Its first CONV_BLOCK samples are left out: the block the input takes to
   fill delays the rest by that much. Any thread, while no one runs it. */
void Conv_Load(conv_slot_t *slot, const short *const channel[CONV_CHANNELS], int length, float scale);
/* The output of the block just pushed, through slot: out[c][CONV_BLOCK], c
   below the slot's channels. */
void Conv_Run(const conv_input_t *in, const conv_slot_t *slot, float out[CONV_CHANNELS][CONV_BLOCK]);
#endif
