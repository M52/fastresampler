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
