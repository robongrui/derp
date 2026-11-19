"""Sky layers contact sheet (throwaway, branch tst-sky).

Splits each sky texture into sky, far ridge and near ridge, fills what the
ridges hide, extends the top, and drifts a valley mist band between the
ridges, so the idea can be judged on a sheet before any engine work.

Usage: py tests/sky_layers.py out.html doom.wad [doom2.wad heretic.wad ...]
"""
import base64
import html
import math
import random
import struct
import sys
import time
import zlib
from pathlib import Path

from material_survey import Game

TICS = 105          # animation length: three seconds at 35 Hz
SWAY = 4            # walking view: the near ridge sways this many texels
JUMP = 0.35         # cost of a one-row step in a cut line between columns
EDGE = 1.5          # how much a cut likes to sit on a colour edge
EXTEND = 32         # rows synthesised above the texture's top


def dist(a, b):
    return abs(a[0] - b[0]) + abs(a[1] - b[1]) + abs(a[2] - b[2])


class Palette:
    def __init__(self, raw):
        self.raw = bytes(raw[:768])
        self.rgb = [tuple(raw[3 * i:3 * i + 3]) for i in range(256)]
        self.cache = {}

    def nearest(self, c):
        c = tuple(int(round(v)) for v in c)
        if c not in self.cache:
            self.cache[c] = min(range(256), key=lambda i: dist(self.rgb[i], c))
        return self.cache[c]


# --- noise, periodic across the sky's width ---------------------------------

def hash01(*v):
    h = 0x9E3779B9
    for x in v:
        h = ((h ^ (x & 0xFFFFFFFF)) * 0x85EBCA6B) & 0xFFFFFFFF
        h ^= h >> 13
    h = (h * 0xC2B2AE35) & 0xFFFFFFFF
    return (h ^ (h >> 16)) / 4294967296.0


def smooth(t):
    return t * t * (3 - 2 * t)


def noise1(x, period, seed):
    i = math.floor(x)
    f = smooth(x - i)
    a, b = hash01(seed, i % period), hash01(seed, (i + 1) % period)
    return a + (b - a) * f


def noise2(x, y, period, seed):
    i, j = math.floor(x), math.floor(y)
    fx, fy = smooth(x - i), smooth(y - j)

    def h(a, b):
        return hash01(seed, a % period, b)
    top = h(i, j) + (h(i + 1, j) - h(i, j)) * fx
    bottom = h(i, j + 1) + (h(i + 1, j + 1) - h(i, j + 1)) * fx
    return top + (bottom - top) * fy


# --- cut lines ----------------------------------------------------------------

def row_models(rgb, w, h, inside):
    """Mean and spread per row of the pixels inside(x, y), widened over
    neighbouring rows until at least 12 pixels speak."""
    sums = [[0.0] * 7 for _ in range(h)]
    for y in range(h):
        s = sums[y]
        for x in range(w):
            if inside(x, y):
                c = rgb[y * w + x]
                s[0] += 1
                for k in range(3):
                    s[1 + k] += c[k]
                    s[4 + k] += c[k] * c[k]
    out = []
    for y in range(h):
        reach = 2
        while True:
            acc = [0.0] * 7
            for yy in range(max(0, y - reach), min(h, y + reach + 1)):
                for k in range(7):
                    acc[k] += sums[yy][k]
            if acc[0] >= 12 or reach >= h:
                break
            reach *= 2
        n = max(acc[0], 1)
        mean = [acc[1 + k] / n for k in range(3)]
        out.append((mean, [math.sqrt(max(acc[4 + k] / n - mean[k] ** 2, 0)) for k in range(3)]))
    return out


def misfit(c, model):
    m, s = model
    return sum(((c[k] - m[k]) / (s[k] + 8)) ** 2 for k in range(3))


def cut_cost(q, edge, x, w, h, lo):
    """Cost of cutting column x at each row (0..h): pixels from lo above the
    cut that look like the lower class (q), below it the upper one."""
    big = 1e9
    cost = [big] * (h + 1)
    c = sum(1 - q[r * w + x] for r in range(lo, h))
    cost[lo] = c
    for y in range(lo, h):
        p = q[y * w + x]
        c += p - (1 - p)
        cost[y + 1] = c - (EDGE * edge[(y + 1) * w + x] if y + 1 < h else 0)
    return cost


