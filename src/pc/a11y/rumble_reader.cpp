/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "rumble_reader.hpp"
#include "game_text.hpp"
#include "sentences.hpp"
#include <algorithm>
#include <cstddef>
#include <string_view>

namespace a11y {
namespace {

constexpr std::string_view kKeys = "A to turn on or off.";
constexpr std::string_view kKeysWithNames = "A to turn on or off, right for the names list.";
/* Said before the hovered name as a controller enters the names list. */
constexpr std::string_view kNames = "Names";

/* The panel's label as the screen draws it: "P1" to "P4". */
std::string label(std::size_t port) {
    return "P" + std::to_string(port + 1);
}

std::string on_off(bool on) {
    return on ? "On" : "Off";
}

/* A plugged-in controller's panel: "P1: On". */
std::string panel(std::size_t port, const A11yRumblePort& state) {
    return label(port) + ": " + on_off(state.rumble);
}

}  // namespace

std::string RumbleReader::opening(const A11yMenuState& state) {
    std::string out;
    bool in_names = false;
    for (std::size_t port = 0; port < A11Y_RUMBLE_PORTS; port++) {
        const A11yRumblePort& now = state.rumble.ports[port];
        if (now.connected) {
            add_sentence(out, panel(port, now));
            in_names = in_names || now.in_names;
        }
    }
    /* Only after forget(): the screen opens with every controller on its
     * own panel. */
    if (in_names) {
        add_sentence(out, entering(state));
    }
    add_sentence(out, state.rumble.name_count > 0 ? kKeysWithNames : kKeys);
    add_sentence(out, m_text.description(state.rumble.bottom_line, state,
                          "[a11y] menu " + std::to_string(state.menu) + " bottom line"));
    remember(state);
    return out;
}

std::string RumbleReader::frame(const A11yMenuState& state) {
    std::string out;
    bool named = false;
    for (std::size_t port = 0; port < A11Y_RUMBLE_PORTS; port++) {
        const A11yRumblePort& now = state.rumble.ports[port];
        const A11yRumblePort& before = m_ports[port];
        if (now.connected != before.connected) {
            add_sentence(out, now.connected ? panel(port, now) : label(port) + " disconnected");
        } else if (now.connected && now.in_names != before.in_names) {
            /* Entering says the hovered name, once a frame however many
             * enter; leaving says the panel the controller is back on. */
            if (!now.in_names) {
                add_sentence(out, panel(port, now));
            } else if (!named) {
                add_sentence(out, entering(state));
                named = true;
            }
        } else if (now.connected && now.rumble != before.rumble) {
            add_sentence(out, panel(port, now));
        }
    }
    /* The names list's one cursor: a move or a scroll says the name it
     * reaches, a switch the name's new setting. At either end the game
     * moves nothing, and nothing is said. */
    const A11yRumble& now = state.rumble;
    if (!named && now.name_slot >= 0) {
        if (now.name_slot != m_name_slot) {
            add_sentence(out, name(state));
        } else if (now.name_rumble != m_name_rumble) {
            add_sentence(out, on_off(now.name_rumble));
        }
    }
    remember(state);
    return out;
}

std::string RumbleReader::entering(const A11yMenuState& state) {
    std::string out(kNames);
    if (state.rumble.name_slot >= 0) {
        add_sentence(out, name(state));
    }
    return out;
}

std::string RumbleReader::name(const A11yMenuState& state) {
    const A11yRumble& rumble = state.rumble;
    std::string slot = std::to_string(rumble.name_slot + 1);
    std::string where = "[a11y] menu " + std::to_string(state.menu) + " name " + slot;
    DecodedText decoded = decode_shift_jis(rumble.name);
    if (std::string trouble = decode_trouble(decoded); !trouble.empty()) {
        m_text.log_once(where + ": " + trouble);
    }
    std::string words = spoken_text(decoded.text);
    if (std::all_of(words.begin(), words.end(), [](char c) { return c == ' '; })) {
        m_text.log_once(where + ": no words, said as its slot");
        words = "Name " + slot;
    }
    return words + ": " + on_off(rumble.name_rumble);
}

void RumbleReader::remember(const A11yMenuState& state) {
    for (std::size_t port = 0; port < A11Y_RUMBLE_PORTS; port++) {
        m_ports[port] = state.rumble.ports[port];
    }
    m_name_slot = state.rumble.name_slot;
    m_name_rumble = state.rumble.name_rumble;
}

}  // namespace a11y
