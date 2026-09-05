#pragma once
// The ImGui layer: a side panel on the left (plugin, MIDI, settings tabs), the
// main area (plugin list, or the header above the plugin's own editor), and
// the GUI keyboard along the bottom.

#include "imgui.h"

struct App;

// One frame. width and height are the window's client size in pixels, scale
// the DPI factor every layout constant is multiplied by.
void ui_draw_frame(App* app, float width, float height, float scale);

// The 88-key keyboard inside the rectangle given. Mouse presses on keys send
// notes; held notes are drawn in the accent colour.
void ui_draw_keyboard(App* app, ImVec2 pos, ImVec2 size, float scale);
