"""The kneel poser's working parts (tests/kneel_pose.py): drawings with their hand-placed
skeletons, a leg pose per view, and the drawing re-posed with its leg texels following
their bones. The game is to do the same (PLAN: kneeling soldiers), so this stays plain.

Units as skeletons_stock.h: u right of the drawing's origin, h above its feet. A view is
one of Doom's eight rotations as the eye sees it: 6, 7 and 8 are id's drawings for 4, 3
and 2 flipped, their joints mirrored and their right and left swapped, so a pose always
names the body's own legs. A leg's pose: its thigh and its shin, each an angle from
straight down (degrees, positive toward the picture's right) and a share of the drawing's
own bone length (under 1: foreshortened, toward or away from the eye), and whether the shin lies
behind its own thigh (a kneeling shin seen from the front). Hips stay where
they were drawn; the whole picture then drops until its lowest texel is where its feet were.
"""
import math
import os
import re
import struct

from death_lab import Picture

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SKELETONS = os.path.join(ROOT, 'platform', 'skeletons_stock.h')
POSES = os.path.join(ROOT, 'platform', 'kneel_poses_stock.h')

HIP, KNEE, ANKLE = (9, 10), (11, 12), (13, 14)     # [body's right, left]
PAIRS = [(3, 4), (5, 6), (7, 8), (9, 10), (11, 12), (13, 14)]
TRUNK = [(0, 1), (1, 2), (3, 5), (4, 6), (5, 7), (6, 8)]   # bones a texel may belong to instead of a leg
STORED = {1: 1, 2: 2, 3: 3, 4: 4, 5: 5, 6: 4, 7: 3, 8: 2}  # the drawing each view shows


def palette_of(lumps):
    raw = lumps['PLAYPAL']
    return [(raw[3 * i], raw[3 * i + 1], raw[3 * i + 2]) for i in range(256)]


def read_skeletons():
    table = {}
    for m in re.finditer(r'\{"([A-Z0-9\[\]\\^]+)", (\d+), (\d+), (\d+)u, \{(.*?)\}\}', open(SKELETONS).read()):
        v = [float(x) for x in re.findall(r'-?\d+\.\d+', m.group(5))]
        table[m.group(1)] = [[v[2 * j], v[2 * j + 1]] for j in range(15)]
    return table


def lump_name(lumps, sprite, frame, rotation):
    """The lump holding a sprite's frame at a stored rotation (POSSA2A8 holds A2)."""
    want = f'{sprite}{frame}{rotation}'
    for name in lumps:
        if name.startswith(sprite) and (name[4:6] == want[4:6] or name[6:8] == want[4:6]):
            return name
    return None


class Drawing:
    """A frame as one view shows it: texels (palette index or -1), offsets, joints."""
    def __init__(self, lumps, skeletons, sprite, frame, view):
        stored = STORED[view]
        self.name = lump_name(lumps, sprite, frame, stored)
        if not self.name or self.name not in skeletons:
            raise KeyError(f'{sprite}{frame}{stored}')
        pic = Picture(lumps[self.name])
        self.width, self.height, self.left, self.top = pic.width, pic.height, pic.left, pic.top
        self.texels = [row[:] for row in pic.texels]
        self.joints = [j[:] for j in skeletons[self.name]]
        self.flipped = view >= 6
        if self.flipped:
            self.texels = [row[::-1] for row in self.texels]
            self.left = self.width - self.left
            self.joints = [[-u, h] for u, h in self.joints]
            for a, b in PAIRS:
                self.joints[a], self.joints[b] = self.joints[b], self.joints[a]
        self.label = f'{sprite}{frame}{view}'

    def at(self, c, r):
        return c - self.left + 0.5, self.top - r - 0.5

    def texel(self, u, h):
        c, r = int(math.floor(u + self.left)), int(math.floor(self.top - h))
        if 0 <= c < self.width and 0 <= r < self.height:
            return self.texels[r][c]
        return -1


