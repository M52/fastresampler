// #pragma once (removed for unity build)
// The GUI keyboard, after FastSampler's: 52 white keys from A0 to C8, black
// keys straddling the boundary above their white neighbour, velocity from how
// far down the key the click lands.

#include <stdio.h>
#include "imgui.h"
#include "ui/fastresampler_ui.h"
#include "ui/fastresampler_palette.h"
#include "fastresampler_app.h"
#include "fastresampler_constants.h"

// Fraction of a white key's width and height a black key covers.
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

// C, D, F, G and A have a black key a semitone up.
static bool keyboard_has_black_above(int note) {
    int pc = note % SEMITONES_PER_OCTAVE;
    return pc == 0 || pc == 2 || pc == 5 || pc == 7 || pc == 9;
}

// The C labels, in FastSampler's naming where middle C (60) is C3.
static const char* keyboard_white_label(int note, char* buf, size_t buf_size) {
    if (note % SEMITONES_PER_OCTAVE != 0) return nullptr;
    snprintf(buf, buf_size, "C%d", note / SEMITONES_PER_OCTAVE - 2);
    return buf;
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

void ui_draw_keyboard(App* app, ImVec2 start, ImVec2 size, float scale) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float key_w = size.x / (float)PIANO_WHITE_KEY_COUNT;
    float key_h = size.y;
    float black_w = key_w * KEYBOARD_BLACK_WIDTH_RATIO;
    float black_h = key_h * KEYBOARD_BLACK_HEIGHT_RATIO;

    ImVec2 mouse = ImGui::GetIO().MousePos;
    bool mouse_down = ImGui::IsMouseDown(ImGuiMouseButton_Left);
    bool over_keyboard = ImGui::IsMouseHoveringRect(start, ImVec2(start.x + size.x, start.y + size.y));

    // A press that starts on a key owns the mouse until release, so a drag
    // across the keys glides from note to note. A press that started on a
    // slider and wanders in here plays nothing.
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && over_keyboard) app->keyboard_dragging = true;
    if (!mouse_down) app->keyboard_dragging = false;

    int active_note = -1;
    float active_velocity = 1.0f;
    if (app->keyboard_dragging && mouse_down) {
        // Black keys sit on top of the white ones, so they get first refusal.
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

    // White keys.
    for (int w_idx = 0; w_idx < PIANO_WHITE_KEY_COUNT; ++w_idx) {
        int note = keyboard_white_note(w_idx);
        ImVec2 k_pos = ImVec2(start.x + (float)w_idx * key_w, start.y);
        ImVec2 k_end = ImVec2(k_pos.x + key_w, k_pos.y + key_h);
        bool hovered = ImGui::IsMouseHoveringRect(k_pos, k_end) && !mouse_down;

        ImU32 color = KEY_COLOR_WHITE_U32;
        if (app->note_held[note]) {
            color = KEY_COLOR_PRESSED_U32;
        } else if (hovered) {
            color = KEY_COLOR_WHITE_HOVERED_U32;
        }
        dl->AddRectFilled(k_pos, k_end, color, 1.5f);
        dl->AddRect(k_pos, k_end, KEY_COLOR_WHITE_BORDER_U32, 1.5f, 0, 1.0f);

        char label_buf[8];
        const char* label = keyboard_white_label(note, label_buf, sizeof(label_buf));
        if (label) {
            ImVec2 text_size = ImGui::CalcTextSize(label);
            float text_x = k_pos.x + (key_w - text_size.x) * 0.5f;
            dl->AddText(ImVec2(text_x, k_end.y - 15.0f * scale), KEY_COLOR_WHITE_BORDER_U32, label);
        }
    }

    // Black keys, drawn over the white ones.
    for (int w_idx = 0; w_idx < PIANO_WHITE_KEY_COUNT; ++w_idx) {
        int note = keyboard_white_note(w_idx);
        if (!keyboard_has_black_above(note) || note + 1 > PIANO_HIGH_NOTE) continue;
        int black_note = note + 1;
        ImVec2 k_pos = keyboard_black_pos(start, w_idx, key_w, black_w);
        ImVec2 k_end = ImVec2(k_pos.x + black_w, k_pos.y + black_h);
        bool hovered = ImGui::IsMouseHoveringRect(k_pos, k_end) && !mouse_down;

        ImU32 color = KEY_COLOR_BLACK_U32;
        if (app->note_held[black_note]) {
            color = KEY_COLOR_PRESSED_U32;
        } else if (hovered) {
            color = KEY_COLOR_BLACK_HOVERED_U32;
        }
        dl->AddRectFilled(k_pos, k_end, color, 1.0f);
        dl->AddRect(k_pos, k_end, KEY_COLOR_BLACK_BORDER_U32, 1.0f, 0, 1.0f);
    }
}
