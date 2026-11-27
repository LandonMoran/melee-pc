/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Items appearing against hand-written match snapshots: new items found by
 * serial from GO to GAME!, those at GO taken as seen and none said twice;
 * the announcement's words, said queued on a fake screen reader behind its
 * two switches; the appearance cue's one-shots, held through a pause and
 * silent relative to you while you are out; the item cues' frame both ways;
 * and the preview's moments. Links item_appearance.cpp, item_frame.cpp,
 * position_cue.cpp, item_names.cpp and speech.cpp only, so this file
 * supplies the pc_log_line that main.c normally provides. */
#include "item_appearance.hpp"
#include "item_frame.hpp"
#include "speech.hpp"
#include "test_match_snapshot.hpp"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace {

using namespace match_test;
using a11y::VoiceTargets;

std::vector<std::string> s_lines;

bool near(float a, float b, float tolerance = 1e-4f) {
    return std::fabs(a - b) <= tolerance;
}

/* A loose item lying about. */
A11yMatchItem item(int serial, A11yItem kind, float x = 0.0f, float y = 0.0f) {
    A11yMatchItem it{};
    it.serial = serial;
    it.kind = kind;
    it.x = x;
    it.y = y;
    it.loose = true;
    it.holder = -1;
    return it;
}

/* You at the centre of Final Destination, past GO, with these items. */
A11yMatchState match_with(std::vector<A11yMatchItem> items) {
    A11yMatchState match{};
    match.running = true;
    match.you = fighter(0, 0.0f, 0.0f);
    match.stage = final_destination();
    match.item_count = static_cast<int>(items.size());
    match.items_in_match = match.item_count;
    std::copy(items.begin(), items.end(), match.items);
    return match;
}

std::vector<int> serials(std::span<const A11yMatchItem> items) {
    std::vector<int> found;
    for (const A11yMatchItem& it : items) {
        found.push_back(it.serial);
    }
    return found;
}

void a_new_item_is_found_once() {
    a11y::NewItems items;
    assert(items.tick(match_with({})).empty());
    std::span<const A11yMatchItem> fresh =
        items.tick(match_with({item(0, A11Y_It_Kind_BombHei, 30.0f, 10.0f)}));
    assert(fresh.size() == 1 && fresh[0].serial == 0 && fresh[0].kind == A11Y_It_Kind_BombHei);
    assert(items.appeared().size() == 1);
    /* Still listed: nothing new. */
    assert(items.tick(match_with({item(0, A11Y_It_Kind_BombHei, 32.0f, 4.0f)})).empty());
    assert(items.appeared().empty());
    /* Off the list, then back on it (a list cut short, a rollback that
     * makes it again): the same serial, never said twice. */
    assert(items.tick(match_with({})).empty());
    assert(items.tick(match_with({item(0, A11Y_It_Kind_BombHei)})).empty());
}

/* Online a rollback can shift serials: a serial seen that comes back as an
 * item of another kind is new; one of the same kind is taken as seen. */
void a_serial_reused_by_another_kind_is_new() {
    a11y::NewItems items;
    items.tick(match_with({}));
    assert((serials(items.tick(match_with({item(3, A11Y_It_Kind_L_Gun)}))) == std::vector<int>{3}));
    /* The corrected timeline: serial 3 a Home-Run Bat, said; serial 4 the
     * Ray Gun, as it is a serial not seen. */
    std::span<const A11yMatchItem> fresh =
        items.tick(match_with({item(3, A11Y_It_Kind_Bat), item(4, A11Y_It_Kind_L_Gun)}));
    assert(fresh.size() == 2 && fresh[0].kind == A11Y_It_Kind_Bat && fresh[1].serial == 4);
    /* Each seen on its own key: neither again. */
    assert(items.tick(match_with({item(3, A11Y_It_Kind_L_Gun)})).empty());
    assert(items.tick(match_with({item(3, A11Y_It_Kind_Bat)})).empty());
    /* The same kind on a serial seen, a true re-creation or another of its
     * kind: taken as seen. */
    assert(items.tick(match_with({item(4, A11Y_It_Kind_L_Gun)})).empty());
    /* From GAME! on, a reused serial marks nothing seen either. */
    A11yMatchState over = match_with({item(3, A11Y_It_Kind_Heart)});
    over.decided = true;
    assert(items.tick(over).empty());
    over.decided = false;
    assert((serials(items.tick(over)) == std::vector<int>{3}));
}

