#pragma once
// A slider after FastSampler's drag slider. A plain drag moves the value in
// steps of the grid; a drag with Shift held moves it coarsely and one with
// Alt held finely, so many units per pixel of mouse travel. Ctrl-click
// types a value, as on every ImGui slider.

struct DragSliderState {
    float start_value;
    float start_mouse_x;
    bool  active;        // a Shift or Alt drag is under way
    bool  last_shift;
    bool  last_alt;
};

// The value is rounded to step_default on a plain drag; a Shift drag moves
// step_shift per pixel, an Alt drag step_alt per pixel. format is the
// slider's text; without a '%' it is shown as it is, so the caller can
// print the value in any unit. Returns true when the value changed.
bool ui_slider_float_drag(const char* label_id, float* val, float min_val, float max_val, const char* format,
                          float step_default, float step_shift, float step_alt, DragSliderState* state);
