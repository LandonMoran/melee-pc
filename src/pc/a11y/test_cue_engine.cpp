/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The cue engine rendered into buffers, with no device. This file supplies
 * the pc_log_line that main.c normally provides. Its one argument is where
 * to write a dump of a known pulse train, which tools/a11y/test_cue_dump.py
 * then reads back through the dump analysis script; the sound files' tests
 * write theirs in a folder beside it. */
#include "cue_engine.hpp"
#include "cue_sounds.hpp"
#include "miniaudio.h"
#include "pitch_shift.hpp"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace {

using a11y::CueEngine;
using a11y::CueEngineOptions;
using a11y::CueLevel;
using a11y::kCueRate;
using a11y::PitchedCopies;
using a11y::Sound;
using a11y::VoiceMode;
using a11y::VoiceSpec;
using a11y::VoiceTargets;

std::vector<std::string> s_lines;

/* One voice at full scale, centred, at full volume: the mix's headroom. */
const float kCentredFull = 0.5f * std::sqrt(0.5f);

Sound constant(float value, int frames) {
    Sound sound;
    sound.samples.assign(static_cast<size_t>(frames), value);
    return sound;
}

Sound sine(double hz, int frames, double amplitude) {
    ma_waveform_config config =
        ma_waveform_config_init(ma_format_f32, 1, kCueRate, ma_waveform_type_sine, amplitude, hz);
    ma_waveform wave;
    assert(ma_waveform_init(&config, &wave) == MA_SUCCESS);
    Sound sound = a11y::read_sound(&wave, frames);
    ma_waveform_uninit(&wave);
    return sound;
}

VoiceSpec looped(const char* name, Sound sound) {
    VoiceSpec spec;
    spec.name = name;
    spec.sound = std::move(sound);
    spec.mode = VoiceMode::looped;
    return spec;
}

VoiceSpec pulsed(const char* name, Sound sound, float fastest) {
    VoiceSpec spec;
    spec.name = name;
    spec.sound = std::move(sound);
    spec.mode = VoiceMode::pulsed;
    spec.fastest_interval = fastest;
    spec.lowest_pitch = 1.0f;
    return spec;
}

VoiceTargets at(float pan, float interval = 0.5f, float gain = 1.0f) {
    VoiceTargets targets;
    targets.pan = pan;
    targets.gain = gain;
    targets.interval = interval;
    return targets;
}

/* Interleaved stereo, rendered in blocks the size of a WASAPI callback. */
std::vector<float> render(CueEngine& engine, int frames) {
    std::vector<float> out(static_cast<size_t>(frames) * 2);
    for (int done = 0; done < frames; done += 480) {
        engine.render(out.data() + static_cast<size_t>(done) * 2, std::min(480, frames - done));
    }
    return out;
}

/* Mean of left² + right² per frame. */
double power(const std::vector<float>& out) {
    double sum = 0.0;
    for (float s : out) {
        sum += static_cast<double>(s) * s;
    }
    return sum / static_cast<double>(out.size() / 2);
}

/* The largest step between neighbouring samples of one channel, from the
 * sample before the buffer. */
float largest_step(const std::vector<float>& out, int channel, float before = 0.0f) {
    float largest = 0.0f;
    float last = before;
    for (size_t i = static_cast<size_t>(channel); i < out.size(); i += 2) {
        largest = std::max(largest, std::fabs(out[i] - last));
        last = out[i];
    }
    return largest;
}

float peak(const std::vector<float>& out) {
    float largest = 0.0f;
    for (float s : out) {
        largest = std::max(largest, std::fabs(s));
    }
    return largest;
}

/* Frames where sound begins after silence. */
std::vector<int> onsets(const std::vector<float>& out) {
    std::vector<int> found;
    bool silent = true;
    for (size_t f = 0; f < out.size() / 2; f++) {
        bool sounding = std::fabs(out[2 * f]) > 1e-7f || std::fabs(out[2 * f + 1]) > 1e-7f;
        if (sounding && silent) {
            found.push_back(static_cast<int>(f));
        }
        silent = !sounding;
    }
    return found;
}

/* Frames where one channel's sound begins after silence. */
std::vector<int> channel_onsets(const std::vector<float>& out, int channel) {
    std::vector<int> found;
    bool silent = true;
    for (size_t f = 0; f < out.size() / 2; f++) {
        bool sounding = std::fabs(out[2 * f + static_cast<size_t>(channel)]) > 1e-7f;
        if (sounding && silent) {
            found.push_back(static_cast<int>(f));
        }
        silent = !sounding;
    }
    return found;
}

/* The frames from the first sounding one to the last, inclusive, of one
 * channel; -1 when silent. */
std::pair<int, int> sounding_span(const std::vector<float>& out, int channel = 0) {
    int first = -1;
    int last = -1;
    for (size_t f = 0; f < out.size() / 2; f++) {
        if (std::fabs(out[2 * f + static_cast<size_t>(channel)]) > 1e-7f) {
            if (first == -1) {
                first = static_cast<int>(f);
            }
            last = static_cast<int>(f);
        }
    }
    return {first, last};
}

bool near(double value, double expected, double tolerance) {
    return std::fabs(value - expected) <= tolerance;
}

bool logged(const std::string& line) {
    return std::find(s_lines.begin(), s_lines.end(), line) != s_lines.end();
}

void constant_power_pan_keeps_loudness() {
    /* 500 Hz, a whole number of periods, so the loop has no seam. */
    double centre = 0.0;
    for (float pan : {0.0f, -1.0f, -0.5f, 0.5f, 1.0f}) {
        CueEngine engine;
        int voice = engine.add_voice(looped("tone", sine(500.0, 4800, 0.5)));
        engine.start(voice, at(pan));
        render(engine, kCueRate / 10);
        std::vector<float> out = render(engine, kCueRate / 10);
        double p = power(out);
        if (pan == 0.0f) {
            centre = p;
        }
        assert(p > 0.0);
        assert(near(p, centre, centre * 0.01));
        if (pan == -1.0f) {
            assert(largest_step(out, 1) < 1e-6f); /* nothing on the right */
        }
    }
}

