#pragma once
// The FastSampler colour palette, so both applications look the same.
//
// FastSampler writes its colours in "code" order and gets them swapped red
// for blue on the way to the screen; the comment behind each of its values
// names the colour that comes out. This host renders straight RGBA, so every
// value here is FastSampler's value with the red and blue channels swapped:
// the screen colour, written as such. The comments keep FastSampler's names.

#include "imgui.h"
#include "fastresampler_constants.h"

// Background colors
#define CODE_COLOR_WINDOW_BG            ImVec4(0.09f, 0.08f, 0.08f, 1.00f) // Dark Warm Grey/Brown
#define CODE_COLOR_CHILD_BG             ImVec4(0.12f, 0.10f, 0.10f, 1.00f) // Slightly lighter Dark Brown
#define CODE_COLOR_BG_U32               IM_COL32(20, 18, 18, 255)
#define CODE_COLOR_PANEL_BG_U32         IM_COL32(12, 10, 10, 255)

// Text
#define CODE_COLOR_TEXT                 ImVec4(0.94f, 0.92f, 0.92f, 1.00f)
#define CODE_COLOR_TEXT_DIM             ImVec4(0.48f, 0.45f, 0.45f, 1.00f)

// Borders & Rulers
#define CODE_COLOR_BORDER               ImVec4(0.20f, 0.18f, 0.18f, 1.00f) // Border Brown
#define CODE_COLOR_BORDER_U32           IM_COL32(38, 35, 35, 255)

// Normal buttons
#define CODE_COLOR_BUTTON               ImVec4(0.16f, 0.14f, 0.14f, 1.00f) // Lighter Brown
#define CODE_COLOR_BUTTON_HOVERED       ImVec4(0.22f, 0.19f, 0.19f, 1.00f) // Lighter Warm Brown
#define CODE_COLOR_BUTTON_ACTIVE        ImVec4(1.00f, 0.55f, 0.00f, 1.00f) // Orange highlight

// Active Highlight / Orange accent
#define CODE_COLOR_ORANGE_VEC4          ImVec4(1.00f, 0.55f, 0.00f, 1.00f) // Orange
#define CODE_COLOR_ORANGE_U32           IM_COL32(255, 140, 0, 255)

// Section headings in the side panel: the orange FastSampler titles its
// panels with ("INSTRUMENTS BROWSER"), for the capitals and the double rule.
#define CODE_COLOR_SECTION_VEC4         CODE_COLOR_ORANGE_VEC4
#define CODE_COLOR_SECTION_U32          CODE_COLOR_ORANGE_U32

// The selected tab, everywhere.
#define CODE_COLOR_TAB_SELECTED_VEC4    ImVec4(0.196f, 0.125f, 0.094f, 1.00f) // Dark Brown
#define CODE_COLOR_SPECTRUM_FILL_U32    IM_COL32(255, 150, 100, 40)
#define CODE_COLOR_SPECTRUM_LINE_U32    IM_COL32(255, 150, 100, 100)

// Success Status (Green)
#define CODE_COLOR_GREEN_VEC4           ImVec4(0.10f, 0.78f, 0.10f, 1.00f) // Green
#define CODE_COLOR_GREEN_U32            IM_COL32(25, 200, 25, 255)
#define CODE_COLOR_GREEN_BUTTON         ImVec4(0.10f, 0.50f, 0.10f, 1.00f)
#define CODE_COLOR_GREEN_BUTTON_HOVERED ImVec4(0.15f, 0.65f, 0.15f, 1.00f)
#define CODE_COLOR_GREEN_BUTTON_ACTIVE  ImVec4(0.20f, 0.80f, 0.20f, 1.00f)

// Information (Blue)
#define CODE_COLOR_INFO_VEC4            ImVec4(0.25f, 0.61f, 1.00f, 1.00f) // Azure Blue

// Warning / Message (Yellow/Orange)
#define CODE_COLOR_WARNING_VEC4         ImVec4(1.00f, 0.75f, 0.00f, 1.00f) // Warm Yellow/Orange
#define CODE_COLOR_WARNING_U32          IM_COL32(255, 190, 0, 255)

