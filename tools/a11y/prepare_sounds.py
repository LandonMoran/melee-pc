#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# /// script
# requires-python = ">=3.10"
# dependencies = ["numpy", "soundfile"]
# ///
"""Prepare a cue's sound file from an original recording.

  uv run tools/a11y/prepare_sounds.py ORIGINAL NAME [--lowest-pitch OCTAVES]

Reads ORIGINAL (WAV or FLAC, any rate, any channel count) and writes
resources/a11y/NAME.wav, mono, 48 kHz, 16-bit, as the cue engine plays it:

  - the channels averaged to one;
  - resampled to 48 kHz, if it is at another rate;
  - silence trimmed at both ends: everything before the first sample and
    after the last one within TRIM_BELOW_PEAK_DB of the file's peak;
  - the end faded to silence over END_FADE_SECONDS, so the trim never
    clicks;
  - normalized so its peak is PEAK, just under full scale, the level the
    generated cues are made at. The balance between cues is set by ear
    through each cue's volume and boost, not here.

No lead-in is added: the engine puts its own silence before the sound
(kLeadInSeconds in src/pc/a11y/cue_sounds.hpp), so the file starts at its
first sound.

The report gives the result's length, with and without the engine's
lead-in, and the fastest pace at which a pulse plays whole: the length with
the lead-in, divided by the cue's lowest pitch. --lowest-pitch gives that
pitch in octaves down (1 is half the playback rate; 0, the default, the
sound's own pitch). A pulsed cue's pace faster than that cuts each pulse
with a fade as the next starts (docs/a11y/cues.md, "A long pulse is cut with
a fade").

The originals stay outside git; the prepared files are committed, so a cue
dump compares with exactly what plays. Run it again on the original to
replace a file. --out-dir writes elsewhere (the tests use it).

Needs uv, which installs numpy and soundfile for the run from this file's
own metadata block. The tests: uv run tools/a11y/test_prepare_sounds.py
"""
import argparse
import math
import os
import re
import sys

import numpy as np
import soundfile

RATE = 48000
# Trimmed at both ends below this level, in dB under the file's own peak, so
# a quiet recording is trimmed as a loud one is.
TRIM_BELOW_PEAK_DB = -50.0
# The fade at the end, so the cut after the trim is silent.
END_FADE_SECONDS = 0.005
# The peak after normalizing: just under full scale, about -0.4 dBFS.
PEAK = 0.95
# The engine's lead-in before each play, kLeadInSeconds in cue_sounds.hpp:
# change both together.
LEAD_IN_SECONDS = 0.012
# The resampler: a Kaiser-windowed sinc this many zero crossings each side,
# its cutoff this share of the lower rate's Nyquist frequency.
RESAMPLE_ZERO_CROSSINGS = 32
RESAMPLE_CUTOFF = 0.95
RESAMPLE_KAISER_BETA = 8.6

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
DEFAULT_OUT_DIR = os.path.join(ROOT, "resources", "a11y")


class Refused(Exception):
    """An input the script will not prepare, with the reason."""


def resample(samples, rate, to_rate):
    """Mono samples at rate as samples at to_rate: a windowed sinc, low-passed
    below the lower rate's Nyquist frequency."""
    if rate == to_rate or len(samples) == 0:
        return samples.astype(np.float64)
    ratio = to_rate / rate
    cutoff = RESAMPLE_CUTOFF * min(1.0, ratio)  # of the input's Nyquist
    half = int(math.ceil(RESAMPLE_ZERO_CROSSINGS / cutoff))
    padded = np.concatenate([np.zeros(half), samples.astype(np.float64), np.zeros(half + 1)])
    count = int(round(len(samples) * ratio))
    out = np.empty(count)
    offsets = np.arange(-half + 1, half + 1)
    beta_norm = np.i0(RESAMPLE_KAISER_BETA)
    chunk = 4096
    for start in range(0, count, chunk):
        times = np.arange(start, min(start + chunk, count)) / ratio
        base = np.floor(times).astype(np.int64)
        taps = base[:, None] + offsets[None, :]
        distance = times[:, None] - taps
        window = np.i0(RESAMPLE_KAISER_BETA *
                       np.sqrt(np.clip(1.0 - (distance / half) ** 2, 0.0, None))) / beta_norm
        weights = cutoff * np.sinc(cutoff * distance) * window
        out[start:start + len(times)] = np.sum(padded[taps + half] * weights, axis=1)
    return out


