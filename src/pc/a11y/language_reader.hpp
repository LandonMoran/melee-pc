/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Language (mnlanguage.c): two choices, Japanese and English, drawn as
 * pictures, the cursor on the language in use as the screen opens. Left and
 * Right move between them; A on the other language saves it and reloads the
 * menu scene on Options, in the new language. The opening says the choice,
 * the keys and the game's bottom line; a move says the choice, and on
 * Japanese, while the game is in English, what choosing it does. Once the
 * menu has reloaded, the change is said before Options' opening
 * (.scratch/options-screens/spec.md, "Speech: Language"). */
#pragma once
#include "leaf_reader.hpp"
#include <optional>
#include <string>
#include <string_view>

namespace a11y {

class LanguageReader final : public LeafReader {
public:
    /* text must outlive this. */
    explicit LanguageReader(MenuText& text) : m_text(text) {}

    std::string opening(const A11yMenuState& state) override;
    std::string frame(const A11yMenuState& state) override;

    /* Read on every frame of the menu reader, whatever the screen: "Language
     * changed to Japanese." on the first frame after the saved language
     * changed, said before what that frame says. Nothing on the first frame
     * ever seen, and nothing otherwise. Kept across scenes, since the change
     * reloads the menu scene. */
    std::string change(const A11yMenuState& state);

private:
    /* The choice in view: "English", or "Japanese" with the warning while the
     * game is in English. */
    std::string choice(const A11yMenuState& state) const;

    MenuText& m_text;
    bool m_cursor_japanese = false;
    std::optional<bool> m_japanese;
};

/* A language as the fork says it: "Japanese" for 日本語, or "English". */
std::string_view language_word(bool japanese);

}  // namespace a11y