// Error / Panic / Delete (Saturated Red)
#define CODE_COLOR_RED_VEC4             ImVec4(0.90f, 0.10f, 0.10f, 1.00f) // Saturated Red
#define CODE_COLOR_RED_U32              IM_COL32(230, 25, 25, 255)
#define CODE_COLOR_RED_BUTTON           ImVec4(0.75f, 0.15f, 0.15f, 1.00f)
#define CODE_COLOR_RED_BUTTON_HOVERED   ImVec4(0.90f, 0.22f, 0.22f, 1.00f)
#define CODE_COLOR_RED_BUTTON_ACTIVE    ImVec4(1.00f, 0.30f, 0.30f, 1.00f)

// Disabled text / Inactive state (Neutral Gray)
#define CODE_COLOR_TEXT_DISABLED        ImVec4(0.50f, 0.50f, 0.50f, 1.00f)

// Extra UI theme colors
#define CODE_COLOR_ORANGE_TRANSLUCENT   ImVec4(0.80f, 0.45f, 0.00f, 0.65f) // Orange (translucent)
#define CODE_COLOR_SLOT_LABEL_TEXT      ImVec4(0.60f, 0.50f, 0.50f, 1.00f) // Warm Gray

// The note range the keyboard marks, in the key's own fill: FastSampler's
// purple for a white key, a dark purple for a black key, lighter when hovered.
#define CODE_COLOR_PURPLE_U32           IM_COL32(150, 80, 220, 255)
#define CODE_COLOR_PURPLE_LIGHT_U32     IM_COL32(185, 130, 240, 255)
#define CODE_COLOR_PURPLE_DIM_U32       IM_COL32(80, 40, 125, 255)

// Keyboard keys
#define KEY_COLOR_WHITE_U32             IM_COL32(255, 255, 255, 255)
#define KEY_COLOR_WHITE_HOVERED_U32     IM_COL32(230, 235, 235, 255)
#define KEY_COLOR_WHITE_BORDER_U32      IM_COL32(42, 40, 40, 255)
#define KEY_COLOR_BLACK_U32             IM_COL32(27, 25, 25, 255)
#define KEY_COLOR_BLACK_HOVERED_U32     IM_COL32(55, 50, 50, 255)
#define KEY_COLOR_BLACK_BORDER_U32      IM_COL32(12, 10, 10, 255)
#define KEY_COLOR_PRESSED_U32           CODE_COLOR_ORANGE_U32

// Output meter colours: green, then amber above METER_AMBER_DB, red at 0 dB.
#define METER_COLOR_GREEN_U32           IM_COL32(50, 205, 50, 255)
#define METER_COLOR_AMBER_U32           IM_COL32(255, 215, 0, 255)
#define METER_COLOR_RED_U32             IM_COL32(255, 69, 0, 255)
#define METER_AMBER_DB                  -12.0f

