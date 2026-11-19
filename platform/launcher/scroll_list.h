/* The arithmetic of a scrolled list (scroll_list.c), for the Settings
 * options and the games list. Positions are in the list's own space: 0 is
 * its top, scroll how far down the view shows it, content its whole height. */
#ifndef DERP_LAUNCHER_SCROLL_LIST_H
#define DERP_LAUNCHER_SCROLL_LIST_H
#ifdef __cplusplus
extern "C" {
#endif

#define SCROLL_KNOB_LEAST 24.0f

/* Never above the top, nor further than shows the last row at the bottom. */
float Scroll_Clamp(float scroll, float content, float view);
/* The scroll that shows a row from top to top + height, moving as little as it can. */
float Scroll_Reveal(float scroll, float top, float height, float view);
/* The scroll bar's knob: where it starts in the track and how long it is. */
void Scroll_Knob(float scroll, float content, float view, float *offset, float *size);
/* The scroll for a knob dragged to start at offset in the track. */
float Scroll_FromKnob(float offset, float content, float view);
/* Page Up / Down (direction -1 / 1), Home / End (-2 / 2): the row to go to,
   from the rows' tops (in order), the row now at current and the view's height. */
int Scroll_PageTarget(const float *tops, int count, int current, float view, int direction);

#ifdef __cplusplus
}
#endif
#endif
