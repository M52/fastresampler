#include <stdio.h>
#include <math.h>
#include "imgui.h"
#include "ui/fastresampler_ui.h"
#include "ui/fastresampler_ui_dragslider.h"
#include "ui/fastresampler_palette.h"
#include "fastresampler_app.h"
#include "fastresampler_licenses.h"
#include "fastresampler_helpers.h"
#include "platform/fastresampler_platform.h"

#define UI_STATUS_HEIGHT 44.0f

#define UI_PATH_MAX_PARTS 64

#define UI_TIME_STEP_SECONDS         0.01f
#define UI_TIME_STEP_SHIFT_SECONDS   0.1f
#define UI_TIME_STEP_ALT_SECONDS     0.001f
#define UI_TIME_MAX_SECONDS          60.0f
#define UI_HOLD_MIN_SECONDS          0.01f
#define UI_PREROLL_GAP_MIN_SECONDS   0.25f
#define UI_PREROLL_GAP_MAX_SECONDS   10.0f

#define UI_MODAL_WIDTH 420.0f
#define UI_DISCLAIMER_WIDTH 620.0f
#define UI_DISCLAIMER_TEXT_HEIGHT 340.0f

#define UI_PROJECT_LIST_ROWS 8.0f

#define UI_SCAN_PANEL_WIDTH 560.0f
#define UI_SCAN_HEADER_BAR_WIDTH 260.0f

#define UI_LAYER_COMBO_WIDTH 90.0f

#define UI_SECTION_RULE_GAP    2.0f
#define UI_SECTION_BOTTOM_PAD  10.0f

#define UI_METER_MIN_DB   -60.0f
#define UI_METER_HEIGHT   10.0f

static float ui_db(float amplitude) {
    if (amplitude <= 0.000001f) return UI_METER_MIN_DB;
    float db = 20.0f * log10f(amplitude);
    return db < UI_METER_MIN_DB ? UI_METER_MIN_DB : db;
}

static ImU32 ui_meter_color(float db) {
    if (db < METER_AMBER_DB) return METER_COLOR_GREEN_U32;
    if (db < 0.0f) return METER_COLOR_AMBER_U32;
    return METER_COLOR_RED_U32;
}

static void ui_draw_meters(App* app, float scale) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float width = ImGui::GetContentRegionAvail().x;
    float bar_h = UI_METER_HEIGHT * scale;
    for (int c = 0; c < FR_OUTPUT_CHANNELS; ++c) {
        ImVec2 pos = ImGui::GetCursorScreenPos();
        ImVec2 end = ImVec2(pos.x + width, pos.y + bar_h);
        dl->AddRectFilled(pos, end, CODE_COLOR_PANEL_BG_U32, 2.0f);
        float db = ui_db(app->meter[c]);
        float fill = (db - UI_METER_MIN_DB) / (0.0f - UI_METER_MIN_DB);
        if (fill > 1.0f) fill = 1.0f;
        if (fill > 0.0f) {
            dl->AddRectFilled(pos, ImVec2(pos.x + width * fill, end.y), ui_meter_color(db), 2.0f);
        }
        dl->AddRect(pos, end, CODE_COLOR_BORDER_U32, 2.0f);
        ImGui::Dummy(ImVec2(width, bar_h + UI_ITEM_SPACING * scale));
    }
}

static void ui_push_button_colors(ImVec4 normal, ImVec4 hovered, ImVec4 active) {
    ImGui::PushStyleColor(ImGuiCol_Button, normal);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hovered);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, active);
}

static bool ui_red_button(const char* label, ImVec2 size = ImVec2(0, 0)) {
    ui_push_button_colors(CODE_COLOR_RED_BUTTON, CODE_COLOR_RED_BUTTON_HOVERED, CODE_COLOR_RED_BUTTON_ACTIVE);
    bool pressed = ImGui::Button(label, size);
    ImGui::PopStyleColor(3);
    return pressed;
}

static bool ui_green_button(const char* label, ImVec2 size = ImVec2(0, 0)) {
    ui_push_button_colors(CODE_COLOR_GREEN_BUTTON, CODE_COLOR_GREEN_BUTTON_HOVERED, CODE_COLOR_GREEN_BUTTON_ACTIVE);
    bool pressed = ImGui::Button(label, size);
    ImGui::PopStyleColor(3);
    return pressed;
}

static bool g_ui_show_tips = true;

static void ui_note(const char* text) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
    ImGui::TextWrapped("%s", text);
    ImGui::PopStyleColor();
}

static void ui_hint(const char* text) {
    if (g_ui_show_tips) ui_note(text);
}

static void ui_folder_name(const char* path, char* out, size_t out_len) {
    fr_strcpy(out, out_len, path);
    size_t len = strlen(out);
    while (len > 1 && (out[len - 1] == '\\' || out[len - 1] == '/')) out[--len] = '\0';
    const char* base = plat_path_basename(out);
    if (base[0]) memmove(out, base, strlen(base) + 1);
}

static void ui_fit_path(const char* path, float width, char* out, size_t out_len) {
    fr_strcpy(out, out_len, path);
    if (ImGui::CalcTextSize(out).x <= width) return;

    const char* starts[UI_PATH_MAX_PARTS];
    int lens[UI_PATH_MAX_PARTS];
    int count = 0;
    const char* p = path;
    while (*p && count < UI_PATH_MAX_PARTS) {
        const char* start = p;
        while (*p && *p != '\\' && *p != '/') ++p;
        if (p > start) {
            starts[count] = start;
            lens[count] = (int)(p - start);
            count++;
        }
        if (*p) ++p;
    }
    if (count >= 3) {
        const char* last = starts[count - 1];
        for (int lead = count - 2; lead >= 1; --lead) {
            int prefix_len = (int)(starts[lead - 1] + lens[lead - 1] - path);
            snprintf(out, out_len, "%.*s%s...%s%s", prefix_len, path, PLAT_PATH_SEP_STR, PLAT_PATH_SEP_STR, last);
            if (ImGui::CalcTextSize(out).x <= width) return;
        }
    }
    size_t len = strlen(path);
    for (size_t keep = len; keep >= 2; --keep) {
        size_t head = (keep + 1) / 2;
        size_t tail = keep - head;
        snprintf(out, out_len, "%.*s...%s", (int)head, path, path + len - tail);
        if (ImGui::CalcTextSize(out).x <= width) return;
    }
}

static DragSliderState g_hold_drag;
static DragSliderState g_gap_drag;
static DragSliderState g_preroll_drag;
static DragSliderState g_legato_source_hold_drag;
static DragSliderState g_legato_overlap_drag;
static DragSliderState g_legato_hold_drag;
static DragSliderState g_legato_margin_drag;

static bool ui_time_slider(const char* id, float* seconds, float min_seconds, float max_seconds, DragSliderState* state) {
    char text[32];
    if (*seconds < 1.0f) {
        snprintf(text, sizeof(text), "%.0f ms", *seconds * 1000.0f);
    } else {
        snprintf(text, sizeof(text), "%.2f s", *seconds);
    }
    return ui_slider_float_drag(id, seconds, min_seconds, max_seconds, text,
                                UI_TIME_STEP_SECONDS, UI_TIME_STEP_SHIFT_SECONDS, UI_TIME_STEP_ALT_SECONDS, state);
}

static void ui_colored_wrapped(const char* text, ImVec4 color) {
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    ImGui::TextWrapped("%s", text);
    ImGui::PopStyleColor();
}

static void ui_label(const char* text, float scale) {
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(text);
    ImGui::SameLine(UI_LABEL_COLUMN_WIDTH * scale);
}

static void ui_label_inline(const char* text) {
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(text);
    ImGui::SameLine();
}

static void ui_section(const char* title, float scale) {
    char caps[64];
    size_t i = 0;
    for (; title[i] && i + 1 < sizeof(caps); ++i) {
        char c = title[i];
        caps[i] = (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
    }
    caps[i] = '\0';
    ImGui::TextColored(CODE_COLOR_SECTION_VEC4, "%s", caps);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 pos = ImGui::GetCursorScreenPos();
    float width = ImGui::GetContentRegionAvail().x;
    float gap = UI_SECTION_RULE_GAP * scale;
    dl->AddLine(pos, ImVec2(pos.x + width, pos.y), CODE_COLOR_SECTION_U32, 1.0f);
    dl->AddLine(ImVec2(pos.x, pos.y + gap + 1.0f), ImVec2(pos.x + width, pos.y + gap + 1.0f), CODE_COLOR_SECTION_U32, 1.0f);
    ImGui::Dummy(ImVec2(width, gap + 2.0f));
}

static void ui_section_end(float scale) {
    ImGui::Dummy(ImVec2(0.0f, UI_SECTION_BOTTOM_PAD * scale));
}

static void ui_folder_row(const char* label, const char* dir, bool disabled, bool* pick_request, float scale) {
    ImGui::PushID((const void*)pick_request);
    char button[FR_MAX_PATH + 32];
    if (dir[0]) {
        char folder[FR_MAX_PATH];
        ui_folder_name(dir, folder, sizeof(folder));
        snprintf(button, sizeof(button), "%s (click to change)##folder", folder);
    } else {
        fr_strcpy(button, sizeof(button), "Choose a folder##folder");
    }
    ui_label(label, scale);
    ImGui::BeginDisabled(disabled);
    if (ImGui::Button(button, ImVec2(-1.0f, 0.0f))) *pick_request = true;
    ImGui::EndDisabled();
    if (dir[0]) {
        char fitted[FR_MAX_PATH + 8];
        ui_fit_path(dir, ImGui::GetContentRegionAvail().x, fitted, sizeof(fitted));
        ImGui::PushStyleColor(ImGuiCol_Text, CODE_COLOR_SLOT_LABEL_TEXT);
        ImGui::TextUnformatted(fitted);
        ImGui::PopStyleColor();
    }
    ImGui::PopID();
}

static void ui_target_folder_row(const char* label, const ResampleConfig* cfg, const char* picked_dir,
                                 bool* override_flag, bool busy, bool* pick_request, float scale) {
    ImGui::PushID((const void*)override_flag);
    char derived[FR_MAX_PATH];
    char err[FR_STATUS_LEN];
    bool from_root = !*override_flag;
    bool have_derived = resample_root_target_dir(cfg, 0, derived, sizeof(derived), err, sizeof(err));
    ui_folder_row(label, from_root ? (have_derived ? derived : "") : picked_dir, busy || from_root, pick_request, scale);
    if (from_root && !have_derived) ui_colored_wrapped(err, CODE_COLOR_WARNING_VEC4);
    if (cfg->bus_count > 1) {
        char note[128];
        snprintf(note, sizeof(note), "and %d more folder%s beside it, one per output bus.",
                 cfg->bus_count - 1, cfg->bus_count - 1 == 1 ? "" : "s");
        ui_note(note);
    }
    ImGui::BeginDisabled(busy);
    ImGui::Checkbox("Choose another folder", override_flag);
    ImGui::EndDisabled();
    ImGui::PopID();
}

static void ui_cc_slider(App* app, const char* label, int controller, float scale) {
    char text[64];
    snprintf(text, sizeof(text), "%s (CC%d)", label, controller);
    ui_label(text, scale);
    char id[32];
    snprintf(id, sizeof(id), "##cc%d", controller);
    int value = app->cc_value[controller];
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::SliderInt(id, &value, 0, MIDI_VALUE_MAX)) app_control_change(app, controller, value);
}