void a_burst_comes_in_the_lists_order() {
    a11y::NewItems items;
    items.tick(match_with({item(4, A11Y_It_Kind_Heart)}));
    std::span<const A11yMatchItem> fresh = items.tick(match_with({item(7, A11Y_It_Kind_Foods),
        item(4, A11Y_It_Kind_Heart), item(5, A11Y_It_Kind_Foods), item(6, A11Y_It_Kind_Tomato)}));
    assert((serials(fresh) == std::vector<int>{7, 5, 6}));
}

void items_there_at_go_are_taken_as_seen() {
    a11y::NewItems items;
    /* Before GO: your input is off. */
    A11yMatchState before = match_with({item(0, A11Y_It_Kind_Capsule)});
    before.you.can_move = false;
    assert(items.tick(before).empty());
    before.item_count = 2;
    before.items[1] = item(1, A11Y_It_Kind_Box);
    assert(items.tick(before).empty());
    /* The GO tick itself, and an item on it, are taken as seen too. */
    A11yMatchState go = match_with(
        {item(0, A11Y_It_Kind_Capsule), item(1, A11Y_It_Kind_Box), item(2, A11Y_It_Kind_Egg)});
    assert(items.tick(go).empty());
    go.item_count = 4;
    go.items[3] = item(3, A11Y_It_Kind_Taru);
    assert((serials(items.tick(go)) == std::vector<int>{3}));
    /* GO, once come, holds: your input off again (a respawn) changes
     * nothing. */
    A11yMatchState respawn = go;
    respawn.you.can_move = false;
    respawn.item_count = 5;
    respawn.items[4] = item(9, A11Y_It_Kind_Star);
    assert((serials(items.tick(respawn)) == std::vector<int>{9}));
}

void nothing_from_game_and_after_a_scene() {
    a11y::NewItems items;
    items.tick(match_with({}));
    /* From GAME! or TIME! on, nothing is new; online a rollback can take
     * GAME! back, and an item listed only then is new as the match goes
     * on. */
    A11yMatchState over = match_with({item(3, A11Y_It_Kind_Foods)});
    over.decided = true;
    assert(items.tick(over).empty());
    over.decided = false;
    assert((serials(items.tick(over)) == std::vector<int>{3}));
    over.decided = true;
    over.item_count = 2;
    over.items[1] = item(4, A11Y_It_Kind_Foods);
    assert(items.tick(over).empty());
    /* Outside a match nothing is listed, and nothing changes. */
    assert(items.tick(A11yMatchState{}).empty());
    /* A new scene, a new match: serials from 0 again, and before GO. */
    items.forget();
    A11yMatchState next = match_with({item(3, A11Y_It_Kind_Foods)});
    assert(items.tick(next).empty());
    next.item_count = 2;
    next.items[1] = item(0, A11Y_It_Kind_Foods);
    assert((serials(items.tick(next)) == std::vector<int>{0}));
}

void paused_ticks_find_items_too() {
    a11y::NewItems items;
    items.tick(match_with({}));
    /* Training's item menu holds the match while it makes the item. */
    A11yMatchState paused = match_with({item(0, A11Y_It_Kind_L_Gun)});
    paused.paused = true;
    assert((serials(items.tick(paused)) == std::vector<int>{0}));
}

