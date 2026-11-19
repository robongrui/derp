"""Gore's rigs (PLAN-skin-suit.md 4.5): place a frame's joints picture by
picture, and see them merged into one 3D pose that all its rotations share.

Id drew a frame's rotations from one pose, each a little off-centre and
off-size: the imp's five views of a frame fit one 3D pose to about 1.6 units
once each view may shift and scale. The joints placed in each picture are kept
exactly (platform/skeletons_stock.h); the pose merged from them
(platform/skeletons3d_stock.h) gives the game their depth, which part lies in
front, and stands in for pictures not placed yet.

  edit <wad> <dump> <sprite>
      The frames of a sprite (TROO). Each picture shows its own joints (solid,
      labelled; placed by hand, else the game's from the dump: -skeletondump)
      and the merged pose as it sees it (grey, dotted). Drag a joint: it moves
      in that picture only, and the merge follows on release. Joints the merge
      can't follow (more than 2.5 units off) are red, with how far. 1-5: one
      picture large, 0: all; n / p (or arrow keys): next / previous frame; s:
      save (every picture's joints as shown, and every frame's merged pose);
      q: quit. Each picture starts from the frame's saved pose seen from its
      side, else from the joints placed in it.
  seed <dump> <sprite>
      Merge every frame of the sprite and save, without editing.

Usage (Windows: py, never python):
  py -I tests/skeleton_rig.py edit path/to/doom2.wad skel.txt TROO
"""
import math
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from death_lab import Picture, read_wad  # noqa: E402
from skeleton_tool import BONES, COLOURS, JOINTS, LABELS, NAME, c_name, read_dump, un_c  # noqa: E402
from skeleton_tool import save as save_drawings  # noqa: E402

STOCK = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'platform', 'skeletons3d_stock.h')
STOCK2D = os.path.join(os.path.dirname(STOCK), 'skeletons_stock.h')


class View:
    def __init__(self, name, width, height, hash_, yaw, du=0.0, dh=0.0, scale=1.0):
        self.name, self.width, self.height, self.hash, self.yaw = name, width, height, hash_, yaw
        self.du, self.dh, self.scale = du, dh, scale

    def axis(self):
        a = math.radians(self.yaw)
        return math.cos(a), math.sin(a)

    def project(self, p):
        """(u, h) in the drawing, and how near the viewer (larger: nearer)."""
        c, s = self.axis()
        return (self.scale * (p[0] * c + p[1] * s) + self.du, self.scale * p[2] + self.dh,
                p[0] * s - p[1] * c)


class Rig:
    def __init__(self, sprite, letter, joints, views):
        self.sprite, self.letter, self.joints, self.views = sprite, letter, joints, views


def solve(entries):
    """One pose from per-drawing joints, each view free to shift and scale."""
    views = [View(e.name, e.width, e.height, e.hash, int(round(e.yaw))) for e in entries]
    joints = [[0.0, 0.0, 0.0] for _ in range(15)]
    for _ in range(40):
        for j in range(15):
            A = [v.axis() for v in views]
            b = [(e.joints[j][0] - v.du) / v.scale for e, v in zip(entries, views)]
            s11 = sum(a * a for a, _ in A); s12 = sum(a * c for a, c in A); s22 = sum(c * c for _, c in A)
            t1 = sum(a * x for (a, _), x in zip(A, b)); t2 = sum(c * x for (_, c), x in zip(A, b))
            det = s11 * s22 - s12 * s12
            if abs(det) < 1e-6:     # one view only: no depth
                x, y = (t1 / s11 if s11 else 0.0), 0.0
            else:
                x, y = (t1 * s22 - t2 * s12) / det, (s11 * t2 - s12 * t1) / det
            z = sum((e.joints[j][1] - v.dh) / v.scale for e, v in zip(entries, views)) / len(views)
            joints[j] = [x, y, z]
        if len(views) < 2:
            break
        for e, v in zip(entries, views):
            pu, ph = [], []
            for p in joints:
                c, s = v.axis()
                pu.append(p[0] * c + p[1] * s)
                ph.append(p[2])
            ou = [q[0] for q in e.joints]; oh = [q[1] for q in e.joints]
            mpu, mph, mou, moh = sum(pu) / 15, sum(ph) / 15, sum(ou) / 15, sum(oh) / 15
            num = sum((p - mpu) * (o - mou) for p, o in zip(pu, ou)) + sum((p - mph) * (o - moh) for p, o in zip(ph, oh))
            den = sum((p - mpu) ** 2 for p in pu) + sum((p - mph) ** 2 for p in ph)
            v.scale = max(0.8, min(1.25, num / den if den else 1.0))
            v.du, v.dh = mou - v.scale * mpu, moh - v.scale * mph
    # A pose moved as a whole is the same pose with each view shifted back: the pelvis is put at the middle
    # of the body's depth (y 0), so depth reads from there.
    ty = joints[0][1]
    for p in joints:
        p[1] -= ty
    for v in views:
        v.du += v.scale * ty * v.axis()[1]
    return joints, views