static void ui_tab_plugin(App* app, float scale) {
    if (!app->plugin) {
        ImGui::TextWrapped("No plugin loaded.");
        ui_hint("Double-click a plugin in the VST3 list or an instrument in the VSTi list to load it.");
        return;
    }
    const HostPluginInfo* info = host_plugin_info(app->plugin);
    ImGui::TextWrapped("%s", info->name);
    ImGui::TextDisabled("by %s", info->vendor);
    if (info->version[0]) ImGui::TextDisabled("Version %s", info->version);
    if (info->category[0]) ImGui::TextDisabled("%s", info->category);
    ImGui::Separator();

    ImGui::Text("Format: %s", info->format == HOST_FORMAT_VST2 ? "VSTi (VST 2.4)" : "VST3");
    ImGui::Text("Output channels: %d", info->output_channels);
    ImGui::Text("Latency: %d frames", info->latency_frames);
    ImGui::Text("MIDI CC mapping: %s", info->has_midi_mapping ? "yes" : "no");
    if (app->editor_open) {
        ImGui::Text("Editor: %d x %d", app->editor_width, app->editor_height);
    } else {
        ImGui::Text("Editor: none");
    }
    ImGui::Separator();

    ui_section("Output", scale);
    ui_draw_meters(app, scale);
    ui_section_end(scale);

    if (ui_red_button("Close plugin", ImVec2(-1.0f, 0.0f))) app->unload_request = true;
}

static void ui_tab_midi(App* app, float scale) {
    ui_section("Channel", scale);
    int channel = app->settings.midi_channel;
    char channel_label[16];
    snprintf(channel_label, sizeof(channel_label), "%d", channel + 1);
    ui_label("MIDI channel", scale);
    ImGui::SetNextItemWidth(80.0f * scale);
    if (ImGui::BeginCombo("##channel", channel_label)) {
        for (int i = 0; i < MIDI_CHANNEL_COUNT; ++i) {
            char item[16];
            snprintf(item, sizeof(item), "%d", i + 1);
            if (ImGui::Selectable(item, i == channel)) app->settings.midi_channel = i;
        }
        ImGui::EndCombo();
    }
    ui_hint("Click a key to play it. Lower on the key is louder.");
    ui_section_end(scale);

    ui_section("Controllers", scale);
    ui_cc_slider(app, "Mod wheel", MIDI_CC_MOD_WHEEL, scale);
    ui_cc_slider(app, "Breath", MIDI_CC_BREATH, scale);
    ui_cc_slider(app, "Volume", MIDI_CC_VOLUME, scale);
    ui_cc_slider(app, "Pan", MIDI_CC_PAN, scale);
    ui_cc_slider(app, "Expression", MIDI_CC_EXPRESSION, scale);

    ui_label("Sustain (CC64)", scale);
    bool sustain = app->cc_value[MIDI_CC_SUSTAIN] >= 64;
    if (ImGui::Checkbox("##sustain", &sustain)) app_control_change(app, MIDI_CC_SUSTAIN, sustain ? MIDI_VALUE_MAX : 0);

    ui_label("Pitch bend", scale);
    int bend = app->pitch_bend - MIDI_PITCH_BEND_CENTER;
    ImGui::SetNextItemWidth(-70.0f * scale);
    if (ImGui::SliderInt("##bend", &bend, -MIDI_PITCH_BEND_CENTER, MIDI_PITCH_BEND_CENTER - 1)) {
        app_pitch_bend(app, bend + MIDI_PITCH_BEND_CENTER);
    }
    ImGui::SameLine();
    if (ImGui::Button("Centre", ImVec2(-1.0f, 0.0f))) app_pitch_bend(app, MIDI_PITCH_BEND_CENTER);
    if (app->plugin && !host_plugin_info(app->plugin)->has_midi_mapping) {
        ui_colored_wrapped("This plugin publishes no MIDI controller mapping, so CC and pitch bend do not reach it.",
                           CODE_COLOR_WARNING_VEC4);
    }
    ui_section_end(scale);

    if (ui_red_button("All notes off", ImVec2(-1.0f, 0.0f))) app_all_notes_off(app);
}

static void ui_speed_label(int speed, char* out, size_t out_len) {
    if (speed <= 0) {
        fr_strcpy(out, out_len, "As fast as possible");
    } else if (speed == 1) {
        fr_strcpy(out, out_len, "Realtime");
    } else {
        snprintf(out, out_len, "%dx realtime", speed);
    }
}

static void ui_layer_label(int layer, char* out, size_t out_len) {
    if (layer <= 0) {
        fr_strcpy(out, out_len, "All layers");
    } else {
        resample_layer_suffix(true, layer, out, out_len);
    }
}

static void ui_update_range(App* app) {
    const ResampleConfig* cfg = &app->settings.resample;
    int low = midi_note_parse(cfg->low_note);
    int high = midi_note_parse(cfg->high_note);
    app->range_valid = low >= 0 && high >= 0 && cfg->step >= 1;
    if (!app->range_valid) return;
    if (high < low) {
        int t = low;
        low = high;
        high = t;
    }
    app->range_low = low;
    app->range_high = high;
    app->range_step = cfg->step;
}

static void ui_project_browse(App* app, const char* dir) {
    fr_strcpy(app->project_browse_dir, sizeof(app->project_browse_dir), dir);
    app->project_browse_request = true;
}

static bool ui_icon_button(const char* label, const char* tooltip) {
    bool pressed = ImGui::Button(label);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", tooltip);
    return pressed;
}

static void ui_project_browser(App* app, bool busy) {
    ProjectBrowser* b = &app->project_browser;
    char parent[FR_MAX_PATH];
    bool has_parent = project_browser_parent(b->dir, parent, sizeof(parent));

    ImGui::BeginDisabled(!has_parent);
    if (ui_icon_button(ICON_LC_ARROW_UP "##projects_up", "Up one folder")) ui_project_browse(app, parent);
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ui_icon_button(ICON_LC_HOUSE "##projects_home", "The Projects folder next to FastResampler")) {
        char home[FR_MAX_PATH];
        project_dir(home, sizeof(home));
        ui_project_browse(app, home);
    }
    ImGui::SameLine();
    if (ui_icon_button(ICON_LC_REFRESH_CW "##projects_refresh", "Read the folder again")) ui_project_browse(app, b->dir);
    ImGui::SameLine();
    char fitted[FR_MAX_PATH + 8];
    ui_fit_path(b->dir, ImGui::GetContentRegionAvail().x, fitted, sizeof(fitted));
    ImGui::AlignTextToFramePadding();
    ImGui::PushStyleColor(ImGuiCol_Text, CODE_COLOR_SLOT_LABEL_TEXT);
    ImGui::TextUnformatted(fitted);
    ImGui::PopStyleColor();

    float list_h = UI_PROJECT_LIST_ROWS * ImGui::GetTextLineHeightWithSpacing() + 2.0f * ImGui::GetStyle().WindowPadding.y;
    ImGui::BeginChild("##project_list", ImVec2(-1.0f, list_h), ImGuiChildFlags_Borders, ImGuiWindowFlags_None);
    if (b->count == 0) ui_note("No projects or folders here yet.");
    for (int i = 0; i < b->count; ++i) {
        const ProjectEntry* e = &b->entries[i];
        char label[FR_MAX_PATH + 32];
        if (e->is_dir) {
            snprintf(label, sizeof(label), ICON_LC_FOLDER "  %s##entry%d", e->name, i);
        } else {
            char name[FR_MAX_PATH];
            project_name_of_path(e->name, name, sizeof(name));
            snprintf(label, sizeof(label), ICON_LC_FILE_SLIDERS "  %s##entry%d", name, i);
        }
        if (!ImGui::Selectable(label, b->selected == i, ImGuiSelectableFlags_AllowDoubleClick)) continue;
        b->selected = i;
        if (!ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) continue;
        char path[FR_MAX_PATH];
        project_browser_entry_path(b, i, path, sizeof(path));
        if (e->is_dir) {
            ui_project_browse(app, path);
        } else if (!busy) {
            fr_strcpy(app->project_load_path, sizeof(app->project_load_path), path);
            app->project_load_request = true;
        }
    }
    ImGui::EndChild();
}

