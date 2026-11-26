/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Android's own text-to-speech engine, the fork's reader and voices on a
 * phone or tablet. Prism has no Android build, so this platform's speech
 * goes through the Java side instead
 * (platforms/android/app/src/main/java/dev/melee/MeleeSpeech.java), which
 * owns one Android TextToSpeech for the reader and one per voice.
 *
 * Nothing here includes a JNI header outside android_tts.cpp: a11y.cmake
 * builds this file on Android alone, and android_bridge.cpp, which is the
 * same three factories as the other platforms, calls these functions only.
 * Safe to call from the game thread (speech) and from the voices' worker
 * (rendering) at the same time; Java serialises the engines' own calls on
 * the main thread. */
#pragma once
#include "screen_reader_bridge.hpp"
#include <string>
#include <string_view>
#include <vector>

namespace a11y::android {

/* Creates the reader and, when instances is above zero and they are not up
 * yet, that many voice engines. False when the device has no engine, or the
 * Java object cannot be reached; the caller logs why it has no speech. */
bool start(int instances);
/* As the game exits. Engines already created are shut down; a later start()
 * would need Java to build new ones, which only a new run does. */
void stop();
/* The engine's package in the log, "com.google.android.tts" on most
 * devices, behind "Android TextToSpeech". */
std::string engine_name();

/* The reader: interrupt cuts off the line being spoken. */
bool speak(std::string_view utf8, bool interrupt);

/* The installed voices, as engine 0 lists them, in the order the indexes
 * below follow. */
std::vector<VoiceInfo> voices();
/* The index of the engine's own default voice, -1 when it will not say. */
int default_voice();
bool set_voice(int instance, int index);
/* 0 to 1, 0.5 the engine's normal speed (TextToSpeech.setSpeechRate 1.0). */
bool set_rate(int instance, float rate);
/* The line synthesized to memory, mono or stereo at the engine's own rate
 * (often 22050 Hz). False when the engine will not synthesize to a file, a
 * device that then has no voices and speaks everything through the reader. */
bool render(int instance, std::string_view utf8, RenderedLine& out);

}  // namespace a11y::android
