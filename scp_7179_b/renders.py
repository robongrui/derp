#!/usr/bin/env python3
"""Episode 0.5's render guard: fixed views of the library, rendered by the
game itself, checked for holes and compared with reference pictures, so
renderer work can't quietly ruin it.

    py -I scp_7179_b/renders.py --iwad D:/dev/DOOM-gamedata/gog_doom/doom2.wad
        [--game "build/play/Release/Play DERP.exe"] [--only across,fall] [--accept [views]]

Each view is rendered twice, hidden (-rendercheck), from the library.wad
beside the game: once as it is, for the look, and once with the far fog
magenta (a DERPINFO on top), where whatever isn't drawn shows the backdrop.
Magenta where the reference had none is a hole: that fails. A look that
differs from the reference is reported for a person to judge, and taken as
the new reference with --accept (all views, or those named). The first run
takes the references. Pictures, references and a page showing them side by
side go to output/library-renders/ (never committed: they show DOOM II's
art). Your own graphics.cfg and doom.cfg are never read or written; F4's
defaults are used, with the FPS readout off.
"""
import argparse
import concurrent.futures
import functools
import html
import operator
import os
import shutil
import struct
import subprocess
import sys
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, "mapgen"))
import scenarios  # noqa: E402  (the places and the demos)
import library  # noqa: E402
from doommap import wad  # noqa: E402

L = library.L
OUT = os.path.join(ROOT, "output", "library-renders")
READER = ["-libraryreader", "A/1990/B/C/Leo/none/D"]   # past the questions and the welcome
EMPTY = ["-libraryempty"]
RAIL_EYE = library.NEAR_RAIL - 30                         # at the railing, on the walkway
RAIL_CLOSE = library.NEAR_RAIL - 10                       # close enough to look over it, and past the overhang
AISLE_X = L["LANDING"] + 5 * L["GALLERY"] + 128           # in an aisle, as the book scenario

HOLE_GRACE = 2          # pixels a reference's magenta may have moved
HOLE_PIXELS = 12        # new magenta pixels that make a hole
# Renders come out the same to the bit, run after run; palette mode, a
# subtle change in these dark views, moves 7-17% of the pixels by 8 or more.
LOOK_STEP = 8           # a channel's change that counts
LOOK_SHARE = 0.002      # changed pixels that make a look worth a person's eye


def still(x, y, angle, pitch, people=False, down=5):
    """A view placed with -renderview, five floors down (the floors above drawn as copies)."""
    return (["-warp", "0", "-librarydown", str(down)] + READER + ([] if people else EMPTY)
            + ["-renderview", str(x), str(y), str(angle), str(pitch)], None)


def played(name):
    """A view from one of the scenarios' demos: its place and its input."""
    for scenario in scenarios.scenarios():
        if scenario[0] == name:
            return list(scenario[3]), scenario[2]
    raise KeyError(name)


# name: what it guards, (arguments, demo), the tic it is taken at
VIEWS = [
    ("landing", "where the chasm meets a landing: the holes found 2026-10-29", still(900, 260, 180, 12), 20),
    ("across", "the far side, its storeys above and below, the people on them", still(scenarios.MIDDLE_X, RAIL_EYE, 90, 0, True), 20),
    ("down", "the far side's storeys below, over the railing", still(scenarios.MIDDLE_X, RAIL_CLOSE, 90, -35), 20),
    ("up", "the far side's storeys above, past the overhang", still(scenarios.MIDDLE_X, RAIL_CLOSE, 90, 35), 20),
    ("walkway", "round the ring into the far fog", still(scenarios.MIDDLE_X, scenarios.NEAR_WALK, 0, 0), 20),
    ("aisle", "the stacks' spines and the back wall", still(AISLE_X, 100, 0, 0), 20),
    ("door", "the stairwell door, shut", still(256, 220, 270, 0), 20),
    # Floor 19 (seed DERP) has someone asleep in the west landing's bed by the mirror.
    ("mirror", "the wall mirror by the beds, him in it; a sleeper in the bed, no blood", still(130, 170, 210, -8, True, 18), 30),
    ("fall", "mid-fall: the rush, the storeys streaming past", played("fall"), 70),
    # Placed in the chasm's middle it falls at once; at tic 90 at full speed, looking down.
    ("shaft", "falling, looking down the shaft: no lit floor where the copied storeys end", still(5000, 512, 90, -65), 90),
    ("book", "an open book: the page panel", played("book"), 16),   # open from tic 5 to 19
]


