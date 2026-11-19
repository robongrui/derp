"""Lab: a first-person kick drawn from a posed 3D leg, as Doom weapon frames.

A stand-in until a sprite artist draws one. The leg is three shapes (thigh,
shin, boot) posed by a two-bone reach from the hip to a foot path, seen from
the eye and drawn straight onto Doom's 320x200 screen, one sample a pixel so
the texels stay chunky. Every frame comes from the same leg, so the frames
track each other by construction.

Colours are the IWAD's own: the trouser and boot ramps are read off the
player's standing sprite (PLAYA1), so the leg matches whatever marine the
WAD draws.

    py tests\\kick_sprite.py [iwad] [--out build\\kick-sprite.png] [--frames 5]

Writes a contact sheet (each frame on the screen, the fist beside it for
scale) and prints each frame's size and weapon offsets.
"""
import argparse
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from death_lab import Picture, png, read_wad  # noqa: E402

W, H = 320, 200
FOCAL = 160.0            # 90 degrees across 320 pixels
TALL = 1.2               # Doom's pixels are 1.2 tall

# The leg, in units, in the eye's frame: x right, y ahead, z up.
# Staged, not anatomical: the hip sits low and behind so the thigh and knee
# stay off the screen's bottom right, and the shin and boot carry the picture.
HIP = np.array([9.0, -8.0, -14.0])
THIGH, SHIN = 15.0, 15.5
# The foot's path: tucked under the screen, then driven out ahead.
FOOT_TUCKED = np.array([10.0, 8.0, -23.0])
FOOT_OUT = np.array([2.0, 17.0, -8.5])
LIGHT = np.array([-0.75, -0.20, 0.62])
LIGHT = LIGHT / np.linalg.norm(LIGHT)


def norm(v):
    return v / np.linalg.norm(v)


def pose(t):
    """The leg at reach t (0 tucked, 1 out): hip, knee, ankle and the foot's frame."""
    ease = t * t * (3 - 2 * t)
    ankle = FOOT_TUCKED + (FOOT_OUT - FOOT_TUCKED) * ease
    ankle = ankle + np.array([0, 0, 3.0 * np.sin(np.pi * ease)])      # the foot arcs up on its way
    reach = ankle - HIP
    far = np.linalg.norm(reach)
    most = THIGH + SHIN - 0.3
    if far > most:
        ankle = HIP + reach * (most / far)
        reach, far = ankle - HIP, most
    along = reach / far
    a = (THIGH * THIGH - SHIN * SHIN + far * far) / (2 * far)
    lift = np.sqrt(max(THIGH * THIGH - a * a, 0.0))
    # The knee bends outward, off the screen.
    pole = np.array([1.0, 0.0, -0.2])
    pole = norm(pole - along * pole.dot(along))
    knee = HIP + along * a + pole * lift
    # The foot: toes up and ahead when out (the sole meets the target), hanging when tucked.
    shin = norm(ankle - knee)
    toes = norm(np.array([-0.50, 0.30, 0.81]) * ease + (shin * 0.5 + np.array([-0.3, 0.6, 0.3])) * (1 - ease))
    # The instep turned toward the eye, so the boot shows in profile with its sole to the target.
    side = norm(np.cross(np.array([0.9, 0.15, 0.25]), toes))
    up = np.cross(toes, side)
    return HIP, knee, ankle, (toes, side, up)


def cone(p, a, b, ra, rb):
    """Distance to a capsule whose radius runs from ra to rb; also how far along (0..1)."""
    ba = b - a
    h = np.clip(((p - a) @ ba) / ba.dot(ba), 0, 1)
    return np.linalg.norm(p - (a + h[:, None] * ba), axis=1) - (ra + (rb - ra) * h), h


def box(q, half, r):
    d = np.abs(q) - (half - r)
    return np.linalg.norm(np.maximum(d, 0), axis=1) + np.minimum(d.max(axis=1), 0) - r


# What a pixel shows.
NONE, TROUSER, SHAFT, BOOT, SOLE = range(5)