def seg(p, a, b):
    """Distance from p to segment ab, and how far along it (0 at a, 1 at b)."""
    dx, dy = b[0] - a[0], b[1] - a[1]
    l2 = dx * dx + dy * dy
    t = 0.0 if l2 < 1e-6 else max(0.0, min(1.0, ((p[0] - a[0]) * dx + (p[1] - a[1]) * dy) / l2))
    return math.hypot(p[0] - a[0] - t * dx, p[1] - a[1] - t * dy), t


def leg_bones(joints):
    """Per leg: thigh (hip to knee) and shin (knee to ankle) as joint pairs."""
    return [[(HIP[s], KNEE[s]), (KNEE[s], ANKLE[s])] for s in (0, 1)]


HELD = 6.0   # how far past the hand what it holds reaches (a gun held low hangs over a thigh)


def held(j, a, b):
    """A trunk bone's far end; a forearm's reaches HELD units past the hand, so the gun stays with it."""
    if b not in (7, 8):
        return j[b]
    dx, dy = j[b][0] - j[a][0], j[b][1] - j[a][1]
    n = max(1e-3, math.hypot(dx, dy))
    return [j[b][0] + dx / n * HELD, j[b][1] + dy / n * HELD]


class Binding:
    """Which texels are legs, and of which bone: (leg, 0 thigh / 1 shin) per texel, None for the body.
    A leg texel lies under the higher hip and nearer a leg bone than any trunk or arm bone; the foot,
    past the ankle, goes with the shin."""
    def __init__(self, d):
        self.d = d
        j = d.joints
        top = max(j[HIP[0]][1], j[HIP[1]][1]) + 0.5
        legs = leg_bones(j)
        self.bone = [[None] * d.width for _ in range(d.height)]
        self.reach = {}                                  # (leg, part) -> (least t, most t) of its texels
        self.thick = {}                                  # (leg, part) -> its farthest texel from the bone
        for r in range(d.height):
            for c in range(d.width):
                if d.texels[r][c] < 0:
                    continue
                p = d.at(c, r)
                if p[1] > top:
                    continue
                best, which = 1e9, None
                for s in (0, 1):
                    for k, (a, b) in enumerate(legs[s]):
                        dist, _ = seg(p, j[a], j[b])
                        if dist < best:
                            best, which = dist, (s, k)
                for a, b in TRUNK:
                    if seg(p, j[a], held(j, a, b))[0] < best - 0.5:
                        which = None
                        break
                if which is None:
                    continue
                self.bone[r][c] = which
                a, b = legs[which[0]][which[1]]
                t = along(p, j[a], j[b])
                lo, hi = self.reach.get(which, (t, t))
                self.reach[which] = (min(lo, t), max(hi, t))
                self.thick[which] = max(self.thick.get(which, 0), best)


def along(p, a, b):
    dx, dy = b[0] - a[0], b[1] - a[1]
    l2 = max(1e-6, dx * dx + dy * dy)
    return ((p[0] - a[0]) * dx + (p[1] - a[1]) * dy) / l2


def direction(angle):
    a = math.radians(angle)
    return math.sin(a), -math.cos(a)


def angle_of(dx, dy):
    return math.degrees(math.atan2(dx, -dy))


def posed_joints(d, pose):
    """The legs' joints as posed (hips where drawn), before the drop."""
    j = [p[:] for p in d.joints]
    for s in (0, 1):
        thigh, ts, shin, ss = pose['legs'][s][:4]
        hip, knee, ankle = j[HIP[s]], d.joints[KNEE[s]], d.joints[ANKLE[s]]
        lt = math.hypot(knee[0] - hip[0], knee[1] - hip[1])
        ls = math.hypot(ankle[0] - knee[0], ankle[1] - knee[1])
        dx, dy = direction(thigh)
        j[KNEE[s]] = [hip[0] + dx * lt * ts, hip[1] + dy * lt * ts]
        dx, dy = direction(shin)
        j[ANKLE[s]] = [j[KNEE[s]][0] + dx * ls * ss, j[KNEE[s]][1] + dy * ls * ss]
    return j


