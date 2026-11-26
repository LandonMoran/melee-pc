#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# /// script
# requires-python = ">=3.10"
# dependencies = ["numpy", "soundfile"]
# ///
"""prepare_sounds.py against originals made here, in a scratch folder.

Not run by ctest, which has no uv: run it with
  uv run tools/a11y/test_prepare_sounds.py
"""
import contextlib
import io
import os
import sys
import tempfile

import numpy as np
import soundfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import prepare_sounds  # noqa: E402

failures = []


def check(condition, message):
    if not condition:
        failures.append(message)


def tone(rate, seconds, hz, amplitude):
    t = np.arange(int(round(seconds * rate))) / rate
    return amplitude * np.sin(2 * np.pi * hz * t)


def run(*args):
    """prepare_sounds.main's exit code, its output and its errors."""
    out, err = io.StringIO(), io.StringIO()
    code = 0
    with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
        try:
            code = prepare_sounds.main(list(args))
        except SystemExit as exit_:
            code = exit_.code
    return code, out.getvalue(), err.getvalue()


def frequency(sound, rate):
    """The tone's frequency from its rising zero crossings, away from the
    ends, where the resampler's ringing and the fade add crossings of their
    own."""
    sound = sound[len(sound) // 10:len(sound) * 9 // 10]
    rising = np.nonzero((sound[:-1] < 0) & (sound[1:] >= 0))[0]
    return (len(rising) - 1) * rate / (rising[-1] - rising[0])


def stereo_at_44100(folder):
    """0.2 s of silence, 0.1 s of a 1 kHz tone louder on the right, 0.3 s of
    silence: mono at 48 kHz, the tone alone, its peak at PEAK, its end
    silent, at the same frequency."""
    rate = 44100
    silence_before = np.zeros(int(0.2 * rate))
    silence_after = np.zeros(int(0.3 * rate))
    left = np.concatenate([silence_before, tone(rate, 0.1, 1000, 0.2), silence_after])
    right = np.concatenate([silence_before, tone(rate, 0.1, 1000, 0.4), silence_after])
    original = os.path.join(folder, "stereo.wav")
    soundfile.write(original, np.stack([left, right], axis=1), rate, subtype="PCM_16")
    code, out, err = run(original, "stereo", "--out-dir", folder, "--lowest-pitch", "1")
    check(code == 0, f"stereo: exit {code}, {err}")
    sound, out_rate = soundfile.read(os.path.join(folder, "stereo.wav"), always_2d=True)
    check(out_rate == 48000, f"stereo: rate {out_rate}")
    check(sound.shape[1] == 1, f"stereo: {sound.shape[1]} channels")
    sound = sound[:, 0]
    seconds = len(sound) / 48000
    check(abs(seconds - 0.1) < 0.001, f"stereo: {seconds:.4f} s long")
    check(abs(np.max(np.abs(sound)) - prepare_sounds.PEAK) < 0.001,
          f"stereo: peak {np.max(np.abs(sound)):.4f}")
    check(abs(sound[-1]) < 0.001, f"stereo: last sample {sound[-1]:.4f}")
    check(abs(frequency(sound, 48000) - 1000) < 5,
          f"stereo: {frequency(sound, 48000):.1f} Hz")
    lines = out.splitlines()
    check(len(lines) == 3, f"stereo: report {lines}")
    frames = len(sound)
    check(f"{seconds:.3f} s ({frames} frames)" in lines[0], f"stereo: {lines[0]}")
    check(f"{seconds + 0.012:.3f} s with the engine's 12 ms lead-in" in lines[0],
          f"stereo: {lines[0]}")
    trimmed_start = float(lines[1].split("Trimmed ")[1].split(" s")[0])
    trimmed_end = float(lines[1].split(" and ")[1].split(" s")[0])
    check(abs(trimmed_start - 0.2) <= 0.001 and abs(trimmed_end - 0.3) <= 0.001,
          f"stereo: {lines[1]}")
    interval = (seconds + 0.012) * 2
    check(f"intervals of {interval:.3f} s or longer at its lowest pitch, 1 octave down"
          in lines[2], f"stereo: {lines[2]}")


def flac_at_22050(folder):
    """A FLAC original at a lower rate, mono, its report at its own pitch."""
    rate = 22050
    original = os.path.join(folder, "low.flac")
    soundfile.write(original, tone(rate, 0.05, 500, 0.5), rate)
    code, out, err = run(original, "low.wav", "--out-dir", folder)
    check(code == 0, f"flac: exit {code}, {err}")
    sound, out_rate = soundfile.read(os.path.join(folder, "low.wav"))
    check(out_rate == 48000, f"flac: rate {out_rate}")
    check(abs(len(sound) / 48000 - 0.05) < 0.001, f"flac: {len(sound) / 48000:.4f} s")
    check(abs(frequency(sound, 48000) - 500) < 5, f"flac: {frequency(sound, 48000):.1f} Hz")
    check("or longer at its own pitch;" in out, f"flac: {out}")


def refusals(folder):
    """A missing original, one that is no sound, one of silence alone and a
    name with a path in it are refused with a clear message."""
    missing = os.path.join(folder, "missing.wav")
    code, _, err = run(missing, "x", "--out-dir", folder)
    check(code == 2 and f"no such file: {missing}" in err, f"missing: {code} {err}")
    text = os.path.join(folder, "text.wav")
    with open(text, "w") as file:
        file.write("not a sound")
    code, _, err = run(text, "x", "--out-dir", folder)
    check(code == 2 and f"cannot read {text}" in err, f"text: {code} {err}")
    silent = os.path.join(folder, "silent.wav")
    soundfile.write(silent, np.zeros(4800), 48000)
    code, _, err = run(silent, "x", "--out-dir", folder)
    check(code == 2 and "it holds only silence" in err, f"silent: {code} {err}")
    code, _, err = run(silent, "../x", "--out-dir", folder)
    check(code == 2 and "the name must be" in err, f"name: {code} {err}")
    check(not os.path.exists(os.path.join(folder, "x.wav")), "a refusal wrote a file")


def main():
    with tempfile.TemporaryDirectory() as folder:
        stereo_at_44100(folder)
        flac_at_22050(folder)
        refusals(folder)
    if failures:
        for failure in failures:
            print("FAIL:", failure)
        return 1
    print("prepare_sounds: all tests passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