def prepare(samples, rate):
    """The prepared sound and what was trimmed: (sound, seconds trimmed at
    the start, seconds trimmed at the end), at RATE."""
    if samples.ndim > 1:
        samples = samples.mean(axis=1)
    sound = resample(samples, rate, RATE)
    peak = float(np.max(np.abs(sound))) if len(sound) else 0.0
    if peak == 0.0:
        raise Refused("it holds only silence")
    threshold = peak * 10.0 ** (TRIM_BELOW_PEAK_DB / 20.0)
    loud = np.nonzero(np.abs(sound) >= threshold)[0]
    first, last = int(loud[0]), int(loud[-1])
    trimmed_start = first / RATE
    trimmed_end = (len(sound) - 1 - last) / RATE
    sound = sound[first:last + 1].copy()
    fade = min(int(END_FADE_SECONDS * RATE), len(sound))
    if fade > 0:
        # A raised cosine from 1 down to 0 at the last sample.
        steps = np.arange(1, fade + 1) / fade
        sound[-fade:] *= 0.5 + 0.5 * np.cos(np.pi * steps)
    sound *= PEAK / float(np.max(np.abs(sound)))
    return sound, trimmed_start, trimmed_end


def report(path, sound, trimmed_start, trimmed_end, octaves_down):
    """The lines the script prints about the file it wrote."""
    seconds = len(sound) / RATE
    with_lead_in = seconds + LEAD_IN_SECONDS
    pitch = 2.0 ** -octaves_down
    if octaves_down == 0:
        where = "at its own pitch"
    else:
        where = f"at its lowest pitch, {octaves_down:g} octave{'' if octaves_down == 1 else 's'} down"
    return [
        f"Wrote {path}: mono, {RATE} Hz, {seconds:.3f} s ({len(sound)} frames), "
        f"{with_lead_in:.3f} s with the engine's {LEAD_IN_SECONDS * 1000:g} ms lead-in.",
        f"Trimmed {trimmed_start:.3f} s of silence at the start and {trimmed_end:.3f} s at the end.",
        f"A pulse plays whole at intervals of {with_lead_in / pitch:.3f} s or longer {where}; "
        f"a faster pace cuts it with a fade.",
    ]


def main(argv=None):
    parser = argparse.ArgumentParser(
        prog="prepare_sounds.py",
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("original", help="the original recording, WAV or FLAC")
    parser.add_argument("name", help="the output's name, written as resources/a11y/NAME.wav")
    parser.add_argument("--lowest-pitch", type=float, default=0.0, metavar="OCTAVES",
                        help="the cue's lowest pitch in octaves down, for the report (default 0)")
    parser.add_argument("--out-dir", default=DEFAULT_OUT_DIR,
                        help="where to write NAME.wav (default resources/a11y)")
    args = parser.parse_args(argv)
    name = args.name[:-4] if args.name.lower().endswith(".wav") else args.name
    if not re.fullmatch(r"[A-Za-z0-9_\-]+", name):
        parser.error(f"the name must be letters, digits, _ and - alone: {args.name}")
    if args.lowest_pitch < 0:
        parser.error("--lowest-pitch is in octaves down, 0 or more")
    if not os.path.isfile(args.original):
        parser.error(f"no such file: {args.original}")
    try:
        samples, rate = soundfile.read(args.original, dtype="float64", always_2d=True)
    except RuntimeError as error:
        parser.error(f"cannot read {args.original}: {error}")
    try:
        sound, trimmed_start, trimmed_end = prepare(samples, rate)
    except Refused as why:
        parser.error(f"{args.original}: {why}")
    os.makedirs(args.out_dir, exist_ok=True)
    path = os.path.join(args.out_dir, name + ".wav")
    soundfile.write(path, sound, RATE, subtype="PCM_16")
    shown = os.path.relpath(path, ROOT) if path.startswith(ROOT) else path
    for line in report(shown, sound, trimmed_start, trimmed_end, args.lowest_pitch):
        print(line)
    return 0


if __name__ == "__main__":
    sys.exit(main())
