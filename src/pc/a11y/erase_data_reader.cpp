/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "erase_data_reader.hpp"
#include "sentences.hpp"
#include <array>
#include <string_view>

namespace a11y {
namespace {

/* The rows as drawn, top to bottom (A11yEraseData.row). */
constexpr std::array<std::string_view, A11Y_ERASE_ROWS> kRows = {"Hidden Characters",
    "Hidden Stages", "1-P High Score", "VS. Mode Scores", "Trophy Collection", "Erase All"};
constexpr std::string_view kKeys = "Up and down to move, A to erase.";
constexpr std::string_view kPromptKeys = "Left and right to choose, A to confirm.";

std::string where(const A11yMenuState& state) {
    return "[a11y] menu " + std::to_string(state.menu) + " row " + std::to_string(state.erase.row);
}

bool known_row(int row) {
    return row >= 0 && row < A11Y_ERASE_ROWS;
}

}  // namespace

std::string EraseDataReader::opening(const A11yMenuState& state) {
    m_prompt_keys_said = false;
    std::string out = row(state);
    add_sentence(out, kKeys);
    add_sentence(out, m_text.description(state.erase.description, state, where(state)));
    if (state.erase.prompt != 0) {
        add_sentence(out, prompt(state));
    }
    remember(state);
    return out;
}

std::string EraseDataReader::frame(const A11yMenuState& state) {
    const A11yEraseData& erase = state.erase;
    std::string out;
    if (erase.prompt != m_prompt) {
        /* A prompt opening, or the second after the first; a prompt closing,
         * by an erase, No or B, says nothing. */
        if (erase.prompt != 0) {
            out = prompt(state);
        }
    } else if (erase.prompt != 0) {
        if (erase.yes != m_yes) {
            out = erase.yes ? "Yes" : "No";
        }
    } else if (erase.row != m_row) {
        out = row(state);
        add_sentence(out, m_text.description(erase.description, state, where(state)));
    }
    remember(state);
    return out;
}

std::string EraseDataReader::row(const A11yMenuState& state) {
    const A11yEraseData& erase = state.erase;
    if (!known_row(erase.row)) {
        m_text.log_once(where(state) + ": not in the menu names table");
        return "Unknown row " + std::to_string(erase.row);
    }
    std::string out(kRows[static_cast<std::size_t>(erase.row)]);
    if (erase.erased[erase.row]) {
        out += ": erased";
    }
    return out;
}

std::string EraseDataReader::prompt(const A11yMenuState& state) {
    const A11yEraseData& erase = state.erase;
    std::string out = m_text.description(erase.prompt_text, state,
        "[a11y] menu " + std::to_string(state.menu) + " prompt " +
            std::to_string(erase.prompt_number));
    add_sentence(out, erase.yes ? "Yes" : "No");
    if (!m_prompt_keys_said) {
        m_prompt_keys_said = true;
        add_sentence(out, kPromptKeys);
    }
    return out;
}

void EraseDataReader::remember(const A11yMenuState& state) {
    m_row = state.erase.row;
    m_prompt = state.erase.prompt;
    m_yes = state.erase.yes;
}

}  // namespace a11y
