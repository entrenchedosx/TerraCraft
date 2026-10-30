#include "test_main.h"
#include "ui/ui.h"

static UiControl control_frame(UiControls *state, const UiFrame *frame, int screen, bool enabled)
{
    ui_controls_begin(state, frame, screen, true);
    UiControl control = ui_control(state, frame, 10, 20, 100, 30, enabled);
    ui_controls_end(state);
    return control;
}

static void three_controls_frame(UiControls *state, const UiFrame *frame, int screen,
                                 bool arrows, const bool enabled[3], UiControl controls[3])
{
    ui_controls_begin(state, frame, screen, arrows);
    for (int i = 0; i < 3; ++i) {
        controls[i] = ui_control(state, frame, (float)(10 + i * 120), 20, 100, 30, enabled[i]);
    }
    ui_controls_end(state);
}

int test_ui_control_capture(void)
{
    int failures = 0;
    UiControls state = {0};
    UiFrame frame;
    ui_frame_clear(&frame);
    frame.mouse_x = 20;
    frame.mouse_y = 30;

    /* Press captures the button; activation waits for release inside it. */
    UiControl control = control_frame(&state, &frame, 1, true);
    TEST_ASSERT(control.hovered && !control.activated && !control.pressed);
    frame.mouse_clicked = true;
    frame.mouse_down = true;
    control = control_frame(&state, &frame, 1, true);
    TEST_ASSERT(control.pressed && !control.activated && state.active == 0);
    TEST_ASSERT(control.focused && state.focus == 0);
    frame.mouse_clicked = false;
    frame.mouse_down = false;
    control = control_frame(&state, &frame, 1, true);
    TEST_ASSERT(control.activated && !control.pressed && state.active == -1);
    control = control_frame(&state, &frame, 1, true);
    TEST_ASSERT(!control.activated);

    /* Releasing outside cancels, and subsequently moving inside cannot fire. */
    frame.mouse_clicked = true;
    frame.mouse_down = true;
    control = control_frame(&state, &frame, 1, true);
    TEST_ASSERT(control.pressed);
    frame.mouse_clicked = false;
    frame.mouse_down = false;
    frame.mouse_x = 200;
    control = control_frame(&state, &frame, 1, true);
    TEST_ASSERT(!control.activated && state.active == -1);
    frame.mouse_x = 20;
    control = control_frame(&state, &frame, 1, true);
    TEST_ASSERT(!control.activated);

    /* A press originating elsewhere never captures a button on drag-in. */
    frame.mouse_x = 200;
    frame.mouse_clicked = true;
    frame.mouse_down = true;
    control = control_frame(&state, &frame, 1, true);
    TEST_ASSERT(!control.pressed && state.active == -1);
    frame.mouse_x = 20;
    frame.mouse_clicked = false;
    control = control_frame(&state, &frame, 1, true);
    TEST_ASSERT(control.hovered && !control.pressed && !control.activated);
    frame.mouse_down = false;
    control = control_frame(&state, &frame, 1, true);
    TEST_ASSERT(!control.activated);

    /* A complete press/release pulse between render frames remains usable. */
    frame.mouse_clicked = true;
    control = control_frame(&state, &frame, 1, true);
    TEST_ASSERT(control.activated && !control.pressed && state.active == -1);
    frame.mouse_clicked = false;
    control = control_frame(&state, &frame, 1, true);
    TEST_ASSERT(!control.activated);

    /* Disabled controls neither capture a gesture nor activate on its release. */
    frame.mouse_clicked = true;
    frame.mouse_down = true;
    control = control_frame(&state, &frame, 1, false);
    TEST_ASSERT(!control.hovered && !control.pressed && !control.activated);
    TEST_ASSERT(state.active == -1);
    frame.mouse_clicked = false;
    frame.mouse_down = false;
    control = control_frame(&state, &frame, 1, false);
    TEST_ASSERT(!control.activated);
    /* Disabling a captured button at release also prevents activation. */
    frame.mouse_clicked = true;
    frame.mouse_down = true;
    (void)control_frame(&state, &frame, 1, true);
    frame.mouse_clicked = false;
    frame.mouse_down = false;
    control = control_frame(&state, &frame, 1, false);
    TEST_ASSERT(!control.activated && state.active == -1);

    /* Disabling during a held gesture cancels capture permanently, even
     * if the control becomes enabled again before the mouse is released. */
    frame.mouse_clicked = true;
    frame.mouse_down = true;
    (void)control_frame(&state, &frame, 1, true);
    frame.mouse_clicked = false;
    control = control_frame(&state, &frame, 1, false);
    TEST_ASSERT(!control.pressed && state.active == -1);
    control = control_frame(&state, &frame, 1, true);
    TEST_ASSERT(!control.pressed && !control.activated);
    frame.mouse_down = false;
    control = control_frame(&state, &frame, 1, true);
    TEST_ASSERT(!control.activated);

    /* A held press cannot transfer its capture into a different screen. */
    frame.mouse_clicked = true;
    frame.mouse_down = true;
    (void)control_frame(&state, &frame, 1, true);
    frame.mouse_clicked = false;
    control = control_frame(&state, &frame, 2, true);
    TEST_ASSERT(!control.activated && !control.pressed && state.active == -1);
    frame.mouse_down = false;
    control = control_frame(&state, &frame, 2, true);
    TEST_ASSERT(!control.activated);
    frame.mouse_clicked = true;
    control = control_frame(&state, &frame, 2, true);
    TEST_ASSERT(control.activated);

    /* Navigation uses the last registered enabled controls, skips disabled
     * buttons, wraps both ways and activates only the focused control. */
    UiControls navigation = {0};
    UiControl controls[3];
    const bool enabled[3] = {true, false, true};
    ui_frame_clear(&frame);
    frame.mouse_x = 1000;
    frame.mouse_y = 1000;
    three_controls_frame(&navigation, &frame, 3, true, enabled, controls);
    frame.key_tab = true;
    three_controls_frame(&navigation, &frame, 3, true, enabled, controls);
    TEST_ASSERT(controls[0].focused && !controls[1].focused && !controls[2].focused);
    three_controls_frame(&navigation, &frame, 3, true, enabled, controls);
    TEST_ASSERT(controls[2].focused && !controls[1].focused);
    three_controls_frame(&navigation, &frame, 3, true, enabled, controls);
    TEST_ASSERT(controls[0].focused);
    frame.key_shift = true;
    three_controls_frame(&navigation, &frame, 3, true, enabled, controls);
    TEST_ASSERT(controls[2].focused);
    frame.key_tab = false;
    frame.key_shift = false;
    frame.key_return = true;
    three_controls_frame(&navigation, &frame, 3, true, enabled, controls);
    TEST_ASSERT(controls[2].activated && !controls[0].activated && !controls[1].activated);
    frame.key_return = false;
    frame.key_down = true;
    three_controls_frame(&navigation, &frame, 3, true, enabled, controls);
    TEST_ASSERT(controls[0].focused && !controls[2].activated);
    frame.key_down = false;
    frame.key_up = true;
    three_controls_frame(&navigation, &frame, 3, true, enabled, controls);
    TEST_ASSERT(controls[2].focused);
    frame.key_up = false;
    frame.key_down = true;
    three_controls_frame(&navigation, &frame, 3, false, enabled, controls);
    TEST_ASSERT(controls[2].focused);
    /* Mouse focus is the starting point for the next Tab step. */
    ui_frame_clear(&frame);
    frame.mouse_x = 20;
    frame.mouse_y = 30;
    frame.mouse_clicked = true;
    three_controls_frame(&navigation, &frame, 3, true, enabled, controls);
    TEST_ASSERT(controls[0].focused && controls[0].activated);
    frame.mouse_clicked = false;
    frame.key_tab = true;
    three_controls_frame(&navigation, &frame, 3, true, enabled, controls);
    TEST_ASSERT(controls[2].focused && !controls[0].focused);
    return failures;
}
