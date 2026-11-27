/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "cue_engine.hpp"
#include "pc/pc.h"
#include <algorithm>
#include <cmath>
#include <numbers>
#include <string>
#include <utility>

namespace a11y {
namespace {

/* The mix's fixed headroom, then a soft limit: one voice at full scale and
 * full volume stays in the limiter's straight part, and five together bend
 * towards full scale without reaching past it. */
constexpr float kHeadroom = 0.5f;
constexpr float kKnee = 0.6f;

constexpr float kHighestPitch = 4.0f;

/* How far past a pulsed sound's end its interpolation still reads samples;
 * a position beyond it is the silence between pulses. */
constexpr double kSoundReach = 2.0;
constexpr float kLongestInterval = 30.0f;

/* Start and stop: a straight ramp of 5 ms, enough that a sound cut at full
 * level does not click. */
constexpr float kRampStep = 1.0f / (0.005f * kCueRate);

/* One-pole glides towards the targets, which arrive once a tick (16.7 ms):
 * pan and gain over about 10 ms, pitch over 20 ms, the Star Fox 64 fork's
 * value, long enough that a tick's step in pitch is heard as a glide. */
float glide_coefficient(float seconds) {
    return 1.0f - std::exp(-1.0f / (seconds * kCueRate));
}
const float kGlide = glide_coefficient(0.010f);
const float kPitchGlide = glide_coefficient(0.020f);

constexpr float kQuarterPi = std::numbers::pi_v<float> / 4.0f;

float clamped(float value, float low, float high, float fallback) {
    if (!std::isfinite(value)) {
        return fallback;
    }
    return std::clamp(value, low, high);
}

float soft_limit(float x) {
    const float a = std::fabs(x);
    if (a <= kKnee) {
        return x;
    }
    const float bent = kKnee + (1.0f - kKnee) * std::tanh((a - kKnee) / (1.0f - kKnee));
    return std::copysign(bent, x);
}

/* The sound at a fractional position, by 4-point Hermite interpolation: a
 * looped sound wraps, any other is silence on either side. */
float sample_at(const std::vector<float>& samples, double position, bool looped) {
    const int size = static_cast<int>(samples.size());
    const double whole = std::floor(position);
    const int i = static_cast<int>(whole);
    const float t = static_cast<float>(position - whole);
    auto at = [&](int k) {
        if (looped) {
            k %= size;
            return samples[static_cast<size_t>(k < 0 ? k + size : k)];
        }
        return k >= 0 && k < size ? samples[static_cast<size_t>(k)] : 0.0f;
    };
    const float x0 = at(i - 1);
    const float x1 = at(i);
    const float x2 = at(i + 1);
    const float x3 = at(i + 2);
    const float c1 = 0.5f * (x2 - x0);
    const float c2 = x0 - 2.5f * x1 + 2.0f * x2 - 0.5f * x3;
    const float c3 = 0.5f * (x3 - x0) + 1.5f * (x1 - x2);
    return ((c3 * t + c2) * t + c1) * t + x1;
}

/* A play's level near its sound's end: the stop ramp over its last frames,
 * so a sound that does not end in silence still ends without a click. */
float end_level(double length, double position, float rate) {
    return static_cast<float>((length - position) / rate) * kRampStep;
}

/* Constant power: left² + right² stays 1 across the pan. */
void add_panned(float* frame, float sample, float pan) {
    const float angle = (pan + 1.0f) * kQuarterPi;
    frame[0] += sample * std::cos(angle);
    frame[1] += sample * std::sin(angle);
}

}  // namespace

int PitchedCopies::nearest(float pitch) const {
    const int last = static_cast<int>(sounds.size()) - 1;
    if (last <= 0 || !(pitch > 0.0f)) {
        return 0;
    }
    const float semitones = std::round(kSemitonesPerOctave * std::log2(pitch / lowest));
    return static_cast<int>(std::clamp(semitones, 0.0f, static_cast<float>(last)));
}

float PitchedCopies::pitch_of(int copy) const {
    return lowest * std::exp2(static_cast<float>(copy) / kSemitonesPerOctave);
}

CueEngine::CueEngine(CueEngineOptions options) : m_options(std::move(options)) {
    if (!m_options.dump.empty()) {
        m_dump = std::fopen(m_options.dump.c_str(), "wb");
        if (m_options.log) {
            pc_log_line(
                m_dump != nullptr ? "[a11y] cue dump: %s" : "[a11y] cue dump: cannot write %s",
                m_options.dump.c_str());
        }
    }
    if (!m_options.trace.empty()) {
        m_trace = std::fopen(m_options.trace.c_str(), "w");
        if (m_trace != nullptr) {
            std::fputs("# tick\tvoice\tstate\tpan\tpitch\tgain\tinterval\n", m_trace);
        }
        if (m_options.log) {
            pc_log_line(
                m_trace != nullptr ? "[a11y] cue trace: %s" : "[a11y] cue trace: cannot write %s",
                m_options.trace.c_str());
        }
    }
}

CueEngine::~CueEngine() {
    if (m_dump != nullptr) {
        std::fclose(m_dump);
    }
    if (m_trace != nullptr) {
        std::fclose(m_trace);
    }
}

int CueEngine::add_voice(VoiceSpec spec) {
    const int count = m_voice_count.load(std::memory_order_relaxed);
    const char* name = spec.name.c_str();
    auto refuse = [&](const char* why) {
        if (m_options.log) {
            pc_log_line("[a11y] cue %s refused: %s", name, why);
        }
        return -1;
    };
    if (count >= kMaxVoices) {
        return refuse("no voice left");
    }
    if (spec.cue < 0 || spec.cue >= kMaxCues) {
        return refuse("no such cue");
    }
    if (spec.mode == VoiceMode::once) {
        if (spec.alternates_with != -1) {
            return refuse("a once voice alternates with none");
        }
    } else if (spec.sound.samples.empty() && spec.copies == nullptr) {
        return refuse("no sound");
    }
    if (spec.copies != nullptr) {
        /* The copies are the voice's sound: a sound beside them would never
         * play, and might not even match them. */
        if (!spec.sound.samples.empty()) {
            return refuse("a sound and its copies both");
        }
        if (spec.mode != VoiceMode::pulsed && spec.mode != VoiceMode::one_shot) {
            return refuse("only a pulsed voice or a one-shot plays copies");
        }
        if (!std::isfinite(spec.copies->lowest) || !(spec.copies->lowest > 0.0f)) {
            return refuse("copies with no lowest pitch");
        }
        const std::vector<Sound>& sounds = spec.copies->sounds;
        if (sounds.empty() || std::any_of(sounds.begin(), sounds.end(),
                                  [](const Sound& copy) { return copy.samples.empty(); }))
        {
            return refuse("an empty copy");
        }
    }
    spec.lowest_pitch = clamped(spec.lowest_pitch, 0.01f, kHighestPitch, 1.0f);
    spec.fastest_interval = clamped(spec.fastest_interval, 0.0f, kLongestInterval, 0.0f);
    const int partner = spec.alternates_with;
    if (partner != -1 &&
        (spec.mode != VoiceMode::pulsed || partner < 0 || partner >= count ||
            m_voices[static_cast<size_t>(partner)].spec.mode != VoiceMode::pulsed ||
            m_voices[static_cast<size_t>(partner)].partner.load(std::memory_order_relaxed) != -1))
    {
        return refuse("no pulsed voice to alternate with");
    }
    Voice& v = m_voices[static_cast<size_t>(count)];
    v.spec = std::move(spec);
    take_copy(v, 1.0f);
    write_targets(v, VoiceTargets{});
    v.partner.store(partner, std::memory_order_relaxed);
    /* Publishes the voice whole: the audio thread reads the count first. */
    m_voice_count.store(count + 1, std::memory_order_release);
    if (partner != -1) {
        /* After the count, so the audio thread never finds a partner it
         * cannot see yet. */
        m_voices[static_cast<size_t>(partner)].partner.store(count, std::memory_order_release);
    }
    return count;
}

CueEngine::Voice* CueEngine::voice_at(int voice) {
    if (voice < 0 || voice >= m_voice_count.load(std::memory_order_relaxed)) {
        return nullptr;
    }
    return &m_voices[static_cast<size_t>(voice)];
}

void CueEngine::write_targets(Voice& v, const VoiceTargets& targets) {
    VoiceTargets& t = v.targets;
    t.pan = clamped(targets.pan, -1.0f, 1.0f, 0.0f);
    t.pitch = clamped(targets.pitch, v.spec.lowest_pitch, kHighestPitch, 1.0f);
    const float most_gain = v.spec.mode == VoiceMode::once ? kMostBoost : 1.0f;
    t.gain = clamped(targets.gain, 0.0f, most_gain, 0.0f);
    t.interval =
        clamped(targets.interval, v.spec.fastest_interval, kLongestInterval, kLongestInterval);
    v.pan.store(t.pan, std::memory_order_relaxed);
    v.pitch.store(t.pitch, std::memory_order_relaxed);
    v.gain.store(t.gain, std::memory_order_relaxed);
    v.interval.store(t.interval, std::memory_order_relaxed);
}

void CueEngine::log_voice(const Voice& v, const char* event) const {
    if (!m_options.log) {
        return;
    }
    const VoiceTargets& t = v.targets;
    if (v.spec.mode == VoiceMode::pulsed) {
        pc_log_line("[a11y] cue %s %s pan %.2f pitch %.2f gain %.2f interval %.3f",
            v.spec.name.c_str(), event, static_cast<double>(t.pan), static_cast<double>(t.pitch),
            static_cast<double>(t.gain), static_cast<double>(t.interval));
    } else {
        pc_log_line("[a11y] cue %s %s pan %.2f pitch %.2f gain %.2f", v.spec.name.c_str(), event,
            static_cast<double>(t.pan), static_cast<double>(t.pitch), static_cast<double>(t.gain));
    }
}

void CueEngine::start(int voice, const VoiceTargets& targets) {
    Voice* v = voice_at(voice);
    if (v == nullptr || v->spec.mode == VoiceMode::once || v->spec.mode == VoiceMode::one_shot) {
        return;
    }
    write_targets(*v, targets);
    if (v->started) {
        return;
    }
    v->started = true;
    /* After the targets, so a voice starting from silence begins at them. */
    v->on.store(true, std::memory_order_release);
    log_voice(*v, "start");
}

bool CueEngine::trigger(int voice, const VoiceTargets& targets) {
    Voice* v = voice_at(voice);
    if (v == nullptr || v->spec.mode != VoiceMode::one_shot) {
        if (m_options.log) {
            pc_log_line("[a11y] cue voice %d cannot be triggered", voice);
        }
        return false;
    }
    write_targets(*v, targets);
    v->on.store(true, std::memory_order_relaxed);
    /* After the targets and the switch: the audio thread reads the count
     * first. */
    v->triggers.fetch_add(1, std::memory_order_seq_cst);
    log_voice(*v, "play");
    return true;
}

void CueEngine::set_targets(int voice, const VoiceTargets& targets) {
    Voice* v = voice_at(voice);
    if (v != nullptr) {
        write_targets(*v, targets);
    }
}

void CueEngine::stop(int voice) {
    Voice* v = voice_at(voice);
    if (v != nullptr && v->spec.mode == VoiceMode::once) {
        Once& o = v->once;
        const int handed = o.next.exchange(-1, std::memory_order_acq_rel);
        if (handed != -1) {
            o.sounds[static_cast<size_t>(handed)] = Sound{};
            o.state[static_cast<size_t>(handed)].store(kFree, std::memory_order_relaxed);
        }
        o.stops.fetch_add(1, std::memory_order_release);
        return;
    }
    if (v != nullptr && v->spec.mode == VoiceMode::one_shot) {
        v->on.store(false, std::memory_order_seq_cst);
        return;
    }
    if (v == nullptr || !v->started) {
        return;
    }
    v->started = false;
    v->on.store(false, std::memory_order_release);
    log_voice(*v, "stop");
}

void CueEngine::stop_all() {
    const int count = m_voice_count.load(std::memory_order_relaxed);
    for (int i = 0; i < count; i++) {
        stop(i);
    }
}

bool CueEngine::playing(int voice) const {
    if (voice < 0 || voice >= m_voice_count.load(std::memory_order_relaxed)) {
        return false;
    }
    const Voice& v = m_voices[static_cast<size_t>(voice)];
    if (v.spec.mode == VoiceMode::once) {
        /* next first: the audio thread marks itself busy before it takes
         * the slot, so a line between the two is never missed. */
        return v.once.next.load(std::memory_order_seq_cst) != -1 ||
               v.once.busy.load(std::memory_order_seq_cst);
    }
    if (v.spec.mode == VoiceMode::one_shot) {
        /* Taken first, as for a once voice: the audio thread marks itself
         * busy before it counts a trigger taken. */
        return v.triggers_taken.load(std::memory_order_seq_cst) !=
                   v.triggers.load(std::memory_order_seq_cst) ||
               v.busy.load(std::memory_order_seq_cst);
    }
    return v.started;
}

bool CueEngine::play(int voice, Sound sound) {
    Voice* v = voice_at(voice);
    if (v == nullptr || v->spec.mode != VoiceMode::once) {
        if (m_options.log) {
            pc_log_line("[a11y] cue voice %d cannot play a sound handed to it", voice);
        }
        return false;
    }
    Once& o = v->once;
    int free_slot = -1;
    for (int i = 0; i < kOnceSlots; i++) {
        std::atomic<int>& state = o.state[static_cast<size_t>(i)];
        if (state.load(std::memory_order_acquire) == kDone) {
            /* Freed here, on the game thread, never on the audio thread. */
            o.sounds[static_cast<size_t>(i)] = Sound{};
            state.store(kFree, std::memory_order_relaxed);
        }
        if (free_slot == -1 && state.load(std::memory_order_relaxed) == kFree) {
            free_slot = i;
        }
    }
    /* At most two slots are held and one handed, so one is always free. */
    if (free_slot == -1) {
        return false;
    }
    o.sounds[static_cast<size_t>(free_slot)] = std::move(sound);
    o.state[static_cast<size_t>(free_slot)].store(kHanded, std::memory_order_relaxed);
    const int replaced = o.next.exchange(free_slot, std::memory_order_seq_cst);
    if (replaced != -1) {
        /* Never taken, so the audio thread will never read it. */
        o.sounds[static_cast<size_t>(replaced)] = Sound{};
        o.state[static_cast<size_t>(replaced)].store(kFree, std::memory_order_relaxed);
    }
    return true;
}

void CueEngine::set_master_volume(float volume) {
    m_master.store(clamped(volume, 0.0f, 1.0f, 1.0f), std::memory_order_relaxed);
}

void CueEngine::AtomicLevel::store(const CueLevel& level) {
    on.store(level.on, std::memory_order_relaxed);
    volume.store(clamped(level.volume, 0.0f, 1.0f, 1.0f), std::memory_order_relaxed);
    boost.store(clamped(level.boost, 0.0f, kMostBoost, 1.0f), std::memory_order_relaxed);
}

float CueEngine::AtomicLevel::gain(bool ignore_switch) const {
    if (!ignore_switch && !on.load(std::memory_order_relaxed)) {
        return 0.0f;
    }
    return volume.load(std::memory_order_relaxed) * boost.load(std::memory_order_relaxed);
}

void CueEngine::set_mix_level(const CueLevel& level) {
    m_mix.store(level);
}

void CueEngine::set_cue_level(int cue, const CueLevel& level) {
    if (cue >= 0 && cue < kMaxCues) {
        m_cues[static_cast<size_t>(cue)].store(level);
    }
}

void CueEngine::trace_tick(int tick) {
    if (m_trace == nullptr) {
        return;
    }
    const int count = m_voice_count.load(std::memory_order_relaxed);
    for (int i = 0; i < count; i++) {
        const Voice& v = m_voices[static_cast<size_t>(i)];
        const VoiceTargets& t = v.targets;
        std::fprintf(m_trace, "%d\t%s\t%s\t%.3f\t%.3f\t%.3f\t", tick, v.spec.name.c_str(),
            playing(i) ? "on" : "off", static_cast<double>(t.pan), static_cast<double>(t.pitch),
            static_cast<double>(t.gain));
        if (v.spec.mode == VoiceMode::pulsed) {
            std::fprintf(m_trace, "%.3f\n", static_cast<double>(t.interval));
        } else {
            std::fputs("-\n", m_trace);
        }
    }
}

void CueEngine::render(float* out, int frames) {
    std::fill_n(out, frames * 2, 0.0f);
    const float master = m_master.load(std::memory_order_relaxed);
    const int count = m_voice_count.load(std::memory_order_acquire);
    for (int i = 0; i < count; i++) {
        Voice& v = m_voices[static_cast<size_t>(i)];
        if (v.spec.mode == VoiceMode::once) {
            render_once(v, master, out, frames);
            continue;
        }
        const bool ignore = v.spec.ignores_switches;
        const float chain =
            master * m_mix.gain(ignore) * m_cues[static_cast<size_t>(v.spec.cue)].gain(ignore);
        if (v.spec.mode == VoiceMode::one_shot) {
            render_one_shot(v, chain, out, frames);
        } else {
            render_voice(v, chain, out, frames);
        }
    }
    for (int i = 0; i < frames * 2; i++) {
        out[i] = soft_limit(out[i] * kHeadroom);
    }
    if (m_dump != nullptr) {
        std::fwrite(out, sizeof(float), static_cast<size_t>(frames) * 2, m_dump);
    }
}

void CueEngine::render_voice(Voice& v, float chain, float* out, int frames) {
    const bool on = v.on.load(std::memory_order_acquire);
    if (!v.sounding && !on) {
        return;
    }
    const float pan = v.pan.load(std::memory_order_relaxed);
    const float pitch = v.pitch.load(std::memory_order_relaxed);
    const float gain = chain * v.gain.load(std::memory_order_relaxed);
    const double interval = v.interval.load(std::memory_order_relaxed) * kCueRate;
    if (!v.sounding) {
        /* From silence: the sound's start, at the targets, ramping in. */
        v.sounding = true;
        v.position = 0.0;
        v.since_pulse = 0.0;
        v.ramp = 0.0f;
        v.pan_now = pan;
        v.pitch_now = pitch;
        v.gain_now = gain;
        v.tail.fade = 0.0f;
        take_copy(v, pitch);
        if (v.spec.mode == VoiceMode::pulsed && on) {
            fall_between(v, interval, frames);
        }
    }
    double length = static_cast<double>(v.samples->size());
    const bool looped = v.spec.mode == VoiceMode::looped;
    const float ramp_step = on ? kRampStep : -kRampStep;
    for (int f = 0; f < frames; f++) {
        v.ramp = std::clamp(v.ramp + ramp_step, 0.0f, 1.0f);
        v.pan_now += (pan - v.pan_now) * kGlide;
        v.gain_now += (gain - v.gain_now) * kGlide;
        v.pitch_now += (pitch - v.pitch_now) * kPitchGlide;
        if (!looped) {
            /* The next pulse one interval after the last, at the interval
             * current now; one much shorter than the time already waited
             * starts the rhythm afresh from here. */
            if (v.since_pulse >= interval) {
                v.since_pulse -= interval;
                if (v.since_pulse >= interval) {
                    v.since_pulse = 0.0;
                }
                if (v.position < length) {
                    /* Still playing: it fades out as the next begins. A
                     * sound that fits has always ended by now. */
                    v.tail = cut(v, 1.0f);
                }
                v.position = 0.0;
                /* The copy of the pitch the glide is heading for, so a
                 * pulse just after a jump plays near its own length once
                 * the glide is done, not the old pitch's copy sped up or
                 * slowed down all the way to the new one. */
                take_copy(v, pitch);
                length = static_cast<double>(v.samples->size());
            }
            v.since_pulse += 1.0;
            if (v.tail.fade > 0.0f) {
                add_tail(v.tail, v.ramp, out + 2 * f);
            }
            if (v.position > length + kSoundReach) {
                continue;
            }
        }
        const float sample = sample_at(*v.samples, v.position, looped) * v.ramp * v.gain_now;
        v.position += v.pitch_now / v.copy_pitch;
        if (looped && v.position >= length) {
            v.position = std::fmod(v.position, length);
        }
        add_panned(out + 2 * f, sample, v.pan_now);
    }
    if (!on && v.ramp <= 0.0f) {
        v.sounding = false;
        v.tail.fade = 0.0f;
    }
}

void CueEngine::render_one_shot(Voice& v, float chain, float* out, int frames) {
    /* The count before the switch: a trigger stores the switch first. */
    const int triggers = v.triggers.load(std::memory_order_acquire);
    const bool on = v.on.load(std::memory_order_acquire);
    const float pan = v.pan.load(std::memory_order_relaxed);
    const float pitch = v.pitch.load(std::memory_order_relaxed);
    const float gain = chain * v.gain.load(std::memory_order_relaxed);
    if (triggers != v.triggers_taken.load(std::memory_order_relaxed)) {
        v.busy.store(true, std::memory_order_seq_cst);
        /* Stopped since: a trigger not yet begun is dropped. */
        if (on) {
            if (v.sounding) {
                const double playing = static_cast<double>(v.samples->size());
                const float rate = v.pitch_now / v.copy_pitch;
                v.tail = cut(v, std::min(v.ramp, end_level(playing, v.position, rate)));
            }
            /* From the top, at the trigger's targets, ramping in. */
            v.sounding = true;
            v.position = 0.0;
            v.ramp = 0.0f;
            v.pan_now = pan;
            v.pitch_now = pitch;
            v.gain_now = gain;
            take_copy(v, pitch);
        }
        v.triggers_taken.store(triggers, std::memory_order_seq_cst);
    }
    if (!v.sounding && v.tail.fade <= 0.0f) {
        v.busy.store(false, std::memory_order_seq_cst);
        return;
    }
    const std::vector<float>& samples = *v.samples;
    const double length = static_cast<double>(samples.size());
    const float ramp_step = on ? kRampStep : -kRampStep;
    for (int f = 0; f < frames; f++) {
        if (v.sounding) {
            v.ramp = std::clamp(v.ramp + ramp_step, 0.0f, 1.0f);
            if ((!on && v.ramp <= 0.0f) || v.position >= length) {
                v.sounding = false;
            } else {
                v.pan_now += (pan - v.pan_now) * kGlide;
                v.gain_now += (gain - v.gain_now) * kGlide;
                v.pitch_now += (pitch - v.pitch_now) * kPitchGlide;
                const float rate = v.pitch_now / v.copy_pitch;
                const float level =
                    std::min(v.ramp, end_level(length, v.position, rate)) * v.gain_now;
                const float sample = sample_at(samples, v.position, false) * level;
                v.position += rate;
                add_panned(out + 2 * f, sample, v.pan_now);
            }
        }
        if (v.tail.fade > 0.0f) {
            add_tail(v.tail, 1.0f, out + 2 * f);
        }
    }
    v.busy.store(v.sounding, std::memory_order_seq_cst);
}

void CueEngine::add_tail(Tail& tail, float level, float* frame) {
    const double length = static_cast<double>(tail.samples->size());
    tail.fade = std::min(tail.fade - kRampStep, end_level(length, tail.position, tail.rate));
    if (tail.fade <= 0.0f) {
        tail.fade = 0.0f;
        return;
    }
    const float sample =
        sample_at(*tail.samples, tail.position, false) * tail.fade * tail.gain * level;
    tail.position += tail.rate;
    add_panned(frame, sample, tail.pan);
}

void CueEngine::take_copy(Voice& v, float pitch) {
    const PitchedCopies* copies = v.spec.copies.get();
    if (copies == nullptr) {
        v.samples = &v.spec.sound.samples;
        v.copy_pitch = 1.0f;
        return;
    }
    const int copy = copies->nearest(pitch);
    v.samples = &copies->sounds[static_cast<size_t>(copy)].samples;
    v.copy_pitch = copies->pitch_of(copy);
}

CueEngine::Tail CueEngine::cut(const Voice& v, float level) {
    return Tail{v.samples, v.position, level, v.pan_now, v.pitch_now / v.copy_pitch, v.gain_now};
}

void CueEngine::render_once(Voice& v, float chain, float* out, int frames) {
    Once& o = v.once;
    auto finish = [&o](int slot) {
        o.state[static_cast<size_t>(slot)].store(kDone, std::memory_order_release);
    };
    const int stops = o.stops.load(std::memory_order_acquire);
    if (stops != o.stops_seen) {
        o.stops_seen = stops;
        if (o.incoming != -1) {
            finish(o.incoming);
            o.incoming = -1;
        }
        o.cutting = o.current != -1;
    }
    if (o.next.load(std::memory_order_acquire) != -1) {
        o.busy.store(true, std::memory_order_seq_cst);
        const int taken = o.next.exchange(-1, std::memory_order_seq_cst);
        if (taken != -1) {
            o.state[static_cast<size_t>(taken)].store(kHeld, std::memory_order_relaxed);
            if (o.current == -1) {
                o.current = taken;
                v.position = 0.0;
                v.ramp = 0.0f;
                v.pan_now = v.pan.load(std::memory_order_relaxed);
                v.gain_now = chain * v.gain.load(std::memory_order_relaxed);
            } else {
                if (o.incoming != -1) {
                    finish(o.incoming);
                }
                o.incoming = taken;
                o.cutting = false;
            }
        }
    }
    const float pan = v.pan.load(std::memory_order_relaxed);
    const float gain = chain * v.gain.load(std::memory_order_relaxed);
    for (int f = 0; f < frames && o.current != -1; f++) {
        const std::vector<float>& samples = o.sounds[static_cast<size_t>(o.current)].samples;
        const double length = static_cast<double>(samples.size());
        const bool out_ramp = o.incoming != -1 || o.cutting;
        v.ramp = std::clamp(v.ramp + (out_ramp ? -kRampStep : kRampStep), 0.0f, 1.0f);
        if ((out_ramp && v.ramp <= 0.0f) || v.position >= length) {
            /* Played out or ramped out: the next begins here, ramping in. */
            finish(o.current);
            o.current = o.incoming;
            o.incoming = -1;
            o.cutting = false;
            v.position = 0.0;
            v.ramp = 0.0f;
            continue;
        }
        v.pan_now += (pan - v.pan_now) * kGlide;
        v.gain_now += (gain - v.gain_now) * kGlide;
        /* The end ramps out too, over the sound's last frames. */
        const float level = std::min(v.ramp, end_level(length, v.position, 1.0f)) * v.gain_now;
        const float sample = samples[static_cast<size_t>(v.position)] * level;
        v.position += 1.0;
        add_panned(out + 2 * f, sample, v.pan_now);
    }
    v.sounding = o.current != -1;
    if (o.current == -1) {
        o.busy.store(false, std::memory_order_seq_cst);
    }
}

/* A pulsed voice starting from silence while its partner sounds: its first
 * pulse half an interval, its own, after the partner's last, and silence
 * until then. */
void CueEngine::fall_between(Voice& v, double interval, int frames) const {
    const int p = v.partner.load(std::memory_order_acquire);
    if (p < 0) {
        return;
    }
    const Voice& partner = m_voices[static_cast<size_t>(p)];
    /* A partner still ramping down counts: started again before the ramp
     * ends, it keeps its rhythm. */
    if (!partner.sounding) {
        return;
    }
    /* Frames since the partner's last pulse at the start of this block: a
     * partner rendered before this voice, render going in voice order, has
     * counted the block already. */
    double since = partner.since_pulse - (&partner < &v ? frames : 0);
    since = std::fmod(since, interval);
    if (since < 0.0) {
        since += interval;
    }
    const double wait = std::fmod(1.5 * interval - since, interval);
    /* The count reaches the interval, and the pulse starts, after the
     * wait. */
    v.since_pulse = interval - wait;
    v.position = static_cast<double>(v.samples->size()) + kSoundReach + 1.0;
}

bool OneShotPool::add(CueEngine& engine, const VoiceSpec& spec) {
    while (m_count < kSize) {
        VoiceSpec voice = spec;
        voice.name = spec.name + " " + std::to_string(m_count + 1);
        const int added = engine.add_voice(std::move(voice));
        if (added == -1) {
            return false;
        }
        m_voices[static_cast<size_t>(m_count)] = added;
        m_count++;
    }
    return true;
}

int OneShotPool::trigger(CueEngine& engine, const VoiceTargets& targets) {
    if (m_count == 0) {
        return -1;
    }
    int chosen = -1;
    for (int i = 0; i < m_count && chosen == -1; i++) {
        if (!engine.playing(m_voices[static_cast<size_t>(i)])) {
            chosen = i;
        }
    }
    if (chosen == -1) {
        chosen = 0;
        for (int i = 1; i < m_count; i++) {
            if (m_taken_at[static_cast<size_t>(i)] < m_taken_at[static_cast<size_t>(chosen)]) {
                chosen = i;
            }
        }
    }
    m_taken_at[static_cast<size_t>(chosen)] = ++m_triggers;
    const int voice = m_voices[static_cast<size_t>(chosen)];
    engine.trigger(voice, targets);
    return voice;
}

void OneShotPool::stop(CueEngine& engine) {
    for (int voice : voices()) {
        engine.stop(voice);
    }
}

bool OneShotPool::playing(const CueEngine& engine) const {
    for (int voice : voices()) {
        if (engine.playing(voice)) {
            return true;
        }
    }
    return false;
}

std::span<const int> OneShotPool::voices() const {
    return {m_voices.data(), static_cast<size_t>(m_count)};
}

}  // namespace a11y
