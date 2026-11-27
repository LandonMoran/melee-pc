/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The item cues' frame (.scratch/items/spec.md, "The item cues' frame";
 * primer docs/a11y/cues.md, "The item cues' frame"): where an item is, as a
 * voice's pan and pitch, for the appearance cue and the item's position cue
 * alike. Relative to you, as the opponent's position cue relative to you,
 * from the item cues' own copies of its distances; or on the stage, as your
 * position cue, in the pan frame the position cues share. Pure functions. */
#pragma once
#include "cue_engine.hpp"
#include "game_access.h"
#include "position_cue.hpp"
#include <optional>

namespace a11y {

/* The item cues' frame, as their settings hold it. */
struct ItemMapping {
    /* Items relative to you, a switch of their own: pan from the horizontal
     * distance and pitch from the vertical, the opponent's position cue's
     * way; off, where the item is on the stage, your position cue's way. */
    bool relative = true;
    /* The pan frame the position cues share (position_cues.pan_to_camera),
     * used on the stage alone, and the item cues' own pitch range, used in
     * both frames. */
    StageMapping stage;
    /* Relative to you. The crossing margin is the item's position cue's
     * alone: a one-shot has no side to keep. */
    RelativeMapping from_you;
};

/* The side of you a horizontal distance (item minus you) gives with no side
 * kept: -1 left, 1 right, and 0, the centre, for an item exactly level with
 * you, such as one Training's item menu makes at your feet. */
int item_side(float dx);

/* An item at (x, y) as a voice's pan and pitch at full gain, the interval
 * left to the caller. Relative to you, on `side` (item_side's, or one a
 * caller keeps with opponent_side and a crossing margin), with the pan
 * growing with the distance; none while you are out, since your place is
 * then where you were knocked out. On the stage, from the snapshot's frame
 * alone. Whether the cue sounds at all (a pause, before GO) is the
 * caller's. */
std::optional<VoiceTargets> item_targets(
    const A11yMatchState& match, float x, float y, int side, const ItemMapping& mapping);

/* The item cues' previews' match: Final Destination's frame as measured
 * (docs/a11y/gameplay-state.md, "Sizes measured"), with you at its centre,
 * past GO. */
A11yMatchState item_preview_match();

}  // namespace a11y
