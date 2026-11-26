/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "pitch_shift.hpp"
#include "signalsmith-stretch.h"
#include <vector>

namespace a11y {
namespace {

/* The shifter's analysis block and the step between blocks, at kCueRate:
 * 30 ms and a quarter of it. The cue sounds last about a tenth of a second,
 * and a block shorter than the library's 120 ms default kept their
 * loudness over time closest to the original's, measured over 5 ms frames
 * at pitches from an octave down to an octave up. */
constexpr int kBlock = kCueRate * 3 / 100;
constexpr int kInterval = kBlock / 4;

/* Partials below it keep their phases together as they move, which keeps a
 * tonal sound like itself: the Star Fox 64 fork's value. */
constexpr float kTonalityLimitHz = 8000.0f;

/* The shifter adds a little noise to its phases; a fixed seed makes the
 * same copies at every start. */
constexpr int kSeed = 1;

}  // namespace

Sound shift_pitch(const Sound& sound, float factor) {
    if (factor == 1.0f || sound.samples.empty()) {
        return sound;
    }
    signalsmith::stretch::SignalsmithStretch<float> stretch(kSeed);
    stretch.configure(1, kBlock, kInterval);
    stretch.setTransposeFactor(factor, kTonalityLimitHz / kCueRate);
    /* An exact shift of a sound of fixed length, as the library's README
     * lays out: seek over the input's first latency, so its processing
     * starts at the sound's start; process the rest with silence after it,
     * as much as the input latency, so it reaches the sound's end; flush
     * what it still holds. The output then runs from the output latency
     * before the sound's start to its end, and the sound is the part after
     * that pre-roll. */
    const int frames = static_cast<int>(sound.samples.size());
    const int input_latency = stretch.inputLatency();
    const int output_latency = stretch.outputLatency();
    std::vector<float> input = sound.samples;
    input.resize(static_cast<size_t>(frames + input_latency), 0.0f);
    std::vector<float> output(static_cast<size_t>(frames + output_latency), 0.0f);
    float* seek_from = input.data();
    stretch.seek(&seek_from, input_latency, 1.0);
    float* from = input.data() + input_latency;
    float* to = output.data();
    stretch.process(&from, frames, &to, frames);
    float* rest = output.data() + frames;
    stretch.flush(&rest, output_latency);
    Sound shifted;
    shifted.samples.assign(output.begin() + output_latency, output.end());
    return shifted;
}

}  // namespace a11y
