#!/usr/bin/env python3
"""DERP's soldiers' shouts, from the recorded MP3s to barks/barks.wad.

Run by hand when the recordings change; the WAD is what is committed. Needs the
miniaudio package for the decoding.

    make_barks.py <zombieman folder> <shotgun guy folder> <body sounds folder> <deaths folder> <out.wad>

Each voice's folder holds <moment>_<take>.mp3; the body sounds' holds
zombieman_<moment>[_<take>].wav and shotgunguy_ the same; the deaths' holds
death_<kind>_<n>.wav, shared out by DEATH_KINDS. The moments are numbered
as bark_t in linuxdoom-1.10/p_barks.h: keep MOMENTS in that order. A take becomes a DMX sound
lump BK<voice><moment><take>, as the mixer plays them: 8-bit, 22 kHz, mono,
silence trimmed from both ends, the peak brought up to full.

That is the plain voices, one set. barks/barks.wad is those through deepen_barks.py.
"""
import os
import re
import struct
import sys

import miniaudio

MOMENTS = [
    "contact", "noise", "nothing", "lostyou", "takingfire", "cover", "flanked",
    "bounding", "coveringbound", "suppressing", "pinned", "friendinline",
    "frienddies", "trap", "door", "watchdoor", "reserve", "key", "fallback",
    "regroup", "lastalive", "pain", "idle", "gibbed", "death",
    # The later recordings: scripts/add_barks.py.
    "push", "pushhurt", "pushaway", "goround", "tooclose", "bait", "baitturn",
    "friendhit", "demonhit", "getdemon", "pinkycharge", "pinkypound", "window",
    "tookitem", "trapsprung", "running", "headshot", "friendgibbed", "inslime",
    "noslime", "flashlight", "barrels",
    "fires", "lane", "waiting", "stillalive", "shrug",
    "jokemath", "jokefree", "jokepressure", "jokecantsee", "jokefeature", "jokereallife",
    "jokewanted", "jokelovegore", "jokeunnecessary", "jokesplash", "jokerightkeys",
    "jokewhatisit", "jokedonttalk", "joketwice",
    "impsees", "impanswer", "impclicks", "impwindup", "imppounce", "impswipe", "impthrow",
    "imppainsmall", "imppainbig", "impdeath", "impgibbed", "impheard", "impsniff",
    "imppackdeath", "impcornered", "impland", "impwindow", "impchitter",
    # y.A.n.I.k.a.d.'s lines: scripts/add_voices.py.
    "flankdoor", "flankother", "hearsrun", "bulletnear", "goeslook", "wordround", "wordanswer",
    "standsready", "standsdown", "hiddendoor", "guardsitem", "friendgone", "holdback", "goall",
    "impsquiet", "impover",
    "chatremember", "chatinside", "chatbody", "chathim", "chatjob", "chatclown",
    "talk01", "talk02", "talk03", "talk04", "talk05", "talk06", "talk07", "talk08", "talk09", "talk10",
    "implurk", "impscrape", "impcall", "impcallback", "impcrouch", "impair", "impoverbody",
]
# Whose each kind of death scream is: zombiemen go high and panicked, shotgun
# guys heavy. The recordings do not say.
DEATH_KINDS = {"panicked": "A", "shocked": "A", "deep": "B", "agonized": "B"}
RATE = 22050
SILENCE = 400		# of 32768: quieter than this at either end goes
LEAD = RATE//200	# 5 ms kept before the first sound


def sample(path):
    decoded = miniaudio.decode_file(path, output_format=miniaudio.SampleFormat.SIGNED16,
                                    nchannels=1, sample_rate=RATE)
    pcm = decoded.samples
    start, end = 0, len(pcm)
    while start < end and abs(pcm[start]) < SILENCE:
        start += 1
    while end > start and abs(pcm[end - 1]) < SILENCE:
        end -= 1
    pcm = pcm[max(start - LEAD, 0):end]
    peak = max((abs(v) for v in pcm), default=1) or 1
    data = bytes(min(255, max(0, 128 + round(v*127/peak))) for v in pcm)
    return struct.pack("<HHI", 3, RATE, len(data)) + data


def main():
    if len(sys.argv) != 6:
        sys.exit(__doc__)
    lumps = []
    for voice, folder in zip("AB", sys.argv[1:3]):
        for name in sorted(os.listdir(folder)):
            match = re.fullmatch(r"([a-z]+)_(\d)\.mp3", name)
            if not match or match.group(1) not in MOMENTS:
                sys.exit(f"{folder}: what is {name}?")
            moment = MOMENTS.index(match.group(1))
            lumps.append((f"BK{voice}{moment:02}{match.group(2)}", sample(os.path.join(folder, name))))
    for name in sorted(os.listdir(sys.argv[3])):
        match = re.fullmatch(r"(zombieman|shotgunguy)_([a-z]+)(?:_(\d))?\.wav", name)
        if not match or match.group(2) not in MOMENTS:
            sys.exit(f"{sys.argv[3]}: what is {name}?")
        voice = "A" if match.group(1) == "zombieman" else "B"
        lumps.append((f"BK{voice}{MOMENTS.index(match.group(2)):02}{match.group(3) or 1}",
                      sample(os.path.join(sys.argv[3], name))))
    taken = {"A": 0, "B": 0}
    for name in sorted(os.listdir(sys.argv[4])):
        match = re.fullmatch(r"death_([a-z]+)_\d+\.wav", name)
        if not match or match.group(1) not in DEATH_KINDS:
            sys.exit(f"{sys.argv[4]}: what is {name}?")
        voice = DEATH_KINDS[match.group(1)]
        taken[voice] += 1
        lumps.append((f"BK{voice}{MOMENTS.index('death'):02}{taken[voice]}", sample(os.path.join(sys.argv[4], name))))
    body = b"".join(data for _, data in lumps)
    with open(sys.argv[5], "wb") as out:
        out.write(b"PWAD" + struct.pack("<II", len(lumps), 12 + len(body)))
        out.write(body)
        at = 12
        for name, data in lumps:
            out.write(struct.pack("<II8s", at, len(data), name.encode()))
            at += len(data)
    seconds = sum(len(d) - 8 for _, d in lumps)/RATE
    print(f"{len(lumps)} lumps, {seconds:.0f} s, {os.path.getsize(sys.argv[5])//1024} KB")


if __name__ == "__main__":
    main()
