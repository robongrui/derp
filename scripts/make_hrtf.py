#!/usr/bin/env python3
"""Bakes a SADIE II head (University of York, Apache 2.0) into platform/audio/.

Takes the 44.1 kHz HRIR SOFA file of a subject, from
https://www.york.ac.uk/sadie-project/database.html (zenodo record 12092466,
D1_HRIR_SOFA.zip is the KU100, D2_HRIR_SOFA.zip the KEMAR). Needs numpy, h5py.

    py scripts/make_hrtf.py <file.sofa> <name> [--taps 64] [--report]

What comes out is smaller than what went in, and it is the left ear only:
- Dummy heads are symmetric, so the two ears are averaged into one and the
  right ear reads the left at the mirrored direction. Dead ahead is even.
- Each direction is a minimum-phase filter plus a delay. Filters without their
  delay can be blended between directions without combing.
- Everything is divided by the head's own smoothed response from dead ahead,
  so a sound in front keeps the tone the mix was tuned with, and only the
  change as it moves around the head is left.
"""
import argparse
import math
import os
import sys

import h5py
import numpy as np

RATE = 44100
NFFT = 4096
STEP = 5                                   # degrees of azimuth between filters
RINGS = (-60, -45, -30, -15, 0, 15, 30, 45, 60)
ITD_LOWPASS_HZ = 1500                      # the ears compare time below here
ITD_OVERSAMPLE = 8
LIMIT_DB = 18                              # no filter boosts or cuts past this
OUT_DIR = os.path.join(os.path.dirname(__file__), "..", "platform", "audio")


def load(path):
    with h5py.File(path, "r") as f:
        rate = float(f["Data.SamplingRate"][0])
        if rate != RATE:
            sys.exit(f"{path}: {rate} Hz, want {RATE}")
        return f["Data.IR"][:], f["SourcePosition"][:]


def nearest(position, azimuth, elevation):
    """Index of the measurement closest to a direction."""
    az, el = np.radians(position[:, 0]), np.radians(position[:, 1])
    a, e = math.radians(azimuth), math.radians(elevation)
    return int(np.argmax(np.cos(el) * math.cos(e) * np.cos(az - a) + np.sin(el) * math.sin(e)))


def smooth(magnitude, fraction=3):
    """Power-smoothed over 1/fraction of an octave."""
    power = magnitude ** 2
    total = np.concatenate(([0], np.cumsum(power)))
    bins = np.arange(len(power))
    width = 2 ** (0.5 / fraction)
    low = np.maximum((bins / width).astype(int), 0)
    high = np.minimum(np.ceil(bins * width).astype(int) + 1, len(power))
    return np.sqrt((total[high] - total[low]) / np.maximum(high - low, 1))


