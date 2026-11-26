/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The cue engine's output: its own SDL audio stream on the default playback
 * device, beside the game's, which SDL mixes with it (ADR-0005). SDL's audio
 * thread pulls the mix from CueEngine::render. Also where the cues' sound
 * files are, the cue engine's other question for SDL. */
#pragma once
#include <memory>
#include <string>

struct SDL_AudioStream;

namespace a11y {

class CueEngine;

class CueOutput {
public:
    /* Opens the stream and starts it playing; nullptr, with the reason in
     * why, when SDL cannot. Logs the driver, the device's rate and its
     * buffer size. The engine must outlive the output. */
    static std::unique_ptr<CueOutput> open(CueEngine& engine, bool log, std::string& why);
    /* No callback runs once this returns. */
    ~CueOutput();
    CueOutput(const CueOutput&) = delete;
    CueOutput& operator=(const CueOutput&) = delete;

private:
    explicit CueOutput(CueEngine& engine) : m_engine(engine) {}
    static void pull(void* userdata, SDL_AudioStream* stream, int additional, int total);

    CueEngine& m_engine;
    SDL_AudioStream* m_stream = nullptr;
    bool m_audio_initialized = false;
};

/* The folder of the cues' sound files, UTF-8 with a trailing separator:
 * resources/a11y/ in SDL's base path, beside melee.exe, as the base port
 * finds its own resources (in a Mac package, Contents/Resources/resources/
 * a11y/). load_sound (cue_sounds.hpp) takes it with a file's name. */
std::string cue_sound_folder();

}  // namespace a11y
