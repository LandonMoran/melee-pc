/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The item's position cue's driver (.scratch/items/spec.md, "The item's
 * position cue", "Ready for several items"; primer docs/a11y/cues.md, "The
 * item's position cue"): from the match snapshot's items, nearest to you
 * first, to a pulsed voice's targets for the loose item within reach, or
 * with none the nearest loose item, chosen afresh each tick, in the item
 * cues' frame (item_frame.hpp). A steady pace, and a faster one while the
 * item is within reach. Its only state is each tracked item's side of you,
 * kept by serial, so following N items is N voices and kTrackedItems. Touches
 * no game state. Game thread only. */
#pragma once
#include "cue_engine.hpp"
#include "game_access.h"
#include "item_frame.hpp"
#include <array>
#include <optional>
#include <unordered_map>

namespace a11y {

/* How many loose items the cue follows, each on a voice of its own: the one
 * within reach on the first, then the nearest. One, with no row: how
 * several sound together is a later feature's question. */
constexpr int kTrackedItems = 1;

/* The item's position cue's mapping, as its settings hold it. */
struct ItemPositionMapping {
    /* The item cues' frame, its pitch range the cue's own. */
    ItemMapping frame;
    /* Seconds between pulses: steady, and while the item is within reach,
     * where pressing the button would take it. */
    float steady_interval = 0.5f;
    float reach_interval = 0.15f;
};

/* What one voice follows this tick: the item, by serial, and the voice's
 * targets. */
struct TrackedItem {
    int serial = -1;
    bool within_reach = false;
    VoiceTargets targets;
};

/* The voices' items, the one within reach first, then nearest first; none
 * for a voice with no item. */
using TrackedItems = std::array<std::optional<TrackedItem>, kTrackedItems>;

class ItemPosition {
public:
    /* Once a tick: the loose items' targets, the one within reach first,
     * even when another lies nearer your feet, so the quick pace always
     * means the button takes the item heard; then the nearest. None for
     * each voice while the cue is silent: outside a match, paused, before
     * GO, from GAME! or TIME! on, with no loose item, and relative to you
     * while you are out, since your place is then where you were knocked
     * out. GO is the first tick with your input on, held through a respawn;
     * on the stage the cue sounds on while you are out. Holding an item
     * does not silence it: where the others are stays useful. Another item
     * taking the voice, by coming nearer or into reach, only moves it.
     * Relative to you an item keeps its side until it is the crossing
     * margin past you, as the opponent does; one newly tracked, or tracked
     * again, takes its side afresh, an item exactly level with you sounding
     * centred until it moves. */
    TrackedItems tick(const A11yMatchState& match, const ItemPositionMapping& mapping);
    /* The sides forgotten, after a silence: the next tick takes each afresh
     * from its snapshot. */
    void forget() { m_sides.clear(); }
    /* At each scene entry: a new match, before its GO. */
    void scene_entered();

private:
    bool m_started = false;               /* GO has come */
    std::unordered_map<int, int> m_sides; /* serial to side: -1 left, 1 right, 0 centre */
};

/* The item's position cue's preview (cue_preview.hpp): an item a little to
 * your right, level with you, with you at the centre of Final Destination,
 * in the frame the mapping sets: kItemPreviewSteadyPulses pulses at the
 * steady pace, then kItemPreviewReachPulses at the within-reach pace; none
 * once it has ended. */
constexpr int kItemPreviewSteadyPulses = 3;
constexpr int kItemPreviewReachPulses = 4;
std::optional<VoiceTargets> item_position_preview(
    double seconds, const ItemPositionMapping& mapping);
/* When the preview's pace changes, seconds after it begins: late in the last
 * steady pulse's interval, so the first fast pulse comes at once, a whole
 * fast interval before the next (CueEngine starts the rhythm afresh when
 * the time waited is twice the new interval). */
double item_position_preview_change(const ItemPositionMapping& mapping);

}  // namespace a11y