// FastSampler's theme, applied to the current ImGui context. The scale is the
// DPI factor, so paddings and roundings grow with the font.
inline void apply_neon_theme(float scale) {
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* colors = style.Colors;

    colors[ImGuiCol_Text]                   = CODE_COLOR_TEXT;
    colors[ImGuiCol_TextDisabled]           = CODE_COLOR_TEXT_DIM;
    colors[ImGuiCol_WindowBg]               = CODE_COLOR_WINDOW_BG;
    colors[ImGuiCol_ChildBg]                = CODE_COLOR_CHILD_BG;
    colors[ImGuiCol_PopupBg]                = CODE_COLOR_WINDOW_BG;
    colors[ImGuiCol_Border]                 = CODE_COLOR_BORDER;
    colors[ImGuiCol_BorderShadow]           = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_ModalWindowDimBg]       = ImVec4(0.02f, 0.02f, 0.02f, 0.60f);

    colors[ImGuiCol_Separator]              = CODE_COLOR_BORDER;
    colors[ImGuiCol_SeparatorHovered]       = CODE_COLOR_BUTTON_HOVERED;
    colors[ImGuiCol_SeparatorActive]        = CODE_COLOR_BUTTON_ACTIVE;

    colors[ImGuiCol_CheckMark]              = CODE_COLOR_ORANGE_VEC4;
    colors[ImGuiCol_TextSelectedBg]         = CODE_COLOR_ORANGE_TRANSLUCENT;
    colors[ImGuiCol_NavCursor]              = CODE_COLOR_ORANGE_VEC4;

    colors[ImGuiCol_Tab]                    = CODE_COLOR_BUTTON;
    colors[ImGuiCol_TabHovered]             = CODE_COLOR_BUTTON_HOVERED;
    colors[ImGuiCol_TabSelected]            = CODE_COLOR_TAB_SELECTED_VEC4;
    colors[ImGuiCol_TabSelectedOverline]    = CODE_COLOR_TAB_SELECTED_VEC4;
    colors[ImGuiCol_TabDimmed]              = CODE_COLOR_BUTTON;
    colors[ImGuiCol_TabDimmedSelected]      = CODE_COLOR_TAB_SELECTED_VEC4;
    colors[ImGuiCol_TabDimmedSelectedOverline] = CODE_COLOR_TAB_SELECTED_VEC4;

    colors[ImGuiCol_FrameBg]                = CODE_COLOR_BUTTON;
    colors[ImGuiCol_FrameBgHovered]         = CODE_COLOR_BUTTON_HOVERED;
    colors[ImGuiCol_FrameBgActive]          = CODE_COLOR_BUTTON_ACTIVE;

    colors[ImGuiCol_TitleBg]                = CODE_COLOR_WINDOW_BG;
    colors[ImGuiCol_TitleBgActive]          = CODE_COLOR_CHILD_BG;
    colors[ImGuiCol_TitleBgCollapsed]       = CODE_COLOR_WINDOW_BG;

    colors[ImGuiCol_ScrollbarBg]            = CODE_COLOR_WINDOW_BG;
    colors[ImGuiCol_ScrollbarGrab]          = CODE_COLOR_BUTTON;
    colors[ImGuiCol_ScrollbarGrabHovered]   = CODE_COLOR_BUTTON_HOVERED;
    colors[ImGuiCol_ScrollbarGrabActive]    = CODE_COLOR_BUTTON_ACTIVE;

    colors[ImGuiCol_SliderGrab]             = CODE_COLOR_BUTTON_ACTIVE;
    colors[ImGuiCol_SliderGrabActive]       = CODE_COLOR_BUTTON_ACTIVE;
    colors[ImGuiCol_Button]                 = CODE_COLOR_BUTTON;
    colors[ImGuiCol_ButtonHovered]          = CODE_COLOR_BUTTON_HOVERED;
    colors[ImGuiCol_ButtonActive]           = CODE_COLOR_BUTTON_ACTIVE;

    colors[ImGuiCol_Header]                 = CODE_COLOR_BUTTON_HOVERED;
    colors[ImGuiCol_HeaderHovered]          = CODE_COLOR_BUTTON_HOVERED;
    colors[ImGuiCol_HeaderActive]           = CODE_COLOR_ORANGE_TRANSLUCENT;
    colors[ImGuiCol_DragDropTarget]         = CODE_COLOR_ORANGE_VEC4;

    colors[ImGuiCol_TableHeaderBg]          = CODE_COLOR_BUTTON;
    colors[ImGuiCol_TableBorderStrong]      = CODE_COLOR_BORDER;
    colors[ImGuiCol_TableBorderLight]       = CODE_COLOR_BORDER;
    colors[ImGuiCol_TableRowBg]             = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_TableRowBgAlt]          = ImVec4(1.00f, 1.00f, 1.00f, 0.03f);

    style.WindowRounding = 4.0f * scale;
    style.FrameRounding = 3.0f * scale;
    style.GrabRounding = 3.0f * scale;
    style.WindowBorderSize = 1.0f;
    style.FrameBorderSize = 1.0f;
    style.FramePadding = ImVec2(4.0f * scale, 3.0f * scale);
    style.ItemSpacing = ImVec2(8.0f * scale, 4.0f * scale);
    style.ItemInnerSpacing = ImVec2(4.0f * scale, 4.0f * scale);
    style.WindowPadding = ImVec2(8.0f * scale, 8.0f * scale);
    style.ScrollbarSize = 14.0f * scale;
    style.GrabMinSize = 12.0f * scale;
    style.IndentSpacing = 21.0f * scale;
    style.TabRounding = 4.0f * scale;

    // The font is a vector font; the DPI factor scales it without a rebake.
    style.FontSizeBase = UI_BASE_FONT_SIZE;
    style.FontScaleDpi = scale;
}