def frames_of(entries, sprite):
    """The dump's drawings of a sprite by frame letter (rotating frames only)."""
    out = {}
    for e in entries:
        if e.name.startswith(sprite) and len(e.name) >= 6 and e.name[5] != '0':
            out.setdefault(e.name[4], []).append(e)
    return out


def load():
    """The rigs in skeletons3d_stock.h: one line each, the joints' braces, then the views'."""
    rigs = []
    for line in open(STOCK, encoding='utf-8'):
        m = re.match(r"""    \{"(\w+)", '(\\.|[^'])', \{(.*)\}\},$""", line.rstrip())
        if not m or '}}, {{"' not in m.group(3):
            continue
        joints_text, views_text = m.group(3).split('}}, {{"', 1)
        nums = [float(x) for x in re.findall(r'-?\d+\.?\d*', joints_text)]
        joints = [nums[3 * j:3 * j + 3] for j in range(15)]
        views = []
        for v in re.finditer(r'"' + NAME + r'", (\d+), (\d+), (\d+)u, (-?\d+), (-?[\d.]+)f, (-?[\d.]+)f, ([\d.]+)f\}', '"' + views_text):
            views.append(View(un_c(v.group(1)), int(v.group(2)), int(v.group(3)), int(v.group(4)), int(v.group(5)),
                              float(v.group(6)), float(v.group(7)), float(v.group(8))))
        rigs.append(Rig(m.group(1), un_c(m.group(2)), joints, views))
    return rigs


def save(rigs):
    text = open(STOCK, encoding='utf-8').read()
    start = text.index('doom_stock_rig3d_table[] = {')
    end = text.index('    {nullptr', start)
    kept = {(r.sprite, r.letter): r for r in load()}
    for r in rigs:
        kept[(r.sprite, r.letter)] = r
    lines = []
    for key in sorted(kept):
        r = kept[key]
        joints = ', '.join('{%.2ff, %.2ff, %.2ff}' % tuple(p) for p in r.joints)
        views = ', '.join('{"%s", %d, %d, %du, %d, %.2ff, %.2ff, %.3ff}' % (c_name(v.name), v.width, v.height, v.hash, v.yaw, v.du, v.dh, v.scale)
                          for v in r.views)
        lines.append('    {"%s", \'%s\', {%s}, {%s}},\n' % (r.sprite, c_name(r.letter), joints, views))
    head = text[:start] + 'doom_stock_rig3d_table[] = {\n    // (tests/skeleton_rig.py writes entries here)\n'
    open(STOCK, 'w', encoding='utf-8', newline='\n').write(head + ''.join(lines) + text[end:])
    print(f'saved {len(rigs)} frame(s) into {STOCK}')


def load_drawings():
    """The joints placed by hand per drawing (platform/skeletons_stock.h), by lump name."""
    out = {}
    for line in open(STOCK2D, encoding='utf-8'):
        m = re.match(r'\s+\{"' + NAME + r'", (\d+), (\d+), (\d+)u, \{(.*)\}\},\s*$', line)
        if not m:
            continue
        nums = [float(x) for x in re.findall(r'-?\d+\.?\d*', m.group(5))]
        out[un_c(m.group(1))] = [nums[2 * j:2 * j + 2] for j in range(15)]
    return out