void starts_and_stops_do_not_jump() {
    /* A constant sound: any step comes from the voice's gain alone. Cut at
     * full level it would step by kCentredFull at once. */
    const float most = kCentredFull / 50.0f;
    CueEngine engine;
    int voice = engine.add_voice(looped("hum", constant(1.0f, 4800)));
    engine.start(voice, at(0.0f));
    std::vector<float> out = render(engine, kCueRate / 20);
    assert(largest_step(out, 0) < most);
    assert(near(out[out.size() - 2], kCentredFull, 1e-4));

    /* A gain target that jumps glides instead. */
    engine.set_targets(voice, at(0.0f, 0.5f, 0.2f));
    float before = out[out.size() - 2];
    out = render(engine, kCueRate / 10);
    assert(largest_step(out, 0, before) < most);
    assert(near(out[out.size() - 2], 0.2f * kCentredFull, 1e-4));

    /* A pan target that jumps glides too. */
    engine.set_targets(voice, at(1.0f, 0.5f, 0.2f));
    before = out[out.size() - 2];
    out = render(engine, kCueRate / 10);
    assert(largest_step(out, 0, before) < most);

    engine.stop(voice);
    before = out[out.size() - 1];
    out = render(engine, kCueRate / 20);
    assert(largest_step(out, 1, before) < most);
    assert(out[out.size() - 1] == 0.0f);
    assert(!engine.playing(voice));

    /* Started again while still ramping down: it ramps back up from where
     * it was. */
    engine.start(voice, at(0.0f));
    render(engine, kCueRate / 20);
    engine.stop(voice);
    std::vector<float> down = render(engine, 48);
    engine.start(voice, at(0.0f));
    std::vector<float> back = render(engine, kCueRate / 20);
    assert(largest_step(back, 0, down[down.size() - 2]) < most);
    assert(near(back[back.size() - 2], kCentredFull, 1e-4));
}

void check_onsets(const std::vector<int>& found, const std::vector<int>& expected) {
    if (found != expected) {
        for (int f : found) {
            std::cerr << f << " ";
        }
        std::cerr << "\n";
    }
    assert(found == expected);
}

void pulses_follow_their_interval() {
    CueEngine engine;
    int voice = engine.add_voice(pulsed("tick", constant(0.5f, 480), 0.02f));
    engine.start(voice, at(0.0f, 0.1f));
    std::vector<float> out = render(engine, 14500);
    /* An interval change after a pulse applies to the next one... */
    engine.set_targets(voice, at(0.0f, 0.05f));
    std::vector<float> more = render(engine, 2600);
    out.insert(out.end(), more.begin(), more.end());
    /* ...and so does one made between pulses, counted from the last. */
    engine.set_targets(voice, at(0.0f, 0.2f));
    more = render(engine, 15000);
    out.insert(out.end(), more.begin(), more.end());
    /* Held to the voice's fastest interval; one shorter than the time
     * already waited starts the rhythm afresh at once. */
    engine.set_targets(voice, at(0.0f, 0.001f));
    more = render(engine, 1900);
    out.insert(out.end(), more.begin(), more.end());

    const std::vector<int> expected = {0, 4800, 9600, 14400, 16800, 26400, 32100, 33060};
    check_onsets(onsets(out), expected);
}

void alternating_voices_pulse_between_each_other() {
    /* Started together, the later added voice waits half an interval. */
    {
        CueEngine engine;
        int left = engine.add_voice(pulsed("left", constant(0.5f, 480), 0.02f));
        VoiceSpec spec = pulsed("right", constant(0.5f, 480), 0.02f);
        spec.alternates_with = left;
        int right = engine.add_voice(std::move(spec));
        assert(right == 1);
        engine.start(left, at(-1.0f, 0.1f));
        engine.start(right, at(1.0f, 0.1f));
        std::vector<float> out = render(engine, 12000);
        check_onsets(channel_onsets(out, 0), {0, 4800, 9600});
        check_onsets(channel_onsets(out, 1), {2400, 7200});
    }
    /* Started later, either one falls between the other's pulses, wherever
     * a block starts. */
    {
        CueEngine engine;
        int left = engine.add_voice(pulsed("left", constant(0.5f, 480), 0.02f));
        VoiceSpec spec = pulsed("right", constant(0.5f, 480), 0.02f);
        spec.alternates_with = left;
        int right = engine.add_voice(std::move(spec));
        engine.start(right, at(1.0f, 0.1f));
        std::vector<float> out = render(engine, 1000);
        engine.start(left, at(-1.0f, 0.1f));
        std::vector<float> more = render(engine, 9000);
        out.insert(out.end(), more.begin(), more.end());
        check_onsets(channel_onsets(out, 1), {0, 4800, 9600});
        check_onsets(channel_onsets(out, 0), {2400, 7200});
        engine.stop(right);
        more = render(engine, 7000);
        out.insert(out.end(), more.begin(), more.end());
        engine.start(right, at(1.0f, 0.1f));
        more = render(engine, 7000);
        out.insert(out.end(), more.begin(), more.end());
        check_onsets(channel_onsets(out, 1), {0, 4800, 9600, 19200});
    }
    /* Only a pulsed voice added earlier and not yet paired can be
     * alternated with. */
    {
        CueEngine engine;
        VoiceSpec spec = pulsed("alone", constant(0.5f, 480), 0.02f);
        spec.alternates_with = 0;
        assert(engine.add_voice(std::move(spec)) == -1);
        assert(logged("[a11y] cue alone refused: no pulsed voice to alternate with"));
        int first = engine.add_voice(pulsed("first", constant(0.5f, 480), 0.02f));
        spec = pulsed("second", constant(0.5f, 480), 0.02f);
        spec.alternates_with = first;
        assert(engine.add_voice(std::move(spec)) != -1);
        spec = pulsed("third", constant(0.5f, 480), 0.02f);
        spec.alternates_with = first;
        assert(engine.add_voice(std::move(spec)) == -1);
        assert(logged("[a11y] cue third refused: no pulsed voice to alternate with"));
    }
}

void the_volume_chain_multiplies() {
    CueEngine engine;
    int voice = engine.add_voice(looped("hum", constant(1.0f, 4800)));
    VoiceSpec other = looped("other", constant(1.0f, 4800));
    other.cue = 1;
    engine.add_voice(other);
    engine.start(voice, at(0.0f));
    std::vector<float> out = render(engine, kCueRate / 10);
    assert(near(out[out.size() - 2], kCentredFull, 1e-4));

    engine.set_master_volume(0.5f);
    engine.set_mix_level(CueLevel{true, 0.8f, 1.0f});
    engine.set_cue_level(0, CueLevel{true, 0.5f, 2.0f});
    engine.set_cue_level(1, CueLevel{true, 0.1f, 1.0f}); /* another cue's */
    engine.set_targets(voice, at(0.0f, 0.5f, 0.6f));
    out = render(engine, kCueRate / 10);
    assert(near(out[out.size() - 2], 0.5 * 0.8 * 0.5 * 2.0 * 0.6 * kCentredFull, 1e-4));
    /* A cue's voice takes its gain up to 1: the boost is its cue level's. */
    engine.set_targets(voice, at(0.0f, 0.5f, 2.0f));
    out = render(engine, kCueRate / 10);
    assert(near(out[out.size() - 2], 0.5 * 0.8 * 0.5 * 2.0 * kCentredFull, 1e-4));
    engine.set_targets(voice, at(0.0f, 0.5f, 0.6f));

    engine.set_cue_level(0, CueLevel{false, 0.5f, 2.0f});
    out = render(engine, kCueRate / 10);
    assert(near(out[out.size() - 2], 0.0, 1e-5));
    engine.set_cue_level(0, CueLevel{});
    engine.set_mix_level(CueLevel{false, 1.0f, 1.0f});
    out = render(engine, kCueRate / 10);
    assert(near(out[out.size() - 2], 0.0, 1e-5));
    /* Still playing underneath: on again is heard again. */
    assert(engine.playing(voice));
    engine.set_mix_level(CueLevel{});
    engine.set_master_volume(1.0f);
    out = render(engine, kCueRate / 10);
    assert(near(out[out.size() - 2], 0.6 * kCentredFull, 1e-4));
}

