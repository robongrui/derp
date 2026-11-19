"""Gore's skeletons per drawing (PLAN-skin-suit.md 4.5): look at what the
fitter found, and place joints by hand where it got them wrong.

The game writes every drawing's skeleton with -skeletondump <file> (one line
each: lump name, width, height, art hash, view yaw, fit, hand, then the 15
joints as u h pairs, units right of the origin and above the feet, then which
of the 11 bones the monster has). This reads that file and the WAD:

  sheet <wad> <dump> <out.png> [prefix]
      every drawing (or those whose name matches prefix, a regular expression) at 4x, its bones
      drawn over it, the fit under each: a contact sheet to judge the fits.
  edit <wad> <dump> [prefix]
      a window: one drawing at 10x with its joints; drag a joint to move it,
      n / p (or the arrow keys) for the next / previous drawing, s saves every
      drawing touched into platform/skeletons_stock.h (keyed by name, size and
      hash, so the game uses it instead of the fit), q quits.

Usage (Windows: py, never python):
  py -I tests/skeleton_tool.py sheet path/to/doom2.wad skel.txt imp.png TROO
"""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from death_lab import Picture, png, read_wad  # noqa: E402

JOINTS = ['pelvis', 'neck', 'head', 'shoulder R', 'shoulder L', 'elbow R', 'elbow L', 'hand R', 'hand L',
          'hip R', 'hip L', 'knee R', 'knee L', 'ankle R', 'ankle L']
# Beside each joint in the editor: P pelvis, N neck, Hd head; shoulders, elbows, hands (Ha), hips (Hi),
# knees, ankles, each R or L for the monster's own right or left (its right is on the screen's left when
# it faces you).
LABELS = ['P', 'N', 'Hd', 'RS', 'LS', 'RE', 'LE', 'RHa', 'LHa', 'RHi', 'LHi', 'RK', 'LK', 'RA', 'LA']
BONES = [(9, 10), (0, 1), (1, 2), (3, 5), (4, 6), (5, 7), (6, 8), (9, 11), (10, 12), (11, 13), (12, 14)]
# The body's right (side 0) in warm colours, its left in cool ones, the trunk white/yellow.
COLOURS = [(255, 255, 255), (255, 240, 80), (255, 200, 40), (255, 90, 60), (60, 160, 255), (255, 150, 60),
           (60, 220, 255), (220, 60, 60), (60, 90, 255), (255, 60, 200), (80, 255, 120)]
STOCK = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'platform', 'skeletons_stock.h')


class Entry:
    def __init__(self, line):
        f = line.split()
        self.name, self.width, self.height, self.hash = f[0], int(f[1]), int(f[2]), int(f[3])
        self.yaw, self.fit, self.hand = float(f[4]), float(f[5]), f[6] == '1'
        v = [float(x) for x in f[7:7 + 30]]
        self.joints = [[v[2 * j], v[2 * j + 1]] for j in range(15)]
        self.has = [x == '1' for x in f[37:48]] if len(f) >= 48 else [True] * 11
        self.touched = False


def read_dump(path, prefix=''):
    return [Entry(l) for l in open(path) if l.strip() and re.match(prefix, l)]


def palette_of(lumps):
    raw = lumps['PLAYPAL']
    return [(raw[3 * i], raw[3 * i + 1], raw[3 * i + 2]) for i in range(256)]


def line(img, w, h, x0, y0, x1, y1, colour):
    steps = int(max(abs(x1 - x0), abs(y1 - y0))) + 1
    for k in range(steps + 1):
        t = k / steps
        x, y = int(x0 + (x1 - x0) * t), int(y0 + (y1 - y0) * t)
        if 0 <= x < w and 0 <= y < h:
            img[y][x] = colour