static void ui_tab_resample_legato(ResampleConfig* cfg, bool busy, float scale) {
    ui_section("Legato (experimental)", scale);
    ImGui::BeginDisabled(busy);
    ImGui::Checkbox("Capture legato transitions", &cfg->legato);
    ImGui::EndDisabled();
    ui_hint("After every note has been sampled, the run holds one note, presses the next over it and lets the first go, "
            "so whatever the plugin plays between them is captured. The instrument maps those transitions to FastSampler's "
            "legato effect, which plays one whenever a key goes down while another is held.");
    if (cfg->legato) {
        ImGui::BeginDisabled(busy);

        ui_label("Intervals", scale);
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputText("##legato_intervals", cfg->legato_intervals, sizeof(cfg->legato_intervals));
        ui_hint("Widths in semitones, each captured up and down: \"1, 2\" is four transitions from every note. Only these "
                "widths get a transition: a slur of any other width changes note without one, because a transition of the "
                "wrong width would land on the wrong note. Leave no gaps, and note that the notes below need no matching "
                "step -- a transition keeps its width when it is transposed, so spend the run on widths rather than on notes.");

        ui_label("From every", scale);
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::BeginCombo("##legato_step", resample_step_label(cfg->legato_step))) {
            for (int step = 1; step <= RESAMPLE_MAX_STEP; ++step) {
                if (ImGui::Selectable(resample_step_label(step), step == cfg->legato_step)) cfg->legato_step = step;
            }
            ImGui::EndCombo();
        }
        ui_hint("The notes the transitions start from, which need not follow the notes above: transitions are often sampled "
                "every semitone where the sustains are sampled every wholetone. A key between two of them plays the transition "
                "of the nearer one.");

        ui_label("Velocities", scale);
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputText("##legato_velocities", cfg->legato_velocities, sizeof(cfg->legato_velocities));
        ui_hint("What the note landed on is pressed at, which many libraries read as the speed of the transition. Cinematic Studio "
                "Strings has three zones, 0-64, 65-100 and 101-127, so \"30, 80, 110\" captures its slow, medium and fast slur. "
                "The note left is pressed at the same velocity.");

        ui_label("Arrival", scale);
        {
            float spacing = ImGui::GetStyle().ItemSpacing.x;
            float detect_w = ImGui::CalcTextSize("Detect").x + 40.0f * scale;
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - detect_w - spacing);
            ImGui::InputText("##legato_arrivals", cfg->legato_arrivals, sizeof(cfg->legato_arrivals));
            ImGui::SameLine();
            ImGui::Checkbox("Detect", &cfg->legato_detect_arrival);
        }
        ui_hint("How long after the note landed on is pressed the transition gets there, in milliseconds: one time, or one "
                "per velocity. Detect reads the real one out of the audio, by following both pitches, and reads where the "
                "transition leaves the first note with it, so the instrument carries both ends of every transition. It keeps "
                "these times for a transition it cannot hear apart.");

        ui_label("Note left", scale);
        {
            float spacing = ImGui::GetStyle().ItemSpacing.x;
            float overlap_label_w = ImGui::CalcTextSize("Overlap").x;
            float slider_w = (ImGui::GetContentRegionAvail().x - overlap_label_w - 2.0f * spacing) * 0.5f;
            ImGui::SetNextItemWidth(slider_w);
            ui_time_slider("##legato_source_hold", &cfg->legato_source_hold_seconds, UI_HOLD_MIN_SECONDS,
                           UI_TIME_MAX_SECONDS, &g_legato_source_hold_drag);
            ui_label_inline("Overlap");
            ImGui::SetNextItemWidth(-1.0f);
            ui_time_slider("##legato_overlap", &cfg->legato_overlap_seconds, 0.0f, UI_TIME_MAX_SECONDS,
                           &g_legato_overlap_drag);
        }
        ui_hint("How long the note that is left sounds before the next one is pressed, and how long it stays down after that. "
                "The overlap is what makes a plugin play a move at all.");

        ui_label("Note landed on", scale);
        ImGui::SetNextItemWidth(-1.0f);
        ui_time_slider("##legato_hold", &cfg->legato_hold_seconds, UI_HOLD_MIN_SECONDS, UI_TIME_MAX_SECONDS,
                       &g_legato_hold_drag);
        ui_hint("How long it is held. It has to reach past the arrival and the blend, so a slow slur needs a longer hold.");

        ui_label("Sustain either side", scale);
        ImGui::SetNextItemWidth(-1.0f);
        ui_time_slider("##legato_margin", &cfg->legato_margin_seconds, UI_HOLD_MIN_SECONDS, UI_TIME_MAX_SECONDS,
                       &g_legato_margin_drag);
        ui_hint("How much of the note being left each move keeps in front of its departure, and how much of the note landed "
                "on past its arrival. Room for FastSampler's two crossfades rather than something that is played as such: the "
                "sampler starts the move one fade before the departure and ends it one fade past the arrival, so this only "
                "sets how long those fades may be. Half a second lets a slow line blend over a long crossfade; a fast run "
                "never reaches most of it. The part in front of the departure is held in RAM, so a longer margin costs "
                "memory per move.");
        ImGui::EndDisabled();
        if (cfg->round_robins > 1 && !cfg->dynamic_layers) {
            ui_note("The moves are captured once and shared by every round robin.");
        }
    }
    ui_section_end(scale);
}

