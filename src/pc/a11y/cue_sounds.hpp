/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Making cue sounds, at engine start: a cue's sound is a mono buffer
 * (Sound, cue_engine.hpp), filled here by code, from any miniaudio data
 * source, or from a file, and its copies at other pitches. */
#pragma once
#include "cue_engine.hpp"
#include <functional>
#include <memory>
#include <string>

/* miniaudio.h's own declaration, so including this header does not pull in
 * all of miniaudio. */
typedef void ma_data_source;

namespace a11y {

/* Up to frames of a data source that gives 32-bit float mono at kCueRate (a
 * waveform, noise, a decoder set to that format); fewer if it ends first. */
Sound read_sound(ma_data_source* source, int frames);

/* Raised-cosine edges of the given length at both ends, so the sound starts
 * and ends in silence. */
void shape_edges(Sound& sound, float seconds);

/* A raised-cosine edge of the given length at the start alone, for a soft
 * attack. */
void fade_in(Sound& sound, float seconds);

/* A raised-cosine edge of the given length at the end alone, so the sound
 * ends in silence. */
void fade_out(Sound& sound, float seconds);

/* Silence of the given length before the sound: room for the start ramp, so
 * it does not eat the sound's attack. */
void lead_in(Sound& sound, float seconds);

/* The lead-in a pulse or one-shot needs, about twice the start ramp, the
 * Star Fox 64 fork's finding. tools/a11y/prepare_sounds.py counts it in its
 * report: change both together. */
constexpr float kLeadInSeconds = 0.012f;

/* A sound from a file, its path UTF-8: WAV, FLAC or MP3, any rate and
 * channel count, decoded by miniaudio to mono at kCueRate, with kLeadInSeconds
 * before it, as tools/a11y/prepare_sounds.py leaves files without. Empty when
 * the file is missing or cannot be decoded, which is logged naming the file
 * and why: the cue playing it stays silent. */
Sound load_sound(const std::string& path);

/* A sound's copies a semitone apart, from lowest up to highest or less
 * than a semitone past it, each made by make at its pitch: made again by
 * the code that made the sound, or a file's sound moved there by
 * shift_pitch (pitch_shift.hpp). A voice that plays them keeps the sound's
 * length at every pitch (cue_engine.hpp, VoiceSpec). */
std::shared_ptr<const PitchedCopies> pitched_copies(
    float lowest, float highest, const std::function<Sound(float pitch)>& make);

}  // namespace a11y
