/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "item_position.hpp"
#include "cue_preview.hpp"
#include "position_cue.hpp"

namespace a11y {
namespace {

/* Where the preview's item lies from you: a few steps to your right, level
 * with you, a little to that side relative to you. */
constexpr float kPreviewSide = 20.0f;

/* How far into the last steady pulse's interval the preview's pace
 * changes. */
constexpr double kPreviewChangeShare = 0.8;

/* An item's side of you: the side kept for it until it is the margin past
 * you, or, with none kept, or the centre kept, its side now. */
int side_of(float dx, int kept, float margin) {
    return kept == 0 ? item_side(dx) : opponent_side(dx, kept, margin);
}

}  // namespace

TrackedItems ItemPosition::tick(const A11yMatchState& match, const ItemPositionMapping& mapping) {
    TrackedItems tracked{};
    if (match.running) {
        m_started = m_started || match.you.can_move;
    }
    const bool silent = !match.running || match.paused || match.decided || !m_started ||
                        (mapping.frame.relative && match.you.out);
    if (silent) {
        forget();
        return tracked;
    }
    /* The sides of the items tracked this tick alone: one no longer
     * tracked takes its side afresh when it is again. */
    std::unordered_map<int, int> sides;
    /* The loose items in the voices' order: the one within reach (the
     * game's single answer, so at most one) ahead of the nearest; the list
     * itself stays nearest first, as the appearances read it. */
    std::array<int, A11Y_MATCH_ITEMS> order{};
    int count = 0;
    for (const bool reach : {true, false}) {
        for (int i = 0; i < match.item_count; i++) {
            const A11yMatchItem& item = match.items[i];
            if (item.loose && item.within_reach == reach) {
                order[count++] = i;
            }
        }
    }
    std::size_t voice = 0;
    for (int at = 0; at < count && voice < tracked.size(); at++) {
        const A11yMatchItem& item = match.items[order[at]];
        const auto kept = m_sides.find(item.serial);
        const int side = side_of(item.x - match.you.x, kept == m_sides.end() ? 0 : kept->second,
            mapping.frame.from_you.crossing_margin);
        std::optional<VoiceTargets> targets =
            item_targets(match, item.x, item.y, side, mapping.frame);
        if (!targets.has_value()) {
            continue;
        }
        targets->interval = item.within_reach ? mapping.reach_interval : mapping.steady_interval;
        tracked[voice++] = TrackedItem{item.serial, item.within_reach, *targets};
        sides[item.serial] = side;
    }
    m_sides = std::move(sides);
    return tracked;
}

void ItemPosition::scene_entered() {
    forget();
    m_started = false;
}

double item_position_preview_change(const ItemPositionMapping& mapping) {
    return (kItemPreviewSteadyPulses - 1 + kPreviewChangeShare) *
           static_cast<double>(mapping.steady_interval);
}

std::optional<VoiceTargets> item_position_preview(
    double seconds, const ItemPositionMapping& mapping) {
    const double change = item_position_preview_change(mapping);
    if (seconds >= change + pulses_seconds(mapping.reach_interval, kItemPreviewReachPulses)) {
        return std::nullopt;
    }
    /* You are never out in the preview, so there is always a place. */
    VoiceTargets targets = item_targets(
        item_preview_match(), kPreviewSide, 0.0f, item_side(kPreviewSide), mapping.frame)
                               .value_or(VoiceTargets{});
    targets.interval = seconds < change ? mapping.steady_interval : mapping.reach_interval;
    return targets;
}

}  // namespace a11y