GAP = 2   # the widest gap closed across, in texels


def close_gaps(out, first):
    """Pinholes the posing tears in the legs (a bend's outer side, a bone's end): from row first down, an
    empty texel with drawn ones within GAP either side across, or straight above and below, takes the
    nearer (the left, the upper on a tie). Wider gaps are the space between the legs, and stay."""
    H, W = len(out), len(out[0])
    for horizontal in (True, False):
        fills = []
        for r in range(max(0, first), H):
            for c in range(W):
                if out[r][c] >= 0:
                    continue
                near = []
                for side in (-1, 1):
                    for k in range(1, GAP + 1):
                        rr, cc = (r, c + side * k) if horizontal else (r + side * k, c)
                        if 0 <= rr < H and 0 <= cc < W and out[rr][cc] >= 0:
                            near.append((k, out[rr][cc]))
                            break
                if len(near) == 2 and near[0][0] + near[1][0] <= GAP + 1:
                    fills.append((r, c, min(near, key=lambda n: n[0])[1]))
        for r, c, t in fills:
            out[r][c] = t


def pose_drawing(binding, pose, reach_out=0.12):
    """The drawing re-posed: (texels, joints, drop). texels as the drawing's own grid, grown by a margin
    (margin texels each side and below); joints posed and dropped; drop in units."""
    d = binding.d
    j0, j1 = d.joints, posed_joints(d, pose)
    m = 16
    W, H = d.width + 2 * m, d.height + m
    out = [[-1] * W for _ in range(H)]
    legs0, legs1 = leg_bones(j0), leg_bones(j1)
    front = pose['front']
    # Each bone's frame, before and after: start, unit way, normal, length.
    frames = []
    for s in (1 - front, front):
        for k in ((1, 0) if pose['legs'][s][4] else (0, 1)):
            if (s, k) not in binding.reach:
                continue
            (a0, b0), (a1, b1) = legs0[s][k], legs1[s][k]
            p0, q0, p1, q1 = j0[a0], j0[b0], j1[a1], j1[b1]
            l0 = max(1e-3, math.hypot(q0[0] - p0[0], q0[1] - p0[1]))
            real = math.hypot(q1[0] - p1[0], q1[1] - p1[1])
            l1 = max(0.3, real)
            w0 = ((q0[0] - p0[0]) / l0, (q0[1] - p0[1]) / l0)
            if real > 1e-3:
                w1 = ((q1[0] - p1[0]) / real, (q1[1] - p1[1]) / real)
            else:
                dx, dy = direction(pose['legs'][s][2 * k])
                w1 = (dx, dy)
            lo, hi = binding.reach[(s, k)]
            frames.append(((s, k), p0, w0, l0, p1, w1, l1, lo, hi))
    left = d.left + m

    def grid(c, r):
        return c - left + 0.5, d.top - r - 0.5
    # Legs, back leg first: each destination texel asks each bone where on the drawing it came from.
    for (key, p0, w0, l0, p1, w1, l1, lo, hi) in frames:
        span = max(1e-3, hi - lo)
        # Only around where the bone now lies: its ends, reached a little past, and its thickness.
        ends = [(p1[0] + w1[0] * l1 * t, p1[1] + w1[1] * l1 * t) for t in (lo - reach_out * span, hi + reach_out * span)]
        thick = binding.thick.get(key, 8) + 1
        c0 = max(0, int(min(e[0] for e in ends) - thick + left))
        c1 = min(W, int(max(e[0] for e in ends) + thick + left) + 2)
        r0 = max(0, int(d.top - max(e[1] for e in ends) - thick))
        r1 = min(H, int(d.top - min(e[1] for e in ends) + thick) + 2)
        for r in range(r0, r1):
            for c in range(c0, c1):
                u, h = grid(c, r)
                du, dh = u - p1[0], h - p1[1]
                t = (du * w1[0] + dh * w1[1]) / l1
                if t < lo - reach_out * span or t > hi + reach_out * span:
                    continue
                across = du * -w1[1] + dh * w1[0]
                tt = min(hi, max(lo, t))
                su = p0[0] + w0[0] * tt * l0 + -w0[1] * across
                sh = p0[1] + w0[1] * tt * l0 + w0[0] * across
                sc, sr = int(math.floor(su + d.left)), int(math.floor(d.top - sh))
                if 0 <= sc < d.width and 0 <= sr < d.height and binding.bone[sr][sc] == key:
                    out[r][c] = d.texels[sr][sc]
    # The body over them, as drawn.
    for r in range(d.height):
        for c in range(d.width):
            if d.texels[r][c] >= 0 and binding.bone[r][c] is None:
                out[r][c + m] = d.texels[r][c]
    close_gaps(out, int(math.ceil(d.top - max(j0[HIP[0]][1], j0[HIP[1]][1]) - 3)))
    # Dropped until the lowest texel is where the drawing's lowest was.
    low0 = min((d.top - r - 1 for r in range(d.height) if any(t >= 0 for t in d.texels[r])), default=0)
    rows = [r for r in range(H) if any(t >= 0 for t in out[r])]
    low1 = d.top - rows[-1] - 1 if rows else low0
    drop = int(round(low1 - low0))
    if drop > 0:
        out = [[-1] * W for _ in range(drop)] + out[:H - drop]
    joints = [[u, h - drop] for u, h in j1]
    return out, joints, drop, left


