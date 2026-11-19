"""Kneel poser: pose a kneeling soldier's legs by hand, view by view, on id's own drawings,
and see the pose on every frame at once. The drawings' joints are the hand-placed skeletons
(platform/skeletons_stock.h); the poses go to platform/kneel_poses_stock.h, which the game
reads (kneeling soldiers, the renderer's kneel in place of today's leg squash).

  edit <wad> [--ref picture.bmp] [sprites...]
      A window. Left: one frame large, posed, its standing drawing faint beside it, and the
      reference drawing if given. Drag a knee or an ankle (circles): the thigh or shin turns
      and stretches, for this view and every frame in it. Right: every frame of each sprite
      (POSS SPOS by default) in this view: standing, today's squash, posed; click one to show it
      large. Everything is on the buttons over them; the keys, for those who like them:
      1-8 (top row or numpad): view (Doom's rotations: 1 facing you, 5 its back, 6-8 the flipped 4-2)
      left / right: frame shown large     up / down or m: sprite     f: swap which leg is drawn in front
      z: this view back to its seed     c: copy this view mirrored into its twin (2<->8, 3<->7, 4<->6)
      s: save     q: quit (unsaved changes are lost)
  sheet <wad> <out.png> [sprites...]
      Every view and frame posed, as a contact sheet.

Usage (Windows: py, never python):
  py -I tests/kneel_pose.py edit path/to/doom.wad --ref path/to/reference.bmp
"""
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from death_lab import png, read_wad  # noqa: E402
from kneel_pose_lib import (ANKLE, HIP, KNEE, Binding, Drawing, angle_of, palette_of, pose_drawing,  # noqa: E402
                            read_bmp, read_poses, read_skeletons, seed_poses, squashed, write_poses)

FRAMES = 'ABCDEFG'
TWIN = {2: 8, 8: 2, 3: 7, 7: 3, 4: 6, 6: 4}
LEG_COLOURS = ['#ff5a3c', '#3ca0ff']     # body's right warm, left cool (as skeleton_tool.py)
LEFT_BG, RIGHT_BG = '#282830', '#202028'


class Cache:
    """Drawings and their leg bindings, made once each."""
    def __init__(self, lumps, skeletons):
        self.lumps, self.skeletons, self.made = lumps, skeletons, {}

    def get(self, sprite, frame, view):
        key = (sprite, frame, view)
        if key not in self.made:
            try:
                self.made[key] = Binding(Drawing(self.lumps, self.skeletons, sprite, frame, view))
            except KeyError:
                self.made[key] = None
        return self.made[key]


def sheet(wad, out, sprites):
    lumps = read_wad(wad)
    pal, cache, poses = palette_of(lumps), Cache(lumps, read_skeletons()), read_poses()
    scale, cell, rowh = 3, 64, 70
    cols = len(FRAMES) * len(sprites)
    W, H = cols * cell * scale, 8 * 2 * rowh * scale
    img = [[(40, 40, 48)] * W for _ in range(H)]

    def put(texels, top, x0, y0):
        for r, row in enumerate(texels):
            for c, t in enumerate(row):
                if t >= 0:
                    y, x = (y0 + rowh - 4 - top + r) * scale, (x0 + c) * scale
                    if 0 <= y < H - scale and 0 <= x < W - scale:
                        for a in range(scale):
                            img[y + a][x:x + scale] = [pal[t]] * scale
    for v in range(1, 9):
        for n, (sprite, frame) in enumerate((s, f) for s in sprites for f in FRAMES):
            b = cache.get(sprite, frame, v)
            if not b:
                continue
            x0 = n * cell
            put(b.d.texels, b.d.top, x0 + 16 - b.d.left + 16, (v - 1) * 2 * rowh)
            texels, joints, drop, left = pose_drawing(b, poses[v])
            put(texels, b.d.top, x0 + 32 - left, (v - 1) * 2 * rowh + rowh)
    open(out, 'wb').write(png(W, H, [[c for px in row for c in px] for row in img]))
    print(f'{out}: views 1-8, each standing over posed')


