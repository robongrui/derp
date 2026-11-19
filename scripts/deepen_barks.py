"""The soldiers' voices as the game has them, from make_barks.py's plain ones.

Five sets of every sound, and each soldier keeps one (p_barks.c), so a squad is
several men. Run it on what make_barks.py writes, never on its own output. Needs
numpy.

    deepen_barks.py <plain barks.wad> <barks.wad>

The sets (a lump's seventh letter, none for the first):
  (none)  the user's Audacity recipe: 6 semitones down, a copy 12 down at -18 dB
  A       a friend's: 3 down, a copy 8 down at -24 dB
  B       5 down with the human throat kept, a copy 12 down at -24 dB
  C       the friend's, with a thin copy 7 up at -26 dB: two things speaking
  D       3 down, over a copy 12 down at -18 dB chopped 31 times a second

Pitch only, the length stays: a stretch that keeps the waveform, then a resample.
Lowering a voice lowers its throat with it, which is what makes an ork; keeping
the throat leaves a man with the wrong voice.
"""
import struct
import sys

import numpy as np

WINDOW = 0.030		# seconds
SEARCH = 0.010
# The throat: the frames it is measured over, and how smooth a shape counts.
FRAME = 1024
FRAME_HOP = 256
LIFTER = 32


def read_wad(path):
    data = open(path, "rb").read()
    kind, count, table = struct.unpack("<4sII", data[:12])
    lumps = []
    for i in range(count):
        pos, size, name = struct.unpack("<II8s", data[table + i*16:table + i*16 + 16])
        lumps.append((name.rstrip(b"\0").decode(), data[pos:pos + size]))
    return lumps


def write_wad(path, lumps):
    body = bytearray()
    table = bytearray()
    for name, data in lumps:
        table += struct.pack("<II8s", 12 + len(body), len(data), name.encode())
        body += data
    with open(path, "wb") as f:
        f.write(struct.pack("<4sII", b"PWAD", len(lumps), 12 + len(body)) + bytes(body) + bytes(table))


def stretch(x, rate, sr):
    """Longer by rate, the pitch as it was: overlap-add, each piece placed where
    its waveform lines up with where the last one was going."""
    n = int(WINDOW*sr)
    hop = n//2
    tol = int(SEARCH*sr)
    src = hop/rate
    padded = np.concatenate([np.zeros(n + tol), x, np.zeros(2*n + 2*tol)])
    win = np.hanning(n)
    frames = int(len(x)*rate/hop) + 2
    out = np.zeros(frames*hop + n)
    weight = np.zeros_like(out)
    pos = n + tol
    for k in range(frames):
        out[k*hop:k*hop + n] += padded[pos:pos + n]*win
        weight[k*hop:k*hop + n] += win
        follow = padded[pos + hop:pos + hop + n]
        centre = n + tol + int((k + 1)*src)
        if centre + n + tol >= len(padded):
            break
        region = padded[centre - tol:centre + n + tol]
        score = np.correlate(region, follow, "valid")
        pos = centre - tol + int(np.argmax(score))
    return out/np.maximum(weight, 1e-3)


def resample(x, length):
    """To length samples, nothing above the new limit left to fold back."""
    spectrum = np.fft.rfft(x)
    keep = length//2 + 1
    return np.fft.irfft(spectrum[:keep], length)*(length/len(x))


def pitched(x, semitones, sr):
    """Down: shorter first, then drawn out to the old length. Up the other way."""
    rate = 2**(semitones/12)
    return resample(stretch(x, rate, sr)[:int(len(x)*rate)], len(x))


def envelope(frame):
    """The smooth shape of a spectrum: the throat, without the pitch."""
    cepstrum = np.fft.irfft(np.log(np.abs(frame) + 1e-6))
    cepstrum[LIFTER:-LIFTER] = 0
    return np.fft.rfft(cepstrum).real


def keep_throat(y, x):
    """y's pitch with x's throat: each frame's shape put back to the original's."""
    win = np.hanning(FRAME)
    pad = np.zeros(FRAME)
    xs = np.concatenate([pad, x, pad])
    ys = np.concatenate([pad, y, pad])
    out = np.zeros_like(ys)
    weight = np.zeros_like(ys)
    for i in range(0, len(ys) - FRAME, FRAME_HOP):
        fx = np.fft.rfft(xs[i:i + FRAME]*win)
        fy = np.fft.rfft(ys[i:i + FRAME]*win)
        gain = np.exp(np.clip(envelope(fx) - envelope(fy), -3, 3))
        out[i:i + FRAME] += np.fft.irfft(fy*gain)*win
        weight[i:i + FRAME] += win*win
    return (out/np.maximum(weight, 1e-3))[FRAME:FRAME + len(y)]


def db(value):
    return 10**(value/20)


def recipe(x, sr):
    return pitched(x, -6, sr) + db(-18)*pitched(x, -12, sr)


def friend(x, sr):
    return pitched(x, -3, sr) + db(-24)*pitched(x, -8, sr)


def throat(x, sr):
    return keep_throat(pitched(x, -5, sr), x) + db(-24)*pitched(x, -12, sr)


def two_voices(x, sr):
    return friend(x, sr) + db(-26)*keep_throat(pitched(x, 7, sr), x)


def rattle(x, sr):
    t = np.arange(len(x))/sr
    return pitched(x, -3, sr) + db(-18)*pitched(x, -12, sr)*(0.5 + 0.5*np.sign(np.sin(2*np.pi*31*t)))


SETS = (("", recipe), ("A", friend), ("B", throat), ("C", two_voices), ("D", rattle))
# The imps (add_voices.py): one voice for all of them, the friend's 3 down, picked by ear
# on 2026-11-19 over the soldiers' 6 down.
imp = friend


def voiced(data, voice):
    """A DMX sound lump through one of the voices above, at its own rate and length."""
    fmt, sr, size = struct.unpack("<HHI", data[:8])
    x = (np.frombuffer(data[8:8 + size], dtype=np.uint8).astype(float) - 128)/127
    y = voice(x, sr)
    y = y/(np.max(np.abs(y)) or 1)
    pcm = np.clip(np.round(128 + y*127), 0, 255).astype(np.uint8).tobytes()
    return struct.pack("<HHI", 3, sr, len(pcm)) + pcm


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    out = []
    for letter, voice in SETS:
        for name, data in read_wad(sys.argv[1]):
            out.append((name + letter, voiced(data, voice)))
    write_wad(sys.argv[2], out)
    print(len(out), "sounds")


if __name__ == "__main__":
    main()