class Frame:
    """A frame's drawings, each with its own joints (by hand where placed, else from the dump), and the 3D
    pose merged from them all, with how far each joint of each drawing lies from that pose."""
    def __init__(self, sprite, letter, entries, placed, rig=None):
        self.sprite, self.letter = sprite, letter
        self.drawings = sorted(entries, key=lambda e: e.yaw)
        seen = {v.name: v for v in rig.views} if rig else {}
        for e in self.drawings:
            # Where to start: the frame's saved 3D pose seen from this picture (the latest work), else the
            # joints placed in it, else the game's.
            e.placed = e.name in seen or e.name in placed
            if e.name in seen:
                e.joints = [list(seen[e.name].project(p)[:2]) for p in rig.joints]
            elif e.name in placed:
                e.joints = [list(q) for q in placed[e.name]]
        self.merge()

    def merge(self):
        self.joints, self.views = solve(self.drawings)
        self.misfit = []
        for e, v in zip(self.drawings, self.views):
            row = []
            for j in range(15):
                u, h, _ = v.project(self.joints[j])
                row.append(math.hypot(e.joints[j][0] - u, e.joints[j][1] - h))
            self.misfit.append(row)

    def rig(self):
        return Rig(self.sprite, self.letter, self.joints, self.views)


def frames_for(dump, sprite):
    placed, rigs = load_drawings(), {(r.sprite, r.letter): r for r in load()}
    return [Frame(sprite, letter, entries, placed, rigs.get((sprite, letter)))
            for letter, entries in sorted(frames_of(read_dump(dump), sprite).items())]


def seed(dump, sprite):
    save([f.rig() for f in frames_for(dump, sprite)])


OFF = 2.5   # units: a joint further than this from the merged pose is drawn red


