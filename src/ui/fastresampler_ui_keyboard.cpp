#include <stdio.h>
#include "imgui.h"
#include "ui/fastresampler_ui.h"
#include "ui/fastresampler_palette.h"
#include "fastresampler_app.h"
#include "fastresampler_constants.h"

#define KEYBOARD_BLACK_WIDTH_RATIO  0.6f
#define KEYBOARD_BLACK_HEIGHT_RATIO 0.6f

static int keyboard_white_note(int w_idx) {
    static const int white_key_to_midi[PIANO_WHITE_KEY_COUNT] = {
        21, 23,
        24, 26, 28, 29, 31, 33, 35,
        36, 38, 40, 41, 43, 45, 47,
        48, 50, 52, 53, 55, 57, 59,
        60, 62, 64, 65, 67, 69, 71,
        72, 74, 76, 77, 79, 81, 83,
        84, 86, 88, 89, 91, 93, 95,
        96, 98, 100, 101, 103, 105, 107,
        108
    };
    if (w_idx < 0 || w_idx >= PIANO_WHITE_KEY_COUNT) return -1;
    return white_key_to_midi[w_idx];
}

static bool keyboard_has_black_above(int note) {
    int pc = note % SEMITONES_PER_OCTAVE;
    return pc == 0 || pc == 2 || pc == 5 || pc == 7 || pc == 9;
}

static float keyboard_velocity(float mouse_y, float key_top, float key_h) {
    float ratio = (mouse_y - key_top) / key_h;
    if (ratio < 0.0f) ratio = 0.0f;
    if (ratio > 1.0f) ratio = 1.0f;
    return KEYBOARD_MIN_VELOCITY + (1.0f - KEYBOARD_MIN_VELOCITY) * ratio;
}

static ImVec2 keyboard_black_pos(ImVec2 start, int w_idx, float key_w, float black_w) {
    return ImVec2(start.x + (float)(w_idx + 1) * key_w - black_w * 0.5f, start.y);
}

static bool keyboard_note_sampled(const App* app, int note) {
    if (app->active_tab != APP_TAB_RESAMPLE || !app->range_valid) return false;
    if (note < app->range_low || note > app->range_high) return false;
    return (note - app->range_low) % app->range_step == 0;
}

static ImU32 keyboard_key_color(const App* app, int note, bool is_black, bool hovered) {
    if (app->note_held[note]) return KEY_COLOR_PRESSED_U32;
    if (keyboard_note_sampled(app, note)) {
        if (is_black) return hovered ? CODE_COLOR_PURPLE_U32 : CODE_COLOR_PURPLE_DIM_U32;
        return hovered ? CODE_COLOR_PURPLE_LIGHT_U32 : CODE_COLOR_PURPLE_U32;
    }
    if (is_black) return hovered ? KEY_COLOR_BLACK_HOVERED_U32 : KEY_COLOR_BLACK_U32;
    return hovered ? KEY_COLOR_WHITE_HOVERED_U32 : KEY_COLOR_WHITE_U32;
}

static void keyboard_draw_rail(App* app, ImDrawList* dl, ImVec2 pos, float width, float height) {
    float text_y = pos.y + (height - ImGui::GetTextLineHeight()) * 0.5f;
    if (app->active_tab == APP_TAB_RESAMPLE && app->range_valid) {
        char low[8], high[8], text[128];
        midi_note_name(app->range_low, low, sizeof(low));
        midi_note_name(app->range_high, high, sizeof(high));
        int count = (app->range_high - app->range_low) / app->range_step + 1;
        snprintf(text, sizeof(text), "Resample range %s to %s, %d notes", low, high, count);
        dl->AddText(ImVec2(pos.x, text_y), CODE_COLOR_PURPLE_LIGHT_U32, text);
    }
    if (app->last_note >= 0) {
        char note[8], text[64];
        midi_note_name(app->last_note, note, sizeof(note));
        snprintf(text, sizeof(text), "%s   velocity %d", note, app->last_velocity);
        ImVec2 size = ImGui::CalcTextSize(text);
        ImU32 color = app->note_held[app->last_note] ? CODE_COLOR_ORANGE_U32 : ImGui::GetColorU32(ImGuiCol_Text);
        dl->AddText(ImVec2(pos.x + width - size.x, text_y), color, text);
    } else {
        const char* hint = "Click a key to play it: lower on the key is louder";
        ImVec2 size = ImGui::CalcTextSize(hint);
        dl->AddText(ImVec2(pos.x + width - size.x, text_y), ImGui::GetColorU32(ImGuiCol_TextDisabled), hint);
    }
}

