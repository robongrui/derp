#!/usr/bin/env python3
"""Plays episode 0.5's library headless and checks what happens: walking
round a floor's ring, the stairs, the fall, the people, the day. Each scenario is a demo written here (DOOM's own input format),
played by doom-headless with -librarylog, and judged by that log.

    python3 scp_7179_b/scenarios.py --headless build/doom-headless \\
        --iwad /path/to/freedoom1.wad [--only stairs,fall] [--keep dir]

Seed 7Q3K (--seed for another; some scenarios pin their own) and the map as mapgen/library.py builds it; some scenarios start
somewhere particular in the built part (-libraryplace). The checks are about
what must happen, not exact numbers, so the layout and the tuning can move.
"""
import argparse
import os
import re
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "mapgen"))
import library  # noqa: E402
from doommap import wad  # noqa: E402

L = library.L
SEED = "7Q3K"
QUIET = ["-libraryseed", "LIBR"]   # a seed whose start has no gang near (who is where comes from the floor key)
LANDING_MIDDLE = L["LANDING"] // 2                       # the west end's landing
STAIRS_Y = -L["WALL"] - L["STAIRWELL"] // 2
KIOSK_X = LANDING_MIDDLE + L["PLATFORM"] // 2 + L["KIOSK_FROM"] + L["KIOSK_WIDTH"] // 2
BED_X = library.CORNICE + L["BED_INSET"] + L["BED_WIDTH"] // 2
NEAR_WALK = library.STACKS + library.WALKWAY // 2           # the near walkway's middle, across
MIDDLE_X = L["LANDING"] + L["SIDE"] // 2                    # halfway along a side


def tic(forward=0, side=0, turn=0, buttons=0):
    return struct.pack("bbBB", forward, side, turn, buttons)


def demo(*parts):
    """A DOOM 1.10 demo of skill 2, episode 0 (the library, E0M1), one player."""
    return bytes([110, 2, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0]) + b"".join(parts) + b"\x80"


def run_back(n):
    # DOOM's movement drifts a little sideways; a nudge now and then keeps
    # a long run off the lamps, as a player would.
    return b"".join(tic(-50, -20 if i % 1000 == 999 else 0) for i in range(n))


def run_on(n):
    return b"".join(tic(50, 20 if i % 1000 == 999 else 0) for i in range(n))


def galleries(log, what="gallery"):
    return [int(m) for m in re.findall(r"^library: %s: .*? / gallery ([-+]?\d+) " % what, log, re.M)]


def ring(log):
    """The galleries round the ring he was in, in order (0-49 near, 50-99 far)."""
    return [int(m) for m in re.findall(r"ring gallery (\d+)\)$", log, re.M)]


def floors(log, what):
    return [int(m) for m in re.findall(r"^library: %s: .*? / floor ([-+]?\d+) " % what, log, re.M)]


def counts(log, key):
    return [int(m) for m in re.findall(r"(\d+) %s" % key, log)]