def edit(wad, dump, sprite):
    import tkinter as tk
    lumps = read_wad(wad)
    raw = lumps['PLAYPAL']
    pal = ['#%02x%02x%02x' % (raw[3 * i], raw[3 * i + 1], raw[3 * i + 2]) for i in range(256)]
    frames = frames_for(dump, sprite)
    if not frames:
        raise SystemExit(f'no rotating frames of {sprite} in {dump}')
    root = tk.Tk()
    root.title('Gore rigs: place per picture, merged in 3D')
    row = tk.Frame(root)
    row.pack()
    status = tk.Label(root, anchor='w', font=('Consolas', 11))
    status.pack(fill='x')
    state = {'n': 0, 'focus': None, 'drag': None, 'touched': set()}
    canvases = []
    images = []   # (Tk drops an image nothing in Python holds)

    def frame():
        return frames[state['n']]

    def shown():
        """The drawings on screen: all of the frame's, or the one in focus."""
        f = frame()
        return [state['focus']] if state['focus'] is not None else list(range(len(f.drawings)))

    def zoom():
        return 11 if state['focus'] is not None else state.get('fit', 6)

    def build():
        for c, _, _ in canvases:
            c.destroy()
        canvases.clear()
        images.clear()
        f = frame()
        pics = [Picture(lumps[f.drawings[k].name]) for k in shown()]
        wide, tall = max(p.width for p in pics), max(p.height for p in pics)
        # All of a frame's views side by side at up to 6x, smaller where they wouldn't fit the screen (eight
        # views of the chaingunner: some 3000 pixels at 6x).
        room = root.winfo_screenwidth() - 40
        state['fit'] = max(2, min(6, (room // len(pics) - 64) // max(1, wide)))
        scale = zoom()
        for slot, k in enumerate(shown()):
            c = tk.Canvas(row, width=wide * scale + 60, height=tall * scale + 60, bg='#282830', highlightthickness=0)
            c.grid(row=0, column=slot, padx=2)
            c.bind('<ButtonPress-1>', lambda ev, k=k: press(ev, k))
            c.bind('<B1-Motion>', lambda ev, k=k: move(ev, k))
            c.bind('<ButtonRelease-1>', lambda ev: release())
            canvases.append((c, k, pics[slot]))
            # The drawing as one image, made here and never again: as a rectangle a texel, redrawn with every
            # move of the mouse, a big monster's five views were some 20000 shapes a move.
            pic = pics[slot]
            image = tk.PhotoImage(width=pic.width, height=pic.height)
            image.put(' '.join('{' + ' '.join(pal[t] if t >= 0 else '#282830' for t in line) + '}' for line in pic.texels))
            image = image.zoom(scale)
            images.append(image)
            c.create_image(30, 30, image=image, anchor='nw')
        draw()

    def screen(pic, u, h):
        scale = zoom()
        return (u + pic.left) * scale + 30, (pic.top - h) * scale + 30

    def draw(only=None):
        """The joints over the drawings: of every picture, or the one being dragged."""
        f = frame()
        for c, k, pic in canvases:
            if only is not None and k != only:
                continue
            e, v = f.drawings[k], f.views[k]
            c.delete('over')
            # The merged pose as this drawing sees it: thin grey, dotted.
            ghost = [v.project(p) for p in f.joints]
            for a, z in BONES:
                c.create_line(*screen(pic, ghost[a][0], ghost[a][1]), *screen(pic, ghost[z][0], ghost[z][1]),
                              fill='#9090a0', width=1, dash=(2, 3), tags='over')
            # This drawing's own joints: solid, labelled; red where the merge can't follow them.
            for b, (a, z) in enumerate(BONES):
                c.create_line(*screen(pic, *e.joints[a]), *screen(pic, *e.joints[z]),
                              fill='#%02x%02x%02x' % COLOURS[b], width=3, tags='over')
            for j in range(15):
                x, y = screen(pic, *e.joints[j])
                off = f.misfit[k][j]
                bad = off > OFF
                c.create_oval(x - 5, y - 5, x + 5, y + 5, outline='#ff3030' if bad else 'white', width=3 if bad else 2, tags='over')
                label = LABELS[j] + (' %.0f' % off if bad else '')
                for dx, dy, colour in ((1, 1, 'black'), (0, 0, '#ff6060' if bad else 'white')):
                    c.create_text(x + 8 + dx, y - 7 + dy, text=label, fill=colour, anchor='sw', font=('Consolas', 10, 'bold'), tags='over')
            note = 'placed' if e.placed else 'not placed yet'
            c.create_text(6, 6, text='%d: %s  %d deg  %s  worst %.1f' % (k + 1, e.name, round(e.yaw), note, max(f.misfit[k])),
                          fill='#c0c0c8', anchor='nw', font=('Consolas', 10), tags='over')
        everything = sorted(x for r in f.misfit for x in r)
        moved = '  (moved)' if f.letter in state['touched'] else ''
        status.config(text='frame %s (%d/%d)  merge: median %.1f, %d joints over %.1f%s   drag joints, 1-%d one picture / 0 all, '
                           'n/p frames, s save, q quit' % (f.letter, state['n'] + 1, len(frames), everything[len(everything) // 2],
                                                            sum(x > OFF for x in everything), OFF, moved, len(f.drawings)))

    def canvas_pic(k):
        return next(p for c, kk, p in canvases if kk == k)

    def press(ev, k):
        f, pic = frame(), canvas_pic(k)
        best, near = None, 14
        for j in range(15):
            x, y = screen(pic, *f.drawings[k].joints[j])
            d = math.hypot(x - ev.x, y - ev.y)
            if d < near:
                best, near = j, d
        state['drag'] = (k, best) if best is not None else None
        if best is not None:
            status.config(text=JOINTS[best])

    def move(ev, k):
        if not state['drag'] or state['drag'][0] != k:
            return
        f, scale, pic = frame(), zoom(), canvas_pic(k)
        e = f.drawings[k]
        e.joints[state['drag'][1]] = [round((ev.x - 30) / scale - pic.left, 2), round(pic.top - (ev.y - 30) / scale, 2)]
        e.placed = e.touched = True
        state['touched'].add(f.letter)
        draw(k)

    def release():
        if state['drag']:
            frame().merge()   # the 3D pose follows what was placed
            draw()
        state['drag'] = None

    def key(ev):
        k = ev.keysym.lower()
        if k in ('n', 'right'):
            state['n'] = (state['n'] + 1) % len(frames)
            state['focus'] = None
        elif k in ('p', 'left'):
            state['n'] = (state['n'] - 1) % len(frames)
            state['focus'] = None
        elif k.isdigit():
            n = int(k)
            state['focus'] = n - 1 if 1 <= n <= len(frame().drawings) else None
        elif k == 's':
            # Every picture as shown (so older joints left in the table can't come back), and every pose.
            for f in frames:
                for e in f.drawings:
                    e.touched = True
            save_drawings([e for f in frames for e in f.drawings])
            save([f.rig() for f in frames])
            state['touched'].clear()
        elif k == 'q':
            root.destroy()
            return
        else:
            return
        build()

    root.bind('<Key>', key)
    build()
    root.mainloop()


def main():
    if len(sys.argv) >= 5 and sys.argv[1] == 'edit':
        edit(sys.argv[2], sys.argv[3], sys.argv[4])
    elif len(sys.argv) >= 4 and sys.argv[1] == 'seed':
        seed(sys.argv[2], sys.argv[3])
    else:
        raise SystemExit(__doc__)


if __name__ == '__main__':
    main()
