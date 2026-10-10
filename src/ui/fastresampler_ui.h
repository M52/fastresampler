#pragma once

#include "imgui.h"

struct App;
struct ResampleCcCurve;
struct ResampleConfig;

void ui_draw_frame(App* app, float width, float height, float scale);

void ui_draw_keyboard(App* app, ImVec2 pos, ImVec2 size, float scale);

void ui_draw_cc_curve(ResampleCcCurve* curve, const ResampleConfig* cfg, const char* id, float scale);