def sheet(wad, dump, out, prefix=''):
    lumps, entries = read_wad(wad), read_dump(dump, prefix)
    pal = palette_of(lumps)
    scale, columns, pad = (4, 10, 6) if len(entries) > 10 else (8, len(entries), 10)
    cell = max(max(e.width, e.height) for e in entries) * scale + pad
    rows = (len(entries) + columns - 1) // columns
    W, H = columns * cell, rows * (cell + 6)
    img = [[(40, 40, 48)] * W for _ in range(H)]
    for n, e in enumerate(entries):
        pic = Picture(lumps[e.name])
        ox, oy = (n % columns) * cell, (n // columns) * (cell + 6)
        for r in range(pic.height):
            for c in range(pic.width):
                t = pic.texels[r][c]
                if t >= 0:
                    for dy in range(scale):
                        for dx in range(scale):
                            img[oy + r * scale + dy][ox + c * scale + dx] = pal[t]

        def at(j):
            u, hh = e.joints[j]
            return ox + (u + pic.left) * scale, oy + (pic.top - hh) * scale
        for b, (a, z) in enumerate(BONES):
            if e.has[b]:
                (x0, y0), (x1, y1) = at(a), at(z)
                line(img, W, H, x0, y0, x1, y1, COLOURS[b])
        # The fit as a bar under the drawing: full width 1.0, red under 0.8 (the game drops those).
        bar = int((cell - pad) * max(0.0, min(1.0, e.fit)))
        colour = (90, 220, 90) if e.fit >= 0.8 or e.hand else (230, 60, 60)
        for x in range(bar):
            for y in range(3):
                img[oy + cell - pad + 2 + y][ox + x] = colour
    open(out, 'wb').write(png(W, H, [[v for px in row for v in px] for row in img]))
    print(f'{len(entries)} drawings -> {out}')


# Lump names as C strings: id ran out of letters for the arch-vile's frames (VILE[1, VILE\1, VILE]1), so a
# name may hold any character but a quote, a backslash written doubled (a plain \1 is an escape in C).
NAME = r'((?:[^"\\]|\\.)+)'


def c_name(name):
    return name.replace('\\', '\\\\')


def un_c(text):
    return text.replace('\\\\', '\\')


def save(entries):
    text = open(STOCK, encoding='utf-8').read()
    start = text.index('doom_stock_skeleton_table[] = {')
    end = text.index('    {nullptr', start)
    body = text[start:end]
    kept = {}
    for m in re.finditer(r'    \{"' + NAME + r'".*?\}\},\n', body, re.S):
        kept[un_c(m.group(1))] = m.group(0)
    for e in entries:
        if not e.touched:
            continue
        joints = ', '.join('{%.2ff, %.2ff}' % (u, h) for u, h in e.joints)
        kept[e.name] = '    {"%s", %d, %d, %du, {%s}},\n' % (c_name(e.name), e.width, e.height, e.hash, joints)
    head = text[:start] + 'doom_stock_skeleton_table[] = {\n    // (tests/skeleton_tool.py writes entries here)\n'
    open(STOCK, 'w', encoding='utf-8', newline='\n').write(head + ''.join(kept[k] for k in sorted(kept)) + text[end:])
    print(f'saved {sum(e.touched for e in entries)} drawing(s) into {STOCK}')


def edit(wad, dump, prefix=''):
    import tkinter as tk
    lumps, entries = read_wad(wad), read_dump(dump, prefix)
    pal = palette_of(lumps)
    scale = 10
    root = tk.Tk()
    canvas = tk.Canvas(root, width=90 * scale, height=80 * scale, bg='#282830')
    canvas.pack()
    status = tk.Label(root, anchor='w', font=('Consolas', 11))
    status.pack(fill='x')
    state = {'n': 0, 'drag': None}

    def current():
        e = entries[state['n']]
        return e, Picture(lumps[e.name])

    def screen(e, pic, j):
        u, h = e.joints[j]
        return (u + pic.left) * scale + 40, (pic.top - h) * scale + 40

    def draw():
        e, pic = current()
        canvas.delete('all')
        for r in range(pic.height):
            for c in range(pic.width):
                t = pic.texels[r][c]
                if t >= 0:
                    x, y = c * scale + 40, r * scale + 40
                    canvas.create_rectangle(x, y, x + scale, y + scale, fill='#%02x%02x%02x' % pal[t], width=0)
        for b, (a, z) in enumerate(BONES):
            if e.has[b]:
                canvas.create_line(*screen(e, pic, a), *screen(e, pic, z), fill='#%02x%02x%02x' % COLOURS[b], width=3)
        for j in range(15):
            x, y = screen(e, pic, j)
            canvas.create_oval(x - 5, y - 5, x + 5, y + 5, outline='white', width=2)
            # Its short name beside it (R / L: the monster's own right and left), shadowed to read on any colour.
            for dx, dy, colour in ((1, 1, 'black'), (0, 0, 'white')):
                canvas.create_text(x + 8 + dx, y - 8 + dy, text=LABELS[j], fill=colour, anchor='sw',
                                   font=('Consolas', 11, 'bold'))
        status.config(text=f'{state["n"] + 1}/{len(entries)}  {e.name}  yaw {e.yaw:.0f}  fit {e.fit:.2f}'
                           f'{"  hand" if e.hand else ""}{"  (moved)" if e.touched else ""}   drag joints, n/p, s save, q quit')

    def press(ev):
        e, pic = current()
        best, near = None, 14
        for j in range(15):
            x, y = screen(e, pic, j)
            d = ((x - ev.x) ** 2 + (y - ev.y) ** 2) ** 0.5
            if d < near:
                best, near = j, d
        state['drag'] = best
        if best is not None:
            status.config(text=f'{JOINTS[best]}')

    def move(ev):
        j = state['drag']
        if j is None:
            return
        e, pic = current()
        e.joints[j] = [round((ev.x - 40) / scale - pic.left, 2), round(pic.top - (ev.y - 40) / scale, 2)]
        e.touched = True
        draw()

    def key(ev):
        k = ev.keysym.lower()
        if k in ('n', 'right'):
            state['n'] = (state['n'] + 1) % len(entries)
        elif k in ('p', 'left'):
            state['n'] = (state['n'] - 1) % len(entries)
        elif k == 's':
            save(entries)
        elif k == 'q':
            root.destroy()
            return
        draw()

    canvas.bind('<ButtonPress-1>', press)
    canvas.bind('<B1-Motion>', move)
    canvas.bind('<ButtonRelease-1>', lambda ev: state.update(drag=None))
    root.bind('<Key>', key)
    root.title('Gore skeletons')
    draw()
    root.mainloop()


def main():
    if len(sys.argv) < 4:
        raise SystemExit(__doc__)
    mode = sys.argv[1]
    if mode == 'sheet' and len(sys.argv) >= 5:
        sheet(sys.argv[2], sys.argv[3], sys.argv[4], sys.argv[5] if len(sys.argv) > 5 else '')
    elif mode == 'edit':
        edit(sys.argv[2], sys.argv[3], sys.argv[4] if len(sys.argv) > 4 else '')
    else:
        raise SystemExit(__doc__)


if __name__ == '__main__':
    main()