def leg(p, bones):
    """Distance to the leg at points p, the part nearest, and two part coordinates."""
    hip, knee, ankle, (toes, side, up) = bones
    thigh, h1 = cone(p, hip, knee, 5.6, 4.4)
    shin, h2 = cone(p, knee, ankle, 4.4, 3.2)
    # The marine's shin guard, from under the knee down to the boot.
    top = knee + (ankle - knee) * 0.22
    shaft, h3 = cone(p, top, ankle, 4.5, 3.7)
    blouse = np.linalg.norm(p - (knee + (ankle - knee) * 0.16), axis=1) - 4.6
    # The foot, in its own frame: x along the toes, y across, z up; lower toward the toe.
    q = (p - ankle) @ np.stack([toes, side, up], axis=1)
    f = q - np.array([4.2, 0.0, -2.2])
    u = np.clip((f[:, 0] + 6.5) / 13.0, 0, 1)
    upper = f.copy()
    upper[:, 2] = (upper[:, 2] + 0.2) / (1.0 - 0.42 * u * u) - 0.2
    foot = box(upper, np.array([6.5, 2.6, 2.6]), 1.6)
    sole = box(f - np.array([0.2, 0.0, -2.9]), np.array([7.1, 3.1, 1.0]), 0.35)
    heel = box(f - np.array([-4.6, 0.0, -3.6]), np.array([2.4, 2.8, 1.0]), 0.3)
    sole = np.minimum(sole, heel)
    cloth = np.minimum(np.minimum(thigh, shin), blouse)
    parts = np.stack([cloth, shaft, foot, sole])
    which = parts.argmin(axis=0)
    d = parts.min(axis=0)
    kind = np.array([TROUSER, SHAFT, BOOT, SOLE])[which]
    # Along the part (for folds, laces and treads) and across it.
    along = np.where(which == 0, np.where(thigh < shin, h1 * THIGH, THIGH + h2 * SHIN),
                     np.where(which == 1, h3 * SHIN * 0.78, f[:, 0]))
    across = np.where(which >= 2, f[:, 1], 0.0)
    height = np.where(which >= 2, f[:, 2], 0.0)
    return d, kind, along, across, height


def render(t):
    """One frame: per pixel the part, the light on it (0..1) and paint marks."""
    bones = pose(t)
    px, py = np.meshgrid(np.arange(W) + 0.5, np.arange(H) + 0.5)
    rays = np.stack([(px - W / 2) / FOCAL, np.ones_like(px), -(py - H / 2) * TALL / FOCAL], axis=-1).reshape(-1, 3)
    rays /= np.linalg.norm(rays, axis=1)[:, None]
    depth = np.full(len(rays), 2.0)
    live = np.ones(len(rays), bool)
    hit = np.zeros(len(rays), bool)
    for _ in range(72):
        index = np.nonzero(live)[0]
        if not len(index):
            break
        d = leg(rays[index] * depth[index, None], bones)[0]
        depth[index] += d
        close = d < 0.02
        hit[index[close]] = True
        live[index[close | (depth[index] > 60)]] = False
    index = np.nonzero(hit)[0]
    p = rays[index] * depth[index, None]
    _, kind, along, across, height = leg(p, bones)
    e = 0.05
    n = np.stack([leg(p + o, bones)[0] - leg(p - o, bones)[0] for o in np.eye(3) * e], axis=1)
    n /= np.linalg.norm(n, axis=1)[:, None] + 1e-9
    facing = np.clip(-(n * rays[index]).sum(axis=1), 0, 1)
    light = 0.12 + 0.88 * np.clip(n @ LIGHT, 0, 1) ** 1.3
    light *= 0.55 + 0.45 * facing ** 0.5                    # edges fall dark, as painted sprites do
    # Paint. Trousers: folds across the leg, deepest at the knee and the blouse.
    fold = np.sin(along * 1.9 + 2.5 * np.sin(across + p[:, 0] * 0.7)) * (0.05 + 0.10 * np.exp(-((along - THIGH) / 4.0) ** 2)
                                                                         + 0.10 * np.exp(-((along - THIGH - 6.5) / 2.0) ** 2))
    light = np.where(kind == TROUSER, light + fold, light)
    # Boot shaft: a lighter cuff at its top, eyelets down the front.
    light = np.where((kind == SHAFT) & (along < 1.3), light + 0.15, light)
    strap = (kind == SHAFT) & ((np.abs(along - 2.0) < 0.5) | (np.abs(along - 6.0) < 0.6) | (np.abs(along - 9.5) < 0.6))
    light = np.where(strap, light * 0.6, light)
    # Boot: toe cap seam, laces across the instep, treads under the sole.
    seam = (kind == BOOT) & (np.abs(along - 3.2) < 0.45)
    laces = (kind == BOOT) & (height > 0.6) & (along < 2.2) & (np.abs(across) < 1.5) & (np.mod(along, 1.5) < 0.6)
    tread = (kind == SOLE) & (height < -3.2) & (np.mod(along + 20, 2.2) < 0.9)
    light = np.where(seam | tread, light * 0.55, light)
    light = np.where(laces, light + 0.25, light)
    # Texel grain, the same on every frame at the same spot of the leg's surface.
    cell = np.floor(np.stack([along * 5.0, across * 5.0 + height * 3.7, facing * 23], axis=1)).astype(np.int64)
    grain = ((cell[:, 0] * 73856093 ^ cell[:, 1] * 19349663 ^ cell[:, 2] * 83492791) & 255) / 255.0
    light = light + np.where(kind == SOLE, 0.0, (grain - 0.5) * 0.09)
    part = np.zeros(W * H, np.int8)
    shade = np.zeros(W * H)
    part[index] = kind
    shade[index] = np.clip(light, 0, 1)
    return part.reshape(H, W), shade.reshape(H, W)