def squashed(d, squash=0.6):
    """Today's kneel (body_motion.cpp's bodyQuads): rows under a hip 45% up kept at 1 - squash, the
    upper body whole on top, rows under the origin as drawn. (texels, its top)."""
    hip = max(4, round(d.top * 0.45))
    lower = 1 - squash
    keep = int(hip * lower)
    top = keep + d.top - hip
    out = []
    for k in range(top):
        at = top - k - 0.5                       # the row's middle, above the feet
        u = hip + (at - keep) if at >= keep else at / lower
        out.append(d.texels[min(d.height - 1, max(0, int(d.top - u)))][:])
    return out + [row[:] for row in d.texels[d.top:]], top


# Seeds: one kneel in 3D, seen from each view. Body space: x the body's right, y forward, z up; each
# bone a way (its length is the drawing's). The right knee on the floor under the hip, its shin back and
# rising to the toes (level, the foot turned with it hung under the shin and held the hips up in the side
# views); the left thigh forward and rising a little to the knee, its shin back under it to the planted
# foot (the user's drawing, from the front: the knee down, the other thigh coming at the eye). Id's legs
# have shins longer than thighs: with that thigh level the planted foot held the hips up and the kneeling
# knee hung short of the floor. Both thighs close to straight on: swung out, the legs came apart from the
# front, a hole under the belt (the user).
KNEEL_3D = {
    'legs': [[(0.12, 0.15, -1.0), (0.0, -1.0, 0.4)],       # right: thigh, shin
             [(-0.12, 1.0, 0.15), (-0.05, -0.3, -1.0)]],    # left
    'hips': [(4.0, 0.0), (-4.0, 0.0)],                       # x, y of each hip
}