void a_voice_past_the_switches_keeps_every_volume() {
    CueEngine engine;
    VoiceSpec spec = looped("preview", constant(1.0f, 4800));
    spec.ignores_switches = true;
    int preview = engine.add_voice(spec);
    int match = engine.add_voice(looped("match", constant(1.0f, 4800)));
    engine.set_mix_level(CueLevel{false, 0.8f, 1.0f});
    engine.set_cue_level(0, CueLevel{false, 0.5f, 2.0f});
    engine.start(preview, at(0.0f));
    engine.start(match, at(0.0f));
    /* Both switches off: the voice past them is heard at its volumes, the
     * other is not. */
    std::vector<float> out = render(engine, kCueRate / 10);
    assert(near(out[out.size() - 2], 0.8 * 0.5 * 2.0 * kCentredFull, 1e-4));
    engine.stop(preview);
    render(engine, kCueRate / 10);
    out = render(engine, kCueRate / 10);
    assert(peak(out) == 0.0f);
    engine.start(preview, at(0.0f));
    /* The master volume, and a volume of 0 at either level, still hold,
     * once the gain has glided down. */
    engine.set_master_volume(0.5f);
    out = render(engine, kCueRate / 10);
    assert(near(out[out.size() - 2], 0.5 * 0.8 * 0.5 * 2.0 * kCentredFull, 1e-4));
    engine.set_master_volume(0.0f);
    render(engine, kCueRate / 10);
    assert(peak(render(engine, kCueRate / 10)) < 1e-4f);
    engine.set_master_volume(1.0f);
    engine.set_mix_level(CueLevel{false, 0.0f, 1.0f});
    render(engine, kCueRate / 10);
    assert(peak(render(engine, kCueRate / 10)) < 1e-4f);
    engine.set_mix_level(CueLevel{false, 1.0f, 1.0f});
    engine.set_cue_level(0, CueLevel{false, 0.0f, 2.0f});
    render(engine, kCueRate / 10);
    assert(peak(render(engine, kCueRate / 10)) < 1e-4f);
}

void five_voices_stay_under_full_scale() {
    for (float pan : {0.0f, -1.0f}) {
        CueEngine engine;
        for (int i = 0; i < 5; i++) {
            /* In phase, the worst case. */
            int voice = engine.add_voice(looped("tone", sine(500.0, 4800, 1.0)));
            engine.start(voice, at(pan));
        }
        std::vector<float> out = render(engine, kCueRate / 5);
        assert(peak(out) < 1.0f);
        assert(peak(out) > 0.9f);
    }
}

/* A pulse longer than its interval: the next pulse cuts it with a fade. */
void a_long_pulse_is_cut_with_a_fade() {
    const float most = kCentredFull / 50.0f;
    /* 0.3 s of full level with 5 ms edges, after the usual lead-in. */
    Sound long_sound = constant(1.0f, kCueRate * 3 / 10);
    a11y::shape_edges(long_sound, 0.005f);
    a11y::lead_in(long_sound, 0.012f);
    const int lead = static_cast<int>(0.012f * kCueRate);
    const int length = static_cast<int>(long_sound.samples.size());
    /* Longer than its fastest interval, and taken. */
    {
        CueEngine engine;
        s_lines.clear();
        assert(engine.add_voice(pulsed("long", long_sound, 0.1f)) == 0);
        assert(s_lines.empty());
        /* Pitched down, it plays longer, and is taken too. */
        VoiceSpec low = pulsed("low", long_sound, 0.1f);
        low.lowest_pitch = 0.5f;
        assert(engine.add_voice(low) == 1);
    }
    /* At a fast pace each pulse is cut at full level, fades out within a
     * ramp, and the next starts from the top: its lead-in is heard as
     * silence. No click anywhere. */
    {
        CueEngine engine;
        int voice = engine.add_voice(pulsed("long", long_sound, 0.1f));
        engine.start(voice, at(0.0f, 0.1f));
        std::vector<float> out = render(engine, 4 * 4800);
        assert(largest_step(out, 0) < most);
        for (int pulse : {4800, 9600, 14400}) {
            assert(near(out[2 * static_cast<size_t>(pulse - 1)], kCentredFull, 1e-4));
            for (int f = pulse + 250; f < pulse + lead; f++) {
                assert(out[2 * static_cast<size_t>(f)] == 0.0f);
            }
            assert(near(out[2 * static_cast<size_t>(pulse + lead + 1000)], kCentredFull, 1e-4));
        }
    }
    /* At a slow pace it plays whole, then silence until the next pulse. */
    {
        CueEngine engine;
        int voice = engine.add_voice(pulsed("long", long_sound, 0.1f));
        engine.start(voice, at(0.0f, 0.5f));
        std::vector<float> out = render(engine, 24000 + 4800);
        auto [first, last] =
            sounding_span(std::vector<float>(out.begin(), out.begin() + 2 * 24000));
        assert(first == lead + 1 && last == length - 2);
        assert(near(out[2 * 14000], kCentredFull, 1e-4));
        check_onsets(onsets(out), {lead + 1, 24000 + lead + 1});
    }
}

/* A sound that ends exactly as the next pulse is due is not cut: every frame
 * is the sound's level, with nothing of a fading pulse added. */
void a_pulse_that_fits_sounds_as_before() {
    CueEngine engine;
    int voice = engine.add_voice(pulsed("tick", constant(0.5f, 480), 0.01f));
    engine.start(voice, at(0.0f, 0.01f));
    std::vector<float> out = render(engine, 4800);
    for (int f = 240; f < 4800; f++) {
        assert(near(out[2 * static_cast<size_t>(f)], 0.5f * kCentredFull, 1e-6));
    }
}

