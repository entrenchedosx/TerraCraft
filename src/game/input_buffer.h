#pragma once

/* Small event-to-tick input latch. A press survives a release until the
 * simulation consumes it, while held state remains independent.
 */

#include <stdbool.h>

#define INPUT_BUFFER_KEY_CAPACITY 512
#define INPUT_BUFFER_MOUSE_CAPACITY 8

typedef struct InputBuffer {
    bool key_held[INPUT_BUFFER_KEY_CAPACITY];
    bool key_pressed[INPUT_BUFFER_KEY_CAPACITY];
    bool mouse_held[INPUT_BUFFER_MOUSE_CAPACITY];
    bool mouse_pressed[INPUT_BUFFER_MOUSE_CAPACITY];
} InputBuffer;

void input_buffer_init(InputBuffer *buffer);
void input_buffer_key_event(InputBuffer *buffer, int scancode, bool down, bool repeat);
void input_buffer_mouse_event(InputBuffer *buffer, int button, bool down);
bool input_buffer_key_down(const InputBuffer *buffer, int scancode);
bool input_buffer_mouse_down(const InputBuffer *buffer, int button);
bool input_buffer_take_key_pressed(InputBuffer *buffer, int scancode);
bool input_buffer_take_mouse_pressed(InputBuffer *buffer, int button);
void input_buffer_clear_edges(InputBuffer *buffer);
void input_buffer_focus_lost(InputBuffer *buffer);
