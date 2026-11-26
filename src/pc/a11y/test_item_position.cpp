/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The item's position cue's driver against hand-written match snapshots:
 * the nearest loose item tracked, a held or thrown one skipped; a change of
 * nearest item; the two paces; the item within reach followed ahead of a
 * nearer one, but not one that is not loose; both frames, the crossing
 * margin held and flipped; silence while paused, before GO, with none loose
 * and while you are out relative to you; tracking on while you hold an
 * item; and the preview's moments. Links item_position.cpp, item_frame.cpp
 * and position_cue.cpp only. */
#include "item_position.hpp"
#include "test_match_snapshot.hpp"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

namespace {

using namespace match_test;
using a11y::ItemPosition;
using a11y::ItemPositionMapping;
using a11y::TrackedItems;
using a11y::VoiceTargets;

bool near(float a, float b, float tolerance = 1e-4f) {
    return std::fabs(a - b) <= tolerance;
}

/* A loose item lying about. */
A11yMatchItem item(int serial, float x, float y = 0.0f) {
    A11yMatchItem it{};
    it.serial = serial;
    it.kind = A11Y_It_Kind_Foods;
    it.x = x;
    it.y = y;
    it.loose = true;
    it.holder = -1;
    return it;
}

/* Held by the fighter in `slot`. */
A11yMatchItem held(int serial, float x, int slot) {
    A11yMatchItem it = item(serial, x);
    it.loose = false;
    it.holder = slot;
    return it;
}

/* You at (you_x, 0) on Final Destination, past GO, with these items, which
 * the caller lists nearest first as the snapshot does. */
A11yMatchState match_with(std::vector<A11yMatchItem> items, float you_x = 0.0f) {
    A11yMatchState match{};
    match.running = true;
    match.you = fighter(0, you_x, 0.0f);
    match.stage = final_destination();
    match.item_count = static_cast<int>(items.size());
    match.items_in_match = match.item_count;
    std::copy(items.begin(), items.end(), match.items);
    return match;
}

const a11y::TrackedItem& first(const TrackedItems& tracked) {
    assert(tracked[0].has_value());
    return *tracked[0];
}

void one_voice_for_now() {
    static_assert(a11y::kTrackedItems == 1);
    static_assert(std::tuple_size_v<TrackedItems> == a11y::kTrackedItems);
}

void the_nearest_loose_item_is_tracked() {
    ItemPosition cue;
    const ItemPositionMapping mapping;
    /* A held item nearest, then one flying after a throw (not loose, held
     * by no one), then the loose one to follow. */
    A11yMatchItem thrown = item(5, 20.0f);
    thrown.loose = false;
    const TrackedItems tracked =
        cue.tick(match_with({held(4, 10.0f, 1), thrown, item(6, -40.0f), item(7, 60.0f)}), mapping);
    const a11y::TrackedItem& it = first(tracked);
    assert(it.serial == 6 && !it.within_reach);
    /* Relative to you, 40 units to your left: the floor plus half the way
     * to full at the side distance of 80. */
    assert(near(it.targets.pan, -0.6f) && near(it.targets.pitch, 1.0f));
    assert(near(it.targets.gain, 1.0f));
    assert(near(it.targets.interval, mapping.steady_interval));
}

void a_new_nearest_item_takes_the_voice() {
    ItemPosition cue;
    const ItemPositionMapping mapping;
    assert(first(cue.tick(match_with({item(1, -40.0f), item(2, 70.0f)}), mapping)).serial == 1);
    /* Another comes nearer on the other side: the same voice follows it. */
    const a11y::TrackedItem& it =
        first(cue.tick(match_with({item(3, 20.0f), item(1, -40.0f), item(2, 70.0f)}), mapping));
    assert(it.serial == 3 && near(it.targets.pan, 0.4f));
    /* The nearest picked up (held now, listed nearest still): the next. */
    const a11y::TrackedItem& next =
        first(cue.tick(match_with({held(3, 0.0f, 0), item(1, -40.0f), item(2, 70.0f)}), mapping));
    assert(next.serial == 1 && near(next.targets.pan, -0.6f));
}

void within_reach_quickens_the_pace() {
    ItemPosition cue;
    ItemPositionMapping mapping;
    mapping.steady_interval = 0.6f;
    mapping.reach_interval = 0.1f;
    A11yMatchItem close = item(0, 8.0f);
    close.within_reach = true;
    const a11y::TrackedItem& it = first(cue.tick(match_with({close}), mapping));
    assert(it.within_reach && near(it.targets.interval, 0.1f));
    /* Turned away, out of reach again: the steady pace. */
    close.within_reach = false;
    assert(near(first(cue.tick(match_with({close}), mapping)).targets.interval, 0.6f));
}

void the_item_within_reach_is_followed_first() {
    ItemPosition cue;
    ItemPositionMapping mapping;
    mapping.steady_interval = 0.6f;
    mapping.reach_interval = 0.1f;
    /* One just behind you, nearest, and one in front within reach, as the
     * game's test finds it: the voice follows the one in front, quick. */
    assert(first(cue.tick(match_with({item(2, -10.0f), item(1, 30.0f)}), mapping)).serial == 2);
    A11yMatchItem ahead = item(1, 30.0f);
    ahead.within_reach = true;
    const a11y::TrackedItem& it = first(cue.tick(match_with({item(2, -10.0f), ahead}), mapping));
    assert(it.serial == 1 && it.within_reach);
    assert(near(it.targets.interval, 0.1f) && near(it.targets.pan, 0.5f));
    /* Turned away from it: back to the nearest, steady, its side afresh. */
    const a11y::TrackedItem& back =
        first(cue.tick(match_with({item(2, -10.0f), item(1, 30.0f)}), mapping));
    assert(back.serial == 2 && !back.within_reach);
    assert(near(back.targets.interval, 0.6f) && back.targets.pan < 0.0f);
    /* In reach but not loose, a thrown item flying in front of you: the
     * nearest loose one is followed, steady. */
    A11yMatchItem thrown = item(3, 15.0f);
    thrown.loose = false;
    thrown.within_reach = true;
    const a11y::TrackedItem& loose =
        first(cue.tick(match_with({item(2, -10.0f), thrown}), mapping));
    assert(loose.serial == 2 && near(loose.targets.interval, 0.6f));
}

void the_side_is_kept_until_past_the_crossing_margin() {
    ItemPosition cue;
    ItemPositionMapping mapping;
    mapping.frame.from_you.crossing_margin = 4.0f;
    /* On your right, then you walk over it: still on the right within the
     * margin past you, on the left beyond it. */
    assert(first(cue.tick(match_with({item(0, 10.0f)}), mapping)).targets.pan > 0.0f);
    assert(first(cue.tick(match_with({item(0, 10.0f)}, 12.0f), mapping)).targets.pan > 0.0f);
    const float held = first(cue.tick(match_with({item(0, 10.0f)}, 13.9f), mapping)).targets.pan;
    assert(near(held, 0.2f + 0.8f * 3.9f / 80.0f));
    const float flipped = first(cue.tick(match_with({item(0, 10.0f)}, 14.5f), mapping)).targets.pan;
    assert(flipped < 0.0f);
    /* And back over it: held on the left until 4 units past. */
    assert(first(cue.tick(match_with({item(0, 10.0f)}, 7.0f), mapping)).targets.pan < 0.0f);
    assert(first(cue.tick(match_with({item(0, 10.0f)}, 5.5f), mapping)).targets.pan > 0.0f);
}

void each_item_keeps_its_own_side() {
    ItemPosition cue;
    const ItemPositionMapping mapping;
    /* Item 0 just right of you after you crossed it from the left: kept on
     * the left. */
    cue.tick(match_with({item(0, 10.0f)}, 13.0f), mapping);
    cue.tick(match_with({item(0, 10.0f)}, 11.0f), mapping);
    assert(first(cue.tick(match_with({item(0, 10.0f)}, 9.0f), mapping)).targets.pan < 0.0f);
    /* Another becomes the nearest: it takes its own side from where it is,
     * on your right. */
    assert(first(cue.tick(match_with({item(1, 9.5f), item(0, 10.0f)}, 9.0f), mapping)).targets.pan >
           0.0f);
    /* Item 0 the nearest again: no longer tracked meanwhile, it takes its
     * side afresh, the right. */
    assert(first(cue.tick(match_with({item(0, 10.0f)}, 9.0f), mapping)).targets.pan > 0.0f);
    /* Exactly level with you, new: centred, and so until it moves. */
    a11y::ItemPosition fresh;
    assert(near(first(fresh.tick(match_with({item(2, 9.0f)}, 9.0f), mapping)).targets.pan, 0.0f));
    assert(first(fresh.tick(match_with({item(2, 8.0f)}, 9.0f), mapping)).targets.pan < 0.0f);
}

void the_pitch_follows_the_height_and_the_cues_range() {
    ItemPosition cue;
    ItemPositionMapping mapping;
    mapping.frame.stage.pitch_range = 1.0f;
    /* Relative to you, at the height distance above: half the range up. */
    assert(near(first(cue.tick(match_with({item(0, 10.0f, 60.0f)}), mapping)).targets.pitch,
        std::exp2(0.5f)));
}

void on_the_stage_it_is_where_the_item_is() {
    ItemPosition cue;
    ItemPositionMapping mapping;
    mapping.frame.relative = false;
    mapping.frame.stage.pitch_range = 2.0f;
    /* You far right; the item 85 units right of centre, on the floor:
     * where it is on the stage, whatever your place. */
    A11yMatchState match = match_with({item(0, 85.0f)}, 150.0f);
    const a11y::TrackedItem& it = first(cue.tick(match, mapping));
    assert(near(it.targets.pan, 0.5f));
    const float expected_pitch = std::exp2(2.0f * (140.0f / 254.0f - 0.5f));
    assert(near(it.targets.pitch, expected_pitch));
    /* The pan frame the position cues share: the blast zones. */
    mapping.frame.stage.pan_to_camera = false;
    assert(near(first(cue.tick(match, mapping)).targets.pan, 85.0f / 246.0f));
    /* While you are out it sounds on: the stage has no need of your
     * place. */
    match.you.out = true;
    match.you.can_move = false;
    assert(cue.tick(match, mapping)[0].has_value());
}

void silent_where_the_spec_says() {
    const ItemPositionMapping mapping;
    ItemPosition cue;
    /* Outside a match. */
    assert(!cue.tick(A11yMatchState{}, mapping)[0].has_value());
    /* Before GO: your input still off. */
    A11yMatchState before = match_with({item(0, 20.0f)});
    before.you.can_move = false;
    assert(!cue.tick(before, mapping)[0].has_value());
    /* GO: it starts. */
    assert(cue.tick(match_with({item(0, 20.0f)}), mapping)[0].has_value());
    /* Paused. */
    A11yMatchState paused = match_with({item(0, 20.0f)});
    paused.paused = true;
    assert(!cue.tick(paused, mapping)[0].has_value());
    /* No loose item: none at all, or only held and thrown ones. */
    assert(!cue.tick(match_with({}), mapping)[0].has_value());
    A11yMatchItem thrown = item(2, 20.0f);
    thrown.loose = false;
    assert(!cue.tick(match_with({held(1, 0.0f, 2), thrown}), mapping)[0].has_value());
    /* From GAME! or TIME! on: the match is over, though its last ticks run
     * on. */
    A11yMatchState over = match_with({item(0, 20.0f)});
    over.decided = true;
    assert(!cue.tick(over, mapping)[0].has_value());
    /* Relative to you while you are out. */
    A11yMatchState out = match_with({item(0, 20.0f)});
    out.you.out = true;
    out.you.can_move = false;
    assert(!cue.tick(out, mapping)[0].has_value());
    /* Back on the respawn platform, your input off still: GO holds. */
    A11yMatchState respawn = match_with({item(0, 20.0f)});
    respawn.you.can_move = false;
    assert(cue.tick(respawn, mapping)[0].has_value());
    /* A new scene, a new match: before its GO again. */
    cue.scene_entered();
    assert(!cue.tick(respawn, mapping)[0].has_value());
}

void a_silence_forgets_the_side() {
    ItemPosition cue;
    const ItemPositionMapping mapping;
    cue.tick(match_with({item(0, 10.0f)}, 8.0f), mapping);
    /* You cross it within the margin: kept on the right. */
    assert(first(cue.tick(match_with({item(0, 10.0f)}, 11.0f), mapping)).targets.pan > 0.0f);
    /* A pause, then the same place: taken afresh, the left. */
    A11yMatchState paused = match_with({item(0, 10.0f)}, 11.0f);
    paused.paused = true;
    cue.tick(paused, mapping);
    assert(first(cue.tick(match_with({item(0, 10.0f)}, 11.0f), mapping)).targets.pan < 0.0f);
    /* The port menu's silence (forget) likewise: on the right from here,
     * kept there without it. */
    cue.tick(match_with({item(0, 10.0f)}, 5.0f), mapping);
    cue.forget();
    assert(first(cue.tick(match_with({item(0, 10.0f)}, 11.0f), mapping)).targets.pan < 0.0f);
}

void it_tracks_while_you_hold_an_item() {
    ItemPosition cue;
    const ItemPositionMapping mapping;
    /* You hold a Ray Gun, listed nearest at your place; the Food further
     * off is followed. */
    const TrackedItems tracked = cue.tick(match_with({held(0, 0.0f, 0), item(1, 50.0f)}), mapping);
    assert(first(tracked).serial == 1 && first(tracked).targets.pan > 0.0f);
}

void the_preview_pulses_steady_then_fast() {
    for (const bool relative : {true, false}) {
        ItemPositionMapping mapping;
        mapping.frame.relative = relative;
        const double change = a11y::item_position_preview_change(mapping);
        /* Three steady pulses, the change late in the third's interval. */
        assert(std::fabs(change - (2.0 + 0.8) * mapping.steady_interval) < 1e-6);
        std::optional<VoiceTargets> at_start = a11y::item_position_preview(0.0, mapping);
        assert(at_start.has_value() && near(at_start->interval, mapping.steady_interval));
        /* A little to your right. */
        assert(at_start->pan > 0.05f && at_start->pan < 0.5f);
        if (relative) {
            assert(near(at_start->pan, 0.4f) && near(at_start->pitch, 1.0f));
        }
        std::optional<VoiceTargets> late = a11y::item_position_preview(change - 0.01, mapping);
        assert(late.has_value() && near(late->interval, mapping.steady_interval));
        std::optional<VoiceTargets> fast = a11y::item_position_preview(change, mapping);
        assert(fast.has_value() && near(fast->interval, mapping.reach_interval));
        assert(near(fast->pan, at_start->pan) && near(fast->pitch, at_start->pitch));
        /* Four fast pulses, then none. */
        const double end = change + (4 - 1 + 0.6) * mapping.reach_interval;
        assert(a11y::item_position_preview(end - 0.01, mapping).has_value());
        assert(!a11y::item_position_preview(end, mapping).has_value());
    }
}

}  // namespace

int main() {
    one_voice_for_now();
    the_nearest_loose_item_is_tracked();
    a_new_nearest_item_takes_the_voice();
    within_reach_quickens_the_pace();
    the_item_within_reach_is_followed_first();
    the_side_is_kept_until_past_the_crossing_margin();
    each_item_keeps_its_own_side();
    the_pitch_follows_the_height_and_the_cues_range();
    on_the_stage_it_is_where_the_item_is();
    silent_where_the_spec_says();
    a_silence_forgets_the_side();
    it_tracks_while_you_hold_an_item();
    the_preview_pulses_steady_then_fast();
    std::cout << "item_position: all tests passed\n";
    return 0;
}
