/* A scrolled list's arithmetic (scroll_list.h). */
#include "scroll_list.h"

float Scroll_Clamp(float scroll, float content, float view)
{
    float most = content > view ? content - view : 0;
    return scroll < 0 ? 0 : scroll > most ? most : scroll;
}

float Scroll_Reveal(float scroll, float top, float height, float view)
{
    if (top < scroll) return top;
    if (top + height > scroll + view) return height > view ? top : top + height - view;
    return scroll;
}

void Scroll_Knob(float scroll, float content, float view, float *offset, float *size)
{
    if (content <= view || view <= 0) { *offset = 0; *size = view; return; }
    *size = view * view / content;
    if (*size < SCROLL_KNOB_LEAST) *size = SCROLL_KNOB_LEAST;
    if (*size > view) *size = view;
    /* The knob's travel is the track less its own length, so the end of the list is the end of the track. */
    *offset = Scroll_Clamp(scroll, content, view) / (content - view) * (view - *size);
}

float Scroll_FromKnob(float offset, float content, float view)
{
    float at, size;
    if (content <= view) return 0;
    Scroll_Knob(0, content, view, &at, &size);
    if (view - size <= 0) return 0;
    return Scroll_Clamp(offset / (view - size) * (content - view), content, view);
}

int Scroll_PageTarget(const float *tops, int count, int current, float view, int direction)
{
    if (count <= 0) return -1;
    if (direction <= -2) return 0;
    if (direction >= 2) return count - 1;
    if (current < 0 || current >= count) return direction < 0 ? 0 : count - 1;
    /* A page on: the farthest row still within a view's height of this one, at least one row. */
    int target = current;
    for (int i = current + direction; i >= 0 && i < count; i += direction)
    {
        float distance = direction > 0 ? tops[i] - tops[current] : tops[current] - tops[i];
        if (distance > view && target != current) break;
        target = i;
        if (distance > view) break;
    }
    return target;
}