static void ui_tab_resample(App* app, float scale) {
    ResampleConfig* cfg = &app->settings.resample;
    bool rendering = app_resample_running(app);
    bool building = app_instrument_running(app);
    bool busy = rendering || building;

    ui_section("Naming", scale);
    ui_folder_row("Project root", cfg->root_dir, busy, &app->root_dir_pick_request, scale);
    ui_hint("The root of every project. A run writes under it, into Instrument\\Articulation\\Bus, "
            "so each take gets a folder of its own and each output bus a folder in it. "
            "The Output and Export sections each have a box to write somewhere else.");
    ui_label("Instrument", scale);
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputText("##instrument", cfg->instrument, sizeof(cfg->instrument));
    ui_label("Articulation", scale);
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputText("##articulation", cfg->articulation, sizeof(cfg->articulation));
    {
        char prefix[RESAMPLE_FILE_NAME_LEN];
        char err[FR_STATUS_LEN];
        char preview[RESAMPLE_FILE_NAME_LEN + 32];
        char target[FR_MAX_PATH];
        if (resample_root_target_dir(cfg, 0, target, sizeof(target), err, sizeof(err))) {
            char fitted[FR_MAX_PATH + 8];
            ui_fit_path(target, ImGui::GetContentRegionAvail().x, fitted, sizeof(fitted));
            ui_note(fitted);
        }
        if (resample_prefix(cfg, 0, prefix, sizeof(prefix), err, sizeof(err))) {
            snprintf(preview, sizeof(preview), "Files: %s_<Note>_<Velocity>_%s<n>.wav", prefix, cfg->dynamic_layers ? "dyn" : "RR");
            ui_note(preview);
            if (app_legato_enabled(app)) {
                snprintf(preview, sizeof(preview), "Transitions: %s_<From>-<To>_<Velocity>_%sleg.wav", prefix,
                         cfg->dynamic_layers ? "dyn<n>-" : "");
                ui_note(preview);
            }
            if (cfg->bus_count > 1) {
                snprintf(preview, sizeof(preview), "and the same again for each of the %d output busses, under its own name.",
                         cfg->bus_count);
                ui_note(preview);
            }
        } else {
            ui_colored_wrapped(err, CODE_COLOR_WARNING_VEC4);
        }
    }
    ui_hint("The microphones are the output busses, named in the Output busses tab under the plugin.");
    ui_section_end(scale);

    ui_section("Notes", scale);
    int low = midi_note_parse(cfg->low_note);
    int high = midi_note_parse(cfg->high_note);
    if (low < 0) low = MIDI_NOTE_A0;
    if (high < 0) high = MIDI_NOTE_C8;
    char low_name[8], high_name[8];
    midi_note_name(low, low_name, sizeof(low_name));
    midi_note_name(high, high_name, sizeof(high_name));
    ui_label("Low note", scale);
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::SliderInt("##low", &low, 0, MIDI_NOTE_COUNT - 1, low_name)) {
        if (high < low) high = low;
    }
    ui_label("High note", scale);
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::SliderInt("##high", &high, 0, MIDI_NOTE_COUNT - 1, high_name)) {
        if (low > high) low = high;
    }
    midi_note_name(low, cfg->low_note, sizeof(cfg->low_note));
    midi_note_name(high, cfg->high_note, sizeof(cfg->high_note));
    ui_update_range(app);
    ui_hint("C3 is middle C, C-2 is note 0. The keyboard marks the notes that will be sampled.");

    ui_label("Sample every", scale);
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::BeginCombo("##step", resample_step_label(cfg->step))) {
        for (int step = 1; step <= RESAMPLE_MAX_STEP; ++step) {
            if (ImGui::Selectable(resample_step_label(step), step == cfg->step)) cfg->step = step;
        }
        ImGui::EndCombo();
    }
    ui_label("Velocities", scale);
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputText("##velocities", cfg->velocities, sizeof(cfg->velocities));

    ui_label("Round robins", scale);
    ImGui::SetNextItemWidth(90.0f * scale);
    if (ImGui::InputInt("##rr", &cfg->round_robins)) {
        cfg->round_robins = fr_clampi(cfg->round_robins, 1, RESAMPLE_MAX_ROUND_ROBINS);
    }
    ImGui::SameLine();
    ImGui::Checkbox("RR are dynamic layers", &cfg->dynamic_layers);
    ui_hint("With the box on, the number is one of dynamic layers: every note is played once per layer, a curve below can be "
            "tied to one layer (three CC1 curves at three levels, say), the files end in dyn<n>, and the instrument crossfades "
            "the layers on that controller, every sustain looped and its ring-out mapped as a release sample.");
    ui_label("Pre-roll", scale);
    ImGui::BeginDisabled(cfg->dynamic_layers);
    ImGui::Checkbox("##preroll", &cfg->preroll);
    ui_label_inline("gap");
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::BeginDisabled(!cfg->preroll);
    ui_time_slider("##prerollgap", &cfg->preroll_gap_seconds, UI_PREROLL_GAP_MIN_SECONDS, UI_PREROLL_GAP_MAX_SECONDS, &g_preroll_drag);
    ImGui::EndDisabled();
    ImGui::EndDisabled();
    if (cfg->dynamic_layers) {
        ui_hint("Pre-roll is the run-up a round robin capture is given, which dynamic layers do not use, so it is off.");
    } else {
        ui_hint("Pre-roll is the run-up before every capture: n-1 gaps, so a painted curve is read the same way into each note. It is only spent when there is a curve. The round robins need no notes of their own, because each capture advances the sampler's counter itself; a short note is played only to carry that counter back into step, and the split discards it.");
    }
    ui_section_end(scale);

    ui_section("Timing", scale);
    ui_label("Hold", scale);
    {
        float spacing = ImGui::GetStyle().ItemSpacing.x;
        float gap_label_w = ImGui::CalcTextSize("Gap").x;
        float slider_w = (ImGui::GetContentRegionAvail().x - gap_label_w - 2.0f * spacing) * 0.5f;
        ImGui::SetNextItemWidth(slider_w);
        ui_time_slider("##hold", &cfg->hold_seconds, UI_HOLD_MIN_SECONDS, UI_TIME_MAX_SECONDS, &g_hold_drag);
        ui_label_inline("Gap");
        ImGui::SetNextItemWidth(-1.0f);
        ui_time_slider("##gap", &cfg->gap_seconds, 0.0f, UI_TIME_MAX_SECONDS, &g_gap_drag);
    }
    ui_hint("Each note is held, then left to ring out for the gap; the sample file spans both. "
            "Drag with Shift for coarse steps, with Alt for fine ones; Ctrl-click to type seconds.");

    ui_label("Sample rate", scale);
    char rate_label[48];
    if (cfg->sample_rate > 0) {
        snprintf(rate_label, sizeof(rate_label), "%d Hz", cfg->sample_rate);
    } else if (app->audio.open) {
        snprintf(rate_label, sizeof(rate_label), "Same as device (%.0f Hz)", app->audio.sample_rate);
    } else {
        fr_strcpy(rate_label, sizeof(rate_label), "Same as device");
    }
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::BeginCombo("##rate", rate_label)) {
        if (ImGui::Selectable("Same as device", cfg->sample_rate == 0)) cfg->sample_rate = 0;
        for (int i = 0; i < RESAMPLE_RATE_OPTION_COUNT; ++i) {
            char item[32];
            snprintf(item, sizeof(item), "%d Hz", g_resample_rate_options[i]);
            if (ImGui::Selectable(item, cfg->sample_rate == g_resample_rate_options[i])) cfg->sample_rate = g_resample_rate_options[i];
        }
        ImGui::EndCombo();
    }
    ui_hint("The plugin renders at this rate, offline, and returns to the device's rate afterwards. The samples and the bank keep it.");

    ui_label("Render speed", scale);
    char speed_label[32];
    ui_speed_label(cfg->render_speed, speed_label, sizeof(speed_label));
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::BeginCombo("##speed", speed_label)) {
        for (int i = 0; i < RESAMPLE_SPEED_OPTION_COUNT; ++i) {
            int speed = g_resample_speed_options[i];
            char item[32];
            ui_speed_label(speed, item, sizeof(item));
            if (ImGui::Selectable(item, cfg->render_speed == speed)) cfg->render_speed = speed;
        }
        ImGui::EndCombo();
    }
    ui_hint("A sampler that streams from disk needs time to read its sample tails. If samples come out cut short, lower the speed.");

    ui_section_end(scale);

    if (app->settings.show_experimental) ui_tab_resample_legato(cfg, busy, scale);

    ui_section("Controllers", scale);
    ImGui::BeginDisabled(busy);
    int remove_curve = -1;
    for (int i = 0; i < cfg->cc_curve_count; ++i) {
        ResampleCcCurve* curve = &cfg->cc_curves[i];
        ImGui::PushID(i);
        char cc_label[32];
        midi_cc_label(curve->controller, cc_label, sizeof(cc_label));
        ui_label("Controller", scale);
        float layer_w = UI_LAYER_COMBO_WIDTH * scale;
        ImGui::SetNextItemWidth(-(layer_w + 32.0f * scale));
        if (ImGui::BeginCombo("##cc", cc_label)) {
            for (int cc = 0; cc <= MIDI_VALUE_MAX; ++cc) {
                char item[32];
                midi_cc_label(cc, item, sizeof(item));
                if (ImGui::Selectable(item, cc == curve->controller)) {
                    curve->controller = cc;
                    app->cc_last_controller = cc;
                }
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(!cfg->dynamic_layers);
        char layer_label[RESAMPLE_LAYER_SUFFIX_LEN + 8];
        ui_layer_label(cfg->dynamic_layers ? curve->layer : 0, layer_label, sizeof(layer_label));
        ImGui::SetNextItemWidth(layer_w);
        if (ImGui::BeginCombo("##layer", layer_label)) {
            if (ImGui::Selectable("All layers", curve->layer == 0)) curve->layer = 0;
            for (int n = 1; n <= cfg->round_robins; ++n) {
                char item[RESAMPLE_LAYER_SUFFIX_LEN];
                resample_layer_suffix(true, n, item, sizeof(item));
                if (ImGui::Selectable(item, curve->layer == n)) curve->layer = n;
            }
            ImGui::EndCombo();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("x", ImVec2(-1.0f, 0.0f))) remove_curve = i;
        ui_draw_cc_curve(curve, cfg, "##curve", scale);
        ImGui::PopID();
        ImGui::Spacing();
    }
    if (remove_curve >= 0) resample_cc_remove(cfg, remove_curve);
    ImGui::BeginDisabled(cfg->cc_curve_count >= RESAMPLE_MAX_CC_CURVES);
    if (ImGui::Button("Add CC curve", ImVec2(-1.0f, 0.0f))) {
        int controller = app_next_cc_controller(app);
        if (resample_cc_add(cfg, controller)) app->cc_last_controller = controller;
    }
    ImGui::EndDisabled();
    ImGui::EndDisabled();
    ui_hint("Each curve sends one controller over the time of every sample: the pre-roll, the held note and the gap. "
            "A new curve takes the controller last picked. "
            "Bezier: drag the nodes and the handles between them, double-click to add a node, right-click to remove one. "
            "Off: paint the curve with the mouse. Constant sets the whole curve to the slider's value. "
            "With dynamic layers, the box after the controller ties a curve to one layer; on All layers it plays in every pass.");
    if (app_legato_enabled(app)) ui_hint("A legato capture is longer than one note, and a curve stretches over the whole of it.");
    if (app->plugin && cfg->cc_curve_count > 0 && !host_plugin_info(app->plugin)->has_midi_mapping) {
        ui_colored_wrapped("This plugin publishes no MIDI controller mapping, so the curves do not reach it.",
                           CODE_COLOR_WARNING_VEC4);
    }
    ui_section_end(scale);

    ui_section("Output", scale);
    ui_target_folder_row("Folder", cfg, cfg->output_dir, &cfg->output_dir_override, busy,
                         &app->output_dir_pick_request, scale);
    ui_hint("The full WAV and the sample files go under the root project folder, in Instrument\\Articulation\\Bus, "
            "one folder per output bus. Tick the box to put them somewhere else; a bus is still the last folder.");
    ImGui::BeginDisabled(busy);
    ImGui::Checkbox("Generate split samples", &cfg->split_samples);
    ImGui::EndDisabled();
    ui_hint("Cuts the full WAV into one file per sample. The FSI + FSB bank is cut from that same WAV, "
            "so a run that only exports to FastSampler does not need these files.");
    ImGui::BeginDisabled(busy);
    if (ImGui::Button("1. Build MIDI", ImVec2(-1.0f, 0.0f))) app_resample_build(app);
    ImGui::EndDisabled();

    if (rendering) {
        const ResampleScript* s = &app->resample_script;
        char overlay[64];
        float fraction;
        if (app->resample_job.phase.load() == RESAMPLE_PHASE_SPLIT) {
            int done = app->resample_job.split_done.load();
            int total = s->segment_count * app->resample_job.bus_count;
            fraction = total > 0 ? (float)done / (float)total : 0.0f;
            snprintf(overlay, sizeof(overlay), "Splitting %d / %d", done, total);
        } else {
            fraction = s->total_frames > 0 ? (float)app->resample_job.progress_frames.load() / (float)s->total_frames : 0.0f;
            snprintf(overlay, sizeof(overlay), "Rendering %.0f%%", fraction * 100.0f);
        }
        ImGui::ProgressBar(fraction, ImVec2(-1.0f, 0.0f), overlay);
        if (ui_red_button("Cancel", ImVec2(-1.0f, 0.0f))) resample_cancel(&app->resample_job);
    } else {
        bool can_render = app->plugin != nullptr && app->resample_script.built && !building;
        ImGui::BeginDisabled(!can_render);
        if (ImGui::Button("2. Render to WAV", ImVec2(-1.0f, 0.0f))) app->resample_start_request = true;
        ImGui::EndDisabled();
        if (!app->plugin) {
            ui_hint("Load a plugin to render.");
        } else if (!app->resample_script.built) {
            ui_hint("Build the MIDI to render.");
        } else if (cfg->split_samples) {
            ui_hint("Renders faster than realtime with the speakers silent, then splits the file into one WAV per sample.");
        } else {
            ui_hint("Renders faster than realtime with the speakers silent, into the one long WAV the bank is built from.");
        }
    }
    ui_section_end(scale);

    ui_section("Export to FastSampler", scale);
    ui_label("Preload", scale);
    char preload_label[32];
    resample_preload_label(cfg->preload_frames, preload_label, sizeof(preload_label));
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::BeginCombo("##preload", preload_label)) {
        for (int i = 0; i < RESAMPLE_PRELOAD_OPTION_COUNT; ++i) {
            int frames = g_resample_preload_options[i];
            char item[32];
            resample_preload_label(frames, item, sizeof(item));
            if (ImGui::Selectable(item, cfg->preload_frames == frames)) cfg->preload_frames = frames;
        }
        ImGui::EndCombo();
    }
    ui_hint("How much of every sample the bank keeps in RAM; the rest streams from disk while it plays. RAM holds every sample whole.");
    ui_label("Bit depth", scale);
    char depth_label[16];
    snprintf(depth_label, sizeof(depth_label), "%d-bit", cfg->bit_depth);
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::BeginCombo("##bit_depth", depth_label)) {
        for (int i = 0; i < RESAMPLE_BIT_DEPTH_OPTION_COUNT; ++i) {
            int bits = g_resample_bit_depth_options[i];
            char item[16];
            snprintf(item, sizeof(item), "%d-bit", bits);
            if (ImGui::Selectable(item, cfg->bit_depth == bits)) cfg->bit_depth = bits;
        }
        ImGui::EndCombo();
    }
    ui_hint("What every sample is stored at, in the preload block and in the streams both. "
            "16-bit halves the bank; 24-bit keeps the render's own depth.");
    ui_target_folder_row("Folder", cfg, cfg->export_dir, &cfg->export_dir_override, busy,
                         &app->export_dir_pick_request, scale);
    ui_hint("The .fsi and .fsb go under the root project folder as well, beside the samples, one instrument "
            "per output bus. Tick the box to put them somewhere else; a bus is still the last folder.");
    ImGui::BeginDisabled(busy);
    ImGui::Checkbox("Export .rzc files", &cfg->export_rzc);
    ImGui::EndDisabled();
    ui_hint("Writes one readable zone collection file per collection of the instrument beside it, the same mapping in "
            "FastSampler's editable text format. Its zones name the split sample files, so it needs those.");
    if (cfg->export_rzc && !cfg->split_samples) {
        ui_colored_wrapped("A .rzc names the split sample files, and Generate split samples is off, so none would be written.",
                           CODE_COLOR_WARNING_VEC4);
    }
    ImGui::BeginDisabled(busy);
    ImGui::Checkbox("Remove full WAV", &cfg->delete_full_wav);
    ImGui::EndDisabled();
    ui_hint("Deletes the one long WAV the samples and the bank are both cut from, once the bank has been built from it, "
            "so the folder keeps only the samples and the instrument.");
    if (building) {
        int done = app->instrument_job.progress.load();
        int total = app->instrument_job.progress_total.load();
        int targets = app->instrument_job.target_count;
        char overlay[64];
        if (targets > 1) {
            int bus = app->instrument_job.bus_done.load() + 1;
            if (bus > targets) bus = targets;
            snprintf(overlay, sizeof(overlay), "Building instrument %d / %d of bus %d / %d", done, total, bus, targets);
        } else {
            snprintf(overlay, sizeof(overlay), "Building instrument %d / %d", done, total);
        }
        ImGui::ProgressBar(total > 0 ? (float)done / (float)total : 0.0f, ImVec2(-1.0f, 0.0f), overlay);
        if (ui_red_button("Cancel##instrument", ImVec2(-1.0f, 0.0f))) instrument_cancel(&app->instrument_job);
    } else {
        ImGui::BeginDisabled(!app->resample_fsi_ready || rendering);
        if (ImGui::Button("Build FSI + FSB", ImVec2(-1.0f, 0.0f))) app->instrument_start_request = true;
        ImGui::EndDisabled();
        if (app->resample_fsi_ready && app->resample_script.legato) {
            ui_hint("Maps the samples across keys and velocities in the folder above, and the moves with them: one collection "
                    "per interval, reached from the notes through FastSampler's legato effect, each move cut one margin "
                    "before it leaves its note and one margin past where it lands on the next. Every move carries those "
                    "two frames, and the sampler places its crossfades from them. Both come off the clock, or out of the "
                    "audio when Detect is on.");
        } else if (app->resample_fsi_ready && app->resample_script.dynamic_layers) {
            ui_hint("Maps the samples across keys and velocities into a FastSampler instrument in the folder above: one looped zone "
                    "collection per dynamic layer and one of release samples for each. The layers' gain is driven by their "
                    "controller through curves that crossfade them, equal power, from dyn1 at the bottom to the last at the top, "
                    "a spectral morph on the same controller carries each layer's tone into the next, and Smart release starts "
                    "every release at the level its layer was sounding at.");
        } else if (app->resample_fsi_ready) {
            ui_hint("Maps the samples across keys and velocities, one zone collection per round robin, into a FastSampler instrument in the folder above.");
        } else if (app->resample_full_wav_removed) {
            ui_hint("The bank was built and the full WAV removed with it. Render again to build another one.");
        } else if (cfg->split_samples) {
            ui_hint("Available once the samples are rendered and split.");
        } else {
            ui_hint("Available once the WAV is rendered.");
        }
    }
    ui_section_end(scale);

    ui_section("Projects", scale);
    ui_project_browser(app, busy);
    const ProjectBrowser* browser = &app->project_browser;
    bool project_selected = browser->selected >= 0 && browser->selected < browser->count &&
                            !browser->entries[browser->selected].is_dir;
    ImGui::BeginDisabled(busy || !project_selected);
    if (ImGui::Button(ICON_LC_FOLDER_OPEN "  Open project", ImVec2(-1.0f, 0.0f))) {
        project_browser_entry_path(browser, browser->selected, app->project_load_path, sizeof(app->project_load_path));
        app->project_load_request = true;
    }
    ImGui::EndDisabled();
    ui_hint("Double-click a project to open it, or a folder to go into it. Opening puts every setting of this tab back, "
            "the folders with them, then loads the plugin the project was saved with and gives it its state again. "
            "Build the MIDI again after opening a project.");

    ui_label("Name", scale);
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputText("##projectname", app->settings.project_name, sizeof(app->settings.project_name));
    ImGui::BeginDisabled(busy || app->settings.project_name[0] == '\0');
    if (ImGui::Button(ICON_LC_SAVE "  Save project", ImVec2(-1.0f, 0.0f))) app->project_save_request = true;
    ImGui::EndDisabled();
    ui_hint("Writes every setting of this tab to <name>.yaml in the folder shown above, and the loaded plugin as it stands "
            "-- its patch, its parameters and the outputs it is routed to -- to <name>.fstate beside it. "
            "A project of the same name is replaced.");
    ui_section_end(scale);
}

