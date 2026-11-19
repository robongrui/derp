"""Freeze a monster's fitted skeletons into the stock tables, as if placed by hand (gore, PLAN-skin-suit.md):
a monster without a hand rig is fitted again at every level load (seconds a monster); frozen, it loads at
once, and the fits can still be corrected picture by picture in tests/skeleton_rig.py.

  freeze <dump> <sprite>
      The sprite's drawings from a -skeletondump (the game's fits): those the fit covers well enough (0.8 of
      their texels within reach of a bone, the game's own cut) go into platform/skeletons_stock.h, and each
      frame's 3D pose merged from them into platform/skeletons3d_stock.h; the others into the table of
      drawings with none (skeletons_stock.h), so the game neither fits nor uses them. Entries of the sprite
      already there are replaced; other sprites' are kept.

Usage (Windows: py, never python):
  py -I tests/skeleton_freeze.py freeze skel_baron.txt BOSS
"""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import skeleton_rig as rig  # noqa: E402
import skeleton_tool as tool  # noqa: E402

NEEDED = 0.8   # scene3d/skeletons.cpp's fitNeeded
STOCK2D = rig.STOCK2D


def coverage(dump, sprite):
    """name -> (width, height, hash, fit) for the sprite's drawings in the dump."""
    out = {}
    for line in open(dump, encoding='utf-8'):
        f = line.split()
        if len(f) > 6 and f[0].startswith(sprite):
            out[f[0]] = (int(f[1]), int(f[2]), int(f[3]), float(f[5]))
    return out


def save_none(sprite, names):
    """The drawings with no skeleton: the sprite's entries replaced by names (name, width, height, hash)."""
    text = open(STOCK2D, encoding='utf-8').read()
    start = text.index('doom_stock_skeleton_none_table[] = {')
    end = text.index('    {nullptr', start)
    kept = {tool.un_c(m.group(1)): m.group(0) for m in re.finditer(r'    \{"' + tool.NAME + r'", \d+, \d+, \d+u\},\n', text[start:end])}
    kept = {k: v for k, v in kept.items() if not k.startswith(sprite)}
    for name, w, h, hsh in names:
        kept[name] = '    {"%s", %d, %d, %du},\n' % (tool.c_name(name), w, h, hsh)
    head = text[:start] + 'doom_stock_skeleton_none_table[] = {\n    // (tests/skeleton_freeze.py writes entries here)\n'
    open(STOCK2D, 'w', encoding='utf-8', newline='\n').write(head + ''.join(kept[k] for k in sorted(kept)) + text[end:])


def drop_drawings(names):
    """Takes these drawings' entries out of the hand-placed table (a drawing frozen before, now with none)."""
    text = open(STOCK2D, encoding='utf-8').read()
    start = text.index('doom_stock_skeleton_table[] = {')
    end = text.index('    {nullptr', start)
    body = re.sub(r'    \{"' + tool.NAME + r'".*?\}\},\n', lambda m: '' if tool.un_c(m.group(1)) in names else m.group(0), text[start:end], flags=re.S)
    open(STOCK2D, 'w', encoding='utf-8', newline='\n').write(text[:start] + body + text[end:])


def freeze(dump, sprite):
    cover = coverage(dump, sprite)
    frames = rig.frames_for(dump, sprite)
    good, weak, rigs = [], [], []
    for f in frames:
        keep = [e for e in f.drawings if cover.get(e.name, (0, 0, 0, 0))[3] >= NEEDED]
        weak += [e.name for e in f.drawings if e not in keep]
        if not keep:
            continue
        f.drawings = keep
        f.merge()   # the frame's pose from the drawings kept only
        for e in keep:
            e.touched = True
        good += keep
        rigs.append(f.rig())
    drop_drawings(set(weak))
    rig.save_drawings(good)
    rig.save(rigs)
    save_none(sprite, [(n,) + cover[n][:3] for n in weak])
    print('%s: %d drawings frozen in %d frames, %d with none' % (sprite, len(good), len(rigs), len(weak)))


def main():
    if len(sys.argv) >= 4 and sys.argv[1] == 'freeze':
        freeze(sys.argv[2], sys.argv[3])
    else:
        raise SystemExit(__doc__)


if __name__ == '__main__':
    main()
