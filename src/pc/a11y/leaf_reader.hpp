/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The leaf screens of the main menu tree that keep their state to themselves,
 * where the table-driven Reader (menu_names.hpp) cannot follow them: Language,
 * and Erase Data and Rumble after it (.scratch/options-screens/spec.md). Each
 * has a reader of its own, owned by MenuSpeech, which says the screen's name
 * as it opens and hands each frame on that screen to the reader. The screen's
 * state comes in the menu snapshot (A11yMenuState), read by game_access.c or
 * handed over by a hook. */
#pragma once
#include "game_access.h"
#include "game_text.hpp"
#include "speech.hpp"
#include <cstdint>
#include <string>

namespace a11y {

/* The menu scene's game text as spoken lines, shared by MenuSpeech and the
 * leaf readers, with their one LogOnce. The scene's string table is SdMenu,
 * English or Japanese by the saved language; the English table's own font
 * glyphs (menu_font_glyphs) are given only while the game is in English, as
 * the Japanese table's are kanji the fork has no names for. */
class MenuText {
public:
    explicit MenuText(GameTextSource source) : m_english(source), m_japanese(source) {
        m_english.font_glyphs = menu_font_glyphs();
        m_japanese.font_glyphs = {};
    }

    /* text as one spoken line, a decoding problem logged once as
     * "<where>: description <trouble>"; nullptr gives no words, and logs
     * "<where>: no description". */
    std::string description(
        const std::uint8_t* text, const A11yMenuState& state, const std::string& where) {
        if (text == nullptr) {
            m_log_once(where + ": no description");
            return "";
        }
        const GameTextSource& source = state.language_japanese ? m_japanese : m_english;
        return spoken_game_text(text, source, m_log_once, where + ": description");
    }

    void log_once(const std::string& line) { m_log_once(line); }

private:
    GameTextSource m_english;
    GameTextSource m_japanese;
    LogOnce m_log_once;
};

/* A leaf screen's own reader. It compares each frame with the one before,
 * keeping what it needs, and touches no game state. */
class LeafReader {
public:
    virtual ~LeafReader() = default;

    /* The screen has opened, or the reader was told to forget: what follows
     * the screen's name, and the screen as seen now. */
    virtual std::string opening(const A11yMenuState& state) = 0;

    /* A later frame on the screen: what changed since the last, or nothing. */
    virtual std::string frame(const A11yMenuState& state) = 0;
};

}  // namespace a11y