void starts_and_stops_are_logged_once() {
    CueEngine engine;
    int voice = engine.add_voice(pulsed("tick", constant(0.5f, 480), 0.02f));
    s_lines.clear();
    engine.start(voice, at(0.5f, 0.25f));
    engine.start(voice, at(0.25f, 0.25f));
    engine.stop(voice);
    engine.stop(voice);
    assert(s_lines.size() == 2);
    assert(s_lines[0] == "[a11y] cue tick start pan 0.50 pitch 1.00 gain 1.00 interval 0.250");
    assert(s_lines[1] == "[a11y] cue tick stop pan 0.25 pitch 1.00 gain 1.00 interval 0.250");
}

VoiceSpec once(const char* name) {
    VoiceSpec spec;
    spec.name = name;
    spec.mode = VoiceMode::once;
    return spec;
}

/* A voice's line: a buffer handed over at run time, played from its start
 * once, ramped in and out, and then silence. */
void a_once_voice_plays_its_buffer_once() {
    const float most = kCentredFull / 50.0f;
    CueEngine engine;
    int voice = engine.add_voice(once("your voice"));
    assert(voice == 0);
    assert(!engine.playing(voice));
    assert(engine.play(voice, constant(1.0f, 4800)));
    assert(engine.playing(voice));
    std::vector<float> out = render(engine, kCueRate / 5);
    auto [first, last] = sounding_span(out);
    assert(first == 0 && last == 4799);
    /* Ramps at both ends: no step bigger than a ramp's, full level in the
     * middle. */
    assert(largest_step(out, 0) < most);
    assert(near(out[2 * 2400], kCentredFull, 1e-4));
    assert(!engine.playing(voice));
    /* Not looped: silence after. */
    assert(peak(render(engine, kCueRate / 5)) == 0.0f);
    /* Again plays it again, from the start. */
    assert(engine.play(voice, constant(1.0f, 4800)));
    out = render(engine, kCueRate / 5);
    assert(sounding_span(out) == std::make_pair(0, 4799));
    /* Nothing was logged for a line: the voices log their own. */
    s_lines.clear();
    engine.play(voice, constant(1.0f, 480));
    render(engine, 960);
    assert(s_lines.empty());
}

/* A new line while one plays cuts it with the stop ramp and starts at
 * once; a line handed over before the last was taken replaces it. */
void a_once_voice_cuts_with_a_ramp() {
    const float most = kCentredFull / 50.0f;
    CueEngine engine;
    int voice = engine.add_voice(once("opponents' voice"));
    engine.play(voice, constant(1.0f, kCueRate));
    std::vector<float> out = render(engine, 4800);
    float before = out[out.size() - 2];
    assert(near(before, kCentredFull, 1e-4));
    engine.play(voice, constant(0.5f, kCueRate));
    out = render(engine, 4800);
    assert(largest_step(out, 0, before) < most);
    /* Down within the ramp, up again, and at the new line's level. */
    float lowest = 1.0f;
    for (int f = 0; f < 480; f++) {
        lowest = std::min(lowest, out[2 * static_cast<size_t>(f)]);
    }
    assert(lowest < most);
    assert(near(out[out.size() - 2], 0.5f * kCentredFull, 1e-4));
    /* Two handed over between blocks: the later one plays, the earlier
     * never does. */
    engine.play(voice, constant(0.25f, kCueRate));
    engine.play(voice, constant(0.125f, kCueRate));
    out = render(engine, 4800);
    assert(near(out[out.size() - 2], 0.125f * kCentredFull, 1e-4));
    /* Stopped, it ramps out and is done. */
    engine.stop(voice);
    before = out[out.size() - 2];
    out = render(engine, 4800);
    assert(largest_step(out, 0, before) < most);
    assert(peak(std::vector<float>(out.begin() + 960, out.end())) == 0.0f);
    assert(!engine.playing(voice));
    /* Many lines in a row never run out of room. */
    for (int i = 0; i < 20; i++) {
        assert(engine.play(voice, constant(0.5f, 960)));
        render(engine, 480);
    }
    render(engine, 4800);
    assert(!engine.playing(voice));
}

/* A voice's chain is master volume × its own gain (the voice's Volume and
 * boost): the cue volume, the cue switches and any cue's level leave it
 * alone. */
void a_once_voice_takes_master_and_its_own_volume() {
    CueEngine engine;
    int voice = engine.add_voice(once("your voice"));
    engine.set_master_volume(0.5f);
    engine.set_mix_level(CueLevel{false, 0.1f, 1.0f});
    engine.set_cue_level(0, CueLevel{false, 0.2f, 3.0f});
    engine.set_targets(voice, at(0.0f, 0.5f, 0.6f));
    engine.play(voice, constant(1.0f, kCueRate));
    std::vector<float> out = render(engine, 4800);
    assert(near(out[out.size() - 2], 0.5 * 0.6 * kCentredFull, 1e-4));
    /* The volume is live, with a glide. */
    engine.set_targets(voice, at(0.0f, 0.5f, 1.0f));
    out = render(engine, 4800);
    assert(near(out[out.size() - 2], 0.5 * kCentredFull, 1e-4));
    /* A boost lifts it past 1, up to the most boost; a cue's voice stops
     * at 1. */
    engine.set_targets(voice, at(0.0f, 0.5f, 2.0f));
    out = render(engine, 4800);
    assert(near(out[out.size() - 2], kCentredFull, 1e-4));
    engine.set_targets(voice, at(0.0f, 0.5f, 100.0f));
    engine.play(voice, constant(0.01f, kCueRate));
    out = render(engine, 4800);
    assert(near(out[out.size() - 2], 0.5 * 0.01 * CueEngine::kMostBoost * kCentredFull, 1e-4));
    engine.set_master_volume(0.0f);
    render(engine, 4800);
    assert(peak(render(engine, 4800)) < 1e-5f);
}

VoiceSpec one_shot(const char* name, Sound sound) {
    VoiceSpec spec;
    spec.name = name;
    spec.sound = std::move(sound);
    spec.mode = VoiceMode::one_shot;
    return spec;
}

VoiceTargets placed(float pan, float pitch, float gain = 1.0f) {
    VoiceTargets targets;
    targets.pan = pan;
    targets.pitch = pitch;
    targets.gain = gain;
    return targets;
}

/* A one-shot plays its sound once from the start at each trigger, at its
 * pan and pitch, ramped in and out, then falls silent by itself. */