def ramps(lumps, pal):
    """Trouser, shin guard and sole ramps (palette indices, dark to light) off the player's sprite."""
    pic = Picture(lumps['PLAYA1'])
    rows = [r for r in range(pic.height) if any(v >= 0 for v in pic.texels[r])]
    first, tall = rows[0], rows[-1] - rows[0] + 1

    def ramp(low, high, wider):
        # The palette run the artwork uses most in that band of the body, a little wider for shading.
        seen = {}
        for r in rows:
            if low <= (r - first) / tall < high:
                for v in pic.texels[r]:
                    if v >= 16:
                        seen[v] = seen.get(v, 0) + 1
        run = max(set(i // 8 for i in seen), key=lambda g: sum(n for i, n in seen.items() if i // 8 == g))
        used = [i for i in seen if abs(i // 8 - run) <= 1 and seen[i] > 1] or [i for i in seen if i // 8 == run]
        lo, hi = max(min(used) - wider, min(used) // 8 * 8), min(max(used) + wider, max(used) // 8 * 8 + 7)   # never into the next run
        return sorted(range(lo, hi + 1), key=lambda i: 0.3 * pal[3 * i] + 0.59 * pal[3 * i + 1] + 0.11 * pal[3 * i + 2])

    return {TROUSER: ramp(0.50, 0.68, 1), SHAFT: ramp(0.72, 0.97, 1), SOLE: ramp(0.97, 1.01, 3)}


def frame(t, colours):
    """A frame as palette indices (-1 clear), cropped, with Doom weapon offsets."""
    part, shade = render(t)
    index = np.full((H, W), -1, int)
    for kind, ramp in ((TROUSER, colours[TROUSER]), (SHAFT, colours[SHAFT]), (BOOT, colours[SHAFT]), (SOLE, colours[SOLE])):
        level = np.clip((shade * len(ramp)).astype(int), 0, len(ramp) - 1)
        index = np.where(part == kind, np.array(ramp)[level], index)
    ys, xs = np.nonzero(index >= 0)
    x0, x1, y0, y1 = xs.min(), xs.max() + 1, ys.min(), ys.max() + 1
    # Weapon frames sit at sx 1, sy 32: left and top place the crop on the screen.
    return index[y0:y1, x0:x1], 1 - x0, 32 - y0


def place(screen, texels, left, top, pal, flip=False):
    """Draw a weapon frame onto an RGB screen the way the game does (sx 1, sy 32)."""
    h, w = texels.shape
    x0, y0 = 1 - left, 32 - top
    for y in range(h):
        for x in range(w):
            v = texels[y, w - 1 - x if flip else x]
            if v >= 0 and 0 <= y0 + y < H and 0 <= x0 + x < W:
                screen[y0 + y, x0 + x] = pal[3 * v:3 * v + 3]


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    ap = argparse.ArgumentParser()
    ap.add_argument('iwad', nargs='?', default=os.path.join(here, '..', '..', 'DOOM-gamedata', 'gog_doom', 'doom.wad'))
    ap.add_argument('--out', default=os.path.join(here, '..', 'build', 'kick-sprite.png'))
    ap.add_argument('--frames', type=int, default=5)
    ap.add_argument('--scale', type=int, default=2)
    args = ap.parse_args()
    lumps = read_wad(args.iwad)
    pal = list(lumps['PLAYPAL'][:768])
    colours = ramps(lumps, pal)
    print('ramps', colours)
    fist = Picture(lumps['PUNGA0'])
    fist_texels = np.array(fist.texels)
    panels = []
    for k in range(args.frames):
        t = 0.45 + 0.55 * k / (args.frames - 1)        # below that the leg is still under the screen
        texels, left, top = frame(t, colours)
        print('frame %c  reach %.2f  %dx%d  left %d  top %d' % (65 + k, t, texels.shape[1], texels.shape[0], left, top))
        # A plain room: wall above, floor below, and the status bar's rows greyed.
        screen = np.zeros((H, W, 3), np.uint8)
        screen[:84] = (88, 76, 64)
        screen[84:168] = (56, 52, 48)
        screen[168:] = (30, 30, 30)
        place(screen, texels, left, top, pal)
        place(screen, fist_texels, fist.left + 150, fist.top, pal, flip=True)   # the fist, moved left, for scale
        panels.append(screen)
    gap = np.full((H, 4, 3), 255, np.uint8)
    row = np.concatenate([x for s in panels for x in (s, gap)][:-1], axis=1)
    # Shown as the game shows it: pixels 1.2 tall.
    s = args.scale
    ys = (np.arange(int(H * s * TALL)) / (s * TALL)).astype(int)
    big = row[ys][:, np.repeat(np.arange(row.shape[1]), s)]
    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    open(args.out, 'wb').write(png(big.shape[1], big.shape[0], [r.reshape(-1).tolist() for r in big]))
    print('wrote', os.path.abspath(args.out))


if __name__ == '__main__':
    main()
