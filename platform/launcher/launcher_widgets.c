/* The launcher's widgets (launcher_widgets.h). */
#include <string.h>
#include "launcher_widgets.h"
#include "launcher_font.h"
#include "scroll_list.h"

const SDL_Color UI_TEXT = { 222, 218, 208, 255 };
const SDL_Color UI_DIM = { 150, 146, 138, 255 };
const SDL_Color UI_FAINT = { 92, 90, 86, 255 };
const SDL_Color UI_ACCENT_COLOR = { 206, 64, 40, 255 };
const SDL_Color UI_WARN_COLOR = { 232, 170, 60, 255 };
const SDL_Color UI_GOOD = { 110, 190, 90, 255 };
static const SDL_Color FIELD = { 34, 33, 31, 255 }, FIELD_FOCUS = { 58, 52, 46, 255 };
static const SDL_Color FOCUS_EDGE = { 240, 200, 120, 255 };

#define TEXT_SCALE 2
#define LINE (FONT_CELL * TEXT_SCALE)

static int inside(SDL_FRect r, float x, float y)
{
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

void UI_BeginFrame(ui_state *ui)
{
    ui->count = 0;
    ui->focus_tip = NULL;
    ui->focus_kind = KIND_PRESS;
    ui->clipped = 0;
}

void UI_DefaultFocus(ui_state *ui) { if (ui->focus < 0) ui->focus = ui->count; }
int UI_Focused(const ui_state *ui, int id) { return ui->focus == id; }
int UI_NextId(const ui_state *ui) { return ui->count; }

typedef struct { int id, focused, hit; } widget;

static widget take(ui_state *ui, SDL_FRect rect, int kind)
{
    widget w = { ui->count, 0, 0 };
    if (ui->count < UI_MOST_WIDGETS)
    {
        ui->rects[ui->count] = rect;
        ui->kinds[ui->count] = kind;
    }
    ++ui->count;
    if (ui->clicked && inside(rect, ui->mouse_x, ui->mouse_y) && (!ui->clipped || inside(ui->clip, ui->mouse_x, ui->mouse_y)))
    {
        w.hit = 1;
        ui->clicked = 0;
        if (ui->focus != w.id) ui->editing = 0;
        ui->focus = w.id;
    }
    w.focused = ui->focus == w.id;
    if (w.focused) { ui->focus_rect = rect; ui->focus_kind = kind; }
    return w;
}

static float distance_in(int nav, SDL_FRect from, SDL_FRect to)
{
    float fx = from.x + from.w / 2, fy = from.y + from.h / 2, tx = to.x + to.w / 2, ty = to.y + to.h / 2;
    float along, across;
    switch (nav)
    {
    case NAV_UP: along = from.y - (to.y + to.h); across = SDL_fabsf(tx - fx); if (ty >= fy) return -1; break;
    case NAV_DOWN: along = to.y - (from.y + from.h); across = SDL_fabsf(tx - fx); if (ty <= fy) return -1; break;
    case NAV_LEFT: along = from.x - (to.x + to.w); across = SDL_fabsf(ty - fy); if (tx >= fx) return -1; break;
    default: along = to.x - (from.x + from.w); across = SDL_fabsf(ty - fy); if (tx <= fx) return -1; break;
    }
    if (along < 0) along = 0;
    /* Left and right stay on the row when there is anything on it. */
    if ((nav == NAV_LEFT || nav == NAV_RIGHT) && across > from.h / 2 + to.h / 2) across += 4000;
    return along + across * 3;
}

void UI_EndFrame(ui_state *ui)
{
    int count = ui->count < UI_MOST_WIDGETS ? ui->count : UI_MOST_WIDGETS;
    ui->focus_moved = 0;
    if (ui->focus >= count) ui->focus = count - 1;
    if (ui->nav && count)
    {
        int target = -1;
        if (ui->focus < 0) target = 0;
        else if (ui->nav == NAV_NEXT) target = (ui->focus + 1) % count;
        else if (ui->nav == NAV_PREVIOUS) target = (ui->focus + count - 1) % count;
        else
        {
            float best = 1e30f;
            for (int i = 0; i < count; ++i)
            {
                if (i == ui->focus) continue;
                float d = distance_in(ui->nav, ui->rects[ui->focus], ui->rects[i]);
                if (d >= 0 && d < best) { best = d; target = i; }
            }
        }
        if (target >= 0 && target != ui->focus)
        {
            ui->focus = target;
            ui->focus_moved = 1;
            ui->editing = 0;
        }
    }
    if (ui->editing && (ui->focus < 0 || ui->focus >= count || ui->kinds[ui->focus] != KIND_TEXT)) ui->editing = 0;
    if (!ui->editing && ui->window && SDL_TextInputActive(ui->window)) SDL_StopTextInput(ui->window);
    ui->wheel = 0;
    ui->page_nav = 0;
    memcpy(ui->last_lists, ui->lists, sizeof(ui->lists));
    ui->last_list_count = ui->list_count;
    ui->list_count = 0;
    ui->clicked = ui->activate = ui->step = ui->remove = ui->nav = ui->backspace = 0;
    ui->typed[0] = 0;
}

void UI_Fill(ui_state *ui, SDL_FRect rect, SDL_Color color)
{
    SDL_SetRenderDrawColor(ui->renderer, color.r, color.g, color.b, color.a);
    SDL_RenderFillRect(ui->renderer, &rect);
}

void UI_Frame(ui_state *ui, SDL_FRect rect, SDL_Color color)
{
    SDL_SetRenderDrawColor(ui->renderer, color.r, color.g, color.b, color.a);
    for (int i = 0; i < 2; ++i)
    {
        SDL_FRect edge = { rect.x + i, rect.y + i, rect.w - 2 * i, rect.h - 2 * i };
        SDL_RenderRect(ui->renderer, &edge);
    }
}

void UI_Label(ui_state *ui, float x, float y, const char *text, int scale, SDL_Color color)
{
    Font_Draw(ui->renderer, x, y, text, scale, color);
}

void UI_Heading(ui_state *ui, float x, float y, const char *text)
{
    Font_Draw(ui->renderer, x, y, text, TEXT_SCALE, UI_ACCENT_COLOR);
}

static void back(ui_state *ui, SDL_FRect rect, widget w)
{
    UI_Fill(ui, rect, w.focused ? FIELD_FOCUS : FIELD);
    if (w.focused) UI_Frame(ui, rect, FOCUS_EDGE);
}

static SDL_Color text_color(int flags)
{
    if (flags & (UI_DISABLED | UI_GREY)) return UI_FAINT;
    if (flags & UI_WARN) return UI_WARN_COLOR;
    return UI_TEXT;
}

static float middle(SDL_FRect rect, int scale) { return rect.y + (rect.h - FONT_CELL * scale) / 2; }

int UI_Button(ui_state *ui, SDL_FRect rect, const char *label, int flags)
{
    widget w = take(ui, rect, KIND_PRESS);
    int scale = flags & UI_SMALL ? 1 : TEXT_SCALE;
    if (flags & UI_ACCENT)
    {
        SDL_Color fill = UI_ACCENT_COLOR;
        if (w.focused) { fill.r = 236; fill.g = 92; fill.b = 60; }
        UI_Fill(ui, rect, fill);
        if (w.focused) UI_Frame(ui, rect, FOCUS_EDGE);
    }
    else back(ui, rect, w);
    float width = Font_Width(label, scale);
    Font_Draw(ui->renderer, rect.x + (rect.w - width) / 2, middle(rect, scale), label, scale,
              flags & UI_ACCENT ? (SDL_Color){ 255, 255, 255, 255 } : text_color(flags));
    if (flags & UI_DISABLED) return 0;
    return w.hit || (w.focused && ui->activate);
}

static void draw_label(ui_state *ui, SDL_FRect rect, const char *label, float value_x, int flags)
{
    Font_DrawFitted(ui->renderer, rect.x + 10, middle(rect, TEXT_SCALE), label, TEXT_SCALE, text_color(flags),
                    value_x - rect.x - 16, 0);
}

int UI_Check(ui_state *ui, SDL_FRect rect, const char *label, int *value, float value_x, int flags)
{
    widget w = take(ui, rect, KIND_VALUE);
    back(ui, rect, w);
    draw_label(ui, rect, label, value_x, flags);
    SDL_FRect box = { value_x, rect.y + (rect.h - 18) / 2, 18, 18 };
    UI_Frame(ui, box, text_color(flags));
    if (*value) { SDL_FRect mark = { box.x + 5, box.y + 5, 8, 8 }; UI_Fill(ui, mark, (flags & UI_DISABLED) ? UI_FAINT : UI_ACCENT_COLOR); }
    Font_Draw(ui->renderer, box.x + 28, middle(rect, TEXT_SCALE), *value ? "On" : "Off", TEXT_SCALE, text_color(flags) );
    if (flags & UI_DISABLED) return 0;
    if (w.hit || (w.focused && (ui->activate || ui->step))) { *value = !*value; return 1; }
    return 0;
}

int UI_Choice(ui_state *ui, SDL_FRect rect, const char *label, const char *value, float value_x, int flags)
{
    widget w = take(ui, rect, KIND_VALUE);
    back(ui, rect, w);
    draw_label(ui, rect, label, value_x, flags);
    SDL_Color color = text_color(flags);
    float y = middle(rect, TEXT_SCALE), right = rect.x + rect.w - 10;
    Font_Draw(ui->renderer, value_x, y, "<", TEXT_SCALE, w.focused ? FOCUS_EDGE : UI_DIM);
    Font_Draw(ui->renderer, right - LINE, y, ">", TEXT_SCALE, w.focused ? FOCUS_EDGE : UI_DIM);
    Font_DrawFitted(ui->renderer, value_x + LINE + 8, y, value, TEXT_SCALE, color, right - value_x - 2 * LINE - 16, 0);
    if (flags & UI_DISABLED) return 0;
    if (w.hit) return ui->mouse_x < value_x + (right - value_x) / 3 ? -1 : 1;
    if (w.focused && ui->step) return ui->step;
    if (w.focused && ui->activate) return 1;
    return 0;
}

int UI_Slider(ui_state *ui, SDL_FRect rect, const char *label, const char *value, float fraction,
              float value_x, int flags, float *picked)
{
    widget w = take(ui, rect, KIND_VALUE);
    back(ui, rect, w);
    draw_label(ui, rect, label, value_x, flags);
    float right = rect.x + rect.w - 10, words = 12 * FONT_CELL * TEXT_SCALE;
    SDL_FRect track = { value_x, rect.y + rect.h / 2 - 3, right - value_x - words - 12, 6 };
    UI_Fill(ui, track, UI_FAINT);
    SDL_FRect filled = track;
    filled.w = track.w * SDL_clamp(fraction, 0.0f, 1.0f);
    UI_Fill(ui, filled, (flags & UI_DISABLED) ? UI_DIM : UI_ACCENT_COLOR);
    SDL_FRect knob = { track.x + filled.w - 4, rect.y + rect.h / 2 - 9, 8, 18 };
    UI_Fill(ui, knob, w.focused ? FOCUS_EDGE : UI_TEXT);
    Font_DrawFitted(ui->renderer, right - words, middle(rect, TEXT_SCALE), value, TEXT_SCALE, text_color(flags), words, 0);
    if (flags & UI_DISABLED) return 0;
    if (w.hit && ui->mouse_x >= track.x - 8 && ui->mouse_x <= track.x + track.w + 8)
    {
        *picked = SDL_clamp((ui->mouse_x - track.x) / track.w, 0.0f, 1.0f);
        return 2;
    }
    if (w.focused && ui->step) return ui->step;
    return 0;
}

int UI_Radio(ui_state *ui, SDL_FRect rect, const char *label, const char *detail, int chosen, int flags)
{
    widget w = take(ui, rect, KIND_PRESS);
    back(ui, rect, w);
    SDL_Color color = text_color(flags);
    float y = middle(rect, TEXT_SCALE);
    Font_Draw(ui->renderer, rect.x + 10, y, chosen ? "(o)" : "( )", TEXT_SCALE, chosen ? UI_ACCENT_COLOR : color);
    float x = rect.x + 10 + 4 * LINE;
    x += Font_Draw(ui->renderer, x, y, label, TEXT_SCALE, color) + LINE;
    if (detail) Font_DrawFitted(ui->renderer, x, y, detail, TEXT_SCALE, UI_DIM, rect.x + rect.w - x - 10, 1);
    if (flags & UI_DISABLED) return 0;
    return w.hit || (w.focused && ui->activate);
}

int UI_Row(ui_state *ui, SDL_FRect rect, const char *text, const char *detail, int flags)
{
    widget w = take(ui, rect, KIND_PRESS);
    if (w.focused || (flags & UI_SELECTED)) back(ui, rect, w);
    if (flags & UI_SELECTED)
    {
        SDL_FRect bar = { rect.x, rect.y, 4, rect.h };
        UI_Fill(ui, bar, UI_ACCENT_COLOR);
    }
    SDL_Color color = text_color(flags);
    if (detail && *detail)
    {
        Font_DrawFitted(ui->renderer, rect.x + 12, rect.y + 6, text, TEXT_SCALE, color, rect.w - 20, 0);
        Font_DrawFitted(ui->renderer, rect.x + 12, rect.y + 8 + LINE, detail, 1, (flags & UI_WARN) ? UI_WARN_COLOR : UI_DIM, rect.w - 20, 1);
    }
    else Font_DrawFitted(ui->renderer, rect.x + 12, middle(rect, TEXT_SCALE), text, TEXT_SCALE, color, rect.w - 20, 0);
    if (w.hit || (w.focused && ui->activate)) return 1;
    if (w.focused && ui->focus_moved) { ui->focus_moved = 0; return 2; }
    return 0;
}

int UI_TextField(ui_state *ui, SDL_FRect rect, char *text, int size, int flags)
{
    widget w = take(ui, rect, KIND_TEXT);
    int changed = 0;
    if (w.hit || (w.focused && ui->activate && !ui->editing))
    {
        ui->editing = 1;
        ui->activate = 0;
        if (ui->window)
        {
            SDL_Rect area = { (int)rect.x, (int)rect.y, (int)rect.w, (int)rect.h };
            SDL_SetTextInputArea(ui->window, &area, 0);
            SDL_StartTextInput(ui->window);
        }
    }
    else if (w.focused && ui->editing)
    {
        if (ui->activate) { ui->editing = 0; ui->activate = 0; }
        if (ui->typed[0] && (int)(strlen(text) + strlen(ui->typed)) < size)
        {
            SDL_strlcat(text, ui->typed, (size_t)size);
            changed = 1;
        }
        for (int i = 0; i < ui->backspace; ++i)
        {
            size_t length = strlen(text);
            if (!length) break;
            text[length - 1] = 0;
            changed = 1;
        }
    }
    back(ui, rect, w);
    int editing = w.focused && ui->editing;
    float y = middle(rect, TEXT_SCALE);
    float width = Font_DrawFitted(ui->renderer, rect.x + 10, y, text[0] || editing ? text : "(none)", TEXT_SCALE,
                                  text[0] ? text_color(flags) : UI_FAINT, rect.w - 20 - LINE, 1);
    if (editing && (SDL_GetTicks() / 500) % 2 == 0)
    {
        SDL_FRect caret = { rect.x + 10 + width + 2, y, 3, LINE };
        UI_Fill(ui, caret, FOCUS_EDGE);
    }
    return changed;
}

/* The scroll bar's track beside a list. */
static SDL_FRect track_of(SDL_FRect view) { return (SDL_FRect){ view.x + view.w + 6, view.y, 6, view.h }; }

static int fully_shown(SDL_FRect row, SDL_FRect view)
{
    return row.y >= view.y - 0.5f && row.y + row.h <= view.y + view.h + 0.5f;
}

void UI_ListBegin(ui_state *ui, ui_list *list, SDL_FRect view, float step, int owner)
{
    int count = list->last - list->first;
    int focus_in = ui->focus >= list->first && ui->focus < list->last && list->last <= UI_MOST_WIDGETS;
    float drawn = list->drawn; /* where last frame's rects were drawn from */
    /* The scroll bar: a click on the knob grabs it, elsewhere on the track jumps there. */
    SDL_FRect track = track_of(view), band = { track.x - 6, track.y, track.w + 14, track.h };
    if (list->content > view.h && ui->clicked && inside(band, ui->mouse_x, ui->mouse_y))
    {
        float offset, size;
        Scroll_Knob(list->scroll, list->content, view.h, &offset, &size);
        float at = ui->mouse_y - view.y;
        list->grab = at >= offset && at <= offset + size ? at - offset : size / 2;
        list->dragging = 1;
        ui->clicked = 0;
    }
    if (list->dragging)
    {
        if (!ui->mouse_held) list->dragging = 0;
        else list->scroll = Scroll_FromKnob(ui->mouse_y - view.y - list->grab, list->content, view.h);
    }
    /* The wheel, over the list or its bar. */
    SDL_FRect area = { view.x, view.y, band.x + band.w - view.x, view.h };
    if (ui->wheel != 0 && inside(area, ui->mouse_x, ui->mouse_y))
    {
        list->scroll -= ui->wheel * step;
        ui->wheel = 0;
    }
    list->scroll = Scroll_Clamp(list->scroll, list->content, view.h);
    /* Up or down from a row scrolled out of sight starts at the rows in sight. */
    if (focus_in && (ui->nav == NAV_UP || ui->nav == NAV_DOWN))
    {
        SDL_FRect row = ui->rects[ui->focus];
        row.y += drawn - list->scroll;
        if (!fully_shown(row, view))
        {
            int pick = -1;
            for (int i = list->first; i < list->last; ++i)
            {
                SDL_FRect r = ui->rects[i];
                r.y += drawn - list->scroll;
                if (!fully_shown(r, view)) continue;
                if (ui->nav == NAV_DOWN && pick < 0) pick = i;
                if (ui->nav == NAV_UP) pick = i;
            }
            if (pick >= 0) { ui->focus = pick; ui->nav = NAV_NONE; ui->focus_moved = 1; }
        }
    }
    /* Page Up / Down, Home / End: the list with the focus, else the page's own list. */
    int focus_elsewhere = 0;
    for (int i = 0; i < ui->last_list_count; ++i)
        if (ui->last_lists[i][0] != list->first && ui->focus >= ui->last_lists[i][0] && ui->focus < ui->last_lists[i][1])
            focus_elsewhere = 1;
    if (ui->page_nav && count > 0 && (focus_in || (owner && !focus_elsewhere)))
    {
        static float tops[UI_MOST_WIDGETS];
        int n = count < UI_MOST_WIDGETS ? count : UI_MOST_WIDGETS;
        for (int i = 0; i < n; ++i) tops[i] = ui->rects[list->first + i].y - view.y + drawn;
        int current = focus_in ? ui->focus - list->first : -1;
        int target = Scroll_PageTarget(tops, n, current, view.h - step, ui->page_nav);
        if (target >= 0) { ui->focus = list->first + target; ui->focus_moved = 1; ui->editing = 0; focus_in = 1; }
        /* Home and End go to the list's very ends, headings and all. */
        if (ui->page_nav <= -2) list->scroll = 0;
        if (ui->page_nav >= 2) list->scroll = list->content;
        ui->page_nav = 0;
    }
    /* A focus the keys or the pad moved here comes into view. */
    if (focus_in && ui->focus_moved)
    {
        SDL_FRect row = ui->rects[ui->focus];
        float top = row.y - view.y + drawn, margin = 6;
        list->scroll = Scroll_Reveal(list->scroll, top - margin, row.h + 2 * margin, view.h);
        list->scroll = Scroll_Clamp(list->scroll, list->content, view.h);
    }
    list->first = UI_NextId(ui);
    list->drawn = list->scroll;
}

void UI_ListEnd(ui_state *ui, ui_list *list, SDL_FRect view, float content)
{
    list->last = UI_NextId(ui);
    list->content = content;
    if (ui->list_count < 8)
    {
        ui->lists[ui->list_count][0] = list->first;
        ui->lists[ui->list_count][1] = list->last;
        ++ui->list_count;
    }
    float clamped = Scroll_Clamp(list->scroll, content, view.h);
    /* The content shrank under the scroll (Display only, a row removed): drawn again in range. */
    if (clamped != list->scroll) { list->scroll = clamped; ui->redraw = 1; }
    if (content <= view.h) { list->dragging = 0; return; }
    SDL_FRect track = track_of(view);
    UI_Fill(ui, track, (SDL_Color){ 40, 39, 37, 255 });
    float offset, size;
    Scroll_Knob(list->scroll, content, view.h, &offset, &size);
    SDL_FRect knob = { track.x, track.y + offset, track.w, size };
    UI_Fill(ui, knob, list->dragging ? FOCUS_EDGE : UI_DIM);
}

void UI_CostBars(ui_state *ui, float x, float y, int cost)
{
    static const SDL_Color colors[3] = { { 110, 190, 90, 255 }, { 220, 190, 60, 255 }, { 220, 70, 50, 255 } };
    for (int i = 0; i < 3; ++i)
    {
        SDL_FRect bar = { x + i * 9, y, 6, 14 };
        UI_Fill(ui, bar, i < cost ? colors[cost - 1] : (SDL_Color){ 52, 50, 48, 255 });
    }
}
