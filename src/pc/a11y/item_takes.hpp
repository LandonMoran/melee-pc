/* SPDX-License-Identifier: GPL-3.0-or-later */
/* A player's takes of items (.scratch/items/spec.md, "Pickup
 * announcements"; primer docs/a11y/gameplay-state.md, "How the fork sees a
 * take"): which items a player took since last tick, read from the HUD
 * snapshot's hands and the game's tally of items taken. Neither alone sees
 * every take: the tally counts a player's first hold of an item only, so it
 * misses a re-pickup after a throw, and the hands miss a touch (a Starman, a
 * Mushroom) and an item used up on the tick it is taken (a healing item, a
 * Metal Box, a Cloaking Device caught in the air). Together they see each
 * take once. Touches no game state. Game thread only. */
#pragma once
#include "game_access.h"
#include <array>
#include <vector>

namespace a11y {

/* One player's takes, followed tick by tick. */
class ItemTakes {
public:
    /* The player as the snapshot shows them now, `present`: the kinds of the
     * items taken since the last call, hand takes first in the hands' order,
     * then the tally's in the kinds' order. The first call after forget()
     * takes what it sees as the baseline and returns nothing.
     *
     * A hand take is an item, by serial and kind, in this tick's hands,
     * taken as a set, that was not in last tick's: a pickup, a catch, the
     * same item picked up again. An item moving between hands (the Bunny
     * Hood put on) or kept through Zelda's and Sheik's change is none. The
     * kind is in the key for an online rollback, which can shift the
     * serials of items made inside its window: a serial held before the
     * rollback that names an item of another kind after it is a take. The
     * same item held on a shifted serial is one too, and so said again:
     * neither the hands nor the tally tell it from a re-pickup.
     *
     * A tally take is a kind's tally risen by more than that kind's hand
     * takes this tick, each unit more one take: a touch, an item used up
     * within the tick it was taken. A falling tally (an online rollback)
     * counts nothing, and the next rise counts from where it fell to. */
    std::vector<A11yItem> tick(const A11yMatchHudPlayer& seen);
    /* The next call starts afresh: at a match's start, and while the slot
     * is not present (its hands are not filled then). */
    void forget() { m_known = false; }

private:
    bool m_known = false;
    std::array<A11yMatchHeld, A11Y_MATCH_HANDS> m_hands{};
    std::array<int, A11Y_ITEM_COMMON_KINDS> m_taken{};
};

}  // namespace a11y