void ui_draw_keyboard(App* app, ImVec2 pos, ImVec2 size, float scale) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float rail_h = UI_KEYBOARD_RAIL_HEIGHT * scale;
    keyboard_draw_rail(app, dl, pos, size.x, rail_h);

    ImVec2 start = ImVec2(pos.x, pos.y + rail_h);
    float key_w = size.x / (float)PIANO_WHITE_KEY_COUNT;
    float key_h = size.y - rail_h;
    if (key_h < 1.0f) return;
    float black_w = key_w * KEYBOARD_BLACK_WIDTH_RATIO;
    float black_h = key_h * KEYBOARD_BLACK_HEIGHT_RATIO;

    ImVec2 mouse = ImGui::GetIO().MousePos;
    bool mouse_down = ImGui::IsMouseDown(ImGuiMouseButton_Left);
    bool window_hovered = ImGui::IsWindowHovered();
    bool over_keys = window_hovered && ImGui::IsMouseHoveringRect(start, ImVec2(start.x + size.x, start.y + key_h));

    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && over_keys) app->keyboard_dragging = true;
    if (!mouse_down) app->keyboard_dragging = false;

    int active_note = -1;
    float active_velocity = 1.0f;
    if (app->keyboard_dragging && mouse_down) {
        for (int w_idx = 0; w_idx < PIANO_WHITE_KEY_COUNT && active_note == -1; ++w_idx) {
            int note = keyboard_white_note(w_idx);
            if (!keyboard_has_black_above(note) || note + 1 > PIANO_HIGH_NOTE) continue;
            ImVec2 k_pos = keyboard_black_pos(start, w_idx, key_w, black_w);
            ImVec2 k_end = ImVec2(k_pos.x + black_w, k_pos.y + black_h);
            if (ImGui::IsMouseHoveringRect(k_pos, k_end)) {
                active_note = note + 1;
                active_velocity = keyboard_velocity(mouse.y, k_pos.y, black_h);
            }
        }
        for (int w_idx = 0; w_idx < PIANO_WHITE_KEY_COUNT && active_note == -1; ++w_idx) {
            ImVec2 k_pos = ImVec2(start.x + (float)w_idx * key_w, start.y);
            ImVec2 k_end = ImVec2(k_pos.x + key_w, k_pos.y + key_h);
            if (ImGui::IsMouseHoveringRect(k_pos, k_end)) {
                active_note = keyboard_white_note(w_idx);
                active_velocity = keyboard_velocity(mouse.y, k_pos.y, key_h);
            }
        }
    }

    if (active_note != app->keyboard_active_note) {
        if (app->keyboard_active_note != -1) app_note_off(app, app->keyboard_active_note);
        if (active_note != -1) app_note_on(app, active_note, active_velocity);
        app->keyboard_active_note = active_note;
    }

    for (int w_idx = 0; w_idx < PIANO_WHITE_KEY_COUNT; ++w_idx) {
        int note = keyboard_white_note(w_idx);
        ImVec2 k_pos = ImVec2(start.x + (float)w_idx * key_w, start.y);
        ImVec2 k_end = ImVec2(k_pos.x + key_w, k_pos.y + key_h);
        bool hovered = window_hovered && ImGui::IsMouseHoveringRect(k_pos, k_end) && !mouse_down;
        dl->AddRectFilled(k_pos, k_end, keyboard_key_color(app, note, false, hovered), 1.5f);
        dl->AddRect(k_pos, k_end, KEY_COLOR_WHITE_BORDER_U32, 1.5f, 0, 1.0f);

        if (note % SEMITONES_PER_OCTAVE == 0) {
            char label[8];
            midi_note_name(note, label, sizeof(label));
            ImVec2 text_size = ImGui::CalcTextSize(label);
            float text_x = k_pos.x + (key_w - text_size.x) * 0.5f;
            dl->AddText(ImVec2(text_x, k_end.y - 15.0f * scale), KEY_COLOR_WHITE_BORDER_U32, label);
        }
    }

    for (int w_idx = 0; w_idx < PIANO_WHITE_KEY_COUNT; ++w_idx) {
        int note = keyboard_white_note(w_idx);
        if (!keyboard_has_black_above(note) || note + 1 > PIANO_HIGH_NOTE) continue;
        int black_note = note + 1;
        ImVec2 k_pos = keyboard_black_pos(start, w_idx, key_w, black_w);
        ImVec2 k_end = ImVec2(k_pos.x + black_w, k_pos.y + black_h);
        bool hovered = window_hovered && ImGui::IsMouseHoveringRect(k_pos, k_end) && !mouse_down;
        dl->AddRectFilled(k_pos, k_end, keyboard_key_color(app, black_note, true, hovered), 1.0f);
        dl->AddRect(k_pos, k_end, KEY_COLOR_BLACK_BORDER_U32, 1.0f, 0, 1.0f);
    }
}