void a_one_shot_plays_once_at_its_pan_and_pitch() {
    const float most = kCentredFull / 50.0f;
    CueEngine engine;
    int voice = engine.add_voice(one_shot("appear", constant(1.0f, 4800)));
    assert(voice == 0);
    assert(!engine.playing(voice));
    s_lines.clear();
    engine.trigger(voice, placed(0.0f, 1.0f));
    assert(engine.playing(voice));
    assert(logged("[a11y] cue appear play pan 0.00 pitch 1.00 gain 1.00"));
    std::vector<float> out = render(engine, kCueRate / 5);
    assert(sounding_span(out) == std::make_pair(0, 4799));
    assert(largest_step(out, 0) < most);
    assert(near(out[2 * 2400], kCentredFull, 1e-4));
    assert(!engine.playing(voice));
    assert(peak(render(engine, kCueRate / 5)) == 0.0f);
    /* Full left and an octave up: half as long, nothing on the right. */
    engine.trigger(voice, placed(-1.0f, 2.0f));
    out = render(engine, kCueRate / 5);
    assert(sounding_span(out) == std::make_pair(0, 2399));
    assert(largest_step(out, 1) < 1e-6f);
    assert(near(out[2 * 1200], 2.0f * kCentredFull * std::sqrt(0.5f), 1e-4));
    assert(!engine.playing(voice));
    /* Nothing but a trigger plays it, and only a one-shot can be
     * triggered. */
    engine.start(voice, placed(0.0f, 1.0f));
    assert(!engine.playing(voice));
    assert(peak(render(engine, 4800)) == 0.0f);
    int tick = engine.add_voice(pulsed("tick", constant(0.5f, 480), 0.02f));
    s_lines.clear();
    engine.trigger(tick, placed(0.0f, 1.0f));
    assert(!engine.playing(tick));
    assert(logged("[a11y] cue voice 1 cannot be triggered"));
    /* A one-shot needs a sound. */
    assert(engine.add_voice(one_shot("empty", Sound{})) == -1);
    assert(logged("[a11y] cue empty refused: no sound"));
}

/* A one-shot is a cue's voice: its cue's switch, volume and boost and the
 * mix's reach it; a preview's takes the volumes alone. */
void a_one_shot_takes_its_cue_level() {
    CueEngine engine;
    VoiceSpec spec = one_shot("appear", constant(1.0f, 10 * kCueRate));
    spec.cue = 2;
    int voice = engine.add_voice(spec);
    engine.set_master_volume(0.5f);
    engine.set_mix_level(CueLevel{true, 0.8f, 1.0f});
    engine.set_cue_level(2, CueLevel{true, 0.5f, 2.0f});
    engine.set_cue_level(0, CueLevel{true, 0.1f, 1.0f}); /* another cue's */
    engine.trigger(voice, placed(0.0f, 1.0f, 0.6f));
    std::vector<float> out = render(engine, 4800);
    assert(near(out[out.size() - 2], 0.5 * 0.8 * 0.5 * 2.0 * 0.6 * kCentredFull, 1e-4));
    /* Its gain stops at 1: the boost is its cue level's. */
    engine.set_targets(voice, placed(0.0f, 1.0f, 2.0f));
    out = render(engine, 4800);
    assert(near(out[out.size() - 2], 0.5 * 0.8 * 0.5 * 2.0 * kCentredFull, 1e-4));
    /* Its cue's switch off, then the mix's: silent, still playing. */
    engine.set_cue_level(2, CueLevel{false, 0.5f, 2.0f});
    render(engine, 4800);
    assert(peak(render(engine, 4800)) < 1e-5f);
    assert(engine.playing(voice));
    engine.set_cue_level(2, CueLevel{});
    engine.set_mix_level(CueLevel{false, 1.0f, 1.0f});
    render(engine, 4800);
    assert(peak(render(engine, 4800)) < 1e-5f);
    engine.set_mix_level(CueLevel{});
    out = render(engine, 4800);
    assert(near(out[out.size() - 2], 0.5 * kCentredFull, 1e-4));

    /* A preview's one-shot sounds with both switches off, at the volumes. */
    VoiceSpec preview_spec = one_shot("appear preview", constant(1.0f, kCueRate));
    preview_spec.ignores_switches = true;
    CueEngine other;
    int preview = other.add_voice(preview_spec);
    other.set_mix_level(CueLevel{false, 0.8f, 1.0f});
    other.set_cue_level(0, CueLevel{false, 0.5f, 2.0f});
    other.trigger(preview, placed(0.0f, 1.0f));
    out = render(other, 4800);
    assert(near(out[out.size() - 2], 0.8 * 0.5 * 2.0 * kCentredFull, 1e-4));
}

/* A trigger while it plays restarts it from the top, the old play fading
 * out as the new one ramps in: no click. A stop ramps it out. */
void a_one_shot_retriggered_restarts_without_a_click() {
    const float most = kCentredFull / 50.0f;
    CueEngine engine;
    int voice = engine.add_voice(one_shot("appear", constant(1.0f, 9600)));
    engine.trigger(voice, placed(0.0f, 1.0f));
    std::vector<float> out = render(engine, 4800);
    float before = out[out.size() - 2];
    assert(near(before, kCentredFull, 1e-4));
    engine.trigger(voice, placed(0.0f, 1.0f, 0.5f));
    assert(engine.playing(voice));
    out = render(engine, 4 * 4800);
    assert(largest_step(out, 0, before) < most);
    /* The whole sound again from the retrigger, at the new gain. */
    assert(sounding_span(out) == std::make_pair(0, 9599));
    assert(near(out[2 * 4800], 0.5f * kCentredFull, 1e-4));
    /* From full left to full right: the old play fades out on the left,
     * the new ramps in on the right. */
    engine.trigger(voice, placed(-1.0f, 1.0f));
    out = render(engine, 4800);
    float left = out[out.size() - 2];
    float right = out[out.size() - 1];
    engine.trigger(voice, placed(1.0f, 1.0f));
    out = render(engine, 4800);
    assert(largest_step(out, 0, left) < most);
    assert(largest_step(out, 1, right) < most);
    assert(sounding_span(out, 0).second < 300);
    assert(near(out[out.size() - 1], kCentredFull * std::sqrt(2.0f), 1e-4));
    /* Stopped, it ramps out and is done; a trigger plays it again. */
    engine.stop(voice);
    before = out[out.size() - 1];
    out = render(engine, 4800);
    assert(largest_step(out, 1, before) < most);
    assert(peak(std::vector<float>(out.begin() + 2 * 480, out.end())) == 0.0f);
    assert(!engine.playing(voice));
    engine.trigger(voice, placed(0.0f, 1.0f));
    out = render(engine, 4800);
    assert(near(out[out.size() - 2], kCentredFull, 1e-4));
    /* A trigger stopped before the audio thread takes it never plays. */
    engine.stop(voice);
    render(engine, 4800);
    engine.trigger(voice, placed(0.0f, 1.0f));
    engine.stop(voice);
    assert(peak(render(engine, 4800)) == 0.0f);
    assert(!engine.playing(voice));
}

