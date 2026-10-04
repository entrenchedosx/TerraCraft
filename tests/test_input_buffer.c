#include "test_main.h"

#include "game/input_buffer.h"

int test_input_buffer_press_release_pulse(void)
{
    int failures = 0;
    InputBuffer buffer;
    input_buffer_init(&buffer);

    input_buffer_key_event(&buffer, 44, true, false); /* Space down. */
    input_buffer_key_event(&buffer, 44, false, false); /* Space up before the next tick. */
    TEST_ASSERT(!input_buffer_key_down(&buffer, 44));
    TEST_ASSERT(input_buffer_take_key_pressed(&buffer, 44));
    TEST_ASSERT(!input_buffer_take_key_pressed(&buffer, 44));

    input_buffer_mouse_event(&buffer, 1, true);
    input_buffer_mouse_event(&buffer, 1, false);
    TEST_ASSERT(!input_buffer_mouse_down(&buffer, 1));
    TEST_ASSERT(input_buffer_take_mouse_pressed(&buffer, 1));
    TEST_ASSERT(!input_buffer_take_mouse_pressed(&buffer, 1));
    return failures;
}

int test_input_buffer_held_and_repeat(void)
{
    int failures = 0;
    InputBuffer buffer;
    input_buffer_init(&buffer);

    input_buffer_key_event(&buffer, 44, true, false); /* Space down. */
    TEST_ASSERT(input_buffer_key_down(&buffer, 44));
    TEST_ASSERT(input_buffer_take_key_pressed(&buffer, 44));
    input_buffer_key_event(&buffer, 44, true, true); /* SDL key repeat is held, not another press. */
    TEST_ASSERT(input_buffer_key_down(&buffer, 44));
    TEST_ASSERT(!input_buffer_take_key_pressed(&buffer, 44));

    input_buffer_mouse_event(&buffer, 3, true);
    TEST_ASSERT(input_buffer_mouse_down(&buffer, 3));
    TEST_ASSERT(input_buffer_take_mouse_pressed(&buffer, 3));
    input_buffer_mouse_event(&buffer, 3, false);
    TEST_ASSERT(!input_buffer_mouse_down(&buffer, 3));
    return failures;
}

int test_input_buffer_clear_and_focus_loss(void)
{
    int failures = 0;
    InputBuffer buffer;
    input_buffer_init(&buffer);
    input_buffer_key_event(&buffer, 6, true, false);
    input_buffer_mouse_event(&buffer, 1, true);
    input_buffer_clear_edges(&buffer);
    TEST_ASSERT(input_buffer_key_down(&buffer, 6));
    TEST_ASSERT(input_buffer_mouse_down(&buffer, 1));
    TEST_ASSERT(!input_buffer_take_key_pressed(&buffer, 6));
    TEST_ASSERT(!input_buffer_take_mouse_pressed(&buffer, 1));

    input_buffer_focus_lost(&buffer);
    TEST_ASSERT(!input_buffer_key_down(&buffer, 6));
    TEST_ASSERT(!input_buffer_mouse_down(&buffer, 1));
    TEST_ASSERT(!input_buffer_take_key_pressed(&buffer, 6));
    TEST_ASSERT(!input_buffer_take_mouse_pressed(&buffer, 1));
    return failures;
}
