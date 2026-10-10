#pragma once

struct DragSliderState {
    float start_value;
    float start_mouse_x;
    bool  active;
    bool  last_shift;
    bool  last_alt;
};

bool ui_slider_float_drag(const char* label_id, float* val, float min_val, float max_val, const char* format,
                          float step_default, float step_shift, float step_alt, DragSliderState* state);
