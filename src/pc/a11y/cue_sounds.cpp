/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "cue_sounds.hpp"
#include "miniaudio.h"
#include "pc/pc.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <memory>
#include <numbers>
#include <vector>

namespace a11y {

Sound read_sound(ma_data_source* source, int frames) {
    Sound sound;
    sound.samples.resize(static_cast<size_t>(std::max(frames, 0)));
    ma_uint64 read = 0;
    if (ma_data_source_read_pcm_frames(source, sound.samples.data(), sound.samples.size(), &read) !=
        MA_SUCCESS)
    {
        read = 0;
    }
    sound.samples.resize(static_cast<size_t>(read));
    return sound;
}

namespace {

/* Raised-cosine gain i frames into an edge of edge frames: 0 to nearly 1. */
float edge_gain(int i, int edge) {
    return 0.5f - 0.5f * std::cos(std::numbers::pi_v<float> * static_cast<float>(i) /
                                  static_cast<float>(edge));
}

void fade_in_frames(Sound& sound, int edge) {
    for (int i = 0; i < edge; i++) {
        sound.samples[static_cast<size_t>(i)] *= edge_gain(i, edge);
    }
}

void fade_out_frames(Sound& sound, int edge) {
    const int size = static_cast<int>(sound.samples.size());
    for (int i = 0; i < edge; i++) {
        sound.samples[static_cast<size_t>(size - 1 - i)] *= edge_gain(i, edge);
    }
}

}  // namespace

void shape_edges(Sound& sound, float seconds) {
    const int size = static_cast<int>(sound.samples.size());
    const int edge = std::min(static_cast<int>(seconds * kCueRate), size / 2);
    fade_in_frames(sound, edge);
    fade_out_frames(sound, edge);
}

void fade_in(Sound& sound, float seconds) {
    const int size = static_cast<int>(sound.samples.size());
    fade_in_frames(sound, std::min(static_cast<int>(seconds * kCueRate), size));
}

void fade_out(Sound& sound, float seconds) {
    const int size = static_cast<int>(sound.samples.size());
    fade_out_frames(sound, std::min(static_cast<int>(seconds * kCueRate), size));
}

void lead_in(Sound& sound, float seconds) {
    const int frames = std::max(static_cast<int>(seconds * kCueRate), 0);
    sound.samples.insert(sound.samples.begin(), static_cast<size_t>(frames), 0.0f);
}

namespace {

/* The decoded file, or empty with why. Read into memory first, through a
 * filesystem path made from the UTF-8 text, so an install folder outside
 * ASCII opens on Windows too, where miniaudio's own file reading takes the
 * system code page. */
Sound decode_file(const std::string& path, std::string& why) {
    const std::filesystem::path file(
        std::u8string(reinterpret_cast<const char8_t*>(path.data()), path.size()));
    std::error_code error;
    if (!std::filesystem::is_regular_file(file, error)) {
        why = "no such file";
        return {};
    }
    std::ifstream stream(file, std::ios::binary);
    if (!stream.is_open()) {
        why = "cannot open it";
        return {};
    }
    const std::vector<char> bytes{
        std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
    const ma_decoder_config config = ma_decoder_config_init(ma_format_f32, 1, kCueRate);
    ma_decoder decoder;
    const ma_result opened = ma_decoder_init_memory(bytes.data(), bytes.size(), &config, &decoder);
    if (opened != MA_SUCCESS) {
        why = std::string("cannot decode (") + ma_result_description(opened) + ")";
        return {};
    }
    constexpr ma_uint64 kChunk = 4096;
    Sound sound;
    for (;;) {
        const size_t done = sound.samples.size();
        sound.samples.resize(done + kChunk);
        ma_uint64 read = 0;
        const ma_result result =
            ma_decoder_read_pcm_frames(&decoder, sound.samples.data() + done, kChunk, &read);
        sound.samples.resize(done + static_cast<size_t>(read));
        if (result != MA_SUCCESS || read == 0) {
            break;
        }
    }
    ma_decoder_uninit(&decoder);
    if (sound.samples.empty()) {
        why = "no sound in it";
    }
    return sound;
}

}  // namespace

Sound load_sound(const std::string& path) {
    std::string why;
    Sound sound = decode_file(path, why);
    if (sound.samples.empty()) {
        pc_log_line("[a11y] cue sound %s not loaded: %s", path.c_str(), why.c_str());
        return sound;
    }
    lead_in(sound, kLeadInSeconds);
    return sound;
}

std::shared_ptr<const PitchedCopies> pitched_copies(
    float lowest, float highest, const std::function<Sound(float pitch)>& make) {
    auto copies = std::make_shared<PitchedCopies>();
    copies->lowest = lowest;
    /* Past highest by less than a semitone at most; a hair under a whole
     * count of semitones is that count, not one more. */
    constexpr float kSlack = 1e-3f;
    const float semitones = PitchedCopies::kSemitonesPerOctave * std::log2(highest / lowest);
    const int count = std::max(1, static_cast<int>(std::ceil(semitones - kSlack)) + 1);
    for (int i = 0; i < count; i++) {
        copies->sounds.push_back(make(copies->pitch_of(i)));
    }
    return copies;
}

}  // namespace a11y