def best_path(costs):
    """One row per column minimising the costs plus JUMP per row of step
    between neighbours (L1 distance transform, so linear per column)."""
    n = len(costs[0])
    acc, back = list(costs[0]), []
    for cost in costs[1:]:
        best, arg = acc[:], list(range(n))
        for y in range(1, n):
            if best[y - 1] + JUMP < best[y]:
                best[y], arg[y] = best[y - 1] + JUMP, arg[y - 1]
        for y in range(n - 2, -1, -1):
            if best[y + 1] + JUMP < best[y]:
                best[y], arg[y] = best[y + 1] + JUMP, arg[y + 1]
        back.append(arg)
        acc = [best[y] + cost[y] for y in range(n)]
    y = min(range(n), key=acc.__getitem__)
    path = [y]
    for arg in reversed(back):
        y = arg[y]
        path.append(y)
    return path[::-1]


def edges(rgb, w, h):
    e = [0.0] * (w * h)
    for y in range(1, h):
        for x in range(w):
            e[y * w + x] = dist(rgb[(y - 1) * w + x], rgb[y * w + x])
    top = max(e) or 1
    return [v / top for v in e]


def no_slits(line, w, r=2):
    """Close dips narrower than 2r+1 columns: dark crevices and caves in rock
    are rock, not sky seen through a slit (1D opening on the line)."""
    low = [min(line[(x + d) % w] for d in range(-r, r + 1)) for x in range(w)]
    return [max(low[(x + d) % w] for d in range(-r, r + 1)) for x in range(w)]