void the_words_name_each_item() {
    a11y::LogOnce log_once;
    const std::vector<A11yMatchItem> one{item(0, A11Y_It_Kind_BombHei)};
    assert(a11y::appearance_text(one, log_once) == "Bob-omb");
    const std::vector<A11yMatchItem> burst{
        item(1, A11Y_It_Kind_Foods), item(2, A11Y_It_Kind_Foods), item(3, A11Y_It_Kind_Tomato)};
    assert(a11y::appearance_text(burst, log_once) == "Food, Food, Maxim Tomato");
    const std::vector<A11yMatchItem> container{item(4, A11Y_It_Kind_Capsule)};
    assert(a11y::appearance_text(container, log_once) == "Capsule");
    s_lines.clear();
    const std::vector<A11yMatchItem> odd{
        item(5, static_cast<A11yItem>(0x40)), item(6, A11Y_It_Kind_Heart)};
    assert(a11y::appearance_text(odd, log_once) == "Unknown item, Heart Container");
    a11y::appearance_text(odd, log_once);
    assert(s_lines.size() == 1 && s_lines[0].find("no name") != std::string::npos);
}

class FakeBridge final : public a11y::ScreenReaderBridge {
public:
    explicit FakeBridge(std::vector<std::string>& spoken) : m_spoken(spoken) {}
    std::string init() override { return "Fake"; }
    void shutdown() override {}
    std::string output(std::string_view text, bool interrupt) override {
        m_spoken.push_back((interrupt ? "interrupt " : "queue ") + std::string(text));
        return "";
    }

private:
    std::vector<std::string>& m_spoken;
};

void the_announcement_is_queued_behind_its_switches() {
    std::vector<std::string> spoken;
    a11y::Speech speech{a11y::Config{}, std::make_unique<FakeBridge>(spoken)};
    speech.init();
    a11y::AppearanceAnnouncement announcement{speech};
    a11y::MatchAnnouncementValues values;
    assert(values.item_appearances);
    const std::vector<A11yMatchItem> burst{
        item(1, A11Y_It_Kind_Foods), item(2, A11Y_It_Kind_Foods), item(3, A11Y_It_Kind_Tomato)};
    announcement.tick(burst, values);
    assert((spoken == std::vector<std::string>{"queue Food, Food, Maxim Tomato"}));
    /* A tick with nothing new says nothing. */
    announcement.tick({}, values);
    assert(spoken.size() == 1);
    /* Item appearances off, and Match announcements off above it. */
    values.item_appearances = false;
    announcement.tick(burst, values);
    values.item_appearances = true;
    values.on = false;
    announcement.tick(burst, values);
    assert(spoken.size() == 1);
    speech.shutdown();
}

void the_cue_plays_where_each_item_came_in() {
    a11y::AppearanceCue cue;
    const a11y::ItemMapping mapping;
    const A11yMatchState match = match_with({item(0, A11Y_It_Kind_L_Gun, -40.0f, 0.0f),
        item(1, A11Y_It_Kind_Foods, 50.0f, 30.0f), item(2, A11Y_It_Kind_Foods, 0.0f, 0.0f)});
    const std::vector<VoiceTargets> plays =
        cue.tick(match, std::span(match.items, match.item_count), mapping);
    assert(plays.size() == 3);
    /* Relative to you: on your left, on your right and higher, and level
     * with you at the centre. */
    const a11y::RelativeMapping& r = mapping.from_you;
    assert(near(plays[0].pan, -(r.pan_floor + (1.0f - r.pan_floor) * 40.0f / r.side_distance)));
    assert(near(plays[0].pitch, 1.0f) && plays[0].gain == 1.0f);
    assert(plays[1].pan > 0.0f && plays[1].pitch > 1.0f);
    assert(near(
        plays[1].pitch, std::exp2(mapping.stage.pitch_range * 0.5f * 30.0f / r.height_distance)));
    assert(plays[2].pan == 0.0f && plays[2].pitch == 1.0f);
    /* Nothing new, nothing played. */
    assert(cue.tick(match, {}, mapping).empty());
}

