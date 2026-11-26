/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The platform's half of the fork on Android: the three factories
 * screen_reader_bridge.cpp declares, over the Java TextToSpeech engine
 * (android_tts.hpp). Prism ships no Android build, so where the Windows and
 * macOS branches wire Prism in, a11y.cmake names A11Y_ANDROID and builds
 * this file instead. */
#include "android_tts.hpp"
#include "screen_reader_bridge.hpp"
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace a11y {
namespace {

/* The reader: Android speaks every announcement with its own text-to-speech,
 * at the accessibility stream so it ducks music and follows the phone's
 * speech volume. TalkBack itself does not read the game, so the fork
 * announces through the engine rather than through an accessibility event. */
class AndroidScreenReader final : public ScreenReaderBridge {
public:
    std::string init() override {
        if (!android::start(0)) {
            return {};
        }
        return android::engine_name();
    }

    void shutdown() override { android::stop(); }

    std::string output(std::string_view utf8, bool interrupt) override {
        return android::speak(utf8, interrupt) ? std::string() : "Android text-to-speech refused";
    }
};

/* The voices: engine 0 lists the installed voices for both, and engine n
 * renders voice n's lines to memory, which the cue engine plays as it plays
 * Prism's on the desktop. */
class AndroidVoiceBridge final : public VoiceBridge {
public:
    std::string open(int count) override {
        if (!android::start(count)) {
            return "Android text-to-speech did not start";
        }
        m_instances = count;
        return {};
    }

    /* The engines live in Java until the process ends, as Prism's context
     * does on Windows; stop() closes them with the reader. */
    void close() override {}

    std::vector<VoiceInfo> voices() override { return android::voices(); }

    int default_voice() override { return android::default_voice(); }

    std::string set_voice(int instance, int index) override {
        if (!instance_ok(instance)) {
            return "no such instance";
        }
        return android::set_voice(instance, index) ? std::string() : "no such voice";
    }

    std::string set_rate(int instance, float rate) override {
        if (!instance_ok(instance)) {
            return "no such instance";
        }
        return android::set_rate(instance, rate) ? std::string() : "the rate was refused";
    }

    std::string render(int instance, std::string_view utf8, RenderedLine& out) override {
        if (!instance_ok(instance)) {
            return "no such instance";
        }
        return android::render(instance, utf8, out) ? std::string() : "the voice did not render";
    }

    /* The engine's calls run on Android's main thread by way of the Java
     * side's handler, never waiting for the game thread, so there is no turn
     * to give back (unlike AVSpeech on a Mac). */
    void turn() override {}

private:
    bool instance_ok(int instance) const { return instance >= 0 && instance < m_instances; }

    int m_instances = 0;
};

/* Both voices start at the engine's own default voice, whatever language the
 * phone is set to: an empty name is "the backend's default" (VoiceDefault).
 * The Accessibility rows let a player pick a named voice from the list. */
constexpr VoiceBackend kVoiceBackend{"Android", {{{"", ""}, {"", ""}}}};

}  // namespace

std::unique_ptr<ScreenReaderBridge> make_screen_reader_bridge() {
    return std::make_unique<AndroidScreenReader>();
}

const VoiceBackend& voice_backend() {
    return kVoiceBackend;
}

std::unique_ptr<VoiceBridge> make_voice_bridge() {
    return std::make_unique<AndroidVoiceBridge>();
}

}  // namespace a11y
