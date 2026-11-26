/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "match_cues.hpp"
#include "cue_sounds.hpp"
#include "miniaudio.h"
#include "pitch_shift.hpp"
#include "pc/pc.h"
#include <algorithm>
#include <cmath>
#include <memory>
#include <numbers>
#include <string>
#include <utility>

namespace a11y {
namespace {

/* The cues' ids, as add_cue takes them and the menus' Preview rows hold
 * them. */
constexpr std::string_view kYourId = "your_position";
constexpr std::string_view kOpponentId = "opponent_position";
constexpr std::string_view kGapId = "gap_warning";
constexpr std::string_view kLedgeId = "ledge_warning";
constexpr std::string_view kAppearanceId = "item_appearance";
constexpr std::string_view kItemPositionId = "item_position";

/* A short click lifted under its volume to sit level with held tones. */
constexpr float kYourBoost = 2.0f;
/* The opponent's lower pulse more, as tuned by ear. */
constexpr float kOpponentBoost = 3.0f;
/* A held tone needs no lift. */
constexpr float kGapBoost = 1.0f;
/* The ledge warning's chord, as tuned by ear. */
constexpr float kLedgeBoost = 1.6f;
/* A prepared file peaks just under full scale (prepare_sounds.py): the
 * appearance sound lifted as tuned by ear beside the others, the item's
 * position cue not yet. */
constexpr float kAppearanceBoost = 2.0f;
constexpr float kItemPositionBoost = 1.0f;

/* After the last play of the appearance cue's preview has sounded, a
 * little more before it ends, so the end's stop finds nothing playing. */
constexpr double kAppearancePreviewTail = 0.1;

/* The tick of your position cue, the Star Fox 64 fork's aim click: a
 * 1500 Hz sine at its own pitch decaying fast over 20 ms, its last 3 ms
 * faded to silence, after 12 ms of silence so the start ramp leaves its
 * attack alone. 32 ms in all, at every pitch. */
Sound tick_sound(float pitch) {
    const double hz = 1500.0 * pitch;
    constexpr float kSeconds = 0.02f;
    constexpr float kDecayPerSecond = 150.0f;
    constexpr float kAmplitude = 0.95f;
    constexpr float kTailSeconds = 0.003f;
    constexpr float kLeadInSeconds = 0.012f;
    ma_waveform_config config =
        ma_waveform_config_init(ma_format_f32, 1, kCueRate, ma_waveform_type_sine, kAmplitude, hz);
    ma_waveform wave;
    Sound sound;
    if (ma_waveform_init(&config, &wave) == MA_SUCCESS) {
        sound = read_sound(&wave, static_cast<int>(kSeconds * kCueRate));
        ma_waveform_uninit(&wave);
    }
    for (size_t i = 0; i < sound.samples.size(); i++) {
        sound.samples[i] *= std::exp(-kDecayPerSecond * static_cast<float>(i) / kCueRate);
    }
    fade_out(sound, kTailSeconds);
    lead_in(sound, kLeadInSeconds);
    return sound;
}

/* The opponent's pulse, lower and rounder than your tick so the two never
 * blend: a 400 Hz sine at its own pitch with a little of its octave,
 * rising over 5 ms and dying away over 70 ms, its last 10 ms faded to
 * silence, after the tick's 12 ms of lead-in. 82 ms in all, at every
 * pitch. */
Sound pulse_sound(float pitch) {
    const double hz = 400.0 * pitch;
    constexpr float kOctave = 0.25f; /* the octave's share of the fundamental */
    constexpr float kSeconds = 0.07f;
    constexpr float kAttackSeconds = 0.005f;
    constexpr float kDecayPerSecond = 30.0f;
    constexpr float kAmplitude = 0.95f;
    constexpr float kTailSeconds = 0.01f;
    constexpr float kLeadInSeconds = 0.012f;
    Sound sound;
    sound.samples.resize(static_cast<size_t>(kSeconds * kCueRate));
    for (size_t i = 0; i < sound.samples.size(); i++) {
        const double t = static_cast<double>(i) / kCueRate;
        const double phase = 2.0 * std::numbers::pi * hz * t;
        const double tone = std::sin(phase) + kOctave * std::sin(2.0 * phase);
        sound.samples[i] = static_cast<float>(tone) * kAmplitude / (1.0f + kOctave) *
                           std::exp(-kDecayPerSecond * static_cast<float>(t));
    }
    fade_in(sound, kAttackSeconds);
    fade_out(sound, kTailSeconds);
    lead_in(sound, kLeadInSeconds);
    return sound;
}

/* The gap warning's tone, held and below both position cues: 120 Hz with
 * its third and fifth harmonics at a third and a fifth of its level, the
 * start of a square wave, a hollow hum that is no pulse and no sine. Its
 * fundamental sits under the opponent's lowest note, 200 Hz at the bottom
 * blast zone, and the harmonics give small speakers something to play.
 * 120 Hz is 400 samples a period, so a buffer of whole periods loops
 * without a seam. Scaled by the amplitudes' sum, it peaks at 0.6 of full
 * scale, 0.5 as RMS, which in the mix sits a little under the opponent's
 * pulse, measured over a tenth of a second, and above your tick: a low
 * tone sounds quieter than a higher one at the same level, and the warning
 * should be heard. */
Sound hum_sound() {
    constexpr int kPeriod = kCueRate / 120;
    constexpr int kPeriods = 12;
    constexpr float kThird = 1.0f / 3.0f;
    constexpr float kFifth = 1.0f / 5.0f;
    Sound sound;
    sound.samples.resize(static_cast<size_t>(kPeriod * kPeriods));
    for (size_t i = 0; i < sound.samples.size(); i++) {
        const double phase = 2.0 * std::numbers::pi * static_cast<double>(i % kPeriod) / kPeriod;
        const double tone =
            std::sin(phase) + kThird * std::sin(3.0 * phase) + kFifth * std::sin(5.0 * phase);
        sound.samples[i] = static_cast<float>(tone) / (1.0f + kThird + kFifth);
    }
    return sound;
}

/* The ledge warning's pulse, a chord unlike the other cues' single tones:
 * 660 and 880 Hz, a just perfect fourth (3:4) as in the Star Fox 64 fork's
 * beside chord, between the opponent's 400 Hz pulse and your 1500 Hz tick
 * on the floor, where ledges are. 90 ms rising over 5 ms and easing off,
 * its last 20 ms faded to silence, after the tick's 12 ms of lead-in: 102
 * ms in all, half of each 0.2 s pulse, so the pulses stay apart. Played
 * only at its own pitch. */
Sound chord_sound() {
    constexpr double kLow = 660.0;
    constexpr double kHigh = 880.0;
    constexpr float kSeconds = 0.09f;
    constexpr float kAttackSeconds = 0.005f;
    constexpr float kDecayPerSecond = 8.0f;
    constexpr float kAmplitude = 0.95f;
    constexpr float kTailSeconds = 0.02f;
    constexpr float kLeadInSeconds = 0.012f;
    Sound sound;
    sound.samples.resize(static_cast<size_t>(kSeconds * kCueRate));
    for (size_t i = 0; i < sound.samples.size(); i++) {
        const double t = static_cast<double>(i) / kCueRate;
        const double tone = std::sin(2.0 * std::numbers::pi * kLow * t) +
                            std::sin(2.0 * std::numbers::pi * kHigh * t);
        sound.samples[i] = static_cast<float>(tone) * kAmplitude / 2.0f *
                           std::exp(-kDecayPerSecond * static_cast<float>(t));
    }
    fade_in(sound, kAttackSeconds);
    fade_out(sound, kTailSeconds);
    lead_in(sound, kLeadInSeconds);
    return sound;
}

/* A ledge warning voice, one a side. On a small floor both sides sound at
 * once with the same chord at the same rate; the right one alternates with
 * the left, so the two are heard as two places rather than one sound in
 * the middle. */
VoiceSpec ledge_voice(const char* name, int cue, int alternates_with = -1) {
    VoiceSpec voice;
    voice.name = name;
    voice.cue = cue;
    voice.sound = chord_sound();
    voice.mode = VoiceMode::pulsed;
    voice.fastest_interval = kLedgeInterval;
    voice.lowest_pitch = 1.0f;
    voice.alternates_with = alternates_with;
    return voice;
}

/* The pitches the widest pitch range reaches, half of it down and half up:
 * the position cues and the item cues are held to them, and their sounds'
 * copies span them. */
const float kRangeLowestPitch = std::exp2(-MatchCues::kWidestPitchRange / 2.0f);
const float kRangeHighestPitch = std::exp2(MatchCues::kWidestPitchRange / 2.0f);

/* A sound made by code, made again at each copy's pitch. */
std::shared_ptr<const PitchedCopies> made_copies(Sound (*make)(float pitch)) {
    return pitched_copies(kRangeLowestPitch, kRangeHighestPitch, make);
}

/* A file's sound moved to each copy's pitch; none for a missing file, whose
 * voice is refused for its empty sound. */
std::shared_ptr<const PitchedCopies> shifted_copies(const Sound& sound) {
    if (sound.samples.empty()) {
        return nullptr;
    }
    return pitched_copies(kRangeLowestPitch, kRangeHighestPitch,
        [&sound](float pitch) { return shift_pitch(sound, pitch); });
}

/* A position cue's pulsed voice: its copies keep the sound's length at
 * every pitch, so a sound that fits the fastest interval fits it at any. */
VoiceSpec position_voice(const char* name, int cue, std::shared_ptr<const PitchedCopies> copies,
    float fastest_interval) {
    VoiceSpec voice;
    voice.name = name;
    voice.cue = cue;
    voice.copies = std::move(copies);
    voice.mode = VoiceMode::pulsed;
    voice.fastest_interval = fastest_interval;
    voice.lowest_pitch = kRangeLowestPitch;
    return voice;
}

/* The appearance cue's one-shot, as long at every pitch. */
VoiceSpec appearance_voice(int cue, std::shared_ptr<const PitchedCopies> copies) {
    VoiceSpec voice;
    voice.name = "item appearance";
    voice.cue = cue;
    voice.copies = std::move(copies);
    voice.mode = VoiceMode::one_shot;
    voice.lowest_pitch = kRangeLowestPitch;
    return voice;
}

/* A voice's twin for the preview: the same sound and limits, named apart
 * in the log, sounding past the switches, and alone: the preview's two
 * ledge voices never sound together. */
VoiceSpec preview_of(VoiceSpec spec) {
    spec.name += " preview";
    spec.ignores_switches = true;
    spec.alternates_with = -1;
    return spec;
}

}  // namespace

StageMapping MatchCues::StageValues::mapping(bool pan_to_camera) const {
    return StageMapping{pan_to_camera, pitch_range.value()};
}

YourMapping MatchCues::YourValues::mapping(bool pan_to_camera) const {
    return YourMapping{stage.mapping(pan_to_camera), slow_interval.value(), fast_interval.value(),
        far_distance.value(), near_distance.value()};
}

OpponentMapping MatchCues::OpponentValues::mapping(bool pan_to_camera) const {
    return OpponentMapping{stage.mapping(pan_to_camera), interval.value(), relative.on(),
        RelativeMapping{side_distance.value(), pan_floor.value(), crossing_margin.value(),
            height_distance.value()}};
}

ItemMapping MatchCues::ItemValues::mapping(bool pan_to_camera) const {
    ItemMapping mapping;
    mapping.relative = relative.on();
    mapping.stage = StageMapping{pan_to_camera, pitch_range.value()};
    mapping.from_you.side_distance = side_distance.value();
    mapping.from_you.pan_floor = pan_floor.value();
    mapping.from_you.height_distance = height_distance.value();
    mapping.from_you.crossing_margin = crossing_margin.value();
    return mapping;
}

ItemPositionMapping MatchCues::ItemPositionValues::mapping(ItemMapping frame) const {
    frame.stage.pitch_range = pitch_range.value();
    return ItemPositionMapping{frame, steady_interval.value(), reach_interval.value()};
}

GapMapping MatchCues::GapValues::mapping() const {
    return GapMapping{static_cast<int>(std::lround(hold.value()))};
}

LedgeMapping MatchCues::LedgeValues::mapping() const {
    return LedgeMapping{distance.value(), pan_floor.value(), level_floor.value() / 100.0f};
}

/* Worded for either position cue, each registering its own copy. */
MatchCues::StageValues MatchCues::add_stage_values(ValueGroup group) {
    const StageMapping defaults;
    return StageValues{
        group.add_number(NumberSpec{"pitch_range", "Pitch range",
            "How many octaves the pitch rises from the bottom blast zone to the top of the "
            "camera's view; midway it is the cue's own pitch.",
            "", defaults.pitch_range, 0.0f, kWidestPitchRange, 0.1f}),
    };
}

MatchCues::YourValues MatchCues::add_your_values(ValueGroup group) {
    const YourMapping defaults;
    StageValues stage = add_stage_values(group);
    return YourValues{
        stage,
        group.add_number(NumberSpec{"slow_interval", "Slow interval",
            "Time between ticks with the opponent at the far distance or beyond, or with no "
            "opponent.",
            "s", defaults.slow_interval, 0.1f, 3.0f, 0.05f}),
        group.add_number(NumberSpec{"fast_interval", "Fast interval",
            "Time between ticks with the opponent at the near distance or closer.", "s",
            defaults.fast_interval, kFastestInterval, 1.0f, 0.01f}),
        group.add_number(NumberSpec{"far_distance", "Far distance",
            "From this distance to the opponent and beyond, the ticks are slow. Final "
            "Destination's main floor is about 171 units wide.",
            "units", defaults.far_distance, 10.0f, 400.0f, 5.0f}),
        group.add_number(NumberSpec{"near_distance", "Near distance",
            "From this distance to the opponent and closer, the ticks are fast.", "units",
            defaults.near_distance, 0.0f, 100.0f, 1.0f}),
    };
}

MatchCues::OpponentValues MatchCues::add_opponent_values(ValueGroup group) {
    const OpponentMapping defaults;
    const RelativeMapping& from_you = defaults.from_you;
    StageValues stage = add_stage_values(group);
    return OpponentValues{
        stage,
        group.add_number(NumberSpec{"interval", "Interval",
            "Time between the opponent's pulses, the same however near they are.", "s",
            defaults.interval, kOpponentFastestInterval, 3.0f, 0.05f}),
        group.add_switch(SwitchSpec{"relative", "Opponent's position cue relative to you",
            "On, the opponent's position cue sounds where the opponent is from you: on your left "
            "or right, above or below you; off, where they are on the stage.",
            defaults.relative, true}),
        group.add_number(NumberSpec{"side_distance", "Side distance",
            "Relative to you: how far to your side the opponent sounds fully to that side.",
            "units", from_you.side_distance, 10.0f, 300.0f, 5.0f}),
        group.add_number(NumberSpec{"pan_floor", "Pan floor",
            "Relative to you: how far to the side the opponent sounds when level with you, so "
            "their side is always heard; at the side distance it is fully to that side.",
            "", from_you.pan_floor, 0.0f, 1.0f, 0.05f}),
        group.add_number(NumberSpec{"crossing_margin", "Crossing margin",
            "Relative to you: how far past you the opponent goes before their pulse changes "
            "sides, so it does not flicker while you overlap.",
            "units", from_you.crossing_margin, 0.0f, 30.0f, 0.5f}),
        group.add_number(NumberSpec{"height_distance", "Height distance",
            "Relative to you: how far above or below you the pitch reaches the top or bottom of "
            "the pitch range; level with you it is the cue's own pitch. Battlefield's top "
            "platform is about 54 units above its main floor.",
            "units", from_you.height_distance, 10.0f, 300.0f, 5.0f}),
    };
}

/* The item cues' own copies of the opponent's distances, at its defaults. */
MatchCues::ItemValues MatchCues::add_item_values(ValueGroup group) {
    const ItemMapping defaults;
    const RelativeMapping& from_you = defaults.from_you;
    return ItemValues{
        group.add_switch(SwitchSpec{"relative", "Items relative to you",
            "On, the item cues sound where an item is from you: on your left or right, above or "
            "below you; off, where it is on the stage.",
            defaults.relative, true}),
        group.add_number(NumberSpec{"pitch_range", "Pitch range",
            "How many octaves the item appearance cue's pitch spans: relative to you, from the "
            "height distance below you to as far above; on the stage, from the bottom blast zone "
            "to the top of the camera's view. Level with you, or midway, it is the cue's own "
            "pitch. The item's position cue has its own.",
            "", defaults.stage.pitch_range, 0.0f, kWidestPitchRange, 0.1f}),
        group.add_number(NumberSpec{"side_distance", "Side distance",
            "Relative to you: how far to your side an item sounds fully to that side.", "units",
            from_you.side_distance, 10.0f, 300.0f, 5.0f}),
        group.add_number(NumberSpec{"pan_floor", "Pan floor",
            "Relative to you: how far to the side an item sounds when right beside you, so "
            "its side is heard; at the side distance it is fully to that side.",
            "", from_you.pan_floor, 0.0f, 1.0f, 0.05f}),
        group.add_number(NumberSpec{"height_distance", "Height distance",
            "Relative to you: how far above or below you the pitch reaches the top or bottom of "
            "the pitch range; level with you it is the cue's own pitch.",
            "units", from_you.height_distance, 10.0f, 300.0f, 5.0f}),
        group.add_number(NumberSpec{"crossing_margin", "Crossing margin",
            "Relative to you: how far past you an item goes before the item's position cue "
            "changes sides, so it does not flicker as you walk over the item.",
            "units", from_you.crossing_margin, 0.0f, 30.0f, 0.5f}),
    };
}

/* The pitch range starts at the item cues' frame's. */
MatchCues::ItemPositionValues MatchCues::add_item_position_values(ValueGroup group) {
    const ItemPositionMapping defaults;
    return ItemPositionValues{
        group.add_number(NumberSpec{"pitch_range", "Pitch range",
            "How many octaves the item's position cue's pitch spans: relative to you, from the "
            "height distance below you to as far above; on the stage, from the bottom blast zone "
            "to the top of the camera's view. Level with you, or midway, it is the cue's own "
            "pitch.",
            "", defaults.frame.stage.pitch_range, 0.0f, kWidestPitchRange, 0.1f}),
        group.add_number(NumberSpec{"steady_interval", "Steady interval",
            "Time between pulses while the item is out of reach.", "s", defaults.steady_interval,
            0.1f, 3.0f, 0.05f}),
        group.add_number(NumberSpec{"reach_interval", "Within reach interval",
            "Time between pulses while the item is within reach, where a press would pick it up.",
            "s", defaults.reach_interval, kItemFastestInterval, 1.0f, 0.01f}),
    };
}

MatchCues::GapValues MatchCues::add_gap_values(ValueGroup group) {
    const GapMapping defaults;
    return GapValues{
        group.add_number(NumberSpec{"hold", "Hold",
            "How long the warning sounds on after you leave a gap, so it does not flicker at a "
            "floor's edge. A tick is a sixtieth of a second.",
            "ticks", static_cast<float>(defaults.hold_ticks), 0.0f, 60.0f, 1.0f}),
    };
}

MatchCues::LedgeValues MatchCues::add_ledge_values(ValueGroup group) {
    const LedgeMapping defaults;
    return LedgeValues{
        group.add_number(NumberSpec{"distance", "Ledge warning distance",
            "How far from the edge the warning starts, in stage units. Final Destination is "
            "about 170 wide.",
            "units", defaults.distance, 5.0f, 100.0f, 1.0f, true}),
        group.add_number(NumberSpec{"pan_floor", "Pan floor",
            "How far to the side the warning sounds at the edge itself; at the warning distance "
            "it is fully to that side.",
            "", defaults.pan_floor, 0.0f, 1.0f, 0.05f}),
        group.add_number(NumberSpec{"level_floor", "Level floor",
            "How loud the warning starts at the warning distance; at the edge it is full.", "%",
            std::round(defaults.level_floor * 100.0f), 0.0f, 100.0f, 5.0f}),
        group.add_number(NumberSpec{"edge_offset", "Edge offset",
            "How far past an end of the floor below you the test for a floor further down looks; "
            "with none there, the end is deadly. Battlefield's side platforms end about 11 units "
            "inside its main floor, so more than 10 would take their ends for deadly.",
            "units", kEdgeOffset, 2.0f, 10.0f, 0.5f}),
    };
}

MatchCues::MatchCues(CueSettings& settings, CueEngine& engine, MatchCueSounds sounds)
    : m_engine(engine),
      m_your_cue(settings.add_cue(std::string(kYourId), "Your position cue",
          "A tick that pans with where you are across the stage, rises in pitch the higher you "
          "are, and speeds up as the nearest opponent gets closer.",
          kYourBoost)),
      m_your(add_your_values(m_your_cue.group)),
      m_opponent_cue(settings.add_cue(std::string(kOpponentId), "Opponent's position cue",
          "A lower, rounder pulse at a steady pace that pans and changes pitch with where the "
          "nearest opponent is, from you or, with relative to you off, on the stage.",
          kOpponentBoost)),
      m_opponent(add_opponent_values(m_opponent_cue.group)),
      m_pan_to_camera(settings.add_shared("position_cues", "Position cues")
              .add_switch(SwitchSpec{"pan_to_camera", "Position cues follow the camera",
                  "On, the cues pan across what the camera shows; off, across the whole stage to "
                  "the side blast zones. Cues relative to you are left alone.",
                  StageMapping{}.pan_to_camera, true})),
      m_gap_cue(settings.add_cue(std::string(kGapId), "Gap warning",
          "A low, steady hum while you are in the air with no floor below you, where falling "
          "straight down would knock you out.",
          kGapBoost)),
      m_gap(add_gap_values(m_gap_cue.group)),
      m_ledge_cue(settings.add_cue(std::string(kLedgeId), "Ledge warning",
          "A pulsing chord on the left or right as you near an edge with nothing below it, "
          "louder and closer to the centre the nearer you get.",
          kLedgeBoost)),
      m_ledge(add_ledge_values(m_ledge_cue.group)),
      m_appearance_cue(settings.add_cue(std::string(kAppearanceId), "Item appearance cue",
          "A sound where an item appears in a match: from you or, with Items relative to you "
          "off, on the stage.",
          kAppearanceBoost)),
      m_item_position_cue(settings.add_cue(std::string(kItemPositionId), "Item's position cue",
          "A pulse at a steady pace from the nearest item lying loose, from you or, with Items "
          "relative to you off, on the stage; it quickens while the item is within reach, where "
          "a press would pick it up.",
          kItemPositionBoost)),
      m_item_position_values(add_item_position_values(m_item_position_cue.group)),
      m_items(add_item_values(settings.add_shared("item_cues", "Item cues"))) {
    VoiceSpec your = position_voice(
        "your position", m_your_cue.values.cue, made_copies(tick_sound), kFastestInterval);
    VoiceSpec opponent = position_voice("opponent position", m_opponent_cue.values.cue,
        made_copies(pulse_sound), kOpponentFastestInterval);
    VoiceSpec gap;
    gap.name = "gap";
    gap.cue = m_gap_cue.values.cue;
    gap.sound = hum_sound();
    gap.mode = VoiceMode::looped;
    VoiceSpec ledge_left = ledge_voice("ledge left", m_ledge_cue.values.cue);
    m_your_voice = m_engine.add_voice(your);
    m_opponent_voice = m_engine.add_voice(opponent);
    m_gap_voice = m_engine.add_voice(gap);
    m_ledge_left_voice = m_engine.add_voice(ledge_left);
    VoiceSpec ledge_right = ledge_voice("ledge right", m_ledge_cue.values.cue, m_ledge_left_voice);
    m_ledge_right_voice = m_engine.add_voice(ledge_right);
    m_preview_voices.your = m_engine.add_voice(preview_of(std::move(your)));
    m_preview_voices.opponent = m_engine.add_voice(preview_of(std::move(opponent)));
    m_preview_voices.gap = m_engine.add_voice(preview_of(std::move(gap)));
    m_preview_voices.ledge_left = m_engine.add_voice(preview_of(std::move(ledge_left)));
    m_preview_voices.ledge_right = m_engine.add_voice(preview_of(std::move(ledge_right)));
    m_appearance_seconds = sounds.item_appearance.seconds();
    VoiceSpec appearance =
        appearance_voice(m_appearance_cue.values.cue, shifted_copies(sounds.item_appearance));
    m_appearance_pool.add(m_engine, appearance);
    m_appearance_preview_pool.add(m_engine, preview_of(std::move(appearance)));
    /* One voice per item tracked, the nearest's named plainly. */
    VoiceSpec item = position_voice("item position", m_item_position_cue.values.cue,
        shifted_copies(sounds.item_position), kItemFastestInterval);
    for (std::size_t i = 0; i < m_item_position_voices.size(); i++) {
        VoiceSpec spec = item;
        if (i > 0) {
            spec.name += " " + std::to_string(i + 1);
        }
        m_item_position_voices[i] = m_engine.add_voice(std::move(spec));
    }
    m_preview_voices.item_position = m_engine.add_voice(preview_of(std::move(item)));
}

void MatchCues::play(int voice, const std::optional<VoiceTargets>& targets) {
    if (voice < 0) {
        return;
    }
    if (targets.has_value()) {
        m_engine.start(voice, *targets);
    } else if (m_engine.playing(voice)) {
        m_engine.stop(voice);
    }
}

void MatchCues::silence() {
    /* The match's voices alone: a preview plays on over a silent match. */
    for (int voice :
        {m_your_voice, m_opponent_voice, m_gap_voice, m_ledge_left_voice, m_ledge_right_voice})
    {
        play(voice, std::nullopt);
    }
    m_appearance_pool.stop(m_engine);
    for (int voice : m_item_position_voices) {
        play(voice, std::nullopt);
    }
    m_opponent_position.forget();
    m_gap_warning.forget();
    m_item_position.forget();
}

void MatchCues::scene_entered() {
    silence();
    m_new_items.forget();
    m_appearance.forget();
    m_item_position.scene_entered();
}

void MatchCues::follow_item(std::size_t i, const std::optional<TrackedItem>& item) {
    const int voice = m_item_position_voices[i];
    if (voice < 0) {
        return;
    }
    if (!item.has_value()) {
        play(voice, std::nullopt);
        return;
    }
    /* The start line gives the pace a voice starts at; after it only the
     * trace shows a pace, so a change of reach gets a line of its own. */
    const bool moving = m_engine.playing(voice);
    m_engine.start(voice, item->targets);
    if (moving && item->within_reach != m_item_within_reach[i] && m_engine.logs()) {
        pc_log_line(
            "[a11y] cue item position %s", item->within_reach ? "within reach" : "out of reach");
    }
    m_item_within_reach[i] = item->within_reach;
}

MatchCues::Preview MatchCues::preview_named(std::string_view id) {
    if (id == kYourId) {
        return Preview::your;
    }
    if (id == kOpponentId) {
        return Preview::opponent;
    }
    if (id == kGapId) {
        return Preview::gap;
    }
    if (id == kLedgeId) {
        return Preview::ledge;
    }
    if (id == kAppearanceId) {
        return Preview::appearance;
    }
    if (id == kItemPositionId) {
        return Preview::item_position;
    }
    return Preview::none;
}

bool MatchCues::preview(std::string_view id, Clock::time_point now) {
    const Preview which = preview_named(id);
    if (which == Preview::none) {
        return false;
    }
    stop_preview(now);
    m_preview = which;
    m_preview_played = 0;
    m_preview_start = std::max(now, m_preview_quiet);
    preview_frame(now);
    return true;
}

void MatchCues::stop_preview(Clock::time_point now) {
    const PreviewVoices& v = m_preview_voices;
    bool stopped = false;
    for (int voice : {v.your, v.opponent, v.gap, v.ledge_left, v.ledge_right, v.item_position}) {
        if (voice >= 0 && m_engine.playing(voice)) {
            m_engine.stop(voice);
            stopped = true;
        }
    }
    if (m_appearance_preview_pool.playing(m_engine)) {
        m_appearance_preview_pool.stop(m_engine);
        stopped = true;
    }
    if (stopped) {
        m_preview_quiet = now + kPreviewRestart;
    }
    m_preview = Preview::none;
}

void MatchCues::preview_frame(Clock::time_point now) {
    if (m_preview == Preview::none || now < m_preview_start) {
        return;
    }
    const double seconds = std::chrono::duration<double>(now - m_preview_start).count();
    const PreviewVoices& v = m_preview_voices;
    /* The fighter sits at the centre, where the pan's frame changes
     * nothing; the opponent relative to you sits level with you. */
    const bool pan_to_camera = m_pan_to_camera.on();
    std::optional<VoiceTargets> targets;
    switch (m_preview) {
    case Preview::your:
        targets = your_position_preview(seconds, m_your.mapping(pan_to_camera));
        if (targets.has_value()) {
            play(v.your, targets);
            return;
        }
        break;
    case Preview::opponent:
        targets = opponent_position_preview(seconds, m_opponent.mapping(pan_to_camera));
        if (targets.has_value()) {
            play(v.opponent, targets);
            return;
        }
        break;
    case Preview::gap:
        targets = gap_warning_preview(seconds);
        if (targets.has_value()) {
            play(v.gap, targets);
            return;
        }
        break;
    case Preview::ledge:
        if (std::optional<LedgeTargets> ledge = ledge_warning_preview(seconds, m_ledge.mapping())) {
            play(v.ledge_left, ledge->left);
            play(v.ledge_right, ledge->right);
            return;
        }
        break;
    case Preview::appearance:
        if (appearance_preview_frame(seconds, pan_to_camera)) {
            return;
        }
        break;
    case Preview::item_position:
        targets = item_position_preview(
            seconds, m_item_position_values.mapping(m_items.mapping(pan_to_camera)));
        if (targets.has_value()) {
            play(v.item_position, targets);
            return;
        }
        break;
    case Preview::none:
        return;
    }
    /* Its end: the voices stop, and wait before they may start again. */
    stop_preview(now);
}

bool MatchCues::appearance_preview_frame(double seconds, bool pan_to_camera) {
    const std::array<OneShotPlay, 3> plays = appearance_preview(m_items.mapping(pan_to_camera));
    double end = 0.0;
    for (const OneShotPlay& play : plays) {
        end = std::max(end, play.at + m_appearance_seconds);
    }
    while (m_preview_played < plays.size() && seconds >= plays[m_preview_played].at) {
        m_appearance_preview_pool.trigger(m_engine, plays[m_preview_played].targets);
        m_preview_played++;
    }
    return m_preview_played < plays.size() || seconds < end + kAppearancePreviewTail;
}

void MatchCues::tick(const A11yMatchState& match) {
    const bool pan_to_camera = m_pan_to_camera.on();
    /* Each new item's one-shot, where it came in (item_appearance.hpp). A
     * pause stops the match's voices, a one-shot still sounding among them;
     * a trigger with no voice, its sound missing, plays nothing. */
    const std::span<const A11yMatchItem> appeared = m_new_items.tick(match);
    for (const VoiceTargets& targets :
        m_appearance.tick(match, appeared, m_items.mapping(pan_to_camera)))
    {
        m_appearance_pool.trigger(m_engine, targets);
    }
    if (match.paused && m_appearance_pool.playing(m_engine)) {
        m_appearance_pool.stop(m_engine);
    }
    const TrackedItems tracked =
        m_item_position.tick(match, m_item_position_values.mapping(m_items.mapping(pan_to_camera)));
    for (std::size_t i = 0; i < tracked.size(); i++) {
        follow_item(i, tracked[i]);
    }
    play(m_your_voice, your_position(match, m_your.mapping(pan_to_camera)));
    play(m_opponent_voice, m_opponent_position.tick(match, m_opponent.mapping(pan_to_camera)));
    play(m_gap_voice, m_gap_warning.tick(match, m_gap.mapping()));
    const LedgeTargets ledge = ledge_warning(match, m_ledge.mapping());
    play(m_ledge_left_voice, ledge.left);
    play(m_ledge_right_voice, ledge.right);
    if (match.running) {
        m_engine.trace_tick(m_tick++);
    }
}

}  // namespace a11y
