/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "item_takes.hpp"
#include <algorithm>

namespace a11y {
namespace {

bool common(A11yItem kind) {
    return static_cast<int>(kind) >= 0 && static_cast<int>(kind) < A11Y_ITEM_COMMON_KINDS;
}

bool same(const A11yMatchHeld& a, const A11yMatchHeld& b) {
    return a.serial == b.serial && a.kind == b.kind;
}

}  // namespace

std::vector<A11yItem> ItemTakes::tick(const A11yMatchHudPlayer& seen) {
    std::array<A11yMatchHeld, A11Y_MATCH_HANDS> hands{};
    std::copy(std::begin(seen.hands), std::end(seen.hands), hands.begin());
    std::vector<A11yItem> takes;
    if (m_known) {
        /* The tally's rise each hand take accounts for. */
        std::array<int, A11Y_ITEM_COMMON_KINDS> by_hand{};
        for (int i = 0; i < A11Y_MATCH_HANDS; i++) {
            const A11yMatchHeld& held = hands[i];
            const A11yItem kind = held.kind;
            const auto is_held = [&held](const A11yMatchHeld& other) { return same(other, held); };
            /* -1 is an empty hand or something no item of ours; an item in
             * two hands at once (never seen) counts once. */
            if (held.serial < 0 || !common(kind) ||
                std::any_of(hands.begin(), hands.begin() + i, is_held) ||
                std::any_of(m_hands.begin(), m_hands.end(), is_held))
            {
                continue;
            }
            takes.push_back(kind);
            by_hand[kind]++;
        }
        for (int kind = 0; kind < A11Y_ITEM_COMMON_KINDS; kind++) {
            for (int more = seen.taken[kind] - m_taken[kind] - by_hand[kind]; more > 0; more--) {
                takes.push_back(static_cast<A11yItem>(kind));
            }
        }
    }
    m_known = true;
    m_hands = hands;
    std::copy(std::begin(seen.taken), std::end(seen.taken), m_taken.begin());
    return takes;
}

}  // namespace a11y