# Each: name, map ("plain" or "people"), demo, extra arguments, check(log)
# returning a list of what went wrong.
def scenarios():
    s = []

    def walk_check(log):
        g = ring(log)
        if not g or max(g) - g[0] < 15 or g[0] - min(g) < 15:
            return ["expected to run 15+ galleries each way, got %s..%s" % (min(g or [0]), max(g or [0]))]
        return []
    s.append(("walk", "plain", demo(*[tic(50)] * 700, *[tic(-50)] * 1400),
              ["-libraryempty", "-libraryplace", str(MIDDLE_X), str(NEAR_WALK), "0"], walk_check))

    def peaceful_check(log):
        bad = []
        if not counts(log, "of them people") or max(counts(log, "of them people")) < 10:
            bad.append("the built part should have its people")
        # His health (a sleeper's own lines carry the sleeper's).
        mine = "\n".join(l for l in log.splitlines() if "in a bed:" not in l)
        if any(h != 100 for h in counts(mine, "health")):
            bad.append("someone hurt him: the people should be indifferent")
        return bad
    s.append(("peaceful", "plain", demo(*[tic(50)] * 700, *[tic(-50)] * 1400), QUIET, peaceful_check))

    # All the way round the ring: east along the near side, across the east
    # landing, back west along the far side, across the west landing, and
    # on along the near side again.
    def around_check(log):
        g = ring(log)
        bad = []
        if not g or not {0, 49, 50, 99} <= set(g):
            bad.append("expected to pass both ends of both sides, got %s" % sorted(set(g))[:8])
        if g and g[-1] >= 50:
            bad.append("he should have come back round to the near side, ended at gallery %d" % g[-1])
        return bad
    left = tic(turn=0x40)   # a quarter turn; 31 tics of running cross a landing to the far walkway's middle
    s.append(("around", "plain", demo(run_on(70), left, *[tic(50)] * 31, left, run_on(1750), left,
                                      *[tic(50)] * 31, left, run_on(300)),
              ["-libraryday", "60", "-libraryempty", "-libraryplace", str(L["LANDING"] + L["SIDE"] - 256), str(NEAR_WALK), "0"],
              around_check))

    def stairs_check(log):
        f = floors(log, "stairs")
        bad = []
        if len(f) < 20 or max(f) < 11 or f[-1] != 1:
            bad.append("expected 10+ floors down from floor 1, then back up to it and no higher, got %s" % f[:5])
        steps = [b - a for a, b in zip(f, f[1:])]
        if any(abs(d) != 1 for d in steps):
            bad.append("the floor count should move one at a time")
        return bad
    # Floor 1 is the top: down the flight (west), then back up (east) past it.
    s.append(("stairs", "plain", demo(*[tic(50)] * 600, tic(turn=0x80), *[tic(50)] * 1200),
              ["-libraryplace", str(LANDING_MIDDLE), str(STAIRS_Y), "180"], stairs_check))

    def fall_check(log):
        bad = []
        if "library: over the railing" not in log:
            bad.append("use at the railing should put him over it")
        f = floors(log, "falling")
        if len(f) < 10:
            bad.append("he should fall past 10+ floors, got %d" % len(f))
        w = floors(log, "woke")
        if not w or (f and w[0] < f[-1]):
            bad.append("steering to the edge should catch a floor below")
        return bad
    s.append(("fall", "plain", demo(tic(buttons=2), *[tic()] * 140, *[tic(-50)] * 100, *[tic()] * 100),
              ["-libraryplace", "5000", str(library.NEAR_RAIL - 30), "90"], fall_check))

    # Woken on a floor below, he waits: within a minute someone falls past
    # (from floor 1 nobody could: nothing is above it).
    def fallers_check(log):
        bad = fall_check(log)
        woke = log.find("library: woke")
        if woke < 0 or "library: someone fell past" not in log[woke:]:
            bad.append("someone should fall past within a minute on a floor below the top")
        return bad
    s.append(("fallers", "plain", demo(tic(buttons=2), *[tic()] * 140, *[tic(-50)] * 100, *[tic()] * 2300),
              ["-libraryempty", "-libraryplace", "5000", str(library.NEAR_RAIL - 30), "90"], fallers_check))

    def revisit_check(log):
        lines = re.findall(r"^library: (start|stairs): .*\((\d+) living near.*?(\d+) bodies, (\d+) dead remembered", log, re.M)
        bad = []
        if len(lines) != 3:
            return ["expected start, down and up again; got %d lines" % len(lines)]
        (_, l0, _b0, _), (_, _l1, b1, d1), (_, l2, b2, _) = lines
        if int(d1) < 1:
            bad.append("shooting from the landing should kill someone")
        if int(b1) != 0:
            bad.append("a floor down, the old floor's bodies should be gone")
        if int(b2) != int(d1) or int(l2) != int(l0) - int(d1):
            bad.append("back up, the dead should lie there and the rest be as before")
        return bad
    # Out through the stairwell's door (use, then wait for it), shoot along
    # the landing, back through the door, down a floor and up again.
    s.append(("revisit", "plain", demo(*[tic()] * 5, tic(buttons=2), *[tic()] * 35, *[tic(50)] * 15, *[tic()] * 15, tic(turn=0xC0), *[tic(buttons=1)] * 350,
                                       tic(turn=0xC0), *[tic()] * 10, *[tic(50)] * 11, *[tic()] * 10, tic(buttons=2), *[tic()] * 35,
                                       *[tic(50)] * 8, *[tic()] * 10,
                                       tic(turn=0xC0), *[tic(25)] * 60, *[tic()] * 10, tic(turn=0x80), *[tic(25)] * 60, *[tic()] * 35),
              ["-libraryseed", "QWER", "-libraryplace", str(LANDING_MIDDLE), str(STAIRS_Y), "90"], revisit_check))   # QWER: someone in the line of fire

    day = ["-libraryday", "1"]

    def has(*events):
        return lambda log: ["missing: %s" % e for e in events if "library: %s" % e not in log]
    # Standing on the walkway a few minutes: one of those at the railing he
    # can see climbs over (floor 1: nobody falls from above).
    s.append(("jumper", "plain", demo(*[tic()] * 6000), QUIET + ["-libraryday", "60", "-libraryplace", str(MIDDLE_X), str(NEAR_WALK), "90"],
              lambda log: [] if re.search(r"someone jumped.*gone below", log, re.S) else ["nobody at the railing jumped and fell away in three minutes"]))
    s.append(("pass-out", "plain", demo(*[tic()] * 2700), day + QUIET, has("yawn", "passed out", "morning")))
    s.append(("bed", "plain", demo(*[tic()] * 1400, tic(buttons=2), *[tic()] * 300), day + ["-libraryplace", str(BED_X), "48", "0"],
              has("to bed", "morning")))

    def too_early(log):
        return ["a bed shouldn't take him before he is tired"] if "library: to bed" in log else []
    s.append(("bed-too-early", "plain", demo(*[tic()] * 10, tic(buttons=2), *[tic()] * 10), day + ["-libraryplace", str(BED_X), "48", "0"], too_early))
    # Use at the kiosk, then something taken from its menu (m_kiosk.c), as
    # the menu sends it: BT_SPECIAL | BTS_KIOSK | KIOSK_EVERYTHING (25).
    s.append(("kiosk", "plain", demo(*[tic()] * 10, tic(buttons=2), *[tic()] * 10, tic(buttons=0x80 | 0x40 | 25), *[tic()] * 10),
              day + ["-libraryplace", str(KIOSK_X), "56", "270"], has("kiosk", "took from the kiosk")))
    # In an aisle, facing a shelf: a book taken out, read a few pages, put
    # back. It is checked as his, and it isn't.
    def book_check(log):
        bad = has("answered", "opened a book")(log)
        if "library: found his book" in log:
            bad.append("the first book he opens shouldn't be his")
        return bad
    s.append(("book", "plain", demo(*[tic()] * 5, tic(buttons=2), *[tic()] * 5, *[tic(50)] * 3, *[tic()] * 5,
                                    tic(buttons=2), *[tic()] * 10),
              ["-libraryempty", "-libraryplace", str(L["LANDING"] + 5 * L["GALLERY"] + 128), "100", "0",
               "-libraryreader", "Margaret Ellison/1984/Edinburgh/Leith/Capricorn/Biscuit/Porridge"], book_check))
    # In an aisle facing the back wall between two stacks: its books are
    # taken too (layout.h: the wall's shelves).
    s.append(("wall-book", "plain", demo(*[tic()] * 5, tic(buttons=2), *[tic()] * 5, tic(buttons=2), *[tic()] * 5),
              ["-libraryempty", "-libraryplace", str(L["LANDING"] + 5 * L["GALLERY"] + 148), "60", "270",
               "-libraryreader", "Margaret Ellison/1984/Edinburgh/Leith/Capricorn/Biscuit/Porridge"], has("opened a book")))
    # Floor 19 (seed DERP) has someone asleep in the west landing's bed by the mirror:
    # lying there until a shot wakes it.
    s.append(("sleeper", "plain", demo(*[tic()] * 25, tic(buttons=1), *[tic()] * 40),
              ["-libraryseed", "DERP", "-librarydown", "18", "-libraryplace", "120", "56", "180"],
              has("asleep in a bed", "woke in a bed")))
    s.append(("death", "people", demo(*[tic()] * 3000), day + ["-libraryplace", str(MIDDLE_X), str(NEAR_WALK), "0"], has("woke from death", "morning")))
    return s


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--headless", required=True)
    parser.add_argument("--iwad", required=True)
    parser.add_argument("--only")
    parser.add_argument("--keep")
    parser.add_argument("--seed", default=SEED)
    parser.add_argument("--smoke", help="doom-native-smoke: also save and load in the library")
    args = parser.parse_args()

    work = os.path.abspath(args.keep or tempfile.mkdtemp(prefix="library-scenarios-"))
    os.makedirs(work, exist_ok=True)
    maps = {}
    for kind, people in (("plain", False), ("people", True)):
        m = library.build(people)
        lumps, _ = m.lumps(library.texture)
        maps[kind] = os.path.join(work, "library-%s.wad" % kind)
        with open(maps[kind], "wb") as f:
            f.write(wad([("E0M1", lumps)], [("DERPINFO", b"map E0M1 { library = true }\n")]))

    failed = 0
    only = set(args.only.split(",")) if args.only else None
    for number, (name, kind, data, extra, check) in enumerate(scenarios()):
        if only and name not in only:
            continue
        lump = "lib%02d" % number  # a lump's name: eight letters at most
        with open(os.path.join(work, lump + ".lmp"), "wb") as f:
            f.write(data)
        # DOOM finds the demo as a lump by its bare name: run beside it.
        command = [os.path.abspath(args.headless), "-iwad", os.path.abspath(args.iwad), "-file", maps[kind],
                   "-playdemo", lump, "-librarylog"] + ([] if "-libraryseed" in extra else ["-libraryseed", args.seed]) + extra
        result = subprocess.run(command, capture_output=True, text=True, timeout=1800, cwd=work)
        log = result.stdout + result.stderr
        with open(os.path.join(work, name + ".log"), "w") as f:
            f.write(log)
        problems = check(log) if result.returncode == 0 else ["doom-headless exited with %d" % result.returncode]
        print("%-14s %s" % (name, "ok" if not problems else "FAILED: " + "; ".join(problems)))
        failed += bool(problems)
    # Saving and loading (the game's own smoke test, on the library's map):
    # the load checks that every one of the people is found again, and the
    # smoke that the floor and those asleep in the beds are; on the first
    # floor, and 12 down (floor 13: someone asleep, seed DERP).
    # Then saves edited as one digging into them might (edit_save in
    # tests/native_smoke.c): a level clock just short of its 32-bit end
    # must load and be renumbered; a state cut short, or a floor above the
    # top, must stop the load with a message, not crash.
    for name, down in (("save-load", []), ("save-load-deep", ["12"]), ("save-clock", ["12", "clock"]),
                       ("save-cut", ["0", "cut"]), ("save-floor", ["0", "floor"]), ("save-bottom", ["0", "bottom"])):
        if not args.smoke or (only and name not in only):
            continue
        environment = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
        result = subprocess.run([os.path.abspath(args.smoke), os.path.abspath(args.iwad), maps["plain"], "0"] + down,
                                capture_output=True, text=True, timeout=600, cwd=work, env=environment)
        log = result.stdout + result.stderr
        with open(os.path.join(work, name + ".log"), "w") as f:
            f.write(log)
        if name in ("save-cut", "save-floor"):
            ok = result.returncode == 1 and "Bad savegame" in log
        else:
            ok = (result.returncode == 0 and "save, and load passed" in log and (name != "save-clock" or "renumbered" in log)
                  and (name != "save-bottom" or "bottom kept" in log))
        print("%-14s %s" % (name, "ok" if ok else "FAILED: see %s.log" % name))
        failed += not ok
    print("logs in", work)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