def view_pose(view, kneel=KNEEL_3D):
    """A 3D kneel as one of Doom's views draws it. View 1 faces the eye, 3 faces the picture's left (as id
    drew POSSA3A7), 5 shows the back. On screen: u right, w toward the eye."""
    phi = math.radians((view - 1) * 45)
    fwd = (-math.sin(phi), math.cos(phi))
    right = (-math.cos(phi), -math.sin(phi))

    def screen(x, y):
        return x * right[0] + y * fwd[0], x * right[1] + y * fwd[1]
    legs, depth = [], []
    for s in (0, 1):
        leg, near, mids = [], 0.0, []
        hx, hy = kneel['hips'][s]
        px, py = hx, hy
        for k, (x, y, z) in enumerate(kneel['legs'][s]):
            n = math.sqrt(x * x + y * y + z * z)
            x, y, z = x / n, y / n, z / n
            u, w = screen(x, y)
            leg += [round(angle_of(u, z), 1), round(max(0.08, math.hypot(u, z)), 3)]
            mids.append(screen(px + x * 5, py + y * 5)[1])
            px, py = px + x * 10, py + y * 10
            near += screen(px, py)[1]
        leg.append(1 if mids[1] < mids[0] - 0.5 else 0)    # the shin farther from the eye than the thigh
        legs.append(leg)
        depth.append(near)
    return {'legs': legs, 'front': 0 if depth[0] > depth[1] else 1}


def seed_poses():
    return {v: view_pose(v) for v in range(1, 9)}


def read_poses():
    if not os.path.exists(POSES):
        return seed_poses()
    poses = {}
    for m in re.finditer(r'\{\{\{(.*?)\}, \{(.*?)\}\}, (\d)\}, // view (\d)', open(POSES).read()):
        legs = [[float(x) for x in re.findall(r'-?\d+(?:\.\d+)?', m.group(k))] for k in (1, 2)]
        poses[int(m.group(4))] = {'legs': legs, 'front': int(m.group(3))}
    return poses if len(poses) == 8 else seed_poses()


def write_poses(poses):
    def leg(p):
        return '{%.1ff, %.3ff, %.1ff, %.3ff, %d}' % tuple(p)
    rows = ''.join('    {{%s, %s}, %d}, // view %d\n' % (leg(poses[v]['legs'][0]), leg(poses[v]['legs'][1]),
                                                       poses[v]['front'], v) for v in range(1, 9))
    text = f'''/* Kneeling legs, posed by hand in tests/kneel_pose.py, which writes this file. Per view (Doom's
   rotations 1-8 as the eye sees them: 6-8 are the drawings for 4-2 flipped, their right and left
   swapped, so a pose always names the body's own legs), per leg (the body's right, then its left):
   the thigh and then the shin, each as an angle from straight down (degrees, positive toward the
   picture's right) and a share of the drawing's own bone length (skeletons_stock.h's hip, knee and
   ankle), then 1 where the shin lies behind its own thigh. The hips stay where they were drawn, the legs' texels follow their bones, and the picture
   drops until its lowest texel is where the drawing's lowest was. front: the leg drawn over the other. */
#ifndef DOOM_KNEEL_POSES_STOCK_H
#define DOOM_KNEEL_POSES_STOCK_H

struct doom_kneel_leg {{ float thigh, thighShare, shin, shinShare; int shinBehind; }};
struct doom_kneel_view {{ doom_kneel_leg leg[2]; int front; }};
static const doom_kneel_view doom_kneel_views[8] = {{
{rows}}};

#endif
'''
    open(POSES, 'w', encoding='utf-8', newline='\n').write(text)


def read_bmp(path):
    """An 8-bit BMP (the user's drawings) as rows of RGB or None, its corner colour clear."""
    data = open(path, 'rb').read()
    start, = struct.unpack_from('<I', data, 10)
    size, = struct.unpack_from('<I', data, 14)
    w, h = struct.unpack_from('<ii', data, 18)
    bpp, = struct.unpack_from('<H', data, 28)
    colours, = struct.unpack_from('<I', data, 46)
    table = data[14 + size:14 + size + 4 * (colours or 256)]
    stride = (w * bpp // 8 + 3) & ~3
    rows = []
    for k in range(abs(h)):
        y = abs(h) - 1 - k if h > 0 else k
        rows.append([data[start + y * stride + x] for x in range(w)])
    clear = rows[0][0]
    return [[None if v == clear else (table[4 * v + 2], table[4 * v + 1], table[4 * v]) for v in row] for row in rows]
