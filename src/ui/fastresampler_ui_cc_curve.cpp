#include <stdio.h>
#include <math.h>
#include "imgui.h"
#include "ui/fastresampler_ui.h"
#include "ui/fastresampler_palette.h"
#include "fastresampler_resample.h"
#include "fastresampler_helpers.h"

#define UI_CURVE_HEIGHT         120.0f
#define UI_CURVE_MIN_WIDTH      100.0f
#define UI_CURVE_NODE_RADIUS    4.0f
#define UI_CURVE_HANDLE_RADIUS  3.0f
#define UI_CURVE_GRAB_DISTANCE  16.0f
#define UI_CURVE_NODE_MIN_GAP   0.01f
#define UI_CURVE_TICK_MIN_PX    36.0f
#define UI_CURVE_LABEL_PAD      4.0f

#define UI_CURVE_PREROLL_FILL_U32 IM_COL32(150, 80, 220, 28)
#define UI_CURVE_HOLD_FILL_U32    IM_COL32(255, 140, 0, 22)
#define UI_CURVE_GRID_U32         IM_COL32(30, 27, 27, 255)
#define UI_CURVE_BOUNDARY_U32     IM_COL32(200, 200, 200, 110)
#define UI_CURVE_LABEL_U32        IM_COL32(200, 200, 200, 255)
#define UI_CURVE_FILL_U32         IM_COL32(255, 140, 0, 40)
#define UI_CURVE_TETHER_U32       IM_COL32(255, 140, 0, 100)
#define UI_CURVE_HANDLE_U32       IM_COL32(100, 200, 255, 220)
#define UI_CURVE_OUTLINE_U32      IM_COL32(255, 255, 255, 255)

static ImGuiID g_curve_drag_owner = 0;
static int     g_curve_drag_type = 0;
static int     g_curve_drag_index = -1;
static ImVec2  g_curve_last_mouse;
static bool    g_curve_last_valid = false;

static float ui_curve_tick_step(float span_seconds, float width, float scale) {
    static const float steps[] = { 0.1f, 0.25f, 0.5f, 1.0f, 2.0f, 5.0f, 10.0f, 30.0f, 60.0f, 120.0f, 300.0f };
    int count = (int)(sizeof(steps) / sizeof(steps[0]));
    for (int i = 0; i < count; ++i) {
        if (width * steps[i] / span_seconds >= UI_CURVE_TICK_MIN_PX * scale) return steps[i];
    }
    return steps[count - 1];
}

static void ui_curve_time_label(float seconds, char* out, size_t out_len) {
    snprintf(out, out_len, "%.3g s", seconds);
}

static void ui_curve_bottom_label(ImDrawList* dl, ImVec2 pos, float width, float bottom, float x,
                                  const char* text, float* last_right, float scale) {
    ImVec2 size = ImGui::CalcTextSize(text);
    float pad = UI_CURVE_LABEL_PAD * scale;
    float left = x - size.x * 0.5f;
    if (left + size.x > pos.x + width - pad) left = pos.x + width - pad - size.x;
    if (left < pos.x + pad) left = pos.x + pad;
    if (left < *last_right + pad) return;
    dl->AddText(ImVec2(left, bottom - size.y - 2.0f * scale), UI_CURVE_LABEL_U32, text);
    *last_right = left + size.x;
}

static void ui_curve_top_label(ImDrawList* dl, ImVec2 pos, float x0, float x1, const char* text,
                               float* last_right, float scale) {
    ImVec2 size = ImGui::CalcTextSize(text);
    float pad = UI_CURVE_LABEL_PAD * scale;
    if (x1 - x0 < size.x + 2.0f * pad) return;
    float left = 0.5f * (x0 + x1) - size.x * 0.5f;
    if (left < *last_right + pad) return;
    dl->AddText(ImVec2(left, pos.y + 2.0f * scale), UI_CURVE_LABEL_U32, text);
    *last_right = left + size.x;
}

