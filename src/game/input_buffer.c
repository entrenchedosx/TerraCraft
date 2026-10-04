#include "game/input_buffer.h"

#include <stddef.h>
#include <string.h>

void input_buffer_init(InputBuffer *buffer)
{
    if (buffer != NULL) {
        memset(buffer, 0, sizeof(*buffer));
    }
}

void input_buffer_key_event(InputBuffer *buffer, int scancode, bool down, bool repeat)
{
    if (buffer == NULL || scancode < 0 || scancode >= INPUT_BUFFER_KEY_CAPACITY) {
        return;
    }
    if (down) {
        if (!repeat && !buffer->key_held[scancode]) {
            buffer->key_pressed[scancode] = true;
        }
        buffer->key_held[scancode] = true;
    } else {
        buffer->key_held[scancode] = false;
    }
}

void input_buffer_mouse_event(InputBuffer *buffer, int button, bool down)
{
    if (buffer == NULL || button <= 0 || button >= INPUT_BUFFER_MOUSE_CAPACITY) {
        return;
    }
    if (down) {
        if (!buffer->mouse_held[button]) {
            buffer->mouse_pressed[button] = true;
        }
        buffer->mouse_held[button] = true;
    } else {
        buffer->mouse_held[button] = false;
    }
}

bool input_buffer_key_down(const InputBuffer *buffer, int scancode)
{
    return buffer != NULL && scancode >= 0 && scancode < INPUT_BUFFER_KEY_CAPACITY &&
           buffer->key_held[scancode];
}

bool input_buffer_mouse_down(const InputBuffer *buffer, int button)
{
    return buffer != NULL && button > 0 && button < INPUT_BUFFER_MOUSE_CAPACITY && buffer->mouse_held[button];
}

bool input_buffer_take_key_pressed(InputBuffer *buffer, int scancode)
{
    if (buffer == NULL || scancode < 0 || scancode >= INPUT_BUFFER_KEY_CAPACITY) {
        return false;
    }
    bool pressed = buffer->key_pressed[scancode];
    buffer->key_pressed[scancode] = false;
    return pressed;
}

bool input_buffer_take_mouse_pressed(InputBuffer *buffer, int button)
{
    if (buffer == NULL || button <= 0 || button >= INPUT_BUFFER_MOUSE_CAPACITY) {
        return false;
    }
    bool pressed = buffer->mouse_pressed[button];
    buffer->mouse_pressed[button] = false;
    return pressed;
}

void input_buffer_clear_edges(InputBuffer *buffer)
{
    if (buffer == NULL) {
        return;
    }
    memset(buffer->key_pressed, 0, sizeof(buffer->key_pressed));
    memset(buffer->mouse_pressed, 0, sizeof(buffer->mouse_pressed));
}

void input_buffer_focus_lost(InputBuffer *buffer)
{
    input_buffer_init(buffer);
}
