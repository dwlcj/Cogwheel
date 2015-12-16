// Cogwheel mouses input.
// ---------------------------------------------------------------------------
// Copyright (C) 2015, Cogwheel. See AUTHORS.txt for authors
//
// This program is open source and distributed under the New BSD License. See
// LICENSE.txt for more detail.
// ---------------------------------------------------------------------------

#ifndef _COGWHEEL_INPUT_MOUSE_H_
#define _COGWHEEL_INPUT_MOUSE_H_

#include <Math/Vector.h>

namespace Cogwheel {
namespace Input {

struct Mouse final {
public:
    static const int BUTTON_COUNT = 4;
    static const int MAX_HALFTAP_COUNT = 127;

    // 8 bit struct containing state of a key; is it pressed or released and how many times was it pressed last frame.
    struct ButtonState {
        bool is_pressed : 1;
        unsigned int halftaps : 7;
    };

    Mouse(Math::Vector2i initial_position)
        : m_position(initial_position) { }

    inline void set_position(Math::Vector2i new_position) {
        m_delta += new_position - m_position;
        m_position = new_position;
    }

    inline Math::Vector2i get_position() const { return m_position; }
    inline Math::Vector2i get_delta() const { return m_delta; }

    inline void button_tapped(int buttonId, bool pressed) {
        ButtonState* buttons = &m_left_button;
        buttons[buttonId].is_pressed = pressed;
        unsigned int halftaps = buttons[buttonId].halftaps;
        buttons[buttonId].halftaps = (halftaps == MAX_HALFTAP_COUNT) ? (MAX_HALFTAP_COUNT - 1) : (halftaps + 1); // Checking for overflow! In case of overflow the tap count is reduced by one to maintain proper even/odd tap count relationship.
    }

    inline ButtonState get_left_button() { return m_left_button; }
    inline ButtonState get_right_button() { return m_right_button; }
    inline ButtonState get_middle_button() { return m_middle_button; }
    inline ButtonState get_button_4() { return m_button_4; }

    inline void add_scroll_delta(float scroll_delta) { m_scroll_delta += scroll_delta; }
    inline float get_scroll_delta() const { return m_scroll_delta; }

    inline void per_frame_reset() {
        m_delta = Math::Vector2i::zero();

        m_left_button.halftaps = 0u;
        m_right_button.halftaps = 0u;
        m_middle_button.halftaps = 0u;
        m_button_4.halftaps = 0u;

        m_scroll_delta = 0.0f;
    }

private:
    Math::Vector2i m_position;
    Math::Vector2i m_delta;

    ButtonState m_left_button;
    ButtonState m_right_button;
    ButtonState m_middle_button;
    ButtonState m_button_4;

    float m_scroll_delta;
};

} // NS Input
} // NS Cogwheel

#endif // _COGWHEEL_INPUT_MOUSE_H_