/* A pool's triggers each take a voice of their own while one is free, so a
 * burst overlaps; a full pool takes the voice triggered longest ago. */
void a_pool_overlaps_and_takes_the_oldest() {
    constexpr int kSize = a11y::OneShotPool::kSize;
    CueEngine engine;
    a11y::OneShotPool pool;
    assert(!pool.playing(engine));
    assert(pool.trigger(engine, placed(0.0f, 1.0f)) == -1); /* none added */
    s_lines.clear();
    assert(pool.add(engine, one_shot("appear", constant(0.25f, 4800))));
    assert(static_cast<int>(pool.voices().size()) == kSize);
    /* A burst on one tick: every voice, each logged by its name. */
    std::vector<int> taken;
    for (int i = 0; i < kSize; i++) {
        taken.push_back(pool.trigger(engine, placed(0.0f, 1.0f)));
    }
    std::vector<int> sorted = taken;
    std::sort(sorted.begin(), sorted.end());
    assert(sorted.front() != -1);
    assert(std::adjacent_find(sorted.begin(), sorted.end()) == sorted.end());
    assert(logged("[a11y] cue appear 1 play pan 0.00 pitch 1.00 gain 1.00"));
    assert(logged(
        "[a11y] cue appear " + std::to_string(kSize) + " play pan 0.00 pitch 1.00 gain 1.00"));
    assert(pool.playing(engine));
    /* All sound together. */
    std::vector<float> out = render(engine, 2400);
    assert(near(out[out.size() - 2], kSize * 0.25 * kCentredFull, 1e-4));
    /* Full: the next two take the oldest, in order. */
    assert(pool.trigger(engine, placed(0.0f, 1.0f)) == taken[0]);
    assert(pool.trigger(engine, placed(0.0f, 1.0f)) == taken[1]);
    out = render(engine, 4800);
    /* Those two were restarted, the rest played out. */
    assert(near(out[2 * 3000], 2 * 0.25 * kCentredFull, 1e-4));
    assert(engine.playing(taken[0]) && engine.playing(taken[1]));
    assert(!engine.playing(taken[2]));
    /* A free voice is taken before the oldest playing one. */
    int next = pool.trigger(engine, placed(0.0f, 1.0f));
    assert(next != taken[0] && next != taken[1]);
    /* Stopped, all ramp out. */
    pool.stop(engine);
    render(engine, 4800);
    assert(!pool.playing(engine));
    /* A pool that does not fit the engine keeps the voices it got. */
    CueEngine small;
    for (int i = 0; i < CueEngine::kMaxVoices - 2; i++) {
        small.add_voice(one_shot("filler", constant(0.25f, 480)));
    }
    a11y::OneShotPool partial;
    assert(!partial.add(small, one_shot("appear", constant(0.25f, 480))));
    assert(partial.voices().size() == 2);
    assert(partial.trigger(small, placed(0.0f, 1.0f)) != -1);
}

/* Copies a semitone apart across the span, each made at its pitch; a
 * pitch finds the nearest, the first or last past the span. */
void pitched_copies_span_their_pitches() {
    std::vector<float> asked;
    std::shared_ptr<const PitchedCopies> copies =
        a11y::pitched_copies(0.5f, 2.0f, [&asked](float pitch) {
            asked.push_back(pitch);
            return constant(pitch, 10);
        });
    assert(copies->sounds.size() == 25);
    assert(asked.size() == 25);
    assert(near(copies->pitch_of(0), 0.5, 1e-6));
    assert(near(copies->pitch_of(12), 1.0, 1e-6));
    assert(near(copies->pitch_of(24), 2.0, 1e-5));
    assert(near(asked[13], std::exp2(1.0 / 12.0), 1e-5));
    assert(copies->nearest(1.0f) == 12);
    assert(copies->nearest(1.02f) == 12);
    assert(copies->nearest(1.04f) == 13);
    assert(copies->nearest(0.3f) == 0);
    assert(copies->nearest(5.0f) == 24);
    /* A span short of a whole semitone still ends past it. */
    assert(a11y::pitched_copies(1.0f, 1.03f, [](float) {
        return constant(1.0f, 10);
    })->sounds.size() == 2);
}

/* Copies whose level tells which one plays: a quarter of its pitch. */
std::shared_ptr<const PitchedCopies> level_copies(int frames) {
    return a11y::pitched_copies(
        0.5f, 2.0f, [frames](float pitch) { return constant(pitch / 4.0f, frames); });
}

/* The frames the left channel sounds within a slice of the mix. */
int sounding_frames(const std::vector<float>& out, int from, int to) {
    auto [first, last] =
        sounding_span(std::vector<float>(out.begin() + 2 * static_cast<std::ptrdiff_t>(from),
            out.begin() + 2 * static_cast<std::ptrdiff_t>(to)));
    return first == -1 ? 0 : last - first + 1;
}

/* With copies, each pulse plays the copy nearest its pitch:
 * as long at every pitch, and only the rest of the pitch past the nearest
 * copy changes its length. */
void a_pulse_with_copies_keeps_its_length() {
    VoiceSpec spec = pulsed("pitched", Sound{}, 0.05f);
    spec.lowest_pitch = 0.5f;
    spec.copies = level_copies(2400);
    CueEngine engine;
    const int voice = engine.add_voice(spec);
    VoiceTargets targets = at(0.0f, 0.2f);
    targets.pitch = 2.0f;
    engine.start(voice, targets);
    std::vector<float> out = render(engine, 4800);
    /* An octave down from the next pulse on; the glide has long ended by
     * the pulse after. */
    targets.pitch = 0.5f;
    engine.set_targets(voice, targets);
    std::vector<float> more = render(engine, 4 * 9600 - 4800);
    out.insert(out.end(), more.begin(), more.end());
    /* Between copies: the nearest, 13 semitones over 0.5, at the rest. */
    targets.pitch = 1.03f;
    engine.set_targets(voice, targets);
    more = render(engine, 3 * 9600);
    out.insert(out.end(), more.begin(), more.end());
    check_onsets(onsets(out), {0, 9600, 19200, 28800, 38400, 48000, 57600});
    assert(sounding_frames(out, 0, 9600) == 2400);
    assert(near(out[2 * 1200], 0.5 * kCentredFull, 1e-5));
    assert(near(sounding_frames(out, 19200, 28800), 2400, 1));
    assert(near(out[2 * (19200 + 1200)], 0.125 * kCentredFull, 1e-5));
    const double copy = 0.5 * std::exp2(13.0 / 12.0);
    assert(near(sounding_frames(out, 57600, 67200), 2400 * copy / 1.03, 2));
    assert(near(out[2 * (57600 + 1200)], copy / 4.0 * kCentredFull, 1e-5));
}