void the_cue_waits_out_a_pause() {
    a11y::AppearanceCue cue;
    const a11y::ItemMapping mapping;
    /* Made while Training's menu holds the match: held. */
    A11yMatchState paused = match_with(
        {item(0, A11Y_It_Kind_L_Gun, 0.0f, 0.0f), item(1, A11Y_It_Kind_Kinoko, 20.0f, 0.0f)});
    paused.paused = true;
    assert(cue.tick(paused, std::span(paused.items, 2), mapping).empty());
    /* Still paused: still held. */
    assert(cue.tick(paused, {}, mapping).empty());
    /* The menu closes: each plays where it is now, unless it has left the
     * list (the Mushroom walked off the stage while the menu stayed open),
     * held ones before any new on the tick. */
    A11yMatchState resumed = match_with(
        {item(0, A11Y_It_Kind_L_Gun, -30.0f, 0.0f), item(5, A11Y_It_Kind_Heart, 10.0f, 0.0f)});
    const std::vector<VoiceTargets> plays =
        cue.tick(resumed, std::span(resumed.items + 1, 1), mapping);
    assert(plays.size() == 2 && plays[0].pan < 0.0f && plays[1].pan > 0.0f);
    /* Played once. */
    assert(cue.tick(resumed, {}, mapping).empty());
    /* A scene entry forgets what was held. */
    assert(cue.tick(paused, std::span(paused.items, 1), mapping).empty());
    cue.forget();
    assert(cue.tick(resumed, {}, mapping).empty());
}

void the_cue_is_silent_relative_to_you_while_you_are_out() {
    a11y::AppearanceCue cue;
    a11y::ItemMapping mapping;
    A11yMatchState out = match_with({item(0, A11Y_It_Kind_Foods, 20.0f, 0.0f)});
    out.you.out = true;
    assert(cue.tick(out, std::span(out.items, 1), mapping).empty());
    /* Dropped, not held: back in play, nothing. */
    assert(cue.tick(match_with({item(0, A11Y_It_Kind_Foods, 20.0f, 0.0f)}), {}, mapping).empty());
    /* On the stage, an item has its place while you are out. */
    mapping.relative = false;
    const std::vector<VoiceTargets> plays = cue.tick(out, std::span(out.items, 1), mapping);
    assert(plays.size() == 1 && plays[0].pan > 0.0f);
}

void the_frame_relative_to_you() {
    a11y::ItemMapping mapping;
    const a11y::RelativeMapping& r = mapping.from_you;
    A11yMatchState match = match_with({});
    match.you = fighter(0, -60.0f, 20.0f);
    /* From you, not from the stage: at the stage's centre, 60 units to your
     * right, and level. */
    std::optional<VoiceTargets> t = a11y::item_targets(match, 0.0f, 20.0f, 1, mapping);
    assert(t.has_value() &&
           near(t->pan, r.pan_floor + (1.0f - r.pan_floor) * 60.0f / r.side_distance));
    assert(near(t->pitch, 1.0f) && t->gain == 1.0f);
    /* Full beyond the side distance; half the range down at the height
     * distance below, held beyond. */
    t = a11y::item_targets(match, -300.0f, 20.0f - 2.0f * r.height_distance, -1, mapping);
    assert(near(t->pan, -1.0f) && near(t->pitch, std::exp2(-mapping.stage.pitch_range / 2.0f)));
    /* The item cues' own pitch range. */
    mapping.stage.pitch_range = 1.0f;
    t = a11y::item_targets(match, -60.0f, 20.0f + r.height_distance, 0, mapping);
    assert(t->pan == 0.0f && near(t->pitch, std::sqrt(2.0f)));
    /* The camera switch leaves it alone. */
    a11y::ItemMapping blast = mapping;
    blast.stage.pan_to_camera = false;
    assert(near(a11y::item_targets(match, 0.0f, 20.0f, 1, blast)->pan,
        a11y::item_targets(match, 0.0f, 20.0f, 1, mapping)->pan));
    /* Silent while you are out. */
    match.you.out = true;
    assert(!a11y::item_targets(match, 0.0f, 20.0f, 1, mapping).has_value());
    /* The side with none kept: the distance's sign, the centre when
     * exactly level. */
    assert(
        a11y::item_side(-0.1f) == -1 && a11y::item_side(0.1f) == 1 && a11y::item_side(0.0f) == 0);
}

