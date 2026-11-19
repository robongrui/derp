#!/usr/bin/env python3
"""Every voice: barks.wad and the y.A.n.I.k.a.d. recordings, into derp-voices.wad and derp-cries.wad.

The cries (derp-cries.wad, committed, shipped with the game) are the sounds without words, made
with ElevenLabs' Sound Effects: every imp noise, and the soldiers' pain, idle, gibbed and death
sounds (make_barks.py's body and death folders). Those may only go out combined with the game,
never as a collection of sounds on its own. The spoken lines (derp-voices.wad, Text to Speech)
are the separate download, not in git: PLAN-voice-lines.md says why. Takes the soldiers' lines
through the demoniser (deepen_barks.py) and the imps' noises 3 semitones down. Run it after a
change to the recordings. Needs miniaudio and numpy.

    add_voices.py <barks3 folder> <barks.wad> <derp-voices.wad> <derp-cries.wad>
    add_voices.py --split <old whole derp-voices.wad> <derp-voices.wad> <derp-cries.wad>

barks3 holds zombieman, shotgunguy, chaingunner and imps, each take <moment>_<n>.mp3.
A soldier's take keeps its number: the parts of a talk are told apart by it, and each
part belongs to one voice. The imps' takes are numbered anew, as gaps mean a rejected one.
"""
import glob
import os
import re
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from add_barks import lump_name, read_wad, write_wad
from make_barks import MOMENTS, sample

VOICES = (("A", "zombieman"), ("B", "shotgunguy"), ("C", "chaingunner"))
# Recorded under another name: "door" was already a moment.
SOLDIER = {"door": "flankdoor", "doorother": "flankother"}
IMP = {"lurkbreath": "implurk", "scrape": "impscrape", "call": "impcall", "answer": "impcallback",
       "crouch": "impcrouch", "air": "impair", "overbody": "impoverbody"}


def takes(folder):
    for path in sorted(glob.glob(os.path.join(folder, "*.mp3"))):
        match = re.fullmatch(r"([a-z]+\d*)_(\d+)\.mp3", os.path.basename(path))
        if match:
            yield match.group(1), int(match.group(2)), path


# The soldiers' sounds without words, from make_barks.py's body and death folders.
SOLDIER_CRIES = ("pain", "idle", "gibbed", "death")


def is_cry(name):
    """Whether a lump BK<voice><moment><take>[set] is a cry: an imp's, or a soldier's wordless moment."""
    if not name.startswith("BK") or len(name) < 6:
        return False
    if name[2] == "D":
        return True
    return name[3:5].isdigit() and MOMENTS[int(name[3:5])] in SOLDIER_CRIES


def split(lumps):
    """The pack in two: the spoken lines, and the cries."""
    voices = {n: d for n, d in lumps.items() if not is_cry(n)}
    cries = {n: d for n, d in lumps.items() if is_cry(n)}
    return voices, cries


def write_both(lumps, voices_out, cries_out):
    voices, cries = split(lumps)
    write_wad(voices_out, voices)
    write_wad(cries_out, cries)
    print(f"{len(voices)} spoken lumps, {os.path.getsize(voices_out)//1024} KB;",
          f"{len(cries)} cries, {os.path.getsize(cries_out)//1024} KB")


def pitch_imps(lumps):
    """Every imp sound (voice D: barks.wad's and barks3's alike) 3 semitones down, with a quiet
    copy 8 down (deepen_barks.imp). Run on plain takes only, never on a pack already made."""
    from deepen_barks import imp, voiced
    for name in [n for n in lumps if n.startswith("BKD")]:
        lumps[name] = voiced(lumps[name], imp)


def main():
    if len(sys.argv) == 5 and sys.argv[1] == "--split":
        # A pack made before the split, its imps already pitched: only shared out.
        write_both(read_wad(sys.argv[2]), sys.argv[3], sys.argv[4])
        return
    if len(sys.argv) != 5:
        sys.exit(__doc__)
    barks3, barks, out, cries_out = sys.argv[1:]
    soldiers = {}
    for voice, folder in VOICES:
        for moment, take, path in takes(os.path.join(barks3, folder)):
            moment = SOLDIER.get(moment, moment)
            if moment not in MOMENTS:
                sys.exit(f"what is {path}? ({moment})")
            soldiers[lump_name(voice, moment, take)] = sample(path)
    demons, count = {}, {}
    for moment, _, path in takes(os.path.join(barks3, "imps")):
        if moment not in IMP:
            continue
        count[moment] = count.get(moment, 0) + 1
        demons[lump_name("D", IMP[moment], count[moment])] = sample(path)
    with tempfile.TemporaryDirectory() as scratch:
        plain, deep = os.path.join(scratch, "plain.wad"), os.path.join(scratch, "deep.wad")
        write_wad(plain, soldiers)
        subprocess.run([sys.executable, os.path.join(os.path.dirname(os.path.abspath(__file__)), "deepen_barks.py"),
                        plain, deep], check=True)
        demonised = read_wad(deep)
    lumps = read_wad(barks)
    lumps.update(demonised)
    lumps.update(demons)
    pitch_imps(lumps)
    print(f"{len(soldiers)} soldier takes, {len(demons)} imp takes, {len(lumps)} lumps")
    write_both(lumps, out, cries_out)


if __name__ == "__main__":
    main()