static void ui_tab_settings(App* app, float scale) {
    ui_section("Plugin folders", scale);
    int remove_index = -1;
    for (int i = 0; i < app->settings.scan_path_count; ++i) {
        char id[32];
        snprintf(id, sizeof(id), "x##path%d", i);
        if (ImGui::SmallButton(id)) remove_index = i;
        ImGui::SameLine();
        ImGui::TextWrapped("%s", app->settings.scan_paths[i]);
    }
    if (remove_index >= 0) {
        settings_remove_scan_path(&app->settings, remove_index);
        app->rescan_request = true;
    }
    ui_label("Add folder", scale);
    ImGui::SetNextItemWidth(-50.0f * scale);
    bool submitted = ImGui::InputTextWithHint("##newpath", "Folder to scan", app->new_scan_path,
                                              sizeof(app->new_scan_path), ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    if (ImGui::Button("Add", ImVec2(-1.0f, 0.0f)) || submitted) {
        if (!plat_path_is_dir(app->new_scan_path)) {
            app_set_status(app, true, "Not a folder: %s", app->new_scan_path);
        } else if (settings_add_scan_path(&app->settings, app->new_scan_path)) {
            app->new_scan_path[0] = '\0';
            app->rescan_request = true;
        } else {
            app_set_status(app, true, "That folder is already in the list.");
        }
    }
    if (ImGui::Button("Rescan plugins", ImVec2(-1.0f, 0.0f))) app->rescan_request = true;
    ui_hint("The folders are searched for VST3 plugins and for VST 2 instruments (.dll). A new DLL is opened once "
            "to see whether it is an instrument; the answer is kept in fastresampler_vsti_cache.txt next to the exe.");
    ui_section_end(scale);

    ui_section("Audio output", scale);
    const char* driver_preview = "No devices";
    char preview_buf[FR_DRIVER_NAME_LEN + 16];
    if (app->pending_driver >= 0 && app->pending_driver < app->drivers.count) {
        const AudioDriverInfo* d = &app->drivers.drivers[app->pending_driver];
        snprintf(preview_buf, sizeof(preview_buf), "%s: %s", audio_api_name(d->api), d->name);
        driver_preview = preview_buf;
    }
    ui_label("Device", scale);
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::BeginCombo("##driver", driver_preview)) {
        for (int i = 0; i < app->drivers.count; ++i) {
            const AudioDriverInfo* d = &app->drivers.drivers[i];
            char item[FR_DRIVER_NAME_LEN + 32];
            snprintf(item, sizeof(item), "%s: %s##drv%d", audio_api_name(d->api), d->name, i);
            if (ImGui::Selectable(item, i == app->pending_driver)) app->pending_driver = i;
        }
        ImGui::EndCombo();
    }

    char block_label[32];
    snprintf(block_label, sizeof(block_label), "%d frames", g_block_size_options[app->pending_block_index]);
    ui_label("Block size", scale);
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::BeginCombo("##block", block_label)) {
        for (int i = 0; i < FR_BLOCK_SIZE_OPTION_COUNT; ++i) {
            char item[32];
            snprintf(item, sizeof(item), "%d frames", g_block_size_options[i]);
            if (ImGui::Selectable(item, i == app->pending_block_index)) app->pending_block_index = i;
        }
        ImGui::EndCombo();
    }
    ui_hint("ASIO drivers may round the block to what they support.");

    if (ui_green_button("Apply", ImVec2(-1.0f, 0.0f))) app->audio_apply_request = true;
    if (ImGui::Button("Refresh devices", ImVec2(-1.0f, 0.0f))) app->audio_refresh_request = true;
    if (audio_has_control_panel(&app->audio)) {
        if (ImGui::Button("ASIO control panel", ImVec2(-1.0f, 0.0f))) audio_control_panel(&app->audio);
    }
    if (app->audio.open) {
        ImGui::Text("Running: %s", app->audio.name);
        ImGui::Text("%.0f Hz, %d frames per block", app->audio.sample_rate, app->audio.block_size);
        ImGui::Text("Output latency: %d frames (%.1f ms)", app->audio.latency_frames,
                    1000.0 * (double)app->audio.latency_frames / app->audio.sample_rate);
    } else {
        ui_colored_wrapped("No audio device is open.", CODE_COLOR_WARNING_VEC4);
    }
    ui_section_end(scale);

    ui_section("Interface", scale);
    ui_label("Show tips", scale);
    ImGui::Checkbox("##tips", &app->settings.show_tips);
    ui_hint("The dim lines that explain a control, like this one.");
    ImGui::Checkbox("Show experimental features", &app->settings.show_experimental);
    ui_hint("Features that are still in development and may change or give unexpected results. "
            "Turns on the capture of legato transitions in the Resample tab.");
    if (ImGui::Button("Show disclaimer", ImVec2(-1.0f, 0.0f))) app->disclaimer_open_request = true;
    ui_section_end(scale);
}

static void ui_reflow(const char* text, char* out, size_t out_len) {
    size_t o = 0;
    const char* s = text;
    while (*s && o + 2 < out_len) {
        if (*s != '\n') {
            out[o++] = *s++;
            continue;
        }
        if (s[1] == '\n') {
            out[o++] = '\n';
            out[o++] = '\n';
            while (*s == '\n') ++s;
            while (*s == ' ') ++s;
            continue;
        }
        const char* t = s + 1;
        while (*t == ' ') ++t;
        bool item = *t == '*' || (*t >= '0' && *t <= '9' && t[1] == '.');
        out[o++] = item ? '\n' : ' ';
        s = t;
    }
    out[o] = '\0';
}