void ui_draw_cc_curve(ResampleCcCurve* curve, const ResampleConfig* cfg, const char* id, float scale) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 pos = ImGui::GetCursorScreenPos();
    float w = ImGui::GetContentRegionAvail().x;
    float h = UI_CURVE_HEIGHT * scale;
    if (w < UI_CURVE_MIN_WIDTH * scale) w = UI_CURVE_MIN_WIDTH * scale;
    ImVec2 end = ImVec2(pos.x + w, pos.y + h);
    float bottom = end.y;

    ImGui::InvisibleButton(id, ImVec2(w, h));
    ImGuiID canvas_id = ImGui::GetItemID();
    bool active = ImGui::IsItemActive();
    bool hovered = ImGui::IsItemHovered();
    ImVec2 mouse = ImGui::GetIO().MousePos;

    float preroll, hold, gap;
    float span = resample_cc_span(cfg, &preroll, &hold, &gap);
    if (span <= 0.0f) span = 1.0f;
    float x_hold = pos.x + w * (preroll / span);
    float x_gap = pos.x + w * ((preroll + hold) / span);

    dl->AddRectFilled(pos, end, CODE_COLOR_PANEL_BG_U32, 4.0f * scale);
    if (preroll > 0.0f) dl->AddRectFilled(pos, ImVec2(x_hold, bottom), UI_CURVE_PREROLL_FILL_U32);
    if (hold > 0.0f) dl->AddRectFilled(ImVec2(x_hold, pos.y), ImVec2(x_gap, bottom), UI_CURVE_HOLD_FILL_U32);
    for (int i = 1; i < 4; ++i) {
        float y = pos.y + h * (float)i * 0.25f;
        dl->AddLine(ImVec2(pos.x, y), ImVec2(end.x, y), UI_CURVE_GRID_U32);
    }
    float step = ui_curve_tick_step(span, w, scale);
    for (float t = step; t < span - step * 0.01f; t += step) {
        float x = pos.x + w * (t / span);
        dl->AddLine(ImVec2(x, pos.y), ImVec2(x, bottom), UI_CURVE_GRID_U32);
    }
    if (preroll > 0.0f) dl->AddLine(ImVec2(x_hold, pos.y), ImVec2(x_hold, bottom), UI_CURVE_BOUNDARY_U32);
    if (gap > 0.0f) dl->AddLine(ImVec2(x_gap, pos.y), ImVec2(x_gap, bottom), UI_CURVE_BOUNDARY_U32);

    auto node_pos = [&](int i) {
        return ImVec2(pos.x + curve->node_x[i] * w, pos.y + (1.0f - curve->node_y[i]) * h);
    };
    auto handle_pos = [&](int i) {
        return ImVec2(pos.x + 0.5f * (curve->node_x[i] + curve->node_x[i + 1]) * w,
                      pos.y + (1.0f - curve->curvature[i]) * h);
    };
    auto distance = [&](ImVec2 p) {
        float dx = mouse.x - p.x, dy = mouse.y - p.y;
        return sqrtf(dx * dx + dy * dy);
    };
    float mx = fr_clampf((mouse.x - pos.x) / w, 0.0f, 1.0f);
    float my = fr_clampf(1.0f - (mouse.y - pos.y) / h, 0.0f, 1.0f);

    if (curve->is_bezier) {
        if (active && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            float best = UI_CURVE_GRAB_DISTANCE * scale;
            g_curve_drag_owner = canvas_id;
            g_curve_drag_type = 0;
            g_curve_drag_index = -1;
            for (int i = 0; i < curve->node_count; ++i) {
                float d = distance(node_pos(i));
                if (d < best) {
                    best = d;
                    g_curve_drag_type = 1;
                    g_curve_drag_index = i;
                }
            }
            if (g_curve_drag_type == 0) {
                for (int i = 0; i < curve->node_count - 1; ++i) {
                    float d = distance(handle_pos(i));
                    if (d < best) {
                        best = d;
                        g_curve_drag_type = 2;
                        g_curve_drag_index = i;
                    }
                }
            }
        }
        if (active && g_curve_drag_owner == canvas_id && g_curve_drag_type > 0) {
            int i = g_curve_drag_index;
            if (g_curve_drag_type == 1) {
                if (i > 0 && i < curve->node_count - 1) {
                    float lo = curve->node_x[i - 1] + UI_CURVE_NODE_MIN_GAP;
                    float hi = curve->node_x[i + 1] - UI_CURVE_NODE_MIN_GAP;
                    curve->node_x[i] = fr_clampf(mx, lo, hi);
                }
                curve->node_y[i] = my;
            } else {
                curve->curvature[i] = my;
            }
            resample_cc_from_bezier(curve);
        }
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left) && g_curve_drag_owner == canvas_id) {
            g_curve_drag_owner = 0;
            g_curve_drag_type = 0;
            g_curve_drag_index = -1;
        }
        if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && curve->node_count < RESAMPLE_CC_MAX_NODES) {
            bool on_node = false;
            for (int i = 0; i < curve->node_count; ++i) {
                on_node = on_node || distance(node_pos(i)) < UI_CURVE_GRAB_DISTANCE * scale;
            }
            if (!on_node && mx > UI_CURVE_NODE_MIN_GAP && mx < 1.0f - UI_CURVE_NODE_MIN_GAP) {
                resample_cc_insert_node(curve, mx, my);
                resample_cc_from_bezier(curve);
                g_curve_drag_type = 0;
                g_curve_drag_index = -1;
            }
        }
        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right) && curve->node_count > 2) {
            float best = UI_CURVE_GRAB_DISTANCE * scale;
            int victim = -1;
            for (int i = 1; i < curve->node_count - 1; ++i) {
                float d = distance(node_pos(i));
                if (d < best) {
                    best = d;
                    victim = i;
                }
            }
            if (victim > 0) {
                for (int i = victim; i < curve->node_count - 1; ++i) {
                    curve->node_x[i] = curve->node_x[i + 1];
                    curve->node_y[i] = curve->node_y[i + 1];
                    curve->curvature[i] = curve->curvature[i + 1];
                }
                curve->node_count--;
                resample_cc_from_bezier(curve);
            }
        }
    } else if (active) {
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) g_curve_last_valid = false;
        const int last = RESAMPLE_CC_CURVE_POINTS - 1;
        int p = fr_clampi((int)(mx * (float)last + 0.5f), 0, last);
        if (g_curve_last_valid) {
            float lx = fr_clampf((g_curve_last_mouse.x - pos.x) / w, 0.0f, 1.0f);
            float ly = fr_clampf(1.0f - (g_curve_last_mouse.y - pos.y) / h, 0.0f, 1.0f);
            int lp = fr_clampi((int)(lx * (float)last + 0.5f), 0, last);
            int from = lp < p ? lp : p;
            int to = lp < p ? p : lp;
            for (int i = from; i <= to; ++i) {
                float t = from == to ? 1.0f : (float)(i - from) / (float)(to - from);
                if (p < lp) t = 1.0f - t;
                curve->values[i] = ly + (my - ly) * t;
            }
        } else {
            curve->values[p] = my;
        }
        g_curve_last_mouse = mouse;
        g_curve_last_valid = true;
    }

    ImVec2 pts[RESAMPLE_CC_CURVE_POINTS];
    for (int p = 0; p < RESAMPLE_CC_CURVE_POINTS; ++p) {
        pts[p] = ImVec2(pos.x + w * (float)p / (float)(RESAMPLE_CC_CURVE_POINTS - 1),
                        pos.y + h * (1.0f - curve->values[p]));
    }
    for (int p = 0; p < RESAMPLE_CC_CURVE_POINTS - 1; ++p) {
        dl->AddQuadFilled(pts[p], pts[p + 1], ImVec2(pts[p + 1].x, bottom), ImVec2(pts[p].x, bottom), UI_CURVE_FILL_U32);
    }
    for (int p = 0; p < RESAMPLE_CC_CURVE_POINTS - 1; ++p) dl->AddLine(pts[p], pts[p + 1], CODE_COLOR_ORANGE_U32, 2.0f);

    if (curve->is_bezier) {
        for (int i = 0; i < curve->node_count - 1; ++i) {
            ImVec2 hp = handle_pos(i);
            ImVec2 mid = ImVec2(hp.x, pos.y + (1.0f - 0.5f * (curve->node_y[i] + curve->node_y[i + 1])) * h);
            dl->AddLine(mid, hp, UI_CURVE_TETHER_U32);
            dl->AddCircleFilled(hp, UI_CURVE_HANDLE_RADIUS * scale, UI_CURVE_HANDLE_U32);
            dl->AddCircle(hp, UI_CURVE_HANDLE_RADIUS * scale, UI_CURVE_OUTLINE_U32);
        }
        for (int i = 0; i < curve->node_count; ++i) {
            ImVec2 np = node_pos(i);
            dl->AddCircleFilled(np, UI_CURVE_NODE_RADIUS * scale, CODE_COLOR_ORANGE_U32);
            dl->AddCircle(np, UI_CURVE_NODE_RADIUS * scale, UI_CURVE_OUTLINE_U32);
        }
    }

    dl->AddRect(pos, end, CODE_COLOR_BORDER_U32, 4.0f * scale);
    float pad = UI_CURVE_LABEL_PAD * scale;
    ImVec2 max_size = ImGui::CalcTextSize("127");
    dl->AddText(ImVec2(pos.x + pad, pos.y + 2.0f * scale), UI_CURVE_LABEL_U32, "127");
    ImVec2 zero_size = ImGui::CalcTextSize("0");
    dl->AddText(ImVec2(pos.x + pad, bottom - zero_size.y - 2.0f * scale), UI_CURVE_LABEL_U32, "0");

    float top_right = pos.x + pad + max_size.x;
    if (preroll > 0.0f) ui_curve_top_label(dl, pos, pos.x, x_hold, "Pre-roll", &top_right, scale);
    ui_curve_top_label(dl, pos, x_hold, x_gap, "Hold", &top_right, scale);
    if (gap > 0.0f) ui_curve_top_label(dl, pos, x_gap, end.x, "Gap", &top_right, scale);

    float bottom_right = pos.x + pad + zero_size.x;
    char text[32];
    if (preroll > 0.0f) {
        ui_curve_time_label(preroll, text, sizeof(text));
        ui_curve_bottom_label(dl, pos, w, bottom, x_hold, text, &bottom_right, scale);
    }
    if (gap > 0.0f) {
        ui_curve_time_label(preroll + hold, text, sizeof(text));
        ui_curve_bottom_label(dl, pos, w, bottom, x_gap, text, &bottom_right, scale);
    }
    ui_curve_time_label(span, text, sizeof(text));
    ui_curve_bottom_label(dl, pos, w, bottom, end.x, text, &bottom_right, scale);

    if (hovered) {
        int value = fr_clampi((int)(resample_cc_value_at(curve, mx) * (float)MIDI_VALUE_MAX + 0.5f), 0, MIDI_VALUE_MAX);
        ImGui::SetTooltip("%.2f s: CC%d = %d", mx * span, curve->controller, value);
    }

    ImGui::PushID(id);
    if (ImGui::Checkbox("Bezier", &curve->is_bezier) && curve->is_bezier) {
        resample_cc_fit_bezier(curve);
        resample_cc_from_bezier(curve);
    }
    ImGui::SameLine();
    if (ImGui::Button("Zero")) resample_cc_set_constant(curve, 0.0f);
    ImGui::SameLine();
    ImGui::BeginDisabled(curve->is_bezier);
    if (ImGui::Button("Smooth")) resample_cc_smooth(curve);
    ImGui::EndDisabled();
    if (ImGui::Button("Constant")) resample_cc_set_constant(curve, (float)curve->constant / (float)MIDI_VALUE_MAX);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::SliderInt("##constant", &curve->constant, 0, MIDI_VALUE_MAX);
    ImGui::PopID();
}
