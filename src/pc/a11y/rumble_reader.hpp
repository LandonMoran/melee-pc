/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Rumble (mnvibration.c): a panel per controller, P1 to P4, each showing its
 * rumble setting, or a red cross while the controller is unplugged, and a
 * names list beside them. Every controller acts alone: A on its panel
 * switches its rumble, and while there are names Right takes it to the
 * names list and Left back; the list has one cursor, shared by every
 * controller in it, and A there switches the hovered name's rumble. The
 * opening says each plugged-in controller's panel, the keys and the line at
 * the screen's foot; a controller switching, plugged in or unplugged says
 * its panel, by the screen's label, "P2: Off" or "P2 disconnected";
 * entering the list says "Names" and the hovered name with its setting,
 * leaving it the panel; the cursor moving or scrolling says the name it
 * reaches, "Bob: Off", and a name switched its new setting
 * (.scratch/options-screens/spec.md, "Speech: Rumble"). */
#pragma once
#include "leaf_reader.hpp"
#include <array>
#include <string>

namespace a11y {

class RumbleReader final : public LeafReader {
public:
    /* text must outlive this. */
    explicit RumbleReader(MenuText& text) : m_text(text) {}

    std::string opening(const A11yMenuState& state) override;
    std::string frame(const A11yMenuState& state) override;

private:
    /* "Names" and the hovered name, as a controller enters the list. */
    std::string entering(const A11yMenuState& state);
    /* The hovered name with its setting, "Ann: On"; a name with no words
     * says its slot, "Name 3: On", logged once. */
    std::string name(const A11yMenuState& state);
    void remember(const A11yMenuState& state);

    MenuText& m_text;
    std::array<A11yRumblePort, A11Y_RUMBLE_PORTS> m_ports{};
    int m_name_slot = -1;
    bool m_name_rumble = false;
};

}  // namespace a11y