static void ui_tab_licenses(float scale) {
    ui_section("Third-party software", scale);
    ui_note("FastResampler is built with the software below. Open an entry to read its license notice.");
    ImGui::Spacing();
    char text[4096];
    for (int i = 0; i < THIRD_PARTY_LICENSE_COUNT; ++i) {
        const ThirdPartyLicense* l = &g_third_party_licenses[i];
        char header[192];
        if (l->license[0]) {
            snprintf(header, sizeof(header), "%s (%s)##license%d", l->name, l->license, i);
        } else {
            snprintf(header, sizeof(header), "%s##license%d", l->name, i);
        }
        if (!ImGui::CollapsingHeader(header)) continue;
        if (l->usage[0]) ui_note(l->usage);
        if (l->copyright[0]) ImGui::TextWrapped("%s", l->copyright);
        ui_reflow(l->text, text, sizeof(text));
        ImGui::TextWrapped("%s", text);
        ImGui::Spacing();
    }
    ui_section_end(scale);
}

static void ui_draw_side_panel(App* app, ImVec2 size, float scale) {
    ImGui::BeginChild("##sidepanel", size, ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar);

    float status_h = UI_STATUS_HEIGHT * scale;
    ImVec2 tabs_size = ImVec2(0.0f, ImGui::GetContentRegionAvail().y - status_h);
    ImGui::BeginChild("##tabs", tabs_size, ImGuiChildFlags_None, ImGuiWindowFlags_None);
    int wanted = app->side_tab_select_request ? app->settings.side_tab : -1;
    app->side_tab_select_request = false;
    int shown = -1;
    if (ImGui::BeginTabBar("##sidetabs")) {
        static const char* const labels[] = { "Plugin", "MIDI", "Resample", "Settings", "Licenses" };
        for (int tab = APP_TAB_PLUGIN; tab <= APP_TAB_LICENSES; ++tab) {
            ImGuiTabItemFlags flags = wanted == tab ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
            if (!ImGui::BeginTabItem(labels[tab], nullptr, flags)) continue;
            shown = tab;
            switch (tab) {
                case APP_TAB_PLUGIN:   ui_tab_plugin(app, scale); break;
                case APP_TAB_MIDI:     ui_tab_midi(app, scale); break;
                case APP_TAB_RESAMPLE: ui_tab_resample(app, scale); break;
                case APP_TAB_SETTINGS: ui_tab_settings(app, scale); break;
                default:               ui_tab_licenses(scale); break;
            }
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::EndChild();
    if (shown >= 0) {
        if (shown == APP_TAB_RESAMPLE && app->active_tab != APP_TAB_RESAMPLE && app->project_browser.dir[0]) {
            ui_project_browse(app, app->project_browser.dir);
        }
        app->active_tab = shown;
        app->settings.side_tab = shown;
    }

    ImGui::Separator();
    ui_colored_wrapped(app->status, app->status_is_error ? CODE_COLOR_RED_VEC4 : CODE_COLOR_SLOT_LABEL_TEXT);

    ImGui::EndChild();
}

static int g_ui_log_written = -1;

static float g_ui_log_grip_carry = 0.0f;

static void ui_log_grip(App* app, float scale) {
    float grip_h = UI_LOG_GRIP_HEIGHT * scale;
    ImGui::SetCursorPos(ImVec2(0.0f, 0.0f));
    ImGui::InvisibleButton("##loggrip", ImVec2(ImGui::GetWindowWidth(), grip_h));
    bool live = app->settings.log_expanded;
    if (live && (ImGui::IsItemHovered() || ImGui::IsItemActive())) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
    if (ImGui::IsItemActivated()) g_ui_log_grip_carry = 0.0f;
    if (live && ImGui::IsItemActive()) {
        float want = g_ui_log_grip_carry - ImGui::GetIO().MouseDelta.y / scale;
        int step = (int)want;
        g_ui_log_grip_carry = want - (float)step;
        if (step != 0) {
            app->settings.log_height = fr_clampi(app->settings.log_height + step,
                                                 (int)UI_LOG_MIN_BODY_HEIGHT, (int)UI_LOG_MAX_BODY_HEIGHT);
        }
    }
    if (live) {
        ImU32 color = ImGui::IsItemActive() ? CODE_COLOR_SECTION_U32
                    : ImGui::IsItemHovered() ? ImGui::GetColorU32(CODE_COLOR_SLOT_LABEL_TEXT)
                                             : CODE_COLOR_BORDER_U32;
        ImVec2 lo = ImGui::GetItemRectMin();
        ImVec2 hi = ImGui::GetItemRectMax();
        float middle = (lo.x + hi.x) * 0.5f;
        float half = 20.0f * scale;
        float y = (lo.y + hi.y) * 0.5f;
        ImGui::GetWindowDrawList()->AddLine(ImVec2(middle - half, y), ImVec2(middle + half, y), color, 2.0f * scale);
    }
    ImGui::SetCursorPos(ImVec2(ImGui::GetStyle().WindowPadding.x, grip_h));
}

static void ui_draw_log_lines(App* app) {
    ImGui::BeginChild("##loglines", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
    if (app->log_count == 0) {
        ui_note("Nothing yet. What a render, a build or a save reports comes here.");
    }
    for (int i = 0; i < app->log_count; ++i) {
        const AppLogLine* line = app_log_line(app, i);
        ImGui::PushStyleColor(ImGuiCol_Text, CODE_COLOR_SLOT_LABEL_TEXT);
        ImGui::TextUnformatted(line->time);
        ImGui::PopStyleColor();
        ImGui::SameLine();
        if (line->is_error) {
            ImGui::TextColored(CODE_COLOR_RED_VEC4, "%s", line->text);
        } else {
            ImGui::TextUnformatted(line->text);
        }
    }
    if (g_ui_log_written != app->log_written) {
        g_ui_log_written = app->log_written;
        ImGui::SetScrollHereY(1.0f);
    }
    ImGui::EndChild();
}

static void ui_draw_bus_meter(App* app, int bus, float width, float scale) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float bar_h = UI_METER_HEIGHT * scale;
    float gap = 2.0f * scale;
    ImVec2 origin = ImGui::GetCursorScreenPos();
    for (int c = 0; c < FR_BUS_CHANNELS; ++c) {
        ImVec2 pos = ImVec2(origin.x, origin.y + (float)c * (bar_h + gap));
        ImVec2 end = ImVec2(pos.x + width, pos.y + bar_h);
        dl->AddRectFilled(pos, end, CODE_COLOR_PANEL_BG_U32, 2.0f);
        float db = ui_db(app->bus_meter[bus][c]);
        float fill = (db - UI_METER_MIN_DB) / (0.0f - UI_METER_MIN_DB);
        if (fill > 1.0f) fill = 1.0f;
        if (fill > 0.0f) dl->AddRectFilled(pos, ImVec2(pos.x + width * fill, end.y), ui_meter_color(db), 2.0f);
        dl->AddRect(pos, end, CODE_COLOR_BORDER_U32, 2.0f);
    }
    ImGui::Dummy(ImVec2(width, FR_BUS_CHANNELS * bar_h + gap));
}

static void ui_draw_bus_tab(App* app, float scale) {
    ResampleConfig* cfg = &app->settings.resample;
    bool busy = app_resample_running(app) || app_instrument_running(app);

    ImGui::BeginChild("##buslist", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None, ImGuiWindowFlags_None);
    ui_hint("Every bus is one stereo pair of the plugin's outputs and becomes a folder of its own, "
            "with its own samples and its own instrument. Route each microphone to its pair in the plugin, "
            "then name the bus to match. Play a note and the meters show which bus the plugin sends it to.");

    int remove = -1;
    float meter_w = 90.0f * scale;
    float pair_w = 90.0f * scale;
    float button_w = 28.0f * scale;
    for (int i = 0; i < cfg->bus_count; ++i) {
        ImGui::PushID(i);
        bool name_clash = resample_bus_name_clashes(cfg, i);
        bool pair_clash = resample_bus_pair_clashes(cfg, i);

        bool is_monitor = cfg->monitor_bus == i;
        if (ImGui::RadioButton("##monitor", is_monitor)) cfg->monitor_bus = i;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Play this bus through the speakers");
        ImGui::SameLine();

        float avail = ImGui::GetContentRegionAvail().x;
        float spacing = ImGui::GetStyle().ItemSpacing.x;
        float rest = avail - pair_w - button_w - 3.0f * spacing;
        float name_w = 260.0f * scale;
        if (name_w > rest - meter_w) name_w = rest - meter_w;
        if (name_w < 60.0f * scale) name_w = 60.0f * scale;
        float bar_w = rest - name_w;
        if (bar_w < meter_w) bar_w = meter_w;

        ImGui::BeginDisabled(busy);
        if (name_clash) ImGui::PushStyleColor(ImGuiCol_Text, CODE_COLOR_RED_VEC4);
        ImGui::SetNextItemWidth(name_w);
        ImGui::InputText("##name", cfg->busses[i].name, sizeof(cfg->busses[i].name));
        if (name_clash) ImGui::PopStyleColor();
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("The microphone's name: its folder, and the start of every file it writes");
        ImGui::SameLine();

        char pair_label[16];
        resample_bus_pair_label(cfg->busses[i].channel, pair_label, sizeof(pair_label));
        ImGui::BeginDisabled(busy);
        if (pair_clash) ImGui::PushStyleColor(ImGuiCol_Text, CODE_COLOR_RED_VEC4);
        ImGui::SetNextItemWidth(pair_w);
        if (ImGui::BeginCombo("##pair", pair_label)) {
            for (int c = 0; c < FR_MAX_RENDER_CHANNELS; c += FR_BUS_CHANNELS) {
                char item[16];
                resample_bus_pair_label(c, item, sizeof(item));
                if (ImGui::Selectable(item, cfg->busses[i].channel == c)) cfg->busses[i].channel = c;
            }
            ImGui::EndCombo();
        }
        if (pair_clash) ImGui::PopStyleColor();
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("The plugin's own outputs this bus reads");
        ImGui::SameLine();

        ui_draw_bus_meter(app, i, bar_w, scale);
        ImGui::SameLine();

        ImGui::BeginDisabled(busy || cfg->bus_count <= 1);
        if (ui_red_button("X", ImVec2(button_w, 0.0f))) remove = i;
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered() && cfg->bus_count > 1) ImGui::SetTooltip("Remove this bus");

        if (name_clash) {
            ui_colored_wrapped("Name this bus something no other bus is called: the name is its folder.",
                               CODE_COLOR_WARNING_VEC4);
        }
        if (pair_clash) {
            ui_colored_wrapped("Another bus reads this pair as well. Move one of them.", CODE_COLOR_WARNING_VEC4);
        }
        ImGui::PopID();
    }
    if (remove >= 0) resample_bus_remove(cfg, remove);

    ImGui::BeginDisabled(busy || cfg->bus_count >= FR_MAX_OUTPUT_BUSSES);
    if (ImGui::Button("Add output bus", ImVec2(150.0f * scale, 0.0f))) resample_bus_add(cfg);
    ImGui::EndDisabled();
    if (cfg->bus_count >= FR_MAX_OUTPUT_BUSSES) {
        ImGui::SameLine();
        ImGui::TextDisabled("%d is the most", FR_MAX_OUTPUT_BUSSES);
    }

    if (app->plugin) {
        const HostPluginInfo* info = host_plugin_info(app->plugin);
        int needed = resample_bus_channel_count(cfg);
        char line[160];
        snprintf(line, sizeof(line), "%s has %d output channel%s over %d bus%s; the busses read %d.",
                 info->name, info->total_output_channels, info->total_output_channels == 1 ? "" : "s",
                 info->output_bus_count, info->output_bus_count == 1 ? "" : "ses", needed);
        if (needed > info->total_output_channels) {
            ui_colored_wrapped(line, CODE_COLOR_WARNING_VEC4);
            ui_colored_wrapped("A bus past the plugin's own outputs records silence. Move it, or route the plugin to more outputs.",
                               CODE_COLOR_WARNING_VEC4);
        } else {
            ui_note(line);
        }
    } else {
        ui_note("Load a plugin to see how many outputs it offers.");
    }
    ImGui::EndChild();
}

static void ui_draw_log(App* app, ImVec2 size, float scale) {
    ImGui::BeginChild("##log", size, ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar);

    ui_log_grip(app, scale);
    bool expanded = app->settings.log_expanded;
    if (ImGui::ArrowButton("##logfold", expanded ? ImGuiDir_Down : ImGuiDir_Up)) {
        app->settings.log_expanded = !expanded;
    }
    ImGui::SameLine();

    bool show_log = app->settings.log_tab == APP_LOG_TAB_LOG;
    int wanted = app->log_tab_select_request ? app->settings.log_tab : -1;
    app->log_tab_select_request = false;
    if (ImGui::BeginTabBar("##logtabs", ImGuiTabBarFlags_None)) {
        ImGuiTabItemFlags log_flags = wanted == APP_LOG_TAB_LOG ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
        if (ImGui::BeginTabItem("Output log", nullptr, log_flags)) {
            app->settings.log_tab = APP_LOG_TAB_LOG;
            show_log = true;
            ImGui::EndTabItem();
        }
        ImGuiTabItemFlags bus_flags = wanted == APP_LOG_TAB_BUSSES ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
        if (ImGui::BeginTabItem("Output busses", nullptr, bus_flags)) {
            app->settings.log_tab = APP_LOG_TAB_BUSSES;
            show_log = false;
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    if (show_log) {
        float clear_w = 56.0f * scale;
        ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - ImGui::GetWindowContentRegionMin().x - clear_w);
        ImGui::BeginDisabled(app->log_count == 0);
        if (ImGui::Button("Clear", ImVec2(clear_w, 0.0f))) app->log_clear_request = true;
        ImGui::EndDisabled();
    } else {
        ImGui::SameLine();
        ImGui::TextDisabled("%d bus%s", app->settings.resample.bus_count,
                            app->settings.resample.bus_count == 1 ? "" : "ses");
    }

    if (app->settings.log_expanded) {
        if (show_log) {
            ui_draw_log_lines(app);
        } else {
            ui_draw_bus_tab(app, scale);
        }
    }

    ImGui::EndChild();
}

static void ui_unban_items(App* app, int format) {
    Settings* s = &app->settings;
    char unban[FR_MAX_PATH];
    unban[0] = '\0';
    int shown = 0;
    for (int i = 0; i < s->banned_count; ++i) {
        const char* path = s->banned_paths[i];
        if (host_format_of_path(path) != format) continue;
        char name[FR_PLUGIN_NAME_LEN];
        fr_strcpy(name, sizeof(name), plat_path_basename(path));
        fr_strip_plugin_ext(name);
        ImGui::PushID(i);
        if (ImGui::MenuItem(name)) fr_strcpy(unban, sizeof(unban), path);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", path);
        ImGui::PopID();
        shown++;
    }
    if (shown == 0) ImGui::MenuItem("No banned plugins", nullptr, false, false);
    if (unban[0]) {
        settings_unban(s, unban);
        app->rescan_request = true;
    }
}

static void ui_draw_plugin_list(App* app, int format, float scale) {
    const PluginList* list = &app->plugins[format];
    int* selected = &app->selected_plugin[format];
    bool vsti = format == HOST_FORMAT_VST2;
    ImGui::Text(vsti ? "Available VST instruments (%d)" : "Available VST3 plugins (%d)", list->count);
    ImGui::SameLine();
    if (ImGui::SmallButton("Rescan")) app->rescan_request = true;
    ImGui::SameLine();
    if (ImGui::SmallButton("Banned")) ImGui::OpenPopup("##banned");
    if (ImGui::BeginPopup("##banned")) {
        ui_unban_items(app, format);
        ImGui::EndPopup();
    }
    if (*selected >= 0 && *selected < list->count) {
        ImGui::SameLine();
        if (ImGui::SmallButton("Load selected")) {
            app->load_request = *selected;
            app->load_request_format = format;
        }
    }
    ImGui::Separator();

    if (list->count == 0) {
        if (vsti) {
            ui_note("No VST instruments found. Add the folder that holds the VST 2 DLLs in the Settings tab. "
                    "Effects are left out; only instruments are listed.");
        } else {
            ui_note("No plugins found. Add a folder in the Settings tab.");
        }
        ui_hint("A banned plugin is not listed; the Banned button puts one back.");
        return;
    }

    ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollY |
                            ImGuiTableFlags_Resizable;
    if (ImGui::BeginTable(vsti ? "##vsti" : "##vst3", 2, flags)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn(vsti ? "Instrument" : "Plugin", ImGuiTableColumnFlags_WidthFixed, 260.0f * scale);
        ImGui::TableSetupColumn("Path", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();
        for (int i = 0; i < list->count; ++i) {
            const PluginEntry* e = &list->entries[i];
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            char id[FR_PLUGIN_NAME_LEN + 16];
            snprintf(id, sizeof(id), "%s##plug%d", e->name, i);
            ImGuiSelectableFlags sel_flags = ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick;
            if (ImGui::Selectable(id, *selected == i, sel_flags)) {
                *selected = i;
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    app->load_request = i;
                    app->load_request_format = format;
                }
            }
            if (ImGui::BeginPopupContextItem()) {
                char ban_label[FR_PLUGIN_NAME_LEN + 16];
                snprintf(ban_label, sizeof(ban_label), "Ban %s", e->name);
                if (ImGui::MenuItem(ban_label)) {
                    settings_ban(&app->settings, e->path);
                    app->rescan_request = true;
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Takes it off the list; it is not scanned or loaded again until unbanned.");
                if (ImGui::BeginMenu("Unban")) {
                    ui_unban_items(app, format);
                    ImGui::EndMenu();
                }
                ImGui::EndPopup();
            }
            ImGui::TableSetColumnIndex(1);
            ImGui::TextDisabled("%s", e->path);
        }
        ImGui::EndTable();
    }
}

static void ui_draw_plugin_tabs(App* app, float scale) {
    if (!ImGui::BeginTabBar("##formats")) return;
    int wanted = app->plugin_tab_select_request ? app->settings.plugin_tab : -1;
    app->plugin_tab_select_request = false;
    for (int format = 0; format < HOST_FORMAT_COUNT; ++format) {
        ImGuiTabItemFlags flags = wanted == format ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
        if (ImGui::BeginTabItem(host_format_name(format), nullptr, flags)) {
            app->settings.plugin_tab = format;
            ui_draw_plugin_list(app, format, scale);
            ImGui::EndTabItem();
        }
    }
    ImGui::EndTabBar();
}

static void ui_scan_progress_bar(App* app, float width) {
    const ScanJob* job = &app->scan;
    float fraction = scan_progress(job);
    char overlay[96];
    if (fraction < 0.0f) {
        int found = app->plugins[HOST_FORMAT_VST3].count + app->plugins[HOST_FORMAT_VST2].count;
        snprintf(overlay, sizeof(overlay), "Searching the plugin folders: %d found", found);
        ImGui::ProgressBar(-1.0f * (float)ImGui::GetTime(), ImVec2(width, 0.0f), overlay);
    } else {
        snprintf(overlay, sizeof(overlay), "Checking VST 2 plugins: %d / %d", job->probe_index, job->probe_total);
        ImGui::ProgressBar(fraction, ImVec2(width, 0.0f), overlay);
    }
}

static void ui_draw_scan_panel(App* app, float scale) {
    ImVec2 avail = ImGui::GetContentRegionAvail();
    float width = UI_SCAN_PANEL_WIDTH * scale;
    if (width > avail.x - 2.0f * UI_PANEL_PADDING * scale) width = avail.x - 2.0f * UI_PANEL_PADDING * scale;
    if (width < 1.0f) width = 1.0f;
    ImVec2 cursor = ImGui::GetCursorPos();
    ImGui::SetCursorPos(ImVec2(cursor.x + (avail.x - width) * 0.5f, cursor.y + avail.y * 0.25f));
    ImGui::BeginChild("##scan", ImVec2(width, 0.0f), ImGuiChildFlags_AutoResizeY, ImGuiWindowFlags_NoScrollbar);
    ui_section("Scanning plugins", scale);
    ui_scan_progress_bar(app, -1.0f);
    if (app->scan.current[0]) {
        char fitted[FR_MAX_PATH + 8];
        ui_fit_path(app->scan.current, ImGui::GetContentRegionAvail().x, fitted, sizeof(fitted));
        ImGui::PushStyleColor(ImGuiCol_Text, CODE_COLOR_SLOT_LABEL_TEXT);
        ImGui::TextUnformatted(fitted);
        ImGui::PopStyleColor();
    } else {
        ImGui::Spacing();
    }
    ImGui::Spacing();
    ui_note("The folders in the Settings tab are searched for VST3 plugins and VST 2 instruments. A VST 2 DLL that "
            "has not been seen before is opened once to find out whether it is an instrument. The answer is kept, "
            "so the next start is quick.");
    ImGui::EndChild();
}

static void ui_draw_plugin_header(App* app, float scale) {
    const HostPluginInfo* info = host_plugin_info(app->plugin);
    ImGui::AlignTextToFramePadding();
    ImGui::Text("%s", info->name);
    ImGui::SameLine();
    ImGui::TextDisabled("by %s", info->vendor);
    if (scan_running(&app->scan)) {
        ImGui::SameLine();
        ui_scan_progress_bar(app, UI_SCAN_HEADER_BAR_WIDTH * scale);
    }
    float close_w = 110.0f * scale;
    ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - close_w);
    if (ui_red_button("Close plugin", ImVec2(close_w, 0.0f))) app->unload_request = true;
    if (!app->editor_open) {
        ui_hint("This plugin has no editor. Play it from the keyboard below.");
    }
}

static void ui_draw_main_area(App* app, ImVec2 size, float scale) {
    ImGui::BeginChild("##main", size, ImGuiChildFlags_Borders, ImGuiWindowFlags_None);
    if (app->plugin) {
        ui_draw_plugin_header(app, scale);
    } else if (scan_running(&app->scan)) {
        ui_draw_scan_panel(app, scale);
    } else {
        ui_draw_plugin_tabs(app, scale);
    }
    ImGui::EndChild();
}

struct UiDisclaimerSection {
    const char* title;
    const char* body;
};

static const UiDisclaimerSection g_ui_disclaimer[] = {
    { "1. Provided as is",
      "FastResampler is free, open-source software provided \"as is\" and \"as available\", without warranty of any "
      "kind, express or implied, including but not limited to the warranties of merchantability, fitness for a "
      "particular purpose, accuracy and non-infringement. You use it entirely at your own risk, including the risk of "
      "data loss, damage to your system and loss of work." },
    { "2. No liability",
      "To the fullest extent permitted by applicable law, in no event shall the authors, contributors or copyright "
      "holders of FastResampler be liable for any claim, damages or other liability, whether direct, indirect, "
      "incidental, special, exemplary or consequential, including but not limited to loss of data, revenue or "
      "profits and claims by third parties, whether in an action of contract, tort or otherwise, arising from, out of "
      "or in connection with the software or its use, even if advised of the possibility of such damages." },
    { "3. Licenses of third-party instruments",
      "FastResampler records the audio output of plugins and sample libraries made by third parties. Those products "
      "and the sounds they produce are protected by copyright and governed by their own license agreements (EULAs). "
      "Many of these agreements prohibit resampling, re-recording, converting or redistributing their sounds, or "
      "using them to create another sample library or virtual instrument. You must not use FastResampler with any "
      "plugin, sample library or other content whose license does not allow it. It is solely your responsibility to "
      "read, understand and comply with the license of every product you use with FastResampler. FastResampler "
      "grants you no rights in third-party content, and its authors are not a party to your agreements with third "
      "parties." },
    { "4. Your responsibility",
      "By using FastResampler you accept full and personal responsibility for how you use it and for everything you "
      "create, store or distribute with it, and you personally assume all liability arising from that use. You agree "
      "to indemnify, defend and hold harmless the authors, contributors and copyright holders of FastResampler from "
      "and against any and all claims, damages, losses, liabilities, costs and expenses, including reasonable legal "
      "fees, arising from your use of the software, your breach of any license agreement or of this notice, or your "
      "infringement of any right of a third party." },
    { "5. General",
      "This notice applies in addition to the license under which FastResampler is distributed. If any part of it is "
      "held to be unenforceable, the remaining parts stay in full effect. Nothing in it limits or excludes any "
      "liability that cannot be limited or excluded under applicable law." },
};

static bool g_ui_disclaimer_hide = false;
static bool g_ui_disclaimer_countdown = false;
static double g_ui_disclaimer_opened_at = 0.0;

static bool ui_draw_disclaimer(App* app, float width, float height, float scale) {
    if (app->disclaimer_open_request && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId)) {
        app->disclaimer_open_request = false;
        g_ui_disclaimer_hide = app->settings.disclaimer_hidden;
        g_ui_disclaimer_countdown = !app->settings.disclaimer_accepted;
        g_ui_disclaimer_opened_at = ImGui::GetTime();
        ImGui::OpenPopup("Disclaimer");
    }
    float modal_w = UI_DISCLAIMER_WIDTH * scale;
    if (modal_w > width - 40.0f * scale) modal_w = width - 40.0f * scale;
    ImGui::SetNextWindowPos(ImVec2(width * 0.5f, height * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(modal_w, 0.0f), ImGuiCond_Always);
    if (!ImGui::BeginPopupModal("Disclaimer", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove)) return false;

    ImGui::TextColored(CODE_COLOR_SECTION_VEC4, "PLEASE READ THIS NOTICE BEFORE YOU USE FASTRESAMPLER");
    ImGui::Spacing();
    float text_h = UI_DISCLAIMER_TEXT_HEIGHT * scale;
    if (text_h > height * 0.55f) text_h = height * 0.55f;
    ImGui::BeginChild("##disclaimer_text", ImVec2(0.0f, text_h), ImGuiChildFlags_Borders, ImGuiWindowFlags_None);
    for (size_t i = 0; i < sizeof(g_ui_disclaimer) / sizeof(g_ui_disclaimer[0]); ++i) {
        if (i > 0) ImGui::Spacing();
        ImGui::TextColored(CODE_COLOR_SECTION_VEC4, "%s", g_ui_disclaimer[i].title);
        ImGui::TextWrapped("%s", g_ui_disclaimer[i].body);
    }
    ImGui::EndChild();
    ImGui::Spacing();
    ImGui::TextWrapped("By clicking I understand & agree you confirm that you have read and understood this notice and "
                       "agree to be bound by it. If you do not agree, click I disagree. Exit, and do not use FastResampler.");
    ImGui::Spacing();
    ImGui::Checkbox("Don't show again", &g_ui_disclaimer_hide);

    int remaining = 0;
    if (g_ui_disclaimer_countdown) {
        double left = (double)FR_DISCLAIMER_SECONDS - (ImGui::GetTime() - g_ui_disclaimer_opened_at);
        remaining = left > 0.0 ? (int)ceil(left) : 0;
    }
    char label[48];
    if (remaining > 0) {
        snprintf(label, sizeof(label), "I understand & agree (%d)###disclaimer_agree", remaining);
    } else {
        fr_strcpy(label, sizeof(label), "I understand & agree###disclaimer_agree");
    }
    float button_w = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    ImGui::BeginDisabled(remaining > 0);
    if (ui_green_button(label, ImVec2(button_w, 0.0f))) {
        app_disclaimer_accept(app, g_ui_disclaimer_hide);
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ui_red_button("I disagree. Exit###disclaimer_exit", ImVec2(-1.0f, 0.0f))) app_disclaimer_reject(app);
    ImGui::EndPopup();
    return true;
}

static bool ui_draw_disk_warning(App* app, float width, float height, float scale) {
    if (app->disk_warning_open && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId)) {
        app->disk_warning_open = false;
        ImGui::OpenPopup("Disk space");
    }
    ImGui::SetNextWindowPos(ImVec2(width * 0.5f, height * 0.5f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(UI_MODAL_WIDTH * scale, 0.0f), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal("Disk space", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove)) return false;
    ui_colored_wrapped(app->disk_warning, CODE_COLOR_WARNING_VEC4);
    ImGui::Spacing();
    if (ImGui::Button("OK", ImVec2(-1.0f, 0.0f))) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
    return true;
}

void ui_draw_frame(App* app, float width, float height, float scale) {
    g_ui_show_tips = app->settings.show_tips;
    float side_w = UI_SIDEPANEL_WIDTH * scale;
    float kb_h = UI_KEYBOARD_HEIGHT * scale;
    float log_h = app_log_strip_height(app, height, scale);
    float main_w = width - side_w;
    if (main_w < 1.0f) main_w = 1.0f;
    float main_h = height - kb_h - log_h;
    if (main_h < 1.0f) main_h = 1.0f;

    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(width, height));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGuiWindowFlags root_flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                  ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                                  ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
    ImGui::Begin("##root", nullptr, root_flags);
    ImGui::PopStyleVar(3);

    if (app->started) {
        ImGui::SetCursorPos(ImVec2(0.0f, 0.0f));
        ui_draw_side_panel(app, ImVec2(side_w, height), scale);

        ImGui::SetCursorPos(ImVec2(side_w, 0.0f));
        ui_draw_main_area(app, ImVec2(main_w, main_h), scale);

        ImGui::SetCursorPos(ImVec2(side_w, main_h));
        ui_draw_log(app, ImVec2(main_w, log_h), scale);

        ImGui::SetCursorPos(ImVec2(side_w, main_h + log_h));
        ImGui::BeginChild("##keyboard", ImVec2(main_w, kb_h), ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar);
        float pad = UI_PANEL_PADDING * scale;
        ImVec2 kb_pos = ImGui::GetWindowPos();
        ImVec2 kb_size = ImGui::GetWindowSize();
        ui_draw_keyboard(app, ImVec2(kb_pos.x + pad, kb_pos.y + pad), ImVec2(kb_size.x - 2.0f * pad, kb_size.y - 2.0f * pad), scale);
        ImGui::EndChild();
    }

    bool modal = ui_draw_disclaimer(app, width, height, scale);
    if (ui_draw_disk_warning(app, width, height, scale)) modal = true;
    app->modal_open = modal;

    ImGui::End();
}
