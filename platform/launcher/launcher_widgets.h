/* The launcher's widgets (launcher_widgets.c), drawn and asked each frame:
 * buttons, checks, choices, sliders, list rows, a text field. Every widget
 * can take the focus, so the window works from the keyboard and a pad as
 * well as the mouse: arrows (or the d-pad) move the focus to the nearest
 * widget that way, Enter / A presses, left and right change a choice. */
#ifndef DERP_LAUNCHER_WIDGETS_H
#define DERP_LAUNCHER_WIDGETS_H
#include <SDL3/SDL.h>
#ifdef __cplusplus
extern "C" {
#endif

#define UI_WIDTH 1280
#define UI_HEIGHT 800
#define UI_MOST_WIDGETS 1024

enum { NAV_NONE, NAV_UP, NAV_DOWN, NAV_LEFT, NAV_RIGHT, NAV_NEXT, NAV_PREVIOUS };
enum { UI_DISABLED = 1, UI_ACCENT = 2, UI_SELECTED = 4, UI_WARN = 8, UI_GREY = 16, UI_SMALL = 32 };
/* Focus kinds: a value widget takes left and right for itself. */
enum { KIND_PRESS, KIND_VALUE, KIND_TEXT };

typedef struct {
    SDL_Renderer *renderer;
    SDL_Window *window;
    /* This frame's input, set by the event handling and cleared after the frame. */
    float mouse_x, mouse_y;
    int clicked;     /* left button went down at mouse_x, mouse_y */
    float wheel;     /* mouse wheel turned (notches; up, away from the player, is positive) */
    int mouse_held;  /* the left button is down (a scroll bar being dragged) */
    int page_nav;    /* Page Up / Down: -1 / 1, Home / End: -2 / 2, for the list holding the focus */
    int redraw;      /* a list scrolled after it was drawn: draw again at once */
    /* The lists' widget numbers this frame and the frame before (first, last). */
    int lists[8][2], list_count, last_lists[8][2], last_list_count;
    int activate;    /* Enter, Space, the pad's A */
    int step;        /* left / right on a value widget */
    int remove;      /* Delete, the pad's X */
    int nav;
    char typed[64];  /* text typed into the field being edited */
    int backspace;
    /* Focus: widgets are numbered in the order they are drawn. */
    int focus;       /* -1: the next UI_DefaultFocus takes it */
    int count;
    int focus_moved; /* the focus came to a widget by a move this frame */
    int editing;     /* the focused text field takes the keys */
    SDL_FRect rects[UI_MOST_WIDGETS];
    int kinds[UI_MOST_WIDGETS];
    SDL_FRect focus_rect;
    int focus_kind;
    /* Mouse clicks count only inside it while set (a scrolled area). */
    int clipped;
    SDL_FRect clip;
    const char *focus_tip; /* the focused widget's longer words, for the page to show */
} ui_state;

extern const SDL_Color UI_TEXT, UI_DIM, UI_FAINT, UI_ACCENT_COLOR, UI_WARN_COLOR, UI_GOOD;

void UI_BeginFrame(ui_state *ui);
/* Resolves a move of the focus among this frame's widgets and clears the input. */
void UI_EndFrame(ui_state *ui);
/* The next widget gets the focus if nothing has it. */
void UI_DefaultFocus(ui_state *ui);
int UI_Focused(const ui_state *ui, int id);
/* Reserves the next widget's number (for the caller to remember it). */
int UI_NextId(const ui_state *ui);

void UI_Fill(ui_state *ui, SDL_FRect rect, SDL_Color color);
void UI_Frame(ui_state *ui, SDL_FRect rect, SDL_Color color);
void UI_Label(ui_state *ui, float x, float y, const char *text, int scale, SDL_Color color);
void UI_Heading(ui_state *ui, float x, float y, const char *text);

int UI_Button(ui_state *ui, SDL_FRect rect, const char *label, int flags);
/* label at the left, the box at value_x; returns 1 when it flipped. */
int UI_Check(ui_state *ui, SDL_FRect rect, const char *label, int *value, float value_x, int flags);
/* A choice shown as < value >; returns -1 or +1 for a step, 0 for none. */
int UI_Choice(ui_state *ui, SDL_FRect rect, const char *label, const char *value, float value_x, int flags);
/* A slider at fraction (0-1) with its value's words; returns -1/+1 for a step,
   2 when clicked at *picked (0-1). */
int UI_Slider(ui_state *ui, SDL_FRect rect, const char *label, const char *value, float fraction,
              float value_x, int flags, float *picked);
/* A round choice among several. */
int UI_Radio(ui_state *ui, SDL_FRect rect, const char *label, const char *detail, int chosen, int flags);
/* A list row: 1 pressed, 2 the focus came to it by a move. */
int UI_Row(ui_state *ui, SDL_FRect rect, const char *text, const char *detail, int flags);
/* An editable line of text; returns 1 when it changed. */
int UI_TextField(ui_state *ui, SDL_FRect rect, char *text, int size, int flags);
/* A scrolled list of widgets (the Settings options, the games, the custom WADs).
   UI_ListBegin goes before its rows: it scrolls by the wheel over the list,
   the scroll bar, Page Up / Down and Home / End, and keeps a focus moved by
   keys in view, all before the rows are drawn, from where they were drawn
   the frame before. Rows are drawn at their place less scroll, clipped to
   the view by the caller. UI_ListEnd takes the content's height, keeps the
   scroll in range and draws the bar beside the view. */
typedef struct {
    float scroll, content;
    float drawn;         /* the scroll the rows were drawn at, the frame before */
    int first, last;     /* the rows' widget numbers, the frame before */
    int dragging;
    float grab;
} ui_list;
/* step: how far a wheel notch goes. owner: the list takes the page keys
   while the focus is on no list. */
void UI_ListBegin(ui_state *ui, ui_list *list, SDL_FRect view, float step, int owner);
void UI_ListEnd(ui_state *ui, ui_list *list, SDL_FRect view, float content);

/* The cost of an option as bars (0-3) at x, y. */
void UI_CostBars(ui_state *ui, float x, float y, int cost);

#ifdef __cplusplus
}
#endif
#endif