def minimum_phase(magnitude):
    """The minimum-phase impulse response of a one-sided magnitude, by cepstrum."""
    log = np.log(np.maximum(magnitude, 1e-5))
    cepstrum = np.fft.irfft(log, NFFT)
    fold = np.zeros(NFFT)
    fold[0] = cepstrum[0]
    fold[1:NFFT // 2] = 2 * cepstrum[1:NFFT // 2]
    fold[NFFT // 2] = cepstrum[NFFT // 2]
    return np.fft.irfft(np.exp(np.fft.rfft(fold)), NFFT)


def time_difference(left, right):
    """How many samples later the right ear hears it than the left."""
    n = NFFT * ITD_OVERSAMPLE
    freqs = np.fft.rfftfreq(NFFT, 1 / RATE)
    keep = 1 / (1 + (freqs / ITD_LOWPASS_HZ) ** 8)
    a = np.fft.rfft(left, NFFT) * keep
    b = np.fft.rfft(right, NFFT) * keep
    correlation = np.fft.irfft(np.conj(a) * b, n)
    lag = int(np.argmax(np.abs(correlation)))
    if lag > n // 2:
        lag -= n
    return lag / ITD_OVERSAMPLE


def bake(ir, position, taps):
    ear = lambda index, side: np.abs(np.fft.rfft(ir[index, side], NFFT))
    def one_ear(azimuth, elevation):
        here = nearest(position, azimuth, elevation)
        there = nearest(position, -azimuth, elevation)
        return np.sqrt((ear(here, 0) ** 2 + ear(there, 1) ** 2) / 2)

    ahead = smooth(one_ear(0, 0))
    steps = 360 // STEP
    filters = np.zeros((len(RINGS), steps, taps))
    delays = np.zeros((len(RINGS), steps))
    window = np.ones(taps)
    fade = taps // 4
    window[-fade:] = np.cos(np.linspace(0, np.pi / 2, fade)) ** 2
    limit = 10 ** (LIMIT_DB / 20)
    for r, elevation in enumerate(RINGS):
        for s in range(steps):
            azimuth = s * STEP
            shaped = np.clip(one_ear(azimuth, elevation) / ahead, 1 / limit, limit)
            filters[r, s] = minimum_phase(shaped)[:taps] * window
            here = nearest(position, azimuth, elevation)
            there = nearest(position, -azimuth, elevation)
            delays[r, s] = (time_difference(ir[here, 0], ir[here, 1])
                            - time_difference(ir[there, 0], ir[there, 1])) / 2
    return filters, delays


def band_db(filter_, low, high):
    freqs = np.fft.rfftfreq(NFFT, 1 / RATE)
    band = (freqs >= low) & (freqs < high)
    return 10 * math.log10(np.mean(np.abs(np.fft.rfft(filter_, NFFT))[band] ** 2) + 1e-12)


def report(filters, delays, ir, position, taps):
    steps = filters.shape[1]
    zero = RINGS.index(0)
    at = lambda degrees: (degrees % 360) // STEP
    print(f"{taps} taps, {filters.size} values")
    print("ring 0: az | left ear 200-2k 2-5k 5-11k dB | delay right-left samples")
    for az in (0, 30, 60, 90, 120, 150, 180, 270):
        f = filters[zero, at(az)]
        print(f"  {az:4d} | {band_db(f, 200, 2000):6.1f} {band_db(f, 2000, 5000):6.1f} {band_db(f, 5000, 11000):6.1f}"
              f" | {delays[zero, at(az)]:6.2f}")
    # Power of both ears against dead ahead, in the band DOOM's samples reach.
    print("both ears 200-5k against ahead, by azimuth (dB):")
    ahead = 2 * 10 ** (band_db(filters[zero, 0], 200, 5000) / 10)
    row = []
    for az in range(0, 360, 45):
        l = filters[zero, at(az)]
        r = filters[zero, at(-az)]
        row.append(10 * math.log10((10 ** (band_db(l, 200, 5000) / 10) + 10 ** (band_db(r, 200, 5000) / 10)) / ahead))
    print("  " + " ".join(f"{v:5.1f}" for v in row))
    # What truncation loses: spectrum of the kept taps against the long filter.
    print("peak tap", np.abs(filters).max())


def write(filters, delays, name, taps):
    scale = 32000 / np.abs(filters).max()
    path = os.path.join(OUT_DIR, f"hrtf_{name}.inc")
    with open(path, "w", newline="\n") as out:
        out.write(f"/* Generated by scripts/make_hrtf.py from SADIE II, University of York\n"
                  f"   (Apache 2.0, https://www.york.ac.uk/sadie-project/). Do not edit. */\n")
        out.write(f"static const float hrtf_{name}_scale = {1 / scale:.9g}f;\n")
        out.write(f"static const short hrtf_{name}_delays[HRTF_RINGS][HRTF_STEPS] = {{\n")
        for ring in delays:
            out.write("{" + ",".join(str(int(round(v * 16))) for v in ring) + "},\n")
        out.write("};\n")
        out.write(f"static const short hrtf_{name}_filters[HRTF_RINGS][HRTF_STEPS][HRTF_TAPS] = {{\n")
        for ring in filters:
            out.write("{\n")
            for filter_ in ring:
                out.write("{" + ",".join(str(int(round(v * scale))) for v in filter_) + "},\n")
            out.write("},\n")
        out.write("};\n")
    print(f"{path}: {os.path.getsize(path)} bytes")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("sofa")
    parser.add_argument("name")
    parser.add_argument("--taps", type=int, default=64)
    parser.add_argument("--report", action="store_true", help="print, write nothing")
    args = parser.parse_args()
    ir, position = load(args.sofa)
    filters, delays = bake(ir, position, args.taps)
    if args.report:
        report(filters, delays, ir, position, args.taps)
    else:
        write(filters, delays, args.name, args.taps)


if __name__ == "__main__":
    main()