def edit(wad, sprites, ref):
    import tkinter as tk
    lumps = read_wad(wad)
    pal, cache = palette_of(lumps), Cache(lumps, read_skeletons())
    poses, seeds = read_poses(), seed_poses()
    reference = read_bmp(ref) if ref else None
    big, small = 9, 2
    root = tk.Tk()
    root.title('Kneel poser')
    bar = tk.Frame(root)
    bar.pack(fill='x', pady=2)
    row = tk.Frame(root)
    row.pack()
    left_c = tk.Canvas(row, width=96 * big, height=74 * big, bg=LEFT_BG, highlightthickness=0)
    left_c.pack(side='left')
    right_c = tk.Canvas(row, width=len(FRAMES) * 50 * small + 20, height=len(sprites) * 3 * 64 * small,
                        bg=RIGHT_BG, highlightthickness=0)
    right_c.pack(side='left')
    status = tk.Label(root, anchor='w', font=('Consolas', 11))
    status.pack(fill='x')
    st = {'view': 1, 'frame': 4, 'sprite': 0, 'drag': None, 'dirty': False, 'images': [], 'posed': None}
    floor_y = 66 * big                     # the big view's feet, on screen

    def place(canvas, texels, scale, x, y, bg, fade=False):
        """Texels (palette index or -1, or RGB tuples / None) drawn scaled up with their column 0, row 0
        at (x, y): cropped to what is drawn, clear texels in the canvas's own colour."""
        clear = (None, -1)
        rows = [r for r, line in enumerate(texels) if any(t not in clear for t in line)]
        cols = [c for c in range(len(texels[0])) if any(line[c] not in clear for line in texels)]
        if not rows:
            return
        r0, c0 = rows[0], cols[0]
        image = tk.PhotoImage(width=cols[-1] - c0 + 1, height=rows[-1] - r0 + 1)
        back = tuple(int(bg[k:k + 2], 16) for k in (1, 3, 5))
        lines = []
        for line in texels[r0:rows[-1] + 1]:
            out = []
            for t in line[c0:cols[-1] + 1]:
                rgb = back if t in clear else t if isinstance(t, tuple) else pal[t]
                if fade and t not in clear:
                    rgb = tuple(int(0.35 * v + 0.65 * w) for v, w in zip(rgb, back))
                out.append('#%02x%02x%02x' % rgb)
            lines.append('{' + ' '.join(out) + '}')
        image.put(' '.join(lines))
        image = image.zoom(scale, scale)
        st['images'].append(image)
        canvas.create_image(x + c0 * scale, y + r0 * scale, image=image, anchor='nw')

    def binding():
        return cache.get(sprites[st['sprite']], FRAMES[st['frame']], st['view'])

    def draw_left():
        left_c.delete('all')
        b = binding()
        if not b:
            left_c.create_text(20, 20, text='no skeleton for this frame', fill='white', anchor='nw')
            return
        d = b.d
        texels, joints, drop, left = pose_drawing(b, poses[st['view']])
        st['posed'] = joints
        ox = 30 * big
        # Standing, faint, to the left; posed in the middle; the reference to the right.
        place(left_c, d.texels, big, ox - 26 * big - d.left * big, floor_y - d.top * big, LEFT_BG, True)
        place(left_c, texels, big, ox - left * big, floor_y - d.top * big, LEFT_BG)
        if reference and st['view'] == 1:
            place(left_c, reference, big, ox + 22 * big, floor_y - len(reference) * big, LEFT_BG)
        left_c.create_line(0, floor_y, 96 * big, floor_y, fill='#606070')
        for s in (0, 1):
            pts = [joints[HIP[s]], joints[KNEE[s]], joints[ANKLE[s]]]
            xy = [(ox + u * big, floor_y - h * big) for u, h in pts]
            left_c.create_line(*xy[0], *xy[1], *xy[2], fill=LEG_COLOURS[s], width=3)
            for x, y in xy[1:]:
                left_c.create_oval(x - 7, y - 7, x + 7, y + 7, outline='white', width=2)
            left_c.create_oval(xy[0][0] - 4, xy[0][1] - 4, xy[0][0] + 4, xy[0][1] + 4, fill=LEG_COLOURS[s])
        p = poses[st['view']]
        legs = '   '.join(f'{"RL"[s]}: thigh {p["legs"][s][0]:+.0f} x{p["legs"][s][1]:.2f} shin {p["legs"][s][2]:+.0f} '
                          f'x{p["legs"][s][3]:.2f}' for s in (0, 1))
        status.config(text=f'view {st["view"]}  {d.label} ({d.name}{" flipped" if d.flipped else ""})  dropped {drop}  '
                           f'front {"RL"[p["front"]]}   {legs}{"   *unsaved*" if st["dirty"] else ""}')

    def draw_right():
        right_c.delete('all')
        y = 10
        for sprite in sprites:
            for n, frame in enumerate(FRAMES):
                b = cache.get(sprite, frame, st['view'])
                if not b:
                    continue
                d = b.d
                x = 10 + n * 50 * small
                base = y + 60 * small
                place(right_c, d.texels, small, x + (24 - d.left) * small, base - d.top * small, RIGHT_BG)
                sq, top = squashed(d)
                place(right_c, sq, small, x + (24 - d.left) * small, base + 64 * small - top * small, RIGHT_BG)
                texels, _, _, left = pose_drawing(b, poses[st['view']])
                place(right_c, texels, small, x + (24 - left) * small, base + 128 * small - d.top * small, RIGHT_BG)
                if n == st['frame'] and sprite == sprites[st['sprite']]:
                    right_c.create_rectangle(x, base + 66 * small, x + 48 * small, base + 128 * small, outline='#c0c040')
                right_c.create_text(x + 2, y, text=f'{sprite}{frame}', fill='#9090a0', anchor='nw', font=('Consolas', 9))
            y += 3 * 64 * small

    def redraw(full=True):
        st['images'] = []
        draw_left()
        if full:
            draw_right()

    def press(ev):
        if not st['posed']:
            return
        ox, best, near = 30 * big, None, 16
        for s in (0, 1):
            # The ankle first: a shin folded to nothing sits on its knee, and pulled out it shows the knee again.
            for k, joint in ((2, ANKLE[s]), (1, KNEE[s])):
                u, h = st['posed'][joint]
                dist = math.hypot(ox + u * big - ev.x, floor_y - h * big - ev.y)
                if dist < near:
                    best, near = (s, k), dist
        # Held still while dragging: the drop the posed drawing had when the drag began.
        st['drag'] = best
        st['held'] = [p[:] for p in st['posed']]

    def move(ev):
        if not st['drag']:
            return
        s, k = st['drag']
        b, held = binding(), st['held']
        u, h = (ev.x - 30 * big) / big, (floor_y - ev.y) / big
        j = b.d.joints
        p = poses[st['view']]['legs'][s]
        if k == 1:                       # the knee: the thigh from the hip
            hu, hh = held[HIP[s]]
            length = math.hypot(j[KNEE[s]][0] - j[HIP[s]][0], j[KNEE[s]][1] - j[HIP[s]][1])
            p[0], p[1] = angle_of(u - hu, h - hh), max(0.05, math.hypot(u - hu, h - hh) / max(1e-3, length))
            # The shin keeps its way on the floor, as it was.
            held[KNEE[s]] = [u, h]
        else:                            # the ankle: the shin from the knee
            ku, kh = held[KNEE[s]]
            length = math.hypot(j[ANKLE[s]][0] - j[KNEE[s]][0], j[ANKLE[s]][1] - j[KNEE[s]][1])
            p[2], p[3] = angle_of(u - ku, h - kh), max(0.05, math.hypot(u - ku, h - kh) / max(1e-3, length))
        st['dirty'] = True
        redraw(False)

    def release(ev):
        if st['drag']:
            st['drag'] = None
            redraw()

    def act(what, arg=None):
        """One thing the buttons and keys both do."""
        v = st['view']
        if what == 'view':
            st['view'] = arg
        elif what == 'frame':
            st['frame'] = arg % len(FRAMES)
        elif what == 'sprite':
            st['sprite'] = arg % len(sprites)
        elif what == 'front':
            poses[v]['front'] = 1 - poses[v]['front']
            st['dirty'] = True
        elif what == 'reset':
            poses[v] = {'legs': [l[:] for l in seeds[v]['legs']], 'front': seeds[v]['front']}
            st['dirty'] = True
        elif what == 'twin':
            if v not in TWIN:
                return
            poses[TWIN[v]] = {'legs': [[-l[0], l[1], -l[2], l[3], l[4]] for l in poses[v]['legs']], 'front': poses[v]['front']}
            st['dirty'] = True
        elif what == 'save':
            write_poses(poses)
            st['dirty'] = False
        elif what == 'quit':
            root.destroy()
            return
        redraw()
        mark()

    def key(ev):
        # By the character typed where there is one (any layout, top row or numpad), else by the key's name.
        name = ev.keysym
        k = (ev.char if ev.char and ev.char.isprintable() else name).lower()
        if name.startswith('KP_') and name[3:].isdigit():
            k = name[3:]
        if k in ('1', '2', '3', '4', '5', '6', '7', '8'):
            act('view', int(k))
        elif name == 'Right':
            act('frame', st['frame'] + 1)
        elif name == 'Left':
            act('frame', st['frame'] - 1)
        elif name in ('Up', 'Down') or k == 'm':
            act('sprite', st['sprite'] + (-1 if name == 'Up' else 1))
        else:
            acts = {'f': 'front', 'z': 'reset', 'c': 'twin', 's': 'save', 'q': 'quit'}
            if k in acts:
                act(acts[k])

    def pick(ev):
        # A small frame clicked: shown large.
        n, s = int((ev.x - 10) // (50 * small)), int((ev.y - 10) // (3 * 64 * small))
        if 0 <= n < len(FRAMES) and 0 <= s < len(sprites):
            st['frame'], st['sprite'] = n, s
            redraw(False)
            draw_right()
            mark()

    # The button bar: views, frames, sprites, then what is done to the pose.
    def button(text, command, width=None):
        b = tk.Button(bar, text=text, command=command, width=width)
        b.pack(side='left', padx=1)
        return b

    def gap(text='  '):
        tk.Label(bar, text=text).pack(side='left')
    names = {1: '1 front', 2: '2', 3: '3 side', 4: '4', 5: '5 back', 6: '6', 7: '7 side', 8: '8'}
    gap('view')
    view_buttons = {v: button(names[v], lambda v=v: act('view', v), 7) for v in range(1, 9)}
    gap('   frame')
    frame_buttons = {n: button(f, lambda n=n: act('frame', n), 2) for n, f in enumerate(FRAMES)}
    gap()
    sprite_buttons = {n: button(s, lambda n=n: act('sprite', n), 5) for n, s in enumerate(sprites)}
    gap()
    for text, what in (('front leg', 'front'), ('mirror to twin', 'twin'), ('reset view', 'reset'), ('save', 'save')):
        button(text, lambda what=what: act(what))

    def mark():
        for group, now in ((view_buttons, st['view']), (frame_buttons, st['frame']), (sprite_buttons, st['sprite'])):
            for k, button in group.items():
                button.config(relief='sunken' if k == now else 'raised', bg='#c8c860' if k == now else 'SystemButtonFace')

    left_c.bind('<ButtonPress-1>', press)
    left_c.bind('<B1-Motion>', move)
    left_c.bind('<ButtonRelease-1>', release)
    right_c.bind('<ButtonPress-1>', pick)
    root.bind('<Key>', key)
    redraw()
    mark()
    root.focus_force()
    root.mainloop()


def main():
    args = sys.argv[1:]
    ref = None
    if '--ref' in args:
        i = args.index('--ref')
        ref = args[i + 1]
        del args[i:i + 2]
    if len(args) >= 2 and args[0] == 'edit':
        edit(args[1], [s.upper() for s in args[2:]] or ['POSS', 'SPOS'], ref)
    elif len(args) >= 3 and args[0] == 'sheet':
        sheet(args[1], args[2], [s.upper() for s in args[3:]] or ['POSS', 'SPOS'])
    else:
        raise SystemExit(__doc__)


if __name__ == '__main__':
    main()