# PNGs, with the standard library: 8-bit RGB or RGBA, not interlaced.
def read_png(path):
    data = open(path, "rb").read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError(path + ": not a PNG")
    at, chunks, width = 8, [], 0
    while at < len(data):
        length, kind = struct.unpack(">I4s", data[at:at + 8])
        body = data[at + 8:at + 8 + length]
        if kind == b"IHDR":
            width, height, depth, colour, _, _, interlace = struct.unpack(">IIBBBBB", body)
            if depth != 8 or colour not in (2, 6) or interlace:
                raise ValueError(path + ": only 8-bit RGB(A), not interlaced")
            channels = 4 if colour == 6 else 3
        elif kind == b"IDAT":
            chunks.append(body)
        at += 12 + length
    raw = zlib.decompress(b"".join(chunks))
    stride = width * channels
    rows, previous = [], bytearray(stride)
    for y in range(height):
        start = y * (stride + 1)
        kind, line = raw[start], bytearray(raw[start + 1:start + 1 + stride])
        if kind == 1:
            for i in range(channels, stride):
                line[i] = (line[i] + line[i - channels]) & 255
        elif kind == 2:
            line = bytearray((a + b) & 255 for a, b in zip(line, previous))
        elif kind == 3:
            for i in range(stride):
                line[i] = (line[i] + ((line[i - channels] if i >= channels else 0) + previous[i]) // 2) & 255
        elif kind == 4:
            for i in range(stride):
                a = line[i - channels] if i >= channels else 0
                b, c = previous[i], previous[i - channels] if i >= channels else 0
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        if channels == 4:
            rgb = bytearray(width * 3)
            for c in range(3):
                rgb[c::3] = line[c::4]
            rows.append(bytes(rgb))
        else:
            rows.append(bytes(line))
        previous = line
    return width, height, rows


def write_png(path, width, height, rows):
    def chunk(kind, body):
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body) & 0xffffffff)
    raw = b"".join(b"\0" + row for row in rows)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
                + chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b""))


