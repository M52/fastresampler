// #pragma once (removed for unity build)
// See fastresampler_ui_dragslider.h. The modifier keys come from ImGui's
// input state, so this file stays free of OS headers.

#include <math.h>
#include "imgui.h"
#include "imgui_internal.h"
#include "ui/fastresampler_ui_dragslider.h"

bool ui_slider_float_drag(const char* label_id, float* val, float min_val, float max_val, const char* format,
                          float step_default, float step_shift, float step_alt, DragSliderState* state) {
    ImGuiIO& io = ImGui::GetIO();
    bool shift_held = io.KeyShift;
    bool alt_held = io.KeyAlt;

    ImGuiID id = ImGui::GetID(label_id);
    bool is_typing = ImGui::TempInputIsActive(id);
    bool changed = false;

    // While a modifier drag runs, the slider itself is held still: the value
    // comes from the mouse travel below, not from where the grab sits.
    if (state->active) ImGui::PushItemFlag(ImGuiItemFlags_ReadOnly, true);
    if (ImGui::SliderFloat(label_id, val, min_val, max_val, format)) {
        if (!state->active) {
            if (!is_typing) *val = roundf(*val / step_default) * step_default;
            changed = true;
        }
    }
    if (state->active) ImGui::PopItemFlag();

    if (ImGui::IsItemActive()) {
        if ((shift_held || alt_held) && !state->active) {
            state->active = true;
            state->start_value = *val;
            state->start_mouse_x = io.MousePos.x;
            state->last_shift = shift_held;
            state->last_alt = alt_held;
        } else if (!shift_held && !alt_held && state->active) {
            state->active = false;
        }
    } else {
        state->active = false;
    }

    if (state->active) {
        // A change of modifier restarts the drag from where the value stands.
        if (shift_held != state->last_shift || alt_held != state->last_alt) {
            state->start_value = *val;
            state->start_mouse_x = io.MousePos.x;
            state->last_shift = shift_held;
            state->last_alt = alt_held;
        }
        float delta_x = io.MousePos.x - state->start_mouse_x;
        float step = shift_held ? step_shift : step_alt;
        float new_val = state->start_value + delta_x * step;
        new_val = roundf(new_val / step) * step;
        if (new_val < min_val) new_val = min_val;
        if (new_val > max_val) new_val = max_val;
        if (new_val != *val) changed = true;
        *val = new_val;
    }
    return changed;
}
