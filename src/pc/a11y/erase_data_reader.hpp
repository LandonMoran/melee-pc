/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Erase Data (mndatadel.c): six rows, Up and Down wrapping, each with its
 * description. A on a row opens "Erase?" on No; Left and Right switch, A on
 * Yes erases, A on No or B closes. Erase All's Yes opens "Are you sure?",
 * again on No. An erased row is marked for the rest of the visit, drawn only
 * as a dim bar while hovered, and A on it does nothing. The opening says the
 * row, the keys and the description; a move says the row, "erased" where it
 * is, and the description; a prompt opening says its text and the choice,
 * with the keys the first time in a visit; a move in a prompt says the
 * choice. A prompt closing says nothing: the game's sounds tell it
 * (.scratch/options-screens/spec.md, "Speech: Erase Data"). */
#pragma once
#include "leaf_reader.hpp"
#include <string>

namespace a11y {

class EraseDataReader final : public LeafReader {
public:
    /* text must outlive this. */
    explicit EraseDataReader(MenuText& text) : m_text(text) {}

    std::string opening(const A11yMenuState& state) override;
    std::string frame(const A11yMenuState& state) override;

private:
    /* The hovered row with its mark and description. */
    std::string row(const A11yMenuState& state);
    /* The prompt up, its text and choice, with the keys the first time. */
    std::string prompt(const A11yMenuState& state);
    void remember(const A11yMenuState& state);

    MenuText& m_text;
    int m_row = 0;
    int m_prompt = 0;
    bool m_yes = false;
    bool m_prompt_keys_said = false;
};

}  // namespace a11y