void the_frame_on_the_stage() {
    a11y::ItemMapping mapping;
    mapping.relative = false;
    A11yMatchState match = match_with({});
    match.you = fighter(0, -60.0f, 20.0f);
    const A11yStageFrame stage = final_destination();
    /* Your position cue's mapping: the camera box's sides, wherever you
     * are, and the item cues' pitch range from the bottom blast zone to the
     * camera's top. */
    std::optional<VoiceTargets> t = a11y::item_targets(match, 85.0f, stage.camera_top, 1, mapping);
    assert(t.has_value() && near(t->pan, 0.5f) && near(t->pitch, 2.0f));
    t = a11y::item_targets(match, 0.0f, stage.blast_bottom, -1, mapping);
    assert(near(t->pan, 0.0f) && near(t->pitch, 0.5f));
    mapping.stage.pitch_range = 1.0f;
    assert(near(
        a11y::item_targets(match, 0.0f, stage.camera_top, 1, mapping)->pitch, std::sqrt(2.0f)));
    /* The shared switch frames it by the blast zones. */
    mapping.stage.pan_to_camera = false;
    assert(near(a11y::item_targets(match, 123.0f, 0.0f, 1, mapping)->pan, 0.5f));
    /* Your being out does not matter here. */
    match.you.out = true;
    assert(a11y::item_targets(match, 123.0f, 0.0f, 1, mapping).has_value());
}

void the_preview_plays_left_right_and_above() {
    a11y::ItemMapping mapping;
    std::array<a11y::OneShotPlay, 3> plays = a11y::appearance_preview(mapping);
    assert(plays[0].at == 0.0 && near(static_cast<float>(plays[1].at), 0.6f) &&
           near(static_cast<float>(plays[2].at), 1.2f));
    /* Relative to you: left and right at the same pan, level; then centred
     * and higher. */
    assert(plays[0].targets.pan < -mapping.from_you.pan_floor);
    assert(near(plays[1].targets.pan, -plays[0].targets.pan));
    assert(near(plays[0].targets.pitch, 1.0f) && near(plays[1].targets.pitch, 1.0f));
    assert(plays[2].targets.pan == 0.0f && plays[2].targets.pitch > 1.0f);
    /* A tuned value is heard as tuned. */
    mapping.from_you.pan_floor = 0.9f;
    assert(a11y::appearance_preview(mapping)[0].targets.pan < -0.9f);
    /* On the stage: you at the centre, so the same order, at the stage's
     * pan and pitch. */
    mapping.relative = false;
    plays = a11y::appearance_preview(mapping);
    assert(plays[0].targets.pan < 0.0f && near(plays[1].targets.pan, -plays[0].targets.pan));
    assert(plays[2].targets.pan == 0.0f);
    assert(near(plays[0].targets.pitch, plays[1].targets.pitch));
    assert(plays[2].targets.pitch > plays[0].targets.pitch);
}

}  // namespace

extern "C" void pc_log_line(const char* fmt, ...) {
    char buffer[512];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    s_lines.emplace_back(buffer);
}

int main() {
    a_new_item_is_found_once();
    a_serial_reused_by_another_kind_is_new();
    a_burst_comes_in_the_lists_order();
    items_there_at_go_are_taken_as_seen();
    nothing_from_game_and_after_a_scene();
    paused_ticks_find_items_too();
    the_words_name_each_item();
    the_announcement_is_queued_behind_its_switches();
    the_cue_plays_where_each_item_came_in();
    the_cue_waits_out_a_pause();
    the_cue_is_silent_relative_to_you_while_you_are_out();
    the_frame_relative_to_you();
    the_frame_on_the_stage();
    the_preview_plays_left_right_and_above();
    std::cout << "item_appearance: all tests passed\n";
    return 0;
}
