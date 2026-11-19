#!/usr/bin/env python3
"""The later recordings, into barks/barks.wad beside what is there.

Takes the soldiers' new lines through the demoniser (deepen_barks.py), the
chaingunner's voice with them, and puts the imps in as they are. Run after a change
to the recordings; lumps of the same name are replaced. Needs miniaudio and numpy.

    add_barks.py <barks2 folder> <imps folder> <imps_test folder> <barks.wad>

barks2 holds zombieman, shotgunguy, chaingunner and chaingunner_jokes, each take
<moment>_<n>.mp3. The imps' takes are <moment>_<n>.mp3, their calls and clicks the
kept tests of imps_test. Takes are numbered anew per moment, so gaps do not matter.
"""
import glob
import os
import re
import struct
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from make_barks import MOMENTS, RATE, sample

# What a chaingunner's take is called in MOMENTS, where it is not its own name.
CHAINGUNNER = {"sees": "contact", "runs": "running", "hit": "shrug", "close": "tooclose"}
IMP = {"windup": "impwindup", "pounce": "imppounce", "swipe": "impswipe", "painsmall": "imppainsmall",
       "painbig": "imppainbig", "death": "impdeath", "gibbed": "impgibbed", "heard": "impheard",
       "sniff": "impsniff", "packdeath": "imppackdeath", "cornered": "impcornered", "land": "impland",
       "window": "impwindow", "chitter": "impchitter"}
# Their kept tests, by file prefix.
IMP_TESTS = {"snarl_D": "impsees", "snarl_E": "impsees", "cut_bark": "impsees",
             "ticks_D": "impclicks", "ticks_E": "impclicks", "cut_clicks": "impclicks",
             "rattle_A": "impthrow"}
# The answer to its call: the call, played higher.
ANSWER_PITCH = 1.22
MOST_TAKES = 9


def lump_name(voice, moment, take):
    # Past 99 a letter for the hundreds and tens: two places in the name either way.
    at = MOMENTS.index(moment)
    return f"BK{voice}{at:02}{take}" if at < 100 else f"BK{voice}{chr(ord('A') + (at - 100)//10)}{at%10}{take}"


def add(lumps, count, voice, moment, path):
    if moment not in MOMENTS:
        sys.exit(f"what is {path}? ({moment})")
    count[voice, moment] = count.get((voice, moment), 0) + 1
    take = count[voice, moment]
    if take > MOST_TAKES:
        sys.exit(f"more than {MOST_TAKES} takes of {moment} for {voice}")
    lumps[lump_name(voice, moment, take)] = sample(path)


def takes(folder):
    for path in sorted(glob.glob(os.path.join(folder, "*.mp3"))):
        match = re.fullmatch(r"([a-z]+)_(\d+)\.mp3", os.path.basename(path))
        if match:
            yield match.group(1), path


def read_wad(path):
    data = open(path, "rb").read()
    _, count, table = struct.unpack("<4sII", data[:12])
    out = {}
    for i in range(count):
        pos, size, name = struct.unpack("<II8s", data[table + i*16:table + i*16 + 16])
        out[name.rstrip(b"\0").decode()] = data[pos:pos + size]
    return out


def write_wad(path, lumps):
    body = b"".join(lumps.values())
    with open(path, "wb") as out:
        out.write(b"PWAD" + struct.pack("<II", len(lumps), 12 + len(body)))
        out.write(body)
        at = 12
        for name, data in lumps.items():
            out.write(struct.pack("<II8s", at, len(data), name.encode()))
            at += len(data)


def main():
    if len(sys.argv) != 5:
        sys.exit(__doc__)
    barks2, imps, tests, wad = sys.argv[1:]
    soldiers, count = {}, {}
    for voice, folder in (("A", "zombieman"), ("B", "shotgunguy")):
        for moment, path in takes(os.path.join(barks2, folder)):
            add(soldiers, count, voice, moment, path)
    for moment, path in takes(os.path.join(barks2, "chaingunner")):
        if moment != "deathshort":
            add(soldiers, count, "C", CHAINGUNNER.get(moment, moment), path)
    for moment, path in takes(os.path.join(barks2, "chaingunner_jokes")):
        add(soldiers, count, "C", "joke" + moment, path)
    demons = {}
    for moment, path in takes(imps):
        add(demons, count, "D", IMP[moment], path)
    for path in sorted(glob.glob(os.path.join(tests, "*.*"))):
        stem = re.sub(r"_\d\.\w+$", "", os.path.basename(path))
        if stem in IMP_TESTS:
            add(demons, count, "D", IMP_TESTS[stem], path)
    for take in range(1, count["D", "impsees"] + 1):
        call = demons[lump_name("D", "impsees", take)]
        demons[lump_name("D", "impanswer", take)] = (
            struct.pack("<HHI", 3, int(RATE*ANSWER_PITCH), len(call) - 8) + call[8:])
    with tempfile.TemporaryDirectory() as scratch:
        plain, deep = os.path.join(scratch, "plain.wad"), os.path.join(scratch, "deep.wad")
        write_wad(plain, soldiers)
        subprocess.run([sys.executable, os.path.join(os.path.dirname(os.path.abspath(__file__)), "deepen_barks.py"),
                        plain, deep], check=True)
        demonised = read_wad(deep)
    lumps = read_wad(wad)
    lumps.update(demonised)
    lumps.update(demons)
    write_wad(wad, lumps)
    print(f"{len(soldiers)} soldier takes, {len(demons)} imp takes, {len(lumps)} lumps,",
          f"{os.path.getsize(wad)//1024} KB")


if __name__ == "__main__":
    main()
