/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Items appearing (.scratch/items/spec.md, "The appearance announcement",
 * "The appearance cue"; primer docs/a11y/cues.md, "The appearance cue"):
 * which items are new on a tick, the appearance cue's one-shot for each,
 * and the announcement naming them on the screen reader. Fed once a tick
 * from the match snapshot (A11yMatchState.items) and touching no game
 * state. Game thread only. */
#pragma once
#include "cue_engine.hpp"
#include "game_access.h"
#include "item_frame.hpp"
#include "match_announcer.hpp"
#include "speech.hpp"
#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <unordered_set>
#include <vector>

namespace a11y {

/* The items that appear in a match, by serial and kind, from GO to GAME! or
 * TIME!. Items already there at GO are taken as seen, unsaid, and so is
 * anything listed before GO; from GAME! on none is new or marked seen, so a
 * GAME! a rollback takes back loses no item. A serial names one item for
 * the whole match on one timeline, so an item is new once: a list that drops
 * it and lists it again, or a rollback that makes it again on its serial,
 * says nothing more. Online a rollback can shift the serials of the items
 * made inside its window (every item made, projectiles included, takes the
 * next from one counter), so a serial seen may come back as another item:
 * one of another kind is new, one of the same kind is taken as seen. */
class NewItems {
public:
    /* Once a tick, paused ticks included (Training's item menu makes an
     * item while it holds the match): the items new on it, in the
     * snapshot's order, nearest to you first. Valid until the next call. */
    std::span<const A11yMatchItem> tick(const A11yMatchState& match);
    /* The items new on the last tick. */
    std::span<const A11yMatchItem> appeared() const { return m_new; }
    /* At each scene entry: serials count from 0 again in the next match,
     * which starts before its GO. */
    void forget();

private:
    bool m_started = false;                   /* GO has come */
    std::unordered_set<std::uint64_t> m_seen; /* serial and kind */
    std::vector<A11yMatchItem> m_new;
};

/* The appearance cue's driver: a one-shot for each new item, where it came
 * in (its place on the first tick it is listed). Silent while paused, since
 * a pause stops the match's voices: an item made then (Training's item menu)
 * is held and played on the first tick that is not paused, where it is by
 * then, unless it has left the list. Relative to you, an item that appears
 * while you are out is not played at all, there being no place to play it
 * from. */
class AppearanceCue {
public:
    /* Once a tick with the items new on it: the targets of each one-shot to
     * play now, held items first. */
    std::vector<VoiceTargets> tick(const A11yMatchState& match,
        std::span<const A11yMatchItem> appeared, const ItemMapping& mapping);
    /* The held items forgotten: at each scene entry. */
    void forget() { m_held.clear(); }

private:
    std::vector<int> m_held; /* serials */
};

/* One play of a one-shot's preview, `at` seconds after it begins. */
struct OneShotPlay {
    double at;
    VoiceTargets targets;
};

/* The pause between the appearance cue's preview plays. */
constexpr double kAppearancePreviewGap = 0.6;

/* The appearance cue's preview (cue_preview.hpp): three plays a pause
 * apart, an item appearing to your left, to your right and then just above
 * you, in the frame the mapping sets, with you at the centre of Final
 * Destination's main floor. Each sounds whole; the caller ends the preview
 * once the last has played out. */
std::array<OneShotPlay, 3> appearance_preview(const ItemMapping& mapping);

/* The announcement for the items new on a tick: their names in the
 * snapshot's order, a name said as often as it appears ("Food, Food, Maxim
 * Tomato"). A kind with no name is said as kUnknownItem and logged once. */
std::string appearance_text(std::span<const A11yMatchItem> appeared, LogOnce& log_once);

/* Says the items new on each tick on the screen reader, queued, so it cuts
 * off nothing being said: the time left, the Training menu's own speech
 * (the item menu makes an item while the menu is open). */
class AppearanceAnnouncement {
public:
    /* speech must outlive this. */
    explicit AppearanceAnnouncement(Speech& speech) : m_speech(speech) {}
    /* Once a tick, with the items new on it and the values in force: said
     * with Match announcements and Item appearances on, else nothing. */
    void tick(std::span<const A11yMatchItem> appeared, const MatchAnnouncementValues& values);

private:
    Speech& m_speech;
    LogOnce m_log_once;
};

}  // namespace a11y