def find_skyline(rgb, w, h, edge):
    """Sky above, ridges below: two per-row colour models refined over a few
    rounds, one cut per column."""
    band = max(2, h // 8)
    line = None
    for _ in range(5):
        if line is None:
            sky = row_models(rgb, w, h, lambda x, y: y < band)
            ground = row_models(rgb, w, h, lambda x, y: y >= h - band)
        else:
            sky = row_models(rgb, w, h, lambda x, y: y < line[x])
            ground = row_models(rgb, w, h, lambda x, y: y >= line[x])
        q = [0.0] * (w * h)
        for y in range(h):
            for x in range(w):
                ds, dg = misfit(rgb[y * w + x], sky[y]), misfit(rgb[y * w + x], ground[y])
                q[y * w + x] = ds / (ds + dg + 1e-9)
        line = best_path([cut_cost(q, edge, x, w, h, 0) for x in range(w)])
    line = no_slits(line, w)
    agree = sum(q[y * w + x] if y >= line[x] else 1 - q[y * w + x] for y in range(h) for x in range(w)) / (w * h)
    cover = sum(2 <= v <= h - 2 for v in line) / w
    return line, sky, 2 * agree - 1, cover


def seam_contrast(rgb, w, h, upper, lower, cols):
    """How hard a cut is: colour change across it (two rows each side)
    over the image's typical change across the same four-row span. A real
    silhouette sits on a strong, steady edge; a line through clouds doesn't."""
    def across(x, y):
        if not 2 <= y <= h - 2:
            return None
        a = [rgb[(y - j) * w + x] for j in (1, 2)]
        b = [rgb[(y + j) * w + x] for j in (0, 1)]
        return dist([sum(c[k] for c in a) / 2 for k in range(3)], [sum(c[k] for c in b) / 2 for k in range(3)])
    seam = [v for v in (across(x, lower[x]) for x in cols if lower[x] - upper[x] >= 0) if v is not None]
    typical = [v for v in (across(x, y) for x in range(0, w, 3) for y in range(2, h - 1, 3)) if v is not None]
    if not seam or not typical:
        return 0.0
    return (sum(seam) / len(seam)) / (sum(typical) / len(typical) + 1e-6)


def otsu(values, bins=64):
    lo, hi = min(values), max(values)
    if hi - lo < 1e-6:
        return lo, 0.0
    hist = [0] * bins
    for v in values:
        hist[min(bins - 1, int((v - lo) / (hi - lo) * bins))] += 1
    n = len(values)
    mean = sum(values) / n
    var = sum((v - mean) ** 2 for v in values) / n or 1e-9
    best, thr, w0, s0 = 0.0, lo, 0, 0.0
    for i in range(bins - 1):
        centre = lo + (i + 0.5) * (hi - lo) / bins
        w0 += hist[i]
        s0 += hist[i] * centre
        if not w0 or w0 == n:
            continue
        m0, m1 = s0 / w0, (mean * n - s0) / (n - w0)
        between = w0 * (n - w0) / n / n * (m0 - m1) ** 2
        if between > best:
            best, thr = between, lo + (i + 1) * (hi - lo) / bins
    return thr, best / var


def find_far(rgb, w, h, line, sky, edge):
    """Inside the ridges, the painter's far ridge sits closer to the sky
    colour than the near one: rank each ground pixel along near -> sky and
    cut the lighter band off the top."""
    band = max(2, h // 8)
    near = [0.0, 0.0, 0.0]
    n = 0
    for y in range(h - band, h):
        for x in range(w):
            if y >= line[x]:
                c = rgb[y * w + x]
                n += 1
                for k in range(3):
                    near[k] += c[k]
    if not n:
        return line[:], 0.0, 0.0
    near = [v / n for v in near]
    t = [0.0] * (w * h)
    values = []
    for y in range(h):
        s = sky[y][0]
        d = [s[k] - near[k] for k in range(3)]
        dd = sum(v * v for v in d) or 1.0
        for x in range(w):
            if y < line[x]:
                continue
            c = rgb[y * w + x]
            v = max(-0.5, min(1.5, sum((c[k] - near[k]) * d[k] for k in range(3)) / dd))
            t[y * w + x] = v
            values.append(v)
    if len(values) < 16:
        return line[:], 0.0, 0.0
    thr, eta = otsu(values)
    q = [0.0] * (w * h)
    for y in range(h):
        for x in range(w):
            if y >= line[x]:
                q[y * w + x] = 1 / (1 + math.exp(max(-40, min(40, (t[y * w + x] - thr) / 0.08))))
    cut = best_path([cut_cost(q, edge, x, w, h, line[x]) for x in range(w)])
    visible = sum(cut[x] - line[x] >= 2 for x in range(w)) / w
    return cut, eta, visible


# --- layers -------------------------------------------------------------------

def fill(pix, rgbp, w, h, top, have, crest, k=4, seed=0, haze=None):
    """Layer image (-1 = empty): column x is real from top[x] to have[x] and
    continues to the bottom with strips of donor columns that match the
    rows above and reach further. crest aligns donors on their own top row
    (rock under a ridge line), otherwise on the same row (sky gradient).
    Neighbouring columns prefer the next donor column at the same offset,
    so the copies come out as 2D patches, not vertical streaks; where no
    donor reaches as deep, any offset will do."""
    rng = random.Random(seed)
    out = [-1] * (w * h)
    for x in range(w):
        for r in range(top[x], have[x]):
            out[r * w + x] = pix[r * w + x]
    used = [[None] * h for _ in range(w)]      # (donor column, donor row) per filled texel

    def score(x, r, d, dr):
        s, n = 0, 0
        for j in range(1, k + 1):
            if r - j < top[x] or dr - j < top[d]:
                break
            s += dist(rgbp[out[(r - j) * w + x]], rgbp[pix[(dr - j) * w + d]])
            n += 1
        s = s / n if n else 120
        s -= 3 * min(have[d] - dr, h - r, 16)
        left = used[(x - 1) % w][r]
        if left and left[0] == (d - 1) % w and left[1] == dr:
            s -= 80
        return s + 40 * min(abs(d - x), w - abs(d - x)) / w

    for x in range(w):
        r = have[x]
        while r < h:
            cands = [(d, r + (top[d] - top[x] if crest else 0)) for d in range(w) if d != x]
            left = used[(x - 1) % w][r]
            if left:
                cands.append(((left[0] + 1) % w, left[1]))
            cands = [(d, dr) for d, dr in cands if top[d] <= dr < have[d]]
            if not cands and haze is not None:
                # deeper than any sky reaches: nobody sees this but the sway's edge
                out[r * w + x] = haze
                r += 1
                continue
            if not cands:
                deep = [d for d in range(w) if have[d] - top[d] > k]
                cands = [(d, dr) for d in rng.sample(deep, min(48, len(deep)))
                         for dr in range(top[d] + k, have[d], 2)]
            if not cands:
                if r - 1 < top[x]:
                    break
                out[r * w + x] = out[(r - 1) * w + x]
                r += 1
                continue
            d, dr = min(cands, key=lambda c: score(x, r, c[0], c[1]))
            while r < h and top[d] <= dr < have[d]:
                out[r * w + x] = pix[dr * w + d]
                used[x][r] = (d, dr)
                r += 1
                dr += 1
    return out


def far_top(line, cut, w, seed):
    """The far ridge line: the skyline where the far band shows, guessed
    between the gaps (and kept behind the near ridge there)."""
    seen = [cut[x] - line[x] >= 3 for x in range(w)]
    if not any(seen):
        return None
    f = [line[x] if seen[x] else None for x in range(w)]
    for x in range(w):
        if seen[x]:
            continue
        a = next(d for d in range(1, w + 1) if seen[(x - d) % w])
        b = next(d for d in range(1, w + 1) if seen[(x + d) % w])
        v = (line[(x - a) % w] * b + line[(x + b) % w] * a) / (a + b)
        v += 4 * (noise1(x / 8, max(1, w // 8), seed) - 0.5) * min(1, min(a, b) / 4)
        f[x] = max(int(round(v)), line[x])
    return f


def extend_up(pix, rgbp, w, h, line, rows, seed):
    """Rows above the texture, grown upward texel by texel from the sky's own
    texels whose neighbourhood matches what is already there."""
    rng = random.Random(seed)

    def sky_at(sx, sy):
        return 0 <= sy < h and sy < line[sx % w]
    sources = [(sx, sy) for sy in range(min(h - 1, h // 2)) for sx in range(w) if sky_at(sx, sy + 1)]
    if not sources:
        return None
    ext = [[None] * w for _ in range(rows)]

    def at(x, k):
        x %= w
        return (pix[x], x, 0) if k < 0 else ext[k][x]
    for k in range(rows):
        for x in range(w):
            neigh = [(dx, 1, at(x + dx, k - 1)) for dx in (-1, 0, 1)]
            neigh += [(-d, 0, ext[k][x - d]) for d in (1, 2) if x - d >= 0]
            cands = [((nsx - dx) % w, nsy - dy) for dx, dy, (_, nsx, nsy) in neigh]
            cands = [c for c in cands if sky_at(c[0], c[1] + 1) and c[1] >= 0]
            cands += rng.sample(sources, min(40, len(sources)))
            best, best_score = None, 1e18
            for sx, sy in cands:
                score = 0
                for dx, dy, (v, _, _) in neigh:
                    nx, ny = (sx + dx) % w, sy + dy
                    score += dist(rgbp[v], rgbp[pix[ny * w + nx]]) if sky_at(nx, ny) else 200
                if score < best_score:
                    best, best_score = (sx, sy), score
            ext[k][x] = (pix[best[1] * w + best[0]], best[0], best[1])
    return [ext[k][x][0] for k in reversed(range(rows)) for x in range(w)]


class Layers:
    def __init__(self, name, game_label, w, h, pix, pal):
        self.name, self.game, self.w, self.h, self.pix, self.pal = name, game_label, w, h, pix, pal
        seed = zlib.crc32(name.encode() + game_label.encode())
        rgb = [pal.rgb[p] for p in pix]
        edge = edges(rgb, w, h)
        self.line, sky_model, self.score, self.cover = find_skyline(rgb, w, h, edge)
        self.cut, self.eta, self.visible = find_far(rgb, w, h, self.line, sky_model, edge)
        self.sky_edge = seam_contrast(rgb, w, h, [0] * w, self.line, range(w))
        shown = [x for x in range(w) if self.cut[x] - self.line[x] >= 3]
        self.far_edge = seam_contrast(rgb, w, h, self.line, self.cut, shown)
        # a skyline must sit on a hard edge and not cling to the top rows;
        # a far ridge needs its own hard edge where it shows
        self.sky_ok = self.sky_edge >= 4 and sum(v <= 2 for v in self.line) < 0.2 * w
        self.far_ok = self.sky_ok and self.far_edge >= 3.4 and self.eta > 0.5 and self.visible > 0.1
        self.found = self.line, self.cut
        if not self.sky_ok:
            self.line = [h] * w
        near_top = self.cut if self.far_ok else self.line
        low = [rgb[y * w + x] for x in range(w) for y in range(max(0, self.line[x] - 3), self.line[x])]
        haze = pal.nearest([sum(c[k] for c in low) / max(1, len(low)) for k in range(3)])
        self.sky = fill(pix, pal.rgb, w, h, [0] * w, self.line, crest=False, seed=seed, haze=haze)
        self.far = None
        if self.far_ok:
            f = far_top(self.line, self.cut, w, seed)
            if f:
                self.far = fill(pix, pal.rgb, w, h, f, [self.cut[x] if self.cut[x] - self.line[x] >= 3 else f[x]
                                                         for x in range(w)], crest=True, seed=seed)
        self.near = [pix[y * w + x] if y >= near_top[x] else -1 for y in range(h) for x in range(w)]
        self.top = extend_up(pix, pal.rgb, w, h, self.line, EXTEND, seed)
        if self.sky_ok:
            self.mist_setup(near_top, rgb, seed)

    def mist_setup(self, near_top, rgb, seed):
        w, h, pal = self.w, self.h, self.pal
        # haze colour: the light end of the far band, else the sky just above the ridges
        if self.far:
            pool = [rgb[y * w + x] for x in range(w) for y in range(self.line[x], min(self.cut[x], h))]
        else:
            pool = [rgb[y * w + x] for x in range(w) for y in range(max(0, self.line[x] - 4), self.line[x])]
        pool.sort(key=sum)
        pool = pool[len(pool) * 7 // 10:] or [(128, 128, 128)]
        self.mist = [sum(c[k] for c in pool) / len(pool) for k in range(3)]
        self.mist_index = pal.nearest(self.mist)
        self.blends = {}
        # the band pools at valley level: halfway from the far crest down to
        # the deepest gap between near ridges, thickening towards the bottom
        crests = sorted(self.line)
        deepest = max(near_top)
        self.level = (crests[len(crests) // 2] + deepest) / 2
        self.ramp = max(6.0, (deepest - self.level) / 2)
        self.n1 = [noise1(u / 32, max(1, w // 32), seed) * 0.65 + noise1(u / 8, max(1, w // 8), seed + 1) * 0.35
                   for u in range(w)]
        self.n2 = [[noise2(u / 8, y / 3, max(1, w // 8), seed + 2) for u in range(w)] for y in range(h)]

    def blend(self, v, step):
        """The texel under the mist, a quarter step at a time towards the mist
        colour, snapped back into the palette (no dither)."""
        if (v, step) not in self.blends:
            c, a = self.pal.rgb[v], step / 4
            self.blends[(v, step)] = self.pal.nearest([c[k] + (self.mist[k] - c[k]) * a for k in range(3)])
        return self.blends[(v, step)]

    def frame(self, t, sway):
        """One tic: the mist band drifts with its own churn, the ridges shift
        by depth when the view sways. The painted sky stays put: it holds
        faint distant peaks that would sail off like clouds."""
        w, h = self.w, self.h
        s_near, s_far, s_mist = round(sway), round(sway * 0.35), round(sway * 0.6)
        off1, off2 = t // 4, t // 7
        top = [self.level + self.ramp * (self.n1[((x - s_mist) % w - off1) % w] - 0.5) for x in range(w)]
        out = []
        for y in range(h):
            row = y * w
            n2 = self.n2[y]
            for x in range(w):
                v = self.near[row + (x - s_near) % w]
                if v >= 0:
                    out.append(v)
                    continue
                v = self.far[row + (x - s_far) % w] if self.far else -1
                if v < 0:
                    v = self.sky[row + x]
                if y >= top[x] - self.ramp / 2:
                    d = (y - top[x]) / self.ramp + 0.7 * (n2[((x - s_mist) % w - off2) % w] - 0.5)
                    step = min(4, int(d * 4))
                    if step >= 4:
                        v = self.mist_index
                    elif step >= 1:
                        v = self.blend(v, step)
                out.append(v)
        return out


# --- images -------------------------------------------------------------------

def png_rgb(w, h, rgb):
    raw = b''.join(b'\0' + bytes(v for c in rgb[y * w:(y + 1) * w] for v in c) for y in range(h))

    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0)) +
            chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b''))


HOLE = (60, 20, 60)


def layer_png(w, h, pix, pal):
    return png_rgb(w, h, [pal.rgb[v] if v >= 0 else HOLE for v in pix])


def lzw(data, size0=8):
    clear, end = 1 << size0, (1 << size0) + 1
    codes, nxt, size = {}, end + 1, size0 + 1
    out = bytearray()
    bits = nbits = 0

    def emit(code):
        nonlocal bits, nbits
        bits |= code << nbits
        nbits += size
        while nbits >= 8:
            out.append(bits & 255)
            bits >>= 8
            nbits -= 8
    emit(clear)
    prefix = data[0]
    for c in data[1:]:
        key = (prefix << 8) | c
        if key in codes:
            prefix = codes[key]
            continue
        emit(prefix)
        codes[key] = nxt
        nxt += 1
        if nxt > (1 << size) and size < 12:
            size += 1
        if nxt >= 4093:
            emit(clear)
            codes, nxt, size = {}, end + 1, size0 + 1
        prefix = c
    emit(prefix)
    emit(end)
    if nbits:
        out.append(bits & 255)
    return bytes(out)


def gif(w, h, frames, pal, delay=3):
    out = bytearray(b'GIF89a' + struct.pack('<HHBBB', w, h, 0xF7, 0, 0) + pal.raw)
    out += b'\x21\xFF\x0BNETSCAPE2.0\x03\x01\x00\x00\x00'
    for f in frames:
        out += b'\x21\xF9\x04\x00' + struct.pack('<H', delay) + b'\x00\x00'
        out += b'\x2C' + struct.pack('<HHHHB', 0, 0, w, h, 0) + b'\x08'
        data = lzw(f)
        for i in range(0, len(data), 255):
            out += bytes([len(data[i:i + 255])]) + data[i:i + 255]
        out += b'\x00'
    return bytes(out + b'\x3B')


def overlay(s):
    """Both cuts as found, rejected or not (the stats line says which)."""
    w, h = s.w, s.h
    line, cut = s.found
    rgb = [s.pal.rgb[v] for v in s.pix]
    for x in range(w):
        if line[x] < h:
            rgb[line[x] * w + x] = (255, 60, 60)
        if cut[x] - line[x] >= 3 and cut[x] < h:
            rgb[cut[x] * w + x] = (60, 230, 255)
    return png_rgb(w, h, rgb)


# --- sheet --------------------------------------------------------------------

CSS = """
:root{--bg:#f4f3ef;--fg:#1c1c1e;--muted:#6a6a70;--line:#dcdad3;--card:#fff}
@media (prefers-color-scheme:dark){:root:not([data-theme=light]){--bg:#141415;--fg:#ececee;--muted:#9a9aa2;--line:#2c2c30;--card:#1d1d20}}
:root[data-theme=dark]{--bg:#141415;--fg:#ececee;--muted:#9a9aa2;--line:#2c2c30;--card:#1d1d20}
body{background:var(--bg);color:var(--fg);font:14px/1.45 system-ui,sans-serif;margin:0 auto;padding:24px 16px;max-width:1700px}
h1{font-size:22px;margin:0 0 4px}h2{font-size:17px;margin:36px 0 2px}p{color:var(--muted);margin:4px 0 10px;max-width:90ch}
.grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(520px,1fr));gap:14px}
figure{margin:0;background:var(--card);border:1px solid var(--line);border-radius:6px;padding:8px}
figcaption{font-size:12px;color:var(--muted);margin-bottom:6px}
img{image-rendering:pixelated;width:100%;height:auto;display:block}
.ok{color:#2f8f4e;font-weight:600}.no{color:#b5481f;font-weight:600}
"""


def data_uri(kind, data):
    return 'data:image/%s;base64,%s' % (kind, base64.b64encode(data).decode())


def section(s):
    w, h, pal = s.w, s.h, s.pal
    figs = [('Original', data_uri('png', layer_png(w, h, s.pix, pal))),
            ('Cuts: red = skyline, cyan = far | near ridge', data_uri('png', overlay(s))),
            ]
    if s.sky_ok:
        figs.append(('Sky layer, hidden part filled', data_uri('png', layer_png(w, h, s.sky, pal))))
    if s.far:
        figs.append(('Far ridge layer (purple = empty)', data_uri('png', layer_png(w, h, s.far, pal))))
    if s.sky_ok:
        figs.append(('Near ridge layer', data_uri('png', layer_png(w, h, s.near, pal))))
    if s.top:
        figs.append(('Top extended by %d rows (line marks the old top)' % EXTEND, data_uri('png', png_rgb(
            w, h + EXTEND + 1, [pal.rgb[v] for v in s.top] + [(255, 60, 60)] * w + [pal.rgb[v] for v in s.pix]))))
    if s.sky_ok:
        still = [s.frame(t, 0) for t in range(TICS)]
        walk = [s.frame(t, SWAY * math.sin(2 * math.pi * t / TICS)) for t in range(TICS)]
        figs.append(('Standing still: mist drifts between the ridges', data_uri('gif', gif(w, h, still, pal))))
        figs.append(('Walking sway: near %d, mist %d, far %d texels' % (SWAY, round(SWAY * 0.6), round(SWAY * 0.35)),
                     data_uri('gif', gif(w, h, walk, pal))))

    def verdict(ok, yes, no):
        return '<span class="%s">%s</span>' % ('ok' if ok else 'no', yes if ok else no)
    stats = ('%s (edge contrast %.1f, need 4) · %s (edge contrast %.1f, need 3.4; band separation %.2f, '
             'shows in %d%% of columns)') % (
        verdict(s.sky_ok, 'skyline kept', 'no skyline: one layer'), s.sky_edge,
        verdict(s.far, 'far ridge kept', 'no far ridge'), s.far_edge, s.eta, 100 * s.visible)
    if s.sky_ok:
        stats += ' · mist rgb(%d, %d, %d)' % tuple(round(v) for v in s.mist)
    return '<h2>%s · %s · %dx%d</h2><p>%s</p><div class="grid">%s</div>' % (
        html.escape(s.name), html.escape(s.game), w, h, stats,
        ''.join('<figure><figcaption>%s</figcaption><img alt="" src="%s"></figure>' % (html.escape(c), u)
                for c, u in figs))


def main(argv):
    if len(argv) < 2:
        sys.exit(__doc__)
    parts = []
    for spec in argv[1:]:
        game = Game(spec)
        pal = Palette(game.palette)
        for name in sorted(n for n in game.textures if n.startswith('SKY')):
            start = time.time()
            pic = game.picture('W', name)
            if not pic:
                continue
            w, h, pix = pic
            s = Layers(name, game.label, w, h, [p if p >= 0 else 0 for p in pix], pal)
            parts.append(section(s))
            print('%s %s: skyline %s (%.1f), far ridge %s (%.1f) (%.1f s)' % (
                game.label, name, 'kept' if s.sky_ok else 'off', s.sky_edge, 'kept' if s.far else 'off',
                s.far_edge, time.time() - start), flush=True)
    page = ('<!doctype html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,'
            'initial-scale=1"><title>Sky Layers</title><style>%s</style></head><body><h1>Sky layers</h1>'
            '<p>Each sky split at runtime-style heuristics into sky, far ridge and near ridge; hidden parts filled '
            'from the sky\'s own columns; mist drifts between far and near ridge. GIFs run per tic (35 Hz), '
            'everything snapped to sky texels and the palette.</p>%s</body></html>') % (CSS, ''.join(parts))
    Path(argv[0]).write_text(page, encoding='utf-8')


if __name__ == '__main__':
    main(sys.argv[1:])
