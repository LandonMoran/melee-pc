/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "language_reader.hpp"
#include "sentences.hpp"

namespace a11y {
namespace {

constexpr std::string_view kHint = "Left and right to choose.";
/* After a switch to Japanese the game's descriptions, prompts and notices
 * are Japanese and the fork's own words stay English. */
constexpr std::string_view kJapaneseWarning = "The game's own text will be in Japanese.";

}  // namespace

std::string_view language_word(bool japanese) {
    return japanese ? "Japanese" : "English";
}

std::string LanguageReader::opening(const A11yMenuState& state) {
    m_cursor_japanese = state.language_cursor_japanese;
    std::string out = choice(state);
    add_sentence(out, kHint);
    /* SdMenu's 0xBF, the screen's only centre text. */
    add_sentence(out, m_text.description(state.center_text_string, state,
                          "[a11y] menu " + std::to_string(state.menu) + " centre text " +
                              std::to_string(state.center_text)));
    return out;
}

std::string LanguageReader::frame(const A11yMenuState& state) {
    if (state.language_cursor_japanese == m_cursor_japanese) {
        return "";
    }
    m_cursor_japanese = state.language_cursor_japanese;
    return choice(state);
}

std::string LanguageReader::change(const A11yMenuState& state) {
    bool changed = m_japanese && *m_japanese != state.language_japanese;
    m_japanese = state.language_japanese;
    if (!changed) {
        return "";
    }
    return "Language changed to " + std::string(language_word(state.language_japanese)) + ".";
}

std::string LanguageReader::choice(const A11yMenuState& state) const {
    std::string out(language_word(state.language_cursor_japanese));
    if (state.language_cursor_japanese && !state.language_japanese) {
        add_sentence(out, kJapaneseWarning);
    }
    return out;
}

}  // namespace a11y