/* A pulse just after a jump of pitch takes the copy of the pitch the glide
 * is heading for, not the one it has reached: the old one played at the
 * new pitch would last over twice as long. */
void a_pulse_after_a_jump_takes_its_target_copy() {
    VoiceSpec spec = pulsed("pitched", Sound{}, 0.05f);
    spec.lowest_pitch = 0.5f;
    spec.copies = level_copies(2400);
    CueEngine engine;
    const int voice = engine.add_voice(spec);
    VoiceTargets targets = at(0.0f, 0.2f);
    targets.pitch = 2.0f;
    engine.start(voice, targets);
    /* Two octaves down a millisecond before the second pulse. */
    std::vector<float> out = render(engine, 9600 - 48);
    targets.pitch = 0.5f;
    engine.set_targets(voice, targets);
    std::vector<float> more = render(engine, 9600 + 48);
    out.insert(out.end(), more.begin(), more.end());
    check_onsets(onsets(out), {0, 9600});
    assert(near(out[2 * (9600 + 100)], 0.125 * kCentredFull, 1e-5));
    /* The glide starts it fast, from nearly the old pitch: shorter than its
     * own length, never longer. */
    assert(sounding_frames(out, 9600, 19200) < 2400);
}

/* A one-shot with copies is as long at every pitch; past the copies' span
 * the last copy plays at the rest. */
void a_one_shot_with_copies_keeps_its_length() {
    VoiceSpec spec = one_shot("pitched", Sound{});
    spec.lowest_pitch = 0.5f;
    spec.copies = level_copies(4800);
    CueEngine engine;
    const int voice = engine.add_voice(spec);
    engine.trigger(voice, placed(0.0f, 2.0f));
    std::vector<float> out = render(engine, kCueRate / 5);
    assert(sounding_span(out) == std::make_pair(0, 4799));
    assert(near(out[2 * 2400], 0.5 * kCentredFull, 1e-5));
    engine.trigger(voice, placed(0.0f, 0.5f));
    out = render(engine, kCueRate / 5);
    assert(sounding_span(out) == std::make_pair(0, 4799));
    assert(near(out[2 * 2400], 0.125 * kCentredFull, 1e-5));
    engine.trigger(voice, placed(0.0f, 4.0f));
    out = render(engine, kCueRate / 5);
    assert(sounding_span(out) == std::make_pair(0, 2399));
    assert(near(out[2 * 1200], 0.5 * kCentredFull, 1e-5));
}

/* Copies take the place of a sound, never stand beside one; only a pulsed
 * voice or a one-shot plays them, every copy has a sound, and the copies a
 * pitch. */
void copies_are_refused_where_they_cannot_play() {
    CueEngine engine;
    VoiceSpec doubled = pulsed("doubled", constant(0.5f, 480), 0.02f);
    doubled.copies = level_copies(480);
    s_lines.clear();
    assert(engine.add_voice(doubled) == -1);
    assert(logged("[a11y] cue doubled refused: a sound and its copies both"));
    VoiceSpec hum = looped("hum", Sound{});
    hum.copies = level_copies(480);
    assert(engine.add_voice(hum) == -1);
    assert(logged("[a11y] cue hum refused: only a pulsed voice or a one-shot plays copies"));
    VoiceSpec tick = pulsed("tick", Sound{}, 0.02f);
    tick.copies = a11y::pitched_copies(
        0.5f, 2.0f, [](float pitch) { return pitch == 1.0f ? Sound{} : constant(0.5f, 480); });
    assert(engine.add_voice(tick) == -1);
    assert(logged("[a11y] cue tick refused: an empty copy"));
    VoiceSpec unpitched = pulsed("unpitched", Sound{}, 0.02f);
    auto copies = std::make_shared<a11y::PitchedCopies>();
    copies->lowest = 0.0f;
    copies->sounds.push_back(constant(0.5f, 480));
    unpitched.copies = copies;
    assert(engine.add_voice(unpitched) == -1);
    assert(logged("[a11y] cue unpitched refused: copies with no lowest pitch"));
}

/* Zero crossings a second over a slice of a sound. */
double crossing_rate(const Sound& sound, int from, int to) {
    int crossings = 0;
    for (int i = from + 1; i < to; i++) {
        const float a = sound.samples[static_cast<size_t>(i - 1)];
        const float b = sound.samples[static_cast<size_t>(i)];
        if ((a < 0.0f) != (b < 0.0f)) {
            crossings++;
        }
    }
    return crossings * static_cast<double>(kCueRate) / (to - from);
}

double rms(const Sound& sound, int from, int to) {
    double sum = 0.0;
    for (int i = from; i < to; i++) {
        const double sample = sound.samples[static_cast<size_t>(i)];
        sum += sample * sample;
    }
    return std::sqrt(sum / (to - from));
}

/* The spectral shift moves a tone's pitch and keeps its length, timing and
 * level; a factor of 1 leaves the sound as it was. */
void a_shifted_sound_keeps_its_length() {
    Sound tone = sine(1000.0, kCueRate / 10, 0.5);
    a11y::shape_edges(tone, 0.005f);
    a11y::lead_in(tone, 0.012f);
    const int frames = static_cast<int>(tone.samples.size());
    const int from = frames / 4;
    const int to = frames * 3 / 4;
    assert(a11y::shift_pitch(tone, 1.0f).samples == tone.samples);
    for (float factor : {0.5f, 2.0f}) {
        Sound shifted = a11y::shift_pitch(tone, factor);
        assert(static_cast<int>(shifted.samples.size()) == frames);
        /* A crossing twice a cycle. */
        assert(near(crossing_rate(shifted, from, to), 2000.0 * factor, 40.0));
        assert(near(20.0 * std::log10(rms(shifted, from, to) / rms(tone, from, to)), 0.0, 1.5));
        /* Quiet through the lead-in to within a millisecond of its end, as
         * the tone is silent, and sounding once the tone's fade-in is done:
         * the shift keeps the onset where it was. */
        constexpr int kLeadIn = kCueRate * 12 / 1000;
        constexpr int kMillisecond = kCueRate / 1000;
        const double steady = rms(tone, from, to);
        assert(rms(shifted, 0, kLeadIn - kMillisecond) < 0.05 * steady);
        assert(
            rms(shifted, kLeadIn + 5 * kMillisecond, kLeadIn + 10 * kMillisecond) > 0.5 * steady);
    }
    assert(a11y::shift_pitch(Sound{}, 2.0f).samples.empty());
}

