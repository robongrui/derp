/* A room's traced tail as one voice. See far_tail.h. */
#include <stdlib.h>
#include <string.h>
#include "far_tail.h"

struct far_tail_s { conv_input_t *input; float in[CONV_BLOCK], out[CONV_BLOCK]; int at; };

far_tail_t *FarTail_New(float seconds, int rate)
{
    far_tail_t *t = calloc(1, sizeof(*t));
    if (!t) return NULL;
    t->input = Conv_NewInput((int)(seconds * rate) / CONV_BLOCK + 1);
    if (!t->input) { free(t); return NULL; }
    return t;
}

void FarTail_Clear(far_tail_t *t)
{
    if (!t) return;
    Conv_ClearInput(t->input);
    memset(t->in, 0, sizeof(t->in)); memset(t->out, 0, sizeof(t->out));
    t->at = 0;
}

void FarTail_Run(far_tail_t *t, const conv_slot_t *response, const float *in, float *out, int n)
{
    for (int k = 0; k < n; ++k)
    {
        /* A block behind: what the last full block gave. */
        t->in[t->at] = in[k];
        out[k] = t->out[t->at];
        if (++t->at < CONV_BLOCK) continue;
        t->at = 0;
        Conv_Push(t->input, t->in);
        if (!response) { memset(t->out, 0, sizeof(t->out)); continue; }
        float o[CONV_CHANNELS][CONV_BLOCK];
        Conv_Run(t->input, response, o);
        memcpy(t->out, o[0], sizeof(t->out));
    }
}