def magenta(rows):
    """Each row's pure magenta pixels, as the bits of an int."""
    out = []
    for row in rows:
        bits = 0
        for x in range(0, len(row), 3):
            if row[x] >= 224 and row[x + 1] <= 40 and row[x + 2] >= 224:
                bits |= 1 << (x // 3)
        out.append(bits)
    return out


def grown(mask, by):
    wide = []
    for bits in mask:
        w = bits
        for d in range(1, by + 1):
            w |= bits << d | bits >> d
        wide.append(w)
    return [functools.reduce(operator.or_, wide[max(0, y - by):y + by + 1], 0) for y in range(len(wide))]


def marked(rows, width, bits_of_row, colour):
    """The picture dimmed, the marked pixels in colour."""
    out = []
    for row, bits in zip(rows, bits_of_row):
        line = bytearray(b // 3 for b in row)
        x = 0
        while bits:
            if bits & 1:
                line[3 * x:3 * x + 3] = colour
            bits >>= 1
            x += 1
        out.append(bytes(line))
    return out


def holes(reference, latest):
    """New magenta in the latest render (past the reference's own, grown a little), per row."""
    allowed = grown(magenta(reference[2]), HOLE_GRACE)
    return [bits & ~room for bits, room in zip(magenta(latest[2]), allowed)]


def changed(reference, latest):
    """Pixels whose colour moved by LOOK_STEP or more in a channel, per row."""
    out = []
    for a, b in zip(reference[2], latest[2]):
        bits = 0
        if a != b:
            for x in range(0, len(a), 3):
                if abs(a[x] - b[x]) >= LOOK_STEP or abs(a[x + 1] - b[x + 1]) >= LOOK_STEP or abs(a[x + 2] - b[x + 2]) >= LOOK_STEP:
                    bits |= 1 << (x // 3)
        out.append(bits)
    return out


def count(mask):
    return sum(bin(bits).count("1") for bits in mask)


def render(game, iwad, work, name, extra, data, tic, holes_wad, png):
    """One frame of a view, hidden; the game's output is kept beside it."""
    graphics, config = os.path.join(work, "graphics.cfg"), os.path.join(work, "doom.cfg")
    lumps = []
    if holes_wad:
        lumps.append(("DERPINFO", ("map E0M1\n{\n    library = true\n    farfog = %d, %d\n    farfogcolor = FF00FF\n}\n"
                                   % library.FAR_FOG).encode()))
    if data:
        lumps.append(("RENDEMO", data))
    command = [game, "-iwad", iwad]
    if lumps:
        pwad = os.path.join(work, "%s%s.wad" % (name, "-holes" if holes_wad else ""))
        with open(pwad, "wb") as f:
            f.write(wad([], lumps))
        command += ["-file", pwad]
    if data:
        command += ["-timedemo", "RENDEMO"]   # a tic a frame: the frame is taken at its tic exactly
    command += ["-libraryseed", "DERP"] + extra
    command += ["-rendercheck", os.path.abspath(png), "-rendertic", str(tic),
                "-graphics", graphics, "-config", config, "-nosound"]
    if os.path.exists(png):
        os.remove(png)
    environment = dict(os.environ, SDL_AUDIODRIVER="dummy")
    result = subprocess.run(command, capture_output=True, text=True, timeout=300, cwd=work, env=environment)
    with open(png + ".log", "w") as f:
        f.write(result.stdout + result.stderr)
    return os.path.exists(png)


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--iwad", required=True, help="DOOM II")
    parser.add_argument("--game", default=os.path.join(ROOT, "build", "play", "Release", "Play DERP.exe"))
    parser.add_argument("--only", help="views, comma separated: " + ", ".join(v[0] for v in VIEWS))
    parser.add_argument("--accept", nargs="?", const="all", help="take the latest renders as the references (all, or views named)")
    parser.add_argument("--jobs", type=int, default=3, help="renders at once")
    args = parser.parse_args()

    only = set(args.only.split(",")) if args.only else None
    views = [v for v in VIEWS if not only or v[0] in only]
    latest, reference = os.path.join(OUT, "latest"), os.path.join(OUT, "reference")
    for folder in (latest, reference):
        os.makedirs(folder, exist_ok=True)
    # F4's defaults, the FPS readout off; DOOM's settings from the smoke tests, never yours.
    with open(os.path.join(latest, "graphics.cfg"), "w") as f:
        f.write("fps 0\n")
    config = os.path.join(latest, "doom.cfg")
    smoke = os.path.join(ROOT, "build", "play", "smoke-data", "smoke.cfg")
    if not os.path.exists(config) and os.path.exists(smoke):
        shutil.copy(smoke, config)

    jobs = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, args.jobs)) as pool:
        for name, _, (extra, data), tic in views:
            for suffix, holes_wad in (("", False), ("-holes", True)):
                png = os.path.join(latest, name + suffix + ".png")
                jobs.append((name + suffix, pool.submit(render, os.path.abspath(args.game), os.path.abspath(args.iwad),
                                                        latest, name, extra, data, tic, holes_wad, png)))
    rendered = {key: job.result() for key, job in jobs}

    accept = None if not args.accept else set(v[0] for v in views) if args.accept == "all" else set(args.accept.split(","))
    failed, rows = 0, []
    for name, guards, _, _ in views:
        verdict, notes = "ok", []
        pictures = [name + ".png", name + "-holes.png"]
        if not all(rendered[n[:-4]] for n in pictures):
            verdict, notes = "FAILED", ["didn't render: see %s.png.log" % name]
        else:
            for picture in pictures:
                if accept and name in accept or not os.path.exists(os.path.join(reference, picture)):
                    shutil.copy(os.path.join(latest, picture), os.path.join(reference, picture))
                    notes.append("%s taken as the reference" % picture)
            ref_look, new_look = (read_png(os.path.join(folder, name + ".png")) for folder in (reference, latest))
            ref_holes, new_holes = (read_png(os.path.join(folder, name + "-holes.png")) for folder in (reference, latest))
            if ref_look[:2] != new_look[:2] or ref_holes[:2] != new_holes[:2]:
                verdict = "FAILED"
                notes.append("size %dx%d, the reference %dx%d: retake with --accept" % (new_look[0], new_look[1], ref_look[0], ref_look[1]))
            else:
                new = holes(ref_holes, new_holes)
                n = count(new)
                if n > HOLE_PIXELS:
                    verdict = "FAILED"
                    notes.append("HOLES: %d pixels show the backdrop" % n)
                    write_png(os.path.join(latest, name + "-holes-marked.png"), new_holes[0], new_holes[1],
                              marked(new_holes[2], new_holes[0], new, b"\x00\xff\x00"))
                moved = changed(ref_look, new_look)
                share = count(moved) / float(new_look[0] * new_look[1])
                if share > LOOK_SHARE:
                    verdict = "CHANGED" if verdict == "ok" else verdict
                    notes.append("look: %.1f%% of the pixels changed" % (100 * share))
                    write_png(os.path.join(latest, name + "-changed.png"), new_look[0], new_look[1],
                              marked(new_look[2], new_look[0], moved, b"\xff\x40\x40"))
        failed += verdict == "FAILED"
        print("%-9s %-8s %s" % (name, verdict, "; ".join(notes)))
        rows.append((name, guards, verdict, notes))
    page(rows)
    print("pictures and a page: %s" % os.path.join(OUT, "index.html"))
    return 1 if failed else 0


def page(rows):
    """Reference, latest and what changed, side by side."""
    parts = ["<!doctype html><meta charset=utf-8><title>Library renders</title>",
             "<style>body{background:#111;color:#ddd;font:14px sans-serif;margin:16px}img{width:32%;margin-right:1%}"
             "h2{font-size:16px;margin:24px 0 4px}.ok{color:#8c8}.CHANGED{color:#ec6}.FAILED{color:#f66}</style>"]
    for name, guards, verdict, notes in rows:
        parts.append("<h2>%s <span class=%s>%s</span></h2><div>%s</div><div>%s</div>"
                     % (name, verdict, verdict, html.escape(guards), html.escape("; ".join(notes))))
        for suffix, marks in (("", "-changed"), ("-holes", "-holes-marked")):
            images = ["reference/%s%s.png" % (name, suffix), "latest/%s%s.png" % (name, suffix)]
            if os.path.exists(os.path.join(OUT, "latest", name + marks + ".png")):
                images.append("latest/%s%s.png" % (name, marks))
            parts.append("<div>%s</div>" % "".join('<img src="%s">' % i for i in images))
    with open(os.path.join(OUT, "index.html"), "w", encoding="utf-8") as f:
        f.write("\n".join(parts))


if __name__ == "__main__":
    sys.exit(main())
