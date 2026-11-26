/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "item_frame.hpp"

namespace a11y {

int item_side(float dx) {
    if (dx < 0.0f) {
        return -1;
    }
    return dx > 0.0f ? 1 : 0;
}

std::optional<VoiceTargets> item_targets(
    const A11yMatchState& match, float x, float y, int side, const ItemMapping& mapping) {
    VoiceTargets targets;
    targets.gain = 1.0f;
    if (!mapping.relative) {
        targets.pan = stage_pan(match.stage, x, mapping.stage.pan_to_camera);
        targets.pitch = stage_pitch(match.stage, y, mapping.stage.pitch_range);
        return targets;
    }
    if (match.you.out) {
        return std::nullopt;
    }
    targets.pan = relative_pan(x - match.you.x, side, mapping.from_you);
    targets.pitch = relative_pitch(y - match.you.y, mapping.stage.pitch_range, mapping.from_you);
    return targets;
}

A11yMatchState item_preview_match() {
    A11yMatchState match{};
    match.running = true;
    match.you.can_move = true;
    A11yStageFrame& stage = match.stage;
    stage.blast_left = -246.0f;
    stage.blast_right = 246.0f;
    stage.blast_top = 188.0f;
    stage.blast_bottom = -140.0f;
    stage.camera_left = -170.0f;
    stage.camera_right = 170.0f;
    stage.camera_top = 114.0f;
    stage.camera_bottom = -80.0f;
    return match;
}

}  // namespace a11y