/* A 16-bit PCM WAV of frames frames, each channel holding its constant. */
void write_wav(
    const std::filesystem::path& path, int rate, const std::vector<float>& channels, int frames) {
    const auto put = [](std::ofstream& file, uint32_t value, int bytes) {
        for (int i = 0; i < bytes; i++) {
            file.put(static_cast<char>((value >> (8 * i)) & 0xFF));
        }
    };
    const uint32_t count = static_cast<uint32_t>(channels.size());
    const uint32_t data = static_cast<uint32_t>(frames) * count * 2;
    std::ofstream file(path, std::ios::binary);
    file.write("RIFF", 4);
    put(file, 36 + data, 4);
    file.write("WAVEfmt ", 8);
    put(file, 16, 4);
    put(file, 1, 2); /* PCM */
    put(file, count, 2);
    put(file, static_cast<uint32_t>(rate), 4);
    put(file, static_cast<uint32_t>(rate) * count * 2, 4);
    put(file, count * 2, 2);
    put(file, 16, 2);
    file.write("data", 4);
    put(file, data, 4);
    for (int i = 0; i < frames; i++) {
        for (float value : channels) {
            put(file, static_cast<uint16_t>(static_cast<int16_t>(value * 32767.0f)), 2);
        }
    }
}

/* A file at another rate and channel count comes out mono at the cue rate,
 * after the lead-in's silence; a folder or name outside ASCII is found. */
void a_sound_file_loads_mono_at_the_cue_rate(const std::filesystem::path& folder) {
    std::filesystem::create_directories(folder);
    const std::filesystem::path path = folder / u8"ääni.wav";
    write_wav(path, 24000, {0.25f, 0.25f}, 2400);
    s_lines.clear();
    const std::u8string utf8 = path.u8string();
    Sound sound = a11y::load_sound(std::string(utf8.begin(), utf8.end()));
    assert(s_lines.empty());
    const int lead = static_cast<int>(a11y::kLeadInSeconds * kCueRate);
    const int size = static_cast<int>(sound.samples.size());
    assert(std::abs(size - (lead + kCueRate / 10)) <= 2);
    for (int i = 0; i < lead; i++) {
        assert(sound.samples[static_cast<size_t>(i)] == 0.0f);
    }
    const float middle = sound.samples[static_cast<size_t>(lead + kCueRate / 20)];
    assert(std::abs(middle - 0.25f) < 0.01f);
}

/* A missing file and one that is no sound give an empty sound and one line
 * naming the file and why. */
void a_sound_file_missing_or_unreadable_is_silent(const std::filesystem::path& folder) {
    std::filesystem::create_directories(folder);
    const std::string missing = (folder / "missing.wav").string();
    std::filesystem::remove(missing);
    s_lines.clear();
    assert(a11y::load_sound(missing).samples.empty());
    assert(s_lines.size() == 1);
    assert(s_lines[0] == "[a11y] cue sound " + missing + " not loaded: no such file");

    const std::string text = (folder / "text.wav").string();
    std::ofstream(text) << "not a sound";
    s_lines.clear();
    assert(a11y::load_sound(text).samples.empty());
    assert(s_lines.size() == 1);
    assert(s_lines[0].starts_with("[a11y] cue sound " + text + " not loaded: cannot decode ("));
}

void the_trace_has_a_line_per_voice_each_tick(const std::string& path) {
    {
        CueEngineOptions options;
        options.trace = path;
        CueEngine engine(options);
        int tick = engine.add_voice(pulsed("tick", constant(0.5f, 480), 0.02f));
        engine.add_voice(looped("ledge right", constant(0.5f, 480)));
        engine.start(tick, at(-0.5f, 0.3f, 0.75f));
        engine.trace_tick(7);
    }
    std::ifstream file(path);
    std::string header, first, second, end;
    std::getline(file, header);
    std::getline(file, first);
    std::getline(file, second);
    assert(header == "# tick\tvoice\tstate\tpan\tpitch\tgain\tinterval");
    assert(first == "7\ttick\ton\t-0.500\t1.000\t0.750\t0.300");
    assert(second == "7\tledge right\toff\t0.000\t1.000\t1.000\t-");
    assert(!std::getline(file, end));
}

/* Two seconds: a 1 kHz tick every 0.25 s on the right, then every 0.125 s
 * on the left, each after 12 ms of lead-in silence. */
void write_pulse_train(const std::string& path) {
    {
        CueEngineOptions options;
        options.dump = path;
        CueEngine engine(options);
        Sound tick = sine(1000.0, kCueRate * 40 / 1000, 0.8);
        a11y::shape_edges(tick, 0.002f);
        a11y::lead_in(tick, 0.012f);
        int voice = engine.add_voice(pulsed("tick", tick, 0.1f));
        engine.start(voice, at(0.5f, 0.25f));
        render(engine, kCueRate);
        engine.set_targets(voice, at(-0.5f, 0.125f));
        render(engine, kCueRate);
    }
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    assert(file.tellg() == static_cast<std::streamoff>(2 * kCueRate * 2 * sizeof(float)));
}

}  // namespace

extern "C" void pc_log_line(const char* fmt, ...) {
    char line[512];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    s_lines.emplace_back(line);
}

int main(int argc, char** argv) {
    const std::string dump = argc > 1 ? argv[1] : "cue_pulse_train.f32";
    constant_power_pan_keeps_loudness();
    starts_and_stops_do_not_jump();
    pulses_follow_their_interval();
    alternating_voices_pulse_between_each_other();
    the_volume_chain_multiplies();
    a_voice_past_the_switches_keeps_every_volume();
    five_voices_stay_under_full_scale();
    a_long_pulse_is_cut_with_a_fade();
    a_pulse_that_fits_sounds_as_before();
    starts_and_stops_are_logged_once();
    a_once_voice_plays_its_buffer_once();
    a_once_voice_cuts_with_a_ramp();
    a_once_voice_takes_master_and_its_own_volume();
    a_one_shot_plays_once_at_its_pan_and_pitch();
    a_one_shot_takes_its_cue_level();
    a_one_shot_retriggered_restarts_without_a_click();
    a_pool_overlaps_and_takes_the_oldest();
    pitched_copies_span_their_pitches();
    a_pulse_with_copies_keeps_its_length();
    a_pulse_after_a_jump_takes_its_target_copy();
    a_one_shot_with_copies_keeps_its_length();
    copies_are_refused_where_they_cannot_play();
    a_shifted_sound_keeps_its_length();
    a_sound_file_loads_mono_at_the_cue_rate(dump + ".sounds");
    a_sound_file_missing_or_unreadable_is_silent(dump + ".sounds");
    the_trace_has_a_line_per_voice_each_tick(dump + ".trace");
    write_pulse_train(dump);
    std::cout << "cue_engine: all tests passed\n";
    return 0;
}
